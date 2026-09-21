#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <math.h>
#include "epd2in9b_V4.h"
#include "cjk_font.h"
#include "secrets.h"

constexpr uint16_t SCREEN_WIDTH = 296;
constexpr uint16_t SCREEN_HEIGHT = 128;
constexpr uint16_t BUFFER_SIZE = (EPD_WIDTH / 8) * EPD_HEIGHT;

// ===== 使用者設定區：請在此填入 Wi-Fi / MQTT 資訊 =====
const char *WIFI_SSID = WIFI_SSID_VALUE;
const char *WIFI_PASSWORD = WIFI_PASSWORD_VALUE;

const char *MQTT_BROKER = MQTT_BROKER_VALUE;
constexpr uint16_t MQTT_PORT = MQTT_PORT_VALUE;
const char *MQTT_USER = MQTT_USER_VALUE;
const char *MQTT_PASSWORD = MQTT_PASSWORD_VALUE;
const char *MQTT_CLIENT_ID = MQTT_CLIENT_ID_VALUE;

// MQTT topic
const char *TOPIC_TEMPERATURE = "home/40_epaper/temperature";
const char *TOPIC_HUMIDITY = "home/40_epaper/humidity";
const char *TOPIC_LIGHT = "home/40_epaper/light";
const char *TOPIC_SENSOR_JSON = "jimmy/class305/data";
const char *TOPIC_LAMP_SET = "jimmy/class/ctrl";
const char *TOPIC_LAMP_STATE = "home/40_epaper/lamp/state";
const char *TOPIC_LAMP_GREEN_SET = "jimmy/class/ctrl/green";
const char *TOPIC_LAMP_YELLOW_SET = "jimmy/class/ctrl/yellow";
const char *TOPIC_LAMP_RED_SET = "jimmy/class/ctrl/red";

// 感測器與燈號接腳
constexpr uint8_t DHT11_PIN = 32;
constexpr uint8_t LIGHT_SENSOR_PIN = 36;
constexpr uint8_t GREEN_LAMP_PIN = 15;
constexpr uint8_t YELLOW_LAMP_PIN = 2;
constexpr uint8_t RED_LAMP_PIN = 4;
constexpr unsigned long UPDATE_INTERVAL_MS = 60000UL;

Epd epd;
uint8_t blackImage[BUFFER_SIZE];
uint8_t redImage[BUFFER_SIZE];
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
bool blueLampState = false;
bool yellowLampState = false;
bool redLampState = false;
bool sensorReadOk = false;
float currentTemperature = NAN;
float currentHumidity = NAN;
uint8_t currentLight = 0;
unsigned long lastUpdate = 0;

void setRawPixel(uint8_t *buffer, int16_t x, int16_t y, bool on) {
  if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) return;
  const size_t index = static_cast<size_t>(y) * (EPD_WIDTH / 8) + (x / 8);
  const uint8_t mask = static_cast<uint8_t>(0x80 >> (x % 8));
  if (on) buffer[index] &= static_cast<uint8_t>(~mask);
  else buffer[index] |= mask;
}

void setLogicalPixel(uint8_t *buffer, int16_t x, int16_t y, bool on) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
  setRawPixel(buffer, y, SCREEN_WIDTH - 1 - x, on);
}

void drawPixel(int16_t x, int16_t y, bool red = false) {
  setLogicalPixel(red ? redImage : blackImage, x, y, true);
}

void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool red = false) {
  const int16_t dx = abs(x1 - x0);
  const int16_t sx = x0 < x1 ? 1 : -1;
  const int16_t dy = -abs(y1 - y0);
  const int16_t sy = y0 < y1 ? 1 : -1;
  int16_t error = dx + dy;
  while (true) {
    drawPixel(x0, y0, red);
    if (x0 == x1 && y0 == y1) return;
    const int16_t e2 = 2 * error;
    if (e2 >= dy) { error += dy; x0 += sx; }
    if (e2 <= dx) { error += dx; y0 += sy; }
  }
}

