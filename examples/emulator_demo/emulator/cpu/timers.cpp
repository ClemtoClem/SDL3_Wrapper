#include "emulator/cpu/timers.hpp"
#include "emulator/core.hpp"

namespace emulator_demo {


Timers::Timers(Core *core, bool cpu) : core(core), cpu(cpu) {
    for (int i = 0; i < 4; i++)
        overflowTask[i] = std::bind(&Timers::overflow, this, i);
}

void Timers::resetCycles() {
    for (int i = 0; i < 4; i++)
        endCycles[i] -= core->getGlobalCycles();
}

void Timers::overflow(int timer) {
    if (!(tmCntH[timer] & BIT(7)) ||
        ((timer == 0 || !(tmCntH[timer] & BIT(2))) && endCycles[timer] != core->getGlobalCycles()))
        return;
    timers[timer] = tmCntL[timer];
    if (timer == 0 || !(tmCntH[timer] & BIT(2))) {
        core->schedule(Task(&overflowTask[timer], (0x10000 - timers[timer]) << shifts[timer]));
        endCycles[timer] = core->getGlobalCycles() + ((0x10000 - timers[timer]) << shifts[timer]);
    }
    if (tmCntH[timer] & BIT(6))
        core->interpreter[cpu].sendInterrupt(3 + timer);
    if (core->isGbaMode() && timer < 2)
        core->spu.gbaFifoTimer(timer);
    if (timer < 3 && (tmCntH[timer + 1] & BIT(2)) && ++timers[timer + 1] == 0)
        overflow(timer + 1);
}

void Timers::writeTmCntL(int timer, uint16_t mask, uint16_t value) {
    tmCntL[timer] = (tmCntL[timer] & ~mask) | (value & mask);
}

void Timers::writeTmCntH(int timer, uint16_t mask, uint16_t value) {
    bool dirty = false;
    if ((tmCntH[timer] & BIT(7)) && (timer == 0 || !(value & BIT(2))))
        timers[timer] = 0x10000 - ((endCycles[timer] - core->getGlobalCycles()) >> shifts[timer]);
    if (mask & 0x00FF) {
        int shift = (((value & 0x0003) && (timer == 0 || !(value & BIT(2)))) ? (4 + (value & 0x0003) * 2) : 0);
        if (shifts[timer] != shift) {
            shifts[timer] = shift;
            dirty         = true;
        }
    }
    if (!(tmCntH[timer] & BIT(7)) && (value & BIT(7))) {
        timers[timer] = tmCntL[timer];
        dirty         = true;
    }
    mask &= 0x00C7;
    tmCntH[timer] = (tmCntH[timer] & ~mask) | (value & mask);
    if (dirty && (tmCntH[timer] & BIT(7)) && (timer == 0 || !(tmCntH[timer] & BIT(2)))) {
        core->schedule(Task(&overflowTask[timer], (0x10000 - timers[timer]) << shifts[timer]));
        endCycles[timer] = core->getGlobalCycles() + ((0x10000 - timers[timer]) << shifts[timer]);
    }
}

uint16_t Timers::readTmCntL(int timer) {
    if ((tmCntH[timer] & BIT(7)) && (timer == 0 || !(tmCntH[timer] & BIT(2))))
        timers[timer] = 0x10000 - ((endCycles[timer] - core->getGlobalCycles()) >> shifts[timer]);
    return timers[timer];
}

void Timers::rearm() {
    for (int i = 0; i < 4; i++) {
        if (!(tmCntH[i] & BIT(7)) || (i != 0 && (tmCntH[i] & BIT(2))))
            continue;
        uint32_t cycles = (endCycles[i] > core->getGlobalCycles()) ? (endCycles[i] - core->getGlobalCycles()) : 1;
        core->schedule(Task(&overflowTask[i], cycles));
    }
}

} // namespace emulator_demo

