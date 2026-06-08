#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Preferences.h>
#include "driver/gpio.h"  // gpio_reset_pin()
#include <cstring>
#include <string>
#include <cmath>   // isnan, NAN

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
volatile bool g_deviceConnected  = false;
volatile bool g_needSync         = false;  // запрос текущего состояния от приложения
volatile bool g_bleDisconnected  = false;  // сброс состояния после разрыва

static void tftStatusCircle(const char* s);
static bool logDirty = true;

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
        g_needSync        = true;   // попросить приложение прислать текущее состояние
        logDirty = true;
        tftStatusCircle("BLE:ON");
    }
    void onDisconnect(BLEServer* pServer) override {
        g_deviceConnected = false;
        g_bleDisconnected = true;   // сброс состояния обработается в loop()
        logDirty = true;
        tftStatusCircle("BLE:OFF");
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

// ===================== Wire1 pin multiplexer =====================
// ESP32 GPIO matrix не сбрасывает маппинг старых пинов при end()+begin().
// gpio_reset_pin() явно отключает пин от периферии до переключения.
static int g_wire1Sda = -1;
static int g_wire1Scl = -1;

static void wire1SwitchTo(int sda, int scl) {
    if (g_wire1Sda == sda && g_wire1Scl == scl) return;
    if (g_wire1Sda >= 0) gpio_reset_pin((gpio_num_t)g_wire1Sda);
    if (g_wire1Scl >= 0) gpio_reset_pin((gpio_num_t)g_wire1Scl);
    Wire1.end();
    Wire1.begin(sda, scl);
    g_wire1Sda = sda;
    g_wire1Scl = scl;
}

// ===================== Hardware instances =====================
Adafruit_GC9A01A tft(Pins::TFT_CS, Pins::TFT_DC, Pins::TFT_RST);

// CST816S ctor: (sda, scl, rst, int)
CST816S touch(Pins::I2C_SDA, Pins::I2C_SCL, Pins::TP_RST, Pins::TP_INT);

static Adafruit_SSD1306 oled0(OledCfg::WIDTHS[0], OledCfg::HEIGHTS[0], &Wire,  -1);
static Adafruit_SSD1306 oled1(OledCfg::WIDTHS[1], OledCfg::HEIGHTS[1], &Wire1, -1);
static Adafruit_SSD1306 oled2(OledCfg::WIDTHS[2], OledCfg::HEIGHTS[2], &Wire1, -1);
static Adafruit_SSD1306* const oleds[OledCfg::COUNT] = {&oled0, &oled1, &oled2};
static bool oledOk[OledCfg::COUNT] = {};

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
    auto cfg = TftTextCfg::Status();
    tft.fillRect(0, cfg.topY, 240, (cfg.bottomY - cfg.topY + 1), GC9A01A_BLACK);
    tft.setTextWrap(false);
    CircleText::drawWithConfig(tft, cfg, s, CircleTextPos::Top);
}

// ===================== Logger =====================
static char logBuf[LogCfg::LINES][LogCfg::LEN];
static uint8_t logHead = 0;

static int g_rearDefrost = -1;     // -1 unknown, 0 off, 1 on
static int g_electricDefrost = -1; // -1 unknown, 0 off, 1 on

static int  g_fanArea = -1;
static char g_fanLevel[12] = "?";  // OFF/AUTO/L1..L9/UNK

static float g_tempMain = NAN;     // area=1
static float g_tempPass = NAN;     // area=4

static Preferences g_prefs;
static bool g_enc2VolumeMode = false;
static int  g_volumeLevel    = 50;   // 0..100

static void logPush(const char* msg) {
    strncpy(logBuf[logHead], msg, LogCfg::LEN - 1);
    logBuf[logHead][LogCfg::LEN - 1] = '\0';
    logHead = (logHead + 1) % LogCfg::LINES;
    logDirty = true;

    Serial.println(msg);
    tftStatusCircle(msg);
    bleSend(msg);
}

