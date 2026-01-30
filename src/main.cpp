#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <cstring>
#include <string>

#include <AiEsp32RotaryEncoder.h>
#include <Adafruit_GC9A01A.h>
#include <Adafruit_SSD1306.h>

#include <CST816S.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>

#include "AppConfig.h"
#include "CircleText.h"

// ===================== BLE =====================
BLECharacteristic* g_char = nullptr;
volatile bool g_deviceConnected = false;

// Android -> ESP RX (handled in loop to avoid heavy work inside BLE callbacks)
static volatile bool g_rxPending = false;
static char g_rxMsg[LogCfg::LEN] = {0};

class RxCallbacks : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic* ch) override {
        std::string v = ch->getValue();
        if (v.empty()) return;

        size_t n = v.size();
        if (n >= LogCfg::LEN) n = LogCfg::LEN - 1;
        std::memcpy((void*)g_rxMsg, v.data(), n);
        g_rxMsg[n] = '\0';
        g_rxPending = true;
    }
};

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        (void)pServer;
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
    BLEDevice::init(Cfg::BLE_NAME);

    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(new MyServerCallbacks());

    BLEService* service = server->createService(Cfg::SERVICE_UUID);
    g_char = service->createCharacteristic(
            Cfg::CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_NOTIFY |
            BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_WRITE_NR
    );
    g_char->setCallbacks(new RxCallbacks());
    g_char->addDescriptor(new BLE2902());

    service->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(Cfg::SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMinPreferred(0x12);

    BLEDevice::startAdvertising();
}

// ===================== Hardware instances =====================
Adafruit_GC9A01A tft(Pins::TFT_CS, Pins::TFT_DC, Pins::TFT_RST);

// CST816S ctor: (sda, scl, rst, int)
CST816S touch(Pins::I2C_SDA, Pins::I2C_SCL, Pins::TP_RST, Pins::TP_INT);

static bool oledOk = false;
static uint8_t oledAddr = 0x3C;
Adafruit_SSD1306 oled(OledCfg::W, OledCfg::H, &Wire, -1);

// ===================== 4067 MUX helpers =====================
static inline void muxSelect(uint8_t ch) {
    digitalWrite(Pins::MUX_S0, (ch >> 0) & 1);
    digitalWrite(Pins::MUX_S1, (ch >> 1) & 1);
    digitalWrite(Pins::MUX_S2, (ch >> 2) & 1);
    digitalWrite(Pins::MUX_S3, (ch >> 3) & 1);
}

static inline bool readButtonPressedByIndex(uint8_t idx) {
    uint8_t ch = BtnCfg::BTN_FIRST_CH + idx;
    muxSelect(ch);
    delayMicroseconds(8);
    return digitalRead(Pins::MUX_SIG) == LOW;
}

// ===================== Encoders =====================
AiEsp32RotaryEncoder enc1(Pins::ENC1_A, Pins::ENC1_B, -1, -1, 4);
AiEsp32RotaryEncoder enc2(Pins::ENC2_A, Pins::ENC2_B, -1, -1, 4);

static uint32_t encKeyDownMs[2] = {0, 0};
static bool encKeyWasDown[2] = {false, false};

void IRAM_ATTR enc1ISR() { enc1.readEncoder_ISR(); }
void IRAM_ATTR enc2ISR() { enc2.readEncoder_ISR(); }

// ===================== Simple UI helpers =====================
static void tftText(int16_t x, int16_t y, uint8_t size, uint16_t color, const char* s) {
    tft.setTextSize(size);
    tft.setTextColor(color);
    tft.setCursor(x, y);
    tft.print(s);
}

static void tftStatusCircle(const char* s) {
    // чистим верхнюю область (прямоугольник) — быстро и достаточно
    auto cfg = TftTextCfg::Status();
    tft.fillRect(0, cfg.topY, 240, (cfg.bottomY - cfg.topY + 1), GC9A01A_BLACK);

    // Важно: чтобы GFX сам не переносил, переносим только через CircleText
    tft.setTextWrap(false);

    CircleText::drawWithConfig(tft, cfg, s, CircleTextPos::Top);
}

static void tftBottomCircleXY(int16_t x, int16_t y) {
    auto cfg = TftTextCfg::Bottom();
    tft.fillRect(0, cfg.topY, 240, (cfg.bottomY - cfg.topY + 1), GC9A01A_BLACK);

    char buf[64];
    snprintf(buf, sizeof(buf), "X:%d  Y:%d", x, y);

    tft.setTextWrap(false);
    CircleText::drawWithConfig(tft, cfg, buf, CircleTextPos::Bottom);
}

// ===================== Logger =====================
static char logBuf[LogCfg::LINES][LogCfg::LEN];
static uint8_t logHead = 0;
static bool logDirty = true;

