#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <time.h>

// ISRG Root X1 certificate for GitHub
const char* root_ca_github = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n" \
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n" \
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n" \
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n" \
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBg\n" \
"WDEwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDCp2mMbQ2dJpY4D5D/\n" \
"o4zbkVv5xDxPds9kSf2Ea20p+9aLxlUpk3vAMB/fmf203qLQo/uUfOaUOzxK/i7s\n" \
"m8Q0e/ZzH3Q5Y7tSPWTMb9l5nB/tM8jZDpm5u9lT7u5vKhT8s4a7sm9oJo2qXbC6\n" \
"nZ7tL6B6sEsm5ctGqVCCfE13f/dYSN6LEyCSWp86v6D/gscJDBKtdo1bT7M9AAmA\n" \
"xddgExbA5kHiqg0xKekGzS8rA5FmTwesvK/+3+03v4Y91qbY6eDB2GjlEwVvPhb/\n" \
"Z8i2s8dcfvK0b0bVMDEw1V3+z/d5qRBkgIm5uMA03EBY04yYjT34w9d300g7Wc+w\n" \
"N42pAgMBAAGjggFvMIIBazAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0TAQH/BAUwAwEB\n" \
"/zAdBgNVHQ4EFgQUVudqJS7K1x0v+qu+v0L2MAnP2nEwHwYDVR0jBBgwFoAUVudq\n" \
"JS7K1x0v+qu+v0L2MAnP2nEwVAYDVR0fBE0wSzBJoEegRYZDaHR0cDovL3gxLmku\n" \
"bW9yZS5kZW50cnVzdC5jb20vY3JsL2lzcmdyb290eDEuY3JsMDQGCCsGAQUFBwEB\n" \
"BCgwJjAkBggrBgEFBQcwAYYYaHR0cDovL2gxLmMubW9yZS5kZW50cnVzdC5jb20w\n" \
"VwYIKwYBBQUHAQEESzBJMB8GCCsGAQUFBzABhhNodHRwOi8veDEuOC5tb3JlLmRl\n" \
"bnRydXN0LmNvbTAoBggrBgEFBQcwAoYcaHR0cDovL2ExLmMubW9yZS5kZW50cnVz\n" \
"dC5jb20wGAYDVR0GAQQMAgYAMAkGA1UdEwQCMAAwDQYJKoZIhvcNAQELBQADggEB\n" \
"AHEqC+n3bC8G3d2b2jS2a5EK26sWd3Prmr1wRso3n7gJ1wE8oyk69P125IA+1y/k\n" \
"DA0+yMbYQMfweJ8L92z1yT4fG5j+2i8fCsSCWzUvY4Y/CgG1Lw4uWYIOlq4y/B5H\n" \
"4QRtjf0/lEmhBfJDkSaO7I+g24vksC3rI/Vz2a9xJRN3Lz0vjHAEiW+AToSCkKbu\n" \
"jU/hX0YOhf3VQTQWJfhdg4nZGGyA9e1PdoC8s/sJArLqTMhSAkYi/58+J/Fw2jCH\n" \
"S3FvEVwl3nWWDPBD2PTAI4LhLS2kFpWZ+eYJFLv27N8mEwK5jBfNl0GYfSGoVSR7\n" \
"Enq9LCUAMv2/0PMqP/3fR0M=\n" \
"-----END CERTIFICATE-----\n";

#define GITHUB_REPO "sanzxprojectid/AI-pocket"
#define FIRMWARE_ASSET_NAME "firmware.bin"

// OLED Display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define SDA_PIN 20
#define SCL_PIN 21
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Button pins
#define BTN_UP 5
#define BTN_DOWN 6
#define BTN_LEFT 3
#define BTN_RIGHT 4
#define BTN_SELECT 9
#define BTN_BACK 2

// LED Built-in
// #define LED_BUILTIN 8 // Defined in board variant

// Battery monitoring
#define BATTERY_PIN 0
#define CHARGING_PIN 1
#define BATTERY_MAX_VOLTAGE 4.2
#define BATTERY_MIN_VOLTAGE 3.3
#define VOLTAGE_DIVIDER_RATIO 2.0

// Dual Gemini API Keys
const char* geminiApiKey1 = "AIzaSyAtKmbcvYB8wuHI9bqkOhufJld0oSKv7zM";
const char* geminiApiKey2 = "AIzaSyBvXPx3SrrRRJIU9Wf6nKcuQu9XjBlSH6Y";
const char* geminiEndpoint = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash-exp:generateContent";

Preferences preferences;

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
const unsigned long wifiTimeout = 300000;
bool wifiAutoOffEnabled = false;

// Calculator
String calcDisplay = "0";
String calcNumber1 = "";
char calcOperator = ' ';
bool calcNewNumber = true;

// Battery monitoring
float batteryVoltage = 0.0;
int batteryPercent = 100;
bool isCharging = false;
unsigned long lastBatteryCheck = 0;
const unsigned long batteryCheckInterval = 2000;

// Battery Guardian
#define BATTERY_HISTORY_SIZE 60
int batteryHistory[BATTERY_HISTORY_SIZE];
unsigned long batteryTimestamps[BATTERY_HISTORY_SIZE];
int batteryHistoryIndex = 0;
bool batteryHistoryFilled = false;
float batteryDrainRate = 0;
int estimatedMinutesLeft = -1;
bool batteryLeakWarning = false;

// Power consumption estimation
float wifiPowerDraw = 0;
float displayPowerDraw = 0;
float cpuPowerDraw = 0;
float totalPowerDraw = 0;

// System stats
uint32_t freeHeap = 0;
uint32_t totalHeap = 0;
uint32_t minFreeHeap = 0;
float cpuFreq = 0;
float cpuTemp = 0;
bool lowMemoryWarning = false;
uint32_t loopCount = 0;
unsigned long lastLoopCount = 0;
unsigned long lastLoopTime = 0;

// ESP-NOW Messaging - FIXED STRUCTURE
#define MAX_ESP_PEERS 5
#define MAX_MESSAGES 10

struct Message {
  char text[200];
  uint8_t sender[6];
  unsigned long timestamp;
  bool read;
};
Message inbox[MAX_MESSAGES];
int inboxCount = 0;

struct PeerInfo {
  uint8_t macAddr[6];
  String name;
};
PeerInfo peers[MAX_ESP_PEERS];
int peerCount = 0;

// FIXED: Separated variables for different contexts
int espnowMenuSelection = 0;      // For ESP-NOW main menu
int espnowPeerSelection = 0;      // For peer list navigation
int espnowInboxSelection = 0;     // For inbox navigation
int espnowPeerOptionsSelection = 0; // For peer options menu

String outgoingMessage = "";

// Breadcrumb navigation
String breadcrumb = "";

int loadingFrame = 0;
unsigned long lastLoadingUpdate = 0;
int selectedAPIKey = 1;

// Transition effects
int transitionFrame = 0;
bool transitionActive = false;
enum TransitionType { TRANS_NONE, TRANS_SLIDE_LEFT, TRANS_SLIDE_RIGHT, TRANS_FADE };
TransitionType currentTransition = TRANS_NONE;

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

