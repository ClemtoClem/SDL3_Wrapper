#include "emulator/gbc/gbc_ppu.hpp"

#include <algorithm>
#include <array>

#include "emulator/gbc/gbc_core.hpp"
#include "emulator/gbc/gbc_memory.hpp"

namespace emulator_demo {

namespace {
struct OamEntry {
    uint8_t y, x, tile, flags;
};
} // namespace

void GbcPpu::step(int cycles) {
    enteredVBlank_ = false;
    enteredHBlank_ = false;

    modeCyclesLeft -= cycles;
    if (modeCyclesLeft < 0) {
        // Durées CUMULÉES (`+=`) : l'instruction qui fait déborder un mode
        // entame déjà le suivant. Les réaffecter (`=`) perdait ce débordement
        // à chaque transition et allongeait l'image.
        //
        // VBlank dure 10 LIGNES (LY 144 à 153, 456 cycles chacune). Il était
        // modélisé comme un seul bloc de 4560 cycles avec LY figé à 144, puis
        // les lignes 144-153 étaient REJOUÉES comme des lignes visibles
        // (OAM/transfert/H-Blank) : 74 784 cycles par image au lieu de
        // 70 224, soit 6,5 % de trop — un jeu cadencé par l'audio tournait à
        // ~56 img/s, et STAT annonçait un mode faux pendant ces lignes.
        const int lineCycles = gbc::kModeVBlankCycles / 10;
        auto updateLyCompare = [this] {
            stat = uint8_t((stat & 0xfb) | ((ly == lyc) ? 4 : 0));
            if ((stat & (1 << 6)) && ly == lyc) core->requestInterrupt(gbc::kIntStat);
        };
        uint8_t mode = stat & 3;
        switch (mode) {
            case 0: // H-Blank -> OAM (ligne suivante) ou VBlank
                if (ly == 143) {
                    stat = uint8_t((stat & 0xfc) | 1);
                    modeCyclesLeft += lineCycles;
                    core->requestInterrupt(gbc::kIntVBlank);
                    enteredVBlank_ = true;
                } else {
                    stat = uint8_t((stat & 0xfc) | 2);
                    modeCyclesLeft += gbc::kModeOamCycles;
                }
                ly = uint8_t(ly + 1);
                updateLyCompare();
                break;
            case 1: // VBlank : une ligne par valeur de LY, puis OAM de la ligne 0
                if (ly < gbc::kLyMax) {
                    ly = uint8_t(ly + 1);
                    modeCyclesLeft += lineCycles;
                } else {
                    ly = 0;
                    stat = uint8_t((stat & 0xfc) | 2);
                    modeCyclesLeft += gbc::kModeOamCycles;
                }
                updateLyCompare();
                break;
            case 2: // OAM -> pixel transfer
                stat = uint8_t((stat & 0xfc) | 3);
                modeCyclesLeft += gbc::kModeTransferCycles;
                break;
            case 3: // pixel transfer -> H-Blank
                stat = uint8_t(stat & 0xfc);
                modeCyclesLeft += gbc::kModeHBlankCycles;
                enteredHBlank_ = true;
                break;
        }
        // Interruptions de mode seulement sur un CHANGEMENT de mode : VBlank
        // enchaîne désormais 10 transitions sans quitter le mode 1.
        uint8_t newMode = stat & 3;
        if (newMode != mode) {
            if ((stat & (1 << 5)) && newMode == 2) core->requestInterrupt(gbc::kIntStat);
            if ((stat & (1 << 4)) && newMode == 1) core->requestInterrupt(gbc::kIntStat);
            if ((stat & (1 << 3)) && newMode == 0) core->requestInterrupt(gbc::kIntStat);
        }
    }

    if (enteredHBlank_) renderLine();
}

uint32_t GbcPpu::dmgShade(uint8_t colorIndex) {
    // Classic DMG green-tinted 4-shade palette (lightest to darkest) —
    // matches the shading most emulators default to for original Game Boy
    // software running without CGB color data.
    static constexpr uint32_t kShades[4] = {0xff0fbc9b, 0xff0fac8b, 0xff306230, 0xff0f380f};
    return kShades[colorIndex & 3];
}

uint32_t GbcPpu::cgbColor(const uint8_t *palData, uint8_t palIndex, uint8_t colorIndex) {
    uint8_t idx = uint8_t(palIndex * 8 + colorIndex * 2);
    uint16_t raw = uint16_t(palData[idx] | (palData[idx + 1] << 8));
    uint8_t r = raw & 0x1f, g = (raw >> 5) & 0x1f, b = (raw >> 10) & 0x1f;
    // 5-bit-per-channel -> 8-bit, same *255/31 scaling the NDS core's own
    // rgb5ToRgb8 conversion uses for the same 15-bit color format.
    uint32_t r8 = r * 255 / 31, g8 = g * 255 / 31, b8 = b * 255 / 31;
    return 0xff000000u | (b8 << 16) | (g8 << 8) | r8;
}

void GbcPpu::renderLine() {
    if (framebuffer.size() != size_t(gbc::kScreenWidth) * gbc::kScreenHeight)
        framebuffer.assign(size_t(gbc::kScreenWidth) * gbc::kScreenHeight, 0xff000000u);

    // renderLine() is called right as we enter H-Blank for line `ly - 1`'s
    // worth of pixel-transfer work having just finished — but `ly` was
    // already advanced to the NEXT line by the H-Blank branch above only
    // when leaving H-Blank, not when entering it, so at this point `ly`
    // still refers to the line whose pixels were just produced.
    int y = ly;
    if (y >= gbc::kScreenHeight) return;

    GbcMemory &mem = core->getMemory();
    bool useColor = mem.isCgb();

    bool winMapHigh   = lcdc & (1 << 6);
    bool winEnable     = lcdc & (1 << 5);
    bool tileDataLow    = lcdc & (1 << 4);
    bool bgMapHigh     = lcdc & (1 << 3);
    bool obj8x16       = lcdc & (1 << 2);
    bool objEnable     = lcdc & (1 << 1);
    bool bgEnable      = lcdc & (1 << 0);
    bool tileIdxUnsigned = tileDataLow;

    if (useColor) bgEnable = true; // CGB: bit 0 instead gates BG-vs-OBJ priority, not visibility

    uint16_t tileDataAddr = tileDataLow ? 0x8000 : 0x9000;
    uint16_t bgMapAddr    = bgMapHigh ? 0x9c00 : 0x9800;
    uint16_t winMapAddr   = winMapHigh ? 0x9c00 : 0x9800;

    uint8_t *bank0 = mem.vramBank(0);
    uint8_t *bank1 = useColor ? mem.vramBank(1) : bank0;
    uint8_t *bgWinTileData = bank0 + (tileDataAddr - 0x8000);
    uint8_t *bgMap     = bank0 + (bgMapAddr - 0x8000);
    uint8_t *winMap    = bank0 + (winMapAddr - 0x8000);
    uint8_t *bgAttrs   = bank1 + (bgMapAddr - 0x8000);  // CGB tile attrs live in VRAM bank 1, same map layout
    uint8_t *winAttrs  = bank1 + (winMapAddr - 0x8000);

    uint32_t *line = &framebuffer[size_t(y) * gbc::kScreenWidth];
    // Raw (pre-palette) BG/window color index per pixel this line, 0-3 —
    // needed separately from the final RGBA color because sprite
    // "OBJ-behind-BG" priority (flag bit 7) checks the raw index, not the
    // resolved color (which for CGB depends on a per-tile palette).
    std::array<uint8_t, gbc::kScreenWidth> bgColorIndex{};

    // Background
    if (bgEnable) {
        for (int x = 0; x < gbc::kScreenWidth; x++) {
            int bgX = (x + scx) & 0xff, bgY = (y + scy) & 0xff;
            int tileX = bgX / 8, tileY = bgY / 8;
            int mapIdx = tileX + tileY * 32;
            int offX = bgX % 8, offY = bgY % 8;

            uint8_t rawIdx = bgMap[mapIdx];
            int16_t tileIdx = tileIdxUnsigned ? int16_t(rawIdx) : int16_t(int8_t(rawIdx));
            uint8_t attr = useColor ? bgAttrs[mapIdx] : 0;
            uint8_t *tileBank = (attr & (1 << 3)) ? bank1 : bank0;
            uint8_t *tileData = useColor ? (tileBank + (tileDataAddr - 0x8000)) : bgWinTileData;

            int tileOff = offX + offY * 8;
            int shift = 7 - (tileOff % 8);
            int dataOff = tileIdx * 16 + (tileOff / 8) * 2;
            uint8_t b1 = tileData[dataOff], b2 = tileData[dataOff + 1];
            uint8_t colIdx = uint8_t(((b1 >> shift) & 1) | (((b2 >> shift) & 1) << 1));

            bgColorIndex[x] = colIdx;
            line[x] = useColor ? cgbColor(bgpd.data(), attr & 7, colIdx) : dmgShade((bgp >> (colIdx * 2)) & 3);
        }
    } else {
        for (int x = 0; x < gbc::kScreenWidth; x++) line[x] = 0xffffffffu;
    }

    // Window
    if (winEnable) {
        for (int x = 0; x < gbc::kScreenWidth; x++) {
            int winX = x - (wx - 7), winY = y - wy;
            if (winX < 0 || winY < 0) continue;
            int tileX = winX / 8, tileY = winY / 8;
            int offX = winX % 8, offY = winY % 8;
            int mapIdx = tileX + tileY * 32;

            uint8_t rawIdx = winMap[mapIdx];
            int16_t tileIdx = tileIdxUnsigned ? int16_t(rawIdx) : int16_t(int8_t(rawIdx));
            uint8_t attr = useColor ? winAttrs[mapIdx] : 0;
            uint8_t *tileBank = (attr & (1 << 3)) ? bank1 : bank0;
            uint8_t *tileData = useColor ? (tileBank + (tileDataAddr - 0x8000)) : bgWinTileData;

            int tileOff = offX + offY * 8;
            int shift = 7 - (tileOff % 8);
            int dataOff = tileIdx * 16 + (tileOff / 8) * 2;
            uint8_t b1 = tileData[dataOff], b2 = tileData[dataOff + 1];
            uint8_t colIdx = uint8_t(((b1 >> shift) & 1) | (((b2 >> shift) & 1) << 1));

            bgColorIndex[x] = colIdx;
            line[x] = useColor ? cgbColor(bgpd.data(), attr & 7, colIdx) : dmgShade((bgp >> (colIdx * 2)) & 3);
        }
    }

    // Sprites (up to 10 per line, OAM order — no priority sort, matching
    // typical simple GB PPU ports; good enough for the vast majority of
    // games, which rely on OAM order for sprite priority anyway).
    if (objEnable) {
        auto *oam = reinterpret_cast<OamEntry *>(mem.oamPtr());
        int objTileHeight = obj8x16 ? 16 : 8;
        OamEntry objs[10];
        int numObjs = 0;
        for (int i = 0; i < 40 && numObjs < 10; i++) {
            int oy = oam[i].y - 16;
            if (y >= oy && y < oy + objTileHeight) objs[numObjs++] = oam[i];
        }
        for (int x = 0; x < gbc::kScreenWidth; x++) {
            for (int i = 0; i < numObjs; i++) {
                int ox = x - (objs[i].x - 8);
                if (ox < 0 || ox >= 8) continue;
                int oyLocal = y - (objs[i].y - 16);
                if (objs[i].flags & (1 << 5)) ox = 7 - ox;
                if (objs[i].flags & (1 << 6)) oyLocal = objTileHeight - 1 - oyLocal;

                uint8_t tileNum = objs[i].tile;
                if (obj8x16) tileNum &= 0xfe;
                int tileOff = ox + oyLocal * 8;
                int shift = 7 - (tileOff % 8);
                int dataOff = tileNum * 16 + (tileOff / 8) * 2;
                uint8_t *tileBank = (useColor && (objs[i].flags & (1 << 3))) ? bank1 : bank0;
                uint8_t b1 = tileBank[dataOff], b2 = tileBank[dataOff + 1];
                uint8_t colIdx = uint8_t(((b1 >> shift) & 1) | (((b2 >> shift) & 1) << 1));
                if (colIdx == 0) continue;

                // OBJ-behind-BG (flag bit 7): hidden behind any non-zero BG/window pixel.
                if ((objs[i].flags & (1 << 7)) && bgEnable && bgColorIndex[x] != 0) continue;

                if (useColor) {
                    line[x] = cgbColor(obpd.data(), objs[i].flags & 7, colIdx);
                } else {
                    uint8_t pal = (objs[i].flags & (1 << 4)) ? obp1 : obp0;
                    line[x] = dmgShade((pal >> (colIdx * 2)) & 3);
                }
                break; // first (highest-priority, lowest OAM index) opaque sprite wins
            }
        }
    }
}

void GbcPpu::getFrame(uint32_t *out) const {
    if (framebuffer.size() != size_t(gbc::kScreenWidth) * gbc::kScreenHeight) {
        std::fill_n(out, size_t(gbc::kScreenWidth) * gbc::kScreenHeight, 0xff000000u);
        return;
    }
    std::copy(framebuffer.begin(), framebuffer.end(), out);
}

uint8_t GbcPpu::read(uint16_t port) {
    switch (port) {
        case 0xff40: return lcdc;
        case 0xff41: return stat;
        case 0xff42: return scy;
        case 0xff43: return scx;
        case 0xff44: return ly;
        case 0xff45: return lyc;
        case 0xff47: return bgp;
        case 0xff48: return obp0;
        case 0xff49: return obp1;
        case 0xff4a: return wy;
        case 0xff4b: return wx;
        case 0xff68: return bgpi;
        case 0xff69: return bgpd[bgpi & 0x3f];
        case 0xff6a: return obpi;
        case 0xff6b: return obpd[obpi & 0x3f];
        default: return 0xff;
    }
}

void GbcPpu::write(uint16_t port, uint8_t value) {
    switch (port) {
        case 0xff40: lcdc = value; break;
        case 0xff41: stat = uint8_t((value & ~7) | (stat & 7)); break;
        case 0xff42: scy = value; break;
        case 0xff43: scx = value; break;
        case 0xff44: ly = 0; stat = uint8_t((stat & 0xfb) | ((ly == lyc) ? 4 : 0)); break;
        case 0xff45:
            lyc = value;
            stat = uint8_t((stat & 0xfb) | ((ly == lyc) ? 4 : 0));
            break;
        case 0xff47: bgp = value; break;
        case 0xff48: obp0 = value; break;
        case 0xff49: obp1 = value; break;
        case 0xff4a: wy = value; break;
        case 0xff4b: wx = value; break;
        case 0xff68: bgpi = value; break;
        case 0xff69:
            bgpd[bgpi & 0x3f] = value;
            if (bgpi & (1 << 7)) bgpi = uint8_t((((bgpi & 0x3f) + 1) & 0x3f) | (1 << 7));
            break;
        case 0xff6a: obpi = value; break;
        case 0xff6b:
            obpd[obpi & 0x3f] = value;
            if (obpi & (1 << 7)) obpi = uint8_t((((obpi & 0x3f) + 1) & 0x3f) | (1 << 7));
            break;
        default: break;
    }
}

void GbcPpu::ioState(StateArchive &archive) {
    archive.io(lcdc);
    archive.io(stat);
    archive.io(scy);
    archive.io(scx);
    archive.io(ly);
    archive.io(lyc);
    archive.io(bgp);
    archive.io(obp0);
    archive.io(obp1);
    archive.io(wy);
    archive.io(wx);
    archive.io(bgpi);
    archive.io(obpi);
    archive.io(bgpd);
    archive.io(obpd);
    archive.io(modeCyclesLeft);
}

} // namespace emulator_demo
