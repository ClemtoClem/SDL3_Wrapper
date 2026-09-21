#include "rom_metadata.hpp"

#include "data/archive.hpp"
#include "rom_source.hpp"

#include "core/string_unicode.hpp"
#include "sdl3/iostream.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>

namespace emulator_demo::app {
namespace {

// ---------------------------------------------------------------------------
// File I/O — sdl3::readFile() already does exactly this (open + read whole
// file into a byte vector), no need to hand-roll it here.
// ---------------------------------------------------------------------------

Option<std::vector<uint8_t>> readWholeFile(const String& path) {
    auto res = sdl3::ReadFile(path);
    if (!res) return NONE;
    return Some(std::move(res).Unwrap());
}

// ---------------------------------------------------------------------------
// Bounds-checked byte helpers
// ---------------------------------------------------------------------------

[[nodiscard]] bool inBounds(const std::vector<uint8_t>& d, size_t off, size_t len) {
    return off <= d.size() && len <= d.size() - off;
}

[[nodiscard]] uint16_t readU16(const std::vector<uint8_t>& d, size_t off) {
    return static_cast<uint16_t>(d[off]) | (static_cast<uint16_t>(d[off + 1]) << 8);
}

[[nodiscard]] uint32_t readU32(const std::vector<uint8_t>& d, size_t off) {
    return static_cast<uint32_t>(d[off])
         | (static_cast<uint32_t>(d[off + 1]) << 8)
         | (static_cast<uint32_t>(d[off + 2]) << 16)
         | (static_cast<uint32_t>(d[off + 3]) << 24);
}

String trimTrailing(const String &s) {
    // String::trimRight() treats every byte <= 0x20 as trimmable (NUL,
    // space, tab, \n, \r, \v, \f) — the exact same set ROM header fields
    // pad with.
    return s.TrimRight();
}

/// Reads a fixed-width ASCII field, stopping at the first NUL byte (or the
/// field's end), then trims trailing whitespace. Returns "" if out of bounds.
String readAsciiField(const std::vector<uint8_t>& d, size_t off, size_t len) {
    if (!inBounds(d, off, len)) return {};
    // Arrêt au premier NUL ou octet non imprimable (bourrage, drapeaux).
    size_t n = 0;
    while (n < len && d[off + n] >= 0x20 && d[off + n] < 0x7F) ++n;
    return trimTrailing(String(reinterpret_cast<const char*>(&d[off]), n));
}

/// Reads a raw 2-character maker/licensee code (no trimming — used as a
/// lookup key). Returns "" if out of bounds.
String readCode2(const std::vector<uint8_t>& d, size_t off) {
    if (!inBounds(d, off, 2)) return {};
    return String(reinterpret_cast<const char*>(&d[off]), 2);
}

/// Reads a UTF-16LE field, stopping at the first NUL code unit, and converts
/// it to UTF-8 via unicode::from_utf16. Returns "" if out of bounds.
String readUtf16Field(const std::vector<uint8_t>& d, size_t off, size_t byteLen) {
    if (!inBounds(d, off, byteLen)) return {};
    std::u16string u16;
    u16.reserve(byteLen / 2);
    for (size_t i = 0; i + 1 < byteLen; i += 2) {
        char16_t c = static_cast<char16_t>(d[off + i] | (static_cast<uint16_t>(d[off + i + 1]) << 8));
        if (c == 0) break;
        u16.push_back(c);
    }
    return trimTrailing(String(unicode::FromUtf16(u16)));
}

// ---------------------------------------------------------------------------
// Publisher lookup tables (display convenience — best-effort, not a
// certified database; unknown codes fall back to "Unknown")
// ---------------------------------------------------------------------------

/// Shared 2-character alphanumeric maker/licensee code scheme used by NDS
/// header maker codes, GBA header maker codes, and GBC "new" licensee codes
/// (when the old licensee byte is 0x33).
const std::unordered_map<String, String, String::Hash>& makerCodeTable() {
    static const std::unordered_map<String, String, String::Hash> table = {
        {"01", "Nintendo"}, {"08", "Capcom"}, {"0A", "Jaleco"},
        {"13", "Electronic Arts"}, {"18", "Hudson Soft"}, {"1A", "Yanoman"},
        {"20", "Destination Software/KSS"}, {"22", "VAP Inc"}, {"28", "Kemco"},
        {"29", "Seta"}, {"30", "Viacom"}, {"31", "Nintendo"}, {"32", "Bandai"},
        {"33", "Ocean/Acclaim"}, {"34", "Konami"}, {"35", "Hector"}, {"37", "Taito"},
        {"38", "Hudson"}, {"39", "Banpresto"}, {"41", "Ubisoft"}, {"42", "Atlus"},
        {"44", "Malibu"}, {"46", "Angel"}, {"47", "Spectrum Holobyte"}, {"49", "Irem"},
        {"4F", "Eidos/U.S. Gold"}, {"50", "Absolute"}, {"51", "Acclaim"},
        {"52", "Activision"}, {"53", "American Sammy"}, {"54", "Konami"},
        {"55", "Hi Tech Entertainment"}, {"56", "LJN"}, {"57", "Matchbox"},
        {"58", "Mattel"}, {"59", "Milton Bradley"}, {"5A", "Mindscape"},
        {"5C", "Taxan"}, {"5D", "Midway"}, {"5G", "Majesco"}, {"5H", "3DO"},
        {"5K", "Hasbro"}, {"60", "Titus"}, {"61", "Virgin"}, {"64", "LucasArts"},
        {"67", "Ocean"}, {"69", "Electronic Arts"}, {"6E", "Elite Systems"},
        {"6F", "Electro Brain"}, {"70", "Infogrames"}, {"71", "Interplay"},
        {"72", "Broderbund"}, {"73", "Sculptured Software"}, {"75", "SCi"},
        {"78", "THQ"}, {"79", "Accolade"}, {"7C", "Microprose"}, {"7F", "Kemco"},
        {"80", "Misawa"}, {"83", "LOZC"}, {"86", "Tokuma Shoten"},
        {"87", "Tsukuda Original"}, {"8B", "Bullet-Proof Software"}, {"8C", "Vic Tokai"},
        {"8J", "General Entertainment"}, {"8N", "Success"}, {"8P", "Sega"},
        {"91", "Chunsoft"}, {"92", "Video System"}, {"93", "Ocean/Acclaim"},
        {"95", "Varie"}, {"96", "Yonezawa/S'Pal"}, {"97", "Kaneko"},
        {"99", "Pack-In-Video"}, {"9F", "Nova"}, {"9H", "Bottom Up"},
        {"A4", "Konami"}, {"AF", "Namco"}, {"B0", "Acclaim"}, {"B1", "ASCII/Nexsoft"},
        {"B2", "Bandai"}, {"B4", "Enix"}, {"B6", "HAL Laboratory"}, {"B7", "SNK"},
        {"B9", "Pony Canyon"}, {"BA", "Culture Brain"}, {"BB", "Sunsoft"},
        {"BD", "Sony Imagesoft"}, {"BF", "Sammy"}, {"BJ", "Compile"}, {"C0", "Taito"},
        {"C2", "Kemco"}, {"C3", "Square Soft"}, {"C4", "Tokuma Shoten"},
        {"C5", "Data East"}, {"C6", "Tonkin House"}, {"C8", "Koei"}, {"C9", "UFL"},
        {"CA", "Ultra"}, {"CB", "Vap"}, {"CC", "Use Corporation"}, {"CD", "Meldac"},
        {"CE", "Pony Canyon"}, {"CF", "Angel"}, {"D0", "Taito"}, {"D1", "Sofel"},
        {"D2", "Quest"}, {"D3", "Sigma Enterprises"}, {"D4", "Ask Kodansha"},
        {"D6", "Naxat Soft"}, {"D7", "Copya System"}, {"D9", "Banpresto"},
        {"DA", "Tomy"}, {"DB", "LJN"}, {"DD", "NCS"}, {"DE", "Human"},
        {"DF", "Altron"}, {"E0", "Jaleco"}, {"E1", "Towa Chiki"}, {"E2", "Yutaka"},
        {"E3", "Varie"}, {"E5", "Epoch"}, {"E7", "Athena"}, {"E8", "Asmik"},
        {"E9", "Natsume"}, {"EA", "King Records"}, {"EB", "Atlus"},
        {"EC", "Epic/Sony Records"}, {"EE", "IGS"}, {"F0", "A Wave"},
        {"F3", "Extreme Entertainment"}, {"FF", "LJN"},
    };
    return table;
}

/// Old-style single-byte GBC licensee codes (used when header byte 0x14B
/// is not 0x33 — see readCode2/0x144 handling in readGbcMetadata).
const std::unordered_map<uint8_t, String>& oldLicenseeTable() {
    static const std::unordered_map<uint8_t, String> table = {
        {0x01, "Nintendo"}, {0x08, "Capcom"}, {0x13, "Electronic Arts"},
        {0x18, "Hudson Soft"}, {0x19, "b-ai"}, {0x20, "KSS"}, {0x22, "Pony Canyon"},
        {0x24, "PCM Complete"}, {0x25, "san-x"}, {0x28, "Kemco Japan"}, {0x29, "seta"},
        {0x30, "Viacom"}, {0x31, "Nintendo"}, {0x34, "Konami"}, {0x35, "Hector"},
        {0x38, "Capcom"}, {0x39, "Banpresto"}, {0x41, "Ubisoft"}, {0x42, "Atlus"},
        {0x44, "Malibu"}, {0x46, "angel"}, {0x47, "Bullet-Proof"}, {0x49, "irem"},
        {0x50, "Absolute"}, {0x51, "Acclaim"}, {0x52, "Activision"},
        {0x53, "American sammy"}, {0x54, "Konami"}, {0x55, "Hi tech entertainment"},
        {0x56, "LJN"}, {0x57, "Matchbox"}, {0x58, "Mattel"}, {0x59, "Milton Bradley"},
        {0x60, "Titus"}, {0x61, "Virgin"}, {0x64, "LucasArts"}, {0x67, "Ocean"},
        {0x69, "Electronic Arts"}, {0x70, "Infogrames"}, {0x71, "Interplay"},
        {0x72, "Broderbund"}, {0x73, "sculptured"}, {0x75, "sci"}, {0x78, "THQ"},
        {0x79, "Accolade"}, {0x80, "misawa"}, {0x83, "lozc"}, {0x86, "Tokuma Shoten i"},
        {0x87, "Tsukuda Original"}, {0x91, "Chunsoft"}, {0x92, "Video system"},
        {0x93, "Ocean/Acclaim"}, {0x95, "Varie"}, {0x96, "Yonezawa/s'pal"},
        {0x97, "Kaneko"}, {0x99, "Pack in soft"}, {0xA4, "Konami"},
    };
    return table;
}

String lookupMakerPublisher(const String& code) {
    const auto& table = makerCodeTable();
    auto it = table.find(code);
    return it != table.end() ? it->second : String("Unknown");
}

String lookupOldLicenseePublisher(uint8_t code) {
    const auto& table = oldLicenseeTable();
    auto it = table.find(code);
    return it != table.end() ? it->second : String("Unknown");
}

// ---------------------------------------------------------------------------
// Format detection
// ---------------------------------------------------------------------------

enum class RomFormat { Nds, Gba, Gbc };

// The 156-byte Nintendo logo bitmap that every GBA cart embeds at 0x04-0x9F
// (checked by the GBA BIOS on real hardware). Used, together with the 0xB2
// sentinel byte, to positively identify GBA headers. A single sentinel byte
// alone is not reliable: NDS ROMs are free to put arbitrary homebrew-tool
// data in their own reserved header bytes, and 0xB2 can coincidentally read
// as 0x96 there (observed on real NDS ROMs in this repo's roms/ test set) —
// the full fixed 156-byte logo essentially never collides by chance.
constexpr uint8_t kGbaLogo[156] = {
    0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21, 0x3D, 0x84, 0x82, 0x0A,
    0x84, 0xE4, 0x09, 0xAD, 0x11, 0x24, 0x8B, 0x98, 0xC0, 0x81, 0x7F, 0x21,
    0xA3, 0x52, 0xBE, 0x19, 0x93, 0x09, 0xCE, 0x20, 0x10, 0x46, 0x4A, 0x4A,
    0xF8, 0x27, 0x31, 0xEC, 0x58, 0xC7, 0xE8, 0x33, 0x82, 0xE3, 0xCE, 0xBF,
    0x85, 0xF4, 0xDF, 0x94, 0xCE, 0x4B, 0x09, 0xC1, 0x94, 0x56, 0x8A, 0xC0,
    0x13, 0x72, 0xA7, 0xFC, 0x9F, 0x84, 0x4D, 0x73, 0xA3, 0xCA, 0x9A, 0x61,
    0x58, 0x97, 0xA3, 0x27, 0xFC, 0x03, 0x98, 0x76, 0x23, 0x1D, 0xC7, 0x61,
    0x03, 0x04, 0xAE, 0x56, 0xBF, 0x38, 0x84, 0x00, 0x40, 0xA7, 0x0E, 0xFD,
    0xFF, 0x52, 0xFE, 0x03, 0x6F, 0x95, 0x30, 0xF1, 0x97, 0xFB, 0xC0, 0x85,
    0x60, 0xD6, 0x80, 0x25, 0xA9, 0x63, 0xBE, 0x03, 0x01, 0x4E, 0x38, 0xE2,
    0xF9, 0xA2, 0x34, 0xFF, 0xBB, 0x3E, 0x03, 0x44, 0x78, 0x00, 0x90, 0xCB,
    0x88, 0x11, 0x3A, 0x94, 0x65, 0xC0, 0x7C, 0x63, 0x87, 0xF0, 0x3C, 0xAF,
    0xD6, 0x25, 0xE4, 0x8B, 0x38, 0x0A, 0xAC, 0x72, 0x21, 0xD4, 0xF8, 0x07,
};

Option<RomFormat> detectFormat(const std::vector<uint8_t>& d) {
    // GBC/GB: fixed Nintendo logo begins at 0x104 (checking its first 4
    // bytes is sufficient in practice to identify the format).
    if (inBounds(d, 0x104, 4) &&
        d[0x104] == 0xCE && d[0x105] == 0xED && d[0x106] == 0x66 && d[0x107] == 0x66) {
        return Some(RomFormat::Gbc);
    }

    // GBA: fixed sentinel byte 0x96 at 0xB2 plus the full 156-byte Nintendo
    // logo at 0x04, requires at least a full header.
    if (inBounds(d, 0, 0xC0) && d[0xB2] == 0x96 &&
        std::equal(std::begin(kGbaLogo), std::end(kGbaLogo), d.begin() + 0x04)) {
        return Some(RomFormat::Gba);
    }

    // NDS: has no single fixed magic value. Validate structurally via the
    // ARM9 ROM offset / entry address fields (0x20 / 0x24), which must
    // plausibly point inside the file for a genuine NDS header.
    if (inBounds(d, 0, 0x180)) {
        uint32_t arm9RomOffset = readU32(d, 0x20);
        uint32_t arm9EntryAddress = readU32(d, 0x24);
        if (arm9RomOffset > 0 && arm9RomOffset < d.size() && arm9EntryAddress != 0) {
            return Some(RomFormat::Nds);
        }
    }

    return NONE;
}

// ---------------------------------------------------------------------------
// NDS icon decode
// ---------------------------------------------------------------------------

/// Packs 8-bit R/G/B/A channels into a single little-endian-ordered word:
/// byte 0 = R, byte 1 = G, byte 2 = B, byte 3 = A (i.e. reading the value's
/// bytes in memory order gives R,G,B,A — the conventional RGBA8888 layout).
[[nodiscard]] uint32_t packRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(r)
         | (static_cast<uint32_t>(g) << 8)
         | (static_cast<uint32_t>(b) << 16)
         | (static_cast<uint32_t>(a) << 24);
}

/// Decodes the 32x32 4bpp icon bitmap + 16-color BGR555 palette at
/// `bannerOffset` (caller must have already bounds-checked both regions).
RomIcon decodeNdsIcon(const std::vector<uint8_t>& d, size_t bannerOffset) {
    RomIcon icon;
    icon.rgba.assign(static_cast<size_t>(RomIcon::SIZE) * RomIcon::SIZE, 0);

    uint32_t colors[16];
    size_t palOff = bannerOffset + 0x220;
    for (int i = 0; i < 16; ++i) {
        uint16_t v = readU16(d, palOff + static_cast<size_t>(i) * 2);
        uint8_t r5 = v & 0x1F;
        uint8_t g5 = (v >> 5) & 0x1F;
        uint8_t b5 = (v >> 10) & 0x1F;
        uint8_t r8 = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
        uint8_t g8 = static_cast<uint8_t>((g5 << 3) | (g5 >> 2));
        uint8_t b8 = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
        uint8_t a8 = (i == 0) ? 0 : 255; // palette index 0 is always transparent
        colors[i] = packRgba(r8, g8, b8, a8);
    }

    size_t bmpOff = bannerOffset + 0x20;
    for (int tileIndex = 0; tileIndex < 16; ++tileIndex) {
        int tx = tileIndex % 4;
        int ty = tileIndex / 4;
        size_t tileOff = bmpOff + static_cast<size_t>(tileIndex) * 32;
        for (int row = 0; row < 8; ++row) {
            for (int col4 = 0; col4 < 4; ++col4) {
                uint8_t byte = d[tileOff + static_cast<size_t>(row) * 4 + col4];
                uint8_t leftIdx = byte & 0x0F;         // low nibble = left pixel
                uint8_t rightIdx = (byte >> 4) & 0x0F; // high nibble = right pixel
                int px0 = tx * 8 + col4 * 2;
                int px1 = px0 + 1;
                int py = ty * 8 + row;
                icon.rgba[static_cast<size_t>(py) * RomIcon::SIZE + px0] = colors[leftIdx];
                icon.rgba[static_cast<size_t>(py) * RomIcon::SIZE + px1] = colors[rightIdx];
            }
        }
    }
    return icon;
}

// ---------------------------------------------------------------------------
// Champs d'en-tête complémentaires
// ---------------------------------------------------------------------------

/// Région d'après la 4e lettre du code produit (convention Nintendo commune
/// NDS/GBA).
String regionFromGameCode(const String& gameCode) {
    // Homebrew : code de remplissage (« #### », « ____ ») sans signification.
    if (gameCode.GetSize() < 4 || !gameCode.IsAlnum()) return {};
    switch (gameCode.CharAt(3)) {
        case 'J': return "Japon";
        case 'E': return "Amérique du Nord";
        case 'P': return "Europe";
        case 'D': return "Allemagne";
        case 'F': return "France";
        case 'I': return "Italie";
        case 'S': return "Espagne";
        case 'H': return "Pays-Bas";
        case 'K': return "Corée";
        case 'C': return "Chine";
        case 'U': return "Australie";
        case 'O': return "International";
        case 'X': case 'Y': case 'Z': return "Europe (variante)";
        default: return String::Format("code %c", gameCode.CharAt(3));
    }
}

/// CRC-16/MODBUS (poly 0xA001, init 0xFFFF) — celui de l'en-tête NDS.
uint16_t crc16Modbus(const std::vector<uint8_t>& d, size_t off, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= d[off + i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? uint16_t((crc >> 1) ^ 0xA001) : uint16_t(crc >> 1);
    }
    return crc;
}

String formatCapacity(uint64_t bytes) {
    if (bytes >= 1024 * 1024) return String::Format("%llu Mio", static_cast<unsigned long long>(bytes / (1024 * 1024)));
    return String::Format("%llu Kio", static_cast<unsigned long long>(bytes / 1024));
}

String gbCartridgeType(uint8_t code) {
    switch (code) {
        case 0x00: return "ROM seule";
        case 0x01: return "MBC1";
        case 0x02: return "MBC1 + RAM";
        case 0x03: return "MBC1 + RAM + pile";
        case 0x05: return "MBC2";
        case 0x06: return "MBC2 + pile";
        case 0x08: return "ROM + RAM";
        case 0x09: return "ROM + RAM + pile";
        case 0x0F: return "MBC3 + horloge + pile";
        case 0x10: return "MBC3 + horloge + RAM + pile";
        case 0x11: return "MBC3";
        case 0x12: return "MBC3 + RAM";
        case 0x13: return "MBC3 + RAM + pile";
        case 0x19: return "MBC5";
        case 0x1A: return "MBC5 + RAM";
        case 0x1B: return "MBC5 + RAM + pile";
        case 0x1C: return "MBC5 + vibreur";
        case 0x1D: return "MBC5 + vibreur + RAM";
        case 0x1E: return "MBC5 + vibreur + RAM + pile";
        case 0x20: return "MBC6";
        case 0x22: return "MBC7 + capteur + vibreur + RAM + pile";
        default: return String::Format("type 0x%02X", code);
    }
}

/// Type de sauvegarde d'une ROM GBA : les bibliothèques Nintendo laissent une
/// chaîne d'identification alignée sur 4 octets (« EEPROM_V », « SRAM_V »,
/// « FLASH1M_V »…) — c'est aussi ce que cherche le cœur pour choisir la puce.
String gbaSaveType(const std::vector<uint8_t>& d) {
    static constexpr const char* KINDS[][2] = {{"EEPROM_V", "EEPROM"}, {"SRAM_V", "SRAM"}, {"SRAM_F_V", "SRAM"},
                                               {"FLASH1M_V", "Flash 1 Mbit"}, {"FLASH512_V", "Flash 512 Kbit"},
                                               {"FLASH_V", "Flash 512 Kbit"}};
    for (size_t at = 0xC0; at + 12 <= d.size(); at += 4) {
        if (d[at] != 'E' && d[at] != 'S' && d[at] != 'F') continue;
        for (const auto& kind : KINDS) {
            size_t length = std::char_traits<char>::length(kind[0]);
            if (at + length <= d.size() && std::memcmp(&d[at], kind[0], length) == 0)
                return String::Format("sauvegarde %s", kind[1]);
        }
    }
    return "sans sauvegarde détectée";
}

// ---------------------------------------------------------------------------
// Per-format metadata extraction
// ---------------------------------------------------------------------------

RomMetadata readNdsMetadata(const std::vector<uint8_t>& d) {
    RomMetadata meta;

    String headerTitle = readAsciiField(d, 0x00, 12);
    meta.publisher = lookupMakerPublisher(readCode2(d, 0x10));
    meta.title = headerTitle;
    meta.gameCode = readAsciiField(d, 0x0C, 4);
    if (!meta.gameCode.IsAlnum()) meta.gameCode.Clear();
    meta.region = regionFromGameCode(meta.gameCode);
    meta.version = d[0x1E];
    switch (d[0x12]) {
        case 0x00: meta.hardware = "Nintendo DS"; break;
        case 0x02: meta.hardware = "Nintendo DS + DSi"; break;
        case 0x03: meta.hardware = "DSi uniquement"; break;
        default: meta.hardware = String::Format("unité 0x%02X", d[0x12]); break;
    }
    if (d[0x14] < 16) meta.romCapacity = formatCapacity(uint64_t(1) << (17 + d[0x14]));
    if (inBounds(d, 0x15E, 2)) meta.headerChecksumOk = Some(crc16Modbus(d, 0, 0x15E) == readU16(d, 0x15E));

    if (!inBounds(d, 0x68, 4)) return meta;
    uint32_t bannerOffset = readU32(d, 0x68);
    if (bannerOffset == 0 || !inBounds(d, bannerOffset, 2)) return meta;

    uint16_t version = readU16(d, bannerOffset);
    if (version < 1) return meta;

    // Title slots: the banner stores six 256-byte (128 UTF-16LE code unit)
    // language slots spaced 0x100 apart starting at +0x240 (Japanese), with
    // English at +0x340. (Verified empirically against real NDS ROMs in
    // this repo's roms/ test set — a naively-assumed 128-byte/0x280 layout
    // for the English slot decodes to garbage/empty on every sample ROM.)
    String englishTitle = readUtf16Field(d, bannerOffset + 0x340, 256);
    String japaneseTitle = readUtf16Field(d, bannerOffset + 0x240, 256);
    if (!englishTitle.IsEmpty()) {
        meta.title = englishTitle;
    } else if (!japaneseTitle.IsEmpty()) {
        meta.title = japaneseTitle;
    } else if (!headerTitle.IsEmpty()) {
        meta.title = headerTitle;
    }

    if (inBounds(d, bannerOffset + 0x20, 512) && inBounds(d, bannerOffset + 0x220, 32)) {
        meta.icon = Some(decodeNdsIcon(d, bannerOffset));
    }

    return meta;
}

RomMetadata readGbaMetadata(const std::vector<uint8_t>& d) {
    RomMetadata meta;
    meta.title = readAsciiField(d, 0xA0, 12);
    meta.publisher = lookupMakerPublisher(readCode2(d, 0xB0));
    meta.gameCode = readAsciiField(d, 0xAC, 4);
    if (!meta.gameCode.IsAlnum()) meta.gameCode.Clear();
    meta.region = regionFromGameCode(meta.gameCode);
    meta.version = d[0xBC];
    meta.hardware = "Game Boy Advance";
    // Complément d'en-tête vérifié par le BIOS : -(somme 0xA0..0xBC) - 0x19.
    uint8_t sum = 0;
    for (size_t i = 0xA0; i <= 0xBC; ++i) sum = uint8_t(sum - d[i]);
    meta.headerChecksumOk = Some(uint8_t(sum - 0x19) == d[0xBD]);
    meta.cartridge = gbaSaveType(d);
    // GBA carts carry no embedded icon; meta.icon stays std::nullopt.
    return meta;
}

RomMetadata readGbcMetadata(const std::vector<uint8_t>& d) {
    RomMetadata meta;
    uint8_t oldCode = inBounds(d, 0x14B, 1) ? d[0x14B] : 0;

    // Longueur du titre (Pan Docs, « 0134-0143 ») : 16 octets sur une
    // cartouche DMG, mais sur une cartouche CGB l'octet 0x143 est le drapeau
    // CGB et, sur les cartouches récentes (code éditeur « nouveau », 0x33),
    // 0x13F-0x142 portent le code fabricant. Lire 16 octets donnait des
    // titres du genre « 10-PIN BOWLAXPP\x80 ».
    const bool cgbFlag = inBounds(d, 0x143, 1) && (d[0x143] & 0x80) != 0;
    size_t titleLength = !cgbFlag ? 16 : (oldCode == 0x33 ? 11 : 15);
    meta.title = readAsciiField(d, 0x134, titleLength);

    if (oldCode == 0x33) {
        meta.publisher = lookupMakerPublisher(readCode2(d, 0x144));
    } else {
        meta.publisher = lookupOldLicenseePublisher(oldCode);
    }
    if (inBounds(d, 0x14E, 1)) {
        uint8_t cgb = d[0x143];
        meta.hardware = (cgb == 0xC0) ? "Game Boy Color uniquement"
                        : (cgb & 0x80) ? "Game Boy Color (compatible Game Boy)"
                                       : "Game Boy";
        if (d[0x146] == 0x03) meta.hardware += " + Super Game Boy";
        meta.cartridge = gbCartridgeType(d[0x147]);
        if (d[0x148] <= 8) meta.romCapacity = formatCapacity(uint64_t(32 * 1024) << d[0x148]);
        meta.region = d[0x14A] == 0 ? "Japon" : "International";
        meta.version = d[0x14C];
        // Somme d'en-tête vérifiée par la console : x = x - octet - 1.
        uint8_t check = 0;
        for (size_t i = 0x134; i <= 0x14C; ++i) check = uint8_t(check - d[i] - 1);
        meta.headerChecksumOk = Some(check == d[0x14D]);
    }
    // GB/GBC carts carry no embedded icon; meta.icon stays std::nullopt.
    return meta;
}

} // namespace