static void logPush(const char* msg) {
    strncpy(logBuf[logHead], msg, LogCfg::LEN - 1);
    logBuf[logHead][LogCfg::LEN - 1] = '\0';
    logHead = (logHead + 1) % LogCfg::LINES;
    logDirty = true;

    Serial.println(msg);
    tftStatusCircle(msg);
    bleSend(msg);
}

static void oledRender() {
    if (!oledOk || !logDirty) return;

    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    oled.setCursor(0, 0);
    oled.print("BLE:");
    oled.print(g_deviceConnected ? "ON " : "OFF");
    oled.print(" I2C:OK");

    for (uint8_t i = 0; i < LogCfg::LINES; i++) {
        uint8_t idx = (logHead + i) % LogCfg::LINES;
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
static bool rawState[BtnCfg::BTN_COUNT];
static bool stableState[BtnCfg::BTN_COUNT];
static uint32_t lastChangeMs[BtnCfg::BTN_COUNT];
static uint32_t btnDownMs[BtnCfg::BTN_COUNT];
static bool btnWasDown[BtnCfg::BTN_COUNT];

static void scanButtons() {
    uint32_t now = millis();

    for (uint8_t idx = 0; idx < BtnCfg::BTN_COUNT; idx++) {
        // энкодерные кнопки ты обрабатываешь отдельно
        if (idx == BtnCfg::ENC1_KEY_IDX || idx == BtnCfg::ENC2_KEY_IDX) {
            continue;
        }

        bool r = readButtonPressedByIndex(idx);

        if (r != rawState[idx]) {
            rawState[idx] = r;
            lastChangeMs[idx] = now;
        }

        if ((now - lastChangeMs[idx]) >= BtnCfg::DEBOUNCE_MS) {
            if (stableState[idx] != rawState[idx]) {
                bool prev = stableState[idx];
                stableState[idx] = rawState[idx];

                // DOWN
                if (!prev && stableState[idx]) {
                    btnWasDown[idx] = true;
                    btnDownMs[idx] = now;
                }

                // UP
                if (prev && !stableState[idx]) {
                    if (btnWasDown[idx]) {
                        btnWasDown[idx] = false;
                        uint32_t dur = now - btnDownMs[idx];
                        bool isLong = (dur >= BtnCfg::LONG_MS);

                        logPush(isLong ? Evt::btnLongByIdx(idx)
                                       : Evt::btnClickByIdx(idx));
                    }
                }
            }
        }
    }
}

// ===================== Encoders handling =====================
static long enc1Last = 0;
static long enc2Last = 0;

static void handleEncoders() {
    long p1 = enc1.readEncoder();
    long d1 = p1 - enc1Last;
    if (d1 != 0) {
        enc1Last = p1;
        logPush(Evt::encStep(1, d1));
    }

    long p2 = enc2.readEncoder();
    long d2 = p2 - enc2Last;
    if (d2 != 0) {
        enc2Last = p2;
        logPush(Evt::encStep(2, d2));
    }
}

static void handleEncoderKeysFromMux() {
    bool k1 = readButtonPressedByIndex(BtnCfg::ENC1_KEY_IDX);
    bool k2 = readButtonPressedByIndex(BtnCfg::ENC2_KEY_IDX);
    bool keys[2] = {k1, k2};

    for (int i = 0; i < 2; i++) {
        if (keys[i] && !encKeyWasDown[i]) {
            encKeyWasDown[i] = true;
            encKeyDownMs[i] = millis();
        } else if (!keys[i] && encKeyWasDown[i]) {
            encKeyWasDown[i] = false;
            uint32_t dur = millis() - encKeyDownMs[i];
            bool isLong = (dur >= EncCfg::KEY_LONG_MS);
            logPush(Evt::encKey((i == 0) ? 1 : 2, isLong));
        }
    }
}

// ===================== Touch =====================
static bool touchDown = false;
static uint32_t lastTouchEventMs = 0;
static uint32_t lastTouchLogMs = 0;
static int16_t lastTx = -1, lastTy = -1;

static void handleTouch() {
    uint32_t now = millis();

    if (touchDown && (now - lastTouchEventMs) > TouchCfg::UP_TIMEOUT_MS) {
        touchDown = false;
        logPush(Evt::TOUCH_UP);
        return;
    }

    if (!touch.available()) return;

    int16_t x = (int16_t)touch.data.x;
    int16_t y = (int16_t)touch.data.y;
    if (x == 0 && y == 0) return;

    x = constrain(x, 0, 239);
    y = constrain(y, 0, 239);

    lastTouchEventMs = now;

    if (!touchDown) {
        touchDown = true;
        lastTx = x;
        lastTy = y;
        lastTouchLogMs = 0;
        logPush(Evt::TOUCH_DOWN);
    }

    static int16_t lastDrawX = -1, lastDrawY = -1;
    static uint32_t lastDrawMs = 0;
    if (now - lastDrawMs >= 40) {
        if (x != lastDrawX || y != lastDrawY) {
            lastDrawX = x;
            lastDrawY = y;
            lastDrawMs = now;
            tft.fillCircle(x, y, 3, GC9A01A_GREEN);
            tftBottomCircleXY(x, y);
        }
    }

    uint16_t dist = (uint16_t)abs(x - lastTx) + (uint16_t)abs(y - lastTy);
    if ((now - lastTouchLogMs) >= TouchCfg::LOG_MIN_MS && dist >= TouchCfg::LOG_MIN_DIST) {
        lastTouchLogMs = now;
        lastTx = x;
        lastTy = y;

        char msg[LogCfg::LEN];
        Evt::touchXY(msg, sizeof(msg), x, y);
        logPush(msg);
    }
}

// ===================== Setup / Loop =====================
void setup() {
    delay(150);
    Serial.begin(115200);

    // TFT init
    SPI.begin(Pins::TFT_SCL, -1, Pins::TFT_SDA, Pins::TFT_CS);
    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(GC9A01A_BLACK);
    tftText(40, 100, 2, GC9A01A_WHITE, "Init...");

    // Touch reset
    pinMode(Pins::TP_RST, OUTPUT);
    digitalWrite(Pins::TP_RST, LOW);
    delay(20);
    digitalWrite(Pins::TP_RST, HIGH);
    delay(80);

    // I2C init + scan
    Wire.begin(Pins::I2C_SDA, Pins::I2C_SCL);
    i2cScan();

    // Touch begin
    touch.begin();

    // OLED init: сначала кандидаты, но только если адрес реально найден сканом
    oledOk = false;
    for (size_t i = 0; i < (sizeof(OledCfg::AddrCandidates) / sizeof(OledCfg::AddrCandidates[0])); i++) {
        uint8_t a = OledCfg::AddrCandidates[i];
        if (!hasAddr(a)) continue;
        if (oled.begin(SSD1306_SWITCHCAPVCC, a)) {
            oledOk = true;
            oledAddr = a;
            break;
        }
    }

    // MUX init
    pinMode(Pins::MUX_S0, OUTPUT);
    pinMode(Pins::MUX_S1, OUTPUT);
    pinMode(Pins::MUX_S2, OUTPUT);
    pinMode(Pins::MUX_S3, OUTPUT);
    pinMode(Pins::MUX_EN, OUTPUT);
    digitalWrite(Pins::MUX_EN, LOW);
    pinMode(Pins::MUX_SIG, INPUT_PULLUP);

    uint32_t now = millis();
    for (uint8_t i = 0; i < BtnCfg::BTN_COUNT; i++) {
        rawState[i] = readButtonPressedByIndex(i);
        stableState[i] = rawState[i];
        btnWasDown[i] = stableState[i];
        btnDownMs[i] = btnWasDown[i] ? now : 0;
        lastChangeMs[i] = now;
    }

    // Encoders init
    pinMode(Pins::ENC1_A, INPUT_PULLUP);
    pinMode(Pins::ENC1_B, INPUT_PULLUP);
    pinMode(Pins::ENC2_A, INPUT_PULLUP);
    pinMode(Pins::ENC2_B, INPUT_PULLUP);

    enc1.begin();
    enc1.setAcceleration(0);

    enc2.begin();
    enc2.setAcceleration(0);

    attachInterrupt(digitalPinToInterrupt(Pins::ENC1_A), enc1ISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(Pins::ENC1_B), enc1ISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(Pins::ENC2_A), enc2ISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(Pins::ENC2_B), enc2ISR, CHANGE);

    // BLE
    bleInit();

    // Ready
    tft.fillScreen(GC9A01A_BLACK);
    logPush(Evt::BOOT);

    if (oledOk) {
        char msg[LogCfg::LEN];
        Evt::oledAddr(msg, sizeof(msg), oledAddr);
        logPush(msg);
    } else {
        logPush(Evt::OLED_NOTFOUND);
    }

    logPush(Evt::READY);
}

void loop() {
    scanButtons();
    handleEncoderKeysFromMux();
    handleEncoders();
    handleTouch();
    oledRender();

    // Process Android -> ESP incoming messages (writable characteristic)
    if (g_rxPending) {
        g_rxPending = false;
        char buf[LogCfg::LEN];
        std::snprintf(buf, sizeof(buf), "RX:%s", g_rxMsg);
        logPush(buf);
    }

    delay(2);
}