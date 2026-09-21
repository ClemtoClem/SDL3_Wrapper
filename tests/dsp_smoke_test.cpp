// Smoke test : audio:: DSP (lib/audio/dsp.hpp) — windowing, FFT, level
// metering, biquad filter, soft clip. Real numeric assertions, not just
// "doesn't crash" — a known sine input must peak at the expected FFT bin,
// a designed low-pass biquad must actually attenuate above its cutoff.
#include "audio/dsp.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace audio;

int main() {
    constexpr float K_PI = 3.14159265358979323846f;

    // ── fft : signal sinusoïdal connu → pic au bon bin ───────────────────────
    {
        constexpr size_t K_N = 1024;
        constexpr int K_SAMPLE_RATE = 44100;

        // Choisir une fréquence qui tombe PILE sur un bin entier pour un pic net
        // (sinon l'énergie fuit sur les bins voisins — "spectral leakage" normal).
        const int TARGET_BIN = 23;
        const float EXACT_TONE_HZ = float(TARGET_BIN) * float(K_SAMPLE_RATE) / float(K_N);

        std::vector<float> samples(K_N);
        for (size_t i = 0; i < K_N; ++i)
            samples[i] = std::sin(2.f * K_PI * EXACT_TONE_HZ * float(i) / float(K_SAMPLE_RATE));

        auto spectrum = ProcessFFT(samples);
        assert(spectrum.size() == K_N);

        auto magDb = ProcessFFTMagnitudeDb(spectrum);
        assert(magDb.size() == K_N / 2);

        // Le bin cible doit dominer largement tous les autres bins.
        int peakBin = 0;
        for (size_t i = 1; i < magDb.size(); ++i)
            if (magDb[i] > magDb[peakBin])
                peakBin = int(i);
        assert(peakBin == TARGET_BIN);
        assert(magDb[TARGET_BIN] > -6.f);   // proche de 0 dBFS pour une sinusoïde pleine échelle
        assert(magDb[TARGET_BIN] - magDb[0] > 20.f); // domine nettement le DC

        auto freqs = ProcessFFTFrequencies(K_N, K_SAMPLE_RATE);
        assert(freqs.size() == K_N / 2);
        assert(std::abs(freqs[TARGET_BIN] - EXACT_TONE_HZ) < 0.01f);

        std::cout << "fft (pic au bin " << TARGET_BIN << " (" << EXACT_TONE_HZ << " Hz), "
                  << magDb[TARGET_BIN] << " dB): ok\n";
    }

    // ── windowing : atténue les extrémités, préserve le centre ───────────────
    {
        std::vector<float> data(8, 1.f);
        WindowHann(data);
        assert(std::abs(data[0]) < 1e-4f);       // bord = ~0
        assert(std::abs(data.back()) < 1e-4f);   // bord = ~0
        assert(data[4] > 0.9f);                  // centre proche de 1
        std::cout << "WindowHann (attenue les bords, preserve le centre): ok\n";
    }

    // ── rms/peak : mesures connues ────────────────────────────────────────────
    {
        std::vector<float> samples = {1.f, -1.f, 1.f, -1.f}; // carré plein échelle
        assert(std::abs(GetRMS(samples) - 1.f) < 1e-5f);
        assert(std::abs(GetPeak(samples) - 1.f) < 1e-5f);

        std::vector<float> half = {0.5f, -0.5f};
        assert(std::abs(GetPeak(half) - 0.5f) < 1e-5f);
        std::cout << "rms/peak (signal carre plein echelle + demi-echelle): ok\n";
    }

    // ── biquad : passe-bas atténue nettement au-dessus de la coupure ────────
    {
        constexpr float K_SAMPLE_RATE = 44100.f;
        constexpr float K_CUTOFF = 500.f;

        BiQuadState state;
        BiQuadSetDesign(state, BiQuadKind::LOW_PASS, K_SAMPLE_RATE, K_CUTOFF);

        auto measureGainDb = [&](float toneHz) {
            BiQuadState s = state; // delay line propre par mesure
            constexpr int K_SETTLE = 2000, K_MEASURE = 2000;
            float inPeak = 0.f, outPeak = 0.f;
            for (int i = 0; i < K_SETTLE + K_MEASURE; ++i) {
                float x = std::sin(2.f * K_PI * toneHz * float(i) / K_SAMPLE_RATE);
                float y = BiQuadStep(s, x);
                if (i >= K_SETTLE) {
                    inPeak = sdl3::Max(inPeak, sdl3::Abs(x));
                    outPeak = sdl3::Max(outPeak, sdl3::Abs(y));
                }
            }
            return 20.f * std::log10(outPeak / inPeak);
        };

        float gainBelow = measureGainDb(100.f);  // bien sous la coupure
        float gainAbove = measureGainDb(5000.f); // bien au-dessus de la coupure

        assert(gainBelow > -1.f);   // quasi inchangé sous la coupure
        assert(gainAbove < -20.f);  // nettement atténué au-dessus
        std::cout << "biquad LowPass (gain " << gainBelow << " dB @100Hz, " << gainAbove
                  << " dB @5kHz, coupure 500Hz): ok\n";
    }

    // ── softClip : transparent sous le seuil, borné au-dessus ────────────────
    {
        assert(std::abs(SoftClip(0.5f, 0.8f) - 0.5f) < 1e-6f); // sous le seuil : inchangé
        float clipped = SoftClip(2.f, 0.8f);
        assert(clipped > 0.8f && clipped < 1.f); // au-dessus du seuil : compressé, jamais >= 1
        assert(SoftClip(-2.f, 0.8f) < -0.8f && SoftClip(-2.f, 0.8f) > -1.f); // symétrique
        std::cout << "softClip (transparent sous le seuil, borne au-dessus): ok\n";
    }

    std::cout << "dsp smoke test done\n";
    return 0;
}
