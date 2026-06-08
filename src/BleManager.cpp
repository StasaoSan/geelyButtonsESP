#include "BleManager.h"
#include "AppConfig.h"
#include "Protocol.h"
#include <cstring>
#include <cmath>

class BleRxCallbacks : public BLECharacteristicCallbacks {
public:
    explicit BleRxCallbacks(BleManager* mgr) : mgr(mgr) {}

    void onWrite(BLECharacteristic* ch) override {
        std::string v = ch->getValue();
        if (v.empty()) return;
        size_t n = v.size();
        if (n >= LogCfg::LEN) n = LogCfg::LEN - 1;
        std::memcpy(mgr->rxMsg, v.data(), n);
        mgr->rxMsg[n] = '\0';
        mgr->rxPending = true;
    }

private:
    BleManager* mgr;
};

class BleServerCallbacks : public BLEServerCallbacks {
public:
    explicit BleServerCallbacks(BleManager* mgr) : mgr(mgr) {}

    void onConnect(BLEServer*) override {
        mgr->state->deviceConnected = true;
        mgr->state->needSync        = true;
        mgr->state->displayDirty    = true;
    }

    void onDisconnect(BLEServer* pServer) override {
        mgr->state->deviceConnected = false;
        mgr->state->bleDisconnected = true;
        mgr->state->displayDirty    = true;
        pServer->getAdvertising()->start();
    }

private:
    BleManager* mgr;
};

void BleManager::begin(AppState* s, EventLogger* log) {
    state  = s;
    logger = log;

    BLEDevice::init(Cfg::BLE_NAME);

    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(new BleServerCallbacks(this));

    BLEService* service = server->createService(Cfg::SERVICE_UUID);
    characteristic = service->createCharacteristic(
        Cfg::CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ   |
        BLECharacteristic::PROPERTY_NOTIFY |
        BLECharacteristic::PROPERTY_WRITE  |
        BLECharacteristic::PROPERTY_WRITE_NR
    );
    characteristic->setCallbacks(new BleRxCallbacks(this));
    characteristic->addDescriptor(new BLE2902());
    service->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(Cfg::SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();
}

void BleManager::send(const char* msg) {
    if (state->deviceConnected && characteristic) {
        characteristic->setValue((uint8_t*)msg, strlen(msg));
        characteristic->notify();
    }
}

void BleManager::update() {
    if (state->bleDisconnected) {
        state->bleDisconnected = false;
        state->tempMain = NAN;
        state->tempPass = NAN;
    }

    if (state->needSync && state->deviceConnected) {
        state->needSync = false;
        send("EVT:SYNC");
        logger->push("SYNC->");
    }

    if (rxPending) {
        rxPending = false;
        handleRx(rxMsg);
    }
}

void BleManager::handleRx(const char* msg) {
    Protocol::processRx(msg, *state, *logger);
}
