#pragma once

void startFallbackAP() {
    if (apActive) return;
    WiFi.mode(WIFI_AP_STA); // Keep retrying the home network in parallel.
    const IPAddress ip(192, 168, 4, 1);
    if (!WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0))) {
        addLog("WARN", "Configuration IP du point d'acces de secours refusee");
    }
    ramCheckpoint("avant_wifi_secours");
    if (WiFi.softAP(WIFI_AP_NAME, WIFI_AP_PASSWORD, 1, false, 2)) {
        apActive = true;
        ramCheckpoint("wifi_secours_actif");
        addLog("INFO", "Secours Wi-Fi actif: SSID %s, URL http://%s", WIFI_AP_NAME,
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
    WiFi.setAutoReconnect(true);
    WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
    wifiDisconnectedSince = millis();
    lastWiFiRetry = millis();
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);
    addLog("INFO", "IO0: retour au mode Wi-Fi maison automatique");
    ramCheckpoint("wifi_retour_maison");
}

void maintainWiFiButton() {
    const uint32_t now = millis();
    const bool pressed = digitalRead(WIFI_MODE_BUTTON_PIN) == LOW;
    if (pressed && !io0Pressed) {
        io0Pressed = true;
        io0Handled = false;
        io0PressedSince = now;
    }
    if (pressed && !io0Handled && now - io0PressedSince >= WIFI_MODE_BUTTON_HOLD_MS) {
        io0Handled = true;
        if (forcedAPMode) leaveForcedAPMode();
        else enterForcedAPMode();
    }
    if (!pressed && io0Pressed) {
        io0Pressed = false;
        io0Handled = false;
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
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // Keep the V1 BLE/Wi-Fi coexistence behavior.
    WiFi.setAutoReconnect(true);
    wifiDisconnectedSince = millis();
    lastWiFiRetry = millis();
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);
    addLog("INFO", "Connexion Wi-Fi domestique en cours: %s (2,4 GHz)", WIFI_STA_SSID);
    addLog("INFO", "En absence de Wi-Fi domestique, secours %s apres %lu s", WIFI_AP_NAME,
           static_cast<unsigned long>(WIFI_FALLBACK_AFTER_MS / 1000));
}

void maintainWiFi() {
    const uint32_t now = millis();
    if (forcedAPMode) {
        if (!apActive) startFallbackAP();
        return;
    }
    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected != previousSTAConnected) {
        previousSTAConnected = connected;
        if (connected) {
            staConnectedSince = now;
            ramCheckpoint("wifi_maison_connecte");
            const String ip = WiFi.localIP().toString();
            addLog("INFO", "Wi-Fi %s connecte, adresse http://%s, RSSI %ld dBm",
                   WIFI_STA_SSID, ip.c_str(), static_cast<long>(WiFi.RSSI()));
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
        addLog("DEBUG", "Nouvelle tentative de connexion au Wi-Fi %s", WIFI_STA_SSID);
        WiFi.reconnect();
    }
}

// HTTP helpers. Accessible on the home LAN or on the fallback AP.
