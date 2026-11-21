"""Offline protocol and frontend sanity checks (no hardware, no network)."""
import html
import pathlib
import re
import subprocess
import sys

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
for item in ['api.js','storage.js','app.js','service-worker.js']:
    run(['node','--check',str(WEB / item)])
assert (WEB / 'index.html').exists() and 'Forcer reconnexion' in (WEB / 'index.html').read_text()
assert 'WEB_PAGE' not in (ROOT / 'BM200_ESP32.ino').read_text()
print('PWA JavaScript and externalized UI: OK')
firmware = "\n".join(path.read_text() for path in [ROOT / "BM200_ESP32.ino", *sorted(ROOT.glob("*Module.h"))])
for route in ['/api/sync','/api/logs','/api/status','/api/start','/api/ble/recover','/api/preview','/api/history.bin','/download/history.csv','/download/history.json','/download/trace.csv','/download/start.json','/download/start-trace.csv']:
    assert '"'+route+'"' in firmware, 'missing route '+route
print("HTTP route inventory: OK")

# Network mode API and secrets checks (static, does not disclose secret values).
firmware = "\n".join(path.read_text() for path in [ROOT / "BM200_ESP32.ino", *sorted(ROOT.glob("*Module.h"))])
for token in ['WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD)', 'WiFi.softAP(',
              'maintainWiFi();', 'MDNS.begin(WIFI_HOSTNAME)',
              r'out += ",\"wifi\":{']:
    assert token in firmware, f"missing Wi-Fi feature: {token}"
assert 'Secrets.h' in (ROOT / '.gitignore').read_text()
print("Wi-Fi fallback and secret management: OK")

# STA is established before the HTTP listener; AP fallback is AP-only.
wifi_source = (ROOT / 'WiFiModule.h').read_text()
ino_source = (ROOT / 'BM200_ESP32.ino').read_text()
config_source = (ROOT / 'Config.h').read_text()
assert 'WIFI_STA_FIRST_DIAGNOSTIC' not in config_source
assert 'WiFi.mode(WIFI_STA);' in wifi_source
assert 'WiFi.mode(WIFI_AP);' in wifi_source
assert 'WiFi.mode(WIFI_AP_STA)' not in wifi_source
assert '    WiFi.reconnect();' not in wifi_source
assert 'ensureNetworkHttp()' in wifi_source
assert 'registerRoutes();' in ino_source
assert ino_source.index('registerRoutes();') < ino_source.index('ensureNetworkHttp();')
assert 'LittleFS.begin(ALLOW_LITTLEFS_AUTOFORMAT)' in ino_source
assert 'ALLOW_LITTLEFS_AUTOFORMAT = false' in config_source
assert 'WIFI_AP_HOME_RETRY_INTERVAL_MS' in wifi_source
assert 'cfg.sta.disable_wpa3_compatible_mode' in wifi_source
assert 'esp_wifi_get_config' in wifi_source
assert 'esp_wifi_set_config' in wifi_source
assert 'esp_wifi_connect()' in wifi_source
assert 'WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD, 0, nullptr, false)' in wifi_source
assert 'WiFi.softAPgetStationNum()' in wifi_source

# Never disclose actual credentials in public firmware/configuration/documentation.
secrets_file = ROOT / 'Secrets.h'
secrets = (secrets_file if secrets_file.exists() else ROOT / 'Secrets.example.h').read_text()
secret_values = re.findall(r'WIFI_(?:STA|AP)_PASSWORD\s*=\s*"([^"]+)"', secrets)
assert len(secret_values) == 2
for secret in secret_values:
    assert len(secret) >= 8
    if not secrets_file.exists():
        continue  # The public example values are necessarily visible.
    for public_path in ['README.md','Config.h','Secrets.example.h'] + [x.name for x in [ROOT / 'BM200_ESP32.ino', *ROOT.glob('*Module.h')]]:
        assert secret not in (ROOT / public_path).read_text(), f"secret found in {public_path}"
print("Sequential Wi-Fi, LittleFS protection and credentials: OK")

# Compile the actual network module against independent Arduino API mocks.
for core_major in [3, 4]:
    mock_bin = TESTS / f"_wifi_state_machine_bin_{core_major}"
    run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
         f"-DESP_ARDUINO_VERSION_MAJOR={core_major}",
         str(TESTS / "wifi_state_machine_test.cpp"), "-o", str(mock_bin)])
    print(f"Core {core_major}: " + run([str(mock_bin)]).stdout.decode().strip())
    mock_bin.unlink(missing_ok=True)

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
