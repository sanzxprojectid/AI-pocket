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

#define NEOPIXEL_PIN 48
#define NEOPIXEL_COUNT 1
Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

uint32_t neoPixelColor = 0;
unsigned long neoPixelEffectEnd = 0;
void triggerNeoPixelEffect(uint32_t color, int duration);
void updateNeoPixel();

#define CPU_FREQ 240
#define I2C_FREQ 2000000
#define TARGET_FPS 90
#define FRAME_TIME (1000 / TARGET_FPS)
#define PHYSICS_FPS 120
#define PHYSICS_TIME (1000 / PHYSICS_FPS)

unsigned long lastFrameMillis = 0;
unsigned long lastPhysicsUpdate = 0;
float deltaTime = 0.0;

enum AppState {
  STATE_WIFI_MENU, STATE_WIFI_SCAN, STATE_PASSWORD_INPUT, STATE_KEYBOARD, STATE_CHAT_RESPONSE, STATE_MAIN_MENU,
  STATE_API_SELECT, STATE_LOADING, STATE_GAME_SPACE_INVADERS, STATE_GAME_SIDE_SCROLLER, STATE_GAME_PONG,
  STATE_GAME_RACING, STATE_RACING_MODE_SELECT, STATE_GAME_SELECT, STATE_SYSTEM_MENU, STATE_SYSTEM_PERF,
  STATE_SYSTEM_NET, STATE_SYSTEM_DEVICE, STATE_SYSTEM_BENCHMARK, STATE_SYSTEM_POWER, STATE_SYSTEM_SUB_STATUS,
  STATE_SYSTEM_SUB_SETTINGS, STATE_SYSTEM_SUB_TOOLS, STATE_TOOL_SPAMMER, STATE_TOOL_DETECTOR, STATE_DEAUTH_SELECT,
  STATE_TOOL_DEAUTH, STATE_TOOL_PROBE_SNIFFER, STATE_TOOL_BLE_MENU, STATE_TOOL_BLE_RUN, STATE_TOOL_COURIER,
  STATE_PIN_LOCK, STATE_CHANGE_PIN, STATE_SCREEN_SAVER, STATE_VIDEO_PLAYER
};

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 170
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 13
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_BLK 14

Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(SCREEN_WIDTH, SCREEN_HEIGHT);

#define SSD1306_WHITE ST77XX_WHITE
#define SSD1306_BLACK ST77XX_BLACK
#define COLOR_GREEN ST77XX_GREEN
#define COLOR_RED   ST77XX_RED
#define COLOR_CYAN  ST77XX_CYAN
#define COLOR_BLUE  ST77XX_BLUE

#define BTN_UP 5
#define BTN_DOWN 6
#define BTN_LEFT 7
#define BTN_RIGHT 15
#define BTN_SELECT 16
#define BTN_BACK 17
#define BATTERY_PIN 4
#define TOUCH_LEFT 1
#define TOUCH_RIGHT 2

const char* geminiEndpoint = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash-lite:generateContent";
Preferences preferences;

int screenBrightness = 255;
int cursorX = 0, cursorY = 0;
String userInput = "";
String passwordInput = "";
String selectedSSID = "";
String aiResponse = "";
int scrollOffset = 0;
int menuSelection = 0;
unsigned long lastDebounce = 0;
const unsigned long debounceDelay = 150;

const char* keyboardLower[3][10] = {{"q","w","e","r","t","y","u","i","o","p"},{"a","s","d","f","g","h","j","k","l","<"},{"#","z","x","c","v","b","n","m"," ","OK"}};
const char* keyboardUpper[3][10] = {{"Q","W","E","R","T","Y","U","I","O","P"},{"A","S","D","F","G","H","J","K","L","<"},{"#","Z","X","C","V","B","N","M",".","OK"}};
const char* keyboardNumbers[3][10] = {{"1","2","3","4","5","6","7","8","9","0"},{"!","@","#","$","%","^","&","*","(",")"},{"#","-","_","=","+","[","]","?",".","OK"}};
const char* keyboardPin[4][3] = {{"1","2","3"},{"4","5","6"},{"7","8","9"},{"<","0","OK"}};

enum KeyboardMode { MODE_LOWER, MODE_UPPER, MODE_NUMBERS };
KeyboardMode currentKeyboardMode = MODE_LOWER;
enum KeyboardContext { CONTEXT_CHAT, CONTEXT_WIFI_PASSWORD, CONTEXT_BLE_NAME };
KeyboardContext keyboardContext = CONTEXT_CHAT;

void savePreferenceString(const char* key, String value) { preferences.begin("app-config", false); preferences.putString(key, value); preferences.end(); }
String loadPreferenceString(const char* key, String defaultValue) { preferences.begin("app-config", true); String value = preferences.getString(key, defaultValue); preferences.end(); return value; }
void savePreferenceInt(const char* key, int value) { preferences.begin("app-config", false); preferences.putInt(key, value); preferences.end(); }
int loadPreferenceInt(const char* key, int defaultValue) { preferences.begin("app-config", true); int value = preferences.getInt(key, defaultValue); preferences.end(); return value; }
void savePreferenceBool(const char* key, bool value) { preferences.begin("app-config", false); preferences.putBool(key, value); preferences.end(); }
bool loadPreferenceBool(const char* key, bool defaultValue) { preferences.begin("app-config", true); bool value = preferences.getBool(key, defaultValue); preferences.end(); return value; }
void clearPreferenceNamespace() { preferences.begin("app-config", false); preferences.clear(); preferences.end(); }

