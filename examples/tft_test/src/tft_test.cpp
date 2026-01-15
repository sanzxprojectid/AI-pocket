/*
 * MINIMAL TFT TEST - Upload ini untuk cek hardware
 * Ini akan test backlight dan layar secara bertahap
 */

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// Pin definitions - PASTIKAN INI SESUAI HARDWARE ANDA
// Konfigurasi Original
#define TFT_CS    10
#define TFT_DC    9
#define TFT_RST   13
#define TFT_MOSI  11
#define TFT_SCLK  12
#define TFT_BL    14
#define TFT_MISO  -1

// JIKA TIDAK MUNCUL, UNCOMMENT SALAH SATU ALTERNATIF:

// Alternatif 1 (Common ESP32-S3 TFT config)
// #define TFT_CS    5
// #define TFT_DC    16
// #define TFT_RST   23
// #define TFT_MOSI  18
// #define TFT_SCLK  19
// #define TFT_BL    4
// #define TFT_MISO  -1

// Alternatif 2 (LilyGO T-Display S3 style)
// #define TFT_CS    6
// #define TFT_DC    7
// #define TFT_RST   5
// #define TFT_MOSI  11
// #define TFT_SCLK  12
// #define TFT_BL    38
// #define TFT_MISO  -1

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n\n");
  Serial.println("=================================");
  Serial.println("  MINIMAL TFT TEST - START");
  Serial.println("=================================\n");

  // ===== TEST 1: BACKLIGHT =====
  Serial.println("TEST 1: Backlight Test");
  Serial.println("Setting TFT_BL (pin 14) as OUTPUT...");
  pinMode(TFT_BL, OUTPUT);

  Serial.println("Turning backlight OFF...");
  digitalWrite(TFT_BL, LOW);
  delay(2000);

  Serial.println("Turning backlight ON...");
  digitalWrite(TFT_BL, HIGH);
  delay(2000);

  // JIKA BACKLIGHT TIDAK NYALA: Cek koneksi pin 14
  // JIKA BACKLIGHT NYALA PUTIH POLOS: Lanjut ke test 2

  Serial.println("✓ Backlight should be ON now\n");

  // ===== TEST 2: SPI INIT =====
  Serial.println("TEST 2: SPI Initialization");
  Serial.printf("MOSI: %d\n", TFT_MOSI);
  Serial.printf("SCLK: %d\n", TFT_SCLK);
  Serial.printf("MISO: %d\n", TFT_MISO);

  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  SPI.setFrequency(40000000);
  Serial.println("✓ SPI started at 40MHz\n");

  // ===== TEST 3: TFT INIT =====
  Serial.println("TEST 3: TFT Display Init");
  Serial.println("Calling tft.init(170, 320)...");

  tft.init(170, 320);
  Serial.println("✓ tft.init() completed");

  Serial.println("Setting rotation to 3 (landscape)...");
  tft.setRotation(3);
  Serial.println("✓ Rotation set\n");

  // ===== TEST 4: COLOR TESTS =====
  Serial.println("TEST 4: Color Fill Tests");
  Serial.println("(Watch the screen for color changes)");

  Serial.println("- Filling RED...");
  tft.fillScreen(ST77XX_RED);
  delay(2000);

  Serial.println("- Filling GREEN...");
  tft.fillScreen(ST77XX_GREEN);
  delay(2000);

  Serial.println("- Filling BLUE...");
  tft.fillScreen(ST77XX_BLUE);
  delay(2000);

  Serial.println("- Filling WHITE...");
  tft.fillScreen(ST77XX_WHITE);
  delay(2000);

  Serial.println("- Filling BLACK...");
  tft.fillScreen(ST77XX_BLACK);
  delay(1000);

  Serial.println("✓ Color test complete\n");

  // ===== TEST 5: TEXT DISPLAY =====
  Serial.println("TEST 5: Text Display");

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(3);
  tft.setCursor(10, 10);
  tft.println("TFT OK!");

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(10, 50);
  tft.println("Hardware Works");

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, 80);
  tft.println("Screen: 320x170");
  tft.setCursor(10, 95);
  tft.println("Rotation: 3 (Landscape)");

  Serial.println("✓ Text displayed\n");

  // ===== TEST 6: GRAPHICS =====
  Serial.println("TEST 6: Graphics Test");

  // Draw rectangles
  tft.drawRect(10, 120, 100, 40, ST77XX_RED);
  tft.fillRect(15, 125, 90, 30, ST77XX_YELLOW);

  // Draw circles
  tft.drawCircle(250, 140, 20, ST77XX_MAGENTA);
  tft.fillCircle(250, 140, 15, ST77XX_BLUE);

  Serial.println("✓ Graphics drawn\n");

  Serial.println("=================================");
  Serial.println("  ALL TESTS COMPLETED!");
  Serial.println("=================================\n");
  Serial.println("If you see colored screen and text,");
  Serial.println("your TFT hardware is working fine.");
  Serial.println("\nCheck Serial Monitor for results.");
}

void loop() {
  // Blink LED untuk konfirmasi ESP32 masih running
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 1000) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    lastBlink = millis();
    Serial.print(".");
  }
}
