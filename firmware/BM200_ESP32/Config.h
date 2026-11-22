#pragma once

// Wi-Fi credentials live in ESP32 Preferences (NVS), not in firmware or PWA storage.
// Bootstrap AP password is a PUBLIC factory value: replace it through configuration.
static const char* WIFI_HOSTNAME = "bm200-esp32";
static const char* WIFI_AP_NAME = "BM200-ESP32";
static const uint32_t WIFI_STA_BOOT_TIMEOUT_MS = 20000;
static const uint32_t WIFI_FALLBACK_AFTER_MS = 12000;
static const uint32_t WIFI_AP_RETRY_INTERVAL_MS = 30000;
static const uint32_t WIFI_AP_HOME_RETRY_INTERVAL_MS = 15UL * 60UL * 1000UL;
static const uint32_t WIFI_AP_IDLE_BEFORE_RETRY_MS = 2UL * 60UL * 1000UL;
static const bool WIFI_DISABLE_WPA3_RSN_OVERRIDE = true;
static const uint16_t DEFAULT_HISTORY_SLOTS = 10;
static const uint32_t BLE_SCAN_MS = 7000;
static const uint32_t BLE_LIVE_TIMEOUT_MS = 5000;
static const uint32_t BLE_HISTORY_TIMEOUT_MS = 30000;
static const uint32_t BLE_START_TIMEOUT_MS = 10000;
static const uint32_t SERIAL_BAUD = 115200;
static const uint8_t WIFI_MODE_BUTTON_PIN = 0;
static const uint32_t WIFI_MODE_BUTTON_HOLD_MS = 5000;
static const uint32_t CONFIG_BUTTON_HOLD_MS = 2000;
static const bool STATUS_LED_ENABLED = true;
static const uint8_t STATUS_LED_PIN = 2;
static const bool STATUS_LED_ACTIVE_HIGH = true;
static const uint32_t STATUS_LED_SLOW_MS = 800;
static const uint32_t STATUS_LED_FAST_MS = 180;
static const bool ALLOW_LITTLEFS_AUTOFORMAT = false;