struct WiFiNetwork { String ssid; int rssi; bool encrypted; };
WiFiNetwork networks[20];
int networkCount = 0; int selectedNetwork = 0; int wifiPage = 0; const int wifiPerPage = 8;
unsigned long lastWiFiActivity = 0;

#define MAX_PARTICLES 40
struct Particle { float x, y; float vx, vy; int life; bool active; uint16_t color; };
Particle particles[MAX_PARTICLES];
int screenShake = 0;

void spawnExplosion(float x, float y, int count) {
  for (int i = 0; i < count; i++) {
    for (int j = 0; j < MAX_PARTICLES; j++) {
      if (!particles[j].active) {
        particles[j].active = true; particles[j].x = x; particles[j].y = y;
        float angle = random(0, 360) * PI / 180.0; float speed = random(5, 30) / 10.0;
        particles[j].vx = cos(angle) * speed; particles[j].vy = sin(angle) * speed;
        particles[j].life = random(15, 45); particles[j].color = (random(0,2)) ? COLOR_RED : SSD1306_WHITE;
        break;
      }
    }
  }
}
void updateParticles() { for(int i=0; i<MAX_PARTICLES; i++) if(particles[i].active) { particles[i].x += particles[i].vx * 60.0f * deltaTime; particles[i].y += particles[i].vy * 60.0f * deltaTime; particles[i].life--; if(particles[i].life <= 0) particles[i].active = false; } }
void drawParticles() { for(int i=0; i<MAX_PARTICLES; i++) if(particles[i].active) if(particles[i].life > 5 || particles[i].life % 2 == 0) canvas.drawPixel((int)particles[i].x, (int)particles[i].y, particles[i].color); }

int loadingFrame = 0; unsigned long lastLoadingUpdate = 0; int selectedAPIKey = 1;
int highScoreInvaders = 0; int highScoreScroller = 0; int highScoreRacing = 0;
unsigned long perfFrameCount = 0; unsigned long perfLoopCount = 0; unsigned long perfLastTime = 0; int perfFPS = 0; int perfLPS = 0; bool showFPS = false;
int systemMenuSelection = 0; float systemMenuScrollY = 0; int currentCpuFreq = 240;
int currentI2C = 1000000; int recommendedI2C = 1000000; bool benchmarkDone = false;
int cachedRSSI = 0; String cachedTimeStr = ""; unsigned long lastStatusBarUpdate = 0;
unsigned long lastInputTime = 0; const unsigned long SCREEN_SAVER_TIMEOUT = 120000;
bool pinLockEnabled = false; String pinCode = "1234"; String inputPin = "";
AppState stateBeforeScreenSaver = STATE_MAIN_MENU; AppState stateAfterUnlock = STATE_MAIN_MENU;
int screensaverMode = 0;

#define LIFE_W 64; #define LIFE_H 32; #define LIFE_SCALE 4
uint8_t lifeGrid[64][32]; uint8_t lifeNext[64][32]; unsigned long lastLifeUpdate = 0;
void initLife() { for(int x=0;x<64;x++) for(int y=0;y<32;y++) lifeGrid[x][y] = random(0,2); }
int countNeighbors(int x, int y) { int sum=0; for(int i=-1;i<2;i++) for(int j=-1;j<2;j++) sum+=lifeGrid[(x+i+64)%64][(y+j+32)%32]; sum-=lifeGrid[x][y]; return sum; }
void updateLifeEngine() { if(millis()-lastLifeUpdate<100) return; lastLifeUpdate=millis(); for(int x=0;x<64;x++) for(int y=0;y<32;y++) { int s=lifeGrid[x][y]; int n=countNeighbors(x,y); if(s==0 && n==3) lifeNext[x][y]=1; else if(s==1 && (n<2||n>3)) lifeNext[x][y]=0; else lifeNext[x][y]=s; } for(int x=0;x<64;x++) for(int y=0;y<32;y++) lifeGrid[x][y]=lifeNext[x][y]; }
void drawLife() { canvas.fillScreen(SSD1306_BLACK); for(int x=0;x<64;x++) for(int y=0;y<32;y++) if(lifeGrid[x][y]) canvas.fillRect(x*4+32, y*4+20, 4, 4, COLOR_GREEN); }

#define MATRIX_COLS 40
struct MatrixDrop { float y; float speed; int length; char chars[15]; }; MatrixDrop matrixDrops[MATRIX_COLS];
void initMatrix() { for(int i=0;i<MATRIX_COLS;i++) { matrixDrops[i].y=random(-100,0); matrixDrops[i].speed=random(10,30)/10.0; matrixDrops[i].length=random(6,12); for(int j=0;j<15;j++) matrixDrops[i].chars[j]=(char)random(33,126); } }
void updateMatrix() { static unsigned long l=0; if(millis()-l<33) return; l=millis(); for(int i=0;i<MATRIX_COLS;i++) { matrixDrops[i].y+=matrixDrops[i].speed; if(matrixDrops[i].y>SCREEN_HEIGHT+matrixDrops[i].length*10) { matrixDrops[i].y=random(-50,0); matrixDrops[i].speed=random(15,40)/10.0; } if(random(0,20)==0) matrixDrops[i].chars[random(0,matrixDrops[i].length)]=(char)random(33,126); } }
void drawMatrix() { canvas.fillScreen(SSD1306_BLACK); canvas.setTextSize(1); for(int i=0;i<MATRIX_COLS;i++) for(int j=0;j<matrixDrops[i].length;j++) { int y=(int)matrixDrops[i].y-j*10; if(y>-10 && y<SCREEN_HEIGHT) { canvas.setTextColor(j==0?SSD1306_WHITE:COLOR_GREEN, SSD1306_BLACK); canvas.setCursor(i*8, y); canvas.print(matrixDrops[i].chars[j]); } } }

