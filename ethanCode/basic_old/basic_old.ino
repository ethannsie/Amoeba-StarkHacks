#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1      // no reset pin
#define OLED_ADDRESS  0x3C   // most 0.96" OLEDs use 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(8, 9); // SDA, SCL

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("ERROR: SSD1306 not found. Check wiring.");
    while (1) delay(10);
  }

  Serial.println("Display found!");

  // --- Screen 1: Welcome ---
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(10, 10);
  display.println("ESP32-S3");

  display.setTextSize(1);
  display.setCursor(10, 38);
  display.println("OLED display ready");
  display.setCursor(10, 50);
  display.println("Starting up...");

  display.display(); // push buffer to screen
  delay(2000);

  // --- Screen 2: Draw some shapes ---
  display.clearDisplay();

  // rectangle outline
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);

  // filled circle
  display.fillCircle(30, 32, 16, SSD1306_WHITE);

  // triangle
  display.drawTriangle(70, 10, 55, 54, 85, 54, SSD1306_WHITE);

  // line
  display.drawLine(100, 10, 120, 54, SSD1306_WHITE);

  display.display();
  delay(2000);
}

void loop() {
  // --- Scrolling counter ---
  static int count = 0;

  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Loop counter:");

  display.setTextSize(3);
  display.setCursor(20, 22);
  display.println(count);

  display.setTextSize(1);
  display.setCursor(0, 56);
  display.print("Uptime: ");
  display.print(millis() / 1000);
  display.print("s");

  display.display();

  count++;
  delay(100);
}