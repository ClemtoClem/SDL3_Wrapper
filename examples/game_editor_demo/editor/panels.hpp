#pragma once
/**
 * game_editor::EditorUi — l'interface de l'éditeur, montée avec `ui::`.
 *
 * Disposition des éditeurs de jeu du marché (Godot, Unity, Unreal) :
 *
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │ Fichier  Édition  Outils  Fenêtre  Compiler  Aide                       │
 *   ├────────────┬────────────────────────────────────────┬───────────────────┤
 *   │ Arbre de   │ [Scène : Donjon] [torchlight.script ×] │ Inspecteur        │
 *   │ scène      │ ⇔ ↻ ⤢ ▦ │ ▶ ⛶ │     🔍 aller à…  ⤢    │  nom, sections    │
 *   │ (recherche │                                        │  repliables par   │
 *   │  + ≡)      │           vue 3D  ⊕ axes               │  composant        │
 *   │            │                        ‹ Persp         │                   │
 *   ├────────────┴───────────────────────┬────────────────┼───────────────────┤
 *   │ Ressources  ← → ↑ Projet › Modèles │ Console        │ Matériaux Scripts │
 *   │ dossiers │ vignettes               │ Profil         │ Monde             │
 *   ├────────────────────────────────────┴────────────────┴───────────────────┤
 *   │ img/s · objets · corps · scène · mode · manipulateur · message          │
 *   └─────────────────────────────────────────────────────────────────────────┘
 *
 * Chaque bordure entre panneaux se tire à la souris. Chaque panneau est un
 * `kit::DockPanel` (onglets + menu ≡) ; leur contenu vit dans des classes
 * dédiées (SceneTreePanel, InspectorPanel, LibraryPanel, AssetBrowserPanel,
 * DocumentArea) — cette classe les ASSEMBLE, route les évènements (caméra,
 * manipulateur, raccourcis) et gère le mode Jeu en plein écran.
 *
 * ── Reconstruction plutôt que synchronisation fine ──────────────────────
 * Les listes (arbre, inspecteur, console) sont RECONSTRUITES quand leur
 * contenu change, jamais mises à jour widget par widget ; un panneau masqué
 * garde son drapeau « à rafraîchir » et ne travaille qu'en redevenant
 * visible. Les valeurs continues (cadence, compteurs) sont écrites
 * directement dans leur widget.
 */
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "core/core.hpp"
#include "sdl3/dialog.hpp"
#include "ui/ui.hpp"

#include "assets.hpp"
#include "documents.hpp"
#include "inspector.hpp"
#include "kit.hpp"
#include "scene_tree.hpp"
#include "../document/project_files.hpp"
#include "../engine/canvas2d_render.hpp"
#include "../engine/runtime.hpp"

namespace game_editor {

/// Onglets de l'inspecteur tels que les scripts les désignent
/// (`editor.open_panel("inspector", 1)`) — compatibilité avec l'interface
/// précédente, qui avait un onglet par famille de réglages.
enum class InspectorTab : int { TRANSFORM = 0, MATERIAL = 1, PHYSICS = 2, WORLD = 3 };

class EditorUi {
public:
	EditorUi(Runtime &runtime, ui::Ui &gui);

	~EditorUi();

	EditorUi(const EditorUi &) = delete;
	EditorUi &operator=(const EditorUi &) = delete;

	/// Renderer de la fenêtre (aperçus d'images du navigateur de ressources).
	void SetRenderer(sdl3::Renderer &renderer) noexcept { m_ctx.renderer = &renderer; }
	/// Fenêtre sans décoration à encadrer : barre de titre (déplacer,
	/// réduire, agrandir, fermer), poignée de redimensionnement dans la barre
	/// d'état et hit-test système. Sans fenêtre (tests), ni l'une ni l'autre.
	void SetWindow(sdl3::Window &window) noexcept { m_window = &window; }

	/// Racines des ressources (`--assets-dir`, lues seulement) et des
	/// sauvegardes (`--saves-dir` : projets, scènes emballées, scripts).
	void SetDirectories(const String &assets, const String &saves);

