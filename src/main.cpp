#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <NetWizard.h>
#include <ESPmDNS.h>
#include <NimBLEDevice.h>

#include "Anime.h"
#include "ApiController.h"
#include "SocketController.h"
#include "w_index_html.h"

DNSServer dnsServer;
AsyncWebServer server(80);
NetWizard NW(&server);
// AsyncWiFiManager wm(&server, &dnsServer);

CallResult<void *> status(nullptr);

uint32_t loopTimestampMillis = 0;
uint32_t loopIteration = 0;

static constexpr uint32_t SCAN_TIME_MS = 5000;
static const NimBLEUUID SERVICE_UUID(uint16_t(0x1812));
static const NimBLEUUID CHAR_UUID(uint16_t(0x2a4d));

class MyClientCallbacks : public NimBLEClientCallbacks {
    NimBLEClient* client;

    void onConnect(NimBLEClient* client) override {
        Serial.printf("Connected: %s\n", client->getPeerAddress().toString().c_str());
        client->secureConnection(true);
        this->client = client;
    }

    void onMTUChange(NimBLEClient* pClient, uint16_t MTU) override {
        printf("MTU change requested: %d\n", MTU);
    }

    bool onConnParamsUpdateRequest(NimBLEClient* pClient, const ble_gap_upd_params* params) override {
        printf("update params requested\n");
        return false;
    }

    virtual void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
        Serial.printf("Auth: %d encryption %d\n", connInfo.isBonded(), connInfo.isEncrypted());
        // delay(200);

        // Serial.println("Getting service");
    
        // NimBLERemoteService* service = client->getService("1812");
        // if (!service) {
        //     Serial.println("HID service not found");
        //     client->disconnect();
        //     return;
        // }

        // Serial.println("Printing charas");

        // for (auto chara : service->getCharacteristics()) {
        //     Serial.println(chara->toString().c_str());
        // }
        

        // NimBLERemoteCharacteristic* inputChar = service->getCharacteristic(CHAR_UUID);
        // if (!inputChar || !inputChar->canNotify()) {
        //     Serial.println("Input report characteristic missing or not notifiable");
        //     client->disconnect();
        //     return;
        // }

        // inputChar->subscribe(true, [](NimBLERemoteCharacteristic* c, uint8_t* data, size_t len, bool notify) {
        //     if (len >= 3 && data[2]) {
        //         Serial.printf("Keycode: 0x%02X\n", data[2]);
        //     }
        // });

        // Serial.println("Subscribed to input report notifications");
    }

    void onDisconnect(NimBLEClient* client, int reason) override {
        Serial.printf("Disconnected (%s): reason = %d\n", client->getPeerAddress().toString().c_str(), reason);
        NimBLEDevice::getScan()->start(SCAN_TIME_MS);
    }
} clientCallbacks;


typedef std::function<void(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify)> notify_callback;
notify_callback callback = [](NimBLERemoteCharacteristic* c, uint8_t* data, size_t len, bool notify) {
    Serial.println("Notified!");
    // if (len >= 3 && data[2]) {
    //     Serial.printf("Keycode: 0x%02X\n", data[2]);
    // }
};

class MyScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        if (!dev->isAdvertisingService(SERVICE_UUID)) return;
        if (dev->getName().find("Shutter") == std::string::npos) return;

        Serial.printf("Found device: %s\n", dev->toString().c_str());
        NimBLEDevice::getScan()->stop();

        NimBLEClient* client = NimBLEDevice::getDisconnectedClient();
        if (!client) {
            client = NimBLEDevice::createClient();
            if (!client) {
                Serial.println("Failed to create client");
                return;
            }
            client->setConnectionParams(12, 12, 0, 150);
            client->setConnectTimeout(5 * 1000);
        }

        client->setClientCallbacks(&clientCallbacks, false);
        if (client->connect(dev, true, false, true)) {
            NimBLERemoteService *pService = client->getService(SERVICE_UUID);

            Serial.println("Got service");

            if (pService != nullptr) {
                NimBLERemoteCharacteristic *inputChar = pService->getCharacteristic(CHAR_UUID);

                if (inputChar != nullptr) {
                    std::string value = inputChar->readValue();
                    Serial.printf("Read value: %s \n", value);

                    if (!inputChar->canNotify()) {
                        Serial.println("Input report characteristic not notifiable");
                        return;
                    } else {
                        inputChar->subscribe(true, callback, false);
                        Serial.printf("Subscribed to %s\n", inputChar->toString().c_str());
                    }
                }
            }
        } else {
            Serial.println("Connect failed");
            NimBLEDevice::deleteClient(client);
            NimBLEDevice::getScan()->start(SCAN_TIME_MS);
            return;
        }
    }

    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        Serial.println("Scan ended, restarting...");
        NimBLEDevice::getScan()->start(SCAN_TIME_MS);
    }
} scanCallbacks;

String processor(const String &var);