void drawCircle(int16_t cx, int16_t cy, int16_t radius, bool red = false) {
  int16_t x = -radius;
  int16_t y = 0;
  int16_t error = 2 - 2 * radius;
  do {
    drawPixel(cx - x, cy + y, red);
    drawPixel(cx - y, cy - x, red);
    drawPixel(cx + x, cy - y, red);
    drawPixel(cx + y, cy + x, red);
    const int16_t oldError = error;
    if (oldError <= y) error += ++y * 2 + 1;
    if (oldError > x || error > y) error += ++x * 2 + 1;
  } while (x < 0);
}

void fillCircle(int16_t cx, int16_t cy, int16_t radius, bool red = false) {
  for (int16_t y = -radius; y <= radius; ++y) {
    const int16_t width = static_cast<int16_t>(sqrt(radius * radius - y * y));
    drawLine(cx - width, cy + y, cx + width, cy + y, red);
  }
}

void glyph(char c, uint8_t out[5]) {
  static const uint8_t blank[5] = {0, 0, 0, 0, 0};
  const uint8_t *g = blank;
  static const uint8_t digits[][5] = {
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}
  };
  static const uint8_t A[5]={0x7E,0x11,0x11,0x11,0x7E};
  static const uint8_t C[5]={0x3E,0x41,0x41,0x41,0x22};
  static const uint8_t E[5]={0x7F,0x49,0x49,0x49,0x41};
  static const uint8_t G[5]={0x3E,0x41,0x49,0x49,0x7A};
  static const uint8_t H[5]={0x7F,0x08,0x08,0x08,0x7F};
  static const uint8_t I[5]={0x00,0x41,0x7F,0x41,0x00};
  static const uint8_t L[5]={0x7F,0x40,0x40,0x40,0x40};
  static const uint8_t M[5]={0x7F,0x02,0x0C,0x02,0x7F};
  static const uint8_t N[5]={0x7F,0x04,0x08,0x10,0x7F};
  static const uint8_t O[5]={0x3E,0x41,0x41,0x41,0x3E};
  static const uint8_t P[5]={0x7F,0x09,0x09,0x09,0x06};
  static const uint8_t R[5]={0x7F,0x09,0x19,0x29,0x46};
  static const uint8_t T[5]={0x01,0x01,0x7F,0x01,0x01};
  static const uint8_t U[5]={0x3F,0x40,0x40,0x40,0x3F};
  static const uint8_t V[5]={0x1F,0x20,0x40,0x20,0x1F};
  static const uint8_t X[5]={0x63,0x14,0x08,0x14,0x63};
  static const uint8_t percent[5]={0x63,0x13,0x08,0x64,0x63};
  static const uint8_t dot[5]={0x00,0x60,0x60,0x00,0x00};
  static const uint8_t minus[5]={0x08,0x08,0x08,0x08,0x08};
  static const uint8_t lowerL[5]={0x00,0x41,0x7F,0x40,0x00};
  static const uint8_t lowerU[5]={0x3C,0x40,0x40,0x20,0x7C};
  static const uint8_t lowerX[5]={0x44,0x28,0x10,0x28,0x44};
  static const uint8_t lowerS[5]={0x48,0x54,0x54,0x54,0x24};
  if (c >= '0' && c <= '9') g = digits[c - '0'];
  else switch (c) {
    case 'A': g=A; break; case 'C': g=C; break; case 'E': g=E; break;
    case 'G': g=G; break; case 'H': g=H; break; case 'I': g=I; break;
    case 'L': g=L; break; case 'M': g=M; break; case 'N': g=N; break;
    case 'O': g=O; break; case 'P': g=P; break; case 'R': g=R; break;
    case 'T': g=T; break; case 'U': g=U; break; case 'V': g=V; break;
    case 'X': g=X; break; case '%': g=percent; break; case '.': g=dot; break;
    case '-': g=minus; break; case 'l': g=lowerL; break; case 'u': g=lowerU; break;
    case 'x': g=lowerX; break; case 's': g=lowerS; break;
  }
  memcpy(out, g, 5);
}

uint16_t textWidth(const char *text, uint8_t scale, uint8_t gap = 1) {
  const size_t length = strlen(text);
  return length == 0 ? 0 : length * (5 + gap) * scale - gap * scale;
}

