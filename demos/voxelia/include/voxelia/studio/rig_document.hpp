#pragma once
/**
 * Document de l'atelier de formes — un modèle ARTICULÉ du moteur (définition
 * « model », format « rig », cf. voxel/rig.hpp) tel qu'écrit dans son mod :
 * hiérarchie de parties (pivot, rotation de repos, boîtes à UV), mouvements
 * walk / idle et animations à images clés.
 *
 * Toutes les opérations écrivent le JSON du document (une étape d'annulation
 * chacune, cf. DefinitionDocument) et gardent ses invariants :
 *  - un parent précède toujours ses enfants dans « parts » (exigé par
 *    RigFromJson) : réordonné après chaque changement de hiérarchie ;
 *  - pas de cycle (une partie ne devient pas l'enfant de sa descendance) ;
 *  - un renommage suit dans les « parent » et dans les pistes d'animation ;
 *    une partie supprimée confie ses enfants à son parent et perd ses pistes.
 *
 * Le moteur reste la référence : Build() passe par RigFromJson, l'échantillonnage
 * des courbes par SampleRigTrack / PoseRig, le dépliage par RigBoxUv, le
 * maillage exporté par BuildRigPart.
 */
#include <array>
#include <vector>

#include <voxel/rig.hpp>

#include "definition_document.hpp"

namespace voxelia::studio {

struct RigPartInfo {
	String name, parent;
	int depth = 0;       ///< 0 : racine
	size_t boxes = 0;
	bool hasMesh = false; ///< partie faite d'un maillage (non éditable en boîtes)
};

struct RigDiagnostic {
	enum class Severity : uint8_t { Warning, Error } severity = Severity::Warning;
	String message;
	String part; ///< partie concernée (vide : modèle entier)
};

class RigDocument : public DefinitionDocument {
public:
	RigDocument() = default;
	/// Modèle « rig » existant ; NONE s'il est inconnu, sans source, ou d'un
	/// autre format (maillage .obj).
	[[nodiscard]] static Option<RigDocument> FromModel(const voxel::ContentRegistry &content, const String &modelId);
	/// Nouveau modèle : une partie « body » d'une boîte 8×8×8.
	[[nodiscard]] static RigDocument New(const String &id, const String &mod, const String &baseDir, const String &file);

	/// Le modèle tel que le moteur le lira.
	[[nodiscard]] Result<voxel::RigModel, String> Build() const;
	[[nodiscard]] std::vector<RigDiagnostic> Validate() const;

	// ── Hiérarchie ──────────────────────────────────────────────────────────
	[[nodiscard]] std::vector<RigPartInfo> Parts() const;
	[[nodiscard]] bool HasPart(const String &name) const;
	Result<bool, String> AddPart(const String &name, const String &parent);
	Result<bool, String> RemovePart(const String &name);
	Result<bool, String> RenamePart(const String &from, const String &to);
	/// `parent` vide : la partie devient racine.
	Result<bool, String> Reparent(const String &name, const String &parent);
	Result<bool, String> DuplicatePart(const String &name, const String &copy);

	// ── Transformations (pixels, degrés) ────────────────────────────────────
	[[nodiscard]] math::FVector3 Pivot(const String &part) const;
	Result<bool, String> SetPivot(const String &part, const math::FVector3 &pivot);
	/// Rotation de repos (degrés : x, puis y, puis z — RigRotation).
	[[nodiscard]] math::FVector3 Rotation(const String &part) const;
	Result<bool, String> SetRotation(const String &part, const math::FVector3 &degrees);

	// ── Boîtes ──────────────────────────────────────────────────────────────
	[[nodiscard]] std::vector<voxel::RigBox> Boxes(const String &part) const;
	Result<bool, String> AddBox(const String &part, const math::FVector3 &from, const math::FVector3 &size, int u, int v);
	Result<bool, String> SetBox(const String &part, size_t index, const voxel::RigBox &box);
	Result<bool, String> RemoveBox(const String &part, size_t index);
	/// Copie miroir (symétrie x = 0) : boîte retournée, dépliage miroir.
	Result<bool, String> MirrorBox(const String &part, size_t index);
	/// Dépliage de la boîte (moteur : RigBoxUv).
	[[nodiscard]] std::array<voxel::RigUvFace, 6> UvLayout(const String &part, size_t index) const;

