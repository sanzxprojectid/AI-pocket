#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
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
#include <ArduinoOTA.h>
#include "secrets.h"

// Hardware Definitions
#define NEOPIXEL_PIN 48
#define NEOPIXEL_COUNT 1
#define BATTERY_PIN 4
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 13
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_BLK 14

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 170

// Button Definitions
#define BTN_UP 5
#define BTN_DOWN 6
#define BTN_LEFT 7
#define BTN_RIGHT 15
#define BTN_SELECT 16
#define BTN_BACK 17
#define TOUCH_LEFT 1
#define TOUCH_RIGHT 2

// Color Map
#define SSD1306_WHITE ST77XX_WHITE
#define SSD1306_BLACK ST77XX_BLACK
#define COLOR_GREEN   ST77XX_GREEN
#define COLOR_RED     ST77XX_RED
#define COLOR_CYAN    ST77XX_CYAN
#define COLOR_BLUE    ST77XX_BLUE

// Objects
Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(SCREEN_WIDTH, SCREEN_HEIGHT);
Preferences preferences;
WebServer server(80);

// App State
enum AppState {
  STATE_WIFI_MENU, STATE_WIFI_SCAN, STATE_PASSWORD_INPUT, STATE_KEYBOARD, STATE_CHAT_RESPONSE, STATE_MAIN_MENU,
  STATE_API_SELECT, STATE_LOADING, STATE_GAME_SPACE_INVADERS, STATE_GAME_SIDE_SCROLLER, STATE_GAME_PONG,
  STATE_GAME_RACING, STATE_RACING_MODE_SELECT, STATE_GAME_SELECT, STATE_SYSTEM_MENU, STATE_SYSTEM_PERF,
  STATE_SYSTEM_NET, STATE_SYSTEM_DEVICE, STATE_SYSTEM_BENCHMARK, STATE_SYSTEM_POWER, STATE_SYSTEM_SUB_STATUS,
  STATE_SYSTEM_SUB_SETTINGS, STATE_SYSTEM_SUB_TOOLS, STATE_TOOL_SPAMMER, STATE_TOOL_DETECTOR, STATE_DEAUTH_SELECT,
  STATE_TOOL_DEAUTH, STATE_TOOL_PROBE_SNIFFER, STATE_TOOL_BLE_MENU, STATE_TOOL_BLE_RUN, STATE_TOOL_COURIER,
  STATE_PIN_LOCK, STATE_CHANGE_PIN, STATE_SCREEN_SAVER, STATE_VIDEO_PLAYER
};

AppState currentState = STATE_MAIN_MENU;
AppState previousState = STATE_MAIN_MENU;

// Globals
int screenBrightness = 255;
int cursorX = 0, cursorY = 0;
String userInput = "";
String passwordInput = "";
String selectedSSID = "";
String aiResponse = "";
int scrollOffset = 0;
int menuSelection = 0;
unsigned long lastDebounce = 0;
unsigned long lastInputTime = 0;
const unsigned long SCREEN_SAVER_TIMEOUT = 120000;
bool pinLockEnabled = false;
String pinCode = "1234";
String inputPin = "";
AppState stateBeforeScreenSaver = STATE_MAIN_MENU;
AppState stateAfterUnlock = STATE_MAIN_MENU;
int screensaverMode = 0;
int selectedAPIKey = 1;
int highScoreInvaders = 0, highScoreScroller = 0, highScoreRacing = 0;
unsigned long perfFrameCount = 0, perfLoopCount = 0, perfLastTime = 0;
int perfFPS = 0, perfLPS = 0;
bool showFPS = false;
int systemMenuSelection = 0;
float systemMenuScrollY = 0;
int currentCpuFreq = 240;
int currentI2C = 1000000;
bool benchmarkDone = false;
int cachedRSSI = 0;
String cachedTimeStr = "";
unsigned long lastStatusBarUpdate = 0;
float menuScrollY = 0, menuTargetScrollY = 0;
int mainMenuSelection = 0;
unsigned long lastUiUpdate = 0;
unsigned long lastFrameMillis = 0;
unsigned long lastPhysicsUpdate = 0;
float deltaTime = 0.0;
uint32_t neoPixelColor = 0;
unsigned long neoPixelEffectEnd = 0;

