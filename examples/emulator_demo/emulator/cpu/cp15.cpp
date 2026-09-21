#include "emulator/cpu/cp15.hpp"
#include "emulator/core.hpp"

namespace emulator_demo {


uint32_t Cp15::read(int cn, int cm, int cp) {
    switch ((cn << 16) | (cm << 8) | (cp << 0)) {
        case 0x000000:
            return 0x41059461;
        case 0x000001:
            return 0x0F0D2112;
        case 0x010000:
            return ctrlReg;
        case 0x090100:
            return dtcmReg;
        case 0x090101:
            return itcmReg;
        default: {
            LOG_VERBOSE("Unknown CP15 register read: C%d,C%d,%d\n", cn, cm, cp);
            return 0;
        }
    }
}

void Cp15::write(int cn, int cm, int cp, uint32_t value) {
    switch ((cn << 16) | (cm << 8) | (cp << 0)) {
        case 0x010000: {
            ctrlReg          = (ctrlReg & ~0x000FF085) | (value & 0x000FF085);
            exceptionAddr    = (ctrlReg & BIT(13)) ? 0xFFFF0000 : 0x00000000;
            dtcmReadEnabled  = (ctrlReg & BIT(16)) && !(ctrlReg & BIT(17));
            dtcmWriteEnabled = (ctrlReg & BIT(16));
            itcmReadEnabled  = (ctrlReg & BIT(18)) && !(ctrlReg & BIT(19));
            itcmWriteEnabled = (ctrlReg & BIT(18));
            core->memory.updateMap9(dtcmAddr, dtcmAddr + dtcmSize);
            core->memory.updateMap9(0x00000000, itcmSize);
            return;
        }
        case 0x090100: {
            uint32_t dtcmAddrOld = dtcmAddr;
            uint32_t dtcmSizeOld = dtcmSize;
            dtcmReg              = value;
            dtcmAddr             = dtcmReg & 0xFFFFF000;
            dtcmSize             = 0x200 << ((dtcmReg & 0x0000003E) >> 1);
            if (dtcmSize < 0x1000)
                dtcmSize = 0x1000;
            core->memory.updateMap9(dtcmAddrOld, dtcmAddrOld + dtcmSizeOld);
            core->memory.updateMap9(dtcmAddr, dtcmAddr + dtcmSize);
            return;
        }
        case 0x070004:
        case 0x070802: {
            core->interpreter[0].halt(0);
            return;
        }
        case 0x090101: {
            uint32_t itcmSizeOld = itcmSize;
            itcmReg              = value;
            itcmSize             = 0x200 << ((itcmReg & 0x0000003E) >> 1);
            if (itcmSize < 0x1000)
                itcmSize = 0x1000;
            core->memory.updateMap9(0x00000000, std::max(itcmSizeOld, itcmSize));
            return;
        }
        default: {
            LOG_VERBOSE("Unknown CP15 register write: C%d,C%d,%d\n", cn, cm, cp);
            return;
        }
    }
}

} // namespace emulator_demo