void drawText(const char *text, int16_t x, int16_t y, uint8_t scale, bool red = false) {
  for (uint16_t i = 0; text[i] != '\0'; ++i) {
    uint8_t columns[5];
    glyph(text[i], columns);
    const int16_t gx = x + i * 6 * scale;
    for (uint8_t col = 0; col < 5; ++col)
      for (uint8_t row = 0; row < 7; ++row)
        if ((columns[col] >> row) & 1)
          for (uint8_t dx = 0; dx < scale; ++dx)
            for (uint8_t dy = 0; dy < scale; ++dy)
              setLogicalPixel(red ? redImage : blackImage,
                              gx + col * scale + dx, y + row * scale + dy, true);
  }
}

void drawCentered(const char *text, int16_t centerX, int16_t y, uint8_t scale, bool red = false) {
  drawText(text, centerX - textWidth(text, scale) / 2, y, scale, red);
}

void drawCjkGlyph(CjkGlyph glyphId, int16_t x, int16_t y, bool red = false) {
  for (uint8_t row = 0; row < 16; ++row) {
    const uint16_t bits =
        (static_cast<uint16_t>(pgm_read_byte(&CJK_GLYPHS[glyphId][row * 2])) << 8) |
        pgm_read_byte(&CJK_GLYPHS[glyphId][row * 2 + 1]);
    for (uint8_t col = 0; col < 16; ++col)
      if (bits & (1U << (15 - col))) drawPixel(x + col, y + row, red);
  }
}

void drawCjkText(const CjkGlyph *text, uint8_t count, int16_t x, int16_t y, bool red = false) {
  for (uint8_t i = 0; i < count; ++i) drawCjkGlyph(text[i], x + i * 16, y, red);
}

void drawCjkCentered(const CjkGlyph *text, uint8_t count, int16_t centerX, int16_t y, bool red = false) {
  drawCjkText(text, count, centerX - count * 8, y, red);
}

void drawThermometer(int16_t cx, int16_t top, bool red = true) {
  fillCircle(cx, top + 27, 7, red);
  drawCircle(cx, top + 27, 9, red);
  drawLine(cx, top + 4, cx, top + 27, red);
  drawLine(cx - 4, top + 4, cx + 4, top + 4, red);
  drawLine(cx - 4, top + 4, cx - 4, top + 20, red);
  drawLine(cx + 4, top + 4, cx + 4, top + 20, red);
  drawLine(cx + 8, top + 10, cx + 12, top + 10, red);
  drawLine(cx + 8, top + 17, cx + 12, top + 17, red);
  drawLine(cx + 8, top + 24, cx + 12, top + 24, red);
}

void drawDrop(int16_t cx, int16_t top, bool red = true) {
  drawLine(cx, top, cx - 13, top + 18, red);
  drawLine(cx, top, cx + 13, top + 18, red);
  fillCircle(cx, top + 18, 12, red);
  drawCircle(cx, top + 18, 13, red);
}

void drawSun(int16_t cx, int16_t cy, bool red = true) {
  fillCircle(cx, cy, 8, red);
  drawCircle(cx, cy, 10, red);
  for (uint8_t i = 0; i < 8; ++i) {
    const float angle = i * 0.785398f;
    drawLine(cx + static_cast<int16_t>(cos(angle) * 14),
             cy + static_cast<int16_t>(sin(angle) * 14),
             cx + static_cast<int16_t>(cos(angle) * 20),
             cy + static_cast<int16_t>(sin(angle) * 20), red);
  }
}

