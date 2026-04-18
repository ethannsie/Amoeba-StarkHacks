#pragma once

// ─── ESP-NOW packet (sender → receiver) ──────────────────
typedef struct {
  float accel_x, accel_y, accel_z;  // m/s²
  float gyro_x,  gyro_y,  gyro_z;   // rad/s
} MPU6050Packet;

// ─── Per-sensor state: calibration + EMA + fused output ──
struct SensorState {
  const char* label;

  // calibration offsets
  float offX = 0, offY = 0, offZ = 0;

  // EMA internal state
  float emaX = 0, emaY = 0, emaZ = 0, emaGZ = 0;
  bool  emaInit = false;

  // latest calibrated + filtered output (written by updateSensor)
  float outX = 0, outY = 0, outZ = 0, outGZ = 0;

  // true once at least one reading has been processed
  bool hasData = false;
};

// ─── Single vehicle event state ───────────────────────────
struct EventState {
  uint32_t laneStart  = 0, brakeStart = 0;
  uint32_t haccStart  = 0, turnStart  = 0, bumpStart = 0;

  uint32_t lastLane  = 0, lastBrake = 0;
  uint32_t lastHacc  = 0, lastTurn  = 0, lastBump  = 0;
};
