#include "BleClient.hpp"
#include "BleButton.hpp"

namespace {

static constexpr uint32_t SCAN_TIME_MS = 30000;
static const NimBLEUUID SERVICE_UUID(uint16_t(0x1812));

NimBLEAddress foundAddr;
bool shouldConnect = false;

const char* getIconNameFromAppearance(uint16_t appearance) {
    switch (appearance) {
        case 0x03C0:
            return "camera";
        case 0x03C1:
            return "media";
        case 0x03C2:
            return "gamepad";
        case 0x03C3:
            return "keyboard";
        case 0x0340:
            return "heart";
        case 0x0180:
            return "phone";
        case 0x0140:
            return "watch";
        case 0x0100:
            return "computer";
        case 0x00C0:
            return "headset";
        default:
            return "generic";
    }
}

class MyScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        const std::string& name = dev->getName();
        const char* icon = getIconNameFromAppearance(dev->getAppearance());

        if (!name.empty()) {
            Serial.printf("[%s] %s\n", icon, name.c_str());
        } else {
            Serial.printf("[%s] %s\n", icon, dev->getAddress().toString().c_str());
        }

        if (!dev->isAdvertisingService(SERVICE_UUID)) return;
        if (name.find("Shutter") == std::string::npos) return;

        NimBLEDevice::getScan()->stop();
        foundAddr = dev->getAddress();
        shouldConnect = true;
    }

    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        Serial.println("Scan ended, restarting...");
        NimBLEDevice::getScan()->start(SCAN_TIME_MS, true, true);
    }
} scanCallbacks;

BleButton* button1 = nullptr;

}  // namespace

namespace BleClient {

void init() {
    NimBLEDevice::init("bl-watcher");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    Serial.println("Bluetooth initialized");
}

void scan() {
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&scanCallbacks);
    scan->setInterval(80);
    scan->setWindow(40);
    scan->setActiveScan(true);
    scan->setDuplicateFilter(1);
    scan->start(SCAN_TIME_MS, false, true);
    Serial.println("Bluetooth scan started");
}

void stopScan() {
    NimBLEDevice::getScan()->stop();
}

void loop() {
    if (shouldConnect) {
        shouldConnect = false;
        if (button1) {
            delete button1;
        }
        button1 = new BleButton(foundAddr);
    }
    if (button1) {
        button1->loop();
    }

    for (auto& pClient : NimBLEDevice::getConnectedClients()) {
        if (!pClient->isConnected()) {
            NimBLEDevice::deleteClient(pClient);
        }
    }
}

}  // namespace BleClient