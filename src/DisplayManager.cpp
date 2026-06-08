#include "DisplayManager.h"
#include "AppConfig.h"
#include <cmath>

void DisplayManager::begin(AppState* s, EventLogger* log) {
    state  = s;
    logger = log;

    oleds[0] = new Adafruit_SSD1306(OledCfg::WIDTHS[0], OledCfg::HEIGHTS[0], &Wire,  -1);
    oleds[1] = new Adafruit_SSD1306(OledCfg::WIDTHS[1], OledCfg::HEIGHTS[1], &Wire1, -1);
    oleds[2] = new Adafruit_SSD1306(OledCfg::WIDTHS[2], OledCfg::HEIGHTS[2], &Wire1, -1);

    // OLED 0 — Wire (I2C_SDA/SCL)
    if (scanHasAddr(Wire, OledCfg::ADDRS[0])) {
        if (oleds[0]->begin(SSD1306_SWITCHCAPVCC, OledCfg::ADDRS[0])) {
            oledOk[0] = true;
            Serial.printf("OLED[0] 0x%02X %dx%d Wire: OK\n",
                          OledCfg::ADDRS[0], OledCfg::WIDTHS[0], OledCfg::HEIGHTS[0]);
        } else {
            Serial.printf("OLED[0]: begin() failed\n");
        }
    } else {
        Serial.printf("OLED[0] 0x%02X: not found on Wire\n", OledCfg::ADDRS[0]);
    }

    // OLED 1 — Wire1 на I2C1
    wire1Switcher.switchTo(Pins::I2C1_SDA, Pins::I2C1_SCL);
    if (scanHasAddr(Wire1, OledCfg::ADDRS[1])) {
        if (oleds[1]->begin(SSD1306_SWITCHCAPVCC, OledCfg::ADDRS[1])) {
            oledOk[1] = true;
            Serial.printf("OLED[1] 0x%02X %dx%d Wire1(I2C1): OK\n",
                          OledCfg::ADDRS[1], OledCfg::WIDTHS[1], OledCfg::HEIGHTS[1]);
        } else {
            Serial.printf("OLED[1]: begin() failed\n");
        }
    } else {
        Serial.printf("OLED[1] 0x%02X: not found on Wire1(I2C1)\n", OledCfg::ADDRS[1]);
    }

    // OLED 2 — Wire1 на I2C2
    wire1Switcher.switchTo(Pins::I2C2_SDA, Pins::I2C2_SCL);
    if (scanHasAddr(Wire1, OledCfg::ADDRS[2])) {
        if (oleds[2]->begin(SSD1306_SWITCHCAPVCC, OledCfg::ADDRS[2])) {
            oledOk[2] = true;
            Serial.printf("OLED[2] 0x%02X %dx%d Wire1(I2C2): OK\n",
                          OledCfg::ADDRS[2], OledCfg::WIDTHS[2], OledCfg::HEIGHTS[2]);
        } else {
            Serial.printf("OLED[2]: begin() failed\n");
        }
    } else {
        Serial.printf("OLED[2] 0x%02X: not found on Wire1(I2C2)\n", OledCfg::ADDRS[2]);
    }
}

bool DisplayManager::scanHasAddr(TwoWire& wire, uint8_t addr) {
    wire.beginTransmission(addr);
    return wire.endTransmission() == 0;
}

void DisplayManager::update() {
    if (!state->displayDirty) return;
    state->displayDirty = false;

    if (oledOk[0]) {
        if (!oledOk[1]) {
            if (state->enc2VolumeMode) renderVolScreen(*oleds[0]);
            else                       renderDualTempScreen(*oleds[0], state->tempMain, state->tempPass);
        } else {
            renderTempScreen(*oleds[0], state->tempMain);
        }
    }
    if (oledOk[1]) {
        wire1Switcher.switchTo(Pins::I2C1_SDA, Pins::I2C1_SCL);
        if (state->enc2VolumeMode) renderVolScreen(*oleds[1]);
        else                       renderTempScreen(*oleds[1], state->tempPass);
    }
    if (oledOk[2]) {
        wire1Switcher.switchTo(Pins::I2C2_SDA, Pins::I2C2_SCL);
        renderInfoScreen(*oleds[2]);
    }
}

