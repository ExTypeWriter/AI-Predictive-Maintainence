// ============================================================
//  AI Predictive Maintenance — Fan Monitor
//  ESP32-S3 DevKitC-1
//  Sensors : INA226 (current/voltage) + MPU-6050 (vibration)
//  Cloud   : Azure IoT Hub via MQTT over TLS
//  AI      : Isolation Forest (custom converted .h files)
// ============================================================

#include <Wire.h>
#include <INA226.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <math.h>

// ── Isolation Forest models (place .h files in same folder as .ino) ──
#include "iforest_speed1.h"
#include "iforest_speed2.h"
#include "iforest_speed3.h"

// ============================================================
//  CONFIG — Fill in your values
// ============================================================

const char* WIFI_SSID     = "NNHOME_2.4G";
const char* WIFI_PASSWORD = "REDACTED";

const char* IOT_HOST      = "wio5-24022026.azure-devices.net";
const char* DEVICE_ID     = "WioTerminal-01";
const char* SAS_TOKEN     = "SharedAccessSignature sr=wio5-24022026.azure-devices.net%2Fdevices%2FWioTerminal-01&sig=REDACTED&se=1711273600";
const int   MQTT_PORT     = 8883;

const char* TOPIC_PUBLISH = "devices/WioTerminal-01/messages/events/";

// ── Hardware pins ─────────────────────────────────────────────
#define I2C_SDA          8
#define I2C_SCL          9
#define PIN_NEOPIXEL     48
#define PIXEL_BRIGHTNESS 50
#define NUMPIXELS        1
#define PIN_BUZZER       5   // change to your GPIO
#define PIN_LED          6   // change to your GPIO

// ── Timing ───────────────────────────────────────────────────
#define PUBLISH_INTERVAL_MS  15000  // 15 sec = 5760 msg/day on free tier

// ── Speed detection thresholds (power_W) ─────────────────────
#define THRESH_1_2  1.1960f   // speed 1 → 2 boundary
#define THRESH_2_3  2.6347f   // speed 2 → 3 boundary

// ── Health score thresholds ───────────────────────────────────
#define THRESHOLD_NORMAL   68.0f
#define THRESHOLD_WARNING  60.0f
// below 60 = failure

// ============================================================
//  Objects
// ============================================================

