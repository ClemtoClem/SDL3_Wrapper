#include "emulator/gbc/gbc_memory.hpp"

#include <chrono>
#include <cstring>
#include <ctime>

#include "emulator/defines.hpp"
#include "emulator/gbc/gbc_core.hpp"
#include "sdl3/iostream.hpp"

namespace emulator_demo {

using gbc::GbcMbc;

bool GbcMemory::loadRom(const std::vector<uint8_t> &romData) {
    if (romData.size() <= gbc::kRomHdrExtRamSize) return false;

    char titleBuf[17] = {};
    for (int i = 0; i < 16 && size_t(gbc::kRomHdrTitle + i) < romData.size(); i++) {
        uint8_t c = romData[gbc::kRomHdrTitle + i];
        titleBuf[i] = (c >= 0x20 && c < 0x7f) ? char(c) : '\0';
    }
    title_ = String(titleBuf);

    isCgb_ = (romData[gbc::kRomHdrCgbFlag] & 0x80) != 0;

    uint8_t cartType = romData[gbc::kRomHdrCartType];
    switch (cartType) {
        case 0x00: mbc = GbcMbc::None; break;
        case 0x01: mbc = GbcMbc::Mbc1; break;
        case 0x02: mbc = GbcMbc::Mbc1; hasExtRam_ = true; break;
        case 0x03: mbc = GbcMbc::Mbc1; hasExtRam_ = true; hasBattery_ = true; break;
        case 0x05: mbc = GbcMbc::Mbc2; break;
        case 0x06: mbc = GbcMbc::Mbc2; hasBattery_ = true; break;
        case 0x08: mbc = GbcMbc::None; hasExtRam_ = true; break;
        case 0x09: mbc = GbcMbc::None; hasExtRam_ = true; hasBattery_ = true; break;
        case 0x0f: mbc = GbcMbc::Mbc3; hasBattery_ = true; hasRtc_ = true; break;
        case 0x10: mbc = GbcMbc::Mbc3; hasExtRam_ = true; hasBattery_ = true; hasRtc_ = true; break;
        case 0x11: mbc = GbcMbc::Mbc3; break;
        case 0x12: mbc = GbcMbc::Mbc3; hasExtRam_ = true; break;
        case 0x13: mbc = GbcMbc::Mbc3; hasExtRam_ = true; hasBattery_ = true; break;
        case 0x19: mbc = GbcMbc::Mbc5; break;
        case 0x1a: mbc = GbcMbc::Mbc5; hasExtRam_ = true; break;
        case 0x1b: mbc = GbcMbc::Mbc5; hasExtRam_ = true; hasBattery_ = true; break;
        case 0x1c: mbc = GbcMbc::Mbc5; break; // +rumble, not emulated
        case 0x1d: mbc = GbcMbc::Mbc5; hasExtRam_ = true; break;
        case 0x1e: mbc = GbcMbc::Mbc5; hasExtRam_ = true; hasBattery_ = true; break;
        default:
            LOG("GbcMemory: unsupported cartridge type 0x%02X\n", cartType);
            return false;
    }

    uint8_t romSizeCode = romData[gbc::kRomHdrRomSize];
    switch (romSizeCode) {
        case 0x00: numRomBanks = 2; break;
        case 0x01: numRomBanks = 4; break;
        case 0x02: numRomBanks = 8; break;
        case 0x03: numRomBanks = 16; break;
        case 0x04: numRomBanks = 32; break;
        case 0x05: numRomBanks = 64; break;
        case 0x06: numRomBanks = 128; break;
        case 0x07: numRomBanks = 256; break;
        case 0x08: numRomBanks = 512; break;
        case 0x52: numRomBanks = 72; break;
        case 0x53: numRomBanks = 80; break;
        case 0x54: numRomBanks = 96; break;
        default:
            LOG("GbcMemory: unsupported ROM size code 0x%02X\n", romSizeCode);
            return false;
    }

    uint8_t ramSizeCode = romData[gbc::kRomHdrExtRamSize];
    switch (ramSizeCode) {
        case 0x00: numExtRamBanks = 0; break;
        case 0x01: numExtRamBanks = 1; break; // 2K, treated as one 8K bank
        case 0x02: numExtRamBanks = 1; break;
        case 0x03: numExtRamBanks = 4; break;
        case 0x04: numExtRamBanks = 16; break;
        case 0x05: numExtRamBanks = 8; break;
        default:
            LOG("GbcMemory: unsupported EXTRAM size code 0x%02X\n", ramSizeCode);
            return false;
    }
    // MBC2 has 512x4-bit built-in RAM, not banked cartridge RAM — model as
    // one small bank so the generic bank-indexed read/write path still works.
    if (mbc == GbcMbc::Mbc2) { hasExtRam_ = true; numExtRamBanks = 1; }

    numWramBanks = isCgb_ ? 8 : 2;
    numVramBanks = isCgb_ ? 2 : 1;

    rom.assign(romData.begin(), romData.end());
    rom.resize(size_t(numRomBanks) * gbc::kRomBankSize, 0);
    wram.assign(size_t(numWramBanks) * gbc::kWramBankSize, 0);
    vram.assign(size_t(numVramBanks) * gbc::kVramBankSize, 0);
    if (hasExtRam_) extram.assign(size_t(numExtRamBanks) * gbc::kExtRamBankSize, 0);

    romBank_ = 1;
    wramBank_ = 1;
    vramBank_ = 0;
    extRamBank_ = 0;
    mbc1UpperBits = 0;
    romRamModeSelect = 0;
    rtcSelect = 0;
    rtcLatchPending = 0xff;
    std::memset(rtcLatched, 0, sizeof(rtcLatched));
    return true;
}

bool GbcMemory::loadExtRam(const String &path) {
    if (!hasBattery_ || extram.empty()) return true; // nothing to load, not an error
    auto res = sdl3::IOStream::FromFile(path, "rb");
    if (!res) return false; // no save file yet — normal for a fresh game
    sdl3::IOStream file = std::move(res).Unwrap();
    std::vector<uint8_t> data = file.ReadAll();
    if (data.size() != extram.size()) return false;
    extram = std::move(data);
    return true;
}

bool GbcMemory::saveExtRam(const String &path) const {
    if (!hasBattery_ || extram.empty()) return true;
    auto res = sdl3::IOStream::FromFile(path, "wb");
    if (!res) return false;
    sdl3::IOStream file = std::move(res).Unwrap();
    return file.Write(extram.data(), extram.size()) == extram.size();
}

void GbcMemory::latchRtc() {
    // Real RTC: seconds/minutes/hours/day-low/day-high(+carry+halt) counted
    // from the moment the cartridge started keeping time. Without persisted
    // "epoch" state across sessions this port derives it from the current
    // wall-clock time — good enough for games that just display a clock or
    // check elapsed time coarsely, not a bit-exact hardware RTC.
    std::time_t t = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    rtcLatched[0] = uint8_t(local.tm_sec);
    rtcLatched[1] = uint8_t(local.tm_min);
    rtcLatched[2] = uint8_t(local.tm_hour);
    int days = local.tm_yday;
    rtcLatched[3] = uint8_t(days & 0xff);
    rtcLatched[4] = uint8_t((days >> 8) & 1);
}

void GbcMemory::mbcWriteLow(uint16_t address, uint8_t value) {
    (void)address;
    switch (mbc) {
        case GbcMbc::None: break;
        case GbcMbc::Mbc1: {
            if (value == 0) value = 1;
            romBank_ = value & 0x1f;
            break;
        }
        case GbcMbc::Mbc2: {
            // MBC2 uses bit 8 of the address to distinguish RAM-enable
            // (ignored here, RAM always enabled) from ROM-bank-select.
            if (address & 0x100) {
                romBank_ = (value == 0) ? 1 : (value & 0x0f);
            }
            break;
        }
        case GbcMbc::Mbc3: {
            if (value == 0) value = 1;
            romBank_ = value & 0x7f;
            break;
        }
        case GbcMbc::Mbc5: {
            if (address < 0x3000) romBank_ = (romBank_ & 0x100) | value;
            else romBank_ = (romBank_ & 0xff) | ((value & 1) << 8);
            break;
        }
    }
    if (numRomBanks > 0) romBank_ &= (numRomBanks - 1) | (mbc == GbcMbc::Mbc5 ? 0x1ff : 0xff);
    if (romBank_ == 0 && mbc != GbcMbc::Mbc5) romBank_ = 1;
}

void GbcMemory::mbcWriteMid(uint16_t address, uint8_t value) {
    (void)address;
    switch (mbc) {
        case GbcMbc::Mbc1:
            if (romRamModeSelect == 0) mbc1UpperBits = value & 3;
            else extRamBank_ = (numExtRamBanks > 1) ? (value & 3) : 0;
            break;
        case GbcMbc::Mbc3:
            rtcSelect = value;
            if (value < 4 && numExtRamBanks > 0) extRamBank_ = value & (numExtRamBanks - 1);
            break;
        case GbcMbc::Mbc5:
            if (numExtRamBanks > 0) extRamBank_ = value & (numExtRamBanks - 1);
            break;
        default: break;
    }
}

void GbcMemory::mbcWriteHigh(uint8_t value) {
    if (mbc == GbcMbc::Mbc1) {
        romRamModeSelect = value & 1;
    } else if (mbc == GbcMbc::Mbc3 && hasRtc_) {
        if (rtcLatchPending == 0x00 && value == 0x01) latchRtc();
        rtcLatchPending = value;
    }
}

uint8_t GbcMemory::read(uint16_t address) {
    if (address < 0x4000) {
        return rom[address];
    } else if (address < 0x8000) {
        int bank = romBank_;
        if (mbc == GbcMbc::Mbc1 && romRamModeSelect == 0) bank |= mbc1UpperBits << 5;
        if (numRomBanks > 0) bank &= numRomBanks - 1;
        return rom[size_t(bank) * gbc::kRomBankSize + (address - 0x4000)];
    } else if (address < 0xa000) {
        return vram[size_t(vramBank_) * gbc::kVramBankSize + (address - 0x8000)];
    } else if (address < 0xc000) {
        if (extram.empty()) return 0xff;
        if (mbc == GbcMbc::Mbc3 && rtcSelect >= 0x08 && rtcSelect <= 0x0c)
            return rtcLatched[rtcSelect - 0x08];
        int bank = (mbc == GbcMbc::Mbc1 && romRamModeSelect == 0) ? 0 : extRamBank_;
        if (numExtRamBanks > 0) bank &= numExtRamBanks - 1; else bank = 0;
        return extram[size_t(bank) * gbc::kExtRamBankSize + (address - 0xa000)];
    } else if (address < 0xd000) {
        return wram[address - 0xc000];
    } else if (address < 0xe000) {
        int bank = wramBank_ ? wramBank_ : 1;
        if (numWramBanks > 0) bank &= numWramBanks - 1;
        return wram[size_t(bank) * gbc::kWramBankSize + (address - 0xd000)];
    } else if (address < 0xfe00) {
        return read(address - 0x2000); // echo of C000-DDFF
    } else if (address < 0xfea0) {
        return oam[address - 0xfe00];
    } else if (address < 0xff00) {
        return 0xff; // unusable
    } else if (address < 0xff80) {
        return readIo(address);
    } else if (address < 0xffff) {
        return hram[address - 0xff80];
    } else {
        return core->readIe();
    }
}

void GbcMemory::write(uint16_t address, uint8_t value) {
    if (address < 0x2000) {
        if (value == 0) extRamDirty_ = true; // RAM-disable often marks "flush now"
    } else if (address < 0x4000) {
        mbcWriteLow(address, value);
    } else if (address < 0x6000) {
        mbcWriteMid(address, value);
    } else if (address < 0x8000) {
        mbcWriteHigh(value);
    } else if (address < 0xa000) {
        vram[size_t(vramBank_) * gbc::kVramBankSize + (address - 0x8000)] = value;
    } else if (address < 0xc000) {
        if (extram.empty()) return;
        if (mbc == GbcMbc::Mbc3 && rtcSelect >= 0x08 && rtcSelect <= 0x0c) {
            rtcLatched[rtcSelect - 0x08] = value;
            return;
        }
        int bank = (mbc == GbcMbc::Mbc1 && romRamModeSelect == 0) ? 0 : extRamBank_;
        if (numExtRamBanks > 0) bank &= numExtRamBanks - 1; else bank = 0;
        extram[size_t(bank) * gbc::kExtRamBankSize + (address - 0xa000)] = value;
        extRamDirty_ = true;
    } else if (address < 0xd000) {
        wram[address - 0xc000] = value;
    } else if (address < 0xe000) {
        int bank = wramBank_ ? wramBank_ : 1;
        if (numWramBanks > 0) bank &= numWramBanks - 1;
        wram[size_t(bank) * gbc::kWramBankSize + (address - 0xd000)] = value;
    } else if (address < 0xfe00) {
        write(address - 0x2000, value); // echo
    } else if (address < 0xfea0) {
        oam[address - 0xfe00] = value;
    } else if (address < 0xff00) {
        // unusable, ignore
    } else if (address < 0xff80) {
        writeIo(address, value);
    } else if (address < 0xffff) {
        hram[address - 0xff80] = value;
    } else {
        core->writeIe(value);
    }
}

uint16_t GbcMemory::read16(uint16_t address) {
    return uint16_t(read(address) | (read(uint16_t(address + 1)) << 8));
}

void GbcMemory::write16(uint16_t address, uint16_t value) {
    write(address, uint8_t(value & 0xff));
    write(uint16_t(address + 1), uint8_t(value >> 8));
}

void GbcMemory::push16(uint16_t &sp, uint16_t value) {
    sp -= 2;
    write16(sp, value);
}

uint16_t GbcMemory::pop16(uint16_t &sp) {
    uint16_t v = read16(sp);
    sp += 2;
    return v;
}

uint8_t GbcMemory::readIo(uint16_t address) {
    return core->readIoPort(address);
}

void GbcMemory::writeIo(uint16_t address, uint8_t value) {
    if (address == 0xff46) { // OAM DMA
        uint16_t src = uint16_t(value) << 8;
        for (int i = 0; i < 0xa0; i++) oam[i] = read(uint16_t(src + i));
        return;
    }
    if (address == 0xff4f && isCgb_) {
        vramBank_ = value & 1;
        return;
    }
    if (address == 0xff70 && isCgb_) {
        int bank = value & 7;
        if (bank == 0) bank = 1;
        if (numWramBanks > 0) bank &= numWramBanks - 1;
        wramBank_ = bank;
        return;
    }
    core->writeIoPort(address, value);
}

void GbcMemory::ioState(StateArchive &archive) {
    archive.io(isCgb_);
    uint8_t mbcVal = uint8_t(mbc);
    archive.io(mbcVal);
    mbc = GbcMbc(mbcVal);
    archive.io(hasExtRam_);
    archive.io(hasBattery_);
    archive.io(hasRtc_);
    archive.io(numRomBanks);
    archive.io(numWramBanks);
    archive.io(numExtRamBanks);
    archive.io(numVramBanks);
    archive.io(romBank_);
    archive.io(wramBank_);
    archive.io(vramBank_);
    archive.io(extRamBank_);
    archive.io(mbc1UpperBits);
    archive.io(romRamModeSelect);
    archive.io(rtcSelect);
    archive.io(rtcLatchPending);
    archive.io(rtcLatched);

    uint32_t romSize = uint32_t(rom.size());
    archive.io(romSize);
    if (!archive.saving) rom.resize(romSize);
    if (romSize) archive.io(rom.data(), romSize);

    uint32_t wramSize = uint32_t(wram.size());
    archive.io(wramSize);
    if (!archive.saving) wram.resize(wramSize);
    if (wramSize) archive.io(wram.data(), wramSize);

    uint32_t vramSize = uint32_t(vram.size());
    archive.io(vramSize);
    if (!archive.saving) vram.resize(vramSize);
    if (vramSize) archive.io(vram.data(), vramSize);

    uint32_t extramSize = uint32_t(extram.size());
    archive.io(extramSize);
    if (!archive.saving) extram.resize(extramSize);
    if (extramSize) archive.io(extram.data(), extramSize);

    archive.io(oam);
    archive.io(hram);
}

} // namespace emulator_demo
