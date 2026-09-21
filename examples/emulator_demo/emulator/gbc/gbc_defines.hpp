#ifndef GBC_DEFINES_H
#define GBC_DEFINES_H

#include <cstdint>

namespace emulator_demo {

// Game Boy / Game Boy Color hardware constants. Reference: Pan Docs
// (gbdev.io/pandocs) and the public-domain-style koenk/gbc emulator
// (https://github.com/koenk/gbc), used here to cross-check timing constants
// and the cartridge-header tables — this port is an independent
// implementation matching this codebase's own conventions (StateArchive,
// sdl3::IOStream, class-per-subsystem with a back-pointer to the owning
// core), not a line-for-line translation.
namespace gbc {

constexpr uint32_t kClockHz = 4194304; // CPU clock, both DMG and CGB single-speed

constexpr int kScreenWidth  = 160;
constexpr int kScreenHeight = 144;

constexpr int kLyMax = 153; // LY counts 0..153 (144..153 = VBlank)

// PPU mode durations, in CPU cycles at single speed.
constexpr int kModeHBlankCycles  = 204;
constexpr int kModeVBlankCycles  = 4560; // for the whole VBlank period (10 lines)
constexpr int kModeOamCycles     = 80;
constexpr int kModeTransferCycles = 172;
constexpr int kFrameCycles       = 70224; // total cycles per frame (~59.7275Hz)

constexpr int kDivFreqHz = 16384;
constexpr int kTimaFreqsHz[4] = {4096, 262144, 65536, 16384};

constexpr uint16_t kRomHdrTitle      = 0x134;
constexpr uint16_t kRomHdrCgbFlag    = 0x143;
constexpr uint16_t kRomHdrCartType   = 0x147;
constexpr uint16_t kRomHdrRomSize    = 0x148;
constexpr uint16_t kRomHdrExtRamSize = 0x149;

constexpr uint32_t kRomBankSize    = 0x4000; // 16K
constexpr uint32_t kWramBankSize   = 0x1000; // 4K
constexpr uint32_t kVramBankSize   = 0x2000; // 8K
constexpr uint32_t kExtRamBankSize = 0x2000; // 8K

constexpr uint8_t kFlagZ = 0x80;
constexpr uint8_t kFlagN = 0x40;
constexpr uint8_t kFlagH = 0x20;
constexpr uint8_t kFlagC = 0x10;

// Interrupt bits (IE/IF), vector = bit_index*8 + 0x40.
constexpr uint8_t kIntVBlank = 1 << 0;
constexpr uint8_t kIntStat   = 1 << 1;
constexpr uint8_t kIntTimer  = 1 << 2;
constexpr uint8_t kIntSerial = 1 << 3;
constexpr uint8_t kIntJoypad = 1 << 4;

enum class GbcMbc { None, Mbc1, Mbc2, Mbc3, Mbc5 };

enum GbcButton {
    GBC_BTN_A,
    GBC_BTN_B,
    GBC_BTN_SELECT,
    GBC_BTN_START,
    GBC_BTN_RIGHT,
    GBC_BTN_LEFT,
    GBC_BTN_UP,
    GBC_BTN_DOWN,
    GBC_BTN_COUNT
};

} // namespace gbc

} // namespace emulator_demo

#endif // GBC_DEFINES_H
