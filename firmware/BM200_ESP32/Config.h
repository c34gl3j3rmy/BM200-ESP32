#pragma once

// Configuration - home STA first, AP-only fallback after timeout.
// Network secrets live in Secrets.h (ignored by Git). No password is logged.
#include "Secrets.h"
static const char* WIFI_STA_SSID = "WifiGG";
static const char* WIFI_HOSTNAME = "bm200-esp32";
static const char* WIFI_AP_NAME = "BM200-ESP32";

// V1.5.3: Wi-Fi domestique prioritaire (mode STA), AP de secours distinct.
// Les modes STA et AP ne sont jamais actifs simultanement : cela elimine
// les tentatives de reconnexion qui perturbaient les premiers diagnostics.
static const uint32_t WIFI_STA_BOOT_TIMEOUT_MS = 20000; // Maximum au demarrage.
static const uint32_t WIFI_FALLBACK_AFTER_MS = 12000; // Perte d'une liaison etablie.
static const uint32_t WIFI_AP_RETRY_INTERVAL_MS = 30000; // Si softAP echoue.
static const uint32_t WIFI_AP_HOME_RETRY_INTERVAL_MS = 15UL * 60UL * 1000UL; // AP idle 15 min -> retry home.
static const uint32_t WIFI_AP_IDLE_BEFORE_RETRY_MS = 2UL * 60UL * 1000UL; // Never interrupt active clients.

// Core 4.x: successful diagnostic on Freebox WPA2/3 Compatible Mode.
// True disables the ESP32 RSN Override WPA3 negotiation; ESP32 joins via
// WPA2-AES. The Freebox WPA2/WPA3 and GCMP-256 settings remain unchanged.
// Applied again after each AP -> STA transition. Not needed on Core 3.x.
static const bool WIFI_DISABLE_WPA3_RSN_OVERRIDE = true;

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
// BOOT/IO0: 5 s to force AP from STA, or to retry STA from AP.
// Do not hold BOOT while resetting or flashing the board.
static const uint8_t WIFI_MODE_BUTTON_PIN = 0;
static const uint32_t WIFI_MODE_BUTTON_HOLD_MS = 5000;

// Candidate on-board user LED on this NodeMCU family. Safe to drive only after boot.
// If the physical board has no GPIO2 LED, leave enabled: the pin state is still harmless.
static const bool STATUS_LED_ENABLED = true;
static const uint8_t STATUS_LED_PIN = 2;
static const bool STATUS_LED_ACTIVE_HIGH = true;
static const uint32_t STATUS_LED_SLOW_MS = 800;
static const uint32_t STATUS_LED_FAST_MS = 180;

// Prevent automatic formatting if an old LittleFS partition fails to mount.
static const bool ALLOW_LITTLEFS_AUTOFORMAT = false;
