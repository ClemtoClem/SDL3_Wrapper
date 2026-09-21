#ifndef GBC_APU_H
#define GBC_APU_H

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#include "../state_archive.hpp"

namespace emulator_demo {

class GbcCore;

/**
 * @brief GBC/DMG 4-channel APU: two square channels (channel 1 also has a
 * frequency sweep), one wave-table channel, one noise (LFSR) channel, mixed
 * through NR50/NR51-style master volume/panning into a double-buffered
 * stereo PCM stream. The double-buffer + condition_variable handoff
 * (bufferIn/bufferOut, swapped in swapBuffers(), consumed by getSamples())
 * deliberately mirrors the NDS/GBA Spu (see io/spu.hpp): swapBuffers()
 * blocks the calling (emulation) thread until the audio-pump thread has
 * drained the previous buffer, which is what paces GBC emulation to real
 * console speed exactly the same way it does for NDS/GBA — see the
 * threading-model section of README.md.
 */
class GbcApu {
  public:
    explicit GbcApu(GbcCore *core) : core(core) {
    }
    ~GbcApu();

    /// Advances channel phase/envelope/sweep/length timing by `cycles` CPU
    /// cycles and appends any newly-due samples to the current input buffer,
    /// swapping (and blocking on backpressure) once it's full.
    void step(int cycles);

    /// Blocks (like Spu::getSamples()) until a full buffer of `count`
    /// packed-stereo samples (low 16 bits = left, high 16 = right) is ready,
    /// then returns a caller-owned `new[]` buffer.
    uint32_t *getSamples(int count);

    uint8_t read(uint16_t port);
    void    write(uint16_t port, uint8_t value);

    void ioState(StateArchive &archive);

  private:
    GbcCore *core;

    struct Channel {
        bool enabled = false;
        int  freqTimer = 0;
        int  lengthCounter = 0;
        bool lengthEnable = false;
        uint8_t volume = 0;
        int  envelopeTimer = 0;
        uint8_t envelopePeriod = 0;
        bool envelopeIncrease = false;
        int  dutyPos = 0; // square channels only
    };

    Channel ch1{}, ch2{}, ch4{};
    // Channel 3 (wave) has its own simpler state: no envelope, a fixed
    // volume shift instead, and a rolling position into wave RAM.
    bool    ch3Enabled = false;
    int     ch3FreqTimer = 0;
    int     ch3LengthCounter = 0;
    bool    ch3LengthEnable = false;
    int     ch3Position = 0;

    // Channel-4 LFSR state.
    uint16_t lfsr = 0x7fff;

    uint8_t nr10 = 0, nr11 = 0, nr12 = 0, nr13 = 0, nr14 = 0;
    uint8_t nr21 = 0, nr22 = 0, nr23 = 0, nr24 = 0;
    uint8_t nr30 = 0, nr31 = 0, nr32 = 0, nr33 = 0, nr34 = 0;
    uint8_t nr41 = 0, nr42 = 0, nr43 = 0, nr44 = 0;
    uint8_t nr50 = 0, nr51 = 0, nr52 = 0xf1;
    std::array<uint8_t, 0x10> waveRam{};

    // Frame sequencer: a 512Hz clock (every 8192 CPU cycles at single
    // speed) that steps length counters (every other tick), envelopes
    // (every 8th tick) and channel-1's sweep (every 4th tick) — the real
    // GB hardware's timing source for all three, kept as one counter here
    // exactly like it is on real hardware.
    int frameSeqCycleAccum = 0;
    int frameSeqStep = 0;

    int sampleCycleAccum = 0;

    static constexpr int kSampleRate = 32768; // matches the NDS Spu's rate
    int       bufferSize    = 0;
    int       bufferPointer = 0;
    uint32_t *bufferIn  = nullptr;
    uint32_t *bufferOut = nullptr;

    std::atomic<bool>      ready{false};
    std::condition_variable cond1, cond2;
    std::mutex              mutex1, mutex2;

    void stepFrameSequencer();
    void stepChannels(int cycles);
    void mixSample();
    void swapBuffers();

    void triggerSquare(Channel &ch, uint8_t nrX1, uint8_t nrX2, uint8_t nrX4, bool hasSweep);
    void triggerWave();
    void triggerNoise();
};

} // namespace emulator_demo

#endif // GBC_APU_H
