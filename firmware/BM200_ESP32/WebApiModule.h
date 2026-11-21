#pragma once

void dontCache() {
    web.sendHeader("Cache-Control", "no-store");
    web.sendHeader("X-Content-Type-Options", "nosniff");
}

void sendStatus() {
    Status s;
    if (xSemaphoreTake(statusMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s = status;
        xSemaphoreGive(statusMutex);
    }
    size_t fsFree = 0;
    if (fsReady) fsFree = LittleFS.totalBytes() - LittleFS.usedBytes();
    String out;
    out.reserve(1150);
    out += "{\"mac\":" + jsonString(BM200_BLE_MAC);
    const bool staOnline = WiFi.status() == WL_CONNECTED;
    out += ",\"wifi_clients\":" + String(apActive ? WiFi.softAPgetStationNum() : 0);
    out += ",\"wifi\":{\"home_ssid\":" + jsonString(WIFI_STA_SSID);
    out += ",\"mode\":" + jsonString(forcedAPMode ? "secours force" : (staOnline ? (apActive ? "maison + secours" : "maison") : (apActive ? "secours" : "connexion")));
    out += ",\"sta_connected\":" + String(staOnline ? "true" : "false");
    out += ",\"sta_ip\":" + jsonString(staOnline ? WiFi.localIP().toString() : String());
    out += ",\"sta_rssi\":" + (staOnline ? String(WiFi.RSSI()) : String("null"));
    out += ",\"ap_active\":" + String(apActive ? "true" : "false");
    out += ",\"forced_ap\":" + String(forcedAPMode ? "true" : "false");
    out += ",\"ap_ip\":" + jsonString(apActive ? WiFi.softAPIP().toString() : String());
    out += ",\"ap_clients\":" + String(apActive ? WiFi.softAPgetStationNum() : 0);
    out += ",\"mdns_name\":" + jsonString(mdnsActive ? String(WIFI_HOSTNAME) + ".local" : String());
    out += "}";
    out += ",\"uptime_ms\":" + String(millis());
    out += ",\"heap_free\":" + String(ESP.getFreeHeap());
    out += ",\"heap_total\":" + String(ESP.getHeapSize());
    out += ",\"heap_min\":" + String(ESP.getMinFreeHeap());
    out += ",\"heap_largest\":" + String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    out += ",\"fs_ready\":" + String(fsReady ? "true" : "false");
    out += ",\"fs_free\":" + String(fsFree);
    out += ",\"rssi\":" + (s.rssi == -127 ? String("null") : String(s.rssi));
    out += ",\"ble_ready\":" + String(bleReady ? "true" : "false");
    out += ",\"ble_connected\":" + String(s.bleConnected ? "true" : "false");
    out += ",\"live_session\":" + String(s.liveSession ? "true" : "false");
    out += ",\"live_wanted\":" + String(keepLive.load() ? "true" : "false");
    out += ",\"dropped_packets\":" + String(static_cast<uint32_t>(droppedPackets));
    out += ",\"live\":{\"valid\":" + String(s.liveValid ? "true" : "false");
    out += ",\"voltage_centi\":" + String(s.voltageCenti);
    out += ",\"soc\":" + String(s.soc);
    out += ",\"temp\":" + String(s.temperatureC);
    out += ",\"at\":" + jsonString(s.lastLiveUTC) + "}";
    out += ",\"sync\":{\"busy\":" + String(s.busy ? "true" : "false");
    out += ",\"requested\":" + String(s.requested);
    out += ",\"received\":" + String(s.received);
    out += ",\"phase\":" + jsonString(s.phase);
    out += ",\"error\":" + jsonString(s.error);
    out += ",\"last_success\":" + String(s.lastSuccess ? "true" : "false");
    out += ",\"last_at\":" + jsonString(s.lastAtUTC);
    out += ",\"last_count\":" + String(s.lastCount);
    out += ",\"last_duration_ms\":" + String(s.lastDurationMs) + "}";
    out += ",\"start\":{\"busy\":" + String(s.startBusy ? "true" : "false");
    out += ",\"read\":" + String(s.startRead ? "true" : "false");
    out += ",\"available\":" + String(s.startAvailable ? "true" : "false");
    out += ",\"no_new_report\":" + String(s.startNoNewReport ? "true" : "false");
    out += ",\"valid\":" + String(s.startValid ? "true" : "false");
    out += ",\"result_code\":" + String(s.startResultCode);
    out += ",\"minimum_voltage_centi\":" + String(s.startMinVoltageCenti);
    out += ",\"duration_ms\":" + String(s.startDurationMs);
    out += ",\"age_steps_120s\":" + String(s.startAgeSteps);
    out += ",\"sample_count\":" + String(s.startSampleCount);
    out += ",\"read_at\":" + jsonString(s.startReadUTC);
    out += ",\"error\":" + jsonString(s.startError) + "}";
    out += ",\"files\":{\"history\":" + String(fsReady && LittleFS.exists(PATH_HISTORY) ? "true" : "false");
    out += ",\"json\":" + String(fsReady && LittleFS.exists(PATH_HISTORY_JSON) ? "true" : "false");
    out += ",\"previous\":" + String(fsReady && LittleFS.exists(PATH_PREVIOUS) ? "true" : "false");
    out += ",\"short\":" + String(fsReady && LittleFS.exists(PATH_SHORT_CSV) ? "true" : "false");
    out += ",\"trace\":" + String(fsReady && LittleFS.exists(PATH_TRACE) ? "true" : "false") + "}}";
    dontCache();
    web.send(200, "application/json; charset=utf-8", out);
}

void sendLogs() {
    // Chunked response avoids allocating an entire log snapshot in heap.
    dontCache();
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.send(200, "text/plain; charset=utf-8", "");
    size_t count = 0, first = 0;
    if (xSemaphoreTake(logsMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        count = logCount;
        first = (logHead + LOG_CAPACITY - logCount) % LOG_CAPACITY;
        xSemaphoreGive(logsMutex);
    }
    for (size_t i = 0; i < count; ++i) {
        String line;
        if (xSemaphoreTake(logsMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            line = logRing[(first + i) % LOG_CAPACITY];
            xSemaphoreGive(logsMutex);
        }
        web.sendContent(line);
        web.sendContent("\n");
    }
    web.sendContent("");
}

bool syncBusy() {
    bool busy = false;
    if (xSemaphoreTake(statusMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        busy = status.busy;
        xSemaphoreGive(statusMutex);
    }
    return busy;
}

void downloadFile(const char* path, const char* filename, const char* mime) {
    if (syncBusy()) { web.send(409, "text/plain", "Synchronisation en cours"); return; }
    if (!fsReady || !LittleFS.exists(path)) {
        web.send(404, "text/plain", "Fichier non disponible"); return;
    }
    File file = LittleFS.open(path, "r");
    if (!file) { web.send(500, "text/plain", "Lecture LittleFS echouee"); return; }
    dontCache();
    web.sendHeader("Content-Disposition", String("attachment; filename=\"") + filename + "\"");
    web.streamFile(file, mime);
    file.close();
}

void startRequestedSync() {
    SyncRequest req{};
    const String id = web.arg("id");
    if (id.length() && id.length() <= 24 &&
        strspn(id.c_str(), "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-") == id.length()) {
        snprintf(req.id, sizeof(req.id), "%s", id.c_str());
    } else {
        snprintf(req.id, sizeof(req.id), "esp-%08lx", static_cast<unsigned long>(esp_random()));
    }
    const String countText = web.arg("count");
    addLog("INFO", "Synchro #%s: demande HTTP recue (%ld emplacements)", req.id, countText.toInt());
    if (countText != "10" && countText != "252" && countText != "709") {
        addLog("WARN", "Synchro #%s: nombre d'emplacements invalide", req.id);
        web.send(400, "text/plain", "Tailles autorisees: 10, 252, 709"); return;
    }
    const uint16_t count = static_cast<uint16_t>(countText.toInt());
    req.operation = OP_SYNC;
    req.count = count;
    if (keepLive.load()) {
        web.send(409,"text/plain","Deconnecter le mode Live avant synchronisation"); return;
    }
    if (!fsReady || !bleReady) {
        const char* reason = fsReady ? "Moteur BLE indisponible" : "LittleFS indisponible";
        addLog("ERROR", "Synchro #%s: %s", req.id, reason);
        web.send(503, "text/plain", reason); return;
    }
    bool accepted = false;
    if (xSemaphoreTake(statusMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (!status.busy) {
            status.busy = true;
            status.requested = count;
            status.rssi = -127;
            status.received = 0;
            status.error = "";
            status.phase = "En file d'attente";
            accepted = true;
        }
        xSemaphoreGive(statusMutex);
    }
    if (!accepted) {
        addLog("WARN", "Synchro #%s: ESP32 occupe", req.id);
        web.send(409, "text/plain", "ESP32 occupe"); return;
    }
    if (xQueueSend(requests, &req, 0) != pdTRUE) {
        if (xSemaphoreTake(statusMutex, portMAX_DELAY) == pdTRUE) {
            status.busy = false;
            status.error = "Queue de synchronisation saturee";
            xSemaphoreGive(statusMutex);
        }
        addLog("ERROR", "Synchro #%s: file BLE saturee", req.id);
        web.send(503, "text/plain", "Le moteur BLE ne repond pas");
        return;
    }
    dontCache();
    web.send(202, "text/plain", "Synchronisation acceptee");
}


void sendStartReport() {
    Status s;
    uint16_t samples[BM200::START_CURVE_POINTS]{};
    if (xSemaphoreTake(statusMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s = status;
        if (s.startValid && !s.startBusy)
            memcpy(samples, startCurve, sizeof(samples));
        xSemaphoreGive(statusMutex);
    }
    String out;
    out.reserve(3000);
    out += "{\"busy\":" + String(s.startBusy ? "true" : "false");
    out += ",\"read\":" + String(s.startRead ? "true" : "false");
    out += ",\"available\":" + String(s.startAvailable ? "true" : "false");
    out += ",\"no_new_report\":" + String(s.startNoNewReport ? "true" : "false");
    out += ",\"valid\":" + String(s.startValid ? "true" : "false");
    out += ",\"result_code\":" + String(s.startResultCode);
    out += ",\"minimum_voltage\":" + String(s.startMinVoltageCenti / 100.0f, 2);
    out += ",\"duration_ms\":" + String(s.startDurationMs);
    out += ",\"age_steps_120s\":" + String(s.startAgeSteps);
    out += ",\"age_seconds\":" + String(static_cast<uint32_t>(s.startAgeSteps) * 120UL);
    out += ",\"read_at\":" + jsonString(s.startReadUTC);
    out += ",\"sample_period_verified\":false";
    out += ",\"error\":" + jsonString(s.startError);
    out += ",\"samples\":[";
    if (s.startValid && !s.startBusy) {
        for (size_t i = 0; i < s.startSampleCount; ++i) {
            if (i) out += ',';
            out += String(samples[i] / 100.0f, 2);
        }
    }
    out += "]}";
    dontCache();
    web.send(200, "application/json; charset=utf-8", out);
}

void requestStartReport() {
    addLog("INFO", "HTTP: lecture du rapport de demarrage demandee");
    if (!bleReady) { web.send(503, "text/plain", "Moteur BLE indisponible"); return; }
    if (keepLive.load()) { web.send(409, "text/plain", "Deconnecter le mode Live avant le test de demarrage"); return; }
    bool accepted = false;
    if (xSemaphoreTake(statusMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (!status.busy) {
            status.busy = true;
            status.startBusy = true;
            status.startNoNewReport = false;
            status.startError = "";
            status.error = "";
            status.phase = "Demarrage: en file d'attente";
            accepted = true;
        }
        xSemaphoreGive(statusMutex);
    }
    if (!accepted) { web.send(409, "text/plain", "ESP32 occupe"); return; }
    SyncRequest req{};
    req.operation = OP_START;
    snprintf(req.id, sizeof(req.id), "start-%08lx", static_cast<unsigned long>(esp_random()));
    if (xQueueSend(requests, &req, 0) != pdTRUE) {
        if (xSemaphoreTake(statusMutex, portMAX_DELAY) == pdTRUE) {
            status.busy = false;
            status.startBusy = false;
            status.startError = "File BLE indisponible";
            xSemaphoreGive(statusMutex);
        }
        web.send(503, "text/plain", "File BLE indisponible");
        return;
    }
    dontCache();
    web.send(202, "text/plain", "Lecture du rapport de demarrage acceptee");
}

void setManualLive() {
    const bool enabled = web.arg("enabled") == "1";
    if (!bleReady) { web.send(503,"text/plain","Moteur BLE indisponible"); return; }
    if (!enabled) {
        keepLive.store(false);
        addLog("INFO","HTTP: deconnexion Live demandee");
        web.send(202,"text/plain","Deconnexion demandee");
        return;
    }
    if (syncBusy() || keepLive.load()) {
        web.send(409,"text/plain","Bluetooth occupe ou Live deja demande"); return;
    }
    if (xSemaphoreTake(statusMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        status.error = "";
        status.phase = "Live: connexion demandee";
        xSemaphoreGive(statusMutex);
    }
    keepLive.store(true);
    SyncRequest req{}; req.operation=OP_LIVE; req.count=0;
    if (xQueueSend(requests,&req,0)!=pdTRUE) {
        keepLive.store(false);
        web.send(503,"text/plain","File BLE indisponible"); return;
    }
    addLog("INFO","HTTP: connexion Live demandee");
    web.send(202,"text/plain","Connexion Live demandee");
}


void recoverBluetooth() {
    addLog("INFO", "HTTP: reconnexion Bluetooth forcee demandee");

    if (syncBusy()) {
        web.send(409, "text/plain", "Bluetooth occupe par une synchronisation ou un test");
        return;
    }

    // Stop an existing Live session. The worker will consume the queued reconnect
    // request immediately after the current client has disconnected and cleaned up.
    keepLive.store(false);

    if (!bleReady && !initializeBleEngine("reconnexion manuelle")) {
        web.send(503, "text/plain", "Reinitialisation NimBLE impossible; voir le journal");
        return;
    }

    if (xSemaphoreTake(statusMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        status.error = "";
        status.phase = "BLE: reconnexion forcee demandee";
        xSemaphoreGive(statusMutex);
    }

    SyncRequest req{};
    req.operation = OP_LIVE;
    req.count = 0;
    if (xQueueSend(requests, &req, 0) != pdTRUE) {
        web.send(503, "text/plain", "File BLE indisponible");
        return;
    }

    dontCache();
    web.send(202, "text/plain", "Reconnexion Bluetooth demandee");
}

void setClockFromBrowser() {
    const int64_t seconds = atoll(web.arg("epoch").c_str());
    if (seconds < 1700000000LL || seconds > 4102444800LL) {
        web.send(400, "text/plain", "Date invalide"); return;
    }
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(seconds);
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    dontCache();
    web.send(200, "text/plain", "Horloge mise a jour");
    addLog("INFO", "Heure UTC reglee depuis le navigateur: %s", utcNow().c_str());
}

void sendPreview() {
    if (syncBusy()) { web.send(409, "text/plain", "Synchronisation en cours"); return; }
    if (!fsReady || !LittleFS.exists(PATH_HISTORY)) {
        web.send(200, "application/json", "[]"); return;
    }
    File file = LittleFS.open(PATH_HISTORY, "r");
    if (!file) { web.send(500, "text/plain", "Lecture historique impossible"); return; }
    file.readStringUntil('\n'); // CSV header
    String result = "[";
    uint16_t rank = 0;
    while (file.available() && rank < 16) {
        const String line = file.readStringUntil('\n');
        // capture_utc,rank,raw_hex,... ; raw byte word is column 3.
        const int comma1 = line.indexOf(',');
        const int comma2 = comma1 < 0 ? -1 : line.indexOf(',', comma1 + 1);
        const int comma3 = comma2 < 0 ? -1 : line.indexOf(',', comma2 + 1);
        if (comma2 < 0 || comma3 < 0) break;
        const String raw = line.substring(comma2 + 1, comma3);
        if (raw.length() != 8) break;
        uint8_t word[4]{};
        bool valid = true;
        for (int i = 0; i < 4; ++i) {
            const String pair = raw.substring(i * 2, i * 2 + 2);
            char* end = nullptr;
            word[i] = static_cast<uint8_t>(strtoul(pair.c_str(), &end, 16));
            if (!end || *end != '\0') valid = false;
        }
        if (!valid) break;
        const BM200::Historical h = BM200::decodeHistory(word);
        if (rank) result += ',';
        result += "{\"rank\":" + String(rank);
        result += ",\"voltage\":" + (h.auxiliary ? String("null") : String(h.voltageCenti / 100.0f, 2));
        result += ",\"soc\":" + (h.auxiliary ? String("null") : String(h.soc));
        result += ",\"temp\":" + (h.auxiliary ? String("null") : String(h.temperatureRaw));
        result += ",\"flag\":" + String(h.flag);
        result += ",\"kind\":" + jsonString(historyKind(h)) + "}";
        ++rank;
    }
    file.close();
    result += ']';
    dontCache();
    web.send(200, "application/json; charset=utf-8", result);
}

// Restore the last successful capture metadata after a Power Bank restart.
// Do not mistake persisted sample values for a fresh live BLE reading.
void restoreSavedHistory() {
    if (!fsReady || !LittleFS.exists(PATH_HISTORY)) return;
    File f = LittleFS.open(PATH_HISTORY, "r");
    if (!f) return;
    f.readStringUntil('\n'); // header
    uint16_t records = 0;
    String firstCapture;
    while (f.available()) {
        const String line = f.readStringUntil('\n');
        if (line.length() == 0) continue;
        if (records == 0) {
            const int comma = line.indexOf(',');
            if (comma > 0) firstCapture = line.substring(0, comma);
        }
        ++records;
    }
    f.close();
    if (records == 0) return;
    if (xSemaphoreTake(statusMutex, portMAX_DELAY) == pdTRUE) {
        status.lastSuccess = true;
        status.lastCount = records;
        status.lastAtUTC = firstCapture;
        status.phase = "Historique sauvegarde disponible (lecture non actualisee)";
        xSemaphoreGive(statusMutex);
    }
    addLog("INFO", "Historique recharge depuis LittleFS: %u lignes", records);
}

int hexDigitValue(char c) {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}

// History payload: 4 decrypted bytes per stored word, in CSV order.
// Metadata/dates remain separate: this endpoint never invents sample timestamps.
void sendBinaryHistory() {
    if (syncBusy()) { web.send(409, "text/plain", "Synchronisation en cours"); return; }
    if (!fsReady || !LittleFS.exists(PATH_HISTORY)) { web.send(404, "text/plain", "Historique absent"); return; }
    File f = LittleFS.open(PATH_HISTORY, "r");
    if (!f) { web.send(500, "text/plain", "LittleFS indisponible"); return; }
    f.readStringUntil('\n');
    size_t count = 0;
    while (f.available()) {
        const String line=f.readStringUntil('\n');
        const int a=line.indexOf(','), b=a<0?-1:line.indexOf(',',a+1);
        const int c=b<0?-1:line.indexOf(',',b+1);
        if (c<0 || c-b!=9) continue;
        bool valid=true;
        for(int i=b+1;i<c;++i) if(hexDigitValue(line[i])<0){valid=false;break;}
        if(valid) ++count;
    }
    f.seek(0); f.readStringUntil('\n');
    dontCache();
    web.sendHeader("X-BM200-Record-Size", "4");
    web.sendHeader("X-BM200-Record-Count", String(count));
    web.setContentLength(count * 4);
    web.send(200, "application/octet-stream", "");
    uint8_t bytes[256]; size_t used = 0;
    while (f.available()) {
        const String line=f.readStringUntil('\n');
        const int first=line.indexOf(','), second=first<0?-1:line.indexOf(',',first+1);
        const int third=second<0?-1:line.indexOf(',',second+1);
        if (third<0 || third-second!=9) continue;
        bool valid=true; uint8_t word[4];
        for(int i=0;i<4;++i) {
            const int hi=hexDigitValue(line[second+1+i*2]);
            const int lo=hexDigitValue(line[second+2+i*2]);
            if(hi<0||lo<0){valid=false;break;}
            word[i]=static_cast<uint8_t>((hi<<4)|lo);
        }
        if (!valid) continue;
        memcpy(bytes+used,word,4);used+=4;
        if(used==sizeof(bytes)){web.client().write(bytes,used);used=0;}
    }
    if(used) web.client().write(bytes,used);
    f.close();
}

void registerRoutes() {
    // Public GitHub Pages frontend. No cookies or auth headers are used.
    // Origin restrictions and a token should be added before exposing the ESP32 to untrusted LANs.
    web.enableCORS(true);
    // Local rescue remains available if GitHub Pages/PWA is inaccessible.
    web.on("/", HTTP_GET, []() {
        dontCache();
        web.send(200,"text/html; charset=utf-8","<!doctype html><meta name=viewport content='width=device-width'><title>BM200 API</title><h2>BM200 - ESP32 API</h2><p>Interface: installer la PWA depuis GitHub Pages.</p><p><a href='/api/status'>Etat JSON</a> | <a href='/api/logs'>Journal</a></p>");
    });
    // Diagnostic minimal : verifier la reponse du serveur sans allouer de gros JSON.
    web.on("/api/ping", HTTP_GET, []() {
        Serial.println("[HTTP] GET /api/ping");
        web.send(200, "text/plain", "PONG");
    });
    web.on("/api/status", HTTP_GET, sendStatus);
    web.on("/api/logs", HTTP_GET, sendLogs);
    web.on("/api/preview", HTTP_GET, sendPreview);
    web.on("/api/history.bin", HTTP_GET, sendBinaryHistory);
    web.on("/api/sync", HTTP_POST, startRequestedSync);
    web.on("/api/start", HTTP_GET, sendStartReport);
    web.on("/api/start", HTTP_POST, requestStartReport);
    web.on("/api/live", HTTP_POST, setManualLive);
    web.on("/api/ble/recover", HTTP_POST, recoverBluetooth);
    web.on("/api/time", HTTP_POST, setClockFromBrowser);
    web.on("/download/history.csv", HTTP_GET, []() { downloadFile(PATH_HISTORY, "BM200_historique.csv", "text/csv"); });
    web.on("/download/history.json", HTTP_GET, []() { downloadFile(PATH_HISTORY_JSON, "BM200_historique.json", "application/json"); });
    web.on("/download/previous.csv", HTTP_GET, []() { downloadFile(PATH_PREVIOUS, "BM200_historique_precedent.csv", "text/csv"); });
    web.on("/download/short.csv", HTTP_GET, []() { downloadFile(PATH_SHORT_CSV, "BM200_dernier_test_court.csv", "text/csv"); });
    web.on("/download/short.json", HTTP_GET, []() { downloadFile(PATH_SHORT_JSON, "BM200_dernier_test_court.json", "application/json"); });
    web.on("/download/trace.csv", HTTP_GET, []() { downloadFile(PATH_TRACE, "BM200_trace_BLE.csv", "text/csv"); });
    web.on("/download/start.json", HTTP_GET, []() { downloadFile(PATH_START_JSON, "BM200_rapport_demarrage.json", "application/json"); });
    web.on("/download/start-trace.csv", HTTP_GET, []() { downloadFile(PATH_START_TRACE, "BM200_trace_demarrage.csv", "text/csv"); });
    web.on("/download/ram.csv", HTTP_GET, []() { downloadFile(PATH_RAM_DIAG, "BM200_diagnostic_RAM.csv", "text/csv"); });
    web.on("/download/ram-previous.csv", HTTP_GET, []() { downloadFile(PATH_RAM_OLD, "BM200_diagnostic_RAM_precedent.csv", "text/csv"); });
    web.onNotFound([]() {
        if (web.method()==HTTP_OPTIONS) {
            web.sendHeader("Access-Control-Allow-Origin", "*");
            web.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            web.sendHeader("Access-Control-Allow-Headers", "Content-Type, Access-Control-Request-Private-Network");
            web.sendHeader("Access-Control-Allow-Private-Network", "true");
            web.send(204, "text/plain", "");
            return;
        }
        web.send(404, "text/plain", "Page introuvable");
    });
}



