#include "EventLogger.h"
#include <cstring>

void EventLogger::push(const char* msg) {
    strncpy(logBuf[logHead], msg, LogCfg::LEN - 1);
    logBuf[logHead][LogCfg::LEN - 1] = '\0';

    logHead = (logHead + 1) % LogCfg::LINES;

    if (state) {
        state->displayDirty = true;
    }

    Serial.println(msg);

    if (sendCallback) {
        sendCallback(msg);
    }
}

const char* EventLogger::line(uint8_t index) const {
    return logBuf[index % LogCfg::LINES];
}

uint8_t EventLogger::head() const {
    return logHead;
}