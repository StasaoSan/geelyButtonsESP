#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include <Wire.h>
#include <U8g2lib.h>
#include <AiEsp32RotaryEncoder.h>
#include <OneButton.h>

// ================= BLE UUIDs =================
static const char* BLE_NAME = "geelyController";
static const char* SERVICE_UUID        = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static const char* CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";

// ================= Pins =================
#define LED_BUILTIN 8
// encoder1
static const int PIN_CLK = 5;
static const int PIN_DT  = 6;
static const int PIN_SW  = 7;

// Screen
static const int PIN_SDA = 9;
static const int PIN_SCL = 10;


// ================= BLE globals =================
BLECharacteristic* g_char = nullptr;
volatile bool g_deviceConnected = false;

uint32_t g_ledUntilMs = 0;
bool g_ledInvert = false;
// ================= Display =================
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

String g_lastMsg = "-";
long g_lastEncPos = 0;
uint32_t g_lastSendMs = 0;

void displayInit() {
    Wire.begin(PIN_SDA, PIN_SCL);
    u8g2.begin();
    u8g2.setFont(u8g2_font_6x10_tf);
}

void displayDraw() {
    u8g2.clearBuffer();

    u8g2.setCursor(0, 12);
    u8g2.print("BLE: ");
    u8g2.print(g_deviceConnected ? "ON" : "OFF");

    u8g2.setCursor(0, 26);
    u8g2.print("ENC: ");
    u8g2.print(g_lastEncPos);

    u8g2.setCursor(0, 40);
    u8g2.print("LAST:");

    u8g2.setCursor(0, 54);
    u8g2.print(g_lastMsg);

    u8g2.sendBuffer();
}

// ================= Helpers =================
void bleSend(const char* msg) {
    g_lastMsg = msg;
    g_lastSendMs = millis();
    Serial.print("SEND: ");
    Serial.println(msg);
    if (g_deviceConnected && g_char) {
        g_char->setValue((uint8_t*)msg, strlen(msg));
        g_char->notify();
    }
}

void blinkLED(int times) {
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_BUILTIN, LOW);
        delay(80);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(80);
    }
    digitalWrite(LED_BUILTIN, LOW); // “горит” в твоей логике
}

// ---- BLE callbacks ----
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        g_deviceConnected = true;
        Serial.println("BLE: connected");
        blinkLED(3);
    }
    void onDisconnect(BLEServer* pServer) override {
        g_deviceConnected = false;
        Serial.println("BLE: disconnected, advertising...");
        pServer->getAdvertising()->start();
    }
};

// ================= Encoder via library =================
// Вариант с “4 шага на щелчок” для KY-040 часто удобнее.
// Если у тебя каждый детент должен давать 1 событие — оставь 4 и дели, или поставь 2/1 по факту.
AiEsp32RotaryEncoder encoder(PIN_CLK, PIN_DT, -1, -1, 4);

// Кнопка (INPUT_PULLUP, активный LOW)
OneButton button(PIN_SW, true, true);

void IRAM_ATTR readEncoderISR() {
    encoder.readEncoder_ISR();
}

void onButtonClick() {
    bleSend("BTN:CLICK");
    digitalWrite(LED_BUILTIN, HIGH);
    g_ledUntilMs = millis() + 40;
}

void onButtonLongPressStart() {
    bleSend("BTN:LONG");
    digitalWrite(LED_BUILTIN, HIGH);
    g_ledUntilMs = millis() + 200;
}

void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    // Encoder pins
    pinMode(PIN_CLK, INPUT_PULLUP);
    pinMode(PIN_DT,  INPUT_PULLUP);
    pinMode(PIN_SW,  INPUT_PULLUP);

    // Encoder init
    encoder.begin();
    encoder.setup(readEncoderISR);
    encoder.setAcceleration(0); // можно включить 1..n если хочешь ускорение
    attachInterrupt(digitalPinToInterrupt(PIN_CLK), readEncoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_DT),  readEncoderISR, CHANGE);

    // Button init
    button.attachClick(onButtonClick);
    button.attachLongPressStart(onButtonLongPressStart);
    button.setDebounceMs(30);
    button.setPressMs(600);

    displayInit();
    g_lastMsg = "BOOT";
    displayDraw();

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
}

void loop() {
    // Энкодер
    long newPos = encoder.readEncoder();
    static long lastPos = 0;
    long diff = newPos - lastPos;
    g_lastEncPos = newPos;

    if (diff != 0) {
        if (diff > 0) {
            bleSend("ENC:+1");
            blinkLED(2);
        } else {
            bleSend("ENC:-1");
            blinkLED(1);
        }
        lastPos = newPos;
    }

    // Кнопка
    button.tick();

    // LED pulse (если используешь вариант без delay в колбэках)
    if (g_ledUntilMs != 0 && millis() > g_ledUntilMs) {
        g_ledUntilMs = 0;
        digitalWrite(LED_BUILTIN, LOW);
    }

    // Экран обновляем редко
    static uint32_t lastUiMs = 0;
    if (millis() - lastUiMs >= 150) {
        lastUiMs = millis();
        displayDraw();
    }

    delay(2);
}