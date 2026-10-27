#pragma once
/**
 * Éclairage du monde voxel : sources, transport de la lumière directe,
 * occlusion par la géométrie.
 *
 * CONVENTIONS (les seules du moteur) :
 *   - Couleurs : JSON en sRGB 0..255 (« [255, 40, 20] » ou « "#ff2814" ») ;
 *     tous les calculs en RGB LINÉAIRE 0..1 (SrgbToLinear), la conversion
 *     inverse et le tone mapping n'ont lieu qu'à l'affichage (shader).
 *   - Intensité I : éclairement reçu à 1 bloc d'une source ponctuelle, sur
 *     une face tournée vers elle. Un éclairement de 1 est « bien éclairé »
 *     (blanc mat lisible) ; l'affichage compresse au-delà (tone mapping).
 *   - Distances en blocs (1 bloc = 1 m) ; angles en degrés dans le JSON.
 *   - Directions dans le repère du bloc à l'orientation 0, tournées avec lui.
 *
 * MODÈLE (éclairage direct, sans rebond) : pour chaque point d'une surface
 * de normale n, chaque source contribue
 *
 *     E += couleur × I × atténuation(d) × distribution(angle) × max(0, n·L) × T
 *
 *   - atténuation : 1 / max(d², 0,25) (loi de l'inverse du carré, régularisée
 *     à moins d'un demi-bloc), « linear » ou « none » au choix, toujours
 *     multipliée par la fenêtre (1 − (d / portée)⁴)² : la contribution
 *     s'éteint progressivement à la portée, sans marche ;
 *   - distribution : 1 (ponctuelle), cône intérieur/extérieur lissé (spot),
 *     faisceau parallèle de rayon fixe (directionnelle : ni inverse du carré
 *     ni cône, la lumière vient de −direction) ;
 *   - T : transmission du trajet source → point (Transmittance) — produit
 *     des transmissions des cellules traversées (parcours DDA de la grille) :
 *     0 dès qu'une cellule opaque est traversée, filtrée par le verre teinté,
 *     l'eau… La cellule de la source elle-même ne compte pas.
 *
 * Les contributions s'ADDITIONNENT (deux lampes : deux fois plus de
 * lumière ; rouge + bleu : magenta), dans l'ordre qu'on veut. La lumière
 * du ciel, l'ambiante et le soleil sont des termes à part (SkyAccess pour la
 * part de ciel visible, le reste dans le shader).
 */
#include <array>
#include <vector>

#include "world.hpp"

namespace voxel::lighting {

using Rgb = std::array<float, 3>;

/// Portée maximale d'une source (blocs) : borne des recherches.
inline constexpr float MAX_RANGE = 32.f;
/// Régularisation de l'inverse du carré : d² ≥ 0,25 (un demi-bloc).
inline constexpr float MIN_DISTANCE_SQ = 0.25f;
/// Couleur des sources historiques (« graphics.light » seul) : blanc chaud.
inline constexpr std::array<uint8_t, 3> LEGACY_COLOR{255, 214, 170};
/// Niveau historique 0..15 → intensité et portée.
[[nodiscard]] constexpr float LegacyIntensity(int level) noexcept { return 0.4f * float(level); }
[[nodiscard]] constexpr float LegacyRange(int level) noexcept { return float(level) + 1.f; }

[[nodiscard]] float SrgbToLinear(float c) noexcept;      ///< 0..1 → 0..1
[[nodiscard]] Rgb SrgbToLinear(uint8_t r, uint8_t g, uint8_t b) noexcept;
/// Niveau « équivalent » 0..15 d'une source (aspect lumineux des textures).
[[nodiscard]] int EquivalentLevel(const LightDef &light) noexcept;

/// Une source active dans le monde (bloc posé ou entité).
struct Source {
	math::FVector3 position{0.f, 0.f, 0.f}; ///< monde
	math::FVector3 direction{0.f, -1.f, 0.f}; ///< monde (spot, directionnelle)
	BlockPos cell{0, 0, 0};                 ///< cellule de la source (ignorée par l'occlusion)
	const LightDef *def = nullptr;
	/// Source d'un bloc à l'orientation `param` (direction et décalage tournés).
	[[nodiscard]] static Source ForBlock(const BlockDef &block, const BlockPos &pos, uint8_t param);
	/// Source d'une entité (pas d'orientation : direction telle quelle).
	[[nodiscard]] static Source ForEntity(const LightDef &light, const math::FVector3 &position);
	[[nodiscard]] bool operator==(const Source &o) const noexcept {
		return def == o.def && cell == o.cell && position.x == o.position.x && position.y == o.position.y && position.z == o.position.z &&
			   direction.x == o.direction.x && direction.y == o.direction.y && direction.z == o.direction.z;
	}
};

/// Sources actives (blocs posés, entités) dont la portée atteint la boîte
/// [lo, hi] — chunks chargés seulement.
void GatherSources(const World &world, const math::FVector3 &lo, const math::FVector3 &hi, std::vector<Source> &out);

/// Contribution d'une source en `point` (normale `normal` ; nulle : sans loi
/// de Lambert, éclairement « scalaire »), SANS occlusion. `toSource` reçoit
/// l'extrémité du trajet à tester (la source, ou son plan pour un faisceau).
[[nodiscard]] Rgb Unoccluded(const Source &source, const math::FVector3 &point, const math::FVector3 &normal, math::FVector3 &toSource);

/// Transmission du segment a → b à travers la grille (DDA), cellule `skip`
/// exclue. Les chunks non chargés sont transparents (rien de connu).
[[nodiscard]] Rgb Transmittance(const World &world, const math::FVector3 &a, const math::FVector3 &b, const BlockPos &skip);

/// Éclairement direct total en `point` : somme des contributions des
/// `sources`, chacune occultée par la géométrie.
[[nodiscard]] Rgb DirectIrradiance(const World &world, const math::FVector3 &point, const math::FVector3 &normal,
								   const std::vector<Source> &sources);

/// Part du ciel visible depuis `point` (0..1) : cinq rayons (zénith et
/// quatre obliques à 45°) jusqu'à 32 blocs ; au-delà des chunks chargés,
/// l'estimation du relief par le générateur.
[[nodiscard]] float SkyAccess(const World &world, const math::FVector3 &point);

} // namespace voxel::lighting