// NEW: Keyboard khusus untuk MAC address (hex only)
const char* keyboardMac[3][10] = {
  {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"},
  {"A", "B", "C", "D", "E", "F", "<", "<", "<", "<"},
  {"#", "#", "#", "#", "#", "#", "#", "#", " ", "OK"}
};

enum KeyboardMode { MODE_LOWER, MODE_UPPER, MODE_NUMBERS, MODE_MAC };
KeyboardMode currentKeyboardMode = MODE_LOWER;

// FIXED: Better keyboard context tracking
enum KeyboardContext {
  CONTEXT_CHAT,
  CONTEXT_ESPNOW_MESSAGE,
  CONTEXT_ESPNOW_PEER_NAME,
  CONTEXT_ESPNOW_MAC,
  CONTEXT_WIFI_PASSWORD
};
KeyboardContext keyboardContext = CONTEXT_CHAT;

enum AppState {
  STATE_WIFI_MENU,
  STATE_WIFI_SCAN,
  STATE_PASSWORD_INPUT,
  STATE_KEYBOARD,
  STATE_CHAT_RESPONSE,
  STATE_MAIN_MENU,
  STATE_CALCULATOR,
  STATE_POWER_BASE,
  STATE_POWER_VISUAL,
  STATE_POWER_STATS,
  STATE_POWER_GRAPH,
  STATE_POWER_CONSUMPTION,
  STATE_API_SELECT,
  STATE_SYSTEM_INFO,
  STATE_SETTINGS_MENU,
  STATE_BATTERY_GUARDIAN,
  STATE_ESP_NOW_MENU,
  STATE_ESP_NOW_SEND,
  STATE_ESP_NOW_INBOX,
  STATE_ESP_NOW_PEERS,
  STATE_ESP_NOW_PEER_OPTIONS,
  STATE_LOADING,
  STATE_OTA_UPDATE
};

AppState currentState = STATE_MAIN_MENU;
AppState previousState = STATE_MAIN_MENU;

// Main Menu Animation Variables
float menuScrollY = 0;
float menuTargetScrollY = 0;
int mainMenuSelection = 0;
float menuTextScrollX = 0;
unsigned long lastMenuTextScrollTime = 0;

int cursorX = 0, cursorY = 0;
String userInput = "";
String passwordInput = "";
String selectedSSID = "";
String aiResponse = "";
int scrollOffset = 0;
int menuSelection = 0;  // Generic menu selection
unsigned long lastDebounce = 0;
const unsigned long debounceDelay = 200;
int powerMenuSelection = 0;
int settingsMenuSelection = 0;

// Keyboard scroll variables
int textInputScrollOffset = 0;
unsigned long lastTextInputUpdate = 0;
const int textInputScrollSpeed = 200; // ms per character scroll

// Icons (8x8 pixel bitmaps)
const unsigned char ICON_WIFI[] PROGMEM = {
  0x00, 0x3C, 0x42, 0x99, 0x24, 0x00, 0x18, 0x00
};

const unsigned char ICON_CHAT[] PROGMEM = {
  0x7E, 0xFF, 0xC3, 0xC3, 0xC3, 0xFF, 0x18, 0x00
};

const unsigned char ICON_CALC[] PROGMEM = {
  0xFF, 0x81, 0x99, 0x81, 0x99, 0x81, 0xFF, 0x00
};

const unsigned char ICON_POWER[] PROGMEM = {
  0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E, 0x3C, 0x18
};

const unsigned char ICON_SETTINGS[] PROGMEM = {
  0x10, 0x38, 0x7C, 0xFE, 0x7C, 0x38, 0x10, 0x00
};

const unsigned char ICON_MESSAGE[] PROGMEM = {
  0x7E, 0x81, 0xA5, 0x81, 0xBD, 0x81, 0x7E, 0x00
};

// Forward declarations
void showMainMenu();
void showWiFiMenu();
void showCalculator();
void showPowerBase();
void showPowerVisual();
void showPowerStats();
void showPowerGraph();
void showPowerConsumption();
void showAPISelect();
void showSystemInfo();
void showSettingsMenu();
void showBatteryGuardian();
void showOTAUpdate();
void performOTAUpdate();
void showESPNowMenu();
void showESPNowSend();
void showESPNowInbox();
void showESPNowPeers();
void showESPNowPeerOptions();
void showLoadingAnimation();
void showProgressBar(String title, int percent);
void displayWiFiNetworks();
void drawKeyboard();
void handleMainMenuSelect();
void handleWiFiMenuSelect();
void handlePowerBaseSelect();
void handleAPISelectSelect();
void handleSettingsMenuSelect();
void handleKeyPress();
void handlePasswordKeyPress();
void handleMacAddressKeyPress();
void handleCalculatorInput();
void handleBackButton();
void connectToWiFi(String ssid, String password);
void scanWiFiNetworks();
void displayResponse();
void showProgressBar(String title, int percent);
void showStatus(String message, int delayMs);
void forgetNetwork();
void refreshCurrentScreen();
void calculateResult();
void resetCalculator();
void updateBatteryLevel();
void updateBatteryHistory();
void updateSystemStats();
void updatePowerConsumption();
void checkBatteryHealth();
void drawBatteryIndicator();
void drawWiFiSignalBars();
void drawChargingPlug();
void drawBreadcrumb();
void drawIcon(int x, int y, const unsigned char* icon);
void startTransition(TransitionType type);
void sendToGemini();
void checkWiFiTimeout();
const char* getCurrentKey();
void toggleKeyboardMode();
void onESPNowDataReceived(const esp_now_recv_info *info, const uint8_t *incomingData, int len);
void onESPNowDataSent(const uint8_t *mac, esp_now_send_status_t status);
void initESPNow();
void sendESPNowMessage(String message, uint8_t* targetMac);
void addPeer(uint8_t* macAddr, String name = "");
void deletePeer(int index);
void savePeers();
void loadPeers();

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

void ledPulse() {
  int pulse = (millis() / 10) % 512;
  if (pulse > 255) pulse = 511 - pulse;
  analogWrite(LED_BUILTIN, pulse);
}

void ledBlink(int speed) {
  digitalWrite(LED_BUILTIN, (millis() / speed) % 2);
}

void ledBreathing() {
  int breath = (millis() / 20) % 512;
  if (breath > 255) breath = 511 - breath;
  analogWrite(LED_BUILTIN, breath);
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

// FIXED: MAC address input handler dengan keyboard khusus
void handleMacAddressKeyPress() {
  const char* key = getCurrentKey();
  String mac = userInput;

  if (strcmp(key, "OK") == 0) {
    // Validasi format MAC address
    if (mac.length() == 17) {
      uint8_t macAddr[6];
      int MacValue[6];
      
      // Parse MAC address
      if (sscanf(mac.c_str(), "%x:%x:%x:%x:%x:%x", 
                 &MacValue[0], &MacValue[1], &MacValue[2], 
                 &MacValue[3], &MacValue[4], &MacValue[5]) == 6) {
        
        for(int i = 0; i < 6; ++i) {
          macAddr[i] = (uint8_t)MacValue[i];
        }
        
        // Check if peer already exists
        bool peerExists = false;
        for (int i = 0; i < peerCount; i++) {
          if (memcmp(peers[i].macAddr, macAddr, 6) == 0) {
            peerExists = true;
            break;
          }
        }
        
        if (peerExists) {
          ledError();
          showStatus("Peer already\nexists!", 2000);
          drawKeyboard();
        } else {
          addPeer(macAddr, "");
          ledSuccess();
          showStatus("Peer added!", 1500);
          currentState = STATE_ESP_NOW_PEERS;
          espnowPeerSelection = peerCount - 1;
          showESPNowPeers();
        }
      } else {
        ledError();
        showStatus("Invalid MAC\nformat!", 2000);
        drawKeyboard();
      }
    } else {
      ledError();
      showStatus("MAC incomplete!\n(need 17 chars)", 2000);
      drawKeyboard();
    }
  } else if (strcmp(key, "<") == 0) {
    if (mac.length() > 0) {
      // Handle backspace - remove character and colon if needed
      if (mac.endsWith(":")) {
        userInput.remove(userInput.length() - 2, 2);
      } else {
        userInput.remove(userInput.length() - 1);
      }
      textInputScrollOffset = 0; // Reset scroll on backspace
    }
    drawKeyboard();
  } else if (strcmp(key, "#") == 0) {
    // Do nothing for MAC input - no mode toggle needed
    drawKeyboard();
  } else if (strcmp(key, " ") == 0) {
    // Ignore space for MAC input
    drawKeyboard();
  } else {
    // Only allow hex characters for MAC address
    char c = key[0];
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) {
      if (mac.length() < 17) {
        userInput += c;
        
        // Auto-add colons at appropriate positions
        int len = userInput.length();
        if (len == 2 || len == 5 || len == 8 || len == 11 || len == 14) {
          userInput += ":";
        }
        
        textInputScrollOffset = 0; // Reset scroll on new input
      }
    }
    drawKeyboard();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== ESP32-C3 Ultra Edition v1.0 ===");
  Serial.println("Advanced Features Enabled");
  
  preferences.begin("wifi-creds", false);
  preferences.begin("settings", false);
  Wire.begin(SDA_PIN, SCL_PIN);
  
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  
  pinMode(BATTERY_PIN, INPUT);
  pinMode(CHARGING_PIN, INPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  
  // FIXED: Initialize battery with multiple readings for stability
  for (int i = 0; i < 5; i++) {
    updateBatteryLevel();
    delay(100);
  }
  
  // Initialize battery history
  for (int i = 0; i < BATTERY_HISTORY_SIZE; i++) {
    batteryHistory[i] = batteryPercent;
    batteryTimestamps[i] = millis();
  }
  
  // Load settings
  preferences.begin("settings", true); // Open read-only
  wifiAutoOffEnabled = preferences.getBool("wifiAutoOff", false);
  preferences.end();
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.display();
  
  ledSuccess();
  
  // Initialize ESP-NOW
  initESPNow();
  loadPeers();
  
  // Auto-connect WiFi
  preferences.begin("wifi-creds", true); // Open read-only
  String savedSSID = preferences.getString("ssid", "");
  String savedPassword = preferences.getString("password", "");
  preferences.end();
  
  if (savedSSID.length() > 0) {
    showProgressBar("WiFi Connect", 0);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      ledBlink(100);
      delay(500);
      attempts++;
      showProgressBar("Connecting", attempts * 5);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      ledSuccess();
      showProgressBar("Connected!", 100);
      delay(1000);
      lastWiFiActivity = millis();
    } else {
      ledError();
      showStatus("Connection failed\nOpening WiFi menu", 2000);
      currentState = STATE_WIFI_MENU;
    }
  } else {
    currentState = STATE_WIFI_MENU;
  }
  
  updateSystemStats();
  showMainMenu();
}

void loop() {
  unsigned long currentMillis = millis();
  
  loopCount++;
  
  // Battery monitoring
  if (currentMillis - lastBatteryCheck > batteryCheckInterval) {
    updateBatteryLevel();
    updateBatteryHistory();
    checkBatteryHealth();
    updatePowerConsumption();
    lastBatteryCheck = currentMillis;
    
    if (currentState != STATE_LOADING) {
      refreshCurrentScreen();
    }
  }
  
  // System stats update
  if (currentMillis - lastLoopTime > 1000) {
    lastLoopCount = loopCount;
    loopCount = 0;
    lastLoopTime = currentMillis;
    updateSystemStats();
  }
  
  // WiFi timeout check
  if (wifiAutoOffEnabled && WiFi.status() == WL_CONNECTED) {
    checkWiFiTimeout();
  }
  
  // Interactive LED Patterns
    switch(currentState) {
      case STATE_LOADING:
        ledPulse();
        break;
      case STATE_CHAT_RESPONSE:
        ledBreathing();
        break;
      case STATE_CALCULATOR:
        ledBlink(500);
        break;
      case STATE_POWER_BASE:
      case STATE_POWER_VISUAL:
      case STATE_POWER_STATS:
      case STATE_POWER_GRAPH:
      case STATE_POWER_CONSUMPTION:
        if (isCharging) {
          ledPulse();
        } else if (batteryPercent < 20) {
          ledBlink(250);
        } else {
          ledBreathing();
        }
        break;
      case STATE_SYSTEM_INFO:
        {
          int rainbow = (millis() / 100) % 30;
          digitalWrite(LED_BUILTIN, rainbow < 10 || (rainbow > 14 && rainbow < 20));
        }
        break;
      case STATE_BATTERY_GUARDIAN:
        if (batteryLeakWarning) {
          ledBlink(150);
        } else {
          ledBreathing();
        }
        break;
      case STATE_ESP_NOW_MENU:
      case STATE_ESP_NOW_SEND:
      case STATE_ESP_NOW_INBOX:
        {
          int pattern = (millis() / 200) % 6;
          digitalWrite(LED_BUILTIN, pattern < 3);
        }
        break;
      case STATE_MAIN_MENU:
      default:
        ledHeartbeat();
        break;
    }
  
  // Loading animation
  if (currentState == STATE_LOADING) {
    if (currentMillis - lastLoadingUpdate > 100) {
      lastLoadingUpdate = currentMillis;
      loadingFrame = (loadingFrame + 1) % 8;
      showLoadingAnimation();
    }
  }
  
  // NEW: Update keyboard text scrolling animation
  if (currentState == STATE_KEYBOARD || currentState == STATE_PASSWORD_INPUT) {
    int maxChars = 0;
    String text = "";
    
    if (keyboardContext == CONTEXT_WIFI_PASSWORD) {
      String masked = "";
      for (unsigned int i = 0; i < passwordInput.length(); i++) {
        masked += "*";
      }
      text = masked;
      maxChars = 14;
    } else if (keyboardContext == CONTEXT_ESPNOW_MAC) {
      text = userInput;
      maxChars = 13;
    } else {
      text = userInput;
      maxChars = 17;
    }
    
    if (text.length() > maxChars) {
      if (currentMillis - lastTextInputUpdate > textInputScrollSpeed) {
        drawKeyboard(); // Redraw to update scroll position
      }
    }
  }

  // Main Menu Animation Logic
  if (currentState == STATE_MAIN_MENU) {
    if (abs(menuScrollY - menuTargetScrollY) > 0.1) {
      menuScrollY += (menuTargetScrollY - menuScrollY) * 0.3;
      refreshCurrentScreen();
    } else if (menuScrollY != menuTargetScrollY) {
      menuScrollY = menuTargetScrollY;
      refreshCurrentScreen();
    }

    int newSelection = round(menuScrollY / 22.0);
    if (menuSelection != newSelection) {
        menuSelection = newSelection;
        lastMenuTextScrollTime = millis();
    }
  }
  
  // Button handling
  if (currentMillis - lastDebounce > debounceDelay) {
    bool buttonPressed = false;
    
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
    
    if (buttonPressed) {
      lastDebounce = currentMillis;
      ledQuickFlash();
      if (WiFi.status() == WL_CONNECTED) {
        lastWiFiActivity = currentMillis;
      }
    }
  }
}

// ========== ESP-NOW FUNCTIONS - FIXED ==========

void initESPNow() {
  WiFi.mode(WIFI_AP_STA);
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  
  esp_now_register_recv_cb(onESPNowDataReceived);
  esp_now_register_send_cb(onESPNowDataSent);
  
  Serial.println("ESP-NOW initialized");
  
  uint8_t mac[6];
  WiFi.macAddress(mac);
  Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void onESPNowDataReceived(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  const uint8_t* mac_addr = info->src_addr;
  if (inboxCount >= MAX_MESSAGES) {
    for (int i = 0; i < MAX_MESSAGES - 1; i++) {
      inbox[i] = inbox[i + 1];
    }
    inboxCount = MAX_MESSAGES - 1;
  }
  
  Message newMsg;
  strncpy(newMsg.text, (char*)incomingData, min(len, 199));
  newMsg.text[min(len, 199)] = '\0';
  memcpy(newMsg.sender, mac_addr, 6);
  newMsg.timestamp = millis();
  newMsg.read = false;
  
  inbox[inboxCount++] = newMsg;
  
  ledQuickFlash();
  Serial.println("Message received!");
}

void onESPNowDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    ledSuccess();
    showStatus("Message sent!", 1500);
  } else {
    ledError();
    showStatus("Send failed!", 1500);
  }
  
  if (currentState == STATE_ESP_NOW_SEND || currentState == STATE_KEYBOARD) {
    currentState = STATE_ESP_NOW_MENU;
    espnowMenuSelection = 0;
    showESPNowMenu();
  }
}

void addPeer(uint8_t* macAddr, String name) {
  if (peerCount >= MAX_ESP_PEERS) {
    ledError();
    showStatus("Peer list full!", 1500);
    return;
  }

  // Check if peer already exists
  for (int i = 0; i < peerCount; i++) {
    if (memcmp(peers[i].macAddr, macAddr, 6) == 0) {
      ledError();
      showStatus("Peer already\nexists!", 1500);
      return;
    }
  }
  
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, macAddr, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) == ESP_OK) {
    memcpy(peers[peerCount].macAddr, macAddr, 6);
    peers[peerCount].name = name;
    peerCount++;
    savePeers();
    ledSuccess();
    Serial.println("Peer added successfully");
  } else {
    ledError();
    Serial.println("Failed to add peer to ESP-NOW");
  }
}

