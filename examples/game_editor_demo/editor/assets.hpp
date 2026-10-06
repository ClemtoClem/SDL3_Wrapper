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
 * le projet (ses scènes, sa bibliothèque de scripts, que l'enregistrement du
 * projet écrit lui-même dans `scenes/` et `scripts/`) et ce qui vit sur le
 * DISQUE (`assets/`, et les sous-dossiers de `scenes/` et `scripts/` : pièces
 * `.scene`, modules…). « Scènes » et « Scripts » montrent les deux : les
 * éléments du projet (`managed`) et tout le reste du dossier, sous-dossiers
 * compris. Les deux se parcourent de la même façon ; seule l'action
 * d'ouverture diffère.
 *
 *  - `AssetOperations` (asset_ops.hpp) : créer, renommer, dupliquer,
 *    supprimer, déplacer — par le projet pour ses éléments, sur le disque
 *    pour le reste, et seulement dans le dossier du projet.
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
	/// Élément du PROJET (scène, script de la bibliothèque, script de jeu) :
	/// ses fichiers sont écrits par l'enregistrement du projet — on le
	/// manipule par le projet, jamais directement sur le disque.
	bool managed = false;
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

	/// Dossier du projet ouvert (vide : aucun) — c'est là que vivent les
	/// sous-dossiers de `scenes/` et `scripts/`.
	void SetProjectDirectory(String directory);
	[[nodiscard]] const String &ProjectDirectory() const noexcept { return m_projectDir; }

	/// Fichiers qui APPARTIENNENT au projet (lus ou écrits par lui) : jamais
	/// montrés comme fichiers ordinaires — renommée ou retirée, une scène
	/// laisse son ancien fichier sur le disque jusqu'à l'enregistrement, qui
	/// l'efface.
	void SetOwnedFiles(std::vector<String> paths);

	/// `<projet>/scenes` et `<projet>/scripts` (vides sans projet).
	[[nodiscard]] String SceneFolder() const;
	[[nodiscard]] String ScriptFolder() const;

	/// Forme canonique d'un emplacement : les dossiers `<projet>/scenes` et
	/// `<projet>/scripts` SONT « Scènes » et « Scripts » (un seul emplacement,
	/// un seul contenu, quel que soit le chemin pris pour y arriver).
	[[nodiscard]] String Canonical(const String &location) const;

	/// Dossier disque d'un emplacement (« Scènes » → `<projet>/scenes`), ou
	/// vide s'il n'en a pas.
	[[nodiscard]] String DiskFolderOf(const String &location) const;
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

	/// Ajoute à `entries` le contenu disque de `folder` (dossiers et fichiers
	/// qui ne sont pas des éléments du projet déjà listés).
	void AppendDiskExtras(std::vector<AssetEntry> &entries, const String &folder, bool scenes) const;

	const Project *m_project = nullptr;
	String m_projectDir;
	std::vector<String> m_ownedFiles; ///< normalisés (files::NormalizePath)
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

class AssetOperations;
struct AssetOpReport;

/**
 * Le panneau. Gestes :
 *  - arbre des dossiers : flèche = déplier/replier (« Scènes » et « Scripts »
 *    montrent d'emblée TOUS leurs sous-dossiers), clic = ouvrir, clic droit =
 *    menu du dossier, dépôt d'éléments = les y déplacer ;
 *  - grille : clic = sélectionner, Ctrl+clic = ajouter/retirer, Maj+clic =
 *    plage, double clic = ouvrir, glisser = déplacer la sélection sur un
 *    dossier (vignette ou arbre), clic droit = menu (nouveau dossier,
 *    renommer, dupliquer, couper/coller, supprimer, tout sélectionner) ;
 *  - clavier, pointeur au-dessus du panneau : Suppr, F2, Ctrl+D, Ctrl+X,
 *    Ctrl+V, Ctrl+A, Entrée (ouvrir), Retour arrière (dossier parent), Échap.
 * Renommer plusieurs éléments les numérote (« mur », « mur 2 »…). Supprimer
 * demande confirmation.
 */
class AssetBrowserPanel {
public:
	AssetBrowserPanel(UiContext &ctx, AssetActions actions);
	~AssetBrowserPanel();

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

	// ── Sélection et opérations (menus, clavier, pilotage par script) ───────

	/// Sélectionne ces entrées du dossier courant (les autres noms sont
	/// ignorés) ; rend le nombre retenu.
	size_t SelectNames(const std::vector<String> &names);
	void SelectAll();
	void ClearSelection();
	[[nodiscard]] std::vector<AssetEntry> SelectedEntries() const;
	[[nodiscard]] const std::vector<String> &Selection() const noexcept { return m_selection; }

	/// Boîtes de dialogue : nom du nouveau dossier, nouveau nom (numéroté
	/// pour une sélection multiple), confirmation de suppression.
	void BeginNewFolder();
	void BeginRename();
	void BeginDelete();

