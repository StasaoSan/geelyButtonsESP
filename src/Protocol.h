#pragma once
#include "AppState.h"
#include "EventLogger.h"

namespace Protocol {
    void processRx(const char* s, AppState& state, EventLogger& logger);
}