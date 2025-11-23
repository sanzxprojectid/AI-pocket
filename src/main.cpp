#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <time.h>

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

// Battery monitoring (stub only for display)
float batteryVoltage = 5.0;
int batteryPercent = 100;
bool isCharging = false;

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

// ESP-NOW Messaging
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

// Selection variables
int espnowMenuSelection = 0;
int espnowPeerSelection = 0;
int espnowInboxSelection = 0;
int espnowPeerOptionsSelection = 0;

String outgoingMessage = "";
String breadcrumb = "";

int loadingFrame = 0;
unsigned long lastLoadingUpdate = 0;
int selectedAPIKey = 1;

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

const char* keyboardMac[3][10] = {
  {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"},
  {"A", "B", "C", "D", "E", "F", "<", "<", "<", "<"},
  {"#", "#", "#", "#", "#", "#", "#", "#", " ", "OK"}
};

enum KeyboardMode { MODE_LOWER, MODE_UPPER, MODE_NUMBERS, MODE_MAC };
KeyboardMode currentKeyboardMode = MODE_LOWER;

enum KeyboardContext {
  CONTEXT_CHAT,
  CONTEXT_ESPNOW_MESSAGE,
  CONTEXT_ESPNOW_PEER_NAME,
  CONTEXT_ESPNOW_MAC,
  CONTEXT_WIFI_PASSWORD
};
KeyboardContext keyboardContext = CONTEXT_CHAT;

// Game state
struct FlappyBird {
  int birdX;
  int birdY;
  int birdVelocity;
  int pipeX;
  int pipeGap;
  int score;
  bool gameOver;
};
FlappyBird game = {10, 32, 0, 80, 20, 0, false};
unsigned long lastGameUpdate = 0;
const int gameUpdateInterval = 30;

// SPACE SHOOTER GAME
struct SpaceEntity {
  int x;
  int y;
  bool active;
};

struct SpaceShooterGame {
  int playerX;
  int playerY;
  SpaceEntity bullets[5];
  SpaceEntity enemies[5];
  int score;
  int level;
  bool gameOver;
  bool shieldActive;
  unsigned long shieldTimer;
  unsigned long lastEnemySpawn;
};

SpaceShooterGame spaceGame;
unsigned long lastSpaceGameUpdate = 0;
const int spaceGameUpdateInterval = 50;

// SNAKE GAME
#define SNAKE_MAX_LEN 50
struct SnakeGame {
  int x[SNAKE_MAX_LEN];
  int y[SNAKE_MAX_LEN];
  int length;
  int dirX; // -1, 0, 1
  int dirY; // -1, 0, 1
  int foodX;
  int foodY;
  int score;
  bool gameOver;
  int speed;
};

SnakeGame snakeGame;
unsigned long lastSnakeUpdate = 0;
int snakeUpdateInterval = 150;

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
  STATE_ESP_NOW_MENU,
  STATE_ESP_NOW_SEND,
  STATE_ESP_NOW_INBOX,
  STATE_ESP_NOW_PEERS,
  STATE_ESP_NOW_PEER_OPTIONS,
  STATE_LOADING,
  STATE_GAME_FLAPPY,
  STATE_GAME_SHOOTER,
  STATE_GAME_SNAKE
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
int menuSelection = 0;
unsigned long lastDebounce = 0;
const unsigned long debounceDelay = 200;
int powerMenuSelection = 0;
int settingsMenuSelection = 0;

// Keyboard scroll variables
int textInputScrollOffset = 0;
unsigned long lastTextInputUpdate = 0;
const int textInputScrollSpeed = 200;

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

const unsigned char ICON_GAME[] PROGMEM = {
  0x3C, 0x42, 0x99, 0xA5, 0xA5, 0x99, 0x42, 0x3C
};

const unsigned char ICON_SHOOTER[] PROGMEM = {
  0x18, 0x3C, 0x7E, 0xDB, 0xFF, 0x24, 0x5A, 0x81
};

const unsigned char ICON_SNAKE[] PROGMEM = {
  0x38, 0x44, 0x24, 0x18, 0x04, 0x44, 0x38, 0x00
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
void showStatus(String message, int delayMs);
void forgetNetwork();
void refreshCurrentScreen();
void calculateResult();
void resetCalculator();
void updateBatteryLevel();
void updateSystemStats();
void updatePowerConsumption();
void drawBatteryIndicator();
void drawWiFiSignalBars();
void drawIcon(int x, int y, const unsigned char* icon);
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
void initGame();
void updateGame();
void drawGame();
void handleGameInput(int direction);
void initSpaceShooter();
void updateSpaceShooter();
void drawSpaceShooter();
void initSnakeGame();
void updateSnakeGame();
void drawSnakeGame();

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

void handleMacAddressKeyPress() {
  const char* key = getCurrentKey();
  String mac = userInput;

  if (strcmp(key, "OK") == 0) {
    if (mac.length() == 17) {
      uint8_t macAddr[6];
      int MacValue[6];
      
      if (sscanf(mac.c_str(), "%x:%x:%x:%x:%x:%x", 
                 &MacValue[0], &MacValue[1], &MacValue[2], 
                 &MacValue[3], &MacValue[4], &MacValue[5]) == 6) {
        
        for(int i = 0; i < 6; ++i) {
          macAddr[i] = (uint8_t)MacValue[i];
        }
        
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
      if (mac.endsWith(":")) {
        userInput.remove(userInput.length() - 2, 2);
      } else {
        userInput.remove(userInput.length() - 1);
      }
      textInputScrollOffset = 0;
    }
    drawKeyboard();
  } else if (strcmp(key, "#") == 0) {
    drawKeyboard();
  } else if (strcmp(key, " ") == 0) {
    drawKeyboard();
  } else {
    char c = key[0];
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) {
      if (mac.length() < 17) {
        userInput += c;
        
        int len = userInput.length();
        if (len == 2 || len == 5 || len == 8 || len == 11 || len == 14) {
          userInput += ":";
        }
        
        textInputScrollOffset = 0;
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
  
  // TTP223 Capacitive Touch Button
  pinMode(TOUCH_LEFT, INPUT);
  pinMode(TOUCH_RIGHT, INPUT);
  
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  
  // Battery monitoring disabled
  batteryPercent = 100;
  batteryVoltage = 5.0;
  
  preferences.begin("settings", true);
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
  
  initESPNow();
  loadPeers();
  
  preferences.begin("wifi-creds", true);
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
  
  // Main Menu Animation Logic
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
  
  // LED Patterns
  switch(currentState) {
    case STATE_LOADING:
      ledBlink(100);
      break;
    case STATE_CHAT_RESPONSE:
      ledBlink(300);
      break;
    case STATE_CALCULATOR:
      ledBlink(500);
      break;
    case STATE_POWER_BASE:
    case STATE_POWER_VISUAL:
    case STATE_POWER_STATS:
    case STATE_POWER_GRAPH:
    case STATE_POWER_CONSUMPTION:
      ledBlink(300);
      break;
    case STATE_SYSTEM_INFO:
      {
        int rainbow = (millis() / 100) % 30;
        digitalWrite(LED_BUILTIN, rainbow < 10 || (rainbow > 14 && rainbow < 20));
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
    case STATE_GAME_FLAPPY:
    case STATE_GAME_SHOOTER:
    case STATE_GAME_SNAKE:
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
  
  // Loading animation
  if (currentState == STATE_LOADING) {
    if (currentMillis - lastLoadingUpdate > 100) {
      lastLoadingUpdate = currentMillis;
      loadingFrame = (loadingFrame + 1) % 8;
      showLoadingAnimation();
    }
  }
  
  // Game update
  if (currentState == STATE_GAME_FLAPPY) {
    if (currentMillis - lastGameUpdate > gameUpdateInterval) {
      updateGame();
      drawGame();
      lastGameUpdate = currentMillis;
    }
  }
  
  // Space Shooter Update
  if (currentState == STATE_GAME_SHOOTER) {
    if (currentMillis - lastSpaceGameUpdate > spaceGameUpdateInterval) {
      updateSpaceShooter();
      drawSpaceShooter();
      lastSpaceGameUpdate = currentMillis;
    }
  }

  // Snake Update
  if (currentState == STATE_GAME_SNAKE) {
    if (currentMillis - lastSnakeUpdate > snakeGame.speed) {
      updateSnakeGame();
      drawSnakeGame();
      lastSnakeUpdate = currentMillis;
    }
  }

  // Keyboard text scrolling animation
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
        drawKeyboard();
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
    
    // TTP223 Capacitive Touch Button
    if (digitalRead(TOUCH_LEFT) == HIGH) {
      if (currentState == STATE_GAME_FLAPPY) {
        handleGameInput(-1);
      } else if (currentState == STATE_GAME_SHOOTER) {
         // Bomb / Nuke
         if (spaceGame.score >= 50) {
             spaceGame.score -= 50;
             for(int i=0; i<5; i++) spaceGame.enemies[i].active = false;
             showStatus("NUKE!", 500);
         }
      } else if (currentState == STATE_GAME_SNAKE) {
         // Slow down
         snakeGame.speed = 250;
      } else {
        handleLeft();
      }
      buttonPressed = true;
    }
    if (digitalRead(TOUCH_RIGHT) == HIGH) {
      if (currentState == STATE_GAME_FLAPPY) {
        handleGameInput(1);
      } else if (currentState == STATE_GAME_SHOOTER) {
         // Shield
         if(!spaceGame.shieldActive && spaceGame.score >= 20) {
           spaceGame.score -= 20;
           spaceGame.shieldActive = true;
           spaceGame.shieldTimer = millis();
         }
      } else if (currentState == STATE_GAME_SNAKE) {
         // Speed up
         snakeGame.speed = 50;
      } else {
        handleRight();
      }
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

// ========== ESP-NOW FUNCTIONS ==========

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

void showESPNowMenu() {
  display.clearDisplay();
  drawBatteryIndicator();
  
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
    
    if (i == 1) {
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

void showESPNowPeerOptions() {
  display.clearDisplay();
  drawBatteryIndicator();

  display.setTextSize(1);
  display.setCursor(20, 2);
  display.print("PEER OPTIONS");

  display.drawLine(0, 12, SCREEN_WIDTH, 12, SSD1306_WHITE);

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

void showESPNowPeers() {
  display.clearDisplay();
  drawBatteryIndicator();

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

  display.setCursor(2, 56);
  display.print("SEL:Options ADD:Back");

  display.display();
}

// ========== WIFI FUNCTIONS ==========

void showWiFiMenu() {
  display.clearDisplay();
  drawBatteryIndicator();
  
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

void showPowerBase() {
  display.clearDisplay();
  drawBatteryIndicator();
  
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
  
  for (int i = 0; i < fillW; i += 2) {
    display.drawLine(battX + 5 + i, battY + 5,
                    battX + 5 + i, battY + battH - 5, SSD1306_WHITE);
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
  if (batteryPercent > 80) display.print("FULL");
  else if (batteryPercent > 20) display.print("GOOD");
  else display.print("LOW!");
  
  display.display();
}

void showPowerStats() {
  display.clearDisplay();
  drawBatteryIndicator();
  
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
  
  display.setTextSize(1);
  display.setCursor(25, 2);
  display.print("POWER GRAPH");
  
  display.setCursor(40, 58);
  display.print("Now: ");
  display.print(batteryPercent);
  display.print("%");
  
  display.display();
}

void showPowerConsumption() {
  display.clearDisplay();
  drawBatteryIndicator();
  
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
  
  display.setTextSize(1);
  display.setCursor(25, 2);
  display.print("SETTINGS");
  
  drawIcon(10, 2, ICON_SETTINGS);
  
  display.drawLine(0, 12, SCREEN_WIDTH, 12, SSD1306_WHITE);
  
  const char* menuItems[] = {
    "WiFi Auto-Off",
    "System Info",
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
      preferences.begin("settings", false);
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
      currentState = STATE_MAIN_MENU;
      menuSelection = mainMenuSelection;
      menuTargetScrollY = mainMenuSelection * 22;
      showMainMenu();
      break;
  }
}

// ========== GAME FUNCTIONS ==========

void initGame() {
  game.birdX = 10;
  game.birdY = 32;
  game.birdVelocity = 0;
  game.pipeX = SCREEN_WIDTH;
  game.pipeGap = 20;
  game.score = 0;
  game.gameOver = false;
}

void updateGame() {
  if (game.gameOver) return;
  
  // Bird physics
  game.birdVelocity += 1; // gravity
  game.birdY += game.birdVelocity;
  
  // Pipe movement
  game.pipeX -= 2;
  
  // Reset pipe
  if (game.pipeX < -10) {
    game.pipeX = SCREEN_WIDTH;
    game.score++;
  }
  
  // Collision detection
  int topPipe = 15;
  int bottomPipe = topPipe + game.pipeGap;
  
  if (game.birdX + 8 > game.pipeX && game.birdX < game.pipeX + 15) {
    if (game.birdY < topPipe || game.birdY + 8 > bottomPipe) {
      game.gameOver = true;
      ledError();
      showStatus("Game Over!\nScore: " + String(game.score), 2000);
      initGame();
      currentState = STATE_MAIN_MENU;
      showMainMenu();
      return;
    }
  }
  
  // Boundary check
  if (game.birdY > SCREEN_HEIGHT || game.birdY < 0) {
    game.gameOver = true;
    ledError();
    showStatus("Game Over!\nScore: " + String(game.score), 2000);
    initGame();
    currentState = STATE_MAIN_MENU;
    showMainMenu();
    return;
  }
}

void drawGame() {
  display.clearDisplay();
  drawBatteryIndicator();
  
  display.setTextSize(1);
  display.setCursor(50, 2);
  display.print("FLAPPY BIRD");
  display.setCursor(105, 2);
  display.print(game.score);
  
  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
  
  // Draw bird
  display.fillCircle(10, game.birdY, 4, SSD1306_WHITE);
  
  // Draw pipes
  int topPipe = 15;
  int bottomPipe = topPipe + game.pipeGap;
  
  display.drawLine(game.pipeX, 10, game.pipeX, topPipe, SSD1306_WHITE);
  display.drawLine(game.pipeX + 14, 10, game.pipeX + 14, topPipe, SSD1306_WHITE);
  display.drawLine(game.pipeX, bottomPipe, game.pipeX, SCREEN_HEIGHT, SSD1306_WHITE);
  display.drawLine(game.pipeX + 14, bottomPipe, game.pipeX + 14, SCREEN_HEIGHT, SSD1306_WHITE);
  
  display.setCursor(2, 56);
  display.print("TOUCH:Jump BACK:Exit");
  
  display.display();
}

void handleGameInput(int direction) {
  if (direction < 0) {
    game.birdVelocity = -5; // Jump
    ledQuickFlash();
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
    {"Flappy Bird", ICON_GAME},
    {"Space Shooter", ICON_SHOOTER},
    {"Snake", ICON_SNAKE},
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
    case 4: // Flappy Bird
      initGame();
      previousState = currentState;
      currentState = STATE_GAME_FLAPPY;
      lastGameUpdate = millis();
      drawGame();
      break;
    case 5: // Space Shooter
      initSpaceShooter();
      previousState = currentState;
      currentState = STATE_GAME_SHOOTER;
      lastSpaceGameUpdate = millis();
      drawSpaceShooter();
      break;
    case 6: // Snake
      initSnakeGame();
      previousState = currentState;
      currentState = STATE_GAME_SNAKE;
      lastSnakeUpdate = millis();
      drawSnakeGame();
      break;
    case 7: // ESP-NOW
      espnowMenuSelection = 0;
      previousState = currentState;
      currentState = STATE_ESP_NOW_MENU;
      showESPNowMenu();
      break;
    case 8: // Settings
      settingsMenuSelection = 0;
      previousState = currentState;
      currentState = STATE_SETTINGS_MENU;
      showSettingsMenu();
      break;
  }
}

// ==========================================
// IMPLEMENTATION OF MISSING FUNCTIONS
// ==========================================

void drawBatteryIndicator() {
  int battX = SCREEN_WIDTH - 22;
  int battY = 2;

  // Draw battery outline
  display.drawRect(battX, battY, 18, 8, SSD1306_WHITE);
  display.fillRect(battX + 18, battY + 2, 2, 4, SSD1306_WHITE);

  // Draw battery level
  int fill = map(batteryPercent, 0, 100, 0, 14);
  if (fill > 0) {
    display.fillRect(battX + 2, battY + 2, fill, 4, SSD1306_WHITE);
  }
}

void drawWiFiSignalBars() {
  int rssi = WiFi.RSSI();
  int bars = 0;

  if (rssi > -55) bars = 4;
  else if (rssi > -65) bars = 3;
  else if (rssi > -75) bars = 2;
  else if (rssi > -85) bars = 1;

  int x = SCREEN_WIDTH - 35;
  int y = 8; // Bottom alignment

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
  // Clear a portion of the screen or popup
  int boxW = SCREEN_WIDTH - 20;
  int boxH = 40;
  int boxX = 10;
  int boxY = (SCREEN_HEIGHT - boxH) / 2;

  display.fillRect(boxX, boxY, boxW, boxH, SSD1306_BLACK);
  display.drawRect(boxX, boxY, boxW, boxH, SSD1306_WHITE);

  display.setCursor(boxX + 5, boxY + 5);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Simple word wrap or just print
  display.print(message);
  display.display();

  if (delayMs > 0) {
    delay(delayMs);
  }
}

void showProgressBar(String title, int percent) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(title);

  int barX = 10;
  int barY = 30;
  int barW = SCREEN_WIDTH - 20;
  int barH = 10;

  display.drawRect(barX, barY, barW, barH, SSD1306_WHITE);

  int fillW = map(percent, 0, 100, 0, barW - 4);
  if (fillW > 0) {
    display.fillRect(barX + 2, barY + 2, fillW, barH - 4, SSD1306_WHITE);
  }

  display.setCursor(SCREEN_WIDTH / 2 - 10, barY + 15);
  display.print(percent);
  display.print("%");

  display.display();
}

void showLoadingAnimation() {
  display.clearDisplay();
  drawBatteryIndicator();

  display.setCursor(35, 25);
  display.print("Loading...");

  // Simple spinner
  int cx = SCREEN_WIDTH / 2;
  int cy = SCREEN_HEIGHT / 2 + 10;
  int r = 8;

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

void updateBatteryLevel() {
    // Stub: Read battery voltage if hardware supports it
    // For now we keep the stub values
    // batteryPercent = ...
}

// ==========================================
// NETWORK AND SYSTEM FUNCTIONS
// ==========================================

void checkWiFiTimeout() {
  if (WiFi.status() == WL_CONNECTED && (millis() - lastWiFiActivity > wifiTimeout)) {
    Serial.println("WiFi Timeout - Disconnecting");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    showStatus("WiFi Auto-Off", 2000);
    ledQuickFlash();
  }
}

void forgetNetwork() {
  WiFi.disconnect(true, true);
  preferences.begin("wifi-creds", false);
  preferences.clear();
  preferences.end();

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
    preferences.begin("wifi-creds", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.end();

    showStatus("Connected!", 1500);
    currentState = STATE_MAIN_MENU;
    showMainMenu();
  } else {
    showStatus("Failed!", 1500);
    currentState = STATE_WIFI_MENU;
    showWiFiMenu();
  }
}

void updatePowerConsumption() {
    // Basic estimation
    wifiPowerDraw = (WiFi.status() == WL_CONNECTED) ? 80 : 10;
    displayPowerDraw = 20; // Approx for OLED
    cpuPowerDraw = 30 + (cpuFreq / 10);
    totalPowerDraw = wifiPowerDraw + displayPowerDraw + cpuPowerDraw;
}

// ==========================================
// INPUT HANDLING AND KEYBOARD FUNCTIONS
// ==========================================

const char* getCurrentKey() {
  if (currentKeyboardMode == MODE_LOWER) {
    return keyboardLower[cursorY][cursorX];
  } else if (currentKeyboardMode == MODE_UPPER) {
    return keyboardUpper[cursorY][cursorX];
  } else if (currentKeyboardMode == MODE_NUMBERS) {
    return keyboardNumbers[cursorY][cursorX];
  } else {
    return keyboardMac[cursorY][cursorX];
  }
}

void toggleKeyboardMode() {
  if (keyboardContext == CONTEXT_ESPNOW_MAC) {
    // Only one mode for MAC
    return;
  }

  if (currentKeyboardMode == MODE_LOWER) {
    currentKeyboardMode = MODE_UPPER;
  } else if (currentKeyboardMode == MODE_UPPER) {
    currentKeyboardMode = MODE_NUMBERS;
  } else {
    currentKeyboardMode = MODE_LOWER;
  }
  drawKeyboard();
}

void drawKeyboard() {
  display.clearDisplay();
  drawBatteryIndicator();

  // Draw Input Field
  display.drawRect(2, 2, SCREEN_WIDTH - 4, 14, SSD1306_WHITE);

  display.setCursor(5, 5);
  String displayText = "";
  if (keyboardContext == CONTEXT_WIFI_PASSWORD) {
     for(unsigned int i=0; i<passwordInput.length(); i++) displayText += "*";
  } else {
     displayText = userInput;
  }

  // Handle scrolling of input text
  int maxChars = 18;
  if (displayText.length() > maxChars) {
      displayText = displayText.substring(displayText.length() - maxChars);
  }
  display.print(displayText);

  int startY = 20;
  int keyW = 11;
  int keyH = 10;
  int gap = 1;

  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 10; c++) {
      int x = 2 + c * (keyW + gap);
      int y = startY + r * (keyH + gap);

      const char* keyLabel;
      if (keyboardContext == CONTEXT_ESPNOW_MAC) {
         keyLabel = keyboardMac[r][c];
      } else if (currentKeyboardMode == MODE_LOWER) {
         keyLabel = keyboardLower[r][c];
      } else if (currentKeyboardMode == MODE_UPPER) {
         keyLabel = keyboardUpper[r][c];
      } else {
         keyLabel = keyboardNumbers[r][c];
      }

      if (r == cursorY && c == cursorX) {
        display.fillRect(x, y, keyW, keyH, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.drawRect(x, y, keyW, keyH, SSD1306_WHITE);
        display.setTextColor(SSD1306_WHITE);
      }

      display.setCursor(x + 3, y + 1);
      display.print(keyLabel);
    }
  }

  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 56);
  if (keyboardContext == CONTEXT_ESPNOW_MAC) {
     display.print("MAC Address Input");
  } else {
     display.print("SEL:Type #:Mode");
  }

  display.display();
}

void handleKeyPress() {
  const char* key = getCurrentKey();

  if (strcmp(key, "OK") == 0) {
    if (keyboardContext == CONTEXT_CHAT) {
      sendToGemini();
    } else if (keyboardContext == CONTEXT_ESPNOW_MESSAGE) {
      // Send message logic
      currentState = STATE_ESP_NOW_SEND;
      uint8_t mac[6];
      memcpy(mac, peers[espnowPeerSelection].macAddr, 6);
      sendESPNowMessage(userInput, mac);
      userInput = "";
    }
  } else if (strcmp(key, "<") == 0) {
    if (userInput.length() > 0) {
      userInput.remove(userInput.length() - 1);
    }
    drawKeyboard();
  } else if (strcmp(key, "#") == 0) {
    toggleKeyboardMode();
  } else {
    userInput += key;
    drawKeyboard();
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
    drawKeyboard();
  } else if (strcmp(key, "#") == 0) {
    toggleKeyboardMode();
  } else {
    passwordInput += key;
    drawKeyboard();
  }
}

void handleUp() {
  lastWiFiActivity = millis();

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
        showWiFiMenu();
      }
      break;
    case STATE_WIFI_SCAN:
      if (selectedNetwork > 0) {
        selectedNetwork--;
        if (selectedNetwork < wifiPage * wifiPerPage) {
          wifiPage--;
        }
        displayWiFiNetworks();
      }
      break;
    case STATE_SETTINGS_MENU:
      if (settingsMenuSelection > 0) {
        settingsMenuSelection--;
        showSettingsMenu();
      }
      break;
    case STATE_POWER_BASE:
      if (powerMenuSelection > 0) {
        powerMenuSelection--;
        showPowerBase();
      }
      break;
    case STATE_ESP_NOW_MENU:
      if (espnowMenuSelection > 0) {
        espnowMenuSelection--;
        showESPNowMenu();
      }
      break;
    case STATE_ESP_NOW_PEERS:
      if (espnowPeerSelection > 0) {
        espnowPeerSelection--;
        showESPNowPeers();
      }
      break;
    case STATE_ESP_NOW_PEER_OPTIONS:
      if (espnowPeerOptionsSelection > 0) {
        espnowPeerOptionsSelection--;
        showESPNowPeerOptions();
      }
      break;
    case STATE_ESP_NOW_SEND:
      if (espnowPeerSelection > 0) {
        espnowPeerSelection--;
        showESPNowSend();
      }
      break;
    case STATE_ESP_NOW_INBOX:
      if (espnowInboxSelection > 0) {
        espnowInboxSelection--;
        showESPNowInbox();
      }
      break;
    case STATE_API_SELECT:
      if (menuSelection > 0) {
        menuSelection--;
        showAPISelect();
      }
      break;
    case STATE_KEYBOARD:
    case STATE_PASSWORD_INPUT:
      if (cursorY > 0) {
        cursorY--;
        drawKeyboard();
      }
      break;
    case STATE_CALCULATOR:
      if (cursorY > 0) {
        cursorY--;
        showCalculator();
      }
      break;
    case STATE_CHAT_RESPONSE:
      if (scrollOffset > 0) {
        scrollOffset -= 10;
        displayResponse();
      }
      break;
    case STATE_GAME_FLAPPY:
        handleGameInput(-1);
        break;
    case STATE_GAME_SHOOTER:
        if (spaceGame.playerY > 10) spaceGame.playerY -= 4;
        break;
    case STATE_GAME_SNAKE:
        if (snakeGame.dirY != 1) { snakeGame.dirX = 0; snakeGame.dirY = -1; }
        break;
  }
}

void handleDown() {
  lastWiFiActivity = millis();

  switch(currentState) {
    case STATE_MAIN_MENU:
      if (menuSelection < 8) { // 9 items
        menuSelection++;
        menuTargetScrollY = menuSelection * 22;
        menuTextScrollX = 0;
        lastMenuTextScrollTime = millis();
      }
      break;
    case STATE_WIFI_MENU:
      if (menuSelection < 2) {
        menuSelection++;
        showWiFiMenu();
      }
      break;
    case STATE_WIFI_SCAN:
      if (selectedNetwork < networkCount - 1) {
        selectedNetwork++;
        if (selectedNetwork >= (wifiPage + 1) * wifiPerPage) {
          wifiPage++;
        }
        displayWiFiNetworks();
      }
      break;
    case STATE_SETTINGS_MENU:
      if (settingsMenuSelection < 2) {
        settingsMenuSelection++;
        showSettingsMenu();
      }
      break;
    case STATE_POWER_BASE:
      if (powerMenuSelection < 4) {
        powerMenuSelection++;
        showPowerBase();
      }
      break;
    case STATE_ESP_NOW_MENU:
      if (espnowMenuSelection < 3) {
        espnowMenuSelection++;
        showESPNowMenu();
      }
      break;
    case STATE_ESP_NOW_PEERS:
      if (espnowPeerSelection < peerCount - 1) {
        espnowPeerSelection++;
        showESPNowPeers();
      }
      break;
    case STATE_ESP_NOW_PEER_OPTIONS:
      if (espnowPeerOptionsSelection < 2) {
        espnowPeerOptionsSelection++;
        showESPNowPeerOptions();
      }
      break;
    case STATE_ESP_NOW_SEND:
      if (espnowPeerSelection < peerCount - 1) {
        espnowPeerSelection++;
        showESPNowSend();
      }
      break;
    case STATE_ESP_NOW_INBOX:
      if (espnowInboxSelection < inboxCount - 1) {
        espnowInboxSelection++;
        showESPNowInbox();
      }
      break;
    case STATE_API_SELECT:
      if (menuSelection < 1) {
        menuSelection++;
        showAPISelect();
      }
      break;
    case STATE_KEYBOARD:
    case STATE_PASSWORD_INPUT:
      if (cursorY < 2) {
        cursorY++;
        drawKeyboard();
      }
      break;
    case STATE_CALCULATOR:
      if (cursorY < 3) {
        cursorY++;
        showCalculator();
      }
      break;
    case STATE_CHAT_RESPONSE:
      scrollOffset += 10;
      displayResponse();
      break;
    case STATE_GAME_FLAPPY:
        // Down does nothing or maybe duck?
        break;
    case STATE_GAME_SHOOTER:
        if (spaceGame.playerY < SCREEN_HEIGHT - 10) spaceGame.playerY += 4;
        break;
    case STATE_GAME_SNAKE:
        if (snakeGame.dirY != -1) { snakeGame.dirX = 0; snakeGame.dirY = 1; }
        break;
  }
}

void handleLeft() {
  lastWiFiActivity = millis();

  switch(currentState) {
    case STATE_KEYBOARD:
    case STATE_PASSWORD_INPUT:
      if (cursorX > 0) {
        cursorX--;
        drawKeyboard();
      }
      break;
    case STATE_CALCULATOR:
      if (cursorX > 0) {
        cursorX--;
        showCalculator();
      }
      break;
    case STATE_GAME_FLAPPY:
      handleGameInput(-1); // Jump
      break;
    case STATE_GAME_SHOOTER:
        if (spaceGame.playerX > 0) spaceGame.playerX -= 4;
        break;
    case STATE_GAME_SNAKE:
        if (snakeGame.dirX != 1) { snakeGame.dirX = -1; snakeGame.dirY = 0; }
        break;
  }
}

void handleRight() {
  lastWiFiActivity = millis();

  switch(currentState) {
    case STATE_KEYBOARD:
    case STATE_PASSWORD_INPUT:
      if (cursorX < 9) {
        cursorX++;
        drawKeyboard();
      }
      break;
    case STATE_CALCULATOR:
      if (cursorX < 3) {
        cursorX++;
        showCalculator();
      }
      break;
    case STATE_GAME_FLAPPY:
      // No right action
      break;
    case STATE_GAME_SHOOTER:
        if (spaceGame.playerX < SCREEN_WIDTH - 8) spaceGame.playerX += 4;
        break;
    case STATE_GAME_SNAKE:
        if (snakeGame.dirX != -1) { snakeGame.dirX = 1; snakeGame.dirY = 0; }
        break;
  }
}

void handleSelect() {
  lastWiFiActivity = millis();

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
          currentState = STATE_PASSWORD_INPUT;
          keyboardContext = CONTEXT_WIFI_PASSWORD;
          cursorX = 0;
          cursorY = 0;
          drawKeyboard();
        } else {
          connectToWiFi(selectedSSID, "");
        }
      }
      break;
    case STATE_SETTINGS_MENU:
      handleSettingsMenuSelect();
      break;
    case STATE_POWER_BASE:
      handlePowerBaseSelect();
      break;
    case STATE_ESP_NOW_MENU:
      switch(espnowMenuSelection) {
        case 0:
          currentState = STATE_ESP_NOW_SEND;
          showESPNowSend();
          break;
        case 1:
          currentState = STATE_ESP_NOW_INBOX;
          showESPNowInbox();
          break;
        case 2:
          currentState = STATE_ESP_NOW_PEERS;
          showESPNowPeers();
          break;
        case 3:
          currentState = STATE_MAIN_MENU;
          showMainMenu();
          break;
      }
      break;
    case STATE_ESP_NOW_PEERS:
      if (peerCount == 0) {
         // Add peer
         currentState = STATE_KEYBOARD;
         keyboardContext = CONTEXT_ESPNOW_MAC;
         currentKeyboardMode = MODE_MAC;
         userInput = "";
         cursorX = 0; cursorY = 0;
         drawKeyboard();
      } else {
         currentState = STATE_ESP_NOW_PEER_OPTIONS;
         espnowPeerOptionsSelection = 0;
         showESPNowPeerOptions();
      }
      break;
    case STATE_ESP_NOW_PEER_OPTIONS:
      if (espnowPeerOptionsSelection == 0) {
          // Rename (Not fully implemented, maybe go to keyboard)
          showStatus("Not impl.", 1000);
      } else if (espnowPeerOptionsSelection == 1) {
          deletePeer(espnowPeerSelection);
          currentState = STATE_ESP_NOW_PEERS;
          showESPNowPeers();
      } else {
          currentState = STATE_ESP_NOW_PEERS;
          showESPNowPeers();
      }
      break;
    case STATE_ESP_NOW_SEND:
      if (peerCount > 0) {
        currentState = STATE_KEYBOARD;
        keyboardContext = CONTEXT_ESPNOW_MESSAGE;
        userInput = "";
        cursorX = 0; cursorY = 0;
        drawKeyboard();
      }
      break;
    case STATE_ESP_NOW_INBOX:
       if (inboxCount > 0) {
         inbox[espnowInboxSelection].read = true;
         // Show full message (maybe overlay)
         showStatus(inbox[espnowInboxSelection].text, 3000);
         showESPNowInbox();
       }
       break;
    case STATE_API_SELECT:
      handleAPISelectSelect();
      break;
    case STATE_KEYBOARD:
      if (keyboardContext == CONTEXT_ESPNOW_MAC) {
        handleMacAddressKeyPress();
      } else {
        handleKeyPress();
      }
      break;
    case STATE_PASSWORD_INPUT:
      handlePasswordKeyPress();
      break;
    case STATE_CALCULATOR:
      handleCalculatorInput();
      break;
    case STATE_GAME_FLAPPY:
      handleGameInput(-1); // Jump
      break;
    case STATE_GAME_SHOOTER:
      // Shoot is usually handled by select in other games or maybe button select?
      // We can put shooting here
      for(int i=0; i<5; i++) {
        if(!spaceGame.bullets[i].active) {
          spaceGame.bullets[i].x = spaceGame.playerX + 4;
          spaceGame.bullets[i].y = spaceGame.playerY;
          spaceGame.bullets[i].active = true;
          break;
        }
      }
      break;
    case STATE_GAME_SNAKE:
      // Maybe pause?
      break;
  }
}

void handleBackButton() {
  lastWiFiActivity = millis();

  switch(currentState) {
    case STATE_WIFI_MENU:
    case STATE_CALCULATOR:
    case STATE_POWER_BASE:
    case STATE_SETTINGS_MENU:
    case STATE_ESP_NOW_MENU:
    case STATE_GAME_FLAPPY:
    case STATE_GAME_SHOOTER:
    case STATE_GAME_SNAKE:
      currentState = STATE_MAIN_MENU;
      menuSelection = mainMenuSelection;
      menuTargetScrollY = mainMenuSelection * 22;
      showMainMenu();
      break;
    case STATE_WIFI_SCAN:
      currentState = STATE_WIFI_MENU;
      showWiFiMenu();
      break;
    case STATE_POWER_VISUAL:
    case STATE_POWER_STATS:
    case STATE_POWER_GRAPH:
    case STATE_POWER_CONSUMPTION:
      currentState = STATE_POWER_BASE;
      showPowerBase();
      break;
    case STATE_ESP_NOW_SEND:
    case STATE_ESP_NOW_INBOX:
    case STATE_ESP_NOW_PEERS:
      currentState = STATE_ESP_NOW_MENU;
      showESPNowMenu();
      break;
    case STATE_ESP_NOW_PEER_OPTIONS:
      currentState = STATE_ESP_NOW_PEERS;
      showESPNowPeers();
      break;
    case STATE_KEYBOARD:
      if (keyboardContext == CONTEXT_ESPNOW_MAC) {
         currentState = STATE_ESP_NOW_PEERS;
         showESPNowPeers();
      } else if (keyboardContext == CONTEXT_ESPNOW_MESSAGE) {
         currentState = STATE_ESP_NOW_SEND;
         showESPNowSend();
      } else {
         currentState = STATE_MAIN_MENU;
         showMainMenu();
      }
      break;
    case STATE_PASSWORD_INPUT:
      currentState = STATE_WIFI_SCAN;
      displayWiFiNetworks();
      break;
    case STATE_CHAT_RESPONSE:
      currentState = STATE_KEYBOARD;
      drawKeyboard();
      break;
    case STATE_SYSTEM_INFO:
      currentState = STATE_SETTINGS_MENU;
      showSettingsMenu();
      break;
    case STATE_API_SELECT:
      currentState = STATE_MAIN_MENU;
      showMainMenu();
      break;
  }
}

// ==========================================
// AI AND SCREEN REFRESH LOGIC
// ==========================================

void refreshCurrentScreen() {
  switch(currentState) {
    case STATE_MAIN_MENU: showMainMenu(); break;
    case STATE_WIFI_MENU: showWiFiMenu(); break;
    case STATE_WIFI_SCAN: displayWiFiNetworks(); break;
    case STATE_CALCULATOR: showCalculator(); break;
    case STATE_POWER_BASE: showPowerBase(); break;
    case STATE_POWER_VISUAL: showPowerVisual(); break;
    case STATE_POWER_STATS: showPowerStats(); break;
    case STATE_POWER_GRAPH: showPowerGraph(); break;
    case STATE_POWER_CONSUMPTION: showPowerConsumption(); break;
    case STATE_API_SELECT: showAPISelect(); break;
    case STATE_SYSTEM_INFO: showSystemInfo(); break;
    case STATE_SETTINGS_MENU: showSettingsMenu(); break;
    case STATE_ESP_NOW_MENU: showESPNowMenu(); break;
    case STATE_ESP_NOW_SEND: showESPNowSend(); break;
    case STATE_ESP_NOW_INBOX: showESPNowInbox(); break;
    case STATE_ESP_NOW_PEERS: showESPNowPeers(); break;
    case STATE_ESP_NOW_PEER_OPTIONS: showESPNowPeerOptions(); break;
    case STATE_LOADING: showLoadingAnimation(); break;
    case STATE_KEYBOARD: drawKeyboard(); break;
    case STATE_PASSWORD_INPUT: drawKeyboard(); break;
    case STATE_CHAT_RESPONSE: displayResponse(); break;
    case STATE_GAME_FLAPPY: drawGame(); break;
    case STATE_GAME_SHOOTER: drawSpaceShooter(); break;
    case STATE_GAME_SNAKE: drawSnakeGame(); break;
    default: showMainMenu(); break;
  }
}

void displayResponse() {
  display.clearDisplay();
  drawBatteryIndicator();

  display.setCursor(0, 0);
  display.setTextSize(1);

  int y = 12 - scrollOffset;

  // Wrap text manually or let GFX do it. GFX does it if wrap is true.
  // But we need to handle scrolling, so we might need to draw text offset by scrollOffset.
  display.setCursor(0, y);
  display.print(aiResponse);

  // Scroll bar
  if (aiResponse.length() > 100) { // arbitrary threshold for scrollbar
      int totalH = (aiResponse.length() / 21 + 1) * 8;
      int visibleH = SCREEN_HEIGHT - 12;
      if (totalH > visibleH) {
         int scrollH = (visibleH * visibleH) / totalH;
         int scrollY = 12 + (scrollOffset * visibleH) / totalH;
         display.fillRect(SCREEN_WIDTH - 2, scrollY, 2, scrollH, SSD1306_WHITE);
      }
  }

  display.display();
}

void sendToGemini() {
  if (WiFi.status() != WL_CONNECTED) {
    showStatus("No WiFi!", 2000);
    return;
  }

  currentState = STATE_LOADING;
  loadingFrame = 0;
  showLoadingAnimation();

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  String apiKey = (selectedAPIKey == 1) ? geminiApiKey1 : geminiApiKey2;
  String url = String(geminiEndpoint) + "?key=" + apiKey;

  // JSON Payload
  JsonDocument doc;
  JsonObject content = doc["contents"].add();
  JsonObject parts = content["parts"].add();
  parts["text"] = userInput;

  String requestBody;
  serializeJson(doc, requestBody);

  Serial.println("Sending to Gemini...");

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(requestBody);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Response received");

    // Parse JSON
    JsonDocument responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);

    if (!error) {
      if (responseDoc.containsKey("candidates")) {
         const char* text = responseDoc["candidates"][0]["content"]["parts"][0]["text"];
         aiResponse = String(text);
      } else {
         aiResponse = "Error: Invalid response format";
      }
    } else {
      aiResponse = "Error: JSON Parsing failed";
    }
  } else {
    aiResponse = "Error: HTTP " + String(httpResponseCode);
  }

  http.end();

  currentState = STATE_CHAT_RESPONSE;
  scrollOffset = 0;
  displayResponse();
}

// ==========================================
// SPACE SHOOTER LOGIC
// ==========================================

void initSpaceShooter() {
  spaceGame.playerX = SCREEN_WIDTH / 2;
  spaceGame.playerY = SCREEN_HEIGHT - 10;
  spaceGame.score = 0;
  spaceGame.level = 1;
  spaceGame.gameOver = false;
  spaceGame.shieldActive = false;
  spaceGame.lastEnemySpawn = 0;

  for(int i=0; i<5; i++) {
    spaceGame.bullets[i].active = false;
    spaceGame.enemies[i].active = false;
  }
}

void updateSpaceShooter() {
  if (spaceGame.gameOver) return;

  // Update Bullets
  for(int i=0; i<5; i++) {
    if(spaceGame.bullets[i].active) {
      spaceGame.bullets[i].y -= 4;
      if(spaceGame.bullets[i].y < 0) spaceGame.bullets[i].active = false;
    }
  }

  // Spawn Enemies
  if(millis() - spaceGame.lastEnemySpawn > (2000 / spaceGame.level)) {
    for(int i=0; i<5; i++) {
      if(!spaceGame.enemies[i].active) {
        spaceGame.enemies[i].x = random(0, SCREEN_WIDTH - 8);
        spaceGame.enemies[i].y = 0;
        spaceGame.enemies[i].active = true;
        spaceGame.lastEnemySpawn = millis();
        break;
      }
    }
  }

  // Update Enemies
  for(int i=0; i<5; i++) {
    if(spaceGame.enemies[i].active) {
      spaceGame.enemies[i].y += 1 + (spaceGame.level / 2);

      // Collision with Player
      if(spaceGame.enemies[i].x < spaceGame.playerX + 8 &&
         spaceGame.enemies[i].x + 8 > spaceGame.playerX &&
         spaceGame.enemies[i].y < spaceGame.playerY + 8 &&
         spaceGame.enemies[i].y + 8 > spaceGame.playerY) {

         if(!spaceGame.shieldActive) {
           spaceGame.gameOver = true;
           ledError();
           showStatus("GAME OVER\nScore: " + String(spaceGame.score), 2000);
           currentState = STATE_MAIN_MENU;
           showMainMenu();
           return;
         } else {
           // Shield destroys enemy
           spaceGame.enemies[i].active = false;
           spaceGame.score += 5;
           ledQuickFlash();
         }
      }

      // Collision with Bullets
      for(int j=0; j<5; j++) {
        if(spaceGame.bullets[j].active) {
          if(spaceGame.bullets[j].x >= spaceGame.enemies[i].x &&
             spaceGame.bullets[j].x <= spaceGame.enemies[i].x + 8 &&
             spaceGame.bullets[j].y >= spaceGame.enemies[i].y &&
             spaceGame.bullets[j].y <= spaceGame.enemies[i].y + 8) {

             spaceGame.enemies[i].active = false;
             spaceGame.bullets[j].active = false;
             spaceGame.score += 10;
             if(spaceGame.score % 100 == 0) spaceGame.level++;
             ledQuickFlash();
          }
        }
      }

      if(spaceGame.enemies[i].y > SCREEN_HEIGHT) {
        spaceGame.enemies[i].active = false;
        // Penalty?
      }
    }
  }

  // Shield Timer
  if(spaceGame.shieldActive && millis() - spaceGame.shieldTimer > 3000) {
    spaceGame.shieldActive = false;
  }
}

void drawSpaceShooter() {
  display.clearDisplay();
  drawBatteryIndicator();

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Lvl:");
  display.print(spaceGame.level);
  display.setCursor(50, 0);
  display.print("Score:");
  display.print(spaceGame.score);

  display.drawLine(0, 8, SCREEN_WIDTH, 8, SSD1306_WHITE);

  // Draw Player
  if(spaceGame.shieldActive) {
    display.drawCircle(spaceGame.playerX + 4, spaceGame.playerY + 4, 6, SSD1306_WHITE);
  }
  // Simple ship
  display.fillTriangle(spaceGame.playerX + 4, spaceGame.playerY,
                       spaceGame.playerX, spaceGame.playerY + 8,
                       spaceGame.playerX + 8, spaceGame.playerY + 8, SSD1306_WHITE);

  // Draw Bullets
  for(int i=0; i<5; i++) {
    if(spaceGame.bullets[i].active) {
      display.drawLine(spaceGame.bullets[i].x, spaceGame.bullets[i].y,
                       spaceGame.bullets[i].x, spaceGame.bullets[i].y + 2, SSD1306_WHITE);
    }
  }

  // Draw Enemies
  for(int i=0; i<5; i++) {
    if(spaceGame.enemies[i].active) {
      display.fillRect(spaceGame.enemies[i].x, spaceGame.enemies[i].y, 8, 8, SSD1306_WHITE);
      // Eyes
      display.drawPixel(spaceGame.enemies[i].x + 2, spaceGame.enemies[i].y + 2, SSD1306_BLACK);
      display.drawPixel(spaceGame.enemies[i].x + 5, spaceGame.enemies[i].y + 2, SSD1306_BLACK);
    }
  }

  display.display();
}

// ==========================================
// SNAKE GAME LOGIC
// ==========================================

void spawnFood() {
  bool valid = false;
  while(!valid) {
    snakeGame.foodX = random(0, SCREEN_WIDTH / 4) * 4;
    snakeGame.foodY = random(3, SCREEN_HEIGHT / 4) * 4; // Avoid top bar
    valid = true;
    for(int i=0; i<snakeGame.length; i++) {
      if(snakeGame.x[i] == snakeGame.foodX && snakeGame.y[i] == snakeGame.foodY) {
        valid = false;
        break;
      }
    }
  }
}

void initSnakeGame() {
  snakeGame.length = 3;
  snakeGame.x[0] = 64; snakeGame.y[0] = 32;
  snakeGame.x[1] = 60; snakeGame.y[1] = 32;
  snakeGame.x[2] = 56; snakeGame.y[2] = 32;
  snakeGame.dirX = 1;
  snakeGame.dirY = 0;
  snakeGame.score = 0;
  snakeGame.gameOver = false;
  snakeGame.speed = 150;
  spawnFood();
}

void updateSnakeGame() {
  if (snakeGame.gameOver) return;

  // Move body
  for (int i = snakeGame.length - 1; i > 0; i--) {
    snakeGame.x[i] = snakeGame.x[i - 1];
    snakeGame.y[i] = snakeGame.y[i - 1];
  }

  // Move head
  snakeGame.x[0] += snakeGame.dirX * 4;
  snakeGame.y[0] += snakeGame.dirY * 4;

  // Wrap around
  if (snakeGame.x[0] >= SCREEN_WIDTH) snakeGame.x[0] = 0;
  if (snakeGame.x[0] < 0) snakeGame.x[0] = SCREEN_WIDTH - 4;
  if (snakeGame.y[0] >= SCREEN_HEIGHT) snakeGame.y[0] = 12; // Below status bar
  if (snakeGame.y[0] < 12) snakeGame.y[0] = SCREEN_HEIGHT - 4;

  // Check self collision
  for (int i = 1; i < snakeGame.length; i++) {
    if (snakeGame.x[0] == snakeGame.x[i] && snakeGame.y[0] == snakeGame.y[i]) {
      snakeGame.gameOver = true;
      ledError();
      showStatus("GAME OVER\nScore: " + String(snakeGame.score), 2000);
      currentState = STATE_MAIN_MENU;
      showMainMenu();
      return;
    }
  }

  // Check food collision
  if (abs(snakeGame.x[0] - snakeGame.foodX) < 4 && abs(snakeGame.y[0] - snakeGame.foodY) < 4) {
    snakeGame.score += 10;
    if (snakeGame.length < SNAKE_MAX_LEN) {
      snakeGame.length++;
    }
    snakeGame.speed = max(50, snakeGame.speed - 2);
    spawnFood();
    ledQuickFlash();
  }
}

void drawSnakeGame() {
  display.clearDisplay();
  drawBatteryIndicator();

  display.setTextSize(1);
  display.setCursor(2, 0);
  display.print("SNAKE");
  display.setCursor(80, 0);
  display.print("Sc:");
  display.print(snakeGame.score);

  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);

  // Draw Snake
  for(int i=0; i<snakeGame.length; i++) {
    display.fillRect(snakeGame.x[i], snakeGame.y[i], 3, 3, SSD1306_WHITE);
  }

  // Draw Food
  display.fillRect(snakeGame.foodX, snakeGame.foodY, 3, 3, SSD1306_WHITE);

  display.display();
}
