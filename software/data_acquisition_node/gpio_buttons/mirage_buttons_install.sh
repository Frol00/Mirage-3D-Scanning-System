#!/bin/bash
set -euo pipefail

APP_DIR="/home/frol/mirage_buttons"

sudo apt update
sudo apt install -y python3-gpiozero curl

mkdir -p "$APP_DIR"
cp mirage_buttons.py "$APP_DIR/mirage_buttons.py"
cp mirage_buttons_config.json "$APP_DIR/mirage_buttons_config.json"
chmod +x "$APP_DIR/mirage_buttons.py"

sudo usermod -aG gpio frol || true
sudo install -m 0644 mirage_buttons.service /etc/systemd/system/mirage-buttons.service
sudo systemctl daemon-reload
sudo systemctl enable --now mirage-buttons.service

echo "Mirage buttons service installed."
echo "Edit pins/config: $APP_DIR/mirage_buttons_config.json"
echo "Logs: journalctl -u mirage-buttons.service -f"
