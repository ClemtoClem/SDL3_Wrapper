#pragma once
/**
 * game_editor — navigateur de ressources (« Asset Manager » des maquettes).
 *
 * Deux moitiés, séparées exprès :
 *
 *  - `AssetBrowserModel` : la NAVIGATION, sans aucun widget — emplacement
 *    courant, historique précédent/suivant, remontée, fil d'Ariane, filtre,
 *    liste des entrées. Testée sans fenêtre.
 *  - `AssetBrowserPanel` : l'AFFICHAGE — barre de navigation, arbre des
 *    dossiers, grille de vignettes, zoom.
 *
 * L'arborescence mêle deux sources, comme dans tout moteur : ce qui vit DANS
 * le projet (ses scènes, sa bibliothèque de scripts — il n'y a pas de fichier
 * sur disque pour elles, le projet est un seul JSON) et ce qui vit sur le
 * DISQUE (`assets/` : modèles glTF, textures, sons, polices, shaders). Les
 * deux se parcourent de la même façon ; seule l'action d'ouverture diffère.
 */
#include <algorithm>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "core/core.hpp"
#include "render3d/object3d.hpp"
#include "render3d/shape.hpp"
#include "sdl3/filesystem.hpp"
#include "sdl3/image.hpp"
#include "ui/ui.hpp"

#include "kit.hpp"
#include "../document/project.hpp"
#include "../engine/runtime.hpp"

namespace game_editor {

enum class AssetKind : uint8_t { FOLDER, SCENE, SCRIPT, MODEL, TEXTURE, SOUND, FONT, SHADER, DATA, OTHER };

[[nodiscard]] const char *AssetKindLabel(AssetKind kind) noexcept;

/// Une entrée du navigateur. `location` est l'adresse que comprend le
/// modèle : `projet:`, `projet:/scenes`, `projet:/scripts/<nom>`, ou un
/// chemin disque (`assets/models/knight/KnightCharacter.gltf`).
struct AssetEntry {
	String name;
	String location;
	AssetKind kind = AssetKind::OTHER;
	String detail; ///< une ligne : « 12 objets », « 48 Ko », « compile »…
	/// Script qui ne compile pas (la vignette porte alors une croix rouge).
	bool broken = false;
};

class AssetBrowserModel {
public:
	static constexpr const char *ROOT = "projet:";
	static constexpr const char *SCENES = "projet:/scenes";
	static constexpr const char *SCRIPTS = "projet:/scripts";

	/// `diskRoot` : les ressources (`assets/`) ; `savesRoot` : les
	/// sauvegardes de l'éditeur (projets, scènes, scripts), montrées dans un
	/// dossier « Sauvegardes » à part — vide : pas de tel dossier.
	/// `savesLabel` : nom affiché de ce second dossier (« Dossier du projet »
	/// quand c'est celui du projet ouvert).
	explicit AssetBrowserModel(const Project *project = nullptr, String diskRoot = String("assets"),
							   String savesRoot = String(), String savesLabel = String("Sauvegardes"))
		: m_project(project), m_diskRoot(std::move(diskRoot)), m_savesRoot(std::move(savesRoot)),
		  m_savesLabel(std::move(savesLabel)), m_location(ROOT) {}

	void SetProject(const Project *project) noexcept { m_project = project; }
	[[nodiscard]] const String &Location() const noexcept { return m_location; }
	[[nodiscard]] const String &DiskRoot() const noexcept { return m_diskRoot; }
	[[nodiscard]] const String &SavesRoot() const noexcept { return m_savesRoot; }

	// ── Navigation ───────────────────────────────────────────────────────────

	/// Va à `location` (historique : le « suivant » est abandonné, comme dans
	/// un navigateur). Refusé si l'emplacement n'est pas un dossier connu.
	bool Navigate(const String &location);

	bool Back();

	bool Forward();

	bool Up();

	[[nodiscard]] bool CanBack() const noexcept { return !m_back.empty(); }
	[[nodiscard]] bool CanForward() const noexcept { return !m_forward.empty(); }
	[[nodiscard]] bool CanUp() const noexcept { return m_location != ROOT; }

	/// Dossier parent. Les dossiers de premier niveau du disque (`assets/x`)
	/// remontent à la racine du projet, dont ils sont des enfants à l'écran.
	[[nodiscard]] String Parent(const String &location) const;

	/// Fil d'Ariane : (libellé, emplacement) du plus haut au courant.
	[[nodiscard]] std::vector<std::pair<String, String>> Breadcrumb() const;

	// ── Contenu ──────────────────────────────────────────────────────────────

	/// Entrées de l'emplacement courant : dossiers d'abord, puis le reste,
	/// chacun par ordre alphabétique ; `filter` (insensible à la casse)
	/// garde les noms qui le contiennent.
	[[nodiscard]] std::vector<AssetEntry> Entries(const String &filter = String()) const;

	[[nodiscard]] std::vector<AssetEntry> List(const String &location, const String &filter = String()) const;

	[[nodiscard]] bool IsFolder(const String &location) const;

	/// Nature d'un fichier d'après son extension.
	[[nodiscard]] static AssetKind KindOf(const String &fileName);

	/// Dossiers ignorés sous la racine des ressources : des données propres à
	/// d'autres démos du dépôt (émulateur), pas des ressources de jeu.
	[[nodiscard]] static bool IsHiddenFolder(const String &name);

private:
	[[nodiscard]] static size_t LastSlash(const String &path);

	[[nodiscard]] std::vector<AssetEntry> ListDisk(const String &directory) const;

