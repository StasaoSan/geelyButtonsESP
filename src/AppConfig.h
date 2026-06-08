#pragma once
#include <Arduino.h>

#include "CircleText.h"

// ===================== BLE =====================
namespace Cfg {
    static constexpr const char* BLE_NAME = "geelyController";
    static constexpr const char* SERVICE_UUID        = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
    static constexpr const char* CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
}

// ===================== Pins =====================
namespace Pins {
    // TFT (GC9A01 SPI)
    static constexpr int TFT_SCL = 13;
    static constexpr int TFT_SDA = 14;
    static constexpr int TFT_CS  = 26;
    static constexpr int TFT_DC  = 27;
    static constexpr int TFT_RST = 25;

    // oled: первый 64x32
    static constexpr int I2C_SDA = 21;
    static constexpr int I2C_SCL = 22;

    // oled1: второй 64x32
    static constexpr int I2C1_SDA = 32;
    static constexpr int I2C1_SCL = 25;

    // oled2: 128x32
    static constexpr int I2C2_SDA = 26;
    static constexpr int I2C2_SCL = 27;

    // Touch (CST816S)
    static constexpr int TP_RST = 32;
    static constexpr int TP_INT = 34;

    // 4067 MUX
    static constexpr int MUX_SIG = 33;
    static constexpr int MUX_S0  = 15;
    static constexpr int MUX_S1  = 2;
    static constexpr int MUX_S2  = 18;
    static constexpr int MUX_S3  = 19;
    static constexpr int MUX_EN  = 5;   // active LOW

    // Encoders A/B
    static constexpr int ENC1_A = 23;
    static constexpr int ENC1_B = 4;

    static constexpr int ENC2_A = 17;
    static constexpr int ENC2_B = 16;
}

// ===================== OLED (multi-screen) =====================
namespace OledCfg {
    static constexpr uint8_t COUNT      = 3;
    static constexpr uint8_t WIRE1_FROM = 1;   // oled0 — Wire, oled1+oled2 — Wire1

    // oled0: 64x32 @ 0x3C — Wire (21/22)
    // oled1: 64x32 @ 0x3C — Wire1 с I2C1_SDA/SCL (32/25)
    // oled2: 128x32 @ 0x3C — Wire1 с I2C2_SDA/SCL (26/27)
    static constexpr uint8_t ADDRS[COUNT]   = {0x3C, 0x3C, 0x3C};
    static constexpr uint8_t WIDTHS[COUNT]  = { 64,   64,  128};
    static constexpr uint8_t HEIGHTS[COUNT] = { 32,   32,   32};

    static constexpr uint8_t AddrCandidates[] = {0x3C, 0x3D};
}

// ===================== Buttons / MUX mapping =====================
namespace BtnCfg {
    static constexpr uint8_t BTN_FIRST_CH = 0;
    static constexpr uint8_t BTN_COUNT    = 16;

    // Encoder keys
    static constexpr uint8_t ENC1_KEY_IDX = 14;
    static constexpr uint8_t ENC2_KEY_IDX = 15;

    static constexpr uint16_t DEBOUNCE_MS = 30;
    static constexpr uint16_t LONG_MS     = 450;
}

// ===================== Encoder key timings =====================
namespace EncCfg {
    static constexpr uint16_t KEY_LONG_MS     = 450;
    static constexpr uint16_t KEY_DEBOUNCE_MS = 25;
}

// ===================== Logger buffer sizes =====================
namespace LogCfg {
    static constexpr uint8_t  LINES = 6;
    static constexpr uint8_t  LEN   = 32;
}

// ===================== Volume mode (enc2) =====================
// Long press MODE_BTN_IDX toggles enc2 between temp control and volume control.
// While in volume mode, OLED shows volume level.
namespace VolumeCfg {
    static constexpr uint8_t MODE_BTN_IDX = 12;
    static constexpr int     MIN          = 0;
    static constexpr int     MAX          = 100;
    static constexpr const char* STEP_M   = "EVT:VOLUME:-1";
    static constexpr const char* STEP_P   = "EVT:VOLUME:+1";
}

// ===================== Event strings + форматтеры =====================
// All physical events use a uniform generic format.
// Android side maps event strings to actions (configurable in future).
namespace Evt {
    static constexpr const char* BOOT          = "EVT:BOOT";
    static constexpr const char* READY         = "EVT:READY";
    static constexpr const char* OLED_NOTFOUND = "EVT:OLED:NOTFOUND";

    // EVT:BTN:<idx>:CLICK  or  EVT:BTN:<idx>:LONG
    static inline void btnEvent(char* out, size_t n, uint8_t idx, bool isLong) {
        snprintf(out, n, "EVT:BTN:%d:%s", idx, isLong ? "LONG" : "CLICK");
    }

    // EVT:ENC:<enc>:+1  or  EVT:ENC:<enc>:-1
    static inline void encStep(char* out, size_t n, uint8_t enc, long delta) {
        snprintf(out, n, "EVT:ENC:%d:%s", enc, delta > 0 ? "+1" : "-1");
    }

    // EVT:ENC:<enc>:CLICK  or  EVT:ENC:<enc>:LONG
    static inline void encKey(char* out, size_t n, uint8_t enc, bool isLong) {
        snprintf(out, n, "EVT:ENC:%d:%s", enc, isLong ? "LONG" : "CLICK");
    }
}

// ===================== TFT circle text configs =====================
namespace TftTextCfg {
    static inline CircleTextConfig Status() {
        CircleTextConfig c;
        c.cx = 120; c.cy = 120; c.r = 118;
        c.topY = 6;
        c.bottomY = 52;
        c.margin = 8;
        c.lineGap = 2;
        c.textSize = 2;
        c.color = 0xFFFF;
        return c;
    }
}
