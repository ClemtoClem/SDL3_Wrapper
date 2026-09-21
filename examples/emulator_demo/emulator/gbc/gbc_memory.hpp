#ifndef GBC_MEMORY_H
#define GBC_MEMORY_H

#include <array>
#include <cstdint>
#include <vector>

#include "gbc_defines.hpp"
#include "core/core.hpp"
#include "../state_archive.hpp"

namespace emulator_demo {

class GbcCore;

/**
 * @brief GBC/DMG memory map + memory bank controller (MBC0/1/3/5).
 *
 * Owns every RAM/ROM array (ROM, WRAM, external/cartridge RAM, VRAM, OAM,
 * HRAM) and the handful of bank-select registers real cartridges expose as
 * writes into ROM address space (0x0000-0x7FFF is never actually "ROM
 * writes" on hardware — it's how games talk to the MBC chip). I/O-port
 * reads/writes (0xFF00-0xFF7F) are dispatched here but delegate to
 * GbcCore's other subsystems (Ppu/Apu/Timer/joypad) via the core
 * back-pointer, mirroring how the NDS/GBA Memory class routes to Core's
 * other subsystems.
 */
class GbcMemory {
  public:
    explicit GbcMemory(GbcCore *core) : core(core) {
    }

    /// Parses the cartridge header (title/CGB flag/MBC type/ROM+RAM bank
    /// counts) from `rom` and allocates all banked memory accordingly.
    /// Returns false if the header describes an unsupported cartridge type.
    bool loadRom(const std::vector<uint8_t> &rom);

    /// Loads/saves battery-backed cartridge RAM (a plain flat dump of
    /// mem_EXTRAM, no envelope) — the ".sav" file next to the ROM.
    bool loadExtRam(const String &path);
    bool saveExtRam(const String &path) const;
    [[nodiscard]] bool hasBattery() const {
        return hasBattery_;
    }
    [[nodiscard]] bool extRamDirty() const {
        return extRamDirty_;
    }
    void clearExtRamDirty() {
        extRamDirty_ = false;
    }

    uint8_t  read(uint16_t address);
    void     write(uint16_t address, uint8_t value);
    uint16_t read16(uint16_t address);
    void     write16(uint16_t address, uint16_t value);

    void push16(uint16_t &sp, uint16_t value);
    uint16_t pop16(uint16_t &sp);

    [[nodiscard]] bool isCgb() const {
        return isCgb_;
    }
    [[nodiscard]] const String &title() const {
        return title_;
    }

    uint8_t *vramBank(int bank) {
        return &vram[size_t(bank & (numVramBanks - 1)) * gbc::kVramBankSize];
    }
    [[nodiscard]] int currentVramBank() const {
        return vramBank_;
    }
    uint8_t *oamPtr() {
        return oam.data();
    }

    void ioState(StateArchive &archive);

    // Direct field access for Ppu/Apu/Timer register reads/writes — those
    // subsystems own the actual behavior, GbcMemory's read()/write() I/O
    // dispatch just forwards to them via GbcCore, so nothing here duplicates
    // register semantics.

  private:
    GbcCore *core;

    std::vector<uint8_t> rom;
    std::vector<uint8_t> wram;
    std::vector<uint8_t> extram;
    std::vector<uint8_t> vram;
    std::array<uint8_t, 0xa0> oam{};
    std::array<uint8_t, 0x7f> hram{};

    String   title_;
    bool     isCgb_       = false;
    gbc::GbcMbc mbc       = gbc::GbcMbc::None;
    bool     hasExtRam_   = false;
    bool     hasBattery_  = false;
    bool     hasRtc_      = false;
    bool     extRamDirty_ = false;

    int numRomBanks    = 2;
    int numWramBanks   = 2;
    int numExtRamBanks = 0;
    int numVramBanks   = 1;

    int romBank_   = 1;
    int wramBank_  = 1;
    int vramBank_  = 0;
    int extRamBank_ = 0;

    // MBC1-specific: the 2-bit "upper ROM bank bits / RAM bank" register is
    // shared between two roles selected by romRamModeSelect.
    uint8_t mbc1UpperBits    = 0;
    uint8_t romRamModeSelect = 0;

    // MBC3 RTC: 5 latched registers (seconds/minutes/hours/day-lo/day-hi)
    // plus the live registers they're latched from, and the write-0x00-
    // then-0x01 latch sequence's pending state.
    uint8_t rtcSelect      = 0;
    uint8_t rtcLatchPending = 0xff;
    uint8_t rtcLatched[5]  = {};

    void mbcWriteLow(uint16_t address, uint8_t value);  // 0x0000-0x3FFF
    void mbcWriteMid(uint16_t address, uint8_t value);  // 0x4000-0x5FFF
    void mbcWriteHigh(uint8_t value);                   // 0x6000-0x7FFF
    void latchRtc();

    uint8_t readIo(uint16_t address);
    void    writeIo(uint16_t address, uint8_t value);
};

} // namespace emulator_demo

#endif // GBC_MEMORY_H
