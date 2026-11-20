#include "../firmware/BM200_ESP32/BM200Protocol.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static bool same(const uint8_t* a, const uint8_t* b, size_t len) {
    return std::memcmp(a, b, len) == 0;
}

int main() {
    uint8_t command[16] = {};
    BM200::commandLive(command);
    const uint8_t liveReq[16] = {0xD1, 0x55, 0x07};
    assert(same(command, liveReq, 16));


    BM200::commandStart(command);
    const uint8_t startReq[16] = {0xD1, 0x55, 0x03};
    assert(same(command, startReq, 16));

    const uint8_t startHeader[16] = {0xD1,0x55,0x03,0x01,0x04,0xC6,0x09,0x42,0x01,0xAF};
    BM200::StartReport sr{};
    assert(BM200::decodeStartReport(startHeader,16,sr));
    assert(sr.available && sr.resultCode == 1);
    assert(sr.minimumVoltageCenti == 1222);
    assert(sr.durationMs == 2370);
    assert(sr.ageSteps120s == 431);
    const uint8_t noStart[16] = {0xD1,0x55,0x03,0xFF};
    assert(BM200::decodeStartReport(noStart,16,sr) && !sr.available);

    uint8_t packed[BM200::START_CURVE_BYTES] = {};
    uint16_t expectedCurve[BM200::START_CURVE_POINTS] = {};
    for (size_t i = 0, j = 0; i < BM200::START_CURVE_POINTS; i += 2, j += 3) {
        const uint16_t a = static_cast<uint16_t>(1100 + (i % 200));
        const uint16_t b = static_cast<uint16_t>(1100 + ((i + 1) % 200));
        expectedCurve[i] = a; expectedCurve[i + 1] = b;
        packed[j] = static_cast<uint8_t>(a >> 4);
        packed[j + 1] = static_cast<uint8_t>(((a & 0x0F) << 4) | (b >> 8));
        packed[j + 2] = static_cast<uint8_t>(b);
    }
    uint16_t curve[BM200::START_CURVE_POINTS] = {};
    size_t curveCount = 0;
    assert(BM200::decodeStartCurve(packed, sizeof(packed), curve, BM200::START_CURVE_POINTS, curveCount));
    assert(curveCount == BM200::START_CURVE_POINTS);
    assert(std::memcmp(curve, expectedCurve, sizeof(curve)) == 0);
    const uint8_t markers[] = {0x00,0xFF,0xFF,0xFE,0x12,0x34,0xFF,0xFE,0xFE};
    assert(BM200::findMarker(markers,sizeof(markers),0xFF,0xFF,0xFE) == 1);
    assert(BM200::findMarker(markers,sizeof(markers),0xFF,0xFE,0xFE,4) == 6);

        BM200::commandHistory(10, command);
    const uint8_t hist10[16] = {0xD1,0x55,0x05,0,0,0,0,0,0x0A};
    assert(same(command, hist10, 16));
    BM200::commandHistory(709, command);
    const uint8_t hist709[16] = {0xD1,0x55,0x05,0,0,0,0,0x02,0xC5};
    assert(same(command, hist709, 16));
    assert(BM200::isValidSlotCount(10));
    assert(BM200::isValidSlotCount(252));
    assert(BM200::isValidSlotCount(709));
    assert(!BM200::isValidSlotCount(710));

    const uint8_t ack[16] = {0xD1,0x55,0x05,0x02,0xC5};
    assert(BM200::isHistoryAck(ack,16,709));
    assert(!BM200::isHistoryAck(ack,16,10));

    const uint8_t end[16] = {0xFF,0xFE,0xFE,0,0x0B,0x1D};
    uint32_t bytes = 0;
    assert(BM200::isHistoryEnd(end,16,bytes));
    assert(bytes == 709u*4u);

    const uint8_t live[16] = {0xD1,0x55,0x07,0x00,0x07,0x00,0x60,0x04,0xF1};
    BM200::Live l{};
    assert(BM200::decodeLive(live,16,l));
    assert(l.voltageCenti == 1265 && l.soc == 96 && l.temperatureC == 7);
    uint8_t neg[16] = {0xD1,0x55,0x07,0x01,0x08,0,0x60,0x04,0xF1};
    assert(BM200::decodeLive(neg,16,l) && l.temperatureC == -8);
    uint8_t pending[16] = {0xD1,0x55,0x07,0xFF};
    assert(!BM200::decodeLive(pending,16,l));

    const uint8_t sample1[4] = {0x4F,0x16,0x20,0xE0};
    auto s = BM200::decodeHistory(sample1);
    assert(s.voltageCenti == 1265 && s.soc == 98 && s.temperatureRaw == 14);
    assert(s.flag == 0 && !s.auxiliary);
    const uint8_t sample2[4] = {0x51,0x16,0x41,0x54};
    s = BM200::decodeHistory(sample2);
    assert(s.voltageCenti == 1297 && s.soc == 100 && s.temperatureRaw == 21 && s.flag == 4);
    const uint8_t special[4] = {0,0,0,1};
    s = BM200::decodeHistory(special);
    assert(s.auxiliary && s.flag == 1);

    std::puts("BM200 protocol codec: all tests passed");
    return 0;
}
