#pragma once
#include <Arduino.h>
#include "AppConfig.h"
#include "AppState.h"

using SendCallback = void (*)(const char* msg);

class EventLogger {
public:
    void begin(AppState* state, SendCallback sendCallback = nullptr) {
        this->state = state;
        this->sendCallback = sendCallback;
    }

    void setSendCallback(SendCallback cb) {
        sendCallback = cb;
    }

    void push(const char* msg);

    const char* line(uint8_t index) const;
    uint8_t head() const;

private:
    AppState* state = nullptr;
    SendCallback sendCallback = nullptr;

    char logBuf[LogCfg::LINES][LogCfg::LEN] = {};
    uint8_t logHead = 0;
};