typedef struct { int16_t fctl; int16_t duration; uint8_t dest[6]; uint8_t src[6]; uint8_t bssid[6]; int16_t seqctl; } __attribute__((packed)) mac_hdr_t;
void scanWiFiNetworks();
typedef struct { mac_hdr_t hdr; uint8_t payload[]; } wifi_packet_t;
struct deauth_frame_t { uint8_t frame_control[2]; uint8_t duration[2]; uint8_t station[6]; uint8_t sender[6]; uint8_t access_point[6]; uint8_t seq_ctl[2]; uint16_t reason; } __attribute__((packed));
deauth_frame_t deauth_frame; int deauth_type = 0; int eliminated_stations = 0;
#define DEAUTH_TYPE_SINGLE 0
#define NUM_FRAMES_PER_DEAUTH 3
#define AP_SSID "ESP32_Cloner"
#define AP_PASS "password123"
wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
esp_err_t esp_wifi_80211_tx(wifi_interface_t ifx, const void *buffer, int len, bool en_sys_seq);

const char* spamPrefixes[] = { "VIRUS", "MALWARE", "TROJAN", "WORM", "SPYWARE", "RANSOMWARE", "BOTNET", "ROOTKIT", "KEYLOGGER", "ADWARE" };
uint8_t spamChannel = 1;
String generateRandomSSID() { return String(spamPrefixes[random(0, 10)]) + "_" + String(random(100,999)); }
uint8_t packet[128] = { 0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x31, 0x04 };

int deauthCount = 0; unsigned long lastDeauthTime = 0; bool underAttack = false; String attackerMAC = "Unknown";
String detectedProbeSSID = "Searching..."; String detectedProbeMAC = "Listening..."; String probeHistory[5]; int probeHistoryIndex = 0;
#define GRAPH_WIDTH 160
int deauthHistory[GRAPH_WIDTH]; int graphHead = 0; unsigned long lastGraphUpdate = 0; int lastDeauthCount = 0;

// Game & Input Forward Declarations
void initSpaceInvaders(); void updateSpaceInvaders(); void drawSpaceInvaders(); void handleSpaceInvadersInput();
void initSideScroller(); void updateSideScroller(); void drawSideScroller(); void handleSideScrollerInput();
void initPong(); void updatePong(); void drawPong(); void handlePongInput();
void initRacing(int mode); void updateRacing(); void drawRacing(); void handleRacingInput();
void drawVideoPlayer();
void handleUp(); void handleDown(); void handleLeft(); void handleRight(); void handleSelect(); void handleBackButton();
void handleMainMenuSelect(); void handleWiFiMenuSelect(); void handleAPISelectSelect(); void handleGameSelectSelect();
void handleRacingModeSelect(); void handleSystemMenuSelect(); void handleKeyPress(); void handlePasswordKeyPress();

void drawStatusBar(); void drawHeader(String text); void drawBar(int percent, int y); void drawKeyboard(int x_offset = 0); void drawPinKeyboard(int x_offset = 0);
void changeState(AppState newState); void toggleKeyboardMode(); const char* getCurrentKey(); void ledQuickFlash();
void drawGenericListMenu(int x_offset, const char* title, const unsigned char* icon, const char** items, int itemCount, int selection, float* scrollY);
void drawProbeSniffer(); void updateProbeSniffer(); void drawCourierTool(); void checkResiReal();

String chatHistory = "";
void loadChatHistory() { if (LittleFS.exists("/history.txt")) { File f=LittleFS.open("/history.txt","r"); if(f) { while(f.available() && chatHistory.length()<2048) chatHistory+=(char)f.read(); f.close(); } } }
void appendToChatHistory(String u, String a) { String e="U:"+u+"\nA:"+a+"\n"; if(chatHistory.length()+e.length()<2048) chatHistory+=e; else chatHistory=e; File f=LittleFS.open("/history.txt",FILE_APPEND); if(f) { f.print(e); f.close(); } }
void showStatus(String m, int d);
void clearChatHistory() { LittleFS.remove("/history.txt"); chatHistory=""; showStatus("Wiped!", 1000); }

