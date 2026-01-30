#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>

enum class CircleTextPos {
    Top,
    Center,
    Bottom
};

struct CircleTextConfig {
    // For GC9A01 240x240 by default
    int16_t cx = 120;
    int16_t cy = 120;
    int16_t r  = 118;

    int16_t topY    = 20;
    int16_t bottomY = 220;

    int16_t margin  = 6;
    int16_t lineGap = 4;

    uint8_t textSize = 2;
    uint16_t color   = 0xFFFF;
};

namespace CircleText {
    void setConfig(const CircleTextConfig& cfg);

    void drawWithConfig(Adafruit_GFX& gfx, const CircleTextConfig& cfg, const char* text, CircleTextPos pos = CircleTextPos::Top);
}