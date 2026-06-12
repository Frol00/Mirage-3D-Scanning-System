#!/usr/bin/env python3
import argparse
import json
import os
import signal
import socket
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from pathlib import Path

try:
    from gpiozero import Button
    GPIO_AVAILABLE = True
except Exception as exc:
    Button = None
    GPIO_AVAILABLE = False
    GPIO_IMPORT_ERROR = exc


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_CONFIG = SCRIPT_DIR / "mirage_buttons_config.json"
COMMANDS = {"start", "stop", "reset"}


def log(message):
    print(time.strftime("[%Y-%m-%d %H:%M:%S]"), message, flush=True)


def load_json(path, fallback):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except FileNotFoundError:
        return fallback


def write_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")


class ClientState:
    def __init__(self, state_file, default_port, default_protocol):
        self.state_file = Path(state_file)
        self.default_port = int(default_port)
        self.default_protocol = str(default_protocol)
        self.lock = threading.Lock()
        self.client = self._load()

    def _load(self):
        data = load_json(self.state_file, {})
        if not isinstance(data, dict):
            return {}
        return data

    def get(self):
        with self.lock:
            return dict(self.client)

    def clear(self):
        with self.lock:
            self.client = {}
            write_json(self.state_file, self.client)

    def register(self, ip, port=None, protocol=None):
        if not port:
            port = self.default_port
        if not protocol:
            protocol = self.default_protocol

        client = {
            "ip": str(ip),
            "port": int(port),
            "protocol": str(protocol).lower(),
            "registered_at": time.time()
        }

        with self.lock:
            self.client = client
            write_json(self.state_file, self.client)

        return client


