#include "CircleText.h"
#include <math.h>

static CircleTextConfig g_cfg;

void CircleText::setConfig(const CircleTextConfig& cfg) {
    g_cfg = cfg;
}

static void measureText(Adafruit_GFX& gfx, const char* s, int16_t* w, int16_t* h) {
    int16_t x1, y1;
    uint16_t ww, hh;
    gfx.getTextBounds(s, 0, 0, &x1, &y1, &ww, &hh);
    *w = (int16_t)ww;
    *h = (int16_t)hh;
}

static int16_t textLineHeight(Adafruit_GFX& gfx) {
    int16_t w, h;
    measureText(gfx, "Ay", &w, &h);
    return (h > 0) ? h : 8;
}

static size_t fitCharsIntoWidth(Adafruit_GFX& gfx, const char* s, int16_t maxW) {
    char tmp[96];
    size_t n = 0;
    tmp[0] = '\0';

    while (s[n] && n < sizeof(tmp) - 1) {
        tmp[n] = s[n];
        tmp[n + 1] = '\0';

        int16_t w, h;
        measureText(gfx, tmp, &w, &h);
        if (w > maxW) return (n == 0) ? 1 : n;
        n++;
    }
    return n;
}

static bool circleLineBounds(int16_t cx, int16_t cy, int16_t R,
                             int16_t y, int16_t margin,
                             int16_t* xLeft, int16_t* xRight) {
    int32_t dy = (int32_t)y - (int32_t)cy;
    int32_t rr = (int32_t)R * (int32_t)R;
    int32_t ddy = dy * dy;
    if (ddy >= rr) return false;

    float dx = sqrtf((float)(rr - ddy));
    int16_t xl = (int16_t)roundf((float)cx - dx) + margin;
    int16_t xr = (int16_t)roundf((float)cx + dx) - margin;

    if (xr <= xl) return false;
    *xLeft = xl;
    *xRight = xr;
    return true;
}

// Layout pass: calculate how many lines will be produced and total height
static int16_t layoutHeight(Adafruit_GFX& gfx,
                            const CircleTextConfig& cfg,
                            const char* text) {
    gfx.setTextSize(cfg.textSize);

    int16_t lineH = textLineHeight(gfx);
    int16_t cursorY = cfg.topY;

    String line;
    line.reserve(160);

    auto flushLine = [&](int16_t xLeft, int16_t maxW) -> bool {
        if (line.length() == 0) return true;

        int16_t lw, lh;
        measureText(gfx, line.c_str(), &lw, &lh);

        cursorY += lh + cfg.lineGap;
        line = "";
        return cursorY <= cfg.bottomY;
    };

    const char* p = text;

    while (*p) {
        while (*p == ' ') p++;

        if (*p == '\n') {
            int16_t xL, xR;
            if (!circleLineBounds(cfg.cx, cfg.cy, cfg.r, cursorY, cfg.margin, &xL, &xR)) break;
            flushLine(xL, xR - xL);
            cursorY += cfg.lineGap;
            p++;
            continue;
        }

        if (!*p) break;

        int16_t xL, xR;
        if (!circleLineBounds(cfg.cx, cfg.cy, cfg.r, cursorY, cfg.margin, &xL, &xR)) break;
        int16_t maxW = xR - xL;

        const char* start = p;
        while (*p && *p != ' ' && *p != '\n') p++;
        size_t len = (size_t)(p - start);
        if (len == 0) continue;

        String word;
        word.reserve(len + 1);
        for (size_t i = 0; i < len; i++) word += start[i];

        String candidate = line;
        if (candidate.length() > 0) candidate += " ";
        candidate += word;

        int16_t candW, candH;
        measureText(gfx, candidate.c_str(), &candW, &candH);

        if (candW <= maxW) {
            line = candidate;
            continue;
        }

        if (line.length() > 0) {
            if (!flushLine(xL, maxW)) break;
            if (!circleLineBounds(cfg.cx, cfg.cy, cfg.r, cursorY, cfg.margin, &xL, &xR)) break;
            maxW = xR - xL;
        }

        int16_t wordW, wordH;
        measureText(gfx, word.c_str(), &wordW, &wordH);

        if (wordW <= maxW) {
            line = word;
            continue;
        }

        const char* ws = word.c_str();
        while (*ws) {
            if (!circleLineBounds(cfg.cx, cfg.cy, cfg.r, cursorY, cfg.margin, &xL, &xR)) break;
            maxW = xR - xL;

            size_t chunkLen = fitCharsIntoWidth(gfx, ws, maxW);
            String chunk;
            chunk.reserve(chunkLen + 1);
            for (size_t i = 0; i < chunkLen; i++) chunk += ws[i];

            line = chunk;
            if (!flushLine(xL, maxW)) break;
            ws += chunkLen;
        }
    }

    // flush last line
    if (line.length() > 0) {
        int16_t xL, xR;
        if (circleLineBounds(cfg.cx, cfg.cy, cfg.r, cursorY, cfg.margin, &xL, &xR)) {
            int16_t lw, lh;
            measureText(gfx, line.c_str(), &lw, &lh);
            cursorY += lh;
        }
    }

    int16_t used = cursorY - cfg.topY;
    if (used < 0) used = 0;
    return used;
}

