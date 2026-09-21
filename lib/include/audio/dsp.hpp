#pragma once
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <span>
#include <vector>

#include "../sdl3/stdinc.hpp"

namespace audio {

// ============================================================================
// Signal — samples + sample rate, the common currency between DSP stages.
// ============================================================================

struct Signal {
    std::vector<float> samples;
    int sampleRate = 44100;

    [[nodiscard]] size_t GetSize() const noexcept { return samples.size(); }
    [[nodiscard]] float DurationSeconds() const noexcept {
        return sampleRate > 0 ? float(samples.size()) / float(sampleRate) : 0.f;
    }
};

// ============================================================================
// Windowing — applied in place, matches an analysis block before ProcessFFT().
// ============================================================================

enum class WindowFunction { Rectangular, Hann, Hamming, Blackman };

inline void WindowHann(std::span<float> data) noexcept {
    const size_t N = data.size();
    if (N < 2)
        return;
    for (size_t i = 0; i < N; ++i)
        data[i] *= 0.5f - 0.5f * sdl3::Cos(2.f * sdl3::PI_F * float(i) / float(N - 1));
}

inline void WindowHamming(std::span<float> data) noexcept {
    const size_t N = data.size();
    if (N < 2)
        return;
    for (size_t i = 0; i < N; ++i)
        data[i] *= 0.54f - 0.46f * sdl3::Cos(2.f * sdl3::PI_F * float(i) / float(N - 1));
}

inline void WindowBlackman(std::span<float> data) noexcept {
    const size_t N = data.size();
    if (N < 2)
        return;
    for (size_t i = 0; i < N; ++i) {
        float t = 2.f * sdl3::PI_F * float(i) / float(N - 1);
        data[i] *= 0.42f - 0.5f * sdl3::Cos(t) + 0.08f * sdl3::Cos(2.f * t);
    }
}

/// No-op, kept so callers can dispatch on WindowFunction without a special case.
inline void WindowRectangular(std::span<float>) noexcept {}

inline void ApplyWindow(std::span<float> data, WindowFunction fn) noexcept {
    switch (fn) {
    case WindowFunction::Hann: WindowHann(data); return;
    case WindowFunction::Hamming: WindowHamming(data); return;
    case WindowFunction::Blackman: WindowBlackman(data); return;
    case WindowFunction::Rectangular: WindowRectangular(data); return;
    }
}

// ============================================================================
// FFT — Cooley-Tukey radix-2, in-place, sample count must be a power of 2.
// ============================================================================

using Complex = std::complex<float>;

namespace detail {
[[nodiscard]] constexpr bool IsPowerOfTwo(size_t n) noexcept { return n != 0 && (n & (n - 1)) == 0; }
} // namespace detail

/// Forward FFT of a real-valued signal. `samples.size()` must be a power of 2.
[[nodiscard]] inline std::vector<Complex> ProcessFFT(std::span<const float> samples) {
    const size_t N = samples.size();
    assert(detail::IsPowerOfTwo(N) && "fft: sample count must be a power of 2");

    std::vector<Complex> data(N);
    for (size_t i = 0; i < N; ++i)
        data[i] = Complex(samples[i], 0.f);

    // Bit-reversal permutation.
    for (size_t i = 1, j = 0; i < N; ++i) {
        size_t bit = N >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }

    // Iterative Cooley-Tukey butterflies.
    for (size_t len = 2; len <= N; len <<= 1) {
        const float ANGLE = -2.f * sdl3::PI_F / float(len);
        const Complex WLEN(sdl3::Cos(ANGLE), sdl3::Sin(ANGLE));
        for (size_t i = 0; i < N; i += len) {
            Complex w(1.f, 0.f);
            for (size_t k = 0; k < len / 2; ++k) {
                Complex u = data[i + k];
                Complex v = data[i + k + len / 2] * w;
                data[i + k] = u + v;
                data[i + k + len / 2] = u - v;
                w *= WLEN;
            }
        }
    }
    return data;
}

/// Magnitude spectrum in dB, one bin per frequency in [0, sampleRate/2) —
/// only the first half of `spectrum` is meaningful for a real-valued input
/// (the second half is its mirror image, cf. Nyquist).
[[nodiscard]] inline std::vector<float> ProcessFFTMagnitudeDb(std::span<const Complex> spectrum, float floorDb = -120.f) {
    const size_t N = spectrum.size() / 2;
    std::vector<float> result(N);
    for (size_t i = 0; i < N; ++i) {
        // Single-sided amplitude: DC keeps the full-scale factor 1/N, every other
        // bin is doubled (2/N) to fold back the energy its mirror bin in the upper
        // half carries for a real-valued input — otherwise a full-scale sine reads
        // ~6 dB low.
        const float SCALE = i == 0 ? 1.f / float(spectrum.size()) : 2.f / float(spectrum.size());
        const float MAG = std::abs(spectrum[i]) * SCALE;
        const float DB = MAG > 1e-10f ? 20.f * sdl3::Log10(MAG) : floorDb;
        result[i] = sdl3::Max(DB, floorDb);
    }
    return result;
}

/// Center frequency (Hz) of each bin returned by ProcessFFTMagnitudeDb() for an FFT
/// of the given size at the given sample rate.
[[nodiscard]] inline std::vector<float> ProcessFFTFrequencies(size_t fftSize, int sampleRate) {
    const size_t N = fftSize / 2;
    std::vector<float> freqs(N);
    for (size_t i = 0; i < N; ++i)
        freqs[i] = float(i) * float(sampleRate) / float(fftSize);
    return freqs;
}

// ============================================================================
// Level metering
// ============================================================================

[[nodiscard]] inline float GetRMS(std::span<const float> samples) noexcept {
    if (samples.empty())
        return 0.f;
    double sumSq = 0.0;
    for (float s : samples)
        sumSq += double(s) * double(s);
    return float(sdl3::Sqrt(float(sumSq / double(samples.size()))));
}

[[nodiscard]] inline float GetPeak(std::span<const float> samples) noexcept {
    float p = 0.f;
    for (float s : samples)
        p = sdl3::Max(p, sdl3::Abs(s));
    return p;
}

// ============================================================================
// BiQuad — RBJ "Audio EQ Cookbook" biquad filter (direct form I).
// ============================================================================

enum class BiQuadKind { LOW_PASS, HIGH_PASS, BAND_PASS };

struct BiQuadCoeffs {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f; // a0 pre-normalised to 1
};

struct BiQuadState {
    BiQuadCoeffs coeffs;
    float x1 = 0.f, x2 = 0.f, y1 = 0.f, y2 = 0.f; // delay line
};

/// Design RBJ biquad coefficients. `q` is the resonance/Q factor (0.7071 ≈ Butterworth).
[[nodiscard]] inline BiQuadCoeffs BiQuadDesign(BiQuadKind kind, float sampleRate, float cutoffHz,
                                                float q = 0.7071f) noexcept {
    const float OMEGA = 2.f * sdl3::PI_F * cutoffHz / sampleRate;
    const float SIN_W = sdl3::Sin(OMEGA), COS_W = sdl3::Cos(OMEGA);
    const float ALPHA = SIN_W / (2.f * q);

    float b0, b1, b2, a0, a1, a2;
    switch (kind) {
    case BiQuadKind::HIGH_PASS:
        b0 = (1.f + COS_W) * 0.5f;
        b1 = -(1.f + COS_W);
        b2 = (1.f + COS_W) * 0.5f;
        a0 = 1.f + ALPHA;
        a1 = -2.f * COS_W;
        a2 = 1.f - ALPHA;
        break;
    case BiQuadKind::BAND_PASS:
        b0 = ALPHA;
        b1 = 0.f;
        b2 = -ALPHA;
        a0 = 1.f + ALPHA;
        a1 = -2.f * COS_W;
        a2 = 1.f - ALPHA;
        break;
    case BiQuadKind::LOW_PASS:
    default:
        b0 = (1.f - COS_W) * 0.5f;
        b1 = 1.f - COS_W;
        b2 = (1.f - COS_W) * 0.5f;
        a0 = 1.f + ALPHA;
        a1 = -2.f * COS_W;
        a2 = 1.f - ALPHA;
        break;
    }
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

/// Re-design `state`'s coefficients in place (delay line untouched — safe to
/// call every block to sweep a cutoff live without a discontinuity reset).
inline void BiQuadSetDesign(BiQuadState &state, BiQuadKind kind, float sampleRate, float cutoffHz,
                             float q = 0.7071f) noexcept {
    state.coeffs = BiQuadDesign(kind, sampleRate, cutoffHz, q);
}

/// Process one sample through the filter, advancing its delay line.
[[nodiscard]] inline float BiQuadStep(BiQuadState &state, float x0) noexcept {
    const BiQuadCoeffs &c = state.coeffs;
    const float Y0 = c.b0 * x0 + c.b1 * state.x1 + c.b2 * state.x2 - c.a1 * state.y1 - c.a2 * state.y2;
    state.x2 = state.x1;
    state.x1 = x0;
    state.y2 = state.y1;
    state.y1 = Y0;
    return Y0;
}

// ============================================================================
// Soft clipping — tanh knee above `threshold`, transparent below it.
// ============================================================================

[[nodiscard]] inline float SoftClip(float x, float threshold = 0.8f) noexcept {
    const float AX = sdl3::Abs(x);
    if (AX <= threshold)
        return x;
    const float SIGN = x < 0.f ? -1.f : 1.f;
    const float RANGE = 1.f - threshold;
    return SIGN * (threshold + RANGE * std::tanh((AX - threshold) / RANGE));
}

inline void SoftClip(std::span<float> samples, float threshold = 0.8f) noexcept {
    for (float &s : samples)
        s = SoftClip(s, threshold);
}

} // namespace audio
