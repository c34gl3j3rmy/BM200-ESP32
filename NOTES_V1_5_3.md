# V1.5.3 - Correctif Wi-Fi Core 4 (RSN Override)

## Diagnostic valide sur la carte

- Avec Core 3.3.13, le STA fonctionne sur `WifiGG`.
- Avec Core 4.0.0, un croquis minimal echoue avec la raison WPA 17.
- En ajoutant `disable_wpa3_compatible_mode=1` avant `esp_wifi_connect()`,
  un croquis minimal sous Core 4.0.0 repond `PONG CORE 4` sur 192.168.1.180.
- La Freebox reste en WPA2/3 Compatibilite avec GCMP-256 active.

## Modifications V1.5.3

1. Sous Core 4, STA configuree sans demarrage de connexion immediat.
2. Lire puis modifier uniquement `disable_wpa3_compatible_mode`.
3. Lancer explicitement `esp_wifi_connect()` apres `esp_wifi_set_config()`.
4. Appliquer la meme sequence au demarrage ET lors du retour AP -> STA.
5. En cas de perte STA, tenter une seule reconnexion Core 4 avant secours AP.
6. Sous Core 3, conserver `WiFi.begin()` standard.
7. Ne pas changer l'AP seul, les routes HTTP, le BLE ou les fichiers LittleFS.
8. Tester la gestion de transitions reseau sur deux mocks de Core (3 et 4).

## Essais a faire sur le materiel

1. Sauvegarder les fichiers d'historique si possible.
2. Utiliser Core 4.0.0, ESP32 Dev Module, Huge APP, Erase All Flash: Disabled.
3. Placer `Secrets.h` dans `firmware/BM200_ESP32/` (jamais sur GitHub).
4. Compiler et televerser. Lire les logs (115200):
   `Wi-Fi Core4: RSN Override desactive (WPA2) cote ESP32`.
5. Verifier `http://192.168.1.xxx/api/ping` et `/api/status`.
6. Verifier BLE et les 709 enregistrements precedents dans LittleFS.
7. BOOT 5 s: associer le telephone a `BM200-ESP32`, puis verifier
   `http://192.168.4.1/api/ping` et `/api/status`.
8. BOOT 5 s de nouveau: verifier le retour a WifiGG et le second ping.

Le test minimal valide le contournement STA sous Core 4, mais PAS encore
le HTTP en AP ni le fonctionnement du firmware complet.
