#pragma once
/**
 * game_editor::EditorUi — l'interface de l'éditeur, montée avec `ui::`.
 *
 * Disposition des éditeurs de jeu du marché (Godot, Unity, Unreal) :
 *
 *   ┌───────────────────────────────────────────────────────────────────────┐
 *   │ Fichier  Édition  Outils  Fenêtre  Compiler  Aide                     │
 *   ├────────────┬──────────────────────────────────────┬───────────────────┤
 *   │ Arbre de   │ [Scène : Donjon] [torchlight.sled ×]  │ Inspecteur        │
 *   │ scène      │ ⇔ ↻ ⤢ ▦ │ ▶ ⛶ │     🔍 aller à…  ⤢  │  nom, sections    │
 *   │ (recherche │                                      │  repliables par   │
 *   │  + ≡)      │           vue 3D  ⊕ axes             │  composant        │
 *   │            │                        ‹ Persp       │                   │
 *   ├────────────┴───────────────────────┬──────────────┼───────────────────┤
 *   │ Ressources  ← → ↑ Projet › Modèles │ Console      │ Matériaux Scripts │
 *   │ dossiers │ vignettes               │ Profil       │ Monde             │
 *   ├────────────────────────────────────┴──────────────┴───────────────────┤
 *   │ img/s · objets · corps · scène · mode · manipulateur · message        │
 *   └───────────────────────────────────────────────────────────────────────┘
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
#include <vector>

#include "core/core.hpp"
#include "ui/ui.hpp"

#include "assets.hpp"
#include "content.hpp"
#include "documents.hpp"
#include "inspector.hpp"
#include "kit.hpp"
#include "runtime.hpp"
#include "scene_tree.hpp"

namespace game_editor {

/// Onglets de l'inspecteur tels que les scripts les désignent
/// (`editor.open_panel("inspector", 1)`) — compatibilité avec l'interface
/// précédente, qui avait un onglet par famille de réglages.
enum class InspectorTab : int { TRANSFORM = 0, MATERIAL = 1, PHYSICS = 2, WORLD = 3 };

class EditorUi {
public:
	EditorUi(Runtime &runtime, ui::Ui &gui)
		: m_ctx{runtime, gui, gui.World(), gui.Factory()}, m_tree(m_ctx),
		  m_inspector(m_ctx, MakeInspectorActions()), m_library(m_ctx, MakeInspectorActions()),
		  m_assets(m_ctx, MakeAssetActions()), m_documents(m_ctx), m_treeDock(m_ctx), m_inspectorDock(m_ctx),
		  m_libraryDock(m_ctx), m_assetDock(m_ctx), m_consoleDock(m_ctx) {
		Rt().onSceneStructureChanged = [this] {
			m_tree.MarkDirty();
			m_inspector.MarkDirty();
			m_scenesDirty = true;
		};
		Rt().onSelectionChanged = [this] {
			m_inspector.MarkDirty();
			m_tree.RevealSelection();
		};
		Rt().onObjectChanged = [this] {
			m_inspector.OnObjectChanged();
			m_tree.MarkDirty(); // nom, visibilité, verrou : l'arbre les montre
		};
		Rt().onLog = [this](const LogEntry &) { m_consoleDirty = true; };
		m_tree.onStatus = [this](const String &text) { SetStatus(text); };
		m_tree.spawnPoint = [this] { return SpawnPointInFrontOfCamera(1.f); };
		m_documents.onStatus = [this](const String &text) { SetStatus(text); };
	}

	EditorUi(const EditorUi &) = delete;
	EditorUi &operator=(const EditorUi &) = delete;

	/// Renderer de la fenêtre (aperçus d'images du navigateur de ressources).
	void SetRenderer(sdl3::Renderer &renderer) noexcept { m_ctx.renderer = &renderer; }

	/// Racines des ressources (`--assets-dir`, lues seulement) et des
	/// sauvegardes (`--saves-dir` : projets, scènes emballées, scripts).
	void SetDirectories(const String &assets, const String &saves) {
		m_assetsDir = assets;
		m_savesDir = saves;
		m_assets.SetRoots(assets, saves);
		projectPath = saves + String("/projects/game_editor_demo_project.json");
	}

	// ── Construction ─────────────────────────────────────────────────────────

	void Build() {
		ui::WidgetBuilder root = m_ctx.factory.Column();
		root.Gap(0.f).Pad(0.f).Fixed().Anchor(ui::Anchor::TopLeft).Bg(kit::PaletteOf(m_ctx).base);
		root.W(ui::Dimension::Rpct(100.f)).H(ui::Dimension::Rpct(100.f));
		m_root = root.Spawn();

		BuildMenuBar(m_root);

		ui::WidgetBuilder body = m_ctx.factory.Row();
		body.Gap(0.f).Pad(math::Sides{4.f, 2.f, 4.f, 2.f}).GrowW().GrowH().Parent(m_root);
		m_body = body.Spawn();

		// Colonne principale : (arbre | documents) au-dessus de (ressources | console).
		ui::WidgetBuilder main = m_ctx.factory.Column();
		main.Pad(0.f);
		main.Gap(0.f).GrowW().GrowH().Parent(m_body);
		ecs::Entity mainEntity = main.Spawn();

		ui::WidgetBuilder top = m_ctx.factory.Row();
		top.Pad(0.f);
		top.Gap(0.f).GrowW().GrowH().Parent(mainEntity);
		ecs::Entity topEntity = top.Spawn();

		m_treeDock.Build(topEntity, [](ui::WidgetBuilder &b) { b.W(ui::Dimension::Px(LEFT_WIDTH)).GrowH(); });
		m_leftHandle = ResizeHandle(topEntity, ui::Orientation::Horizontal, m_treeDock.Frame(), true, 160.f, 700.f);
		m_documents.Build(topEntity, [](ui::WidgetBuilder &b) { b.GrowW().GrowH(); });

		ui::WidgetBuilder bottom = m_ctx.factory.Row();
		bottom.Pad(0.f);
		bottom.Gap(0.f).GrowW().H(ui::Dimension::Px(BOTTOM_HEIGHT)).Parent(mainEntity);
		m_bottomRow = bottom.Spawn();
		// La poignée PRÉCÈDE la rangée du bas : elle la redimensionne « après ».
		(void)ResizeHandle(mainEntity, ui::Orientation::Vertical, m_bottomRow, false, 90.f, 700.f);
		MoveBefore(mainEntity, m_bottomRow);

		m_assetDock.Build(m_bottomRow, [](ui::WidgetBuilder &b) { b.GrowW().GrowH(); });
		ui::WidgetBuilder consoleSlot = m_ctx.factory.Column();
		consoleSlot.Pad(0.f);
		consoleSlot.Gap(0.f).W(ui::Dimension::Px(CONSOLE_WIDTH)).GrowH().Parent(m_bottomRow);
		ecs::Entity consoleSlotEntity = consoleSlot.Spawn();
		(void)ResizeHandle(m_bottomRow, ui::Orientation::Horizontal, consoleSlotEntity, false, 200.f, 900.f);
		MoveBefore(m_bottomRow, consoleSlotEntity);
		m_consoleDock.Build(consoleSlotEntity, [](ui::WidgetBuilder &b) { b.GrowW().GrowH(); });

		// Colonne de droite : inspecteur au-dessus de la bibliothèque.
		ui::WidgetBuilder right = m_ctx.factory.Column();
		right.Pad(0.f);
		right.Gap(0.f).W(ui::Dimension::Px(RIGHT_WIDTH)).GrowH().Parent(m_body);
		m_rightColumn = right.Spawn();
		(void)ResizeHandle(m_body, ui::Orientation::Horizontal, m_rightColumn, false, 240.f, 700.f);
		MoveBefore(m_body, m_rightColumn);
		m_inspectorDock.Build(m_rightColumn, [](ui::WidgetBuilder &b) { b.GrowW().GrowH(); });
		ui::WidgetBuilder librarySlot = m_ctx.factory.Column();
		librarySlot.Pad(0.f);
		librarySlot.Gap(0.f).GrowW().H(ui::Dimension::Px(LIBRARY_HEIGHT)).Parent(m_rightColumn);
		ecs::Entity librarySlotEntity = librarySlot.Spawn();
		(void)ResizeHandle(m_rightColumn, ui::Orientation::Vertical, librarySlotEntity, false, 80.f, 600.f);
		MoveBefore(m_rightColumn, librarySlotEntity);
		m_libraryDock.Build(librarySlotEntity, [](ui::WidgetBuilder &b) { b.GrowW().GrowH(); });

		BuildStatusBar(m_root);

		// ── Contenu des panneaux ─────────────────────────────────────────
		m_tree.Build(m_treeDock.AddPage(String("Arbre de scène"), false, false, 4.f));
		m_sceneListPage = m_treeDock.AddPage(String("Scènes"), true);
		m_treeDock.SetActive(0);
		m_treeDock.onActivate = [this](int page) {
			if (page == 1)
				m_scenesDirty = true;
		};
		m_treeDock.onMenu = [this](float x, float y) { OpenMenu(m_treeMenu, x, y); };

		BuildViewportPage(m_documents.AddViewportPage(ViewportTitle()));
		m_documents.Dock().onMenu = [this](float x, float y) { OpenMenu(m_documentsMenu, x, y); };

		m_inspector.Build(m_inspectorDock.AddPage(String("Inspecteur"), true, false, 8.f));
		m_inspectorDock.onMenu = [this](float x, float y) { OpenMenu(m_inspectorMenu, x, y); };

		m_library.BuildMaterials(m_libraryDock.AddPage(String("Matériaux"), true));
		m_library.BuildScripts(m_libraryDock.AddPage(String("Scripts"), true));
		m_library.BuildWorld(m_libraryDock.AddPage(String("Monde"), true));
		m_libraryDock.SetActive(0);
		m_libraryDock.onActivate = [this](int page) {
			if (page == 1)
				m_library.RefreshScripts();
			if (page == 2)
				m_library.RefreshWorld();
		};

		m_assets.Build(m_assetDock.AddPage(String("Ressources"), false, false, 4.f));
		m_assetDock.onMenu = [this](float x, float y) { OpenMenu(m_assetsMenu, x, y); };

		BuildConsolePage(m_consoleDock.AddPage(String("Console"), false, false, 0.f));
		m_profilerPage = m_consoleDock.AddPage(String("Profil"), true);
		m_consoleDock.SetActive(0);
		m_consoleDock.onActivate = [this](int page) {
			if (page == 0)
				m_consoleDirty = true;
			if (page == 1)
				RefreshProfiler();
		};
		(void)kit::IconButton(m_ctx, m_consoleDock.HeaderExtra(), ui::MaterialIcons::DELETE, String("Effacer la console"),
							  [this] { OnClearConsole(); }, 22.f);

		BuildDockMenus();
		m_sceneName = ActiveSceneName();
		RefreshConsole();
		RefreshSceneList();
	}

	/// Détruit puis reconstruit toute l'interface (changement de thème). L'état
	/// applicatif vit dans `Runtime` : rien n'est perdu.
	void Rebuild() {
		Teardown();
		Build();
	}

	/// Détruit l'arbre d'interface ET les popups, racines indépendantes (cf.
	/// UiFactory::Menu) qui survivraient sinon à chaque reconstruction.
	void Teardown() {
		ExitRunMode();
		for (ecs::Entity popup : m_menuPopups)
			if (popup.Valid())
				ui::DespawnTree(m_ctx.registry, popup);
		m_menuPopups.clear();
		m_tree.Teardown();
		m_inspector.Teardown();
		m_library.Teardown();
		m_assets.Teardown();
		m_documents.Teardown();
		if (m_root.Valid()) {
			ui::DespawnTree(m_ctx.registry, m_root);
			m_root = ecs::Entity{};
		}
		m_maximized = false;
	}

	// ── Thème ────────────────────────────────────────────────────────────────

	[[nodiscard]] const String &ThemeName() const noexcept { return m_themeName; }

	/// Change de thème. Avant la première construction, seule la palette est
	/// posée (reconstruire ici doublerait l'interface au `Build()` suivant).
	bool SetTheme(const String &name) {
		Option<ui::UiTheme> theme = ThemeByName(name);
		if (theme.IsNone())
			return false;
		m_themeName = name.ToLower();
		m_ctx.gui.SetTheme(theme.Unwrap());
		kit::syntax::Colors() = kit::PaletteOf(m_ctx).light ? kit::syntax::Scheme::Light() : kit::syntax::Scheme::Dark();
		if (m_root.Valid())
			Rebuild();
		return true;
	}

	[[nodiscard]] static Option<ui::UiTheme> ThemeByName(const String &name) {
		String key = name.ToLower();
		if (key == "studio")
			return Some(ui::UiTheme::Studio());
		if (key == "dark")
			return Some(ui::UiTheme::Dark());
		if (key == "light")
			return Some(ui::UiTheme::Light());
		if (key == "aero")
			return Some(ui::UiTheme::Aero());
		return NONE;
	}

	// ── Vue 3D ───────────────────────────────────────────────────────────────

	[[nodiscard]] sdl3::FRect ViewportRect() const {
		const ecs::Entity viewport = m_runMode ? m_runViewport : m_viewport;
		auto computed = m_ctx.registry.GetComponent<ui::UiComputed>(viewport);
		return computed.IsSome() ? computed.Unwrap()->screen : sdl3::FRect{};
	}

	/// Vrai si le point est dans la vue 3D ET qu'aucun widget n'y est dessiné
	/// par-dessus (menu déroulé, popup) — sinon le même clic sélectionnerait
	/// aussi l'objet derrière le menu.
	[[nodiscard]] bool PointerOverViewport(float x, float y) const {
		const sdl3::FRect rect = ViewportRect();
		if (rect.w <= 0.f || rect.h <= 0.f)
			return false;
		if (x < rect.x || y < rect.y || x >= rect.x + rect.w || y >= rect.y + rect.h)
			return false;
		ecs::Entity front = m_ctx.gui.Input().FrontMostAt(m_ctx.registry, m_ctx.gui.Layout(), sdl3::FPoint{x, y});
		const ecs::Entity viewport = m_runMode ? m_runViewport : m_viewport;
		return !front.Valid() || ui::IsDescendantOrSelf(m_ctx.registry, front, viewport);
	}

	[[nodiscard]] bool PointerInViewport() const {
		float x = 0.f, y = 0.f;
		SDL_GetMouseState(&x, &y);
		return PointerOverViewport(x, y);
	}

	[[nodiscard]] Option<math::FRay> RayAt(float screenX, float screenY) const {
		const sdl3::FRect rect = ViewportRect();
		if (!PointerOverViewport(screenX, screenY))
			return NONE;
		return Some(Rt().ViewportRay(screenX - rect.x, screenY - rect.y, rect.w, rect.h));
	}

	// ── Mode Jeu plein écran (« Run Mode ») ──────────────────────────────────

	[[nodiscard]] bool IsRunMode() const noexcept { return m_runMode; }

	/// Lance le jeu en plein écran : l'interface disparaît, la vue remplit la
	/// fenêtre avec un réticule et la liste des scripts actifs ; Échap revient
	/// à l'éditeur (et arrête la partie, qui restaure la scène).
	void EnterRunMode() {
		if (m_runMode || !m_root.Valid())
			return;
		if (!Rt().IsPlaying())
			Rt().Play();
		m_runMode = true;
		kit::SetHidden(m_ctx, m_root, true);

		ui::WidgetBuilder root = m_ctx.factory.Column();
		root.Fixed().Anchor(ui::Anchor::TopLeft).W(ui::Dimension::Rpct(100.f)).H(ui::Dimension::Rpct(100.f));
		root.Gap(0.f).Pad(0.f).Bg(kit::Rgb(0, 0, 0));
		m_runRoot = root.Spawn();

		ui::WidgetBuilder viewport = m_ctx.factory.Viewport3D(Rt().SceneRoot(), Rt().ActiveCamera());
		viewport.GrowW().GrowH().Parent(m_runRoot);
		m_runViewport = viewport.Spawn();

		// Réticule au centre.
		ui::WidgetBuilder crosshair = m_ctx.factory.Canvas([](sdl3::Renderer &ren, sdl3::FRect r) {
			const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
			ren.SetDrawColor(sdl3::FColor{1.f, 1.f, 1.f, 0.85f});
			ren.FillRect({cx - 11.f, cy - 1.f, 8.f, 2.f});
			ren.FillRect({cx + 3.f, cy - 1.f, 8.f, 2.f});
			ren.FillRect({cx - 1.f, cy - 11.f, 2.f, 8.f});
			ren.FillRect({cx - 1.f, cy + 3.f, 2.f, 8.f});
		});
		crosshair.Absolute().Anchor(ui::Anchor::TopLeft).W(ui::Dimension::Rpct(100.f)).H(ui::Dimension::Rpct(100.f));
		crosshair.PointerThrough().Parent(m_runViewport);
		(void)crosshair.Spawn();

		ui::WidgetBuilder hint = m_ctx.factory.Label(String("Échap : revenir à l'éditeur · ZQSD/WASD + souris : se déplacer"));
		hint.Absolute().Anchor(ui::Anchor::TopLeft).Offset(14.f, 12.f).WAuto().HAuto().FontSize(12.f);
		hint.TextColor(sdl3::FColor{1.f, 1.f, 1.f, 0.6f}).PointerThrough().Parent(m_runViewport);
		(void)hint.Spawn();

		// Encart « MODE TEST » : les scripts qui tournent.
		ui::WidgetBuilder panel = m_ctx.factory.Column();
		panel.Absolute().Anchor(ui::Anchor::BottomRight).Offset(-16.f, -16.f).WAuto().HAuto();
		panel.Gap(4.f).Pad(math::Sides{12.f, 8.f}).Radius(6.f).Bg(sdl3::FColor{0.08f, 0.08f, 0.09f, 0.82f});
		panel.BorderColor(kit::PaletteOf(m_ctx).separator).PointerThrough().Parent(m_runViewport);
		m_runPanel = panel.Spawn();
		RefreshRunPanel();
		m_ctx.gui.Layout().MarkDirty();
		SetStatus(String("Mode Jeu plein écran — Échap pour revenir"));
	}

	void ExitRunMode() {
		if (!m_runMode)
			return;
		m_runMode = false;
		if (m_runRoot.Valid())
			ui::DespawnTree(m_ctx.registry, m_runRoot);
		m_runRoot = m_runViewport = m_runPanel = ecs::Entity{};
		kit::SetHidden(m_ctx, m_root, false);
		if (Rt().IsPlaying())
			Rt().Stop();
	}

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
	void HandleEvent(const sdl3::Event &event) {
		if (event.IsConsumed())
			return;
		if (m_runMode) {
			HandleRunModeEvent(event);
			return;
		}
		if (event.IsMouseDown(SDL_BUTTON_RIGHT))
			m_rightMouseDown = PointerInViewport();
		if (event.IsMouseUp(SDL_BUTTON_RIGHT))
			m_rightMouseDown = false;
		if (event.IsMouseDown(SDL_BUTTON_MIDDLE))
			m_middleMouseDown = PointerInViewport();
		if (event.IsMouseUp(SDL_BUTTON_MIDDLE))
			m_middleMouseDown = false;
		if (event.IsMouseUp(SDL_BUTTON_LEFT))
			m_orbiting = false;

		if (event.IsMouseMotion() && m_rightMouseDown) {
			constexpr float SENSITIVITY = 0.0035f;
			Rt().ApplyLookDelta(float(event.MouseMotion().xrel) * SENSITIVITY,
								-float(event.MouseMotion().yrel) * SENSITIVITY);
		}
		if (event.IsMouseMotion() && m_middleMouseDown) {
			const float scale = Rt().PivotDistance() * 0.0016f;
			Rt().PanCamera(-float(event.MouseMotion().xrel) * scale, float(event.MouseMotion().yrel) * scale);
		}
		if (event.IsMouseMotion() && m_orbiting) {
			constexpr float SENSITIVITY = 0.006f;
			Rt().OrbitCamera(float(event.MouseMotion().xrel) * SENSITIVITY,
							 -float(event.MouseMotion().yrel) * SENSITIVITY, m_orbitPivot);
		}
		if (event.IsMouseWheel() && PointerInViewport())
			Rt().DollyCamera(float(event.MouseWheel().y) * Rt().PivotDistance() * 0.18f);

		if (event.IsMouseDown(SDL_BUTTON_LEFT) && (SDL_GetModState() & SDL_KMOD_ALT) != 0) {
			if (RayAt(event.MouseButton().x, event.MouseButton().y).IsSome()) {
				m_orbitPivot = Rt().CameraPivot();
				m_orbiting = true;
			}
		} else if (event.IsMouseDown(SDL_BUTTON_LEFT)) {
			if (Option<math::FRay> ray = RayAt(event.MouseButton().x, event.MouseButton().y); ray.IsSome()) {
				const Runtime::GizmoAxis axis = Rt().PickGizmoAxis(ray.Unwrap());
				if (axis != Runtime::GizmoAxis::NONE)
					(void)Rt().BeginGizmoDrag(axis, ray.Unwrap());
				else
					(void)Rt().SelectAt(ray.Unwrap());
			}
		}
		if (event.IsMouseUp(SDL_BUTTON_LEFT))
			Rt().EndGizmoDrag();
		if (event.IsMouseMotion() && !m_rightMouseDown && !m_middleMouseDown && !m_orbiting) {
			if (Option<math::FRay> ray = RayAt(event.MouseMotion().x, event.MouseMotion().y); ray.IsSome()) {
				if (Rt().IsGizmoDragging())
					(void)Rt().UpdateGizmoDrag(ray.Unwrap());
				else
					Rt().SetHoveredAxis(Rt().PickGizmoAxis(ray.Unwrap()));
			} else if (!Rt().IsGizmoDragging()) {
				Rt().SetHoveredAxis(Runtime::GizmoAxis::NONE);
			}
		}

		const bool control = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
		// Ctrl+S et Ctrl+Entrée valent AUSSI dans un champ (c'est là qu'on
		// écrit un script) : la règle de transmission de l'interface les
		// laisse passer, comme les touches de fonction.
		if (control && event.IsKeyDown(SDLK_S)) {
			if (!m_documents.SaveActive())
				OnSaveProject();
			return;
		}
		if (control && event.IsKeyDown(SDLK_RETURN)) {
			m_documents.RunActive();
			return;
		}

		if (control && event.IsKeyDown(SDLK_Z)) {
			if ((SDL_GetModState() & SDL_KMOD_SHIFT) != 0)
				OnRedo();
			else
				OnUndo();
		}
		if (control && event.IsKeyDown(SDLK_Y))
			OnRedo();
		if (control && event.IsKeyDown(SDLK_D))
			OnDuplicate();
		if (!control && !m_rightMouseDown) {
			if (event.IsKeyDown(SDLK_W))
				SetGizmoMode(Runtime::GizmoMode::TRANSLATE);
			if (event.IsKeyDown(SDLK_E))
				SetGizmoMode(Runtime::GizmoMode::ROTATE);
			if (event.IsKeyDown(SDLK_R))
				SetGizmoMode(Runtime::GizmoMode::SCALE);
			if (event.IsKeyDown(SDLK_X))
				ToggleSnap();
		}
		if (event.IsKeyDown(SDLK_F5))
			Rt().TogglePlay();
		if (event.IsKeyDown(SDLK_F6))
			EnterRunMode();
		if (event.IsKeyDown(SDLK_F2))
			m_tree.BeginRename(Rt().SelectedId());
		if (event.IsKeyDown(SDLK_DELETE))
			OnDeleteSelection();
		if (event.IsKeyDown(SDLK_F))
			OnFocusSelection();
		if (event.IsKeyDown(SDLK_ESCAPE) && !m_ctx.gui.HasOpenModal())
			Rt().ClearSelection();
	}

	/// Une image : caméra au clavier, vues à jour, rafraîchissements différés.
	void Tick(float dt, double fps) {
		m_fps = fps;
		PollCameraKeys(dt);
		const float aspect = UpdateViewportCamera(m_runMode ? m_runViewport : m_viewport);
		if (aspect > 0.f)
			Rt().SetViewportAspect(aspect);

		// Changement de scène : les identifiants de nœud ne désignent plus
		// les mêmes objets, l'état de dépli de l'arbre repart de zéro.
		const String sceneName = ActiveSceneName();
		if (sceneName != m_sceneName) {
			m_sceneName = sceneName;
			m_tree.ResetExpansion();
			m_documents.Dock().SetTitle(0, ViewportTitle());
			m_library.RefreshWorld();
			m_scenesDirty = true;
		}

		if (m_runMode) {
			if (++m_tickCount % RUN_PANEL_REFRESH_FRAMES == 0)
				RefreshRunPanel();
			if (!Rt().IsPlaying())
				ExitRunMode(); // la partie s'est arrêtée (script, F5)
			return;
		}

		if (m_treeDock.Active() == 0)
			m_tree.Tick();
		if (m_scenesDirty && m_treeDock.Active() == 1) {
			m_scenesDirty = false;
			RefreshSceneList();
		}
		m_inspector.Tick();
		m_assets.Tick(dt);
		m_documents.Tick();
		if (m_consoleDirty && m_consoleDock.Active() == 0) {
			m_consoleDirty = false;
			RefreshConsole();
		}
		if (++m_tickCount % PROFILER_REFRESH_FRAMES == 0 && m_consoleDock.Active() == 1)
			RefreshProfiler();
		RefreshStatusBar();
	}

	// ── Pilotage par script ──────────────────────────────────────────────────

	/// `editor.open_panel(nom, onglet)` — met un panneau au premier plan.
	bool FocusPanel(const String &panel, int tab) {
		const String key = panel.ToLower();
		if (key == "outliner" || key == "tree") {
			m_treeDock.SetActive(0);
			return true;
		}
		if (key == "scenes") {
			m_treeDock.SetActive(1);
			return true;
		}
		if (key == "assets") {
			m_assetDock.SetActive(0);
			return true;
		}
		if (key == "inspector") {
			if (tab == int(InspectorTab::WORLD)) {
				m_libraryDock.SetActive(2);
				return true;
			}
			m_inspectorDock.SetActive(0);
			m_inspector.FocusSection(String(InspectorPanel::SectionForTab(tab)));
			return true;
		}
		if (key == "materials" || key == "library") {
			m_libraryDock.SetActive(0);
			return true;
		}
		if (key == "scripts") {
			m_libraryDock.SetActive(1);
			return true;
		}
		if (key == "world") {
			m_libraryDock.SetActive(2);
			return true;
		}
		if (key == "console") {
			m_consoleDock.SetActive(0);
			return true;
		}
		if (key == "profiler") {
			m_consoleDock.SetActive(1);
			return true;
		}
		if (key == "script") {
			m_documents.OpenToolConsole();
			return true;
		}
		if (key == "viewport") {
			m_documents.Dock().SetActive(0);
			return true;
		}
		return false;
	}

	void SetStatus(String message) {
		m_statusMessage = std::move(message);
		kit::SetLabelText(m_ctx, m_statusMessageLabel, m_statusMessage);
	}

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
	bool UiCommand(const String &command, const String &argument) {
		const String cmd = command.ToLower();
		auto nodeId = [this](const String &name) { return Rt().ResolveId(name); };
		if (cmd == "select") {
			const scene::NodeId id = nodeId(argument);
			return id.Valid() && Rt().Select(id);
		}
		if (cmd == "context_menu") {
			const scene::NodeId id = nodeId(argument);
			if (!id.Valid())
				return false;
			(void)Rt().Select(id);
			m_tree.RevealSelection();
			m_tree.Tick(); // lignes à jour ; leur position vient au prochain rendu
			Option<sdl3::FRect> row = m_tree.RowRect(id);
			const sdl3::FRect r = row.IsSome() ? row.Unwrap() : sdl3::FRect{40.f, 200.f, 200.f, 22.f};
			m_tree.OpenContextMenu(id, r.x + r.w * 0.55f, r.y + r.h * 0.7f);
			return true;
		}
		if (cmd == "section") {
			m_inspectorDock.SetActive(0);
			m_inspector.FocusSection(argument);
			return true;
		}
		if (cmd == "context_submenu")
			return m_tree.OpenCreateSubmenu();
		if (cmd == "create")
			return m_tree.CreateNode(argument, Rt().SelectedId()).IsSome();
		if (cmd == "rename") {
			const scene::NodeId id = nodeId(argument);
			if (!id.Valid())
				return false;
			m_tree.BeginRename(id);
			return true;
		}
		if (cmd == "close_menus") {
			std::vector<ecs::Entity> open;
			m_ctx.registry.Query<ui::UiPopupState>([&open](ecs::Entity e, ui::UiPopupState &state) {
				if (state.open)
					open.push_back(e);
			});
			for (ecs::Entity e : open) {
				if (m_ctx.registry.GetComponent<ui::UiPopupState>(e).Unwrap()->modal)
					m_ctx.gui.CloseModal(e);
				else
					m_ctx.gui.ClosePopup(e);
			}
			return true;
		}
		if (cmd == "browse") {
			m_assetDock.SetActive(0);
			const String where = argument.StartsWith(AssetBrowserModel::ROOT) ? argument
								 : argument.IsEmpty()                     ? String(AssetBrowserModel::ROOT)
																		  : m_assetsDir + String("/") + argument;
			const bool moved = m_assets.Model().Navigate(where) || m_assets.Model().Location() == where;
			m_assets.MarkDirty();
			return moved;
		}
		if (cmd == "select_asset")
			return m_assets.SelectByName(argument);
		if (cmd == "open_script") {
			m_documents.OpenLibraryScript(argument);
			return m_documents.Active() != nullptr;
		}
		if (cmd == "open_scene_script") {
			m_documents.OpenSceneScript(argument.IsEmpty() ? ActiveSceneName() : argument);
			return m_documents.Active() != nullptr;
		}
		if (cmd == "open_console") {
			m_documents.OpenToolConsole();
			return true;
		}
		if (cmd == "open_json") {
			const size_t bar = argument.Find('|');
			const String name = bar == String::NPOS ? argument : argument.Substr(0, bar);
			const String component = bar == String::NPOS ? String() : argument.Substr(bar + 1);
			const scene::NodeId id = nodeId(name);
			if (!id.Valid())
				return false;
			m_documents.OpenComponentJson(id, component);
			return m_documents.Active() != nullptr;
		}
		if (cmd == "document") {
			m_documents.Dock().SetActive(int(std::strtol(argument.CStr(), nullptr, 10)));
			return true;
		}
		if (cmd == "library") {
			m_libraryDock.SetActive(int(std::strtol(argument.CStr(), nullptr, 10)));
			return true;
		}
		if (cmd == "run_mode") {
			if (argument == "off")
				ExitRunMode();
			else
				EnterRunMode();
			return true;
		}
		if (cmd == "maximize") {
			ToggleMaximized();
			return true;
		}
		return false;
	}

	// ── Accès (scénarios, tests) ─────────────────────────────────────────────

	[[nodiscard]] SceneTreePanel &Tree() noexcept { return m_tree; }
	[[nodiscard]] InspectorPanel &Inspector() noexcept { return m_inspector; }
	[[nodiscard]] AssetBrowserPanel &Assets() noexcept { return m_assets; }
	[[nodiscard]] DocumentArea &Documents() noexcept { return m_documents; }

	/// Installé par l'application : exporte le rapport depuis le menu.
	std::function<void()> onExportReport;
	/// Installé par l'application : Fichier › Quitter.
	std::function<void()> onQuit;

	/// Chemin utilisé par Ouvrir/Enregistrer (cf. SetDirectories).
	String projectPath = "saves/game_editor_demo/projects/game_editor_demo_project.json";

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

	InspectorActions MakeInspectorActions() {
		InspectorActions actions;
		actions.openScript = [this](const String &name) { m_documents.OpenLibraryScript(name); };
		actions.editJson = [this](scene::NodeId id, const String &type) { m_documents.OpenComponentJson(id, type); };
		actions.status = [this](const String &text) { SetStatus(text); };
		return actions;
	}

	AssetActions MakeAssetActions() {
		AssetActions actions;
		actions.openScript = [this](const String &name) { m_documents.OpenLibraryScript(name); };
		actions.openSceneScript = [this](const String &scene) { m_documents.OpenSceneScript(scene); };
		actions.openFile = [this](const String &path) { m_documents.OpenFile(path); };
		actions.importModel = [this](const String &path) { ImportModel(path); };
		actions.openScene = [this](const String &scene) { (void)Rt().SwitchScene(scene); };
		actions.instantiateScene = [this](const String &path) {
			auto instance = Rt().InstantiateSceneFile(path);
			if (instance.IsError()) {
				Rt().LogError(String::Format("Instanciation : %s", instance.Error().CStr()));
				return;
			}
			(void)Rt().Select(instance.Value());
			SetStatus(String::Format("Scène instanciée : %s", path.CStr()));
		};
		actions.status = [this](const String &text) { SetStatus(text); };
		return actions;
	}

	[[nodiscard]] String ActiveSceneName() const {
		const SceneDesc *scene = Rt().ActiveScene();
		return scene ? scene->name : String();
	}

	[[nodiscard]] String ViewportTitle() const { return String::Format("Scène : %s", ActiveSceneName().CStr()); }

	/// Point à 7 unités devant la caméra, remonté au sol s'il tombe dessous —
	/// là où apparaît ce qu'on crée à la racine.
	[[nodiscard]] math::FVector3 SpawnPointInFrontOfCamera(float lift) const {
		const render3d::Camera &camera = Rt().ActiveCamera();
		const math::FVector3 forward = (camera.target - camera.position).Normalize();
		math::FVector3 at = camera.position + forward * 7.f;
		at.y = sdl3::Max(at.y, lift * 0.5f);
		return at;
	}

	// ── Mise en page : poignées de redimensionnement ─────────────────────────

	/// Poignée entre deux panneaux. `before` : la poignée SUIT `target` (la
	/// tirer vers la droite/le bas l'agrandit) ; sinon elle le PRÉCÈDE et le
	/// rétrécit (colonne de droite, rangée du bas — cf. ui::ResolveResizeDrag,
	/// qui applique le signe opposé au panneau « après »).
	ecs::Entity ResizeHandle(ecs::Entity parent, ui::Orientation orient, ecs::Entity target, bool before, float minSize,
							 float maxSize) {
		const bool horizontal = orient == ui::Orientation::Horizontal;
		ui::WidgetBuilder handle = m_ctx.factory.Row();
		handle.Pad(0.f);
		if (horizontal)
			handle.W(ui::Dimension::Px(5.f)).GrowH();
		else
			handle.GrowW().H(ui::Dimension::Px(5.f));
		handle.Bg(kit::PaletteOf(m_ctx).base).Parent(parent);
		ecs::Entity entity = handle.Spawn();

		ui::UiResizeHandle h;
		h.orient = orient;
		ecs::ArchetypeRegistry *world = &m_ctx.registry;
		auto get = [world, target, horizontal]() -> float {
			if (auto item = world->GetComponent<ui::UiItem>(target); item.IsSome())
				return horizontal ? item.Unwrap()->width.value : item.Unwrap()->height.value;
			return 0.f;
		};
		auto set = [world, target, horizontal](float v) {
			if (auto item = world->GetComponent<ui::UiItem>(target); item.IsSome()) {
				if (horizontal)
					item.Unwrap()->width = ui::Dimension::Px(v);
				else
					item.Unwrap()->height = ui::Dimension::Px(v);
			}
		};
		if (before) {
			h.getBefore = get;
			h.setBefore = set;
			h.minBefore = minSize;
			h.maxBefore = maxSize;
		} else {
			h.getAfter = get;
			h.setAfter = set;
			h.minAfter = minSize;
			h.maxAfter = maxSize;
		}
		m_ctx.registry.AddComponent(entity, std::move(h));
		return entity;
	}

	/// Place le DERNIER enfant de `parent` (la poignée qu'on vient d'ajouter)
	/// juste avant `anchor` : une poignée « après » doit précéder son panneau.
	void MoveBefore(ecs::Entity parent, ecs::Entity anchor) {
		auto children = m_ctx.registry.GetComponent<ui::UiChildren>(parent);
		if (children.IsNone() || children.Unwrap()->list.size() < 2)
			return;
		std::vector<ecs::Entity> &list = children.Unwrap()->list;
		const ecs::Entity moved = list.back();
		list.pop_back();
		list.insert(std::find(list.begin(), list.end(), anchor), moved);
		m_ctx.gui.Layout().MarkDirty();
	}

	// ── Barre de menus ───────────────────────────────────────────────────────

	[[nodiscard]] ui::WidgetBuilder MenuAction(const char *text, const char *shortcut, std::function<void()> action) {
		ui::WidgetBuilder item = m_ctx.factory.MenuItem(String(text), String(shortcut));
		item.OnClick(std::move(action));
		return item;
	}

	void AddItem(ecs::Entity menu, const char *text, const char *shortcut, std::function<void()> action) {
		ui::WidgetBuilder item = MenuAction(text, shortcut, std::move(action));
		item.Parent(menu);
		(void)item.Spawn();
	}

	void BuildMenuBar(ecs::Entity parent) {
		ui::WidgetBuilder bar = m_ctx.factory.MenuBar();
		bar.Parent(parent).GrowW().HAuto().Pad(math::Sides{6.f, 2.f}).Bg(kit::PaletteOf(m_ctx).header);
		ecs::Entity barEntity = bar.Spawn();

		ecs::Entity file = m_ctx.factory.Menu(
			barEntity, String("Fichier"), MenuAction("Nouveau projet", "", [this] { OnNewProject(); }),
			MenuAction("Ouvrir le projet", "", [this] { OnOpenProject(); }),
			MenuAction("Enregistrer le projet", "Ctrl+S", [this] { OnSaveProject(); }),
			MenuAction("Enregistrer la sélection comme scène", "", [this] { OnSaveSelectionAsScene(); }),
			MenuAction("Exporter le rapport", "", [this] { OnExportReport(); }),
			MenuAction("Quitter", "Ctrl+Q", [this] {
				if (onQuit)
					onQuit();
			}));
		m_menuPopups.push_back(file);
		ecs::Entity scenes = m_ctx.factory.SubMenu(file, String("Ouvrir une scène"));
		m_menuPopups.push_back(scenes);
		for (const String &name : Rt().GetProject().SceneNames())
			AddItem(scenes, name.CStr(), "", [this, name] { (void)Rt().SwitchScene(name); });

		m_menuPopups.push_back(m_ctx.factory.Menu(
			barEntity, String("Édition"), MenuAction("Annuler", "Ctrl+Z", [this] { OnUndo(); }),
			MenuAction("Rétablir", "Ctrl+Y", [this] { OnRedo(); }),
			MenuAction("Dupliquer", "Ctrl+D", [this] { OnDuplicate(); }),
			MenuAction("Supprimer", "Suppr", [this] { OnDeleteSelection(); }),
			MenuAction("Renommer…", "F2", [this] { m_tree.BeginRename(Rt().SelectedId()); }),
			MenuAction("Tout désélectionner", "Échap", [this] { Rt().ClearSelection(); }),
			MenuAction("Déplacer", "W", [this] { SetGizmoMode(Runtime::GizmoMode::TRANSLATE); }),
			MenuAction("Tourner", "E", [this] { SetGizmoMode(Runtime::GizmoMode::ROTATE); }),
			MenuAction("Redimensionner", "R", [this] { SetGizmoMode(Runtime::GizmoMode::SCALE); }),
			MenuAction("Magnétisme", "X", [this] { ToggleSnap(); })));

		ecs::Entity tools = m_ctx.factory.Menu(
			barEntity, String("Outils"), MenuAction("Console de script", "", [this] { m_documents.OpenToolConsole(); }),
			MenuAction("Script de la scène", "", [this] { m_documents.OpenSceneScript(ActiveSceneName()); }),
			MenuAction("Vérifier tous les scripts", "", [this] { OnCheckAllScripts(); }),
			MenuAction("Cadrer la sélection", "F", [this] { OnFocusSelection(); }),
			MenuAction("Parcourir les modèles", "", [this] {
				m_assetDock.SetActive(0);
				if (m_assets.Model().Navigate(m_assetsDir + String("/models")))
					m_assets.MarkDirty();
			}));
		m_menuPopups.push_back(tools);
		ecs::Entity create = m_ctx.factory.SubMenu(tools, String("Créer un nœud"));
		m_menuPopups.push_back(create);
		for (const NodeTemplate &entry : NODE_TEMPLATES) {
			const String key(entry.key);
			AddItem(create, entry.label, "", [this, key] {
				const SceneDesc *scene = Rt().ActiveScene();
				(void)m_tree.CreateNode(key, scene ? scene->tree.Root() : scene::NodeId{});
			});
		}

		ecs::Entity window = m_ctx.factory.Menu(
			barEntity, String("Fenêtre"), MenuAction("Arbre de scène", "", [this] { (void)FocusPanel("tree", 0); }),
			MenuAction("Inspecteur", "", [this] { (void)FocusPanel("inspector", 0); }),
			MenuAction("Ressources", "", [this] { (void)FocusPanel("assets", 0); }),
			MenuAction("Console", "", [this] { (void)FocusPanel("console", 0); }),
			MenuAction("Profileur", "", [this] { (void)FocusPanel("profiler", 0); }),
			MenuAction("Vue 3D seule", "", [this] { ToggleMaximized(); }));
		m_menuPopups.push_back(window);
		ecs::Entity themes = m_ctx.factory.SubMenu(window, String("Thème"));
		m_menuPopups.push_back(themes);
		AddItem(themes, "Atelier (par défaut)", "", [this] { (void)SetTheme("studio"); });
		AddItem(themes, "Sombre", "", [this] { (void)SetTheme("dark"); });
		AddItem(themes, "Clair", "", [this] { (void)SetTheme("light"); });
		AddItem(themes, "Verre", "", [this] { (void)SetTheme("aero"); });

		m_menuPopups.push_back(m_ctx.factory.Menu(
			barEntity, String("Compiler"), MenuAction("Lancer dans la vue", "F5", [this] { Rt().TogglePlay(); }),
			MenuAction("Lancer en plein écran", "F6", [this] { EnterRunMode(); }),
			MenuAction("Arrêter", "", [this] { Rt().Stop(); }),
			MenuAction("Vérifier les scripts", "", [this] { OnCheckAllScripts(); }),
			MenuAction("Exporter le rapport", "", [this] { OnExportReport(); })));

		m_menuPopups.push_back(m_ctx.factory.Menu(
			barEntity, String("Aide"), MenuAction("Raccourcis", "", [this] { OnShowShortcuts(); }),
			MenuAction("À propos", "", [this] { OnAbout(); })));
	}

	/// Menus ≡ des panneaux.
	void BuildDockMenus() {
		m_treeMenu = m_ctx.factory.ContextMenu();
		m_menuPopups.push_back(m_treeMenu);
		AddItem(m_treeMenu, "Tout déplier", "", [this] { m_tree.ExpandAll(true); });
		AddItem(m_treeMenu, "Tout replier", "", [this] { m_tree.ExpandAll(false); });
		AddItem(m_treeMenu, "Montrer la sélection", "", [this] { m_tree.RevealSelection(); });

		m_documentsMenu = m_ctx.factory.ContextMenu();
		m_menuPopups.push_back(m_documentsMenu);
		AddItem(m_documentsMenu, "Console de script", "", [this] { m_documents.OpenToolConsole(); });
		AddItem(m_documentsMenu, "Script de la scène", "", [this] { m_documents.OpenSceneScript(ActiveSceneName()); });
		AddItem(m_documentsMenu, "Fermer tous les documents", "", [this] {
			while (m_documents.Dock().Count() > 1)
				m_documents.Close(m_documents.Dock().Count() - 1);
		});
		AddItem(m_documentsMenu, "Vue 3D seule", "", [this] { ToggleMaximized(); });

		m_inspectorMenu = m_ctx.factory.ContextMenu();
		m_menuPopups.push_back(m_inspectorMenu);
		AddItem(m_inspectorMenu, "Modifier les propriétés en JSON…", "", [this] {
			if (Rt().SelectedId().Valid())
				m_documents.OpenComponentJson(Rt().SelectedId(), String());
		});
		AddItem(m_inspectorMenu, "Cadrer la sélection", "F", [this] { OnFocusSelection(); });

		m_assetsMenu = m_ctx.factory.ContextMenu();
		m_menuPopups.push_back(m_assetsMenu);
		const std::pair<const char *, const char *> places[] = {
			{"Projet", AssetBrowserModel::ROOT},        {"Scènes", AssetBrowserModel::SCENES},
			{"Scripts", AssetBrowserModel::SCRIPTS},    {"Modèles", "/models"},
			{"Textures", "/textures"},                  {"Sons", "/sounds"},
		};
		for (const auto &[label, location] : places) {
			// Les dossiers du disque sont relatifs à la racine des ressources.
			const String where = location[0] == '/' ? m_assetsDir + String(location) : String(location);
			AddItem(m_assetsMenu, label, "", [this, where] {
				if (m_assets.Model().Navigate(where))
					m_assets.MarkDirty();
			});
		}
	}

	void OpenMenu(ecs::Entity menu, float x, float y) { m_ctx.gui.OpenPopupAt(menu, sdl3::FPoint{x - 180.f, y}); }

	// ── Vue 3D : barre d'outils, superpositions ──────────────────────────────

	void BuildViewportPage(ecs::Entity page) {
		const ui::UiTheme &theme = m_ctx.Theme();
		ui::WidgetBuilder bar = m_ctx.factory.Row();
		bar.Gap(2.f).Pad(math::Sides{6.f, 3.f}).GrowW().HAuto().Align(ui::CrossAlign::Center);
		bar.Bg(kit::PaletteOf(m_ctx).toolbar).Parent(page);
		ecs::Entity barEntity = bar.Spawn();

		m_gizmoButtons[0] = kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::OPEN_WITH, String("Déplacer (W)"),
											[this] { SetGizmoMode(Runtime::GizmoMode::TRANSLATE); }, 26.f);
		m_gizmoButtons[1] = kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::ROTATE_RIGHT, String("Tourner (E)"),
											[this] { SetGizmoMode(Runtime::GizmoMode::ROTATE); }, 26.f);
		m_gizmoButtons[2] = kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::ZOOM_OUT_MAP, String("Redimensionner (R)"),
											[this] { SetGizmoMode(Runtime::GizmoMode::SCALE); }, 26.f);
		m_snapButton = kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::GRID_ON, String("Magnétisme (X)"),
									   [this] { ToggleSnap(); }, 26.f);
		AddToolbarSeparator(barEntity);
		(void)kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::PLAY_ARROW, String("Lancer / arrêter dans la vue (F5)"),
							  [this] { Rt().TogglePlay(); }, 26.f, &m_playIcon, kit::PaletteOf(m_ctx).ok);
		(void)kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::SPORTS_ESPORTS, String("Lancer en plein écran (F6)"),
							  [this] { EnterRunMode(); }, 26.f);
		(void)kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::CENTER_FOCUS_STRONG, String("Cadrer la sélection (F)"),
							  [this] { OnFocusSelection(); }, 26.f);
		(void)kit::Spacer(m_ctx, barEntity);

		std::vector<String> sceneNames = Rt().GetProject().SceneNames();
		int active = 0;
		for (size_t i = 0; i < sceneNames.size(); ++i)
			if (sceneNames[i] == ActiveSceneName())
				active = int(i);
		ui::WidgetBuilder combo = m_ctx.factory.Combo(sceneNames, active);
		combo.Size(170.f, 24.f).FontSize(13.f).Tooltip(String("Scène active")).Parent(barEntity);
		combo.OnChange([this, sceneNames](float index) {
			const size_t i = size_t(index < 0.f ? 0.f : index);
			if (i < sceneNames.size())
				(void)Rt().SwitchScene(sceneNames[i]);
		});
		m_sceneCombo = combo.Spawn();

		ui::WidgetBuilder searchRow = m_ctx.factory.Row();
		searchRow.Pad(0.f);
		searchRow.Gap(4.f).W(ui::Dimension::Px(200.f)).HAuto().Align(ui::CrossAlign::Center).Parent(barEntity);
		ecs::Entity searchRowEntity = searchRow.Spawn();
		(void)kit::Glyph(m_ctx, searchRowEntity, ui::MaterialIcons::SEARCH, theme.muted, 15.f);
		ui::WidgetBuilder search = m_ctx.factory.Input(String("Aller à un nœud…"));
		search.GrowW().H(ui::Dimension::Px(24.f)).FontSize(13.f).Parent(searchRowEntity);
		search.OnSubmit([this](const String &text) { GoToNode(text); });
		(void)search.Spawn();
		(void)kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::FULLSCREEN, String("Vue 3D seule (masquer les panneaux)"),
							  [this] { ToggleMaximized(); }, 26.f);

		// La vue elle-même, avec les superpositions (axes, projection, badge).
		ui::WidgetBuilder viewport = m_ctx.factory.Viewport3D(Rt().SceneRoot(), Rt().ActiveCamera());
		viewport.GrowW().GrowH().Parent(page);
		// Glisser une vignette de modèle du navigateur dans la vue : import.
		viewport.DropTarget(String("asset"));
		viewport.OnDrop([this](int64_t index) { m_assets.Activate(size_t(index)); });
		m_viewport = viewport.Spawn();

		Runtime *runtime = &Rt();
		ui::WidgetBuilder axes = m_ctx.factory.Canvas([runtime](sdl3::Renderer &ren, sdl3::FRect r) {
			DrawAxisGizmo(ren, r, runtime->ActiveCamera());
		});
		axes.Absolute().Anchor(ui::Anchor::TopRight).Offset(-10.f, 10.f).Size(92.f, 92.f).PointerThrough();
		axes.Parent(m_viewport);
		(void)axes.Spawn();
		ui::WidgetBuilder projection = m_ctx.factory.Label(String("‹ Persp"));
		projection.Absolute().Anchor(ui::Anchor::TopRight).Offset(-30.f, 104.f).WAuto().HAuto().FontSize(12.f);
		projection.TextColor(sdl3::FColor{1.f, 1.f, 1.f, 0.65f}).PointerThrough().Parent(m_viewport);
		(void)projection.Spawn();

		ui::WidgetBuilder badge = m_ctx.factory.Label(String("▶  MODE JEU — F5 pour arrêter"));
		badge.Absolute().Anchor(ui::Anchor::TopLeft).Offset(12.f, 10.f).WAuto().HAuto().FontSize(13.f).Bold();
		badge.Pad(math::Sides{10.f, 4.f}).Radius(4.f).Bg(sdl3::FColor{0.12f, 0.45f, 0.18f, 0.85f});
		badge.TextColor(sdl3::FColor::WHITE()).PointerThrough().Parent(m_viewport);
		m_playBadge = badge.Spawn();
		kit::SetHidden(m_ctx, m_playBadge, true);
	}

	void AddToolbarSeparator(ecs::Entity parent) {
		ui::WidgetBuilder separator = m_ctx.factory.Row();
		separator.Pad(0.f);
		separator.Size(1.f, 20.f).Bg(kit::PaletteOf(m_ctx).separator).Margin(math::Sides{4.f, 0.f}).Parent(parent);
		(void)separator.Spawn();
	}

	/// Petit trièdre d'orientation (coin haut-droit de la vue, comme dans
	/// Godot et Blender) : les trois axes du monde vus par la caméra, X rouge,
	/// Y vert, Z bleu ; les demi-axes négatifs en pastilles pâles ; le plus
	/// lointain dessiné en premier. Les lettres sont tracées au trait : aucune
	/// police n'est disponible dans un `Canvas`.
	static void DrawAxisGizmo(sdl3::Renderer &ren, sdl3::FRect r, const render3d::Camera &camera) {
		const math::FVector3 forward = (camera.target - camera.position).Normalize();
		math::FVector3 right = forward.Cross(math::FVector3{0.f, 1.f, 0.f});
		if (right.LengthSq() < 1e-6f)
			right = math::FVector3{1.f, 0.f, 0.f};
		right = right.Normalize();
		const math::FVector3 up = right.Cross(forward).Normalize();
		const sdl3::FPoint center{r.x + r.w * 0.5f, r.y + r.h * 0.5f};
		const float radius = r.w * 0.36f;

		struct Axis {
			sdl3::FColor color;
			char letter;
			bool positive;
			float depth;
			sdl3::FPoint tip;
		};
		std::vector<Axis> axes;
		const math::FVector3 dirs[3] = {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
		const sdl3::FColor colors[3] = {kit::AXIS_X, kit::AXIS_Y, kit::AXIS_Z};
		const char letters[3] = {'X', 'Y', 'Z'};
		for (int i = 0; i < 3; ++i) {
			for (float sign : {1.f, -1.f}) {
				const math::FVector3 d = dirs[i] * sign;
				axes.push_back(Axis{colors[i], letters[i], sign > 0.f, d.Dot(forward),
									sdl3::FPoint{center.x + d.Dot(right) * radius, center.y - d.Dot(up) * radius}});
			}
		}
		std::sort(axes.begin(), axes.end(), [](const Axis &a, const Axis &b) { return a.depth > b.depth; });

		ren.SetDrawColor(sdl3::FColor{0.f, 0.f, 0.f, 0.22f});
		ren.FillCircle(center, r.w * 0.48f);
		for (const Axis &axis : axes) {
			if (axis.positive) {
				ren.SetDrawColor(axis.color);
				for (float offset : {-1.f, 0.f, 1.f}) {
					ren.DrawLine(center.x + offset, center.y, axis.tip.x + offset, axis.tip.y);
					ren.DrawLine(center.x, center.y + offset, axis.tip.x, axis.tip.y + offset);
				}
				ren.FillCircle(axis.tip, 7.5f);
				ren.SetDrawColor(sdl3::FColor{0.07f, 0.07f, 0.08f, 1.f});
				DrawLetter(ren, axis.letter, axis.tip, 3.2f);
			} else {
				ren.SetDrawColor(sdl3::FColor{axis.color.r, axis.color.g, axis.color.b, 0.45f});
				ren.FillCircle(axis.tip, 5.f);
			}
		}
		ren.SetDrawColor(sdl3::FColor{0.85f, 0.85f, 0.88f, 1.f});
		ren.FillCircle(center, 3.5f);
	}

	/// X, Y ou Z au trait, dans un carré de demi-côté `s` centré sur `c`.
	static void DrawLetter(sdl3::Renderer &ren, char letter, sdl3::FPoint c, float s) {
		auto line = [&](float x0, float y0, float x1, float y1) {
			ren.DrawLine(c.x + x0 * s, c.y + y0 * s, c.x + x1 * s, c.y + y1 * s);
			ren.DrawLine(c.x + x0 * s + 0.6f, c.y + y0 * s, c.x + x1 * s + 0.6f, c.y + y1 * s);
		};
		switch (letter) {
			case 'X':
				line(-1.f, -1.f, 1.f, 1.f);
				line(-1.f, 1.f, 1.f, -1.f);
				break;
			case 'Y':
				line(-1.f, -1.f, 0.f, 0.f);
				line(1.f, -1.f, 0.f, 0.f);
				line(0.f, 0.f, 0.f, 1.f);
				break;
			default:
				line(-1.f, -1.f, 1.f, -1.f);
				line(1.f, -1.f, -1.f, 1.f);
				line(-1.f, 1.f, 1.f, 1.f);
				break;
		}
	}

	/// « Vue 3D seule » : masque les colonnes latérales et la rangée du bas
	/// (et leurs poignées).
	void ToggleMaximized() {
		m_maximized = !m_maximized;
		for (ecs::Entity entity : {m_treeDock.Frame(), m_leftHandle, m_bottomRow, Previous(m_bottomRow), m_rightColumn,
								   Previous(m_rightColumn)})
			kit::SetHidden(m_ctx, entity, m_maximized);
		SetStatus(String(m_maximized ? "Vue 3D seule — même bouton pour revenir" : "Panneaux restaurés"));
	}

	[[nodiscard]] ecs::Entity Previous(ecs::Entity entity) const {
		auto parent = m_ctx.registry.GetComponent<ui::UiParent>(entity);
		if (parent.IsNone())
			return ecs::Entity{};
		auto children = m_ctx.registry.GetComponent<ui::UiChildren>(parent.Unwrap()->parent);
		if (children.IsNone())
			return ecs::Entity{};
		const std::vector<ecs::Entity> &list = children.Unwrap()->list;
		for (size_t i = 1; i < list.size(); ++i)
			if (list[i] == entity)
				return list[i - 1];
		return ecs::Entity{};
	}

	/// Recherche « aller à » : premier nœud dont le nom contient le texte.
	void GoToNode(const String &text) {
		const SceneDesc *scene = Rt().ActiveScene();
		const String wanted = text.Trim().ToLower();
		if (!scene || wanted.IsEmpty())
			return;
		scene::NodeId found;
		scene->tree.Traverse(scene->tree.Root(), [&](scene::NodeId id, const scene::Node &node) {
			if (!found.Valid() && id != scene->tree.Root() && node.name.ToLower().Contains(wanted))
				found = id;
		});
		if (!found.Valid()) {
			SetStatus(String::Format("Aucun nœud ne contient « %s »", text.CStr()));
			return;
		}
		(void)Rt().Select(found);
		(void)Rt().FocusOn(found);
	}

	/// Aspect et caméra d'un viewport ; rend l'aspect (0 si pas encore mis en page).
	float UpdateViewportCamera(ecs::Entity entity) {
		auto viewport = m_ctx.registry.GetComponent<ui::UiViewport3D>(entity);
		if (viewport.IsNone())
			return 0.f;
		viewport.Unwrap()->camera = Rt().ActiveCamera();
		const ui::UiViewport3D &vp = *viewport.Unwrap();
		return vp.textureHeight > 0 ? float(vp.textureWidth) / float(vp.textureHeight) : 0.f;
	}

	// ── Mode Jeu plein écran ─────────────────────────────────────────────────

	void HandleRunModeEvent(const sdl3::Event &event) {
		if (event.IsMouseMotion())
			Rt().AddMouseDelta(float(event.MouseMotion().xrel), float(event.MouseMotion().yrel));
		if (event.IsKeyDown(SDLK_ESCAPE) || event.IsKeyDown(SDLK_F6) || event.IsKeyDown(SDLK_F5))
			ExitRunMode();
	}

	void RefreshRunPanel() {
		if (!m_runPanel.Valid())
			return;
		kit::ClearChildren(m_ctx, m_runPanel);
		ui::WidgetBuilder title = m_ctx.factory.Label(String("MODE TEST (Run Mode)"));
		title.FontSize(15.f).WAuto().HAuto().TextColor(sdl3::FColor{0.92f, 0.92f, 0.94f, 1.f}).PointerThrough();
		title.Parent(m_runPanel);
		(void)title.Spawn();
		ui::WidgetBuilder rule = m_ctx.factory.Row();
		rule.Pad(0.f);
		rule.GrowW().H(ui::Dimension::Px(1.f)).Bg(kit::PaletteOf(m_ctx).separator).PointerThrough().Parent(m_runPanel);
		(void)rule.Spawn();
		for (const ScriptStatus &status : Rt().ScriptStatuses()) {
			ui::WidgetBuilder row = m_ctx.factory.Row();
			row.Pad(0.f);
			row.Gap(6.f).WAuto().HAuto().Align(ui::CrossAlign::Center).PointerThrough().Parent(m_runPanel);
			ecs::Entity rowEntity = row.Spawn();
			const bool failed = !status.error.IsEmpty();
			const sdl3::FColor dot = failed ? kit::PaletteOf(m_ctx).error : status.running ? kit::PaletteOf(m_ctx).ok : kit::PaletteOf(m_ctx).warning;
			ui::WidgetBuilder square = m_ctx.factory.Row();
			square.Pad(0.f);
			square.Size(8.f, 8.f).Bg(dot).PointerThrough().Parent(rowEntity);
			(void)square.Spawn();
			const bool sceneScript = status.role == "scène";
			const char *state = failed ? "Erreur" : !status.running ? "Arrêté" : sceneScript ? "Actif" : "Animé";
			const String text = sceneScript ? String::Format("%s.sled (%s)", status.name.CStr(), state)
											: String::Format("%s.sled (%s, %d nœud%s)", status.name.CStr(), state,
															 int(status.nodes), status.nodes > 1 ? "s" : "");
			ui::WidgetBuilder label = m_ctx.factory.Label(text);
			label.FontSize(13.f).WAuto().HAuto().TextColor(sdl3::FColor{0.86f, 0.86f, 0.88f, 1.f}).PointerThrough();
			label.Parent(rowEntity);
			(void)label.Spawn();
		}
	}

	// ── Console, profileur, scènes ───────────────────────────────────────────

	void BuildConsolePage(ecs::Entity page) {
		ui::WidgetBuilder console = m_ctx.factory.InputArea(String("(journal vide)"));
		console.GrowW().GrowH().FontSize(12.f).IoMode(ui::IOMode::READ_AND_COPY_ONLY).Monospace();
		console.Highlighter(kit::syntax::HighlightLog).Parent(page);
		m_console = console.Spawn();
	}

	void RefreshConsole() {
		auto area = m_ctx.registry.GetComponent<ui::UiInputArea>(m_console);
		if (area.IsNone())
			return;
		// Les dernières lignes seulement : recopier 500 entrées à chaque ligne
		// nouvelle coûterait plus que tout le reste de l'interface.
		const std::vector<LogEntry> &log = Rt().Log();
		const size_t start = log.size() > CONSOLE_LINES ? log.size() - CONSOLE_LINES : 0;
		String text;
		for (size_t i = start; i < log.size(); ++i) {
			text.Concat(String::Format("[%-6s] %s", log[i].LevelName(), log[i].text.CStr()));
			text.Concat("\n");
		}
		area.Unwrap()->text = std::move(text);
	}

	void AddProfilerRow(const char *label, const String &value) {
		kit::PropertyRows rows(m_ctx, 150.f);
		(void)rows.ReadOnly(m_profilerPage, label, value);
	}

	void RefreshProfiler() {
		if (!m_profilerPage.Valid())
			return;
		kit::ClearChildren(m_ctx, m_profilerPage);
		const SceneDesc *scene = Rt().ActiveScene();
		AddProfilerRow("Cadence", String::Format("%.1f img/s", m_fps));
		AddProfilerRow("Simulation", String::Format("%.2f ms", Rt().SimulationMs()));
		AddProfilerRow("Objets (scène)", String::From(int(scene ? scene->ObjectCount() : 0)));
		AddProfilerRow("Nœuds 3D", String::From(int(Rt().RuntimeObjectCount())));
		AddProfilerRow("Lumières actives", String::Format("%d ponctuelles, %d projecteurs", int(Rt().PointLights().size()),
														  int(Rt().SpotLights().size())));
		AddProfilerRow("Corps physiques", String::From(int(Rt().RigidBodyCount())));
		AddProfilerRow("Entités ECS", String::From(int(m_ctx.registry.AliveCount())));
		AddProfilerRow("Scripts exécutés", String::From(int(Rt().ScriptRunCount())));
		AddProfilerRow("Rappels de script", String::From(int(Rt().ScriptCallCount())));
		AddProfilerRow("Erreurs de script", String::From(int(Rt().ScriptErrorCount())));
		AddProfilerRow("Déclenchements", String::From(int(Rt().TriggerCount())));
		AddProfilerRow("Fils du vivier", String::Format("%d (pic %d en parallèle)", int(Rt().Jobs().WorkerCount()),
														 Rt().Jobs().PeakConcurrentWorkers()));
		if (m_fpsHistory.size() >= 2) {
			ui::WidgetBuilder plot = m_ctx.factory.PlotLines(m_fpsHistory, String("images / seconde"));
			plot.GrowW().H(ui::Dimension::Px(120.f)).Parent(m_profilerPage);
			(void)plot.Spawn();
		}
	}

	void RefreshSceneList() {
		if (!m_sceneListPage.Valid())
			return;
		kit::ClearChildren(m_ctx, m_sceneListPage);
		const Project &project = Rt().GetProject();
		int index = 0;
		for (const SceneDesc &scene : project.scenes) {
			const bool active = scene.name == ActiveSceneName();
			ui::WidgetBuilder row = m_ctx.factory.Selectable(String(), index++);
			row.GrowW().HAuto().Gap(6.f).Pad(math::Sides{6.f, 3.f}).Tooltip(scene.description).Parent(m_sceneListPage);
			const String name = scene.name;
			row.OnClick([this, name] { (void)Rt().SwitchScene(name); });
			ecs::Entity rowEntity = row.Spawn();
			if (auto selectable = m_ctx.registry.GetComponent<ui::UiSelectable>(rowEntity); selectable.IsSome())
				selectable.Unwrap()->selected = active;
			(void)kit::Glyph(m_ctx, rowEntity, ui::MaterialIcons::LAYERS, kit::Rgb(126, 172, 232), 16.f);
			ui::WidgetBuilder label = m_ctx.factory.Label(scene.name);
			label.WAuto().HAuto().FontSize(13.f).PointerThrough().Parent(rowEntity);
			(void)label.Spawn();
			ui::WidgetBuilder detail = m_ctx.factory.Label(String::Format("(%d objets)", int(scene.ObjectCount())));
			detail.GrowW().HAuto().TextEllipsis().FontSize(12.f).TextColor(m_ctx.Theme().muted).PointerThrough().Parent(rowEntity);
			(void)detail.Spawn();
		}
	}

	// ── Barre d'état ─────────────────────────────────────────────────────────

	void BuildStatusBar(ecs::Entity parent) {
		ui::WidgetBuilder bar = m_ctx.factory.Row();
		bar.Gap(16.f).Pad(math::Sides{10.f, 3.f}).GrowW().HAuto().Align(ui::CrossAlign::Center);
		bar.Bg(kit::PaletteOf(m_ctx).header).Parent(parent);
		ecs::Entity barEntity = bar.Spawn();
		m_statusFps = StatusLabel(barEntity, "— img/s", 70.f);
		m_statusObjects = StatusLabel(barEntity, "— objets", 90.f);
		m_statusBodies = StatusLabel(barEntity, "— corps", 80.f);
		m_statusScene = StatusLabel(barEntity, "—", 140.f);
		m_statusMode = StatusLabel(barEntity, "■ Édition", 80.f);
		m_statusGizmo = StatusLabel(barEntity, "déplacer", 170.f);
		ui::WidgetBuilder message = m_ctx.factory.Label(m_statusMessage);
		message.GrowW().HAuto().FontSize(12.f).TextColor(m_ctx.Theme().muted).TextEllipsis().TextAlign(ui::TextAlign::Right);
		message.Parent(barEntity);
		m_statusMessageLabel = message.Spawn();
	}

	ecs::Entity StatusLabel(ecs::Entity parent, const char *text, float width) {
		ui::WidgetBuilder label = m_ctx.factory.Label(String(text));
		label.Size(width, 0.f).HAuto().FontSize(12.f).TextEllipsis().Parent(parent);
		return label.Spawn();
	}

	void RefreshStatusBar() {
		const SceneDesc *scene = Rt().ActiveScene();
		kit::SetLabelText(m_ctx, m_statusFps, String::Format("%.0f img/s", m_fps));
		kit::SetLabelText(m_ctx, m_statusObjects, String::Format("%d objets", int(scene ? scene->ObjectCount() : 0)));
		kit::SetLabelText(m_ctx, m_statusBodies, String::Format("%d corps", int(Rt().RigidBodyCount())));
		kit::SetLabelText(m_ctx, m_statusScene, scene ? scene->name : String("(aucune scène)"));
		kit::SetLabelText(m_ctx, m_statusMode, String(Rt().IsPlaying() ? "▶ Jeu" : "■ Édition"));
		kit::SetLabelText(m_ctx, m_statusGizmo, String::Format("%s%s", GizmoModeName(Rt().GetGizmoMode()),
															  Rt().SnapEnabled() ? " · magnétisme" : ""));

		// Outil actif et magnétisme surlignés dans la barre d'outils de la vue —
		// seulement quand ils CHANGENT (re-styler à chaque image relancerait la
		// cascade de styles pour rien).
		const int mode = int(Rt().GetGizmoMode());
		const bool snap = Rt().SnapEnabled();
		if (mode != m_shownGizmoMode || snap != m_shownSnap) {
			m_shownGizmoMode = mode;
			m_shownSnap = snap;
			for (int i = 0; i < 3; ++i)
				kit::SetBackground(m_ctx, m_gizmoButtons[i], i == mode ? kit::PaletteOf(m_ctx).selection : sdl3::FColor{0.f, 0.f, 0.f, 0.f});
			kit::SetBackground(m_ctx, m_snapButton, snap ? kit::PaletteOf(m_ctx).selection : sdl3::FColor{0.f, 0.f, 0.f, 0.f});
		}
		if (Rt().IsPlaying() != m_shownPlaying) {
			m_shownPlaying = Rt().IsPlaying();
			kit::SetHidden(m_ctx, m_playBadge, !m_shownPlaying);
			if (auto glyph = m_ctx.registry.GetComponent<ui::UiIcon>(m_playIcon); glyph.IsSome())
				glyph.Unwrap()->glyph = ui::Glyphs::ToUtf8(m_shownPlaying ? ui::MaterialIcons::STOP : ui::MaterialIcons::PLAY_ARROW);
		}
		// Le sélecteur de scène suit les changements venus d'ailleurs.
		if (auto combo = m_ctx.registry.GetComponent<ui::UiComboBox>(m_sceneCombo); combo.IsSome() && scene) {
			const std::vector<String> &items = combo.Unwrap()->items;
			for (size_t i = 0; i < items.size(); ++i)
				if (items[i] == scene->name)
					combo.Unwrap()->selected = int(i);
		}
		m_fpsHistory.push_back(float(m_fps));
		if (m_fpsHistory.size() > FPS_HISTORY_SIZE)
			m_fpsHistory.erase(m_fpsHistory.begin());
	}

	// ── Actions ──────────────────────────────────────────────────────────────

	void SetGizmoMode(Runtime::GizmoMode mode) {
		Rt().SetGizmoMode(mode);
		SetStatus(String::Format("Manipulateur : %s", GizmoModeName(mode)));
	}

	void ToggleSnap() {
		Rt().SetSnapEnabled(!Rt().SnapEnabled());
		SetStatus(Rt().SnapEnabled() ? String::Format("Magnétisme : %.2f u / %.0f° / %.2f", Rt().TranslateSnap(),
													  Rt().RotateSnapDegrees(), Rt().ScaleSnap())
									 : String("Magnétisme désactivé"));
	}

	[[nodiscard]] static const char *GizmoModeName(Runtime::GizmoMode mode) noexcept {
		switch (mode) {
			case Runtime::GizmoMode::TRANSLATE:
				return "déplacer";
			case Runtime::GizmoMode::ROTATE:
				return "tourner";
			case Runtime::GizmoMode::SCALE:
				return "redimensionner";
		}
		return "?";
	}

	void ImportModel(const String &path) {
		auto imported = Rt().ImportGltf(path);
		if (imported.IsError()) {
			Rt().LogError(String::Format("Import : %s", imported.Error().CStr()));
			return;
		}
		(void)Rt().Select(imported.Value());
		SetStatus(String::Format("Modèle importé : %s", imported.Value().CStr()));
	}

	void OnNewProject() {
		Rt().OpenProject(MakeDemoProject());
		Rebuild();
	}

	void OnOpenProject() {
		auto loaded = Rt().LoadProjectFile(projectPath);
		if (loaded.IsError()) {
			Rt().LogError(String::Format("Ouverture impossible : %s", loaded.Error().CStr()));
			return;
		}
		Rebuild();
	}

	/// Crée le dossier qui contiendra `path` (les sous-dossiers de
	/// sauvegarde n'existent pas forcément encore).
	static void EnsureParentDirectory(const String &path) {
		size_t slash = String::NPOS;
		for (size_t i = 0; i < path.size(); ++i)
			if (path[i] == '/')
				slash = i;
		if (slash != String::NPOS && slash > 0)
			(void)sdl3::filesystem::CreateDirectory(path.Substr(0, slash));
	}

	void OnSaveProject() {
		EnsureParentDirectory(projectPath);
		auto saved = Rt().SaveProjectFile(projectPath);
		if (saved.IsError())
			Rt().LogError(saved.Error());
		else
			Rt().LogSuccess(String::Format("Projet enregistré : %s", projectPath.CStr()));
	}

	void OnSaveSelectionAsScene() {
		const scene::Node *node = Rt().SelectedObject();
		if (!node) {
			SetStatus(String("Sélectionnez le nœud à enregistrer comme scène"));
			return;
		}
		const String path = String::Format("%s/scenes/%s.tscene", m_savesDir.CStr(), node->name.CStr());
		EnsureParentDirectory(path);
		auto saved = Rt().SavePackedScene(node->id, path);
		if (saved.IsError())
			Rt().LogError(saved.Error());
		else
			Rt().LogSuccess(String::Format("Scène enregistrée : %s", path.CStr()));
	}

	void OnExportReport() {
		if (onExportReport)
			onExportReport();
		else
			Rt().LogWarning(String("Export du rapport indisponible dans ce mode"));
	}

	void OnUndo() {
		if (!Rt().Undo())
			SetStatus(String("Rien à annuler"));
	}

	void OnRedo() {
		if (!Rt().Redo())
			SetStatus(String("Rien à rétablir"));
	}

	/// Duplique la sélection AVEC son sous-arbre et décale la copie.
	void OnDuplicate() {
		SceneDesc *scene = Rt().ActiveScene();
		scene::Node *object = Rt().SelectedObject();
		if (!scene || !object)
			return;
		const float offset = VisualDesc::Has(*object) ? VisualDesc::Read(*object).dimensions.x + 0.5f : 1.f;
		Option<scene::NodeId> copy = Rt().DuplicateNode(object->id);
		if (copy.IsNone())
			return;
		math::FVector3 position = scene->tree.Get(copy.Unwrap())->transform.position;
		position.x += offset;
		(void)Rt().SetPosition(copy.Unwrap(), position);
	}

	void OnDeleteSelection() {
		const scene::NodeId id = Rt().SelectedId();
		const scene::Node *node = Rt().FindObject(id);
		if (!node)
			return;
		const String name = node->name;
		if (Rt().RemoveNode(id))
			SetStatus(String::Format("Supprimé : %s", name.CStr()));
	}

	void OnFocusSelection() {
		if (Rt().SelectedId().Valid())
			(void)Rt().FocusOn(Rt().SelectedId());
	}

	void OnClearConsole() {
		Rt().ClearLog();
		RefreshConsole();
	}

	/// Compile tous les scripts du projet et rend compte dans la console.
	void OnCheckAllScripts() {
		int ok = 0, broken = 0;
		const Project &project = Rt().GetProject();
		auto check = [&](const String &label, const String &source) {
			if (Option<data::script::ScriptError> error = Runtime::CheckScript(source); error.IsSome()) {
				++broken;
				Rt().LogError(String::Format("%s : %s", label.CStr(), error.Unwrap().Format().CStr()));
			} else {
				++ok;
			}
		};
		for (const ScriptAsset &script : project.scripts)
			check(script.name + String(".sled"), script.source);
		for (const SceneDesc &scene : project.scenes)
			if (!scene.gameplayScript.IsEmpty())
				check(scene.name + String(".main.sled"), scene.gameplayScript);
		if (broken == 0)
			Rt().LogSuccess(String::Format("%d script(s) vérifié(s), aucune erreur", ok));
		else
			Rt().LogWarning(String::Format("%d script(s) en erreur sur %d", broken, ok + broken));
		(void)FocusPanel("console", 0);
	}

	void OnShowShortcuts() {
		Rt().LogInfo(String("Vue : clic droit orienter + ZQSD/WASD voler · clic milieu panoramique · molette "
							"avancer · Alt+clic orbiter · F cadrer"));
		Rt().LogInfo(String("Édition : W déplacer · E tourner · R redimensionner · X magnétisme · F2 renommer · "
							"Suppr supprimer · Ctrl+D dupliquer · Ctrl+Z / Ctrl+Y"));
		Rt().LogInfo(String("Arbre : clic droit pour le menu contextuel · glisser une ligne pour reparenter"));
		Rt().LogInfo(String("Code : Ctrl+S enregistrer · Ctrl+Entrée exécuter la console · F5 jouer · F6 plein écran"));
		(void)FocusPanel("console", 0);
	}

	void OnAbout() {
		Rt().LogInfo(String("game_editor_demo — éditeur de jeu 3D bâti sur le wrapper SDL3/C++23 "
							"(ui::, scene::, ecs::, render3d::, physics::, data::script)"));
		(void)FocusPanel("console", 0);
	}

	// ── Clavier ──────────────────────────────────────────────────────────────

	[[nodiscard]] bool TextFieldHasFocus() const {
		bool focused = false;
		m_ctx.registry.Query<ui::UiInputArea>([&focused](ecs::Entity, const ui::UiInputArea &area) {
			if (area.focused && area.ioMode != ui::IOMode::READ_AND_COPY_ONLY)
				focused = true;
		});
		if (focused)
			return true;
		m_ctx.registry.Query<ui::UiInput>([&focused](ecs::Entity, const ui::UiInput &input) {
			if (input.focused)
				focused = true;
		});
		return focused;
	}

	/// Vol de la caméra : ZQSD/WASD TANT QUE le clic droit est tenu (sans
	/// quoi W/E/R ne pourraient pas aussi changer d'outil) ; flèches et Page
	/// haut/bas sans bouton.
	void PollCameraKeys(float dt) {
		if (TextFieldHasFocus() || m_runMode)
			return;
		const float speed = sdl3::keyboard::IsPressed(SDLK_LSHIFT) ? 22.f : 8.f;
		math::FVector3 delta{};
		if (m_rightMouseDown) {
			if (sdl3::keyboard::IsPressed(SDLK_W))
				delta.z += 1.f;
			if (sdl3::keyboard::IsPressed(SDLK_S))
				delta.z -= 1.f;
			if (sdl3::keyboard::IsPressed(SDLK_D))
				delta.x += 1.f;
			if (sdl3::keyboard::IsPressed(SDLK_A))
				delta.x -= 1.f;
			if (sdl3::keyboard::IsPressed(SDLK_E))
				delta.y += 1.f;
			if (sdl3::keyboard::IsPressed(SDLK_Q))
				delta.y -= 1.f;
		}
		if (sdl3::keyboard::IsPressed(SDLK_UP))
			delta.z += 1.f;
		if (sdl3::keyboard::IsPressed(SDLK_DOWN))
			delta.z -= 1.f;
		if (sdl3::keyboard::IsPressed(SDLK_RIGHT))
			delta.x += 1.f;
		if (sdl3::keyboard::IsPressed(SDLK_LEFT))
			delta.x -= 1.f;
		if (sdl3::keyboard::IsPressed(SDLK_PAGEUP))
			delta.y += 1.f;
		if (sdl3::keyboard::IsPressed(SDLK_PAGEDOWN))
			delta.y -= 1.f;
		if (delta.x != 0.f || delta.y != 0.f || delta.z != 0.f)
			Rt().MoveCamera(delta * (speed * dt));
	}

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
	String m_sceneName;
	double m_fps = 0.0;
	long m_tickCount = 0;
	bool m_rightMouseDown = false;
	bool m_middleMouseDown = false;
	bool m_orbiting = false;
	math::FVector3 m_orbitPivot{};
	bool m_consoleDirty = true;
	bool m_scenesDirty = true;
	bool m_maximized = false;
	bool m_runMode = false;
	int m_shownGizmoMode = -1;
	bool m_shownSnap = false;
	bool m_shownPlaying = true;
	std::vector<float> m_fpsHistory;
	std::vector<ecs::Entity> m_menuPopups;

	ecs::Entity m_root{}, m_body{}, m_bottomRow{}, m_rightColumn{}, m_leftHandle{};
	ecs::Entity m_viewport{}, m_playIcon{}, m_playBadge{}, m_sceneCombo{}, m_snapButton{};
	ecs::Entity m_gizmoButtons[3]{};
	ecs::Entity m_sceneListPage{}, m_profilerPage{}, m_console{};
	ecs::Entity m_treeMenu{}, m_documentsMenu{}, m_inspectorMenu{}, m_assetsMenu{};
	ecs::Entity m_runRoot{}, m_runViewport{}, m_runPanel{};
	ecs::Entity m_statusFps{}, m_statusObjects{}, m_statusBodies{}, m_statusScene{}, m_statusMode{}, m_statusGizmo{};
	ecs::Entity m_statusMessageLabel{};
};

} // namespace game_editor
