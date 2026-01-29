#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include <AiEsp32RotaryEncoder.h>
#include <Adafruit_GC9A01A.h>
#include <Adafruit_SSD1306.h>

#include <CST816S.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>

// ===================== BLE =====================
static const char* BLE_NAME = "geelyController";
static const char* SERVICE_UUID        = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static const char* CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";

BLECharacteristic* g_char = nullptr;
volatile bool g_deviceConnected = false;

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        g_deviceConnected = true;
    }
    void onDisconnect(BLEServer* pServer) override {
        g_deviceConnected = false;
        pServer->getAdvertising()->start();
    }
};

static inline void bleSend(const char* msg) {
    if (g_deviceConnected && g_char) {
        g_char->setValue((uint8_t*)msg, strlen(msg));
        g_char->notify();
    }
}

static void bleInit() {
    BLEDevice::init(BLE_NAME);

    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(new MyServerCallbacks());

    BLEService* service = server->createService(SERVICE_UUID);
    g_char = service->createCharacteristic(
            CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    g_char->addDescriptor(new BLE2902());

    service->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMinPreferred(0x12);

    BLEDevice::startAdvertising();
}

// ===================== TFT (GC9A01 SPI) =====================
#define TFT_SCL  13
#define TFT_SDA  14
#define TFT_CS   26
#define TFT_DC   27
#define TFT_RST  25

Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);

// ===================== TOUCH (CST816S I2C) =====================
#define TP_SDA 21
#define TP_SCL 22
#define TP_RST 32
#define TP_INT 34

CST816S touch(TP_SDA, TP_SCL, TP_RST, TP_INT);

// ===================== OLED (I2C 128x64) =====================
static bool oledOk = false;
static uint8_t oledAddr = 0x3C;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

// ===================== 4067 MUX (buttons) =====================
#define MUX_SIG 33
#define MUX_S0  16
#define MUX_S1  17
#define MUX_S2  18
#define MUX_S3  19
#define MUX_EN  5  // active LOW


static const uint8_t BTN_FIRST_CH = 1;
static const uint8_t BTN_COUNT    = 15;

static const uint16_t BTN_DEBOUNCE_MS = 30;

static inline void muxSelect(uint8_t ch) {
    digitalWrite(MUX_S0, (ch >> 0) & 1);
    digitalWrite(MUX_S1, (ch >> 1) & 1);
    digitalWrite(MUX_S2, (ch >> 2) & 1);
    digitalWrite(MUX_S3, (ch >> 3) & 1);
}

static inline bool readButtonPressedByIndex(uint8_t idx) {
    uint8_t ch = BTN_FIRST_CH + idx;
    muxSelect(ch);
    delayMicroseconds(8);
    return digitalRead(MUX_SIG) == LOW;
}

// ===== Encoders A/B pins =====
#define ENC1_A 23
#define ENC1_B 4

#define ENC2_A 2
#define ENC2_B 15

// Buttons for encoder keys via MUX channels:
static const uint8_t ENC1_KEY_CH = 1;
static const uint8_t ENC2_KEY_CH = 2;

AiEsp32RotaryEncoder enc1(ENC1_A, ENC1_B, -1, -1, 4);
AiEsp32RotaryEncoder enc2(ENC2_A, ENC2_B, -1, -1, 4);

static uint32_t encKeyDownMs[2] = {0, 0};
static bool encKeyWasDown[2] = {false, false};
static const uint16_t ENC_KEY_LONG_MS = 450;

void IRAM_ATTR enc1ISR() { enc1.readEncoder_ISR(); }
void IRAM_ATTR enc2ISR() { enc2.readEncoder_ISR(); }

// ===================== Simple UI helpers =====================
static void tftText(int16_t x, int16_t y, uint8_t size, uint16_t color, const char* s) {
    tft.setTextSize(size);
    tft.setTextColor(color);
    tft.setCursor(x, y);
    tft.print(s);
}

static void tftTopStatus(const char* s) {
    tft.fillRect(0, 0, 240, 34, GC9A01A_BLACK);
    tft.setTextWrap(true);
    tft.setTextSize(2);
    tft.setTextColor(GC9A01A_WHITE);
    tft.setCursor(6, 8);
    tft.print(s);
}