	/// Reconstruit l'interface à l'image suivante (projet ouvert, créé ou
	/// fermé) — jamais au milieu du traitement d'un clic ou d'un script.
	void RequestRebuild() noexcept { m_rebuildRequested = true; }

	/// Vrai quand l'éditeur complet est affiché (un projet est ouvert) ; faux
	/// sur la page « Aucun projet ouvert ».
	[[nodiscard]] bool IsEditorBuilt() const noexcept { return m_editorBuilt; }

	// ── Construction ─────────────────────────────────────────────────────────

	void Build();

	/// Détruit puis reconstruit toute l'interface (changement de thème). L'état
	/// applicatif vit dans `Runtime` : rien n'est perdu.
	void Rebuild();

	/// Détruit l'arbre d'interface ET les popups, racines indépendantes (cf.
	/// UiFactory::Menu) qui survivraient sinon à chaque reconstruction.
	void Teardown();

	// ── Thème ────────────────────────────────────────────────────────────────

	[[nodiscard]] const String &ThemeName() const noexcept { return m_themeName; }

	/// Change de thème. Avant la première construction, seule la palette est
	/// posée (reconstruire ici doublerait l'interface au `Build()` suivant).
	bool SetTheme(const String &name);

	[[nodiscard]] static Option<ui::UiTheme> ThemeByName(const String &name);

	// ── Vue 3D ───────────────────────────────────────────────────────────────

	[[nodiscard]] sdl3::FRect ViewportRect() const;

	/// Vrai si le point est dans la vue 3D ET qu'aucun widget n'y est dessiné
	/// par-dessus (menu déroulé, popup) — sinon le même clic sélectionnerait
	/// aussi l'objet derrière le menu.
	[[nodiscard]] bool PointerOverViewport(float x, float y) const;

	[[nodiscard]] bool PointerInViewport() const;

	[[nodiscard]] Option<math::FRay> RayAt(float screenX, float screenY) const;

	// ── Mode Jeu plein écran (« Run Mode ») ──────────────────────────────────

	[[nodiscard]] bool IsRunMode() const noexcept { return m_runMode; }

	/// Lance le jeu en plein écran : l'interface disparaît, la vue remplit la
	/// fenêtre avec un réticule et la liste des scripts actifs ; Échap revient
	/// à l'éditeur (et arrête la partie, qui restaure la scène).
	void EnterRunMode();

	void ExitRunMode();

	// ── Interface des scripts de jeu ─────────────────────────────────────────
	//
	// Pendant une partie, un calque transparent couvre la vue du jeu (celle
	// de l'éditeur ou celle du mode plein écran) : c'est la racine de `ui.*`
	// des scripts. Il passe d'une vue à l'autre avec ses widgets, repart de
	// zéro à chaque scène jouée et disparaît à la fin de la partie.

	/// Vue qui porte le jeu en ce moment.
	[[nodiscard]] ecs::Entity GameViewport() const { return m_runMode ? m_runViewport : m_viewport; }

	/// Crée le calque s'il manque, ou le rattache à la vue courante.
	void EnsureGameOverlay();

	/// Nouvelle scène jouée : calque vierge ; fin de partie : plus de calque.
	void ResetGameOverlay();

	// ── Évènements ───────────────────────────────────────────────────────────

	/// Navigation (les gestes des éditeurs 3D du marché) :
	///  - clic DROIT maintenu : orienter la vue, + ZQSD/WASD voler, E/Q monter ;
	///  - clic MILIEU : panoramique ; MOLETTE : avancer/reculer ;
	///  - ALT + clic gauche : orbiter autour de la sélection ;
	///  - flèches et Page haut/bas : mêmes déplacements sans bouton.
	/// Clic GAUCHE dans la vue : attraper un axe du manipulateur, sinon
	/// sélectionner l'objet visé. Le clic droit HORS de la vue appartient aux
	/// panneaux (menus contextuels). L'évènement arrive APRÈS l'interface :
	/// consommé par elle (frappe dans le champ qui a le focus, cf.
	/// ui::Ui::HandleEvent), il est ignoré ici — seuls les raccourcis que le
	/// champ laisse passer (F1–F12, Ctrl+S…) atteignent l'éditeur.
	void HandleEvent(const sdl3::Event &event);

