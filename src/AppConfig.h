#pragma once
#include <Arduino.h>

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

    // I2C (Touch + OLED)
    static constexpr int I2C_SDA = 21;
    static constexpr int I2C_SCL = 22;

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

// ===================== OLED =====================
namespace OledCfg {
    static constexpr int W = 128;
    static constexpr int H = 64;
    // базовые кандидаты; фактический адрес выбирается после i2cScan()
    static constexpr uint8_t AddrCandidates[] = {0x3C, 0x3D, 0x03, 0x3F};
}

// ===================== Buttons / MUX mapping =====================
namespace BtnCfg {
    static constexpr uint8_t BTN_FIRST_CH = 0;
    static constexpr uint8_t BTN_COUNT    = 16;

    // Encoder keys (через индексы в readButtonPressedByIndex(idx))
    static constexpr uint8_t ENC1_KEY_IDX = 1;
    static constexpr uint8_t ENC2_KEY_IDX = 2;

    static constexpr uint16_t DEBOUNCE_MS = 30;
    static constexpr bool ACTIVE_LOW = true;
}

// ===================== Encoder key timings =====================
namespace EncCfg {
    static constexpr uint16_t KEY_LONG_MS = 450;
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
    static constexpr uint8_t  LINES = 7;
    static constexpr uint8_t  LEN   = 32;
}

// ===================== Event strings + форматтеры =====================
namespace Evt {
    // статические
    static constexpr const char* BOOT         = "EVT:BOOT";
    static constexpr const char* READY        = "EVT:READY";
    static constexpr const char* OLED_NOTFOUND= "EVT:OLED:NOTFOUND";

    static constexpr const char* TOUCH_DOWN   = "EVT:TOUCH:DOWN";
    static constexpr const char* TOUCH_UP     = "EVT:TOUCH:UP";

    static constexpr const char* ENC1_P       = "EVT:TEMP_MAIN:+1";
    static constexpr const char* ENC1_M       = "EVT:TEMP_MAIN:-1";
    static constexpr const char* ENC2_P       = "EVT:TEMP_PASS:+1";
    static constexpr const char* ENC2_M       = "EVT:TEMP_PASS:-1";

    static constexpr const char* ENC1_CLICK   = "EVT:CLIMATE_SW";
    static constexpr const char* ENC1_LONG    = "EVT:DUAL_SW";
    static constexpr const char* ENC2_CLICK   = "EVT:REAR_DEFROST";
    static constexpr const char* ENC2_LONG    = "EVT:ELECTRIC_DEFROST";

    static constexpr const char* BTN_CLICK[BtnCfg::BTN_COUNT] = {
            "EVT:BTN:C0:CLICK", // nothing connected yet
            "", // see encoder actions
            "", // see encoder actions
            "EVT:FAN:+1",   // d3
            "EVT:FAN:-1", // d4
            "EVT:CLIMATE_BODY", // d5
            "EVT:CLIMATE_LEGS", // d6
            "EVT:CLIMATE_WINDOWS", // d7
            "EVT:THUNK", // d8
            "EVT:DRIVER_HEAT", // d9
            "EVT:DRIVER_FAN", // d10
            "EVT:WHEEL_HEAT", // d11
            "EVT:PASS_HEAT", // d12
            "EVT:PASS_FAN", // d13
            "EVT:BTN:C14:CLICK", // d14, action not invented yet
            "EVT:BTN:C15:CLICK", // d15, action not invented yet
    };

    // хелперы выбора статических строк
    static inline const char* encStep(uint8_t enc, long delta) {
        if (enc == 1) return (delta > 0) ? ENC1_P : ENC1_M;
        return (delta > 0) ? ENC2_P : ENC2_M;
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

#include "CircleText.h"

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