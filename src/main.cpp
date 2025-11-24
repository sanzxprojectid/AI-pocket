#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>

// ==========================================
// SYSTEM CONFIGURATION & PERFORMANCE
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define SDA_PIN 41
#define SCL_PIN 40

// Input Pins
#define BTN_UP 10
#define BTN_DOWN 11
#define BTN_LEFT 9
#define BTN_RIGHT 13
#define BTN_SELECT 14
#define BTN_BACK 12
#define TOUCH_LEFT 1
#define TOUCH_RIGHT 2

// High Performance Settings
#define I2C_SPEED 800000
#define CPU_FREQ 240

// ==========================================
// GLOBALS
// ==========================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Preferences preferences;

// API Keys
const char* geminiApiKey1 = "AIzaSyAtKmbcvYB8wuHI9bqkOhufJld0oSKv7zM";
const char* geminiApiKey2 = "AIzaSyBvXPx3SrrRRJIU9Wf6nKcuQu9XjBlSH6Y";
const char* geminiEndpoint = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash-exp:generateContent";
int selectedAPIKey = 1;

enum AppState {
    STATE_BOOT,
    STATE_MAIN_MENU,
    STATE_WIFI_MENU,
    STATE_WIFI_SCAN,
    STATE_CHAT_INPUT,
    STATE_CHAT_LOADING,
    STATE_CHAT_RESULT,
    STATE_GAME_INVADERS,
    STATE_GAME_SCROLLER,
    STATE_GAME_PONG,
    // Preserved States (even if simplified for now, we keep the enum)
    STATE_ESP_NOW,
    STATE_VIDEO_PLAYER,
    STATE_NOTES
};

AppState currentState = STATE_BOOT;

// Time & Input
unsigned long lastFrameTime = 0;
float dt = 0.0f;

struct InputState {
    bool up, down, left, right, select, back;
    bool upPressed, downPressed, leftPressed, rightPressed, selectPressed, backPressed;
};
InputState input;

// Visuals
float screenShakeX = 0, screenShakeY = 0, shakeTrauma = 0;
struct Particle { float x, y, vx, vy, life; bool active; int color; };
#define MAX_PARTICLES 50
Particle particles[MAX_PARTICLES];

// WiFi & Chat Globals
String chatResponse = "";
int wifiScanIndex = 0;
int wifiScanScroll = 0;
String wifiSSID = "";
String wifiPass = "";
String chatInput = "";
bool keyboardWifiMode = false;

// Forward Declarations
void handleInput();
void updateShake(float dt);
void drawMainMenu();
void drawHeader(String title);
void updateGameInvaders(float dt); void drawGameInvaders();
void updateGameScroller(float dt); void drawGameScroller();
void updateGamePong(float dt); void drawGamePong();
void drawKeyboard(); void handleKeyboardInput();
void initParticles(); void updateParticles(float dt); void drawParticles();
void spawnExplosion(float x, float y, int count, int color);
void addShake(float amount);
void scanWiFiNetworks(); void displayWiFiNetworks(); void drawWiFiMenu(); void drawChatResult();

void setup() {
    Serial.begin(115200);
    setCpuFrequencyMhz(CPU_FREQ);
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(I2C_SPEED);
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) for(;;);

    pinMode(BTN_UP, INPUT_PULLUP); pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_LEFT, INPUT_PULLUP); pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(BTN_SELECT, INPUT_PULLUP); pinMode(BTN_BACK, INPUT_PULLUP);
    pinMode(TOUCH_LEFT, INPUT); pinMode(TOUCH_RIGHT, INPUT);
    pinMode(LED_BUILTIN, OUTPUT);

    initParticles();

    // Load saved WiFi
    preferences.begin("wifi-creds", true);
    String ssid = preferences.getString("ssid", "");
    String pass = preferences.getString("password", "");
    preferences.end();

    if(ssid.length() > 0) {
        WiFi.begin(ssid.c_str(), pass.c_str());
    }

    currentState = STATE_BOOT;
}

