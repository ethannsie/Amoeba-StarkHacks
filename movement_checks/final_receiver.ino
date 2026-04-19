#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "shared_types.h"

// ─── I2C & OLED ──────────────────────────────────────────
#define I2C_SDA        7
#define I2C_SCL       15
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ─── LED ─────────────────────────────────────────────────
#define LED_PIN 48

// ─── WIFI / WEBSOCKET ────────────────────────────────────
#define AP_SSID    "ESP32-Speaker"
#define AP_PASS    "12345678"
#define AP_CHANNEL 1
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ─── EVENT THRESHOLDS ────────────────────────────────────
// Board mounted vertically: aX=gravity, aY=lateral, aZ=fwd/back, gX=turn
float LANE_ACCEL  = 1.50f;   // aY threshold — lateral lane change (m/s²)
float BRAKE_ACCEL = 1.96f;   // aZ threshold — hard braking (m/s²)
float HACC_ACCEL  = 3.00f;   // aZ threshold — hard acceleration (m/s²)
float TURN_GYRO   = 0.70f;   // gX threshold — sharp turn (rad/s)
#define LANE_TIME      100    // ms aY must stay above threshold
#define BRAKE_TIME     100    // ms aZ must stay below threshold
#define HACC_TIME      100    // ms aZ must stay above threshold
#define TURN_TIME      100    // ms gX must stay above threshold
#define LOOP_MS         50    // sensor loop rate (20Hz)

#define HEAD_PITCH_THRESHOLD  6.0f   // tune after testing
#define HEAD_PITCH_TIME       2000    // ms looking down before trigger
#define COOLDOWN_HEAD         8000    // longer cooldown — don't nag

// Individual cooldowns — how long before the SAME event can fire again
#define COOLDOWN_LANE   4000
#define COOLDOWN_BRAKE  4000
#define COOLDOWN_HACC   4000
#define COOLDOWN_TURN   4000

// Global cooldown — minimum gap between ANY two events.
// Keeps sounds and animations from stepping on each other.
#define COOLDOWN_GLOBAL 2000

#define EMA_ALPHA 0.3f

// ─── ANIMATION STATE ─────────────────────────────────────
enum AnimationType { ANIM_NONE, ANIM_LANE, ANIM_BRAKE, ANIM_HACC, ANIM_TURN, ANIM_HEAD };
volatile AnimationType activeAnim = ANIM_NONE;
uint32_t animStart     = 0;
uint32_t lastAnyTrigger = 0;   // global cooldown timestamp
#define ANIM_DURATION 2000     // matches minimum global cooldown

// ─── SENSOR INSTANCES ────────────────────────────────────
Adafruit_MPU6050 localMPU;
SensorState localSensor  = { "LOCAL"  };
SensorState remoteSensor = { "REMOTE" };
EventState  car;
uint32_t headDownStart   = 0;
uint32_t lastHeadTrigger = 0;

// ─── ESP-NOW ─────────────────────────────────────────────
volatile bool newRemoteData = false;
MPU6050Packet remotePacket;

void onDataReceived(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(MPU6050Packet)) return;
  memcpy((void*)&remotePacket, data, sizeof(MPU6050Packet));
  newRemoteData = true;
}

// ─── EMA ─────────────────────────────────────────────────
void applyEMA(SensorState &s, float aX, float aY, float aZ, float gZ) {
  if (!s.emaInit) {
    s.emaX = aX; s.emaY = aY; s.emaZ = aZ; s.emaGZ = gZ;
    s.emaInit = true;
  }
  s.emaX  = EMA_ALPHA * aX  + (1.0f - EMA_ALPHA) * s.emaX;
  s.emaY  = EMA_ALPHA * aY  + (1.0f - EMA_ALPHA) * s.emaY;
  s.emaZ  = EMA_ALPHA * aZ  + (1.0f - EMA_ALPHA) * s.emaZ;
  s.emaGZ = EMA_ALPHA * gZ  + (1.0f - EMA_ALPHA) * s.emaGZ;
  s.outX  = s.emaX;
  s.outY  = s.emaY;
  s.outZ  = s.emaZ;
  s.outGZ = s.emaGZ;
  s.hasData = true;
}