// Consts
const char* geminiEndpoint = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash-lite:generateContent";
const char* keyboardLower[3][10] = {{"q","w","e","r","t","y","u","i","o","p"},{"a","s","d","f","g","h","j","k","l","<"},{"#","z","x","c","v","b","n","m"," ","OK"}};
const char* keyboardUpper[3][10] = {{"Q","W","E","R","T","Y","U","I","O","P"},{"A","S","D","F","G","H","J","K","L","<"},{"#","Z","X","C","V","B","N","M",".","OK"}};
const char* keyboardNumbers[3][10] = {{"1","2","3","4","5","6","7","8","9","0"},{"!","@","#","$","%","^","&","*","(",")"},{"#","-","_","=","+","[","]","?",".","OK"}};
const char* keyboardPin[4][3] = {{"1","2","3"},{"4","5","6"},{"7","8","9"},{"<","0","OK"}};
const unsigned char ICON_WIFI[] PROGMEM = { 0x00, 0x3C, 0x42, 0x99, 0x24, 0x00, 0x18, 0x00 };
const unsigned char ICON_CHAT[] PROGMEM = { 0x7E, 0xFF, 0xC3, 0xC3, 0xC3, 0xFF, 0x18, 0x00 };
const unsigned char ICON_GAME[] PROGMEM = { 0x3C, 0x42, 0x99, 0xA5, 0xA5, 0x99, 0x42, 0x3C };
const unsigned char ICON_VIDEO[] PROGMEM = { 0x7E, 0x81, 0x81, 0xBD, 0xBD, 0x81, 0x81, 0x7E };
const unsigned char ICON_SYSTEM[] PROGMEM = { 0x3C, 0x7E, 0xDB, 0xFF, 0xC3, 0xFF, 0x7E, 0x3C };
const unsigned char ICON_SYS_STATUS[] PROGMEM = { 0x18, 0x3C, 0x7E, 0x18, 0x18, 0x7E, 0x3C, 0x18 };
const unsigned char ICON_SYS_SETTINGS[] PROGMEM = { 0x3C, 0x42, 0x99, 0xBD, 0xBD, 0x99, 0x42, 0x3C };
const unsigned char ICON_SYS_TOOLS[] PROGMEM = { 0x18, 0x3C, 0x7E, 0xFF, 0x5A, 0x24, 0x18, 0x00 };
const unsigned char ICON_BLE[] PROGMEM = { 0x18, 0x5A, 0xDB, 0x5A, 0x18, 0x18, 0x18, 0x18 };
const unsigned char ICON_TRUCK[] PROGMEM = { 0x00, 0x18, 0x7E, 0x7E, 0x7E, 0x24, 0x00, 0x00 };

enum KeyboardMode { MODE_LOWER, MODE_UPPER, MODE_NUMBERS };
KeyboardMode currentKeyboardMode = MODE_LOWER;
enum KeyboardContext { CONTEXT_CHAT, CONTEXT_WIFI_PASSWORD, CONTEXT_BLE_NAME };
KeyboardContext keyboardContext = CONTEXT_CHAT;

enum TransitionState { TRANSITION_NONE, TRANSITION_OUT, TRANSITION_IN };
TransitionState transitionState = TRANSITION_NONE;
AppState transitionTargetState;
float transitionProgress = 0.0;
const float transitionSpeed = 3.5f;

// Forward Declarations
void changeState(AppState newState);
void showStatus(String message, int delayMs);
void ledQuickFlash();
void drawStatusBar();
void drawGenericListMenu(int x_offset, const char* title, const unsigned char* icon, const char** items, int itemCount, int selection, float* scrollY);
const char* getCurrentKey();
void toggleKeyboardMode();
float getBatteryVoltage();
void runRecoveryMode();

// Preference Helpers
void savePreferenceString(const char* key, String value) { preferences.begin("app-config", false); preferences.putString(key, value); preferences.end(); }
String loadPreferenceString(const char* key, String defaultValue) { preferences.begin("app-config", true); String value = preferences.getString(key, defaultValue); preferences.end(); return value; }
void savePreferenceInt(const char* key, int value) { preferences.begin("app-config", false); preferences.putInt(key, value); preferences.end(); }
int loadPreferenceInt(const char* key, int defaultValue) { preferences.begin("app-config", true); int value = preferences.getInt(key, defaultValue); preferences.end(); return value; }
void savePreferenceBool(const char* key, bool value) { preferences.begin("app-config", false); preferences.putBool(key, value); preferences.end(); }
bool loadPreferenceBool(const char* key, bool defaultValue) { preferences.begin("app-config", true); bool value = preferences.getBool(key, defaultValue); preferences.end(); return value; }

// --- WiFi & Tools ---
struct WiFiNetwork { String ssid; int rssi; bool encrypted; };
WiFiNetwork networks[20];
int selectedNetwork = 0; int wifiPage = 0; const int wifiPerPage = 8;

void scanWiFiNetworks() {
  WiFi.scanNetworks();
  networkCount = WiFi.scanComplete();
  changeState(STATE_WIFI_SCAN);
}

// --- Games Logic ---
#define MAX_ENEMIES 15
#define MAX_BULLETS 5
#define MAX_SCROLLER_BULLETS 8
#define MAX_SCROLLER_ENEMIES 6

// Space Invaders
struct SpaceInvaders {
  float playerX, playerY;
  int lives, score, level;
  bool gameOver;
  struct Enemy { float x,y; bool active; int type; } enemies[MAX_ENEMIES];
  struct Bullet { float x,y; bool active; } bullets[MAX_BULLETS];
  int enemyDirection;
};
SpaceInvaders invaders;