class MirageButtons:
    def __init__(self, config):
        self.config = config
        client_cfg = config.get("client", {})
        self.client_state = ClientState(
            config.get("state_file", "/home/frol/mirage_buttons/client_state.json"),
            client_cfg.get("default_port", 5055),
            client_cfg.get("protocol", "http")
        )
        self.httpd = None
        self.buttons = []
        self.stopping = threading.Event()

    def check_token(self, handler):
        expected = str(self.config.get("token", "") or "")
        if not expected:
            return True

        header = handler.headers.get("X-Mirage-Token", "")
        query = urllib.parse.parse_qs(urllib.parse.urlparse(handler.path).query)
        query_token = query.get("token", [""])[0]
        return header == expected or query_token == expected

    def send_command(self, command):
        command = str(command).strip().lower()
        if command not in COMMANDS:
            raise ValueError("Unknown command: " + command)

        client = self.client_state.get()
        if not client.get("ip"):
            log("Button command ignored, no active PC client: " + command)
            return {"ok": False, "error": "No active client", "command": command}

        protocol = client.get("protocol", "http")
        if protocol == "udp":
            return self._send_udp(client, command)
        return self._send_http(client, command)

    def _send_http(self, client, command):
        cfg = self.config.get("client", {})
        template = cfg.get("http_path_template", "/{command}")
        method = str(cfg.get("http_method", "GET")).upper()
        timeout = float(cfg.get("timeout_sec", 1.0))
        path = template.format(command=urllib.parse.quote(command))
        if not path.startswith("/"):
            path = "/" + path

        url = "http://{}:{}{}".format(client["ip"], int(client["port"]), path)
        data = None
        headers = {}

        token = str(self.config.get("token", "") or "")
        if token:
            headers["X-Mirage-Token"] = token

        if method == "POST":
            data = json.dumps({"command": command}).encode("utf-8")
            headers["Content-Type"] = "application/json"

        req = urllib.request.Request(url, data=data, headers=headers, method=method)

        try:
            with urllib.request.urlopen(req, timeout=timeout) as response:
                body = response.read(256).decode("utf-8", errors="replace")
            log("Sent {} to {} by HTTP {}".format(command, client["ip"], response.status))
            return {"ok": True, "command": command, "url": url, "status": response.status, "body": body}
        except urllib.error.URLError as exc:
            log("Failed to send {} to {}: {}".format(command, client["ip"], exc))
            return {"ok": False, "command": command, "url": url, "error": str(exc)}

    def _send_udp(self, client, command):
        cfg = self.config.get("client", {})
        timeout = float(cfg.get("timeout_sec", 1.0))
        encoding = cfg.get("udp_encoding", "utf-8")
        payload = command.encode(encoding)

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(timeout)
            sock.sendto(payload, (client["ip"], int(client["port"])))

        log("Sent {} to {} by UDP".format(command, client["ip"]))
        return {"ok": True, "command": command}

    def start_http_server(self):
        app = self

        class Handler(BaseHTTPRequestHandler):
            server_version = "MirageButtons/1.0"

            def log_message(self, fmt, *args):
                log("{} {}".format(self.client_address[0], fmt % args))

            def _json(self, data, code=200):
                body = json.dumps(data, ensure_ascii=False).encode("utf-8")
                self.send_response(code)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(body)

            def _body_json(self):
                length = int(self.headers.get("Content-Length", "0") or "0")
                if length <= 0:
                    return {}
                raw = self.rfile.read(length)
                try:
                    return json.loads(raw.decode("utf-8"))
                except Exception:
                    return {}

            def _require_token(self):
                if app.check_token(self):
                    return True
                self._json({"ok": False, "error": "Forbidden"}, 403)
                return False

            def do_GET(self):
                if not self._require_token():
                    return

                path = urllib.parse.urlparse(self.path).path
                if path == "/status":
                    return self._json({
                        "ok": True,
                        "client": app.client_state.get(),
                        "gpio_available": GPIO_AVAILABLE
                    })

                if path == "/clear":
                    app.client_state.clear()
                    return self._json({"ok": True})

                if path.startswith("/send/"):
                    command = path.rsplit("/", 1)[-1]
                    return self._json(app.send_command(command))

                return self._json({"ok": False, "error": "Not found"}, 404)

            def do_POST(self):
                if not self._require_token():
                    return

                path = urllib.parse.urlparse(self.path).path
                data = self._body_json()

                if path == "/register":
                    client = app.client_state.register(
                        self.client_address[0],
                        data.get("port"),
                        data.get("protocol")
                    )
                    log("Registered active PC client: {}:{} {}".format(
                        client["ip"], client["port"], client["protocol"]))
                    return self._json({"ok": True, "client": client})

                if path == "/clear":
                    app.client_state.clear()
                    return self._json({"ok": True})

                if path.startswith("/send/"):
                    command = path.rsplit("/", 1)[-1]
                    return self._json(app.send_command(command))

                return self._json({"ok": False, "error": "Not found"}, 404)

        host = self.config.get("listen_host", "0.0.0.0")
        port = int(self.config.get("listen_port", 8091))
        self.httpd = ThreadingHTTPServer((host, port), Handler)

        thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        thread.start()
        log("Mirage buttons server listening on {}:{}".format(host, port))

    def setup_buttons(self):
        if not GPIO_AVAILABLE:
            log("GPIO unavailable: {}".format(GPIO_IMPORT_ERROR))
            log("HTTP registration/test server will still run.")
            return

        button_cfg = self.config.get("buttons", {})

        self._add_press_button("start", button_cfg.get("start", {}))
        self._add_press_button("stop", button_cfg.get("stop", {}))
        self._add_reset_button(button_cfg.get("reset", {}))

    def _add_press_button(self, command, cfg):
        pin = cfg.get("pin")
        if pin is None:
            log("Button disabled, no pin configured: " + command)
            return

        btn = Button(
            int(pin),
            pull_up=bool(cfg.get("pull_up", True)),
            bounce_time=float(cfg.get("bounce_time", 0.08))
        )
        btn.when_pressed = lambda cmd=command: self.send_command(cmd)
        self.buttons.append(btn)
        log("GPIO button {} on BCM pin {}".format(command, pin))

    def _add_reset_button(self, cfg):
        pin = cfg.get("pin")
        if pin is None:
            log("Reset button disabled, no pin configured.")
            return

        btn = Button(
            int(pin),
            pull_up=bool(cfg.get("pull_up", True)),
            bounce_time=float(cfg.get("bounce_time", 0.08)),
            hold_time=float(cfg.get("hold_time", 1.5)),
            hold_repeat=False
        )
        btn.when_held = lambda: self.send_command("reset")
        self.buttons.append(btn)
        log("GPIO button reset on BCM pin {} hold {} sec".format(pin, btn.hold_time))

    def run(self):
        self.start_http_server()
        self.setup_buttons()

        while not self.stopping.is_set():
            time.sleep(0.25)

    def stop(self):
        self.stopping.set()
        if self.httpd:
            self.httpd.shutdown()


def main():
    parser = argparse.ArgumentParser(description="Mirage GPIO buttons to PC control bridge")
    parser.add_argument("--config", default=str(DEFAULT_CONFIG))
    args = parser.parse_args()

    config = load_json(args.config, {})
    if not config:
        print("Config not found or empty: {}".format(args.config), file=sys.stderr)
        return 2

    app = MirageButtons(config)

    def handle_signal(signum, frame):
        log("Stopping")
        app.stop()

    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)

    app.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