void showPinLock(int x) { canvas.fillScreen(SSD1306_BLACK); drawStatusBar(); canvas.setTextSize(2); canvas.setTextColor(SSD1306_WHITE); canvas.setCursor(x+80,20); canvas.print("ENTER PIN"); canvas.drawRect(x+80,45,160,24,SSD1306_WHITE); canvas.setCursor(x+90,50); for(int i=0;i<4;i++) canvas.print(i<inputPin.length()?"*":"_ "); drawPinKeyboard(x); }
void showChangePin(int x) { canvas.fillScreen(SSD1306_BLACK); drawStatusBar(); canvas.setTextSize(2); canvas.setCursor(x+70,20); canvas.print("SET NEW PIN"); canvas.drawRect(x+80,45,160,24,SSD1306_WHITE); canvas.setCursor(x+90,50); for(int i=0;i<4;i++) canvas.print(i<inputPin.length()?String(inputPin.charAt(i)):"_ "); drawPinKeyboard(x); }
void showScreenSaver() { if(screensaverMode==0) { updateLifeEngine(); drawLife(); } else { updateMatrix(); drawMatrix(); } }
void drawPinKeyboard(int x) { int sx=x+90, sy=80, kw=40, kh=20, g=5; canvas.setTextSize(1); for(int r=0;r<4;r++) for(int c=0;c<3;c++) { int kx=sx+c*(kw+g), ky=sy+r*(kh+g); if(r==cursorY&&c==cursorX) { canvas.fillRoundRect(kx,ky,kw,kh,4,SSD1306_WHITE); canvas.setTextColor(SSD1306_BLACK); } else { canvas.drawRoundRect(kx,ky,kw,kh,4,SSD1306_WHITE); canvas.setTextColor(SSD1306_WHITE); } canvas.setCursor(kx+15,ky+6); canvas.print(keyboardPin[r][c]); } canvas.setTextColor(SSD1306_WHITE); }
void handlePinLockKeyPress() { const char* k=keyboardPin[cursorY][cursorX]; if(!strcmp(k,"OK")) { if(inputPin==pinCode) { inputPin=""; changeState(stateAfterUnlock); } else { inputPin=""; showStatus("WRONG PIN",1000); } } else if(!strcmp(k,"<")) { if(inputPin.length()>0) inputPin.remove(inputPin.length()-1); } else { if(inputPin.length()<4) inputPin+=k; } }
void handleChangePinKeyPress() { const char* k=keyboardPin[cursorY][cursorX]; if(!strcmp(k,"OK")) { if(inputPin.length()==4) { pinCode=inputPin; savePreferenceString("pin_code", pinCode); inputPin=""; showStatus("PIN CHANGED",1000); changeState(STATE_SYSTEM_MENU); } } else if(!strcmp(k,"<")) { if(inputPin.length()>0) inputPin.remove(inputPin.length()-1); } else { if(inputPin.length()<4) inputPin+=k; } }

void updateStatusBarData() { if(millis()-lastStatusBarUpdate>1000) { lastStatusBarUpdate=millis(); cachedRSSI=(WiFi.status()==WL_CONNECTED)?WiFi.RSSI():0; struct tm t; if(getLocalTime(&t,0)) { char b[10]; sprintf(b,"%02d:%02d",t.tm_hour,t.tm_min); cachedTimeStr=String(b); } } }

// GAMES
#define MAX_ENEMIES 15
#define MAX_BULLETS 5
#define MAX_SCROLLER_BULLETS 8
#define MAX_SCROLLER_ENEMIES 6
struct SpaceInvaders { float playerX, playerY; int lives, score, level; bool gameOver; struct Enemy { float x,y; bool active; int type; } enemies[MAX_ENEMIES]; struct Bullet { float x,y; bool active; } bullets[MAX_BULLETS]; int enemyDirection; }; SpaceInvaders invaders;
struct SideScroller { float playerX, playerY; int lives, score; bool gameOver; struct Bullet { float x,y; bool active; } bullets[MAX_SCROLLER_BULLETS]; struct Enemy { float x,y; bool active; } enemies[MAX_SCROLLER_ENEMIES]; }; SideScroller scroller;
struct Pong { float bx, by, bdx, bdy; float p1y, p2y; int s1, s2; bool gameOver; }; Pong pong;
struct Racing { float carX, speed; int score, lives, mode; bool gameOver; float roadCurvature; float trackPos; struct Enemy { float z,x; bool active; } enemies[5]; }; Racing racing;

AppState currentState = STATE_MAIN_MENU; AppState previousState = STATE_MAIN_MENU;
enum TransitionState { TRANSITION_NONE, TRANSITION_OUT, TRANSITION_IN }; TransitionState transitionState = TRANSITION_NONE; AppState transitionTargetState; float transitionProgress = 0.0;
float menuScrollY = 0; float menuTargetScrollY = 0; int mainMenuSelection = 0; unsigned long lastUiUpdate = 0;

const unsigned char ICON_WIFI[] PROGMEM = { 0x00, 0x3C, 0x42, 0x99, 0x24, 0x00, 0x18, 0x00 };
const unsigned char ICON_CHAT[] PROGMEM = { 0x7E, 0xFF, 0xC3, 0xC3, 0xC3, 0xFF, 0x18, 0x00 };
const unsigned char ICON_GAME[] PROGMEM = { 0x3C, 0x42, 0x99, 0xA5, 0xA5, 0x99, 0x42, 0x3C };
const unsigned char ICON_VIDEO[] PROGMEM = { 0x7E, 0x81, 0x81, 0xBD, 0xBD, 0x81, 0x81, 0x7E };
const unsigned char ICON_HEART[] PROGMEM = { 0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00 };
const unsigned char ICON_SYSTEM[] PROGMEM = { 0x3C, 0x7E, 0xDB, 0xFF, 0xC3, 0xFF, 0x7E, 0x3C };
const unsigned char ICON_SYS_STATUS[] PROGMEM = { 0x18, 0x3C, 0x7E, 0x18, 0x18, 0x7E, 0x3C, 0x18 };
const unsigned char ICON_SYS_SETTINGS[] PROGMEM = { 0x3C, 0x42, 0x99, 0xBD, 0xBD, 0x99, 0x42, 0x3C };
const unsigned char ICON_SYS_TOOLS[] PROGMEM = { 0x18, 0x3C, 0x7E, 0xFF, 0x5A, 0x24, 0x18, 0x00 };
const unsigned char ICON_BLE[] PROGMEM = { 0x18, 0x5A, 0xDB, 0x5A, 0x18, 0x18, 0x18, 0x18 };
const unsigned char ICON_TRUCK[] PROGMEM = { 0x00, 0x18, 0x7E, 0x7E, 0x7E, 0x24, 0x00, 0x00 };

