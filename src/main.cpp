#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include <time.h>
#include "secrets.h"

// NeoPixel LED settings
#define NEOPIXEL_PIN 48
#define NEOPIXEL_COUNT 1
Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// NeoPixel Effect State
enum NeoPixelMode { NEO_OFF, NEO_STATIC, NEO_BREATHE, NEO_RAINBOW, NEO_BLINK };
NeoPixelMode neoMode = NEO_OFF;
uint32_t neoColor = 0;
unsigned long neoTimer = 0;
int neoBrightness = 0;
int neoDirection = 1;

void setNeoPixelMode(NeoPixelMode mode, uint32_t color = 0);
void updateNeoPixel();

// Performance settings
#define CPU_FREQ 240
#define I2C_FREQ 1000000
#define TARGET_FPS 60
#define FRAME_TIME (1000 / TARGET_FPS)

// NTP Settings (WIB: UTC+7)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 25200;
const int   daylightOffset_sec = 0;

// Delta Time
unsigned long lastFrameMillis = 0;
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
  STATE_GAME_SELECT,
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

// WiFi Auto-off
unsigned long lastWiFiActivity = 0;

// Game Effects
#define MAX_PARTICLES 40
struct Particle {
  float x, y;
  float vx, vy;
  int life;
  bool active;
  uint16_t color;
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
        float speed = random(5, 25) / 10.0;
        particles[j].vx = cos(angle) * speed;
        particles[j].vy = sin(angle) * speed;
        particles[j].life = random(15, 40);
        particles[j].color = SSD1306_WHITE;
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
      if (particles[i].life % 2 == 0) {
        display.drawPixel((int)particles[i].x, (int)particles[i].y, particles[i].color);
      }
    }
  }
}

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

enum KeyboardMode { MODE_LOWER, MODE_UPPER, MODE_NUMBERS };
KeyboardMode currentKeyboardMode = MODE_LOWER;

enum KeyboardContext { CONTEXT_CHAT, CONTEXT_WIFI_PASSWORD };
KeyboardContext keyboardContext = CONTEXT_CHAT;

// Space Invaders State
#define MAX_ENEMIES 15
#define MAX_BULLETS 5
#define MAX_ENEMY_BULLETS 10
#define MAX_POWERUPS 3

struct SpaceInvaders {
  float playerX, playerY;
  int playerWidth, playerHeight;
  int lives, score, level;
  bool gameOver;
  int weaponType, shieldTime;
  struct Enemy { float x, y; int width, height; bool active; int type; int health; };
  Enemy enemies[MAX_ENEMIES];
  struct Bullet { float x, y; bool active; };
  Bullet bullets[MAX_BULLETS];
  Bullet enemyBullets[MAX_ENEMY_BULLETS];
  struct PowerUp { float x, y; int type; bool active; };
  PowerUp powerups[MAX_POWERUPS];
  float enemyDirection;
  unsigned long lastEnemyMove, lastEnemyShoot, lastSpawn;
} invaders;

// Side Scroller State
#define MAX_OBSTACLES 8
#define MAX_SCROLLER_BULLETS 8
#define MAX_SCROLLER_ENEMIES 6

struct SideScroller {
  float playerX, playerY;
  int playerWidth, playerHeight;
  int lives, score, level;
  bool gameOver;
  int weaponLevel, specialCharge;
  bool shieldActive;
  struct Obstacle { float x, y; int width, height; bool active; float scrollSpeed; };
  Obstacle obstacles[MAX_OBSTACLES];
  struct ScrollerBullet { float x, y; int dirX, dirY; bool active; int damage; };
  ScrollerBullet bullets[MAX_SCROLLER_BULLETS];
  struct ScrollerEnemy { float x, y; int width, height; bool active; int health, type, dirY; };
  ScrollerEnemy enemies[MAX_SCROLLER_ENEMIES];
  struct ScrollerEnemyBullet { float x, y; bool active; };
  ScrollerEnemyBullet enemyBullets[MAX_OBSTACLES];
  unsigned long lastMove, lastShoot, lastEnemySpawn, lastObstacleSpawn;
  int scrollOffset;
} scroller;

// Pong State
struct Pong {
  float ballX, ballY, ballDirX, ballDirY, ballSpeed;
  float paddle1Y, paddle2Y;
  int paddleWidth, paddleHeight;
  int score1, score2;
  bool gameOver;
  bool aiMode;
  int difficulty;
} pong;
unsigned long pongResetTimer = 0;
bool pongResetting = false;

// Racing Game State
#define MAX_RACING_CARS 5
struct RacingGame {
  float roadOffset;
  float playerX;
  float speed;      // 0 to 250 km/h
  float rpm;        // 0 to 9000
  int gear;         // 1 to 6
  bool clutch;      // True if pressed
  float distance;
  int score;
  bool gameOver;

  struct Opponent {
    float x, y;
    float speed;
    bool active;
    int type; // 0=Traffic, 1=Racer
  };
  Opponent opponents[MAX_RACING_CARS];

  unsigned long lastSpawn;
  float trackCurve;
  float currentCurve;
} racing;

AppState currentState = STATE_MAIN_MENU;
AppState previousState = STATE_MAIN_MENU;

// UI Transition
enum TransitionState { TRANSITION_NONE, TRANSITION_OUT, TRANSITION_IN };
TransitionState transitionState = TRANSITION_NONE;
AppState transitionTargetState;
float transitionProgress = 0.0;
const float transitionSpeed = 3.0f;

// Menu Logic
float menuScrollY = 0;
float menuTargetScrollY = 0;
int mainMenuSelection = 0;
int cursorX = 0, cursorY = 0;
String userInput = "";
String passwordInput = "";
String selectedSSID = "";
String aiResponse = "";
int scrollOffset = 0;
int menuSelection = 0;
unsigned long lastDebounce = 0;
const unsigned long debounceDelay = 150;

unsigned long lastGameUpdate = 0;
const int gameUpdateInterval = FRAME_TIME;

// Icons
const unsigned char ICON_WIFI[] PROGMEM = { 0x00, 0x3C, 0x42, 0x99, 0x24, 0x00, 0x18, 0x00 };
const unsigned char ICON_CHAT[] PROGMEM = { 0x7E, 0xFF, 0xC3, 0xC3, 0xC3, 0xFF, 0x18, 0x00 };
const unsigned char ICON_GAME[] PROGMEM = { 0x3C, 0x42, 0x99, 0xA5, 0xA5, 0x99, 0x42, 0x3C };
const unsigned char ICON_VIDEO[] PROGMEM = { 0x7E, 0x81, 0x81, 0xBD, 0xBD, 0x81, 0x81, 0x7E };

const unsigned char* videoFrames[] = { NULL };
int videoTotalFrames = 0;
int videoCurrentFrame = 0;
unsigned long lastVideoFrameTime = 0;
const int videoFrameDelay = 60;

// Forward declarations
void showMainMenu(int x_offset = 0);
void showWiFiMenu(int x_offset = 0);
void showAPISelect(int x_offset = 0);
void showGameSelect(int x_offset = 0);
void showLoadingAnimation(int x_offset = 0);
void showProgressBar(String title, int percent);
void displayWiFiNetworks(int x_offset = 0);
void drawKeyboard(int x_offset = 0);
void handleMainMenuSelect();
void handleWiFiMenuSelect();
void handleAPISelectSelect();
void handleGameSelectSelect();
void handleKeyPress();
void handlePasswordKeyPress();
void handleBackButton();
void connectToWiFi(String ssid, String password);
void scanWiFiNetworks();
void displayResponse();
void showStatus(String message, int delayMs);
void forgetNetwork();
void drawHeader();
void drawWiFiSignalBars();
void drawIcon(int x, int y, const unsigned char* icon);
void sendToGemini();
const char* getCurrentKey();
void toggleKeyboardMode();
void changeState(AppState newState);