void updateSensor(SensorState &s,
                  float rawX, float rawY, float rawZ, float rawGZ) {
  rawX -= s.offX; rawY -= s.offY; rawZ -= s.offZ;
  applyEMA(s, rawX, rawY, rawZ, rawGZ);
}

void fuseSensors(float &aX, float &aY, float &aZ, float &gZ) {
  if (remoteSensor.hasData) {
    aX = (localSensor.outX  + remoteSensor.outX)  * 0.5f;
    aY = (localSensor.outY  + remoteSensor.outY)  * 0.5f;
    aZ = (localSensor.outZ  + remoteSensor.outZ)  * 0.5f;
    gZ = (localSensor.outGZ + remoteSensor.outGZ) * 0.5f;
  } else {
    aX = localSensor.outX; aY = localSensor.outY;
    aZ = localSensor.outZ; gZ = localSensor.outGZ;
  }
}

// ─── TRIGGER ─────────────────────────────────────────────
void fireTrigger(const char* type, const char* sound, AnimationType anim,
                 float aX, float aY, float aZ, float gZ) {
  const char* src = remoteSensor.hasData ? "FUSED" : "LOCAL";
  Serial.printf("[%s] %-12s  aX=%6.3f  aY=%6.3f  aZ=%6.3f  gZ=%6.3f\n",
                src, type, aX, aY, aZ, gZ);
  ws.textAll(sound);
  activeAnim     = anim;
  animStart      = millis();
  lastAnyTrigger = millis();
  digitalWrite(LED_PIN, HIGH);
}

bool cooldownPassed(uint32_t lastEvent, uint32_t cooldown) {
  return (millis() - lastEvent) >= cooldown;
}

// ─── MOVEMENT CHECKS ─────────────────────────────────────
// Board mounted vertically: aX=gravity, aY=lateral, aZ=fwd/back, gX=turn
// gX is passed in as the gZ parameter through the pipeline
void checkMovement(float aX, float aY, float aZ, float gZ) {
  // Global gate — nothing fires until 2s after the last event
  if (!cooldownPassed(lastAnyTrigger, COOLDOWN_GLOBAL)) return;

  uint32_t now = millis();

  // ── Lane/turn priority — whichever exceeds its threshold more wins ──
  float laneExcess = abs(aY) - LANE_ACCEL;
  float turnExcess = abs(gZ) - TURN_GYRO;
  bool laneActive  = laneExcess > 0;
  bool turnActive  = turnExcess > 0;
  if (laneActive && turnActive) {
    if (laneExcess >= turnExcess) turnActive = false;
    else                          laneActive = false;
  }

  // Lane change — lateral force on Y axis
  if (laneActive) {
    if (car.laneStart == 0) car.laneStart = now;
    if ((now - car.laneStart) >= LANE_TIME
        && cooldownPassed(car.lastLane, COOLDOWN_LANE)) {
      fireTrigger("LANE_CHANGE", "PLAY:lane_change.mp3", ANIM_LANE, aX, aY, aZ, gZ);
      car.lastLane  = now;
      car.laneStart = 0;
    }
  } else { car.laneStart = 0; }

  // Hard brake — negative Z (forward deceleration)
  if (aZ < -BRAKE_ACCEL) {
    if (car.brakeStart == 0) car.brakeStart = now;
    if ((now - car.brakeStart) >= BRAKE_TIME
        && cooldownPassed(car.lastBrake, COOLDOWN_BRAKE)) {
      fireTrigger("HARD_BRAKE", "PLAY:dont_break_hard.mp3", ANIM_BRAKE, aX, aY, aZ, gZ);
      car.lastBrake  = now;
      car.brakeStart = 0;
    }
  } else { car.brakeStart = 0; }

  // Hard accel — positive Z (forward thrust)
  if (aZ > HACC_ACCEL) {
    if (car.haccStart == 0) car.haccStart = now;
    if ((now - car.haccStart) >= HACC_TIME
        && cooldownPassed(car.lastHacc, COOLDOWN_HACC)) {
      fireTrigger("HARD_ACCEL", "PLAY:hard_accel.mp3", ANIM_HACC, aX, aY, aZ, gZ);
      car.lastHacc  = now;
      car.haccStart = 0;
    }
  } else { car.haccStart = 0; }

  // Sharp turn — gX passed in as gZ, rotation around vertical axis
  if (turnActive) {
    if (car.turnStart == 0) car.turnStart = now;
    if ((now - car.turnStart) >= TURN_TIME
        && cooldownPassed(car.lastTurn, COOLDOWN_TURN)) {
      fireTrigger("SHARP_TURN", "PLAY:sharp_turn.mp3", ANIM_TURN, aX, aY, aZ, gZ);
      car.lastTurn  = now;
      car.turnStart = 0;
    }
  } else { car.turnStart = 0; }
}

