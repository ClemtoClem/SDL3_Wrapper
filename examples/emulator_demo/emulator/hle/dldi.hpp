#ifndef DLDI_H
#define DLDI_H

#include <cstdint>

#include "sdl3/iostream.hpp"

namespace emulator_demo {

class Core;

/// Sentinel "return addresses" patched into a ROM's DLDI driver stub in
/// place of real startup/isInserted/readSectors/writeSectors/clearStatus/
/// shutdown function pointers — CPU::handleReserved() recognizes a branch to
/// one of these (the reserved 0xF condition code makes the address read back
/// as its own value on open bus, so PC == opcode here) and HLEs it instead of
/// executing real code.
enum DldiFunc {
    DLDI_START = 0xF0000000,
    DLDI_INSERT,
    DLDI_READ,
    DLDI_WRITE,
    DLDI_CLEAR,
    DLDI_STOP
};

/**
 * @brief HLE DLDI driver: patches a homebrew ROM's DLDI reserved area (if
 * present) to redirect its SD-card I/O calls here, backed by a flat image
 * file (Settings::getSdImagePath()). No-op for ROMs without a DLDI header
 * (patchRom() just doesn't find the magic signature and leaves the ROM as-is).
 */
class Dldi {
  public:
    Dldi(Core *core) : core(core) {
    }

    void patchRom(uint8_t *rom, uint32_t offset, uint32_t size);
    bool isPatched() {
        return patched;
    }

    int startup();
    int isInserted();
    int readSectors(bool cpu, uint32_t sector, uint32_t numSectors, uint32_t buf);
    int writeSectors(bool cpu, uint32_t sector, uint32_t numSectors, uint32_t buf);
    int clearStatus();
    int shutdown();

  private:
    Core *core;
    bool  patched = false;
    sdl3::IOStream sdImage;
};

} // namespace emulator_demo

#endif // DLDI_H
