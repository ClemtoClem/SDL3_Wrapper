#include "emulator/io/ipc.hpp"

#include "emulator/core.hpp"
#include "emulator/io/rtc.hpp"

namespace emulator_demo {


void Ipc::writeIpcSync(bool cpu, uint16_t mask, uint16_t value) {
    mask &= 0x4F00;
    ipcSync[cpu] = (ipcSync[cpu] & ~mask) | (value & mask);

    // Override for HLE ARM7 (see hle_arm7.hpp) if enabled: the real ARM7 is
    // permanently halted in this mode, so it will never see the normal
    // cross-copy/interrupt below — redirect straight to the HLE handler.
    if (core->arm7Hle && !cpu) {
        core->hleArm7.ipcSync(uint8_t((ipcSync[cpu] >> 8) & 0xF));
        return;
    }

    ipcSync[!cpu] = (ipcSync[!cpu] & ~((mask >> 8) & 0x000F)) | (((value & mask) >> 8) & 0x000F);
    if ((value & BIT(13)) && (ipcSync[!cpu] & BIT(14)))
        core->interpreter[!cpu].sendInterrupt(16);
}

void Ipc::writeIpcFifoCnt(bool cpu, uint16_t mask, uint16_t value) {
    if ((value & BIT(3)) && !fifos[cpu].empty()) {
        while (!fifos[cpu].empty())
            fifos[cpu].pop();
        ipcFifoRecv[!cpu] = 0;
        ipcFifoCnt[cpu] |= BIT(0);
        ipcFifoCnt[cpu] &= ~BIT(1);
        ipcFifoCnt[!cpu] |= BIT(8);
        ipcFifoCnt[!cpu] &= ~BIT(9);
        if (ipcFifoCnt[cpu] & BIT(2))
            core->interpreter[cpu].sendInterrupt(17);
    }
    if ((ipcFifoCnt[cpu] & BIT(0)) && !(ipcFifoCnt[cpu] & BIT(2)) && (value & BIT(2)))
        core->interpreter[cpu].sendInterrupt(17);
    if (!(ipcFifoCnt[cpu] & BIT(8)) && !(ipcFifoCnt[cpu] & BIT(10)) && (value & BIT(10)))
        core->interpreter[cpu].sendInterrupt(18);
    if (value & BIT(14))
        ipcFifoCnt[cpu] &= ~BIT(14);
    mask &= 0x8404;
    ipcFifoCnt[cpu] = (ipcFifoCnt[cpu] & ~mask) | (value & mask);
}

void Ipc::writeIpcFifoSend(bool cpu, uint32_t mask, uint32_t value) {
    if (ipcFifoCnt[cpu] & BIT(15)) {
        if (fifos[cpu].size() < 16) {
            // Override for HLE ARM7 if enabled — see writeIpcSync() above.
            if (core->arm7Hle && !cpu) {
                core->hleArm7.ipcFifo(value & mask);
                return;
            }
            fifos[cpu].push(value & mask);
            if (fifos[cpu].size() == 1) {
                ipcFifoCnt[cpu] &= ~BIT(0);
                ipcFifoCnt[!cpu] &= ~BIT(8);
                if (ipcFifoCnt[!cpu] & BIT(10))
                    core->interpreter[!cpu].sendInterrupt(18);
            } else if (fifos[cpu].size() == 16) {
                ipcFifoCnt[cpu] |= BIT(1);
                ipcFifoCnt[!cpu] |= BIT(9);
            }
        } else {
            ipcFifoCnt[cpu] |= BIT(14);
        }
    }
}

uint32_t Ipc::readIpcFifoRecv(bool cpu) {
    if (!fifos[!cpu].empty()) {
        ipcFifoRecv[cpu] = fifos[!cpu].front();
        if (ipcFifoCnt[cpu] & BIT(15)) {
            fifos[!cpu].pop();
            if (fifos[!cpu].empty()) {
                ipcFifoCnt[cpu] |= BIT(8);
                ipcFifoCnt[!cpu] |= BIT(0);
                if (ipcFifoCnt[!cpu] & BIT(2))
                    core->interpreter[!cpu].sendInterrupt(17);
            } else if (fifos[!cpu].size() == 15) {
                ipcFifoCnt[cpu] &= ~BIT(9);
                ipcFifoCnt[!cpu] &= ~BIT(1);
            }
        }
    } else {
        ipcFifoCnt[cpu] |= BIT(14);
    }
    return ipcFifoRecv[cpu];
}

} // namespace emulator_demo

