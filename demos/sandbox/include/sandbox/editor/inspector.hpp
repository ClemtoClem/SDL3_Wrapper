#pragma once
/**
 * sandbox — l'inspecteur et la bibliothèque (colonne de droite).
 *
 * `InspectorPanel` montre le nœud sélectionné comme dans les maquettes :
 * son nom (éditable), puis une SECTION repliable par composant — Transform,
 * Maillage, Matériau, Lumière, Ombres, Caméra, Déclencheur, Physique,
 * Script, propriétés libres — chacune avec son menu ⋮ (réinitialiser,
 * retirer, modifier en JSON), et « Ajouter un composant » en bas.
 *
 * ── Ne pas se reconstruire sous la souris ────────────────────────────────
 * Toute modification du nœud notifie l'interface (Runtime::onNodeChanged),
 * et l'inspecteur se reconstruit pour montrer la nouvelle valeur. Mais une
 * valeur tirée à la souris change à CHAQUE mouvement : reconstruire à ce
 * moment détruirait le champ qu'on est en train de tirer. Les éditions
 * CONTINUES (glisser, curseur, couleur) passent donc par `Live()`, qui
 * signale « c'est moi » et fait taire la reconstruction ; les éditions
 * DISCRÈTES (liste, case à cocher) reconstruisent, parce qu'elles peuvent
 * faire apparaître d'autres champs (un projecteur a un angle, une lumière
 * ponctuelle non).
 *
 * `LibraryPanel` (sous l'inspecteur) : matériaux prêts à appliquer, scripts
 * de la bibliothèque, réglages du monde.
 */
#include <functional>
#include <unordered_map>
#include <vector>

#include <core/core.hpp>
#include <ui/ui.hpp>

#include "kit.hpp"
#include <sandbox/document/project.hpp>
#include <sandbox/engine/runtime.hpp>

namespace sandbox {

/// Ce que l'inspecteur demande à l'éditeur (ouvrir un document).
struct InspectorActions {
	std::function<void(const String &script)> openScript;
	/// Édition JSON d'un composant (`type` vide : propriétés libres du nœud).
	std::function<void(scene::NodeId, const String &type)> editJson;
	std::function<void(const String &)> status;
	/// Modélisation (cf. EditorUi::BeginModeling) : éditer, terminer, exporter.
	std::function<void(scene::NodeId)> beginModeling;
	std::function<void()> endModeling;
	std::function<void(const char *extension)> exportMesh;
};

class InspectorPanel {
public:
	InspectorPanel(UiContext &ctx, InspectorActions actions) : m_ctx(ctx), m_actions(std::move(actions)) {}
	InspectorPanel(const InspectorPanel &) = delete;
	InspectorPanel &operator=(const InspectorPanel &) = delete;

	void Build(ui::Widget page);

	void Teardown();

	/// À brancher sur Runtime::onNodeChanged / onSelectionChanged.
	void OnNodeChanged();
	void MarkDirty() noexcept { m_dirty = true; }

	void Tick();

	/// Déplie une section et la fait apparaître (pilotage par script :
	/// `editor.open_panel("inspector", 1)` → Matériau).
	void FocusSection(const String &title);

	[[nodiscard]] static const char *SectionForTab(int tab) noexcept;

private:
	// ── Garde des éditions continues (cf. en-tête) ──────────────────────────

	template <typename F> auto Live(F edit) {
		return [this, edit](auto value) {
			m_localEdit = true;
			edit(value);
			m_localEdit = false;
		};
	}

	void ClearPopups();

	void Status(const String &text);

	// ── Construction ─────────────────────────────────────────────────────────

	void Refresh();

	/// En-tête : icône du type + nom éditable, puis « Propriétés ⋮ ».
	void BuildHeader(const SceneDesc &scene, const scene::Node &node);

	/// Section repliable. `component` non vide : section d'un composant, qui
	/// reçoit son menu ⋮ (réinitialiser / retirer / JSON).
	ui::Widget Section(const char *title, const String &component);

	void AddMenuItem(ui::Widget menu, const char *text, std::function<void()> action);

	/// Le clic sur `button` ouvre `menu` sous lui.
	void AttachMenu(ui::Widget button, ui::Widget menu);

	// ── Sections par composant ───────────────────────────────────────────────

	void BuildVisualSections(kit::PropertyRows &rows, scene::NodeId id, const VisualDesc &visual);
	/// Maillage modélisé : fichier, statistiques, Modéliser / Terminer, export.
	void BuildModeledMeshRows(kit::PropertyRows &rows, ui::Widget section, scene::NodeId id, const VisualDesc &visual);

