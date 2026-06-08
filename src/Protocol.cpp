#include "Protocol.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>

void Protocol::processRx(const char* s, AppState& state, EventLogger& logger) {
    if (strncmp(s, "FB:REAR:", 8) == 0) {
        int v = atoi(s + 8);
        state.rearDefrost = v;

        char b[LogCfg::LEN];
        snprintf(b, sizeof(b), "REAR_DEF:%s", v == 1 ? "ON" : "OFF");
        logger.push(b);
        return;
    }

    if (strncmp(s, "FB:ELECTRIC:", 12) == 0) {
        int v = atoi(s + 12);
        state.electricDefrost = v;

        char b[LogCfg::LEN];
        snprintf(b, sizeof(b), "E_DEF:%s", v == 1 ? "ON" : "OFF");
        logger.push(b);
        return;
    }

    // FB:FAN:<area>:<level>
    if (strncmp(s, "FB:FAN:", 7) == 0) {
        const char* p = s + 7;
        state.fanArea = atoi(p);

        const char* c1 = strchr(p, ':');
        if (c1 && *(c1 + 1)) {
            strncpy(state.fanLevel, c1 + 1, sizeof(state.fanLevel) - 1);
            state.fanLevel[sizeof(state.fanLevel) - 1] = '\0';
        } else {
            strncpy(state.fanLevel, "?", sizeof(state.fanLevel));
        }

        char b[LogCfg::LEN];
        snprintf(b, sizeof(b), "FAN:%d:%s", state.fanArea, state.fanLevel);
        logger.push(b);
        return;
    }

    // FB:TEMP:MAIN:<val>  |  FB:TEMP:PASS:<val>
    if (strncmp(s, "FB:TEMP:", 8) == 0) {
        const char* p = s + 8;
        char b[LogCfg::LEN];
        if (strncmp(p, "MAIN:", 5) == 0) {
            state.tempMain = (float)atof(p + 5);
            snprintf(b, sizeof(b), "T1:%.1f", state.tempMain);
            logger.push(b);
            return;
        }
        if (strncmp(p, "PASS:", 5) == 0) {
            state.tempPass = (float)atof(p + 5);
            snprintf(b, sizeof(b), "T4:%.1f", state.tempPass);
            logger.push(b);
            return;
        }
    }

    // GIB:FLOAT:<id>:<area>:<value>
    if (strncmp(s, "GIB:FLOAT:", 10) == 0) {
        const char* p = s + 10;
        int id = atoi(p);

        const char* c1 = strchr(p, ':');
        if (!c1) goto fallback;
        int area = atoi(c1 + 1);

        const char* c2 = strchr(c1 + 1, ':');
        if (!c2) goto fallback;
        float v = (float)atof(c2 + 1);

        if (id == 268828928) {  // IHvac.HVAC_FUNC_TEMP
            if (area == 1)      state.tempMain = v;
            else if (area == 4) state.tempPass = v;

            char b[LogCfg::LEN];
            snprintf(b, sizeof(b), "TEMP:%d:%.1f", area, v);
            logger.push(b);
            return;
        }

        {
            char b[LogCfg::LEN];
            snprintf(b, sizeof(b), "F:%d:%d:%.2f", id, area, v);
            logger.push(b);
            return;
        }
    }

fallback:
    char buf[LogCfg::LEN];
    snprintf(buf, sizeof(buf), "RX:%s", s);
    logger.push(buf);
}