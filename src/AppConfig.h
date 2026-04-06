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

// ===================== Touch timings =====================
namespace TouchCfg {
    static constexpr uint16_t UP_TIMEOUT_MS = 250;
    static constexpr uint16_t LOG_MIN_MS    = 80;
    static constexpr uint16_t LOG_MIN_DIST  = 6;   // Manhattan sum
}

// ===================== Logger buffer sizes =====================
namespace LogCfg {
    static constexpr uint8_t  LINES = 6;
    static constexpr uint8_t  LEN   = 32;
}

// ===================== Climate direction cycle =====================
// Кнопка BTN_IDX циклически перебирает комбинации зон обдува
namespace ClimateDirCfg {
    static constexpr uint8_t BTN_IDX   = 6;
    static constexpr uint8_t MODE_COUNT = 5;
    static constexpr const char* EVENTS[MODE_COUNT] = {
        "EVT:CLIMATE:BODY",
        "EVT:CLIMATE:BODY+LEGS",
        "EVT:CLIMATE:LEGS",
        "EVT:CLIMATE:LEGS+WINDOWS",
        "EVT:CLIMATE:WINDOWS",
    };
}

// ===================== Volume mode (enc2) =====================
namespace VolumeCfg {
    static constexpr uint8_t MODE_BTN_IDX = 12; // long press → toggle enc2 → volume mode
    static constexpr int     MIN          = 0;
    static constexpr int     MAX          = 100;
    static constexpr const char* STEP_M   = "EVT:VOLUME:-1";
    static constexpr const char* STEP_P   = "EVT:VOLUME:+1";
    static constexpr const char* MODE_ON  = "EVT:VOL_MODE:ON";
    static constexpr const char* MODE_OFF = "EVT:VOL_MODE:OFF";
}

// ===================== Event strings + форматтеры =====================
namespace Evt {
    // статические
    static constexpr const char* BOOT          = "EVT:BOOT";
    static constexpr const char* READY         = "EVT:READY";
    static constexpr const char* OLED_NOTFOUND = "EVT:OLED:NOTFOUND";

    static constexpr const char* TOUCH_DOWN    = "EVT:TOUCH:DOWN";
    static constexpr const char* TOUCH_UP      = "EVT:TOUCH:UP";

    static constexpr const char* ENC1_P        = "EVT:TEMP_MAIN:+1";
    static constexpr const char* ENC1_M        = "EVT:TEMP_MAIN:-1";
    static constexpr const char* ENC2_P        = "EVT:TEMP_PASS:-1";
    static constexpr const char* ENC2_M        = "EVT:TEMP_PASS:+1";

    static constexpr const char* ENC1_CLICK    = "EVT:CLIMATE_MODE";  //  chage climate (body / legs / windows in some order)
    static constexpr const char* ENC1_LONG     = "EVT:CLIMATE:AUTO";  // auto mode
    static constexpr const char* ENC2_CLICK    = "EVT:DUAL_SW";       // synchronizaton main and pass temps
    static constexpr const char* ENC2_LONG     = "EVT:RECIRCULATION"; // recirculation mode

    static constexpr const char* BTN_CLICK[BtnCfg::BTN_COUNT] = {
            "EVT:DRIVER_FAN",        // d0
            "EVT:DRIVER_HEAT",       // d1
            "EVT:ELECTRIC_DEFROST",  // d2
            "EVT:WHEEL_HEAT",        // d3
            "EVT:FAN:+1",            // d4
            "EVT:FAN:-1",            // d5
            "",                      // d6
            "EVT:BTN:C7:CLICK",      // d7 empty
            "EVT:REAR_LEFT_HEAT",    // d8 (intent not ready)
            "EVT:REAR_RIGHT_HEAT",   // d9 (intent not ready)
            "EVT:PASS_FAN",         // d10
            "EVT:PASS_HEAT",        // d11
            "EVT:THUNK",            // d12
            "EVT:DRIVE_MODE",       // d13
            "",                     // encoder key handled separately
            "",                     // encoder key handled separately
    };

    static constexpr const char* BTN_LONG[BtnCfg::BTN_COUNT] = {
            "EVT:DRIVER_FAN_OFF",
            "EVT:DRIVER_HEAT_OFF",
            "EVT:REAR_DEFROST",
            "EVT:WHEEL_HEAT_OFF",
            "EVT:BTN:C4:LONG",
            "EVT:BTN:C5:LONG",
            "EVT:BTN:C6:LONG",
            "EVT:BTN:C7:LONG",
            "EVT:RL_HEAT_OFF",
            "EVT:RR_HEAT_OFF",   // d9
            "EVT:PASS_FAN_OFF", // d10
            "EVT:PASS_HEAT_OFF",// d11
            "EVT:BTN:C12:LONG", // d12
            "EVT:SCREEN_BRIGHT", // d13 max / auto display bright
            "EVT:BTN:C14:LONG",
            "EVT:BTN:C15:LONG",
    };

    // хелперы выбора статических строк
    static inline const char* encStep(uint8_t enc, long delta) {
        if (enc == 1) return (delta > 0) ? ENC1_P : ENC1_M;
        return (delta > 0) ? ENC2_P : ENC2_M;
    }

    static inline const char* btnLongByIdx(uint8_t idx) {
        if (idx >= BtnCfg::BTN_COUNT) return "EVT:BTN:UNKNOWN:LONG";
        return BTN_LONG[idx];
    }

    static inline const char* btnClickByIdx(uint8_t idx) {
        if (idx >= BtnCfg::BTN_COUNT) return "EVT:BTN:UNKNOWN:CLICK";
        return BTN_CLICK[idx];
    }

    static inline const char* encKey(uint8_t enc, bool isLong) {
        if (enc == 1) return isLong ? ENC1_LONG : ENC1_CLICK;
        return isLong ? ENC2_LONG : ENC2_CLICK;
    }

    // динамические
    static inline void touchXY(char* out, size_t n, int16_t x, int16_t y) {
        snprintf(out, n, "EVT:TOUCH:X=%d,Y=%d", x, y);
    }

    static inline void oledAddr(char* out, size_t n, uint8_t addr) {
        snprintf(out, n, "EVT:OLED:0x%02X", addr);
    }
}

// ===================== TFT circle text configs =====================
namespace TftTextCfg {
    // Строка статуса сверху (как твой tftTopStatus, но под круг)
    static inline CircleTextConfig Status() {
        CircleTextConfig c;
        c.cx = 120; c.cy = 120; c.r = 118;

        // зона текста: верхняя "шапка"
        c.topY = 6;
        c.bottomY = 52;

        c.margin = 8;
        c.lineGap = 2;

        c.textSize = 2;
        c.color = 0xFFFF; // white
        return c;
    }

    // Нижняя зона под координаты/подсказки
    static inline CircleTextConfig Bottom() {
        CircleTextConfig c;
        c.cx = 120; c.cy = 120; c.r = 118;

        c.topY = 188;
        c.bottomY = 236;

        c.margin = 8;
        c.lineGap = 2;

        c.textSize = 2;
        c.color = 0xFFFF;
        return c;
    }
}