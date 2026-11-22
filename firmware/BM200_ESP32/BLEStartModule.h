#pragma once

bool saveStartReport(const BM200::StartReport& report, const String& readAt, size_t sampleCount) {
    if (!fsReady) return false;
    File f = LittleFS.open(PATH_START_JSON, "w");
    if (!f) return false;
    f.print("{\"read_utc\":"); f.print(readAt.length() ? jsonString(readAt) : "null");
    f.print(",\"available\":true");
    f.print(",\"result_code\":"); f.print(report.resultCode);
    f.print(",\"minimum_voltage\":"); f.print(report.minimumVoltageCenti / 100.0f, 2);
    f.print(",\"duration_ms\":"); f.print(report.durationMs);
    f.print(",\"age_steps_120s\":"); f.print(report.ageSteps120s);
    f.print(",\"sample_period_verified\":false");
    f.print(",\"samples_centi_volt\":[");
    for (size_t i = 0; i < sampleCount; ++i) {
        if (i) f.print(',');
        f.print(startCurve[i]);
    }
    f.println("]}");
    f.flush();
    const bool ok = f.getWriteError() == 0;
    f.close();
    return ok;
}

// Reuse the authenticated FFF3/FFF4 session immediately after D15505.
// Failure to read D15503 must not invalidate a successfully saved history.
bool readStartInExistingSession(NimBLEClient* client, NimBLERemoteCharacteristic* writer, File& trace) {
    if (!client || !client->isConnected() || !writer) return false;
    setPhase("Demarrage: lecture dans la connexion existante");
    addLog("INFO", "Test demarrage: lecture D15503 sur la connexion BLE existante");
    xQueueReset(blePackets);  // Ignore residual D15505 padding/notifications.
    uint8_t command[16]; BM200::commandStart(command);
    if (!sendCommand(writer, trace, command)) {
        addLog("WARN", "Test demarrage: commande D15503 refusee sur la session existante");
        return false;
    }
    const String readAt = utcNow();
    BM200::StartReport report{};
    uint8_t stream[BM200::START_CURVE_BYTES + MAX_BLE_PACKET + 32]{};
    size_t used = 0, samples = 0;
    bool headerSeen = false, noNew = false, complete = false;
    String error;
    const uint32_t began = millis();
    while (millis() - began < BLE_START_TIMEOUT_MS && client->isConnected()) {
        BlePacket packet{};
        uint8_t data[MAX_BLE_PACKET]{};
        if (!receiveDecrypted(packet, data, trace, 120)) continue;
        BM200::Live live{};
        if (BM200::decodeLive(data, packet.length, live)) {applyLive(live);continue;}
        const uint8_t* payload = data;
        size_t length = packet.length;
        if (!headerSeen) {
            if (!BM200::decodeStartReport(data, packet.length, report)) continue;
            headerSeen = true;
            if (!report.available) { noNew = true; complete = true; break; }
            payload = data + 10;
            length = packet.length > 10 ? packet.length - 10 : 0;
        }
        if (used + length > sizeof(stream)) {error="Courbe trop longue";break;}
        if (length) {memcpy(stream+used,payload,length);used+=length;}
        const int begin = BM200::findMarker(stream, used, 0xFF,0xFF,0xFE);
        if (begin<0) continue;
        const size_t from = static_cast<size_t>(begin)+3;
        const int end = BM200::findMarker(stream, used, 0xFF,0xFE,0xFE,from);
        if (end<0) continue;
        const size_t dataLen = static_cast<size_t>(end)-from;
        if (!BM200::decodeStartCurve(stream+from,dataLen,startCurve,BM200::START_CURVE_POINTS,samples)) {
            error = String("Courbe incomplete: ")+dataLen+" octets";
            break;
        }
        complete=true;break;
    }
    if (!complete && !error.length()) error=headerSeen?"Courbe incomplete ou timeout":"Aucune reponse D15503";
    if (xSemaphoreTake(statusMutex, portMAX_DELAY)==pdTRUE) {
        status.startRead = complete;
        status.startBusy = false;
        status.startNoNewReport = complete && noNew;
        status.startError = error;
        if (complete && report.available) {
            status.startAvailable=true;
            status.startValid = samples==BM200::START_CURVE_POINTS;
            status.startResultCode=report.resultCode;
            status.startMinVoltageCenti=report.minimumVoltageCenti;
            status.startDurationMs=report.durationMs;
            status.startAgeSteps=report.ageSteps120s;
            status.startSampleCount=static_cast<uint16_t>(samples);
            status.startReadUTC=readAt;
        }
        xSemaphoreGive(statusMutex);
    }
    if (complete && report.available && !saveStartReport(report,readAt,samples))
        addLog("WARN", "Test demarrage: sauvegarde du rapport echouee");
    if (noNew) addLog("INFO", "Test demarrage: aucun nouveau rapport, dernier resultat conserve");
    else if (complete) addLog("INFO", "Test demarrage: %u points recus sans nouveau scan BLE", static_cast<unsigned>(samples));
    else addLog("WARN", "Test demarrage apres historique: %s",error.c_str());
    return complete;
}

