#include "emulator/io/wifi.hpp"

#include "emulator/core.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace emulator_demo {

#define MS_CYCLES 34418


Wifi::Wifi(Core *core) : core(core) {
    bbRegisters[0x00] = 0x6D;
    bbRegisters[0x5D] = 0x01;
    bbRegisters[0x64] = 0xFF;
    countMsTask       = std::bind(&Wifi::countMs, this);
}

void Wifi::scheduleInit() {
    core->schedule(Task(&countMsTask, MS_CYCLES));
    scheduled = true;
}

void Wifi::addConnection(Core *core) {
    mutex.lock();
    connections.push_back(&core->wifi);
    mutex.unlock();
    core->wifi.mutex.lock();
    core->wifi.connections.push_back(this);
    core->wifi.mutex.unlock();
}

void Wifi::remConnection(Core *core) {
    mutex.lock();
    auto position = std::find(connections.begin(), connections.end(), &core->wifi);
    connections.erase(position);
    mutex.unlock();
    core->wifi.mutex.lock();
    position = std::find(core->wifi.connections.begin(), core->wifi.connections.end(), this);
    core->wifi.connections.erase(position);
    core->wifi.mutex.unlock();
}

void Wifi::sendInterrupt(int bit) {
    if (!(wIe & wIrf) && (wIe & BIT(bit)))
        core->interpreter[1].sendInterrupt(24);
    wIrf |= BIT(bit);
    if (bit == 14) {
        wPostBeacon = 0xFFFF;
        wTxreqRead &= 0xFFF2;
    }
}

void Wifi::countMs() {
    if (!packets.empty())
        processPackets();
    if (wUsCountcnt) {
        if (wBeaconCount == wPreBeacon)
            sendInterrupt(15);
        if (--wBeaconCount == 0) {
            if ((wTxbufLoc[4] & BIT(15)) && (wTxreqRead & BIT(4)))
                transfer(4);
            wBeaconCount = wBeaconInt;
            if (wUsComparecnt)
                sendInterrupt(14);
        }
        if (wPostBeacon && --wPostBeacon == 0)
            sendInterrupt(13);
    }
    if (!connections.empty() || wUsCountcnt)
        core->schedule(Task(&countMsTask, MS_CYCLES));
    else
        scheduled = false;
}

void Wifi::processPackets() {
    mutex.lock();
    for (size_t i = 0; i < packets.size(); i++) {
        uint16_t size = (packets[i][4] + 12) / 2;
        for (size_t j = 0; j < size; j++) {
            core->memory.write<uint16_t>(1, 0x4804000 + wRxbufWrcsr, packets[i][j]);
            wRxbufWrcsr += 2;
            if ((wRxbufBegin & 0x1FFE) != (wRxbufEnd & 0x1FFE))
                wRxbufWrcsr = (wRxbufBegin & 0x1FFE) +
                              (wRxbufWrcsr - (wRxbufBegin & 0x1FFE)) % ((wRxbufEnd & 0x1FFE) - (wRxbufBegin & 0x1FFE));
            wRxbufWrcsr &= 0x1FFE;
        }
        delete[] packets[i];
        sendInterrupt(0);
    }
    packets.clear();
    mutex.unlock();
}

void Wifi::transfer(int index) {
    uint16_t address = (wTxbufLoc[index] & 0xFFF) << 1;
    uint16_t size    = core->memory.read<uint16_t>(1, 0x4804000 + address + 0x0A) + 8;
    LOG("Sending packet on channel %d with size 0x%X\n", index, size);

    // Read the packet once; reused both for local (same-process) connections
    // and for the optional network bridge below.
    std::vector<uint16_t> packet(size / 2);
    for (size_t j = 0; j < size; j += 2)
        packet[j / 2] = core->memory.read<uint16_t>(1, 0x4804000 + address + j);
    packet[4] = size - 12;

    mutex.lock();
    for (size_t i = 0; i < connections.size(); i++) {
        uint16_t *data = new uint16_t[size / 2];
        std::memcpy(data, packet.data(), size);
        connections[i]->mutex.lock();
        connections[i]->packets.push_back(data);
        connections[i]->mutex.unlock();
    }
    mutex.unlock();

    if (onPacketSent)
        onPacketSent(reinterpret_cast<const uint8_t *>(packet.data()), size);

    if (index != 4)
        wTxbufLoc[index] &= ~BIT(15);
    sendInterrupt(1);
}

void Wifi::injectPacket(const uint8_t *data, size_t len) {
    uint16_t *copy = new uint16_t[len / 2];
    std::memcpy(copy, data, len - (len % 2));
    mutex.lock();
    packets.push_back(copy);
    mutex.unlock();
}