// ─── HEAD CHECK ──────────────────────────────────────────
// Detects sustained downward head pitch — distracted driving
// Only runs when head unit is connected (remoteSensor.hasData)
void checkHeadMovement() {
  if (!remoteSensor.hasData) return;
  if (!cooldownPassed(lastHeadTrigger, COOLDOWN_HEAD)) return;
  if (!cooldownPassed(lastAnyTrigger, COOLDOWN_GLOBAL)) return;

  uint32_t now = millis();

  // remoteSensor.outY — tune threshold after verifying pitch axis
  if (abs(remoteSensor.outY) > HEAD_PITCH_THRESHOLD) {
    if (headDownStart == 0) headDownStart = now;
    if ((now - headDownStart) >= HEAD_PITCH_TIME) {
      Serial.println("[HEAD] DISTRACTED — looking down too long");
      ws.textAll("PLAY:look_at_road.mp3");
      activeAnim      = ANIM_HEAD;
      animStart       = millis();
      lastAnyTrigger  = millis();
      lastHeadTrigger = now;
      headDownStart   = 0;
      digitalWrite(LED_PIN, HIGH);
    }
  } else {
    headDownStart = 0;
  }
}

// ─── ANIMATIONS ──────────────────────────────────────────
void drawLaneAnim(uint32_t e) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(12, 4);
  display.print("LANE");
  display.setCursor(12, 24);
  display.print("CHANGE");
  int base = (e / 12) % 160 - 20;
  for (int i = 0; i < 4; i++) {
    int ax = base + i * 18;
    if (ax < -10 || ax > SCREEN_WIDTH) continue;
    display.drawLine(ax,     54, ax + 8, 48, SSD1306_WHITE);
    display.drawLine(ax,     54, ax + 8, 60, SSD1306_WHITE);
    display.drawLine(ax + 8, 48, ax + 8, 60, SSD1306_WHITE);
  }
  display.display();
}

void drawBrakeAnim(uint32_t e) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  int r = (e / 25) % 50;
  display.drawCircle(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, r, SSD1306_WHITE);
  display.drawCircle(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, r + 8, SSD1306_WHITE);
  if ((e / 100) % 2 == 0) {
    display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
    display.drawRect(2, 2, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, SSD1306_WHITE);
  }
  display.setTextSize(4);
  display.setCursor(54, 18);
  display.print("!");
  display.display();
}

void drawHaccAnim(uint32_t e) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(22, 4);
  display.print("SPEED!");
  for (int i = 0; i < 6; i++) {
    int y = 26 + i * 6;
    int off = ((e / 8) + i * 20) % 160 - 30;
    display.drawLine(off, y, off + 25, y, SSD1306_WHITE);
  }
  display.display();
}

void drawTurnAnim(uint32_t e) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(22, 4);
  display.print("TURN!");
  float angle = e / 80.0f;
  int cx = SCREEN_WIDTH / 2, cy = 42;
  for (int i = 0; i < 24; i++) {
    float a = angle + i * 0.26f;
    int x = cx + (int)(cos(a) * 16);
    int y = cy + (int)(sin(a) * 13);
    display.drawPixel(x, y, SSD1306_WHITE);
    display.drawPixel(x + 1, y, SSD1306_WHITE);
  }
  display.display();
}