void initSpaceInvaders() {
  invaders.lives=3; invaders.score=0; invaders.level=1; invaders.gameOver=false;
  invaders.playerX=SCREEN_WIDTH/2; invaders.playerY=SCREEN_HEIGHT-20;
  invaders.enemyDirection=1;
  for(int i=0; i<MAX_ENEMIES; i++) invaders.enemies[i].active=false;
  for(int i=0; i<MAX_BULLETS; i++) invaders.bullets[i].active=false;
  for(int i=0; i<5; i++) {
    invaders.enemies[i].active=true;
    invaders.enemies[i].x=20+i*40;
    invaders.enemies[i].y=30;
    invaders.enemies[i].type=random(0,3);
  }
}

void updateSpaceInvaders() {
  if (invaders.gameOver) return;
  bool hitEdge = false;
  float speed = 20.0f + (invaders.level * 5.0f);

  // Enemy Movement
  for (int i=0; i<MAX_ENEMIES; i++) {
    if (invaders.enemies[i].active) {
      invaders.enemies[i].x += invaders.enemyDirection * speed * deltaTime;
      if ((invaders.enemyDirection > 0 && invaders.enemies[i].x >= SCREEN_WIDTH - 20) ||
          (invaders.enemyDirection < 0 && invaders.enemies[i].x <= 0)) {
        hitEdge = true;
      }
    }
  }
  if (hitEdge) {
    invaders.enemyDirection *= -1;
    for (int i=0; i<MAX_ENEMIES; i++) {
      if (invaders.enemies[i].active) {
        invaders.enemies[i].y += 10;
        if (invaders.enemies[i].y >= invaders.playerY) invaders.gameOver=true;
      }
    }
  }

  // Bullet Movement
  for (int i=0; i<MAX_BULLETS; i++) {
    if (invaders.bullets[i].active) {
      invaders.bullets[i].y -= 150.0f * deltaTime;
      if (invaders.bullets[i].y < 0) invaders.bullets[i].active = false;

      // Collision
      for (int j=0; j<MAX_ENEMIES; j++) {
        if (invaders.enemies[j].active) {
          if (abs(invaders.bullets[i].x - invaders.enemies[j].x) < 15 && abs(invaders.bullets[i].y - invaders.enemies[j].y) < 10) {
            invaders.enemies[j].active = false;
            invaders.bullets[i].active = false;
            invaders.score += 10;
            break;
          }
        }
      }
    }
  }
}

void drawSpaceInvaders() {
  canvas.fillScreen(SSD1306_BLACK);
  canvas.fillRect(invaders.playerX, invaders.playerY, 20, 10, SSD1306_WHITE);
  canvas.fillTriangle(invaders.playerX+10, invaders.playerY-5, invaders.playerX, invaders.playerY, invaders.playerX+20, invaders.playerY, SSD1306_WHITE);

  for (int i=0; i<MAX_ENEMIES; i++) {
    if (invaders.enemies[i].active) {
      uint16_t c = (invaders.enemies[i].type==0)?COLOR_GREEN:(invaders.enemies[i].type==1?COLOR_CYAN:COLOR_RED);
      canvas.fillRect(invaders.enemies[i].x, invaders.enemies[i].y, 16, 12, c);
    }
  }

  for (int i=0; i<MAX_BULLETS; i++) {
    if (invaders.bullets[i].active) canvas.fillRect(invaders.bullets[i].x, invaders.bullets[i].y, 2, 6, SSD1306_WHITE);
  }

  canvas.setCursor(0,0); canvas.print("Score: "); canvas.print(invaders.score);
  if (invaders.gameOver) {
    canvas.setCursor(100, 80); canvas.setTextSize(2); canvas.print("GAME OVER");
  }
}

void handleSpaceInvadersInput() {
  if (digitalRead(BTN_LEFT)==LOW) invaders.playerX -= 150.0f * deltaTime;
  if (digitalRead(BTN_RIGHT)==LOW) invaders.playerX += 150.0f * deltaTime;
  if (invaders.playerX < 0) invaders.playerX = 0;
  if (invaders.playerX > SCREEN_WIDTH-20) invaders.playerX = SCREEN_WIDTH-20;
}

// Side Scroller
struct SideScroller {
  float playerX, playerY;
  int lives, score;
  bool gameOver;
  struct Bullet { float x,y; bool active; } bullets[MAX_SCROLLER_BULLETS];
  struct Enemy { float x,y; bool active; } enemies[MAX_SCROLLER_ENEMIES];
};
SideScroller scroller;

void initSideScroller() {
  scroller.lives=3; scroller.score=0; scroller.gameOver=false;
  scroller.playerX=20; scroller.playerY=85;
  for(int i=0;i<MAX_SCROLLER_ENEMIES;i++) { scroller.enemies[i].active=true; scroller.enemies[i].x=SCREEN_WIDTH+random(0,200); scroller.enemies[i].y=random(20,150); }
  for(int i=0;i<MAX_SCROLLER_BULLETS;i++) scroller.bullets[i].active=false;
}

