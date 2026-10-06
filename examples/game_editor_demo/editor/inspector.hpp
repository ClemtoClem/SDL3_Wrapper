#pragma once
/**
 * game_editor — l'inspecteur et la bibliothèque (colonne de droite).
 *
 * `InspectorPanel` montre le nœud sélectionné comme dans les maquettes :
 * son nom (éditable), puis une SECTION repliable par composant — Transform,
 * Maillage, Matériau, Lumière, Ombres, Caméra, Déclencheur, Physique,
 * Script, propriétés libres — chacune avec son menu ⋮ (réinitialiser,
 * retirer, modifier en JSON), et « Ajouter un composant » en bas.
 *
 * ── Ne pas se reconstruire sous la souris ────────────────────────────────
 * Toute modification du nœud notifie l'interface (Runtime::onObjectChanged),
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

#include "core/core.hpp"
#include "ui/ui.hpp"

#include "kit.hpp"
#include "../document/project.hpp"
#include "../engine/runtime.hpp"

namespace game_editor {

/// Ce que l'inspecteur demande à l'éditeur (ouvrir un document).
struct InspectorActions {
	std::function<void(const String &script)> openScript;
	/// Édition JSON d'un composant (`type` vide : propriétés libres du nœud).
	std::function<void(scene::NodeId, const String &type)> editJson;
	std::function<void(const String &)> status;
};

class InspectorPanel {
public:
	InspectorPanel(UiContext &ctx, InspectorActions actions) : m_ctx(ctx), m_actions(std::move(actions)) {}
	InspectorPanel(const InspectorPanel &) = delete;
	InspectorPanel &operator=(const InspectorPanel &) = delete;

	void Build(ecs::Entity page);

	void Teardown();

	/// À brancher sur Runtime::onObjectChanged / onSelectionChanged.
	void OnObjectChanged();
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
	ecs::Entity Section(const char *title, const String &component);

	void AddMenuItem(ecs::Entity menu, const char *text, std::function<void()> action);

	/// Le clic sur `button` ouvre `menu` sous lui.
	void AttachMenu(ecs::Entity button, ecs::Entity menu);

	// ── Sections par composant ───────────────────────────────────────────────

	void BuildVisualSections(kit::PropertyRows &rows, scene::NodeId id, const VisualDesc &visual);

	void BuildLightSections(kit::PropertyRows &rows, scene::NodeId id, const LightDesc &light);

	void BuildCameraSection(kit::PropertyRows &rows, scene::NodeId id, const CameraNodeDesc &camera);

	void BuildTriggerSection(kit::PropertyRows &rows, scene::NodeId id, const TriggerDesc &trigger);

	void BuildPhysicsSection(kit::PropertyRows &rows, scene::NodeId id, const PhysicsDesc &physics);

	void BuildScriptSection(kit::PropertyRows &rows, scene::NodeId id, const ScriptRef &ref);
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

public:
	[[nodiscard]] static const char *ComponentLabel(const String &type);

	[[nodiscard]] static const char *ShapeLabel(ShapeKind shape) noexcept;

private:
	static constexpr ShapeKind EDITABLE_SHAPES[] = {ShapeKind::BOX,   ShapeKind::SPHERE,      ShapeKind::CYLINDER,
													ShapeKind::CONE,  ShapeKind::TORUS,       ShapeKind::PLANE,
													ShapeKind::ICOSAHEDRON, ShapeKind::TORUS_KNOT};

	UiContext &m_ctx;
	InspectorActions m_actions;
	ecs::Entity m_page{};
	std::vector<ecs::Entity> m_popups;
	std::vector<std::pair<String, ecs::Entity>> m_sections;
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

	void BuildMaterials(ecs::Entity page);

	void BuildScripts(ecs::Entity page);

	void RefreshScripts();

	void BuildWorld(ecs::Entity page);

	void RefreshWorld();

	void Teardown();

private:
	void Apply(const MaterialPreset &preset);

	UiContext &m_ctx;
	InspectorActions m_actions;
	ecs::Entity m_materialsPage{}, m_scriptsPage{}, m_worldPage{};
	std::vector<ecs::Entity> m_popups;
	int m_scriptRowIndex = 0;
};

} // namespace game_editor
