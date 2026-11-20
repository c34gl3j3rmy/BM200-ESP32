#pragma once

bool decryptPacket(const BlePacket& frame, uint8_t out[MAX_BLE_PACKET]) {
    if (!frame.length || frame.length > MAX_BLE_PACKET || (frame.length % 16) != 0)
        return false;
    for (size_t p = 0; p < frame.length; p += 16) {
        if (!aesBlock(frame.encrypted + p, out + p, false)) return false;
    }
    return true;
}

// Called by the NimBLE stack. Never perform file I/O or WebServer operations here.
void onBLEnotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
    if (!blePackets) return;
    if (length == 0 || length > MAX_BLE_PACKET) {
        ++droppedPackets;
        return;
    }
    BlePacket packet{};
    packet.receivedMs = millis();
    packet.length = static_cast<uint16_t>(length);
    memcpy(packet.encrypted, data, length);
    if (xQueueSend(blePackets, &packet, 0) != pdTRUE) ++droppedPackets;
}

void traceRow(File& trace, const char* direction, uint32_t ms,
              const uint8_t* encrypted, const uint8_t* plain, size_t len) {
    if (!trace) return;
    trace.print(ms);
    trace.print(',');
    trace.print(direction);
    trace.print(',');
    trace.print(len);
    trace.print(',');
    trace.print(asHex(encrypted, len));
    trace.print(',');
    if (plain) trace.print(asHex(plain, len));
    trace.println();
}

bool sendCommand(NimBLERemoteCharacteristic* writer, File& trace, const uint8_t plain[16]) {
    uint8_t encrypted[16];
    if (!aesBlock(plain, encrypted, true)) return false;
    traceRow(trace, "TX", millis(), encrypted, plain, 16);
    addLog("DEBUG", "TX %s", asHex(plain, 16).c_str());
    return writer->writeValue(encrypted, sizeof(encrypted), true);
}

void applyLive(const BM200::Live& live) {
    if (xSemaphoreTake(statusMutex, portMAX_DELAY) == pdTRUE) {
        status.liveValid = true;
        status.voltageCenti = live.voltageCenti;
        status.soc = live.soc;
        status.temperatureC = live.temperatureC;
        status.lastLiveUTC = utcNow();
        xSemaphoreGive(statusMutex);
    }
}

// Receives and decrypts a packet in the worker task (not in NimBLE callbacks).
bool receiveDecrypted(BlePacket& rx, uint8_t out[MAX_BLE_PACKET], File& trace, uint32_t waitMs) {
    if (xQueueReceive(blePackets, &rx, pdMS_TO_TICKS(waitMs)) != pdTRUE)
        return false;
    const bool valid = decryptPacket(rx, out);
    traceRow(trace, "RX", rx.receivedMs, rx.encrypted, valid ? out : nullptr, rx.length);
    if (!valid) addLog("WARN", "Paquet BLE taille non valide: %u octets", rx.length);
    return valid;
}

String historyKind(const BM200::Historical& h) {
    if (h.auxiliary) return "auxiliary";
    if (h.flag == 2) return "departure_candidate";
    if (h.flag == 4) return "arrival_candidate";
    return "measure";
}