void loop() {
    unsigned long now = millis();
    dt = (now - lastFrameTime) / 1000.0f;
    if(dt > 0.05f) dt = 0.05f;
    lastFrameTime = now;

    handleInput();
    updateShake(dt);

    // Clear with Shake Offset applied logically if we had a camera system,
    // but for SSD1306 we just clear.
    display.clearDisplay();

    switch(currentState) {
        case STATE_BOOT:
            // Boot Animation
            drawHeader("SYSTEM INIT");
            display.setCursor(10, 30); display.print("Loading...");
            display.drawRect(10, 45, (millis()/20)%100, 5, SSD1306_WHITE);
            if(millis() > 2000) currentState = STATE_MAIN_MENU;
            break;

        case STATE_MAIN_MENU: drawMainMenu(); break;

        // Games (High Performance Mode)
        case STATE_GAME_INVADERS: updateGameInvaders(dt); drawGameInvaders(); break;
        case STATE_GAME_SCROLLER: updateGameScroller(dt); drawGameScroller(); break;
        case STATE_GAME_PONG: updateGamePong(dt); drawGamePong(); break;

        // Utilities
        case STATE_WIFI_MENU: drawWiFiMenu(); break;
        case STATE_WIFI_SCAN: displayWiFiNetworks(); break;
        case STATE_CHAT_INPUT: drawKeyboard(); handleKeyboardInput(); break;
        case STATE_CHAT_RESULT: drawChatResult(); break;
        case STATE_CHAT_LOADING:
             drawHeader("Thinking...");
             display.setCursor(30, 30); display.print("AI Processing");
             display.fillCircle(64 + sin(millis()/200.0)*20, 45, 3, SSD1306_WHITE);
             // Logic handled in keyboard input currently
             break;

        default: currentState = STATE_MAIN_MENU; break;
    }
    display.display();
}

// UTILS
void handleInput() {
    // Debounce could be added here, but for high-perf games we want raw input mostly.
    // We'll use simple edge detection for menus.
    static bool lastUp, lastDown, lastLeft, lastRight, lastSelect, lastBack;

    bool u = digitalRead(BTN_UP)==0;
    bool d = digitalRead(BTN_DOWN)==0;
    bool l = digitalRead(BTN_LEFT)==0 || digitalRead(TOUCH_LEFT)==HIGH;
    bool r = digitalRead(BTN_RIGHT)==0 || digitalRead(TOUCH_RIGHT)==HIGH;
    bool s = digitalRead(BTN_SELECT)==0;
    bool b = digitalRead(BTN_BACK)==0;

    input.up = u; input.down = d; input.left = l; input.right = r; input.select = s; input.back = b;

    input.upPressed = u && !lastUp;
    input.downPressed = d && !lastDown;
    input.leftPressed = l && !lastLeft;
    input.rightPressed = r && !lastRight;
    input.selectPressed = s && !lastSelect;
    input.backPressed = b && !lastBack;

    lastUp=u; lastDown=d; lastLeft=l; lastRight=r; lastSelect=s; lastBack=b;
}

void updateShake(float dt) {
    if(shakeTrauma > 0) {
        shakeTrauma -= dt * 2.0f;
        if(shakeTrauma < 0) shakeTrauma = 0;
        float s = shakeTrauma * shakeTrauma * 5.0f;
        screenShakeX = (random(100)/50.0f - 1.0f) * s;
        screenShakeY = (random(100)/50.0f - 1.0f) * s;
    } else {
        screenShakeX = 0;
        screenShakeY = 0;
    }
}

void addShake(float amount) {
    shakeTrauma += amount;
    if(shakeTrauma > 1.0f) shakeTrauma = 1.0f;
}

void initParticles() {
    for(int i=0; i<MAX_PARTICLES; i++) particles[i].active = false;
}

