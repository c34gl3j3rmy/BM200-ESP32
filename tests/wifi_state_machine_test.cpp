// Arduino-independent state-transition tests for WiFiModule.h.
// This uses mocks, not ESP32 drivers or real radio hardware.
#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <string>

using String = std::string;
static uint32_t fakeNow = 0;
static bool buttonPressed = false;
uint32_t millis() { return fakeNow; }
void delay(uint32_t ms) { fakeNow += ms; }
int digitalRead(uint8_t) { return buttonPressed ? 0 : 1; }
void digitalWrite(uint8_t, int) {}
constexpr int LOW = 0, HIGH = 1;
constexpr int WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2;
constexpr int WL_CONNECTED = 3, WL_DISCONNECTED = 6;
using arduino_event_id_t = int;
constexpr int ARDUINO_EVENT_WIFI_AP_STACONNECTED = 1;
constexpr int ARDUINO_EVENT_WIFI_AP_STADISCONNECTED = 2;
constexpr uint8_t WIFI_MODE_BUTTON_PIN = 0;
constexpr uint32_t WIFI_MODE_BUTTON_HOLD_MS = 5000;
constexpr bool STATUS_LED_ENABLED = true;
constexpr uint8_t STATUS_LED_PIN = 2;
constexpr bool STATUS_LED_ACTIVE_HIGH = true;
constexpr uint32_t STATUS_LED_SLOW_MS = 800;
constexpr uint32_t STATUS_LED_FAST_MS = 180;
constexpr uint32_t WIFI_STA_BOOT_TIMEOUT_MS = 20000;
constexpr uint32_t WIFI_FALLBACK_AFTER_MS = 12000;
constexpr uint32_t WIFI_AP_RETRY_INTERVAL_MS = 30000;
constexpr uint32_t WIFI_AP_HOME_RETRY_INTERVAL_MS = 15UL * 60UL * 1000UL;
constexpr uint32_t WIFI_AP_IDLE_BEFORE_RETRY_MS = 2UL * 60UL * 1000UL;
const char* WIFI_HOSTNAME = "test";
const char* WIFI_STA_SSID = "test";
const char* WIFI_STA_PASSWORD = "TEST_PASS";
const char* WIFI_AP_NAME = "TEST_AP";
const char* WIFI_AP_PASSWORD = "TEST_PASS";
struct IPAddress {
    IPAddress(int=0, int=0, int=0, int=0) {}
    String toString() const { return "192.168.4.1"; }
};
struct SerialMock {
    void println(const char*) {}
} Serial;
struct WiFiMock {
    bool allowConnect = false;
    bool ap = false;
    int modeV = WIFI_OFF;
    int staStatus = WL_DISCONNECTED;
    unsigned clientCount = 0;
    unsigned beginCalls = 0;
    unsigned softAPCalls = 0;
    void persistent(bool) {}
    template<class Callback> void onEvent(Callback, int) {}
    void mode(int m) { modeV = m; if (m == WIFI_AP) { staStatus=WL_DISCONNECTED; } else if (m == WIFI_STA) { ap=false; } }
    int getMode() const { return modeV; }
    void setAutoReconnect(bool) {}
    bool disconnect(bool, bool) { staStatus=WL_DISCONNECTED; return true; }
    int status() const { return staStatus; }
    void begin(const char*, const char*) { ++beginCalls; staStatus=allowConnect ? WL_CONNECTED : WL_DISCONNECTED; }
    bool softAPConfig(IPAddress,IPAddress,IPAddress) { return true; }
    bool softAP(const char*,const char*,int,bool,int) { ap=true; ++softAPCalls; return true; }
    bool softAPdisconnect(bool) { ap=false; return true; }
    IPAddress softAPIP() const { return IPAddress(); }
    IPAddress localIP() const { return IPAddress(); }
    unsigned softAPgetStationNum() const { return clientCount; }
    int RSSI() const { return -55; }
} WiFi;
struct WebMock { bool started=false; int starts=0, stops=0; void begin() {started=true; ++starts;} void stop(){started=false; ++stops;} } web;
struct MdnsMock { bool begin(const char*) {return true;} void addService(const char*,const char*,int) {} void end() {} } MDNS;
void ramCheckpoint(const char*) {}
void addLog(const char*,const char*,...) {}

