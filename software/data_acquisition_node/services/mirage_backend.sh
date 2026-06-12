#!/bin/bash
set -euo pipefail

sudo apt update
sudo apt install -y network-manager curl xserver-xorg openbox xinit x11-xserver-utils

sudo systemctl enable --now NetworkManager
sudo loginctl enable-linger frol

mkdir -p /home/frol/mirage_web
cp web.html /home/frol/mirage_web/web.html
cp mirage_api.py /home/frol/mirage_web/mirage_api.py
chmod +x /home/frol/mirage_web/mirage_api.py

cp start_kiosk.fixed.sh /home/frol/start_kiosk.sh
chmod +x /home/frol/start_kiosk.sh

sudo install -m 0644 kiosk.service.fixed /etc/systemd/system/kiosk.service
sudo install -m 0440 mirage-sudoers /etc/sudoers.d/mirage-kiosk
sudo visudo -cf /etc/sudoers.d/mirage-kiosk

sudo systemctl daemon-reload
sudo systemctl enable kiosk.service
sudo systemctl restart kiosk.service

echo "Done. Check logs with: journalctl -u kiosk.service -f"
