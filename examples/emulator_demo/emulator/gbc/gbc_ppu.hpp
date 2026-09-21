#ifndef GBC_PPU_H
#define GBC_PPU_H

#include <array>
#include <cstdint>
#include <vector>

#include "gbc_defines.hpp"
#include "../state_archive.hpp"

namespace emulator_demo {

class GbcCore;

/**
 * @brief GBC/DMG picture processing unit: the mode 0-3 scanline state
 * machine (H-Blank/V-Blank/OAM-scan/pixel-transfer) driving STAT/LY and the
 * vblank/stat interrupts, plus the actual background/window/sprite scanline
 * renderer for both DMG (4-shade grayscale-green) and CGB (15-bit RGB
 * palette RAM) modes. Produces one RGBA8888 frame per call sequence that
 * reaches VBlank — see GbcCore::runFrame().
 */
class GbcPpu {
  public:
    explicit GbcPpu(GbcCore *core) : core(core) {
    }

    /// Advances the PPU state machine by `cycles` CPU cycles (the cost of
    /// the instruction just executed) and renders the current scanline the
    /// instant it's reached (matches real hardware: a line's pixels are
    /// produced during its own "mode 3" window, not after the fact).
    void step(int cycles);

    [[nodiscard]] bool enteredVBlank() const {
        return enteredVBlank_;
    }

    /// Copies the completed frame (RGBA8888, kScreenWidth x kScreenHeight)
    /// into `out`.
    void getFrame(uint32_t *out) const;

    uint8_t read(uint16_t port);
    void    write(uint16_t port, uint8_t value);

    void ioState(StateArchive &archive);

  private:
    GbcCore *core;

    uint8_t lcdc = 0x91, stat = 0x00;
    uint8_t scy = 0, scx = 0, ly = 0, lyc = 0;
    uint8_t bgp = 0xfc, obp0 = 0xff, obp1 = 0xff;
    uint8_t wy = 0, wx = 0;
    uint8_t bgpi = 0, obpi = 0;
    std::array<uint8_t, 0x40> bgpd{};
    std::array<uint8_t, 0x40> obpd{};

    int modeCyclesLeft = 0;
    bool enteredVBlank_ = false;
    bool enteredHBlank_ = false;

    std::vector<uint32_t> framebuffer; // RGBA8888, filled line-by-line

    void renderLine();
    static uint32_t dmgShade(uint8_t colorIndex);
    static uint32_t cgbColor(const uint8_t *palData, uint8_t palIndex, uint8_t colorIndex);

    friend class GbcCore; // GbcCore::step()'s HDMA needs enteredHBlank_/stat mode
};

} // namespace emulator_demo

#endif // GBC_PPU_H
