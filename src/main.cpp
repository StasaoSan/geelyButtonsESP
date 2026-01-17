#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ================= BLE UUIDs =================
static const char* BLE_NAME = "geelyController";
static const char* SERVICE_UUID        = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static const char* CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";

// ================= Pins =================
#define LED_BUILTIN 8  // Встроенный LED на GPIO8
static const int PIN_CLK = 5;   // KY-040 CLK
static const int PIN_DT  = 6;   // KY-040 DT
static const int PIN_SW  = 7;   // KY-040 Button

// ================= Settings =================
static const uint32_t ENC_DEBOUNCE_US = 1000;
static const uint32_t BTN_DEBOUNCE_MS = 30;
static const uint32_t BTN_LONG_MS     = 600;

// ================= BLE globals =================
BLECharacteristic* g_char = nullptr;
volatile bool g_deviceConnected = false;

// ================= Encoder state =================
volatile int8_t g_encDelta = 0;
volatile uint32_t g_lastEncMicros = 0;

// Button state
bool g_btnLast = true;
uint32_t g_btnLastChangeMs = 0;
uint32_t g_btnPressStartMs = 0;
bool g_btnLongSent = false;

// LED indication
bool g_ledState = false;

// ---- BLE callbacks ----
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        g_deviceConnected = true;
        Serial.println("BLE: connected");
        // Быстро мигнуть 3 раза при подключении
        for (int i = 0; i < 3; i++) {
            digitalWrite(LED_BUILTIN, LOW);
            delay(80);
            digitalWrite(LED_BUILTIN, HIGH);
            delay(80);
        }
    }
    void onDisconnect(BLEServer* pServer) override {
        g_deviceConnected = false;
        Serial.println("BLE: disconnected, advertising...");
        pServer->getAdvertising()->start();
    }
};

// ---- Send helper ----
void bleSend(const char* msg) {
    Serial.print("SEND: ");
    Serial.println(msg);

    if (g_deviceConnected && g_char) {
        g_char->setValue((uint8_t*)msg, strlen(msg));
        g_char->notify();
    }
}

// ---- LED blink helper ----
void blinkLED(int times) {
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_BUILTIN, LOW);  // включить
        delay(100);
        digitalWrite(LED_BUILTIN, HIGH); // выключить
        delay(100);
    }
}

// ---- Encoder ISR ----
void IRAM_ATTR onClkChange() {
    uint32_t now = micros();
    if (now - g_lastEncMicros < ENC_DEBOUNCE_US) return;
    g_lastEncMicros = now;

    int clk = digitalRead(PIN_CLK);
    int dt  = digitalRead(PIN_DT);

    // Исправление volatile warnings
    if (dt != clk) {
        g_encDelta = g_encDelta + 1;
    } else {
        g_encDelta = g_encDelta - 1;
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);

    // LED setup
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW); // включить при старте

    // Encoder pins
    pinMode(PIN_CLK, INPUT_PULLUP);
    pinMode(PIN_DT,  INPUT_PULLUP);
    pinMode(PIN_SW,  INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(PIN_CLK), onClkChange, CHANGE);

    // ---- BLE init ----
    BLEDevice::init(BLE_NAME);
    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(new MyServerCallbacks());

    BLEService* service = server->createService(SERVICE_UUID);

    g_char = service->createCharacteristic(
            CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );

    g_char->addDescriptor(new BLE2902());

    service->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMinPreferred(0x12);

    BLEDevice::startAdvertising();
    Serial.println("BLE: advertising started");

    // LED горит = готов к работе
    digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
    // ---- Consume encoder delta ----
    int8_t delta = 0;
    noInterrupts();
    delta = g_encDelta;
    g_encDelta = 0;
    interrupts();

    if (delta != 0) {
        if (delta > 0) {
            bleSend("ENC:+1");
            blinkLED(2); // 2 раза = вправо
        } else {
            bleSend("ENC:-1");
            blinkLED(1); // 1 раз = влево
        }
        // Возвращаем LED в состояние "горит"
        digitalWrite(LED_BUILTIN, LOW);
    }

    // ---- Button handling ----
    bool btn = digitalRead(PIN_SW);
    uint32_t nowMs = millis();

    if (btn != g_btnLast) {
        if (nowMs - g_btnLastChangeMs > BTN_DEBOUNCE_MS) {
            g_btnLastChangeMs = nowMs;
            g_btnLast = btn;

            if (btn == LOW) {
                g_btnPressStartMs = nowMs;
                g_btnLongSent = false;
            } else {
                uint32_t held = nowMs - g_btnPressStartMs;
                if (!g_btnLongSent && held < BTN_LONG_MS) {
                    bleSend("BTN:CLICK");
                    // Короткая вспышка на клик
                    digitalWrite(LED_BUILTIN, HIGH);
                    delay(50);
                    digitalWrite(LED_BUILTIN, LOW);
                }
            }
        }
    }

    // long press
    if (g_btnLast == LOW && !g_btnLongSent) {
        if (nowMs - g_btnPressStartMs >= BTN_LONG_MS) {
            g_btnLongSent = true;
            bleSend("BTN:LONG");
            // Длинная вспышка на long press
            digitalWrite(LED_BUILTIN, HIGH);
            delay(200);
            digitalWrite(LED_BUILTIN, LOW);
        }
    }

    delay(5);
}