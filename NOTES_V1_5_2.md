# V1.5.2 - Notes de test et diagnostic

## Constat confirme avant cette version

- V1.5.1 sous Core 3.3.13 : HTTP domestique repond sur `192.168.1.180`.
- Ancien firmware : de multiples `WiFi.reconnect()` echouent avec `sta is connecting`.
- Ancien AP seul : `ERR_CONNECTION_RESET` meme sur `/api/ping`.
- Un croquis AP minimal a egalement echoue, tandis que HelloServer en mode
  station sur le Wi-Fi domestique fonctionne. Ce test ne prouve aucune cause
  precise du reset dans l'AP.

## Modification V1.5.2

1. Demarrer en STA, attendre une adresse IP.
2. Sans connexion, desactiver explicitement STA et activer uniquement AP.
3. Demarrer HTTP apres que le reseau choisi soit pret.
4. Ne jamais lancer `WiFi.reconnect()` en mode AP.
5. Sur perte du Wi-Fi maison, attendre 12 secondes, puis passer en AP.
6. En AP sans client, reessayer le reseau maison apres 15 minutes ; pas de
   bascule si client connecte, ou en mode secours force.
7. Le bouton IO0 pendant 5 secondes bascule entre secours et tentative STA.
8. Pas de formatage automatique LittleFS au changement de Core.

## Procedure sans ambiguite

1. Exporter les 709 mesures avant tout flash.
2. Avec Core 3.3.13, flasher V1.5.2, tester ping/status sur WifiGG.
3. Forcer AP avec IO0 5 s, reconnecter le telephone, tester
   `http://192.168.4.1/api/ping`.
4. Si AP ne marche pas, relever le log et revenir a la V1.5.1 fonctionnelle.
5. Si les deux modes marchent, installer Core 4.0.0, compiler le meme projet
   et refaire les tests. Ne pas changer le partitionnement.

Compilations et essais sur la carte : a faire. Les tests statiques et les
simulations de transitions de Wi-Fi ne valident pas la radio elle-meme.
