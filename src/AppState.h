#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <cmath>

struct AppState {
    bool deviceConnected = false;
    bool needSync = false;
    bool bleDisconnected = false;

    int rearDefrost = -1;
    int electricDefrost = -1;

    int fanArea = -1;
    char fanLevel[12] = "?";

    float tempMain = NAN;
    float tempPass = NAN;

    bool enc2VolumeMode = false;
    int volumeLevel = 50;

    bool displayDirty = true;

    Preferences prefs;

    void begin() {
        prefs.begin("geely", false);
        enc2VolumeMode = prefs.getBool("volMode", false);
    }

    void setDisplayDirty() {
        displayDirty = true;
    }
};