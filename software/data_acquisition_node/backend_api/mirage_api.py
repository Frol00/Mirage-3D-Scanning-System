#!/usr/bin/env python3
import json
import os
import re
import subprocess
import mimetypes
from pathlib import Path
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import unquote


HOST = "127.0.0.1"
PORT = 8080
WEB_ROOT = Path("/home/frol/mirage_web")


# ═══════════════════════════════════════════════════
# HELPERS
# ═══════════════════════════════════════════════════

def run_cmd(cmd, timeout=20, sudo=False):
    """
    Run command and return:
    {
      ok: bool,
      code: int,
      out: str,
      err: str
    }
    """
    if sudo:
        cmd = ["sudo", "-n"] + cmd

    try:
        p = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout
        )

        return {
            "ok": p.returncode == 0,
            "code": p.returncode,
            "out": p.stdout.strip(),
            "err": p.stderr.strip()
        }

    except subprocess.TimeoutExpired:
        return {
            "ok": False,
            "code": -1,
            "out": "",
            "err": "Command timeout"
        }

    except Exception as e:
        return {
            "ok": False,
            "code": -1,
            "out": "",
            "err": str(e)
        }


def sh(command, timeout=20):
    return run_cmd(["bash", "-lc", command], timeout=timeout)


def sh_sudo(command, timeout=20):
    return run_cmd(["bash", "-lc", "sudo -n " + command], timeout=timeout)


def json_response(handler, data, code=200):
    body = json.dumps(data, ensure_ascii=False).encode("utf-8")

    handler.send_response(code)
    handler.send_header("Content-Type", "application/json; charset=utf-8")
    handler.send_header("Content-Length", str(len(body)))
    handler.send_header("Cache-Control", "no-store")
    handler.end_headers()
    handler.wfile.write(body)


def read_json_body(handler):
    length = int(handler.headers.get("Content-Length", "0") or "0")
    if length <= 0:
        return {}

    raw = handler.rfile.read(length)

    try:
        return json.loads(raw.decode("utf-8"))
    except Exception:
        return {}


def error_response(handler, message, code=500):
    json_response(handler, {"error": str(message)}, code)


# ═══════════════════════════════════════════════════
# NETWORK HELPERS
# ═══════════════════════════════════════════════════

def get_wifi_interface():
    """
    Returns Wi-Fi interface name, usually wlan0.
    """
    r = sh("nmcli -t -f DEVICE,TYPE device status 2>/dev/null | awk -F: '$2==\"wifi\" {print $1; exit}'")

    if r["ok"] and r["out"]:
        return r["out"].splitlines()[0].strip()

    # fallback
    if Path("/sys/class/net/wlan0").exists():
        return "wlan0"

    r = sh("ls /sys/class/net | grep -E '^wl|^wlan' | head -n1")
    if r["ok"] and r["out"]:
        return r["out"].splitlines()[0].strip()

    return "wlan0"


def get_ip_address():
    """
    Returns first IPv4 address from wlan0 / active default route.
    """
    wifi_if = get_wifi_interface()

    r = sh(f"ip -4 addr show {wifi_if} 2>/dev/null | awk '/inet / {{print $2}}' | cut -d/ -f1 | head -n1")
    if r["ok"] and r["out"]:
        return r["out"].strip()

    r = sh("hostname -I 2>/dev/null | awk '{print $1}'")
    if r["ok"] and r["out"]:
        return r["out"].strip()

    return ""


def get_active_wifi_ssid():
    """
    Returns active Wi-Fi SSID from nmcli if NetworkManager controls Wi-Fi.
    """
    wifi_if = get_wifi_interface()

    r = sh(f"nmcli -t -f GENERAL.CONNECTION device show {wifi_if} 2>/dev/null | cut -d: -f2-")
    if r["ok"] and r["out"]:
        ssid = r["out"].strip()
        if ssid and ssid not in ("--", "lo"):
            return ssid

    r = sh("iwgetid -r 2>/dev/null")
    if r["ok"] and r["out"]:
        return r["out"].strip()

    return ""


def get_device_state():
    wifi_if = get_wifi_interface()
    r = sh(f"nmcli -t -f DEVICE,STATE device status 2>/dev/null | awk -F: '$1==\"{wifi_if}\" {{print $2}}'")

    if r["ok"] and r["out"]:
        return r["out"].strip()

    return "unknown"


def is_hotspot_active():
    """
    Detect active NetworkManager hotspot.
    """
    r = sh("nmcli -t -f NAME,TYPE connection show --active 2>/dev/null | grep -Ei 'Hotspot|wifi' || true")

    if not r["ok"]:
        return False

    out = r["out"].lower()

    if "hotspot" in out:
        return True

    # fallback: check if IP 10.42.0.1 exists
    ip = get_ip_address()
    if ip.startswith("10.42."):
        return True

    return False