static void tftBottomXY(int16_t x, int16_t y) {
    tft.fillRect(0, 200, 240, 40, GC9A01A_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(GC9A01A_WHITE);
    tft.setCursor(10, 210);
    tft.print("X:");
    tft.print(x);
    tft.print(" Y:");
    tft.print(y);
}

// ===================== Logger (OLED + TFT + Serial + BLE) =====================
// На OLED оставим 1 строку под статус, значит логов 7 строк.
static const uint8_t LOG_LINES = 7;
static const uint8_t LOG_LEN   = 32;

static char logBuf[LOG_LINES][LOG_LEN];
static uint8_t logHead = 0;
static bool logDirty = true;

static void logPush(const char* msg) {
    // сохранить
    strncpy(logBuf[logHead], msg, LOG_LEN - 1);
    logBuf[logHead][LOG_LEN - 1] = '\0';
    logHead = (logHead + 1) % LOG_LINES;
    logDirty = true;

    // вывести
    Serial.println(msg);
    tftTopStatus(msg);

    // отправить в BLE (один источник правды)
    bleSend(msg);
}

static void oledRender() {
    if (!oledOk || !logDirty) return;

    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    // 0-я строка: статус
    oled.setCursor(0, 0);
    oled.print("BLE:");
    oled.print(g_deviceConnected ? "ON " : "OFF");
    oled.print(" I2C:");
    oled.print("OK");

    // 7 строк логов
    for (uint8_t i = 0; i < LOG_LINES; i++) {
        uint8_t idx = (logHead + i) % LOG_LINES;
        oled.setCursor(0, 8 + i * 8);
        oled.print(logBuf[idx]);
    }

    oled.display();
    logDirty = false;
}

// ===================== I2C scan + OLED detect =====================
static uint8_t foundAddrs[16];
static uint8_t foundCnt = 0;

static void i2cScan() {
    foundCnt = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            if (foundCnt < sizeof(foundAddrs)) foundAddrs[foundCnt++] = addr;
        }
        delay(2);
    }
}

static bool hasAddr(uint8_t a) {
    for (uint8_t i = 0; i < foundCnt; i++) if (foundAddrs[i] == a) return true;
    return false;
}

// ===================== Buttons debounce =====================
static bool rawState[BTN_COUNT];
static bool stableState[BTN_COUNT];
static uint32_t lastChangeMs[BTN_COUNT];

static void scanButtons() {
    uint32_t now = millis();

    for (uint8_t ch = 0; ch < BTN_COUNT; ch++) {
        bool r = readButtonPressedByIndex(ch);

        if (r != rawState[ch]) {
            rawState[ch] = r;
            lastChangeMs[ch] = now;
        }

        if ((now - lastChangeMs[ch]) >= BTN_DEBOUNCE_MS) {
            if (stableState[ch] != rawState[ch]) {
                stableState[ch] = rawState[ch];

                char msg[LOG_LEN];
                // Формат событий — под BLE тоже
                uint8_t physicalCh = BTN_FIRST_CH + ch;
                snprintf(msg, sizeof(msg), "EVT:BTN:C%u:%s",
                         (unsigned)physicalCh,
                         stableState[ch] ? "DOWN" : "UP");
                logPush(msg);
            }
        }
    }
}

static long enc1Last = 0;
static long enc2Last = 0;

static void handleEncoders() {
    long p1 = enc1.readEncoder();
    long d1 = p1 - enc1Last;
    if (d1 != 0) {
        enc1Last = p1;
        logPush(d1 > 0 ? "EVT:ENC1:+1" : "EVT:ENC1:-1");
    }

    long p2 = enc2.readEncoder();
    long d2 = p2 - enc2Last;
    if (d2 != 0) {
        enc2Last = p2;
        logPush(d2 > 0 ? "EVT:ENC2:+1" : "EVT:ENC2:-1");
    }
}

static void handleEncoderKeysFromMux() {
    // pressed = true/false
    bool k1 = readButtonPressedByIndex(ENC1_KEY_CH);
    bool k2 = readButtonPressedByIndex(ENC2_KEY_CH);

    bool keys[2] = {k1, k2};

    for (int i = 0; i < 2; i++) {
        if (keys[i] && !encKeyWasDown[i]) {
            encKeyWasDown[i] = true;
            encKeyDownMs[i] = millis();
        } else if (!keys[i] && encKeyWasDown[i]) {
            encKeyWasDown[i] = false;
            uint32_t dur = millis() - encKeyDownMs[i];

            if (dur >= ENC_KEY_LONG_MS) {
                logPush(i == 0 ? "EVT:ENC1:LONG" : "EVT:ENC2:LONG");
            } else {
                logPush(i == 0 ? "EVT:ENC1:CLICK" : "EVT:ENC2:CLICK");
            }
        }
    }
}
// ===================== Touch logging (DOWN/XY/UP) =====================
static bool touchDown = false;
static uint32_t lastTouchEventMs = 0;
static uint32_t lastTouchLogMs = 0;
static int16_t lastTx = -1, lastTy = -1;

static const uint16_t TOUCH_UP_TIMEOUT_MS = 250;
static const uint16_t TOUCH_LOG_MIN_MS    = 80;
static const uint16_t TOUCH_LOG_MIN_DIST  = 6; // суммарный манхэттен