void drawHeadDownAnim(uint32_t e) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Eyes looking down — pupils shifted low
  display.fillCircle(44, 26, 6, SSD1306_WHITE);
  display.fillCircle(84, 26, 6, SSD1306_WHITE);

  // Heavy brows — angled inward, concerned look
  display.drawLine(36, 12, 52, 16, SSD1306_WHITE);
  display.drawLine(36, 11, 52, 15, SSD1306_WHITE);
  display.drawLine(92, 12, 76, 16, SSD1306_WHITE);
  display.drawLine(92, 11, 76, 15, SSD1306_WHITE);

  // Mouth — downward frown
  display.drawLine(40, 50, 64, 46, SSD1306_WHITE);
  display.drawLine(64, 46, 88, 50, SSD1306_WHITE);

  // Flashing "EYES UP" text
  if ((e / 300) % 2 == 0) {
    display.setTextSize(1);
    display.setCursor(30, 56);
    display.print("EYES ON ROAD");
  }

  display.display();
}

void drawIdle() {
  display.clearDisplay();

  if (remoteSensor.hasData) {
    // ── Connected — normal happy face ──────────────────
    // Eyes — both level
    display.fillCircle(44, 22, 6, SSD1306_WHITE);
    display.fillCircle(84, 22, 6, SSD1306_WHITE);

    // Mouth — flat line
    display.drawLine(34, 44, 94, 44, SSD1306_WHITE);
    display.drawLine(34, 45, 94, 45, SSD1306_WHITE);

  } else {
    // ── Waiting — raised eyebrow skeptical face ────────
    display.fillCircle(44, 22, 6, SSD1306_WHITE);
    display.fillCircle(84, 22, 6, SSD1306_WHITE);

    // Left eyebrow — flat
    display.drawLine(36, 14, 52, 14, SSD1306_WHITE);
    display.drawLine(36, 13, 52, 13, SSD1306_WHITE);

    // Right eyebrow — angled up (raised)
    display.drawLine(76, 5, 92, 9, SSD1306_WHITE);
    display.drawLine(76, 4, 92, 8, SSD1306_WHITE);

    // Mouth — flat line, same position
    display.drawLine(54, 34, 74, 34, SSD1306_WHITE);
    display.drawLine(54, 35, 74, 35, SSD1306_WHITE);
  }

  display.display();
}

// ─── BOOT FACE ───────────────────────────────────────────
// Plays a little blinking animation before going to idle
void bootFace() {
  // Blink 3 times
  for (int blink = 0; blink < 2; blink++) {
    // Eyes open
    display.clearDisplay();
    display.fillCircle(44, 22, 6, SSD1306_WHITE);
    display.fillCircle(84, 22, 6, SSD1306_WHITE);
    display.drawLine(34, 44, 94, 44, SSD1306_WHITE);
    display.drawLine(34, 45, 94, 45, SSD1306_WHITE);
    display.display();
    delay(300);

    // Eyes closed — thin lines
    display.clearDisplay();
    display.drawLine(38, 22, 50, 22, SSD1306_WHITE);
    display.drawLine(38, 23, 50, 23, SSD1306_WHITE);
    display.drawLine(78, 22, 90, 22, SSD1306_WHITE);
    display.drawLine(78, 23, 90, 23, SSD1306_WHITE);
    display.drawLine(34, 44, 94, 44, SSD1306_WHITE);
    display.drawLine(34, 45, 94, 45, SSD1306_WHITE);
    display.display();
    delay(100);
  }

  // Final eyes open — hold for a moment then hand off to idle
  display.clearDisplay();
  display.fillCircle(44, 22, 6, SSD1306_WHITE);
  display.fillCircle(84, 22, 6, SSD1306_WHITE);
  display.drawLine(34, 44, 94, 44, SSD1306_WHITE);
  display.drawLine(34, 45, 94, 45, SSD1306_WHITE);
  display.display();
  delay(600);
}

