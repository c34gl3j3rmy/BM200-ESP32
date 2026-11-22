"""Offline protocol and frontend sanity checks (no hardware, no network)."""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1] / "firmware" / "BM200_ESP32"
WEB = pathlib.Path(__file__).resolve().parents[1] / "web"
TESTS = pathlib.Path(__file__).resolve().parent

def run(cmd, **kwargs):
    return subprocess.run(cmd, check=True, capture_output=True, **kwargs)

# Compile and execute the Arduino-independent C++ protocol codec.
exe = TESTS / "_protocol_test_bin"
run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", str(TESTS / "protocol_test.cpp"), "-o", str(exe)])
print(run([str(exe)]).stdout.decode().strip())
exe.unlink(missing_ok=True)

# Check that the public BM6 AES vector matches; ECB == CBC zero IV for one block.
key = bytes([108,101,97,103,101,110,100,255,254,48,49,48,48,48,48,57])
plain = bytes.fromhex("d1550700000000000000000000000000")
expected = bytes.fromhex("697ea0b5d54cf024e794772355554114")
r = run(["openssl", "enc", "-aes-128-ecb", "-K", key.hex(), "-nosalt", "-nopad"], input=plain)
assert r.stdout == expected, (r.stdout.hex(), expected.hex())
print("Public AES test vector: OK")

# Check the externalized frontend and the preserved ESP32 HTTP routes.
for item in ['api.js','storage.js','app.js','settings.js','service-worker.js']:
    run(['node','--check',str(WEB / item)])
assert (WEB / 'index.html').exists() and 'Forcer reconnexion' in (WEB / 'index.html').read_text()
assert 'WEB_PAGE' not in (ROOT / 'BM200_ESP32.ino').read_text()
print('PWA JavaScript and externalized UI: OK')
firmware = "\n".join(path.read_text() for path in [ROOT / "BM200_ESP32.ino", *sorted(ROOT.glob("*Module.h"))])
for route in ['/api/sync','/api/logs','/api/status','/api/start','/api/ble/recover','/api/preview','/api/history.bin','/api/settings/wifi','/download/history.csv','/download/history.json','/download/trace.csv','/download/start.json','/download/start-trace.csv']:
    assert '"'+route+'"' in firmware, 'missing route '+route
print("HTTP route inventory: OK")

# Network mode API and secrets checks (static, does not disclose secret values).
firmware = "\n".join(path.read_text() for path in [ROOT / "BM200_ESP32.ino", *sorted(ROOT.glob("*Module.h"))])
for token in ['WiFi.begin(homeSSID.c_str(), homePassword.c_str())', 'WiFi.softAP(',
              'maintainWiFi();', 'MDNS.begin(WIFI_HOSTNAME)',
              r'out += ",\"wifi\":{']:
    assert token in firmware, f"missing Wi-Fi feature: {token}"
assert 'Secrets.h' in (ROOT / '.gitignore').read_text()
assert 'wifiPrefs.putString("home_ssid"' in firmware
assert 'wifiPrefs.putString("ap_pass"' in firmware
assert 'WIFI_AP_RESET_HOLD_MS' in firmware
print("Wi-Fi fallback, NVS configuration and BOOT reset: OK")

# Espressif requires hostname configuration before WiFi.mode/start.
config = (ROOT / 'Config.h').read_text()
assert firmware.index('WiFi.setHostname(WIFI_HOSTNAME)') < firmware.index('WiFi.mode(WIFI_STA);')
# Keep legacy private files ignored if they exist locally from older versions,
# but never make the new firmware depend on them.
assert 'Secrets.h' in (ROOT / '.gitignore').read_text()
assert '#include "Secrets.h"' not in config
print("Hostname order and Wi-Fi credential isolation: OK")

# Verify that the firmware can boot without any private header.
wifi = (ROOT / 'WiFiModule.h').read_text()
assert '#include "Secrets.h"' not in config
assert 'WIFI_STA_PASSWORD' not in config + wifi
assert 'getString("home_ssid", "")' in wifi
assert 'if (!homeSSID.length())' in wifi
assert 'startFallbackAP();' in wifi
assert not (ROOT / 'Secrets.example.h').exists()
print('First-boot Wi-Fi without Secrets.h: OK')

# Exercise exact physical BOOT timing boundaries in portable C++.
button_bin = TESTS / "_button_test_bin"
run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
     str(TESTS / "button_test.cpp"), "-o", str(button_bin)])
print(run([str(button_bin)]).stdout.decode().strip())
button_bin.unlink(missing_ok=True)

# Prevent reintroducing a dependency on internal NimBLE host include paths.
assert "#include <host/ble_hs.h>" not in firmware
assert "BLE_SCAN_COMPLETED_TIMEOUT = 13" in firmware
print("NimBLE scan header compatibility: OK")

# Connection reuse and historical binary transport checks.
sync_src=(ROOT/'BLESyncModule.h').read_text()
start_src=(ROOT/'BLEStartModule.h').read_text()
worker_src=(ROOT/'BLELiveModule.h').read_text()
assert 'readStartInExistingSession(client, writer, trace)' in sync_src
assert 'bool readStartInExistingSession(' in start_src
assert 'performStartReport(startError)' not in worker_src
assert 'web.setContentLength(count * 4)' in (ROOT/'WebApiModule.h').read_text()
print('BLE single-session sequence and binary API static checks: OK')
