# Passenger Princess 👸

> *A wearable hardware co-pilot that uses real-time sensor fusion to coach drivers with hands-free, eyes-on-the-road audio feedback.*

**StarkHacks 2026** — Built by Ethan Sie, Avelyse Odle, Monish RJ, and Ollie L.

---

## What It Does

Passenger Princess is a dual-device smart driving system. One unit mounts on the dashboard; the other wraps behind your ear as a wearable earpiece. Together they:

- Detect driving events — hard braking, sharp turns, aggressive acceleration, lane changes, and distracted head position — using fused IMU data from both units
- Stream real-time audio alerts to your phone via a WebSocket server (no app install, just a URL)
- Display animated visual cues on an OLED screen mounted to the dashboard

No screen-glancing. No Bluetooth pairing. Just a co-pilot that actually talks back.

---

## Hardware

### Dashboard Unit
| Component | Role |
|---|---|
| ESP32-S3 | Event detection, WebSocket server, ESP-NOW receiver |
| MPU-6050 | Local accelerometer + gyroscope (car motion) |
| OLED 0.96" SSD1306 | Animated event feedback display |

### Earpiece Unit
| Component | Role |
|---|---|
| ESP32-S3 | ESP-NOW transmitter |
| MPU-6050 | Head-mounted accelerometer + gyroscope (driver head motion) |

---

## System Architecture

```
[Earpiece MPU-6050]
       │  ESP-NOW (~50Hz)
       ▼
[Dashboard ESP32-S3] ──── I2C ────▶ [Local MPU-6050]
       │
       ├── Sensor fusion (EMA filter + calibration)
       ├── Event detection (threshold + time-gating + cooldowns)
       ├── OLED animations
       └── WebSocket server (192.168.4.1)
                    │
              [Phone Browser]
              Web Audio API → MP3 playback via LittleFS
```

The dashboard ESP32 runs in `WIFI_AP_STA` mode simultaneously — it hosts the WiFi AP for the phone connection while using ESP-NOW for the earpiece link on the same channel.

---

## Signal Processing

**Calibration:** On boot, each sensor averages 20 readings at rest to compute zero-offset corrections for X, Y, and Z axes. The gravity component on the dominant axis is subtracted out.

**EMA Filter:** Exponential moving average with α = 0.3 smooths raw sensor readings before event detection, reducing noise without introducing significant lag.

**Sensor Fusion:** When the earpiece is connected, dashboard and earpiece readings are averaged 50/50 for car-motion events. Head-movement detection uses only the remote (earpiece) sensor independently.

## Firmware Setup

### Dependencies (Arduino Library Manager)
- `Adafruit MPU6050`
- `Adafruit Unified Sensor`
- `Adafruit GFX Library`
- `Adafruit SSD1306`
- `AsyncTCP`
- `ESPAsyncWebServer`
- `LittleFS` (built into ESP32 Arduino core)

### Dashboard Unit (`ethanCode/receiver/receiver.ino`)

1. Flash `receiver.ino` to the dashboard ESP32-S3.
2. On boot, keep the unit still for ~1 second during calibration.
3. Connect your phone to WiFi `ESP32-Speaker` (password: `12345678`).
4. Open `http://192.168.4.1` and tap **Unlock Audio**.

### Earpiece Unit (`ethanCode/sender/sender.ino`)

1. Find the dashboard ESP32's MAC address from Serial Monitor on boot.
2. Update `RECEIVER_MAC[]` in `sender.ino`.
3. Flash to the earpiece ESP32-S3.
4. Sender runs at ~50Hz and requires no further configuration.

### MP3 Audio Files

Audio is stored on the dashboard ESP32's LittleFS filesystem. To upload files:

1. Open `http://192.168.4.1/upload` while connected to the AP.
2. Upload `.mp3` files — they are served from `/files/` and referenced by name.

Expected filenames:
```
lane_change.mp3
dont_break_hard.mp3
hard_accel.mp3
sharp_turn.mp3
look_at_road.mp3
```

Audio was generated with ElevenLabs for realistic voice feedback.

---

## OLED Animations

Each event triggers a 2-second animation on the 128×64 display:

| Event | Animation |
|---|---|
| Lane Change | Scrolling arrow pattern |
| Hard Brake | Expanding concentric circles + flashing border |
| Hard Accel | Horizontal speed lines |
| Sharp Turn | Rotating pixel ellipse |
| Head Down | Face with downcast eyes + flashing "EYES ON ROAD" |
| Idle (linked) | Neutral face |
| Idle (waiting) | Skeptical face with one raised eyebrow |
| Boot | Blinking animation |

---

## Physical Design

Custom 3D-printed housings were designed in Autodesk Fusion 360 and OnShape:
- **Dashboard mount** — snap-fit enclosure for the ESP32-S3, MPU-6050, and OLED
- **Earpiece** — minimal over-ear clip holding the MPU-6050 behind the ear


## Links

- [Devpost](https://devpost.com/software/passenger-princess)
- [Slide Deck](https://canva.link/p1uow6gpysrmx5f)
