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
 *   model    maillage de la partie au lieu de boîtes : fichier .gltf (ou
 *            .obj) relatif au mod ; avec "node", le seul nœud glTF de ce nom,
 *            dans son repère, posé au pivot ; sans, la scène entière dans le
 *            repère du modèle. Unités du fichier : blocs (converties en pixels).
 *
 * Animations à IMAGES CLÉS (en plus de walk / idle) :
 *   "animations": {"idle": {"length": 2, "loop": true, "tracks": [
 *      {"part": "head", "channel": "rotation",
 *       "keys": [{"t": 0, "v": [0, 0, 0]}, {"t": 1, "v": [0, 25, 0], "interp": "smooth"}]}]}}
 *   channel  « rotation » (degrés, ajoutés au repos) ou « position »
 *            (pixels, décalage du pivot)
 *   interp   interpolation VERS cette clé : linear (défaut), step, smooth
 *            (courbe douce : tangentes nulles), bezier (poignées libres)
 *   in, out  poignées de Bézier de la clé, relatives à elle :
 *            {"t": décalage en secondes, "v": [décalage de valeur]} ; `out`
 *            sert au segment SUIVANT (si sa clé d'arrivée est « bezier »),
 *            `in` au segment qui arrive. Absentes : un tiers de l'intervalle,
 *            valeur inchangée (équivaut à « smooth »).
 *   Le clip « idle » joue en permanence ; « walk » suit la marche (sa durée
 *   = un pas complet) et son poids est l'intensité de marche.
 *
 * MÉLANGE de clips (couches jouées en permanence, dans l'ordre, par-dessus
 * idle et walk) :
 *   "layers": [{"clip": "breath", "weight": 0.5, "speed": 1, "mode": "add"},
 *              {"clip": "look_around", "weight": 0.8, "mode": "mix"}]
 *   add  ajoute la piste pondérée par `weight` ;
 *   mix  amène la pose vers celle du clip (repos + piste) à hauteur de
 *        `weight`, sur les seuls canaux que le clip anime.
 */
#include <array>
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

enum class RigInterp : uint8_t { LINEAR, STEP, SMOOTH, BEZIER };
enum class RigChannel : uint8_t { ROTATION, POSITION };

/// Poignée de Bézier, relative à sa clé (secondes, valeur).
struct RigHandle {
	float time = 0.f;
	math::FVector3 value{0.f, 0.f, 0.f};
};

struct RigKey {
	float time = 0.f;
	math::FVector3 value{0.f, 0.f, 0.f};
	RigInterp interp = RigInterp::LINEAR; ///< vers cette clé, depuis la précédente
	/// Poignées (NONE : automatiques — un tiers de l'intervalle, pente nulle).
	std::optional<RigHandle> in, out;
};

struct RigTrack {
	int part = -1; ///< indice dans RigModel::parts
	RigChannel channel = RigChannel::ROTATION;
	std::vector<RigKey> keys; ///< triées par temps
};

struct RigClip {
	String name;
	float length = 1.f;
	bool loop = true;
	std::vector<RigTrack> tracks;
};

enum class RigBlend : uint8_t { MIX, ADD };

/// Une couche de mélange : un clip, son poids, sa vitesse, son mode.
struct RigLayer {
	String clip;
	float weight = 1.f;
	float speed = 1.f;
	RigBlend mode = RigBlend::MIX;
};

/// Point d'attache d'une partie (pixels, repère du modèle au repos) : les
/// yeux (caméra du joueur, juste devant le visage), les mains (objets tenus).
struct RigAnchor {
	int part = -1; ///< partie qui le porte (-1 : aucun)
	math::FVector3 at{0.f, 0.f, 0.f};
	[[nodiscard]] bool IsSet() const noexcept { return part >= 0; }
};

struct RigModel {
	int textureWidth = 64, textureHeight = 32;
	float scale = 1.f / 16.f; ///< unités du modèle → blocs
	/// Vrai si le modèle est ramené à une hauteur de 1 (modèle .obj) :
	/// l'entité le met à la taille de sa boîte de collision.
	bool unitHeight = false;
	String texture; ///< skin par défaut (id de texture ou fichier)
	std::vector<RigPart> parts;
	std::vector<RigClip> clips;
	std::vector<RigLayer> layers; ///< mélange permanent (cf. en-tête)
	/// « eye » du JSON, sinon : devant la face avant de la première partie qui
	/// suit le regard (« look »), à mi-hauteur de sa première boîte.
	RigAnchor eye;
	/// « hands » du JSON ({"right": {"part", "at"}, "left": …}), sinon : bas
	/// de la première boîte des parties « right_arm » / « left_arm ».
	RigAnchor rightHand, leftHand;
	[[nodiscard]] int Find(const String &name) const;
	[[nodiscard]] const RigClip *FindClip(const String &name) const;
};

/// `baseDir` : dossier du mod, pour les parties « model » (maillages glTF / OBJ).
[[nodiscard]] Result<RigModel, String> RigFromJson(const NodePtr &def, const String &baseDir = String());
/// Modèle d'une seule partie fait d'un maillage (posé sur y = 0, centré,
/// hauteur ramenée à 1).
[[nodiscard]] RigModel RigFromMesh(ModelMesh mesh, const String &texture);

/// Maillage d'une partie en BLOCS, relatif à son pivot, avec des UV pour une
/// texture de `width` × `height` pixels (dimensions réelles du skin).
[[nodiscard]] ModelMesh BuildRigPart(const RigModel &rig, const RigPart &part, int width, int height);

/// Rectangle de texture d'une face de boîte (pixels de la taille déclarée).
struct RigUvFace {
	const char *face; ///< « front », « right », « left », « back », « top », « bottom »
	float u, v, width, height;
};
/// Dépliage d'une boîte — exactement celui de BuildRigPart (origine `uv`,
/// miroir compris ; `uv64` ignoré).
[[nodiscard]] std::array<RigUvFace, 6> RigBoxUv(const RigBox &box);

/// Point de Bézier en (temps, valeur) d'un segment a → b, pour s ∈ [0, 1] ;
/// poignées automatiques complétées, temps des poignées bornés au segment
/// (la courbe reste une fonction du temps).
struct RigBezier {
	float t[4];
	math::FVector3 v[4];
};
[[nodiscard]] RigBezier RigSegmentBezier(const RigKey &a, const RigKey &b);
/// Valeur d'une piste au temps `time` (avant la première clé : la première ;
/// après la dernière : la dernière).
[[nodiscard]] math::FVector3 SampleRigTrack(const RigTrack &track, float time);
/// Temps dans un clip : bouclé, ou borné à sa durée.
[[nodiscard]] float RigClipTime(const RigClip &clip, float time);

/// Ce qui anime un rig à un instant.
struct RigPoseInput {
	float walkPhase = 0.f;  ///< radians (avance avec la vitesse)
	float walkAmount = 0.f; ///< 0..1
	float time = 0.f;       ///< secondes
	float lookPitch = 0.f;  ///< radians
	/// Clip imposé (éditeur, scripts) à `clipTime` ; NONE : idle / walk.
	const RigClip *clip = nullptr;
	float clipTime = 0.f;
	/// Couches de mélange à la place de celles du modèle (éditeur) ; nullptr :
	/// RigModel::layers. Ignorées quand un clip est imposé.
	const std::vector<RigLayer> *layers = nullptr;
	/// Objet tenu en main droite / gauche : le bras se lève vers l'avant.
	bool holdRight = false, holdLeft = false;
	/// Geste du bras droit (coup, pose d'un bloc), 0..1 ; 0 : aucun.
	float swing = 0.f;
};

/// Pose d'une partie : rotation (degrés, x/y/z, repos compris) et décalage
/// du pivot (pixels).
struct RigPartPose {
	math::FVector3 rotation{0.f, 0.f, 0.f};
	math::FVector3 offset{0.f, 0.f, 0.f};
};

/// Pose complète d'une partie : repos, walk / idle sinusoïdaux, regard,
/// clips à images clés (idle permanent, walk pondéré par la marche) puis les
/// couches de mélange — ou le seul clip imposé.
[[nodiscard]] RigPartPose PoseRig(const RigModel &rig, size_t part, const RigPoseInput &input);

/// Position d'un point de la partie `part` (pixels, repère du modèle AU
/// REPOS) une fois le modèle posé par PoseRig — chaîne des parents comprise ;
/// résultat en pixels, repère du modèle (avant RigModel::scale et l'orientation
/// de l'entité).
[[nodiscard]] math::FVector3 RigPointInModel(const RigModel &rig, size_t part, const math::FVector3 &point, const RigPoseInput &input);

/// Rotation d'une partie depuis ses angles (degrés) : x d'abord, puis y,
/// puis z (q = Rz · Ry · Rx) — la convention du rendu des entités.
[[nodiscard]] math::FQuaternion RigRotation(const math::FVector3 &degrees);

/// Rotation d'une partie (degrés, x/y/z) pour une phase de marche, une
/// intensité de marche (0..1), un temps (s) et un tangage de regard (rad).
[[nodiscard]] math::FVector3 PoseRigPart(const RigPart &part, float walkPhase, float walkAmount, float time, float lookPitch);

} // namespace voxel