void updateDisplay() {
  uint32_t now = millis();
  if (activeAnim != ANIM_NONE) {
    uint32_t e = now - animStart;
    if (e > ANIM_DURATION) {
      activeAnim = ANIM_NONE;
      digitalWrite(LED_PIN, LOW);
      drawIdle();
      return;
    }
    switch (activeAnim) {
      case ANIM_LANE:  drawLaneAnim(e);  break;
      case ANIM_BRAKE: drawBrakeAnim(e); break;
      case ANIM_HACC:  drawHaccAnim(e);  break;
      case ANIM_TURN:  drawTurnAnim(e);  break;
      case ANIM_HEAD:  drawHeadDownAnim(e); break;
      default: break;
    }
  } else {
    static uint32_t lastIdle = 0;
    if (now - lastIdle > 500) {
      lastIdle = now;
      drawIdle();
    }
  }
}

// ─── CALIBRATE ───────────────────────────────────────────
// Board mounted vertically: aX holds gravity (-9.81).
// offX zeroes out aX entirely. offY zeroes aY. offZ zeroes aZ.
void calibrate(SensorState &s, Adafruit_MPU6050 &sensor) {
  Serial.printf("Calibrating %s — keep still...\n", s.label);
  delay(500);
  float sX = 0, sY = 0, sZ = 0;
  for (int i = 0; i < 20; i++) {
    sensors_event_t a, g, t;
    sensor.getEvent(&a, &g, &t);
    sX += a.acceleration.x;
    sY += a.acceleration.y;
    sZ += a.acceleration.z;
    delay(50);
  }
  // Zero out all three axes — gravity on aX is removed along with tilt offsets
  s.offX = sX / 20.0f;
  s.offY = sY / 20.0f;
  s.offZ = sZ / 20.0f;
  Serial.printf("  %s offsets — X: %.4f  Y: %.4f  Z: %.4f\n",
                s.label, s.offX, s.offY, s.offZ);
}

