#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>

#include <Wire.h>
#include <U8g2lib.h>
#include <AiEsp32RotaryEncoder.h>
#include <OneButton.h>

// BLE UUIDs
static const char* BLE_NAME = "geelyController";
static const char* SERVICE_UUID        = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static const char* CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";

// Pins (ESP32-C3 Super Mini)
#define LED_BUILTIN 8

// Encoder1 (KY-040: CLK, DT, SW) (main temp)
static const int ENC1_CLK = 5;
static const int ENC1_DT  = 6;
static const int ENC1_SW  = 7;

// Encoder2 (S1, S2, KEY) (pass temp)
static const int ENC2_S1  = 20;
static const int ENC2_S2  = 21;
static const int ENC2_KEY = 2;

// Buttons (fan speed)
static const int BTN_FAN_UP   = 0;
static const int BTN_FAN_DOWN = 1;

// Extra buttons
static const int BTN_3 = 3;
static const int BTN_4 = 4;
static const int BTN_8 = 8;

// Screen (I2C)
static const int PIN_SDA = 9;
static const int PIN_SCL = 10;

// BLE globals
BLECharacteristic* g_char = nullptr;
volatile bool g_deviceConnected = false;
uint32_t g_ledUntilMs = 0;

// Display
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

String g_lastMsg = "-";
long g_enc1Pos = 0;
long g_enc2Pos = 0;
int g_btn1Count = 0;
int g_btn2Count = 0;
int g_btn3Count = 0;
int g_btn4Count = 0;
int g_btn8Count = 0;

// If BTN_8 shares the same GPIO as the onboard LED, disable LED pulses to avoid conflicts.
static const int LED_PIN = LED_BUILTIN;
static const bool LED_ENABLED = false; // BTN_8 != LED_PIN

void displayInit() {
    Wire.begin(PIN_SDA, PIN_SCL);
    u8g2.begin();
    u8g2.setFont(u8g2_font_6x10_tf);
}

void displayDraw() {
    u8g2.clearBuffer();

    u8g2.setCursor(0, 10);
    u8g2.print("BLE: ");
    u8g2.print(g_deviceConnected ? "ON" : "OFF");

    u8g2.setCursor(0, 22);
    u8g2.print("E1:");
    u8g2.print(g_enc1Pos);
    u8g2.print(" E2:");
    u8g2.print(g_enc2Pos);

    u8g2.setCursor(0, 34);
    u8g2.print("F+:");
    u8g2.print(g_btn1Count);
    u8g2.print(" F-:");
    u8g2.print(g_btn2Count);

    u8g2.setCursor(0, 46);
    u8g2.print("B3:");
    u8g2.print(g_btn3Count);
    u8g2.print(" B4:");
    u8g2.print(g_btn4Count);
    u8g2.print(" B8:");
    u8g2.print(g_btn8Count);

    u8g2.setCursor(0, 58);
    u8g2.print("LAST:");
    u8g2.print(g_lastMsg);

    u8g2.sendBuffer();
}

// Helpers
void bleSend(const char* msg) {
    g_lastMsg = msg;
    Serial.print("SEND: ");
    Serial.println(msg);
    if (g_deviceConnected && g_char) {
        g_char->setValue((uint8_t*)msg, strlen(msg));
        g_char->notify();
    }
}

void ledPulse(int ms) {
    if (!LED_ENABLED) return;
    digitalWrite(LED_PIN, HIGH);
    g_ledUntilMs = millis() + ms;
}

// BLE callbacks
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        g_deviceConnected = true;
        Serial.println("BLE: connected");
        bleSend("EVT:BLE:CONN");
    }
    void onDisconnect(BLEServer* pServer) override {
        g_deviceConnected = false;
        Serial.println("BLE: disconnected");
        pServer->getAdvertising()->start();
    }
};

// Encoders
AiEsp32RotaryEncoder enc1(ENC1_CLK, ENC1_DT, -1, -1, 4);
AiEsp32RotaryEncoder enc2(ENC2_S1, ENC2_S2, -1, -1, 4);

OneButton enc1Btn(ENC1_SW, true, true);
OneButton enc2Btn(ENC2_KEY, true, true);
OneButton btnFanUp(BTN_FAN_UP, true, true);
OneButton btnFanDown(BTN_FAN_DOWN, true, true);

OneButton btn3(BTN_3, true, true);
OneButton btn4(BTN_4, true, true);
OneButton btn8(BTN_8, true, true);

void IRAM_ATTR enc1ISR() { enc1.readEncoder_ISR(); }
void IRAM_ATTR enc2ISR() { enc2.readEncoder_ISR(); }

// Callbacks
void onEnc1Click() {
    // climate on/off
    bleSend("EVT:ENC1CLK");
    ledPulse(60);
}

void onEnc1Long() {
    // dual mode
    bleSend("EVT:ENC1LONG");
    ledPulse(60);
}

void onEnc2Click() {
    // rear defrost
    bleSend("EVT:REAR_DEFROST");
    ledPulse(60);
}

void onEnc2Long() {
    // front defrost
    bleSend("EVT:ELECTRIC_DEFROST");
    ledPulse(60);
}

void onFanUp() {
    // fan speed
    g_btn1Count++;
    bleSend("EVT:FAN:+1");
    ledPulse(30);
}

