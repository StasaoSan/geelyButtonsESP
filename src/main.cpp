#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>

#include <AiEsp32RotaryEncoder.h>

#include "AppConfig.h"
#include "AppState.h"
#include "EventLogger.h"
#include "BleManager.h"
#include "DisplayManager.h"

// ===================== Globals =====================
static AppState     state;
static EventLogger  logger;
static BleManager   ble;
static DisplayManager display;

static void bleSendWrapper(const char* msg) { ble.send(msg); }

// ===================== Encoders =====================
AiEsp32RotaryEncoder enc1(Pins::ENC1_A, Pins::ENC1_B, -1, -1, 4);
AiEsp32RotaryEncoder enc2(Pins::ENC2_A, Pins::ENC2_B, -1, -1, 4);

static uint32_t encKeyDownMs[2] = {};
static bool     encKeyWasDown[2] = {};
static long     enc1Last = 0;
static long     enc2Last = 0;

void IRAM_ATTR enc1ISR() { enc1.readEncoder_ISR(); }
void IRAM_ATTR enc2ISR() { enc2.readEncoder_ISR(); }

// ===================== Button state =====================
static bool     rawState[BtnCfg::BTN_COUNT]    = {};
static bool     stableState[BtnCfg::BTN_COUNT] = {};
static uint32_t lastChangeMs[BtnCfg::BTN_COUNT] = {};
static uint32_t btnDownMs[BtnCfg::BTN_COUNT]   = {};
static bool     btnWasDown[BtnCfg::BTN_COUNT]  = {};

// ===================== MUX helpers =====================
static inline void muxSelect(uint8_t ch) {
    digitalWrite(Pins::MUX_S0, (ch >> 0) & 1);
    digitalWrite(Pins::MUX_S1, (ch >> 1) & 1);
    digitalWrite(Pins::MUX_S2, (ch >> 2) & 1);
    digitalWrite(Pins::MUX_S3, (ch >> 3) & 1);
}

static inline bool readButtonPressedByIndex(uint8_t idx) {
    muxSelect(BtnCfg::BTN_FIRST_CH + idx);
    delayMicroseconds(8);
    return digitalRead(Pins::MUX_SIG) == LOW;
}

// ===================== Button handling =====================
static void handleButtonEvent(uint8_t idx, bool isLong) {
    if (isLong && idx == VolumeCfg::MODE_BTN_IDX) {
        state.enc2VolumeMode = !state.enc2VolumeMode;
        state.prefs.putBool("volMode", state.enc2VolumeMode);
        state.displayDirty = true;
    }
    char ev[LogCfg::LEN];
    Evt::btnEvent(ev, sizeof(ev), idx, isLong);
    logger.push(ev);
}

static void scanButtons() {
    uint32_t now = millis();

    for (uint8_t idx = 0; idx < BtnCfg::BTN_COUNT; idx++) {
        if (idx == BtnCfg::ENC1_KEY_IDX || idx == BtnCfg::ENC2_KEY_IDX) continue;

        bool r = readButtonPressedByIndex(idx);
        if (r != rawState[idx]) {
            rawState[idx]      = r;
            lastChangeMs[idx]  = now;
        }

        if ((now - lastChangeMs[idx]) >= BtnCfg::DEBOUNCE_MS) {
            if (stableState[idx] != rawState[idx]) {
                bool prev        = stableState[idx];
                stableState[idx] = rawState[idx];

                if (!prev && stableState[idx]) {
                    btnWasDown[idx] = true;
                    btnDownMs[idx]  = now;
                }
                if (prev && !stableState[idx] && btnWasDown[idx]) {
                    btnWasDown[idx]  = false;
                    bool isLong = ((now - btnDownMs[idx]) >= BtnCfg::LONG_MS);
                    handleButtonEvent(idx, isLong);
                }
            }
        }
    }
}

// ===================== Encoder handling =====================
static void handleEncoderKeysFromMux() {
    bool k1 = readButtonPressedByIndex(BtnCfg::ENC1_KEY_IDX);
    bool k2 = readButtonPressedByIndex(BtnCfg::ENC2_KEY_IDX);
    bool keys[2] = {k1, k2};

    for (int i = 0; i < 2; i++) {
        if (keys[i] && !encKeyWasDown[i]) {
            encKeyWasDown[i] = true;
            encKeyDownMs[i]  = millis();
        } else if (!keys[i] && encKeyWasDown[i]) {
            encKeyWasDown[i] = false;
            bool isLong = ((millis() - encKeyDownMs[i]) >= EncCfg::KEY_LONG_MS);
            char ev[LogCfg::LEN];
            Evt::encKey(ev, sizeof(ev), (i == 0) ? 1 : 2, isLong);
            logger.push(ev);
        }
    }
}

static void handleEncoders() {
    long p1 = enc1.readEncoder();
    long d1 = p1 - enc1Last;
    if (d1 != 0) {
        enc1Last = p1;
        char ev[LogCfg::LEN];
        Evt::encStep(ev, sizeof(ev), 1, d1);
        logger.push(ev);
    }

    long p2 = enc2.readEncoder();
    long d2 = p2 - enc2Last;
    if (d2 != 0) {
        enc2Last = p2;
        if (state.enc2VolumeMode) {
            state.volumeLevel = constrain(state.volumeLevel + (d2 > 0 ? -1 : 1),
                                          VolumeCfg::MIN, VolumeCfg::MAX);
            logger.push(d2 > 0 ? VolumeCfg::STEP_M : VolumeCfg::STEP_P);
        } else {
            char ev[LogCfg::LEN];
            Evt::encStep(ev, sizeof(ev), 2, d2);
            logger.push(ev);
        }
    }
}

// ===================== Setup / Loop =====================
void setup() {
    delay(150);
    Serial.begin(115200);

    Wire.begin(Pins::I2C_SDA, Pins::I2C_SCL);

    logger.begin(&state, bleSendWrapper);
    display.begin(&state, &logger);
    ble.begin(&state, &logger);

    state.begin();  // NVS после BLE — как в оригинале

    // MUX
    pinMode(Pins::MUX_S0,  OUTPUT);
    pinMode(Pins::MUX_S1,  OUTPUT);
    pinMode(Pins::MUX_S2,  OUTPUT);
    pinMode(Pins::MUX_S3,  OUTPUT);
    pinMode(Pins::MUX_EN,  OUTPUT);
    digitalWrite(Pins::MUX_EN, LOW);
    pinMode(Pins::MUX_SIG, INPUT_PULLUP);

    uint32_t now = millis();
    for (uint8_t i = 0; i < BtnCfg::BTN_COUNT; i++) {
        rawState[i]    = readButtonPressedByIndex(i);
        stableState[i] = rawState[i];
        btnWasDown[i]  = stableState[i];
        btnDownMs[i]   = btnWasDown[i] ? now : 0;
        lastChangeMs[i] = now;
    }

    // Encoders
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

    logger.push(Evt::READY);
}

void loop() {
    scanButtons();
    handleEncoderKeysFromMux();
    handleEncoders();
    ble.update();
    display.update();
    delay(2);
}