void updateSideScroller() {
  if(scroller.gameOver) return;
  // Enemies
  for(int i=0;i<MAX_SCROLLER_ENEMIES;i++) {
    if(scroller.enemies[i].active) {
      scroller.enemies[i].x -= 60.0f * deltaTime;
      if(scroller.enemies[i].x < 0) {
        scroller.enemies[i].x = SCREEN_WIDTH + random(0,100);
        scroller.enemies[i].y = random(20,150);
      }
      if(abs(scroller.playerX-scroller.enemies[i].x)<15 && abs(scroller.playerY-scroller.enemies[i].y)<15) {
        scroller.lives--;
        scroller.enemies[i].x = SCREEN_WIDTH + random(50,150);
        if(scroller.lives<=0) scroller.gameOver=true;
      }
    }
  }
  // Bullets
  for(int i=0;i<MAX_SCROLLER_BULLETS;i++) {
    if(scroller.bullets[i].active) {
      scroller.bullets[i].x += 200.0f * deltaTime;
      if(scroller.bullets[i].x > SCREEN_WIDTH) scroller.bullets[i].active=false;
      for(int j=0;j<MAX_SCROLLER_ENEMIES;j++) {
        if(scroller.enemies[j].active && abs(scroller.bullets[i].x-scroller.enemies[j].x)<15 && abs(scroller.bullets[i].y-scroller.enemies[j].y)<15) {
          scroller.enemies[j].x = SCREEN_WIDTH + random(50,200);
          scroller.bullets[i].active=false;
          scroller.score+=10;
        }
      }
    }
  }
}

void drawSideScroller() {
  canvas.fillScreen(SSD1306_BLACK);
  canvas.fillTriangle(scroller.playerX, scroller.playerY, scroller.playerX-10, scroller.playerY-5, scroller.playerX-10, scroller.playerY+5, COLOR_CYAN);
  for(int i=0;i<MAX_SCROLLER_ENEMIES;i++) {
    if(scroller.enemies[i].active) canvas.fillCircle(scroller.enemies[i].x, scroller.enemies[i].y, 6, COLOR_RED);
  }
  for(int i=0;i<MAX_SCROLLER_BULLETS;i++) {
    if(scroller.bullets[i].active) canvas.drawPixel(scroller.bullets[i].x, scroller.bullets[i].y, SSD1306_WHITE);
  }
  canvas.setCursor(0,0); canvas.print("L:"); canvas.print(scroller.lives); canvas.print(" S:"); canvas.print(scroller.score);
  if(scroller.gameOver) { canvas.setCursor(100,80); canvas.setTextSize(2); canvas.print("GAME OVER"); }
}

void handleSideScrollerInput() {
  if(digitalRead(BTN_UP)==LOW) scroller.playerY -= 100.0f * deltaTime;
  if(digitalRead(BTN_DOWN)==LOW) scroller.playerY += 100.0f * deltaTime;
  if(scroller.playerY<10) scroller.playerY=10;
  if(scroller.playerY>SCREEN_HEIGHT-10) scroller.playerY=SCREEN_HEIGHT-10;
}

// Racing Game
struct Racing {
  float carX, speed;
  int score, lives, mode;
  bool gameOver;
  struct Enemy { float z,x; bool active; } enemies[5];
};
Racing racing;

void initRacing(int m) {
  racing.mode=m; racing.carX=0; racing.speed=0; racing.lives=3; racing.score=0; racing.gameOver=false;
  for(int i=0;i<5;i++) { racing.enemies[i].active=false; }
}

void updateRacing() {
  if(racing.gameOver) return;
  racing.speed += (digitalRead(BTN_UP)==LOW) ? 50.0f*deltaTime : -20.0f*deltaTime;
  if(racing.speed<0) racing.speed=0;
  if(racing.speed>100) racing.speed=100;

  if(digitalRead(BTN_LEFT)==LOW) racing.carX -= 1.0f * deltaTime;
  if(digitalRead(BTN_RIGHT)==LOW) racing.carX += 1.0f * deltaTime;
  if(racing.carX < -1.0f) racing.carX = -1.0f;
  if(racing.carX > 1.0f) racing.carX = 1.0f;

  if(random(0,100)<2) {
    for(int i=0;i<5;i++) if(!racing.enemies[i].active) {
      racing.enemies[i].active=true; racing.enemies[i].z=100; racing.enemies[i].x=(random(0,2)==0?-0.5f:0.5f); break;
    }
  }
  for(int i=0;i<5;i++) {
    if(racing.enemies[i].active) {
      racing.enemies[i].z -= racing.speed * deltaTime * 5.0f;
      if(racing.enemies[i].z < 1) {
        racing.enemies[i].active=false;
        racing.score += 10;
      }
      if(racing.enemies[i].z < 5 && abs(racing.enemies[i].x - racing.carX) < 0.3f) {
        racing.enemies[i].active=false;
        racing.lives--;
        if(racing.lives<=0) racing.gameOver=true;
      }
    }
  }
}