	// ── Animation ───────────────────────────────────────────────────────────
	[[nodiscard]] std::vector<String> Clips() const;
	Result<bool, String> AddClip(const String &name, float length, bool loop);
	Result<bool, String> RemoveClip(const String &name);
	/// Clé (remplace celle du même temps, à 1 ms près).
	Result<bool, String> SetKey(const String &clip, const String &part, voxel::RigChannel channel, float time,
								const math::FVector3 &value, voxel::RigInterp interp = voxel::RigInterp::LINEAR);
	Result<bool, String> RemoveKey(const String &clip, const String &part, voxel::RigChannel channel, float time);
	/// Clés d'une piste (vide si elle n'existe pas).
	[[nodiscard]] std::vector<voxel::RigKey> Keys(const String &clip, const String &part, voxel::RigChannel channel) const;
	/// Courbe d'une piste échantillonnée sur la durée du clip (graphe).
	[[nodiscard]] std::vector<math::FVector3> SampleCurve(const String &clip, const String &part, voxel::RigChannel channel,
														  size_t samples) const;

	/// Poignées de Bézier d'une clé existante (NONE : automatique) ; la clé
	/// passe en interpolation « bezier ».
	Result<bool, String> SetKeyHandles(const String &clip, const String &part, voxel::RigChannel channel, float time,
									   Option<voxel::RigHandle> in, Option<voxel::RigHandle> out);
	/// Lissage automatique d'une piste (tangentes de Catmull-Rom, extrémités
	/// à plat) : toutes ses clés passent en « bezier » à poignées calculées.
	Result<bool, String> AutoSmooth(const String &clip, const String &part, voxel::RigChannel channel);

	// ── Mélange de clips (couches) ──────────────────────────────────────────
	[[nodiscard]] std::vector<voxel::RigLayer> Layers() const;
	Result<bool, String> AddLayer(const voxel::RigLayer &layer);
	Result<bool, String> SetLayer(size_t index, const voxel::RigLayer &layer);
	Result<bool, String> RemoveLayer(size_t index);

	// ── glTF ────────────────────────────────────────────────────────────────
	/// Export glTF 2.0 : un nœud par partie (pivot, rotation de repos), un
	/// maillage par partie, chaque clip en animation (rotation et translation
	/// échantillonnées à `fps` images par seconde, donc Bézier comprises).
	/// `skinImage` (facultatif) : image copiée à côté, texture du matériau ;
	/// `skinWidth` × `skinHeight` : sa taille RÉELLE — les UV sont construites
	/// pour elle, comme le jeu (un skin 64×64 sur un modèle déclaré 64×32
	/// active uv64 / only64) ; 0 : la taille déclarée.
	Result<size_t, String> ExportGltf(const String &path, const String &skinImage = String(), float fps = 30.f, int skinWidth = 0,
									  int skinHeight = 0) const;
	/// Import : chaque nœud du document devient une partie (pivot, rotation
	/// de repos, parent) maillée par « model » + « node » ; les parties
	/// existantes de même nom sont remplacées. Rend le nombre de parties.
	Result<size_t, String> ImportGltf(const String &path);

	/// Maillage posé (repos si `clip` est vide, sinon le clip à `time`) écrit
	/// en Wavefront OBJ (UV comprises, v vers le haut) — Blender, Blockbench.
	Result<size_t, String> ExportObj(const String &path, const String &clip = String(), float time = 0.f) const;

private:
	[[nodiscard]] dataio::NodePtr PartNode(const String &name) const;
	[[nodiscard]] bool IsDescendant(const String &part, const String &ancestor) const;
};

[[nodiscard]] const char *RigChannelId(voxel::RigChannel channel) noexcept; ///< "rotation", "position"
[[nodiscard]] const char *RigInterpId(voxel::RigInterp interp) noexcept;    ///< "linear", "step", "smooth", "bezier"
/// Angles (degrés, convention du rig : x puis y puis z) d'une rotation.
[[nodiscard]] math::FVector3 RigEulerFromQuaternion(const math::FQuaternion &q);

} // namespace voxelia::studio
