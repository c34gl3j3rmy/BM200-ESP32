#pragma once

// BM200 / BM6 protocol facts backed by the captured BLE sessions.
// No Arduino dependency: testable with a normal C++ compiler.
#include <stddef.h>
#include <stdint.h>

namespace BM200 {

static const uint8_t AES_KEY[16] = {
    108, 101, 97, 103, 101, 110, 100, 255,
    254, 48, 49, 48, 48, 48, 48, 57
};

static const uint8_t CMD_LIVE = 0x07;
static const uint8_t CMD_HISTORY = 0x05;
static const uint8_t CMD_START = 0x03;
static const size_t START_CURVE_POINTS = 352;
static const size_t START_CURVE_BYTES = (START_CURVE_POINTS * 12) / 8;

struct Live {
    uint16_t voltageCenti;
    uint8_t soc;
    int16_t temperatureC;
};

struct StartReport {
    bool available;
    uint8_t resultCode;
    uint16_t minimumVoltageCenti;
    uint16_t durationMs;
    uint16_t ageSteps120s;
};

struct Historical {
    uint16_t voltageCenti;
    uint8_t soc;
    uint8_t temperatureRaw;
    uint8_t flag;
    bool auxiliary;
};

inline void commandStart(uint8_t out[16]) {
    for (size_t i = 0; i < 16; ++i) out[i] = 0;
    out[0] = 0xD1;
    out[1] = 0x55;
    out[2] = CMD_START;
}

inline void commandLive(uint8_t out[16]) {
    for (size_t i = 0; i < 16; ++i) out[i] = 0;
    out[0] = 0xD1;
    out[1] = 0x55;
    out[2] = CMD_LIVE;
}

// Count occupies bytes 7-8 (big endian), verified for 10, 252, 709.
inline void commandHistory(uint16_t slots, uint8_t out[16]) {
    for (size_t i = 0; i < 16; ++i) out[i] = 0;
    out[0] = 0xD1;
    out[1] = 0x55;
    out[2] = CMD_HISTORY;
    out[7] = static_cast<uint8_t>(slots >> 8);
    out[8] = static_cast<uint8_t>(slots);
}


inline bool decodeStartReport(const uint8_t* p, size_t len, StartReport& out) {
    if (len < 4 || p[0] != 0xD1 || p[1] != 0x55 || p[2] != CMD_START)
        return false;
    if (p[3] == 0xFF) {
        out = {false, 0xFF, 0, 0, 0};
        return true;
    }
    if (len < 10) return false;
    out.available = true;
    out.resultCode = p[3];
    out.minimumVoltageCenti = (static_cast<uint16_t>(p[4]) << 8) | p[5];
    out.durationMs = (static_cast<uint16_t>(p[6]) << 8) | p[7];
    out.ageSteps120s = (static_cast<uint16_t>(p[8]) << 8) | p[9];
    return out.minimumVoltageCenti > 0 && out.minimumVoltageCenti <= 4000;
}

// The APK reconstructs the startup graph as 12-bit values. 352 samples
// therefore occupy exactly 528 bytes (two samples per three bytes).
inline bool decodeStartCurve(const uint8_t* p, size_t len, uint16_t* out,
                             size_t capacity, size_t& count) {
    count = 0;
    if (!p || !out || len != START_CURVE_BYTES || capacity < START_CURVE_POINTS)
        return false;
    for (size_t i = 0; i < len; i += 3) {
        const uint16_t a = (static_cast<uint16_t>(p[i]) << 4) | (p[i + 1] >> 4);
        const uint16_t b = (static_cast<uint16_t>(p[i + 1] & 0x0F) << 8) | p[i + 2];
        if (a > 4000 || b > 4000) return false;
        out[count++] = a;
        out[count++] = b;
    }
    return count == START_CURVE_POINTS;
}

inline int findMarker(const uint8_t* p, size_t len, uint8_t a, uint8_t b, uint8_t c, size_t from = 0) {
    if (!p || len < 3 || from >= len) return -1;
    for (size_t i = from; i + 2 < len; ++i)
        if (p[i] == a && p[i + 1] == b && p[i + 2] == c) return static_cast<int>(i);
    return -1;
}

inline bool decodeLive(const uint8_t* p, size_t len, Live& out) {
    if (len < 16 || p[0] != 0xD1 || p[1] != 0x55 || p[2] != CMD_LIVE || p[3] == 0xFF)
        return false;
    const uint16_t cv = (static_cast<uint16_t>(p[7]) << 8) | p[8];
    if (cv == 0 || cv > 4000 || p[6] > 100) return false;
    out.voltageCenti = cv;
    out.soc = p[6];
    out.temperatureC = (p[3] == 1) ? -static_cast<int16_t>(p[4]) : static_cast<int16_t>(p[4]);
    return true;
}

inline bool isHistoryAck(const uint8_t* p, size_t len, uint16_t requested) {
    if (len < 16 || p[0] != 0xD1 || p[1] != 0x55 || p[2] != CMD_HISTORY)
        return false;
    const uint16_t n = (static_cast<uint16_t>(p[3]) << 8) | p[4];
    return n == requested;
}

inline bool isHistoryEnd(const uint8_t* p, size_t len, uint32_t& bytesUseful) {
    if (len < 6 || p[0] != 0xFF || p[1] != 0xFE || p[2] != 0xFE)
        return false;
    const uint32_t total = (static_cast<uint32_t>(p[3]) << 16) |
                           (static_cast<uint32_t>(p[4]) << 8) | p[5];
    if (total < 9) return false;
    bytesUseful = total - 9;
    return true;
}

// Four-byte historical word = VVV SS TT F (nibble-packed).
// Keep auxiliary markers such as 00000000 or 00000001 as raw records.
inline Historical decodeHistory(const uint8_t p[4]) {
    Historical h;
    h.voltageCenti = (static_cast<uint16_t>(p[0]) << 4) | (p[1] >> 4);
    h.soc = static_cast<uint8_t>(((p[1] & 0x0F) << 4) | (p[2] >> 4));
    h.temperatureRaw = static_cast<uint8_t>(((p[2] & 0x0F) << 4) | (p[3] >> 4));
    h.flag = p[3] & 0x0F;
    h.auxiliary = (p[0] == 0 && p[1] == 0 && p[2] == 0 && (p[3] & 0xF0) == 0);
    return h;
}

inline bool isValidSlotCount(uint16_t n) {
    return n == 10 || n == 252 || n == 709;
}

}  // namespace BM200
