#!/bin/bash
set -u
export DISPLAY=:0
export XDG_RUNTIME_DIR=/run/user/1000

# Убиваем старые зависшие процессы Chromium и старый API-сервер, если они есть
killall -9 chromium-browser chromium 2>/dev/null || true
pkill -f '/home/frol/mirage_web/mirage_api.py' 2>/dev/null || true

# Отключаем гашение экрана
xset s noblank
xset s off
xset -dpms

# Красим фон X-сервера в цвет интерфейса
xsetroot -solid "#07090f"

# Оконный менеджер
openbox &

# Backend + static server вместо python3 -m http.server
cd /home/frol/mirage_web
/usr/bin/python3 /home/frol/mirage_web/mirage_api.py &
API_PID=$!

# Ждем, пока API начнет отвечать, без фиксированного лишнего ожидания
for i in $(seq 1 30); do
  if curl -fsS http://127.0.0.1:8080/api/status >/dev/null 2>&1; then
    break
  fi
  sleep 0.2
done

# Бесконечный цикл для Chromium
while true; do
    chromium --kiosk --incognito --window-position=0,0 --window-size=800,480 \
             --disable-infobars --no-first-run --touch-events=enabled \
             --overscroll-history-navigation=0 --pull-to-refresh=0 \
             --disable-pinch \
             --disable-features=Translate \
             --disable-background-networking \
             --disable-sync \
             --disk-cache-dir=/tmp \
             --enable-gpu-rasterization \
             --enable-zero-copy \
             --app="http://127.0.0.1:8080/web.html"
    sleep 2
done
