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
    [[nodiscard]] float DurationSeconds() const noexcept;
};

// ============================================================================
// Windowing — applied in place, matches an analysis block before ProcessFFT().
// ============================================================================

enum class WindowFunction { Rectangular, Hann, Hamming, Blackman };

void WindowHann(std::span<float> data) noexcept;

void WindowHamming(std::span<float> data) noexcept;

void WindowBlackman(std::span<float> data) noexcept;

/// No-op, kept so callers can dispatch on WindowFunction without a special case.
inline void WindowRectangular(std::span<float>) noexcept {}

void ApplyWindow(std::span<float> data, WindowFunction fn) noexcept;

// ============================================================================
// FFT — Cooley-Tukey radix-2, in-place, sample count must be a power of 2.
// ============================================================================

using Complex = std::complex<float>;

namespace detail {
[[nodiscard]] constexpr bool IsPowerOfTwo(size_t n) noexcept { return n != 0 && (n & (n - 1)) == 0; }
} // namespace detail

/// Forward FFT of a real-valued signal. `samples.size()` must be a power of 2.
[[nodiscard]] std::vector<Complex> ProcessFFT(std::span<const float> samples);

/// Magnitude spectrum in dB, one bin per frequency in [0, sampleRate/2) —
/// only the first half of `spectrum` is meaningful for a real-valued input
/// (the second half is its mirror image, cf. Nyquist).
[[nodiscard]] std::vector<float> ProcessFFTMagnitudeDb(std::span<const Complex> spectrum, float floorDb = -120.f);

/// Center frequency (Hz) of each bin returned by ProcessFFTMagnitudeDb() for an FFT
/// of the given size at the given sample rate.
[[nodiscard]] std::vector<float> ProcessFFTFrequencies(size_t fftSize, int sampleRate);

// ============================================================================
// Level metering
// ============================================================================

[[nodiscard]] float GetRMS(std::span<const float> samples) noexcept;

[[nodiscard]] float GetPeak(std::span<const float> samples) noexcept;

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
[[nodiscard]] BiQuadCoeffs BiQuadDesign(BiQuadKind kind, float sampleRate, float cutoffHz,
                                                float q = 0.7071f) noexcept;

/// Re-design `state`'s coefficients in place (delay line untouched — safe to
/// call every block to sweep a cutoff live without a discontinuity reset).
void BiQuadSetDesign(BiQuadState &state, BiQuadKind kind, float sampleRate, float cutoffHz,
                             float q = 0.7071f) noexcept;

/// Process one sample through the filter, advancing its delay line.
[[nodiscard]] float BiQuadStep(BiQuadState &state, float x0) noexcept;

// ============================================================================
// Soft clipping — tanh knee above `threshold`, transparent below it.
// ============================================================================

[[nodiscard]] float SoftClip(float x, float threshold = 0.8f) noexcept;

void SoftClip(std::span<float> samples, float threshold = 0.8f) noexcept;

} // namespace audio
