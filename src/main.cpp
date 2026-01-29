#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GC9A01A.h>
#include <CST816S.h>

// ===== TFT =====
#define TFT_SCL  13
#define TFT_SDA 14
#define TFT_CS   26
#define TFT_DC   27
#define TFT_RST  25

Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);

// ===== TOUCH =====
#define TP_SDA 21
#define TP_SCL 22
#define TP_RST 32
#define TP_INT 34

CST816S touch(TP_SDA, TP_SCL, TP_RST, TP_INT);

static void drawText(int16_t x, int16_t y, uint8_t size, const char* s) {
    tft.setTextSize(size);
    tft.setTextColor(GC9A01A_WHITE);
    tft.setCursor(x, y);
    tft.print(s);
}

static void drawI2CScanResult() {
    tft.fillScreen(GC9A01A_BLACK);
    drawText(20, 20, 2, "I2C scan:");

    int16_t y = 50;
    bool any = false;

    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            any = true;
            char buf[16];
            snprintf(buf, sizeof(buf), "0x%02X", addr);

            drawText(20, y, 2, buf);
            y += 18;

            // если строк много — переносим в столбик справа
            if (y > 220) {
                y = 50;
                // второй столбик
                // (простая раскладка)
                tft.setCursor(120, y);
            }
        }
        delay(2);
    }

    if (!any) {
        drawText(20, 60, 2, "No devices!");
        drawText(20, 80, 2, "Check wiring.");
    }
}

static void drawXY(int16_t x, int16_t y) {
    // плашка снизу
    tft.fillRect(0, 200, 240, 40, GC9A01A_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(GC9A01A_WHITE);
    tft.setCursor(10, 210);
    tft.print("X:");
    tft.print(x);
    tft.print(" Y:");
    tft.print(y);
}

void setup() {
    delay(200);

    // TFT init
    SPI.begin(TFT_SCL, -1, TFT_SDA, TFT_CS);
    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(GC9A01A_BLACK);

    drawText(40, 100, 2, "Touch init...");

    // Touch reset (очень желательно)
    pinMode(TP_RST, OUTPUT);
    digitalWrite(TP_RST, LOW);
    delay(20);
    digitalWrite(TP_RST, HIGH);
    delay(80);

    // I2C init
    Wire.begin(TP_SDA, TP_SCL);

    // покажем, что реально видим по I2C (ожидаем 0x15)
    drawI2CScanResult();
    delay(1200);

    // begin() в твоей библиотеке возвращает void — просто вызываем
    touch.begin();

    // UI
    tft.fillScreen(GC9A01A_BLACK);
    drawText(50, 90, 2, "Touch ready");
    drawText(30, 120, 2, "Tap to draw");
}

void loop() {
    static int16_t lastX = -1, lastY = -1;
    static uint32_t lastMs = 0;

    if (!touch.available()) {
        delay(5);
        return;
    }

    int16_t x = (int16_t)touch.data.x;
    int16_t y = (int16_t)touch.data.y;

    // фильтр мусора
    if (x == 0 && y == 0) return;

    // ограничение частоты обновления
    uint32_t now = millis();
    if (now - lastMs < 40) return;

    // не рисовать одно и то же
    if (x == lastX && y == lastY) return;

    lastX = x;
    lastY = y;
    lastMs = now;

    x = constrain(x, 0, 239);
    y = constrain(y, 0, 239);

    tft.fillCircle(x, y, 3, GC9A01A_GREEN);
    drawXY(x, y);
}