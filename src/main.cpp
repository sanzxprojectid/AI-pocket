#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

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

// Battery monitoring
float batteryVoltage = 5.0;
int batteryPercent = 100;

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
  int playerX;
  int playerY;
  int playerWidth;
  int playerHeight;
  int lives;
  int score;
  int level;
  bool gameOver;
  int weaponType; // 0=single, 1=double, 2=triple
  int shieldTime;
  
  struct Enemy {
    int x, y;
    int width, height;
    bool active;
    int type; // 0=basic, 1=fast, 2=tank
    int health;
  };
  Enemy enemies[MAX_ENEMIES];
  
  struct Bullet {
    int x, y;
    bool active;
  };
  Bullet bullets[MAX_BULLETS];
  Bullet enemyBullets[MAX_ENEMY_BULLETS];
  
  struct PowerUp {
    int x, y;
    int type; // 0=weapon, 1=shield, 2=life
    bool active;
  };
  PowerUp powerups[MAX_POWERUPS];
  
  int enemyDirection; // 1=right, -1=left
  unsigned long lastEnemyMove;
  unsigned long lastEnemyShoot;
  unsigned long lastSpawn;
  bool bossActive;
  int bossX, bossY, bossHealth;
};
SpaceInvaders invaders;

// Side Scroller Shooter Game State
#define MAX_OBSTACLES 8
#define MAX_SCROLLER_BULLETS 8
#define MAX_SCROLLER_ENEMIES 6

struct SideScroller {
  int playerX, playerY;
  int playerWidth, playerHeight;
  int lives;
  int score;
  int level;
  bool gameOver;
  int weaponLevel; // 1-5
  int specialCharge; // 0-100
  bool shieldActive;
  
  struct Obstacle {
    int x, y, width, height;
    bool active;
    int scrollSpeed;
  };
  Obstacle obstacles[MAX_OBSTACLES];
  
  struct ScrollerBullet {
    int x, y;
    int dirX, dirY; // -1, 0, or 1
    bool active;
    int damage;
  };
  ScrollerBullet bullets[MAX_SCROLLER_BULLETS];
  
  struct ScrollerEnemy {
    int x, y;
    int width, height;
    bool active;
    int health;
    int type; // 0=basic, 1=shooter, 2=kamikaze
    int dirY;
  };
  ScrollerEnemy enemies[MAX_SCROLLER_ENEMIES];
  
  struct ScrollerEnemyBullet {
    int x, y;
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
  int ballX, ballY;
  int ballDirX, ballDirY;
  int ballSpeed;
  int paddle1Y, paddle2Y;
  int paddleWidth, paddleHeight;
  int score1, score2;
  bool gameOver;
  bool aiMode; // true = vs AI, false = 2 player
  int difficulty; // 1-3
};
Pong pong;

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
  STATE_GAME_SELECT
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
const unsigned long debounceDelay = 150;

unsigned long lastGameUpdate = 0;
const int gameUpdateInterval = 16; // ~60 FPS

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

// Forward declarations
void showMainMenu();
void showWiFiMenu();
void showAPISelect();
void showGameSelect();
void showLoadingAnimation();
void showProgressBar(String title, int percent);
void displayWiFiNetworks();
void drawKeyboard();
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
void refreshCurrentScreen();
void drawBatteryIndicator();
void drawWiFiSignalBars();
void drawIcon(int x, int y, const unsigned char* icon);
void sendToGemini();
const char* getCurrentKey();
void toggleKeyboardMode();

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
  setCpuFrequencyMhz(240); // Set CPU to 240MHz for high performance
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== ESP32-S3 Gaming Edition v1.0 ===");
  