bool storeHistory(uint16_t slots, const String& capturedAt, String& error, uint16_t& decoded) {
    if (!fsReady) { error = "LittleFS indisponible"; return false; }
    File csv = LittleFS.open(PATH_HISTORY_TEMP, "w");
    File json = LittleFS.open(PATH_JSON_TEMP, "w");
    if (!csv || !json) {
        if (csv) csv.close();
        if (json) json.close();
        LittleFS.remove(PATH_HISTORY_TEMP);
        LittleFS.remove(PATH_JSON_TEMP);
        error = "Impossible de creer les fichiers temporaires";
        return false;
    }

    csv.println("capture_utc,rank,raw_hex,voltage_v,soc_pct,temperature_raw_c,indicator,kind,measurement_utc");
    json.print("{\"device\":\"ANCEL BM200\",\"capture_utc\":");
    json.print(capturedAt.length() ? jsonString(capturedAt) : "null");
    json.print(",\"requested_slots\":");
    json.print(slots);
    json.print(",\"measurement_timestamps_verified\":false,\"records\":[");

    for (uint16_t i = 0; i < slots; ++i) {
        const uint8_t* word = historyRaw + (static_cast<size_t>(i) * 4);
        const BM200::Historical h = BM200::decodeHistory(word);
        ++decoded;
        const String raw = asHex(word, 4);
        const String kind = historyKind(h);
        csv.print(capturedAt); csv.print(',');
        csv.print(i); csv.print(',');
        csv.print(raw); csv.print(',');
        if (!h.auxiliary) {
            csv.print(h.voltageCenti / 100.0f, 2);
        }
        csv.print(',');
        if (!h.auxiliary) csv.print(h.soc);
        csv.print(',');
        if (!h.auxiliary) csv.print(h.temperatureRaw);
        csv.print(','); csv.print(h.flag); csv.print(',');
        csv.print(kind); csv.println(',');  // measurement_utc intentionally blank

        if (i) json.print(',');
        json.print("{\"rank\":"); json.print(i);
        json.print(",\"raw_hex\":"); json.print(jsonString(raw));
        json.print(",\"voltage\":");
        if (h.auxiliary) json.print("null"); else json.print(h.voltageCenti / 100.0f, 2);
        json.print(",\"soc\":");
        if (h.auxiliary) json.print("null"); else json.print(h.soc);
        json.print(",\"temperature_raw\":");
        if (h.auxiliary) json.print("null"); else json.print(h.temperatureRaw);
        json.print(",\"flag\":"); json.print(h.flag);
        json.print(",\"kind\":"); json.print(jsonString(kind));
        json.print(",\"measurement_utc\":null}");
    }
    json.println("]}");
    csv.flush(); json.flush();
    const bool good = (csv.getWriteError() == 0 && json.getWriteError() == 0);
    csv.close(); json.close();
    if (!good) {
        LittleFS.remove(PATH_HISTORY_TEMP);
        LittleFS.remove(PATH_JSON_TEMP);
        error = "Ecriture incomplete: espace flash insuffisant";
        return false;
    }

    // A small diagnostic request must NOT overwrite a larger successful capture.
    // This protects the 709-word capture from the subsequent 10-word test.
    size_t previousRows = 0;
    if (LittleFS.exists(PATH_HISTORY)) {
        File old = LittleFS.open(PATH_HISTORY, "r");
        if (old) {
            old.readStringUntil('\n');
            while (old.available()) {
                if (old.readStringUntil('\n').length()) ++previousRows;
            }
            old.close();
        }
    }
    if (previousRows > slots) {
        LittleFS.remove(PATH_SHORT_CSV);
        LittleFS.remove(PATH_SHORT_JSON);
        if (!LittleFS.rename(PATH_HISTORY_TEMP, PATH_SHORT_CSV) ||
            !LittleFS.rename(PATH_JSON_TEMP, PATH_SHORT_JSON)) {
            error = "Impossible de conserver le test court";
            return false;
        }
        addLog("INFO", "Test court (%u) conserve separement : historique principal de %u lignes preserve",
               slots, static_cast<unsigned>(previousRows));
        return true;
    }
    // Commit only complete captures; keep one previous successful export.
    LittleFS.remove(PATH_PREVIOUS);
    LittleFS.remove(PATH_PREVIOUS_JSON);
    if (LittleFS.exists(PATH_HISTORY)) LittleFS.rename(PATH_HISTORY, PATH_PREVIOUS);
    if (LittleFS.exists(PATH_HISTORY_JSON)) LittleFS.rename(PATH_HISTORY_JSON, PATH_PREVIOUS_JSON);
    if (!LittleFS.rename(PATH_HISTORY_TEMP, PATH_HISTORY) ||
        !LittleFS.rename(PATH_JSON_TEMP, PATH_HISTORY_JSON)) {
        error = "Echec du renommage des fichiers LittleFS";
        return false;
    }
    return true;
}

// Executes in a separate FreeRTOS task, allowing the web page to remain responsive.