Adafruit_NeoPixel pixel(NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
INA226            INA(0x40);
Adafruit_MPU6050  mpu;
WiFiClientSecure  wifiClient;
PubSubClient      mqttClient(wifiClient);

float         offX = 0, offY = 0, offZ = 0;
unsigned long lastPublish = 0;

// ============================================================
//  LED / Buzzer
// ============================================================

void setLED(uint32_t color) {
  pixel.setBrightness(PIXEL_BRIGHTNESS);
  pixel.setPixelColor(0, color);
  pixel.show();
}

void alertBuzzerLED(const String& status) {
  if (status == "failure") {
    for (int i = 0; i < 3; i++) {
      digitalWrite(PIN_BUZZER, HIGH);
      setLED(pixel.Color(150, 0, 0));   // Red
      delay(200);
      digitalWrite(PIN_BUZZER, LOW);
      setLED(pixel.Color(0, 0, 0));
      delay(200);
    }
  } else if (status == "warning") {
    digitalWrite(PIN_BUZZER, HIGH);
    setLED(pixel.Color(150, 150, 0));   // Yellow
    delay(500);
    digitalWrite(PIN_BUZZER, LOW);
  } else {
    setLED(pixel.Color(0, 150, 0));     // Green
  }
}

// ============================================================
//  WiFi
// ============================================================

void connectWiFi() {
  Serial.print("Connecting WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected! IP: " + WiFi.localIP().toString());
}

// ============================================================
//  MQTT (Azure IoT Hub)
// ============================================================

void connectMQTT() {
  wifiClient.setInsecure();  // OK for prototype
  mqttClient.setServer(IOT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(512);

  while (!mqttClient.connected()) {
    Serial.print("Connecting MQTT...");
    String username = String(IOT_HOST) + "/" + DEVICE_ID +
                      "/?api-version=2021-04-12";

    if (mqttClient.connect(DEVICE_ID, username.c_str(), SAS_TOKEN)) {
      Serial.println(" Connected!");
    } else {
      Serial.print(" Failed rc=");
      Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}

// ============================================================
//  Speed detection (using power_W)
// ============================================================

int detectSpeed(float power_W) {
  if (power_W < THRESH_1_2) return 1;
  if (power_W < THRESH_2_3) return 2;
  return 3;
}

// ============================================================
//  Run Isolation Forest — returns health score 0-100%
//  .h files handle scaling + inference + normalization
//  Pass raw sensor values directly — no preprocessing needed
// ============================================================

float runModel(int speed, float voltage_V, float current_mA,
               float vib_rms, float power_W) {
  if (speed == 1) return if1_health(voltage_V, current_mA, vib_rms, power_W);
  if (speed == 2) return if2_health(voltage_V, current_mA, vib_rms, power_W);
  return                 if3_health(voltage_V, current_mA, vib_rms, power_W);
}

// ============================================================
//  Status from health score
// ============================================================

String getStatus(float health_score) {
  if (health_score >= THRESHOLD_NORMAL)  return "normal";
  if (health_score >= THRESHOLD_WARNING) return "warning";
  return "failure";
}

// ============================================================
//  Publish to Azure IoT Hub
// ============================================================

void publishToAzure(float voltage_V, float current_mA,
                    float vib_rms,   float power_W,
                    int speed, float health_score,
                    const String& status) {

  if (!mqttClient.connected()) connectMQTT();

  StaticJsonDocument<256> doc;
  doc["timestamp"]     = millis();
  doc["speed"]         = speed;
  doc["voltage_V"]     = round(voltage_V  * 100)   / 100.0;
  doc["current_mA"]    = round(current_mA * 100)   / 100.0;
  doc["vibration_rms"] = round(vib_rms    * 1000)  / 1000.0;
  doc["power_W"]       = round(power_W    * 10000) / 10000.0;
  doc["health_score"]  = round(health_score * 10)  / 10.0;
  doc["status"]        = status;

  char payload[256];
  serializeJson(doc, payload);

  if (mqttClient.publish(TOPIC_PUBLISH, payload)) {
    Serial.println("Published: " + String(payload));
  } else {
    Serial.println("Publish failed");
  }
}

// ============================================================
//  SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED,    OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED,    LOW);

  pixel.begin();
  setLED(pixel.Color(0, 0, 150));  // Blue: Booting
  Serial.println("\n=== Fan Monitor Booting ===");

  Wire.begin(I2C_SDA, I2C_SCL, 400000);

  bool ina_ok = INA.begin();
  bool mpu_ok = mpu.begin();

  if (!ina_ok || !mpu_ok) {
    Serial.println(ina_ok ? "INA226 OK" : "INA226 FAIL");
    Serial.println(mpu_ok ? "MPU6050 OK" : "MPU6050 FAIL");
    setLED(pixel.Color(150, 0, 0));
    while (1) delay(100);
  }

  Serial.println("INA226 OK | MPU6050 OK");
  INA.setMaxCurrentShunt(2.0, 0.1);
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);

  // ── MPU calibration ──────────────────────────────────────
  delay(500);
  setLED(pixel.Color(150, 150, 0));  // Yellow: Calibrating
  Serial.println("Calibrating MPU-6050...");

  int samples = 200;
  for (int i = 0; i < samples; i++) {
    sensors_event_t a, g, temp;
    if (mpu.getEvent(&a, &g, &temp)) {
      offX += a.acceleration.x;
      offY += a.acceleration.y;
      offZ += a.acceleration.z;
    } else { i--; }
    delay(5);
  }
  offX /= samples;
  offY /= samples;
  offZ /= samples;
  offZ -= 9.81f;
  Serial.printf("Offsets: X=%.3f Y=%.3f Z=%.3f\n", offX, offY, offZ);

  // ── WiFi + MQTT ───────────────────────────────────────────
  setLED(pixel.Color(0, 0, 150));  // Blue: Connecting
  connectWiFi();
  connectMQTT();

  setLED(pixel.Color(0, 150, 0));  // Green: Ready
  Serial.println("=== System Ready ===");
}

// ============================================================
//  LOOP
// ============================================================

void loop() {
  if (!mqttClient.connected()) connectMQTT();
  mqttClient.loop();

  // ── 1. Read sensors ───────────────────────────────────────
  sensors_event_t a, g, temp;
  if (!mpu.getEvent(&a, &g, &temp)) {
    setLED(pixel.Color(150, 0, 0));
    return;
  }

  float voltage_V  = INA.getBusVoltage();
  float current_mA = (INA.getShuntVoltage() * 1000.0f) / 0.1f;
  float ax         = a.acceleration.x - offX;
  float ay         = a.acceleration.y - offY;
  float az         = a.acceleration.z - offZ;
  float vib_rms    = sqrtf(ax*ax + ay*ay + az*az);
  float power_W    = (voltage_V * current_mA) / 1000.0f;

  // ── 2. Detect fan speed ───────────────────────────────────
  int speed = detectSpeed(power_W);

  // ── 3. Run model on ESP32 (true edge computing) ──────────
  float health_score = runModel(speed, voltage_V, current_mA,
                                vib_rms, power_W);
  String status = getStatus(health_score);

  // ── 4. Debug print ────────────────────────────────────────
  Serial.printf("[Spd%d] V=%.2f I=%.1f vib=%.3f pwr=%.4f | health=%.1f%% → %s\n",
                speed, voltage_V, current_mA, vib_rms, power_W,
                health_score, status.c_str());

  // ── 5. Alert on device ────────────────────────────────────
  alertBuzzerLED(status);

  // ── 6. Publish to Azure every 15 seconds ─────────────────
  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_INTERVAL_MS) {
    publishToAzure(voltage_V, current_mA, vib_rms, power_W,
                   speed, health_score, status);
    lastPublish = now;
  }

  delay(10);  // ~100Hz sampling
}