void sendESPNowMessage(String message, uint8_t* targetMac) {
  if (message.length() == 0) return;
  
  esp_err_t result = esp_now_send(targetMac, (uint8_t*)message.c_str(), message.length());
  
  if (result != ESP_OK) {
    Serial.printf("Send error: %d\n", result);
  }
}

void deletePeer(int index) {
  if (index < 0 || index >= peerCount) return;

  if (esp_now_del_peer(peers[index].macAddr) != ESP_OK) {
    ledError();
    showStatus("Failed to delete\nfrom ESP-NOW", 2000);
    return;
  }

  for (int i = index; i < peerCount - 1; i++) {
    peers[i] = peers[i + 1];
  }
  peerCount--;

  savePeers();
  ledSuccess();
  
  // FIXED: Better selection management after deletion
  if (espnowPeerSelection >= peerCount && peerCount > 0) {
    espnowPeerSelection = peerCount - 1;
  } else if (peerCount == 0) {
    espnowPeerSelection = 0;
  }
}

void savePeers() {
  preferences.begin("esp-now-peers", false);
  preferences.putInt("peer_count", peerCount);
  
  for (int i = 0; i < peerCount; i++) {
    String macKey = "peer_" + String(i) + "_mac";
    String nameKey = "peer_" + String(i) + "_name";
    preferences.putBytes(macKey.c_str(), peers[i].macAddr, 6);
    preferences.putString(nameKey.c_str(), peers[i].name);
  }
  preferences.end();
}

void loadPeers() {
  preferences.begin("esp-now-peers", true);
  peerCount = preferences.getInt("peer_count", 0);
  peerCount = min(peerCount, MAX_ESP_PEERS);
  
  for (int i = 0; i < peerCount; i++) {
    String macKey = "peer_" + String(i) + "_mac";
    String nameKey = "peer_" + String(i) + "_name";
    preferences.getBytes(macKey.c_str(), peers[i].macAddr, 6);
    peers[i].name = preferences.getString(nameKey.c_str(), "");

    // Re-register peer with ESP-NOW
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, peers[i].macAddr, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    
    if (esp_now_is_peer_exist(peers[i].macAddr)) {
        esp_now_del_peer(peers[i].macAddr);
    }
    esp_now_add_peer(&peerInfo);
  }
  preferences.end();
  
  Serial.printf("Loaded %d peers\n", peerCount);
}

// FIXED: ESP-NOW Menu dengan selection yang jelas
void showESPNowMenu() {
  display.clearDisplay();
  drawBatteryIndicator();
  drawBreadcrumb();
  
  display.setTextSize(1);
  display.setCursor(20, 12);
  display.print("ESP-NOW MESH");
  
  drawIcon(5, 12, ICON_MESSAGE);
  
  const char* menuItems[] = {
    "Send Message",
    "Inbox",
    "Manage Peers",
    "Back"
  };
  
  for (int i = 0; i < 4; i++) {
    display.setCursor(10, 26 + i * 10);
    if (i == espnowMenuSelection) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    
    display.print(menuItems[i]);
    
    if (i == 1) {  // Inbox
      display.print(" (");
      display.print(inboxCount);
      display.print(")");
    }
  }
  
  display.setCursor(85, 56);
  display.print("Peers:");
  display.print(peerCount);
  
  display.display();
}

// FIXED: Peer Options Menu yang lebih jelas
void showESPNowPeerOptions() {
  display.clearDisplay();
  drawBatteryIndicator();
  drawBreadcrumb();

  display.setTextSize(1);
  display.setCursor(20, 2);
  display.print("PEER OPTIONS");

  display.drawLine(0, 12, SCREEN_WIDTH, 12, SSD1306_WHITE);

  // Show peer info
  display.setCursor(2, 15);
  if (peers[espnowPeerSelection].name.length() > 0) {
    String name = peers[espnowPeerSelection].name;
    if (name.length() > 20) {
      name = name.substring(0, 20);
    }
    display.print(name);
  } else {
    display.print("Unnamed Peer");
  }
  
  display.setCursor(2, 24);
  display.printf("%02X:%02X:%02X:%02X:%02X:%02X",
                peers[espnowPeerSelection].macAddr[0], 
                peers[espnowPeerSelection].macAddr[1],
                peers[espnowPeerSelection].macAddr[2], 
                peers[espnowPeerSelection].macAddr[3],
                peers[espnowPeerSelection].macAddr[4], 
                peers[espnowPeerSelection].macAddr[5]);

  const char* menuItems[] = {"Rename", "Delete", "Back"};
  for (int i = 0; i < 3; i++) {
    int y = 36 + i * 10;
    display.setCursor(5, y);
    if (i == espnowPeerOptionsSelection) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    display.print(menuItems[i]);
  }

  display.display();
}