static void processRx(const char* s) {
    // FB:REAR:1
    if (strncmp(s, "FB:REAR:", 8) == 0) {
        int v = atoi(s + 8);
        g_rearDefrost = v;
        char b[LogCfg::LEN];
        snprintf(b, sizeof(b), "REAR_DEF:%s", (v == 1 ? "ON" : "OFF"));
        logPush(b);
        return;
    }

    // FB:ELECTRIC:0
    if (strncmp(s, "FB:ELECTRIC:", 12) == 0) {
        int v = atoi(s + 12);
        g_electricDefrost = v;
        char b[LogCfg::LEN];
        snprintf(b, sizeof(b), "E_DEF:%s", (v == 1 ? "ON" : "OFF"));
        logPush(b);
        return;
    }

    // FB:FAN:<area>:<level>
    // example: FB:FAN:8:L3
    if (strncmp(s, "FB:FAN:", 7) == 0) {
        const char* p = s + 7;
        g_fanArea = atoi(p);

        const char* c1 = strchr(p, ':');
        if (c1 && *(c1 + 1)) {
            strncpy(g_fanLevel, c1 + 1, sizeof(g_fanLevel) - 1);
            g_fanLevel[sizeof(g_fanLevel) - 1] = '\0';
        } else {
            strncpy(g_fanLevel, "?", sizeof(g_fanLevel));
            g_fanLevel[sizeof(g_fanLevel) - 1] = '\0';
        }

        char b[LogCfg::LEN];
        snprintf(b, sizeof(b), "FAN:%d:%s", g_fanArea, g_fanLevel);
        logPush(b);
        return;
    }

    // FB:TEMP:MAIN:22.0  |  FB:TEMP:PASS:22.0
    if (strncmp(s, "FB:TEMP:", 8) == 0) {
        const char* p = s + 8;
        if (strncmp(p, "MAIN:", 5) == 0) {
            g_tempMain = (float)atof(p + 5);
            char b[LogCfg::LEN];
            snprintf(b, sizeof(b), "T1:%.1f", g_tempMain);
            logPush(b);
            return;
        }
        if (strncmp(p, "PASS:", 5) == 0) {
            g_tempPass = (float)atof(p + 5);
            char b[LogCfg::LEN];
            snprintf(b, sizeof(b), "T4:%.1f", g_tempPass);
            logPush(b);
            return;
        }
    }

    // GIB:FLOAT:<id>:<area>:<value>
    // example: GIB:FLOAT:268828928:1:22.5
    if (strncmp(s, "GIB:FLOAT:", 10) == 0) {
        const char* p = s + 10;

        int id = atoi(p);
        const char* c1 = strchr(p, ':');
        if (!c1) goto fallback;
        int area = atoi(c1 + 1);

        const char* c2 = strchr(c1 + 1, ':');
        if (!c2) goto fallback;
        float v = (float)atof(c2 + 1);

        if (id == 268828928) { // IHvac.HVAC_FUNC_TEMP
            if (area == 1) g_tempMain = v;
            else if (area == 4) g_tempPass = v;

            char b[LogCfg::LEN];
            snprintf(b, sizeof(b), "TEMP:%d:%.1f", area, v);
            logPush(b);
            return;
        }

        char b[LogCfg::LEN];
        snprintf(b, sizeof(b), "F:%d:%d:%.2f", id, area, v);
        logPush(b);
        return;
    }

    fallback:
    char buf[LogCfg::LEN];
    snprintf(buf, sizeof(buf), "RX:%s", s);
    logPush(buf);
}

// ===================== 64x32 screen renderers =====================

