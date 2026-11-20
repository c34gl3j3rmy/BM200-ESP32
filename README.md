# BM200 ESP32 - V1.5.0 (migration PWA, phase 1)

Firmware ESP32 + frontend PWA déployé sur GitHub Pages.

## Installation
1. Arduino IDE : ouvrir `firmware/BM200_ESP32/BM200_ESP32.ino`.
2. Copier `Secrets.example.h` en `Secrets.h` et y saisir les informations privées (ne jamais publier ce dernier).
3. Conserver ESP32 Core 4.0.0, NimBLE-Arduino 2.5.x, ESP32 Dev Module et partition Huge APP selon flash.
4. Créer un dépôt GitHub, envoyer les fichiers, activer **Settings > Pages > Source: GitHub Actions**.
5. Ouvrir l’URL GitHub Pages dans Chrome, installer la PWA et configurer l’adresse API (par défaut `http://192.168.4.1`).
6. Autoriser l'accès au réseau local si le navigateur le demande. Chrome/LNA reste à tester sur l'appareil réel ; HTTPS→HTTP local peut ne pas fonctionner sur certains navigateurs/versions.

## Limites et sécurité
- **Phase 1 de migration** : l'interface a été externalisée et la sauvegarde IndexedDB des mots historiques est disponible ; la synchro BLE incrémentale reste **à développer**. La commande D15503 est maintenant lancée dans la connexion utilisée par la synchronisation (à valider sur appareil).
- Les 709 mots de 4 octets sont transmis via `/api/history.bin` sans fabrication d'un grand JSON ; conservation des marqueurs et ordre du CSV. Les horodatages historiques ne sont pas inventés.
- La PWA utilise le stockage navigateur. Une capture peut contenir des mots déjà acquis ; aucune fusion automatique n'est tentée sans identifiants de mesure fiables.
- L’ESP32 conserve ses fichiers LittleFS comme sécurité tant que le téléphone n’a pas acquis et sauvegardé les données.
- Cette première version utilise `Access-Control-Allow-Origin: *` pour faciliter le test avec un dépôt GitHub Pages encore inconnu : **ne pas exposer ce serveur à Internet**, ne pas l'utiliser sur un réseau partagé non fiable. Renforcer l’authentification et CORS avant tout accès réseau non contrôlé.
- Le protocole AES/commandes reste inchangé. Le moteur BLE enchaîne D15503 après D15505 avant la déconnexion, avec conservation du bouton Forcer reconnexion.
- Le Service Worker met uniquement les fichiers de la PWA en cache, jamais les requêtes à l’ESP32.
- La compilation ESP32 Core 4.0.0 et le fonctionnement Chrome LNA restent à valider sur appareil réel.

## Tests locaux (sans ESP32)
```sh
python3 tests/check_offline.py
python3 tests/check_core4_crypto.py
```
Le test du navigateur Chrome et une compilation/téléversement ESP32 doivent être faits sur le matériel.

## Étapes de validation (dans cet ordre)
1. Sauvegarder les fichiers CSV/JSON de l’ESP32 actuellement opérationnel.
2. Vérifier l’installation PWA et son cache hors ligne (mode avion, page déjà installée).
3. Vérifier l’accès HTTPS GitHub Pages → HTTP local sur **Chrome Android actuel** : permission locale, CORS, préflight Private Network Access. Si le navigateur refuse la connexion, conserver la V1.4.4 fonctionnelle et ne pas migrer sans un accès HTTPS local adapté.
4. Compiler et téléverser la V1.5.0, lire `/api/status` et `/api/logs` en local.
5. Tester Live, synchronisation 10 mesures et lecture D15503 sur une seule connexion BLE, puis 709 mesures.
6. Archiver une capture depuis la PWA et exporter l’archive JSON, puis couper le réseau et vérifier le chargement de l’UI.

**Non validé dans cette archive** : compilation Arduino réelle, test HTTPS→HTTP Chrome, mesures de gain RAM/Flash et tests BLE sur BM200.