	/// Une image : caméra au clavier, vues à jour, rafraîchissements différés.
	void Tick(float dt, double fps);

	/// Souris dans la vue en espace de travail 3D : caméra, sélection au
	/// rayon, manipulateur.
	void Handle3DMouse(const sdl3::Event &event);

	// ── Espace de travail 2D ─────────────────────────────────────────────────
	//
	// La vue garde son widget 3D ; un calque (UiCanvas) posé dessus dessine
	// la 2D : en mode Jeu, le jeu (par-dessus le monde 3D, ou sur un fond uni
	// si la scène est purement 2D) ; en édition 2D, la scène 2D sur une
	// grille, avec le cadre de l'écran de référence, les caméras 2D et la
	// sélection. Panoramique : clic droit ou milieu ; zoom : molette ; clic
	// gauche : sélectionner puis glisser ; flèches : décaler d'un pixel (×10
	// avec Maj).

	[[nodiscard]] bool Is2DEditing() const noexcept { return m_view2d && !Rt().IsPlaying(); }

	void Set2DView(bool enabled);

	/// Repères propres à la 3D (trièdre, projection) masqués en 2D.
	void Show2DWorkspace();

	/// Sélectionner un nœud 2D bascule en 2D, un maillage en 3D.
	void FollowSelectionWorkspace();

	/// Chemin d'image du document → fichier : absolu, sinon dossier du
	/// projet, puis ressources.
	[[nodiscard]] String ResolveAsset(const String &path) const;

	void Spawn2DLayer(ecs::Entity viewport, bool runView);

	void Draw2DLayer(sdl3::Renderer &ren, sdl3::FRect r, bool runView);

	/// Grille du monde 2D : pas adapté au zoom, une ligne sur quatre plus
	/// marquée, axes X (rouge) et Y (vert) à l'origine.
	void DrawGrid2D(sdl3::Renderer &ren, sdl3::FRect r);

	/// Cadre de l'écran de référence, caméras 2D, sélection.
	void DrawEditorOverlays2D(sdl3::Renderer &ren, const Canvas2DFrame &frame, math::FVector2 reference);

	/// Cadre la vue 2D sur un nœud (touche F).
	bool Focus2D(scene::NodeId id);

	/// Souris (et flèches) en édition 2D.
	void Handle2DEvent(const sdl3::Event &event);

	// ── Pilotage par script ──────────────────────────────────────────────────

	/// `editor.open_panel(nom, onglet)` — met un panneau au premier plan.
	bool FocusPanel(const String &panel, int tab);

	void SetStatus(String message);

	/**
	 * `editor.ui(commande, argument)` — ce que ferait la souris, pour les
	 * scénarios et les captures :
	 *
	 *   select NŒUD            sélectionner (et montrer dans l'arbre)
	 *   context_menu NŒUD      menu contextuel de la ligne du nœud
	 *   context_submenu        déployer « Créer un nœud enfant »
	 *   create MODÈLE          créer un enfant de la sélection (cf. NODE_TEMPLATES)
	 *   rename NŒUD            ouvrir la boîte de renommage
	 *   section TITRE          déplier une section de l'inspecteur et y aller
	 *   close_menus            refermer menus et boîtes ouverts
	 *   browse EMPLACEMENT     navigateur de ressources (`models/…` relatif à
	 *                          la racine des ressources, ou `projet:/scripts`)
	 *   select_asset NOM       sélectionner une vignette
	 *   open_script NOM        ouvrir un script de la bibliothèque
	 *   open_scene_script SCÈNE / open_console
	 *   open_json NŒUD[|Composant]  propriétés (ou composant) en JSON
	 *   document N             onglet N de la zone centrale (0 = vue 3D)
	 *   library N              onglet N de la bibliothèque (matériaux…)
	 *   run_mode on|off        mode Jeu plein écran
	 *   maximize               vue 3D seule (bascule)
	 */
	bool UiCommand(const String &command, const String &argument);