void drawRacing() {
  canvas.fillScreen(SSD1306_BLACK);
  canvas.fillRect(0, 85, 320, 85, 0x2222); // Ground
  int cx = 160 + (racing.carX * 100);
  // Horizon lines
  for(int i=0;i<5;i++) {
    int y = 85 + (i*15 + (int)(millis()/10)%15);
    canvas.drawLine(0, y, 320, y, 0x4444);
  }
  // Enemies
  for(int i=0;i<5;i++) {
    if(racing.enemies[i].active) {
      int ez = (int)racing.enemies[i].z;
      int ex = 160 + (racing.enemies[i].x * 1500 / ez);
      int ey = 85 + (2000 / ez);
      int es = 200 / ez;
      canvas.fillRect(ex-es/2, ey-es, es, es, COLOR_RED);
    }
  }
  // Car
  canvas.fillRect(160-20, 140, 40, 20, COLOR_BLUE);

  canvas.setCursor(0,0); canvas.print("Spd: "); canvas.print((int)racing.speed);
  if(racing.gameOver) { canvas.setCursor(100,80); canvas.print("CRASHED"); }
}

void handleRacingInput() { /* handled in update */ }

// Pong
struct Pong { float bx, by, bdx, bdy; float p1y, p2y; int s1, s2; };
Pong pong;
void initPong() { pong.bx=160; pong.by=85; pong.bdx=2; pong.bdy=2; pong.p1y=70; pong.p2y=70; pong.s1=0; pong.s2=0; }
void updatePong() {
  pong.bx += pong.bdx; pong.by += pong.bdy;
  if(pong.by<0 || pong.by>170) pong.bdy *= -1;
  if(pong.bx<10 && abs(pong.p1y-pong.by)<20) pong.bdx *= -1;
  if(pong.bx>310 && abs(pong.p2y-pong.by)<20) pong.bdx *= -1;
  if(pong.bx<0) { pong.bx=160; pong.s2++; }
  if(pong.bx>320) { pong.bx=160; pong.s1++; }
}
void drawPong() {
  canvas.fillScreen(SSD1306_BLACK);
  canvas.fillCircle(pong.bx, pong.by, 4, SSD1306_WHITE);
  canvas.fillRect(5, pong.p1y-15, 5, 30, SSD1306_WHITE);
  canvas.fillRect(310, pong.p2y-15, 5, 30, SSD1306_WHITE);
  canvas.setCursor(140, 10); canvas.print(pong.s1); canvas.print("-"); canvas.print(pong.s2);
}
void handlePongInput() {
  if(digitalRead(BTN_UP)==LOW) pong.p1y-=4;
  if(digitalRead(BTN_DOWN)==LOW) pong.p1y+=4;
}

// Video Player Stub (Placeholder for frames)
int videoCurrentFrame = 0;
void drawVideoPlayer() {
  canvas.fillScreen(SSD1306_BLACK);
  canvas.setCursor(100, 80);
  canvas.print("NO VIDEO DATA");
}

// Logic Mapping
void initAllGames() {}