// Температура: целая часть — size=3, дробная — size=1 (сверху справа)
static void oledRenderTempScreen(Adafruit_SSD1306& d, float temp) {
    d.clearDisplay();
    d.setTextWrap(false);
    d.setTextColor(SSD1306_WHITE);

    if (isnan(temp)) {
        d.setTextSize(3);
        d.setCursor((64 - 18) / 2, 4);
        d.print("?");
    } else {
        bool neg    = (temp < 0.0f);
        float abst  = fabsf(temp);
        int   ipart = (int)abst;
        int   dpart = (int)roundf((abst - ipart) * 10.0f);
        if (dpart >= 10) { ipart++; dpart = 0; }

        char ibuf[8];
        snprintf(ibuf, sizeof(ibuf), neg ? "-%d" : "%d", ipart);
        bool hasDec = (dpart != 0);

        // size=3 → 18px/char wide, 24px tall
        // size=1 → 6px/char wide,  8px tall
        int iw = (int)strlen(ibuf) * 18;
        int dw = hasDec ? 12 : 0;  // ".X" = 2 chars * 6px
        int sx = (64 - iw - dw) / 2;
        if (sx < 0) sx = 0;

        d.setTextSize(3);
        d.setCursor(sx, 4);
        d.print(ibuf);

        if (hasDec) {
            char dbuf[4];
            snprintf(dbuf, sizeof(dbuf), ".%d", dpart);
            d.setTextSize(1);
            d.setCursor(sx + iw, 4);
            d.print(dbuf);
        }
    }

    d.display();
}

// Две температуры на одном 64x32 (если второй экран недоступен: оба на 0x3C)
// Верхняя половина — водитель, нижняя — пассажир, size=2
static void oledRenderDualTempScreen(Adafruit_SSD1306& d, float tempD, float tempP) {
    d.clearDisplay();
    d.setTextWrap(false);
    d.setTextColor(SSD1306_WHITE);

    auto drawHalf = [&](float t, int y) {
        char buf[10];
        if (isnan(t)) strcpy(buf, "?");
        else snprintf(buf, sizeof(buf), "%.1f", t);
        int w = (int)strlen(buf) * 12;  // size=2: 6*2=12px/char
        d.setTextSize(2);
        d.setCursor((64 - w) / 2, y);
        d.print(buf);
    };

    drawHalf(tempD, 0);   // y=0..15: водитель
    drawHalf(tempP, 16);  // y=16..31: пассажир

    d.display();
}

// Громкость: "VOL" мелко сверху, число крупно снизу
static void oledRenderVolScreen(Adafruit_SSD1306& d) {
    d.clearDisplay();
    d.setTextWrap(false);
    d.setTextColor(SSD1306_WHITE);

    d.setTextSize(1);
    d.setCursor(0, 0);
    d.print("VOL");

    char buf[5];
    snprintf(buf, sizeof(buf), "%d", g_volumeLevel);
    int w = (int)strlen(buf) * 18;
    d.setTextSize(3);
    d.setCursor((64 - w) / 2, 8);
    d.print(buf);

    d.display();
}

static void oledRenderOne(Adafruit_SSD1306& d) {
    d.clearDisplay();
    d.setTextSize(1);
    d.setTextColor(SSD1306_WHITE);
    d.setTextWrap(false);

    // line 0: BLE + defrost
    d.setCursor(0, 0);
    d.print(g_deviceConnected ? "BLE:ON" : "BLE:OFF");
    d.print(" R:");
    d.print(g_rearDefrost < 0 ? "?" : (g_rearDefrost ? "1" : "0"));
    d.print(" E:");
    d.print(g_electricDefrost < 0 ? "?" : (g_electricDefrost ? "1" : "0"));

    // line 1: fan + temps
    d.setCursor(0, 8);
    d.print("F:");
    if (g_fanArea < 0) d.print("?:");
    else { d.print(g_fanArea); d.print(":"); }
    d.print(g_fanLevel);
    d.print(" T1:");
    if (isnan(g_tempMain)) d.print("?");
    else d.print(String(g_tempMain, 1));

    // lines 2-3: last 2 log entries
    for (uint8_t i = 0; i < 2; i++) {
        uint8_t idx = (logHead + LogCfg::LINES - 2 + i) % LogCfg::LINES;
        d.setCursor(0, 16 + i * 8);
        d.print(logBuf[idx]);
    }

    d.display();
}