// ─── WEBPAGE ─────────────────────────────────────────────
const char webpage[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Driver Monitor</title>
<style>
body{font-family:sans-serif;display:flex;flex-direction:column;align-items:center;
     justify-content:center;height:100vh;margin:0;background:#1a1a2e;color:#fff;gap:20px}
button{padding:16px 32px;font-size:1.1rem;border:none;border-radius:12px;
       background:#e94560;color:#fff;cursor:pointer}
button.hidden{display:none}
#status{color:#aaa;font-size:1rem;text-align:center;padding:0 20px}
#event{font-size:1.4rem;font-weight:bold;color:#e94560;min-height:2rem}
</style></head><body>
<div id="status">Press to unlock audio</div>
<div id="event"></div>
<button id="unlockBtn">🔊 Unlock Audio</button>
<script>
let audioCtx=null,currentSource=null;

document.getElementById('unlockBtn').addEventListener('click',()=>{
  audioCtx=new(window.AudioContext||window.webkitAudioContext)();
  const b=audioCtx.createBuffer(1,1,22050),s=audioCtx.createBufferSource();
  s.buffer=b;s.connect(audioCtx.destination);s.start(0);
  document.getElementById('unlockBtn').classList.add('hidden');
  document.getElementById('status').textContent='Monitoring drive...';
});

function playMP3(filename){
  if(!audioCtx){document.getElementById('status').textContent='Unlock audio first!';return;}
  audioCtx.resume().then(()=>{
    fetch('/files/'+filename)
      .then(r=>{if(!r.ok)throw new Error('Not found: '+filename);return r.arrayBuffer();})
      .then(buf=>audioCtx.decodeAudioData(buf))
      .then(decoded=>{
        if(currentSource){try{currentSource.stop();}catch(e){}}
        currentSource=audioCtx.createBufferSource();
        currentSource.buffer=decoded;
        currentSource.connect(audioCtx.destination);
        currentSource.start(0);
      })
      .catch(err=>{document.getElementById('status').textContent='MP3 error: '+err.message;});
  });
}

function playTone(f,d,t){
  if(!audioCtx)return;
  audioCtx.resume().then(()=>{
    const o=audioCtx.createOscillator(),g=audioCtx.createGain();
    o.connect(g);g.connect(audioCtx.destination);
    o.type=t||'sine';o.frequency.setValueAtTime(f,audioCtx.currentTime);
    g.gain.setValueAtTime(0.8,audioCtx.currentTime);
    g.gain.exponentialRampToValueAtTime(0.001,audioCtx.currentTime+d);
    o.start();o.stop(audioCtx.currentTime+d);
  });
}

const ws=new WebSocket(`ws://${location.hostname}/ws`);
ws.onmessage=(e)=>{
  const msg=e.data;
  document.getElementById('event').textContent=msg.startsWith('PLAY:')?msg.substring(5).replace('.mp3','').replace(/_/g,' '):msg;
  setTimeout(()=>{document.getElementById('event').textContent='';},2500);
  if(msg.startsWith('PLAY:'))playMP3(msg.substring(5));
  else if(msg==='BEEP')playTone(880,0.15,'sine');
};
ws.onclose=()=>{document.getElementById('status').textContent='Disconnected — reload.';};
</script></body></html>
)rawhtml";

// ─── SETUP ───────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("=== BOOT ===");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("SSD1306 not found — continuing without display.");
  } else {
    Serial.println("OLED ready.");
    bootFace(); 
  }

  // MPU
  if (!localMPU.begin()) {
    Serial.println("Local MPU6050 not found!");
    while (1);
  }
  localMPU.setAccelerometerRange(MPU6050_RANGE_2_G);
  localMPU.setGyroRange(MPU6050_RANGE_500_DEG);
  localMPU.setFilterBandwidth(MPU6050_BAND_21_HZ);
  calibrate(localSensor, localMPU);

  // LittleFS (MP3s live here)
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed — MP3s unavailable.");
  } else {
    Serial.println("LittleFS mounted.");
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while (file) {
      Serial.printf("  %s  (%d bytes)\n", file.name(), file.size());
      file = root.openNextFile();
    }
  }

  // WiFi AP + ESP-NOW
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL);
  Serial.printf("AP up — SSID: %s  IP: %s  ch: %d\n",
                AP_SSID, WiFi.softAPIP().toString().c_str(), AP_CHANNEL);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (1);
  }
  esp_now_register_recv_cb(onDataReceived);

  // Serve MP3s + webpage + WebSocket
  server.serveStatic("/files/", LittleFS, "/");
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", webpage);
  });
  ws.onEvent([](AsyncWebSocket* s, AsyncWebSocketClient* c,
                AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT)
      Serial.printf("Phone connected: #%u\n", c->id());
    else if (type == WS_EVT_DISCONNECT)
      Serial.printf("Phone disconnected: #%u\n", c->id());
  });
  server.addHandler(&ws);
  server.begin();

  Serial.println("=== READY — open http://192.168.4.1 on phone ===\n");
}

// ─── LOOP ────────────────────────────────────────────────
void loop() {
  uint32_t loopStart = millis();

  ws.cleanupClients();

  // Local sensor — gX passed as turn axis (board mounted vertically)
  sensors_event_t accel, gyro, temp;
  localMPU.getEvent(&accel, &gyro, &temp);
  updateSensor(localSensor,
               accel.acceleration.x,
               accel.acceleration.y,
               accel.acceleration.z,
               gyro.gyro.x);   // ← gX not gZ

  // Remote sensor
  if (newRemoteData) {
    newRemoteData = false;
    updateSensor(remoteSensor,
                 remotePacket.accel_x,
                 remotePacket.accel_y,
                 remotePacket.accel_z,
                 remotePacket.gyro_z);
  }

  checkMovement(localSensor.outX, localSensor.outY,
                localSensor.outZ, localSensor.outGZ);
  checkHeadMovement();

  // debug for head data
  if (remoteSensor.hasData) {
  Serial.printf("HEAD — outX=%.3f  outY=%.3f  outZ=%.3f\n",
                remoteSensor.outX, remoteSensor.outY, remoteSensor.outZ);
  }

  // Display
  updateDisplay();

  uint32_t elapsed = millis() - loopStart;
  if (elapsed < LOOP_MS) delay(LOOP_MS - elapsed);
}