bool performStartReport(String& error) {
    ramCheckpoint("demarrage_debut");
    NimBLEClient* client = nullptr;
    File trace;
    bool success = false;
    BM200::StartReport report{};
    size_t curveCount = 0;
    uint8_t curveStream[BM200::START_CURVE_BYTES + MAX_BLE_PACKET + 32]{};
    size_t curveStreamLen = 0;
    bool headerSeen = false;
    bool noReport = false;
    String readAt;

    do {
        if (!fsReady) { error = "LittleFS non monte"; break; }
        if (diagnosticLevel == 2) {
            trace = LittleFS.open(PATH_START_TRACE, "w");
            if (trace) trace.println("uptime_ms,direction,bytes,encrypted_hex,decrypted_hex");
            else addLog("WARN", "Trace demarrage indisponible, lecture poursuivie");
        }
        xQueueReset(blePackets);

        setPhase("Demarrage: recherche BM200");
        NimBLEScan* scan = NimBLEDevice::getScan();
        scan->setActiveScan(true);
        scan->setInterval(100);
        scan->setWindow(80);
        scanCompletion.reason.store(ScanCompletion::Pending);
        const uint32_t scanStarted = millis();
        if (!scan->start(BLE_SCAN_MS)) { error = "Echec demarrage scan BLE"; break; }
        while (scanCompletion.reason.load() == ScanCompletion::Pending &&
               millis() - scanStarted < BLE_SCAN_MS + 1000) delay(10);
        const int scanReason = scanCompletion.reason.load();
        if (scanReason != 0 && scanReason != BLE_SCAN_COMPLETED_TIMEOUT) {
            scan->stop(); scan->clearResults();
            error = String("Scan BLE interrompu, code ") + scanReason; break;
        }
        NimBLEScanResults results = scan->getResults();
        NimBLEAddress address(BM200_BLE_MAC, 0);
        bool found = false;
        for (int i = 0; i < results.getCount(); ++i) {
            const NimBLEAdvertisedDevice* dev = results.getDevice(i);
            if (!dev) continue;
            const String mac(dev->getAddress().toString().c_str());
            addLog("INFO", "Test demarrage scan BLE %s RSSI %d dBm", mac.c_str(), dev->getRSSI());
            if (mac.equalsIgnoreCase(BM200_BLE_MAC)) {
                found = true; address = dev->getAddress();
                if (xSemaphoreTake(statusMutex, portMAX_DELAY) == pdTRUE) {
                    status.rssi = dev->getRSSI(); xSemaphoreGive(statusMutex);
                }
            }
        }
        scan->clearResults();
        if (!found) { error = "BM200 introuvable"; break; }

        setPhase("Demarrage: connexion BLE");
        client = NimBLEDevice::createClient();
        if (!client) { error = "Client BLE indisponible"; break; }
        client->setConnectTimeout(8000);
        if (!client->connect(address)) { error = "Connexion BLE refusee"; break; }
        setConnected(true);
        NimBLERemoteService* service = client->getService(NimBLEUUID("FFF0"));
        if (!service) { error = "Service FFF0 introuvable"; break; }
        NimBLERemoteCharacteristic* tx = service->getCharacteristic(NimBLEUUID("FFF3"));
        NimBLERemoteCharacteristic* rx = service->getCharacteristic(NimBLEUUID("FFF4"));
        if (!tx || !rx || !rx->canNotify() || !rx->subscribe(true, onBLEnotify)) {
            error = "Caracteristiques FFF3 / FFF4 indisponibles"; break;
        }

        xQueueReset(blePackets);
        uint8_t cmd[16]; BM200::commandStart(cmd);
        setPhase("Demarrage: lecture du dernier rapport");
        addLog("INFO", "Test demarrage: envoi D1 55 03");
        if (!sendCommand(tx, trace, cmd)) { error = "Commande D15503 refusee"; break; }
        readAt = utcNow();
        const uint32_t started = millis();
        while (millis() - started < BLE_START_TIMEOUT_MS && client->isConnected()) {
            BlePacket packet{};
            uint8_t decoded[MAX_BLE_PACKET]{};
            if (!receiveDecrypted(packet, decoded, trace, 120)) continue;

            BM200::Live live{};
            if (BM200::decodeLive(decoded, packet.length, live)) {
                applyLive(live);
                continue;
            }

            if (!headerSeen) {
                if (!BM200::decodeStartReport(decoded, packet.length, report)) continue;
                headerSeen = true;
                if (!report.available) {
                    noReport = true;
                    addLog("INFO", "Test demarrage: aucun nouveau rapport (D15503FF)");
                    break;
                }
                addLog("INFO", "Rapport demarrage: code=%u mini=%.2f V duree=%u ms age=%u x 120 s",
                       report.resultCode, report.minimumVoltageCenti / 100.0f,
                       report.durationMs, report.ageSteps120s);
                // Bytes 0..9 are the report header. Any remaining bytes can already
                // contain the FFFFFE curve marker or the first curve bytes.
                const size_t extra = packet.length > 10 ? packet.length - 10 : 0;
                if (extra > sizeof(curveStream)) { error = "Courbe demarrage trop longue"; break; }
                if (extra) { memcpy(curveStream, decoded + 10, extra); curveStreamLen = extra; }
            } else {
                if (curveStreamLen + packet.length > sizeof(curveStream)) {
                    error = "Courbe demarrage depasse le format attendu"; break;
                }
                memcpy(curveStream + curveStreamLen, decoded, packet.length);
                curveStreamLen += packet.length;
            }

            const int begin = BM200::findMarker(curveStream, curveStreamLen, 0xFF, 0xFF, 0xFE);
            if (begin >= 0) {
                const size_t dataStart = static_cast<size_t>(begin) + 3;
                const int end = BM200::findMarker(curveStream, curveStreamLen, 0xFF, 0xFE, 0xFE, dataStart);
                if (end >= 0) {
                    const size_t dataLen = static_cast<size_t>(end) - dataStart;
                    if (!BM200::decodeStartCurve(curveStream + dataStart, dataLen, startCurve,
                                                 BM200::START_CURVE_POINTS, curveCount)) {
                        error = String("Courbe demarrage inattendue: ") + dataLen + " octets";
                        break;
                    }
                    addLog("INFO", "Courbe demarrage decodee: %u points", static_cast<unsigned>(curveCount));
                    success = true;
                    break;
                }
            }
        }

        if (noReport) { success = true; break; }
        if (!headerSeen) { error = "Aucune reponse D15503"; break; }
        if (!success && !error.length()) error = "Courbe demarrage incomplete ou timeout";
    } while (false);

    if (client) {
        if (client->isConnected()) client->disconnect();
        NimBLEDevice::deleteClient(client);
    }
    setConnected(false);
    if (trace) { trace.flush(); trace.close(); }

    if (xSemaphoreTake(statusMutex, portMAX_DELAY) == pdTRUE) {
        status.startBusy = false;
        status.startRead = success;
        status.startNoNewReport = success && noReport;
        if (success && report.available) {
            status.startAvailable = true;
            status.startValid = curveCount == BM200::START_CURVE_POINTS;
            status.startResultCode = report.resultCode;
            status.startMinVoltageCenti = report.minimumVoltageCenti;
            status.startDurationMs = report.durationMs;
            status.startAgeSteps = report.ageSteps120s;
            status.startSampleCount = static_cast<uint16_t>(curveCount);
            status.startReadUTC = readAt;
        }
        status.startError = error;
        status.busy = false;
        status.phase = error.length() ? "Demarrage: erreur" : (noReport ? "Demarrage: aucun nouveau rapport" : "Demarrage: rapport lu");
        status.error = error;
        xSemaphoreGive(statusMutex);
    }
    if (success && report.available) {
        if (!saveStartReport(report, readAt, curveCount))
            addLog("WARN", "Rapport demarrage lu mais sauvegarde JSON echouee");
    }
    if (error.length()) addLog("ERROR", "Test demarrage: %s", error.c_str());
    interruptedSyncMarker = 0;
    ramCheckpoint("demarrage_fin");
    return success;
}