	// ── Accès (scénarios, tests) ─────────────────────────────────────────────

	[[nodiscard]] SceneTreePanel &Tree() noexcept { return m_tree; }
	[[nodiscard]] InspectorPanel &Inspector() noexcept { return m_inspector; }
	[[nodiscard]] AssetBrowserPanel &Assets() noexcept { return m_assets; }
	[[nodiscard]] DocumentArea &Documents() noexcept { return m_documents; }

	/// Installé par l'application : exporte le rapport depuis le menu.
	std::function<void()> onExportReport;
	/// Installé par l'application : Fichier › Quitter.
	std::function<void()> onQuit;

private:
	static constexpr long PROFILER_REFRESH_FRAMES = 20;
	static constexpr long RUN_PANEL_REFRESH_FRAMES = 30;
	static constexpr float LEFT_WIDTH = 270.f;
	static constexpr float RIGHT_WIDTH = 330.f;
	static constexpr float BOTTOM_HEIGHT = 270.f;
	static constexpr float CONSOLE_WIDTH = 420.f;
	static constexpr float LIBRARY_HEIGHT = 230.f;
	static constexpr size_t CONSOLE_LINES = 200;
	static constexpr size_t FPS_HISTORY_SIZE = 120;

	[[nodiscard]] Runtime &Rt() const noexcept { return m_ctx.runtime; }

	InspectorActions MakeInspectorActions();

	AssetActions MakeAssetActions();

	[[nodiscard]] String ActiveSceneName() const;

	[[nodiscard]] String ViewportTitle() const { return String::Format("Scène : %s", ActiveSceneName().CStr()); }

	/// Point à 7 unités devant la caméra, remonté au sol s'il tombe dessous —
	/// là où apparaît ce qu'on crée à la racine.
	[[nodiscard]] math::FVector3 SpawnPointInFrontOfCamera(float lift) const;

	// ── Mise en page : poignées de redimensionnement ─────────────────────────

	/// Poignée entre deux panneaux. `before` : la poignée SUIT `target` (la
	/// tirer vers la droite/le bas l'agrandit) ; sinon elle le PRÉCÈDE et le
	/// rétrécit (colonne de droite, rangée du bas — cf. ui::ResolveResizeDrag,
	/// qui applique le signe opposé au panneau « après »).
	ecs::Entity ResizeHandle(ecs::Entity parent, ui::Orientation orient, ecs::Entity target, bool before, float minSize,
							 float maxSize);

	/// Place le DERNIER enfant de `parent` (la poignée qu'on vient d'ajouter)
	/// juste avant `anchor` : une poignée « après » doit précéder son panneau.
	void MoveBefore(ecs::Entity parent, ecs::Entity anchor);

	// ── Barre de menus ───────────────────────────────────────────────────────

	[[nodiscard]] ui::WidgetBuilder MenuAction(const char *text, const char *shortcut, std::function<void()> action);

	void AddItem(ecs::Entity menu, const char *text, const char *shortcut, std::function<void()> action);
	/// Pose une instance d'objet sous la sélection du document ouvert.
	void PlaceObject(const String &objectName);

	void BuildMenuBar(ecs::Entity parent);

	/// Menus ≡ des panneaux.
	void BuildDockMenus();

	void OpenMenu(ecs::Entity menu, float x, float y) { m_ctx.gui.OpenPopupAt(menu, sdl3::FPoint{x - 180.f, y}); }

	// ── Vue 3D : barre d'outils, superpositions ──────────────────────────────

	void BuildViewportPage(ecs::Entity page);

	void AddToolbarSeparator(ecs::Entity parent);

