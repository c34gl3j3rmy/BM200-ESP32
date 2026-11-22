#pragma once

static uint32_t configNonce = 0;

static void configReply(int code, const char* message) {
    web.sendHeader("Cache-Control", "no-store");
    web.send(code, "application/json", String("{\"message\":") + jsonString(String(message)) +
             ",\"pending\":" + (configPending ? "true" : "false") +
             ",\"testing\":" + (configAwaitingNetwork ? "true" : "false") +
             ",\"awaiting_confirmation\":" + (configAwaitingAck ? "true" : "false") + "}");
}
static void sendConfigState() {
    String result;
    result.reserve(320);
    result = String("{\"schema\":1,\"ssid\":") + jsonString(WIFI_STA_SSID) +
        ",\"ap_ssid\":" + jsonString(WIFI_AP_NAME) +
        ",\"ap_password_set\":true,\"mac\":" + jsonString(BM200_BLE_MAC) +
        ",\"diagnostics\":" + String(diagnosticLevel) +
        ",\"pending\":" + (configPending ? "true" : "false") +
        ",\"testing\":" + (configAwaitingNetwork ? "true" : "false") +
        ",\"awaiting_confirmation\":" + (configAwaitingAck ? "true" : "false") +
        ",\"nonce\":" + String(configAwaitingAck ? configNonce : 0) + "}";
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", result);
}
static bool validMac(const String& s) {
    if (s.length() != 17) return false;
    for (size_t i = 0; i < 17; ++i) {
        if (i % 3 == 2) { if (s[i] != ':') return false; }
        else if (!isxdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}
static void requestConfigChange() {
    if (configPending || configAwaitingNetwork || configAwaitingAck) {
        configReply(409, "Une modification est deja en cours"); return;
    }
    // Simple form encoding; no credentials returned by GET/status/logs.
    const String ssid = web.arg("ssid");
    const String staPass = web.arg("password");
    const String apPass = web.arg("ap_password");
    const String mac = web.arg("mac");
    const String diag = web.arg("diagnostics");
    if (ssid.length() > 32 || (ssid.length() && staPass.length() < 8 && (staPass.length() || strcmp(WIFI_STA_SSID, ssid.c_str()) != 0)) ||
        staPass.length() > 63 || apPass.length() > 63 || (apPass.length() && apPass.length() < 8) ||
        (!mac.isEmpty() && !validMac(mac)) ||
        (!diag.isEmpty() && diag != "0" && diag != "1" && diag != "2")) {
        configReply(400, "Parametres invalides: SSID <=32, WPA2 8-63, MAC AA:BB:CC:DD:EE:FF, diag 0-2"); return;
    }
    if (ssid.isEmpty() && !staPass.isEmpty()) {
        configReply(400, "SSID manquant"); return;
    }
    configCopy(stagedSsid, sizeof(stagedSsid), ssid);
    configCopy(stagedPass, sizeof(stagedPass), staPass.isEmpty() && ssid == WIFI_STA_SSID ? String(WIFI_STA_PASSWORD) : staPass);
    configCopy(stagedApPass, sizeof(stagedApPass), apPass.isEmpty() ? String(WIFI_AP_PASSWORD) : apPass);
    configCopy(stagedMac, sizeof(stagedMac), mac.isEmpty() ? String(BM200_BLE_MAC) : mac);
    stagedLevel = diag.isEmpty() ? diagnosticLevel : diag.toInt();
    stagedWifiChanged = ssid != WIFI_STA_SSID || strcmp(stagedPass, WIFI_STA_PASSWORD) != 0;
    stagedApChanged = strcmp(stagedApPass, WIFI_AP_PASSWORD) != 0;
    configStagedAt = millis();
    configPending = true;
    addLog("INFO", "Configuration proposee: maintenir IO0 2 s (expire dans 60 s)");
    configReply(202, "Maintenir IO0 pendant 2 secondes pour autoriser les changements");
}
void applyStagedConfigAfterButton() {
    if (!configPending || millis() - configStagedAt >= 60000) { clearStagedConfig(); return; }
    configPending = false;
    addLog("INFO", "IO0: configuration autorisee physiquement");
    if (!stagedWifiChanged) {
        configCopy(WIFI_AP_PASSWORD, sizeof(WIFI_AP_PASSWORD), stagedApPass);
        configCopy(BM200_BLE_MAC, sizeof(BM200_BLE_MAC), stagedMac);
        diagnosticLevel = stagedLevel;
        if (diagnosticLevel == 2) completeDiagStartedAt = millis();
        if (storeDeviceConfig()) addLog("INFO", "Configuration non-Wi-Fi enregistree");
        else addLog("ERROR", "Ecriture configuration NVS impossible");
        if (stagedApChanged && apActive) {
            apActive = false;
            WiFi.softAPdisconnect(true);
            startFallbackAP();
        }
        clearStagedConfig();
        return;
    }
    configCopy(originalSsid, sizeof(originalSsid), WIFI_STA_SSID);
    configCopy(originalPass, sizeof(originalPass), WIFI_STA_PASSWORD);
    configCopy(WIFI_STA_SSID, sizeof(WIFI_STA_SSID), stagedSsid);
    configCopy(WIFI_STA_PASSWORD, sizeof(WIFI_STA_PASSWORD), stagedPass);
    configAwaitingNetwork = true;
    configNetworkStartedAt = millis();
    if (WIFI_STA_SSID[0]) startHomeSTA("test configuration IO0");
    else { // Explicitly return to AP-only / no home credentials.
        if (!apActive) startFallbackAP();
        configAwaitingNetwork = false;
        configAwaitingAck = true;
        configNonce = esp_random();
        configNetworkStartedAt = millis();
    }
}
static void restoreOldConfig() {
    configCopy(WIFI_STA_SSID, sizeof(WIFI_STA_SSID), originalSsid);
    configCopy(WIFI_STA_PASSWORD, sizeof(WIFI_STA_PASSWORD), originalPass);
    clearStagedConfig();
    addLog("WARN", "Configuration Wi-Fi annulee: retour aux anciens reglages");
    if (WIFI_STA_SSID[0]) startHomeSTA("retour apres test non confirme");
    else { if (!apActive) startFallbackAP(); }
}
static void confirmConfig() {
    if (!configAwaitingAck || strtoul(web.arg("nonce").c_str(), nullptr, 10) != configNonce) {
        configReply(403, "Confirmation absente ou invalide"); return;
    }
    // Keep the last known-good Wi-Fi untouched in NVS until HTTP confirmation.
    configCopy(WIFI_AP_PASSWORD, sizeof(WIFI_AP_PASSWORD), stagedApPass);
    configCopy(BM200_BLE_MAC, sizeof(BM200_BLE_MAC), stagedMac);
    diagnosticLevel = stagedLevel;
    if (diagnosticLevel == 2) completeDiagStartedAt = millis();
    const bool ok = storeDeviceConfig();
    clearStagedConfig();
    configReply(ok ? 200 : 500, ok ? "Configuration enregistree" : "Ecriture NVS impossible");
    addLog(ok ? "INFO" : "ERROR", "Configuration Wi-Fi %s", ok ? "confirmee" : "non sauvegardee");
}
void maintainConfigTransition() {
    if (configPending && millis() - configStagedAt > 60000) {
        clearStagedConfig();
        addLog("WARN", "Configuration expiree: IO0 non confirme");
    }
    if (configAwaitingNetwork) {
        if (WiFi.status() == WL_CONNECTED && !apActive) {
            configAwaitingNetwork = false;
            configAwaitingAck = true;
            configNonce = esp_random() & 0x7fffffff;
            configNetworkStartedAt = millis();
            addLog("INFO", "Wi-Fi test OK: confirmer depuis la PWA sous 120 s");
        } else if (millis() - configNetworkStartedAt > WIFI_STA_BOOT_TIMEOUT_MS + 3000) {
            restoreOldConfig();
        }
    }
    if (configAwaitingAck && millis() - configNetworkStartedAt > 120000) {
        restoreOldConfig();
    }
}
