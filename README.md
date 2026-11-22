# BM200 ESP32 - V1.5.2 (PWA, Wi-Fi configurable)

Firmware ESP32 + frontend PWA déployé sur GitHub Pages.

## Installation
1. Arduino IDE : ouvrir `firmware/BM200_ESP32/BM200_ESP32.ino`.
2. Aucun `Secrets.h` à créer : au premier démarrage sans identifiants enregistrés, rejoindre le Wi-Fi ouvert `BM200-ESP32` puis ouvrir `http://192.168.4.1/` pour configurer le Wi-Fi domestique et le Wi-Fi direct. Maintenir BOOT de 5 à moins de 30 secondes puis relâcher pour autoriser les modifications pendant 10 minutes.
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


## V1.5.1 - Paramètres Wi-Fi

- La PWA comporte maintenant deux onglets : Tableau de bord et Paramètres.
- Le Wi-Fi maison et le Wi-Fi direct sont modifiables via l'API et sauvegardés dans la NVS ESP32 (`bm200wifi`). Les mots de passe ne sont jamais renvoyés par l'API.
- Première installation : le réseau direct est ouvert ; maintenir BOOT **5 à 29 secondes puis relâcher** pour autoriser les modifications pendant 10 minutes. Il est conseillé de définir rapidement un mot de passe WPA2.
- BOOT **30 à moins de 60 secondes, puis relâchement** : retire seulement le mot de passe du Wi-Fi direct ; conserve les identifiants du Wi-Fi domestique et les historiques BM200. **À partir de 60 secondes : aucune action.**
- Après un changement Wi-Fi, le navigateur perd temporairement la liaison ; il faut reconnecter le téléphone.
- En l'absence d'accès GitHub Pages, ouvrir `http://192.168.4.1/` pour configurer les réseaux depuis la page locale de secours.
- Les paramètres sont protégés par l'autorisation physique du bouton BOOT. **Attention** : pendant cette fenêtre, les mots de passe sont transmis via HTTP ; le Wi-Fi direct ouvert ne chiffre pas le trafic. À utiliser dans un environnement de confiance. Ne pas exposer le port 80 sur Internet.
- Le bouton BOOT agit uniquement **au relâchement** : 0 à moins de 5 s = aucune action ; 5 à moins de 30 s = bascule du mode Wi-Fi et autorisation des réglages pendant 10 minutes ; 30 à moins de 60 s = suppression du mot de passe direct ; 60 s et plus = aucune action.

Le firmware doit être compilé et téléversé séparément de la PWA. Tests statiques exécutables via `python tests/check_offline.py` ; la compilation sur la carte et les tests Wi-Fi réels restent nécessaires.

## V1.5.2 - Première configuration et plages du bouton BOOT

- **Plus de dépendance à `Secrets.h`** : aucun identifiant Wi-Fi n'est intégré au code source ou nécessaire à la compilation. Les valeurs déjà enregistrées dans la NVS restent utilisées après une mise à jour.
- Sans Wi-Fi maison enregistré, démarrage immédiat du point d'accès `BM200-ESP32` (ou du SSID personnalisé en NVS) ; sans mot de passe direct enregistré, le réseau est ouvert.
- Les changements de SSID et de mot de passe se font depuis l'onglet **Paramètres** de la PWA ou la page locale `http://192.168.4.1/`, après autorisation avec le bouton BOOT.
- L'action BOOT est évaluée **au relâchement** : 5-29,999 s = changement du mode Wi-Fi et autorisation des paramètres pendant 10 min ; 30-59,999 s = suppression **du seul mot de passe Wi-Fi direct** ; 60 s ou plus = aucune action. Le Wi-Fi maison et les historiques ne sont jamais réinitialisés par ce bouton.
- Pour utiliser le bouton BOOT, attendre que l'ESP32 ait démarré : maintenir GPIO0/BOOT pendant une remise sous tension peut activer le chargeur de démarrage ESP32.
- **Attention** : le réseau direct ouvert ne chiffre pas le trafic HTTP. Pour une utilisation quotidienne, définir un mot de passe WPA2 avant de laisser le logger dans la voiture.

### Test de validation sur la carte

1. Conserver **Huge APP (3MB No OTA/1MB SPIFFS)** et ne pas activer l'effacement complet de la Flash ; exporter auparavant les historiques importants.
2. Televerser, attendre le message de demarrage `V1.5.2`, puis verifier que l'API `/api/status` repond en Wi-Fi direct (couper temporairement les donnees mobiles si Android change de reseau).
3. Maintenir BOOT pendant environ **8 secondes**, relacher, ouvrir `http://192.168.4.1/` puis configurer les identifiants Wi-Fi dans les dix minutes.
4. Avec un mot de passe direct enregistre, maintenir BOOT pendant **35 secondes**, relacher, puis verifier que le reseau direct est devenu ouvert sans que les reglages domestiques ni les historiques n'aient disparu.
5. Si souhaite, verifier qu'un relachement apres **60 secondes ou plus** ne modifie pas le mot de passe. Ce test est facultatif compte tenu de la duree.

Les tests du depot sont hors materiel : ils ne remplacent pas une compilation Arduino Core 4.0.0 ni des essais sur l'ESP32.
