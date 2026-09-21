# ESP32 Sensors MQTT E-Paper Jimmy

ESP32 environmental monitoring project for a Waveshare 2.9-inch V4
three-color e-paper display. The project reads temperature, humidity, and
ambient light, displays the values on the e-paper, publishes them through
MQTT, and controls three indicator lamps through MQTT.

## Features

- DHT11 temperature and humidity measurement
- Analog light sensor measurement
- Chinese e-paper dashboard with red icons and sensor values
- Automatic display and MQTT update every 60 seconds
- MQTT retained sensor JSON
- Individual temperature, humidity, and light MQTT values
- Three-lamp MQTT control
- Wi-Fi reconnect and MQTT reconnect handling
- Credential separation through an ignored local `secrets.h`

## Hardware

### Sensors and lamps

| Device | ESP32 GPIO |
|---|---:|
| DHT11 DATA | 32 |
| Light sensor AO | 36 |
| Green lamp output | 15 |
| Yellow lamp output | 2 |
| Red lamp output | 4 |

### Waveshare 2.9-inch e-paper V4

| E-paper signal | ESP32 GPIO |
|---|---:|
| SCK | 18 |
| MOSI | 23 |
| CS | 27 |
| DC | 26 |
| RST | 25 |
| BUSY | 34 |

Display resolution is 296 × 128 pixels.

## MQTT settings

| Setting | Value |
|---|---|
| Broker | `mqttgo.io` |
| Port | `1883` |
| Sensor JSON topic | `jimmy/class305/data` |
| Lamp control topic | `jimmy/class/ctrl` |

### Sensor JSON payload

Example:

```json
{
  "temp": 25.2,
  "humi": 42.0,
  "light": 18,
  "temperature": 25.2,
  "humidity": 42.0
}
```

The short field names (`temp`, `humi`, `light`) are retained for
compatibility, while the descriptive field names are also included.

The firmware also publishes individual retained values to:

```text
home/40_epaper/temperature
home/40_epaper/humidity
home/40_epaper/light
```

### Lamp control payload

Publish JSON to `jimmy/class/ctrl`:

```json
{"yled":"on","bled":"off","rled":"on"}
```

Numeric values are also supported:

```json
{"yled":1,"bled":0,"rled":1}
```

Lamp mapping:

- `bled` → GPIO15
- `yled` → GPIO2
- `rled` → GPIO4

The payloads `ON` and `OFF` can be used to control all lamps together.

## Arduino setup

1. Install Arduino IDE and the ESP32 board package.
2. Install the `PubSubClient` library.
3. Open `40_epaper_mqtt.ino`.
4. Copy `secrets.h.example` to `secrets.h`.
5. Fill in the Wi-Fi and MQTT credentials in `secrets.h`.
6. Select an ESP32 board and the correct serial port.
7. Compile and upload the sketch.

The local `secrets.h` file is excluded by `.gitignore` and must never be
committed to a public repository.

## Project files

- `40_epaper_mqtt.ino` - main ESP32 application
- `epd2in9b_V4.*` - Waveshare e-paper driver
- `epdif.*` - e-paper hardware interface
- `cjk_font.h` - Chinese font glyph data
- `secrets.h.example` - safe credential template
- `README.md` - project documentation