def wifi_state():
    if is_hotspot_active():
        return "hotspot"

    state = get_device_state().lower()

    if "connected" in state:
        ssid = get_active_wifi_ssid()
        if ssid:
            return "connected"

    ip = get_ip_address()
    if ip and not ip.startswith("10.42."):
        # fallback for cases where nmcli is imperfect
        ssid = get_active_wifi_ssid()
        if ssid:
            return "connected"

    if "unmanaged" in state:
        return "unmanaged"

    if "unavailable" in state:
        return "unavailable"

    return "disconnected"


# ═══════════════════════════════════════════════════
# REALSENSE HELPERS
# ═══════════════════════════════════════════════════

def camera_connected():
    """
    Detect Intel RealSense camera by USB.
    D415 usually appears as Intel Corp. RealSense.
    """
    r = sh("lsusb 2>/dev/null | grep -iE 'RealSense|Intel Corp.*RealSense|8086:'", timeout=3)
    return r["ok"] and bool(r["out"])


def rs_server_active():
    r = sh("pgrep -fa 'rs-server' 2>/dev/null | grep -v grep || true", timeout=3)
    return r["ok"] and bool(r["out"])


# ═══════════════════════════════════════════════════
# API FUNCTIONS
# ═══════════════════════════════════════════════════

def api_status():
    hotspot = is_hotspot_active()
    ssid = get_active_wifi_ssid()

    return {
        "wifi_state": wifi_state(),
        "connected_ssid": "" if hotspot else ssid,
        "ip_address": get_ip_address(),
        "hotspot_active": hotspot,
        "wifi_if": get_wifi_interface(),
        "camera_connected": camera_connected(),
        "rs_server_active": rs_server_active()
    }


def api_wifi_scan():
    wifi_if = get_wifi_interface()

    # turn Wi-Fi on
    sh_sudo("nmcli radio wifi on", timeout=8)

    # rescan
    sh_sudo(f"nmcli device wifi rescan ifname {wifi_if}", timeout=12)

    # get list
    r = sh(
        f"nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY device wifi list ifname {wifi_if} 2>/dev/null",
        timeout=12
    )

    networks = []
    seen = set()

    if r["ok"] and r["out"]:
        for line in r["out"].splitlines():
            parts = line.split(":")

            if len(parts) < 4:
                continue

            in_use = parts[0].strip() == "*"
            ssid = parts[1].strip()
            signal_raw = parts[2].strip()
            security = ":".join(parts[3:]).strip()

            if not ssid:
                continue

            key = ssid
            if key in seen:
                continue

            seen.add(key)

            try:
                signal = int(signal_raw)
            except Exception:
                signal = 0

            networks.append({
                "ssid": ssid,
                "signal": signal,
                "security": security,
                "in_use": in_use
            })

    networks.sort(key=lambda x: x.get("signal", 0), reverse=True)

    return {
        "networks": networks
    }


def api_wifi_connect(data):
    ssid = str(data.get("ssid", "")).strip()
    password = str(data.get("password", ""))

    if not ssid:
        raise Exception("SSID is required")

    wifi_if = get_wifi_interface()

    # Stop hotspot first, if active
    stop_hotspot_silent()

    # Delete old same connection to avoid stale password
    sh_sudo(f"nmcli connection delete id {quote_sh(ssid)} 2>/dev/null || true", timeout=10)

    if password:
        cmd = f"nmcli device wifi connect {quote_sh(ssid)} password {quote_sh(password)} ifname {quote_sh(wifi_if)}"
    else:
        cmd = f"nmcli device wifi connect {quote_sh(ssid)} ifname {quote_sh(wifi_if)}"

    r = sh_sudo(cmd, timeout=35)

    if not r["ok"]:
        raise Exception(r["err"] or r["out"] or "Failed to connect Wi-Fi")

    return {
        "ok": True,
        "message": f"Connected to {ssid}"
    }


def api_wifi_disconnect():
    wifi_if = get_wifi_interface()

    r = sh_sudo(f"nmcli device disconnect {quote_sh(wifi_if)}", timeout=15)

    if not r["ok"]:
        # not fatal if already disconnected
        err = (r["err"] or r["out"] or "").lower()
        if "not active" not in err and "not connected" not in err:
            raise Exception(r["err"] or r["out"] or "Failed to disconnect Wi-Fi")

    return {
        "ok": True,
        "message": "Wi-Fi disconnected"
    }


