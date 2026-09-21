#ifndef HLE_ARM7_H
#define HLE_ARM7_H

#include <cstdint>
#include "../state_archive.hpp"

namespace emulator_demo {

class Core;

/**
 * @brief High-level emulation of the ARM7 side: instead of interpreting a
 * real ARM7 BIOS/firmware image, the ARM7 CPU is permanently halted and this
 * class directly stubs out the IPC (inter-processor communication) protocol
 * games use to ask the ARM7 "firmware" for touch-screen/extra-key input —
 * letting an NDS session boot with no firmware/ARM7 BIOS dump at all
 * (Settings::getArm7Hle(), opt-in). GBA sessions are unaffected.
 */
class HleArm7 {
  public:
    HleArm7(Core *core) : core(core) {
    }

    /// Halts the ARM7 permanently and sets the initial IPC handshake state —
    /// called once from Core's constructor when Settings::getArm7Hle() is set.
    void init();

    void ipcSync(uint8_t value);
    void ipcFifo(uint32_t value);

    /// Per-frame housekeeping (auto key/touch polling) — called once per NDS
    /// frame from Core::endFrame().
    void runFrame();

    void ioState(StateArchive &archive) {
        archive.io(inited);
        archive.io(autoTouch);
    }

  private:
    Core *core;
    bool  inited    = false;
    bool  autoTouch = false;

    void pollTouch(uint32_t value);
};

} // namespace emulator_demo

#endif // HLE_ARM7_H