static void oledRender() {
    if (!logDirty) return;
    logDirty = false;

    if (oledOk[0]) {
        if (!oledOk[1]) {
            // Второй экран недоступен — обе температуры на одном
            if (g_enc2VolumeMode) oledRenderVolScreen(oled0);
            else                  oledRenderDualTempScreen(oled0, g_tempMain, g_tempPass);
        } else {
            oledRenderTempScreen(oled0, g_tempMain);
        }
    }
    if (oledOk[1]) {
        wire1SwitchTo(Pins::I2C1_SDA, Pins::I2C1_SCL);
        if (g_enc2VolumeMode) oledRenderVolScreen(oled1);
        else                  oledRenderTempScreen(oled1, g_tempPass);
    }
    if (oledOk[2]) {
        wire1SwitchTo(Pins::I2C2_SDA, Pins::I2C2_SCL);
        oledRenderOne(oled2);
    }
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

static uint8_t foundAddrs1[16];
static uint8_t foundCnt1 = 0;

static void i2cScan1() {
    foundCnt1 = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire1.beginTransmission(addr);
        if (Wire1.endTransmission() == 0) {
            if (foundCnt1 < sizeof(foundAddrs1)) foundAddrs1[foundCnt1++] = addr;
        }
        delay(2);
    }
}

static bool hasAddr1(uint8_t a) {
    for (uint8_t i = 0; i < foundCnt1; i++) if (foundAddrs1[i] == a) return true;
    return false;
}

// ===================== Buttons debounce =====================
static bool rawState[BtnCfg::BTN_COUNT];
static bool stableState[BtnCfg::BTN_COUNT];
static uint32_t lastChangeMs[BtnCfg::BTN_COUNT];
static uint32_t btnDownMs[BtnCfg::BTN_COUNT];
static bool btnWasDown[BtnCfg::BTN_COUNT];

static void handleButtonEvent(uint8_t idx, bool isLong) {
    // Volume mode toggle (ESP-internal: changes enc2 behavior and OLED display)
    if (isLong && idx == VolumeCfg::MODE_BTN_IDX) {
        g_enc2VolumeMode = !g_enc2VolumeMode;
        g_prefs.putBool("volMode", g_enc2VolumeMode);
        logDirty = true;
    }

    char ev[LogCfg::LEN];
    Evt::btnEvent(ev, sizeof(ev), idx, isLong);
    logPush(ev);
}

