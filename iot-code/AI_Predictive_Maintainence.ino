#include <Wire.h>
#include <INA226.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_NeoPixel.h>

#define I2C_SDA 8
#define I2C_SCL 9
#define PIN_NEOPIXEL 48 
#define PIXEL_BRIGHTNESS 50
#define NUMPIXELS 1

Adafruit_NeoPixel pixel(NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
INA226 INA(0x40);
Adafruit_MPU6050 mpu;

float offX = 0, offY = 0, offZ = 0;
unsigned long startTime = 0;

void setLED(uint32_t color) {
  pixel.setBrightness(PIXEL_BRIGHTNESS);
  pixel.setPixelColor(0, color);
  pixel.show();
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  pixel.begin();
  setLED(pixel.Color(0, 0, 150)); // Blue: Booting

  // I2C
  Wire.begin(I2C_SDA, I2C_SCL, 400000);  // 400kHz Fast Mode

  bool ina_ok = INA.begin();
  bool mpu_ok = mpu.begin();

  if (!ina_ok || !mpu_ok) {
    Serial.println(ina_ok ? "INA OK" : "INA FAIL");
    Serial.println(mpu_ok ? "MPU OK" : "MPU FAIL");
    setLED(pixel.Color(150, 0, 0)); // Red: Error
    while (1) delay(100);
  }

  INA.setMaxCurrentShunt(2.0, 0.1);
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  
  // เพิ่มหน่วงเวลาให้ Sensor นิ่งก่อน Calibrate
  delay(500); 
  setLED(pixel.Color(150, 150, 0)); // Yellow: Calibrating
  Serial.println("Calibrating...");

  int samples = 200;
  for (int i = 0; i < samples; i++) {
    sensors_event_t a, g, temp;
    if (mpu.getEvent(&a, &g, &temp)) {
      offX += a.acceleration.x;
      offY += a.acceleration.y;
      offZ += a.acceleration.z;
    } else {
      i--;
    }
    delay(5);
  }
  
  offX /= samples; offY /= samples; offZ /= samples;
  offZ -= 9.81;

  setLED(pixel.Color(0, 150, 0)); // Green: Ready
  Serial.println("System Ready!");
  startTime = millis();  // <-- Added: capture time after calibration
}

void loop() {
  sensors_event_t a, g, temp;
  if (mpu.getEvent(&a, &g, &temp)) {
    float busV = INA.getBusVoltage();
    float current_mA = (INA.getShuntVoltage() * 1000.0) / 0.1;

    Serial.print(millis() - startTime); Serial.print(",");  // <-- Changed
    Serial.print(busV, 2); Serial.print(",");
    Serial.print(current_mA, 2); Serial.print(",");
    Serial.print(a.acceleration.x - offX, 3); Serial.print(",");
    Serial.print(a.acceleration.y - offY, 3); Serial.print(",");
    Serial.println(a.acceleration.z - offZ, 3);
  } else {
    // หาก Sensor หลุดระหว่างทำงาน ให้ไฟเป็นสีแดง
    setLED(pixel.Color(150, 0, 0));
  }
  delay(10);
}
