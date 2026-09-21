#include "emulator/gbc/gbc_core.hpp"

#include "emulator/defines.hpp"

namespace emulator_demo {

GbcCore::GbcCore(const std::vector<uint8_t> &rom)
    : memory(this), cpu(this), ppu(this), apu(this), lastFpsTime(std::chrono::steady_clock::now()) {
    valid = memory.loadRom(rom);
    if (valid) cpu.reset(memory.isCgb());
}

void GbcCore::pressKey(gbc::GbcButton button) {
    uint8_t before = joypadSelect;
    switch (button) {
        case gbc::GBC_BTN_A: buttonsButtons_ &= ~1; break;
        case gbc::GBC_BTN_B: buttonsButtons_ &= ~2; break;
        case gbc::GBC_BTN_SELECT: buttonsButtons_ &= ~4; break;
        case gbc::GBC_BTN_START: buttonsButtons_ &= ~8; break;
        case gbc::GBC_BTN_RIGHT: buttonsDirs_ &= ~1; break;
        case gbc::GBC_BTN_LEFT: buttonsDirs_ &= ~2; break;
        case gbc::GBC_BTN_UP: buttonsDirs_ &= ~4; break;
        case gbc::GBC_BTN_DOWN: buttonsDirs_ &= ~8; break;
        default: break;
    }
    updateJoypadInterrupt(before);
}

void GbcCore::releaseKey(gbc::GbcButton button) {
    switch (button) {
        case gbc::GBC_BTN_A: buttonsButtons_ |= 1; break;
        case gbc::GBC_BTN_B: buttonsButtons_ |= 2; break;
        case gbc::GBC_BTN_SELECT: buttonsButtons_ |= 4; break;
        case gbc::GBC_BTN_START: buttonsButtons_ |= 8; break;
        case gbc::GBC_BTN_RIGHT: buttonsDirs_ |= 1; break;
        case gbc::GBC_BTN_LEFT: buttonsDirs_ |= 2; break;
        case gbc::GBC_BTN_UP: buttonsDirs_ |= 4; break;
        case gbc::GBC_BTN_DOWN: buttonsDirs_ |= 8; break;
        default: break;
    }
}

void GbcCore::updateJoypadInterrupt(uint8_t /*before*/) {
    // Real hardware fires the joypad interrupt on a 1->0 transition of any
    // selected line; approximated here as "fire whenever a button is
    // pressed while its group is selected" — good enough for the games
    // that actually rely on this interrupt (most just poll KEY1 instead).
    if (((joypadSelect & (1 << 4)) == 0 && buttonsDirs_ != 0x0f) ||
        ((joypadSelect & (1 << 5)) == 0 && buttonsButtons_ != 0x0f))
        cpu.requestInterrupt(gbc::kIntJoypad);
}

void GbcCore::stepTimer(int cycles) {
    divCycleAccum += cycles;
    constexpr int kDivPeriod = int(gbc::kClockHz / gbc::kDivFreqHz);
    while (divCycleAccum >= kDivPeriod) {
        divCycleAccum -= kDivPeriod;
        div_++;
    }

    if (tac & (1 << 2)) {
        timaCycleAccum += cycles;
        int period = int(gbc::kClockHz) / gbc::kTimaFreqsHz[tac & 3];
        while (timaCycleAccum >= period) {
            timaCycleAccum -= period;
            tima++;
            if (tima == 0) {
                tima = tma;
                cpu.requestInterrupt(gbc::kIntTimer);
            }
        }
    }
}

void GbcCore::hdmaStart(uint8_t lenMode) {
    uint16_t blocks = uint16_t((lenMode & ~0x80) + 1);
    bool     hblankMode = lenMode & 0x80;
    uint16_t src = uint16_t(((hdmaSrcHigh << 8) | hdmaSrcLow) & ~0xf);
    uint16_t dst = uint16_t((((hdmaDstHigh << 8) | hdmaDstLow) & 0x1fff) | 0x8000);

    if (hdmaRunning && !hblankMode) {
        hdmaRunning = false;
        hdmaStatus  = 0xff;
        return;
    }

    if (!hblankMode) {
        uint16_t len = uint16_t(blocks * 0x10);
        for (uint16_t i = 0; i < len; i++) memory.write(uint16_t(dst + i), memory.read(uint16_t(src + i)));
        hdmaStatus = 0xff;
    } else {
        hdmaRunning  = true;
        hdmaNextSrc  = src;
        hdmaNextDst  = dst;
        hdmaStatus   = uint8_t(blocks - 1);
    }
}

void GbcCore::hdmaDoBlock() {
    for (int i = 0; i < 0x10; i++) {
        uint8_t v = memory.read(hdmaNextSrc++);
        memory.write(hdmaNextDst++, v);
    }
    hdmaStatus--;
    if (hdmaStatus == 0xff) hdmaRunning = false; // underflowed past 0 -> done
}

uint8_t GbcCore::readIoPort(uint16_t address) {
    if (address == 0xff00) {
        uint8_t rv = uint8_t(joypadSelect | 0xc0);
        if ((joypadSelect & (1 << 4)) == 0) rv = uint8_t((rv & 0xf0) | buttonsDirs_);
        else if ((joypadSelect & (1 << 5)) == 0) rv = uint8_t((rv & 0xf0) | buttonsButtons_);
        else rv |= 0x0f;
        return rv;
    }
    switch (address) {
        case 0xff01: return serialData;
        case 0xff02: return serialControl;
        case 0xff04: return div_;
        case 0xff05: return tima;
        case 0xff06: return tma;
        case 0xff07: return tac;
        case 0xff0f: return cpu.readIf();
        case 0xff4d: return 0x7f; // KEY1 (CGB double-speed) — always "not requested/not fast"
        case 0xff55: return hdmaStatus;
        case 0xff56: return 0x00; // infrared, unused
        default:
            if (address >= 0xff10 && address <= 0xff26) return apu.read(address);
            if (address >= 0xff30 && address <= 0xff3f) return apu.read(address);
            if (address >= 0xff40 && address <= 0xff4b) return ppu.read(address);
            if (address >= 0xff68 && address <= 0xff6b) return ppu.read(address);
            return 0xff;
    }
}

void GbcCore::writeIoPort(uint16_t address, uint8_t value) {
    if (address == 0xff00) {
        joypadSelect = uint8_t(value & 0x30);
        updateJoypadInterrupt(joypadSelect);
        return;
    }
    switch (address) {
        case 0xff01: serialData = value; return;
        case 0xff02: serialControl = value; return;
        case 0xff04: div_ = 0; divCycleAccum = 0; return;
        case 0xff05: tima = value; return;
        case 0xff06: tma = value; return;
        case 0xff07: tac = value; return;
        case 0xff0f: cpu.writeIf(value); return;
        case 0xff4d: return; // KEY1, ignored (no double-speed emulation)
        case 0xff51: hdmaSrcHigh = value; return;
        case 0xff52: hdmaSrcLow = value; return;
        case 0xff53: hdmaDstHigh = value; return;
        case 0xff54: hdmaDstLow = value; return;
        case 0xff55: hdmaStart(value); return;
        case 0xff56: return; // infrared, ignored
        default:
            if (address >= 0xff10 && address <= 0xff26) { apu.write(address, value); return; }
            if (address >= 0xff30 && address <= 0xff3f) { apu.write(address, value); return; }
            if (address >= 0xff40 && address <= 0xff4b) { ppu.write(address, value); return; }
            if (address >= 0xff68 && address <= 0xff6b) { ppu.write(address, value); return; }
    }
}

void GbcCore::runFrame() {
    if (!valid) return;
    do {
        int cyc = cpu.step();
        ppu.step(cyc);
        apu.step(cyc);
        stepTimer(cyc);
        if (ppu.enteredHBlank_ && hdmaRunning) hdmaDoBlock();
    } while (!ppu.enteredVBlank());

    fpsFrameCount++;
    auto now = std::chrono::steady_clock::now();
    if (now - lastFpsTime >= std::chrono::seconds(1)) {
        fps = fpsFrameCount;
        fpsFrameCount = 0;
        lastFpsTime = now;
    }

    if (memory.hasBattery() && memory.extRamDirty() && !extRamPath.IsEmpty()) {
        if (memory.saveExtRam(extRamPath)) memory.clearExtRamDirty();
    }
}

void GbcCore::ioState(StateArchive &archive) {
    memory.ioState(archive);
    cpu.ioState(archive);
    ppu.ioState(archive);
    apu.ioState(archive);
    archive.io(joypadSelect);
    archive.io(buttonsDirs_);
    archive.io(buttonsButtons_);
    archive.io(serialData);
    archive.io(serialControl);
    archive.io(div_);
    archive.io(divCycleAccum);
    archive.io(tima);
    archive.io(tma);
    archive.io(tac);
    archive.io(timaCycleAccum);
    archive.io(hdmaSrcHigh);
    archive.io(hdmaSrcLow);
    archive.io(hdmaDstHigh);
    archive.io(hdmaDstLow);
    archive.io(hdmaStatus);
    archive.io(hdmaRunning);
    archive.io(hdmaNextSrc);
    archive.io(hdmaNextDst);
}

} // namespace emulator_demo
