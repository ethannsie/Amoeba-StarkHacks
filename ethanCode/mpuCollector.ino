#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

Adafruit_MPU6050 mpu;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  // Initialize I2C on GPIO 8 (SDA) and GPIO 9 (SCL)
  Wire.begin(8, 9);

  Serial.println("Initializing MPU-6050...");

  if (!mpu.begin()) {
    Serial.println("ERROR: MPU-6050 not found. Check wiring and I2C address.");
    while (1) delay(10);
  }

  Serial.println("MPU-6050 found!");

  // --- Accelerometer range ---
  // Options: MPU6050_RANGE_2_G, 4_G, 8_G, 16_G
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);

  // --- Gyroscope range ---
  // Options: MPU6050_RANGE_250_DEG, 500_DEG, 1000_DEG, 2000_DEG
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);

  // --- Low-pass filter bandwidth ---
  // Options: MPU6050_BAND_260_HZ down to MPU6050_BAND_5_HZ
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  Serial.println();
  Serial.println("Accel X (m/s²), Accel Y, Accel Z, Gyro X (rad/s), Gyro Y, Gyro Z, Temp (°C)");
  delay(100);
}

void loop() {
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  // Print as CSV — easy to graph in the Serial Plotter
  Serial.print(accel.acceleration.x, 3); Serial.print(",");
  Serial.print(accel.acceleration.y, 3); Serial.print(",");
  Serial.print(accel.acceleration.z, 3); Serial.print(",");
  Serial.print(gyro.gyro.x, 3);          Serial.print(",");
  Serial.print(gyro.gyro.y, 3);          Serial.print(",");
  Serial.print(gyro.gyro.z, 3);          Serial.print(",");
  Serial.println(temp.temperature, 2);

  delay(50); // ~20 Hz sample rate
}
