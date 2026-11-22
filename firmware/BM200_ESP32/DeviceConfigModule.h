#pragma once
#include <Preferences.h>

// Active settings are committed ONLY after physical IO0 and a successful network test.
// Firmware survives reboot without a PWA or an external secrets header.
static char WIFI_STA_SSID[33] = {};
static char WIFI_STA_PASSWORD[65] = {};
static char WIFI_AP_PASSWORD[65] = "BM200-Setup"; // Public bootstrap password (not a secret).
static char BM200_BLE_MAC[18] = "C8:17:F5:29:4E:F8";
static uint8_t diagnosticLevel = 0; // 0=normal, 1=diagnostic, 2=complete
static uint32_t completeDiagStartedAt = 0;
static bool configPending = false;
static bool configAwaitingNetwork = false;
static bool configAwaitingAck = false;
static uint32_t configStagedAt = 0;
static uint32_t configNetworkStartedAt = 0;
static char stagedSsid[33] = {};
static char stagedPass[65] = {};
static char stagedApPass[65] = {};
static char stagedMac[18] = {};
static uint8_t stagedLevel = 0;
static bool stagedWifiChanged = false;
static bool stagedApChanged = false;
static char originalSsid[33] = {};
static char originalPass[65] = {};

static void configCopy(char* dst, size_t len, const String& src) {
    snprintf(dst, len, "%s", src.c_str());
}
static void loadDeviceConfig() {
    Preferences pref;
    if (!pref.begin("bm200", true)) return;
    configCopy(WIFI_STA_SSID, sizeof(WIFI_STA_SSID), pref.getString("ssid", ""));
    configCopy(WIFI_STA_PASSWORD, sizeof(WIFI_STA_PASSWORD), pref.getString("pass", ""));
    configCopy(WIFI_AP_PASSWORD, sizeof(WIFI_AP_PASSWORD), pref.getString("ap_pass", "BM200-Setup"));
    configCopy(BM200_BLE_MAC, sizeof(BM200_BLE_MAC), pref.getString("mac", "C8:17:F5:29:4E:F8"));
    diagnosticLevel = min(static_cast<uint8_t>(2), pref.getUChar("diag", 0));
    if (diagnosticLevel == 2) completeDiagStartedAt = millis();
    pref.end();
}
static bool storeDeviceConfig() {
    Preferences pref;
    if (!pref.begin("bm200", false)) return false;
    const bool ok = pref.putString("ssid", WIFI_STA_SSID) > 0 || WIFI_STA_SSID[0] == '\0';
    const bool passOk = pref.putString("pass", WIFI_STA_PASSWORD) > 0 || WIFI_STA_PASSWORD[0] == '\0';
    const bool apOk = pref.putString("ap_pass", WIFI_AP_PASSWORD) > 0;
    const bool macOk = pref.putString("mac", BM200_BLE_MAC) > 0;
    const bool diagOk = pref.putUChar("diag", diagnosticLevel) == 1;
    pref.end();
    return ok && passOk && apOk && macOk && diagOk;
}
static void clearStagedConfig() {
    configPending = false;
    configAwaitingNetwork = false;
    configAwaitingAck = false;
    memset(stagedPass, 0, sizeof(stagedPass));
    memset(originalPass, 0, sizeof(originalPass));
}

static void maintainDiagnosticsTimeout() {
    if (diagnosticLevel == 2 && millis() - completeDiagStartedAt >= 15UL * 60UL * 1000UL) {
        diagnosticLevel = 0;
        Preferences pref;
        if (pref.begin("bm200", false)) { pref.putUChar("diag", 0); pref.end(); }
        addLog("INFO", "Diagnostics complets termines: retour automatique au mode Normal");
    }
}
