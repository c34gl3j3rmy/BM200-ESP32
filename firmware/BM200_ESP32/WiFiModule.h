#pragma once

// Credentials are stored in the ESP32 NVS namespace "bm200wifi".
// A blank AP password means an open point of access, by user choice.
void initStoredWiFi() {
    wifiPrefs.begin("bm200wifi", false);
    homeSSID = wifiPrefs.getString("home_ssid", "");
    homePassword = wifiPrefs.getString("home_pass", "");
    accessPointSSID = wifiPrefs.getString("ap_ssid", WIFI_AP_NAME);
    // Never store a default password in the firmware. First setup: open AP.
    accessPointPassword = wifiPrefs.getString("ap_pass", "");
    if (!accessPointSSID.length()) accessPointSSID = WIFI_AP_NAME;
}

bool wifiSettingsUnlocked() {
    return settingsUnlockedUntil && static_cast<int32_t>(settingsUnlockedUntil - millis()) > 0;
}

void unlockWifiSettings() {
    settingsUnlockedUntil = millis() + WIFI_SETTINGS_WINDOW_MS;
    addLog("INFO", "Configuration Wi-Fi autorisee pendant 10 minutes (bouton BOOT)");
}

void startFallbackAP();

void restartAccessPoint() {
    if (apActive) {
        WiFi.softAPdisconnect(true);
        apActive = false;
    }
    startFallbackAP();
}

void applyPendingWiFi() {
    if (!pendingWifiApply || static_cast<int32_t>(millis() - applyWifiAt) < 0) return;
    uint8_t mode = pendingWifiApply;
    pendingWifiApply = 0;
    if (mode == 2) {
        restartAccessPoint();
    } else if (mode == 1) {
        if (mdnsActive) { MDNS.end(); mdnsActive = false; }
        WiFi.disconnect(false, false);
        previousSTAConnected = false;
        wifiDisconnectedSince = millis();
        lastWiFiRetry = millis();
        if (!forcedAPMode && homeSSID.length()) {
            WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
            WiFi.setAutoReconnect(true);
            WiFi.begin(homeSSID.c_str(), homePassword.c_str());
        }
        startFallbackAP(); // Keep a recovery path if the new home password is wrong.
    }
}

void startFallbackAP() {
    if (apActive) return;
    // Pure AP mode during initial setup or forced fallback: do not scan STA.
    WiFi.mode((forcedAPMode || !homeSSID.length()) ? WIFI_AP : WIFI_AP_STA);
    const IPAddress ip(192, 168, 4, 1);
    if (!WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0))) {
        addLog("WARN", "Configuration IP du point d'acces de secours refusee");
    }
    ramCheckpoint("avant_wifi_secours");
    if (WiFi.softAP(accessPointSSID.c_str(), accessPointPassword.length() ? accessPointPassword.c_str() : nullptr, 1, false, 2)) {
        apActive = true;
        ramCheckpoint("wifi_secours_actif");
        addLog("INFO", "Secours Wi-Fi actif: SSID %s, URL http://%s", accessPointSSID.c_str(),
               WiFi.softAPIP().toString().c_str());
    } else {
        addLog("ERROR", "Impossible de demarrer le point d'acces de secours");
    }
}

void stopFallbackAP() {
    if (!apActive) return;
    if (!WiFi.softAPdisconnect(true)) {
        addLog("WARN", "Arret du point d'acces de secours refuse");
        return;
    }
    apActive = false;
    addLog("INFO", "Secours Wi-Fi arrete: acces via le reseau domestique");
}


void setStatusLed(bool on) {
    if (!STATUS_LED_ENABLED) return;
    ledLogicalOn = on;
    digitalWrite(STATUS_LED_PIN, (on == STATUS_LED_ACTIVE_HIGH) ? HIGH : LOW);
}

void enterForcedAPMode() {
    if (forcedAPMode) return;
    forcedAPMode = true;
    WiFi.setAutoReconnect(false);
    if (mdnsActive) { MDNS.end(); mdnsActive = false; }
    WiFi.disconnect(false, false);
    previousSTAConnected = false;
    startFallbackAP();
    addLog("INFO", "IO0: mode secours force active; ouvrir http://192.168.4.1");
    ramCheckpoint("wifi_secours_force");
}

void leaveForcedAPMode() {
    if (!forcedAPMode) return;
    forcedAPMode = false;
    WiFi.setAutoReconnect(homeSSID.length() > 0);
    if (homeSSID.length()) {
        WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
        wifiDisconnectedSince = millis();
        lastWiFiRetry = millis();
        WiFi.begin(homeSSID.c_str(), homePassword.c_str());
        addLog("INFO", "IO0: retour au mode Wi-Fi maison automatique");
    } else {
        startFallbackAP();
        addLog("INFO", "IO0: aucun Wi-Fi maison configure; maintien du point d'acces direct");
    }
    ramCheckpoint("wifi_retour_maison");
}

