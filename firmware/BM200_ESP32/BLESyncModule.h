#pragma once

bool performSync(const SyncRequest& sync, String& error) {
    const uint16_t slots = sync.count;
    uint16_t decoded = 0;
    setPhase("Preparation de la trace BLE");
    File trace;
    NimBLEClient* client = nullptr;
    bool success = false;
    const uint32_t droppedBefore = droppedPackets;
    String capturedAt;
    uint16_t seen = 0;
    bool sawAck = false;
    bool sawEnd = false;
    bool nonzeroPadding = false;
    uint32_t trailerBytes = 0;
    const uint32_t expectedBytes = static_cast<uint32_t>(slots) * 4;

    ramCheckpoint("sync_debut");
    do {
        if (!fsReady) { error = "LittleFS non monte; stockage impossible"; break; }
        trace = LittleFS.open(PATH_TRACE_TEMP, "w");
        if (!trace) { error = "Ouverture de la trace BLE impossible"; break; }
        trace.println("uptime_ms,direction,bytes,encrypted_hex,decrypted_hex");
        xQueueReset(blePackets);

        ramCheckpoint("avant_scan_ble");
        setPhase("Recherche BLE du BM200");
        addLog("INFO", "Scan BLE pour %s (max %lu ms)", BM200_BLE_MAC, static_cast<unsigned long>(BLE_SCAN_MS));
        NimBLEScan* scan = NimBLEDevice::getScan();
        scan->setActiveScan(true);
        scan->setInterval(100);
        scan->setWindow(80);
        scanCompletion.reason.store(ScanCompletion::Pending);
        const uint32_t scanStarted = millis();
        if (!scan->start(BLE_SCAN_MS)) {
            error = "NimBLE a refuse le demarrage du scan";
            break;
        }
        while (scanCompletion.reason.load() == ScanCompletion::Pending &&
               millis() - scanStarted < BLE_SCAN_MS + 1000) {
            delay(10);
        }
        const int scanReason = scanCompletion.reason.load();
        if (scanReason != 0 && scanReason != BLE_SCAN_COMPLETED_TIMEOUT) {
            scan->stop();
            scan->clearResults();
            error = String("Scan BLE interrompu, code ") + scanReason;
            break;
        }
        NimBLEScanResults results = scan->getResults();
        const int deviceCount = results.getCount();
        bool found = false;
        NimBLEAddress foundAddress(BM200_BLE_MAC, 0);
        int rssi = -127;
        for (int i = 0; i < deviceCount; ++i) {
            const NimBLEAdvertisedDevice* device = results.getDevice(i);
            if (!device) continue;
            const String addr(device->getAddress().toString().c_str());
            const bool isTarget = addr.equalsIgnoreCase(BM200_BLE_MAC);
            std::string name = device->getName();
            for (char& c : name) {
                if (static_cast<unsigned char>(c) < 32 || c == 127) c = ' ';
            }
            addLog("INFO", "BLE %s | %d dBm | %.56s%s", addr.c_str(), device->getRSSI(),
                   name.empty() ? "(sans nom)" : name.c_str(), isTarget ? " | BM200 cible" : "");
            if (isTarget) {
                foundAddress = device->getAddress(); // Includes PUBLIC/RANDOM address type.
                rssi = device->getRSSI();
                found = true;
            }
        }
        scan->clearResults();
        ramCheckpoint("apres_scan_ble");
        addLog("INFO", "Scan termine en %lu ms: %d adresse(s), BM200 %s",
               static_cast<unsigned long>(millis() - scanStarted), deviceCount,
               found ? "trouve" : "absent");
        if (!found) { error = "BM200 introuvable: verifier sa proximite et fermer ANCEL"; break; }
        addLog("INFO", "BM200 repere, RSSI %d dBm, type adresse %d", rssi, foundAddress.getType());
        if (xSemaphoreTake(statusMutex, portMAX_DELAY) == pdTRUE) {
            status.rssi = rssi;
            xSemaphoreGive(statusMutex);
        }

        setPhase("Connexion et decouverte GATT");
        client = NimBLEDevice::createClient();
        if (!client) { error = "Creation client NimBLE impossible"; break; }
        client->setConnectTimeout(8000);
        const uint32_t connectionStarted = millis();
        if (!client->connect(foundAddress)) { error = "Connexion BLE refusee/expiree"; break; }
        setConnected(true);
        ramCheckpoint("ble_connecte");
        addLog("INFO", "BLE connecte en %lu ms, MTU %u",
               static_cast<unsigned long>(millis() - connectionStarted), client->getMTU());
        setPhase("Decouverte du service FFF0 et des caracteristiques");
        NimBLERemoteService* service = client->getService(NimBLEUUID("FFF0"));
        if (!service) { error = "Service FFF0 introuvable"; break; }
        NimBLERemoteCharacteristic* writer = service->getCharacteristic(NimBLEUUID("FFF3"));
        NimBLERemoteCharacteristic* notify = service->getCharacteristic(NimBLEUUID("FFF4"));
        if (!writer || !notify) { error = "FFF3 ou FFF4 introuvable"; break; }
        addLog("INFO", "Service FFF0 et caracteristiques FFF3/FFF4 trouves");
        setPhase("Activation des notifications FFF4");
        if (!notify->canNotify() || !notify->subscribe(true, onBLEnotify)) {
            error = "Impossible d'activer les notifications FFF4"; break;
        }
        addLog("INFO", "Notifications FFF4 actives");
        ramCheckpoint("notifications_actives");

        uint8_t request[16];
        BM200::commandLive(request);
        setPhase("Lecture instantanee");
        if (!sendCommand(writer, trace, request)) {
            error = "Ecriture de la commande D15507 echouee"; break;
        }
        addLog("INFO", "Commande instantanee envoyee");
        const uint32_t liveStart = millis();
        bool gotLiveFrame = false;
        bool gotLive = false;
        while (millis() - liveStart < BLE_LIVE_TIMEOUT_MS && client->isConnected()) {
            BlePacket packet{};
            uint8_t decoded[MAX_BLE_PACKET]{};
            if (!receiveDecrypted(packet, decoded, trace, 100)) continue;
            if (!gotLiveFrame) {
                gotLiveFrame = true;
                addLog("INFO", "Premiere trame instantanee apres %lu ms",
                       static_cast<unsigned long>(millis() - liveStart));
            }
            BM200::Live live{};
            if (BM200::decodeLive(decoded, packet.length, live)) {
                applyLive(live);
                gotLive = true;
                addLog("INFO", "Instantane: %.2f V, %u %%, %d C",
                       live.voltageCenti / 100.0f, live.soc, live.temperatureC);
                break;
            }
        }
        ramCheckpoint("apres_instantane");
        if (!gotLive) addLog("WARN", "%s apres %lu ms",
                             gotLiveFrame ? "Aucune mesure instantanee decodee" : "Aucune trame instantanee exploitable",
                             static_cast<unsigned long>(millis() - liveStart));
        if (!client->isConnected()) { error = "BM200 deconnecte avant l'historique"; break; }

        xQueueReset(blePackets);
        capturedAt = utcNow(); // Actual history request time, not the older measurement times.
        BM200::commandHistory(slots, request);
        setPhase("Telechargement historique");
        addLog("INFO", "Demande historique de %u emplacements", slots);
        if (!sendCommand(writer, trace, request)) {
            error = "Ecriture de la commande D15505 echouee"; break;
        }
        addLog("INFO", "Commande historique envoyee");
        ramCheckpoint("historique_demarre");
        const uint32_t startedAt = millis();
        bool gotHistoryFrame = false;
        size_t bytesReceived = 0;
        // Snapshot RAM in memory during BLE bursts; flush later so flash writes
        // cannot block notification processing and overflow the BLE queue.
        struct MemorySample { uint16_t slots; uint32_t freeBytes; uint32_t largest; };
        MemorySample samples[8]{};
        uint8_t sampleCount = 0;
        uint16_t nextSampleAt = 100;
        bool receivedUnrecognized = false;
        while (millis() - startedAt < BLE_HISTORY_TIMEOUT_MS && client->isConnected()) {
            BlePacket packet{};
            uint8_t decoded[MAX_BLE_PACKET]{};
            if (!receiveDecrypted(packet, decoded, trace, 150)) continue;

            if (!gotHistoryFrame) {
                gotHistoryFrame = true;
                addLog("INFO", "Premiere trame historique apres %lu ms",
                       static_cast<unsigned long>(millis() - startedAt));
            }
            BM200::Live live{};
            if (BM200::decodeLive(decoded, packet.length, live)) {
                applyLive(live);
                continue;
            }
            if (BM200::isHistoryAck(decoded, packet.length, slots)) {
                sawAck = true;
                addLog("DEBUG", "ACK historique de %u emplacements", slots);
                continue;
            }
            uint32_t useful = 0;
            if (BM200::isHistoryEnd(decoded, packet.length, useful)) {
                trailerBytes = useful;
                sawEnd = true;
                addLog("DEBUG", "Fin historique: %lu octets utiles", static_cast<unsigned long>(useful));
                break;
            }
            if (decoded[0] == 0xD1 && decoded[1] == 0x55) {
                receivedUnrecognized = true;
                continue; // Other command response, not a historical sample block.
            }
            if (!sawAck) {
                receivedUnrecognized = true;
                continue;
            }
            const size_t remaining = expectedBytes > bytesReceived ? expectedBytes - bytesReceived : 0;
            const size_t copyLen = packet.length < remaining ? packet.length : remaining;
            if (copyLen) memcpy(historyRaw + bytesReceived, decoded, copyLen);
            bytesReceived += copyLen;
            // Last data notification is usually zero-padded to 160 bytes.
            for (size_t j = copyLen; j < packet.length; ++j)
                if (decoded[j] != 0) nonzeroPadding = true;
            const uint16_t nowSeen = static_cast<uint16_t>(bytesReceived / 4);
            if (nowSeen >= nextSampleAt && sampleCount < 8) {
                samples[sampleCount++] = {nowSeen, ESP.getFreeHeap(),
                    heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)};
                nextSampleAt = static_cast<uint16_t>(nextSampleAt + 100);
            }
            if (nowSeen != seen) { seen = nowSeen; setReceived(seen); }
        }
        // Replay lightweight snapshots to serial and LittleFS after BLE transfer.
        for (uint8_t k = 0; k < sampleCount; ++k) {
            addLog("RAM", "Historique %u emplacements: libre=%lu Ko, bloc=%lu Ko",
                samples[k].slots, static_cast<unsigned long>(samples[k].freeBytes/1024),
                static_cast<unsigned long>(samples[k].largest/1024));
            if (fsReady) {
                File measureLog = LittleFS.open(PATH_RAM_DIAG, "a");
                if (measureLog) {
                    measureLog.print(millis()); measureLog.print(',');
                    measureLog.print(utcNow()); measureLog.print(",historique_");
                    measureLog.print(samples[k].slots); measureLog.print(',');
                    measureLog.print(samples[k].freeBytes); measureLog.print(',');
                    measureLog.print(ESP.getMinFreeHeap()); measureLog.print(',');
                    measureLog.print(samples[k].largest); measureLog.println(",0");
                    measureLog.close();
                }
            }
        }
        ramCheckpoint("historique_recu");
        if (!sawAck) { error = "ACK D15505 absent"; break; }
        if (!sawEnd) { error = "Fin FF FE FE absente ou timeout"; break; }
        if (trailerBytes != expectedBytes) { error = "Compteur de fin different de la demande"; break; }
        if (bytesReceived != expectedBytes) { error = "Transfert incomplet: octets manquants"; break; }
        if (droppedPackets != droppedBefore) { error = "Perte de notifications BLE; transfert invalide"; break; }
        if (nonzeroPadding) addLog("WARN", "Octets non nuls apres la longueur utile: voir trace");
        if (receivedUnrecognized) addLog("WARN", "Notifications hors historique recues: voir trace");

        setPhase("Sauvegarde des donnees");
        ramCheckpoint("avant_sauvegarde");
        if (!storeHistory(slots, capturedAt, error, decoded)) break;
        ramCheckpoint("apres_sauvegarde");
        success = true;
        // D15503 before disconnect: avoids a second scan/GATT discovery and
        // still stores history if the start report is unavailable.
        readStartInExistingSession(client, writer, trace);
    } while (false);

    if (client) {
        if (client->isConnected()) client->disconnect();
        NimBLEDevice::deleteClient(client);
    }
    setConnected(false);
    ramCheckpoint("apres_deconnexion_ble");
    if (trace) {
        trace.flush();
        trace.close();
        if (fsReady) {
            LittleFS.remove(PATH_TRACE);
            if (!LittleFS.rename(PATH_TRACE_TEMP, PATH_TRACE))
                addLog("WARN", "Impossible de conserver la nouvelle trace BLE");
        }
    }
    addLog("INFO", "Synchro #%s: %u demandes, %u recus, %u decodes, %u enregistres",
           sync.id, slots, seen, decoded, success ? slots : 0);
    ramCheckpoint("sync_fin");
    return success;
}

// Dedicated manual BLE session. No history downloads are performed in live mode.
// We use the same target / GATT / AES path that succeeded in V1.2.1.
