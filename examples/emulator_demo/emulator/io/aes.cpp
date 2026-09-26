#include "aes.hpp"

#include "../core.hpp"

#include <algorithm>
#include <cstring>

namespace emulator_demo {

namespace {
// Bit-rotation helpers for the Rijndael math below — not used anywhere else
// in the codebase, so kept local rather than added to defines.hpp.
inline uint8_t ROL8(uint8_t v, int n) {
    return uint8_t((v << n) | (v >> (8 - n)));
}
inline uint32_t ROL32(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}
inline uint32_t ROR32(uint32_t v, int n) {
    return (v >> n) | (v << (32 - n));
}
} // namespace

Aes::Aes(Core *core): core(core) {
    updateTask = std::bind(&Aes::update, this);

    // Generate temporary pow and log tables
    uint8_t pow[0x100], log[0x100];
    for (int i = 0, x = 1; i < 0x100; i++) {
        pow[i] = x; log[x] = i;
        x ^= (x << 1) ^ ((x & 0x80) ? 0x11B : 0);
    }

    // Generate the AES forward and reverse S-boxes
    for (int i = 0; i < 0x100; i++) {
        uint8_t x = i ? pow[0xFF - log[i]] : 0;
        x = x ^ ROL8(x, 1) ^ ROL8(x, 2) ^ ROL8(x, 3) ^ ROL8(x, 4) ^ 0x63;
        fsBox[i] = x; rsBox[x] = i;
    }

    // Generate the AES forward and reverse tables
    for (int i = 0; i < 0x100; i++) {
        uint8_t x = (fsBox[i] << 1) ^ ((fsBox[i] & 0x80) ? 0x11B : 0);
        fTable[i] = (fsBox[i] * 0x10101) ^ (x * 0x1000001);
        rTable[i] = (x = rsBox[i]) ? (pow[(log[x] + log[0xE]) % 0xFF] << 24) | (pow[(log[x] + log[0x9]) % 0xFF] << 16) |
            (pow[(log[x] + log[0xD]) % 0xFF] << 8) | pow[(log[x] + log[0xB]) % 0xFF] : 0;
    }
}

void Aes::ioState(StateArchive &archive) {
    // DSi-only state — this fork never sets core->dsiMode, so this is a
    // no-op today, but keeps Aes serializable the same way as every other
    // component if DSi support ever lands.
    if (!core->dsiMode) return;
    archive.io(scheduled);
    archive.io(keys);
    archive.io(keysX);
    archive.io(keysY);
    archive.io(rKey);
    archive.io(ctr);
    archive.io(cbc);
    archive.io(curBlock);
    archive.io(curExtra);
    archive.io(curKey);
    archive.io(aesCnt);
    archive.io(aesBlkcnt);
    archive.io(aesRdfifo);
    archive.io(aesIv);
    archive.io(aesMac);

    // StateArchive has no std::deque overload (only ioQueue() for
    // std::queue) — serialize manually, mirroring ioQueue()'s own format.
    auto ioDeque = [&archive](std::deque<uint32_t> &d) {
        if (archive.saving) {
            uint32_t n = uint32_t(d.size());
            archive.io(n);
            for (uint32_t v : d)
                archive.io(v);
        } else {
            uint32_t n = 0;
            archive.io(n);
            d.clear();
            for (uint32_t i = 0; i < n; i++) {
                uint32_t v = 0;
                archive.io(v);
                d.push_back(v);
            }
        }
    };
    ioDeque(writeFifo);
    ioDeque(readFifo);
}

uint32_t Aes::scatter8(uint8_t *t, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    // Perform a scatter operation with an 8-bit table
    return (t[(d >> 24) & 0xFF] << 24) | (t[(c >> 16) & 0xFF] << 16) | (t[(b >> 8) & 0xFF] << 8) | t[a & 0xFF];
}

uint32_t Aes::scatter32(uint32_t *t, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    // Perform a scatter operation with a 32-bit table
    return t[(d >> 24) & 0xFF] ^ ROR32(t[(c >> 16) & 0xFF], 8) ^ ROR32(t[(b >> 8) & 0xFF], 16) ^ ROR32(t[a & 0xFF], 24);
}

void Aes::rolKey(uint32_t *key, uint8_t rol) {
    // Rotate a key left by entire words
    while (rol >= 32) {
        uint32_t temp = key[3];
        key[3] = key[2];
        key[2] = key[1];
        key[1] = key[0];
        key[0] = temp;
        rol -= 32;
    }

    // Rotate a key left by less than a word
    if (!rol) return;
    uint32_t temp = key[3];
    key[3] = (key[3] << rol) | (key[2] >> (32 - rol));
    key[2] = (key[2] << rol) | (key[1] >> (32 - rol));
    key[1] = (key[1] << rol) | (key[0] >> (32 - rol));
    key[0] = (key[0] << rol) | (temp >> (32 - rol));
}

void Aes::xorKey(uint32_t *dst, uint32_t *src) {
    // Bitwise exclusive or one key with another
    for (int i = 0; i < 4; i++)
        dst[i] ^= src[i];
}

void Aes::addKey(uint32_t *dst, uint32_t *src) {
    // Add one key to another
    bool over = false;
    for (int i = 0; i < 4; i++) {
        uint32_t res = dst[i] + src[i] + over;
        over = (res < dst[i] || (res == dst[i] && over));
        dst[i] = res;
    }
}

void Aes::cryptBlock(uint32_t *src, uint32_t *dst) {
    // Set initial values for encryption/decryption
    uint32_t y0 = rKey[0] ^ src[0];
    uint32_t y1 = rKey[1] ^ src[1];
    uint32_t y2 = rKey[2] ^ src[2];
    uint32_t y3 = rKey[3] ^ src[3];

    // Encrypt/decrypt a block of data
    for (int i = 4; i < 40; i += 4) {
        uint32_t x0 = rKey[i + 0] ^ scatter32(fTable, y1, y2, y3, y0);
        uint32_t x1 = rKey[i + 1] ^ scatter32(fTable, y2, y3, y0, y1);
        uint32_t x2 = rKey[i + 2] ^ scatter32(fTable, y3, y0, y1, y2);
        uint32_t x3 = rKey[i + 3] ^ scatter32(fTable, y0, y1, y2, y3);
        y0 = x0; y1 = x1; y2 = x2; y3 = x3;
    }

    // Output the final encrypted/decrypted values
    dst[0] = rKey[40] ^ scatter8(fsBox, y1, y2, y3, y0);
    dst[1] = rKey[41] ^ scatter8(fsBox, y2, y3, y0, y1);
    dst[2] = rKey[42] ^ scatter8(fsBox, y3, y0, y1, y2);
    dst[3] = rKey[43] ^ scatter8(fsBox, y0, y1, y2, y3);
}

void Aes::initFifo() {
    // Get the current AES mode and reload block counters
    uint8_t mode = (aesCnt >> 28) & 0x3;
    curBlock = (aesBlkcnt >> 16);
    curExtra = (mode < 2) ? (aesBlkcnt & 0xFFFF) : 0;
    aesCnt &= ~BIT(21); // CCM verify
    LOG("AES FIFO starting in mode %d\n", mode);

    // Initialize the ring key values
    // `y` porte la dernière valeur d'un tour au suivant : déclaré DANS la
    // boucle (comme à l'origine), sa lecture au tour suivant était indéfinie.
    uint32_t y = 0;
    for (int i = 0, x = 1; i < 44; i += 4) {
        for (int j = 3; j >= 0; j--)
            rKey[i + j] = y = (i ? (y ^ rKey[i + j - 4]) : keys[curKey][j]);
        y = scatter8(fsBox, y, y, y, y);
        y = ROL32(y, 8) ^ (x << 24);
        x = (x << 1) ^ ((x & 0x80) ? 0x11B : 0);
    }

    // Initialize encryption/decryption based on the mode
    switch (mode) {
    case 0: case 1: // CCM
        ctr[0] = cbc[0] = (aesIv[0] << 24);
        ctr[1] = cbc[1] = (aesIv[1] << 24) | (aesIv[0] >> 8);
        ctr[2] = cbc[2] = (aesIv[2] << 24) | (aesIv[1] >> 8);
        ctr[3] = cbc[3] = (0x2 << 24) | (aesIv[2] >> 8);
        cbc[0] |= (curBlock << 4);
        cbc[3] |= ((curExtra > 0) << 30) | ((aesCnt << 11) & 0x38000000);
        cryptBlock(cbc, cbc);
        return;

    default: // CTR
        memcpy(ctr, aesIv, sizeof(ctr));
        return;
    }
}

void Aes::triggerFifo() {
    // Schedule a FIFO update if one hasn't been already
    if (scheduled) return;
    core->schedule(Task(&updateTask, 1));
    scheduled = true;
}

void Aes::update() {
    // Process FIFO blocks when active and available
    while ((aesCnt & BIT(31)) && writeFifo.size() >= 4 && readFifo.size() <= 12) {
        // Receive an input block from the write FIFO
        uint32_t src[4], dst[4];
        for (int i = 0; i < 4; i++) {
            src[i] = writeFifo.front();
            writeFifo.pop_front();
        }

        // Process a CCM extra block and send it to the read FIFO if enabled
        if (curExtra) {
            for (int i = 0; i < 4; i++)
                cbc[i] ^= src[i];
            cryptBlock(cbc, cbc);
            if (aesCnt & BIT(19))
                for (int i = 0; i < 4; i++)
                    readFifo.push_back(src[i]);
            curExtra--;
            continue;
        }

        // Handle the block based on the selected mode
        uint8_t mode = (aesCnt >> 28) & 0x3;
        switch (mode) {
        case 0: // CCM decrypt
            for (int i = 0, c = 1; i < 4; i++)
                ctr[i] += c, c &= (ctr[i] == 0);
            cryptBlock(ctr, dst);
            for (int i = 0; i < 4; i++)
                cbc[i] ^= (dst[i] ^= src[i]);
            cryptBlock(cbc, cbc);
            break;

        case 1: // CCM encrypt
            for (int i = 0, c = 1; i < 4; i++)
                ctr[i] += c, c &= (ctr[i] == 0);
            cryptBlock(ctr, dst);
            for (int i = 0; i < 4; i++)
                cbc[i] ^= src[i], dst[i] ^= src[i];
            cryptBlock(cbc, cbc);
            break;

        default: // CTR
            cryptBlock(ctr, dst);
            for (int i = 0, c = 1; i < 4; i++) {
                ctr[i] += c;
                c &= (ctr[i] == 0);
                dst[i] ^= src[i];
            }
            break;
        }

        // Send an output block to the read FIFO
        for (int i = 0; i < 4; i++)
            readFifo.push_back(dst[i]);

        // Disable the FIFO once all blocks are processed and trigger an interrupt if enabled
        if (--curBlock > 0) continue;
        aesCnt &= ~BIT(31);
        if (aesCnt & BIT(30))
            core->interpreter[1].sendInterrupt(44);
        LOG("AES FIFO finished processing\n");

        // Calculate a CCM MAC for applicable modes
        if (mode > 1) continue;
        ctr[0] &= ~0xFFFFFF;
        cryptBlock(ctr, ctr);
        for (int i = 0; i < 4; i++)
            cbc[i] ^= ctr[i];

        // Create a mask based on the MAC length
        uint32_t mask[4] = {};
        uint8_t len = std::max(4U, ((aesCnt >> 15) & 0xE) + 2);
        for (int i = 0; i < len; i += 2)
            mask[i / 4] |= 0xFFFF << ((i << 3) & 0x10);

        // Handle final verification steps for CCM modes
        if (mode == 1) { // Encrypt
            // Append the MAC to the output data (possibly overflowing the FIFO, but that's fine)
            for (int i = 0; i < 4; i++)
                readFifo.push_back(cbc[i] & mask[i]);
        }
        else if (~aesCnt & BIT(20)) {
            // Catch unhandled verification using the write FIFO
            LOG("Unhandled AES-CCM MAC verification source used: FIFO\n");
        }
        else { // Decrypt
            // Verify decryption by comparing the MAC with one provided via registers
            bool fail = false;
            for (int i = 0; i < 4; i++)
                if ((fail = (cbc[i] ^ aesMac[i]) & mask[i])) break;
            aesCnt |= (!fail << 21);
        }
    }

    // Update the AES FIFO sizes
    aesCnt = (aesCnt & ~0x3FF) | (std::min<uint8_t>(16, readFifo.size()) << 5) | writeFifo.size();
    scheduled = false;

    // NOTE: upstream sets/clears NDMA DRQs 0xA/0xB here to auto-feed the AES
    // FIFOs via DMA. This fork never ported NDMA (it's DSi-only, and DSi
    // mode is never enabled here — see core->dsiMode), so there's no NDMA
    // channel to signal. Left unimplemented rather than faking a stub.
}

uint32_t Aes::readRdfifo() {
    // Pop a value from the read FIFO
    if (readFifo.empty()) return aesRdfifo;
    aesRdfifo = readFifo.front();
    readFifo.pop_front();
    triggerFifo();
    return aesRdfifo;
}

void Aes::writeCnt(uint32_t mask, uint32_t value) {
    // Write to the AES_CNT register
    uint32_t mask2 = (mask & 0xFC1FF000);
    bool start = (value & mask2 & ~aesCnt & BIT(31));
    aesCnt = (aesCnt & ~mask2) | (value & mask2);

    // Handle write-only bits that trigger events
    if (value & mask & BIT(10)) writeFifo = {}; // Empty write FIFO
    if (value & mask & BIT(11)) readFifo = {}; // Empty read FIFO
    if (value & mask & BIT(24)) curKey = (aesCnt >> 26) & 0x3; // Apply key slot

    // Start processing a new set of FIFO blocks if triggered
    if (!start) return;
    initFifo();
    triggerFifo();
}

void Aes::writeBlkcnt(uint32_t mask, uint32_t value) {
    // Write to the AES_BLKCNT register
    aesBlkcnt = (aesBlkcnt & ~mask) | (value & mask);
}

void Aes::writeWrfifo(uint32_t mask, uint32_t value) {
    // Push a value to the write FIFO
    if (writeFifo.size() == 16) return;
    writeFifo.push_back(value & mask);
    triggerFifo();
}

void Aes::writeIv(int i, uint32_t mask, uint32_t value) {
    // Write to part of the AES_IV value
    aesIv[i] = (aesIv[i] & ~mask) | (value & mask);
}

void Aes::writeMac(int i, uint32_t mask, uint32_t value) {
    // Write to part of the AES_MAC value
    aesMac[i] = (aesMac[i] & ~mask) | (value & mask);
}

void Aes::writeKey(int i, int j, uint32_t mask, uint32_t value) {
    // Write to part of one of the AES key slots
    keys[i][j] = (keys[i][j] & ~mask) | (value & mask);
}

void Aes::writeKeyx(int i, int j, uint32_t mask, uint32_t value) {
    // Write to part of one of the AES key X slots
    keysX[i][j] = (keys[i][j] & ~mask) | (value & mask);
}

void Aes::writeKeyy(int i, int j, uint32_t mask, uint32_t value) {
    // Write to part of one of the AES key Y slots
    keysY[i][j] = (keys[i][j] & ~mask) | (value & mask);

    // Generate a key using X and Y when the last word is written to
    if (j < 3) return;
    memcpy(keys[i], keysX[i], sizeof(keys[0]));
    uint32_t seed[4] = { 0x1A4F3E79, 0x2A680F5F, 0x29590258, 0xFFFEFB4E };
    xorKey(keys[i], keysY[i]);
    addKey(keys[i], seed);
    rolKey(keys[i], 42);
}

} // namespace emulator_demo