	/// Les mêmes, sans dialogue (validation de la boîte, scripts, tests).
	bool CreateFolder(const String &name);
	bool RenameSelection(const String &name);
	bool DeleteSelection();
	bool DuplicateSelection();
	void CutSelection();
	bool Paste();
	/// Déplace dans le dossier `location` (dépôt, coller).
	bool MoveEntries(const std::vector<AssetEntry> &entries, const String &location);

	/// Touche reçue pendant que le pointeur survole le panneau ; vrai si elle
	/// a été traitée (et ne doit donc rien faire d'autre — Suppr ne doit pas
	/// effacer le nœud sélectionné de la scène).
	bool HandleKey(const sdl3::Event &event);
	[[nodiscard]] bool PointerOver() const;

	/// Ouvre une entrée par son index dans `Entries()` (double clic, dépôt
	/// dans la vue, pilotage par script).
	void Activate(size_t index);

	static constexpr const char *NEW_SCRIPT_TEMPLATE =
		"# Nouveau script de nœud : il DÉFINIT un comportement, une classe dérivée\n"
		"# de `Behaviour`. Attachez-le à un nœud (inspecteur > Script) : en mode Jeu,\n"
		"# chaque nœud qui le porte reçoit sa propre instance (`this.node()`).\n"
		"\n"
		"class NouveauComportement extends Behaviour {\n"
		"    let temps = 0\n"
		"\n"
		"    fn on_start() {\n"
		"        editor.log(\"Démarrage de \" .. this.node())\n"
		"    }\n"
		"\n"
		"    fn on_update(dt) {\n"
		"        this.temps += dt\n"
		"    }\n"
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

	/// Une branche de l'arbre : la ligne du dossier puis, s'il est déplié,
	/// ses sous-dossiers. `openByDefault` : déplié tant que l'utilisateur ne
	/// l'a pas replié (« Scènes » et « Scripts » et tout ce qu'ils contiennent).
	void AddTreeBranch(const AssetEntry &folder, int depth, bool openByDefault);

	[[nodiscard]] bool IsTreeOpen(const String &location, bool openByDefault) const;

	[[nodiscard]] static bool IsOnPath(const String &folder, const String &current);

	void AddTreeRow(const AssetEntry &folder, int depth, bool hasChildren, bool open);

	void BuildMenus();
	void BuildDialogs();
	void OpenMenu(float x, float y, bool onFolderRow);
	[[nodiscard]] ecs::Entity MenuItem(const char *text, const char *shortcut, std::function<void()> action);
	void OnDrop(const String &folder, int64_t draggedIndex);
	void ApplyNameDialog(const String &text);
	void Report(const AssetOpReport &report, const char *verb);
	void SelectLocations(const std::vector<String> &locations);
	[[nodiscard]] bool IsSelected(const String &location) const;
	void Status(const String &text);

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
	std::unique_ptr<AssetOperations> m_ops;
	std::vector<AssetEntry> m_entries;
	String m_filter;
	/// Sélection par EMPLACEMENT : elle survit aux rafraîchissements et à la
	/// reconstruction de l'interface. `m_anchor` : départ d'une plage (Maj+clic).
	std::vector<String> m_selection;
	String m_anchor;
	/// Couper / coller : emplacements à déplacer au prochain « Coller ».
	std::vector<AssetEntry> m_clipboard;
	/// Dossiers dépliés ou repliés EXPLICITEMENT dans l'arbre.
	std::unordered_map<String, bool> m_treeOpen;
	/// Cible du menu contextuel ouvert depuis une ligne de l'arbre (sinon :
	/// la sélection de la grille).
	Option<AssetEntry> m_menuFolder = NONE;
	enum class NameDialog : uint8_t { NEW_FOLDER, RENAME } m_nameDialog = NameDialog::NEW_FOLDER;
	float m_tileSize = 64.f;
	int m_treeRowIndex = 0;
	float m_gridWidth = 0.f;
	float m_clock = 0.f;
	bool m_dirty = true;
	ecs::Entity m_page{}, m_back{}, m_forward{}, m_up{}, m_crumbs{}, m_search{}, m_tree{}, m_grid{}, m_info{};
	ecs::Entity m_newFolderButton{}, m_renameButton{}, m_duplicateButton{}, m_deleteButton{};
	std::vector<ecs::Entity> m_tiles; ///< une par entrée, même ordre que m_entries
	std::vector<ecs::Entity> m_popups;
	ecs::Entity m_menu{}, m_menuOpen{}, m_menuNewFolder{}, m_menuRename{}, m_menuDuplicate{}, m_menuCut{},
		m_menuPaste{}, m_menuDelete{}, m_menuSelectAll{};
	ecs::Entity m_nameModal{}, m_nameTitle{}, m_nameInput{}, m_confirmModal{}, m_confirmText{}, m_confirmTitle{};
	std::unordered_map<String, std::unique_ptr<render3d::Object3D>> m_thumbnailRoots;
	std::unordered_map<String, math::FAABB> m_thumbnailBounds;
};

} // namespace game_editor
