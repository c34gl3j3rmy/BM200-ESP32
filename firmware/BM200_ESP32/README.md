# Firmware ANCEL BM200 ESP32 V1.5.2

Configuration recommandee pour ESP32-WROOM-32 classique :

| Parametre | Valeur |
| --- | --- |
| Carte | ESP32 Dev Module |
| Core | 3.3.13 ou 4.0.0 (a valider sur carte) |
| Bibliotheque | NimBLE-Arduino 2.5.2 |
| Flash | 4 MB |
| Partitions | Huge APP (3MB No OTA / 1MB SPIFFS) |
| PSRAM | Disabled |
| Erase All Flash | Disabled |

Le fichier prive `Secrets.h` doit etre copie ici depuis l'ancien projet ;
`Secrets.example.h` contient des valeurs fictives. Ne jamais committer
`Secrets.h` ni copier son contenu dans les journaux ou les issues GitHub.

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

**Statut des essais :** logique reseau verifiee par tests C++ avec interfaces
simulees, mais pas encore compilee ni testee sur ESP32 reel. Le mode AP
et Core 4.0.0 doivent etre verifies physiquement.
