# 40_epaper_mqtt

ESP32 + Waveshare 2.9-inch three-color e-paper V4 project.

## Features

- DHT11 temperature and humidity display
- Analog light sensor display
- 60-second e-paper refresh
- MQTT sensor publishing
- MQTT lamp control

## Hardware

- DHT11 DATA: GPIO32
- Light sensor AO: GPIO36
- Lamps: GPIO15, GPIO2, GPIO4
- E-paper SPI/control: SCK GPIO18, MOSI GPIO23, CS GPIO27, DC GPIO26, RST GPIO25, BUSY GPIO34

## MQTT

- Broker: `mqttgo.io`
- Port: `1883`
- Sensor topic: `jimmy/class305/data`
- Lamp control topic: `jimmy/class/ctrl`
- Lamp payload fields: `yled`, `bled`, `rled`

## Credentials

Credentials are kept in the local, ignored `secrets.h` file. To set up a new
checkout:

1. Copy `secrets.h.example` to `secrets.h`.
2. Fill in the Wi-Fi and MQTT values.
3. Open `40_epaper_mqtt.ino` in Arduino IDE.
