#include <Arduino.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <ESPmDNS.h>

#include <FS.h>
#include <SD.h>
#include <SPI.h>

#include "LuaAnimation.h"

#include "Animations.h"
#include "AnimationManager.h"
#include "w_index_html.h"
#include "ShaderStorage.h"
#include "ApiController.h"
#include "SocketController.h"
#include "Test.h"

#define SD_CS 5

#define NUM_LEDS 50
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
SocketController *socket;

void handleButtons();

void callLeft() {
  left = millis();
}

void callRight() {
  right = millis();
}

String processor(const String& var){
  if(var == "SELF_IP") {
    return String(WiFi.localIP().toString());
  }
  return String();
}

uint32_t mls = 0;
uint32_t call = 0;
CallResult<void*> status(nullptr);

void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\n", path);
  File file = fs.open(path, FILE_WRITE);
  if(!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if(file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}


void setup() {
  Serial.begin(115200);
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  AsyncWiFiManager wm(&server,&dnsServer);    
  wm.autoConnect("Ducky LED");
  Serial.println("Connected to wifi");

  if(!MDNS.begin("duckyled")) {
     Serial.println("Error starting mDNS");
     return;
  }

  // SD.begin(SD_CS);  
  // if(!SD.begin(SD_CS)) {
  //   Serial.println("Card Mount Failed");
  //   return;
  // }
  // uint8_t cardType = SD.cardType();
  // if(cardType == CARD_NONE) {
  //   Serial.println("No SD card attached");
  //   return;
  // }
  // Serial.println("Initializing SD card...");
  // if (!SD.begin(SD_CS)) {
  //   Serial.println("ERROR - SD card initialization failed!");
  //   return;    // init failed
  // }

  // // If the data.txt file doesn't exist
  // // Create a file on the SD card and write the data labels
  // File file = SD.open("/data.txt");
  // if(!file) {
  //   Serial.println("File doens't exist");
  //   Serial.println("Creating file...");
  //   writeFile(SD, "/data.txt", "Reading ID, Date, Hour, Temperature \r\n");
  // }
  // else {
  //   Serial.println("File already exists");  
  // }
  // file.close();

  server.reset();

  // pinMode(LFT, INPUT);
  // attachInterrupt(digitalPinToInterrupt(LFT), callLeft, RISING);

  // pinMode(RGT, INPUT);
  // attachInterrupt(digitalPinToInterrupt(RGT), callRight, RISING);
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
  Serial.println("http://duckyled.local/");
}

void loop() {
  socket->cleanUp();
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
