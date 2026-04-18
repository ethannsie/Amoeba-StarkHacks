typedef struct {
  float accel_x, accel_y, accel_z;  // m/s²
  float gyro_x,  gyro_y,  gyro_z;   // rad/s
} MPU6050Packet;

// sender.ino
#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ── Replace with your receiver's MAC ──────────────────────────────────
uint8_t RECEIVER_MAC[] = {0x90, 0x70, 0x69, 0x35, 0x3A, 0xA0};
// ──────────────────────────────────────────────────────────────────────

Adafruit_MPU6050 mpu;
MPU6050Packet packet;

void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("Send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void setup() {
  Serial.begin(115200);

  // Init MPU6050
  Wire.begin(/* SDA= */8, /* SCL= */9);  // adjust pins for your wiring
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found! Check wiring.");
    while (1);
  }
  delay(100);  
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  // Init ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (1);
  }
  esp_now_register_send_cb(onDataSent);

  // Register receiver as a peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, RECEIVER_MAC, 6);
  peerInfo.channel = 0;   // 0 = current channel
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    while (1);
  }

  Serial.println("Sender ready.");
}

void loop() {
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  packet.accel_x = accel.acceleration.x;
  packet.accel_y = accel.acceleration.y;
  packet.accel_z = accel.acceleration.z;
  packet.gyro_x  = gyro.gyro.x;
  packet.gyro_y  = gyro.gyro.y;
  packet.gyro_z  = gyro.gyro.z;

  esp_now_send(RECEIVER_MAC, (uint8_t *)&packet, sizeof(packet));

  delay(20);  // ~50Hz — adjust as needed
}

