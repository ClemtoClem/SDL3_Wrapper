#pragma once
/**
 * Modèles ARTICULÉS des entités (créatures, joueur, projectiles).
 *
 * Un « rig » est une hiérarchie de parties ; chaque partie tourne autour de
 * son pivot et porte des boîtes texturées selon le dépliage classique des
 * skins (une origine UV par boîte) ou un maillage (modèle .obj).
 *
 * Définition JSON (type "model", format "rig") — unités : pixels (1/16 de
 * bloc), y vers le haut depuis les pieds, le modèle regarde vers +z :
 *
 *   {"type": "model", "id": "biped", "format": "rig", "textureSize": [64, 32],
 *    "parts": [
 *      {"name": "head", "pivot": [0, 24, 0], "look": true,
 *       "boxes": [{"from": [-4, 24, -4], "size": [8, 8, 8], "uv": [0, 0]},
 *                 {"from": [-4, 24, -4], "size": [8, 8, 8], "uv": [32, 0], "inflate": 0.5}]},
 *      {"name": "left_leg", "pivot": [2, 12, 0], "walk": ["x", 180, 40],
 *       "boxes": [{"from": [0, 0, -2], "size": [4, 12, 4], "uv": [0, 16], "mirror": true,
 *                  "uv64": [16, 48]}]}]}
 *
 *   uv       coin haut-gauche du dépliage de la boîte (dessus, dessous puis
 *            côté droit, face, côté gauche, dos)
 *   uv64     autre origine pour les skins 64×64 (bras et jambe gauches)
 *   mirror   dépliage retourné (membre gauche qui reprend le droit)
 *   inflate  boîte agrandie sans changer ses UV (calques : chapeau, veste)
 *   only64   boîte ignorée sur un skin 64×32 (calques des skins récents)
 *   walk     [axe, phase°, amplitude°] : balancement selon la marche
 *   idle     [axe, phase°, amplitude°] : mouvement permanent (queue, ailes)
 *   look     la partie suit le regard (tangage de la tête)
 *   rotation rotation de repos [x, y, z] en degrés
 *   parent   nom de la partie porteuse (pivot relatif à la sienne)
 */
#include <optional>
#include <vector>

#include "assets.hpp"

namespace voxel {

struct RigMotion {
	int axis = 0;          ///< 0 x, 1 y, 2 z
	float phase = 0.f;     ///< degrés
	float amplitude = 0.f; ///< degrés
};

struct RigBox {
	math::FVector3 from{0.f, 0.f, 0.f}, size{1.f, 1.f, 1.f};
	float inflate = 0.f;
	int u = 0, v = 0;
	std::optional<std::pair<int, int>> uv64;
	bool mirror = false;
	bool only64 = false; ///< calque des skins 64×64 seulement (veste, manches…)
};

struct RigPart {
	String name, parent;
	math::FVector3 pivot{0.f, 0.f, 0.f};    ///< pixels, repère du modèle
	math::FVector3 rotation{0.f, 0.f, 0.f}; ///< degrés, au repos
	std::vector<RigBox> boxes;
	ModelMesh mesh; ///< maillage tout fait (modèle .obj), dans le repère du modèle
	std::optional<RigMotion> walk, idle;
	bool look = false;
};

struct RigModel {
	int textureWidth = 64, textureHeight = 32;
	float scale = 1.f / 16.f; ///< unités du modèle → blocs
	/// Vrai si le modèle est ramené à une hauteur de 1 (modèle .obj) :
	/// l'entité le met à la taille de sa boîte de collision.
	bool unitHeight = false;
	String texture; ///< skin par défaut (id de texture ou fichier)
	std::vector<RigPart> parts;
	[[nodiscard]] int Find(const String &name) const;
};

[[nodiscard]] Result<RigModel, String> RigFromJson(const NodePtr &def);
/// Modèle d'une seule partie fait d'un maillage (posé sur y = 0, centré,
/// hauteur ramenée à 1).
[[nodiscard]] RigModel RigFromMesh(ModelMesh mesh, const String &texture);

/// Maillage d'une partie en BLOCS, relatif à son pivot, avec des UV pour une
/// texture de `width` × `height` pixels (dimensions réelles du skin).
[[nodiscard]] ModelMesh BuildRigPart(const RigModel &rig, const RigPart &part, int width, int height);

/// Rotation d'une partie (degrés, x/y/z) pour une phase de marche, une
/// intensité de marche (0..1), un temps (s) et un tangage de regard (rad).
[[nodiscard]] math::FVector3 PoseRigPart(const RigPart &part, float walkPhase, float walkAmount, float time, float lookPitch);

} // namespace voxel
