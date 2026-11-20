"""Source-level checks for the ESP32 core 4 PSA migration."""
from pathlib import Path
root = Path(__file__).resolve().parents[1] / 'firmware' / 'BM200_ESP32'
ino = (root / 'BM200_ESP32.ino').read_text()
crypto = (root / 'BM200Crypto.h').read_text()
core = (root / 'BLECoreModule.h').read_text()
assert '#include <mbedtls/aes.h>' not in ino
assert '#include "BM200Crypto.h"' in ino
assert ino.index('#include "BM200Crypto.h"') < ino.index('#include "BLECoreModule.h"')
setup = ino[ino.index('void setup()'): ]
assert setup.index('initBm200Crypto()') < setup.index('initializeBleEngine("demarrage")')
assert 'PSA_ALG_ECB_NO_PADDING' in crypto
assert 'PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT' in crypto
assert 'psa_cipher_encrypt(' in crypto and 'psa_cipher_decrypt(' in crypto
assert 'outputLength == 16' in crypto
assert 'psa_import_key(' in crypto and 'psa_crypto_init()' in crypto
assert 'psa_reset_key_attributes(' in crypto
assert 'for (size_t p = 0; p < frame.length; p += 16)' in core
assert 'aesBlock(frame.encrypted + p, out + p, false)' in core
assert 'aesBlock(plain, encrypted, true)' in core
print('ESP32 core 4 PSA integration checks: OK')

assert '#include "esp32-hal-alloc-ble-mem.h"' in ino
assert ino.index('#include "esp32-hal-alloc-ble-mem.h"') < ino.index('#include <NimBLEDevice.h>')
assert 'initializeBleEngine(' in ino
print('ESP32 core 4 BLE memory reservation checks: OK')