	void BuildLightSections(kit::PropertyRows &rows, scene::NodeId id, const LightDesc &light);

	void BuildCameraSection(kit::PropertyRows &rows, scene::NodeId id, const CameraNodeDesc &camera);

	void BuildTriggerSection(kit::PropertyRows &rows, scene::NodeId id, const TriggerDesc &trigger);
	void BuildAudioSourceSection(kit::PropertyRows &rows, scene::NodeId id, const AudioSourceDesc &source);

	void BuildPhysicsSection(kit::PropertyRows &rows, scene::NodeId id, const PhysicsDesc &physics);

	void BuildScriptSection(kit::PropertyRows &rows, scene::NodeId id, const ScriptRef &ref);
	/// Instance d'un objet réutilisable : objet source, contenu généré,
	/// « Ouvrir l'objet », « Rendre indépendant ».
	void BuildInstanceSection(kit::PropertyRows &rows, const SceneDesc &scene, scene::NodeId id);
	/// Objets de script vivants qui portent le nœud (champs, destruction).
	void BuildScriptObjects(kit::PropertyRows &rows, scene::NodeId id);

	/// Apparence 2D (forme, couleur, taille, texte, image, contour, z).
	void BuildCanvasItemSection(kit::PropertyRows &rows, scene::NodeId id, const CanvasItemDesc &item);

	void BuildCamera2DSection(kit::PropertyRows &rows, scene::NodeId id, const Camera2DDesc &camera);

	void MutateCanvasItem(scene::NodeId id, const std::function<void(CanvasItemDesc &)> &change);

	/// Propriétés libres du nœud (lues par les scripts via node.prop).
	void BuildCustomProperties(const scene::Node &node);

	/// « Ajouter un composant » : les composants que le nœud n'a pas encore.
	void BuildAddComponent(const scene::Node &node);

	void ScrollToPendingSection();

	// ── Mutations (lecture de l'état COURANT, pas d'une copie périmée) ──────

	void MutateVisual(scene::NodeId id, const std::function<void(VisualDesc &)> &change);

	void MutateMaterial(scene::NodeId id, const std::function<void(MaterialDesc &)> &change);

	void MutateLight(scene::NodeId id, const std::function<void(LightDesc &)> &change);

	void MutatePhysics(scene::NodeId id, const std::function<void(PhysicsDesc &)> &change);

	[[nodiscard]] CameraNodeDesc CurrentCamera(scene::NodeId id);

	[[nodiscard]] TriggerDesc CurrentTrigger(scene::NodeId id);
	void MutateAudioSource(scene::NodeId id, const std::function<void(AudioSourceDesc &)> &change);

public:
	[[nodiscard]] static const char *ComponentLabel(const String &type);

	[[nodiscard]] static const char *ShapeLabel(ShapeKind shape) noexcept;

private:
	static constexpr ShapeKind EDITABLE_SHAPES[] = {ShapeKind::BOX,   ShapeKind::SPHERE,      ShapeKind::CYLINDER,
													ShapeKind::CONE,  ShapeKind::TORUS,       ShapeKind::PLANE,
													ShapeKind::ICOSAHEDRON, ShapeKind::TORUS_KNOT};

	UiContext &m_ctx;
	InspectorActions m_actions;
	ui::Widget m_page{};
	std::vector<ui::Widget> m_popups;
	std::vector<std::pair<String, ui::Widget>> m_sections;
	std::unordered_map<String, bool> m_open;
	String m_pendingSection;
	bool m_dirty = true;
	bool m_localEdit = false;
};

// ============================================================================
// LibraryPanel — matériaux, scripts, monde
// ============================================================================

/// Préréglage de matériau (onglet « Matériaux »).
struct MaterialPreset {
	const char *name;
	MaterialDesc material;
};

[[nodiscard]] std::vector<MaterialPreset> MaterialPresets();

class LibraryPanel {
public:
	LibraryPanel(UiContext &ctx, InspectorActions actions) : m_ctx(ctx), m_actions(std::move(actions)) {}

	void BuildMaterials(ui::Widget page);

	void BuildScripts(ui::Widget page);

	void RefreshScripts();

	void BuildWorld(ui::Widget page);

	void RefreshWorld();

	void Teardown();

private:
	void Apply(const MaterialPreset &preset);

	UiContext &m_ctx;
	InspectorActions m_actions;
	ui::Widget m_materialsPage{}, m_scriptsPage{}, m_worldPage{};
	std::vector<ui::Widget> m_popups;
	int m_scriptRowIndex = 0;
};

} // namespace sandbox