	/// Petit trièdre d'orientation (coin haut-droit de la vue, comme dans
	/// Godot et Blender) : les trois axes du monde vus par la caméra, X rouge,
	/// Y vert, Z bleu ; les demi-axes négatifs en pastilles pâles ; le plus
	/// lointain dessiné en premier. Les lettres sont tracées au trait : aucune
	/// police n'est disponible dans un `Canvas`.
	static void DrawAxisGizmo(sdl3::Renderer &ren, sdl3::FRect r, const render3d::Camera &camera);

	/// X, Y ou Z au trait, dans un carré de demi-côté `s` centré sur `c`.
	static void DrawLetter(sdl3::Renderer &ren, char letter, sdl3::FPoint c, float s);

	/// « Vue 3D seule » : masque les colonnes latérales et la rangée du bas
	/// (et leurs poignées).
	void ToggleMaximized();

	[[nodiscard]] ecs::Entity Previous(ecs::Entity entity) const;

	/// Recherche « aller à » : premier nœud dont le nom contient le texte.
	void GoToNode(const String &text);

	/// Aspect et caméra d'un viewport ; rend l'aspect (0 si pas encore mis en page).
	float UpdateViewportCamera(ecs::Entity entity);

	// ── Mode Jeu plein écran ─────────────────────────────────────────────────

	void HandleRunModeEvent(const sdl3::Event &event);

	/// Déplacement de l'encart « MODE TEST » en le tirant par son en-tête
	/// (souris libre). Vrai si l'évènement a servi au déplacement.
	bool HandleRunPanelDrag(const sdl3::Event &event);

	/// Décale l'encart, borné à la vue du jeu.
	void MoveRunPanel(float dx, float dy);

	/// Masque ou réaffiche l'encart (bouton ✕, touche F9).
	void SetRunPanelHidden(bool hidden);

	/// Souris du jeu pendant une partie : touche de bascule de la capture,
	/// déplacements relatifs quand elle est capturée.
	bool HandleGameMouse(const sdl3::Event &event);

	void RefreshRunPanel();

	// ── Console, profileur, scènes ───────────────────────────────────────────

	void BuildConsolePage(ecs::Entity page);

	void RefreshConsole();

	void AddProfilerRow(const char *label, const String &value);

	void RefreshProfiler();

	/// Onglet « Scènes » : les scènes du projet (pas les objets).
	void RefreshSceneList();
	/// Onglet « Objets » : les objets réutilisables (ouvrir, poser une instance).
	void RefreshObjectList();
	/// Choix du sélecteur de document de la vue : sous-menus Scènes et Objets.
	[[nodiscard]] std::vector<ui::ComboCategory> DocumentChoices() const;

	// ── Barre d'état ─────────────────────────────────────────────────────────

	void BuildStatusBar(ecs::Entity parent);

	ecs::Entity StatusLabel(ecs::Entity parent, const char *text, float width);

	void RefreshStatusBar();

	// ── Actions ──────────────────────────────────────────────────────────────

	void SetGizmoMode(Runtime::GizmoMode mode);

	void ToggleSnap();

	[[nodiscard]] static const char *GizmoModeName(Runtime::GizmoMode mode) noexcept;

	void ImportModel(const String &path);

	// ── Projet : fichiers ────────────────────────────────────────────────────

	void OnNewProject();

	void OnOpenProject();

	/// Ctrl+S : le projet à son emplacement ; un projet qui n'en a pas encore
	/// passe par « Enregistrer sous ».
	void OnSaveProject();

	/// Copie du projet dans un NOUVEAU dossier de projet (même disposition
	/// que « Nouveau projet »), qui devient le projet ouvert.
	void OnSaveProjectAs();

	void OnCloseProject();

	void OnNewScene();

	/// Dossier proposé pour un fichier de `sub` (scenes, scripts) : celui du
	/// projet, à défaut les sauvegardes.
	[[nodiscard]] String SuggestedPath(const char *sub, const String &fileName) const;

	void OnSaveObjectAs();

	void OnImportObject();

	void OnSaveSceneAs();

	void OnImportScene();

	void OnImportScript();

