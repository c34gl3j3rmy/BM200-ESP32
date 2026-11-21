#pragma once

// Lifecycle: connect STA before HTTP, switch to AP-only on timeout.
// Do not run WiFi.reconnect() while the AP is serving clients.
// Only setup()/loop() modify the network state (no Wi-Fi operations in callbacks).

void stopNetworkHttp() {
    if (!httpServerStarted) return;
    web.stop();
    httpServerStarted = false;
    addLog("DEBUG", "HTTP: arret du serveur avant changement de reseau");
}

void stopNetworkMdns() {
    if (!mdnsActive) return;
    MDNS.end();
    mdnsActive = false;
}

void startFallbackAP() {
    if (apActive) return;
    stopNetworkHttp();
    stopNetworkMdns();

    // Completely leave station mode. Never keep a pending STA connection
    // active while the emergency access point is serving HTTP requests.
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_AP);
    const IPAddress ip(192, 168, 4, 1);
    if (!WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0))) {
        addLog("WARN", "Wi-Fi secours: configuration IP refusee");
    }
    ramCheckpoint("avant_wifi_secours");
    if (!WiFi.softAP(WIFI_AP_NAME, WIFI_AP_PASSWORD, 1, false, 2)) {
        apActive = false;
        addLog("ERROR", "Wi-Fi secours: creation AP impossible; nouvel essai plus tard");
        return;
    }
    apActive = true;
    staAttemptRunning = false;
    wifiRecoveringAfterLoss = false;
    previousSTAConnected = false;
    apStartedAt = millis();
    apLastClientSeenAt = apStartedAt;
    ramCheckpoint("wifi_secours_actif");
    addLog("INFO", "Wi-Fi secours AP seul: %s, IP http://%s, mode=%d",
           WIFI_AP_NAME, WiFi.softAPIP().toString().c_str(),
           static_cast<int>(WiFi.getMode()));
    // The HTTP server starts only after the AP network is ready, in setup/loop.
}

// Core 4.x (ESP-IDF 6.x): on Freebox WPA2/3 Compatible Mode,
// the newer driver can fail the RSN Override (WPA3) handshake (reason 17).
// The minimal diagnostic confirmed that disabling only this ESP32 feature
// permits a successful WPA2-AES connection without changing router settings.
// Core 3.x retains the previously working standard Arduino WiFi.begin().
bool beginHomeSTAConnection() {
#if ESP_ARDUINO_VERSION_MAJOR >= 4
    WiFi.setAutoReconnect(false); // Network transitions are managed by loop().

    // Prepare credentials, but do not start the WPA handshake yet.
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD, 0, nullptr, false);

    wifi_config_t cfg = {};
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        addLog("ERROR", "Wi-Fi Core4: get_config: %s", esp_err_to_name(err));
        return false;
    }
    cfg.sta.disable_wpa3_compatible_mode = WIFI_DISABLE_WPA3_RSN_OVERRIDE ? 1 : 0;
    err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        addLog("ERROR", "Wi-Fi Core4: set_config: %s", esp_err_to_name(err));
        return false;
    }
    addLog("INFO", "Wi-Fi Core4: RSN Override %s cote ESP32",
           WIFI_DISABLE_WPA3_RSN_OVERRIDE ? "desactive (WPA2)" : "autorise");
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        addLog("ERROR", "Wi-Fi Core4: connect: %s", esp_err_to_name(err));
        return false;
    }
    return true;
#else
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);
    return true;
#endif
}

void startHomeSTA(const char* reason) {
    stopNetworkHttp();
    stopNetworkMdns();
    if (apActive) {
        WiFi.softAPdisconnect(true);
        apActive = false;
    }
    forcedAPMode = false;
    WiFi.mode(WIFI_STA);
    wifiAttemptStartedAt = millis();
    staAttemptRunning = true;
    wifiRecoveringAfterLoss = false;
    previousSTAConnected = false;
    lastAPAttempt = 0;
    beginHomeSTAConnection();
    addLog("INFO", "Wi-Fi STA: connexion a %s (%s)", WIFI_STA_SSID,
           reason ? reason : "inconnu");
}

void setStatusLed(bool on) {
    if (!STATUS_LED_ENABLED) return;
    ledLogicalOn = on;
    digitalWrite(STATUS_LED_PIN, (on == STATUS_LED_ACTIVE_HIGH) ? HIGH : LOW);
}

void enterForcedAPMode() {
    if (apActive) return;
    forcedAPMode = true;
    lastAPAttempt = millis();
    startFallbackAP();
    addLog("INFO", "IO0: secours AP force; ouvrir http://192.168.4.1/api/ping");
    ramCheckpoint("wifi_secours_force");
}