bool apActive = false;
bool httpServerStarted = false;
bool mdnsActive = false;
bool previousSTAConnected = false;
bool staAttemptRunning = false;
bool wifiRecoveringAfterLoss = false;
uint32_t wifiAttemptStartedAt = 0;
uint32_t apStartedAt = 0;
uint32_t apLastClientSeenAt = 0;
uint32_t lastAPAttempt = 0;
bool forcedAPMode = false;
bool io0Pressed = false;
bool io0Handled = false;
uint32_t io0PressedSince = 0;
uint32_t ledLastToggle = 0;
bool ledLogicalOn = false;

#include "../firmware/BM200_ESP32/WiFiModule.h"

void resetState() {
    fakeNow=0; buttonPressed=false; WiFi=WiFiMock(); web=WebMock(); MDNS=MdnsMock();
    apActive=false; httpServerStarted=false; mdnsActive=false;
    previousSTAConnected=false; staAttemptRunning=false; wifiRecoveringAfterLoss=false;
    wifiAttemptStartedAt=apStartedAt=apLastClientSeenAt=lastAPAttempt=0;
    forcedAPMode=io0Pressed=io0Handled=false; io0PressedSince=0;
}

int main() {
    // Working STA path must stay working, including server before BLE.
    resetState(); WiFi.allowConnect=true;
    initWiFi(); ensureNetworkHttp(); maintainWiFi();
    assert(WiFi.getMode()==WIFI_STA && !apActive && httpServerStarted && web.starts==1);
    assert(mdnsActive && WiFi.beginCalls==1);

    // No WLAN at boot: switch completely to AP-only, start HTTP afterward.
    resetState(); initWiFi();
    assert(fakeNow>=WIFI_STA_BOOT_TIMEOUT_MS && apActive && WiFi.getMode()==WIFI_AP);
    assert(!httpServerStarted && WiFi.beginCalls==1);
    ensureNetworkHttp();
    assert(httpServerStarted && web.starts==1);
    WiFi.clientCount=1; fakeNow += WIFI_AP_HOME_RETRY_INTERVAL_MS + 300000;
    maintainWiFi();
    assert(apActive && WiFi.beginCalls==1); // Do not disconnect an AP client.

    // Automatic retry only with no AP client; return to WLAN if available.
    WiFi.clientCount=0; WiFi.allowConnect=true;
    fakeNow += WIFI_AP_IDLE_BEFORE_RETRY_MS+1;
    maintainWiFi(); ensureNetworkHttp(); maintainWiFi();
    assert(!apActive && WiFi.getMode()==WIFI_STA && httpServerStarted);
    assert(WiFi.beginCalls==2);

    // BOOT 5s switches home -> forced AP, then AP -> home.
    resetState(); WiFi.allowConnect=true;
    initWiFi(); ensureNetworkHttp(); maintainWiFi();
    buttonPressed=true; maintainWiFiButton(); fakeNow+=WIFI_MODE_BUTTON_HOLD_MS;
    maintainWiFiButton(); ensureNetworkHttp();
    assert(forcedAPMode && apActive && WiFi.getMode()==WIFI_AP);
    fakeNow+=WIFI_AP_HOME_RETRY_INTERVAL_MS+1000;
    maintainWiFi();
    assert(apActive && WiFi.beginCalls==1); // Force means stay AP.
    buttonPressed=false; maintainWiFiButton();
    buttonPressed=true; maintainWiFiButton(); fakeNow+=WIFI_MODE_BUTTON_HOLD_MS;
    maintainWiFiButton(); ensureNetworkHttp(); maintainWiFi();
    assert(!forcedAPMode && !apActive && WiFi.getMode()==WIFI_STA);
    assert(httpServerStarted && WiFi.beginCalls==2);

    // A dropped established STA link gets 12 seconds to recover, not 20.
    resetState(); WiFi.allowConnect=true;
    initWiFi(); ensureNetworkHttp(); maintainWiFi();
    WiFi.staStatus=WL_DISCONNECTED;
    maintainWiFi();
    assert(wifiRecoveringAfterLoss && !httpServerStarted);
    fakeNow+=WIFI_FALLBACK_AFTER_MS-100;
    maintainWiFi(); assert(!apActive);
    fakeNow+=100;
    maintainWiFi(); ensureNetworkHttp();
    assert(apActive && httpServerStarted && WiFi.getMode()==WIFI_AP);

    std::puts("WiFi state machine mocks: OK (STA, fallback AP, retry, IO0, drop)");
}