void DisplayManager::renderTempScreen(Adafruit_SSD1306& d, float temp) {
    d.clearDisplay();
    d.setTextWrap(false);
    d.setTextColor(SSD1306_WHITE);

    if (isnan(temp)) {
        d.setTextSize(3);
        d.setCursor((64 - 18) / 2, 4);
        d.print("?");
    } else {
        bool  neg   = (temp < 0.0f);
        float abst  = fabsf(temp);
        int   ipart = (int)abst;
        int   dpart = (int)roundf((abst - ipart) * 10.0f);
        if (dpart >= 10) { ipart++; dpart = 0; }

        char ibuf[8];
        snprintf(ibuf, sizeof(ibuf), neg ? "-%d" : "%d", ipart);
        bool hasDec = (dpart != 0);

        int iw = (int)strlen(ibuf) * 18;
        int dw = hasDec ? 12 : 0;
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

void DisplayManager::renderDualTempScreen(Adafruit_SSD1306& d, float tempD, float tempP) {
    d.clearDisplay();
    d.setTextWrap(false);
    d.setTextColor(SSD1306_WHITE);

    auto drawHalf = [&](float t, int y) {
        char buf[10];
        if (isnan(t)) strcpy(buf, "?");
        else          snprintf(buf, sizeof(buf), "%.1f", t);
        int w = (int)strlen(buf) * 12;
        d.setTextSize(2);
        d.setCursor((64 - w) / 2, y);
        d.print(buf);
    };

    drawHalf(tempD, 0);
    drawHalf(tempP, 16);

    d.display();
}

void DisplayManager::renderVolScreen(Adafruit_SSD1306& d) {
    d.clearDisplay();
    d.setTextWrap(false);
    d.setTextColor(SSD1306_WHITE);

    d.setTextSize(1);
    d.setCursor(0, 0);
    d.print("VOL");

    char buf[5];
    snprintf(buf, sizeof(buf), "%d", state->volumeLevel);
    int w = (int)strlen(buf) * 18;
    d.setTextSize(3);
    d.setCursor((64 - w) / 2, 8);
    d.print(buf);

    d.display();
}

// Parses "L1".."L9" → 1..9, everything else → 0
static int parseFanLevel(const char* lvl) {
    if (lvl[0] == 'L' && lvl[1] >= '1' && lvl[1] <= '9' && lvl[2] == '\0')
        return lvl[1] - '0';
    return 0;
}

static const char* fanDirLabel(int area) {
    switch (area) {
        case 268894465: return "FACE";
        case 268894467: return "FACE+F";
        case 268894466: return "FEET";
        case 268894469: return "FEET+W";
        case 268894468: return "WIND";
        case 268894472: return "AUTO";
        case 0:         return "OFF";
        default:        return nullptr;
    }
}

void DisplayManager::renderInfoScreen(Adafruit_SSD1306& d) {
    d.clearDisplay();
    d.setTextWrap(false);
    d.setTextColor(SSD1306_WHITE);

    // ── Row 0: status ───────────────────────────────────────────
    d.setTextSize(1);
    d.setCursor(0, 0);
    d.print(state->deviceConnected ? "BLE" : "ble");
    d.print(" R:");
    d.print(state->rearDefrost    < 0 ? "?" : (state->rearDefrost    ? "1" : "0"));
    d.print(" E:");
    d.print(state->electricDefrost < 0 ? "?" : (state->electricDefrost ? "1" : "0"));
    d.print(" T:");
    if (isnan(state->tempMain)) d.print("?");
    else                        d.print(String(state->tempMain, 1));

    // ── Fan level bar (y=9..20, h=12) ───────────────────────────
    // 9 cells × 11px + 8 gaps × 2px = 115px, centred in 128px → x=6
    constexpr int CELL_COUNT = 9;
    constexpr int CELL_W     = 11;
    constexpr int GAP        = 2;
    constexpr int STEP       = CELL_W + GAP;
    constexpr int BAR_X      = (128 - (CELL_COUNT * CELL_W + (CELL_COUNT - 1) * GAP)) / 2;
    constexpr int BAR_Y      = 9;
    constexpr int BAR_H      = 12;
    constexpr int RADIUS     = 2;

    bool isOff  = (strcmp(state->fanLevel, "OFF") == 0 || strcmp(state->fanLevel, "?") == 0);
    bool isAuto = (strcmp(state->fanLevel, "AUTO") == 0);
    int  level  = (isOff || isAuto) ? 0 : parseFanLevel(state->fanLevel);

    for (int i = 0; i < CELL_COUNT; i++) {
        int cx = BAR_X + i * STEP;
        if (i < level) {
            d.fillRoundRect(cx, BAR_Y, CELL_W, BAR_H, RADIUS, SSD1306_WHITE);
        } else {
            d.drawRoundRect(cx, BAR_Y, CELL_W, BAR_H, RADIUS, SSD1306_WHITE);
        }
    }

    // ── Row 3: numeric level + direction ────────────────────────
    d.setTextSize(1);
    d.setCursor(0, 25);

    if (isOff) {
        d.print("OFF");
    } else if (isAuto) {
        d.print("AUTO");
    } else {
        d.print(state->fanLevel);   // e.g. "L4"
    }

    // Direction (right side of bottom row)
    if (state->fanArea >= 0) {
        const char* dir = fanDirLabel(state->fanArea);
        // right-align direction text: calc x from right edge
        const char* label = dir ? dir : nullptr;
        if (label) {
            int labelW = (int)strlen(label) * 6;
            d.setCursor(128 - labelW, 25);
            d.print(label);
        } else {
            // unknown area — show raw number
            char buf[12];
            snprintf(buf, sizeof(buf), "D:%d", state->fanArea);
            int labelW = (int)strlen(buf) * 6;
            d.setCursor(128 - labelW, 25);
            d.print(buf);
        }
    }

    d.display();
}
