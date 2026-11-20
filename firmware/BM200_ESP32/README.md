# BM200 / ESP32 - V1.5.0 (version expérimentale)

Projet Arduino a nom fixe : `BM200_ESP32/BM200_ESP32.ino`.

## Installation (Arduino IDE 2.3.x)

1. Decompresser l'archive dans un emplacement fixe. Ouvrir `BM200_ESP32.ino`.
2. IMPORTANT : copier votre ancien fichier prive `Secrets.h` dans ce dossier. Il n'est **pas** inclus dans le ZIP. `Secrets.example.h` est seulement un modele. Ne pas mettre de vrais mots de passe dans Git.
3. Carte : **ESP32 Dev Module**. Port : celui de votre adaptateur CP210x (precedemment COM3).
4. Core : **esp32 by Espressif Systems 4.0.0** ; bibliotheque **NimBLE-Arduino 2.5.2**.
5. Outils > Partition Scheme : **Huge APP (3MB No OTA/1MB SPIFFS)**.
6. Compiler avec Verification, puis televerser. Ouvrir le Moniteur serie a **115200**.
7. Si Wi-Fi maison connecte : utiliser l'IP affichee ; sinon rejoindre `BM200-ESP32` et ouvrir `http://192.168.4.1`.

Il n'y a pas de firmware precompile dans ce ZIP. La compilation reelle Arduino et les essais sur le BM200 restent a faire.

## Ce qui fonctionne / est livre

- Wi-Fi maison et point d'acces de secours, serveur local et traces en memoire vive.
- Lecture instantanee, synchronisations BLE de **10, 252, 709** emplacements, decodage et export CSV/JSON, conserves comme **tests de diagnostic** deja validates dans la V1.2.1.
- Session Bluetooth Live avec boutons Connecter / Deconnecter ; lecture repetee toutes les 2 s ; histogramme de tension cote navigateur.
- **Nouveau V1.4.0 a verifier sur materiel** : lecture du dernier rapport de demarrage avec `D1 55 03`, statut, tension minimale, duree, anciennete et courbe de 352 points. Le bouton `Test de demarrage` est actif.
- Apres une synchronisation historique reussie, la lecture du rapport de demarrage D15503 est lancee dans la MEME connexion BLE avant de deconnecter (a valider sur BM200 reel). Une reponse `D1 55 03 FF` n'efface pas le dernier rapport valide deja conserve en RAM.
- **Nouveau** : interface inspiree de la capture ANCEL, jauge SoC, tension, temperature, indicateurs Wi-Fi/BLE/RAM.
- **Nouveau** : diagnostic memoire (heap libre, minimum, plus gros bloc) aux jalons BLE/Wi-Fi et environ toutes les 15 s en Live. CSV disponible sous `/download/ram.csv` (rotation a 64 Kio). Traces serie + journal RAM recent accessible sous `/api/logs`.
- **Nouveau** : si l'historique principal comporte plus de lignes que le transfert actuel, le transfert plus court est conserve dans `/download/short.csv` et `/download/short.json` sans remplacer l'historique principal.
- **Aucune affectation au bouton BOOT / IO0.**

## Ce qui n'est PAS encore implemente

- Synchronisation automatique integrale type ANCEL, calcul dynamique du nombre de mesures, datation, fusion et gestion des doublons. Les captures courtes restent des tests. Aucun horodatage de mesure historique n'est invente.
- Sequence des tests de charge, trajets et parametres du type batterie : ces commandes restent documentees dans l'audit V11 mais ne sont pas activees ici.
- Le **pas temporel exact** entre les 352 points de la courbe de demarrage n'est pas considere comme valide. L'interface affiche donc l'ordre des echantillons et non un axe en millisecondes invente.
- Reconnexion automatique BLE au redemarrage ; la session Live est manuelle, non persistante.

### Pourquoi l'historique est provisoire

L'audit APK `BM200_AUDIT_PROTOCOLE_XAPK_HCI_V11.md` (6 novembre) confirme : `D15505`, quantite calculee par `BleDataOperation.E()` a partir de la derniere date locale et une marge de dix intervalles ; 31 ou 72 jours possibles (jusqu'a 22 320 / 51 840 emplacements). Avec l'actuelle partition LittleFS de 1 Mio, conserver tous ces emplacements simultanement sous forme CSV **et** JSON depasserait rapidement sa capacite. La prochaine etape est un stockage binaire compact et un export a la demande. Le remplacement immediat de la synchronisation validee par une demande massive serait risque et n'est pas fait ici.

L'audit V11 retrouve le marquage des voyages `2` (debut), `4` (fin), suivi d'un mot de deux compteurs 16 bits. L'algorithme de datation ANCEL n'est pas entierement reproduit : 36 differences identifiees lors des relectures courtes. Les anciennes captures doivent rester disponibles jusqu'a validation.

## URLs principales