void leaveForcedAPMode() {
    if (!apActive) return;
    startHomeSTA("IO0: nouvel essai manuel");
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
        if (apActive) leaveForcedAPMode();
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
    if (apActive) {
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
    WiFi.persistent(false);
    // AP connection notifications are informative only. They run in a
    // separate Wi-Fi task: do not change WebServer/WiFi state in callbacks.
    WiFi.onEvent([](arduino_event_id_t) {
        Serial.println("[WIFI] Client connecte au point d'acces");
    }, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
    WiFi.onEvent([](arduino_event_id_t) {
        Serial.println("[WIFI] Client deconnecte du point d'acces");
    }, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);

    // Preserve the successful HelloServer / V1.5.1 startup sequence.
    // No AP, no BLE and no WebServer yet while joining the home Wi-Fi.
    WiFi.mode(WIFI_STA);
    beginHomeSTAConnection();
    wifiAttemptStartedAt = millis();
    staAttemptRunning = true;
    wifiRecoveringAfterLoss = false;
    addLog("INFO", "Wi-Fi STA prioritaire: connexion a %s", WIFI_STA_SSID);
    while (WiFi.status() != WL_CONNECTED &&
           millis() - wifiAttemptStartedAt < WIFI_STA_BOOT_TIMEOUT_MS) {
        delay(500);
    }
    if (WiFi.status() == WL_CONNECTED) {
        staAttemptRunning = false;
        addLog("INFO", "Wi-Fi maison connecte: http://%s/api/status, RSSI %ld dBm",
               WiFi.localIP().toString().c_str(), static_cast<long>(WiFi.RSSI()));
    } else {
        addLog("WARN", "Wi-Fi maison indisponible apres %lu s; activation du secours AP seul",
               static_cast<unsigned long>(WIFI_STA_BOOT_TIMEOUT_MS / 1000));
        lastAPAttempt = millis();
        startFallbackAP();
    }
}

// Called by setup()/loop() only after registerRoutes() has run.
void ensureNetworkHttp() {
    const bool ready = apActive || WiFi.status() == WL_CONNECTED;
    if (ready && !httpServerStarted) {
        web.begin();
        httpServerStarted = true;
        addLog("INFO", "Serveur HTTP demarre sur %s port 80",
               apActive ? "AP" : "STA");
    } else if (!ready && httpServerStarted) {
        stopNetworkHttp();
    }
}

void maintainWiFi() {
    const uint32_t now = millis();
    if (apActive) {
        const uint32_t clients = WiFi.softAPgetStationNum();
        if (clients != 0) apLastClientSeenAt = now;
        // Automatic return to the home WLAN is only attempted with no AP
        // clients, and never while the user has explicitly forced AP mode.
        if (!forcedAPMode && clients == 0 &&
            now - apStartedAt >= WIFI_AP_HOME_RETRY_INTERVAL_MS &&
            now - apLastClientSeenAt >= WIFI_AP_IDLE_BEFORE_RETRY_MS) {
            addLog("INFO", "Wi-Fi secours sans client: recherche du reseau maison");
            startHomeSTA("essai automatique apres secours");
        }
        return;
    }

    if (forcedAPMode) {
        // If softAP previously failed, retry it without attempting STA.
        if (lastAPAttempt == 0 || now - lastAPAttempt >= WIFI_AP_RETRY_INTERVAL_MS) {
            lastAPAttempt = now;
            startFallbackAP();
        }
        return;
    }

    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected) {
        if (!previousSTAConnected) {
            previousSTAConnected = true;
            staAttemptRunning = false;
            wifiRecoveringAfterLoss = false;
            addLog("INFO", "Wi-Fi maison actif: http://%s/api/status, RSSI %ld dBm",
                   WiFi.localIP().toString().c_str(), static_cast<long>(WiFi.RSSI()));
        }
        if (!mdnsActive && MDNS.begin(WIFI_HOSTNAME)) {
            MDNS.addService("http", "tcp", 80);
            mdnsActive = true;
        }
        return;
    }

    if (previousSTAConnected) {
        previousSTAConnected = false;
        staAttemptRunning = true;
        wifiRecoveringAfterLoss = true;
        wifiAttemptStartedAt = now;
        stopNetworkHttp();
        stopNetworkMdns();
#if ESP_ARDUINO_VERSION_MAJOR >= 4
        // Core 4 uses manual reconnection (auto reconnect is intentionally off).
        // Try exactly once; never issue repeated esp_wifi_connect() calls.
        const esp_err_t retryErr = esp_wifi_connect();
        addLog(retryErr == ESP_OK ? "INFO" : "WARN",
               "Wi-Fi Core4: tentative unique de reconnexion: %s",
               esp_err_to_name(retryErr));
#endif
        addLog("WARN", "Wi-Fi domestique perdu: attente %lu s avant secours",
               static_cast<unsigned long>(WIFI_FALLBACK_AFTER_MS / 1000));
    }
    const uint32_t timeout = wifiRecoveringAfterLoss
        ? WIFI_FALLBACK_AFTER_MS : WIFI_STA_BOOT_TIMEOUT_MS;
    if (now - wifiAttemptStartedAt >= timeout &&
        (lastAPAttempt == 0 || now - lastAPAttempt >= WIFI_AP_RETRY_INTERVAL_MS)) {
        lastAPAttempt = now;
        startFallbackAP();
    }
}

// HTTP helpers. Accessible from the home WLAN or from the fallback AP.