// Game Funcs
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
void initRacing();
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

void refreshCurrentScreen() {
  int x_offset = 0;
  if (transitionState == TRANSITION_OUT) {
    x_offset = -SCREEN_WIDTH * transitionProgress;
  } else if (transitionState == TRANSITION_IN) {
    x_offset = SCREEN_WIDTH * (1.0 - transitionProgress);
  }

  switch(currentState) {
    case STATE_MAIN_MENU: showMainMenu(x_offset); break;
    case STATE_WIFI_MENU: showWiFiMenu(x_offset); break;
    case STATE_WIFI_SCAN: displayWiFiNetworks(x_offset); break;
    case STATE_API_SELECT: showAPISelect(x_offset); break;
    case STATE_GAME_SELECT: showGameSelect(x_offset); break;
    case STATE_LOADING: showLoadingAnimation(x_offset); break;
    case STATE_KEYBOARD: drawKeyboard(x_offset); break;
    case STATE_PASSWORD_INPUT: drawKeyboard(x_offset); break;
    case STATE_CHAT_RESPONSE: displayResponse(); break;
    case STATE_GAME_SPACE_INVADERS:
    case STATE_GAME_SIDE_SCROLLER:
    case STATE_GAME_PONG:
    case STATE_GAME_RACING:
    case STATE_VIDEO_PLAYER:
      break;
    default: showMainMenu(x_offset); break;
  }
}

// Global Transition
void changeState(AppState newState) {
  if (transitionState == TRANSITION_NONE && currentState != newState) {
    transitionTargetState = newState;
    transitionState = TRANSITION_OUT;
    transitionProgress = 0.0f;
    previousState = currentState;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // MAX PERFORMANCE
  setCpuFrequencyMhz(CPU_FREQ);
  
  Serial.println("\n=== ESP32-S3 Ultimate Edition ===");
  Serial.printf("CPU Freq: %d MHz\n", getCpuFrequencyMhz());
  Serial.printf("PSRAM: %d KB\n", ESP.getPsramSize() / 1024);
  
  preferences.begin("wifi-creds", false);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_FREQ);
  
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  
  pinMode(TOUCH_LEFT, INPUT);
  pinMode(TOUCH_RIGHT, INPUT);
  
  pinMode(LED_BUILTIN, OUTPUT);

  pixels.begin();
  setNeoPixelMode(NEO_BREATHE, pixels.Color(0, 255, 255)); // Cyan Breathe
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.display();
  
  // NTP Setup
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  preferences.begin("wifi-creds", true);
  String savedSSID = preferences.getString("ssid", "");
  String savedPassword = preferences.getString("password", "");
  preferences.end();
  
  if (savedSSID.length() > 0) {
    showProgressBar("System Init", 0);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 15) {
      delay(500);
      attempts++;
      showProgressBar("Syncing", attempts * 6);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      setNeoPixelMode(NEO_STATIC, pixels.Color(0, 255, 0)); // Green
      showProgressBar("Ready", 100);
      delay(500);
    } else {
      setNeoPixelMode(NEO_STATIC, pixels.Color(255, 0, 0)); // Red
      showStatus("Offline Mode", 1000);
    }
  }
  
  setNeoPixelMode(NEO_RAINBOW);
  showMainMenu();
}

void setNeoPixelMode(NeoPixelMode mode, uint32_t color) {
  neoMode = mode;
  neoColor = color;
  if (mode == NEO_STATIC) {
    pixels.setPixelColor(0, color);
    pixels.show();
  }
}

void updateNeoPixel() {
  unsigned long now = millis();
  
  switch(neoMode) {
    case NEO_OFF:
      pixels.setPixelColor(0, 0);
      pixels.show();
      break;
    case NEO_STATIC:
      // Already set
      break;
    case NEO_BREATHE:
      if (now - neoTimer > 20) {
        neoTimer = now;
        neoBrightness += neoDirection * 2;
        if (neoBrightness <= 10) neoDirection = 1;
        if (neoBrightness >= 200) neoDirection = -1;

        uint8_t r = (uint8_t)((neoColor >> 16) & 0xFF) * neoBrightness / 255;
        uint8_t g = (uint8_t)((neoColor >> 8) & 0xFF) * neoBrightness / 255;
        uint8_t b = (uint8_t)(neoColor & 0xFF) * neoBrightness / 255;
        pixels.setPixelColor(0, pixels.Color(r, g, b));
        pixels.show();
      }
      break;
    case NEO_RAINBOW:
      if (now - neoTimer > 30) {
        neoTimer = now;
        neoBrightness = (neoBrightness + 1) % 256;
        // Simple hue cycle
        pixels.setPixelColor(0, pixels.gamma32(pixels.ColorHSV(neoBrightness * 256)));
        pixels.show();
      }
      break;
    case NEO_BLINK:
      if (now - neoTimer > 200) {
        neoTimer = now;
        neoDirection = !neoDirection;
        if (neoDirection) pixels.setPixelColor(0, neoColor);
        else pixels.setPixelColor(0, 0);
        pixels.show();
      }
      break;
  }
}

