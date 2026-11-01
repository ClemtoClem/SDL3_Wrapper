#pragma once
/**
 * Atelier de formes du studio (panneau) — modèles articulés des entités
 * (RigDocument) : hiérarchie de parties, pivots et rotations, boîtes et leur
 * dépliage UV, animations à images clés avec timeline et graphe de courbes,
 * aperçu 3D animé, export OBJ, rechargement à chaud dans la partie.
 *
 * L'aperçu est une scène render3d PROPRE au panneau (pas celle de la partie),
 * construite par les fonctions du moteur (BuildRigPart, PoseRig, RigRotation) :
 * ce qu'on y voit est ce que le jeu affichera.
 *
 * Fil principal uniquement, quand la simulation ne tourne pas (cf. Studio).
 */
#include <functional>
#include <memory>
#include <vector>

#include <render3d/object3d.hpp>
#include <render3d/shape.hpp>
#include <ui/ui.hpp>
#include <voxel/game.hpp>

#include "console.hpp"
#include "report.hpp"
#include "rig_document.hpp"

namespace voxelia {

class Shell;

namespace studio {

class ModelEditor {
public:
	ModelEditor(Shell &shell, StudioReport *report);
	~ModelEditor();
	ModelEditor(const ModelEditor &) = delete;
	ModelEditor &operator=(const ModelEditor &) = delete;

	void Build(ui::Widget parent);
	/// Liste des modèles articulés (onglet du dock gauche).
	void BuildCatalog(ui::Widget parent);
	void Update(float dt);
	/// Commandes de la console (`studio.model …`).
	void RegisterCommands(ConsoleModel &console);

	// ── Actions ─────────────────────────────────────────────────────────────
	CommandResult Open(const String &modelId);
	CommandResult New(const String &id, const String &mod);
	CommandResult SelectPart(const String &part);
	CommandResult Apply();
	CommandResult Save();
	CommandResult Export(const String &path);
	CommandResult ExportGltf(const String &path);
	CommandResult ImportGltf(const String &path);
	CommandResult Undo();
	CommandResult Redo();
	CommandResult Play(const String &clip);
	CommandResult SetTime(float time);

	[[nodiscard]] const RigDocument *Document() const noexcept { return m_doc.IsSome() ? &m_doc.Value() : nullptr; }
	[[nodiscard]] const String &SelectedPart() const noexcept { return m_part; }
	[[nodiscard]] float Time() const noexcept { return m_time; }
	[[nodiscard]] size_t PreviewParts() const noexcept { return m_previewNodes.size(); }
	std::function<void()> onDocumentOpened{};

private:
	[[nodiscard]] voxel::Game *Game();
	/// Résultat d'une opération du document : rafraîchit, ou rend l'erreur.
	CommandResult Done(Result<bool, String> result, const char *feature, const String &ok);
	void Changed();
	void Visit(const char *feature, FeatureStatus status = FeatureStatus::Ok, String detail = String());
	void Gesture(); ///< ouvre / prolonge un geste continu (un glissé = une étape)
	void CloseGesture();

	void RebuildAll();
	void RebuildCatalog();
	void RebuildParts();
	void RebuildBoxes();
	void RebuildAnimation();
	void RebuildDiagnostics();
	void RebuildPreview();
	void PosePreview();

	Shell &m_shell;
	StudioReport *m_report;
	Option<RigDocument> m_doc = NONE;
	String m_part, m_clip;
	voxel::RigChannel m_channel = voxel::RigChannel::ROTATION;
	voxel::RigInterp m_interp = voxel::RigInterp::LINEAR;
	math::FVector3 m_keyValue{0.f, 0.f, 0.f};
	Option<float> m_selectedKey = NONE;      ///< clé dont on édite les poignées
	voxel::RigHandle m_handleIn{}, m_handleOut{};
	bool m_mixPreview = false;               ///< aperçu : pose du jeu (idle + couches)
	float m_time = 0.f;
	bool m_playing = false;
	bool m_gesture = false;
	float m_gestureTimer = 0.f;
	uint64_t m_version = 0, m_shownVersion = ~0ull, m_previewVersion = ~0ull;
	float m_orbit = 0.6f;

	// Aperçu (scène propre au panneau).
	render3d::Object3D m_previewRoot;
	std::vector<render3d::Object3D *> m_previewNodes;
	std::shared_ptr<const voxel::RigModel> m_previewRig;
	std::unique_ptr<sdl3::GpuTexture> m_skin;
	String m_skinId;
	int m_skinWidth = 0, m_skinHeight = 0; ///< taille réelle du skin (UV de BuildRigPart)
	float m_previewHeight = 2.f;

	ui::Widget m_gltfPath, m_tabs;
	ui::Widget m_title, m_viewport, m_diagnostics, m_parts, m_partForm, m_boxes, m_uv, m_animation, m_curves, m_json,
		m_jsonStatus, m_catalog;
};

} // namespace studio
} // namespace voxelia