	/// « Enregistrer sous… » d'un document de script (`key` : nom du script
	/// de bibliothèque, ou `@Scène` pour un script de jeu).
	void OnSaveScriptAs(const String &key);
	/// Clé du script visé par « Enregistrer le script sous » : le script au
	/// premier plan (`nom`, ou `@Scène` pour un script de jeu).
	[[nodiscard]] String ActiveScriptKey();

	/// Crée le dossier qui contiendra `path` (les sous-dossiers de
	/// sauvegarde n'existent pas forcément encore).
	static void EnsureParentDirectory(const String &path);

	/// Le sous-arbre sélectionné, en scène réutilisable dans `assets/` du
	/// projet (à instancier ensuite dans n'importe quelle scène).
	void OnSaveSelectionAsScene();

	// ── Page « Aucun projet ouvert » ─────────────────────────────────────────

	/// Sans projet : barre de menus réduite et, au centre, de quoi créer un
	/// projet ou en ouvrir un (liste des projets du dossier des projets).
	void BuildStartPage();

	/// Une ligne cliquable « Nom — chemin » qui ouvre le projet.
	void SpawnProjectButton(ecs::Entity parent, const files::ProjectEntry &project);

	/// Ouvre le projet de `path` ; vrai en cas de succès (l'interface est
	/// reconstruite à l'image suivante).
	bool OpenProjectAt(const String &path);

	// ── Boîtes de dialogue de fichiers ───────────────────────────────────────

	enum class DialogKind : uint8_t {
		NEW_PROJECT,
		OPEN_PROJECT,
		SAVE_PROJECT_AS,
		SAVE_OBJECT_AS,
		IMPORT_OBJECT,
		SAVE_SCENE_AS,
		IMPORT_SCENE,
		IMPORT_SCRIPT,
		SAVE_SCRIPT_AS,
	};

	/// Boîte modale : titre, explication, un champ (nom ou chemin), une ligne
	/// d'erreur, et Parcourir… (sélecteur du système) / Annuler / action.
	void OpenDialog(DialogKind kind, const String &title, const String &message, const String &text,
					const String &placeholder, const String &confirm);

	void CloseDialog();

	void SetDialogText(const String &text);

	[[nodiscard]] String DialogText() const;

	void ShowDialogError(const String &message);

	/// Ajoute `extension` au chemin s'il ne l'a pas déjà.
	[[nodiscard]] static String WithExtension(const String &path, const char *extension);

	void ConfirmDialog();

	/// Sélecteur de fichiers du système. Sa réponse arrive sur un autre fil :
	/// elle est déposée ici et reportée dans le champ à l'image suivante
	/// (cf. ApplyBrowseResult).
	void BrowseForDialog();

	void ApplyBrowseResult();

	void OnExportReport();

	void OnUndo();

	void OnRedo();

	/// Duplique la sélection AVEC son sous-arbre et décale la copie.
	void OnDuplicate();

	void OnDeleteSelection();

	void OnFocusSelection();

	void OnClearConsole();

	/// Compile tous les scripts du projet et rend compte dans la console.
	void OnCheckAllScripts();

	void OnShowShortcuts();

	void OnAbout();

	// ── Clavier ──────────────────────────────────────────────────────────────

	[[nodiscard]] bool TextFieldHasFocus() const;

	/// Vol de la caméra : ZQSD/WASD TANT QUE le clic droit est tenu (sans
	/// quoi W/E/R ne pourraient pas aussi changer d'outil) ; flèches et Page
	/// haut/bas sans bouton.
	void PollCameraKeys(float dt);

	// ── Membres ──────────────────────────────────────────────────────────────
	// Ordre imposé : le contexte AVANT les panneaux qui en gardent une
	// référence.

	mutable UiContext m_ctx;
	SceneTreePanel m_tree;
	InspectorPanel m_inspector;
	LibraryPanel m_library;
	AssetBrowserPanel m_assets;
	DocumentArea m_documents;
	kit::DockPanel m_treeDock, m_inspectorDock, m_libraryDock, m_assetDock, m_consoleDock;

