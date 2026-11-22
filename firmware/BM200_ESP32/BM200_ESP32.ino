/*
 * ANCEL BM200 - Standalone BLE to Wi-Fi gateway - version V1.5.0 (PSA Crypto / ESP32 core 4.x).
 * Board: ESP32-WROOM-32 / ESP32 Dev Module (classic ESP32).
 * Arduino ESP32 core 4.0.0 and NimBLE-Arduino 2.5.x.
 *
 * Read-only requests backed by captured protocol evidence: live D1 55 07, history D1 55 05, startup report D1 55 03.
 * No GPS, alarm configuration, charge tests or parameter writes.
 * Raw historical words are stored WITHOUT invented measurement dates.
 *
 * Sources and test vectors are in README.md and tests/.
 */

#include <Arduino.h>
#include <atomic>
#include <esp_system.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "esp32-hal-alloc-ble-mem.h"
#include <NimBLEDevice.h>
#include <sys/time.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>

#include "Config.h"
#include "WifiButtonAction.h"
#include "BM200Protocol.h"


// Temporary V1.3 diagnostic cap; NOT a BM200 capacity or unread-count limit.
constexpr size_t MAX_SLOTS = 709;
constexpr size_t MAX_HISTORY_BYTES = MAX_SLOTS * 4;
constexpr size_t MAX_BLE_PACKET = 192;        // Captures show 16- or 160-byte messages.
constexpr size_t BLE_QUEUE_DEPTH = 48;
constexpr size_t LOG_CAPACITY = 90;
constexpr size_t LOG_LINE_SIZE = 182;
// NimBLE host scan timeout completion code (BLE_HS_ETIMEOUT).
// Keep this local: NimBLE-Arduino does not expose its internal host headers
// through the Arduino sketch include path.
constexpr int BLE_SCAN_COMPLETED_TIMEOUT = 13;

constexpr const char* PATH_SHORT_CSV = "/last-short.csv";
constexpr const char* PATH_SHORT_JSON = "/last-short.json";
constexpr const char* PATH_RAM_DIAG = "/ram-diagnostic.csv";
constexpr const char* PATH_RAM_OLD = "/ram-diagnostic-old.csv";
constexpr const char* PATH_HISTORY = "/history.csv";
constexpr const char* PATH_HISTORY_JSON = "/history.json";
constexpr const char* PATH_PREVIOUS = "/history-prev.csv";
constexpr const char* PATH_PREVIOUS_JSON = "/history-prev.json";
constexpr const char* PATH_HISTORY_TEMP = "/history.tmp.csv";
constexpr const char* PATH_JSON_TEMP = "/history.tmp.json";
constexpr const char* PATH_TRACE = "/ble-trace.csv";
constexpr const char* PATH_TRACE_TEMP = "/ble-trace.tmp.csv";
constexpr const char* PATH_START_TRACE = "/start-trace.csv";
constexpr const char* PATH_START_JSON = "/startup-report.json";

struct BlePacket {
    uint32_t receivedMs;
    uint16_t length;
    uint8_t encrypted[MAX_BLE_PACKET];
};
enum BleOperation : uint8_t { OP_SYNC = 1, OP_LIVE = 2, OP_START = 3 };

struct SyncRequest {
    uint8_t operation;
    uint16_t count;
    char id[25];
};

class ScanCompletion : public NimBLEScanCallbacks {
public:
    static constexpr int Pending = -1;
    std::atomic<int> reason{Pending};

    void onScanEnd(const NimBLEScanResults&, int result) override {
        reason.store(result);
    }
};

ScanCompletion scanCompletion;
bool bleReady = false;
TaskHandle_t bleWorkerTaskHandle = nullptr;
constexpr uint32_t SYNC_MARKER = 0x424D3230;
RTC_NOINIT_ATTR uint32_t interruptedSyncMarker;
RTC_NOINIT_ATTR char interruptedPhase[48];

