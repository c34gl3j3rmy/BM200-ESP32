# BM200 ESP32 - V1.5.2 (Wi-Fi STA + AP de secours)

Passerelle locale BLE / Wi-Fi pour le moniteur de batterie ANCEL BM200. Le firmware
ESP32 collecte des mesures, conserve un historique LittleFS et expose une API HTTP.
La PWA statique dans `web/` est publiee separement via GitHub Pages.

## Ce qui change dans la V1.5.2

- Le Wi-Fi domestique est initialise en premier, comme dans la V1.5.1 validee
  sur `WifiGG`. Le serveur HTTP n'est ouvert qu'apres connexion au reseau.
- Si la connexion domestique echoue apres 20 s (ou disparait pendant 12 s),
  basculement dans un veritable `WIFI_AP`, sans connexion STA simultanee.
- Le point d'acces `BM200-ESP32` utilise `http://192.168.4.1`.
- Appui long de 5 s sur BOOT/IO0 : maison -> secours force, ou secours ->
  nouvel essai de connexion maison. Ne pas maintenir BOOT pendant un reset.
- Sans client connecte au secours, essai automatique du Wi-Fi domestique au
  bout de 15 min ; avec un client, l'AP n'est pas interrompu.
- Pas de `WiFi.reconnect()` repetitif pendant le secours.
- Compatible au niveau du code avec Arduino ESP32 Core 3.3.13 (mbedTLS AES)
  et 4.0.0 (PSA Crypto AES), avec NimBLE-Arduino 2.5.2.
- Protection de LittleFS : pas de formatage automatique si le montage echoue.

## Installation

1. Sauvegarder les donnees existantes depuis l'API qui fonctionne :
   `/download/history.csv`, `/download/history.json` et `/download/ram.csv`.
2. Decompresser le ZIP et copier l'ancien `Secrets.h` prive dans
   `firmware/BM200_ESP32/` (le ZIP ne contient aucun mot de passe).
3. Ouvrir `firmware/BM200_ESP32/BM200_ESP32.ino` dans Arduino IDE.
4. Choisir ESP32 Dev Module, Flash 4 MB, Huge APP (3 MB No OTA / 1 MB SPIFFS),
   PSRAM Disabled et Erase All Flash **Disabled**.
5. Conserver d'abord le Core 3.3.13 pour tester le retour du secours AP.
   Si l'API maison reste OK, passer ensuite au Core 4.0.0 sans autre changement.
6. Installer ou conserver `NimBLE-Arduino` **2.5.2**.
7. Ouvrir le Moniteur serie a 115200 bauds ; lire la version Core et l'IP.

## Validation minimale (sans PWA)

- Sur le Wi-Fi maison : ouvrir `http://<IP-ESP32>/api/ping` puis `/api/status`.
- Eloigner l'ESP32 du reseau domestique ou maintenir BOOT pendant 5 s apres
  le demarrage : le secours `BM200-ESP32` doit apparaitre et afficher une IP
  `192.168.4.1` dans le journal.
- Depuis un telephone connecte a ce reseau, ouvrir
  `http://192.168.4.1/api/ping`. Resultat attendu : `PONG`.
- En secours, maintenir BOOT pendant 5 s : nouvelle tentative `WifiGG`.
- Verifier ensuite les fonctions BLE et les exports d'historique.

**Important :** des essais precedents avec un croquis AP minimal renvoyaient
`ERR_CONNECTION_RESET`, meme sans code BM200. La cause materielle / pile Wi-Fi /
telephone n'a PAS ete identifiee. La nouvelle logique evite les reconnexions
concurrentes mais ne garantit pas que ce symptome est corrige.

## Tests et compilation

Tests locaux independants du materiel :

```sh
python3 tests/check_offline.py
python3 tests/check_core4_crypto.py
```

La workflow GitHub Actions `Compiler le firmware BM200` est disponible en
lancement manuel pour tenter une compilation des Core 3.3.13 et 4.0.0.
L'execution sur la carte et le test du mode AP ne peuvent pas etre simules.

La compilation Arduino et le fonctionnement reel sur Core 4.0.0 doivent etre
verifies sur le materiel avant de considerer la migration comme validee.

## Depot / PWA

- `firmware/BM200_ESP32/` : croquis et modules embarques.
- `web/` : PWA statique et stockage navigateur IndexedDB.
- `tests/` : tests du protocole et de la machine d'etats Wi-Fi.
- `.github/workflows/pages.yml` : publication automatique de la PWA.
- `.github/workflows/firmware-compile.yml` : compilation manuelle optionnelle.

La PWA utilise une adresse d'API configurable : renseigner l'adresse locale
reelle de l'ESP32 quand il est sur `WifiGG` et `http://192.168.4.1` en secours.
L'acces HTTPS de GitHub Pages a une API HTTP locale peut etre restreint par
le navigateur ; toujours commencer les tests avec l'adresse HTTP directe.

Securite : le serveur HTTP local ne possede pas d'authentification ; ne pas
l'exposer a Internet ou a un reseau Wi-Fi public. Conserver `Secrets.h`
strictement en local ; le depot et l'archive ne contiennent aucun identifiant.