	String m_themeName = "studio";
	String m_assetsDir = "assets";
	String m_savesDir = "saves/game_editor_demo";
	String m_statusMessage = "Prêt";
	bool m_editorBuilt = false;
	bool m_rebuildRequested = false;
	DialogKind m_dialogKind = DialogKind::NEW_PROJECT;
	String m_dialogKey; ///< SAVE_SCRIPT_AS : le script visé
	ecs::Entity m_dialog{}, m_dialogInput{}, m_dialogError{};
	std::mutex m_browseMutex;
	Option<String> m_browseResult = NONE;
	String m_sceneName;
	double m_fps = 0.0;
	long m_tickCount = 0;
	bool m_rightMouseDown = false;
	bool m_middleMouseDown = false;
	bool m_orbiting = false;
	math::FVector3 m_orbitPivot{};
	bool m_consoleDirty = true;
	bool m_scenesDirty = true;
	bool m_documentChoicesDirty = true; ///< recharger les choix du sélecteur de document
	bool m_maximized = false;
	bool m_runMode = false;
	int m_shownGizmoMode = -1;
	bool m_shownSnap = false;
	bool m_shownPlaying = true;
	std::vector<float> m_fpsHistory;
	std::vector<ecs::Entity> m_menuPopups;

	ecs::Entity m_root{}, m_body{}, m_bottomRow{}, m_rightColumn{}, m_leftHandle{};
	ecs::Entity m_viewport{}, m_playIcon{}, m_playBadge{}, m_sceneCombo{}, m_snapButton{};
	ecs::Entity m_gameOverlay{}; ///< racine de l'interface des scripts de jeu (cf. EnsureGameOverlay)
	ecs::Entity m_gizmoButtons[3]{};
	// ── Espace de travail 2D ──
	ecs::Entity m_workspaceButtons[2]{}, m_axesGizmo{}, m_projectionLabel{};
	bool m_view2d = false;
	bool m_shownView2d = true; ///< force le premier surlignage
	Canvas2DRenderer m_renderer2d;
	math::FVector3 m_edit2dCenter{640.f, 360.f, 0.f};
	float m_edit2dZoom = 0.f; ///< 0 : à cadrer sur l'écran de référence
	String m_edit2dScene;     ///< scène pour laquelle la vue 2D a été cadrée
	View2D m_edit2dView;      ///< vue de la dernière image (pour la souris)
	bool m_panning2d = false;
	struct Drag2D {
		scene::NodeId id;
		math::FVector2 startPixel;
		math::FVector3 startPosition;
		Transform2D parentToPixels;
	};
	Option<Drag2D> m_drag2d;
	ecs::Entity m_sceneListPage{}, m_objectListPage{}, m_profilerPage{}, m_console{};
	ecs::Entity m_treeMenu{}, m_documentsMenu{}, m_inspectorMenu{}, m_assetsMenu{};
	ecs::Entity m_runRoot{}, m_runViewport{}, m_runPanel{};
	/// Encart « MODE TEST » : en-tête (poignée de déplacement, ✕) construit
	/// une fois, corps (état des scripts) reconstruit périodiquement — un
	/// clic sur ✕ ne doit pas tomber entre deux reconstructions.
	ecs::Entity m_runPanelHeader{}, m_runPanelClose{}, m_runPanelBody{};
	/// Décalage depuis le coin bas-droit de la vue, gardé d'une session à
	/// l'autre (l'utilisateur l'a posé là où il ne gêne pas son jeu).
	sdl3::FPoint m_runPanelOffset{-16.f, -16.f};
	bool m_draggingRunPanel = false;
	ecs::Entity m_statusFps{}, m_statusNodes{}, m_statusBodies{}, m_statusScene{}, m_statusMode{}, m_statusGizmo{};
	ecs::Entity m_statusMessageLabel{};
	sdl3::Window *m_window = nullptr;
	void BuildWindowTitleBar();
	void AttachWindowChrome();
	ui::TitleBarWidgets m_titleBar;
	ecs::Entity m_statusBar{}, m_statusGrip{};
	ui::WindowChrome m_chrome;
};

} // namespace game_editor