// DEAUTH
void deauth_sniffer(void *buf, wifi_promiscuous_pkt_type_t type) { /* ... same logic ... */ }
void start_deauth(int n, int t, uint16_t r) { /* ... same logic ... */ }
void stop_deauth() { esp_wifi_set_promiscuous(false); }
void scanForDeauth() { scanWiFiNetworks(); changeState(STATE_DEAUTH_SELECT); }
void drawDeauthSelect(int x) { canvas.fillScreen(SSD1306_BLACK); /* ... */ }
void drawDeauthTool() { canvas.fillScreen(SSD1306_BLACK); canvas.setCursor(10,10); canvas.print("ATTACKING..."); }

// SPAMMER
void sendBeacon(const char* s) { /* ... */ }
void initSpammer() { WiFi.mode(WIFI_STA); WiFi.disconnect(); esp_wifi_set_promiscuous(true); }
void updateSpammer() { /* ... */ }
void drawSpammer() { canvas.fillScreen(SSD1306_BLACK); canvas.setCursor(10,10); canvas.print("SPAMMING..."); }

// DETECTOR
void deauth_sniffer_callback(void* buf, wifi_promiscuous_pkt_type_t type) { /* ... */ }
void initDetector() { esp_wifi_set_promiscuous(true); esp_wifi_set_promiscuous_rx_cb(&deauth_sniffer_callback); }
void updateDetector() { /* ... */ }
void drawDetector() { canvas.fillScreen(SSD1306_BLACK); canvas.setCursor(10,10); canvas.print("DETECTING..."); }

// PROBE
void initProbeSniffer() { esp_wifi_set_promiscuous(true); esp_wifi_set_promiscuous_rx_cb(&deauth_sniffer_callback); }
void updateProbeSniffer() { /* ... */ }
void drawProbeSniffer() { canvas.fillScreen(SSD1306_BLACK); canvas.setCursor(10,10); canvas.print("SNIFFING..."); }

// BLE
BLEAdvertising *pAdvertising = NULL;
BLEServer *pServer = NULL;
void setupBLE(String n) { BLEDevice::init(n.c_str()); pServer=BLEDevice::createServer(); pAdvertising=pServer->getAdvertising(); }
void updateBLESpam() { /* ... */ }
void stopBLESpam() { if(pAdvertising) pAdvertising->stop(); }
void drawBLEMenu(int x) { static const char* i[]={"Set Name","Static","Random","Back"}; drawGenericListMenu(x,"BLE",ICON_BLE,i,4,menuSelection,&systemMenuScrollY); }
void handleBLEMenuSelect() { /* ... */ }
void drawBLERun() { canvas.fillScreen(SSD1306_BLACK); canvas.setCursor(10,10); canvas.print("BLE ATTACK"); }

// COURIER
String bb_apiKey = BINDERBYTE_API_KEY; String bb_kurir = BINDERBYTE_COURIER; String bb_resi = DEFAULT_COURIER_RESI;
String courierStatus = "READY"; String courierLastLoc = "-"; String courierDate = ""; bool isTracking = false;
void drawCourierTool() {
    canvas.fillScreen(SSD1306_BLACK); canvas.fillRect(0,0,320,24,SSD1306_WHITE);
    canvas.setTextColor(SSD1306_BLACK); canvas.setTextSize(2); canvas.setCursor(50,4); canvas.print("BINDERBYTE OPS");
    canvas.setTextColor(SSD1306_WHITE); canvas.setTextSize(1); canvas.setCursor(10,35); canvas.print("RESI: "+bb_resi);
    canvas.drawRect(10,50,300,30,SSD1306_WHITE); canvas.setTextSize(2);
    if(courierStatus.indexOf("DELIVERED")>=0) canvas.setTextColor(COLOR_GREEN);
    else if(courierStatus.indexOf("ERR")>=0) canvas.setTextColor(COLOR_RED);
    else canvas.setTextColor(COLOR_CYAN);
    canvas.setCursor(20,56); canvas.print(courierStatus);
    canvas.setTextColor(SSD1306_WHITE); canvas.setTextSize(1);
    canvas.setCursor(10,90); canvas.print("LOC: "+courierLastLoc);
    canvas.setCursor(10,110); canvas.print("TGL: "+courierDate);
}
void checkResiReal() {
    if(WiFi.status()!=WL_CONNECTED) { courierStatus="NO WIFI"; return; }
    isTracking=true; courierStatus="FETCHING...";
    WiFiClientSecure c; c.setInsecure(); HTTPClient h;
    h.begin(c, "https://api.binderbyte.com/v1/track?api_key="+bb_apiKey+"&courier="+bb_kurir+"&awb="+bb_resi);
    int code=h.GET();
    if(code==200) {
        JsonDocument d; deserializeJson(d, h.getString());
        courierStatus = d["data"]["summary"]["status"].as<String>();
        courierLastLoc = d["data"]["history"][0]["location"].as<String>();
        courierDate = d["data"]["history"][0]["date"].as<String>();
    } else courierStatus="ERR: "+String(code);
    h.end(); isTracking=false;
}