// Menu & System Logic
void handleUp() {
  switch(currentState) {
    case STATE_MAIN_MENU: if(menuSelection>0) menuSelection--; break;
    case STATE_WIFI_MENU: if(menuSelection>0) menuSelection--; break;
    case STATE_GAME_SELECT: if(menuSelection>0) menuSelection--; break;
    case STATE_SYSTEM_MENU: if(systemMenuSelection>0) systemMenuSelection--; break;
    case STATE_KEYBOARD: cursorY--; if(cursorY<0) cursorY=2; break;
    default: break;
  }
}
void handleDown() {
  switch(currentState) {
    case STATE_MAIN_MENU: if(menuSelection<5) menuSelection++; break;
    case STATE_WIFI_MENU: if(menuSelection<2) menuSelection++; break;
    case STATE_GAME_SELECT: if(menuSelection<4) menuSelection++; break;
    case STATE_SYSTEM_MENU: if(systemMenuSelection<3) systemMenuSelection++; break;
    case STATE_KEYBOARD: cursorY++; if(cursorY>2) cursorY=0; break;
    default: break;
  }
}
void handleLeft() { if(currentState==STATE_KEYBOARD) { cursorX--; if(cursorX<0) cursorX=9; } }
void handleRight() { if(currentState==STATE_KEYBOARD) { cursorX++; if(cursorX>9) cursorX=0; } }
void handleSelect() {
  switch(currentState) {
    case STATE_MAIN_MENU:
      if(menuSelection==0) changeState(STATE_API_SELECT);
      else if(menuSelection==1) changeState(STATE_WIFI_MENU);
      else if(menuSelection==2) changeState(STATE_GAME_SELECT);
      else if(menuSelection==3) changeState(STATE_VIDEO_PLAYER);
      else if(menuSelection==4) changeState(STATE_TOOL_COURIER);
      else if(menuSelection==5) changeState(STATE_SYSTEM_MENU);
      break;
    case STATE_WIFI_MENU:
      if(menuSelection==0) scanWiFiNetworks();
      else changeState(STATE_MAIN_MENU);
      break;
    case STATE_GAME_SELECT:
      if(menuSelection==0) { initSpaceInvaders(); changeState(STATE_GAME_SPACE_INVADERS); }
      else if(menuSelection==1) { initSideScroller(); changeState(STATE_GAME_SIDE_SCROLLER); }
      else if(menuSelection==2) { initPong(); changeState(STATE_GAME_PONG); }
      else if(menuSelection==3) { initRacing(0); changeState(STATE_GAME_RACING); }
      else changeState(STATE_MAIN_MENU);
      break;
    case STATE_KEYBOARD:
      userInput += keyboardLower[cursorY][cursorX];
      break;
    case STATE_GAME_SPACE_INVADERS:
      for(int i=0; i<MAX_BULLETS; i++) if(!invaders.bullets[i].active) { invaders.bullets[i].x=invaders.playerX+10; invaders.bullets[i].y=invaders.playerY; invaders.bullets[i].active=true; break; }
      break;
    case STATE_GAME_SIDE_SCROLLER:
      for(int i=0; i<MAX_SCROLLER_BULLETS; i++) if(!scroller.bullets[i].active) { scroller.bullets[i].x=scroller.playerX+10; scroller.bullets[i].y=scroller.playerY; scroller.bullets[i].active=true; break; }
      break;
    default: break;
  }
}
void handleBackButton() {
  if(currentState!=STATE_MAIN_MENU) changeState(STATE_MAIN_MENU);
}

// Misc Helpers
void showMainMenu(int x) {
  canvas.fillScreen(SSD1306_BLACK);
  static const char* items[] = {"AI Chat","WiFi","Games","Video","Courier","System"};
  int sidebarW = 60;
  canvas.drawLine(sidebarW, 0, sidebarW, 170, SSD1306_WHITE);
  for(int i=0; i<6; i++) {
      if(i==menuSelection) { canvas.fillRect(0, 10+i*25, sidebarW, 24, SSD1306_WHITE); canvas.setTextColor(SSD1306_BLACK); }
      else canvas.setTextColor(SSD1306_WHITE);
      canvas.setCursor(5, 15+i*25); canvas.print(items[i]);
  }
  canvas.setTextColor(SSD1306_WHITE);
  canvas.setCursor(sidebarW+10, 10); canvas.setTextSize(2); canvas.print(items[menuSelection]);
}

void showWiFiMenu(int x) {
  static const char* items[] = {"Scan Networks","Forget","Back"};
  drawGenericListMenu(x, "WIFI", NULL, items, 3, menuSelection, &menuScrollY);
}
void showAPISelect(int x) {
  static const char* items[] = {"Key 1","Key 2"};
  drawGenericListMenu(x, "API", NULL, items, 2, menuSelection, &menuScrollY);
}
void showGameSelect(int x) {
  static const char* items[] = {"Invaders","Scroller","Pong","Racing","Back"};
  drawGenericListMenu(x, "GAMES", NULL, items, 5, menuSelection, &menuScrollY);
}
void showSystemMenu(int x) {
  static const char* items[] = {"Status","Settings","Tools","Back"};
  drawGenericListMenu(x, "SYSTEM", NULL, items, 4, systemMenuSelection, &systemMenuScrollY);
}
void showSystemPerf(int x) {
  canvas.fillScreen(SSD1306_BLACK); drawStatusBar();
  canvas.setCursor(20,40); canvas.print("CPU: 240MHz");
  canvas.setCursor(20,60); canvas.print("RAM: "+String(ESP.getFreeHeap()/1024)+"KB");
  canvas.setCursor(20,80); canvas.print("BAT: "+String(getBatteryVoltage())+"V");
}
void showSystemNet(int x) { canvas.fillScreen(SSD1306_BLACK); drawStatusBar(); canvas.setCursor(20,40); canvas.print("IP: "+WiFi.localIP().toString()); }
void showSystemDevice(int x) { canvas.fillScreen(SSD1306_BLACK); drawStatusBar(); canvas.setCursor(20,40); canvas.print("Chip: "+String(ESP.getChipModel())); }
void showSystemBenchmark(int x) { canvas.fillScreen(SSD1306_BLACK); canvas.setCursor(20,40); canvas.print("I2C BENCH"); }
void showSystemPower(int x) { static const char* items[] = {"Saver","Balanced","Perf"}; drawGenericListMenu(x, "POWER", NULL, items, 3, menuSelection, &menuScrollY); }
void showSystemStatusMenu(int x) { static const char* items[] = {"Perf","Net","Dev","Back"}; drawGenericListMenu(x, "STATUS", NULL, items, 4, systemMenuSelection, &systemMenuScrollY); }
void showSystemSettingsMenu(int x) { static const char* items[] = {"Power","Bright","FPS","PIN","Back"}; drawGenericListMenu(x, "SETTINGS", NULL, items, 5, systemMenuSelection, &systemMenuScrollY); }
void showSystemToolsMenu(int x) { static const char* items[] = {"Clear","Bench","Spam","Deauth","Probe","Ble","Cour","Recov","Boot","Back"}; drawGenericListMenu(x, "TOOLS", NULL, items, 10, systemMenuSelection, &systemMenuScrollY); }
void showRacingModeSelect(int x) { static const char* items[] = {"Free","Challenge"}; drawGenericListMenu(x, "RACE", NULL, items, 2, menuSelection, &menuScrollY); }
void showLoadingAnimation(int x) { canvas.fillScreen(SSD1306_BLACK); canvas.setCursor(100,80); canvas.print("LOADING..."); }
void displayWiFiNetworks(int x) { canvas.fillScreen(SSD1306_BLACK); canvas.setCursor(20,20); canvas.print("Networks: "+String(networkCount)); }
void displayResponse() { canvas.fillScreen(SSD1306_BLACK); canvas.setCursor(0,20); canvas.print(aiResponse); }

