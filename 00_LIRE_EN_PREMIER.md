# BM200 ESP32 - V1.5.4 (essai materiel)

Cette archive inclut le firmware, la PWA, les tests et le portail captif qui a fonctionne sur HyperOS. Le firmware a ete modifie mais **pas compile ni teste sur carte dans cet environnement**.

## Installation

1. Conserver une sauvegarde du firmware V1.5.3 fonctionnel et des historiques LittleFS.
2. Ouvrir `firmware/BM200_ESP32/BM200_ESP32.ino` dans Arduino IDE.
3. Ne pas ajouter `Secrets.h` : il n'existe plus.
4. Selectionner ESP32 Dev Module, Arduino ESP32 Core 4.0.0, Huge APP (3MB No OTA/1MB SPIFFS), PSRAM Disabled.
5. Compiler / televerser, puis controler le moniteur serie a 115200 bauds.
6. Au premier demarrage sans NVS Wi-Fi, se connecter a `BM200-ESP32`, mot de passe **BM200-Setup** (mot de passe provisoire public a remplacer).
7. Ouvrir `http://192.168.4.1/config` ou la PWA. Renseigner les parametres et cliquer « Preparer ».
8. Maintenir **IO0 pendant 2 secondes** dans la minute suivant la preparation. IO0 5 secondes sans demande en cours conserve sa fonction de bascule AP/STA.
9. Apres basculement, si Wi-Fi maison choisi, reconnecter le telephone sur le reseau maison, configurer la nouvelle adresse IP et cliquer « Confirmer la connexion Wi-Fi » dans les **120 secondes**. Sans confirmation, les anciens identifiants Wi-Fi sont restaures (sans effacer LittleFS).
10. Modifier le mot de passe du point d'acces de secours lors de la premiere configuration, via la meme validation IO0. Le mot de passe AP est enregistre en NVS.

## Avertissements

- Configuration et historique sont dans des partitions distinctes. **Ne pas effacer la Flash ni modifier les partitions sans sauvegarde**.
- La page locale `/config` permet la configuration sans Internet. La PWA n'enregistre aucun mot de passe Wi-Fi dans IndexedDB.
- NVS n'est pas chiffree par defaut sur un ESP32 Arduino standard. Ne pas exposer l'API a Internet. L'IO0 physique est une preuve de presence, pas une protection cryptographique des echanges HTTP.
- Le mode diagnostic `Complet` repasse automatiquement en `Normal` apres 15 minutes, mais peut generer plus d'ecritures LittleFS pendant son activation.
- Les fonctionnalites BLE, historique et portail doivent etre revalidees sur carte ; aucun essai physique n'a ete effectue dans cet environnement.
