#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
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
#define I2C_FREQ 1000000
#define TARGET_FPS 60
#define FRAME_TIME (1000 / TARGET_FPS)

// Delta Time for smooth, frame-rate independent movement
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

// WiFi Auto-off settings
unsigned long lastWiFiActivity = 0;

// Battery monitoring
float batteryVoltage = 5.0;
int batteryPercent = 100;

// Game Effects System
#define MAX_PARTICLES 30
struct Particle {
  float x, y;
  float vx, vy;
  int life;
  bool active;
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
        float speed = random(5, 20) / 10.0;
        particles[j].vx = cos(angle) * speed;
        particles[j].vy = sin(angle) * speed;
        particles[j].life = random(10, 30);
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
      if (particles[i].life % 2 == 0) { // Flicker effect
        display.drawPixel((int)particles[i].x, (int)particles[i].y, SSD1306_WHITE);
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

enum KeyboardContext {
  CONTEXT_CHAT,
  CONTEXT_WIFI_PASSWORD
};
KeyboardContext keyboardContext = CONTEXT_CHAT;

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
};
Pong pong;
unsigned long pongResetTimer = 0;
bool pongResetting = false;

AppState currentState = STATE_MAIN_MENU;
AppState previousState = STATE_MAIN_MENU;

// UI Transition System
enum TransitionState { TRANSITION_NONE, TRANSITION_OUT, TRANSITION_IN };
TransitionState transitionState = TRANSITION_NONE;
AppState transitionTargetState;
float transitionProgress = 0.0; // 0.0 to 1.0
const float transitionSpeed = 2.5f;

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
const unsigned long debounceDelay = 150;

unsigned long lastGameUpdate = 0;
const int gameUpdateInterval = FRAME_TIME;

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

// Video Player Data
// ==========================================
// PASTE YOUR VIDEO FRAME ARRAY HERE
// Format: const unsigned char frame1[] PROGMEM = { ... };
//         const unsigned char* videoFrames[] = { frame1, frame2, ... };
// ==========================================
const unsigned char* videoFrames[] = { NULL }; // Placeholder
int videoTotalFrames = 0;
int videoCurrentFrame = 0;
unsigned long lastVideoFrameTime = 0;
const int videoFrameDelay = 70; // 25 FPS

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
    case STATE_LOADING: showLoadingAnimation(x_offset); break;
    case STATE_KEYBOARD: drawKeyboard(x_offset); break;
    case STATE_PASSWORD_INPUT: drawKeyboard(x_offset); break;
    case STATE_CHAT_RESPONSE: displayResponse(); break;
    // Game states handle their own drawing, so no call here
    case STATE_GAME_SPACE_INVADERS:
    case STATE_GAME_SIDE_SCROLLER:
    case STATE_GAME_PONG:
    case STATE_VIDEO_PLAYER:
      break;
    default: showMainMenu(x_offset); break;
  }
}
void drawBatteryIndicator();
void drawWiFiSignalBars();
void drawIcon(int x, int y, const unsigned char* icon);
void sendToGemini();
const char* getCurrentKey();
void toggleKeyboardMode();