void onFanDown() {
    // fan speed
    g_btn2Count++;
    bleSend("EVT:FAN:-1");
    ledPulse(30);
}

void onBtn3Click() {
    g_btn3Count++;
    bleSend("EVT:BTN3:CLICK");
    ledPulse(30);
}

void onBtn4Click() {
    g_btn4Count++;
    bleSend("EVT:BTN4:CLICK");
    ledPulse(30);
}

void onBtn8Click() {
    g_btn8Count++;
    bleSend("EVT:BTN8:CLICK");
    ledPulse(30);
}

void onBtn3Long() {
    bleSend("EVT:BTN3:LONG");
    ledPulse(60);
}

void onBtn4Long() {
    bleSend("EVT:BTN4:LONG");
    ledPulse(60);
}

void onBtn8Long() {
    bleSend("EVT:BTN8:LONG");
    ledPulse(60);
}

void setup() {
    Serial.begin(115200);
    delay(150);

    if (LED_ENABLED) {
        pinMode(LED_PIN, OUTPUT);
        digitalWrite(LED_PIN, LOW);
    }

    // Display
    displayInit();
    g_lastMsg = "BOOT";
    displayDraw();

    // Encoder1 (KY-040)
    pinMode(ENC1_CLK, INPUT_PULLUP);
    pinMode(ENC1_DT, INPUT_PULLUP);
    pinMode(ENC1_SW, INPUT_PULLUP);
    enc1.begin();
    enc1.setup(enc1ISR);
    enc1.setAcceleration(0);
    attachInterrupt(digitalPinToInterrupt(ENC1_CLK), enc1ISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC1_DT), enc1ISR, CHANGE);

    // Encoder2 (S1=20, S2=21, KEY=2)
    pinMode(ENC2_S1, INPUT_PULLUP);
    pinMode(ENC2_S2, INPUT_PULLUP);
    pinMode(ENC2_KEY, INPUT_PULLUP);

    Serial.print("ENC2 init: S1(20)=");
    Serial.print(digitalRead(ENC2_S1));
    Serial.print(" S2(21)=");
    Serial.println(digitalRead(ENC2_S2));

    enc2.begin();
    enc2.setup(enc2ISR);
    enc2.setAcceleration(0);
    attachInterrupt(digitalPinToInterrupt(ENC2_S1), enc2ISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC2_S2), enc2ISR, CHANGE);

    // Кнопки вентилятора
    pinMode(BTN_FAN_UP, INPUT_PULLUP);
    pinMode(BTN_FAN_DOWN, INPUT_PULLUP);

    // Extra buttons
    pinMode(BTN_3, INPUT_PULLUP);
    pinMode(BTN_4, INPUT_PULLUP);
    pinMode(BTN_8, INPUT_PULLUP);

    enc1Btn.attachClick(onEnc1Click);
    enc1Btn.attachLongPressStart(onEnc1Long);

    enc2Btn.attachClick(onEnc2Click);
    enc2Btn.attachLongPressStart(onEnc2Long);

    enc1Btn.setDebounceMs(5);
    enc2Btn.setDebounceMs(5);

    btnFanUp.attachClick(onFanUp);
    btnFanDown.attachClick(onFanDown);
    btnFanUp.setDebounceMs(5);
    btnFanDown.setDebounceMs(5);

    btn3.attachClick(onBtn3Click);
    btn4.attachClick(onBtn4Click);
    btn8.attachClick(onBtn8Click);

    btn3.attachLongPressStart(onBtn3Long);
    btn4.attachLongPressStart(onBtn4Long);
    btn8.attachLongPressStart(onBtn8Long);

    btn3.setDebounceMs(5);
    btn4.setDebounceMs(5);
    btn8.setDebounceMs(5);

    // BLE
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
    Serial.println("BLE: started");
}

void loop() {
    // Encoder1
    static long enc1Last = 0;
    long p1 = enc1.readEncoder();
    long d1 = p1 - enc1Last;
    if (d1 != 0) {
        Serial.print("ENC1: ");
        Serial.println(d1);
        bleSend(d1 > 0 ? "EVT:TEMP_MAIN:+1" : "EVT:TEMP_MAIN:-1");
        ledPulse(40);
        enc1Last = p1;
    }
    g_enc1Pos = p1;

    // Encoder2
    static long enc2Last = 0;
    long p2 = enc2.readEncoder();
    long d2 = p2 - enc2Last;
    if (d2 != 0) {
        Serial.print("ENC2: ");
        Serial.print(d2);
        Serial.print(" pos=");
        Serial.println(p2);
        bleSend(d2 > 0 ? "EVT:TEMP_PASS:+1" : "EVT:TEMP_PASS:-1");
        ledPulse(40);
        enc2Last = p2;
    }
    g_enc2Pos = p2;

    // Кнопки
    enc1Btn.tick();
    enc2Btn.tick();
    btnFanUp.tick();
    btnFanDown.tick();
    btn3.tick();
    btn4.tick();
    btn8.tick();

    // LED pulse
    if (LED_ENABLED && g_ledUntilMs != 0 && millis() > g_ledUntilMs) {
        g_ledUntilMs = 0;
        digitalWrite(LED_PIN, LOW);
    }

    // Display
    static uint32_t lastUiMs = 0;
    if (millis() - lastUiMs >= 150) {
        lastUiMs = millis();
        displayDraw();
    }

    delay(2);
}
