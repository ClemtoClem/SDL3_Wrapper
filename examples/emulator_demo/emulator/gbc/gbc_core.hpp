#ifndef GBC_CORE_H
#define GBC_CORE_H

#include <chrono>
#include <cstdint>
#include <vector>

#include "gbc_apu.hpp"
#include "gbc_cpu.hpp"
#include "gbc_defines.hpp"
#include "gbc_memory.hpp"
#include "gbc_ppu.hpp"
#include "core/core.hpp"
#include "../state_archive.hpp"

namespace emulator_demo {

/**
 * @brief Top-level Game Boy / Game Boy Color emulator session — the GBC
 * counterpart to the NDS/GBA `Core`, but deliberately its own, wholly
 * separate class rather than shoehorned into it: the two consoles share no
 * hardware lineage (8-bit LR35902 vs 32-bit ARM946E-S/ARM7TDMI, completely
 * different memory maps/PPU/APU), so unifying them would only add
 * indirection without any real code reuse. GameView holds either a `Core`
 * or a `GbcCore`, never both.
 *
 * Frame timing has no scheduler (unlike the ARM Core's cycle-based task
 * queue) — the GB's hardware timeline is simple enough that runFrame() is
 * just "step the CPU one instruction at a time, feed its cycle cost to the
 * PPU/APU/timer, until the PPU signals VBlank," matching real hardware and
 * the koenk/gbc reference this was cross-checked against.
 */
class GbcCore {
  public:
    /// Parses `rom` as a GBC/DMG cartridge and boots directly past 0x0100
    /// (no BIOS dump needed — this port always direct-boots, matching this
    /// codebase's `directBoot` philosophy for the other platforms).
    /// isValid() is false if the header describes an unsupported cartridge.
    explicit GbcCore(const std::vector<uint8_t> &rom);

    [[nodiscard]] bool isValid() const {
        return valid;
    }
    [[nodiscard]] bool isCgb() const {
        return memory.isCgb();
    }
    [[nodiscard]] const String &title() const {
        return memory.title();
    }

    /// Runs CPU/PPU/APU/timer forward until a VBlank is reached (one
    /// emulated frame, ~59.7275Hz of real GB time).
    void runFrame();

    /// Copies the just-rendered frame (RGBA8888, gbc::kScreenWidth x
    /// gbc::kScreenHeight) into `out`.
    void getFrame(uint32_t *out) const {
        ppu.getFrame(out);
    }

    void pressKey(gbc::GbcButton button);
    void releaseKey(gbc::GbcButton button);

    [[nodiscard]] int getFps() const {
        return fps;
    }

    // Battery-backed cartridge RAM (.sav) — separate from save states
    // (ioState() below): this is the game's own save data, expected to
    // persist across sessions regardless of quick-save slots. Call
    // setExtRamPath() once after construction (if hasBattery()); runFrame()
    // auto-flushes to that path whenever the RAM was actually written since
    // the last flush, once per frame — mirrors how real cartridges/emulators
    // commit battery RAM opportunistically rather than on every single write.
    bool loadExtRam(const String &path) {
        return memory.loadExtRam(path);
    }
    void setExtRamPath(const String &path) {
        extRamPath = path;
    }
    [[nodiscard]] bool hasBattery() const {
        return memory.hasBattery();
    }

    void ioState(StateArchive &archive);

    // ── Internal wiring for the CPU/Memory/Ppu/Apu subsystems ──────────────
    [[nodiscard]] GbcMemory &getMemory() {
        return memory;
    }
    [[nodiscard]] GbcApu &getApu() {
        return apu;
    }
    void requestInterrupt(uint8_t bit) {
        cpu.requestInterrupt(bit);
    }
    uint8_t readIe() const {
        return cpu.readIe();
    }
    void writeIe(uint8_t value) {
        cpu.writeIe(value);
    }
    /// Dispatches an I/O-port read/write (0xFF00-0xFF7F) to whichever
    /// subsystem owns that register — see gbc_core.cpp for the full map.
    uint8_t readIoPort(uint16_t address);
    void    writeIoPort(uint16_t address, uint8_t value);

  private:
    bool valid = false;

    GbcMemory memory;
    GbcCpu    cpu;
    GbcPpu    ppu;
    GbcApu    apu;

    uint8_t joypadSelect = 0x00; // bits 4-5 written by the game
    uint8_t buttonsDirs_    = 0x0f; // active-low: 0 = pressed
    uint8_t buttonsButtons_ = 0x0f;

    uint8_t serialData = 0, serialControl = 0;

    uint8_t div_ = 0;
    int     divCycleAccum = 0;
    uint8_t tima = 0, tma = 0, tac = 0;
    int     timaCycleAccum = 0;

    // CGB HDMA (VRAM DMA) — general-purpose transfers run to completion
    // immediately on trigger; H-Blank-mode transfers copy one 0x10-byte
    // block per H-Blank until done, matching real hardware pacing closely
    // enough for games that rely on it not stalling the CPU for the whole
    // transfer.
    uint8_t hdmaSrcHigh = 0, hdmaSrcLow = 0, hdmaDstHigh = 0, hdmaDstLow = 0;
    uint8_t hdmaStatus  = 0xff;
    bool    hdmaRunning = false;
    uint16_t hdmaNextSrc = 0, hdmaNextDst = 0;

    int fps = 0, fpsFrameCount = 0;
    std::chrono::steady_clock::time_point lastFpsTime;

    String extRamPath;

    void stepTimer(int cycles);
    void hdmaStart(uint8_t lenMode);
    void hdmaDoBlock();
    void updateJoypadInterrupt(uint8_t before);
};

} // namespace emulator_demo

#endif // GBC_CORE_H
