#ifndef AES_H
#define AES_H

#include <cstdint>
#include <deque>
#include <functional>

namespace emulator_demo {

class Core;
class StateArchive;

class Aes {
public:
    Aes(Core *core);
    void ioState(StateArchive &archive);
    void update();

    uint32_t readCnt() { return aesCnt; }
    uint32_t readRdfifo();

    void writeCnt(uint32_t mask, uint32_t value);
    void writeBlkcnt(uint32_t mask, uint32_t value);
    void writeWrfifo(uint32_t mask, uint32_t value);
    void writeIv(int i, uint32_t mask, uint32_t value);
    void writeMac(int i, uint32_t mask, uint32_t value);
    void writeKey(int i, int j, uint32_t mask, uint32_t value);
    void writeKeyx(int i, int j, uint32_t mask, uint32_t value);
    void writeKeyy(int i, int j, uint32_t mask, uint32_t value);

private:
    Core *core;
    bool scheduled = false;
    std::function<void()> updateTask;

    uint8_t fsBox[0x100];
    uint8_t rsBox[0x100];
    uint32_t fTable[0x100];
    uint32_t rTable[0x100];

    uint32_t keys[4][4] = {};
    uint32_t keysX[4][4] = {};
    uint32_t keysY[4][4] = {};
    uint32_t rKey[44] = {};
    uint32_t ctr[4] = {};
    uint32_t cbc[4] = {};
    uint16_t curBlock = 0;
    uint16_t curExtra = 0;
    uint8_t curKey = 0;

    std::deque<uint32_t> writeFifo;
    std::deque<uint32_t> readFifo;

    uint32_t aesCnt = 0;
    uint32_t aesBlkcnt = 0;
    uint32_t aesRdfifo = 0;
    uint32_t aesIv[4] = {};
    uint32_t aesMac[4] = {};

    static uint32_t scatter8(uint8_t *t, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
    static uint32_t scatter32(uint32_t *t, uint32_t a, uint32_t b, uint32_t c, uint32_t d);

    static void rolKey(uint32_t *key, uint8_t rol);
    static void xorKey(uint32_t *dst, uint32_t *src);
    static void addKey(uint32_t *dst, uint32_t *src);

    void cryptBlock(uint32_t *src, uint32_t *dst);
    void initFifo();
    void triggerFifo();
};

} // namespace emulator_demo

#endif // AES_H