void setup() {
    delay(500);
    Serial.begin(115200);

    // NimBLEDevice::init("");
    // NimBLEDevice::setSecurityAuth(true, true, true);
    // NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    // NimBLEScan *pScan = NimBLEDevice::getScan();
    // NimBLEScanResults results = pScan->getResults(10 * 1000);

    // NimBLEUUID serviceUuid("1812");

    // for (int i = 0; i < results.getCount(); i++) {
    //     if (!results.getDevice(i)->isAdvertisingService(SERVICE_UUID)) continue;;
    //     if (results.getDevice(i)->getName().find("Shutter") == std::string::npos) continue;
    //     const NimBLEAdvertisedDevice *device = results.getDevice(i);

    //     if (device->isAdvertisingService(serviceUuid)) {
    //         NimBLEClient *pClient = NimBLEDevice::createClient();

    //         if (!pClient) {
    //             break;
    //         }

    //         if (pClient->connect(device)) {
    //             NimBLERemoteService *pService = pClient->getService(SERVICE_UUID);
    //             Serial.println("Got service");

    //             if (pService != nullptr) {
    //                 NimBLERemoteCharacteristic *inputChar = pService->getCharacteristic(CHAR_UUID);

    //                 if (inputChar != nullptr) {
    //                     std::string value = inputChar->readValue();
    //                     Serial.printf("Read value: %s \n", value);

    //                     if (!inputChar->canNotify()) {
    //                         Serial.println("Input report characteristic not notifiable");
    //                         return;
    //                     } else {
    //                         if(!inputChar->subscribe(true, [](NimBLERemoteCharacteristic* c, uint8_t* data, size_t len, bool notify) {
    //                             Serial.println("Notified!");
    //                             if (len >= 3 && data[2]) {
    //                                 Serial.printf("Keycode: 0x%02X\n", data[2]);
    //                             }
    //                         })) {
    //                             Serial.println("Subscribing failed");
    //                         }
    //                     }
    //                 }
    //             }
    //         } else {
    //             // failed to connect
    //         }
    //     }
    // }

    // NimBLEDevice::init("ble-hid-watcher");
    // NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    // NimBLEDevice::setSecurityAuth(true, false, false);
    // NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    // NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    // NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);


    // NimBLEScan* scan = NimBLEDevice::getScan();
    // scan->setScanCallbacks(&scanCallbacks);
    // scan->setInterval(45);
    // scan->setWindow(45);
    // scan->setActiveScan(true);
    // scan->start(SCAN_TIME_MS);
    // Serial.println("Bluetooth started");

    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);

    server.reset();
    
    NW.setStrategy(NetWizardStrategy::NON_BLOCKING);
    NW.autoConnect("LED", "");
    Serial.print("Connected to wifi: ");
    Serial.println(WiFi.localIP());

    if (!MDNS.begin("led")) {
        Serial.println("Error starting mDNS");
        return;
    }
    MDNS.addService("http", "tcp", 80);

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS, PUT, DELETE");

    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            request->send(404);
        }
    });

    server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/plain", "pong");
    });

    server.on("/nuke", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("NUKE TRIGGERED: formatting SPIFFS...");
        delay(100); // allow response to go through
        ShaderStorage::get().nuke();
        request->send(200, "text/plain", "Rebooting and formatting FS...");
    });

    server.on("/health", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P((int)status.getCode(), "text/plain", status.getMessage().c_str());
    });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", index_html, processor);
    });

    auto shaderPost = new AsyncCallbackJsonWebHandler("/api/shader", [](AsyncWebServerRequest *request, JsonVariant &json) {
        ApiController::onAddShader(request, json);
    });
    shaderPost->setMethod(HTTP_POST);
    server.addHandler(shaderPost);

    server.on("^\\/api\\/show\\/([a-zA-Z0-9_-]+)$", HTTP_GET, [](AsyncWebServerRequest *request) {
        String path = request->pathArg(0);
        ApiController::onShow(path, request);
    });

    server.on("^\\/api\\/shader\\/([a-zA-Z0-9_-]+)$", HTTP_GET, [](AsyncWebServerRequest *request) {
        String path = request->pathArg(0);
        ApiController::onGetShader(path, request);
    });

    server.on("^\\/api\\/shader\\/([a-zA-Z0-9_-]+)$", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        String path = request->pathArg(0);
        ApiController::onDeleteShader(path, request);
    });

    server.on("/api/shader", HTTP_GET, [](AsyncWebServerRequest *request) {
        ApiController::onListShaders(request);
    });

    server.on("/api/show", HTTP_GET, [](AsyncWebServerRequest *request) {
        ApiController::onGetShow(request);
    });

    ShaderStorage::init();
    SocketController::bind(server);

    status = Anime::connect();
    while (status.hasError()) {
        Serial.print("Error starting Anime: ");
        Serial.println(status.getMessage());
        delay(1000);
        status = Anime::connect();
    }

    server.begin();
    Serial.println("http://led.local/");
}

void loop() {
    loopTimestampMillis = millis();
    loopIteration++;

    auto pClients = NimBLEDevice::getConnectedClients();
    for (auto& pClient : pClients) {
        if (!pClient->isConnected()) {
            NimBLEDevice::deleteClient(pClient);
        }
    }

    NW.loop();

    SocketController::cleanUp();
    status = Anime::draw();
}

String processor(const String &var) {
    if (var == "SELF_IP") {
        return String(WiFi.localIP().toString());
    }
    return String();
}