bool readDht11(float &temperature, float &humidity) {
  uint8_t data[5] = {0, 0, 0, 0, 0};

  pinMode(DHT11_PIN, OUTPUT);
  digitalWrite(DHT11_PIN, LOW);
  delay(20);
  digitalWrite(DHT11_PIN, HIGH);
  delayMicroseconds(40);
  pinMode(DHT11_PIN, INPUT_PULLUP);

  uint32_t start = micros();
  while (digitalRead(DHT11_PIN) == HIGH) {
    if (micros() - start > 100) return false;
  }
  start = micros();
  while (digitalRead(DHT11_PIN) == LOW) {
    if (micros() - start > 100) return false;
  }
  start = micros();
  while (digitalRead(DHT11_PIN) == HIGH) {
    if (micros() - start > 100) return false;
  }

  noInterrupts();
  for (uint8_t bit = 0; bit < 40; ++bit) {
    uint32_t waitStart = micros();
    while (digitalRead(DHT11_PIN) == LOW) {
      if (micros() - waitStart > 100) {
        interrupts();
        return false;
      }
    }
    const uint32_t highStart = micros();
    while (digitalRead(DHT11_PIN) == HIGH) {
      if (micros() - highStart > 120) {
        interrupts();
        return false;
      }
    }
    if (micros() - highStart > 40) data[bit / 8] |= (1 << (7 - (bit % 8)));
  }
  interrupts();

  if (static_cast<uint8_t>(data[0] + data[1] + data[2] + data[3]) != data[4]) return false;
  humidity = data[0] + data[1] / 10.0f;
  temperature = data[2] + data[3] / 10.0f;
  return true;
}

void readSensors() {
  float temperature = NAN;
  float humidity = NAN;
  sensorReadOk = readDht11(temperature, humidity);
  if (sensorReadOk) {
    currentTemperature = temperature;
    currentHumidity = humidity;
  }

  const int rawLight = analogRead(LIGHT_SENSOR_PIN);
  currentLight = static_cast<uint8_t>(constrain(map(rawLight, 0, 4095, 0, 100), 0, 100));
  Serial.printf("DHT11: %s, temperature=%.1f C, humidity=%.1f %%RH, light=%u %% (ADC=%d)\n",
                sensorReadOk ? "OK" : "ERROR", currentTemperature, currentHumidity,
                currentLight, rawLight);
}

void applyLampStates(bool publishState = true) {
  digitalWrite(GREEN_LAMP_PIN, blueLampState ? HIGH : LOW);
  digitalWrite(YELLOW_LAMP_PIN, yellowLampState ? HIGH : LOW);
  digitalWrite(RED_LAMP_PIN, redLampState ? HIGH : LOW);
  if (publishState && mqttClient.connected()) {
    char state[64];
    snprintf(state, sizeof(state), "{\"blue\":%d,\"yellow\":%d,\"red\":%d}",
             blueLampState, yellowLampState, redLampState);
    mqttClient.publish(TOPIC_LAMP_STATE, state, true);
  }
}

bool payloadIsOn(const String &value) {
  String normalized = value;
  normalized.trim();
  normalized.toUpperCase();
  return normalized == "ON" || normalized == "1" || normalized == "TRUE";
}

bool jsonKeyIsOn(const String &json, const char *key, bool &value) {
  const String quotedKey = String("\"") + key + "\"";
  const int keyIndex = json.indexOf(quotedKey);
  if (keyIndex < 0) return false;
  const int colonIndex = json.indexOf(':', keyIndex + quotedKey.length());
  if (colonIndex < 0) return false;
  String valueText = json.substring(colonIndex + 1);
  valueText.trim();
  const int commaIndex = valueText.indexOf(',');
  if (commaIndex >= 0) valueText = valueText.substring(0, commaIndex);
  const int braceIndex = valueText.indexOf('}');
  if (braceIndex >= 0) valueText = valueText.substring(0, braceIndex);
  valueText.trim();
  valueText.replace("\"", "");
  value = payloadIsOn(valueText);
  return true;
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String rawCommand;
  for (unsigned int i = 0; i < length; ++i) rawCommand += static_cast<char>(payload[i]);
  rawCommand.trim();
  String command = rawCommand;
  command.toUpperCase();

  if (strcmp(topic, TOPIC_LAMP_GREEN_SET) == 0) {
    blueLampState = payloadIsOn(command);
  } else if (strcmp(topic, TOPIC_LAMP_YELLOW_SET) == 0) {
    yellowLampState = payloadIsOn(command);
  } else if (strcmp(topic, TOPIC_LAMP_RED_SET) == 0) {
    redLampState = payloadIsOn(command);
  } else if (strcmp(topic, TOPIC_LAMP_SET) == 0) {
    bool anyChanged = false;
    bool state = false;
    bool changed = jsonKeyIsOn(rawCommand, "bled", state);
    if (!changed) changed = jsonKeyIsOn(rawCommand, "gled", state);
    if (changed) {
      blueLampState = state;
      anyChanged = true;
    }
    changed = jsonKeyIsOn(rawCommand, "yled", state);
    if (changed) yellowLampState = state;
    anyChanged |= changed;
    changed = jsonKeyIsOn(rawCommand, "rled", state);
    if (changed) redLampState = state;
    anyChanged |= changed;
    if (!anyChanged) {
      const bool on = payloadIsOn(command);
      blueLampState = on;
      yellowLampState = on;
      redLampState = on;
    }
  } else {
    return;
  }

  applyLampStates();
  Serial.printf("MQTT lamp state: B=%d Y=%d R=%d\n",
                blueLampState, yellowLampState, redLampState);
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting Wi-Fi: %s\n", WIFI_SSID);
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000UL) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected, IP=");
    Serial.println(WiFi.localIP());
    return true;
  }
  Serial.println("Wi-Fi connection failed.");
  return false;
}

