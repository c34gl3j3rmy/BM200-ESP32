#pragma once
// BM200 AES-128 ECB per block (equivalent to zero-IV CBC reset each block).
#include <esp_arduino_version.h>
#if ESP_ARDUINO_VERSION_MAJOR >= 4
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

#else
#include <mbedtls/aes.h>

static mbedtls_aes_context bm200AesEncrypt;
static mbedtls_aes_context bm200AesDecrypt;
static bool bm200AesReady = false;

bool initBm200Crypto() {
    if (bm200AesReady) return true;
    mbedtls_aes_init(&bm200AesEncrypt);
    mbedtls_aes_init(&bm200AesDecrypt);
    const int enc = mbedtls_aes_setkey_enc(&bm200AesEncrypt, BM200::AES_KEY, 128);
    const int dec = mbedtls_aes_setkey_dec(&bm200AesDecrypt, BM200::AES_KEY, 128);
    if (enc != 0 || dec != 0) {
        Serial.printf("BM200 MbedTLS AES init error: %d / %d\n", enc, dec);
        mbedtls_aes_free(&bm200AesEncrypt);
        mbedtls_aes_free(&bm200AesDecrypt);
        return false;
    }
    bm200AesReady = true;
    return true;
}

bool aesBlock(const uint8_t input[16], uint8_t output[16], bool encrypt) {
    if (!bm200AesReady) return false;
    return mbedtls_aes_crypt_ecb(encrypt ? &bm200AesEncrypt : &bm200AesDecrypt,
                                 encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
                                 input, output) == 0;
}
#endif