void showESPNowSend() {
  display.clearDisplay();
  drawBatteryIndicator();
  drawBreadcrumb();
  
  display.setTextSize(1);
  display.setCursor(15, 2);
  display.print("SEND MESSAGE");
  
  display.drawLine(0, 12, SCREEN_WIDTH, 12, SSD1306_WHITE);
  
  if (peerCount == 0) {
    display.setCursor(10, 25);
    display.print("No peers added!");
    display.setCursor(5, 40);
    display.print("Add in Manage Peers");
  } else {
    display.setCursor(5, 15);
    display.print("Select peer:");
    
    int startIdx = max(0, espnowPeerSelection - 1);
    int endIdx = min(peerCount, startIdx + 3);
    
    for (int i = startIdx; i < endIdx; i++) {
      int y = 28 + (i - startIdx) * 10;
      
      if (i == espnowPeerSelection) {
        display.fillRect(0, y - 1, SCREEN_WIDTH, 10, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }
      
      display.setCursor(5, y);
      
      if (peers[i].name.length() > 0) {
        String name = peers[i].name;
        if (name.length() > 18) {
          name = name.substring(0, 18);
        }
        display.print(name);
      } else {
        display.printf("%02X:%02X:%02X:%02X",
                      peers[i].macAddr[0], peers[i].macAddr[1],
                      peers[i].macAddr[2], peers[i].macAddr[3]);
      }
      
      display.setTextColor(SSD1306_WHITE);
    }
  }
  
  display.display();
}

void showESPNowInbox() {
  display.clearDisplay();
  drawBatteryIndicator();
  drawBreadcrumb();
  
  display.setTextSize(1);
  display.setCursor(35, 2);
  display.print("INBOX (");
  display.print(inboxCount);
  display.print(")");
  
  display.drawLine(0, 12, SCREEN_WIDTH, 12, SSD1306_WHITE);
  
  if (inboxCount == 0) {
    display.setCursor(25, 30);
    display.print("No messages");
  } else {
    int startIdx = max(0, espnowInboxSelection - 1);
    int endIdx = min(inboxCount, startIdx + 4);
    
    for (int i = startIdx; i < endIdx; i++) {
      int y = 15 + (i - startIdx) * 12;
      
      if (i == espnowInboxSelection) {
        display.fillRect(0, y, SCREEN_WIDTH, 11, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }
      
      display.setCursor(2, y + 2);
      if (!inbox[i].read) {
        display.print("* ");
      } else {
        display.print("  ");
      }
      
      String msgPreview = String(inbox[i].text);
      if (msgPreview.length() > 16) {
        msgPreview = msgPreview.substring(0, 16) + "..";
      }
      display.print(msgPreview);
      
      display.setTextColor(SSD1306_WHITE);
    }
  }
  
  display.display();
}

// FIXED: Peer management dengan navigasi yang lebih baik
void showESPNowPeers() {
  display.clearDisplay();
  drawBatteryIndicator();
  drawBreadcrumb();

  display.setTextSize(1);
  display.setCursor(20, 2);
  display.print("MANAGE PEERS");

  display.drawLine(0, 12, SCREEN_WIDTH, 12, SSD1306_WHITE);

  if (peerCount == 0) {
    display.setCursor(30, 25);
    display.print("No peers");
    display.setCursor(20, 36);
    display.print("Press SELECT");
    display.setCursor(25, 45);
    display.print("to add peer");
  } else {
    // Display peers with scrolling
    int startIdx = max(0, espnowPeerSelection - 2);
    int endIdx = min(peerCount, startIdx + 4);
    
    for (int i = startIdx; i < endIdx; i++) {
      int y = 15 + (i - startIdx) * 10;
      
      if (i == espnowPeerSelection) {
        display.fillRect(0, y, SCREEN_WIDTH, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }
      
      display.setCursor(2, y + 1);
      if (peers[i].name.length() > 0) {
        String name = peers[i].name;
        if (name.length() > 18) {
          name = name.substring(0, 18);
        }
        display.print(name);
      } else {
        display.printf("%02X:%02X:%02X:%02X:%02X:%02X",
                      peers[i].macAddr[0], peers[i].macAddr[1],
                      peers[i].macAddr[2], peers[i].macAddr[3],
                      peers[i].macAddr[4], peers[i].macAddr[5]);
      }
      display.setTextColor(SSD1306_WHITE);
    }
  }

  // Show controls at bottom
  display.setCursor(2, 56);
  display.print("SEL:Options ADD:Back");

  display.display();
}

// ========== BATTERY GUARDIAN ==========

void updateBatteryHistory() {
  batteryHistory[batteryHistoryIndex] = batteryPercent;
  batteryTimestamps[batteryHistoryIndex] = millis();
  
  batteryHistoryIndex = (batteryHistoryIndex + 1) % BATTERY_HISTORY_SIZE;
  if (batteryHistoryIndex == 0) batteryHistoryFilled = true;
}

void checkBatteryHealth() {
  if (!batteryHistoryFilled && batteryHistoryIndex < 5) return;
  
  int dataPoints = batteryHistoryFilled ? BATTERY_HISTORY_SIZE : batteryHistoryIndex;
  if (dataPoints < 5) return;
  
  int oldIdx = batteryHistoryFilled ? batteryHistoryIndex : 0;
  int recentIdx = batteryHistoryFilled ? 
                  (batteryHistoryIndex - 1 + BATTERY_HISTORY_SIZE) % BATTERY_HISTORY_SIZE :
                  batteryHistoryIndex - 1;
  
  int batteryDiff = batteryHistory[oldIdx] - batteryHistory[recentIdx];
  unsigned long timeDiff = (batteryTimestamps[recentIdx] - batteryTimestamps[oldIdx]) / 60000;
  
  if (timeDiff > 0) {
    batteryDrainRate = (float)batteryDiff / timeDiff;
    
    if (batteryDrainRate > 2.0 && !isCharging) {
      batteryLeakWarning = true;
    } else {
      batteryLeakWarning = false;
    }
    
    if (batteryDrainRate > 0 && !isCharging) {
      estimatedMinutesLeft = (int)(batteryPercent / batteryDrainRate);
    } else if (isCharging && batteryDrainRate < 0) {
      estimatedMinutesLeft = (int)((100 - batteryPercent) / abs(batteryDrainRate));
    } else {
      estimatedMinutesLeft = -1;
    }
  }
}

void showBatteryGuardian() {
  display.clearDisplay();
  drawBatteryIndicator();
  drawBreadcrumb();
  
  display.setTextSize(1);
  display.setCursor(5, 12);
  display.print("BATTERY GUARDIAN");
  
  drawIcon(108, 12, ICON_POWER);
  
  display.drawLine(0, 22, SCREEN_WIDTH, 22, SSD1306_WHITE);
  
  display.setCursor(2, 26);
  display.print("Level: ");
  display.print(batteryPercent);
  display.print("% (");
  display.print(batteryVoltage, 2);
  display.print("V)");
  
  display.setCursor(2, 36);
  display.print("Drain: ");
  if (isCharging) {
    display.print("+");
    display.print(abs(batteryDrainRate), 2);
    display.print("%/min");
  } else {
    display.print(batteryDrainRate, 2);
    display.print("%/min");
  }
  
  display.setCursor(2, 46);
  if (estimatedMinutesLeft > 0) {
    if (isCharging) {
      display.print("Full in: ");
    } else {
      display.print("Empty in: ");
    }
    
    int hours = estimatedMinutesLeft / 60;
    int mins = estimatedMinutesLeft % 60;
    
    if (hours > 0) {
      display.print(hours);
      display.print("h ");
    }
    display.print(mins);
    display.print("m");
  } else {
    display.print("Time: Calculating...");
  }
  
  if (batteryLeakWarning) {
    if ((millis() / 500) % 2 == 0) {
      display.fillRect(0, 56, SCREEN_WIDTH, 8, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(15, 57);
      display.print("BATTERY LEAK!");
      display.setTextColor(SSD1306_WHITE);
    }
  }
  
  display.display();
}

// ========== POWER CONSUMPTION MONITOR ==========

void updatePowerConsumption() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiPowerDraw = 80;
  } else if (WiFi.getMode() != WIFI_OFF) {
    wifiPowerDraw = 120;
  } else {
    wifiPowerDraw = 0;
  }
  
  displayPowerDraw = 20;
  cpuPowerDraw = map(cpuFreq, 80, 160, 30, 60);
  totalPowerDraw = wifiPowerDraw + displayPowerDraw + cpuPowerDraw;
}

void showPowerConsumption() {
  display.clearDisplay();
  drawBatteryIndicator();
  drawBreadcrumb();
  
  display.setTextSize(1);
  display.setCursor(10, 2);
  display.print("POWER MONITOR");
  
  display.drawLine(0, 12, SCREEN_WIDTH, 12, SSD1306_WHITE);
  
  display.setCursor(2, 15);
  display.print("WiFi:");
  display.setCursor(70, 15);
  display.print(wifiPowerDraw, 0);
  display.print(" mA");
  
  display.setCursor(2, 25);
  display.print("Display:");
  display.setCursor(70, 25);
  display.print(displayPowerDraw, 0);
  display.print(" mA");
  
  display.setCursor(2, 35);
  display.print("CPU:");
  display.setCursor(70, 35);
  display.print(cpuPowerDraw, 0);
  display.print(" mA");
  
  display.drawLine(2, 43, SCREEN_WIDTH - 2, 43, SSD1306_WHITE);
  
  display.setCursor(2, 46);
  display.print("TOTAL:");
  display.setCursor(70, 46);
  display.print(totalPowerDraw, 0);
  display.print(" mA");
  
  int graphY = 56;
  int graphW = 126;
  
  if (totalPowerDraw > 0) {
    int wifiBar = map(wifiPowerDraw, 0, totalPowerDraw, 0, graphW);
    int displayBar = map(displayPowerDraw, 0, totalPowerDraw, 0, graphW);
    int cpuBar = map(cpuPowerDraw, 0, totalPowerDraw, 0, graphW);
    
    display.fillRect(1, graphY, wifiBar, 3, SSD1306_WHITE);
    display.fillRect(1, graphY + 3, displayBar, 2, SSD1306_WHITE);
    display.fillRect(1, graphY + 5, cpuBar, 2, SSD1306_WHITE);
  }
  
  display.display();
}

// ========== SYSTEM INFO ==========

void updateSystemStats() {
  freeHeap = ESP.getFreeHeap();
  totalHeap = ESP.getHeapSize();
  minFreeHeap = ESP.getMinFreeHeap();
  cpuFreq = ESP.getCpuFreqMHz();
  cpuTemp = temperatureRead();
  
  if (freeHeap < 20000) {
    lowMemoryWarning = true;
  } else {
    lowMemoryWarning = false;
  }
}

void showSystemInfo() {
  display.clearDisplay();
  drawBatteryIndicator();
  drawBreadcrumb();
  
  display.setTextSize(1);
  display.setCursor(15, 2);
  display.print("SYSTEM INFO");
  
  display.drawLine(0, 12, SCREEN_WIDTH, 12, SSD1306_WHITE);
  
  display.setCursor(2, 15);
  display.print("Free Heap:");
  display.setCursor(70, 15);
  display.print(freeHeap / 1024);
  display.print(" KB");
  
  if (lowMemoryWarning) {
    display.setCursor(110, 15);
    display.print("!");
  }
  
  display.setCursor(2, 24);
  display.print("Total:");
  display.setCursor(70, 24);
  display.print(totalHeap / 1024);
  display.print(" KB");
  
  display.setCursor(2, 33);
  display.print("CPU Freq:");
  display.setCursor(70, 33);
  display.print((int)cpuFreq);
  display.print(" MHz");
  
  display.setCursor(2, 42);
  display.print("CPU Temp:");
  display.setCursor(70, 42);
  display.print(cpuTemp, 1);
  display.print(" C");
  
  display.setCursor(2, 51);
  display.print("Loop/sec:");
  display.setCursor(70, 51);
  display.print(lastLoopCount);
  
  if (lowMemoryWarning && (millis() / 500) % 2 == 0) {
    display.fillRect(0, 58, SCREEN_WIDTH, 6, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(25, 59);
    display.print("LOW MEMORY");
    display.setTextColor(SSD1306_WHITE);
  }
  
  display.display();
}

// ========== SETTINGS MENU ==========

void showSettingsMenu() {
  display.clearDisplay();
  drawBatteryIndicator();
  drawBreadcrumb();
  
  display.setTextSize(1);
  display.setCursor(25, 2);
  display.print("SETTINGS");
  
  drawIcon(10, 2, ICON_SETTINGS);
  
  display.drawLine(0, 12, SCREEN_WIDTH, 12, SSD1306_WHITE);
  
  const char* menuItems[] = {
    "WiFi Auto-Off",
    "System Info",
    "OTA Update",
    "Back"
  };
  
  const int numItems = sizeof(menuItems) / sizeof(menuItems[0]);
  int visibleItems = 5;
  int startIdx = max(0, settingsMenuSelection - 2);
  int endIdx = min(numItems, startIdx + visibleItems);
  
  for (int i = startIdx; i < endIdx; i++) {
    int y = 15 + (i - startIdx) * 10;
    display.setCursor(5, y);
    
    if (i == settingsMenuSelection) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    display.print(menuItems[i]);
    
    if (i == 0) {
      display.setCursor(95, y);
      display.print(wifiAutoOffEnabled ? "[ON]" : "[OFF]");
    }
  }
  
  display.display();
}

void handleSettingsMenuSelect() {
  switch(settingsMenuSelection) {
    case 0:
      wifiAutoOffEnabled = !wifiAutoOffEnabled;
      preferences.begin("settings", false); // Open for writing
      preferences.putBool("wifiAutoOff", wifiAutoOffEnabled);
      preferences.end();
      ledSuccess();
      if (wifiAutoOffEnabled) {
        lastWiFiActivity = millis();
        showStatus("WiFi Auto-Off\nENABLED", 1500);
      } else {
        showStatus("WiFi Auto-Off\nDISABLED", 1500);
      }
      showSettingsMenu();
      break;
    case 1:
      previousState = currentState;
      currentState = STATE_SYSTEM_INFO;
      showSystemInfo();
      break;
    case 2:
      performOTAUpdate();
      break;
    case 3:
      currentState = STATE_MAIN_MENU;
      menuSelection = mainMenuSelection;
      menuTargetScrollY = mainMenuSelection * 22;
      showMainMenu();
      break;
  }
}

// ========== BREADCRUMB & ICONS ==========

void drawBreadcrumb() {
  String crumb = "";
  
  switch(currentState) {
    case STATE_MAIN_MENU:
      crumb = "Main";
      break;
    case STATE_WIFI_MENU:
    case STATE_WIFI_SCAN:
    case STATE_PASSWORD_INPUT:
      crumb = "Main > WiFi";
      break;
    case STATE_CALCULATOR:
      crumb = "Main > Calc";
      break;
    case STATE_POWER_BASE:
    case STATE_POWER_VISUAL:
    case STATE_POWER_STATS:
    case STATE_POWER_GRAPH:
    case STATE_POWER_CONSUMPTION:
      crumb = "Main > Power";
      break;
    case STATE_SETTINGS_MENU:
    case STATE_SYSTEM_INFO:
      crumb = "Main > Settings";
      break;
    case STATE_BATTERY_GUARDIAN:
      crumb = "Main > Battery";
      break;
    case STATE_ESP_NOW_MENU:
    case STATE_ESP_NOW_SEND:
    case STATE_ESP_NOW_INBOX:
    case STATE_ESP_NOW_PEERS:
    case STATE_ESP_NOW_PEER_OPTIONS:
      crumb = "Main > ESP-NOW";
      break;
    case STATE_CHAT_RESPONSE:
    case STATE_KEYBOARD:
    case STATE_API_SELECT:
      crumb = "Main > Chat";
      break;
    default:
      crumb = "";
  }
}

void drawIcon(int x, int y, const unsigned char* icon) {
  for (int i = 0; i < 8; i++) {
    byte row = pgm_read_byte(&icon[i]);
    for (int j = 0; j < 8; j++) {
      if (row & (1 << (7 - j))) {
        display.drawPixel(x + j, y + i, SSD1306_WHITE);
      }
    }
  }
}

// ========== MAIN MENU ==========

void showMainMenu() {
  display.clearDisplay();
  drawBatteryIndicator();
  
  if (WiFi.status() == WL_CONNECTED) {
    drawWiFiSignalBars();
  }
  
  struct MenuItem {
    const char* text;
    const unsigned char* icon;
  };
  
  MenuItem menuItems[] = {
    {"Chat AI", ICON_CHAT},
    {"WiFi", ICON_WIFI},
    {"Calculator", ICON_CALC},
    {"Power", ICON_POWER},
    {"Battery Guard", ICON_POWER},
    {"ESP-NOW", ICON_MESSAGE},
    {"Settings", ICON_SETTINGS}
  };
  
  int numItems = sizeof(menuItems) / sizeof(MenuItem);
  int screenCenterY = SCREEN_HEIGHT / 2;
  
  for (int i = 0; i < numItems; i++) {
    float itemY = screenCenterY + (i * 22) - menuScrollY;
    float distance = abs(itemY - screenCenterY);
    
    if (itemY > -20 && itemY < SCREEN_HEIGHT + 20) {
      if (i == menuSelection) {
        display.drawRoundRect(2, screenCenterY - 11, SCREEN_WIDTH - 4, 22, 5, SSD1306_WHITE);
        drawIcon(8, screenCenterY - 4, menuItems[i].icon);

        display.setTextSize(2);
        display.setTextWrap(false);

        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(menuItems[i].text, 0, 0, &x1, &y1, &w, &h);

        if (w > SCREEN_WIDTH - 30) {
            if (millis() - lastMenuTextScrollTime > 15) {
                menuTextScrollX--;
                if (menuTextScrollX <= -(w + 10)) {
                    menuTextScrollX += (w + 10);
                }
                lastMenuTextScrollTime = millis();
            }
            display.setCursor(22 + menuTextScrollX, screenCenterY - 6);
            display.print(menuItems[i].text);
            display.setCursor(22 + menuTextScrollX + w + 10, screenCenterY - 6);
            display.print(menuItems[i].text);
        } else {
            display.setCursor(22, screenCenterY - 6);
            display.print(menuItems[i].text);
        }

        display.setTextWrap(true);
      } else {
        display.setTextSize(1);
        int textX = 22 + (distance / 2);
        if (textX > SCREEN_WIDTH / 2) textX = SCREEN_WIDTH / 2;

        display.setCursor(textX, itemY - 3);
        display.print(menuItems[i].text);
        drawIcon(textX - 14, itemY - 4, menuItems[i].icon);
      }
    }
  }
  
  display.display();
}

void handleMainMenuSelect() {
  mainMenuSelection = menuSelection;
  switch(mainMenuSelection) {
    case 0: // Chat AI
      if (WiFi.status() == WL_CONNECTED) {
        menuSelection = 0;
        previousState = currentState;
        currentState = STATE_API_SELECT;
        showAPISelect();
      } else {
        ledError();
        showStatus("WiFi not connected!\nGo to WiFi Settings", 2000);
        showMainMenu();
      }
      break;
    case 1: // WiFi
      menuSelection = 0;
      previousState = currentState;
      currentState = STATE_WIFI_MENU;
      showWiFiMenu();
      break;
    case 2: // Calculator
      resetCalculator();
      previousState = currentState;
      currentState = STATE_CALCULATOR;
      cursorX = 0;
      cursorY = 0;
      showCalculator();
      break;
    case 3: // Power
      powerMenuSelection = 0;
      previousState = currentState;
      currentState = STATE_POWER_BASE;
      showPowerBase();
      break;
    case 4: // Battery Guardian
      previousState = currentState;
      currentState = STATE_BATTERY_GUARDIAN;
      showBatteryGuardian();
      break;
    case 5: // ESP-NOW
      espnowMenuSelection = 0;
      previousState = currentState;
      currentState = STATE_ESP_NOW_MENU;
      showESPNowMenu();
      break;
    case 6: // Settings
      settingsMenuSelection = 0;
      previousState = currentState;
      currentState = STATE_SETTINGS_MENU;
      showSettingsMenu();
      break;
  }
}

// ========== BACK BUTTON HANDLER - FIXED ==========

void handleBackButton() {
  ledQuickFlash();
  
  switch(currentState) {
    case STATE_MAIN_MENU:
      break;
      
    case STATE_WIFI_MENU:
    case STATE_CALCULATOR:
    case STATE_POWER_BASE:
    case STATE_SETTINGS_MENU:
    case STATE_BATTERY_GUARDIAN:
      currentState = STATE_MAIN_MENU;
      menuSelection = mainMenuSelection;
      menuTargetScrollY = mainMenuSelection * 22;
      showMainMenu();
      break;
      
    case STATE_ESP_NOW_MENU:
      currentState = STATE_MAIN_MENU;
      menuSelection = mainMenuSelection;
      menuTargetScrollY = mainMenuSelection * 22;
      showMainMenu();
      break;
      
    case STATE_WIFI_SCAN:
    case STATE_PASSWORD_INPUT:
      currentState = STATE_WIFI_MENU;
      menuSelection = 0;
      showWiFiMenu();
      break;
      
    case STATE_POWER_VISUAL:
    case STATE_POWER_STATS:
    case STATE_POWER_GRAPH:
    case STATE_POWER_CONSUMPTION:
      currentState = STATE_POWER_BASE;
      powerMenuSelection = 0;
      showPowerBase();
      break;
      
    case STATE_SYSTEM_INFO:
      currentState = STATE_SETTINGS_MENU;
      settingsMenuSelection = 0;
      showSettingsMenu();
      break;
      
    case STATE_ESP_NOW_SEND:
    case STATE_ESP_NOW_INBOX:
    case STATE_ESP_NOW_PEERS:
      currentState = STATE_ESP_NOW_MENU;
      espnowMenuSelection = 0;
      showESPNowMenu();
      break;
      
    case STATE_ESP_NOW_PEER_OPTIONS:
      currentState = STATE_ESP_NOW_PEERS;
      showESPNowPeers();
      break;

    case STATE_KEYBOARD:
      // FIXED: Better context-aware back handling
      if (keyboardContext == CONTEXT_ESPNOW_MESSAGE) {
        currentState = STATE_ESP_NOW_MENU;
        espnowMenuSelection = 0;
        showESPNowMenu();
      } else if (keyboardContext == CONTEXT_ESPNOW_PEER_NAME) {
        currentState = STATE_ESP_NOW_PEER_OPTIONS;
        showESPNowPeerOptions();
      } else if (keyboardContext == CONTEXT_ESPNOW_MAC) {
        currentState = STATE_ESP_NOW_PEERS;
        showESPNowPeers();
      } else {
        currentState = STATE_MAIN_MENU;
        menuSelection = mainMenuSelection;
        menuTargetScrollY = mainMenuSelection * 22;
        userInput = "";
        aiResponse = "";
        showMainMenu();
      }
      break;
      
    case STATE_API_SELECT:
    case STATE_CHAT_RESPONSE:
      currentState = STATE_MAIN_MENU;
      menuSelection = mainMenuSelection;
      menuTargetScrollY = mainMenuSelection * 22;
      userInput = "";
      aiResponse = "";
      showMainMenu();
      break;
      
    default:
      currentState = STATE_MAIN_MENU;
      menuSelection = 0;
      showMainMenu();
      break;
  }
}

// ========== POWER FUNCTIONS ==========

void showPowerBase() {
  display.clearDisplay();
  drawBatteryIndicator();
  drawBreadcrumb();
  
  display.setTextSize(1);
  display.setCursor(15, 2);
  display.print("POWER CENTRAL");
  
  drawIcon(2, 2, ICON_POWER);
  
  display.drawLine(0, 12, SCREEN_WIDTH, 12, SSD1306_WHITE);
  
  const char* menuItems[] = {
    "Battery Visual",
    "Statistics",
    "Power Graph",
    "Consumption",
    "Back"
  };
  
  for (int i = 0; i < 5; i++) {
    display.setCursor(10, 16 + i * 9);
    if (i == powerMenuSelection) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    display.print(menuItems[i]);
  }
  
  display.display();
}

void handlePowerBaseSelect() {
  previousState = currentState;
  
  switch(powerMenuSelection) {
    case 0:
      currentState = STATE_POWER_VISUAL;
      showPowerVisual();
      break;
    case 1:
      currentState = STATE_POWER_STATS;
      showPowerStats();
      break;
    case 2:
      currentState = STATE_POWER_GRAPH;
      showPowerGraph();
      break;
    case 3:
      currentState = STATE_POWER_CONSUMPTION;
      showPowerConsumption();
      break;
    case 4:
      currentState = STATE_MAIN_MENU;
      menuSelection = mainMenuSelection;
      menuTargetScrollY = mainMenuSelection * 22;
      showMainMenu();
      break;
  }
}

void showPowerVisual() {
  display.clearDisplay();
  drawBatteryIndicator();
  drawBreadcrumb();
  
  int battX = 20;
  int battY = 14;
  int battW = 70;
  int battH = 36;
  
  display.fillRect(battX + 2, battY + 2, battW, battH, SSD1306_WHITE);
  display.fillRect(battX, battY, battW, battH, SSD1306_BLACK);
  
  display.drawRect(battX, battY, battW, battH, SSD1306_WHITE);
  display.drawRect(battX + 1, battY + 1, battW - 2, battH - 2, SSD1306_WHITE);
  
  display.fillRect(battX + battW, battY + 13, 7, 10, SSD1306_WHITE);
  
  int fillW = map(batteryPercent, 0, 100, 0, battW - 10);
  
  if (isCharging) {
    int waveSpeed = (millis() / 60) % 30;
    for (int i = 0; i < fillW; i++) {
      int wave = abs(((i - waveSpeed) % 30) - 15);
      if (wave < 8) {
        int height = map(wave, 0, 8, battH - 10, 4);
        display.drawLine(battX + 5 + i, battY + 5, 
                        battX + 5 + i, battY + 5 + height, SSD1306_WHITE);
      }
    }
  } else {
    for (int i = 0; i < fillW; i += 2) {
      display.drawLine(battX + 5 + i, battY + 5,
                      battX + 5 + i, battY + battH - 5, SSD1306_WHITE);
    }
  }
  
  display.setTextSize(2);
  String percentText = String(batteryPercent) + "%";
  int textW = percentText.length() * 12;
  int textX = battX + (battW - textW) / 2;
  int textY = battY + (battH - 16) / 2;
  
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(textX, textY);
  display.print(percentText);
  
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 54);
  display.print(batteryVoltage, 2);
  display.print("V");
  
  display.setCursor(45, 54);
  if (isCharging) {
    display.print("CHARGING");
  } else {
    if (batteryPercent > 80) display.print("FULL");
    else if (batteryPercent > 20) display.print("GOOD");
    else display.print("LOW!");
  }
  
  display.display();
}

void showPowerStats() {
  display.clearDisplay();
  drawBatteryIndicator();
  drawBreadcrumb();
  
  display.setTextSize(1);
  display.setCursor(15, 2);
  display.print("POWER STATS");
  
  display.drawLine(0, 12, SCREEN_WIDTH, 12, SSD1306_WHITE);
  
  display.setCursor(2, 16);
  display.print("Voltage:");
  display.setCursor(70, 16);
  display.print(batteryVoltage, 3);
  display.print(" V");
  
  display.setCursor(2, 26);
  display.print("Battery:");
  display.setCursor(70, 26);
  display.print(batteryPercent);
  display.print(" %");
  
  display.setCursor(2, 36);
  display.print("Status:");
  display.setCursor(70, 36);
  display.print(isCharging ? "CHARGING" : "DISCHARGING");
  
  display.setCursor(2, 46);
  display.print("Power:");
  display.setCursor(70, 46);
  display.print(totalPowerDraw, 0);
  display.print(" mA");
  
  display.display();
}

void showPowerGraph() {
  display.clearDisplay();
  drawBatteryIndicator();
  drawBreadcrumb();
  
  display.setTextSize(1);
  display.setCursor(25, 2);
  display.print("POWER GRAPH");
  
  int graphX = 10;
  int graphY = 15;
  int graphW = 108;
  int graphH = 40;
  
  display.drawLine(graphX, graphY, graphX, graphY + graphH, SSD1306_WHITE);
  display.drawLine(graphX, graphY + graphH, graphX + graphW, graphY + graphH, SSD1306_WHITE);
  
  int dataPoints = batteryHistoryFilled ? BATTERY_HISTORY_SIZE : batteryHistoryIndex;
  if (dataPoints > 1) {
    int startIdx = batteryHistoryFilled ? batteryHistoryIndex : 0;
    
    for (int i = 0; i < dataPoints - 1; i++) {
      int idx1 = (startIdx + i) % BATTERY_HISTORY_SIZE;
      int idx2 = (startIdx + i + 1) % BATTERY_HISTORY_SIZE;
      
      int x1 = graphX + (i * graphW / (dataPoints - 1));
      int y1 = graphY + graphH - (batteryHistory[idx1] * graphH / 100);
      int x2 = graphX + ((i + 1) * graphW / (dataPoints - 1));
      int y2 = graphY + graphH - (batteryHistory[idx2] * graphH / 100);
      
      display.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
    }
  }
  
  display.setCursor(40, 58);
  display.print("Now: ");
  display.print(batteryPercent);
  display.print("%");
  
  display.display();
}

// ========== WIFI FUNCTIONS ==========

void showWiFiMenu() {
  display.clearDisplay();
  drawBatteryIndicator();
  drawBreadcrumb();
  
  display.setTextSize(1);
  display.setCursor(25, 2);
  display.print("WiFi MENU");
  
  drawIcon(10, 2, ICON_WIFI);
  
  display.drawLine(0, 12, SCREEN_WIDTH, 12, SSD1306_WHITE);
  
  display.setCursor(5, 16);
  if (WiFi.status() == WL_CONNECTED) {
    display.print("Connected:");
    display.setCursor(5, 24);
    String ssid = WiFi.SSID();
    if (ssid.length() > 18) {
      ssid = ssid.substring(0, 18) + "..";
    }
    display.print(ssid);
  } else {
    display.print("Not connected");
  }
  
  const char* menuItems[] = {"Scan Networks", "Forget Network", "Back"};
  
  for (int i = 0; i < 3; i++) {
    display.setCursor(5, 36 + i * 9);
    if (i == menuSelection) {
      display.print("> ");
    } else {
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
      currentState = STATE_MAIN_MENU;
      menuSelection = mainMenuSelection;
      menuTargetScrollY = mainMenuSelection * 22;
      showMainMenu();
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
  currentState = STATE_WIFI_SCAN;
  displayWiFiNetworks();
}

void displayWiFiNetworks() {
  display.clearDisplay();
  drawBatteryIndicator();
  drawBreadcrumb();
  
  display.setTextSize(1);
  display.setCursor(5, 0);
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

// ========== CALCULATOR ==========

void showCalculator() {
  display.clearDisplay();
  drawBatteryIndicator();
  drawBreadcrumb();
  
  display.setTextSize(1);
  display.setCursor(30, 2);
  display.print("CALCULATOR");
  
  drawIcon(15, 2, ICON_CALC);
  
  display.drawRect(2, 12, SCREEN_WIDTH - 4, 12, SSD1306_WHITE);
  display.setCursor(6, 15);
  String displayText = calcDisplay;
  if (displayText.length() > 18) {
    displayText = displayText.substring(displayText.length() - 18);
  }
  display.print(displayText);
  
  const char* calcPad[4][4] = {
    {"7", "8", "9", "/"},
    {"4", "5", "6", "*"},
    {"1", "2", "3", "-"},
    {"C", "0", "=", "+"}
  };
  
  int startY = 28;
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      int x = col * 30 + 4;
      int y = startY + row * 9;
      
      if (row == cursorY && col == cursorX) {
        display.fillRect(x - 2, y - 1, 16, 8, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }
      
      display.setCursor(x, y);
      display.print(calcPad[row][col]);
    }
  }
  
  display.setTextColor(SSD1306_WHITE);
  display.display();
}

void handleCalculatorInput() {
  const char* calcPad[4][4] = {
    {"7", "8", "9", "/"},
    {"4", "5", "6", "*"},
    {"1", "2", "3", "-"},
    {"C", "0", "=", "+"}
  };
  
  String key = calcPad[cursorY][cursorX];
  
  if (key == "C") {
    resetCalculator();
  } else if (key == "=") {
    calculateResult();
  } else if (key == "+" || key == "-" || key == "*" || key == "/") {
    if (calcDisplay != "0" && calcDisplay != "Error") {
      calcNumber1 = calcDisplay;
      calcOperator = key.charAt(0);
      calcNewNumber = true;
    }
  } else {
    if (calcNewNumber || calcDisplay == "0" || calcDisplay == "Error") {
      calcDisplay = key;
      calcNewNumber = false;
    } else {
      if (calcDisplay.length() < 12) {
        calcDisplay += key;
      }
    }
  }
  
  showCalculator();
}

void calculateResult() {
  if (calcNumber1.length() > 0 && calcOperator != ' ') {
    float num1 = calcNumber1.toFloat();
    float num2 = calcDisplay.toFloat();
    float result = 0;
    
    switch(calcOperator) {
      case '+': result = num1 + num2; break;
      case '-': result = num1 - num2; break;
      case '*': result = num1 * num2; break;
      case '/':
        if (num2 != 0) {
          result = num1 / num2;
        } else {
          ledError();
          calcDisplay = "Error";
          calcNumber1 = "";
          calcOperator = ' ';
          calcNewNumber = true;
          return;
        }
        break;
    }
    
    ledSuccess();
    
    calcDisplay = String(result, 2);
    while (calcDisplay.endsWith("0") && calcDisplay.indexOf('.') != -1) {
      calcDisplay.remove(calcDisplay.length() - 1);
    }
    if (calcDisplay.endsWith(".")) {
      calcDisplay.remove(calcDisplay.length() - 1);
    }
    
    calcNumber1 = "";
    calcOperator = ' ';
    calcNewNumber = true;
  }
}

void resetCalculator() {
  calcDisplay = "0";
  calcNumber1 = "";
  calcOperator = ' ';
  calcNewNumber = true;
}

// ========== API SELECT ==========

void showAPISelect() {
  display.clearDisplay();
  drawBatteryIndicator();
  drawBreadcrumb();
  
  display.setTextSize(1);
  display.setCursor(15, 5);
  display.print("SELECT GEMINI API");
  
  display.drawLine(0, 15, SCREEN_WIDTH, 15, SSD1306_WHITE);
  
  int y1 = 25;
  if (menuSelection == 0) {
    display.fillRect(5, y1 - 2, 118, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  }
  display.setCursor(10, y1);
  display.print("1. Gemini API #1");
  if (selectedAPIKey == 1) {
    display.setCursor(100, y1);
    display.print("[*]");
  }
  display.setTextColor(SSD1306_WHITE);
  
  int y2 = 42;
  if (menuSelection == 1) {
    display.fillRect(5, y2 - 2, 118, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  }
  display.setCursor(10, y2);
  display.print("2. Gemini API #2");
  if (selectedAPIKey == 2) {
    display.setCursor(100, y2);
    display.print("[*]");
  }
  display.setTextColor(SSD1306_WHITE);
  
  display.display();
}

void handleAPISelectSelect() {
  if (menuSelection == 0) {
    selectedAPIKey = 1;
  } else {
    selectedAPIKey = 2;
  }
  
  ledSuccess();
  
  userInput = "";
  keyboardContext = CONTEXT_CHAT;
  currentState = STATE_KEYBOARD;
  cursorX = 0;
  cursorY = 0;
  currentKeyboardMode = MODE_LOWER;
  drawKeyboard();
}

// ========== KEYBOARD FUNCTIONS ==========

void drawKeyboard() {
  display.clearDisplay();
  drawBatteryIndicator();
  
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // NEW: Calculate text display with scrolling
  String displayText = "";
  int maxVisibleChars = 0;
  int prefixWidth = 0;
  
  if (keyboardContext == CONTEXT_WIFI_PASSWORD) {
    display.setCursor(0, 0);
    display.print("Pass:");
    prefixWidth = 30; // Width of "Pass:"
    maxVisibleChars = (SCREEN_WIDTH - prefixWidth - 18) / 6; // Reserve space for mode indicator
    
    String masked = "";
    for (unsigned int i = 0; i < passwordInput.length(); i++) {
      masked += "*";
    }
    displayText = masked;
    
  } else if (keyboardContext == CONTEXT_ESPNOW_MAC) {
    display.setCursor(0, 0);
    display.print("MAC:");
    prefixWidth = 24; // Width of "MAC:"
    maxVisibleChars = (SCREEN_WIDTH - prefixWidth - 30) / 6; // Reserve space for "HEX"
    displayText = userInput;
    
  } else {
    display.setCursor(0, 0);
    display.print(">");
    prefixWidth = 6; // Width of ">"
    maxVisibleChars = (SCREEN_WIDTH - prefixWidth - 18) / 6;
    displayText = userInput;
  }
  
  // NEW: Smooth scrolling effect
  int textLength = displayText.length();
  
  if (textLength > maxVisibleChars) {
    // Text is too long, needs scrolling
    unsigned long currentMillis = millis();
    
    if (currentMillis - lastTextInputUpdate > textInputScrollSpeed) {
      textInputScrollOffset++;
      
      // Reset scroll when we've scrolled past the end
      if (textInputScrollOffset > textLength - maxVisibleChars + 3) {
        textInputScrollOffset = 0;
        delay(500); // Pause at the beginning
      }
      
      lastTextInputUpdate = currentMillis;
    }
    
    // Extract visible portion
    int startIdx = min(textInputScrollOffset, textLength - maxVisibleChars);
    String visibleText = displayText.substring(startIdx, startIdx + maxVisibleChars);
    
    display.setCursor(prefixWidth, 0);
    display.print(visibleText);
    
    // Show scroll indicator
    if (startIdx > 0) {
      display.setCursor(prefixWidth - 6, 0);
      display.print("<");
    }
    if (startIdx + maxVisibleChars < textLength) {
      display.setCursor(SCREEN_WIDTH - 24, 0);
      display.print(">");
    }
    
  } else {
    // Text fits, no scrolling needed
    textInputScrollOffset = 0;
    display.setCursor(prefixWidth, 0);
    display.print(displayText);
  }
  
  // Show keyboard mode indicator
  if (keyboardContext != CONTEXT_ESPNOW_MAC) {
    display.setCursor(SCREEN_WIDTH - 18, 0);
    if (currentKeyboardMode == MODE_UPPER) {
      display.print("A");
    } else if (currentKeyboardMode == MODE_NUMBERS) {
      display.print("#");
    } else {
      display.print("a");
    }
  } else {
    display.setCursor(SCREEN_WIDTH - 30, 0);
    display.print("HEX");
  }
  
  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
  
  int startY = 14;
  const char* currentKeyboard[3][10];
  
  // Use MAC keyboard for MAC input
  if (keyboardContext == CONTEXT_ESPNOW_MAC) {
    memcpy(currentKeyboard, keyboardMac, sizeof(keyboardMac));
  } else {
    switch(currentKeyboardMode) {
      case MODE_LOWER:
        memcpy(currentKeyboard, keyboardLower, sizeof(keyboardLower));
        break;
      case MODE_UPPER:
        memcpy(currentKeyboard, keyboardUpper, sizeof(keyboardUpper));
        break;
      case MODE_NUMBERS:
        memcpy(currentKeyboard, keyboardNumbers, sizeof(keyboardNumbers));
        break;
      default:
        memcpy(currentKeyboard, keyboardLower, sizeof(keyboardLower));
        break;
    }
  }
  
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 10; col++) {
      int x = col * 12 + 2;
      int y = startY + row * 16;
      
      if (row == cursorY && col == cursorX) {
        display.fillRect(x - 1, y - 1, 11, 10, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }
      
      display.setCursor(x, y);
      
      const char* key = currentKeyboard[row][col];
      
      if (strcmp(key, "OK") == 0) {
        display.print("OK");
      } else if (strcmp(key, "<") == 0) {
        display.print("<");
      } else if (strcmp(key, "#") == 0) {
        display.print("#");
      } else if (strcmp(key, " ") == 0) {
        display.print("_");
      } else {
        display.print(key);
      }
    }
  }
  
  display.display();
}

const char* getCurrentKey() {
  if (keyboardContext == CONTEXT_ESPNOW_MAC) {
    return keyboardMac[cursorY][cursorX];
  }
  
  switch(currentKeyboardMode) {
    case MODE_LOWER:
      return keyboardLower[cursorY][cursorX];
    case MODE_UPPER:
      return keyboardUpper[cursorY][cursorX];
    case MODE_NUMBERS:
      return keyboardNumbers[cursorY][cursorX];
    default:
      return keyboardLower[cursorY][cursorX];
  }
}

void toggleKeyboardMode() {
  switch(currentKeyboardMode) {
    case MODE_LOWER:
      currentKeyboardMode = MODE_UPPER;
      break;
    case MODE_UPPER:
      currentKeyboardMode = MODE_NUMBERS;
      break;
    case MODE_NUMBERS:
      currentKeyboardMode = MODE_LOWER;
      break;
  }
}

void handleKeyPress() {
  const char* key = getCurrentKey();
  
  if (strcmp(key, "OK") == 0) {
    if (userInput.length() > 0) {
      sendToGemini();
    }
  } else if (strcmp(key, "<") == 0) {
    if (userInput.length() > 0) {
      userInput.remove(userInput.length() - 1);
      textInputScrollOffset = 0; // Reset scroll on backspace
    }
    drawKeyboard();
  } else if (strcmp(key, "#") == 0) {
    toggleKeyboardMode();
    drawKeyboard();
  } else {
    userInput += key;
    textInputScrollOffset = 0; // Reset scroll on new input
    if (currentKeyboardMode == MODE_UPPER) {
      currentKeyboardMode = MODE_LOWER;
    }
    drawKeyboard();
  }
}

void handlePasswordKeyPress() {
  const char* key = getCurrentKey();
  
  if (strcmp(key, "OK") == 0) {
    if (passwordInput.length() > 0 || !networks[selectedNetwork].encrypted) {
      connectToWiFi(selectedSSID, passwordInput);
    }
  } else if (strcmp(key, "<") == 0) {
    if (passwordInput.length() > 0) {
      passwordInput.remove(passwordInput.length() - 1);
      textInputScrollOffset = 0; // Reset scroll on backspace
    }
    drawKeyboard();
  } else if (strcmp(key, "#") == 0) {
    toggleKeyboardMode();
    drawKeyboard();
  } else {
    passwordInput += key;
    textInputScrollOffset = 0; // Reset scroll on new input
    if (currentKeyboardMode == MODE_UPPER) {
      currentKeyboardMode = MODE_LOWER;
    }
    drawKeyboard();
  }
}

// ========== WIFI & GEMINI FUNCTIONS ==========

void connectToWiFi(String ssid, String password) {
  showProgressBar("Connecting", 0);
  
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    ledBlink(100);
    delay(500);
    attempts++;
    showProgressBar("Connecting", attempts * 3);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    preferences.begin("wifi-creds", false); // Open for writing
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.end();
    
    ledSuccess();
    showProgressBar("Connected!", 100);
    delay(1000);
    
    lastWiFiActivity = millis();
    
    currentState = STATE_MAIN_MENU;
    menuSelection = 0;
    showMainMenu();
  } else {
    ledError();
    showStatus("Connection failed!", 2000);
    currentState = STATE_WIFI_SCAN;
    displayWiFiNetworks();
  }
}

void forgetNetwork() {
  preferences.begin("wifi-creds", false);
  preferences.clear();
  preferences.end();
  WiFi.disconnect();
  showStatus("Network forgotten!", 2000);
  showWiFiMenu();
}

void checkWiFiTimeout() {
  if (millis() - lastWiFiActivity > wifiTimeout) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    showStatus("WiFi Auto-Off\nTimeout reached", 2000);
    showMainMenu();
  }
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
  
  // FIXED: Check free heap before JSON operations
  if (freeHeap < 30000) {
    ledError();
    aiResponse = "Low memory! Cannot process request.";
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
  
  String escapedInput = userInput;
  escapedInput.replace("\\", "\\\\");
  escapedInput.replace("\"", "\\\"");
  escapedInput.replace("\n", "\\n");
  
  String jsonPayload = "{\"contents\":[{\"parts\":[{\"text\":\"" + escapedInput + "\"}]}]}";
  
  int httpResponseCode = http.POST(jsonPayload);
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    
    // Use JsonDocument which is the new standard
    JsonDocument responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);
    
    if (!error && responseDoc["candidates"]) {
      JsonArray candidates = responseDoc["candidates"];
      if (candidates.size() > 0) {
        JsonObject content = candidates[0]["content"];
        JsonArray parts = content["parts"];
        if (parts.size() > 0) {
          aiResponse = parts[0]["text"].as<String>();
          ledSuccess();
        } else {
          aiResponse = "Error: Empty response";
          ledError();
        }
      } else {
        aiResponse = "Error: No candidates";
        ledError();
      }
    } else {
      ledError();
      aiResponse = "JSON Parse Error";
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

void displayResponse() {
  display.clearDisplay();
  drawBatteryIndicator();
  
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

void showLoadingAnimation() {
  display.clearDisplay();
  drawBatteryIndicator();
  
  display.setTextSize(1);
  display.setCursor(20, 10);
  display.print("Loading AI");
  
  int dots = (loadingFrame % 4);
  for (int i = 0; i < dots; i++) {
    display.print(".");
  }
  
  int centerX = SCREEN_WIDTH / 2;
  int centerY = 35;
  int radius = 12;
  
  display.drawCircle(centerX, centerY, radius, SSD1306_WHITE);
  
  float angle = loadingFrame * PI / 4;
  int x2 = centerX + cos(angle) * radius;
  int y2 = centerY + sin(angle) * radius;
  
  display.drawLine(centerX, centerY, x2, y2, SSD1306_WHITE);
  
  display.display();
}

void showProgressBar(String title, int percent) {
  display.clearDisplay();
  
  display.setTextSize(1);
  int titleW = title.length() * 6;
  display.setCursor((SCREEN_WIDTH - titleW) / 2, 15);
  display.print(title);
  
  int barX = 10;
  int barY = 30;
  int barW = SCREEN_WIDTH - 20;
  int barH = 12;
  
  display.drawRect(barX, barY, barW, barH, SSD1306_WHITE);
  
  int fillW = map(percent, 0, 100, 0, barW - 4);
  display.fillRect(barX + 2, barY + 2, fillW, barH - 4, SSD1306_WHITE);
  
  display.setCursor(55, 47);
  display.print(percent);
  display.print("%");
  
  display.display();
}

void showStatus(String message, int delayMs) {
  display.clearDisplay();
  display.setTextSize(1);
  
  int y = 20;
  int x = 0;
  String word = "";
  
  for (unsigned int i = 0; i < message.length(); i++) {
    char c = message.charAt(i);
    
    if (c == '\n') {
      display.setCursor(x, y);
      display.print(word);
      word = "";
      y += 10;
      x = 0;
    } else if (c == ' ') {
      word += c;
      int wordWidth = word.length() * 6;
      if (x + wordWidth > SCREEN_WIDTH) {
        y += 10;
        x = 0;
      }
      display.setCursor(x, y);
      display.print(word);
      x += wordWidth;
      word = "";
    } else {
      word += c;
    }
  }
  
  if (word.length() > 0) {
    display.setCursor(x, y);
    display.print(word);
  }
  
  display.display();
  delay(delayMs);
}

void refreshCurrentScreen() {
  switch(currentState) {
    case STATE_MAIN_MENU:
      showMainMenu();
      break;
    case STATE_WIFI_MENU:
      showWiFiMenu();
      break;
    case STATE_WIFI_SCAN:
      displayWiFiNetworks();
      break;
    case STATE_CALCULATOR:
      showCalculator();
      break;
    case STATE_POWER_BASE:
      showPowerBase();
      break;
    case STATE_POWER_VISUAL:
      showPowerVisual();
      break;
    case STATE_POWER_STATS:
      showPowerStats();
      break;
    case STATE_POWER_GRAPH:
      showPowerGraph();
      break;
    case STATE_POWER_CONSUMPTION:
      showPowerConsumption();
      break;
    case STATE_API_SELECT:
      showAPISelect();
      break;
    case STATE_SYSTEM_INFO:
      showSystemInfo();
      break;
    case STATE_SETTINGS_MENU:
      showSettingsMenu();
      break;
    case STATE_BATTERY_GUARDIAN:
      showBatteryGuardian();
      break;
    case STATE_ESP_NOW_MENU:
      showESPNowMenu();
      break;
    case STATE_ESP_NOW_SEND:
      showESPNowSend();
      break;
    case STATE_ESP_NOW_INBOX:
      showESPNowInbox();
      break;
    case STATE_ESP_NOW_PEERS:
      showESPNowPeers();
      break;
    case STATE_ESP_NOW_PEER_OPTIONS:
      showESPNowPeerOptions();
      break;
    case STATE_KEYBOARD:
    case STATE_PASSWORD_INPUT:
      drawKeyboard();
      break;
    case STATE_CHAT_RESPONSE:
      displayResponse();
      break;
    case STATE_OTA_UPDATE:
      showOTAUpdate();
      break;
  }
}

void showOTAUpdate() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(25, 5);
    display.print("OTA Update");
    display.display();
}

void performOTAUpdate() {
    currentState = STATE_OTA_UPDATE;
    showStatus("Checking for\nupdates...", 1000);

    WiFiClientSecure client;
    client.setCACert(root_ca_github);

    HTTPClient http;
    String apiUrl = "https://api.github.com/repos/" + String(GITHUB_REPO) + "/releases/latest";
    http.begin(client, apiUrl);
    http.addHeader("Accept", "application/vnd.github.v3+json");

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        showStatus("Failed to check\nfor updates", 2000);
        currentState = STATE_SETTINGS_MENU;
        refreshCurrentScreen();
        http.end();
        return;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        showStatus("JSON parse error", 2000);
        currentState = STATE_SETTINGS_MENU;
        refreshCurrentScreen();
        return;
    }

    String firmwareUrl = "";
    JsonArray assets = doc["assets"].as<JsonArray>();
    for (JsonObject asset : assets) {
        if (String(asset["name"].as<const char*>()) == FIRMWARE_ASSET_NAME) {
            firmwareUrl = asset["browser_download_url"].as<String>();
            break;
        }
    }

    if (firmwareUrl == "") {
        showStatus("No firmware found\nin latest release", 2000);
        currentState = STATE_SETTINGS_MENU;
        refreshCurrentScreen();
        return;
    }

    showStatus("Update found!\nStarting...", 1000);

    http.begin(client, firmwareUrl);
    httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        showStatus("Update failed\nServer error", 2000);
        currentState = STATE_SETTINGS_MENU;
        refreshCurrentScreen();
        http.end();
        return;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        showStatus("Update failed\nInvalid size", 2000);
        currentState = STATE_SETTINGS_MENU;
        refreshCurrentScreen();
        http.end();
        return;
    }

    if (!Update.begin(contentLength)) {
        showStatus("Update failed\nNo space", 2000);
        currentState = STATE_SETTINGS_MENU;
        refreshCurrentScreen();
        http.end();
        return;
    }

    WiFiClient& updateStream = http.getStream();
    size_t written = 0;
    byte buff[1024] = { 0 };

    while (http.connected() && (contentLength > 0 || contentLength == -1)) {
        size_t size = updateStream.readBytes(buff, sizeof(buff));
        if (size > 0) {
            Update.write(buff, size);
            written += size;
            int progress = (int)((float)written / contentLength * 100);
            showProgressBar("Updating...", progress);
        }
        delay(1);
    }

    if (!Update.end(true)) {
        showStatus("Update failed!", 2000);
        currentState = STATE_SETTINGS_MENU;
        refreshCurrentScreen();
        return;
    }

    showStatus("Update complete!\nRebooting...", 3000);
    ESP.restart();
}

// FIXED: Battery level dengan pembacaan yang lebih stabil
void updateBatteryLevel() {
  // Multiple readings for stability
  int adcSum = 0;
  for (int i = 0; i < 10; i++) {
    adcSum += analogRead(BATTERY_PIN);
    delay(5);
  }
  int adcValue = adcSum / 10;
  
  float adcVoltage = (adcValue / 4095.0) * 3.3;
  batteryVoltage = adcVoltage * VOLTAGE_DIVIDER_RATIO;
  
  batteryPercent = map(batteryVoltage * 100, BATTERY_MIN_VOLTAGE * 100, BATTERY_MAX_VOLTAGE * 100, 0, 100);
  batteryPercent = constrain(batteryPercent, 0, 100);
  
  // FIXED: More stable charging detection
  int chargingSum = 0;
  for (int i = 0; i < 10; i++) {
    chargingSum += analogRead(CHARGING_PIN);
    delay(2);
  }
  int chargingValue = chargingSum / 10;
  float chargingVoltage = (chargingValue / 4095.0) * 3.3 * VOLTAGE_DIVIDER_RATIO;
  
  // More conservative charging detection threshold
  isCharging = (chargingVoltage > batteryVoltage + 0.5);
}

void drawBatteryIndicator() {
  int battX = SCREEN_WIDTH - 24;
  int battY = 1;
  int battWidth = 22;
  int battHeight = 8;
  
  display.drawRect(battX, battY, battWidth, battHeight, SSD1306_WHITE);
  display.fillRect(battX + battWidth, battY + 2, 2, battHeight - 4, SSD1306_WHITE);
  
  int fillWidth = map(batteryPercent, 0, 100, 0, battWidth - 4);
  
  if (batteryPercent > 20) {
    display.fillRect(battX + 2, battY + 2, fillWidth, battHeight - 4, SSD1306_WHITE);
  } else if (batteryPercent > 10) {
    if ((millis() / 500) % 2 == 0) {
      display.fillRect(battX + 2, battY + 2, fillWidth, battHeight - 4, SSD1306_WHITE);
    }
  } else {
    if ((millis() / 250) % 2 == 0) {
      display.fillRect(battX + 2, battY + 2, fillWidth, battHeight - 4, SSD1306_WHITE);
    }
  }
  
  display.setTextSize(1);
  
  String percentText = String(batteryPercent);
  int textWidth = percentText.length() * 6;
  int textX = battX + (battWidth - textWidth) / 2;
  int textY = battY + 1;
  
  if (batteryPercent > 50) {
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.setTextColor(SSD1306_WHITE);
  }
  
  display.setCursor(textX, textY);
  display.print(batteryPercent);
  
  if (isCharging) {
    drawChargingPlug();
  }
  
  display.setTextColor(SSD1306_WHITE);
}

void drawChargingPlug() {
  int plugX = SCREEN_WIDTH - 35;
  int plugY = 2;
  
  int anim = (millis() / 200) % 2;
  
  display.drawLine(plugX, plugY, plugX, plugY + 2 + anim, SSD1306_WHITE);
  display.drawLine(plugX + 3, plugY, plugX + 3, plugY + 2 + anim, SSD1306_WHITE);
  
  display.drawRect(plugX - 1, plugY + 2, 5, 4, SSD1306_WHITE);
  display.drawLine(plugX + 1, plugY + 6, plugX + 1, plugY + 7, SSD1306_WHITE);
  
  if (anim == 1) {
    display.drawPixel(plugX - 2, plugY + 4, SSD1306_WHITE);
    display.drawPixel(plugX + 5, plugY + 3, SSD1306_WHITE);
  }
}

void drawWiFiSignalBars() {
  int rssi = WiFi.RSSI();
  int bars = 0;
  
  if (rssi > -50) bars = 4;
  else if (rssi > -60) bars = 3;
  else if (rssi > -70) bars = 2;
  else if (rssi > -80) bars = 1;
  
  int x = 2;
  int y = 2;
  
  for (int i = 0; i < 4; i++) {
    int barHeight = (i + 1) * 2;
    if (i < bars) {
      display.fillRect(x + i * 3, y + (8 - barHeight), 2, barHeight, SSD1306_WHITE);
    } else {
      display.drawRect(x + i * 3, y + (8 - barHeight), 2, barHeight, SSD1306_WHITE);
    }
  }
}

// ========== BUTTON HANDLERS - FIXED ==========

void handleUp() {
  switch(currentState) {
    case STATE_MAIN_MENU:
      mainMenuSelection = (mainMenuSelection - 1 + 7) % 7;
      menuTargetScrollY = mainMenuSelection * 22;
      menuTextScrollX = 0;
      break;
    case STATE_WIFI_MENU:
      menuSelection = (menuSelection - 1 + 3) % 3;
      showWiFiMenu();
      break;
    case STATE_POWER_BASE:
      powerMenuSelection = (powerMenuSelection - 1 + 5) % 5;
      showPowerBase();
      break;
    case STATE_SETTINGS_MENU:
      settingsMenuSelection = (settingsMenuSelection - 1 + 4) % 4;
      showSettingsMenu();
      break;
    case STATE_API_SELECT:
      menuSelection = (menuSelection - 1 + 2) % 2;
      showAPISelect();
      break;
    case STATE_ESP_NOW_MENU:
      espnowMenuSelection = (espnowMenuSelection - 1 + 4) % 4;
      showESPNowMenu();
      break;
    case STATE_ESP_NOW_SEND:
      if (peerCount > 0) {
        espnowPeerSelection = (espnowPeerSelection - 1 + peerCount) % peerCount;
        showESPNowSend();
      }
      break;
    case STATE_ESP_NOW_INBOX:
      if (inboxCount > 0) {
        espnowInboxSelection = (espnowInboxSelection - 1 + inboxCount) % inboxCount;
        showESPNowInbox();
      }
      break;
    case STATE_ESP_NOW_PEERS:
      if (peerCount > 0) {
        espnowPeerSelection = (espnowPeerSelection - 1 + peerCount) % peerCount;
        showESPNowPeers();
      }
      break;
    case STATE_ESP_NOW_PEER_OPTIONS:
      espnowPeerOptionsSelection = (espnowPeerOptionsSelection - 1 + 3) % 3;
      showESPNowPeerOptions();
      break;
    case STATE_WIFI_SCAN:
      if (networkCount > 0) {
        selectedNetwork = (selectedNetwork - 1 + networkCount) % networkCount;
        wifiPage = selectedNetwork / wifiPerPage;
        displayWiFiNetworks();
      }
      break;
    case STATE_KEYBOARD:
    case STATE_PASSWORD_INPUT:
      cursorY = (cursorY - 1 + 3) % 3;
      drawKeyboard();
      break;
    case STATE_CALCULATOR:
      cursorY = (cursorY - 1 + 4) % 4;
      showCalculator();
      break;
    case STATE_CHAT_RESPONSE:
      scrollOffset = max(0, scrollOffset - 8);
      displayResponse();
      break;
  }
}

void handleDown() {
  switch(currentState) {
    case STATE_MAIN_MENU:
      mainMenuSelection = (mainMenuSelection + 1) % 7;
      menuTargetScrollY = mainMenuSelection * 22;
      menuTextScrollX = 0;
      break;
    case STATE_WIFI_MENU:
      menuSelection = (menuSelection + 1) % 3;
      showWiFiMenu();
      break;
    case STATE_POWER_BASE:
      powerMenuSelection = (powerMenuSelection + 1) % 5;
      showPowerBase();
      break;
    case STATE_SETTINGS_MENU:
      settingsMenuSelection = (settingsMenuSelection + 1) % 4;
      showSettingsMenu();
      break;
    case STATE_API_SELECT:
      menuSelection = (menuSelection + 1) % 2;
      showAPISelect();
      break;
    case STATE_ESP_NOW_MENU:
      espnowMenuSelection = (espnowMenuSelection + 1) % 4;
      showESPNowMenu();
      break;
    case STATE_ESP_NOW_SEND:
      if (peerCount > 0) {
        espnowPeerSelection = (espnowPeerSelection + 1) % peerCount;
        showESPNowSend();
      }
      break;
    case STATE_ESP_NOW_INBOX:
      if (inboxCount > 0) {
        espnowInboxSelection = (espnowInboxSelection + 1) % inboxCount;
        showESPNowInbox();
      }
      break;
    case STATE_ESP_NOW_PEERS:
      if (peerCount > 0) {
        espnowPeerSelection = (espnowPeerSelection + 1) % peerCount;
        showESPNowPeers();
      }
      break;
    case STATE_ESP_NOW_PEER_OPTIONS:
      espnowPeerOptionsSelection = (espnowPeerOptionsSelection + 1) % 3;
      showESPNowPeerOptions();
      break;
    case STATE_WIFI_SCAN:
      if (networkCount > 0) {
        selectedNetwork = (selectedNetwork + 1) % networkCount;
        wifiPage = selectedNetwork / wifiPerPage;
        displayWiFiNetworks();
      }
      break;
    case STATE_KEYBOARD:
    case STATE_PASSWORD_INPUT:
      cursorY = (cursorY + 1) % 3;
      drawKeyboard();
      break;
    case STATE_CALCULATOR:
      cursorY = (cursorY + 1) % 4;
      showCalculator();
      break;
    case STATE_CHAT_RESPONSE:
      scrollOffset += 8;
      displayResponse();
      break;
  }
}

void handleLeft() {
  switch(currentState) {
    case STATE_KEYBOARD:
    case STATE_PASSWORD_INPUT:
      cursorX = (cursorX - 1 + 10) % 10;
      drawKeyboard();
      break;
    case STATE_CALCULATOR:
      cursorX = (cursorX - 1 + 4) % 4;
      showCalculator();
      break;
  }
}

void handleRight() {
  switch(currentState) {
    case STATE_KEYBOARD:
    case STATE_PASSWORD_INPUT:
      cursorX = (cursorX + 1) % 10;
      drawKeyboard();
      break;
    case STATE_CALCULATOR:
      cursorX = (cursorX + 1) % 4;
      showCalculator();
      break;
  }
}

// FIXED: Handle Select dengan context yang jelas
void handleSelect() {
  switch(currentState) {
    case STATE_MAIN_MENU:
      handleMainMenuSelect();
      break;
    case STATE_WIFI_MENU:
      handleWiFiMenuSelect();
      break;
    case STATE_POWER_BASE:
      handlePowerBaseSelect();
      break;
    case STATE_POWER_VISUAL:
    case STATE_POWER_STATS:
    case STATE_POWER_GRAPH:
    case STATE_POWER_CONSUMPTION:
      currentState = STATE_POWER_BASE;
      powerMenuSelection = 0;
      showPowerBase();
      break;
    case STATE_SETTINGS_MENU:
      handleSettingsMenuSelect();
      break;
    case STATE_SYSTEM_INFO:
    case STATE_BATTERY_GUARDIAN:
      currentState = STATE_SETTINGS_MENU;
      settingsMenuSelection = 0;
      showSettingsMenu();
      break;
    case STATE_API_SELECT:
      handleAPISelectSelect();
      break;
    case STATE_CALCULATOR:
      handleCalculatorInput();
      break;
    case STATE_ESP_NOW_MENU:
      switch(espnowMenuSelection) {
        case 0: // Send Message
          if (peerCount > 0) {
            currentState = STATE_ESP_NOW_SEND;
            espnowPeerSelection = 0;
            showESPNowSend();
          } else {
            ledError();
            showStatus("No peers!\nAdd in Manage Peers", 2000);
            showESPNowMenu();
          }
          break;
        case 1: // Inbox
          if (inboxCount > 0) {
            currentState = STATE_ESP_NOW_INBOX;
            espnowInboxSelection = 0;
            showESPNowInbox();
          } else {
            showStatus("Inbox is empty", 1500);
            showESPNowMenu();
          }
          break;
        case 2: // Manage Peers
          currentState = STATE_ESP_NOW_PEERS;
          espnowPeerSelection = 0;
          showESPNowPeers();
          break;
        case 3: // Back
          currentState = STATE_MAIN_MENU;
          menuSelection = mainMenuSelection;
          menuTargetScrollY = mainMenuSelection * 22;
          showMainMenu();
          break;
      }
      break;
    case STATE_ESP_NOW_SEND:
      if (peerCount > 0) {
        keyboardContext = CONTEXT_ESPNOW_MESSAGE;
        currentState = STATE_KEYBOARD;
        userInput = "";
        cursorX = 0;
        cursorY = 0;
        currentKeyboardMode = MODE_LOWER;
        drawKeyboard();
      }
      break;
    case STATE_ESP_NOW_INBOX:
      if (inboxCount > 0) {
        inbox[espnowInboxSelection].read = true;
        showStatus(inbox[espnowInboxSelection].text, 3000);
        showESPNowInbox();
      }
      break;
    case STATE_ESP_NOW_PEERS:
      if (peerCount > 0) {
        // Open peer options menu
        currentState = STATE_ESP_NOW_PEER_OPTIONS;
        espnowPeerOptionsSelection = 0;
        showESPNowPeerOptions();
      } else {
        // Add new peer if no peers exist
        if (peerCount < MAX_ESP_PEERS) {
          keyboardContext = CONTEXT_ESPNOW_MAC;
          currentState = STATE_KEYBOARD;
          userInput = "";
          cursorX = 0;
          cursorY = 0;
          currentKeyboardMode = MODE_MAC; // Use MAC keyboard mode
          drawKeyboard();
        } else {
          ledError();
          showStatus("Peer list is full!", 2000);
          showESPNowPeers();
        }
      }
      break;
    case STATE_ESP_NOW_PEER_OPTIONS:
      switch(espnowPeerOptionsSelection) {
        case 0: // Rename
          keyboardContext = CONTEXT_ESPNOW_PEER_NAME;
          currentState = STATE_KEYBOARD;
          userInput = peers[espnowPeerSelection].name;
          cursorX = 0;
          cursorY = 0;
          currentKeyboardMode = MODE_LOWER;
          drawKeyboard();
          break;
        case 1: // Delete
          deletePeer(espnowPeerSelection);
          currentState = STATE_ESP_NOW_PEERS;
          showESPNowPeers();
          break;
        case 2: // Back
          currentState = STATE_ESP_NOW_PEERS;
          showESPNowPeers();
          break;
      }
      break;
    case STATE_WIFI_SCAN:
      if (networkCount > 0) {
        selectedSSID = networks[selectedNetwork].ssid;
        if (networks[selectedNetwork].encrypted) {
          passwordInput = "";
          keyboardContext = CONTEXT_WIFI_PASSWORD;
          currentState = STATE_PASSWORD_INPUT;
          cursorX = 0;
          cursorY = 0;
          currentKeyboardMode = MODE_LOWER;
          drawKeyboard();
        } else {
          connectToWiFi(selectedSSID, "");
        }
      }
      break;
    case STATE_KEYBOARD:
      // FIXED: Context-aware keyboard OK handling
      if (keyboardContext == CONTEXT_ESPNOW_MESSAGE) {
        const char* key = getCurrentKey();
        if (strcmp(key, "OK") == 0) {
          if (userInput.length() > 0 && peerCount > 0) {
            sendESPNowMessage(userInput, peers[espnowPeerSelection].macAddr);
            userInput = "";
            currentState = STATE_ESP_NOW_MENU;
            espnowMenuSelection = 0;
            showESPNowMenu();
          }
        } else {
          handleKeyPress();
        }
      } else if (keyboardContext == CONTEXT_ESPNOW_PEER_NAME) {
        const char* key = getCurrentKey();
        if (strcmp(key, "OK") == 0) {
          peers[espnowPeerSelection].name = userInput;
          savePeers();
          ledSuccess();
          showStatus("Peer renamed!", 1500);
          currentState = STATE_ESP_NOW_PEERS;
          showESPNowPeers();
        } else {
          handleKeyPress();
        }
      } else if (keyboardContext == CONTEXT_ESPNOW_MAC) {
        handleMacAddressKeyPress();
      } else if (keyboardContext == CONTEXT_CHAT) {
        handleKeyPress();
      }
      break;
    case STATE_PASSWORD_INPUT:
      handlePasswordKeyPress();
      break;
    case STATE_CHAT_RESPONSE:
      currentState = STATE_MAIN_MENU;
      userInput = "";
      aiResponse = "";
      scrollOffset = 0;
      menuSelection = mainMenuSelection;
      menuTargetScrollY = mainMenuSelection * 22;
      showMainMenu();
      break;
  }
}