void updateParticles(float dt) {
    for(int i=0; i<MAX_PARTICLES; i++) {
        if(particles[i].active) {
            particles[i].x += particles[i].vx * dt;
            particles[i].y += particles[i].vy * dt;
            particles[i].life -= dt * 2.0f;
            if(particles[i].life <= 0) particles[i].active = false;
        }
    }
}

void drawParticles() {
    for(int i=0; i<MAX_PARTICLES; i++) {
        if(particles[i].active) {
            // Apply shake to particles too
            display.drawPixel((int)(particles[i].x + screenShakeX), (int)(particles[i].y + screenShakeY), SSD1306_WHITE);
        }
    }
}

void spawnExplosion(float x, float y, int count, int color) {
    int n = 0;
    for(int i=0; i<MAX_PARTICLES && n<count; i++) {
        if(!particles[i].active) {
            particles[i] = {
                x, y,
                cos(random(0,628)/100.0f) * random(20,100),
                sin(random(0,628)/100.0f) * random(20,100),
                1.0f, true, color
            };
            n++;
        }
    }
}

void drawHeader(String title) {
    display.fillRect(0, 0, SCREEN_WIDTH, 10, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(2, 1);
    display.print(title);
    display.setTextColor(SSD1306_WHITE);
    
    // Simple Battery Indicator
    display.drawRect(110, 2, 16, 6, SSD1306_BLACK);
    display.fillRect(112, 3, 12, 4, SSD1306_BLACK);
}

// ==========================================
// GAME STATE STRUCTS
// ==========================================

// --- SPACE INVADERS ---
struct InvaderEntity { float x, y, w, h; bool active; int type; };
struct InvaderGame {
    float playerX, playerY;
    int score, lives, level;
    bool gameOver;
    float enemyMoveTimer;
    int enemyDir;
    std::vector<InvaderEntity> enemies;
    std::vector<InvaderEntity> bullets;
    std::vector<InvaderEntity> enemyBullets;
    
    void reset() {
        playerX = SCREEN_WIDTH/2 - 4; playerY = SCREEN_HEIGHT - 10;
        score = 0; lives = 3; level = 1; gameOver = false;
        enemyMoveTimer = 0; enemyDir = 1;
        enemies.clear(); bullets.clear(); enemyBullets.clear();
        spawnWave();
    }
    void spawnWave() {
        int rows = 3 + (level/2); if(rows>5) rows=5;
        for(int r=0; r<rows; r++) {
            for(int c=0; c<6; c++) {
                enemies.push_back({(float)(10+c*16), (float)(10+r*10), 8, 6, true, r%3});
            }
        }
    }
} invaderGame;

// --- SCROLLER ---
struct ScrollerEntity { float x, y, w, h; bool active; int type; };
struct ScrollerGame {
    float playerX, playerY, playerVelY;
    int score, lives;
    bool gameOver;
    std::vector<ScrollerEntity> obstacles;
    std::vector<ScrollerEntity> stars;
    std::vector<ScrollerEntity> bullets;
    float spawnTimer;
    
    void reset() {
        playerX = 10; playerY = SCREEN_HEIGHT/2; playerVelY = 0;
        score = 0; lives = 3; gameOver = false; spawnTimer = 0;
        obstacles.clear(); bullets.clear(); stars.clear();
        for(int i=0; i<20; i++) stars.push_back({(float)random(0,128), (float)random(0,64), (float)random(1,3), 0, true, 0});
    }
} scrollerGame;

// --- PONG ---
struct PongGame {
    float ballX, ballY, ballVelX, ballVelY;
    float paddle1Y, paddle2Y;
    int score1, score2;
    bool aiMode, gameOver;
    struct Point { float x, y; } trail[10];
    int trailIdx;
    
    void reset() {
        ballX=64; ballY=32; ballVelX=80; ballVelY=60;
        paddle1Y=22; paddle2Y=22; score1=0; score2=0;
        aiMode=true; gameOver=false;
    }
} pongGame;

// ==========================================
// GAME LOGIC IMPLEMENTATIONS
// ==========================================

void updateGameInvaders(float dt) {
    if(invaderGame.gameOver) {
        if(input.selectPressed) invaderGame.reset();
        if(input.backPressed) currentState = STATE_MAIN_MENU;
        return;
    }
    
    // Player
    if(input.left) invaderGame.playerX -= 100 * dt;
    if(input.right) invaderGame.playerX += 100 * dt;
    invaderGame.playerX = constrain(invaderGame.playerX, 0, SCREEN_WIDTH-8);
    
    if(input.selectPressed) {
        invaderGame.bullets.push_back({invaderGame.playerX+4, invaderGame.playerY, 1, 4, true, 0});
        screenShakeY = 2;
    }

    // Bullets
    for(auto &b : invaderGame.bullets) {
        if(b.active) {
            b.y -= 150 * dt;
            if(b.y < 0) b.active = false;
            for(auto &e : invaderGame.enemies) {
                if(e.active && b.x >= e.x && b.x <= e.x+e.w && b.y >= e.y && b.y <= e.y+e.h) {
                    e.active = false; b.active = false;
                    invaderGame.score += 10;
                    spawnExplosion(e.x+4, e.y+3, 5, 1);
                    addShake(0.2f);
                }
            }
        }
    }

    // Enemies
    invaderGame.enemyMoveTimer += dt;
    if(invaderGame.enemyMoveTimer > 0.05f) {
        bool hitEdge = false;
        for(auto &e : invaderGame.enemies) {
            if(e.active) {
                e.x += invaderGame.enemyDir * 20.0f * dt;
                if(e.x < 2 || e.x > SCREEN_WIDTH-10) hitEdge = true;
            }
        }
        if(hitEdge) {
            invaderGame.enemyDir *= -1;
            for(auto &e : invaderGame.enemies) e.y += 4;
        }
        // Shoot
        if(random(0,100)<2) {
             for(auto &e : invaderGame.enemies) {
                 if(e.active && random(0,10)==0) {
                     invaderGame.enemyBullets.push_back({e.x+4, e.y+6, 2, 4, true, 0});
                     break;
                 }
             }
        }
        invaderGame.enemyMoveTimer = 0;
    }
    
    // Enemy Bullets
    for(auto &b : invaderGame.enemyBullets) {
        if(b.active) {
            b.y += 80 * dt;
            if(b.y > SCREEN_HEIGHT) b.active = false;
            if(b.x >= invaderGame.playerX && b.x <= invaderGame.playerX+8 &&
               b.y >= invaderGame.playerY && b.y <= invaderGame.playerY+6) {
                b.active = false;
                invaderGame.lives--;
                addShake(1.0f);
                spawnExplosion(invaderGame.playerX, invaderGame.playerY, 20, 1);
                if(invaderGame.lives <= 0) invaderGame.gameOver = true;
            }
        }
    }
    
    // Wave Clear
    bool allDead = true;
    for(auto &e : invaderGame.enemies) if(e.active) allDead = false;
    if(allDead) { invaderGame.level++; invaderGame.spawnWave(); addShake(0.5f); }

    updateParticles(dt);
}

void drawGameInvaders() {
    int sx = (int)screenShakeX, sy = (int)screenShakeY;
    display.setCursor(0,0); display.print("S:"); display.print(invaderGame.score);
    display.setCursor(60,0); display.print("L:"); display.print(invaderGame.lives);

    if(!invaderGame.gameOver) display.drawRect(invaderGame.playerX+sx, invaderGame.playerY+sy, 8, 6, SSD1306_WHITE);

    for(auto &e : invaderGame.enemies) if(e.active) display.drawRect(e.x+sx, e.y+sy, 8, 6, SSD1306_WHITE);
    for(auto &b : invaderGame.bullets) if(b.active) display.drawFastVLine(b.x+sx, b.y+sy, 4, SSD1306_WHITE);
    for(auto &b : invaderGame.enemyBullets) if(b.active) display.drawFastVLine(b.x+sx, b.y+sy, 4, SSD1306_WHITE);

    drawParticles();

    if(invaderGame.gameOver) {
        display.fillRect(20, 20, 88, 30, SSD1306_BLACK);
        display.drawRect(20, 20, 88, 30, SSD1306_WHITE);
        display.setCursor(35, 30); display.print("GAME OVER");
    }
}

void updateGameScroller(float dt) {
    if(scrollerGame.gameOver) {
        if(input.selectPressed) scrollerGame.reset();
        if(input.backPressed) currentState = STATE_MAIN_MENU;
        return;
    }

    if(input.up) scrollerGame.playerVelY = -80;
    else if(input.down) scrollerGame.playerVelY = 80;
    else scrollerGame.playerVelY = 0;

    if(input.left) scrollerGame.playerX -= 60 * dt;
    if(input.right) scrollerGame.playerX += 60 * dt;

    scrollerGame.playerY += scrollerGame.playerVelY * dt;
    scrollerGame.playerY = constrain(scrollerGame.playerY, 0, SCREEN_HEIGHT-8);
    scrollerGame.playerX = constrain(scrollerGame.playerX, 0, SCREEN_WIDTH/2);

    // Stars
    for(auto &s : scrollerGame.stars) {
        s.x -= s.w * 60 * dt;
        if(s.x < 0) { s.x = SCREEN_WIDTH; s.y = random(0,64); }
    }

    // Shoot
    if(input.selectPressed) scrollerGame.bullets.push_back({scrollerGame.playerX+8, scrollerGame.playerY+3, 4, 2, true, 0});

    for(auto &b : scrollerGame.bullets) {
        if(b.active) {
            b.x += 150 * dt;
            if(b.x > SCREEN_WIDTH) b.active = false;
        }
    }

    // Spawner
    scrollerGame.spawnTimer += dt;
    if(scrollerGame.spawnTimer > 1.5f) {
        scrollerGame.obstacles.push_back({(float)SCREEN_WIDTH, (float)random(0,54), 10, 10, true, 0});
        scrollerGame.spawnTimer = 0;
    }

    for(auto &e : scrollerGame.obstacles) {
        if(e.active) {
            e.x -= 60 * dt;
            // Hit Player
            if(e.x < scrollerGame.playerX+8 && e.x+e.w > scrollerGame.playerX &&
               e.y < scrollerGame.playerY+6 && e.y+e.h > scrollerGame.playerY) {
                   e.active = false; scrollerGame.lives--;
                   addShake(1.0f); spawnExplosion(scrollerGame.playerX, scrollerGame.playerY, 15, 1);
                   if(scrollerGame.lives<=0) scrollerGame.gameOver = true;
            }
            // Hit Bullet
            for(auto &b : scrollerGame.bullets) {
                if(b.active && b.x > e.x && b.x < e.x+e.w && b.y > e.y && b.y < e.y+e.h) {
                    b.active = false; e.active = false;
                    scrollerGame.score += 50;
                    spawnExplosion(e.x, e.y, 8, 1);
                }
            }
            if(e.x < -20) e.active = false;
        }
    }
    updateParticles(dt);
}

void drawGameScroller() {
    int sx = (int)screenShakeX, sy = (int)screenShakeY;
    for(auto &s : scrollerGame.stars) display.drawPixel(s.x, s.y, SSD1306_WHITE);

    if(!scrollerGame.gameOver) {
        display.fillTriangle(scrollerGame.playerX+8+sx, scrollerGame.playerY+3+sy,
                             scrollerGame.playerX+sx, scrollerGame.playerY+sy,
                             scrollerGame.playerX+sx, scrollerGame.playerY+6+sy, SSD1306_WHITE);
    }

    for(auto &e : scrollerGame.obstacles) if(e.active) display.drawRect(e.x+sx, e.y+sy, e.w, e.h, SSD1306_WHITE);
    for(auto &b : scrollerGame.bullets) if(b.active) display.drawFastHLine(b.x+sx, b.y+sy, 4, SSD1306_WHITE);

    display.setCursor(0,0); display.print("S:"); display.print(scrollerGame.score);
    drawParticles();
}

void updateGamePong(float dt) {
    if(pongGame.gameOver) {
        if(input.selectPressed) pongGame.reset();
        if(input.backPressed) currentState = STATE_MAIN_MENU;
        return;
    }

    if(input.up) pongGame.paddle1Y -= 100 * dt;
    if(input.down) pongGame.paddle1Y += 100 * dt;
    pongGame.paddle1Y = constrain(pongGame.paddle1Y, 0, SCREEN_HEIGHT-16);

    // AI
    float center = pongGame.paddle2Y + 8;
    if(center < pongGame.ballY - 4) pongGame.paddle2Y += 55 * dt;
    else if(center > pongGame.ballY + 4) pongGame.paddle2Y -= 55 * dt;
    pongGame.paddle2Y = constrain(pongGame.paddle2Y, 0, SCREEN_HEIGHT-16);

    // Ball
    pongGame.ballX += pongGame.ballVelX * dt;
    pongGame.ballY += pongGame.ballVelY * dt;

    if(pongGame.ballY <= 0 || pongGame.ballY >= 63) { pongGame.ballVelY *= -1; addShake(0.1f); }

    // Paddle Hits
    if(pongGame.ballX < 4 && pongGame.ballY > pongGame.paddle1Y && pongGame.ballY < pongGame.paddle1Y+16) {
        pongGame.ballVelX = abs(pongGame.ballVelX) * 1.1; pongGame.ballX=4; addShake(0.2f);
    }
    if(pongGame.ballX > 124 && pongGame.ballY > pongGame.paddle2Y && pongGame.ballY < pongGame.paddle2Y+16) {
        pongGame.ballVelX = -abs(pongGame.ballVelX) * 1.1; pongGame.ballX=124; addShake(0.2f);
    }

    if(pongGame.ballX < -10) { pongGame.score2++; pongGame.ballX=64; pongGame.ballVelX=80; addShake(1.0f); }
    if(pongGame.ballX > 138) { pongGame.score1++; pongGame.ballX=64; pongGame.ballVelX=-80; addShake(1.0f); }

    updateParticles(dt);
}

void drawGamePong() {
    int sx = (int)screenShakeX, sy = (int)screenShakeY;
    display.drawFastVLine(64, 0, 64, SSD1306_WHITE);
    display.setCursor(44, 2); display.print(pongGame.score1);
    display.setCursor(79, 2); display.print(pongGame.score2);
    display.fillRect(0, pongGame.paddle1Y+sy, 4, 16, SSD1306_WHITE);
    display.fillRect(124, pongGame.paddle2Y+sy, 4, 16, SSD1306_WHITE);
    display.fillCircle(pongGame.ballX+sx, pongGame.ballY+sy, 2, SSD1306_WHITE);
    drawParticles();
}

int menuSelection = 0;
void drawMainMenu() {
    drawHeader("MAIN MENU");

    const char* items[] = {"Space Invaders", "Astro Rush", "Neon Pong", "AI Chat", "WiFi Settings"};
    int count = 5;

    if(input.upPressed && menuSelection > 0) menuSelection--;
    if(input.downPressed && menuSelection < count-1) menuSelection++;

    for(int i=0; i<count; i++) {
        int y = 20 + i * 10;
        if(i == menuSelection) {
            display.fillRect(5, y-1, 118, 9, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK);
            display.setCursor(10, y); display.print(items[i]);
        } else {
            display.setTextColor(SSD1306_WHITE);
            display.setCursor(10, y); display.print(items[i]);
        }
    }
    display.setTextColor(SSD1306_WHITE);

    if(input.selectPressed) {
        if(menuSelection == 0) { invaderGame.reset(); currentState = STATE_GAME_INVADERS; }
        if(menuSelection == 1) { scrollerGame.reset(); currentState = STATE_GAME_SCROLLER; }
        if(menuSelection == 2) { pongGame.reset(); currentState = STATE_GAME_PONG; }
        if(menuSelection == 3) {
            currentState = STATE_CHAT_INPUT;
            keyboardWifiMode=false;
            chatInput="";
        }
        if(menuSelection == 4) { currentState = STATE_WIFI_MENU; }
    }
}

// Keyboard variables
const char* keysLower = "qwertyuiopasdfghjkl zxcvbnm";
int keyboardCursorX=0, keyboardCursorY=0;

void drawKeyboard() {
    drawHeader(keyboardWifiMode ? "Enter Password" : "AI Chat Prompt");

    // Display Input Field
    display.drawRect(5, 15, 118, 11, SSD1306_WHITE);
    display.setCursor(7, 17);
    String show = keyboardWifiMode ? wifiPass : chatInput;
    if(keyboardWifiMode) {
        String mask = "";
        for(int i=0; i<show.length(); i++) mask += "*";
        show = mask;
    }
    if(show.length() > 18) show = show.substring(show.length()-18);
    display.print(show);
    
    int startY = 30;
    for(int r=0; r<3; r++) {
        for(int c=0; c<10; c++) {
            int idx = r*10+c;
            if(idx<27) {
                int x = 5 + c*12 + (r==2?12:0);
                int y = startY + r*10;
                if(r==keyboardCursorY && c==keyboardCursorX) {
                    display.fillRect(x, y, 10, 8, SSD1306_WHITE);
                    display.setTextColor(SSD1306_BLACK);
                } else {
                    display.drawRect(x, y, 10, 8, SSD1306_WHITE);
                    display.setTextColor(SSD1306_WHITE);
                }
                display.setCursor(x+2, y); display.print(keysLower[idx]);
            }
        }
    }
    // OK Button
    int okY = startY + 30;
    if(keyboardCursorY==3) {
        display.fillRect(40, 55, 40, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
    } else {
        display.drawRect(40, 55, 40, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(50, 56); display.print("SEND");
    display.setTextColor(SSD1306_WHITE);
}

void handleKeyboardInput() {
    if(input.upPressed && keyboardCursorY > 0) keyboardCursorY--;
    if(input.downPressed && keyboardCursorY < 3) keyboardCursorY++;
    if(input.leftPressed && keyboardCursorX > 0) keyboardCursorX--;
    if(input.rightPressed && keyboardCursorX < 9) keyboardCursorX++;

    if(input.selectPressed) {
        if(keyboardCursorY==3) {
            // Send
            if(keyboardWifiMode) {
                WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
                preferences.begin("wifi-creds", false);
                preferences.putString("ssid", wifiSSID);
                preferences.putString("password", wifiPass);
                preferences.end();
                currentState=STATE_WIFI_MENU;
            }
            else {
                // Actual Chat Logic
                currentState=STATE_CHAT_LOADING;
                display.display(); // Force update

                if(WiFi.status() == WL_CONNECTED) {
                    WiFiClientSecure client;
                    client.setInsecure();
                    HTTPClient http;
                    String apiKey = (selectedAPIKey == 1) ? geminiApiKey1 : geminiApiKey2;
                    String url = String(geminiEndpoint) + "?key=" + apiKey;

                    JsonDocument doc;
                    JsonObject content = doc["contents"].add();
                    JsonObject parts = content["parts"].add();
                    parts["text"] = chatInput;
                    String requestBody;
                    serializeJson(doc, requestBody);

                    http.begin(client, url);
                    http.addHeader("Content-Type", "application/json");
                    int code = http.POST(requestBody);
                    if(code > 0) {
                        String resp = http.getString();
                        JsonDocument rDoc;
                        deserializeJson(rDoc, resp);
                        if(rDoc.containsKey("candidates")) {
                             const char* t = rDoc["candidates"][0]["content"]["parts"][0]["text"];
                             chatResponse = String(t);
                        } else {
                            chatResponse = "API Error";
                        }
                    } else {
                        chatResponse = "HTTP Error: " + String(code);
                    }
                    http.end();
                } else {
                    chatResponse = "No WiFi Connection";
                }
                currentState = STATE_CHAT_RESULT;
            }
        } else {
             int idx = keyboardCursorY*10+keyboardCursorX;
             if(idx < 27) {
                 if(keyboardWifiMode) wifiPass += keysLower[idx];
                 else chatInput += keysLower[idx];
             }
        }
    }
    if(input.backPressed) currentState = STATE_MAIN_MENU;
}

// WiFi & Chat Logic
void drawWiFiMenu() {
    drawHeader("WiFi Settings");
    display.setCursor(10, 20);
    if(WiFi.status() == WL_CONNECTED) {
        display.print("Connected: "); display.println(WiFi.SSID());
    } else {
        display.println("Not Connected");
    }
    display.setCursor(10, 40); display.print("[Scan Networks]");
    display.setCursor(10, 50); display.print("[Back]");

    // Selection Logic for WiFi Menu
    static int wifiMenuSel = 0;
    if(input.downPressed) wifiMenuSel = 1;
    if(input.upPressed) wifiMenuSel = 0;

    display.setCursor(2, 40 + wifiMenuSel*10); display.print(">");

    if(input.selectPressed) {
        if(wifiMenuSel == 0) {
            scanWiFiNetworks();
            currentState = STATE_WIFI_SCAN;
        } else {
            currentState = STATE_MAIN_MENU;
        }
    }
    if(input.backPressed) currentState = STATE_MAIN_MENU;
}

void scanWiFiNetworks() {
    display.clearDisplay();
    drawHeader("Scanning...");
    display.display();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.scanNetworks(true); // Async
}

void displayWiFiNetworks() {
    drawHeader("Select Network");
    int n = WiFi.scanComplete();
    if(n == -2) {
        display.setCursor(10, 30); display.print("Scanning...");
    } else if(n == 0) {
        display.setCursor(10, 30); display.print("No Networks");
        if(input.backPressed) currentState = STATE_WIFI_MENU;
    } else {
        int start = wifiScanScroll;
        for(int i=0; i<4; i++) {
            int idx = start + i;
            if(idx >= n) break;
            if(idx == wifiScanIndex) {
                display.fillRect(0, 15 + i*10, SCREEN_WIDTH, 10, SSD1306_WHITE);
                display.setTextColor(SSD1306_BLACK);
            } else {
                display.setTextColor(SSD1306_WHITE);
            }
            display.setCursor(2, 16 + i*10);
            String s = WiFi.SSID(idx);
            if(s.length()>15) s = s.substring(0,15);
            display.print(s);
        }
        display.setTextColor(SSD1306_WHITE);

        if(input.downPressed && wifiScanIndex < n-1) {
            wifiScanIndex++;
            if(wifiScanIndex >= wifiScanScroll + 4) wifiScanScroll++;
        }
        if(input.upPressed && wifiScanIndex > 0) {
            wifiScanIndex--;
            if(wifiScanIndex < wifiScanScroll) wifiScanScroll--;
        }
        if(input.selectPressed) {
            wifiSSID = WiFi.SSID(wifiScanIndex);
            wifiPass = "";
            keyboardWifiMode = true;
            currentState = STATE_CHAT_INPUT; // Reuse keyboard
        }
        if(input.backPressed) currentState = STATE_WIFI_MENU;
    }
}

void drawChatResult() {
    drawHeader("Response");
    display.setCursor(0, 12);
    display.print(chatResponse);
    if(input.backPressed) currentState = STATE_MAIN_MENU;
}