void loop() {
  unsigned long currentMillis = millis();
  updateNeoPixel();
  
  // LED Logic based on state
  if (currentState == STATE_LOADING) setNeoPixelMode(NEO_BLINK, pixels.Color(0, 0, 255));
  
  // Game updates
  if (currentMillis - lastGameUpdate > gameUpdateInterval) {
    if (lastFrameMillis == 0) lastFrameMillis = currentMillis;
    deltaTime = (currentMillis - lastFrameMillis) / 1000.0f;
    lastFrameMillis = currentMillis;

    // Input polling
    if (currentState == STATE_GAME_SPACE_INVADERS) handleSpaceInvadersInput();
    else if (currentState == STATE_GAME_SIDE_SCROLLER) handleSideScrollerInput();
    else if (currentState == STATE_GAME_PONG) handlePongInput();
    else if (currentState == STATE_GAME_RACING) handleRacingInput();

    switch(currentState) {
      case STATE_GAME_SPACE_INVADERS: updateSpaceInvaders(); drawSpaceInvaders(); break;
      case STATE_GAME_SIDE_SCROLLER: updateSideScroller(); drawSideScroller(); break;
      case STATE_GAME_PONG: updatePong(); drawPong(); break;
      case STATE_GAME_RACING: updateRacing(); drawRacing(); break;
    }
    lastGameUpdate = currentMillis;
  }

  if (currentState == STATE_VIDEO_PLAYER) drawVideoPlayer();

  // Transition Logic
  if (transitionState != TRANSITION_NONE) {
    transitionProgress += transitionSpeed * deltaTime;
    if (transitionProgress >= 1.0f) {
      transitionProgress = 1.0f;
      if (transitionState == TRANSITION_OUT) {
        currentState = transitionTargetState;
        transitionState = TRANSITION_IN;
        transitionProgress = 0.0f;

        if (transitionTargetState == STATE_MAIN_MENU) {
          menuSelection = mainMenuSelection;
          menuTargetScrollY = mainMenuSelection * 22;
          menuScrollY = menuTargetScrollY;
          setNeoPixelMode(NEO_RAINBOW);
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

  refreshCurrentScreen();

  // Menu Animation
  if (currentState == STATE_MAIN_MENU && transitionState == TRANSITION_NONE) {
    if (abs(menuScrollY - menuTargetScrollY) > 0.1) {
      menuScrollY += (menuTargetScrollY - menuScrollY) * 0.3;
    } else {
      menuScrollY = menuTargetScrollY;
    }
  }
  
  // Input Handling
  if (transitionState == TRANSITION_NONE && currentMillis - lastDebounce > debounceDelay) {
    bool buttonPressed = false;
    
    if (digitalRead(BTN_UP) == LOW) { handleUp(); buttonPressed = true; }
    if (digitalRead(BTN_DOWN) == LOW) { handleDown(); buttonPressed = true; }
    if (digitalRead(BTN_LEFT) == LOW) { handleLeft(); buttonPressed = true; }
    if (digitalRead(BTN_RIGHT) == LOW) { handleRight(); buttonPressed = true; }
    if (digitalRead(BTN_SELECT) == LOW) { handleSelect(); buttonPressed = true; }
    if (digitalRead(BTN_BACK) == LOW) { handleBackButton(); buttonPressed = true; }
    
    // Touch Buttons (Restricted)
    if (currentState != STATE_KEYBOARD && currentState != STATE_PASSWORD_INPUT) {
      if (digitalRead(TOUCH_LEFT) == HIGH) {
        handleLeft();
        if (currentState == STATE_GAME_SPACE_INVADERS || currentState == STATE_GAME_SIDE_SCROLLER) {
          handleSelect();
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
    }
  }
}

// ========== HEADER UI (Time + WiFi) ==========
void drawHeader() {
  // Time
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(2, 2);
    display.printf("%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  } else {
    display.setCursor(2, 2);
    display.print("Sync..");
  }

  // WiFi Signal
  if (WiFi.status() == WL_CONNECTED) {
    drawWiFiSignalBars();
  }
}

void drawWiFiSignalBars() {
  int rssi = WiFi.RSSI();
  int bars = 0;
  if (rssi > -55) bars = 4;
  else if (rssi > -65) bars = 3;
  else if (rssi > -75) bars = 2;
  else if (rssi > -85) bars = 1;

  int x = SCREEN_WIDTH - 15;
  int y = 8;
  for (int i = 0; i < 4; i++) {
    int h = (i + 1) * 2;
    if (i < bars) display.fillRect(x + (i * 3), y - h + 2, 2, h, SSD1306_WHITE);
    else display.drawRect(x + (i * 3), y - h + 2, 2, h, SSD1306_WHITE);
  }
}

void drawIcon(int x, int y, const unsigned char* icon) {
  display.drawBitmap(x, y, icon, 8, 8, SSD1306_WHITE);
}

// ========== RACING GAME ==========

void initRacing() {
  racing.roadOffset = 0;
  racing.playerX = 0; // -1.0 to 1.0 (Lane position)
  racing.speed = 0;
  racing.rpm = 1000;
  racing.gear = 1;
  racing.clutch = false;
  racing.distance = 0;
  racing.score = 0;
  racing.gameOver = false;
  racing.trackCurve = 0;
  racing.currentCurve = 0;

  for(int i=0; i<MAX_RACING_CARS; i++) {
    racing.opponents[i].active = false;
  }
  setNeoPixelMode(NEO_STATIC, pixels.Color(255, 0, 0)); // Start with Red (Brake lights)
}

void updateRacing() {
  if (racing.gameOver) return;

  updateParticles();
  if (screenShake > 0) screenShake--;

  // 1. Physics Engine
  float maxSpeed = racing.gear * 40.0f + 20.0f; // Max speed per gear
  float acceleration = (8000.0f - racing.rpm) / 5000.0f;
  if (acceleration < 0.1f) acceleration = 0.1f;

  // Engine Sound / RPM Logic
  if (digitalRead(BTN_UP) == LOW) { // Gas
    if (racing.clutch) {
      racing.rpm += 500.0f; // Rev freely
    } else {
      racing.rpm += 150.0f;
      racing.speed += acceleration * deltaTime * 20.0f;
    }
  } else {
    racing.rpm -= 200.0f;
    racing.speed -= 10.0f * deltaTime;
  }

  // Brake
  if (digitalRead(BTN_DOWN) == LOW) {
    racing.speed -= 80.0f * deltaTime;
    setNeoPixelMode(NEO_STATIC, pixels.Color(255, 0, 0)); // Red Brake Lights
  } else {
     // RPM/Speed LED effect
     float rpmRatio = racing.rpm / 8000.0f;
     uint8_t r = (uint8_t)(rpmRatio * 255);
     uint8_t g = (uint8_t)((1.0f - rpmRatio) * 255);
     setNeoPixelMode(NEO_STATIC, pixels.Color(r, g, 0));
  }

  // Clutch Logic
  if (digitalRead(BTN_SELECT) == LOW) {
    racing.clutch = true;
  } else {
    racing.clutch = false;
  }

  // Resistance
  racing.speed -= (racing.speed * 0.01f);
  racing.rpm = constrain(racing.rpm, 800, 9000);
  racing.speed = constrain(racing.speed, 0, 250);

  // Link RPM to Speed roughly if clutch engaged
  if (!racing.clutch) {
     float targetRPM = (racing.speed / maxSpeed) * 7000.0f + 1000.0f;
     racing.rpm = (racing.rpm * 0.8f) + (targetRPM * 0.2f);

     if (racing.rpm > 8500 && racing.gear < 6) {
       // Simple auto shift up for playability
       if (racing.rpm > 8000) { racing.gear++; racing.rpm = 5000; }
     }
     if (racing.rpm < 3000 && racing.gear > 1) {
       racing.gear--;
       racing.rpm = 6000;
     }
  }

  // Steering
  if (digitalRead(BTN_LEFT) == LOW) racing.playerX -= 1.5f * deltaTime;
  if (digitalRead(BTN_RIGHT) == LOW) racing.playerX += 1.5f * deltaTime;
  racing.playerX = constrain(racing.playerX, -1.0f, 1.0f);

  // Road Curve
  racing.roadOffset += racing.speed * deltaTime * 0.1f;

  // Spawn Opponents
  if (millis() - racing.lastSpawn > (3000 - racing.speed * 5)) {
     for(int i=0; i<MAX_RACING_CARS; i++) {
       if (!racing.opponents[i].active) {
         racing.opponents[i].active = true;
         racing.opponents[i].x = random(-80, 80) / 100.0f;
         racing.opponents[i].y = 0; // Horizon
         racing.opponents[i].speed = racing.speed * 0.5f + 20.0f; // Slower than player
         racing.lastSpawn = millis();
         break;
       }
     }
  }

  // Update Opponents (Fake 3D Z-movement)
  for(int i=0; i<MAX_RACING_CARS; i++) {
    if (racing.opponents[i].active) {
      // Perspective move: Y goes 0 -> 1.0
      racing.opponents[i].y += (racing.speed - racing.opponents[i].speed) * 0.0005f * deltaTime;

      // Collision
      if (racing.opponents[i].y > 0.8f && racing.opponents[i].y < 1.0f) {
         if (abs(racing.playerX - racing.opponents[i].x) < 0.3f) {
            // CRASH
            racing.speed = 0;
            screenShake = 10;
            racing.gameOver = true;
         }
      }

      if (racing.opponents[i].y > 1.2f || racing.opponents[i].y < -0.1f) {
         racing.opponents[i].active = false;
         racing.score += 10;
      }
    }
  }

  racing.distance += racing.speed * deltaTime * 0.001f;
}

void drawRacing() {
  display.clearDisplay();
  drawHeader();

  int cx = SCREEN_WIDTH / 2;
  int cy = SCREEN_HEIGHT / 2;

  int horizon = 20;

  // Draw Sky/Ground
  display.drawLine(0, horizon, SCREEN_WIDTH, horizon, SSD1306_WHITE);

  // Draw Road (Perspective)
  // Vanishing point
  int vpX = cx - (racing.playerX * 30);

  // Road Edges
  display.drawLine(vpX, horizon, 0, SCREEN_HEIGHT, SSD1306_WHITE);
  display.drawLine(vpX, horizon, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

  // Lane markers
  int roadOffset = (int)(racing.distance * 100) % 20;
  if (roadOffset < 10) {
    display.drawLine(vpX, horizon, cx, SCREEN_HEIGHT, SSD1306_WHITE);
  }

  // Draw Opponents
  for(int i=0; i<MAX_RACING_CARS; i++) {
    if (racing.opponents[i].active && racing.opponents[i].y > 0) {
      // Projection
      float z = racing.opponents[i].y;
      int w = 4 + (z * 20);
      int h = 3 + (z * 15);
      int ox = vpX + (racing.opponents[i].x - racing.playerX) * (z * 60); // Parallax
      int oy = horizon + (z * (SCREEN_HEIGHT - horizon));

      display.fillRect(ox - w/2, oy - h, w, h, SSD1306_WHITE);
    }
  }

  // Draw Player Car (Modern Sports Car Sprite)
  int carW = 24;
  int carH = 14;
  int carX = cx - carW/2 + random(-screenShake, screenShake+1);
  int carY = SCREEN_HEIGHT - carH - 2;

  // Main Body
  display.fillRoundRect(carX, carY + 4, carW, carH - 4, 2, SSD1306_BLACK);
  display.drawRoundRect(carX, carY + 4, carW, carH - 4, 2, SSD1306_WHITE);

  // Cabin (narrower top)
  display.fillRect(carX + 4, carY, carW - 8, 5, SSD1306_BLACK);
  display.drawRect(carX + 4, carY, carW - 8, 5, SSD1306_WHITE);

  // Spoiler
  display.drawLine(carX, carY+2, carX+carW, carY+2, SSD1306_WHITE);

  // Tyres
  display.fillRect(carX - 2, carY + 8, 3, 5, SSD1306_WHITE);
  display.fillRect(carX + carW - 1, carY + 8, 3, 5, SSD1306_WHITE);

  // Brake Lights
  if (digitalRead(BTN_DOWN) == LOW) { // Braking
     display.fillRect(carX + 2, carY + 5, 6, 3, SSD1306_WHITE);
     display.fillRect(carX + carW - 8, carY + 5, 6, 3, SSD1306_WHITE);
  } else {
     display.drawRect(carX + 2, carY + 5, 6, 3, SSD1306_WHITE);
     display.drawRect(carX + carW - 8, carY + 5, 6, 3, SSD1306_WHITE);
  }

  // HUD
  display.setTextSize(1);
  display.setCursor(2, 55);
  display.printf("%d KM/H", (int)racing.speed);
  display.setCursor(80, 55);
  display.printf("G:%d", racing.gear);
  display.setCursor(110, 55);
  if(racing.clutch) display.print("C");

  // RPM Bar (Dynamic)
  int rpmW = map(racing.rpm, 0, 9000, 0, 50);
  display.drawRect(35, 60, 52, 4, SSD1306_WHITE);

  if (racing.rpm > 7000) {
      if ((millis() / 50) % 2 == 0) display.fillRect(36, 61, rpmW, 2, SSD1306_WHITE);
  } else {
      display.fillRect(36, 61, rpmW, 2, SSD1306_WHITE);
  }

  if (racing.gameOver) {
    display.fillRect(20, 20, 88, 30, SSD1306_BLACK);
    display.drawRect(20, 20, 88, 30, SSD1306_WHITE);
    display.setCursor(35, 25);
    display.print("CRASHED!");
    display.setCursor(35, 38);
    display.print("Score: "); display.print(racing.score);
  }

  display.display();
}

void handleRacingInput() {
  // Input handled in updateRacing for physics simulation
}

// ========== SPACE INVADERS ==========

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
  
  for (int i = 0; i < MAX_ENEMIES; i++) invaders.enemies[i].active = false;
  
  for (int i = 0; i < 5; i++) {
    invaders.enemies[i].active = true;
    invaders.enemies[i].x = 10 + i * 20;
    invaders.enemies[i].y = 15;
    invaders.enemies[i].width = 8;
    invaders.enemies[i].height = 6;
    invaders.enemies[i].type = random(0, 3);
    invaders.enemies[i].health = invaders.enemies[i].type + 1;
  }
  
  for (int i = 0; i < MAX_BULLETS; i++) invaders.bullets[i].active = false;
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) invaders.enemyBullets[i].active = false;
  for (int i = 0; i < MAX_POWERUPS; i++) invaders.powerups[i].active = false;

  setNeoPixelMode(NEO_RAINBOW);
}

void updateSpaceInvaders() {
  if (invaders.gameOver) return;
  
  unsigned long now = millis();
  updateParticles();
  if (screenShake > 0) screenShake--;
  if (invaders.shieldTime > 0) invaders.shieldTime--;
  
  float enemySpeed = 5.0f + (invaders.level * 2.0f);
  bool hitEdge = false;

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
        invaders.enemies[i].y += 4;
        if (invaders.enemies[i].y >= invaders.playerY) {
          invaders.lives--;
          screenShake = 10;
          if (invaders.lives <= 0) invaders.gameOver = true;
        }
      }
    }
  }
  
  // Enemy Shooting
  if (now - invaders.lastEnemyShoot > 1000) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
      if (invaders.enemies[i].active && random(0, 5) == 0) {
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

  // Bullets
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (invaders.bullets[i].active) {
      invaders.bullets[i].y -= 150.0f * deltaTime;
      if (invaders.bullets[i].y < 0) invaders.bullets[i].active = false;

      // Collision
      for(int j=0; j<MAX_ENEMIES; j++) {
         if(invaders.enemies[j].active &&
            invaders.bullets[i].x >= invaders.enemies[j].x &&
            invaders.bullets[i].x <= invaders.enemies[j].x + 8 &&
            invaders.bullets[i].y >= invaders.enemies[j].y &&
            invaders.bullets[i].y <= invaders.enemies[j].y + 6) {

            invaders.bullets[i].active = false;
            invaders.enemies[j].health--;
            if(invaders.enemies[j].health <= 0) {
              invaders.enemies[j].active = false;
              invaders.score += 10;
              spawnExplosion(invaders.enemies[j].x, invaders.enemies[j].y, 5);
            }
            break;
         }
      }
    }
  }
  
  // Enemy Bullets
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (invaders.enemyBullets[i].active) {
      invaders.enemyBullets[i].y += 90.0f * deltaTime;
      if (invaders.enemyBullets[i].y > SCREEN_HEIGHT) invaders.enemyBullets[i].active = false;

      if (invaders.shieldTime == 0 &&
          invaders.enemyBullets[i].x >= invaders.playerX &&
          invaders.enemyBullets[i].x <= invaders.playerX + invaders.playerWidth &&
          invaders.enemyBullets[i].y >= invaders.playerY &&
          invaders.enemyBullets[i].y <= invaders.playerY + invaders.playerHeight) {

           invaders.enemyBullets[i].active = false;
           invaders.lives--;
           screenShake = 6;
           if(invaders.lives <= 0) invaders.gameOver = true;
      }
    }
  }
  
  // Respawn
  bool allDead = true;
  for (int i = 0; i < MAX_ENEMIES; i++) if (invaders.enemies[i].active) allDead = false;
  if (allDead && now - invaders.lastSpawn > 2000) {
     invaders.level++;
     for (int i = 0; i < 5; i++) {
       invaders.enemies[i].active = true;
       invaders.enemies[i].x = 10 + i * 20;
       invaders.enemies[i].y = 15;
       invaders.enemies[i].type = random(0,3);
       invaders.enemies[i].health = invaders.enemies[i].type + 1;
     }
     invaders.lastSpawn = now;
  }
}

void drawSpaceInvaders() {
  display.clearDisplay();
  drawHeader();
  
  // HUD
  display.setCursor(0, 10);
  display.printf("L:%d S:%d", invaders.lives, invaders.score);
  
  // Player
  display.drawTriangle(invaders.playerX+4, invaders.playerY, invaders.playerX, invaders.playerY+6, invaders.playerX+8, invaders.playerY+6, SSD1306_WHITE);
  
  // Enemies
  for(int i=0; i<MAX_ENEMIES; i++) {
    if(invaders.enemies[i].active) {
       display.drawRect(invaders.enemies[i].x, invaders.enemies[i].y, 8, 6, SSD1306_WHITE);
    }
  }
  
  // Bullets
  for(int i=0; i<MAX_BULLETS; i++) {
    if(invaders.bullets[i].active) display.drawPixel(invaders.bullets[i].x, invaders.bullets[i].y, SSD1306_WHITE);
  }
  
  drawParticles();
  if (invaders.gameOver) {
    display.setCursor(30,30); display.print("GAME OVER");
  }
  display.display();
}

void handleSpaceInvadersInput() {
  if (invaders.gameOver) return;
  float speed = 120.0f;
  if (digitalRead(BTN_LEFT) == LOW) invaders.playerX -= speed * deltaTime;
  if (digitalRead(BTN_RIGHT) == LOW) invaders.playerX += speed * deltaTime;
  invaders.playerX = constrain(invaders.playerX, 0, SCREEN_WIDTH - 8);

  // Shoot
  if (digitalRead(BTN_SELECT) == LOW || digitalRead(TOUCH_LEFT) == HIGH) {
      for (int i = 0; i < MAX_BULLETS; i++) {
        if (!invaders.bullets[i].active) {
          invaders.bullets[i].x = invaders.playerX + 4;
          invaders.bullets[i].y = invaders.playerY;
          invaders.bullets[i].active = true;
          break;
        }
      }
  }
}


// ========== SIDE SCROLLER ==========

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
  
  // Spawn obstacles
  if (now - scroller.lastObstacleSpawn > 2000) {
    for (int i = 0; i < MAX_OBSTACLES; i++) {
      if (!scroller.obstacles[i].active) {
        scroller.obstacles[i].x = SCREEN_WIDTH;
        scroller.obstacles[i].y = random(15, SCREEN_HEIGHT - 20);
        scroller.obstacles[i].width = 8;
        scroller.obstacles[i].height = 12;
        scroller.obstacles[i].scrollSpeed = 1.0 + (random(0, 10) / 5.0);
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

      // Collision Player vs Obstacle
      if (abs(scroller.playerX - scroller.obstacles[i].x) < 8 &&
          abs(scroller.playerY - scroller.obstacles[i].y) < 8) {
          if (!scroller.shieldActive) {
             scroller.lives--;
             screenShake = 6;
             if (scroller.lives <= 0) scroller.gameOver = true;
          }
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
      
      if (scroller.enemies[i].y < 12 || scroller.enemies[i].y > SCREEN_HEIGHT - 8) {
        scroller.enemies[i].dirY *= -1;
      }
      
      if (scroller.enemies[i].x < -8) scroller.enemies[i].active = false;
      
      // Collision Player vs Enemy
      if (abs(scroller.playerX - scroller.enemies[i].x) < 8 &&
          abs(scroller.playerY - scroller.enemies[i].y) < 8) {
          if (!scroller.shieldActive) {
             scroller.lives--;
             screenShake = 6;
             if (scroller.lives <= 0) scroller.gameOver = true;
          }
          scroller.enemies[i].active = false;
      }
    }
  }
  
  // Player Bullets
  float playerBulletSpeed = 200.0f;
  for (int i = 0; i < MAX_SCROLLER_BULLETS; i++) {
    if (scroller.bullets[i].active) {
      scroller.bullets[i].x += scroller.bullets[i].dirX * playerBulletSpeed * deltaTime;
      scroller.bullets[i].y += scroller.bullets[i].dirY * playerBulletSpeed * deltaTime;
      
      if (scroller.bullets[i].x > SCREEN_WIDTH) scroller.bullets[i].active = false;

      // Collision Bullet vs Enemy
      for (int j = 0; j < MAX_SCROLLER_ENEMIES; j++) {
        if (scroller.enemies[j].active) {
          if (abs(scroller.bullets[i].x - scroller.enemies[j].x) < 8 &&
              abs(scroller.bullets[i].y - scroller.enemies[j].y) < 8) {
            
            scroller.bullets[i].active = false;
            scroller.enemies[j].health -= scroller.bullets[i].damage;
            
            if (scroller.enemies[j].health <= 0) {
              scroller.enemies[j].active = false;
              spawnExplosion(scroller.enemies[j].x, scroller.enemies[j].y, 6);
              scroller.score += 15;
            }
            break;
          }
        }
      }
    }
  }
}

void drawSideScroller() {
  display.clearDisplay();
  drawHeader();

  // HUD
  display.setCursor(0, 10);
  display.printf("L:%d S:%d W:%d", scroller.lives, scroller.score, scroller.weaponLevel);
  
  // Stars / Background
  for (int i = 0; i < SCREEN_WIDTH; i += 16) {
    int x = (i + scroller.scrollOffset) % SCREEN_WIDTH;
    display.drawPixel(x, 12 + random(0, 3), SSD1306_WHITE);
    display.drawPixel(x, SCREEN_HEIGHT - 2 - random(0, 3), SSD1306_WHITE);
  }
  
  // Player
  display.drawTriangle(
    scroller.playerX + 4, scroller.playerY,
    scroller.playerX - 4, scroller.playerY - 3,
    scroller.playerX - 4, scroller.playerY + 3,
    SSD1306_WHITE
  );
  
  // Obstacles
  for (int i = 0; i < MAX_OBSTACLES; i++) {
    if (scroller.obstacles[i].active) {
      display.drawCircle(scroller.obstacles[i].x, scroller.obstacles[i].y, 5, SSD1306_WHITE);
    }
  }
  
  // Enemies
  for (int i = 0; i < MAX_SCROLLER_ENEMIES; i++) {
    if (scroller.enemies[i].active) {
      display.drawRect(scroller.enemies[i].x - 4, scroller.enemies[i].y - 4, 8, 8, SSD1306_WHITE);
    }
  }
  
  // Bullets
  for (int i = 0; i < MAX_SCROLLER_BULLETS; i++) {
    if (scroller.bullets[i].active) {
      display.drawLine(scroller.bullets[i].x, scroller.bullets[i].y,
                       scroller.bullets[i].x - 3, scroller.bullets[i].y, SSD1306_WHITE);
    }
  }

  drawParticles();
  
  if (scroller.gameOver) {
    display.setCursor(30, 25);
    display.print("GAME OVER");
  }
  
  display.display();
}

void handleSideScrollerInput() {
  if (scroller.gameOver) return;
  float speed = 110.0f;

  if (digitalRead(BTN_LEFT) == LOW) scroller.playerX -= speed * deltaTime;
  if (digitalRead(BTN_RIGHT) == LOW) scroller.playerX += speed * deltaTime;
  if (digitalRead(BTN_UP) == LOW) scroller.playerY -= speed * deltaTime;
  if (digitalRead(BTN_DOWN) == LOW) scroller.playerY += speed * deltaTime;

  scroller.playerX = constrain(scroller.playerX, 0, SCREEN_WIDTH - 8);
  scroller.playerY = constrain(scroller.playerY, 12, SCREEN_HEIGHT - 8);

  // Shoot
  if (digitalRead(BTN_SELECT) == LOW || digitalRead(TOUCH_LEFT) == HIGH) {
     for (int i = 0; i < MAX_SCROLLER_BULLETS; i++) {
        if (!scroller.bullets[i].active) {
          scroller.bullets[i].x = scroller.playerX + 4;
          scroller.bullets[i].y = scroller.playerY;
          scroller.bullets[i].dirX = 1;
          scroller.bullets[i].dirY = 0;
          scroller.bullets[i].damage = 1;
          scroller.bullets[i].active = true;
          break;
        }
      }
  }
}


// ========== PONG ==========

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
}

void updatePong() {
  if (pong.gameOver) return;

  updateParticles();
  if (screenShake > 0) screenShake--;
  
  if (!pongResetting) {
    float effectiveSpeed = pong.ballSpeed * 60.0f;
    pong.ballX += pong.ballDirX * effectiveSpeed * deltaTime;
    pong.ballY += pong.ballDirY * effectiveSpeed * deltaTime;
  }
  
  // Collision top/bottom
  if (pong.ballY <= 12) {
    pong.ballY = 12; pong.ballDirY *= -1;
  }
  if (pong.ballY >= SCREEN_HEIGHT - 2) {
    pong.ballY = SCREEN_HEIGHT - 2; pong.ballDirY *= -1;
  }
  
  // Paddles
  // Left
  if (pong.ballX <= 6 && pong.ballX >= 2) {
    if (pong.ballY >= pong.paddle1Y - 2 && pong.ballY <= pong.paddle1Y + pong.paddleHeight + 2) {
      pong.ballDirX = 1;
      pong.ballSpeed = min(pong.ballSpeed + 0.2f, 5.0f);
      spawnExplosion(pong.ballX, pong.ballY, 3);
    }
  }
  // Right
  if (pong.ballX >= SCREEN_WIDTH - 6 && pong.ballX <= SCREEN_WIDTH - 2) {
    if (pong.ballY >= pong.paddle2Y - 2 && pong.ballY <= pong.paddle2Y + pong.paddleHeight + 2) {
      pong.ballDirX = -1;
      pong.ballSpeed = min(pong.ballSpeed + 0.2f, 5.0f);
      spawnExplosion(pong.ballX, pong.ballY, 3);
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
  
  // AI
  if (pong.aiMode) {
    float targetY = pong.ballY - pong.paddleHeight / 2.0;
    float diff = targetY - pong.paddle2Y;
    float aiSpeed = pong.difficulty * 45.0f;
    if (abs(diff) > 1.0) {
      if (diff > 0) pong.paddle2Y += min(aiSpeed * deltaTime, diff);
      else pong.paddle2Y += max(-aiSpeed * deltaTime, diff);
    }
  }
  
  pong.paddle1Y = constrain(pong.paddle1Y, 12, SCREEN_HEIGHT - pong.paddleHeight);
  pong.paddle2Y = constrain(pong.paddle2Y, 12, SCREEN_HEIGHT - pong.paddleHeight);
}

void drawPong() {
  display.clearDisplay();
  drawHeader();
  
  display.setCursor(30, 15); display.print(pong.score1);
  display.setCursor(SCREEN_WIDTH - 40, 15); display.print(pong.score2);
  
  display.drawRect(2, pong.paddle1Y, pong.paddleWidth, pong.paddleHeight, SSD1306_WHITE);
  display.drawRect(SCREEN_WIDTH - 6, pong.paddle2Y, pong.paddleWidth, pong.paddleHeight, SSD1306_WHITE);
  
  display.drawCircle(pong.ballX, pong.ballY, 2, SSD1306_WHITE);
  
  drawParticles();
  
  if (pong.gameOver) {
    display.setCursor(30, 30);
    if (pong.score1 >= 10) display.print("PLAYER 1 WINS");
    else display.print("PLAYER 2 WINS");
  }
  
  display.display();
}

void handlePongInput() {
  if (pong.gameOver) return;
  float speed = 130.0f;
  if (digitalRead(BTN_UP) == LOW) pong.paddle1Y -= speed * deltaTime;
  if (digitalRead(BTN_DOWN) == LOW) pong.paddle1Y += speed * deltaTime;
}

// ========== MAIN MENU ==========

void showMainMenu(int x_offset) {
  display.clearDisplay();

  struct MenuItem {
    const char* text;
    const unsigned char* icon;
  };

  MenuItem menuItems[] = {
    {"Chat AI", ICON_CHAT},
    {"WiFi", ICON_WIFI},
    {"Games", ICON_GAME},
    {"Video", ICON_VIDEO}
  };

  int numItems = 4;
  int screenCenterY = SCREEN_HEIGHT / 2;

  for (int i = 0; i < numItems; i++) {
    float itemY = screenCenterY + (i * 24) - menuScrollY; // Increased spacing
    float distance = abs(itemY - screenCenterY);

    if (itemY > -25 && itemY < SCREEN_HEIGHT + 25) {
      if (i == menuSelection) {
        // Modern Selection Box
        display.fillRoundRect(x_offset + 5, screenCenterY - 12, SCREEN_WIDTH - 10, 24, 8, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
        drawIcon(x_offset + 12, screenCenterY - 4, menuItems[i].icon); // Invert color logic needed here?
        // Actually drawing bitmap in BLACK on WHITE bg is tricky with 1-bit,
        // simplest is to draw text in black. Icons we just skip or draw inverted manually.
        // Let's keep it simple: White text on black is standard, inverse is highlighted.
        display.setCursor(x_offset + 30, screenCenterY - 6);
        display.setTextSize(2);
        display.print(menuItems[i].text);
        display.setTextColor(SSD1306_WHITE);
      } else {
        // Unselected
        display.drawRoundRect(x_offset + 10, itemY - 10, SCREEN_WIDTH - 20, 20, 4, SSD1306_WHITE);
        display.setCursor(x_offset + 35, itemY - 4);
        display.setTextSize(1);
        display.print(menuItems[i].text);
      }
    }
  }

  drawHeader();
  display.display();
}

void handleMainMenuSelect() {
  mainMenuSelection = menuSelection;
  switch(mainMenuSelection) {
    case 0: // Chat AI
      if (WiFi.status() == WL_CONNECTED) {
        changeState(STATE_API_SELECT);
      } else {
        showStatus("Connect WiFi", 1000);
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
  }
}

// ========== GAME SELECT ==========

void showGameSelect(int x_offset) {
  display.clearDisplay();
  drawHeader();
  
  display.setCursor(x_offset + 25, 12);
  display.println("SELECT GAME");
  display.drawLine(0, 22, SCREEN_WIDTH, 22, SSD1306_WHITE);
  
  const char* games[] = {
    "Neon Invaders",
    "Astro Rush",
    "Vector Pong",
    "Turbo Racing",
    "Back"
  };
  
  for (int i = 0; i < 5; i++) {
    display.setCursor(x_offset + 10, 26 + i * 10);
    if (i == menuSelection) display.print("> ");
    else display.print("  ");
    display.print(games[i]);
  }
  display.display();
}

void handleGameSelectSelect() {
  switch(menuSelection) {
    case 0: initSpaceInvaders(); changeState(STATE_GAME_SPACE_INVADERS); break;
    case 1: initSideScroller(); changeState(STATE_GAME_SIDE_SCROLLER); break;
    case 2: initPong(); changeState(STATE_GAME_PONG); break;
    case 3: initRacing(); changeState(STATE_GAME_RACING); break;
    case 4: changeState(STATE_MAIN_MENU); break;
  }
}

// ========== WiFi & Keyboard & Others ==========
// (Retaining logic but ensuring battery icon calls are gone and header is used)

void showWiFiMenu(int x_offset) {
  display.clearDisplay();
  drawHeader();
  
  display.setCursor(x_offset + 25, 15);
  display.print("WiFi SETTINGS");
  display.drawLine(0, 25, SCREEN_WIDTH, 25, SSD1306_WHITE);
  
  const char* menuItems[] = {"Scan Networks", "Forget Network", "Back"};
  for (int i = 0; i < 3; i++) {
    display.setCursor(10, 35 + i * 10);
    if (i == menuSelection) display.print("> ");
    else display.print("  ");
    display.print(menuItems[i]);
  }
  display.display();
}

void handleWiFiMenuSelect() {
  switch(menuSelection) {
    case 0: scanWiFiNetworks(); break;
    case 1: forgetNetwork(); break;
    case 2: changeState(STATE_MAIN_MENU); break;
  }
}

void displayWiFiNetworks(int x_offset) {
  display.clearDisplay();
  drawHeader();
  
  display.setCursor(x_offset + 5, 12);
  display.printf("Found: %d", networkCount);
  
  if (networkCount == 0) {
    display.setCursor(10, 30); display.print("No networks");
  } else {
    int startIdx = wifiPage * wifiPerPage;
    int endIdx = min(networkCount, startIdx + wifiPerPage);
    
    for (int i = startIdx; i < endIdx; i++) {
      int y = 24 + (i - startIdx) * 10;
      if (i == selectedNetwork) {
        display.fillRect(0, y, SCREEN_WIDTH, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }
      display.setCursor(2, y+1);
      String s = networks[i].ssid;
      if(s.length()>15) s = s.substring(0,15);
      display.print(s);
      display.setTextColor(SSD1306_WHITE);
    }
  }
  display.display();
}

void showAPISelect(int x_offset) {
  display.clearDisplay();
  drawHeader();
  display.setCursor(x_offset + 10, 15); display.print("SELECT AI MODEL");
  
  display.setCursor(10, 30);
  if(menuSelection==0) display.print("> API 1"); else display.print("  API 1");
  if(selectedAPIKey==1) display.print(" [*]");
  
  display.setCursor(10, 40);
  if(menuSelection==1) display.print("> API 2"); else display.print("  API 2");
  if(selectedAPIKey==2) display.print(" [*]");
  
  display.display();
}

void handleAPISelectSelect() {
  selectedAPIKey = (menuSelection == 0) ? 1 : 2;
  userInput = "";
  keyboardContext = CONTEXT_CHAT;
  changeState(STATE_KEYBOARD);
}

void drawKeyboard(int x_offset) {
  display.clearDisplay();
  drawHeader();
  
  // Input Box
  display.drawRect(x_offset + 2, 10, SCREEN_WIDTH - 4, 12, SSD1306_WHITE);
  display.setCursor(x_offset + 4, 12);
  
  String txt = (keyboardContext == CONTEXT_WIFI_PASSWORD) ? passwordInput : userInput;
  if(keyboardContext == CONTEXT_WIFI_PASSWORD) {
    String masked = "";
    for(int i=0; i<txt.length(); i++) masked += "*";
    txt = masked;
  }
  if(txt.length() > 18) txt = txt.substring(txt.length()-18);
  display.print(txt);
  
  // Keys
  int startY = 26;
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 10; c++) {
      int x = 2 + c * 12;
      int y = startY + r * 11;
      const char* keyLabel;
      if (currentKeyboardMode == MODE_LOWER) keyLabel = keyboardLower[r][c];
      else if (currentKeyboardMode == MODE_UPPER) keyLabel = keyboardUpper[r][c];
      else keyLabel = keyboardNumbers[r][c];

      if (r == cursorY && c == cursorX) {
        display.fillRect(x, y, 11, 10, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.drawRect(x, y, 11, 10, SSD1306_WHITE);
        display.setTextColor(SSD1306_WHITE);
      }
      display.setCursor(x+3, y+1);
      display.print(keyLabel);
    }
  }
  display.setTextColor(SSD1306_WHITE);
  display.display();
}

void handleKeyPress() {
  const char* key = getCurrentKey();
  if (strcmp(key, "OK") == 0) sendToGemini();
  else if (strcmp(key, "<") == 0) { if(userInput.length()>0) userInput.remove(userInput.length()-1); }
  else if (strcmp(key, "#") == 0) toggleKeyboardMode();
  else userInput += key;
}

void handlePasswordKeyPress() {
  const char* key = getCurrentKey();
  if (strcmp(key, "OK") == 0) connectToWiFi(selectedSSID, passwordInput);
  else if (strcmp(key, "<") == 0) { if(passwordInput.length()>0) passwordInput.remove(passwordInput.length()-1); }
  else if (strcmp(key, "#") == 0) toggleKeyboardMode();
  else passwordInput += key;
}

const char* getCurrentKey() {
  if (currentKeyboardMode == MODE_LOWER) return keyboardLower[cursorY][cursorX];
  else if (currentKeyboardMode == MODE_UPPER) return keyboardUpper[cursorY][cursorX];
  else return keyboardNumbers[cursorY][cursorX];
}

void toggleKeyboardMode() {
  if (currentKeyboardMode == MODE_LOWER) currentKeyboardMode = MODE_UPPER;
  else if (currentKeyboardMode == MODE_UPPER) currentKeyboardMode = MODE_NUMBERS;
  else currentKeyboardMode = MODE_LOWER;
}


// Video Player Logic
void drawVideoPlayer() {
  if (millis() - lastVideoFrameTime > videoFrameDelay) {
    lastVideoFrameTime = millis();
    display.clearDisplay();

    if (videoTotalFrames > 0 && videoFrames[0] != NULL) {
      display.drawBitmap(0, 0, videoFrames[videoCurrentFrame], SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
      videoCurrentFrame++;
      if (videoCurrentFrame >= videoTotalFrames) videoCurrentFrame = 0;
    } else {
      display.setCursor(10, 20);
      display.println("No Video Data");
    }
    display.display();
  }
}

// ========== Gemini & Networking ==========

void sendToGemini() {
  currentState = STATE_LOADING;
  if (WiFi.status() != WL_CONNECTED) {
    aiResponse = "No WiFi!";
    currentState = STATE_CHAT_RESPONSE;
    return;
  }

  HTTPClient http;
  String url = String(geminiEndpoint) + "?key=" + ((selectedAPIKey==1)?geminiApiKey1:geminiApiKey2);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  String cleanInput = userInput;
  cleanInput.replace("\"", "\\\"");
  String payload = "{\"contents\":[{\"parts\":[{\"text\":\"" + cleanInput + "\"}]}]}";

  int code = http.POST(payload);
  if (code == 200) {
    String res = http.getString();
    StaticJsonDocument<4096> doc;
    deserializeJson(doc, res);
    aiResponse = doc["candidates"][0]["content"]["parts"][0]["text"].as<String>();
  } else {
    aiResponse = "Error: " + String(code);
  }
  http.end();
  currentState = STATE_CHAT_RESPONSE;
  scrollOffset = 0;
}

void displayResponse() {
  display.clearDisplay();
  drawHeader();
  int y = 15 - scrollOffset;
  display.setCursor(0, y);
  display.print(aiResponse);
  display.display();
}

void showStatus(String message, int delayMs) {
  display.fillRoundRect(10, 20, 108, 24, 4, SSD1306_BLACK);
  display.drawRoundRect(10, 20, 108, 24, 4, SSD1306_WHITE);
  display.setCursor(15, 28);
  display.print(message);
  display.display();
  delay(delayMs);
}

void showProgressBar(String title, int percent) {
  display.clearDisplay();
  display.setCursor(0,0); display.print(title);
  display.drawRect(10, 30, 108, 10, SSD1306_WHITE);
  display.fillRect(12, 32, percent, 6, SSD1306_WHITE);
  display.display();
}

void showLoadingAnimation(int x_offset) {
  display.clearDisplay();
  drawHeader();
  display.setCursor(40, 30); display.print("Thinking...");
  display.display();
}

void forgetNetwork() {
  WiFi.disconnect(true, true);
  preferences.begin("wifi-creds", false);
  preferences.clear();
  preferences.end();
  showStatus("Forgotten", 1000);
}

void connectToWiFi(String ssid, String password) {
  showProgressBar("Connecting", 0);
  WiFi.begin(ssid.c_str(), password.c_str());
  int t=0;
  while(WiFi.status()!=WL_CONNECTED && t<20) { delay(500); t++; showProgressBar("Connecting", t*5); }
  if(WiFi.status()==WL_CONNECTED) {
    preferences.begin("wifi-creds", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.end();
    changeState(STATE_MAIN_MENU);
  } else {
    showStatus("Failed", 1000);
    changeState(STATE_WIFI_MENU);
  }
}

// Other inputs
void handleUp() {
  if(currentState==STATE_MAIN_MENU && menuSelection>0) menuSelection--;
  else if(currentState==STATE_GAME_SELECT && menuSelection>0) menuSelection--;
  else if(currentState==STATE_WIFI_MENU && menuSelection>0) menuSelection--;
  else if(currentState==STATE_API_SELECT && menuSelection>0) menuSelection--;
  else if(currentState==STATE_WIFI_SCAN && selectedNetwork>0) selectedNetwork--;
  else if(currentState==STATE_KEYBOARD || currentState==STATE_PASSWORD_INPUT) { cursorY--; if(cursorY<0) cursorY=2; }
  else if(currentState==STATE_CHAT_RESPONSE) scrollOffset-=10;
}

void handleDown() {
  if(currentState==STATE_MAIN_MENU && menuSelection<3) menuSelection++;
  else if(currentState==STATE_GAME_SELECT && menuSelection<4) menuSelection++;
  else if(currentState==STATE_WIFI_MENU && menuSelection<2) menuSelection++;
  else if(currentState==STATE_API_SELECT && menuSelection<1) menuSelection++;
  else if(currentState==STATE_WIFI_SCAN && selectedNetwork<networkCount-1) selectedNetwork++;
  else if(currentState==STATE_KEYBOARD || currentState==STATE_PASSWORD_INPUT) { cursorY++; if(cursorY>2) cursorY=0; }
  else if(currentState==STATE_CHAT_RESPONSE) scrollOffset+=10;
}

void handleLeft() {
  if(currentState==STATE_KEYBOARD || currentState==STATE_PASSWORD_INPUT) { cursorX--; if(cursorX<0) cursorX=9; }
}

void handleRight() {
  if(currentState==STATE_KEYBOARD || currentState==STATE_PASSWORD_INPUT) { cursorX++; if(cursorX>9) cursorX=0; }
}

void handleSelect() {
  if(currentState==STATE_MAIN_MENU) handleMainMenuSelect();
  else if(currentState==STATE_GAME_SELECT) handleGameSelectSelect();
  else if(currentState==STATE_WIFI_MENU) handleWiFiMenuSelect();
  else if(currentState==STATE_WIFI_SCAN) {
     selectedSSID = networks[selectedNetwork].ssid;
     if(networks[selectedNetwork].encrypted) { passwordInput=""; keyboardContext=CONTEXT_WIFI_PASSWORD; changeState(STATE_PASSWORD_INPUT); }
     else connectToWiFi(selectedSSID, "");
  }
  else if(currentState==STATE_API_SELECT) handleAPISelectSelect();
  else if(currentState==STATE_KEYBOARD) handleKeyPress();
  else if(currentState==STATE_PASSWORD_INPUT) handlePasswordKeyPress();
}

void handleBackButton() {
  if(currentState==STATE_MAIN_MENU) {} // Do nothing
  else if(currentState==STATE_GAME_SPACE_INVADERS || currentState==STATE_GAME_SIDE_SCROLLER || currentState==STATE_GAME_PONG || currentState==STATE_GAME_RACING) changeState(STATE_GAME_SELECT);
  else if(currentState==STATE_GAME_SELECT) changeState(STATE_MAIN_MENU);
  else if(currentState==STATE_API_SELECT) changeState(STATE_MAIN_MENU);
  else if(currentState==STATE_KEYBOARD) changeState(STATE_API_SELECT); // Or wifi scan depending on context
  else if(currentState==STATE_CHAT_RESPONSE) changeState(STATE_KEYBOARD);
  else changeState(STATE_MAIN_MENU);
}

void scanWiFiNetworks() {
  int n = WiFi.scanNetworks();
  networkCount = min(n, 20);
  for(int i=0; i<networkCount; i++) {
    networks[i].ssid = WiFi.SSID(i);
    networks[i].rssi = WiFi.RSSI(i);
    networks[i].encrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
  }
  changeState(STATE_WIFI_SCAN);
}
