#include <Arduino.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <ESPmDNS.h>

#include "AnimationManager.h"
#include "w_index_html.h"
#include "ShaderStorage.h"
#include "ApiController.h"
#include "SocketController.h"

#define NUM_LEDS 50
#define LEFT_BTN_PIN -1
#define RIGHT_BTN_PIN -1
#define BTN_DELAY 500

const uint8_t LED_PIN = 32;

DNSServer dnsServer;
AsyncWebServer server(80);

GlobalAnimationEnv *globalAnimationEnv;
ShaderStorage* shaderStorage;
AnimationManager* anime;
ApiController* apiController;
SocketController *socket;

CallResult<void*> status(nullptr);

uint32_t loopTimestampMillis = 0;
uint32_t loopIteration = 0;

long leftClickMillis = 0;
long rightClickMillis = 0;

void handleButtons();
void onLeftClick();
void onRightClick();

String processor(const String& var);

void setup() {
  Serial.begin(115200);
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  AsyncWiFiManager wm(&server,&dnsServer);    
  wm.autoConnect("LED");
  Serial.println("Connected to wifi");

  if(!MDNS.begin("led")) {
     Serial.println("Error starting mDNS");
     return;
  }
  MDNS.addService("http", "tcp", 80);

  server.reset();

  if (LEFT_BTN_PIN >= 0) {
    pinMode(LEFT_BTN_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(LEFT_BTN_PIN), onLeftClick, RISING);
  }

  if (RIGHT_BTN_PIN >= 0) {
    pinMode(RIGHT_BTN_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(RIGHT_BTN_PIN), onRightClick, RISING);
  }

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

  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", "pong");
  });

  server.on("/health", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P((int) status.getCode(), "text/plain", status.getMessage().c_str());
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/css", style_css);
  });
  
  auto shaderPost = new AsyncCallbackJsonWebHandler("/api/shader", [](AsyncWebServerRequest *request, JsonVariant &json) {
    apiController->onAddShader(request, json);
  });
  shaderPost->setMethod(HTTP_POST);
  server.addHandler(shaderPost);

  server.on("^\\/api\\/show\\/([a-zA-Z0-9_-]+)$", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String path = request->pathArg(0);
    apiController->onShow(path, request);
  });

  server.on("^\\/api\\/shader\\/([a-zA-Z0-9_-]+)$", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String path = request->pathArg(0);
    apiController->onGetShader(path, request);
  });

  server.on("^\\/api\\/shader\\/([a-zA-Z0-9_-]+)$", HTTP_DELETE, [] (AsyncWebServerRequest *request) {
    String path = request->pathArg(0);
    apiController->onDeleteShader(path, request);
  });

  server.on("/api/shader", HTTP_GET, [] (AsyncWebServerRequest *request){
    apiController->onListShaders(request);
  });

  server.on("/api/show", HTTP_GET, [] (AsyncWebServerRequest *request){
    apiController->onGetShow(request);
  });

  globalAnimationEnv = new GlobalAnimationEnv();
  shaderStorage = new ShaderStorage();
  anime = new AnimationManager(shaderStorage, globalAnimationEnv);
  socket = new SocketController(anime);
  socket->bind(server);

  anime->setListener(socket);
  shaderStorage->setListener(socket);

  apiController = new ApiController(shaderStorage, anime);

  status = anime->connect<LED_PIN>(NUM_LEDS);
  while (status.hasError()) {
    Serial.println(status.getMessage());
    delay(1000);
    status = anime->connect<LED_PIN>(NUM_LEDS);
  }

  server.begin();
  Serial.println("http://led.local/");
}

void loop() {
  loopTimestampMillis = millis();
  loopIteration++;

  socket->cleanUp();

  globalAnimationEnv->timeMillis = loopTimestampMillis;
  globalAnimationEnv->iteration = loopIteration;

  status = anime->draw();
  handleButtons();
}

void handleButtons() {
  long current = loopTimestampMillis;
  if (leftClickMillis != 0 && rightClickMillis != 0)
  {
    if (rightClickMillis > leftClickMillis)
      anime->next();
    if (rightClickMillis < leftClickMillis)
      anime->previous();
    rightClickMillis = 0;
    leftClickMillis = 0;
  }
  if (leftClickMillis != 0 && current - leftClickMillis > BTN_DELAY)
  {
    anime->slower();
    rightClickMillis = 0;
    leftClickMillis = 0;
  }
  if (rightClickMillis != 0 && current - rightClickMillis > BTN_DELAY)
  {
    anime->faster();
    rightClickMillis = 0;
    leftClickMillis = 0;
  }
}

void onLeftClick() {
  leftClickMillis = millis();
}

void onRightClick() {
  rightClickMillis = millis();
}

String processor(const String& var){
  if(var == "SELF_IP") {
    return String(WiFi.localIP().toString());
  }
  return String();
}