void drawGenericListMenu(int x_offset, const char* title, const unsigned char* icon, const char** items, int itemCount, int selection, float* scrollY) {
  canvas.fillScreen(SSD1306_BLACK); drawStatusBar();
  canvas.setTextSize(2); canvas.setCursor(20,20); canvas.print(title);
  for(int j=0;j<itemCount;j++) {
    if(j==selection) canvas.setTextColor(COLOR_GREEN); else canvas.setTextColor(SSD1306_WHITE);
    canvas.setCursor(20, 50+j*20); canvas.print(items[j]);
  }
}

// Helpers
void changeState(AppState n) { currentState=n; transitionState=TRANSITION_IN; transitionProgress=0; if(n==STATE_SCREEN_SAVER) { if(screensaverMode==0) initLife(); else initMatrix(); } }
void ledHeartbeat() { digitalWrite(LED_BUILTIN, (millis()/100)%20<2); }
void ledBlink(int s) { digitalWrite(LED_BUILTIN, (millis()/s)%2); }
void ledSuccess() { for(int i=0;i<3;i++) { digitalWrite(LED_BUILTIN,1); delay(50); digitalWrite(LED_BUILTIN,0); delay(50); } }
void ledError() { for(int i=0;i<5;i++) { digitalWrite(LED_BUILTIN,1); delay(50); digitalWrite(LED_BUILTIN,0); delay(50); } }
void ledQuickFlash() { digitalWrite(LED_BUILTIN,1); delay(20); digitalWrite(LED_BUILTIN,0); }
void drawStatusBar() { canvas.setTextSize(1); canvas.setTextColor(SSD1306_WHITE); canvas.setCursor(0,0); canvas.print(WiFi.localIP()); canvas.setCursor(260,0); canvas.print(getBatteryVoltage()); canvas.print("V"); }
void toggleKeyboardMode() {}
const char* getCurrentKey() { return keyboardLower[cursorY][cursorX]; }
void drawKeyboard(int x) {
    canvas.fillScreen(0); canvas.drawRect(x+10,10,300,30,SSD1306_WHITE); canvas.setCursor(x+15,18); canvas.print(userInput);
    int sx=x+20, sy=60;
    for(int r=0;r<3;r++) for(int c=0;c<10;c++) {
        if(r==cursorY && c==cursorX) canvas.setTextColor(SSD1306_BLACK, SSD1306_WHITE); else canvas.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
        canvas.setCursor(sx+c*25, sy+r*25); canvas.print(keyboardLower[r][c]);
    }
}
void sendToGemini() { currentState=STATE_LOADING; HTTPClient h; h.begin(geminiEndpoint); h.POST("{\"text\":\""+userInput+"\"}"); aiResponse=h.getString(); h.end(); currentState=STATE_CHAT_RESPONSE; }
void runI2CBenchmark() {}
void handleMainMenuSelect() { if(menuSelection==0) changeState(STATE_API_SELECT); else if(menuSelection==1) changeState(STATE_WIFI_MENU); else if(menuSelection==2) changeState(STATE_GAME_SELECT); else if(menuSelection==3) changeState(STATE_VIDEO_PLAYER); else if(menuSelection==4) changeState(STATE_TOOL_COURIER); else if(menuSelection==5) changeState(STATE_SYSTEM_MENU); }
void handleWiFiMenuSelect() { if(menuSelection==0) scanWiFiNetworks(); else changeState(STATE_MAIN_MENU); }
void handleAPISelectSelect() { changeState(STATE_KEYBOARD); }
void handleGameSelectSelect() { if(menuSelection==0) { initSpaceInvaders(); changeState(STATE_GAME_SPACE_INVADERS); } else if(menuSelection==1) { initSideScroller(); changeState(STATE_GAME_SIDE_SCROLLER); } else if(menuSelection==2) { initPong(); changeState(STATE_GAME_PONG); } else if(menuSelection==3) { initRacing(0); changeState(STATE_GAME_RACING); } else changeState(STATE_MAIN_MENU); }
void handleRacingModeSelect() {}
void handleSystemMenuSelect() { changeState(STATE_SYSTEM_SUB_STATUS); }
void handleKeyPress() { userInput+=getCurrentKey(); }
void handlePasswordKeyPress() { passwordInput+=getCurrentKey(); }
void connectToWiFi(String s, String p) { WiFi.begin(s.c_str(),p.c_str()); }
void forgetNetwork() { WiFi.disconnect(); }
void drawIcon(int x, int y, const unsigned char* i) {}

