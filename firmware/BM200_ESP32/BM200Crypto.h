#pragma once
// ESP32 Arduino core 4.x (ESP-IDF 6 / Mbed TLS 4): use public PSA Crypto API.
// The BM200 cipher is AES-128-CBC with a zero IV restarted for EVERY 16-byte
// block, which is equivalent to AES-128-ECB on each individual block.
#include <psa/crypto.h>

static psa_key_id_t bm200AesKey = PSA_KEY_ID_NULL;

bool initBm200Crypto() {
    if (bm200AesKey != PSA_KEY_ID_NULL) return true;
    psa_status_t rc = psa_crypto_init();
    if (rc != PSA_SUCCESS) {
        Serial.printf("BM200 PSA init error: %ld\n", static_cast<long>(rc));
        return false;
    }
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 128);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECB_NO_PADDING);
    rc = psa_import_key(&attributes, BM200::AES_KEY, sizeof(BM200::AES_KEY), &bm200AesKey);
    psa_reset_key_attributes(&attributes);
    if (rc != PSA_SUCCESS) {
        bm200AesKey = PSA_KEY_ID_NULL;
        Serial.printf("BM200 PSA import error: %ld\n", static_cast<long>(rc));
        return false;
    }
    return true;
}

bool aesBlock(const uint8_t input[16], uint8_t output[16], bool encrypt) {
    if (bm200AesKey == PSA_KEY_ID_NULL) return false;
    size_t outputLength = 0;
    const psa_status_t rc = encrypt
        ? psa_cipher_encrypt(bm200AesKey, PSA_ALG_ECB_NO_PADDING,
                             input, 16, output, 16, &outputLength)
        : psa_cipher_decrypt(bm200AesKey, PSA_ALG_ECB_NO_PADDING,
                             input, 16, output, 16, &outputLength);
    return rc == PSA_SUCCESS && outputLength == 16;
}