static void handleTouch() {
    uint32_t now = millis();

    // UP по таймауту (если давно не приходили touch.available())
    if (touchDown && (now - lastTouchEventMs) > TOUCH_UP_TIMEOUT_MS) {
        touchDown = false;
        logPush("EVT:TOUCH:UP");
        return;
    }

    if (!touch.available()) return;

    int16_t x = (int16_t)touch.data.x;
    int16_t y = (int16_t)touch.data.y;

    if (x == 0 && y == 0) return;

    x = constrain(x, 0, 239);
    y = constrain(y, 0, 239);

    lastTouchEventMs = now;

    // DOWN (первый пакет после "тишины")
    if (!touchDown) {
        touchDown = true;
        lastTx = x;
        lastTy = y;
        lastTouchLogMs = 0; // чтобы сразу прологочить координаты
        logPush("EVT:TOUCH:DOWN");
    }

    // рисование как у тебя
    static int16_t lastDrawX = -1, lastDrawY = -1;
    static uint32_t lastDrawMs = 0;
    if (now - lastDrawMs >= 40) {
        if (x != lastDrawX || y != lastDrawY) {
            lastDrawX = x;
            lastDrawY = y;
            lastDrawMs = now;
            tft.fillCircle(x, y, 3, GC9A01A_GREEN);
            tftBottomXY(x, y);
        }
    }

    // лог координат (троттлим)
    uint16_t dist = (uint16_t)abs(x - lastTx) + (uint16_t)abs(y - lastTy);
    if ((now - lastTouchLogMs) >= TOUCH_LOG_MIN_MS && dist >= TOUCH_LOG_MIN_DIST) {
        lastTouchLogMs = now;
        lastTx = x;
        lastTy = y;

        char msg[LOG_LEN];
        snprintf(msg, sizeof(msg), "EVT:TOUCH:X=%d,Y=%d", x, y);
        logPush(msg);
    }
}

// ===================== Setup / Loop =====================
void setup() {
    delay(150);
    Serial.begin(115200);

    // TFT init
    SPI.begin(TFT_SCL, -1, TFT_SDA, TFT_CS);
    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(GC9A01A_BLACK);
    tftText(40, 100, 2, GC9A01A_WHITE, "Init...");

    // Touch reset
    pinMode(TP_RST, OUTPUT);
    digitalWrite(TP_RST, LOW);
    delay(20);
    digitalWrite(TP_RST, HIGH);
    delay(80);

    // I2C init + scan
    Wire.begin(TP_SDA, TP_SCL);
    i2cScan();

    // Touch begin
    touch.begin();

    // OLED init (попробуем 0x3C/0x3D и то, что нашли сканом)
    oledOk = false;
    uint8_t candidates[] = {0x3C, 0x3D, 0x03, 0x3F};
    for (uint8_t i = 0; i < sizeof(candidates); i++) {
        uint8_t a = candidates[i];
        if (!hasAddr(a)) continue;
        if (oled.begin(SSD1306_SWITCHCAPVCC, a)) {
            oledOk = true;
            oledAddr = a;
            break;
        }
    }

    // MUX init
    pinMode(MUX_S0, OUTPUT);
    pinMode(MUX_S1, OUTPUT);
    pinMode(MUX_S2, OUTPUT);
    pinMode(MUX_S3, OUTPUT);
    pinMode(MUX_EN, OUTPUT);
    digitalWrite(MUX_EN, LOW); // enable
    pinMode(MUX_SIG, INPUT_PULLUP);

    uint32_t now = millis();
    for (uint8_t ch = 0; ch < BTN_COUNT; ch++) {
        rawState[ch] = readButtonPressedByIndex(ch);
        stableState[ch] = rawState[ch];
        lastChangeMs[ch] = now;
    }

    // Encoders init
    pinMode(ENC1_A, INPUT_PULLUP);
    pinMode(ENC1_B, INPUT_PULLUP);
    pinMode(ENC2_A, INPUT_PULLUP);
    pinMode(ENC2_B, INPUT_PULLUP);

    enc1.begin();
    enc1.setAcceleration(0);

    enc2.begin();
    enc2.setAcceleration(0);

    attachInterrupt(digitalPinToInterrupt(ENC1_A), enc1ISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC1_B), enc1ISR, CHANGE);

    attachInterrupt(digitalPinToInterrupt(ENC2_A), enc2ISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC2_B), enc2ISR, CHANGE);

    // BLE
    bleInit();

    // UI ready
    tft.fillScreen(GC9A01A_BLACK);
    logPush("EVT:BOOT");

    if (oledOk) {
        char msg[LOG_LEN];
        snprintf(msg, sizeof(msg), "EVT:OLED:0x%02X", oledAddr);
        logPush(msg);
    } else {
        logPush("EVT:OLED:NOTFOUND");
    }

    logPush("EVT:READY");
}


void loop() {
    scanButtons();            // остальные кнопки через MUX
    handleEncoderKeysFromMux(); // KEY1/KEY2 (клик/лонг)
    handleEncoders();         // вращение ENC1/ENC2
    handleTouch();
    oledRender();

    delay(2);
}