bool connectMqtt() {
  if (mqttClient.connected()) return true;
  if (!connectWiFi()) return false;

  Serial.printf("Connecting MQTT: %s:%u\n", MQTT_BROKER, MQTT_PORT);
  bool connected;
  if (strlen(MQTT_USER) > 0) {
    connected = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD);
  } else {
    connected = mqttClient.connect(MQTT_CLIENT_ID);
  }
  if (!connected) {
    Serial.printf("MQTT connection failed, state=%d\n", mqttClient.state());
    return false;
  }

  mqttClient.subscribe(TOPIC_LAMP_SET);
  mqttClient.subscribe(TOPIC_LAMP_GREEN_SET);
  mqttClient.subscribe(TOPIC_LAMP_YELLOW_SET);
  mqttClient.subscribe(TOPIC_LAMP_RED_SET);
  applyLampStates();
  Serial.println("MQTT connected and lamp topic subscribed.");
  return true;
}

void publishSensorData() {
  if (!connectMqtt()) return;

  char value[16];
  bool published = true;
  if (sensorReadOk) {
    snprintf(value, sizeof(value), "%.1f", currentTemperature);
    const bool tempPublished = mqttClient.publish(TOPIC_TEMPERATURE, value, true);
    published = published && tempPublished;
    Serial.printf("MQTT %s [%s]: %s\n",
                  tempPublished ? "published" : "publish FAILED",
                  TOPIC_TEMPERATURE, value);

    snprintf(value, sizeof(value), "%.1f", currentHumidity);
    const bool humidityPublished = mqttClient.publish(TOPIC_HUMIDITY, value, true);
    published = published && humidityPublished;
    Serial.printf("MQTT %s [%s]: %s\n",
                  humidityPublished ? "published" : "publish FAILED",
                  TOPIC_HUMIDITY, value);
  }
  snprintf(value, sizeof(value), "%u", currentLight);
  const bool lightPublished = mqttClient.publish(TOPIC_LIGHT, value, true);
  published = published && lightPublished;
  Serial.printf("MQTT %s [%s]: %s\n",
                lightPublished ? "published" : "publish FAILED",
                TOPIC_LIGHT, value);

  char json[96];
  if (sensorReadOk) {
    snprintf(json, sizeof(json),
             "{\"temp\":%.1f,\"humi\":%.1f,\"light\":%u,\"temperature\":%.1f,\"humidity\":%.1f}",
             currentTemperature, currentHumidity, currentLight,
             currentTemperature, currentHumidity);
  } else {
    snprintf(json, sizeof(json),
             "{\"temp\":null,\"humi\":null,\"light\":%u,\"temperature\":null,\"humidity\":null}",
             currentLight);
  }
  const bool jsonPublished = mqttClient.publish(TOPIC_SENSOR_JSON, json, true);
  published = published && jsonPublished;
  mqttClient.loop();
  Serial.printf("MQTT %s [%s]: %s\n",
                jsonPublished ? "published" : "publish FAILED",
                TOPIC_SENSOR_JSON, json);
  if (!published) {
    Serial.printf("MQTT publish summary: FAILED, connected=%s, state=%d\n",
                  mqttClient.connected() ? "yes" : "no", mqttClient.state());
  }
}