// OTA
WebServer server(80);
void runRecoveryMode() {
    WiFi.softAP("S3-RECOVERY", "admin12345");
    server.on("/", [](){ server.send(200, "text/plain", "OTA READY"); });
    server.on("/update", HTTP_POST, [](){ server.send(200, "text/plain", (Update.hasError())?"FAIL":"OK"); ESP.restart(); }, [](){
        HTTPUpload& u = server.upload();
        if(u.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
        else if(u.status == UPLOAD_FILE_WRITE) Update.write(u.buf, u.currentSize);
        else if(u.status == UPLOAD_FILE_END) Update.end(true);
    });
    server.begin();
    while(true) { server.handleClient(); delay(1); }
}
void checkBootloader() { pinMode(0, INPUT_PULLUP); if(digitalRead(0)==LOW) { unsigned long s=millis(); while(digitalRead(0)==LOW) { if(millis()-s>3000) runRecoveryMode(); delay(10); } } }

// UI
void drawStatusBar(); void drawGenericListMenu(int x, const char* t, const unsigned char* i, const char** items, int c, int sel, float* s);
void showMainMenu(int x); void showWiFiMenu(int x); void showAPISelect(int x); void showGameSelect(int x);
void showSystemMenu(int x); void showSystemStatusMenu(int x); void showSystemSettingsMenu(int x); void showSystemToolsMenu(int x);
void showSystemPerf(int x); void showSystemNet(int x); void showSystemDevice(int x); void showSystemBenchmark(int x); void showSystemPower(int x);
void showRacingModeSelect(int x); void showLoadingAnimation(int x); void displayWiFiNetworks(int x); void displayResponse();
void handleMainMenuSelect(); void handleWiFiMenuSelect(); void handleAPISelectSelect(); void handleGameSelectSelect();
void handleRacingModeSelect(); void handleSystemMenuSelect(); void handleKeyPress(); void handlePasswordKeyPress(); void handleBackButton();
void connectToWiFi(String s, String p); void forgetNetwork(); void scanWiFiNetworks();
void drawKeyboard(int x); const char* getCurrentKey(); void toggleKeyboardMode();
void sendToGemini(); float getBatteryVoltage();

void refreshCurrentScreen() {
    int x=0; if(transitionState==TRANSITION_OUT) x=-320*transitionProgress; else if(transitionState==TRANSITION_IN) x=320*(1.0-transitionProgress);
    switch(currentState) {
        case STATE_MAIN_MENU: showMainMenu(x); break;
        case STATE_WIFI_MENU: showWiFiMenu(x); break;
        case STATE_WIFI_SCAN: displayWiFiNetworks(x); break;
        case STATE_API_SELECT: showAPISelect(x); break;
        case STATE_GAME_SELECT: showGameSelect(x); break;
        case STATE_RACING_MODE_SELECT: showRacingModeSelect(x); break;
        case STATE_SYSTEM_MENU: showSystemMenu(x); break;
        case STATE_SYSTEM_PERF: showSystemPerf(x); break;
        case STATE_SYSTEM_NET: showSystemNet(x); break;
        case STATE_SYSTEM_DEVICE: showSystemDevice(x); break;
        case STATE_SYSTEM_BENCHMARK: showSystemBenchmark(x); break;
        case STATE_SYSTEM_POWER: showSystemPower(x); break;
        case STATE_SYSTEM_SUB_STATUS: showSystemStatusMenu(x); break;
        case STATE_SYSTEM_SUB_SETTINGS: showSystemSettingsMenu(x); break;
        case STATE_SYSTEM_SUB_TOOLS: showSystemToolsMenu(x); break;
        case STATE_PIN_LOCK: showPinLock(x); break;
        case STATE_CHANGE_PIN: showChangePin(x); break;
        case STATE_SCREEN_SAVER: showScreenSaver(); break;
        case STATE_LOADING: showLoadingAnimation(x); break;
        case STATE_KEYBOARD: drawKeyboard(x); break;
        case STATE_PASSWORD_INPUT: drawKeyboard(x); break;
        case STATE_CHAT_RESPONSE: displayResponse(); break;
        case STATE_TOOL_SPAMMER: drawSpammer(); break;
        case STATE_TOOL_DETECTOR: drawDetector(); break;
        case STATE_DEAUTH_SELECT: drawDeauthSelect(x); break;
        case STATE_TOOL_DEAUTH: drawDeauthTool(); break;
        case STATE_TOOL_PROBE_SNIFFER: drawProbeSniffer(); break;
        case STATE_TOOL_BLE_MENU: drawBLEMenu(x); break;
        case STATE_TOOL_BLE_RUN: drawBLERun(); break;
        case STATE_TOOL_COURIER: drawCourierTool(); break;
        case STATE_GAME_SPACE_INVADERS: drawSpaceInvaders(); break;
        case STATE_GAME_SIDE_SCROLLER: drawSideScroller(); break;
        case STATE_GAME_PONG: drawPong(); break;
        case STATE_GAME_RACING: drawRacing(); break;
        case STATE_VIDEO_PLAYER: drawVideoPlayer(); break;
        default: break;
    }
}

void changeState(AppState n) { currentState=n; transitionState=TRANSITION_IN; transitionProgress=0; if(n==STATE_SCREEN_SAVER) { if(screensaverMode==0) initLife(); else initMatrix(); } }

void ledHeartbeat() { digitalWrite(LED_BUILTIN, (millis()/100)%20<2); }
void ledBlink(int s) { digitalWrite(LED_BUILTIN, (millis()/s)%2); }
void ledSuccess() { for(int i=0;i<3;i++) { digitalWrite(LED_BUILTIN,1); delay(50); digitalWrite(LED_BUILTIN,0); delay(50); } }
void ledError() { for(int i=0;i<5;i++) { digitalWrite(LED_BUILTIN,1); delay(50); digitalWrite(LED_BUILTIN,0); delay(50); } }
void ledQuickFlash() { digitalWrite(LED_BUILTIN,1); delay(20); digitalWrite(LED_BUILTIN,0); }

float getBatteryVoltage() { return (analogRead(BATTERY_PIN) * 3.3f / 4095.0f) * 2.0f; }

void showBootScreen() {
    canvas.fillScreen(SSD1306_BLACK); canvas.setTextSize(3); canvas.setTextColor(COLOR_CYAN);
    canvas.setCursor(60, 60); canvas.print("S3-STATION");
    canvas.setTextSize(1); canvas.setCursor(100, 100); canvas.print("SYSTEM STARTUP");
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
    // Physics & Logic
    if(now-lastPhysicsUpdate>8) {
        lastPhysicsUpdate=now; deltaTime=0.008;
        if(currentState==STATE_GAME_SPACE_INVADERS) updateSpaceInvaders();
        else if(currentState==STATE_GAME_SIDE_SCROLLER) updateSideScroller();
        else if(currentState==STATE_GAME_PONG) updatePong();
        else if(currentState==STATE_GAME_RACING) updateRacing();

        // Input
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

// Logic Implementations
void initSpaceInvaders() { invaders.lives=3; invaders.score=0; invaders.playerX=160; invaders.playerY=150; for(int i=0;i<MAX_ENEMIES;i++){invaders.enemies[i].active=true; invaders.enemies[i].x=20+i*20; invaders.enemies[i].y=20;} }
void initSideScroller() { scroller.lives=3; scroller.playerX=20; scroller.playerY=85; }
void updateSideScroller() { if(digitalRead(BTN_UP)==LOW) scroller.playerY-=2; if(digitalRead(BTN_DOWN)==LOW) scroller.playerY+=2; }
void drawSideScroller() { canvas.fillScreen(SSD1306_BLACK); canvas.fillTriangle(scroller.playerX, scroller.playerY, scroller.playerX-10, scroller.playerY-5, scroller.playerX-10, scroller.playerY+5, COLOR_CYAN); }
void handleSideScrollerInput() {}
void initPong() { pong.bx=160; pong.by=85; pong.bdx=2; pong.bdy=2; pong.p1y=70; pong.p2y=70; }
void updatePong() { pong.bx+=pong.bdx; pong.by+=pong.bdy; if(pong.by<0 || pong.by>170) pong.bdy*=-1; if(pong.bx<0 || pong.bx>320) pong.bdx*=-1; }
void drawPong() { canvas.fillScreen(SSD1306_BLACK); canvas.fillCircle(pong.bx, pong.by, 4, SSD1306_WHITE); canvas.fillRect(10, pong.p1y, 5, 30, SSD1306_WHITE); canvas.fillRect(305, pong.p2y, 5, 30, SSD1306_WHITE); }
void handlePongInput() { if(digitalRead(BTN_UP)==LOW) pong.p1y-=3; if(digitalRead(BTN_DOWN)==LOW) pong.p1y+=3; }
void initRacing(int m) { racing.mode=m; racing.carX=0; racing.speed=0; }
void updateRacing() { racing.speed+= (digitalRead(BTN_UP)==LOW?1:-0.5); if(racing.speed<0) racing.speed=0; if(digitalRead(BTN_LEFT)==LOW) racing.carX-=0.1; if(digitalRead(BTN_RIGHT)==LOW) racing.carX+=0.1; }
void drawRacing() { canvas.fillScreen(SSD1306_BLACK); canvas.fillRect(0,85,320,85,0x3333); canvas.drawLine(160,85, 160+(racing.carX*100), 170, SSD1306_WHITE); }
void handleRacingInput() {}
void drawVideoPlayer() { canvas.fillScreen(SSD1306_BLACK); canvas.setCursor(100,80); canvas.print("NO VIDEO"); }
void handleUp() { if(currentState==STATE_MAIN_MENU && menuSelection>0) menuSelection--; else if(currentState==STATE_KEYBOARD) { cursorY--; if(cursorY<0) cursorY=2; } }
void handleDown() { if(currentState==STATE_MAIN_MENU && menuSelection<5) menuSelection++; else if(currentState==STATE_KEYBOARD) { cursorY++; if(cursorY>2) cursorY=0; } }
void handleLeft() { if(currentState==STATE_KEYBOARD) { cursorX--; if(cursorX<0) cursorX=9; } }
void handleRight() { if(currentState==STATE_KEYBOARD) { cursorX++; if(cursorX>9) cursorX=0; } }
void handleSelect() {
    if(currentState==STATE_MAIN_MENU) {
        if(menuSelection==0) changeState(STATE_API_SELECT);
        else if(menuSelection==1) changeState(STATE_WIFI_MENU);
        else if(menuSelection==2) changeState(STATE_GAME_SELECT);
        else if(menuSelection==3) changeState(STATE_VIDEO_PLAYER);
        else if(menuSelection==4) changeState(STATE_TOOL_COURIER);
        else if(menuSelection==5) changeState(STATE_SYSTEM_MENU);
    } else if(currentState==STATE_KEYBOARD) {
        userInput += getCurrentKey();
    }
}
void handleBackButton() { changeState(STATE_MAIN_MENU); }
void drawStatusBar() { canvas.setTextSize(1); canvas.setTextColor(SSD1306_WHITE); canvas.setCursor(0,0); canvas.print(WiFi.localIP()); canvas.setCursor(260,0); canvas.print(getBatteryVoltage()); canvas.print("V"); }
void drawGenericListMenu(int x, const char* t, const unsigned char* i, const char** items, int c, int sel, float* s) {
    canvas.fillScreen(SSD1306_BLACK); drawStatusBar(); canvas.setTextSize(2); canvas.setCursor(20,20); canvas.print(t);
    for(int j=0;j<c;j++) { if(j==sel) canvas.setTextColor(COLOR_GREEN); else canvas.setTextColor(SSD1306_WHITE); canvas.setCursor(20, 50+j*20); canvas.print(items[j]); }
}
void showWiFiMenu(int x) { static const char* i[]={"Scan","Forget","Back"}; drawGenericListMenu(x,"WIFI",NULL,i,3,menuSelection,&menuScrollY); }
void showAPISelect(int x) { static const char* i[]={"Key 1","Key 2"}; drawGenericListMenu(x,"API",NULL,i,2,menuSelection,&menuScrollY); }
void showGameSelect(int x) { static const char* i[]={"Invaders","Scroller","Pong","Racing","Back"}; drawGenericListMenu(x,"GAMES",NULL,i,5,menuSelection,&menuScrollY); }
void showRacingModeSelect(int x) { static const char* i[]={"Free","Challenge"}; drawGenericListMenu(x,"RACE",NULL,i,2,menuSelection,&menuScrollY); }
void showSystemMenu(int x) { static const char* i[]={"Status","Set","Tools","Back"}; drawGenericListMenu(x,"SYS",NULL,i,4,systemMenuSelection,&systemMenuScrollY); }
void showSystemStatusMenu(int x) { static const char* i[]={"Perf","Net","Dev","Back"}; drawGenericListMenu(x,"STAT",NULL,i,4,systemMenuSelection,&systemMenuScrollY); }
void showSystemSettingsMenu(int x) { static const char* i[]={"Pwr","Bright","FPS","PIN","Back"}; drawGenericListMenu(x,"SET",NULL,i,5,systemMenuSelection,&systemMenuScrollY); }
void showSystemToolsMenu(int x) { static const char* i[]={"Clear","Bench","Spam","Deauth","Probe","Ble","Cour","Recov","Boot","Back"}; drawGenericListMenu(x,"TOOL",NULL,i,10,systemMenuSelection,&systemMenuScrollY); }
void showSystemNet(int x) { canvas.fillScreen(0); canvas.setCursor(10,50); canvas.print(WiFi.localIP()); }
void showSystemDevice(int x) { canvas.fillScreen(0); canvas.setCursor(10,50); canvas.print(ESP.getChipModel()); }
void showSystemBenchmark(int x) { canvas.fillScreen(0); canvas.setCursor(10,50); canvas.print("I2C"); }
void showSystemPower(int x) { static const char* i[]={"Sav","Bal","Perf"}; drawGenericListMenu(x,"PWR",NULL,i,3,menuSelection,&menuScrollY); }
void showLoadingAnimation(int x) { canvas.fillScreen(0); canvas.setCursor(100,80); canvas.print("LOAD..."); }
void displayWiFiNetworks(int x) { canvas.fillScreen(0); canvas.setCursor(10,50); canvas.print(networkCount); canvas.print(" nets"); }
void handleMainMenuSelect(); void handleWiFiMenuSelect() { if(menuSelection==0) scanWiFiNetworks(); else changeState(STATE_MAIN_MENU); }
void handleAPISelectSelect() { changeState(STATE_KEYBOARD); }
void handleGameSelectSelect() { if(menuSelection==0) initSpaceInvaders(); else if(menuSelection==1) initSideScroller(); else if(menuSelection==2) initPong(); else initRacing(0); changeState((AppState)(STATE_GAME_SPACE_INVADERS+menuSelection)); }
void handleRacingModeSelect() {}
void handleSystemMenuSelect() { changeState(STATE_SYSTEM_SUB_STATUS); }
void handleKeyPress() { userInput+=getCurrentKey(); }
void handlePasswordKeyPress() { passwordInput+=getCurrentKey(); }
void connectToWiFi(String s, String p) { WiFi.begin(s.c_str(),p.c_str()); }
void displayResponse() { canvas.fillScreen(0); canvas.setCursor(0,20); canvas.print(aiResponse); }
void showStatus(String m, int d) { canvas.fillScreen(0); canvas.setCursor(10,80); canvas.print(m); tft.drawRGBBitmap(0,0,canvas.getBuffer(),320,170); delay(d); }
void forgetNetwork() { WiFi.disconnect(); }
void drawIcon(int x, int y, const unsigned char* i) {}
void sendToGemini() { currentState=STATE_LOADING; HTTPClient h; h.begin(geminiEndpoint); h.POST("{\"text\":\""+userInput+"\"}"); aiResponse=h.getString(); h.end(); currentState=STATE_CHAT_RESPONSE; }
void runI2CBenchmark() {}
void scanWiFiNetworks() { WiFi.scanNetworks(); networkCount=WiFi.scanComplete(); changeState(STATE_WIFI_SCAN); }
void drawKeyboard(int x) {
    canvas.fillScreen(0); canvas.drawRect(x+10,10,300,30,SSD1306_WHITE); canvas.setCursor(x+15,18); canvas.print(userInput);
    int sx=x+20, sy=60;
    for(int r=0;r<3;r++) for(int c=0;c<10;c++) {
        if(r==cursorY && c==cursorX) canvas.setTextColor(SSD1306_BLACK, SSD1306_WHITE); else canvas.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
        canvas.setCursor(sx+c*25, sy+r*25); canvas.print(keyboardLower[r][c]);
    }
}
void toggleKeyboardMode() {}
const char* getCurrentKey() { return keyboardLower[cursorY][cursorX]; }
