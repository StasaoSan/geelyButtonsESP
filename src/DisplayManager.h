#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

#include "AppState.h"
#include "EventLogger.h"
#include "I2CBusSwitcher.h"

class DisplayManager {
public:
    void begin(AppState* state, EventLogger* logger);
    void update();

private:
    AppState* state = nullptr;
    EventLogger* logger = nullptr;

    I2CBusSwitcher wire1Switcher;

    Adafruit_SSD1306* oleds[OledCfg::COUNT] = {};
    bool oledOk[OledCfg::COUNT] = {};

    void renderTempScreen(Adafruit_SSD1306& d, float temp);
    void renderDualTempScreen(Adafruit_SSD1306& d, float tempD, float tempP);
    void renderVolScreen(Adafruit_SSD1306& d);
    void renderInfoScreen(Adafruit_SSD1306& d);

    bool scanHasAddr(TwoWire& wire, uint8_t addr);
};