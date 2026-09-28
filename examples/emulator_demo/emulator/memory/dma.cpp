#include "dma.hpp"

#include "../core.hpp"

namespace emulator_demo {


Dma::Dma(Core *core, bool cpu) : core(core), cpu(cpu) {
    for (int i = 0; i < 4; i++)
        transferTask[i] = std::bind(&Dma::transfer, this, i);
}

void Dma::transfer(int channel) {
    int dstAddrCnt  = (dmaCnt[channel] & 0x00600000) >> 21;
    int srcAddrCnt  = (dmaCnt[channel] & 0x01800000) >> 23;
    int mode        = (dmaCnt[channel] & 0x38000000) >> 27;
    int gxFifoCount = 0;
    if (core->isGbaMode() && mode == 6 && (channel == 1 || channel == 2)) {
        for (unsigned int i = 0; i < 4; i++) {
            core->memory.write<uint32_t>(cpu, dstAddrs[channel], core->memory.read<uint32_t>(cpu, srcAddrs[channel]));
            if (srcAddrCnt == 0)
                srcAddrs[channel] += 4;
            else if (srcAddrCnt == 1)
                srcAddrs[channel] -= 4;
        }
    } else if (dmaCnt[channel] & BIT(26)) {
        for (unsigned int i = 0; i < wordCounts[channel]; i++) {
            core->memory.write<uint32_t>(cpu, dstAddrs[channel], core->memory.read<uint32_t>(cpu, srcAddrs[channel]));
            if (srcAddrCnt == 0)
                srcAddrs[channel] += 4;
            else if (srcAddrCnt == 1)
                srcAddrs[channel] -= 4;
            if (dstAddrCnt == 0 || dstAddrCnt == 3)
                dstAddrs[channel] += 4;
            else if (dstAddrCnt == 1)
                dstAddrs[channel] -= 4;
            if (mode == 7 && ++gxFifoCount == 112)
                break;
        }
    } else {
        for (unsigned int i = 0; i < wordCounts[channel]; i++) {
            core->memory.write<uint16_t>(cpu, dstAddrs[channel], core->memory.read<uint16_t>(cpu, srcAddrs[channel]));
            if (srcAddrCnt == 0)
                srcAddrs[channel] += 2;
            else if (srcAddrCnt == 1)
                srcAddrs[channel] -= 2;
            if (dstAddrCnt == 0 || dstAddrCnt == 3)
                dstAddrs[channel] += 2;
            else if (dstAddrCnt == 1)
                dstAddrs[channel] -= 2;
            if (mode == 7 && ++gxFifoCount == 112)
                break;
        }
    }
    if (mode == 7) {
        wordCounts[channel] -= gxFifoCount;
        if (wordCounts[channel] > 0) {
            if (core->gpu3D.readGxStat() & BIT(25))
                core->schedule(Task(&transferTask[channel], 1));
            return;
        }
    }
    if ((dmaCnt[channel] & BIT(25)) && mode != 0) {
        wordCounts[channel] = dmaCnt[channel] & 0x001FFFFF;
        if (dstAddrCnt == 3)
            dstAddrs[channel] = dmaDad[channel];
        if (mode == 7 && core->gpu3D.readGxStat() & BIT(25))
            core->schedule(Task(&transferTask[channel], 1));
    } else {
        dmaCnt[channel] &= ~BIT(31);
    }
    if (dmaCnt[channel] & BIT(30))
        core->interpreter[cpu].sendInterrupt(8 + channel);
}

void Dma::trigger(int mode, uint8_t channels) {
    if (cpu == 1)
        mode <<= 1;
    for (int i = 0; i < 4; i++) {
        if ((channels & BIT(i)) && (dmaCnt[i] & BIT(31)) && ((dmaCnt[i] & 0x38000000) >> 27) == uint32_t(mode))
            core->schedule(Task(&transferTask[i], 1));
    }
}

void Dma::writeDmaSad(int channel, uint32_t mask, uint32_t value) {
    mask &= ((cpu == 0 || channel != 0) ? 0x0FFFFFFF : 0x07FFFFFF);
    dmaSad[channel] = (dmaSad[channel] & ~mask) | (value & mask);
}

void Dma::writeDmaDad(int channel, uint32_t mask, uint32_t value) {
    mask &= ((cpu == 0 || channel == 3) ? 0x0FFFFFFF : 0x07FFFFFF);
    dmaDad[channel] = (dmaDad[channel] & ~mask) | (value & mask);
}

void Dma::writeDmaCnt(int channel, uint32_t mask, uint32_t value) {
    uint32_t old = dmaCnt[channel];
    mask &= ((cpu == 0) ? 0xFFFFFFFF : (channel == 3 ? 0xF7E0FFFF : 0xF7E03FFF));
    dmaCnt[channel] = (dmaCnt[channel] & ~mask) | (value & mask);
    if ((dmaCnt[channel] & BIT(31)) && ((dmaCnt[channel] & 0x38000000) >> 27) == 7 &&
        (core->gpu3D.readGxStat() & BIT(25)))
        core->schedule(Task(&transferTask[channel], 1));
    if ((old & BIT(31)) || !(dmaCnt[channel] & BIT(31)))
        return;
    dstAddrs[channel]   = dmaDad[channel];
    srcAddrs[channel]   = dmaSad[channel];
    wordCounts[channel] = dmaCnt[channel] & 0x001FFFFF;
    if (((dmaCnt[channel] & 0x38000000) >> 27) == 0)
        core->schedule(Task(&transferTask[channel], 1));
}

uint32_t Dma::readDmaCnt(int channel) {
    return dmaCnt[channel] & ~(core->isGbaMode() ? 0x0000FFFF : 0x00000000);
}

void Dma::rearm() {
    for (int i = 0; i < 4; i++) {
        if (!(dmaCnt[i] & BIT(31)))
            continue;
        int mode = (dmaCnt[i] & 0x38000000) >> 27;
        if (mode == 7 && wordCounts[i] > 0 && (core->gpu3D.readGxStat() & BIT(25)))
            core->schedule(Task(&transferTask[i], 1));
    }
}

} // namespace emulator_demo

