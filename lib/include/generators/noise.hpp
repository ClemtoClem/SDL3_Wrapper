#pragma once
/**
 * generators — bruits cohérents, la matière première du terrain procédural
 * (dans l'esprit de TerraForge3D : couches de bruit + fractales + distorsion).
 *
 *   Base      : PERLIN (gradient), SIMPLEX, VALUE, WORLEY (cellulaire) ;
 *   Fractale  : NONE, FBM (somme d'octaves), RIDGED (crêtes : montagnes),
 *               BILLOW (dunes, nuages), PING_PONG (strates) ;
 *   Distorsion de domaine (domain warp) : les coordonnées sont décalées par
 *   un second bruit avant l'échantillonnage (reliefs tordus, érodés).
 *
 * Sorties : environ [-1, 1] pour les bruits de gradient/valeur ; [0, 1]
 * (distance normalisée) pour WORLEY. Tout est déterministe par graine.
 *
 * @code
 * generators::NoiseSettings s;
 * s.type = generators::NoiseType::SIMPLEX;
 * s.fractal = generators::FractalType::RIDGED;
 * s.octaves = 6; s.frequency = 0.004f; s.seed = 42;
 * generators::Noise noise(s);
 * float h = noise.Sample(x, y);
 * @endcode
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "random.hpp"

namespace generators {

enum class NoiseType : uint8_t { PERLIN, SIMPLEX, VALUE, WORLEY };
enum class FractalType : uint8_t { NONE, FBM, RIDGED, BILLOW, PING_PONG };
/// Cellulaire : distance à la cellule la plus proche (F1), à la deuxième
/// (F2), leur différence (F2 - F1 : réseau de fissures), ou valeur de la
/// cellule (pavage de Voronoï).
enum class CellularReturn : uint8_t { F1, F2, F2_MINUS_F1, CELL_VALUE };
enum class DistanceMetric : uint8_t { EUCLIDEAN, MANHATTAN, CHEBYSHEV };

struct NoiseSettings {
	NoiseType type = NoiseType::PERLIN;
	FractalType fractal = FractalType::FBM;
	uint32_t seed = 1337;
	float frequency = 0.01f;
	int octaves = 5;
	float lacunarity = 2.f;  ///< multiplicateur de fréquence entre octaves
	float gain = 0.5f;       ///< multiplicateur d'amplitude entre octaves (persistance)
	float weightedStrength = 0.f; ///< atténue les octaves là où les précédentes sont basses (0 : désactivé)
	float pingPongStrength = 2.f;
	CellularReturn cellular = CellularReturn::F1;
	DistanceMetric metric = DistanceMetric::EUCLIDEAN;
	float jitter = 1.f;      ///< écart des points de Worley à la grille (0 : grille régulière)
	float warpAmplitude = 0.f; ///< distorsion de domaine (0 : aucune), en unités de coordonnées
	float warpFrequency = 0.01f;
	float offsetX = 0.f, offsetY = 0.f, offsetZ = 0.f;
};

class Noise {
public:
	explicit Noise(NoiseSettings settings = {}) : m_s(settings) { BuildPermutation(); }

	[[nodiscard]] const NoiseSettings &Settings() const noexcept { return m_s; }
	void SetSettings(const NoiseSettings &settings);

	/// Échantillon 2D (fractale et distorsion comprises).
	[[nodiscard]] float Sample(float x, float y) const;
	/// Échantillon 3D.
	[[nodiscard]] float Sample(float x, float y, float z) const;
	/// Même chose, ramené dans [0, 1].
	[[nodiscard]] float Sample01(float x, float y) const;

	// ── Bruits de base, une octave, fréquence 1 ─────────────────────────────

	[[nodiscard]] float Base2(float x, float y, uint32_t octave) const;
	[[nodiscard]] float Base3(float x, float y, float z, uint32_t octave) const;

	[[nodiscard]] float Perlin2(float x, float y, uint32_t octave = 0) const;
	[[nodiscard]] float Perlin3(float x, float y, float z, uint32_t octave = 0) const;

	/// Simplex 2D (Gustavson) — moins d'artefacts d'alignement que Perlin.
	[[nodiscard]] float Simplex2(float x, float y, uint32_t octave = 0) const;
	[[nodiscard]] float Simplex3(float x, float y, float z, uint32_t octave = 0) const;

	/// Bruit de valeur : valeurs aléatoires aux nœuds, interpolées.
	[[nodiscard]] float Value2(float x, float y, uint32_t octave = 0) const;
	[[nodiscard]] float Value3(float x, float y, float z, uint32_t octave = 0) const;

	/// Bruit cellulaire (Worley) 2D, dans [0, 1].
	[[nodiscard]] float Worley2(float x, float y, uint32_t octave = 0) const;
	[[nodiscard]] float Worley3(float x, float y, float z, uint32_t octave = 0) const;

private:
	[[nodiscard]] static int FastFloor(float v) noexcept;
	[[nodiscard]] static float Fade(float t) noexcept { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }
	[[nodiscard]] static float Lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

	void BuildPermutation();
	[[nodiscard]] uint32_t Perm(int x, int y, uint32_t octave) const noexcept;
	[[nodiscard]] uint32_t Perm(int x, int y, int z, uint32_t octave) const noexcept;
	[[nodiscard]] static float Grad2(uint32_t hash, float x, float y) noexcept;
	[[nodiscard]] static float Grad3(uint32_t hash, float x, float y, float z) noexcept;
	[[nodiscard]] float Distance(float dx, float dy, float dz) const noexcept;
	[[nodiscard]] float CellularResult(float f1, float f2, float cellValue) const noexcept;

	/// Normalise la somme des amplitudes des octaves (sortie ≈ [-1, 1]).
	[[nodiscard]] float FractalBounding() const noexcept;

	template <typename SampleFn> [[nodiscard]] float Fractal(SampleFn sample) const {
		if (m_s.fractal == FractalType::NONE)
			return sample(1.f, 0u);
		float sum = 0.f, amp = FractalBounding(), freq = 1.f;
		for (int o = 0; o < std::max(1, m_s.octaves); ++o) {
			float n = sample(freq, uint32_t(o));
			switch (m_s.fractal) {
				case FractalType::RIDGED: {
					// Crêtes : 1 - |n|, ramené dans [-1, 1].
					n = (1.f - std::fabs(n)) * 2.f - 1.f;
					break;
				}
				case FractalType::BILLOW:
					n = std::fabs(n) * 2.f - 1.f;
					break;
				case FractalType::PING_PONG: {
					float t = (n + 1.f) * m_s.pingPongStrength;
					t -= float(int(t * 0.5f)) * 2.f;
					n = (t < 1.f ? t : 2.f - t) * 2.f - 1.f;
					break;
				}
				default:
					break;
			}
			sum += n * amp;
			// Octaves pondérées : un relief bas reste lisse (TerraForge3D).
			const float weight = m_s.weightedStrength > 0.f ? Lerp(1.f, std::clamp(n * 0.5f + 0.5f, 0.f, 1.f), m_s.weightedStrength)
															: 1.f;
			amp *= m_s.gain * weight;
			freq *= m_s.lacunarity;
		}
		return sum;
	}
	[[nodiscard]] float Fractal2(float x, float y) const;
	[[nodiscard]] float Fractal3(float x, float y, float z) const;

	void Warp(float &x, float &y) const;
	void Warp(float &x, float &y, float &z) const;

	NoiseSettings m_s;
	std::array<uint8_t, 256> m_perm{};
};

} // namespace generators