// UI Transition Function
void changeState(AppState newState) {
  if (transitionState == TRANSITION_NONE && currentState != newState) {
    transitionTargetState = newState;
    transitionState = TRANSITION_OUT;
    transitionProgress = 0.0f;
    previousState = currentState; // Store where we came from
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

void setup() {
  Serial.begin(115200);
  delay(1000);

  // High Performance Setup for ESP32-S3 N16R8
  setCpuFrequencyMhz(CPU_FREQ);
  psramInit();
  
  Serial.println("\n=== ESP32-S3 Gaming Edition v1.0 ===");
  Serial.printf("CPU Freq: %d MHz\n", getCpuFrequencyMhz());
  Serial.printf("PSRAM Size: %d KB\n", ESP.getPsramSize() / 1024);
  
  preferences.begin("wifi-creds", false);
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
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.display();
  
  ledSuccess();
  
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
    } else {
      ledError();
      showStatus("Connection failed", 2000);
    }
  }
  
  showMainMenu();
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
  
  updateNeoPixel();

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
  
  // Game updates
  if (currentMillis - lastGameUpdate > gameUpdateInterval) {
    // Calculate Delta Time
    if (lastFrameMillis == 0) lastFrameMillis = currentMillis;
    deltaTime = (currentMillis - lastFrameMillis) / 1000.0f;
    lastFrameMillis = currentMillis;

    // Poll Game Inputs (Smooth Movement)
    if (currentState == STATE_GAME_SPACE_INVADERS) handleSpaceInvadersInput();
    else if (currentState == STATE_GAME_SIDE_SCROLLER) handleSideScrollerInput();
    else if (currentState == STATE_GAME_PONG) handlePongInput();

    switch(currentState) {
      case STATE_GAME_SPACE_INVADERS:
        updateSpaceInvaders();
        drawSpaceInvaders();
        break;
      case STATE_GAME_SIDE_SCROLLER:
        updateSideScroller();
        drawSideScroller();
        break;
      case STATE_GAME_PONG:
        updatePong();
        drawPong();
        break;
    }
    lastGameUpdate = currentMillis;
  }

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

  // Draw current screen with transition offset
  // We call this every frame now, not just on input
  refreshCurrentScreen();

  // Main Menu Animation (Only if not transitioning)
  if (currentState == STATE_MAIN_MENU && transitionState == TRANSITION_NONE) {
    if (abs(menuScrollY - menuTargetScrollY) > 0.1) {
      menuScrollY += (menuTargetScrollY - menuScrollY) * 0.3;
    } else if (menuScrollY != menuTargetScrollY) {
      menuScrollY = menuTargetScrollY;
    }
  }
  
  // Button handling (only if not transitioning)
  if (transitionState == TRANSITION_NONE && currentMillis - lastDebounce > debounceDelay) {
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
    
    // Touch buttons
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
    
    if (buttonPressed) {
      lastDebounce = currentMillis;
      ledQuickFlash();
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
  float enemySpeed = 5.0f + (invaders.level * 2.0f); // Speed in pixels per second

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
              
              spawnExplosion(invaders.enemies[j].x + 4, invaders.enemies[j].y + 3, 5);
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

  drawBatteryIndicator();
  
  // Draw HUD (Fixed position, no shake)
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print("L:");
  display.print(invaders.lives);
  display.setCursor(40, 2);
  display.print("S:");
  display.print(invaders.score);
  display.setCursor(90, 2);
  display.print("Lv:");
  display.print(invaders.level);
  
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

  drawBatteryIndicator();
  
  // Draw HUD
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print("L:");
  display.print(scroller.lives);
  display.setCursor(30, 2);
  display.print("S:");
  display.print(scroller.score);
  display.setCursor(75, 2);
  display.print("W:");
  display.print(scroller.weaponLevel);
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
}

void updatePong() {
  if (pong.gameOver) return;

  updateParticles();
  if (screenShake > 0) screenShake--;
  
  // Move ball
  if (!pongResetting) {
    float effectiveSpeed = pong.ballSpeed * 60.0f; // Base speed at 60fps
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
      spawnExplosion(pong.ballX, pong.ballY, 3);

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
      spawnExplosion(pong.ballX, pong.ballY, 3);

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
  drawBatteryIndicator();
  
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

// ========== GAME SELECT ==========

void showGameSelect(int x_offset) {
  display.clearDisplay();
  drawBatteryIndicator();
  
  display.setTextSize(1);
  display.setCursor(x_offset + 25, 2);
  display.print("SELECT GAME");
  
  drawIcon(x_offset + 10, 2, ICON_GAME);
  
  display.drawLine(x_offset + 0, 12, x_offset + SCREEN_WIDTH, 12, SSD1306_WHITE);
  
  const char* games[] = {
    "Neon Invaders",
    "Astro Rush",
    "Vector Pong",
    "Back"
  };
  
  for (int i = 0; i < 4; i++) {
    display.setCursor(x_offset + 10, 18 + i * 11);
    if (i == menuSelection) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    display.print(games[i]);
  }
  
  display.display();
}

void handleGameSelectSelect() {
  switch(menuSelection) {
    case 0:
      initSpaceInvaders();
      changeState(STATE_GAME_SPACE_INVADERS);
      break;
    case 1:
      initSideScroller();
      changeState(STATE_GAME_SIDE_SCROLLER);
      break;
    case 2:
      initPong();
      changeState(STATE_GAME_PONG);
      break;
    case 3:
      changeState(STATE_MAIN_MENU);
      break;
  }
}

// ========== WIFI FUNCTIONS ==========

void showWiFiMenu(int x_offset) {
  display.clearDisplay();
  drawBatteryIndicator();
  
  display.setTextSize(1);
  display.setCursor(x_offset + 25, 2);
  display.print("WiFi MENU");
  
  drawIcon(x_offset + 10, 2, ICON_WIFI);
  
  display.drawLine(x_offset + 0, 12, x_offset + SCREEN_WIDTH, 12, SSD1306_WHITE);
  
  display.setCursor(x_offset + 5, 16);
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
  drawBatteryIndicator();
  
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
  drawBatteryIndicator();
  
  display.setTextSize(1);
  display.setCursor(x_offset + 15, 5);
  display.print("SELECT GEMINI API");
  
  display.drawLine(x_offset + 0, 15, x_offset + SCREEN_WIDTH, 15, SSD1306_WHITE);
  
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
  cursorX = 0;
  cursorY = 0;
  currentKeyboardMode = MODE_LOWER;
  changeState(STATE_KEYBOARD);
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
  
  int numItems = sizeof(menuItems) / sizeof(MenuItem);
  int screenCenterY = SCREEN_HEIGHT / 2;
  
  for (int i = 0; i < numItems; i++) {
    float itemY = screenCenterY + (i * 22) - menuScrollY;
    float distance = abs(itemY - screenCenterY);
    
    // Simple easing function for scaling
    float scale = 1.0 - (distance / (SCREEN_HEIGHT / 2.0));
    scale = max(0.5f, scale); // Min scale

    if (itemY > -20 && itemY < SCREEN_HEIGHT + 20) {
      int itemX = x_offset + 20 + (distance / 2.5);

      if (i == menuSelection) {
        // Highlighted item
        display.drawRoundRect(x_offset + 5, screenCenterY - 12, SCREEN_WIDTH - 10, 24, 6, SSD1306_WHITE);
        drawIcon(x_offset + 12, screenCenterY - 4, menuItems[i].icon);
        display.setTextSize(2);
        display.setCursor(x_offset + 30, screenCenterY - 7);
        display.print(menuItems[i].text);
      } else {
        // Other items
        display.setTextSize(1);
        display.setCursor(itemX, itemY - 3);
        display.print(menuItems[i].text);
      }
    }
  }

  // Top and bottom status bar (fixed position)
  drawBatteryIndicator();
  if (WiFi.status() == WL_CONNECTED) {
    drawWiFiSignalBars();
  }
  
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

// ========== UTILITY FUNCTIONS ==========

void drawBatteryIndicator() {
  int battX = SCREEN_WIDTH - 22;
  int battY = 2;

  display.drawRect(battX, battY, 18, 8, SSD1306_WHITE);
  display.fillRect(battX + 18, battY + 2, 2, 4, SSD1306_WHITE);

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

void showLoadingAnimation(int x_offset) {
  display.clearDisplay();
  drawBatteryIndicator();

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
  drawBatteryIndicator();

  display.drawRect(x_offset + 2, 2, SCREEN_WIDTH - 4, 14, SSD1306_WHITE);

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
  int keyH = 10;
  int gap = 1;

  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 10; c++) {
      int x = 2 + c * (keyW + gap);
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
  display.print("SEL:Type #:Mode");

  display.display();
}

void handleKeyPress() {
  const char* key = getCurrentKey();

  if (strcmp(key, "OK") == 0) {
    if (keyboardContext == CONTEXT_CHAT) {
      sendToGemini();
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
  }
}

void handleDown() {
  switch(currentState) {
    case STATE_MAIN_MENU:
      if (menuSelection < 3) {
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
      if (menuSelection < 3) {
        menuSelection++;
      }
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
    case STATE_CHAT_RESPONSE:
      scrollOffset += 10;
      break;
    case STATE_GAME_PONG:
       // Handled in handlePongInput
      break;
    case STATE_GAME_SIDE_SCROLLER:
       // Handled in handleSideScrollerInput
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
    case STATE_GAME_SPACE_INVADERS:
      // Handled in handleSpaceInvadersInput
      break;
    case STATE_GAME_SIDE_SCROLLER:
      // Handled in handleSideScrollerInput
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
    case STATE_GAME_SPACE_INVADERS:
      // Handled in handleSpaceInvadersInput
      break;
    case STATE_GAME_SIDE_SCROLLER:
       // Handled in handleSideScrollerInput
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
    case STATE_API_SELECT:
      handleAPISelectSelect();
      break;
    case STATE_KEYBOARD:
      handleKeyPress();
      break;
    case STATE_PASSWORD_INPUT:
      handlePasswordKeyPress();
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
    case STATE_GAME_SELECT:
      changeState(STATE_MAIN_MENU);
      break;

    // Other simple cases
    case STATE_VIDEO_PLAYER:
      changeState(STATE_MAIN_MENU);
      break;

    default:
      // Default back to main menu if we're lost
      changeState(STATE_MAIN_MENU);
      break;
  }
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

  String escapedInput = userInput;
  escapedInput.replace("\\", "\\\\");
  escapedInput.replace("\"", "\\\"");
  escapedInput.replace("\n", "\\n");

  String jsonPayload = "{\"contents\":[{\"parts\":[{\"text\":\"" + escapedInput + "\"}]}]}";

  int httpResponseCode = http.POST(jsonPayload);

  if (httpResponseCode == 200) {
    String response = http.getString();

    StaticJsonDocument<16384> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);

    if (!error && responseDoc.containsKey("candidates")) {
      JsonArray candidates = responseDoc["candidates"];
      if (candidates.size() > 0) {
        JsonObject content = candidates[0]["content"];
        JsonArray parts = content["parts"];
        if (parts.size() > 0) {
          aiResponse = parts[0]["text"].as<String>();
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
