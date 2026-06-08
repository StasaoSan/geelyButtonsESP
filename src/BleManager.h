#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>

#include "AppState.h"
#include "EventLogger.h"

class BleManager {
public:
    void begin(AppState* state, EventLogger* logger);
    void update();

    void send(const char* msg);

private:
    AppState* state = nullptr;
    EventLogger* logger = nullptr;

    BLECharacteristic* characteristic = nullptr;

    volatile bool rxPending = false;
    char rxMsg[LogCfg::LEN] = {0};

    void handleRx(const char* msg);

    friend class BleRxCallbacks;
    friend class BleServerCallbacks;
};