static void drawWrappedTextCircle(Adafruit_GFX& gfx,
                                  CircleTextConfig cfg,
                                  const char* text,
                                  CircleTextPos pos) {
    gfx.setTextSize(cfg.textSize);
    gfx.setTextColor(cfg.color);

    // compute Y start based on pos
    int16_t h = layoutHeight(gfx, cfg, text);

    int16_t available = cfg.bottomY - cfg.topY;
    if (available < 0) available = 0;

    int16_t startY = cfg.topY;
    if (pos == CircleTextPos::Center) {
        startY = cfg.topY + (available - h) / 2;
    } else if (pos == CircleTextPos::Bottom) {
        startY = cfg.bottomY - h;
    }
    if (startY < cfg.topY) startY = cfg.topY;

    int16_t cursorY = startY;

    String line;
    line.reserve(160);

    auto flushLine = [&](int16_t xLeft, int16_t maxW) -> bool {
        if (line.length() == 0) return true;

        int16_t lw, lh;
        measureText(gfx, line.c_str(), &lw, &lh);
        if (cursorY + lh > cfg.bottomY) return false;

        gfx.setCursor(xLeft, cursorY);
        gfx.print(line);

        cursorY += lh + cfg.lineGap;
        line = "";
        return true;
    };

    const char* p = text;

    while (*p) {
        while (*p == ' ') p++;

        if (*p == '\n') {
            int16_t xL, xR;
            if (!circleLineBounds(cfg.cx, cfg.cy, cfg.r, cursorY, cfg.margin, &xL, &xR)) return;
            if (!flushLine(xL, xR - xL)) return;
            cursorY += cfg.lineGap;
            p++;
            continue;
        }

        if (!*p) break;

        int16_t xL, xR;
        if (!circleLineBounds(cfg.cx, cfg.cy, cfg.r, cursorY, cfg.margin, &xL, &xR)) return;
        int16_t maxW = xR - xL;

        const char* start = p;
        while (*p && *p != ' ' && *p != '\n') p++;
        size_t len = (size_t)(p - start);
        if (len == 0) continue;

        String word;
        word.reserve(len + 1);
        for (size_t i = 0; i < len; i++) word += start[i];

        String candidate = line;
        if (candidate.length() > 0) candidate += " ";
        candidate += word;

        int16_t candW, candH;
        measureText(gfx, candidate.c_str(), &candW, &candH);

        if (candW <= maxW) {
            line = candidate;
            continue;
        }

        if (line.length() > 0) {
            if (!flushLine(xL, maxW)) return;

            if (!circleLineBounds(cfg.cx, cfg.cy, cfg.r, cursorY, cfg.margin, &xL, &xR)) return;
            maxW = xR - xL;
        }

        int16_t wordW, wordH;
        measureText(gfx, word.c_str(), &wordW, &wordH);

        if (wordW <= maxW) {
            line = word;
            continue;
        }

        const char* ws = word.c_str();
        while (*ws) {
            if (!circleLineBounds(cfg.cx, cfg.cy, cfg.r, cursorY, cfg.margin, &xL, &xR)) return;
            maxW = xR - xL;

            size_t chunkLen = fitCharsIntoWidth(gfx, ws, maxW);
            String chunk;
            chunk.reserve(chunkLen + 1);
            for (size_t i = 0; i < chunkLen; i++) chunk += ws[i];

            line = chunk;
            if (!flushLine(xL, maxW)) return;
            ws += chunkLen;
        }
    }

    if (line.length() > 0) {
        int16_t xL, xR;
        if (!circleLineBounds(cfg.cx, cfg.cy, cfg.r, cursorY, cfg.margin, &xL, &xR)) return;
        flushLine(xL, xR - xL);
    }
}

void CircleText::drawWithConfig(Adafruit_GFX& gfx, const CircleTextConfig& cfg, const char* text, CircleTextPos pos) {
    drawWrappedTextCircle(gfx, cfg, text, pos);
}

void CircleText::draw(Adafruit_GFX& gfx, const char* text, CircleTextPos pos) {
    drawWrappedTextCircle(gfx, g_cfg, text, pos);
}