void Wifi::writeWModeWep(uint16_t mask, uint16_t value) {
    wModeWep = (wModeWep & ~mask) | (value & mask);
}

void Wifi::writeWIrf(uint16_t mask, uint16_t value) {
    wIrf &= ~(value & mask);
}

void Wifi::writeWIe(uint16_t mask, uint16_t value) {
    if (!(wIe & wIrf) && (value & mask & wIrf))
        core->interpreter[1].sendInterrupt(24);
    mask &= 0xFBFF;
    wIe = (wIe & ~mask) | (value & mask);
}

void Wifi::writeWMacaddr(int index, uint16_t mask, uint16_t value) {
    wMacaddr[index] = (wMacaddr[index] & ~mask) | (value & mask);
}

void Wifi::writeWBssid(int index, uint16_t mask, uint16_t value) {
    wBssid[index] = (wBssid[index] & ~mask) | (value & mask);
}

void Wifi::writeWAidFull(uint16_t mask, uint16_t value) {
    mask &= 0x07FF;
    wAidFull = (wAidFull & ~mask) | (value & mask);
}

void Wifi::writeWRxcnt(uint16_t mask, uint16_t value) {
    mask &= 0xFF0E;
    wRxcnt = (wRxcnt & ~mask) | (value & mask);
    if (value & BIT(0))
        wRxbufWrcsr = wRxbufWrAddr << 1;
}

void Wifi::writeWPowerstate(uint16_t mask, uint16_t value) {
    mask &= 0x0003;
    wPowerstate = (wPowerstate & ~mask) | (value & mask);
    if (wPowerstate & BIT(1))
        wPowerstate &= ~BIT(9);
}

void Wifi::writeWPowerforce(uint16_t mask, uint16_t value) {
    mask &= 0x8001;
    wPowerforce = (wPowerforce & ~mask) | (value & mask);
    if (wPowerforce & BIT(15))
        wPowerstate = (wPowerstate & ~BIT(9)) | ((wPowerforce & BIT(0)) << 9);
}

void Wifi::writeWRxbufBegin(uint16_t mask, uint16_t value) {
    wRxbufBegin = (wRxbufBegin & ~mask) | (value & mask);
}

void Wifi::writeWRxbufEnd(uint16_t mask, uint16_t value) {
    wRxbufEnd = (wRxbufEnd & ~mask) | (value & mask);
}

void Wifi::writeWRxbufWrAddr(uint16_t mask, uint16_t value) {
    mask &= 0x0FFF;
    wRxbufWrAddr = (wRxbufWrAddr & ~mask) | (value & mask);
}

void Wifi::writeWRxbufRdAddr(uint16_t mask, uint16_t value) {
    mask &= 0x1FFE;
    wRxbufRdAddr = (wRxbufRdAddr & ~mask) | (value & mask);
}

void Wifi::writeWRxbufReadcsr(uint16_t mask, uint16_t value) {
    mask &= 0x0FFF;
    wRxbufReadcsr = (wRxbufReadcsr & ~mask) | (value & mask);
}

void Wifi::writeWRxbufGap(uint16_t mask, uint16_t value) {
    mask &= 0x1FFE;
    wRxbufGap = (wRxbufGap & ~mask) | (value & mask);
}

void Wifi::writeWRxbufGapdisp(uint16_t mask, uint16_t value) {
    mask &= 0x0FFF;
    wRxbufGapdisp = (wRxbufGapdisp & ~mask) | (value & mask);
}

void Wifi::writeWRxbufCount(uint16_t mask, uint16_t value) {
    mask &= 0x0FFF;
    wRxbufCount = (wRxbufCount & ~mask) | (value & mask);
}

void Wifi::writeWTxbufWrAddr(uint16_t mask, uint16_t value) {
    mask &= 0x1FFE;
    wTxbufWrAddr = (wTxbufWrAddr & ~mask) | (value & mask);
}

void Wifi::writeWTxbufCount(uint16_t mask, uint16_t value) {
    mask &= 0x0FFF;
    wTxbufCount = (wTxbufCount & ~mask) | (value & mask);
}

void Wifi::writeWTxbufWrData(uint16_t mask, uint16_t value) {
    core->memory.write<uint16_t>(1, 0x4804000 + wTxbufWrAddr, value & mask);
    wTxbufWrAddr += 2;
    if (wTxbufWrAddr == wTxbufGap)
        wTxbufWrAddr += wTxbufGapdisp << 1;
    wTxbufWrAddr &= 0x1FFF;
    if (wTxbufCount > 0 && --wTxbufCount == 0)
        sendInterrupt(8);
}

void Wifi::writeWTxbufGap(uint16_t mask, uint16_t value) {
    mask &= 0x1FFE;
    wTxbufGap = (wTxbufGap & ~mask) | (value & mask);
}

