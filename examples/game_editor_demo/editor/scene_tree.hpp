#pragma once
/**
 * game_editor::SceneTreePanel — l'arbre de la scène (« Scene Tree »).
 *
 * Une ligne par nœud VISIBLE (les sous-arbres repliés ne coûtent aucun
 * widget : le donjon compte ~450 nœuds, dont une vingtaine à l'écran), avec
 * la flèche de dépliement, l'icône du type, le nom et le type en gris — comme
 * « TorchLight (Light) » dans les maquettes.
 *
 * Interactions :
 *  - en tête, la liste des SCÈNES du projet (repliable) : un clic ouvre la
 *    scène ;
 *  - clic : sélection ; clic sur la flèche : plier/déplier ;
 *  - clic DROIT : menu contextuel (créer un enfant ▸ 3D / Lumière / Logique /
 *    Nœud, dupliquer, supprimer, renommer, cadrer, copier le chemin) ;
 *  - glisser une ligne : un FANTÔME (icône, nom et action prévue) suit la
 *    souris ; sur une ligne, le tiers haut insère AVANT elle, le tiers bas
 *    APRÈS elle (trait d'insertion), le milieu en fait l'ENFANT (cadre) ; un
 *    dépôt impossible (dans son propre sous-arbre) s'affiche en rouge ; près
 *    des bords, la liste défile toute seule ;
 *  - champ de recherche : liste à plat des nœuds dont le nom correspond ;
 *  - F2 (cf. EditorUi) : renommer.
 *
 * Par défaut, seuls les DOSSIERS sont dépliés : c'est l'organisation que
 * l'utilisateur a choisie, alors qu'un mur et ses 100 pierres ne sont qu'un
 * détail de construction. La sélection faite ailleurs (clic dans la vue)
 * déplie ses ancêtres, pour que la ligne sélectionnée soit toujours visible.
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

class SceneTreePanel {
public:
	explicit SceneTreePanel(UiContext &ctx) : m_ctx(ctx) {}

	SceneTreePanel(const SceneTreePanel &) = delete;
	SceneTreePanel &operator=(const SceneTreePanel &) = delete;

	/// Écrit un message dans la barre d'état de l'éditeur.
	std::function<void(const String &)> onStatus;
	/// Place un nouveau nœud créé à la RACINE (devant la caméra) — fourni par
	/// l'éditeur, qui connaît la vue.
	std::function<math::FVector3()> spawnPoint;
	/// Idem pour un nœud 2D (le centre de la vue 2D).
	std::function<math::FVector3()> spawnPoint2D;

	void Build(ecs::Entity page);

	/// Détruit les popups (racines indépendantes, hors du sous-arbre de la
	/// page — sans ça, chaque reconstruction de l'interface en laisserait un
	/// exemplaire fantôme).
	void Teardown();

	void MarkDirty() noexcept { m_dirty = true; }
	[[nodiscard]] bool IsDirty() const noexcept { return m_dirty; }

	/// Reconstruit si besoin, puis fait défiler jusqu'à la sélection.
	void Tick();

	/// Déplie les ancêtres du nœud sélectionné (appelé à chaque changement
	/// de sélection : la ligne sélectionnée doit être visible).
	void RevealSelection();

	void SetExpanded(scene::NodeId id, bool expanded);

	void ExpandAll(bool expanded);

	/// Oublie l'état de déplié (changement de scène : les identifiants ne
	/// désignent plus les mêmes nœuds).
	void ResetExpansion();

	/// Ouvre le menu contextuel pour `target` au point écran (x, y).
	void OpenContextMenu(scene::NodeId target, float x, float y);

	/// Crée un nœud d'après un modèle (cf. NODE_TEMPLATES) sous `parent`,
	/// le sélectionne et le montre.
	Option<scene::NodeId> CreateNode(const String &key, scene::NodeId parent);

	/// Ouvre la boîte de renommage pour `id`.
	void BeginRename(scene::NodeId id);

	/// Rectangle écran de la ligne d'un nœud (NONE s'il n'est pas affiché ou
	/// pas encore mis en page).
	[[nodiscard]] Option<sdl3::FRect> RowRect(scene::NodeId id) const;

	/// Déploie le sous-menu « Créer un nœud enfant » du menu contextuel
	/// ouvert (pilotage par script : le geste de la souris qui le survole).
	bool OpenCreateSubmenu();

	/// Où un nœud glissé sera déposé par rapport à la ligne survolée.
	enum class DropZone : uint8_t { NONE, BEFORE, AFTER, INSIDE, INVALID };

	/// Zone de dépôt de `dragged` sur la ligne `target` quand le pointeur est
	/// à l'ordonnée `pointerY` (repère de l'interface). Public : les tests.
	[[nodiscard]] DropZone DropZoneAt(scene::NodeId dragged, scene::NodeId target, float pointerY) const;

	/// Applique un dépôt (reparentage et/ou rang parmi les frères) ; rend
	/// vrai si l'arbre a changé.
	bool ApplyDrop(scene::NodeId dragged, scene::NodeId target, DropZone zone);

	/// Glissé en cours : nœud déplacé, ligne survolée, zone (cf. le retour
	/// visuel). NONE hors glissé.
	[[nodiscard]] DropZone CurrentDropZone() const noexcept { return m_dropZone; }

	/// Nombre de lignes affichées (tests, rapport).
	[[nodiscard]] size_t VisibleRowCount() const noexcept { return m_rows.size(); }
	[[nodiscard]] bool IsExpanded(scene::NodeId id) const;

private:
	struct RowRef {
		ecs::Entity entity;
		scene::NodeId node;
		int depth = 0;
	};

	void Status(const String &text);

	/// État de dépli : explicite s'il a été choisi, sinon « dossier = déplié ».
	[[nodiscard]] bool ExpandedByDefault(scene::NodeId id, const scene::Node &node) const;

	void Refresh();

	void AddSubtree(const SceneDesc &scene, scene::NodeId id, int depth, int &index);

	void AddRow(const SceneDesc &scene, scene::NodeId id, int depth, int index, bool flat);

	void OnDrop(uint32_t draggedIndex, scene::NodeId target);

	/// Liste des scènes du projet (en tête du panneau).
	void RefreshSceneStrip();

	/// Fantôme et indicateurs d'insertion (widgets `Fixed`, transparents au
	/// pointeur, créés une fois).
	void BuildDragFeedback();

	/// Suit le glissé en cours : fantôme, trait/cadre, défilement près des bords.
	void UpdateDragFeedback();

	void HideDragFeedback();

	[[nodiscard]] const RowRef *RowOf(scene::NodeId id) const;

	/// Position du pointeur (repère de l'interface = celui de la fenêtre).
	[[nodiscard]] static sdl3::FPoint Pointer();

	/// Fait défiler la liste pour que la ligne sélectionnée soit visible —
	/// une fois la mise en page faite (d'où l'appel depuis Tick).
	void ScrollToSelection();

	// ── Menus ────────────────────────────────────────────────────────────────

	ecs::Entity Item(ecs::Entity menu, const char *text, const char *shortcut, std::function<void()> action);

	/// Sous-menus de création, sous `menu` : « 3D ▸ », « Lumière ▸ »,
	/// « Logique ▸ », puis les entrées de la catégorie « Nœud » à plat.
	void AddCreateEntries(ecs::Entity menu);

	void BuildMenus();

	void BuildRenameDialog();

	void ApplyRename(const String &text);

	static constexpr size_t MAX_SEARCH_RESULTS = 200;

	UiContext &m_ctx;
	ecs::Entity m_list{}, m_search{}, m_addButton{}, m_optionsButton{};
	ecs::Entity m_contextMenu{}, m_createMenu{}, m_optionsMenu{};
	ecs::Entity m_createSubmenu{}, m_createTrigger{};
	ecs::Entity m_renameModal{}, m_renameInput{};
	std::vector<ecs::Entity> m_popups;
	std::vector<ecs::Entity> m_nodeOnlyItems;
	std::vector<RowRef> m_rows;
	ecs::Entity m_sceneStrip{}, m_sceneRows{};
	bool m_scenesExpanded = true;
	/// Retour visuel du glisser-déposer.
	ecs::Entity m_ghost{}, m_ghostIcon{}, m_ghostLabel{}, m_ghostAction{};
	ecs::Entity m_dropLine{}, m_dropBox{}, m_dropBoxInvalid{};
	scene::NodeId m_dragSource, m_dropTarget;
	DropZone m_dropZone = DropZone::NONE;
	std::unordered_map<scene::NodeId, bool> m_expanded;
	/// Classes des objets de script qui portent chaque nœud (relevées une
	/// fois par rafraîchissement, cf. Runtime::LiveScriptObjects).
	std::unordered_map<scene::NodeId, String> m_scriptClasses;
	scene::NodeId m_contextTarget;
	scene::NodeId m_renameTarget;
	String m_filter;
	bool m_dirty = true;
	bool m_scrollToSelection = false;
};

} // namespace game_editor