struct Status {
    bool busy = false;
    bool bleConnected = false;
    bool liveSession = false;
    uint32_t liveAgeMs = 0;
    bool liveValid = false;
    uint16_t voltageCenti = 0;
    uint8_t soc = 0;
    int16_t temperatureC = 0;
    int rssi = -127;
    uint16_t requested = 0;
    uint16_t received = 0;
    bool lastSuccess = false;
    uint16_t lastCount = 0;
    uint32_t lastDurationMs = 0;
    String phase = "Pret pour une synchronisation";
    String error;
    String lastAtUTC;
    String lastLiveUTC;
    bool startBusy = false;
    bool startRead = false;
    bool startAvailable = false;
    bool startNoNewReport = false;
    bool startValid = false;
    uint8_t startResultCode = 0;
    uint16_t startMinVoltageCenti = 0;
    uint16_t startDurationMs = 0;
    uint16_t startAgeSteps = 0;
    uint16_t startSampleCount = 0;
    String startReadUTC;
    String startError;
};

WebServer web(80);
QueueHandle_t blePackets = nullptr;
QueueHandle_t requests = nullptr;
SemaphoreHandle_t statusMutex = nullptr;
SemaphoreHandle_t logsMutex = nullptr;
Status status;
bool fsReady = false;
volatile uint32_t droppedPackets = 0;
char logRing[LOG_CAPACITY][LOG_LINE_SIZE] = {};
size_t logHead = 0;
size_t logCount = 0;
uint8_t historyRaw[MAX_HISTORY_BYTES] = {};
uint16_t startCurve[BM200::START_CURVE_POINTS] = {};
std::atomic<bool> keepLive{false};
uint32_t ramBaseline = 0;
uint32_t ramLast = 0;
uint32_t ramLastLogged = 0;
uint32_t ramMinimum = UINT32_MAX;
uint32_t ramLastCheckpointMs = 0;


// Network management in loop(): do not reboot the BLE stack when Wi-Fi changes.
bool apActive = false;
bool mdnsActive = false;
bool previousSTAConnected = false;
uint32_t wifiDisconnectedSince = 0;
uint32_t lastWiFiRetry = 0;
uint32_t staConnectedSince = 0;
uint32_t lastAPAttempt = 0;
bool forcedAPMode = false;
bool io0Pressed = false;
uint32_t io0PressedSince = 0;
uint32_t ledLastToggle = 0;
bool ledLogicalOn = false;
// Persisted Wi-Fi configuration lives in NVS, separately from BLE/LittleFS history.
Preferences wifiPrefs;
String homeSSID;
String homePassword;
String accessPointSSID;
String accessPointPassword;
uint32_t settingsUnlockedUntil = 0;
uint32_t applyWifiAt = 0;
uint8_t pendingWifiApply = 0; // 1 = home credentials, 2 = fallback AP


// Forward declaration used by WebApiModule.h.
bool initializeBleEngine(const char* origin);

// Internal modules are included after shared state so they keep the same
// single-translation-unit behavior as the validated V1.3.0 firmware.
#include "DiagnosticsModule.h"
#include "BM200Crypto.h"
#include "BLECoreModule.h"
#include "BLEStartModule.h"
#include "BLESyncModule.h"
#include "BLELiveModule.h"
#include "WiFiModule.h"
#include "WebApiModule.h"

