#include "emulator/gbc/gbc_apu.hpp"

#include <chrono>
#include <cstring>
#include <utility>

#include "emulator/gbc/gbc_core.hpp"
#include "emulator/gbc/gbc_defines.hpp"
#include "emulator/settings.hpp"

namespace emulator_demo {

namespace {
constexpr int kDutySteps[4] = {1, 2, 4, 6}; // out of 8 steps high, approximating 12.5/25/50/75%
constexpr int kNoiseDivisors[8] = {8, 16, 32, 48, 64, 80, 96, 112};

int square_freq_period(uint8_t lo, uint8_t hi) {
    int freqReg = lo | ((hi & 7) << 8);
    return (2048 - freqReg) * 4;
}
} // namespace

GbcApu::~GbcApu() {
    delete[] bufferIn;
    delete[] bufferOut;
}

void GbcApu::triggerSquare(Channel &ch, uint8_t nrX1, uint8_t nrX2, uint8_t, bool) {
    ch.enabled = true;
    if (ch.lengthCounter == 0) ch.lengthCounter = 64 - (nrX1 & 0x3f);
    ch.volume = nrX2 >> 4;
    ch.envelopePeriod = nrX2 & 7;
    ch.envelopeIncrease = nrX2 & 8;
    ch.envelopeTimer = ch.envelopePeriod;
    if ((nrX2 >> 3) == 0) ch.enabled = false; // DAC off (both volume=0 and increase=0)
}

void GbcApu::triggerWave() {
    ch3Enabled = (nr30 & 0x80) != 0;
    if (ch3LengthCounter == 0) ch3LengthCounter = 256 - nr31;
    ch3Position = 0;
}

void GbcApu::triggerNoise() {
    ch4.enabled = true;
    if (ch4.lengthCounter == 0) ch4.lengthCounter = 64 - (nr41 & 0x3f);
    ch4.volume = nr42 >> 4;
    ch4.envelopePeriod = nr42 & 7;
    ch4.envelopeIncrease = nr42 & 8;
    ch4.envelopeTimer = ch4.envelopePeriod;
    lfsr = 0x7fff;
    if ((nr42 >> 3) == 0) ch4.enabled = false;
}

void GbcApu::stepFrameSequencer() {
    bool lengthTick = (frameSeqStep % 2) == 0;
    bool sweepTick  = (frameSeqStep == 2 || frameSeqStep == 6);
    bool envTick    = (frameSeqStep == 7);

    auto tickLength = [&](int &counter, bool enable, bool &enabled) {
        if (enable && counter > 0) {
            counter--;
            if (counter == 0) enabled = false;
        }
    };
    if (lengthTick) {
        tickLength(ch1.lengthCounter, ch1.lengthEnable, ch1.enabled);
        tickLength(ch2.lengthCounter, ch2.lengthEnable, ch2.enabled);
        tickLength(ch3LengthCounter, ch3LengthEnable, ch3Enabled);
        tickLength(ch4.lengthCounter, ch4.lengthEnable, ch4.enabled);
    }

    if (sweepTick && ch1.enabled) {
        uint8_t period = (nr10 >> 4) & 7;
        uint8_t shift  = nr10 & 7;
        if (period && shift) {
            int freqReg = nr13 | ((nr14 & 7) << 8);
            int delta   = freqReg >> shift;
            int newFreq = (nr10 & 8) ? freqReg - delta : freqReg + delta;
            if (newFreq > 2047) {
                ch1.enabled = false;
            } else if (newFreq >= 0) {
                nr13 = uint8_t(newFreq & 0xff);
                nr14 = uint8_t((nr14 & 0xf8) | ((newFreq >> 8) & 7));
            }
        }
    }

    if (envTick) {
        auto tickEnv = [](Channel &ch) {
            if (ch.envelopePeriod == 0) return;
            if (ch.envelopeTimer > 0) ch.envelopeTimer--;
            if (ch.envelopeTimer == 0) {
                ch.envelopeTimer = ch.envelopePeriod;
                if (ch.envelopeIncrease && ch.volume < 15) ch.volume++;
                else if (!ch.envelopeIncrease && ch.volume > 0) ch.volume--;
            }
        };
        tickEnv(ch1);
        tickEnv(ch2);
        tickEnv(ch4);
    }

    frameSeqStep = (frameSeqStep + 1) & 7;
}

void GbcApu::stepChannels(int cycles) {
    // Channel 1/2: square wave, freqTimer counts in CPU cycles.
    for (Channel *ch : {&ch1, &ch2}) {
        uint8_t lo = (ch == &ch1) ? nr13 : nr23;
        uint8_t hi = (ch == &ch1) ? nr14 : nr24;
        ch->freqTimer -= cycles;
        while (ch->freqTimer <= 0) {
            ch->freqTimer += square_freq_period(lo, hi);
            ch->dutyPos = (ch->dutyPos + 1) & 7;
        }
    }
    // Channel 3: wave, advances at double the square-channel rate (2 samples/period vs 1).
    if (ch3Enabled) {
        ch3FreqTimer -= cycles;
        while (ch3FreqTimer <= 0) {
            int freqReg = nr33 | ((nr34 & 7) << 8);
            ch3FreqTimer += (2048 - freqReg) * 2;
            ch3Position = (ch3Position + 1) & 31;
        }
    }
    // Channel 4: noise LFSR.
    ch4.freqTimer -= cycles;
    while (ch4.freqTimer <= 0) {
        uint8_t shift = nr43 >> 4;
        uint8_t divCode = nr43 & 7;
        ch4.freqTimer += kNoiseDivisors[divCode] << shift;
        uint16_t bit = (lfsr ^ (lfsr >> 1)) & 1;
        lfsr = uint16_t((lfsr >> 1) | (bit << 14));
        if (nr43 & 8) lfsr = uint16_t((lfsr & ~0x40) | (bit << 6));
    }
}

void GbcApu::mixSample() {
    if (!(nr52 & 0x80) || bufferSize <= 0) return;

    int left = 0, right = 0;

    auto addChannel = [&](int amp, int channelBit) {
        if (nr51 & (1 << (channelBit + 4))) left += amp;
        if (nr51 & (1 << channelBit)) right += amp;
    };

    if (ch1.enabled) {
        bool high = ch1.dutyPos < kDutySteps[(nr11 >> 6) & 3];
        addChannel(high ? ch1.volume : 0, 0);
    }
    if (ch2.enabled) {
        bool high = ch2.dutyPos < kDutySteps[(nr21 >> 6) & 3];
        addChannel(high ? ch2.volume : 0, 1);
    }
    if (ch3Enabled && (nr30 & 0x80)) {
        uint8_t sample = waveRam[ch3Position / 2];
        sample = (ch3Position & 1) ? uint8_t(sample & 0xf) : uint8_t(sample >> 4);
        uint8_t shift = (nr32 >> 5) & 3;
        int amp = shift == 0 ? 0 : (sample >> (shift - 1));
        addChannel(amp, 2);
    }
    if (ch4.enabled) {
        bool high = (lfsr & 1) == 0;
        addChannel(high ? ch4.volume : 0, 3);
    }

    // Each channel contributes 0-15; 4 channels -> 0-60 per side. Scale by
    // NR50's per-side master volume (0-7, "+1" per hardware convention) and
    // normalize into signed 16-bit PCM.
    int leftVol  = ((nr50 >> 4) & 7) + 1;
    int rightVol = (nr50 & 7) + 1;
    int16_t l = int16_t((left * leftVol * 32767) / (60 * 8));
    int16_t r = int16_t((right * rightVol * 32767) / (60 * 8));

    bufferIn[bufferPointer++] = (uint32_t(uint16_t(r)) << 16) | uint32_t(uint16_t(l));
    if (bufferPointer == bufferSize) swapBuffers();
}

void GbcApu::step(int cycles) {
    stepChannels(cycles);

    frameSeqCycleAccum += cycles;
    constexpr int kFrameSeqPeriod = 8192; // 4194304 / 512
    while (frameSeqCycleAccum >= kFrameSeqPeriod) {
        frameSeqCycleAccum -= kFrameSeqPeriod;
        stepFrameSequencer();
    }

    sampleCycleAccum += cycles;
    int cyclesPerSample = gbc::kClockHz / kSampleRate;
    while (sampleCycleAccum >= cyclesPerSample) {
        sampleCycleAccum -= cyclesPerSample;
        mixSample();
    }
}

void GbcApu::swapBuffers() {
    if (Settings::getFpsLimiter() == 2) {
        auto waitTime = std::chrono::steady_clock::now();
        while (ready.load() && std::chrono::steady_clock::now() - waitTime <= std::chrono::microseconds(1000000))
            ;
    } else if (Settings::getFpsLimiter() == 1) {
        std::unique_lock<std::mutex> lock(mutex1);
        cond1.wait_for(lock, std::chrono::microseconds(1000000), [&] { return !ready.load(); });
    }
    std::swap(bufferIn, bufferOut);
    {
        std::lock_guard<std::mutex> guard(mutex2);
        ready.store(true);
        cond2.notify_one();
    }
    bufferPointer = 0;
}

uint32_t *GbcApu::getSamples(int count) {
    if (bufferSize != count) {
        delete[] bufferIn;
        delete[] bufferOut;
        bufferIn      = new uint32_t[count];
        bufferOut     = new uint32_t[count];
        bufferSize    = count;
        bufferPointer = 0;
    }
    bool wait;
    if (Settings::getFpsLimiter() == 2) {
        auto waitTime = std::chrono::steady_clock::now();
        wait = false;
        while (!ready.load()) {
            if (std::chrono::steady_clock::now() - waitTime > std::chrono::microseconds(1000000 / 60)) {
                wait = true;
                break;
            }
        }
    } else {
        std::unique_lock<std::mutex> lock(mutex2);
        wait = !cond2.wait_for(lock, std::chrono::microseconds(1000000 / 60), [&] { return ready.load(); });
    }
    uint32_t *out = new uint32_t[count];
    if (wait) {
        uint32_t last = bufferOut ? bufferOut[count - 1] : 0;
        for (int i = 0; i < count; i++) out[i] = last;
    } else {
        std::memcpy(out, bufferOut, size_t(count) * sizeof(uint32_t));
    }
    {
        std::lock_guard<std::mutex> guard(mutex1);
        ready.store(false);
        cond1.notify_one();
    }
    return out;
}

uint8_t GbcApu::read(uint16_t port) {
    switch (port) {
        case 0xff10: return uint8_t(nr10 | 0x80);
        case 0xff11: return uint8_t(nr11 | 0x3f);
        case 0xff12: return nr12;
        case 0xff13: return 0xff;
        case 0xff14: return uint8_t(nr14 | 0xbf);
        case 0xff16: return uint8_t(nr21 | 0x3f);
        case 0xff17: return nr22;
        case 0xff18: return 0xff;
        case 0xff19: return uint8_t(nr24 | 0xbf);
        case 0xff1a: return uint8_t(nr30 | 0x7f);
        case 0xff1b: return 0xff;
        case 0xff1c: return uint8_t(nr32 | 0x9f);
        case 0xff1d: return 0xff;
        case 0xff1e: return uint8_t(nr34 | 0xbf);
        case 0xff20: return 0xff;
        case 0xff21: return nr42;
        case 0xff22: return nr43;
        case 0xff23: return uint8_t(nr44 | 0xbf);
        case 0xff24: return nr50;
        case 0xff25: return nr51;
        case 0xff26:
            return uint8_t((nr52 & 0x80) | 0x70 | (ch1.enabled ? 1 : 0) | (ch2.enabled ? 2 : 0) |
                            (ch3Enabled ? 4 : 0) | (ch4.enabled ? 8 : 0));
        default:
            if (port >= 0xff30 && port <= 0xff3f) return waveRam[port - 0xff30];
            return 0xff;
    }
}

void GbcApu::write(uint16_t port, uint8_t value) {
    if (!(nr52 & 0x80) && port != 0xff26 && !(port >= 0xff30 && port <= 0xff3f)) return; // APU off: ignore writes
    switch (port) {
        case 0xff10: nr10 = value; break;
        case 0xff11: nr11 = value; ch1.lengthCounter = 64 - (value & 0x3f); break;
        case 0xff12: nr12 = value; break;
        case 0xff13: nr13 = value; break;
        case 0xff14:
            nr14 = value;
            ch1.lengthEnable = value & 0x40;
            if (value & 0x80) triggerSquare(ch1, nr11, nr12, nr14, true);
            break;
        case 0xff16: nr21 = value; ch2.lengthCounter = 64 - (value & 0x3f); break;
        case 0xff17: nr22 = value; break;
        case 0xff18: nr23 = value; break;
        case 0xff19:
            nr24 = value;
            ch2.lengthEnable = value & 0x40;
            if (value & 0x80) triggerSquare(ch2, nr21, nr22, nr24, false);
            break;
        case 0xff1a: nr30 = value; if (!(value & 0x80)) ch3Enabled = false; break;
        case 0xff1b: nr31 = value; ch3LengthCounter = 256 - value; break;
        case 0xff1c: nr32 = value; break;
        case 0xff1d: nr33 = value; break;
        case 0xff1e:
            nr34 = value;
            ch3LengthEnable = value & 0x40;
            if (value & 0x80) triggerWave();
            break;
        case 0xff20: nr41 = value; ch4.lengthCounter = 64 - (value & 0x3f); break;
        case 0xff21: nr42 = value; break;
        case 0xff22: nr43 = value; break;
        case 0xff23:
            nr44 = value;
            ch4.lengthEnable = value & 0x40;
            if (value & 0x80) triggerNoise();
            break;
        case 0xff24: nr50 = value; break;
        case 0xff25: nr51 = value; break;
        case 0xff26:
            nr52 = uint8_t((value & 0x80) | (nr52 & 0x7f));
            if (!(value & 0x80)) {
                ch1 = Channel{}; ch2 = Channel{}; ch4 = Channel{};
                ch3Enabled = false;
                nr10 = nr11 = nr12 = nr13 = nr14 = 0;
                nr21 = nr22 = nr23 = nr24 = 0;
                nr30 = nr31 = nr32 = nr33 = nr34 = 0;
                nr41 = nr42 = nr43 = nr44 = 0;
                nr50 = nr51 = 0;
            }
            break;
        default:
            if (port >= 0xff30 && port <= 0xff3f) waveRam[port - 0xff30] = value;
            break;
    }
}

void GbcApu::ioState(StateArchive &archive) {
    auto ioChannel = [&](Channel &ch) {
        archive.io(ch.enabled);
        archive.io(ch.freqTimer);
        archive.io(ch.lengthCounter);
        archive.io(ch.lengthEnable);
        archive.io(ch.volume);
        archive.io(ch.envelopeTimer);
        archive.io(ch.envelopePeriod);
        archive.io(ch.envelopeIncrease);
        archive.io(ch.dutyPos);
    };
    ioChannel(ch1);
    ioChannel(ch2);
    ioChannel(ch4);
    archive.io(ch3Enabled);
    archive.io(ch3FreqTimer);
    archive.io(ch3LengthCounter);
    archive.io(ch3LengthEnable);
    archive.io(ch3Position);
    archive.io(lfsr);
    archive.io(nr10); archive.io(nr11); archive.io(nr12); archive.io(nr13); archive.io(nr14);
    archive.io(nr21); archive.io(nr22); archive.io(nr23); archive.io(nr24);
    archive.io(nr30); archive.io(nr31); archive.io(nr32); archive.io(nr33); archive.io(nr34);
    archive.io(nr41); archive.io(nr42); archive.io(nr43); archive.io(nr44);
    archive.io(nr50); archive.io(nr51); archive.io(nr52);
    archive.io(waveRam);
    archive.io(frameSeqCycleAccum);
    archive.io(frameSeqStep);
    archive.io(sampleCycleAccum);
}

} // namespace emulator_demo
