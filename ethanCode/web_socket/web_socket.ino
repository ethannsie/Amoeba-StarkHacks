#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char webpage[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Speaker</title>
  <style>
    body {
      font-family: sans-serif;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      height: 100vh;
      margin: 0;
      background: #1a1a2e;
      color: white;
      gap: 20px;
    }
    button {
      padding: 16px 32px;
      font-size: 1.1rem;
      border: none;
      border-radius: 12px;
      background: #e94560;
      color: white;
      cursor: pointer;
    }
    button.hidden { display: none; }
    #status { color: #aaa; font-size: 0.95rem; text-align: center; padding: 0 20px; }
  </style>
</head>
<body>
  <div id="status">Press to unlock audio</div>
  <button id="unlockBtn">🔊 Unlock Audio</button>

  <script>
    let audioCtx = null;
    let voices   = [];

    function loadVoices() {
      voices = window.speechSynthesis.getVoices();
    }
    window.speechSynthesis.onvoiceschanged = loadVoices;
    loadVoices();

    document.getElementById('unlockBtn').addEventListener('click', () => {
      audioCtx = new (window.AudioContext || window.webkitAudioContext)();

      // Silent buffer — fully unlocks AudioContext on iOS
      const buf = audioCtx.createBuffer(1, 1, 22050);
      const src = audioCtx.createBufferSource();
      src.buffer = buf;
      src.connect(audioCtx.destination);
      src.start(0);

      loadVoices();
      document.getElementById('unlockBtn').classList.add('hidden');
      document.getElementById('status').textContent = 'Ready — waiting for commands...';
    });

    // --- MP3 playback via AudioContext (no HTML5 Audio element needed) ---
    let currentSource = null;

    function playMP3(filename) {
      if (!audioCtx) {
        document.getElementById('status').textContent = 'Unlock audio first!';
        return;
      }
      audioCtx.resume().then(() => {
        fetch('/files/' + filename)
          .then(r => {
            if (!r.ok) throw new Error('File not found: ' + filename);
            return r.arrayBuffer();
          })
          .then(arrayBuf => audioCtx.decodeAudioData(arrayBuf))
          .then(decoded => {
            // Stop anything currently playing
            if (currentSource) {
              try { currentSource.stop(); } catch(e) {}
            }
            currentSource = audioCtx.createBufferSource();
            currentSource.buffer = decoded;
            currentSource.connect(audioCtx.destination);
            currentSource.start(0);
          })
          .catch(err => {
            document.getElementById('status').textContent = 'MP3 error: ' + err.message;
          });
      });
    }

    // --- Tone playback ---
    function playTone(freq, dur, type) {
      if (!audioCtx) return;
      audioCtx.resume().then(() => {
        const osc  = audioCtx.createOscillator();
        const gain = audioCtx.createGain();
        osc.connect(gain);
        gain.connect(audioCtx.destination);
        osc.type = type || 'sine';
        osc.frequency.setValueAtTime(freq, audioCtx.currentTime);
        gain.gain.setValueAtTime(0.8, audioCtx.currentTime);
        gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + dur);
        osc.start();
        osc.stop(audioCtx.currentTime + dur);
      });
    }

    function playPattern(notes) {
      if (!audioCtx) return;
      audioCtx.resume().then(() => {
        let time = audioCtx.currentTime;
        notes.forEach(([freq, dur, type]) => {
          const osc  = audioCtx.createOscillator();
          const gain = audioCtx.createGain();
          osc.connect(gain);
          gain.connect(audioCtx.destination);
          osc.type = type || 'sine';
          osc.frequency.setValueAtTime(freq, time);
          gain.gain.setValueAtTime(0.8, time);
          gain.gain.exponentialRampToValueAtTime(0.001, time + dur);
          osc.start(time);
          osc.stop(time + dur);
          time += dur + 0.05;
        });
      });
    }

    // --- TTS ---
    function sayText(text) {
      window.speechSynthesis.cancel();
      const utterance  = new SpeechSynthesisUtterance(text);
      utterance.rate   = 1.0;
      utterance.pitch  = 1.0;
      utterance.volume = 1.0;
      utterance.voice  = voices.find(v => v.lang === 'en-US') || voices[0] || null;
      window.speechSynthesis.speak(utterance);
    }

    // --- WebSocket ---
    const ws = new WebSocket(`ws://${location.hostname}/ws`);

    ws.onmessage = (e) => {
      const msg = e.data;
      document.getElementById('status').textContent = 'Received: ' + msg;

      if (msg.startsWith('PLAY:')) {
        playMP3(msg.substring(5));
      } else if (msg.startsWith('SAY:')) {
        sayText(msg.substring(4));
      } else if (msg.startsWith('TONE:')) {
        const [, freq, dur, type] = msg.split(':');
        playTone(parseFloat(freq), parseFloat(dur), type);
      } else if (msg === 'ALERT') {
        playPattern([[880,0.1],[880,0.1],[880,0.2]]);
      } else if (msg === 'ALARM') {
        playPattern([[1200,0.15],[900,0.15],[600,0.3]]);
      } else if (msg === 'SUCCESS') {
        playPattern([[523,0.1],[659,0.1],[784,0.2]]);
      } else if (msg === 'FAIL') {
        playPattern([[400,0.15],[300,0.3]]);
      } else if (msg === 'BEEP') {
        playTone(880, 0.15, 'sine');
      }
    };

    ws.onclose = () => {
      document.getElementById('status').textContent = 'Disconnected — reload page.';
    };
  </script>