def stop_hotspot_silent():
    sh_sudo("nmcli connection down Hotspot 2>/dev/null || true", timeout=12)
    sh_sudo("nmcli connection down hotspot 2>/dev/null || true", timeout=12)
    sh_sudo("nmcli connection delete Hotspot 2>/dev/null || true", timeout=12)
    sh_sudo("nmcli connection delete hotspot 2>/dev/null || true", timeout=12)


def api_hotspot_start(data):
    ssid = str(data.get("ssid", "Mirage")).strip() or "Mirage"
    password = str(data.get("password", "")).strip()
    band = str(data.get("band", "2.4GHz")).strip()

    if len(password) < 8:
        raise Exception("Password must be at least 8 characters")

    wifi_if = get_wifi_interface()

    # Stop previous hotspot/profile
    stop_hotspot_silent()

    # Make sure Wi-Fi is on
    sh_sudo("nmcli radio wifi on", timeout=8)

    # NetworkManager bands:
    # bg = 2.4GHz
    # a  = 5GHz
    nm_band = "a" if "5" in band else "bg"

    cmd = (
        f"nmcli device wifi hotspot "
        f"ifname {quote_sh(wifi_if)} "
        f"con-name Hotspot "
        f"ssid {quote_sh(ssid)} "
        f"password {quote_sh(password)} "
        f"band {quote_sh(nm_band)}"
    )

    r = sh_sudo(cmd, timeout=35)

    if not r["ok"]:
        raise Exception(r["err"] or r["out"] or "Failed to start hotspot")

    return {
        "ok": True,
        "message": "Hotspot started",
        "ssid": ssid,
        "band": band,
        "gateway": "10.42.0.1"
    }


def api_hotspot_stop():
    stop_hotspot_silent()

    return {
        "ok": True,
        "message": "Hotspot stopped"
    }


def api_reboot():
    subprocess.Popen(["sudo", "-n", "systemctl", "reboot"])
    return {
        "ok": True,
        "message": "Rebooting"
    }


def api_shutdown():
    subprocess.Popen(["sudo", "-n", "systemctl", "poweroff"])
    return {
        "ok": True,
        "message": "Shutting down"
    }


def quote_sh(value):
    """
    Safe shell quote.
    """
    value = str(value)
    return "'" + value.replace("'", "'\"'\"'") + "'"


# ═══════════════════════════════════════════════════
# HTTP HANDLER
# ═══════════════════════════════════════════════════

class Handler(BaseHTTPRequestHandler):
    server_version = "MirageAPI/1.0"

    def log_message(self, fmt, *args):
        print("[%s] %s" % (self.address_string(), fmt % args))

    def do_GET(self):
        try:
            path = unquote(self.path.split("?", 1)[0])

            if path == "/api/status":
                return json_response(self, api_status())

            if path == "/api/wifi/scan":
                return json_response(self, api_wifi_scan())

            return self.serve_file(path)

        except Exception as e:
            return error_response(self, e, 500)

    def do_POST(self):
        try:
            path = unquote(self.path.split("?", 1)[0])
            data = read_json_body(self)

            if path == "/api/wifi/connect":
                return json_response(self, api_wifi_connect(data))

            if path == "/api/wifi/disconnect":
                return json_response(self, api_wifi_disconnect())

            if path == "/api/hotspot/start":
                return json_response(self, api_hotspot_start(data))

            if path == "/api/hotspot/stop":
                return json_response(self, api_hotspot_stop())

            if path == "/api/reboot":
                return json_response(self, api_reboot())

            if path == "/api/shutdown":
                return json_response(self, api_shutdown())

            return error_response(self, "Not found", 404)

        except Exception as e:
            return error_response(self, e, 500)

    def serve_file(self, path):
        if path == "/":
            path = "/web.html"

        # remove leading slash
        rel = path.lstrip("/")

        # prevent path traversal
        rel = os.path.normpath(rel)
        if rel.startswith(".."):
            return error_response(self, "Forbidden", 403)

        file_path = WEB_ROOT / rel

        if not file_path.exists() or not file_path.is_file():
            return error_response(self, "Not found", 404)

        content_type = mimetypes.guess_type(str(file_path))[0] or "application/octet-stream"

        data = file_path.read_bytes()

        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))

        if file_path.name.endswith(".html"):
            self.send_header("Cache-Control", "no-store")
        else:
            self.send_header("Cache-Control", "public, max-age=3600")

        self.end_headers()
        self.wfile.write(data)


# ═══════════════════════════════════════════════════
# MAIN
# ═══════════════════════════════════════════════════

if __name__ == "__main__":
    os.chdir(str(WEB_ROOT))

    httpd = ThreadingHTTPServer((HOST, PORT), Handler)

    print(f"Mirage API server: http://{HOST}:{PORT}/web.html")
    print(f"Web root: {WEB_ROOT}")

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("Stopping Mirage API server")
        httpd.server_close()