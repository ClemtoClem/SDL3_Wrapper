#include <emulator/core/io/rtc.hpp>

#include <emulator/core/core.hpp>

#include <ctime>

namespace emulator {

void Rtc::updateRtc(bool cs, bool sck, bool sio) {
    if (cs) {
        if (!sckCur && sck) {
            if (writeCount < 8) {
                command |= sio << (7 - writeCount);
                if (writeCount == 7 && (command & 0xF0) != 0x60) {
                    uint8_t value = 0;
                    for (int i = 0; i < 8; i++)
                        value |= ((command >> i) & BIT(0)) << (7 - i);
                    command = value;
                }
            } else if (command & BIT(0)) {
                sio = readRegister((command >> 1) & 0x7);
            } else {
                writeRegister((command >> 1) & 0x7, sio);
            }
            writeCount++;
        }
    } else {
        writeCount = 0;
        command    = 0;
    }
    csCur  = cs;
    sckCur = sck;
    sioCur = sio;
}

void Rtc::updateDateTime() {
    std::time_t t    = std::time(nullptr) + std::time_t(timeOffset);
    std::tm    *time = std::localtime(&t);
    time->tm_year %= 100;
    time->tm_mon++;
    if (!(control & BIT(core->isGbaMode() ? 6 : 1)))
        time->tm_hour %= 12;
    dateTime[0] = static_cast<uint8_t>(((time->tm_year / 10) << 4) | (time->tm_year % 10));
    dateTime[1] = static_cast<uint8_t>(((time->tm_mon / 10) << 4) | (time->tm_mon % 10));
    dateTime[2] = static_cast<uint8_t>(((time->tm_mday / 10) << 4) | (time->tm_mday % 10));
    dateTime[3] = uint8_t(time->tm_wday);
    dateTime[4] = static_cast<uint8_t>(((time->tm_hour / 10) << 4) | (time->tm_hour % 10));
    dateTime[5] = static_cast<uint8_t>(((time->tm_min / 10) << 4) | (time->tm_min % 10));
    dateTime[6] = static_cast<uint8_t>(((time->tm_sec / 10) << 4) | (time->tm_sec % 10));
    if (time->tm_hour >= 12)
        dateTime[4] = static_cast<uint8_t>(dateTime[4] | BIT(6 << core->isGbaMode()));
}

void Rtc::reset() {
    updateRtc(0, 0, 0);
    control     = 0;
    rtc         = 0;
    gpDirection = 0;
    gpControl   = 0;
    resetRegisters();
}

void Rtc::resetRegisters() {
    status2      = 0;
    clockAdjust  = 0;
    freeRegister = 0;
    for (int i = 0; i < 3; i++)
        int1[i] = int2[i] = 0;
}

namespace {

[[nodiscard]] int FromBcd(uint8_t value) { return (value >> 4) * 10 + (value & 0xF); }

} // namespace

void Rtc::applyPendingDateTime(int first) {
    // Date courante (pour une écriture de l'heure seule), puis octets écrits.
    std::time_t now  = std::time(nullptr);
    std::time_t cur  = now + std::time_t(timeOffset);
    std::tm     tm   = *std::localtime(&cur);
    if (first == 0) {
        tm.tm_year = 100 + FromBcd(pending[0]);   // 2000-2099
        tm.tm_mon  = FromBcd(pending[1] & 0x1F) - 1;
        tm.tm_mday = FromBcd(pending[2] & 0x3F);
    }
    const bool gba  = core->isGbaMode();
    const bool pm   = pending[4] & BIT(gba ? 7 : 6);
    int        hour = FromBcd(pending[4] & 0x3F);
    if (!(control & BIT(gba ? 6 : 1)) && pm && hour < 12) // mode 12 h
        hour += 12;
    tm.tm_hour  = hour;
    tm.tm_min   = FromBcd(pending[5] & 0x7F);
    tm.tm_sec   = FromBcd(pending[6] & 0x7F);
    tm.tm_isdst = -1;
    const std::time_t target = std::mktime(&tm);
    if (target != std::time_t(-1))
        timeOffset = int64_t(target) - int64_t(now);
}

namespace {

// dateTime = { année, mois, jour, jour de semaine, heure, minute, seconde }.
// Les 8 premiers bits écrits sont la commande : writeCount vaut donc 8..63
// pendant la lecture du registre date+heure (octets 0..6), et 8..31 pendant
// celle du registre heure seule (octets 4..6). L'index de l'heure seule
// était calculé `writeCount / 8 - 5`, soit -4..-2 : lecture HORS du tableau
// (relevée par UBSan) — les jeux qui lisent l'horloge recevaient des octets
// voisins au lieu de l'heure. Bornés dans les deux cas par sécurité.
[[nodiscard]] size_t DateTimeByteIndex(uint8_t writeCount) {
    size_t index = writeCount >= 8 ? size_t(writeCount / 8) - 1 : 0;
    return index < 7 ? index : 6;
}

[[nodiscard]] size_t TimeByteIndex(uint8_t writeCount) {
    size_t index = (writeCount >= 8 ? size_t(writeCount / 8) - 1 : 0) + 4;
    return index < 7 ? index : 6;
}

} // namespace

bool Rtc::readRegister(uint8_t index) {
    if (core->isGbaMode()) {
        switch (index) {
            case 0:
                reset();
                return 0;
            case 1:
                return (control >> (writeCount & 7)) & BIT(0);
            case 2:
                if (writeCount == 8)
                    updateDateTime();
                return (dateTime[DateTimeByteIndex(writeCount)] >> (writeCount % 8)) & BIT(0);
            case 3:
                if (writeCount == 8)
                    updateDateTime();
                return (dateTime[TimeByteIndex(writeCount)] >> (writeCount % 8)) & BIT(0);
            default:
                LOG_VERBOSE("Read from unknown GBA RTC register: %d\n", index);
                return 0;
        }
    }
    const uint8_t bit  = writeCount & 7;
    const size_t  byte = writeCount >= 8 ? size_t(writeCount / 8) - 1 : 0;
    switch (index) {
        case 0:
            return (control >> (writeCount & 7)) & BIT(0);
        case 1:
            return (status2 >> bit) & BIT(0);
        case 4:
            return (int1[byte < 3 ? byte : 2] >> bit) & BIT(0);
        case 5:
            return (int2[byte < 3 ? byte : 2] >> bit) & BIT(0);
        case 6:
            return (clockAdjust >> bit) & BIT(0);
        case 7:
            return (freeRegister >> bit) & BIT(0);
        case 2:
            if (writeCount == 8)
                updateDateTime();
            return (dateTime[DateTimeByteIndex(writeCount)] >> (writeCount & 7)) & BIT(0);
        case 3:
            if (writeCount == 8)
                updateDateTime();
            return (dateTime[TimeByteIndex(writeCount)] >> (writeCount & 7)) & BIT(0);
        default:
            LOG_VERBOSE("Read from unknown RTC register: %d\n", index);
            return 0;
    }
}

void Rtc::writeRegister(uint8_t index, bool value) {
    if (core->isGbaMode()) {
        switch (index) {
            case 1:
                if (BIT(writeCount & 7) & 0x6A)
                    control = static_cast<uint8_t>((control & ~BIT(writeCount & 7)) | (value << (writeCount & 7)));
                return;
            case 2:
            case 3: {
                const int     first = index == 2 ? 0 : 4;
                const uint8_t bit   = writeCount & 7;
                if (writeCount == 8) {
                    updateDateTime();
                    for (int i = 0; i < 7; i++)
                        pending[i] = dateTime[i];
                }
                const size_t target = size_t(first) + (writeCount >= 8 ? size_t(writeCount / 8) - 1 : 0);
                if (target < 7)
                    pending[target] = uint8_t((pending[target] & ~BIT(bit)) | (value << bit));
                if (writeCount == (index == 2 ? 63 : 31))
                    applyPendingDateTime(first);
                return;
            }
            default:
                LOG_VERBOSE("Write to unknown GBA RTC register: %d\n", index);
                return;
        }
    }
    const uint8_t bit  = writeCount & 7;
    const size_t  byte = writeCount >= 8 ? size_t(writeCount / 8) - 1 : 0;
    auto set = [&](uint8_t &reg) { reg = uint8_t((reg & ~BIT(bit)) | (value << bit)); };
    switch (index) {
        case 0:
            if ((BIT(writeCount & 7) & 0x01) && value) {
                // Remise à zéro : registres et heure, sans couper le transfert.
                control = 0;
                resetRegisters();
                timeOffset = 0;
            } else if (BIT(writeCount & 7) & 0x0E) {
                control = static_cast<uint8_t>((control & ~BIT(writeCount & 7)) | (value << (writeCount & 7)));
            }
            return;
        case 1:
            set(status2);
            return;
        case 2: // Date + heure : 7 octets
        case 3: // Heure seule : 3 octets
        {
            const int first = index == 2 ? 0 : 4;
            if (writeCount == 8) {
                updateDateTime();
                for (int i = 0; i < 7; i++)
                    pending[i] = dateTime[i];
            }
            const size_t target = size_t(first) + byte;
            if (target < 7)
                set(pending[target]);
            if (writeCount == (index == 2 ? 63 : 31)) // dernier bit
                applyPendingDateTime(first);
            return;
        }
        case 4:
            if (byte < 3)
                set(int1[byte]);
            return;
        case 5:
            if (byte < 3)
                set(int2[byte]);
            return;
        case 6:
            set(clockAdjust);
            return;
        case 7:
            set(freeRegister);
            return;
        default:
            return;
    }
}

void Rtc::writeRtc(uint8_t value) {
    rtc      = static_cast<uint8_t>(value & ~0x07);
    bool cs  = (rtc & BIT(6)) ? (value & BIT(2)) : csCur;
    bool sck = (rtc & BIT(5)) ? !(value & BIT(1)) : sckCur;
    bool sio = (rtc & BIT(4)) ? (value & BIT(0)) : sioCur;
    updateRtc(cs, sck, sio);
}

void Rtc::writeGpData(uint16_t mask, uint16_t value) {
    if (mask & 0xFF) {
        bool cs  = (gpDirection & BIT(2)) ? (value & BIT(2)) : csCur;
        bool sio = (gpDirection & BIT(1)) ? (value & BIT(1)) : sioCur;
        bool sck = (gpDirection & BIT(0)) ? (value & BIT(0)) : sckCur;
        updateRtc(cs, sck, sio);
    }
}

void Rtc::writeGpDirection(uint16_t mask, uint16_t value) {
    mask &= 0x000F;
    gpDirection = (gpDirection & ~mask) | (value & mask);
}

void Rtc::writeGpControl(uint16_t mask, uint16_t value) {
    if (!gpRtc)
        return;
    mask &= 0x0001;
    gpControl = (gpControl & ~mask) | (value & mask);
    core->memory.updateMap7(0x8000000, 0x8001000);
}

uint8_t Rtc::readRtc() {
    bool cs  = csCur;
    bool sck = (rtc & BIT(5)) ? 0 : sckCur;
    bool sio = (rtc & BIT(4)) ? 0 : sioCur;
    return rtc | (cs << 2) | (sck << 1) | (sio << 0);
}

uint16_t Rtc::readGpData() {
    bool cs  = (gpDirection & BIT(2)) ? 0 : csCur;
    bool sio = (gpDirection & BIT(1)) ? 0 : sioCur;
    bool sck = (gpDirection & BIT(0)) ? 0 : sckCur;
    return (cs << 2) | (sio << 1) | (sck << 0);
}

} // namespace emulator
