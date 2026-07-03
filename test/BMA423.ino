#include <Wire.h>
#include <SPI.h>
#include <Arduino.h>
#include "SensorBMA423.hpp"

#ifndef SENSOR_SDA
#define SENSOR_SDA 10
#endif

#ifndef SENSOR_SCL
#define SENSOR_SCL 11
#endif

#ifndef SENSOR_IRQ
#define SENSOR_IRQ 14
#endif

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

SensorBMA423 accel;

volatile bool sensorIRQ = false;
void IRAM_ATTR setFlag() { sensorIRQ = true; }

static const int16_t DEADZONE = 180;   // tăng/giảm để lọc rung
static const uint32_t SAMPLE_MS = 50;

enum Direction {
  DIR_NONE,
  DIR_LEFT,
  DIR_RIGHT,
  DIR_UP,
  DIR_DOWN
};

Direction lastDir = DIR_NONE;
uint32_t lastSample = 0;

const char* dirToString(Direction d) {
  switch (d) {
    case DIR_LEFT:  return "LEFT";
    case DIR_RIGHT: return "RIGHT";
    case DIR_UP:    return "UP";
    case DIR_DOWN:  return "DOWN";
    default:        return "NONE";
  }
}

Direction detectDirection(int16_t x, int16_t y, int16_t z) {
  int16_t ax = abs(x);
  int16_t ay = abs(y);
  int16_t az = abs(z);

  // Chỉ xét khi có thay đổi đủ lớn
  if (max(ax, ay) < DEADZONE && az < DEADZONE) {
    return DIR_NONE;
  }

  // Ưu tiên trục có độ lệch lớn nhất
  if (ax >= ay && ax >= az) {
    // Quy ước có thể phải đảo tùy chiều đặt board
    return (x > 0) ? DIR_RIGHT : DIR_LEFT;
  }

  if (ay >= ax && ay >= az) {
    return (y > 0) ? DIR_UP : DIR_DOWN;
  }

  return DIR_NONE;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  pinMode(SENSOR_IRQ, INPUT);

  if (!accel.begin(Wire, BMA423_I2C_ADDR_SECONDARY, SENSOR_SDA, SENSOR_SCL)) {
    Serial.println("Failed to find BMA423 - check wiring!");
    for (;;)
      delay(1000);
  }

  Serial.println("Init BMA423 Sensor success!");

  accel.configAccelerometer();   // default 4G, ~200 Hz
  accel.enableAccelerometer();

  // Chọn đúng orientation của board bạn đang gắn
  accel.setRemapAxes(SensorBMA423::REMAP_BOTTOM_LAYER_TOP_RIGHT_CORNER);

  // Tắt các feature IRQ nếu không dùng
  accel.disablePedometerIRQ();
  accel.disableActivityIRQ();
  accel.disableAnyNoMotionIRQ();
  accel.disableWakeupIRQ();
  accel.disableTiltIRQ();

  accel.enableFeature(SensorBMA423::FEATURE_STEP_CNTR, false);
  accel.enableFeature(SensorBMA423::FEATURE_ACTIVITY,  false);
  accel.enableFeature(SensorBMA423::FEATURE_ANY_MOTION,false);
  accel.enableFeature(SensorBMA423::FEATURE_NO_MOTION, false);
  accel.enableFeature(SensorBMA423::FEATURE_WAKEUP,    false);

  accel.readIrqStatus();

  Serial.println("Raw direction mode armed. Move the board left/right/up/down.");
}

void loop() {
  if (millis() - lastSample >= SAMPLE_MS) {
    lastSample = millis();

    int16_t x, y, z;
    accel.getAccelerometer(x, y, z);

    Direction d = detectDirection(x, y, z);

    Serial.print("X:");
    Serial.print(x);
    Serial.print(" Y:");
    Serial.print(y);
    Serial.print(" Z:");
    Serial.print(z);
    Serial.print(" -> ");

    if (d == DIR_NONE) {
      Serial.println("NONE");
    } else {
      Serial.println(dirToString(d));
    }

    lastDir = d;
  }

  // Nếu vẫn muốn đọc IRQ cho debug, giữ đoạn này
  if (sensorIRQ) {
    sensorIRQ = false;
    uint16_t status = accel.readIrqStatus();
    Serial.print("INT_STATUS: 0x");
    Serial.println(status, HEX);
  }

  delay(5);
}