#pragma once

String utcNow() {
    time_t now = time(nullptr);
    if (now < 1700000000) return String();
    struct tm tmUtc;
    gmtime_r(&now, &tmUtc);
    char buf[25];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmUtc);
    return String(buf);
}

void addLog(const char* level, const char* fmt, ...) {
    // Do not spend heap/serial/flash on verbose traces in normal mode.
    if (strcmp(level, "DEBUG") == 0 && diagnosticLevel < 1) return;
    if (strcmp(level, "RAM") == 0 && diagnosticLevel < 2) return;
    char message[LOG_LINE_SIZE - 38];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    const String at = utcNow();
    char line[LOG_LINE_SIZE];
    if (at.length()) {
        snprintf(line, sizeof(line), "[%s][%s] %s", at.c_str(), level, message);
    } else {
        snprintf(line, sizeof(line), "[+%lus][%s] %s", static_cast<unsigned long>(millis() / 1000), level, message);
    }
    Serial.println(line);
    if (logsMutex && xSemaphoreTake(logsMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        strncpy(logRing[logHead], line, LOG_LINE_SIZE - 1);
        logRing[logHead][LOG_LINE_SIZE - 1] = '\0';
        logHead = (logHead + 1) % LOG_CAPACITY;
        if (logCount < LOG_CAPACITY) ++logCount;
        xSemaphoreGive(logsMutex);
    }
}

// The complete diagnostic log is kept on LittleFS, not duplicated in heap.
// Truncate/rotate at 64 KiB to avoid filling the 1 MiB LittleFS partition.
void ramCheckpoint(const char* event) {
    const uint32_t freeNow = ESP.getFreeHeap();
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const uint32_t sdkMinimum = ESP.getMinFreeHeap();
    if (freeNow < ramMinimum) ramMinimum = freeNow;
    const int32_t delta = ramLast ? static_cast<int32_t>(freeNow) - static_cast<int32_t>(ramLast) : 0;
    ramLast = freeNow;
    ramLastCheckpointMs = millis();
    addLog("RAM", "%s: libre=%lu Ko, delta=%ld octets, min=%lu Ko, bloc=%lu Ko", event,
        static_cast<unsigned long>(freeNow / 1024), static_cast<long>(delta),
        static_cast<unsigned long>(sdkMinimum / 1024), static_cast<unsigned long>(largest / 1024));
    if (!fsReady || diagnosticLevel < 2) return;
    File f = LittleFS.open(PATH_RAM_DIAG, "a");
    if (!f) return;
    f.print(millis()); f.print(',');
    f.print(utcNow()); f.print(',');
    f.print(event); f.print(',');
    f.print(freeNow); f.print(',');
    f.print(sdkMinimum); f.print(',');
    f.print(largest); f.print(',');
    f.println(delta);
    f.close();
    File check = LittleFS.open(PATH_RAM_DIAG, "r");
    if (check) {
        const size_t used = check.size(); check.close();
        if (used > 65536) {
            LittleFS.remove(PATH_RAM_OLD);
            LittleFS.rename(PATH_RAM_DIAG, PATH_RAM_OLD);
        }
    }
}

void setPhase(const char* value) {
    snprintf(interruptedPhase, sizeof(interruptedPhase), "%s", value);
    interruptedSyncMarker = SYNC_MARKER;
    if (xSemaphoreTake(statusMutex, portMAX_DELAY) == pdTRUE) {
        status.phase = value;
        xSemaphoreGive(statusMutex);
    }
}
void setConnected(bool connected) {
    if (xSemaphoreTake(statusMutex, portMAX_DELAY) == pdTRUE) {
        status.bleConnected = connected;
        xSemaphoreGive(statusMutex);
    }
}
void setReceived(uint16_t count) {
    if (xSemaphoreTake(statusMutex, portMAX_DELAY) == pdTRUE) {
        status.received = count;
        xSemaphoreGive(statusMutex);
    }
}

String asHex(const uint8_t* b, size_t n) {
    static const char digits[] = "0123456789abcdef";
    String out;
    out.reserve(n * 2 + 1);
    for (size_t i = 0; i < n; ++i) {
        out += digits[b[i] >> 4];
        out += digits[b[i] & 15];
    }
    return out;
}

String jsonString(const String& text) {
    String out = "\"";
    out.reserve(text.length() + 8);
    for (size_t i = 0; i < text.length(); ++i) {
        const char c = text[i];
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (static_cast<unsigned char>(c) < 0x20) out += ' ';
        else out += c;
    }
    return out + '"';
}

// Each 16-byte BLE block has independent AES-CBC (zero IV) semantics.
// With a single zero-IV block, this is equivalent to AES-128-ECB.
// Long 160-byte notifications must be decrypted block by block, NOT
// as one continuous chained CBC message.
