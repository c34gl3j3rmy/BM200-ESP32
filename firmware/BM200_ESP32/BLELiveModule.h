#pragma once

void performLiveSession() {
    if (!bleReady) return;
    ramCheckpoint("live_debut");
    NimBLEClient* client = nullptr;
    File trace;
    String failure;
    do {
        xQueueReset(blePackets);
        setPhase("Live: recherche BM200");
        NimBLEScan* scan = NimBLEDevice::getScan();
        scan->setActiveScan(true);
        scan->setInterval(100);
        scan->setWindow(80);
        scanCompletion.reason.store(ScanCompletion::Pending);
        const uint32_t tScan = millis();
        if (!scan->start(BLE_SCAN_MS)) { failure = "Echec demarrage scan BLE"; break; }
        while (keepLive.load() && scanCompletion.reason.load() == ScanCompletion::Pending &&
               millis() - tScan < BLE_SCAN_MS + 1000) delay(10);
        if (!keepLive.load()) { scan->stop(); scan->clearResults(); break; }
        NimBLEScanResults results = scan->getResults();
        NimBLEAddress address(BM200_BLE_MAC, 0);
        bool found = false;
        for (int i=0; i<results.getCount(); ++i) {
            const NimBLEAdvertisedDevice* dev = results.getDevice(i);
            if (!dev) continue;
            const String mac(dev->getAddress().toString().c_str());
            addLog("INFO", "Live scan BLE %s RSSI %d dBm", mac.c_str(), dev->getRSSI());
            if (mac.equalsIgnoreCase(BM200_BLE_MAC)) {
                found=true; address=dev->getAddress();
                if (xSemaphoreTake(statusMutex,portMAX_DELAY)==pdTRUE) {
                    status.rssi=dev->getRSSI(); xSemaphoreGive(statusMutex);
                }
            }
        }
        scan->clearResults();
        ramCheckpoint("live_scan_termine");
        if (!found) { failure="BM200 introuvable"; break; }
        if (!keepLive.load()) break;
        client=NimBLEDevice::createClient();
        if (!client) { failure="Client BLE indisponible"; break; }
        client->setConnectTimeout(8000);
        if (!client->connect(address)) { failure="Connexion BLE refusee"; break; }
        setConnected(true);
        ramCheckpoint("live_connecte");
        NimBLERemoteService* service=client->getService(NimBLEUUID("FFF0"));
        if (!service) { failure="Service FFF0 introuvable"; break; }
        NimBLERemoteCharacteristic* tx=service->getCharacteristic(NimBLEUUID("FFF3"));
        NimBLERemoteCharacteristic* rx=service->getCharacteristic(NimBLEUUID("FFF4"));
        if (!tx || !rx || !rx->canNotify() || !rx->subscribe(true,onBLEnotify)) {
            failure="Caracteristiques FFF3 / FFF4 indisponibles"; break;
        }
        setPhase("Live: mesures en direct");
        if (xSemaphoreTake(statusMutex,portMAX_DELAY)==pdTRUE) {
            status.liveSession=true; xSemaphoreGive(statusMutex);
        }
        addLog("INFO", "BM200 Live connecte: interrogation toutes les 2 secondes");
        uint32_t lastRequest=0;
        uint32_t lastSuccess=millis();
        while (keepLive.load() && client->isConnected()) {
            const uint32_t now=millis();
            if (lastRequest==0 || now-lastRequest>=2000) {
                uint8_t cmd[16]; BM200::commandLive(cmd);
                if (!sendCommand(tx,trace,cmd)) { failure="Commande Live refusee"; break; }
                lastRequest=now;
            }
            BlePacket packet{};
            uint8_t decrypted[MAX_BLE_PACKET]{};
            if (receiveDecrypted(packet,decrypted,trace,100)) {
                BM200::Live measurement{};
                if (BM200::decodeLive(decrypted,packet.length,measurement)) {
                    applyLive(measurement);
                    lastSuccess=millis();
                }
            }
            if (millis()-lastSuccess>12000) {
                failure="Plus de mesures Live depuis 12 secondes"; break;
            }
            if (millis()-ramLastCheckpointMs>15000) ramCheckpoint("live_15s");
        }
    } while(false);
    if (client) {
        if (client->isConnected()) client->disconnect();
        NimBLEDevice::deleteClient(client);
    }
    setConnected(false);
    if (xSemaphoreTake(statusMutex,portMAX_DELAY)==pdTRUE) {
        status.liveSession=false;
        status.phase=failure.length()?"Live: erreur":"Live: deconnecte";
        if (failure.length()) status.error=failure;
        xSemaphoreGive(statusMutex);
    }
    if (failure.length()) addLog("ERROR", "Live: %s", failure.c_str());
    else addLog("INFO", "Live: deconnexion normale");
    keepLive.store(false);
    ramCheckpoint("live_deconnecte");
}

void bleWorker(void*) {
    SyncRequest request{};
    for (;;) {
        if (xQueueReceive(requests, &request, portMAX_DELAY) != pdTRUE) continue;
        if (request.operation == OP_LIVE) {
            keepLive.store(true);
            performLiveSession();
            continue;
        }
        if (request.operation == OP_START) {
            String error;
            performStartReport(error);
            continue;
        }
        const uint32_t started = millis();
        String error;
        addLog("INFO", "Synchro #%s: demarrage du moteur BLE", request.id);
        const bool ok = performSync(request, error);
        const unsigned long elapsed = millis() - started;
        if (!ok) addLog("ERROR", "Synchro #%s: echec apres %lu ms (%s): %s",
                        request.id, elapsed, interruptedPhase, error.c_str());
        else addLog("INFO", "Synchro #%s: terminee en %lu ms", request.id, elapsed);
        interruptedSyncMarker = 0;
        if (xSemaphoreTake(statusMutex, portMAX_DELAY) == pdTRUE) {
            status.busy = false;
            status.lastSuccess = ok;
            status.lastDurationMs = millis() - started;
            status.phase = ok ? "Synchronisation terminee" : "Echec de synchronisation";
            status.error = error;
            if (ok) {
                status.lastAtUTC = utcNow();
                status.lastCount = request.count;
            }
            xSemaphoreGive(statusMutex);
        }
        // Start report is now read before disconnect inside performSync().

    }
}

// Home STA has priority; the AP is enabled only while it is unavailable.
// All network operations happen in the main Arduino task, not the BLE worker.