Option<RomMetadata> ReadRomMetadataFromBytes(const std::vector<uint8_t>& data) {
    Option<RomFormat> fmt = detectFormat(data);
    if (fmt.IsNone()) return NONE;

    RomMetadata meta;
    switch (fmt.Unwrap()) {
        case RomFormat::Nds: meta = readNdsMetadata(data); meta.system = RomSystem::NDS; break;
        case RomFormat::Gba: meta = readGbaMetadata(data); meta.system = RomSystem::GBA; break;
        case RomFormat::Gbc: meta = readGbcMetadata(data); meta.system = RomSystem::GBC; break;
    }
    meta.sizeBytes = int64_t(data.size());
    meta.crc32 = data::archive::Crc32(data);
    return Some(std::move(meta));
}

Option<RomMetadata> ReadRomMetadata(const String& path) {
    if (IsArchivePath(path)) {
        auto rom = LoadRom(path);
        if (rom.IsError()) return NONE;
        return ReadRomMetadataFromBytes(rom.Value().bytes);
    }
    Option<std::vector<uint8_t>> dataOpt = readWholeFile(path);
    if (dataOpt.IsNone()) return NONE;
    return ReadRomMetadataFromBytes(dataOpt.Value());
}

std::vector<std::pair<String, String>> DescribeRom(const RomMetadata& meta) {
    std::vector<std::pair<String, String>> lines;
    auto add = [&lines](const char* key, const String& value) {
        if (!value.IsEmpty()) lines.emplace_back(String(key), value);
    };
    add("Titre", meta.title.Replace('\n', ' '));
    add("Console", String(RomSystemName(meta.system)));
    add("Matériel", meta.hardware);
    add("Éditeur", meta.publisher);
    add("Code produit", meta.gameCode);
    add("Région", meta.region);
    add("Version", String::Format("%d", meta.version));
    add("Cartouche", meta.cartridge);
    add("Capacité", meta.romCapacity);
    add("Taille", String::Format("%.2f Mio (%lld octets)", double(meta.sizeBytes) / (1024.0 * 1024.0),
                                 static_cast<long long>(meta.sizeBytes)));
    add("CRC-32", String::Format("%08X", meta.crc32));
    if (meta.headerChecksumOk.IsSome())
        add("En-tête", String(meta.headerChecksumOk.Unwrap() ? "somme de contrôle valide" : "somme de contrôle INVALIDE"));
    add("Icône", String(meta.icon.IsSome() ? "oui (32x32)" : "non"));
    return lines;
}

} // namespace emulator_demo::app
