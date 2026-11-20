#pragma once

// Configuration - gateway reachable on the home Wi-Fi or its own fallback AP.
// Network secrets live in Secrets.h (ignored by Git). No password is logged.
#include "Secrets.h"
static const char* WIFI_STA_SSID = "WifiGG";
static const char* WIFI_HOSTNAME = "bm200-esp32";
static const char* WIFI_AP_NAME = "BM200-ESP32";

// Wait before enabling the fallback AP; keep attempting to join the home WLAN.
static const uint32_t WIFI_FALLBACK_AFTER_MS = 12000;
static const uint32_t WIFI_RETRY_INTERVAL_MS = 45000;
static const uint32_t WIFI_AP_RETRY_INTERVAL_MS = 30000;
// Once the home network returns, close the fallback AP only if nobody uses it.
static const uint32_t WIFI_AP_CLOSE_DELAY_MS = 15000;

// Target BLE MAC from your BM200 captures. Match MAC only, not advertised name.
static const char* BM200_BLE_MAC = "C8:17:F5:29:4E:F8";

// Legacy diagnostic sizes only. Not the number of unread records on the BM200.
static const uint16_t DEFAULT_HISTORY_SLOTS = 10;

// BLE operation timeout. The BM200 already maintains its own history.
static const uint32_t BLE_SCAN_MS = 7000;
static const uint32_t BLE_LIVE_TIMEOUT_MS = 5000;
static const uint32_t BLE_HISTORY_TIMEOUT_MS = 30000;
static const uint32_t BLE_START_TIMEOUT_MS = 10000;

// USB serial logs. Full packets are also saved in the downloadable BLE trace.
static const uint32_t SERIAL_BAUD = 115200;

// Physical controls - NodeMCU ESP-32S V1.1.
// IO0 remains the boot button during reset; after boot a 5 s hold toggles forced AP mode.
static const uint8_t WIFI_MODE_BUTTON_PIN = 0;
static const uint32_t WIFI_MODE_BUTTON_HOLD_MS = 5000;

// Candidate on-board user LED on this NodeMCU family. Safe to drive only after boot.
// If the physical board has no GPIO2 LED, leave enabled: the pin state is still harmless.
static const bool STATUS_LED_ENABLED = true;
static const uint8_t STATUS_LED_PIN = 2;
static const bool STATUS_LED_ACTIVE_HIGH = true;
static const uint32_t STATUS_LED_SLOW_MS = 800;
static const uint32_t STATUS_LED_FAST_MS = 180;
