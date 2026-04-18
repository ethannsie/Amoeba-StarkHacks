#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

Adafruit_MPU6050 mpu;

#define LED_PIN 48

// ─── THRESHOLDS ──────────────────────────────────────────
#define LANE_ACCEL    1.50f
#define LANE_TIME      300
#define BRAKE_ACCEL  -1.96f
#define BRAKE_TIME     150
#define HACC_ACCEL    3.00f
#define HACC_TIME      200
#define TURN_GYRO     0.40f
#define TURN_TIME      300
#define BUMP_ACCEL    3.00f
#define BUMP_TIME      150
#define COOLDOWN_MS   3000
#define LOOP_MS         50

// ─── CALIBRATION ─────────────────────────────────────────
float offsetX = 0, offsetY = 0, offsetZ = 0;

// ─── STATE ───────────────────────────────────────────────
uint32_t laneTimer = 0, brakeTimer = 0;
uint32_t haccTimer = 0, turnTimer  = 0, bumpTimer = 0;
uint32_t lastTrigger = 0;
String   lastEvent = "";
bool     triggerFlag = false;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  Wire.begin(8, 9);

  Serial.println("Initializing MPU-6050...");
  if (!mpu.begin()) {
    Serial.println("ERROR: MPU-6050 not found.");
    while (1) delay(10);
  }
  Serial.println("MPU-6050 found!");

  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  // ── Calibration — keep still for ~1 second ───────────────
  Serial.println("Calibrating — keep board still...");
  delay(500);

  float sumX = 0, sumY = 0, sumZ = 0;
  for (int i = 0; i < 20; i++) {
    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);
    sumX += a.acceleration.x;
    sumY += a.acceleration.y;
    sumZ += a.acceleration.z;
    delay(50);
  }
  offsetX = sumX / 20.0f;
  offsetY = sumY / 20.0f;
  offsetZ = (sumZ / 20.0f) - 9.81f;

  Serial.println("--- CALIBRATION RESULT ---");
  Serial.print("  offsetX: "); Serial.println(offsetX, 4);
  Serial.print("  offsetY: "); Serial.println(offsetY, 4);
  Serial.print("  offsetZ: "); Serial.println(offsetZ, 4);
  Serial.println("--- READY — move the board ---");
  Serial.println("--- REST CHECK (should all be ~0) ---");
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);
  Serial.print("  restX: "); Serial.println(a.acceleration.x - offsetX, 4);
  Serial.print("  restY: "); Serial.println(a.acceleration.y - offsetY, 4);
  Serial.print("  restZ: "); Serial.println(a.acceleration.z - 9.81f - offsetZ, 4);
  delay(750);

  // Plotter header — 4 values only, no string column
  Serial.println("accel_x:,accel_y:,accel_z:,gyro_z:");
}

void fireTrigger(String type) {
  uint32_t now = millis();
  if (now - lastTrigger < COOLDOWN_MS) return;

  lastTrigger = now;
  triggerFlag = true;
  lastEvent   = type;

  digitalWrite(LED_PIN, HIGH);
  delay(200);
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  uint32_t loopStart = millis();

  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  float aX = accel.acceleration.x - offsetX;
  float aY = accel.acceleration.y - offsetY;
  float aZ = accel.acceleration.z - 9.81f - offsetZ;
  float gZ = gyro.gyro.z;

  // ── 1. Bump filter ───────────────────────────────────────
  bool isBump = false;
  if (abs(aZ) > BUMP_ACCEL) {
    bumpTimer += LOOP_MS;
    if (bumpTimer < BUMP_TIME) isBump = true;
  } else { bumpTimer = 0; }

  // ── 2. Lane change ───────────────────────────────────────
  if (!isBump && abs(aY) > LANE_ACCEL) {
    laneTimer += LOOP_MS;
    if (laneTimer >= LANE_TIME) {
      fireTrigger("LANE_CHANGE");
      laneTimer = 0;
    }
  } else { laneTimer = 0; }

  // ── 3. Hard braking ──────────────────────────────────────
  if (!isBump && aX < BRAKE_ACCEL) {
    brakeTimer += LOOP_MS;
    if (brakeTimer >= BRAKE_TIME) {
      fireTrigger("HARD_BRAKE");
      brakeTimer = 0;
    }
  } else { brakeTimer = 0; }

  // ── 4. Hard acceleration ─────────────────────────────────
  if (!isBump && aX > HACC_ACCEL) {
    haccTimer += LOOP_MS;
    if (haccTimer >= HACC_TIME) {
      fireTrigger("HARD_ACCEL");
      haccTimer = 0;
    }
  } else { haccTimer = 0; }

  // ── 5. Sharp turn ────────────────────────────────────────
  if (!isBump && abs(gZ) > TURN_GYRO) {
    turnTimer += LOOP_MS;
    if (turnTimer >= TURN_TIME) {
      fireTrigger("SHARP_TURN");
      turnTimer = 0;
    }
  } else { turnTimer = 0; }

  // ── Plotter output — 4 numbers only ──────────────────────
  // Serial.print(aX, 3); Serial.print(",");
  // Serial.print(aY, 3); Serial.print(",");
  // Serial.print(aZ, 3); Serial.print(",");
  // Serial.println(gZ, 3);

  // ── Debug — only prints when triggered ───────────────────
  if (triggerFlag) {
    Serial.print("[TRIGGER] ");
    Serial.print(lastEvent);
    Serial.print("  aX="); Serial.print(aX, 3);
    Serial.print("  aY="); Serial.print(aY, 3);
    Serial.print("  aZ="); Serial.print(aZ, 3);
    Serial.print("  gZ="); Serial.println(gZ, 3);
    triggerFlag = false;
    lastEvent = "";
  }

  uint32_t elapsed = millis() - loopStart;
  if (elapsed < LOOP_MS) delay(LOOP_MS - elapsed);
}
