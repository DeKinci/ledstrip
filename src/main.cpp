#include <Arduino.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>

#include "LuaAnimation.h"

#include "Animations.h"
#include "AnimationManager.h"
#include "w_index_html.h"
#include "ShaderStorage.h"
#include "ApiController.h"
#include "Test.h"

#define NUM_LEDS 1
#define LFT 4
#define RGT 2
#define BTN_DEL 500

long left = 0;
long right = 0;

const uint8_t LED_PIN = 32;

AsyncWebServer server(80);
DNSServer dnsServer;

GlobalAnimationEnv *globalAnimationEnv;
ShaderStorage* shaderStorage;
ApiController* apiController;
AnimationManager* anime;

void handleButtons();

void callLeft() {
  left = millis();
}

void callRight() {
  right = millis();
}

String processor(const String& var){
  if(var == "BUTTONPLACEHOLDER") {
    return String();
  }
  return String();
}

uint32_t mls = 0;
uint32_t call = 0;
CallResult<void*> status(nullptr);

void setup() {
  Serial.begin(115200);
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);

  WiFi.mode(WIFI_STA);
  AsyncWiFiManager wm(&server,&dnsServer);    
  wm.autoConnect("Ducky LED");
  Serial.println("Connected to wifi");
  server.reset();

  // pinMode(LFT, INPUT);
  // attachInterrupt(digitalPinToInterrupt(LFT), callLeft, RISING);

  // pinMode(RGT, INPUT);
  // attachInterrupt(digitalPinToInterrupt(RGT), callRight, RISING);

  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", "pong");
  });

  server.on("/health", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P((int) status.getCode(), "text/plain", status.getMessage().c_str());
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });
  
  auto shaderPost = new AsyncCallbackJsonWebHandler("/api/shader", [](AsyncWebServerRequest *request, JsonVariant &json) {
    apiController->onAddShader(request, json);
  });
  shaderPost->setMethod(HTTP_POST);
  server.addHandler(shaderPost);

  server.on("/api/shader", HTTP_GET, [] (AsyncWebServerRequest *request){
    apiController->onListShaders(request);
  });

  server.on("^\\/api\\/shader\\/([a-zA-Z0-9_-]+)$", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String path = request->pathArg(0);
    apiController->onGetShader(path, request);
  });

  server.on("^\\/api\\/shader\\/([a-zA-Z0-9_-]+)$", HTTP_DELETE, [] (AsyncWebServerRequest *request) {
    String path = request->pathArg(0);
    apiController->onDeleteShader(path, request);
  });

  server.begin();

  globalAnimationEnv = new GlobalAnimationEnv();
  shaderStorage = new ShaderStorage();
  anime = new AnimationManager(shaderStorage, globalAnimationEnv);
  apiController = new ApiController(shaderStorage, anime);
  status = anime->connect<LED_PIN>(1);
  while (status.hasError()) {
    Serial.println(status.getMessage());
    delay(1000);
    status = anime->connect<LED_PIN>(1);
  }
}

void loop() {
  mls = millis();
  call++;

  globalAnimationEnv->timeMillis = millis();
  globalAnimationEnv->iteration = call;

  status = anime->draw();
}

void handleButtons() {
  long current = millis();
  if (left != 0 && right != 0)
  {
    if (right > left)
      anime->next();
    if (right < left)
      anime->previous();
    right = 0;
    left = 0;
  }
  if (left != 0 && current - left > BTN_DEL)
  {
    anime->slower();
    right = 0;
    left = 0;
  }
  if (right != 0 && current - right > BTN_DEL)
  {
    anime->faster();
    right = 0;
    left = 0;
  }
}