bool initializeBleEngine(const char* origin) {
    if (bleReady) {
        addLog("INFO", "BLE: moteur deja initialise (%s)", origin ? origin : "inconnu");
        return true;
    }

    addLog("INFO", "BLE: initialisation demandee (%s)", origin ? origin : "inconnu");
    ramCheckpoint("avant_nimble_init");
    if (!NimBLEDevice::init("BM200-ESP32")) {
        addLog("ERROR", "Initialisation NimBLE impossible (%s)", origin ? origin : "inconnu");
        return false;
    }

    NimBLEDevice::getScan()->setScanCallbacks(&scanCompletion);
    NimBLEDevice::setMTU(247); // Must accommodate the observed 160-byte notifications.

    if (!bleWorkerTaskHandle) {
        if (xTaskCreate(bleWorker, "BM200-BLE", 12288, nullptr, 1, &bleWorkerTaskHandle) != pdPASS) {
            addLog("ERROR", "Impossible de creer la tache BLE");
            NimBLEDevice::deinit(false);
            return false;
        }
    }

    bleReady = true;
    addLog("INFO", "Moteur BLE pret, adresse cible %s", BM200_BLE_MAC);
    ramCheckpoint("nimble_pret");
    return true;
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(250);
    Serial.println();
    Serial.println("==== ANCEL BM200 ESP32 Wi-Fi Gateway V1.5.2 ====");
    statusMutex = xSemaphoreCreateMutex();
    logsMutex = xSemaphoreCreateMutex();
    blePackets = xQueueCreate(BLE_QUEUE_DEPTH, sizeof(BlePacket));
    requests = xQueueCreate(2, sizeof(SyncRequest));
    if (!statusMutex || !logsMutex || !blePackets || !requests) {
        Serial.println("ERREUR CRITIQUE: allocation FreeRTOS impossible");
        while (true) delay(2000);
    }

    const esp_reset_reason_t resetReason = esp_reset_reason();
    const char* resetLabel = "autre";
    switch (resetReason) {
        case ESP_RST_POWERON: resetLabel = "mise sous tension"; break;
        case ESP_RST_SW: resetLabel = "redemarrage logiciel"; break;
        case ESP_RST_PANIC: resetLabel = "erreur systeme"; break;
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT: resetLabel = "watchdog"; break;
        case ESP_RST_BROWNOUT: resetLabel = "chute de tension"; break;
        default: break;
    }
    addLog("INFO", "Demarrage: %s (code %d)", resetLabel, static_cast<int>(resetReason));
    if (resetReason != ESP_RST_POWERON && interruptedSyncMarker == SYNC_MARKER) {
        addLog("WARN", "Synchro interrompue; derniere etape: %.47s", interruptedPhase);
    }
    interruptedSyncMarker = 0;

    // First boot may format an empty LittleFS partition.
    fsReady = LittleFS.begin(true);
    addLog(fsReady ? "INFO" : "ERROR", "LittleFS: %s", fsReady ? "monte" : "echec du montage");
    if (fsReady) {
        LittleFS.remove(PATH_HISTORY_TEMP);
        LittleFS.remove(PATH_JSON_TEMP);
        LittleFS.remove(PATH_TRACE_TEMP);
        addLog("INFO", "Stockage: %lu / %lu octets utilises",
               static_cast<unsigned long>(LittleFS.usedBytes()),
               static_cast<unsigned long>(LittleFS.totalBytes()));
    }

    if (fsReady && !LittleFS.exists(PATH_RAM_DIAG)) {
        File f=LittleFS.open(PATH_RAM_DIAG,"w");
        if (f) { f.println("uptime_ms,utc,phase,free_bytes,min_bytes,largest_block_bytes,delta_bytes"); f.close(); }
    }
    ramCheckpoint("boot_fs");
    restoreSavedHistory();

    pinMode(WIFI_MODE_BUTTON_PIN, INPUT_PULLUP);
    if (STATUS_LED_ENABLED) {
        pinMode(STATUS_LED_PIN, OUTPUT);
        digitalWrite(STATUS_LED_PIN, STATUS_LED_ACTIVE_HIGH ? LOW : HIGH);
    }

    ramCheckpoint("avant_wifi");
    initStoredWiFi();
    initWiFi();
    ramCheckpoint("apres_wifi_init");

    ramCheckpoint("avant_serveur_web");
    registerRoutes();
    web.begin();
    addLog("INFO", "Serveur Web disponible sur port 80");
    ramCheckpoint("serveur_web_actif");

    if (!initBm200Crypto()) {
        addLog("ERROR", "Chiffrement PSA AES indisponible ; BLE non demarre");
        return;
    }
    addLog("INFO", "Chiffrement PSA AES-128 pret");

    if (!initializeBleEngine("demarrage")) {
        addLog("ERROR", "BLE indisponible au demarrage; utiliser 'Forcer reconnexion' apres correction/reessai");
    }
}

void loop() {
    maintainWiFiButton();
    applyPendingWiFi();
    maintainWiFi();
    maintainStatusLed();
    web.handleClient();
    delay(5);
}
