
#include <cstring>
#include "emulator/core.hpp"
#include "emulator/defines.hpp"
#include "emulator/hle/dldi.hpp"
#include "emulator/settings.hpp"

namespace emulator_demo {

void Dldi::patchRom(uint8_t *rom, uint32_t offset, uint32_t size) {
    // Scan the ROM for DLDI drivers and patch them if found
    for (uint32_t i = 0; i < size; i += 4) {
        // Check for the DLDI magic number
        if (uint32_t(U8TO32(rom, i)) != 0xBF8DA5ED)
            continue;

        // Check for the DLDI magic string
        const char *str = " Chishm\0";
        bool        match = true;
        for (size_t j = 0; j < 8; j++) {
            if (rom[i + 4 + j] != uint8_t(str[j])) {
                match = false;
                break;
            }
        }
        if (!match)
            continue;

        // Ensure there's room to patch the DLDI driver
        if (rom[i + 0x0F] < 0x08) { // Space in ROM
            LOG("Not enough space to patch DLDI driver at ROM offset 0x%X\n", offset + i);
            break;
        }

        // Patch the DLDI driver to use the HLE functions
        rom[i + 0x0C] = 0x01;                              // DLDI driver version
        rom[i + 0x0D] = 0x08;                              // Size of driver in terms of 1 << n (256 bytes)
        rom[i + 0x0E] = 0x00;                              // Sections to adjust
        strcpy((char *)&rom[i + 0x10], "NooDS DLDI");       // Long driver name
        uint32_t address = U8TO32(rom, i + 0x40);           // Address of driver
        U32TO8(rom, i + 0x44, address + 0x98);              // End of driver code
        U32TO8(rom, i + 0x58, address + 0x98);              // Start of BSS area
        U32TO8(rom, i + 0x5C, address + 0x98);              // End of BSS area
        memcpy(&rom[i + 0x60], "NOOD", 4);                  // Short driver name
        U32TO8(rom, i + 0x64, 0x00000023);                  // Feature flags (read, write, NDS slot)
        U32TO8(rom, i + 0x68, address + 0x80);              // Address of startup()
        U32TO8(rom, i + 0x6C, address + 0x84);              // Address of isInserted()
        U32TO8(rom, i + 0x70, address + 0x88);              // Address of readSectors(sector, numSectors, buf)
        U32TO8(rom, i + 0x74, address + 0x8C);              // Address of writeSectors(sector, numSectors, buf)
        U32TO8(rom, i + 0x78, address + 0x90);              // Address of clearStatus()
        U32TO8(rom, i + 0x7C, address + 0x94);              // Address of shutdown()
        U32TO8(rom, i + 0x80, uint32_t(DLDI_START));         // startup()
        U32TO8(rom, i + 0x84, uint32_t(DLDI_INSERT));        // isInserted()
        U32TO8(rom, i + 0x88, uint32_t(DLDI_READ));          // readSectors(sector, numSectors, buf)
        U32TO8(rom, i + 0x8C, uint32_t(DLDI_WRITE));         // writeSectors(sector, numSectors, buf)
        U32TO8(rom, i + 0x90, uint32_t(DLDI_CLEAR));         // clearStatus()
        U32TO8(rom, i + 0x94, uint32_t(DLDI_STOP));          // shutdown()

        LOG("Patched DLDI driver at ROM offset 0x%X\n", offset + i);
        patched = true;
    }
}

int Dldi::startup() {
    auto res = sdl3::IOStream::FromFile(Settings::getSdImagePath(), "rb+");
    if (!res)
        return 0;
    sdImage = std::move(res).Unwrap();
    return 1;
}

int Dldi::isInserted() {
    return sdImage ? 1 : 0;
}

int Dldi::readSectors(bool cpu, uint32_t sector, uint32_t numSectors, uint32_t buf) {
    if (!sdImage)
        return 0;
    const uint64_t offset = uint64_t(sector) << 9;
    const uint64_t size   = uint64_t(numSectors) << 9;

    uint8_t *data = new uint8_t[size];
    sdImage.Seek(Sint64(offset), SDL_IO_SEEK_SET);
    (void)sdImage.Read(data, size);

    for (uint64_t i = 0; i < size; i++)
        core->memory.write<uint8_t>(cpu, buf + uint32_t(i), data[i]);
    delete[] data;
    return 1;
}

int Dldi::writeSectors(bool cpu, uint32_t sector, uint32_t numSectors, uint32_t buf) {
    if (!sdImage)
        return 0;
    const uint64_t offset = uint64_t(sector) << 9;
    const uint64_t size   = uint64_t(numSectors) << 9;

    uint8_t *data = new uint8_t[size];
    for (uint64_t i = 0; i < size; i++)
        data[i] = core->memory.read<uint8_t>(cpu, buf + uint32_t(i));

    sdImage.Seek(Sint64(offset), SDL_IO_SEEK_SET);
    sdImage.Write(data, size);
    delete[] data;
    return 1;
}

int Dldi::clearStatus() {
    return sdImage ? 1 : 0;
}

int Dldi::shutdown() {
    if (!sdImage)
        return 0;
    sdImage.Reset();
    return 1;
}

} // namespace emulator_demo
