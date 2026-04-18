// shared_types.h
#pragma once

typedef struct {
  float accel_x, accel_y, accel_z;  // m/s²
  float gyro_x,  gyro_y,  gyro_z;   // rad/s
} MPU6050Packet;
// receiver.ino
#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

Adafruit_MPU6050 localMPU;

// Store latest received packet in a global, updated by callback
volatile bool newDataAvailable = false;
MPU6050Packet remotePacket;

void onDataReceived(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(MPU6050Packet)) return;
  memcpy((void*)&remotePacket, data, sizeof(MPU6050Packet));
  newDataAvailable = true;
}

void setup() {
  Serial.begin(115200);

  // Init local MPU6050 on custom pins
  Wire.begin(/* SDA= */7, /* SCL= */15);
  if (!localMPU.begin()) {
    Serial.println("Local MPU6050 not found! Check wiring.");
    while (1);
  }
  localMPU.setAccelerometerRange(MPU6050_RANGE_8_G);
  localMPU.setGyroRange(MPU6050_RANGE_500_DEG);
  localMPU.setFilterBandwidth(MPU6050_BAND_21_HZ);
  Serial.println("Local MPU6050 ready.");

  // Init ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (1);
  }
  esp_now_register_recv_cb(onDataReceived);
  Serial.println("Receiver ready, waiting for data...\n");
}

void loop() {
  // Read local MPU6050
  sensors_event_t accel, gyro, temp;
  localMPU.getEvent(&accel, &gyro, &temp);

  Serial.println("=== LOCAL  (Receiver MPU6050) ===");
  Serial.printf("  Accel  X: %6.2f  Y: %6.2f  Z: %6.2f m/s²\n",
                accel.acceleration.x, accel.acceleration.y, accel.acceleration.z);
  Serial.printf("  Gyro   X: %6.2f  Y: %6.2f  Z: %6.2f rad/s\n",
                gyro.gyro.x, gyro.gyro.y, gyro.gyro.z);

  // Print latest remote data if available
  Serial.println("=== REMOTE (Sender MPU6050)   ===");
  if (newDataAvailable) {
    Serial.printf("  Accel  X: %6.2f  Y: %6.2f  Z: %6.2f m/s²\n",
                  remotePacket.accel_x, remotePacket.accel_y, remotePacket.accel_z);
    Serial.printf("  Gyro   X: %6.2f  Y: %6.2f  Z: %6.2f rad/s\n",
                  remotePacket.gyro_x, remotePacket.gyro_y, remotePacket.gyro_z);
  } else {
    Serial.println("  Waiting for sender...");
  }

  Serial.println();
  delay(20);  // match sender's ~50Hz rate
}