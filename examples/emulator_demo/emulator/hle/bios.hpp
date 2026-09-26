
#ifndef BIOS_H
#define BIOS_H

#include <cstdint>
#include <memory>
#include "../state_archive.hpp"

namespace emulator_demo {

class Core;
class Bios {
  public:
    Bios(Core *core, int (Bios::**swiTable)(bool, uint32_t **)) : core(core), swiTable(swiTable) {
    }
    int  execute(uint8_t vector, bool cpu, uint32_t **registers);
    void checkWaitFlags(bool cpu);
    bool shouldCheck() {
        return waitFlags;
    }
    int swiWaitByLoop(bool cpu, uint32_t **registers);
    int swiInterruptWait(bool cpu, uint32_t **registers);
    int swiVBlankIntrWait(bool cpu, uint32_t **registers);
    int swiHalt(bool cpu, uint32_t **registers);
    int swiDivide(bool cpu, uint32_t **registers);
    int swiCpuSet(bool cpu, uint32_t **registers);
    int swiCpuFastSet(bool cpu, uint32_t **registers);
    int swiSquareRoot(bool cpu, uint32_t **registers);
    int swiGetCrc16(bool cpu, uint32_t **registers);
    int swiIsDebugger(bool cpu, uint32_t **registers);
    int swiBitUnpack(bool cpu, uint32_t **registers);
    int swiLz77Uncomp(bool cpu, uint32_t **registers);
    int swiRunlenUncomp(bool cpu, uint32_t **registers);
    int swiUnknown(bool cpu, uint32_t **registers);

    /**
     * @brief Sérialise/désérialise l'état du BIOS (masque d'attente des SWI) pour les save states.
     * @param archive Archive de sauvegarde/chargement.
     */
    void ioState(StateArchive &archive) {
        archive.io(waitFlags);
    }

  protected:
    Core *core;

  private:
    int (Bios::**swiTable)(bool, uint32_t **);
    uint32_t waitFlags = 0;
};



class NdsBios9 : public Bios {
  public:
    NdsBios9(Core *core) : Bios(core, (int(Bios::**)(bool, uint32_t **))swiTable9) {
    }

  private:
    static int (NdsBios9::*swiTable9[0x21])(bool, uint32_t **);
    int swiDiffUnfilt8(bool cpu, uint32_t **registers);
    int swiDiffUnfilt16(bool cpu, uint32_t **registers);
};



class NdsBios7 : public Bios {
  public:
    NdsBios7(Core *core) : Bios(core, (int(Bios::**)(bool, uint32_t **))swiTable7) {
    }

  private:
    static int (NdsBios7::*swiTable7[0x21])(bool, uint32_t **);
    int swiSleep(bool cpu, uint32_t **registers);
    int swiSoundBias(bool cpu, uint32_t **registers);
    int swiGetSineTable(bool cpu, uint32_t **registers);
    int swiGetPitchTable(bool cpu, uint32_t **registers);
    int swiGetVolumeTable(bool cpu, uint32_t **registers);
};

} // namespace emulator_demo

#endif