  preferences.begin("wifi-creds", false);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(800000); // Set I2C to 800kHz for smooth display
  
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

void loop() {
  unsigned long currentMillis = millis();
  
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

  // Main Menu Animation
  if (currentState == STATE_MAIN_MENU) {
    if (abs(menuScrollY - menuTargetScrollY) > 0.1) {
      menuScrollY += (menuTargetScrollY - menuScrollY) * 0.3;
      refreshCurrentScreen();
    } else if (menuScrollY != menuTargetScrollY) {
      menuScrollY = menuTargetScrollY;
      refreshCurrentScreen();
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
    
    // Touch buttons - Only active in games
    if (currentState == STATE_GAME_SPACE_INVADERS ||
        currentState == STATE_GAME_SIDE_SCROLLER ||
        currentState == STATE_GAME_PONG) {

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
  
  // Decrease shield
  if (invaders.shieldTime > 0) invaders.shieldTime--;
  
  // Move enemies
  if (now - invaders.lastEnemyMove > 800) {
    bool hitEdge = false;
    
    for (int i = 0; i < MAX_ENEMIES; i++) {
      if (invaders.enemies[i].active) {
        invaders.enemies[i].x += invaders.enemyDirection * 5;
        
        if (invaders.enemies[i].x <= 0 || invaders.enemies[i].x >= SCREEN_WIDTH - 8) {
          hitEdge = true;
        }
      }
    }
    
    if (hitEdge) {
      invaders.enemyDirection *= -1;
      for (int i = 0; i < MAX_ENEMIES; i++) {
        if (invaders.enemies[i].active) {
          invaders.enemies[i].y += 4;
          
          // Check if enemy reached player
          if (invaders.enemies[i].y >= invaders.playerY) {
            invaders.lives = 0;
          }
        }
      }
    }
    
    invaders.lastEnemyMove = now;
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
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (invaders.bullets[i].active) {
      invaders.bullets[i].y -= 4;
      if (invaders.bullets[i].y < 0) {
        invaders.bullets[i].active = false;
      }
    }
  }
  
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (invaders.enemyBullets[i].active) {
      invaders.enemyBullets[i].y += 3;
      if (invaders.enemyBullets[i].y > SCREEN_HEIGHT) {
        invaders.enemyBullets[i].active = false;
      }
    }
  }
  
  // Move powerups
  for (int i = 0; i < MAX_POWERUPS; i++) {
    if (invaders.powerups[i].active) {
      invaders.powerups[i].y += 2;
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
          ledError();
          
          if (invaders.lives <= 0) {
            invaders.gameOver = true;
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
        ledSuccess();
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
  drawBatteryIndicator();
  
  // Draw HUD
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
  
  // Draw player
  if (invaders.shieldTime > 0 && (millis() / 100) % 2 == 0) {
    display.drawCircle(invaders.playerX + 4, invaders.playerY + 3, 8, SSD1306_WHITE);
  }
  display.fillRect(invaders.playerX, invaders.playerY, invaders.playerWidth, invaders.playerHeight, SSD1306_WHITE);
  display.drawPixel(invaders.playerX + 4, invaders.playerY - 1, SSD1306_WHITE);
  
  // Draw enemies
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (invaders.enemies[i].active) {
      // Different shapes for different types
      switch(invaders.enemies[i].type) {
        case 0: // Basic
          display.fillRect(invaders.enemies[i].x, invaders.enemies[i].y, 8, 6, SSD1306_WHITE);
          break;
        case 1: // Fast
          display.drawTriangle(
            invaders.enemies[i].x + 4, invaders.enemies[i].y,
            invaders.enemies[i].x, invaders.enemies[i].y + 6,
            invaders.enemies[i].x + 8, invaders.enemies[i].y + 6,
            SSD1306_WHITE
          );
          break;
        case 2: // Tank
          display.fillRect(invaders.enemies[i].x, invaders.enemies[i].y, 8, 8, SSD1306_WHITE);
          display.drawRect(invaders.enemies[i].x + 1, invaders.enemies[i].y + 1, 6, 6, SSD1306_BLACK);
          break;
      }
    }
  }
  
  // Draw bullets
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (invaders.bullets[i].active) {
      display.drawLine(invaders.bullets[i].x, invaders.bullets[i].y,
                      invaders.bullets[i].x, invaders.bullets[i].y + 3, SSD1306_WHITE);
    }
  }
  
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (invaders.enemyBullets[i].active) {
      display.drawLine(invaders.enemyBullets[i].x, invaders.enemyBullets[i].y,
                      invaders.enemyBullets[i].x, invaders.enemyBullets[i].y - 3, SSD1306_WHITE);
    }
  }
  
  // Draw powerups
  for (int i = 0; i < MAX_POWERUPS; i++) {
    if (invaders.powerups[i].active) {
      switch(invaders.powerups[i].type) {
        case 0: // Weapon
          display.drawCircle(invaders.powerups[i].x, invaders.powerups[i].y, 3, SSD1306_WHITE);
          display.drawPixel(invaders.powerups[i].x, invaders.powerups[i].y, SSD1306_WHITE);
          break;
        case 1: // Shield
          display.drawCircle(invaders.powerups[i].x, invaders.powerups[i].y, 3, SSD1306_WHITE);
          break;
        case 2: // Life
          display.fillRect(invaders.powerups[i].x - 2, invaders.powerups[i].y - 2, 4, 4, SSD1306_WHITE);
          break;
      }
    }
  }
  
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
  // Handled in main loop via button handlers
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
  
  scroller.scrollOffset += 2;
  if (scroller.scrollOffset > SCREEN_WIDTH) scroller.scrollOffset = 0;
  
  // Spawn obstacles
  if (now - scroller.lastObstacleSpawn > 2000) {
    for (int i = 0; i < MAX_OBSTACLES; i++) {
      if (!scroller.obstacles[i].active) {
        scroller.obstacles[i].x = SCREEN_WIDTH;
        scroller.obstacles[i].y = random(15, SCREEN_HEIGHT - 20);
        scroller.obstacles[i].width = 8;
        scroller.obstacles[i].height = 12;
        scroller.obstacles[i].scrollSpeed = 2;
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
      scroller.obstacles[i].x -= scroller.obstacles[i].scrollSpeed;
      if (scroller.obstacles[i].x < -scroller.obstacles[i].width) {
        scroller.obstacles[i].active = false;
      }
    }
  }
  
  // Move and update enemies
  for (int i = 0; i < MAX_SCROLLER_ENEMIES; i++) {
    if (scroller.enemies[i].active) {
      scroller.enemies[i].x -= 2;
      scroller.enemies[i].y += scroller.enemies[i].dirY;
      
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
  for (int i = 0; i < MAX_SCROLLER_BULLETS; i++) {
    if (scroller.bullets[i].active) {
      scroller.bullets[i].x += scroller.bullets[i].dirX * 4;
      scroller.bullets[i].y += scroller.bullets[i].dirY * 4;
      
      if (scroller.bullets[i].x < 0 || scroller.bullets[i].x > SCREEN_WIDTH ||
          scroller.bullets[i].y < 12 || scroller.bullets[i].y > SCREEN_HEIGHT) {
        scroller.bullets[i].active = false;
      }
    }
  }
  
  for (int i = 0; i < MAX_OBSTACLES; i++) {
    if (scroller.enemyBullets[i].active) {
      scroller.enemyBullets[i].x -= 3;
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
          ledError();
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
          ledError();
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
          ledError();
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
  
  // Draw scrolling background
  for (int i = 0; i < SCREEN_WIDTH; i += 16) {
    int x = (i + scroller.scrollOffset) % SCREEN_WIDTH;
    display.drawPixel(x, 12 + random(0, 3), SSD1306_WHITE);
    display.drawPixel(x, SCREEN_HEIGHT - 2 - random(0, 3), SSD1306_WHITE);
  }
  
  // Draw player
  if (scroller.shieldActive && (millis() / 100) % 2 == 0) {
    display.drawCircle(scroller.playerX, scroller.playerY, 7, SSD1306_WHITE);
  }
  
  // Player ship design
  display.fillTriangle(
    scroller.playerX + 4, scroller.playerY,
    scroller.playerX - 4, scroller.playerY - 3,
    scroller.playerX - 4, scroller.playerY + 3,
    SSD1306_WHITE
  );
  display.drawLine(scroller.playerX - 4, scroller.playerY - 2,
                  scroller.playerX - 6, scroller.playerY - 4, SSD1306_WHITE);
  display.drawLine(scroller.playerX - 4, scroller.playerY + 2,
                  scroller.playerX - 6, scroller.playerY + 4, SSD1306_WHITE);
  
  // Draw obstacles
  for (int i = 0; i < MAX_OBSTACLES; i++) {
    if (scroller.obstacles[i].active) {
      display.fillRect(scroller.obstacles[i].x, scroller.obstacles[i].y,
                      scroller.obstacles[i].width, scroller.obstacles[i].height, SSD1306_WHITE);
      display.drawRect(scroller.obstacles[i].x + 1, scroller.obstacles[i].y + 1,
                      scroller.obstacles[i].width - 2, scroller.obstacles[i].height - 2, SSD1306_BLACK);
    }
  }
  
  // Draw enemies
  for (int i = 0; i < MAX_SCROLLER_ENEMIES; i++) {
    if (scroller.enemies[i].active) {
      switch(scroller.enemies[i].type) {
        case 0: // Basic
          display.fillCircle(scroller.enemies[i].x, scroller.enemies[i].y, 4, SSD1306_WHITE);
          break;
        case 1: // Shooter
          display.fillRect(scroller.enemies[i].x - 4, scroller.enemies[i].y - 4, 8, 8, SSD1306_WHITE);
          display.drawLine(scroller.enemies[i].x - 2, scroller.enemies[i].y,
                          scroller.enemies[i].x + 2, scroller.enemies[i].y, SSD1306_BLACK);
          break;
        case 2: // Kamikaze
          display.drawTriangle(
            scroller.enemies[i].x - 6, scroller.enemies[i].y,
            scroller.enemies[i].x + 2, scroller.enemies[i].y - 4,
            scroller.enemies[i].x + 2, scroller.enemies[i].y + 4,
            SSD1306_WHITE
          );
          break;
      }
    }
  }
  
  // Draw bullets
  for (int i = 0; i < MAX_SCROLLER_BULLETS; i++) {
    if (scroller.bullets[i].active) {
      display.fillCircle(scroller.bullets[i].x, scroller.bullets[i].y, 2, SSD1306_WHITE);
    }
  }
  
  for (int i = 0; i < MAX_OBSTACLES; i++) {
    if (scroller.enemyBullets[i].active) {
      display.fillRect(scroller.enemyBullets[i].x - 1, scroller.enemyBullets[i].y - 1, 2, 2, SSD1306_WHITE);
    }
  }
  
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
  // Handled in main loop
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
  
  // Move ball
  pong.ballX += pong.ballDirX * pong.ballSpeed;
  pong.ballY += pong.ballDirY * pong.ballSpeed;
  
  // Ball collision with top/bottom
  if (pong.ballY <= 12 || pong.ballY >= SCREEN_HEIGHT - 2) {
    pong.ballDirY *= -1;
  }
  
  // Ball collision with paddles
  // Left paddle
  if (pong.ballX <= 6 && pong.ballX >= 2) {
    if (pong.ballY >= pong.paddle1Y && pong.ballY <= pong.paddle1Y + pong.paddleHeight) {
      pong.ballDirX = 1;
      // Add spin based on where it hit the paddle
      int hitPos = pong.ballY - (pong.paddle1Y + pong.paddleHeight / 2);
      if (hitPos > 5) pong.ballDirY = 1;
      else if (hitPos < -5) pong.ballDirY = -1;
      ledQuickFlash();
    }
  }
  
  // Right paddle
  if (pong.ballX >= SCREEN_WIDTH - 6 && pong.ballX <= SCREEN_WIDTH - 2) {
    if (pong.ballY >= pong.paddle2Y && pong.ballY <= pong.paddle2Y + pong.paddleHeight) {
      pong.ballDirX = -1;
      int hitPos = pong.ballY - (pong.paddle2Y + pong.paddleHeight / 2);
      if (hitPos > 5) pong.ballDirY = 1;
      else if (hitPos < -5) pong.ballDirY = -1;
      ledQuickFlash();
    }
  }
  
  // Scoring
  if (pong.ballX < 0) {
    pong.score2++;
    pong.ballX = SCREEN_WIDTH / 2;
    pong.ballY = SCREEN_HEIGHT / 2;
    pong.ballDirX = 1;
    delay(500);
    
    if (pong.score2 >= 10) {
      pong.gameOver = true;
    }
  }
  
  if (pong.ballX > SCREEN_WIDTH) {
    pong.score1++;
    pong.ballX = SCREEN_WIDTH / 2;
    pong.ballY = SCREEN_HEIGHT / 2;
    pong.ballDirX = -1;
    delay(500);
    
    if (pong.score1 >= 10) {
      pong.gameOver = true;
    }
  }
  
  // AI for right paddle
  if (pong.aiMode) {
    int targetY = pong.ballY - pong.paddleHeight / 2;
    int diff = targetY - pong.paddle2Y;
    
    // AI difficulty
    int aiSpeed = pong.difficulty;
    if (abs(diff) > aiSpeed) {
      if (diff > 0) pong.paddle2Y += aiSpeed;
      else pong.paddle2Y -= aiSpeed;
    }
  }
  
  // Clamp paddles
  pong.paddle1Y = constrain(pong.paddle1Y, 12, SCREEN_HEIGHT - pong.paddleHeight);
  pong.paddle2Y = constrain(pong.paddle2Y, 12, SCREEN_HEIGHT - pong.paddleHeight);
}

void drawPong() {
  display.clearDisplay();
  drawBatteryIndicator();
  
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
  
  // Draw paddles
  display.fillRect(2, pong.paddle1Y, pong.paddleWidth, pong.paddleHeight, SSD1306_WHITE);
  display.fillRect(SCREEN_WIDTH - 6, pong.paddle2Y, pong.paddleWidth, pong.paddleHeight, SSD1306_WHITE);
  
  // Draw ball
  display.fillCircle(pong.ballX, pong.ballY, 2, SSD1306_WHITE);
  
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
  // Handled in main loop
}

// ========== GAME SELECT ==========

void showGameSelect() {
  display.clearDisplay();
  drawBatteryIndicator();
  
  display.setTextSize(1);
  display.setCursor(25, 2);
  display.print("SELECT GAME");
  
  drawIcon(10, 2, ICON_GAME);
  
  display.drawLine(0, 12, SCREEN_WIDTH, 12, SSD1306_WHITE);
  
  const char* games[] = {
    "Space Invaders",
    "Side Scroller",
    "Pong",
    "Back"
  };
  
  for (int i = 0; i < 4; i++) {
    display.setCursor(10, 18 + i * 11);
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
      currentState = STATE_GAME_SPACE_INVADERS;
      break;
    case 1:
      initSideScroller();
      currentState = STATE_GAME_SIDE_SCROLLER;
      break;
    case 2:
      initPong();
      currentState = STATE_GAME_PONG;
      break;
    case 3:
      currentState = STATE_MAIN_MENU;
      menuSelection = mainMenuSelection;
      menuTargetScrollY = mainMenuSelection * 22;
      showMainMenu();
      break;
  }
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
    {"Games", ICON_GAME}
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
    case 2: // Games
      menuSelection = 0;
      previousState = currentState;
      currentState = STATE_GAME_SELECT;
      showGameSelect();
      break;
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

void showLoadingAnimation() {
  display.clearDisplay();
  drawBatteryIndicator();

  display.setCursor(35, 25);
  display.print("Loading...");

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
  drawKeyboard();
}

void drawKeyboard() {
  display.clearDisplay();
  drawBatteryIndicator();

  display.drawRect(2, 2, SCREEN_WIDTH - 4, 14, SSD1306_WHITE);

  display.setCursor(5, 5);
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
    case STATE_GAME_SELECT:
      if (menuSelection > 0) {
        menuSelection--;
        showGameSelect();
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
    case STATE_CHAT_RESPONSE:
      if (scrollOffset > 0) {
        scrollOffset -= 10;
        displayResponse();
      }
      break;
    case STATE_GAME_PONG:
      if (pong.paddle1Y > 12) {
        pong.paddle1Y -= 3;
      }
      break;
    case STATE_GAME_SIDE_SCROLLER:
      if (scroller.playerY > 15) {
        scroller.playerY -= 3;
      }
      break;
  }
}

void handleDown() {
  switch(currentState) {
    case STATE_MAIN_MENU:
      if (menuSelection < 2) {
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
    case STATE_GAME_SELECT:
      if (menuSelection < 3) {
        menuSelection++;
        showGameSelect();
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
    case STATE_CHAT_RESPONSE:
      scrollOffset += 10;
      displayResponse();
      break;
    case STATE_GAME_PONG:
      if (pong.paddle1Y < SCREEN_HEIGHT - pong.paddleHeight) {
        pong.paddle1Y += 3;
      }
      break;
    case STATE_GAME_SIDE_SCROLLER:
      if (scroller.playerY < SCREEN_HEIGHT - 8) {
        scroller.playerY += 3;
      }
      break;
  }
}

void handleLeft() {
  switch(currentState) {
    case STATE_KEYBOARD:
    case STATE_PASSWORD_INPUT:
      if (cursorX > 0) {
        cursorX--;
        drawKeyboard();
      }
      break;
    case STATE_GAME_SPACE_INVADERS:
      if (invaders.playerX > 0) {
        invaders.playerX -= 4;
      }
      break;
    case STATE_GAME_SIDE_SCROLLER:
      if (scroller.playerX > 10) {
        scroller.playerX -= 3;
      }
      break;
  }
}

void handleRight() {
  switch(currentState) {
    case STATE_KEYBOARD:
    case STATE_PASSWORD_INPUT:
      if (cursorX < 9) {
        cursorX++;
        drawKeyboard();
      }
      break;
    case STATE_GAME_SPACE_INVADERS:
      if (invaders.playerX < SCREEN_WIDTH - invaders.playerWidth) {
        invaders.playerX += 4;
      }
      break;
    case STATE_GAME_SIDE_SCROLLER:
      if (scroller.playerX < SCREEN_WIDTH - 15) {
        scroller.playerX += 3;
      }
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
    case STATE_WIFI_MENU:
    case STATE_GAME_SELECT:
      currentState = STATE_MAIN_MENU;
      menuSelection = mainMenuSelection;
      menuTargetScrollY = mainMenuSelection * 22;
      showMainMenu();
      break;
    case STATE_WIFI_SCAN:
      currentState = STATE_WIFI_MENU;
      showWiFiMenu();
      break;
    case STATE_KEYBOARD:
      currentState = STATE_MAIN_MENU;
      showMainMenu();
      break;
    case STATE_PASSWORD_INPUT:
      currentState = STATE_WIFI_SCAN;
      displayWiFiNetworks();
      break;
    case STATE_CHAT_RESPONSE:
      currentState = STATE_KEYBOARD;
      drawKeyboard();
      break;
    case STATE_API_SELECT:
      currentState = STATE_MAIN_MENU;
      showMainMenu();
      break;
    case STATE_GAME_SPACE_INVADERS:
    case STATE_GAME_SIDE_SCROLLER:
    case STATE_GAME_PONG:
      currentState = STATE_GAME_SELECT;
      menuSelection = 0;
      showGameSelect();
      break;
  }
}

void refreshCurrentScreen() {
  switch(currentState) {
    case STATE_MAIN_MENU: showMainMenu(); break;
    case STATE_WIFI_MENU: showWiFiMenu(); break;
    case STATE_WIFI_SCAN: displayWiFiNetworks(); break;
    case STATE_API_SELECT: showAPISelect(); break;
    case STATE_GAME_SELECT: showGameSelect(); break;
    case STATE_LOADING: showLoadingAnimation(); break;
    case STATE_KEYBOARD: drawKeyboard(); break;
    case STATE_PASSWORD_INPUT: drawKeyboard(); break;
    case STATE_CHAT_RESPONSE: displayResponse(); break;
    default: showMainMenu(); break;
  }
}

void displayResponse() {
  display.clearDisplay();
  drawBatteryIndicator();

  display.setCursor(0, 0);
  display.setTextSize(1);

  int y = 12 - scrollOffset;

  display.setCursor(0, y);
  display.print(aiResponse);

  if (aiResponse.length() > 100) {
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
