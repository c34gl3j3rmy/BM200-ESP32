#pragma once

// BM200 - Portail captif local (ESP32 Arduino core 4.x).
// Module inclus AVANT WiFiModule.h et WebApiModule.h dans le sketch.
// N'intervient pas dans les operations BLE ou LittleFS.

static DNSServer bm200CaptiveDns;
static bool bm200CaptiveDnsRunning = false;
static bool bm200CaptiveDhcpDone = false;
static uint32_t bm200CaptiveDnsLastAttempt = 0;

// Une seule page locale autonome : ni CDN ni dependance Internet.
static const char BM200_CAPTIVE_PAGE[] PROGMEM = R"BM200PORTAL(
<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light dark">
<title>BM200 - Acces local</title>
<style>
:root{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;color-scheme:light dark}
body{margin:0;background:#111827;color:#f9fafb;line-height:1.5}
main{max-width:490px;margin:0 auto;padding:32px 20px}
h1{font-size:1.65rem;margin:0 0 8px}p{color:#d1d5db}
small{color:#a7b3c4}.mark{font-size:.85rem;font-weight:700;color:#5eead4;letter-spacing:.09em}
.card{border:1px solid #334155;background:#1e293b;border-radius:16px;padding:20px;margin:22px 0}
a.action{display:block;text-align:center;text-decoration:none;padding:12px 14px;margin:10px 0;border-radius:9px;font-weight:600;background:#0d9488;color:#fff}
a.secondary{background:#334155}
code{background:#334155;padding:2px 5px;border-radius:3px;color:#fff}
</style>
</head>
<body><main>
<p class="mark">ANCEL BM200 / ESP32</p>
<h1>Connexion locale</h1>
<p>Vous etes connecte au Wi-Fi <strong>BM200-ESP32</strong>.
Ce reseau permet d'acceder au moniteur de batterie mais ne fournit pas Internet.</p>
<div class="card">
<strong>Acces au BM200</strong>
<a class="action" href="/config">Configurer l'ESP32 (Wi-Fi)</a>
<a class="action secondary" href="/api/status">Voir l'etat de l'ESP32</a>
<a class="action secondary" href="/api/logs">Consulter le journal</a>
<a class="action secondary" href="https://c34gl3j3rmy.github.io/BM200-ESP32/" target="_blank" rel="noopener">Ouvrir la PWA BM200</a>
<small>La PWA necessite un navigateur normal et un premier chargement depuis Internet,
ou une installation deja mise en cache.</small>
</div>
<p>Si HyperOS propose une confirmation, choisissez de rester connecte a ce Wi-Fi.
Si l'application n'arrive pas a joindre l'ESP32, ouvrez Chrome et testez
<code>http://192.168.4.1/api/status</code>.</p>
<small>Cette page est servie par l'ESP32, sans compte ni Internet.</small>
</main></body>
</html>
)BM200PORTAL";

void sendCaptivePortalPage() {
    if (!apActive) {
        web.send(404, "text/plain", "Portail disponible uniquement sur BM200-ESP32");
        return;
    }
    web.sendHeader("Cache-Control", "no-store");
    web.sendHeader("X-Content-Type-Options", "nosniff");
    web.send_P(200, "text/html; charset=utf-8", BM200_CAPTIVE_PAGE);
}

void sendCaptiveRedirect() {
    // A 204 would tell Android that the network has Internet; use HTTP 302.
    web.sendHeader("Cache-Control", "no-store");
    web.sendHeader("Location", "http://192.168.4.1/portal", true);
    web.send(302, "text/plain", "");
}

void sendCaptiveProbe() {
    if (apActive) sendCaptiveRedirect();
    else web.send(404, "text/plain", "Portail inactif sur le reseau domestique");
}

void maintainCaptivePortal() {
    // Le DNS wildcard n'est actif que lorsque le point d'acces est allume.
    if (!apActive) {
        if (bm200CaptiveDnsRunning) {
            bm200CaptiveDns.stop();
            bm200CaptiveDnsRunning = false;
            addLog("INFO", "Portail captif: DNS arrete");
        }
        bm200CaptiveDhcpDone = false;
        bm200CaptiveDnsLastAttempt = 0;
        return;
    }

    const IPAddress ip = WiFi.softAPIP();
    if (ip == IPAddress(0, 0, 0, 0)) return;

#if defined(ESP_IDF_VERSION) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 2)
    if (!bm200CaptiveDhcpDone) {
        bm200CaptiveDhcpDone = true;
        if (WiFi.AP.enableDhcpCaptivePortal()) {
            addLog("INFO", "Portail captif: URL annoncee par DHCP (http://%s)", ip.toString().c_str());
        } else {
            addLog("WARN", "Portail captif: annonce DHCP indisponible; DNS/HTTP restent actifs");
        }
    }
#endif

    if (bm200CaptiveDnsRunning) return;
    const uint32_t now = millis();
    if (bm200CaptiveDnsLastAttempt != 0 && now - bm200CaptiveDnsLastAttempt < 5000) return;
    bm200CaptiveDnsLastAttempt = now;

    // Core 4.x : DNSServer utilise AsyncUDP, sans processNextRequest() dans loop().
    if (bm200CaptiveDns.start(53, "*", ip)) {
        bm200CaptiveDnsRunning = true;
        addLog("INFO", "Portail captif: DNS * => %s, HTTP /portal", ip.toString().c_str());
    } else {
        addLog("WARN", "Portail captif: DNS port 53 indisponible; nouvel essai dans 5 s");
    }
}
