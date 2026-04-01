// Test firmware for Matter protocol integration tests.
// Runs full Matter stack (UDP:5540) + HTTP debug API (port 80).
//
// Test clusters: OnOff (endpoint 1), LevelControl (endpoint 1)
// Uses test passcode 20202021, discriminator 0xF00
//
// Debug API exposes full session state for test verification:
//   GET /ping                  -> "pong"
//   GET /debug/state           -> cluster values + session state
//   GET /debug/session         -> detailed session info
//   GET /debug/matter-port     -> "5540"
//   POST /debug/reset          -> reset cluster state (session untouched)
//   POST /debug/reset-session  -> full session reset (re-open commissioning)

#include <Arduino.h>
#include <WiFi.h>
#include <HttpServer.h>
#include <WiFiMan.h>
#include <MicroLog.h>
#include <ResponseBuffer.h>
#include <Property.h>
#include "MatterTransport.h"
#include "MatterSession.h"

static const char* TAG = "TestMatter";

static const char* sessionStateName(matter::MatterSession::State s) {
    switch (s) {
        case matter::MatterSession::IDLE:               return "IDLE";
        case matter::MatterSession::PASE_WAIT_PBKDF_REQ:return "PASE_WAIT_PBKDF_REQ";
        case matter::MatterSession::PASE_WAIT_PAKE1:    return "PASE_WAIT_PAKE1";
        case matter::MatterSession::PASE_WAIT_PAKE3:    return "PASE_WAIT_PAKE3";
        case matter::MatterSession::PASE_ACTIVE:        return "PASE_ACTIVE";
        case matter::MatterSession::CASE_WAIT_SIGMA1:   return "CASE_WAIT_SIGMA1";
        case matter::MatterSession::CASE_WAIT_SIGMA3:   return "CASE_WAIT_SIGMA3";
        case matter::MatterSession::CASE_ACTIVE:        return "CASE_ACTIVE";
        default: return "UNKNOWN";
    }
}

// MicroProto properties for Matter cluster binding
static MicroProto::Property<bool> propOnOff(
    "onOff", false, MicroProto::PropertyLevel::LOCAL);
static MicroProto::Property<uint8_t> propBrightness(
    "brightness", 128, MicroProto::PropertyLevel::LOCAL,
    MicroProto::Constraints<uint8_t>().min(0).max(255));

// Matter transport (owns session, IM, clusters internally)
static matter::MatterTransport matterTransport;

// HTTP debug server
static HttpServer http(80);
static WiFiMan::WiFiManager wifiManager(&http.dispatcher());

// Helper
static HttpResponse jsonDebug(ResponseBuffer& buf, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char* start = buf.writePtr();
    int len = vsnprintf(start, buf.remaining(), fmt, args);
    va_end(args);
    if (len > 0) buf.advance(len);
    return HttpResponse::json(start, (size_t)(len > 0 ? len : 0), 200);
}

void setup() {
    Serial.begin(115200);
    Serial.println("test_matter: starting");

    MicroLog::Logger::init();

    // Map MicroProto properties to Matter clusters
    matterTransport.mapOnOff(propOnOff);
    matterTransport.mapLevel(propBrightness);
    matterTransport.setDeviceName("MatterTest");
    matterTransport.setVendorName("TestVendor");

    // Full debug API — cluster state + session state
    http.dispatcher().onGet("/debug/state", [](HttpRequest& req, ResponseBuffer& buf) {
        auto* sess = matterTransport.session();
        return jsonDebug(buf,
            "{\"onOff\":%s,\"brightness\":%d,"
            "\"sessionState\":\"%s\",\"sessionSecure\":%s,"
            "\"commissioned\":%s,\"localSessionId\":%d,\"peerSessionId\":%d}",
            propOnOff.get() ? "true" : "false",
            (int)propBrightness.get(),
            sess ? sessionStateName(sess->state()) : "NO_SESSION",
            sess && sess->isSecure() ? "true" : "false",
            sess && sess->isCommissioned() ? "true" : "false",
            sess ? sess->localSessionId() : 0,
            sess ? sess->peerSessionId() : 0);
    });

    http.dispatcher().onGet("/debug/session", [](HttpRequest& req, ResponseBuffer& buf) {
        auto* sess = matterTransport.session();
        if (!sess) return HttpResponse::json("{\"error\":\"no session\"}");
        return jsonDebug(buf,
            "{\"state\":\"%s\",\"secure\":%s,\"commissioned\":%s,"
            "\"localSessionId\":%d,\"peerSessionId\":%d,\"nodeId\":%lu}",
            sessionStateName(sess->state()),
            sess->isSecure() ? "true" : "false",
            sess->isCommissioned() ? "true" : "false",
            sess->localSessionId(),
            sess->peerSessionId(),
            (unsigned long)sess->nodeId());
    });

    http.dispatcher().onGet("/debug/matter-port", [](HttpRequest& req, ResponseBuffer&) {
        return HttpResponse::text("5540");
    });

    http.dispatcher().onPost("/debug/reset", [](HttpRequest& req, ResponseBuffer&) {
        propOnOff.set(false);
        propBrightness.set(128);
        return HttpResponse::json("{\"success\":true}");
    });

    http.dispatcher().onPost("/debug/reset-session", [](HttpRequest& req, ResponseBuffer&) {
        matterTransport.resetCommissioning();
        propOnOff.set(false);
        propBrightness.set(128);
        LOG_INFO(TAG, "Full commissioning reset (session + MRP + peer)");
        return HttpResponse::json("{\"success\":true}");
    });

    wifiManager.begin();
    http.begin();
    LOG_INFO(TAG, "Waiting for WiFi...");
}

static bool matterStarted = false;

void loop() {
    wifiManager.loop();
    http.loop();

    if (!matterStarted && wifiManager.isConnected()) {
        if (matterTransport.begin()) {
            matterStarted = true;
            LOG_INFO(TAG, "Matter started on port 5540");
        }
    }

    if (matterStarted) {
        matterTransport.loop();
    }

    MicroLog::Logger::loop();
}
