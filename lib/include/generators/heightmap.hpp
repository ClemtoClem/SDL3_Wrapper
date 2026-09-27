#pragma once
/**
 * generators — cartes de hauteur : la chaîne de traitement d'un terrain
 * procédural, dans l'esprit de TerraForge3D.
 *
 *   bruit(s) → mélanges (masques) → filtres (courbe, terrasses, flou…)
 *            → érosion (hydraulique, thermique) → cartes dérivées (pente,
 *              normales, masques) → export (PGM 8/16 bits, RAW 16 bits, PPM)
 *
 * Une carte est une grille de flottants (hauteurs normalisées ou non).
 * Toutes les opérations sont en place et CHAÎNABLES :
 *
 * @code
 * generators::HeightMap map(256, 256);
 * map.Fill(montagnes).Combine(collines, generators::BlendMode::ADD, 0.3f)
 *    .Normalize().Terrace(8, 0.6f).ErodeHydraulic({.droplets = 50000})
 *    .SavePgm("terrain.pgm", true);
 * @endcode
 *
 * Aucune exception : l'export rend `Option<String>` (l'erreur, ou rien).
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "../core/core.hpp"
#include "noise.hpp"
#include "random.hpp"

namespace generators {

/// Mélange d'une couche sur la carte (a : carte, b : couche).
enum class BlendMode : uint8_t { SET, ADD, SUBTRACT, MULTIPLY, MIN, MAX, AVERAGE, SCREEN, DIFFERENCE, OVERLAY };

[[nodiscard]] float Blend(BlendMode mode, float a, float b) noexcept;

/// Érosion hydraulique par gouttes (Hans Theobald Beyer, 2015) : chaque
/// goutte descend la pente, arrache de la matière là où elle accélère et la
/// dépose là où elle ralentit — ravines, vallées et cônes de déjection.
struct HydraulicErosion {
	int droplets = 70000;
	int maxLifetime = 30;
	float inertia = 0.05f;        ///< 0 : suit la pente ; 1 : garde sa direction
	float sedimentCapacity = 4.f;
	float minCapacity = 0.01f;
	float erodeSpeed = 0.3f;
	float depositSpeed = 0.3f;
	float evaporateSpeed = 0.01f;
	float gravity = 4.f;
	int radius = 3;               ///< rayon d'érosion (brosse)
	float initialWater = 1.f;
	float initialSpeed = 1.f;
	uint64_t seed = 1;
};

/// Érosion thermique : la matière glisse là où la pente dépasse l'angle de
/// talus — éboulis, versants adoucis.
struct ThermalErosion {
	int iterations = 50;
	float talus = 0.01f; ///< différence de hauteur (entre voisins) au-delà de laquelle ça glisse
	float amount = 0.5f; ///< part de l'excédent déplacée à chaque itération
};

class HeightMap {
public:
	HeightMap() = default;
	HeightMap(int width, int height, float fill = 0.f)
		: m_width(std::max(1, width)), m_height(std::max(1, height)), m_data(size_t(m_width) * size_t(m_height), fill) {}

	[[nodiscard]] int Width() const noexcept { return m_width; }
	[[nodiscard]] int Height() const noexcept { return m_height; }
	[[nodiscard]] size_t Size() const noexcept { return m_data.size(); }
	[[nodiscard]] const std::vector<float> &Data() const noexcept { return m_data; }
	[[nodiscard]] std::vector<float> &Data() noexcept { return m_data; }

	/// Case (x, y), coordonnées ramenées dans la carte.
	[[nodiscard]] float At(int x, int y) const noexcept;
	[[nodiscard]] float &Ref(int x, int y) noexcept;
	void Set(int x, int y, float v) noexcept { Ref(x, y) = v; }

	/// Interpolation bilinéaire en coordonnées de case (réelles).
	[[nodiscard]] float Sample(float x, float y) const noexcept;
	/// Même chose en coordonnées normalisées u, v ∈ [0, 1].
	[[nodiscard]] float SampleUV(float u, float v) const noexcept;

	// ── Remplissage et mélange ──────────────────────────────────────────────

	/// Chaque case prend `fn(x, y)`.
	template <typename Fn> HeightMap &Generate(Fn &&fn) {
		for (int y = 0; y < m_height; ++y)
			for (int x = 0; x < m_width; ++x)
				m_data[size_t(y) * size_t(m_width) + size_t(x)] = float(fn(x, y));
		return *this;
	}
	/// Bruit ramené dans [0, 1] (`scale` : taille du monde par case).
	HeightMap &Fill(const Noise &noise, float scale = 1.f);
	/// Mélange une couche de bruit : `strength` dose l'effet, `mask`
	/// (même taille, [0, 1]) le localise.
	HeightMap &AddNoise(const Noise &noise, BlendMode mode = BlendMode::ADD, float strength = 1.f, float scale = 1.f,
						const HeightMap *mask = nullptr) {
		for (int y = 0; y < m_height; ++y)
			for (int x = 0; x < m_width; ++x) {
				float &a = Ref(x, y);
				const float b = noise.Sample01(float(x) * scale, float(y) * scale);
				const float w = strength * (mask ? std::clamp(mask->SampleUV(float(x) / float(std::max(1, m_width - 1)),
																			 float(y) / float(std::max(1, m_height - 1))),
															  0.f, 1.f)
												 : 1.f);
				a = a + (Blend(mode, a, b) - a) * w;
			}
		return *this;
	}
	/// Mélange une autre carte (rééchantillonnée si les tailles diffèrent).
	HeightMap &Combine(const HeightMap &other, BlendMode mode, float strength = 1.f, const HeightMap *mask = nullptr) {
		for (int y = 0; y < m_height; ++y)
			for (int x = 0; x < m_width; ++x) {
				const float u = float(x) / float(std::max(1, m_width - 1)), v = float(y) / float(std::max(1, m_height - 1));
				float &a = Ref(x, y);
				const float w = strength * (mask ? std::clamp(mask->SampleUV(u, v), 0.f, 1.f) : 1.f);
				a = a + (Blend(mode, a, other.SampleUV(u, v)) - a) * w;
			}
		return *this;
	}

	// ── Filtres ─────────────────────────────────────────────────────────────

	template <typename Fn> HeightMap &Apply(Fn &&fn) {
		for (float &v : m_data)
			v = float(fn(v));
		return *this;
	}
	HeightMap &Scale(float factor, float offset = 0.f);
	HeightMap &Clamp(float lo = 0.f, float hi = 1.f);
	HeightMap &Invert();
	HeightMap &Abs();
	/// Exposant (sur des valeurs normalisées) : > 1 creuse les vallées, < 1 les comble.
	HeightMap &Power(float exponent);
	/// Ramène les valeurs de [min, max] vers [lo, hi].
	HeightMap &Normalize(float lo = 0.f, float hi = 1.f);
	HeightMap &Remap(float fromLo, float fromHi, float toLo, float toHi);
	/// Courbe par points (x croissants, entrée et sortie normalisées) :
	/// l'outil « courbe » d'un logiciel de terrain.
	HeightMap &Curve(const std::vector<std::pair<float, float>> &points);
	/// Terrasses : `levels` paliers. Chaque marche est un plateau sur la
	/// fraction `sharpness` de sa hauteur, puis une montée adoucie jusqu'à
	/// la suivante (1 : marches nettes, 0 : aucune terrasse).
	HeightMap &Terrace(int levels, float sharpness = 0.5f);
	/// Niveau de la mer : tout ce qui est plus bas est aplati à `level`.
	HeightMap &SeaLevel(float level);
	/// Flou de boîte (séparable), `radius` en cases.
	HeightMap &BoxBlur(int radius, int passes = 1);
	/// Flou gaussien (≈ trois flous de boîte).
	HeightMap &Smooth(float sigma);
	/// Accentue le détail : v + amount × (v − flou(v)).
	HeightMap &Sharpen(float amount, int radius = 1);
	/// Île : atténue vers les bords (distance radiale^exposant).
	HeightMap &IslandFalloff(float exponent = 2.f, float strength = 1.f);
	/// Rééchantillonnage bilinéaire à une nouvelle taille.
	[[nodiscard]] HeightMap Resized(int width, int height) const;

	// ── Érosion ─────────────────────────────────────────────────────────────

	HeightMap &ErodeHydraulic(const HydraulicErosion &p);

	HeightMap &ErodeThermal(const ThermalErosion &p);

	// ── Cartes dérivées ─────────────────────────────────────────────────────

	/// Pente en degrés (`cellSize` : taille d'une case, `heightScale` :
	/// hauteur réelle d'une unité de la carte).
	[[nodiscard]] HeightMap SlopeMap(float cellSize = 1.f, float heightScale = 1.f) const;
	/// Normale (nx, ny, nz) de la case, Y vers le haut (convention du dépôt).
	void Normal(int x, int y, float cellSize, float heightScale, float &nx, float &ny, float &nz) const noexcept;
	/// Masque [0, 1] des hauteurs dans [lo, hi], adouci sur `feather`.
	[[nodiscard]] HeightMap HeightMask(float lo, float hi, float feather = 0.f) const;
	/// Masque [0, 1] des pentes (degrés) dans [minDeg, maxDeg].
	[[nodiscard]] HeightMap SlopeMask(float minDeg, float maxDeg, float feather = 0.f, float cellSize = 1.f,
									  float heightScale = 1.f) const;
	/// 1 dans l'intervalle, 0 hors de lui, rampe linéaire de largeur `feather`.
	[[nodiscard]] static float Band(float v, float lo, float hi, float feather) noexcept;

	// ── Statistiques ────────────────────────────────────────────────────────

	[[nodiscard]] float Min() const noexcept { return m_data.empty() ? 0.f : *std::min_element(m_data.begin(), m_data.end()); }
	[[nodiscard]] float Max() const noexcept { return m_data.empty() ? 0.f : *std::max_element(m_data.begin(), m_data.end()); }
	[[nodiscard]] float Mean() const noexcept;

	// ── Export ──────────────────────────────────────────────────────────────

	/// Image en niveaux de gris PGM (binaire) ; `sixteenBits` : 16 bits par
	/// pixel (ce qu'attendent les moteurs pour un terrain). Valeurs
	/// supposées dans [0, 1] (bornées).
	[[nodiscard]] Option<String> SavePgm(const String &path, bool sixteenBits = false) const;
	/// Données brutes 16 bits petit-boutistes (format « .r16 » des moteurs).
	[[nodiscard]] Option<String> SaveRaw16(const String &path) const;
	/// Image couleur PPM : `color(valeur, x, y)` rend (r, g, b) dans [0, 255].
	template <typename ColorFn> [[nodiscard]] Option<String> SavePpm(const String &path, ColorFn &&color) const {
		std::FILE *file = std::fopen(path.CStr(), "wb");
		if (!file)
			return Some(String::Format("impossible d'écrire %s", path.CStr()));
		std::fprintf(file, "P6\n%d %d\n255\n", m_width, m_height);
		for (int y = 0; y < m_height; ++y)
			for (int x = 0; x < m_width; ++x) {
				const auto rgb = color(At(x, y), x, y);
				const unsigned char bytes[3] = {uint8_t(std::clamp(int(rgb[0]), 0, 255)), uint8_t(std::clamp(int(rgb[1]), 0, 255)),
												uint8_t(std::clamp(int(rgb[2]), 0, 255))};
				std::fwrite(bytes, 1, 3, file);
			}
		return std::fclose(file) == 0 ? Option<String>(NONE) : Some(String::Format("écriture incomplète de %s", path.CStr()));
	}

private:
	int m_width = 1, m_height = 1;
	std::vector<float> m_data = std::vector<float>(1, 0.f);
};

} // namespace generators
