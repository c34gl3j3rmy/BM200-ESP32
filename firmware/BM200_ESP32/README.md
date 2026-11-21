# Firmware ANCEL BM200 ESP32 V1.5.3

Configuration recommandee pour ESP32-WROOM-32 classique :

| Parametre | Valeur |
| --- | --- |
| Carte | ESP32 Dev Module |
| Core | 4.0.0 recommande pour ce correctif; 3.3.13 conserve le demarrage classique |
| Bibliotheque | NimBLE-Arduino 2.5.2 |
| Flash | 4 MB |
| Partitions | Huge APP (3MB No OTA / 1MB SPIFFS) |
| PSRAM | Disabled |
| Erase All Flash | Disabled |

Le fichier prive `Secrets.h` doit etre copie ici depuis l'ancien projet ;
`Secrets.example.h` contient des valeurs fictives. Ne jamais committer
`Secrets.h` ni copier son contenu dans les journaux ou les issues GitHub.

## Correctif Wi-Fi Core 4.0.0 (Freebox WPA2/3 Compatibilite)

Un croquis minimal sur cet ESP32 classique sous Core 4.0.0 a reproduit
les erreurs `IE_IN_4WAY_DIFFERS` (raison 17). En preparant la STA avec
`WiFi.begin(..., false)`, puis en definissant
`cfg.sta.disable_wpa3_compatible_mode = 1` via `esp_wifi_set_config()`
avant `esp_wifi_connect()`, le test a retourne `PONG CORE 4` a
`http://192.168.1.180/api/ping` sans modifier les reglages Freebox.

V1.5.3 reprend cette sequence pour chaque connexion domestique sous Core 4.
Ce reglage ne desactive pas WPA3 sur la Freebox. Il conduit uniquement
cet ESP32 a utiliser WPA2 sur le reseau mixte.

**Validation restante :** ce correctif est prouve sur le croquis minimal,
mais pas encore sur le firmware BM200 complet. Le mode AP doit aussi etre
teste reellement. Le bogue exact de la pile WPA3 / RSN Override n'est pas
prouve independamment du contournement.

## Comportement reseau

- STA prioritaire : attend au plus 20 s `WifiGG` au demarrage.
- HTTP commence apres que le reseau soit pret ; BLE demarre ensuite.
- Si STA echoue ou si le reseau domestique est perdu, AP seul `BM200-ESP32`
  sur `192.168.4.1`. Pas de scan domestique en parallele dans ce mode.
- IO0 / BOOT, appui de 5 s apres le boot : bascule STA vers secours force,
  ou secours vers nouvel essai STA.
- En secours automatique, sans client, le STA est tente apres 15 min. Les
  clients AP actifs empechent ce changement ; le secours force aussi.
- L'API de diagnostic `/api/ping` doit renvoyer `PONG`.

## Historique et precaution de migration

Conserver Huge APP et desactiver l'effacement total. Le programme appelle
`LittleFS.begin(false)` par defaut pour ne pas formater le stockage apres
une erreur de montage. Sur une carte neuve, un formatage initial de LittleFS
peut etre necessaire : mettre temporairement `ALLOW_LITTLEFS_AUTOFORMAT`
a `true` dans `Config.h` seulement si le stockage est vide et sauvegarde.

Avant tout changement de Core, exporter depuis la V1.5.1 fonctionnelle :
`/download/history.csv` et `/download/history.json`.

## API

- `/` : page de secours minimale.
- `/api/ping` : verification HTTP sans JSON.
- `/api/status` : Wi-Fi, BLE, stockage, statut synchronisation.
- `/api/logs` : journal.
- `/api/sync`, `/api/live`, `/api/start`, `/api/ble/recover` : commandes.
- `/api/history.bin`, `/download/history.csv`, `/download/history.json` : historique.
- `/download/ram.csv`, `/download/trace.csv` : diagnostics.

## Compatibilite BLE / AES

Le code conserve les commandes de lecture BLE connues, l'interface NimBLE
2.x et la reservation memoire BLE requise sur le Core 4.0.0. Sur Core 4,
le chiffrement utilise PSA Crypto ; sur Core 3, mbedTLS AES. Aucun changement
des commandes ou des fichiers LittleFS n'est introduit par la migration.

**Statut des essais :** tests de protocole, JavaScript et logique reseau
simulee disponibles en local. Le croquis de diagnostic a ete teste sur
ESP32 reel sous Core 4, mais V1.5.3 doit encore etre compilee et validee
sur le materiel, notamment BLE, STA et AP seul.