void updateNeoPixel() { if (neoPixelEffectEnd > 0 && millis() > neoPixelEffectEnd) { neoPixelColor = 0; pixels.setPixelColor(0, neoPixelColor); pixels.show(); neoPixelEffectEnd = 0; } }
void triggerNeoPixelEffect(uint32_t color, int duration) { neoPixelColor = color; pixels.setPixelColor(0, neoPixelColor); pixels.show(); neoPixelEffectEnd = millis() + duration; }

void showBootScreen() {
    canvas.fillScreen(SSD1306_BLACK); canvas.setTextSize(3); canvas.setTextColor(COLOR_CYAN);
    canvas.setCursor(60, 60); canvas.print("SHARKY");
    canvas.setTextSize(1); canvas.setCursor(100, 100); canvas.print("STATION");
    tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 320, 170); delay(3000);
}

void setup() {
    Serial.begin(115200); delay(500); setCpuFrequencyMhz(CPU_FREQ);
    SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
    if(!LittleFS.begin(true)) Serial.println("FS Fail"); else loadChatHistory();
    pinMode(BTN_UP,INPUT_PULLUP); pinMode(BTN_DOWN,INPUT_PULLUP); pinMode(BTN_LEFT,INPUT_PULLUP);
    pinMode(BTN_RIGHT,INPUT_PULLUP); pinMode(BTN_SELECT,INPUT_PULLUP); pinMode(BTN_BACK,INPUT_PULLUP);
    pinMode(TOUCH_LEFT,INPUT); pinMode(TOUCH_RIGHT,INPUT); pinMode(BATTERY_PIN,INPUT);
    pinMode(LED_BUILTIN,OUTPUT); pinMode(TFT_BLK,OUTPUT); digitalWrite(TFT_BLK,HIGH);
    pixels.begin(); tft.init(170, 320); tft.setRotation(1); tft.fillScreen(SSD1306_BLACK);
    ledSuccess();
    showFPS=loadPreferenceBool("showFPS",false);
    pinLockEnabled=loadPreferenceBool("pin_lock",false);
    pinCode=loadPreferenceString("pin_code","1234");
    String s=loadPreferenceString("ssid",""), p=loadPreferenceString("password","");
    if(s.length()>0) { WiFi.begin(s.c_str(),p.c_str()); configTime(25200,0,"pool.ntp.org","time.nist.gov"); }
    ArduinoOTA.begin();
    showBootScreen(); checkBootloader();
    if(pinLockEnabled) { inputPin=""; currentState=STATE_PIN_LOCK; } else showMainMenu(0);
}

void loop() {
    ArduinoOTA.handle(); unsigned long now=millis();
    if(now-lastUiUpdate>16) {
        lastUiUpdate=now; refreshCurrentScreen();
        tft.drawRGBBitmap(0,0,canvas.getBuffer(),320,170);
    }
    if(now-lastPhysicsUpdate>8) {
        lastPhysicsUpdate=now; deltaTime=0.008;
        if(currentState==STATE_GAME_SPACE_INVADERS) updateSpaceInvaders();
        else if(currentState==STATE_GAME_SIDE_SCROLLER) updateSideScroller();
        else if(currentState==STATE_GAME_PONG) updatePong();
        else if(currentState==STATE_GAME_RACING) updateRacing();

        bool btn=false;
        if(digitalRead(BTN_UP)==LOW) { handleUp(); btn=true; }
        if(digitalRead(BTN_DOWN)==LOW) { handleDown(); btn=true; }
        if(digitalRead(BTN_LEFT)==LOW) { handleLeft(); btn=true; }
        if(digitalRead(BTN_RIGHT)==LOW) { handleRight(); btn=true; }
        if(digitalRead(BTN_SELECT)==LOW) { handleSelect(); btn=true; }
        if(digitalRead(BTN_BACK)==LOW) { handleBackButton(); btn=true; }
        if(btn && now-lastDebounce>150) { lastDebounce=now; ledQuickFlash(); }
    }
}