static void scanButtons() {
    uint32_t now = millis();

    for (uint8_t idx = 0; idx < BtnCfg::BTN_COUNT; idx++) {
        // энкодерные кнопки отдельно
        if (idx == BtnCfg::ENC1_KEY_IDX || idx == BtnCfg::ENC2_KEY_IDX) continue;

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

                        handleButtonEvent(idx, isLong);
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
        char ev[LogCfg::LEN];
        Evt::encStep(ev, sizeof(ev), 1, d1);
        logPush(ev);
    }

    long p2 = enc2.readEncoder();
    long d2 = p2 - enc2Last;
    if (d2 != 0) {
        enc2Last = p2;
        if (g_enc2VolumeMode) {
            g_volumeLevel = constrain(g_volumeLevel + (d2 > 0 ? -1 : 1),
                                      VolumeCfg::MIN, VolumeCfg::MAX);
            logPush(d2 > 0 ? VolumeCfg::STEP_M : VolumeCfg::STEP_P);
        } else {
            char ev[LogCfg::LEN];
            Evt::encStep(ev, sizeof(ev), 2, d2);
            logPush(ev);
        }
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
            char ev[LogCfg::LEN];
            Evt::encKey(ev, sizeof(ev), (i == 0) ? 1 : 2, isLong);
            logPush(ev);
        }
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

    // Touch disabled — CST816S не подключён
    // pinMode(Pins::TP_RST, OUTPUT); ...

    // I2C init + scan
    Wire.begin(Pins::I2C_SDA, Pins::I2C_SCL);
    i2cScan();
    Serial.print("Wire  found: ");
    for (uint8_t i = 0; i < foundCnt; i++) Serial.printf("0x%02X ", foundAddrs[i]);
    Serial.println();

    // OLED 0 — Wire (21/22)
    if (hasAddr(OledCfg::ADDRS[0])) {
        if (oled0.begin(SSD1306_SWITCHCAPVCC, OledCfg::ADDRS[0])) {
            oledOk[0] = true;
            Serial.printf("OLED[0] 0x%02X %dx%d Wire: OK\n", OledCfg::ADDRS[0], OledCfg::WIDTHS[0], OledCfg::HEIGHTS[0]);
        } else { Serial.printf("OLED[0]: begin() failed\n"); }
    } else { Serial.printf("OLED[0] 0x%02X: not found on Wire\n", OledCfg::ADDRS[0]); }

    // OLED 1 — Wire1 на пинах I2C1 (32/25)
    wire1SwitchTo(Pins::I2C1_SDA, Pins::I2C1_SCL);
    i2cScan1();
    Serial.print("Wire1(I2C1 32/25) found: ");
    for (uint8_t i = 0; i < foundCnt1; i++) Serial.printf("0x%02X ", foundAddrs1[i]);
    Serial.println();
    if (hasAddr1(OledCfg::ADDRS[1])) {
        if (oled1.begin(SSD1306_SWITCHCAPVCC, OledCfg::ADDRS[1])) {
            oledOk[1] = true;
            Serial.printf("OLED[1] 0x%02X %dx%d Wire1(I2C1): OK\n", OledCfg::ADDRS[1], OledCfg::WIDTHS[1], OledCfg::HEIGHTS[1]);
        } else { Serial.printf("OLED[1]: begin() failed\n"); }
    } else { Serial.printf("OLED[1] 0x%02X: not found on Wire1(I2C1)\n", OledCfg::ADDRS[1]); }

    // OLED 2 — Wire1 переключён на пины I2C2 (26/27)
    wire1SwitchTo(Pins::I2C2_SDA, Pins::I2C2_SCL);
    i2cScan1();
    Serial.print("Wire1(I2C2 26/27) found: ");
    for (uint8_t i = 0; i < foundCnt1; i++) Serial.printf("0x%02X ", foundAddrs1[i]);
    Serial.println();
    if (hasAddr1(OledCfg::ADDRS[2])) {
        if (oled2.begin(SSD1306_SWITCHCAPVCC, OledCfg::ADDRS[2])) {
            oledOk[2] = true;
            Serial.printf("OLED[2] 0x%02X %dx%d Wire1(I2C2): OK\n", OledCfg::ADDRS[2], OledCfg::WIDTHS[2], OledCfg::HEIGHTS[2]);
        } else { Serial.printf("OLED[2]: begin() failed\n"); }
    } else { Serial.printf("OLED[2] 0x%02X: not found on Wire1(I2C2)\n", OledCfg::ADDRS[2]); }

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

    // NVS — load persisted state
    g_prefs.begin("geely", false);
    g_enc2VolumeMode = g_prefs.getBool("volMode", false);

    // Ready
    tft.fillScreen(GC9A01A_BLACK);
    logPush(Evt::BOOT);

    bool anyOled = false;
    for (uint8_t i = 0; i < OledCfg::COUNT; i++) {
        if (!oledOk[i]) continue;
        anyOled = true;
        char msg[LogCfg::LEN];
        snprintf(msg, sizeof(msg), "OLED[%d]:0x%02X", i, OledCfg::ADDRS[i]);
        logPush(msg);
    }
    if (!anyOled) logPush(Evt::OLED_NOTFOUND);

    logPush(Evt::READY);
}

void loop() {
    scanButtons();
    handleEncoderKeysFromMux();
    handleEncoders();
    oledRender();

    if (g_bleDisconnected) {
        g_bleDisconnected = false;
        g_tempMain = NAN;
        g_tempPass = NAN;
    }

    if (g_needSync && g_deviceConnected) {
        g_needSync = false;
        bleSend("EVT:SYNC");   // приложение должно ответить текущими FB значениями
        logPush("SYNC->");
    }

    if (g_rxPending) {
        g_rxPending = false;
        processRx(g_rxMsg);
    }

    delay(2);
}