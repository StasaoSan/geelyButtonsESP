#pragma once
#include <Arduino.h>
#include <Wire.h>

class I2CBusSwitcher {
public:
    void switchTo(int sda, int scl);

private:
    int currentSda = -1;
    int currentScl = -1;
};