	const Project *m_project = nullptr;
	String m_diskRoot;
	String m_savesRoot;
	String m_savesLabel;
	String m_location;
	std::vector<String> m_back, m_forward;
};

// ============================================================================
// AssetBrowserPanel
// ============================================================================

/// Actions qu'une entrée déclenche à l'ouverture — fournies par l'éditeur,
/// qui seul sait ouvrir un document ou changer de scène.
struct AssetActions {
	std::function<void(const String &script)> openScript;         ///< nom de la bibliothèque
	std::function<void(const String &scene)> openSceneScript;     ///< script de jeu d'une scène
	std::function<void(const String &path)> openFile;             ///< fichier texte du disque
	std::function<void(const String &path)> importModel;         ///< modèle glTF
	std::function<void(const String &scene)> openScene;
	std::function<void(const String &path)> instantiateScene;    ///< fichier .scene
	std::function<void(const String &text)> status;
};

class AssetBrowserPanel {
public:
	AssetBrowserPanel(UiContext &ctx, AssetActions actions) : m_ctx(ctx), m_actions(std::move(actions)) {}

	~AssetBrowserPanel() { ClearThumbnails(); }
	AssetBrowserPanel(const AssetBrowserPanel &) = delete;
	AssetBrowserPanel &operator=(const AssetBrowserPanel &) = delete;

	void Build(ecs::Entity page);

	/// Détruit les ressources hors de l'arbre d'interface (vignettes 3D).
	void Teardown();

	void MarkDirty() noexcept { m_dirty = true; }

	/// Reconstruit si besoin — aussi quand la grille a changé de largeur (le
	/// nombre de colonnes en dépend).
	void Tick(float dt);

	[[nodiscard]] AssetBrowserModel &Model() noexcept { return m_model; }

	/// Racine des ressources sur le disque (cf. `--assets-dir`).
	void SetRoots(const String &assets, const String &saves, const String &savesLabel = String("Sauvegardes"));
	[[nodiscard]] const std::vector<AssetEntry> &Entries() const noexcept { return m_entries; }

	/// Sélectionne l'entrée nommée `name` du dossier courant (comme un clic).
	bool SelectByName(const String &name);

	/// Ouvre une entrée par son index dans `Entries()` (double clic, dépôt
	/// dans la vue, pilotage par script).
	void Activate(size_t index);

	static constexpr const char *NEW_SCRIPT_TEMPLATE =
		"# Nouveau script de nœud.\n"
		"# Attachez-le à un nœud (inspecteur > Script) : il reçoit ce nœud en `self`.\n"
		"\n"
		"fn on_start(self) {\n"
		"    editor.log(\"Démarrage de \" .. self)\n"
		"}\n"
		"\n"
		"fn on_update(self, dt) {\n"
		"}\n";

private:
	static constexpr float MIN_TILE = 48.f;
	static constexpr float MAX_TILE = 132.f;
	static constexpr float TILE_GAP = 10.f;

	/// Colonnes de vignettes qui tiennent dans `width` : marges de la grille
	/// (2 × 8 px) et barre de défilement verticale (~14 px) déduites — sans
	/// elles, la dernière colonne débordait et une barre horizontale
	/// apparaissait.
	[[nodiscard]] int ColumnsFor(float width) const noexcept;

	void Refresh();

	void SetEnabled(ecs::Entity button, bool enabled);

	void RefreshCrumbs();

	/// Arbre des dossiers : la racine et ses dossiers, puis la CHAÎNE qui
	/// mène à l'emplacement courant, dépliée — l'arbre montre où l'on est
	/// sans lister tout le disque.
	void RefreshTree();

	void AddSubTree(const String &folder, const String &current, int depth);

	[[nodiscard]] static bool IsOnPath(const String &folder, const String &current);

	void AddTreeRow(const String &name, const String &location, int depth, bool open);

	void RefreshGrid();

	void AddTile(ecs::Entity row, size_t index);

	/// Aperçu réel : la texture elle-même, ou le modèle rendu dans un petit
	/// viewport 3D à la demande (un seul rendu, cf. UiViewport3D::continuous).
	bool AddPreview(ecs::Entity frame, const AssetEntry &entry, float size);

	/// Racine de scène de la vignette d'un modèle, chargée une fois puis
	/// gardée en cache pour toute la session (les vignettes sont recréées à
	/// chaque navigation, le maillage, lui, ne se relit pas).
	render3d::Object3D *ThumbnailFor(const String &path);

	/// Caméra qui cadre le modèle entier, vu de trois quarts.
	[[nodiscard]] render3d::Camera ThumbnailCamera(const String &path) const;

	void ClearThumbnails();

	void OnTileClick(size_t index);

	void RefreshInfo();

	[[nodiscard]] static String TooltipOf(const AssetEntry &entry);

	[[nodiscard]] static ui::MaterialIcons IconOf(AssetKind kind) noexcept;

	[[nodiscard]] static sdl3::FColor ColorOf(AssetKind kind) noexcept;

	UiContext &m_ctx;
	AssetActions m_actions;
	AssetBrowserModel m_model;
	std::vector<AssetEntry> m_entries;
	String m_filter;
	int m_selected = -1;
	float m_tileSize = 64.f;
	int m_treeRowIndex = 0;
	float m_gridWidth = 0.f;
	float m_clock = 0.f;
	float m_lastClick = -10.f;
	bool m_dirty = true;
	ecs::Entity m_back{}, m_forward{}, m_up{}, m_crumbs{}, m_search{}, m_tree{}, m_grid{}, m_info{};
	std::unordered_map<String, std::unique_ptr<render3d::Object3D>> m_thumbnailRoots;
	std::unordered_map<String, math::FAABB> m_thumbnailBounds;
};

} // namespace game_editor