void maintainWiFiButton() {
    const uint32_t now = millis();
    const bool pressed = digitalRead(WIFI_MODE_BUTTON_PIN) == LOW;
    if (pressed && !io0Pressed) {
        io0Pressed = true;
        io0PressedSince = now;
        return;
    }
    if (pressed || !io0Pressed) return;

    // Act only on release so a 60-second hold cannot accidentally clear Wi-Fi.
    io0Pressed = false;
    const uint32_t held = now - io0PressedSince; // unsigned, safe across millis wrap.
    switch (wifiBootActionForHold(held, WIFI_MODE_BUTTON_HOLD_MS,
                                  WIFI_AP_RESET_HOLD_MS, WIFI_BOOT_IGNORE_HOLD_MS)) {
        case WifiBootAction::ToggleAP:
            unlockWifiSettings();
            if (forcedAPMode) leaveForcedAPMode();
            else enterForcedAPMode();
            addLog("INFO", "BOOT relache apres %lu ms: changement de mode Wi-Fi",
                   static_cast<unsigned long>(held));
            break;
        case WifiBootAction::ClearAPPassword:
            // Only NVS AP password changes: home Wi-Fi and LittleFS remain intact.
            wifiPrefs.remove("ap_pass");
            accessPointPassword = "";
            unlockWifiSettings();
            if (!forcedAPMode) enterForcedAPMode();
            pendingWifiApply = 2;
            applyWifiAt = now + 300;
            addLog("WARN", "BOOT relache entre 30 et 59 s: Wi-Fi direct remis en mode ouvert");
            break;
        case WifiBootAction::None:
            if (held >= WIFI_BOOT_IGNORE_HOLD_MS)
                addLog("INFO", "BOOT >= 60 s: aucune action (securite)");
            break;
    }
}

void maintainStatusLed() {
    if (!STATUS_LED_ENABLED) return;
    const uint32_t now = millis();
    if (forcedAPMode || (apActive && WiFi.status() != WL_CONNECTED)) {
        if (now - ledLastToggle >= STATUS_LED_SLOW_MS) {
            ledLastToggle = now;
            setStatusLed(!ledLogicalOn);
        }
        return;
    }
    if (WiFi.status() == WL_CONNECTED) {
        setStatusLed(true);
        return;
    }
    if (now - ledLastToggle >= STATUS_LED_FAST_MS) {
        ledLastToggle = now;
        setStatusLed(!ledLogicalOn);
    }
}

void initWiFi() {
    WiFi.persistent(false); // Avoid storing credentials in Wi-Fi NVS.
    // Espressif requires setHostname before WiFi.mode() or WiFi.begin().
    if (!WiFi.setHostname(WIFI_HOSTNAME))
        addLog("WARN", "Nom d'hote Wi-Fi refuse; l'adresse IP reste utilisable");
    WiFi.onEvent([](arduino_event_id_t) {
        addLog("INFO", "Wi-Fi secours: client connecte, %u client(s) connecte(s)",
               static_cast<unsigned>(WiFi.softAPgetStationNum()));
    }, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
    WiFi.onEvent([](arduino_event_id_t) {
        addLog("INFO", "Wi-Fi secours: client deconnecte, %u client(s) connecte(s)",
               static_cast<unsigned>(WiFi.softAPgetStationNum()));
    }, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);
    if (!homeSSID.length()) {
        WiFi.mode(WIFI_AP);
        WiFi.setSleep(false); // Keep the V1 BLE/Wi-Fi coexistence behavior.
        WiFi.setAutoReconnect(false);
        addLog("INFO", "Premier demarrage: aucun Wi-Fi maison enregistre");
        startFallbackAP();
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    wifiDisconnectedSince = millis();
    lastWiFiRetry = millis();
    WiFi.begin(homeSSID.c_str(), homePassword.c_str());
    addLog("INFO", "Connexion Wi-Fi domestique en cours: %s (2,4 GHz)", homeSSID.c_str());
    addLog("INFO", "En absence de Wi-Fi domestique, secours %s apres %lu s", accessPointSSID.c_str(),
           static_cast<unsigned long>(WIFI_FALLBACK_AFTER_MS / 1000));
}

void maintainWiFi() {
    const uint32_t now = millis();
    if (forcedAPMode || !homeSSID.length()) {
        if (!apActive) startFallbackAP();
        return; // Never reconnect without a configured home SSID.
    }
    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected != previousSTAConnected) {
        previousSTAConnected = connected;
        if (connected) {
            staConnectedSince = now;
            ramCheckpoint("wifi_maison_connecte");
            const String ip = WiFi.localIP().toString();
            addLog("INFO", "Wi-Fi %s connecte, adresse http://%s, RSSI %ld dBm",
                   homeSSID.c_str(), ip.c_str(), static_cast<long>(WiFi.RSSI()));
            if (MDNS.begin(WIFI_HOSTNAME)) {
                MDNS.addService("http", "tcp", 80);
                mdnsActive = true;
                addLog("INFO", "mDNS disponible (selon appareils): http://%s.local", WIFI_HOSTNAME);
            } else {
                addLog("WARN", "mDNS indisponible; utiliser l'adresse IP du routeur");
            }
        } else {
            if (mdnsActive) { MDNS.end(); mdnsActive = false; }
            wifiDisconnectedSince = now;
            ramCheckpoint("wifi_maison_deconnecte");
            addLog("WARN", "Connexion au Wi-Fi domestique perdue; reconnexion automatique");
        }
    }
    if (connected) {
        if (apActive && now - staConnectedSince >= WIFI_AP_CLOSE_DELAY_MS &&
            WiFi.softAPgetStationNum() == 0) {
            stopFallbackAP(); // Never interrupt an active fallback client.
        }
        return;
    }
    if (!apActive && now - wifiDisconnectedSince >= WIFI_FALLBACK_AFTER_MS &&
        (lastAPAttempt == 0 || now - lastAPAttempt >= WIFI_AP_RETRY_INTERVAL_MS)) {
        lastAPAttempt = now;
        startFallbackAP();
    }
    if (now - lastWiFiRetry >= WIFI_RETRY_INTERVAL_MS) {
        lastWiFiRetry = now;
        addLog("DEBUG", "Nouvelle tentative de connexion au Wi-Fi %s", homeSSID.c_str());
        WiFi.reconnect();
    }
}

// HTTP helpers. Accessible on the home LAN or on the fallback AP.