</body>
</html>
)rawhtml";

void handleSerialCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd == "BEEP" || cmd == "ALERT" || cmd == "ALARM" || cmd == "SUCCESS" || cmd == "FAIL") {
    ws.textAll(cmd);
    Serial.println("Sent: " + cmd);
  } else if (cmd.startsWith("TONE:") || cmd.startsWith("SAY:") || cmd.startsWith("PLAY:")) {
    ws.textAll(cmd);
    Serial.println("Sent: " + cmd);
  } else if (cmd == "LIST") {
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    Serial.println("Files on LittleFS:");
    bool any = false;
    while (file) {
      Serial.printf("  %s  (%d bytes)\n", file.name(), file.size());
      file = root.openNextFile();
      any = true;
    }
    if (!any) Serial.println("  (none)");
  } else if (cmd.startsWith("DELETE:")) {
    String path = "/" + cmd.substring(7);
    if (LittleFS.remove(path)) {
      Serial.println("Deleted: " + path);
    } else {
      Serial.println("ERROR: Could not delete " + path);
    }
  } else {
    Serial.println("Unknown command. Valid commands:");
    Serial.println("  BEEP");
    Serial.println("  ALERT");
    Serial.println("  ALARM");
    Serial.println("  SUCCESS");
    Serial.println("  FAIL");
    Serial.println("  TONE:frequency:duration:type  e.g. TONE:440:1.0:square");
    Serial.println("  SAY:your text here            e.g. SAY:Hello world");
    Serial.println("  PLAY:filename.mp3             e.g. PLAY:alert.mp3");
    Serial.println("  LIST                          shows all files on ESP32");
    Serial.println("  DELETE:filename.mp3           deletes a file");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!LittleFS.begin(true)) {
    Serial.println("ERROR: LittleFS mount failed.");
  } else {
    Serial.println("LittleFS mounted.");
  }

  WiFi.softAP("ESP32-Speaker", "12345678");
  Serial.print("Connect to 'ESP32-Speaker' then open: http://");
  Serial.println(WiFi.softAPIP());

  // Serve MP3s from /files/
  server.serveStatic("/files/", LittleFS, "/");

  // Main webpage
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", webpage);
  });

  // Upload page
  server.on("/upload", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/html",
      "<!DOCTYPE html><html><body style='font-family:sans-serif;padding:2rem;background:#1a1a2e;color:white'>"
      "<h2>Upload MP3</h2>"
      "<form method='POST' action='/upload' enctype='multipart/form-data'>"
      "<input type='file' name='file' accept='.mp3' style='color:white'><br><br>"
      "<input type='submit' value='Upload' style='padding:10px 24px;background:#e94560;color:white;border:none;border-radius:8px;cursor:pointer'>"
      "</form>"
      "<br><a href='/' style='color:#aaa'>Back to main page</a>"
      "</body></html>"
    );
  });

  // Handle file upload
  server.on("/upload", HTTP_POST,
    [](AsyncWebServerRequest* req) {
      req->send(200, "text/html",
        "<!DOCTYPE html><html><body style='font-family:sans-serif;padding:2rem;background:#1a1a2e;color:white'>"
        "<h2>Upload complete!</h2>"
        "<a href='/upload' style='color:#e94560'>Upload another</a>"
        "&nbsp;&nbsp;"
        "<a href='/' style='color:#aaa'>Back to main page</a>"
        "</body></html>"
      );
    },
    [](AsyncWebServerRequest* req, String filename, size_t index, uint8_t* data, size_t len, bool final) {
      static File uploadFile;
      if (index == 0) {
        String path = "/" + filename;
        Serial.println("Uploading: " + path);
        uploadFile = LittleFS.open(path, "w");
        if (!uploadFile) Serial.println("ERROR: Could not open file for writing");
      }
      if (uploadFile) uploadFile.write(data, len);
      if (final && uploadFile) {
        uploadFile.close();
        Serial.printf("Upload complete: %s (%d bytes)\n", filename.c_str(), index + len);
      }
    }
  );

  ws.onEvent([](AsyncWebSocket* s, AsyncWebSocketClient* c,
                AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT)
      Serial.printf("Phone connected: client #%u\n", c->id());
    else if (type == WS_EVT_DISCONNECT)
      Serial.printf("Phone disconnected: client #%u\n", c->id());
  });

  server.addHandler(&ws);
  server.begin();
  Serial.println("Ready. Type a command into Serial Monitor.");
  Serial.println("To upload MP3s open: http://192.168.4.1/upload");
}

void loop() {
  ws.cleanupClients();

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    handleSerialCommand(cmd);
  }

  delay(10);
}