- `/` secours minimal local (interface complète dans `web/` sur GitHub Pages)
- `/api/status` statut JSON et memoire
- `/api/live` POST `enabled=1` / `enabled=0`
- `/api/ble/recover` POST pour forcer une reinitialisation/reconnexion Bluetooth puis relancer le Live
- `/api/sync` POST `count=10|252|709` + identifiant facultatif
- `/api/start` POST pour demander le rapport ; GET pour lire le rapport et la courbe
- `/api/logs`, `/api/preview`
- `/download/history.csv`, `/download/history.json`, `/download/previous.csv`, `/download/short.csv`, `/download/short.json`, `/download/trace.csv`
- `/download/start.json`, `/download/start-trace.csv`
- `/download/ram.csv`, `/download/ram-previous.csv`

## Tests hors ligne

Dans un environnement disposant de g++, OpenSSL, Node et Python : `python tests/check_offline.py`.

**Ne pas effacer les fichiers d'historique d'une version precedente avant de les avoir exportes.** Le partitionnement Huge APP ne conserve pas les fichiers en cas de changement de partition ou de formatage de LittleFS. Sauvegarder les exports avant de changer de schema ou de mettre a jour le firmware.


## V1.3.1 - modularisation et commande locale

- `BM200_ESP32.ino` est ramené sous 400 lignes.
- Le firmware est séparé en onglets Arduino: diagnostics, BLE coeur, synchronisation, live, Wi-Fi et API Web.
- Un appui long de 5 secondes sur IO0 bascule entre le mode Wi-Fi automatique et le point d'accès de secours forcé.
- GPIO2 est utilisé comme LED d'état candidate sur NodeMCU ESP-32S V1.1: fixe en Wi-Fi maison, clignotement lent en secours, rapide pendant la connexion.
- La jauge mobile est redimensionnée pour ne plus chevaucher tension et température.
- Le protocole BLE historique reste inchangé par rapport à V1.3.0.


## V1.4.0 - rapport de demarrage

- Ajout de la commande read-only `D1 55 03`.
- Decodage de l'entete confirme par l'audit APK/HCI : code resultat, tension minimale, duree et anciennete par pas de 120 s.
- Collecte de la courbe entre les marqueurs `FF FF FE` et `FF FE FE`.
- Decodage de 352 valeurs de 12 bits, stockees en centivolts.
- Conservation du dernier rapport valide si le BM200 repond ensuite `D1 55 03 FF`.
- Export JSON et trace BLE dediee.
- Tests hors ligne ajoutes pour la commande, l'exemple reel d'entete `D155030104C6094201AF...`, les marqueurs et le codec de courbe.

La structure est fondee sur l'audit APK/HCI V11 et les captures reelles. Le chemin complet ESP32 -> BM200 -> interface reste a valider sur le materiel avant de qualifier cette fonction de validee en exploitation.

## Compatibilite core ESP32 4.0.0 (V1.4.3)

- Migration de `mbedtls/aes.h` vers l'API officielle `<psa/crypto.h>`. Aucun changement du protocole BM200.
- Import d'une seule cle AES-128 volatile au demarrage, autorisant les operations de chiffrement et dechiffrement.
- Chaque paquet est traite en blocs de 16 octets AES-ECB sans bourrage, equivalent au CBC avec IV nul reinitialise pour chaque bloc comme dans l'application BM200.
- Si l'initialisation PSA echoue, le BLE ne demarre pas ; le journal serie indique le statut d'erreur.
- Conserver `NimBLE-Arduino 2.5.2` ; aucune autre bibliotheque externe AES n'est necessaire.
- Verifier le build avec Arduino IDE 2.3.11 + `esp32 by Espressif Systems 4.0.0` avant flash : la compilation complete n'a pas ete effectuee ici.
- Pour conserver les donnees LittleFS lors du flash, ne pas activer l'effacement complet de la flash ; garder le meme schema de partitions.

## ESP32 Core 4.0.0 - reservation memoire BLE et recuperation manuelle

- Le firmware inclut `esp32-hal-alloc-ble-mem.h` avant l'initialisation NimBLE afin d'empecher Arduino Core 4.0.0 de liberer la memoire BLE avant `setup()`.
- Le bouton **Forcer reconnexion** demande une nouvelle initialisation du moteur BLE si necessaire puis lance un nouveau scan/connexion Live vers le BM200.
- Le journal indique explicitement l'origine de chaque tentative d'initialisation NimBLE et chaque appareil vu pendant le scan.

## Migration V1.5.0
La page `WebPage.h` est retirée du firmware. Le répertoire `web/` à la racine du dépôt contient désormais l’interface. La route `/api/history.bin` transmet les mots historiques bruts déchiffrés, sans créer de grand JSON en RAM, et `/api/logs` envoie les lignes progressivement. Le backend conserve ses exports CSV/JSON antérieurs pendant la validation pour éviter toute perte de fonctionnalité.
Voir `../../README.md` pour la publication et les précautions Chrome Local Network Access.