void Wifi::writeWTxbufGapdisp(uint16_t mask, uint16_t value) {
    mask &= 0x0FFF;
    wTxbufGapdisp = (wTxbufGapdisp & ~mask) | (value & mask);
}

void Wifi::writeWTxbufLoc(int index, uint16_t mask, uint16_t value) {
    wTxbufLoc[index] = (wTxbufLoc[index] & ~mask) | (value & mask);
    if (index != 4 && (wTxbufLoc[index] & BIT(15)) && (wTxreqRead & BIT(index)))
        transfer(index);
}

void Wifi::writeWBeaconInt(uint16_t mask, uint16_t value) {
    mask &= 0x03FF;
    wBeaconInt   = (wBeaconInt & ~mask) | (value & mask);
    wBeaconCount = wBeaconInt;
}

void Wifi::writeWTxreqReset(uint16_t mask, uint16_t value) {
    mask &= 0x000F;
    wTxreqRead &= ~(value & mask);
}

void Wifi::writeWTxreqSet(uint16_t mask, uint16_t value) {
    mask &= 0x000F;
    wTxreqRead |= (value & mask);
    for (int i = 0; i < 4; i++) {
        if ((wTxbufLoc[i] & BIT(15)) && (wTxreqRead & BIT(i)))
            transfer(i);
    }
}

void Wifi::writeWUsCountcnt(uint16_t mask, uint16_t value) {
    mask &= 0x0001;
    wUsCountcnt = (wUsCountcnt & ~mask) | (value & mask);
}

void Wifi::writeWUsComparecnt(uint16_t mask, uint16_t value) {
    mask &= 0x0001;
    wUsComparecnt = (wUsComparecnt & ~mask) | (value & mask);
    if (value & BIT(1))
        sendInterrupt(14);
}

void Wifi::writeWPreBeacon(uint16_t mask, uint16_t value) {
    wPreBeacon = (wPreBeacon & ~mask) | (value & mask);
}

void Wifi::writeWBeaconCount(uint16_t mask, uint16_t value) {
    wBeaconCount = (wBeaconCount & ~mask) | (value & mask);
}

void Wifi::writeWConfig(int index, uint16_t mask, uint16_t value) {
    const uint16_t masks[] = {0x81FF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0FFF, 0x8FFF, 0xFFFF, 0xFFFF,
                              0x00FF, 0x00FF, 0x00FF, 0x00FF, 0xFFFF, 0xFF3F, 0x7A7F};
    mask &= masks[index];
    wConfig[index] = (wConfig[index] & ~mask) | (value & mask);
}

void Wifi::writeWPostBeacon(uint16_t mask, uint16_t value) {
    wPostBeacon = (wPostBeacon & ~mask) | (value & mask);
}

void Wifi::writeWBbCnt(uint16_t mask, uint16_t value) {
    int index = value & 0x00FF;
    switch ((value & 0xF000) >> 12) {
        case 5: {
            if ((index >= 0x01 && index <= 0x0C) || (index >= 0x13 && index <= 0x15) ||
                (index >= 0x1B && index <= 0x26) || (index >= 0x28 && index <= 0x4C) ||
                (index >= 0x4E && index <= 0x5C) || (index >= 0x62 && index <= 0x63) || (index == 0x65) ||
                (index >= 0x67 && index <= 0x68))
                bbRegisters[index] = wBbWrite;
            break;
        }
        case 6: {
            wBbRead = bbRegisters[index];
            break;
        }
    }
}

void Wifi::writeWBbWrite(uint16_t mask, uint16_t value) {
    wBbWrite = (wBbWrite & ~mask) | (value & mask);
}

void Wifi::writeWIrfSet(uint16_t mask, uint16_t value) {
    if (!(wIe & wIrf) && (wIe & value & mask))
        core->interpreter[1].sendInterrupt(24);
    mask &= 0xFBFF;
    wIrf |= (value & mask);
}

uint16_t Wifi::readWRxbufRdData() {
    uint16_t value = core->memory.read<uint16_t>(1, 0x4804000 + wRxbufRdAddr);
    if ((wRxbufRdAddr += 2) == wRxbufGap)
        wRxbufRdAddr += wRxbufGapdisp << 1;
    if ((wRxbufBegin & 0x1FFE) != (wRxbufEnd & 0x1FFE))
        wRxbufRdAddr = (wRxbufBegin & 0x1FFE) +
                       (wRxbufRdAddr - (wRxbufBegin & 0x1FFE)) % ((wRxbufEnd & 0x1FFE) - (wRxbufBegin & 0x1FFE));
    wRxbufRdAddr &= 0x1FFF;
    if (wRxbufCount > 0 && --wRxbufCount == 0)
        sendInterrupt(9);
    return value;
}

} // namespace emulator_demo

