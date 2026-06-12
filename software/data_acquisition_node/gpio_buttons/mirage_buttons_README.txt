Mirage physical buttons bridge

These files are the Raspberry Pi side. The PC program in this MirageApp package
now includes the matching HTTP listener on port 5055.

Files:
- mirage_buttons.py
  Reads GPIO buttons and sends start/stop/reset commands to the active PC.

- mirage_buttons_config.json
  GPIO pins and network settings.

- mirage_buttons.service
  systemd service for Raspberry Pi.

- mirage_buttons_install.sh
  Installs the service to /home/frol/mirage_buttons.

Default GPIO wiring:
- START button: BCM GPIO 25 to GND
- STOP button:  BCM GPIO 8 to GND
- RESET button: BCM GPIO 7 to GND, hold for 1.5 sec

The config uses pull_up=true, so each physical button should connect the GPIO
pin to GND when pressed. Change pins in mirage_buttons_config.json if your board
uses different GPIOs.

How client registration works:
1. InfiniTAM on the PC listens on port 5055 for:

   GET /start
   GET /stop
   GET /reset

2. When the PC connects to the Raspberry Pi stream in Network mode, it registers
   itself automatically:

   POST http://RASPBERRY_IP:8091/register
   JSON body:
   {
     "port": 5055,
     "protocol": "http"
   }

3. Raspberry Pi stores the sender IP automatically.
4. Physical button presses are sent to:

   http://ACTIVE_PC_IP:5055/start
   http://ACTIVE_PC_IP:5055/stop
   http://ACTIVE_PC_IP:5055/reset

Useful test commands from another machine:

Register this machine as active client:
  curl -X POST http://RASPBERRY_IP:8091/register -H "Content-Type: application/json" -d "{\"port\":5055,\"protocol\":\"http\"}"

Check status:
  curl http://RASPBERRY_IP:8091/status

Simulate a physical button press from the Pi:
  curl http://127.0.0.1:8091/send/start
  curl http://127.0.0.1:8091/send/stop
  curl http://127.0.0.1:8091/send/reset

Install on Raspberry Pi:
  chmod +x mirage_buttons_install.sh
  ./mirage_buttons_install.sh

Watch logs:
  journalctl -u mirage-buttons.service -f

Important:
This does not modify the RealSense network stream. The stream stays separate.
Buttons use a small HTTP control channel beside it.