void renderDashboard() {
  memset(blackImage, 0xFF, sizeof(blackImage));
  memset(redImage, 0xFF, sizeof(redImage));

  char tempText[12], humText[12], lightText[12];
  if (sensorReadOk) snprintf(tempText, sizeof(tempText), "%4.1f", currentTemperature);
  else strcpy(tempText, "--.-");
  if (sensorReadOk) snprintf(humText, sizeof(humText), "%2.0f", currentHumidity);
  else strcpy(humText, "--");
  snprintf(lightText, sizeof(lightText), "%3u", currentLight);

  const CjkGlyph title[] = {GLYPH_HUAN, GLYPH_JING, GLYPH_JIAN, GLYPH_CE};
  const CjkGlyph temperatureLabel[] = {GLYPH_WEN, GLYPH_DU};
  const CjkGlyph humidityLabel[] = {GLYPH_SHI, GLYPH_DU};
  const CjkGlyph brightnessLabel[] = {GLYPH_LIANG, GLYPH_DU};
  const CjkGlyph okLabel[] = {GLYPH_ZHENG, GLYPH_CHANG};
  const CjkGlyph errorLabel[] = {GLYPH_WU, GLYPH_ZI, GLYPH_LIAO};

  drawCjkCentered(title, 4, SCREEN_WIDTH / 2, 1);
  drawLine(8, 13, SCREEN_WIDTH - 9, 13, true);
  drawLine(98, 17, 98, 108);
  drawLine(197, 17, 197, 108);

  drawThermometer(49, 12, true);
  drawDrop(148, 20, true);
  drawSun(247, 35, true);
  // 中文標籤下移；數字與單位再下移，讓標籤和讀值的間距約加倍。
  drawCjkCentered(temperatureLabel, 2, 49, 57);
  drawCjkCentered(humidityLabel, 2, 148, 57);
  drawCjkCentered(brightnessLabel, 2, 247, 57);

  drawCentered(tempText, 49, 79, 2, sensorReadOk && currentTemperature > 35.0f);
  drawCentered(humText, 148, 79, 2, sensorReadOk && currentHumidity > 80.0f);
  drawCentered(lightText, 247, 79, 2, currentLight < 20);
  drawCentered("C", 49, 98, 1);
  drawCentered("%RH", 148, 98, 1);
  drawCentered("%", 247, 98, 1);
  drawCjkCentered(sensorReadOk ? okLabel : errorLabel,
                  sensorReadOk ? 2 : 3,
                  SCREEN_WIDTH / 2, 112, !sensorReadOk);
}

void setup() {
  Serial.begin(115200);
  pinMode(GREEN_LAMP_PIN, OUTPUT);
  pinMode(YELLOW_LAMP_PIN, OUTPUT);
  pinMode(RED_LAMP_PIN, OUTPUT);
  applyLampStates(false);
  analogReadResolution(12);
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(256);
  Serial.println("40_epaper_mqtt: DHT11 + light sensor + MQTT");
  readSensors();
  renderDashboard();
  connectMqtt();
  if (epd.Init() != 0) {
    Serial.println("e-Paper initialization failed.");
    return;
  }
  epd.Display(blackImage, redImage);
  Serial.println("Dashboard display complete.");
  publishSensorData();
  lastUpdate = millis();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) mqttClient.loop();
  if (!mqttClient.connected()) connectMqtt();

  if (millis() - lastUpdate >= UPDATE_INTERVAL_MS) {
    lastUpdate = millis();
    readSensors();
    renderDashboard();
    epd.Display(blackImage, redImage);
    publishSensorData();
  }
  delay(20);
}
