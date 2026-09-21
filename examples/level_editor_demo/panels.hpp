#pragma once
/**
 * level_editor::EditorUi — l'interface, montée entièrement avec `ui::`.
 *
 * Disposition calquée sur les éditeurs 3D courants (Blender/Godot/Unreal) :
 *
 *   ┌──────────────────────────────────────────────────────────────┐
 *   │ Fichier  Édition  Scène  Objet  Script  Affichage  Aide      │ barre de menus
 *   ├──────────────────────────────────────────────────────────────┤
 *   │ [nouv][ouvr][enr] │ [box][sph][cyl][tore] │ [▶][■] │ scène ▾ │ barre d'outils
 *   ├────────────┬─────────────────────────────────┬───────────────┤
 *   │ Outliner   │                                 │ Transform     │
 *   │ Scènes     │          viewport 3D            │ Matériau      │
 *   │ Ressources │                                 │ Physique      │
 *   │            ├─────────────────────────────────┤ Monde         │
 *   │            │ Console · Script · Profil       │               │
 *   ├────────────┴─────────────────────────────────┴───────────────┤
 *   │ img/s · objets · corps · scène · mode · message              │ barre d'état
 *   └──────────────────────────────────────────────────────────────┘
 *
 * ── Reconstruction plutôt que synchronisation fine ──────────────────────
 * L'outliner, l'inspecteur et la console sont RECONSTRUITS quand leur
 * contenu change (sélection, structure de scène, nouvelle ligne de journal),
 * jamais mis à jour widget par widget. C'est ce que fait déjà le reste du
 * dépôt pour les listes dynamiques, et ça évite toute une classe de bugs de
 * désynchronisation — au prix d'un coût qui ne se paie que sur l'évènement,
 * pas à chaque image. Les valeurs qui changent en continu (cadence,
 * compteurs, position d'un objet en cours de simulation) sont en revanche
 * écrites DIRECTEMENT dans le composant du widget concerné, sans rien
 * reconstruire.
 *
 * ── Changement de thème ─────────────────────────────────────────────────
 * `ui::Ui::SetTheme` (ajouté pour cette démo) repeint les widgets existants,
 * mais certains styles « verre » du thème Aero sont posés À LA CONSTRUCTION
 * du widget (cf. `UiFactory::Panel`) : passer d'Aero à un thème plat impose
 * donc de reconstruire l'arbre. `SetTheme()` ci-dessous le fait, ce qui rend
 * la bascule correcte dans les six sens possibles.
 */
#include <functional>
#include <vector>

#include "core/core.hpp"
#include "ui/ui.hpp"

#include "content.hpp"
#include "runtime.hpp"

namespace level_editor {

/// Identifiants des onglets, pour que `editor.open_panel("inspector", 1)`
/// désigne quelque chose de stable côté script.
enum class InspectorTab : int { TRANSFORM = 0, MATERIAL = 1, PHYSICS = 2, WORLD = 3 };
enum class LeftTab : int { OUTLINER = 0, SCENES = 1, ASSETS = 2 };
enum class BottomTab : int { CONSOLE = 0, SCRIPT = 1, PROFILER = 2 };

class EditorUi {
public:
	EditorUi(Runtime &runtime, ui::Ui &gui)
		: m_runtime(runtime), m_gui(gui), m_registry(gui.World()), m_factory(gui.Factory()) {
		m_runtime.onSceneStructureChanged = [this] { m_outlinerDirty = m_worldDirty = true; };
		m_runtime.onSelectionChanged = [this] { m_inspectorDirty = true; };
		m_runtime.onObjectChanged = [this] { m_inspectorDirty = true; };
		m_runtime.onLog = [this](const LogEntry &) { m_consoleDirty = true; };
	}

	EditorUi(const EditorUi &) = delete;
	EditorUi &operator=(const EditorUi &) = delete;

	// ── Construction ─────────────────────────────────────────────────────────

	void Build() {
		ui::WidgetBuilder root = m_factory.Column();
		root.Gap(0.f).Pad(0.f).Fixed().Anchor(ui::Anchor::TopLeft);
		root.W(ui::Dimension::Rpct(100.f)).H(ui::Dimension::Rpct(100.f));
		m_root = root.Spawn();

		BuildMenuBar(m_root);
		BuildToolbar(m_root);

		ui::WidgetBuilder body = m_factory.Row();
		body.Gap(6.f).Pad(6.f).GrowW().GrowH();
		body.Parent(m_root);
		ecs::Entity bodyEntity = body.Spawn();

		BuildLeftDock(bodyEntity);
		BuildCenter(bodyEntity);
		BuildRightDock(bodyEntity);
		BuildStatusBar(m_root);

		RefreshOutliner();
		RefreshSceneList();
		RefreshAssets();
		RefreshInspector();
		RefreshConsole();
		RefreshProfiler();
	}

	/// Détruit puis reconstruit toute l'interface — utilisé au changement de
	/// thème (cf. en-tête). L'état applicatif vit dans `Runtime`, donc rien
	/// n'est perdu.
	void Rebuild() {
		Teardown();
		Build();
	}

	/// Détruit l'arbre d'interface. Les popups de menu sont détruits
	/// SÉPARÉMENT : `UiFactory::Menu` les spawne comme RACINES INDÉPENDANTES
	/// (leur ancrage doit se résoudre en coordonnées fenêtre, cf. factory.hpp),
	/// ils ne sont donc pas dans le sous-arbre de `m_root` et survivraient à
	/// chaque reconstruction — un menu fantôme de plus par changement de thème.
	void Teardown() {
		for (ecs::Entity popup : m_menuPopups)
			if (popup.Valid())
				ui::DespawnTree(m_registry, popup);
		m_menuPopups.clear();
		if (m_root.Valid()) {
			ui::DespawnTree(m_registry, m_root);
			m_root = ecs::Entity{};
		}
	}

	// ── Thème ────────────────────────────────────────────────────────────────

	[[nodiscard]] const String &ThemeName() const noexcept { return m_themeName; }

	/// Change de thème. Si l'interface n'a pas encore été construite (choix
	/// du thème au démarrage, depuis la ligne de commande), seule la palette
	/// est posée : reconstruire ici produirait un PREMIER arbre que le
	/// `Build()` suivant doublerait au lieu de remplacer — deux interfaces
	/// superposées, dont une figée (bug réellement observé : la barre d'état
	/// affichait ses valeurs initiales par-dessus les vraies).
	bool SetTheme(const String &name) {
		Option<ui::UiTheme> theme = ThemeByName(name);
		if (theme.IsNone())
			return false;
		m_themeName = name.ToLower();
		m_gui.SetTheme(theme.Unwrap());
		if (m_root.Valid())
			Rebuild();
		return true;
	}

	[[nodiscard]] static Option<ui::UiTheme> ThemeByName(const String &name) {
		String key = name.ToLower();
		if (key == "dark")
			return Some(ui::UiTheme::Dark());
		if (key == "light")
			return Some(ui::UiTheme::Light());
		if (key == "aero")
			return Some(ui::UiTheme::Aero());
		return NONE;
	}

	// ── Boucle ───────────────────────────────────────────────────────────────

	/// Rectangle écran du viewport 3D (vide tant que la mise en page n'a pas
	/// eu lieu).
	[[nodiscard]] sdl3::FRect ViewportRect() const {
		auto computed = m_registry.GetComponent<ui::UiComputed>(m_viewport);
		return computed.IsSome() ? computed.Unwrap()->screen : sdl3::FRect{};
	}

	/// Vrai si le point donné appartient au viewport 3D ET qu'aucun widget ne
	/// se dessine PAR-DESSUS lui à cet endroit : un menu déroulé ou un popup
	/// ouvert au-dessus de la vue garde le pointeur pour lui, sans quoi le
	/// même clic sélectionnerait aussi un objet de la scène derrière le menu
	/// (cf. ui::HitTestIndex — même ordre de dessin que le rendu).
	[[nodiscard]] bool PointerOverViewport(float x, float y) const {
		const sdl3::FRect rect = ViewportRect();
		if (rect.w <= 0.f || rect.h <= 0.f)
			return false;
		if (x < rect.x || y < rect.y || x >= rect.x + rect.w || y >= rect.y + rect.h)
			return false;
		// Via l'InputSystem, qui tient déjà l'index à jour : ui::HitTestTopMost
		// le reconstruirait à chaque appel, et cette fonction est appelée
		// plusieurs fois par évènement souris.
		ecs::Entity front = m_gui.Input().FrontMostAt(m_registry, m_gui.Layout(), sdl3::FPoint{x, y});
		return !front.Valid() || ui::IsDescendantOrSelf(m_registry, front, m_viewport);
	}

	/// Vrai si le curseur est au-dessus du viewport 3D : la molette y déplace
	/// la caméra, ailleurs elle appartient au panneau survolé.
	[[nodiscard]] bool PointerInViewport() const {
		float x = 0.f, y = 0.f;
		SDL_GetMouseState(&x, &y);
		return PointerOverViewport(x, y);
	}

	/// Rayon monde sous le curseur, ou NONE si le curseur n'est pas dans le
	/// viewport 3D (cliquer dans un panneau — ou dans un menu ouvert au-dessus
	/// de la vue — ne doit rien sélectionner).
	[[nodiscard]] Option<math::FRay> RayAt(float screenX, float screenY) const {
		const sdl3::FRect rect = ViewportRect();
		if (!PointerOverViewport(screenX, screenY))
			return NONE;
		return Some(m_runtime.ViewportRay(screenX - rect.x, screenY - rect.y, rect.w, rect.h));
	}

	/// Navigation dans la vue — toutes les directions, avec les gestes des
	/// éditeurs 3D du marché :
	///
	///  - clic DROIT maintenu : orienter la vue, + ZQSD/WASD pour voler,
	///    E/Q pour monter et descendre, Maj pour accélérer ;
	///  - clic MILIEU maintenu : panoramique (la vue glisse dans son propre
	///    plan, comme si on tirait le décor) ;
	///  - MOLETTE : avancer / reculer le long de la visée ;
	///  - ALT + clic gauche : tourner AUTOUR de la sélection (ou de ce qu'on
	///    regarde) sans la perdre de vue ;
	///  - FLÈCHES et Page haut/bas : mêmes déplacements sans tenir de bouton,
	///    pour qui préfère le clavier seul.
	///
	/// Les gestes de panoramique et de molette sont proportionnels à la
	/// distance du pivot : le même mouvement de souris parcourt un petit
	/// objet finement et un circuit entier rapidement.
	///
	/// Clic GAUCHE seul dans le viewport : attraper un axe du manipulateur,
	/// sinon sélectionner l'objet sous le curseur (rien dessous =
	/// désélection). Les raccourcis clavier ne sont traités que si aucun
	/// champ de saisie n'a le focus, pour ne pas voler les frappes de
	/// l'éditeur de script.
	void HandleEvent(const sdl3::Event &event) {
		if (event.IsMouseDown(SDL_BUTTON_RIGHT))
			m_rightMouseDown = true;
		if (event.IsMouseUp(SDL_BUTTON_RIGHT))
			m_rightMouseDown = false;
		if (event.IsMouseDown(SDL_BUTTON_MIDDLE))
			m_middleMouseDown = true;
		if (event.IsMouseUp(SDL_BUTTON_MIDDLE))
			m_middleMouseDown = false;
		if (event.IsMouseUp(SDL_BUTTON_LEFT))
			m_orbiting = false;

		if (event.IsMouseMotion() && m_rightMouseDown) {
			constexpr float SENSITIVITY = 0.0035f;
			m_runtime.ApplyLookDelta(float(event.MouseMotion().xrel) * SENSITIVITY,
									 -float(event.MouseMotion().yrel) * SENSITIVITY);
		}
		if (event.IsMouseMotion() && m_middleMouseDown) {
			// Panoramique : on tire le décor, donc la caméra part à l'opposé
			// du mouvement de la souris.
			const float scale = m_runtime.PivotDistance() * 0.0016f;
			m_runtime.PanCamera(-float(event.MouseMotion().xrel) * scale,
								float(event.MouseMotion().yrel) * scale);
		}
		if (event.IsMouseMotion() && m_orbiting) {
			// Mêmes sens que le regard libre (clic droit) : la souris vers la
			// droite tourne la vue vers la droite, vers le bas la fait
			// plonger — ici en tournant AUTOUR du pivot au lieu de pivoter
			// sur place.
			constexpr float SENSITIVITY = 0.006f;
			m_runtime.OrbitCamera(float(event.MouseMotion().xrel) * SENSITIVITY,
								  -float(event.MouseMotion().yrel) * SENSITIVITY, m_orbitPivot);
		}
		if (event.IsMouseWheel() && PointerInViewport()) {
			// Molette : avancer d'une fraction de la distance au pivot —
			// on approche vite de loin, finement de près, et on ne traverse
			// jamais l'objet visé d'un cran.
			m_runtime.DollyCamera(float(event.MouseWheel().y) * m_runtime.PivotDistance() * 0.18f);
		}

		if (event.IsMouseDown(SDL_BUTTON_LEFT) && (SDL_GetModState() & SDL_KMOD_ALT) != 0) {
			// ALT + clic gauche : orbite. Le pivot est figé au début du
			// geste, sinon il suivrait la sélection qui bouge et la caméra
			// dériverait.
			if (RayAt(event.MouseButton().x, event.MouseButton().y).IsSome()) {
				m_orbitPivot = m_runtime.CameraPivot();
				m_orbiting = true;
			}
		} else if (event.IsMouseDown(SDL_BUTTON_LEFT)) {
			if (Option<math::FRay> ray = RayAt(event.MouseButton().x, event.MouseButton().y); ray.IsSome()) {
				// L'axe l'emporte sur l'objet : le manipulateur est dessiné
				// PAR-DESSUS la scène, il doit donc se cliquer par-dessus.
				const Runtime::GizmoAxis axis = m_runtime.PickGizmoAxis(ray.Unwrap());
				if (axis != Runtime::GizmoAxis::NONE)
					(void)m_runtime.BeginGizmoDrag(axis, ray.Unwrap());
				else
					(void)m_runtime.SelectAt(ray.Unwrap());
			}
		}
		if (event.IsMouseUp(SDL_BUTTON_LEFT))
			m_runtime.EndGizmoDrag();
		if (event.IsMouseMotion() && !m_rightMouseDown && !m_middleMouseDown && !m_orbiting) {
			if (Option<math::FRay> ray = RayAt(event.MouseMotion().x, event.MouseMotion().y); ray.IsSome()) {
				if (m_runtime.IsGizmoDragging())
					(void)m_runtime.UpdateGizmoDrag(ray.Unwrap());
				else
					m_runtime.SetHoveredAxis(m_runtime.PickGizmoAxis(ray.Unwrap()));
			} else if (!m_runtime.IsGizmoDragging()) {
				m_runtime.SetHoveredAxis(Runtime::GizmoAxis::NONE);
			}
		}

		if (TextFieldHasFocus())
			return;

		const bool control = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
		if (control && event.IsKeyDown(SDLK_Z)) {
			// Ctrl+Z annule, Ctrl+Maj+Z et Ctrl+Y rétablissent (les deux
			// conventions existent, aucune ne surprend).
			if ((SDL_GetModState() & SDL_KMOD_SHIFT) != 0)
				(void)m_runtime.Redo();
			else
				(void)m_runtime.Undo();
		}
		if (control && event.IsKeyDown(SDLK_Y))
			(void)m_runtime.Redo();
		if (control && event.IsKeyDown(SDLK_D))
			OnDuplicate();
		if (control && event.IsKeyDown(SDLK_S))
			OnSaveProject();

		if (!control && !m_rightMouseDown) {
			// W/E/R : déplacer, tourner, redimensionner (convention Unity/
			// Unreal) — inactifs pendant le vol de la caméra, où les mêmes
			// touches la déplacent.
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
			m_runtime.TogglePlay();
		if (event.IsKeyDown(SDLK_DELETE)) {
			if (Option<String> selected = m_runtime.SelectedName(); selected.IsSome())
				(void)m_runtime.RemoveObject(selected.Unwrap());
		}
		if (event.IsKeyDown(SDLK_F)) {
			if (Option<String> selected = m_runtime.SelectedName(); selected.IsSome())
				(void)m_runtime.FocusOn(selected.Unwrap());
		}
	}

	/// Change le mode du manipulateur et le signale dans la barre d'état.
	void SetGizmoMode(Runtime::GizmoMode mode) {
		m_runtime.SetGizmoMode(mode);
		m_runtime.LogInfo(String::Format("Manipulateur : %s", GizmoModeName(mode)));
	}

	void ToggleSnap() {
		m_runtime.SetSnapEnabled(!m_runtime.SnapEnabled());
		m_runtime.LogInfo(m_runtime.SnapEnabled()
							  ? String::Format("Magnétisme activé (%.2f u / %.0f° / %.2f)", m_runtime.TranslateSnap(),
											   m_runtime.RotateSnapDegrees(), m_runtime.ScaleSnap())
							  : String("Magnétisme désactivé"));
	}

	[[nodiscard]] static const char *GizmoModeIcon(Runtime::GizmoMode mode) noexcept {
		switch (mode) {
			case Runtime::GizmoMode::TRANSLATE:
				return "⇔";
			case Runtime::GizmoMode::ROTATE:
				return "↻";
			case Runtime::GizmoMode::SCALE:
				return "⤢";
		}
		return "";
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

	/// Déplacement de la caméra au clavier (état continu, pas évènementiel —
	/// même idiome que examples/renderer.cpp) + rafraîchissements différés.
	void Tick(float dt, double fps) {
		m_fps = fps;
		PollCameraKeys(dt);

		if (auto viewport = m_registry.GetComponent<ui::UiViewport3D>(m_viewport); viewport.IsSome()) {
			viewport.Unwrap()->camera = m_runtime.ActiveCamera();
			const ui::UiViewport3D &vp = *viewport.Unwrap();
			if (vp.textureHeight > 0)
				m_runtime.SetViewportAspect(float(vp.textureWidth) / float(vp.textureHeight));
		}

		// Un panneau MASQUÉ n'est pas reconstruit : son drapeau « à rafraîchir »
		// reste levé et le travail se fait au moment où l'onglet redevient
		// visible. Sans ça, l'éditeur reconstruisait l'outliner, l'inspecteur,
		// la console et le profileur de panneaux invisibles — et chaque
		// reconstruction (despawn/spawn de dizaines d'entités, donc migrations
		// d'archétypes, puis cascade de styles et mise en page complètes)
		// produisait un à-coup de 10 à 20 ms bien visible dans le p99.
		if (m_outlinerDirty && ActiveTab(m_leftTabs) == int(LeftTab::OUTLINER)) {
			RefreshOutliner();
			m_outlinerDirty = false;
		}
		if (m_inspectorDirty) {
			RefreshInspector();
			m_inspectorDirty = false;
		}
		if (m_consoleDirty && ActiveTab(m_bottomTabs) == int(BottomTab::CONSOLE)) {
			RefreshConsole();
			m_consoleDirty = false;
		}
		if (m_worldDirty && ActiveTab(m_leftTabs) == int(LeftTab::SCENES)) {
			RefreshSceneList();
			m_worldDirty = false;
		}
		// Le profileur change en continu : mis à jour périodiquement plutôt
		// qu'à chaque image, et seulement quand son onglet est au premier plan.
		if (++m_tickCount % PROFILER_REFRESH_FRAMES == 0 &&
			ActiveTab(m_bottomTabs) == int(BottomTab::PROFILER))
			RefreshProfiler();
		RefreshStatusBar();
	}

	// ── Pilotage par script ──────────────────────────────────────────────────

	/// `editor.open_panel("inspector", 2)` — met un panneau au premier plan.
	bool FocusPanel(const String &panel, int tab) {
		String key = panel.ToLower();
		if (key == "outliner")
			return SetTabActive(m_leftTabs, tab > 0 ? tab : int(LeftTab::OUTLINER));
		if (key == "scenes")
			return SetTabActive(m_leftTabs, int(LeftTab::SCENES));
		if (key == "assets")
			return SetTabActive(m_leftTabs, int(LeftTab::ASSETS));
		if (key == "inspector")
			return SetTabActive(m_rightTabs, tab);
		if (key == "console")
			return SetTabActive(m_bottomTabs, int(BottomTab::CONSOLE));
		if (key == "script")
			return SetTabActive(m_bottomTabs, int(BottomTab::SCRIPT));
		if (key == "profiler")
			return SetTabActive(m_bottomTabs, int(BottomTab::PROFILER));
		return false;
	}

	void SetStatus(String message) {
		m_statusMessage = std::move(message);
		SetLabelText(m_statusMessageLabel, m_statusMessage);
	}

private:
	static constexpr long PROFILER_REFRESH_FRAMES = 20;
	static constexpr float LEFT_DOCK_WIDTH = 264.f;
	static constexpr float RIGHT_DOCK_WIDTH = 330.f;
	static constexpr float BOTTOM_DOCK_HEIGHT = 208.f;
	/// Colonne de libellés de l'inspecteur — assez large pour « Frottement »
	/// et « Demi-dim. » sans rognage, assez étroite pour laisser la place aux
	/// trois champs d'un vecteur.
	static constexpr float INSPECTOR_LABEL_WIDTH = 86.f;

	// ── Petites aides ────────────────────────────────────────────────────────

	void SetLabelText(ecs::Entity entity, const String &text) {
		if (auto label = m_registry.GetComponent<ui::UiLabel>(entity); label.IsSome())
			label.Unwrap()->text = text;
	}

	/// Vide un conteneur de ses enfants. Les entités sont COLLECTÉES avant
	/// destruction : `DespawnTree` modifie la liste d'enfants du parent, la
	/// parcourir en même temps la ferait pointer dans le vide.
	void ClearChildren(ecs::Entity parent) {
		std::vector<ecs::Entity> children;
		if (auto list = m_registry.GetComponent<ui::UiChildren>(parent); list.IsSome())
			children = list.Unwrap()->list;
		for (ecs::Entity child : children)
			ui::DespawnTree(m_registry, child);
		if (auto list = m_registry.GetComponent<ui::UiChildren>(parent); list.IsSome())
			list.Unwrap()->list.clear();
		m_gui.Layout().MarkDirty();
	}

	/// Index de l'onglet actif d'un conteneur, -1 si ce n'en est pas un.
	[[nodiscard]] int ActiveTab(ecs::Entity tabview) const {
		auto view = m_registry.GetComponent<ui::UiTabView>(tabview);
		return view.IsSome() ? view.Unwrap()->active : -1;
	}

	/// Active un onglet et masque les autres. Le système d'entrée fait ce
	/// travail au CLIC uniquement (cf. systems.hpp) : le même geste doit donc
	/// exister ici pour l'état initial et pour le pilotage par script.
	bool SetTabActive(ecs::Entity tabview, int index) {
		auto view = m_registry.GetComponent<ui::UiTabView>(tabview);
		if (view.IsNone() || view.Unwrap()->tabs.empty())
			return false;
		int clamped = sdl3::Clamp(index, 0, int(view.Unwrap()->tabs.size()) - 1);
		view.Unwrap()->active = clamped;

		auto children = m_registry.GetComponent<ui::UiChildren>(tabview);
		if (children.IsSome()) {
			std::vector<ecs::Entity> list = children.Unwrap()->list;
			for (size_t i = 0; i < list.size(); ++i) {
				if (int(i) == clamped)
					m_registry.RemoveComponent<ui::UiHidden>(list[i]);
				else if (!m_registry.HasComponent<ui::UiHidden>(list[i]))
					m_registry.AddComponent(list[i], ui::UiHidden{});
			}
		}
		m_gui.Layout().MarkDirty();
		// Rattrapage : le panneau qui vient d'apparaître peut avoir des
		// rafraîchissements en attente (cf. Tick).
		if (tabview == m_bottomTabs && clamped == int(BottomTab::PROFILER))
			RefreshProfiler();
		if (tabview == m_bottomTabs && clamped == int(BottomTab::CONSOLE) && m_consoleDirty) {
			RefreshConsole();
			m_consoleDirty = false;
		}
		if (tabview == m_leftTabs && clamped == int(LeftTab::OUTLINER) && m_outlinerDirty) {
			RefreshOutliner();
			m_outlinerDirty = false;
		}
		if (tabview == m_leftTabs && clamped == int(LeftTab::SCENES) && m_worldDirty) {
			RefreshSceneList();
			m_worldDirty = false;
		}
		return true;
	}

	/// Un champ de saisie a-t-il le focus ? Les raccourcis à une touche
	/// (F, Suppr…) doivent se taire pendant qu'on tape un script.
	[[nodiscard]] bool TextFieldHasFocus() const {
		bool focused = false;
		m_registry.Query<ui::UiInputArea>([&focused](ecs::Entity, const ui::UiInputArea &area) {
			if (area.focused)
				focused = true;
		});
		if (focused)
			return true;
		m_registry.Query<ui::UiInput>([&focused](ecs::Entity, const ui::UiInput &input) {
			if (input.focused)
				focused = true;
		});
		return focused;
	}

	/// Vol de la caméra : W/A/S/D/Q/E TANT QUE LE CLIC DROIT EST MAINTENU,
	/// convention d'Unity et d'Unreal. Sans cette condition, W, E et R ne
	/// pourraient pas servir aussi à changer de mode de manipulateur — et
	/// c'est bien la même main qui fait les deux.
	void PollCameraKeys(float dt) {
		if (TextFieldHasFocus())
			return;
		const float speed = sdl3::keyboard::IsPressed(SDL_SCANCODE_LSHIFT) ? 22.f : 8.f;
		math::FVector3 delta{};

		// ZQSD/WASD + E/Q : vol, tant que le clic droit est tenu (les mêmes
		// touches servent sinon aux modes du manipulateur).
		if (m_rightMouseDown) {
			if (sdl3::keyboard::IsPressed(SDL_SCANCODE_W))
				delta.z += 1.f;
			if (sdl3::keyboard::IsPressed(SDL_SCANCODE_S))
				delta.z -= 1.f;
			if (sdl3::keyboard::IsPressed(SDL_SCANCODE_D))
				delta.x += 1.f;
			if (sdl3::keyboard::IsPressed(SDL_SCANCODE_A))
				delta.x -= 1.f;
			if (sdl3::keyboard::IsPressed(SDL_SCANCODE_E))
				delta.y += 1.f;
			if (sdl3::keyboard::IsPressed(SDL_SCANCODE_Q))
				delta.y -= 1.f;
		}

		// Flèches et Page haut/bas : mêmes déplacements SANS tenir de bouton
		// (aucune de ces touches n'est un raccourci d'édition).
		if (sdl3::keyboard::IsPressed(SDL_SCANCODE_UP))
			delta.z += 1.f;
		if (sdl3::keyboard::IsPressed(SDL_SCANCODE_DOWN))
			delta.z -= 1.f;
		if (sdl3::keyboard::IsPressed(SDL_SCANCODE_RIGHT))
			delta.x += 1.f;
		if (sdl3::keyboard::IsPressed(SDL_SCANCODE_LEFT))
			delta.x -= 1.f;
		if (sdl3::keyboard::IsPressed(SDL_SCANCODE_PAGEUP))
			delta.y += 1.f;
		if (sdl3::keyboard::IsPressed(SDL_SCANCODE_PAGEDOWN))
			delta.y -= 1.f;

		if (delta.x != 0.f || delta.y != 0.f || delta.z != 0.f)
			m_runtime.MoveCamera(delta * (speed * dt));
	}

	[[nodiscard]] ui::WidgetBuilder SectionTitle(const char *text) {
		ui::WidgetBuilder label = m_factory.Label(String(text));
		label.FontSize(12.f).Bold().TextColor(m_gui.Theme().muted).WAuto().HAuto();
		return label;
	}

	// ── Barre de menus ───────────────────────────────────────────────────────

	void BuildMenuBar(ecs::Entity parent) {
		ui::WidgetBuilder bar = m_factory.MenuBar();
		bar.Parent(parent).GrowW().HAuto().Bg(m_gui.Theme().panelBg);
		ecs::Entity barEntity = bar.Spawn();

		m_menuPopups.push_back(m_factory.Menu(
			barEntity, String("Fichier"), MenuAction("Nouveau projet", "Ctrl+N", [this] { OnNewProject(); }),
			MenuAction("Ouvrir…", "Ctrl+O", [this] { OnOpenProject(); }),
			MenuAction("Enregistrer", "Ctrl+S", [this] { OnSaveProject(); }),
			MenuAction("Exporter le rapport", "", [this] { OnExportReport(); })));

		m_menuPopups.push_back(m_factory.Menu(
			barEntity, String("Édition"), MenuAction("Annuler", "Ctrl+Z", [this] { OnUndo(); }),
			MenuAction("Rétablir", "Ctrl+Y", [this] { OnRedo(); }),
			MenuAction("Dupliquer la sélection", "Ctrl+D", [this] { OnDuplicate(); }),
			MenuAction("Supprimer la sélection", "Suppr", [this] { OnDeleteSelection(); }),
			MenuAction("Tout désélectionner", "Échap", [this] { m_runtime.ClearSelection(); }),
			MenuAction("Déplacer", "W", [this] { SetGizmoMode(Runtime::GizmoMode::TRANSLATE); }),
			MenuAction("Tourner", "E", [this] { SetGizmoMode(Runtime::GizmoMode::ROTATE); }),
			MenuAction("Redimensionner", "R", [this] { SetGizmoMode(Runtime::GizmoMode::SCALE); }),
			MenuAction("Magnétisme", "X", [this] { ToggleSnap(); })));

		m_menuPopups.push_back(m_factory.Menu(
			barEntity, String("Scène"), MenuAction("Vitrine", "", [this] { (void)m_runtime.SwitchScene("Vitrine"); }),
			MenuAction("Circuit", "", [this] { (void)m_runtime.SwitchScene("Circuit"); }),
			MenuAction("Laboratoire physique", "",
					   [this] { (void)m_runtime.SwitchScene("Laboratoire physique"); })));

		m_menuPopups.push_back(m_factory.Menu(
			barEntity, String("Objet"), MenuAction("Ajouter un cube", "", [this] { SpawnPrimitive(ShapeKind::BOX); }),
			MenuAction("Ajouter une sphère", "", [this] { SpawnPrimitive(ShapeKind::SPHERE); }),
			MenuAction("Ajouter un cylindre", "", [this] { SpawnPrimitive(ShapeKind::CYLINDER); }),
			MenuAction("Ajouter un tore", "", [this] { SpawnPrimitive(ShapeKind::TORUS); }),
			MenuAction("Cadrer la sélection", "F", [this] { OnFocusSelection(); })));

		m_menuPopups.push_back(
			m_factory.Menu(barEntity, String("Script"),
						   MenuAction("Exécuter l'éditeur de script", "Ctrl+Entrée", [this] { OnRunScript(); }),
						   MenuAction("Effacer la console", "", [this] { OnClearConsole(); })));

		m_menuPopups.push_back(m_factory.Menu(
			barEntity, String("Affichage"), MenuAction("Thème sombre", "", [this] { (void)SetTheme("dark"); }),
			MenuAction("Thème clair", "", [this] { (void)SetTheme("light"); }),
			MenuAction("Thème verre", "", [this] { (void)SetTheme("aero"); }),
			MenuAction("Console", "", [this] { (void)FocusPanel("console", 0); }),
			MenuAction("Profileur", "", [this] { (void)FocusPanel("profiler", 0); })));

		m_menuPopups.push_back(m_factory.Menu(
			barEntity, String("Aide"), MenuAction("Raccourcis", "", [this] { OnShowShortcuts(); }),
			MenuAction("À propos", "", [this] { OnAbout(); })));
	}

	[[nodiscard]] ui::WidgetBuilder MenuAction(const char *text, const char *shortcut, std::function<void()> action) {
		ui::WidgetBuilder item = m_factory.MenuItem(String(text), String(shortcut));
		item.OnClick(std::move(action));
		return item;
	}

	// ── Barre d'outils ───────────────────────────────────────────────────────

	void BuildToolbar(ecs::Entity parent) {
		ui::WidgetBuilder toolbar = m_factory.Row();
		toolbar.Gap(4.f).Pad(math::Sides{6.f, 4.f}).GrowW().HAuto().Align(ui::CrossAlign::Center);
		toolbar.Bg(m_gui.Theme().panelBg).Parent(parent);
		ecs::Entity toolbarEntity = toolbar.Spawn();

		AddToolButton(toolbarEntity, ui::MaterialIcons::INSERT_DRIVE_FILE, "Nouveau projet",
					  [this] { OnNewProject(); });
		AddToolButton(toolbarEntity, ui::MaterialIcons::FOLDER_OPEN, "Ouvrir un projet",
					  [this] { OnOpenProject(); });
		AddToolButton(toolbarEntity, ui::MaterialIcons::SAVE, "Enregistrer le projet", [this] { OnSaveProject(); });
		AddSeparator(toolbarEntity);

		AddToolButton(toolbarEntity, ui::MaterialIcons::UNDO, "Annuler (Ctrl+Z)", [this] { OnUndo(); });
		AddToolButton(toolbarEntity, ui::MaterialIcons::REDO, "Rétablir (Ctrl+Y)", [this] { OnRedo(); });
		AddSeparator(toolbarEntity);

		// Modes du manipulateur : les trois boutons que tout éditeur 3D place
		// côte à côte, avec les raccourcis d'Unity/Unreal.
		AddToolButton(toolbarEntity, ui::MaterialIcons::OPEN_IN_FULL, "Déplacer (W)",
					  [this] { SetGizmoMode(Runtime::GizmoMode::TRANSLATE); });
		AddToolButton(toolbarEntity, ui::MaterialIcons::ROTATE_RIGHT, "Tourner (E)",
					  [this] { SetGizmoMode(Runtime::GizmoMode::ROTATE); });
		AddToolButton(toolbarEntity, ui::MaterialIcons::ZOOM_OUT_MAP, "Redimensionner (R)",
					  [this] { SetGizmoMode(Runtime::GizmoMode::SCALE); });
		AddToolButton(toolbarEntity, ui::MaterialIcons::GRID_ON, "Magnétisme (X)", [this] { ToggleSnap(); });
		AddSeparator(toolbarEntity);

		AddToolButton(toolbarEntity, ui::MaterialIcons::VIEW_IN_AR, "Ajouter un cube",
					  [this] { SpawnPrimitive(ShapeKind::BOX); });
		AddToolButton(toolbarEntity, ui::MaterialIcons::PUBLIC, "Ajouter une sphère",
					  [this] { SpawnPrimitive(ShapeKind::SPHERE); });
		AddToolButton(toolbarEntity, ui::MaterialIcons::CATEGORY, "Ajouter un cylindre",
					  [this] { SpawnPrimitive(ShapeKind::CYLINDER); });
		AddToolButton(toolbarEntity, ui::MaterialIcons::TERRAIN, "Ajouter un tore",
					  [this] { SpawnPrimitive(ShapeKind::TORUS); });
		AddToolButton(toolbarEntity, ui::MaterialIcons::DELETE, "Supprimer la sélection",
					  [this] { OnDeleteSelection(); });
		AddSeparator(toolbarEntity);

		m_playButton = AddToolButton(toolbarEntity, ui::MaterialIcons::PLAY_ARROW, "Lancer / arrêter le mode Jeu (F5)",
									 [this] { m_runtime.TogglePlay(); }, &m_playIcon);
		AddToolButton(toolbarEntity, ui::MaterialIcons::VIDEOCAM, "Cadrer la sélection (F)",
					  [this] { OnFocusSelection(); });
		AddSeparator(toolbarEntity);

		// Sélecteur de scène : reflète le projet, et le change.
		std::vector<String> sceneNames = m_runtime.GetProject().SceneNames();
		int activeIndex = 0;
		for (size_t i = 0; i < sceneNames.size(); ++i)
			if (m_runtime.ActiveScene() && sceneNames[i] == m_runtime.ActiveScene()->name)
				activeIndex = int(i);

		ui::WidgetBuilder sceneCombo = m_factory.Combo(sceneNames, activeIndex);
		sceneCombo.Size(190.f, 28.f).Tooltip("Scène active").Parent(toolbarEntity);
		sceneCombo.OnChange([this, sceneNames](float index) {
			size_t i = size_t(index < 0.f ? 0.f : index);
			if (i < sceneNames.size())
				(void)m_runtime.SwitchScene(sceneNames[i]);
		});
		m_sceneCombo = sceneCombo.Spawn();

		std::vector<String> themeNames = {String("dark"), String("light"), String("aero")};
		int themeIndex = m_themeName == "light" ? 1 : (m_themeName == "aero" ? 2 : 0);
		ui::WidgetBuilder themeCombo = m_factory.Combo(themeNames, themeIndex);
		themeCombo.Size(110.f, 28.f).Tooltip("Thème de l'interface").Parent(toolbarEntity);
		themeCombo.OnChange([this, themeNames](float index) {
			size_t i = size_t(index < 0.f ? 0.f : index);
			if (i < themeNames.size())
				(void)SetTheme(themeNames[i]);
		});
		themeCombo.Spawn();

		// Pousse le badge de mode tout à droite.
		ui::WidgetBuilder spacer = m_factory.Row();
		spacer.GrowW().HAuto().Parent(toolbarEntity);
		spacer.Spawn();

		ui::WidgetBuilder modeBadge = m_factory.Badge(String("ÉDITION"));
		modeBadge.Parent(toolbarEntity);
		m_modeBadge = modeBadge.Spawn();
	}

	/// Bouton d'outil = bouton sans texte + icône centrée par-dessus.
	/// `outIcon` récupère l'entité de l'icône pour les boutons dont le glyphe
	/// change en cours de route (le bouton Jeu/Arrêt) — `UiButton` n'a pas
	/// d'état « enfoncé » persistant à détourner pour ça.
	template <typename IconEnum>
	ecs::Entity AddToolButton(ecs::Entity parent, IconEnum icon, const char *tooltip, std::function<void()> action,
							  ecs::Entity *outIcon = nullptr) {
		ui::WidgetBuilder button = m_factory.Button(String());
		button.Size(32.f, 28.f).Tooltip(String(tooltip)).Parent(parent);
		button.OnClick(std::move(action));
		ecs::Entity entity = button.Spawn();

		ui::WidgetBuilder glyph = m_factory.Icon(icon, 18.f);
		glyph.Parent(entity).Anchor(ui::Anchor::Center).Absolute();
		ecs::Entity glyphEntity = glyph.Spawn();
		if (outIcon)
			*outIcon = glyphEntity;
		return entity;
	}

	void AddSeparator(ecs::Entity parent) {
		ui::WidgetBuilder separator = m_factory.Separator(ui::Orientation::Vertical);
		separator.Size(1.f, 22.f).Parent(parent);
		separator.Spawn();
	}

	// ── Dock gauche ──────────────────────────────────────────────────────────

	void BuildLeftDock(ecs::Entity parent) {
		ui::WidgetBuilder dock = m_factory.Panel();
		dock.W(ui::Dimension::Px(LEFT_DOCK_WIDTH)).GrowH().Gap(0.f).Pad(0.f).Parent(parent);
		ecs::Entity dockEntity = dock.Spawn();

		ui::WidgetBuilder tabs =
			m_factory.Tabview({String("Outliner"), String("Scènes"), String("Ressources")}, 0);
		tabs.GrowW().GrowH().Parent(dockEntity);
		m_leftTabs = tabs.Spawn();

		m_outlinerList = AddTabPage(m_leftTabs);
		m_sceneList = AddTabPage(m_leftTabs);
		m_assetList = AddTabPage(m_leftTabs);
		(void)SetTabActive(m_leftTabs, 0);
	}

	/// Une page d'onglet : un conteneur défilable qui remplit la zone.
	ecs::Entity AddTabPage(ecs::Entity tabview) {
		ui::WidgetBuilder page = m_factory.Column();
		page.Gap(4.f).Pad(2.f).GrowW().GrowH().Scrollable().Clip().Parent(tabview);
		return page.Spawn();
	}

	void RefreshOutliner() {
		if (!m_outlinerList.Valid())
			return;
		ClearChildren(m_outlinerList);

		const SceneDesc *scene = m_runtime.ActiveScene();
		if (!scene)
			return;

		ui::WidgetBuilder header = SectionTitle("HIÉRARCHIE");
		header.Parent(m_outlinerList);
		header.Spawn();

		Option<String> selected = m_runtime.SelectedName();
		int index = 0;
		// Les objets racine d'abord, chacun suivi de ses enfants : un niveau
		// d'imbrication suffit pour ce que le document permet d'exprimer
		// (un `parent` par objet, pas de cycle possible).
		for (const ObjectDesc &object : scene->objects) {
			if (!object.parent.IsEmpty())
				continue;
			AddOutlinerRow(object, index++, 0, selected);
			for (const ObjectDesc &child : scene->objects)
				if (child.parent == object.name)
					AddOutlinerRow(child, index++, 1, selected);
		}
	}

	void AddOutlinerRow(const ObjectDesc &object, int index, int depth, const Option<String> &selected) {
		String label = String::Format("%s%s  %s", depth > 0 ? "   " : "", ShapeGlyph(object.shape),
									  object.name.CStr());
		ui::WidgetBuilder row = m_factory.Selectable(label, index);
		row.GrowW().HAuto().Parent(m_outlinerList);
		if (!object.visible)
			row.TextColor(m_gui.Theme().muted);
		if (!object.tag.IsEmpty())
			row.Tooltip(String::Format("%s — étiquette « %s »", ShapeKindName(object.shape), object.tag.CStr()));
		String name = object.name;
		row.OnClick([this, name] { (void)m_runtime.Select(name); });
		ecs::Entity entity = row.Spawn();

		if (selected.IsSome() && selected.Unwrap() == object.name)
			if (auto selectable = m_registry.GetComponent<ui::UiSelectable>(entity); selectable.IsSome())
				selectable.Unwrap()->selected = true;
	}

	/// Petit repère textuel par famille de forme — le même rôle qu'une icône
	/// dans l'outliner d'un vrai éditeur, sans dépendre d'une police d'icônes
	/// dans une liste qui peut compter des centaines de lignes.
	[[nodiscard]] static const char *ShapeGlyph(ShapeKind shape) noexcept {
		switch (shape) {
			case ShapeKind::BOX:
				return "▣";
			case ShapeKind::SPHERE:
				return "●";
			case ShapeKind::CYLINDER:
				return "▮";
			case ShapeKind::CONE:
				return "▲";
			case ShapeKind::TORUS:
				return "◎";
			case ShapeKind::PLANE:
				return "▭";
			case ShapeKind::ICOSAHEDRON:
				return "◆";
			case ShapeKind::TORUS_KNOT:
				return "✺";
			case ShapeKind::PORTAL_QUAD:
				return "◫";
			case ShapeKind::MODEL:
				return "⬟";
		}
		return "▣";
	}

	void RefreshSceneList() {
		if (!m_sceneList.Valid())
			return;
		ClearChildren(m_sceneList);

		ui::WidgetBuilder header = SectionTitle("SCÈNES DU PROJET");
		header.Parent(m_sceneList);
		header.Spawn();

		const Project &project = m_runtime.GetProject();
		int index = 0;
		for (const SceneDesc &scene : project.scenes) {
			bool active = project.ActiveScene() && project.ActiveScene()->name == scene.name;
			ui::WidgetBuilder row =
				m_factory.Selectable(String::Format("%s%s", active ? "▶ " : "   ", scene.name.CStr()), index++);
			row.GrowW().HAuto().Tooltip(scene.description).Parent(m_sceneList);
			String name = scene.name;
			row.OnClick([this, name] { (void)m_runtime.SwitchScene(name); });
			row.Spawn();

			ui::WidgetBuilder detail =
				m_factory.Label(String::Format("      %d objets%s", int(scene.objects.size()),
											   scene.gameplayScript.IsEmpty() ? "" : " · script"));
			detail.FontSize(11.f).TextColor(m_gui.Theme().muted).GrowW().HAuto().Parent(m_sceneList);
			detail.Spawn();
		}
	}

	/// Onglet « Ressources » : ce que le projet met à disposition (formes,
	/// familles de matériau) sous forme de boutons d'ajout — l'équivalent du
	/// navigateur d'assets d'un éditeur, avec le contenu réellement
	/// disponible ici plutôt qu'une fausse arborescence de fichiers.
	void RefreshAssets() {
		if (!m_assetList.Valid())
			return;
		ClearChildren(m_assetList);

		ui::WidgetBuilder header = SectionTitle("PRIMITIVES");
		header.Parent(m_assetList);
		header.Spawn();

		struct Entry {
			const char *label;
			ShapeKind shape;
		};
		static constexpr Entry PRIMITIVES[] = {
			{"Cube", ShapeKind::BOX},           {"Sphère", ShapeKind::SPHERE},
			{"Cylindre", ShapeKind::CYLINDER},  {"Cône", ShapeKind::CONE},
			{"Tore", ShapeKind::TORUS},         {"Plan", ShapeKind::PLANE},
			{"Icosaèdre", ShapeKind::ICOSAHEDRON}, {"Nœud de tore", ShapeKind::TORUS_KNOT},
		};
		for (const Entry &entry : PRIMITIVES) {
			ui::WidgetBuilder button =
				m_factory.Button(String::Format("%s  %s", ShapeGlyph(entry.shape), entry.label));
			button.GrowW().HAuto().Parent(m_assetList);
			ShapeKind shape = entry.shape;
			button.OnClick([this, shape] { SpawnPrimitive(shape); });
			button.Spawn();
		}

		ui::WidgetBuilder modelsHeader = SectionTitle("MODÈLES glTF");
		modelsHeader.Parent(m_assetList);
		modelsHeader.Spawn();

		// Les modèles livrés avec le dépôt, inventoriés SANS charger leur
		// géométrie (`loadBuffers = false`) : ouvrir le panneau ne doit pas
		// lire des mégaoctets de sommets.
		for (const char *path : GLTF_ASSETS) {
			String file(path);
			auto document = data::gltf::LoadFile(file, false);
			if (document.IsError())
				continue; // modèle absent de cette copie du dépôt : on l'omet

			ui::WidgetBuilder button = m_factory.Button(String::Format("⬇  %s", Runtime::FileStem(file).CStr()));
			button.GrowW().HAuto().Parent(m_assetList);
			button.Tooltip(String::Format("%s — %d maillage(s), %d matériau(x)", path,
										  int(document.Value().meshes.size()),
										  int(document.Value().materials.size())));
			button.OnClick([this, file] {
				auto imported = m_runtime.ImportGltf(file);
				if (imported.IsError())
					m_runtime.LogError(String::Format("Import : %s", imported.Error().CStr()));
				else
					(void)m_runtime.Select(imported.Value());
			});
			button.Spawn();
		}

		ui::WidgetBuilder materialsHeader = SectionTitle("MATÉRIAUX");
		materialsHeader.Parent(m_assetList);
		materialsHeader.Spawn();

		static constexpr MaterialKind MATERIALS[] = {MaterialKind::PLASTIC, MaterialKind::METAL, MaterialKind::WOOD,
													 MaterialKind::PBR, MaterialKind::UNLIT};
		for (MaterialKind kind : MATERIALS) {
			ui::WidgetBuilder button = m_factory.Button(String(MaterialKindName(kind)));
			button.GrowW().HAuto().Tooltip(String("Appliquer à la sélection")).Parent(m_assetList);
			button.OnClick([this, kind] {
				ObjectDesc *object = m_runtime.SelectedObject();
				if (!object)
					return;
				MaterialDesc material = object->material;
				material.kind = kind;
				(void)m_runtime.SetMaterial(object->name, material);
				m_inspectorDirty = true;
			});
			button.Spawn();
		}
	}

	// ── Zone centrale ────────────────────────────────────────────────────────

	void BuildCenter(ecs::Entity parent) {
		ui::WidgetBuilder center = m_factory.Column();
		center.Gap(6.f).Pad(0.f).GrowW().GrowH().Parent(parent);
		ecs::Entity centerEntity = center.Spawn();

		ui::WidgetBuilder viewport = m_factory.Viewport3D(m_runtime.SceneRoot(), m_runtime.ActiveCamera());
		viewport.GrowW().GrowH().Radius(6.f).Parent(centerEntity);
		m_viewport = viewport.Spawn();

		BuildBottomDock(centerEntity);
	}

	void BuildBottomDock(ecs::Entity parent) {
		ui::WidgetBuilder dock = m_factory.Panel();
		dock.GrowW().H(ui::Dimension::Px(BOTTOM_DOCK_HEIGHT)).Gap(0.f).Pad(0.f).Parent(parent);
		ecs::Entity dockEntity = dock.Spawn();

		ui::WidgetBuilder tabs = m_factory.Tabview({String("Console"), String("Script"), String("Profil")}, 0);
		tabs.GrowW().GrowH().Parent(dockEntity);
		m_bottomTabs = tabs.Spawn();

		// Console : zone de texte en lecture seule (copiable), remplie par
		// RefreshConsole. Enveloppée dans une page qui CLIPPE : le rendu de
		// `UiInputArea` déborde au-dessus de sa boîte quand le texte dépasse
		// sa hauteur, et les premières lignes venaient se superposer à la
		// barre d'onglets juste au-dessus.
		ui::WidgetBuilder consolePage = m_factory.Column();
		consolePage.Gap(0.f).Pad(0.f).GrowW().GrowH().Clip().Parent(m_bottomTabs);
		ecs::Entity consolePageEntity = consolePage.Spawn();

		ui::WidgetBuilder console = m_factory.InputArea(String("(journal vide)"));
		console.GrowW().GrowH().IoMode(ui::IOMode::READ_AND_COPY_ONLY).FontSize(12.f).Parent(consolePageEntity);
		m_console = console.Spawn();

		// Éditeur de script : la console interactive de l'éditeur.
		ui::WidgetBuilder scriptPage = m_factory.Column();
		scriptPage.Gap(4.f).Pad(2.f).GrowW().GrowH().Clip().Parent(m_bottomTabs);
		ecs::Entity scriptPageEntity = scriptPage.Spawn();

		ui::WidgetBuilder editor = m_factory.InputArea(String("# Script d'éditeur — Ctrl+Entrée pour exécuter"));
		editor.GrowW().GrowH().FontSize(12.f).Parent(scriptPageEntity);
		m_scriptEditor = editor.Spawn();
		if (auto area = m_registry.GetComponent<ui::UiInputArea>(m_scriptEditor); area.IsSome())
			area.Unwrap()->text = String(DEFAULT_SCRIPT_SNIPPET);

		ui::WidgetBuilder scriptBar = m_factory.Row();
		scriptBar.Gap(6.f).GrowW().HAuto().Parent(scriptPageEntity);
		ecs::Entity scriptBarEntity = scriptBar.Spawn();

		ui::WidgetBuilder run = m_factory.Button(String("Exécuter"));
		run.HAuto().Parent(scriptBarEntity);
		run.OnClick([this] { OnRunScript(); });
		run.Spawn();

		ui::WidgetBuilder clear = m_factory.Button(String("Effacer la console"));
		clear.HAuto().Parent(scriptBarEntity);
		clear.OnClick([this] { OnClearConsole(); });
		clear.Spawn();

		// Profileur.
		ui::WidgetBuilder profiler = m_factory.Column();
		profiler.Gap(4.f).Pad(4.f).GrowW().GrowH().Parent(m_bottomTabs);
		m_profilerPage = profiler.Spawn();

		(void)SetTabActive(m_bottomTabs, 0);
	}

	void RefreshConsole() {
		if (!m_console.Valid())
			return;
		auto area = m_registry.GetComponent<ui::UiInputArea>(m_console);
		if (area.IsNone())
			return;

		// Seules les dernières lignes sont affichées : recopier un journal de
		// 500 entrées à chaque nouvelle ligne coûterait plus cher que tout le
		// reste de l'interface réunie.
		const std::vector<LogEntry> &log = m_runtime.Log();
		size_t start = log.size() > CONSOLE_LINES ? log.size() - CONSOLE_LINES : 0;
		String text;
		for (size_t i = start; i < log.size(); ++i) {
			text.Concat(String::Format("[%-6s] %s", log[i].LevelName(), log[i].text.CStr()));
			text.Concat("\n");
		}
		area.Unwrap()->text = std::move(text);
	}

	void RefreshProfiler() {
		if (!m_profilerPage.Valid())
			return;
		ClearChildren(m_profilerPage);

		const SceneDesc *scene = m_runtime.ActiveScene();
		AddProfilerRow("Cadence", String::Format("%.1f img/s", m_fps));
		AddProfilerRow("Objets (scène)", String::From(int(scene ? scene->objects.size() : 0)));
		AddProfilerRow("Nœuds 3D", String::From(int(m_runtime.RuntimeObjectCount())));
		AddProfilerRow("Corps physiques", String::From(int(m_runtime.RigidBodyCount())));
		AddProfilerRow("Entités ECS", String::From(int(m_registry.AliveCount())));
		AddProfilerRow("Scripts exécutés", String::From(int(m_runtime.ScriptRunCount())));
		AddProfilerRow("Rappels de script", String::From(int(m_runtime.ScriptCallCount())));
		AddProfilerRow("Erreurs de script", String::From(int(m_runtime.ScriptErrorCount())));
		AddProfilerRow("Fils du vivier", String::Format("%d (pic %d en parallèle)",
														int(m_runtime.Jobs().WorkerCount()),
														m_runtime.Jobs().PeakConcurrentWorkers()));
		AddProfilerRow("Mode", String(m_runtime.IsPlaying() ? "Jeu" : "Édition"));

		if (m_fpsHistory.size() >= 2) {
			ui::WidgetBuilder plot = m_factory.PlotLines(m_fpsHistory, String("images / seconde"));
			plot.GrowW().H(ui::Dimension::Px(140.f)).Parent(m_profilerPage);
			plot.Spawn();
		}
	}

	void AddProfilerRow(const char *label, const String &value) {
		ui::WidgetBuilder row = m_factory.Row();
		row.Gap(8.f).GrowW().HAuto().Parent(m_profilerPage);
		ecs::Entity rowEntity = row.Spawn();

		ui::WidgetBuilder name = m_factory.Label(String(label));
		name.Size(150.f, 0.f).HAuto().FontSize(12.f).TextColor(m_gui.Theme().muted).Parent(rowEntity);
		name.Spawn();

		ui::WidgetBuilder text = m_factory.Label(value);
		text.HAuto().FontSize(12.f).Parent(rowEntity);
		text.Spawn();
	}

	// ── Dock droit : l'inspecteur ────────────────────────────────────────────

	void BuildRightDock(ecs::Entity parent) {
		ui::WidgetBuilder dock = m_factory.Panel();
		dock.W(ui::Dimension::Px(RIGHT_DOCK_WIDTH)).GrowH().Gap(0.f).Pad(0.f).Parent(parent);
		ecs::Entity dockEntity = dock.Spawn();

		ui::WidgetBuilder tabs = m_factory.Tabview(
			{String("Transform"), String("Matériau"), String("Physique"), String("Monde")}, 0);
		tabs.GrowW().GrowH().Parent(dockEntity);
		// Changer d'onglet reconstruit la page qui apparaît : elle peut dater
		// d'une sélection précédente (cf. RefreshInspector).
		tabs.OnChange([this](float) { m_inspectorDirty = true; });
		m_rightTabs = tabs.Spawn();

		m_transformPage = AddTabPage(m_rightTabs);
		m_materialPage = AddTabPage(m_rightTabs);
		m_physicsPage = AddTabPage(m_rightTabs);
		m_worldPage = AddTabPage(m_rightTabs);
		(void)SetTabActive(m_rightTabs, 0);
	}

	/// Ne reconstruit que l'onglet d'inspecteur AFFICHÉ : les trois autres le
	/// seront à leur prochaine apparition (cf. SetTabActive). Une sélection
	/// reconstruisait auparavant les quatre pages d'un coup, dont trois
	/// invisibles.
	void RefreshInspector() {
		int active = ActiveTab(m_rightTabs);
		if (active < 0 || active == int(InspectorTab::TRANSFORM))
			RefreshTransformPage();
		if (active < 0 || active == int(InspectorTab::MATERIAL))
			RefreshMaterialPage();
		if (active < 0 || active == int(InspectorTab::PHYSICS))
			RefreshPhysicsPage();
		if (active < 0 || active == int(InspectorTab::WORLD))
			RefreshWorldPage();
	}

	[[nodiscard]] ObjectDesc *Selected() { return m_runtime.SelectedObject(); }

	void AddEmptyNotice(ecs::Entity page) {
		ui::WidgetBuilder notice = m_factory.Label(String("Aucun objet sélectionné"));
		notice.TextColor(m_gui.Theme().muted).GrowW().HAuto().Parent(page);
		notice.Spawn();
	}

	/// En-tête commun aux trois onglets d'objet : nom éditable + étiquette.
	void AddObjectHeader(ecs::Entity page, const ObjectDesc &object) {
		ui::WidgetBuilder title = m_factory.Label(object.name);
		title.FontSize(15.f).Bold().GrowW().HAuto().Parent(page);
		title.Spawn();

		ui::WidgetBuilder subtitle = m_factory.Label(
			String::Format("%s%s%s", ShapeKindName(object.shape), object.tag.IsEmpty() ? "" : " · ",
						   object.tag.CStr()));
		subtitle.FontSize(11.f).TextColor(m_gui.Theme().muted).GrowW().HAuto().Parent(page);
		subtitle.Spawn();
	}

	void RefreshTransformPage() {
		if (!m_transformPage.Valid())
			return;
		ClearChildren(m_transformPage);
		ObjectDesc *object = Selected();
		if (!object) {
			AddEmptyNotice(m_transformPage);
			return;
		}
		AddObjectHeader(m_transformPage, *object);
		String name = object->name;

		AddVectorRow(m_transformPage, "Position", object->transform.position, -200.f, 200.f, 0.05f,
					 [this, name](math::FVector3 value) { (void)m_runtime.SetPosition(name, value); });
		AddVectorRow(m_transformPage, "Rotation", object->transform.eulerDeg, -180.f, 180.f, 0.5f,
					 [this, name](math::FVector3 value) { (void)m_runtime.SetEulerDegrees(name, value); });
		AddVectorRow(m_transformPage, "Échelle", object->transform.scale, 0.01f, 20.f, 0.02f,
					 [this, name](math::FVector3 value) { (void)m_runtime.SetScale(name, value); });

		AddCheckboxRow(m_transformPage, "Visible", object->visible, [this, name](bool value) {
			(void)m_runtime.SetVisible(name, value);
			m_outlinerDirty = true;
		});

		ui::WidgetBuilder focus = m_factory.Button(String("Cadrer sur cet objet"));
		focus.GrowW().HAuto().Parent(m_transformPage);
		focus.OnClick([this, name] { (void)m_runtime.FocusOn(name); });
		focus.Spawn();

		ui::WidgetBuilder duplicate = m_factory.Button(String("Dupliquer"));
		duplicate.GrowW().HAuto().Parent(m_transformPage);
		duplicate.OnClick([this] { OnDuplicate(); });
		duplicate.Spawn();

		ui::WidgetBuilder remove = m_factory.Button(String("Supprimer"));
		remove.GrowW().HAuto().Parent(m_transformPage);
		remove.OnClick([this] { OnDeleteSelection(); });
		remove.Spawn();
	}

	void RefreshMaterialPage() {
		if (!m_materialPage.Valid())
			return;
		ClearChildren(m_materialPage);
		ObjectDesc *object = Selected();
		if (!object) {
			AddEmptyNotice(m_materialPage);
			return;
		}
		AddObjectHeader(m_materialPage, *object);
		String name = object->name;
		const MaterialDesc &material = object->material;

		std::vector<String> kinds;
		for (MaterialKind kind : {MaterialKind::PLASTIC, MaterialKind::METAL, MaterialKind::WOOD, MaterialKind::PBR,
								  MaterialKind::UNLIT, MaterialKind::BASIC})
			kinds.push_back(String(MaterialKindName(kind)));

		AddComboRow(m_materialPage, "Famille", kinds, int(material.kind), [this, name](int index) {
			ObjectDesc *target = m_runtime.ActiveScene() ? m_runtime.ActiveScene()->Find(name) : nullptr;
			if (!target)
				return;
			MaterialDesc updated = target->material;
			updated.kind = MaterialKind(index);
			(void)m_runtime.SetMaterial(name, updated);
		});

		AddColorRow(m_materialPage, "Couleur", material.baseColor, [this, name](sdl3::Color color) {
			(void)m_runtime.SetMaterialColor(name, color);
		});

		AddSliderRow(m_materialPage, "Métallique", material.metallic, 0.f, 1.f, [this, name](float value) {
			ObjectDesc *target = m_runtime.ActiveScene() ? m_runtime.ActiveScene()->Find(name) : nullptr;
			if (!target)
				return;
			MaterialDesc updated = target->material;
			updated.metallic = value;
			(void)m_runtime.SetMaterial(name, updated);
		});

		AddSliderRow(m_materialPage, "Rugosité", material.roughness, 0.f, 1.f, [this, name](float value) {
			ObjectDesc *target = m_runtime.ActiveScene() ? m_runtime.ActiveScene()->Find(name) : nullptr;
			if (!target)
				return;
			MaterialDesc updated = target->material;
			updated.roughness = value;
			(void)m_runtime.SetMaterial(name, updated);
		});

		AddCheckboxRow(m_materialPage, "Double face", material.doubleSided, [this, name](bool value) {
			ObjectDesc *target = m_runtime.ActiveScene() ? m_runtime.ActiveScene()->Find(name) : nullptr;
			if (!target)
				return;
			MaterialDesc updated = target->material;
			updated.doubleSided = value;
			(void)m_runtime.SetMaterial(name, updated);
		});

		AddCheckboxRow(m_materialPage, "Fil de fer", material.wireframe, [this, name](bool value) {
			ObjectDesc *target = m_runtime.ActiveScene() ? m_runtime.ActiveScene()->Find(name) : nullptr;
			if (!target)
				return;
			MaterialDesc updated = target->material;
			updated.wireframe = value;
			(void)m_runtime.SetMaterial(name, updated);
		});
	}

	void RefreshPhysicsPage() {
		if (!m_physicsPage.Valid())
			return;
		ClearChildren(m_physicsPage);
		ObjectDesc *object = Selected();
		if (!object) {
			AddEmptyNotice(m_physicsPage);
			return;
		}
		AddObjectHeader(m_physicsPage, *object);
		String name = object->name;
		const PhysicsDesc &physics = object->physics;

		auto mutatePhysics = [this, name](const std::function<void(PhysicsDesc &)> &change) {
			ObjectDesc *target = m_runtime.ActiveScene() ? m_runtime.ActiveScene()->Find(name) : nullptr;
			if (!target)
				return;
			PhysicsDesc updated = target->physics;
			change(updated);
			(void)m_runtime.SetPhysics(name, updated);
		};

		AddComboRow(m_physicsPage, "Corps",
					{String("aucun"), String("statique"), String("dynamique")}, int(physics.body),
					[mutatePhysics](int index) {
						mutatePhysics([index](PhysicsDesc &desc) { desc.body = BodyKind(index); });
					});

		AddComboRow(m_physicsPage, "Collision", {String("boîte"), String("sphère"), String("capsule")},
					int(physics.collider), [mutatePhysics](int index) {
						mutatePhysics([index](PhysicsDesc &desc) { desc.collider = ColliderKind(index); });
					});

		AddVectorRow(m_physicsPage, "Demi-dim", physics.halfExtents, 0.01f, 50.f, 0.02f,
					 [mutatePhysics](math::FVector3 value) {
						 mutatePhysics([value](PhysicsDesc &desc) { desc.halfExtents = value; });
					 });

		AddDragRow(m_physicsPage, "Masse", physics.mass, 0.01f, 5000.f, 0.5f, [mutatePhysics](float value) {
			mutatePhysics([value](PhysicsDesc &desc) { desc.mass = value; });
		});

		AddSliderRow(m_physicsPage, "Rebond", physics.restitution, 0.f, 1.f, [mutatePhysics](float value) {
			mutatePhysics([value](PhysicsDesc &desc) { desc.restitution = value; });
		});

		AddSliderRow(m_physicsPage, "Frottement", physics.friction, 0.f, 2.f, [mutatePhysics](float value) {
			mutatePhysics([value](PhysicsDesc &desc) { desc.friction = value; });
		});

		ui::WidgetBuilder impulse = m_factory.Button(String("Impulsion vers le haut"));
		impulse.GrowW().HAuto().Tooltip(String("Test rapide : n'a d'effet qu'en mode Jeu")).Parent(m_physicsPage);
		impulse.OnClick([this, name] { (void)m_runtime.ApplyImpulse(name, math::FVector3{0.f, 240.f, 0.f}); });
		impulse.Spawn();
	}

	void RefreshWorldPage() {
		if (!m_worldPage.Valid())
			return;
		ClearChildren(m_worldPage);
		SceneDesc *scene = m_runtime.ActiveScene();
		if (!scene) {
			AddEmptyNotice(m_worldPage);
			return;
		}

		ui::WidgetBuilder title = m_factory.Label(scene->name);
		title.FontSize(15.f).Bold().GrowW().HAuto().Parent(m_worldPage);
		title.Spawn();

		ui::WidgetBuilder description = m_factory.Label(scene->description);
		description.FontSize(11.f).TextColor(m_gui.Theme().muted).GrowW().HAuto().Parent(m_worldPage);
		description.Spawn();

		AddVectorRow(m_worldPage, "Gravité", scene->environment.gravity, -40.f, 40.f, 0.05f,
					 [this](math::FVector3 value) {
						 if (SceneDesc *target = m_runtime.ActiveScene())
							 target->environment.gravity = value;
						 // La gravité est lue par physics::World à chaque pas :
						 // il suffit de la reposer, sans rien reconstruire.
						 m_runtime.ApplyEnvironment();
					 });

		AddSliderRow(m_worldPage, "Soleil", scene->environment.sunIntensity, 0.f, 3.f, [this](float value) {
			if (SceneDesc *target = m_runtime.ActiveScene())
				target->environment.sunIntensity = value;
			m_runtime.ApplyEnvironment();
		});

		AddColorRow(m_worldPage, "Ambiance", scene->environment.ambientColor, [this](sdl3::Color color) {
			if (SceneDesc *target = m_runtime.ActiveScene())
				target->environment.ambientColor = color;
			m_runtime.ApplyEnvironment();
		});

		AddColorRow(m_worldPage, "Fond", scene->environment.backgroundColor, [this](sdl3::Color color) {
			if (SceneDesc *target = m_runtime.ActiveScene())
				target->environment.backgroundColor = color;
			m_runtime.ApplyEnvironment();
		});

		ui::WidgetBuilder stats = m_factory.Label(
			String::Format("%d objets · %d corps simulés", int(scene->objects.size()),
						   int(m_runtime.RigidBodyCount())));
		stats.FontSize(11.f).TextColor(m_gui.Theme().muted).GrowW().HAuto().Parent(m_worldPage);
		stats.Spawn();
	}

	// ── Lignes d'inspecteur réutilisables ────────────────────────────────────

	/// Libellé + 3 champs glissants (x/y/z) ; `onChange` reçoit le VECTEUR
	/// complet reconstitué, pour que l'appelant n'ait pas à recoller les
	/// composantes lui-même à chaque fois.
	void AddVectorRow(ecs::Entity page, const char *label, math::FVector3 value, float minValue, float maxValue,
					  float speed, std::function<void(math::FVector3)> onChange) {
		ui::WidgetBuilder row = m_factory.Row();
		row.Gap(3.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(page);
		ecs::Entity rowEntity = row.Spawn();

		ui::WidgetBuilder name = m_factory.Label(String(label));
		name.Size(INSPECTOR_LABEL_WIDTH, 0.f).HAuto().FontSize(12.f).TextColor(m_gui.Theme().muted).Parent(rowEntity);
		name.Spawn();

		// L'état vit dans un shared_ptr partagé par les trois rappels : chacun
		// n'écrit que SA composante et republie le vecteur entier.
		auto state = std::make_shared<math::FVector3>(value);
		const float components[3] = {value.x, value.y, value.z};
		for (int axis = 0; axis < 3; ++axis) {
			ui::WidgetBuilder field = m_factory.DragValue(minValue, maxValue, components[axis], 0.f, speed, 2);
			field.GrowW().H(ui::Dimension::Px(22.f)).Parent(rowEntity);
			field.OnChange([state, axis, onChange](float componentValue) {
				if (axis == 0)
					state->x = componentValue;
				else if (axis == 1)
					state->y = componentValue;
				else
					state->z = componentValue;
				onChange(*state);
			});
			field.Spawn();
		}
	}

	void AddDragRow(ecs::Entity page, const char *label, float value, float minValue, float maxValue, float speed,
					std::function<void(float)> onChange) {
		ui::WidgetBuilder row = m_factory.Row();
		row.Gap(6.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(page);
		ecs::Entity rowEntity = row.Spawn();

		ui::WidgetBuilder name = m_factory.Label(String(label));
		name.Size(INSPECTOR_LABEL_WIDTH, 0.f).HAuto().FontSize(12.f).TextColor(m_gui.Theme().muted).Parent(rowEntity);
		name.Spawn();

		ui::WidgetBuilder field = m_factory.DragValue(minValue, maxValue, value, 0.f, speed, 2);
		field.GrowW().H(ui::Dimension::Px(22.f)).Parent(rowEntity);
		field.OnChange(std::move(onChange));
		field.Spawn();
	}

	void AddSliderRow(ecs::Entity page, const char *label, float value, float minValue, float maxValue,
					  std::function<void(float)> onChange) {
		ui::WidgetBuilder row = m_factory.Row();
		row.Gap(6.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(page);
		ecs::Entity rowEntity = row.Spawn();

		ui::WidgetBuilder name = m_factory.Label(String(label));
		name.Size(INSPECTOR_LABEL_WIDTH, 0.f).HAuto().FontSize(12.f).TextColor(m_gui.Theme().muted).Parent(rowEntity);
		name.Spawn();

		ui::WidgetBuilder slider = m_factory.Slider(minValue, maxValue, value);
		slider.GrowW().H(ui::Dimension::Px(18.f)).Parent(rowEntity);
		slider.OnChange(std::move(onChange));
		slider.Spawn();
	}

	void AddCheckboxRow(ecs::Entity page, const char *label, bool value, std::function<void(bool)> onToggle) {
		ui::WidgetBuilder row = m_factory.Row();
		row.Gap(6.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(page);
		ecs::Entity rowEntity = row.Spawn();

		ui::WidgetBuilder name = m_factory.Label(String(label));
		name.Size(INSPECTOR_LABEL_WIDTH, 0.f).HAuto().FontSize(12.f).TextColor(m_gui.Theme().muted).Parent(rowEntity);
		name.Spawn();

		ui::WidgetBuilder box = m_factory.Checkbox(value);
		box.Size(18.f, 18.f).Parent(rowEntity);
		box.OnToggle(std::move(onToggle));
		box.Spawn();
	}

	void AddComboRow(ecs::Entity page, const char *label, std::vector<String> items, int selected,
					 std::function<void(int)> onChange) {
		ui::WidgetBuilder row = m_factory.Row();
		row.Gap(6.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(page);
		ecs::Entity rowEntity = row.Spawn();

		ui::WidgetBuilder name = m_factory.Label(String(label));
		name.Size(INSPECTOR_LABEL_WIDTH, 0.f).HAuto().FontSize(12.f).TextColor(m_gui.Theme().muted).Parent(rowEntity);
		name.Spawn();

		ui::WidgetBuilder combo = m_factory.Combo(std::move(items), selected);
		combo.GrowW().H(ui::Dimension::Px(24.f)).Parent(rowEntity);
		combo.OnChange([onChange](float index) { onChange(int(index)); });
		combo.Spawn();
	}

	/// Libellé + pastille de couleur + trois champs R/G/B. Pas de sélecteur
	/// en popup ici : les trois champs se pilotent aussi bien à la souris
	/// qu'AU SCRIPT, ce qui compte davantage pour cette démo.
	void AddColorRow(ecs::Entity page, const char *label, sdl3::Color color, std::function<void(sdl3::Color)> onChange) {
		ui::WidgetBuilder row = m_factory.Row();
		row.Gap(4.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(page);
		ecs::Entity rowEntity = row.Spawn();

		ui::WidgetBuilder name = m_factory.Label(String(label));
		name.Size(INSPECTOR_LABEL_WIDTH, 0.f).HAuto().FontSize(12.f).TextColor(m_gui.Theme().muted).Parent(rowEntity);
		name.Spawn();

		ui::WidgetBuilder swatch =
			m_factory.ColorSwatch(sdl3::FColor{float(color.r) / 255.f, float(color.g) / 255.f,
											   float(color.b) / 255.f, 1.f});
		swatch.Size(22.f, 22.f).Parent(rowEntity);
		ecs::Entity swatchEntity = swatch.Spawn();

		auto state = std::make_shared<sdl3::Color>(color);
		const uint8_t channels[3] = {color.r, color.g, color.b};
		for (int channel = 0; channel < 3; ++channel) {
			ui::WidgetBuilder field = m_factory.DragValue(0.f, 255.f, float(channels[channel]), 1.f, 1.f, 0);
			field.GrowW().H(ui::Dimension::Px(22.f)).Parent(rowEntity);
			field.OnChange([this, state, channel, onChange, swatchEntity](float value) {
				uint8_t byteValue = uint8_t(sdl3::Clamp(value, 0.f, 255.f));
				if (channel == 0)
					state->r = byteValue;
				else if (channel == 1)
					state->g = byteValue;
				else
					state->b = byteValue;
				if (auto widget = m_registry.GetComponent<ui::UiColorSwatch>(swatchEntity); widget.IsSome())
					widget.Unwrap()->color = sdl3::FColor{float(state->r) / 255.f, float(state->g) / 255.f,
														  float(state->b) / 255.f, 1.f};
				onChange(*state);
			});
			field.Spawn();
		}
	}

	// ── Barre d'état ─────────────────────────────────────────────────────────

	void BuildStatusBar(ecs::Entity parent) {
		ui::WidgetBuilder bar = m_factory.Row();
		bar.Gap(14.f).Pad(math::Sides{10.f, 4.f}).GrowW().HAuto().Align(ui::CrossAlign::Center);
		bar.Bg(m_gui.Theme().panelBg).Parent(parent);
		ecs::Entity barEntity = bar.Spawn();

		m_statusFps = AddStatusLabel(barEntity, "— img/s", 90.f);
		m_statusObjects = AddStatusLabel(barEntity, "— objets", 110.f);
		m_statusBodies = AddStatusLabel(barEntity, "— corps", 100.f);
		m_statusScene = AddStatusLabel(barEntity, "—", 190.f);
		m_statusMode = AddStatusLabel(barEntity, "Édition", 80.f);
		m_statusGizmo = AddStatusLabel(barEntity, "⇔ déplacer", 200.f);

		ui::WidgetBuilder message = m_factory.Label(m_statusMessage);
		message.GrowW().HAuto().FontSize(12.f).TextColor(m_gui.Theme().muted).Parent(barEntity);
		m_statusMessageLabel = message.Spawn();
	}

	ecs::Entity AddStatusLabel(ecs::Entity parent, const char *text, float width) {
		ui::WidgetBuilder label = m_factory.Label(String(text));
		label.Size(width, 0.f).HAuto().FontSize(12.f).Parent(parent);
		return label.Spawn();
	}

	void RefreshStatusBar() {
		const SceneDesc *scene = m_runtime.ActiveScene();
		SetLabelText(m_statusFps, String::Format("%.0f img/s", m_fps));
		SetLabelText(m_statusObjects, String::Format("%d objets", int(scene ? scene->objects.size() : 0)));
		SetLabelText(m_statusBodies, String::Format("%d corps", int(m_runtime.RigidBodyCount())));
		SetLabelText(m_statusScene, scene ? scene->name : String("(aucune scène)"));
		SetLabelText(m_statusMode, String(m_runtime.IsPlaying() ? "▶ Jeu" : "■ Édition"));
		SetLabelText(m_statusGizmo, String::Format("%s %s%s", GizmoModeIcon(m_runtime.GetGizmoMode()),
												   GizmoModeName(m_runtime.GetGizmoMode()),
												   m_runtime.SnapEnabled() ? " · magnétisme" : ""));

		if (auto badge = m_registry.GetComponent<ui::UiBadge>(m_modeBadge); badge.IsSome())
			badge.Unwrap()->text = m_runtime.IsPlaying() ? String("JEU") : String("ÉDITION");
		// Le sélecteur de scène de la barre d'outils doit suivre les
		// changements venus d'AILLEURS (menu Scène, panneau Scènes, script) —
		// sinon il affiche encore la scène précédente.
		if (auto combo = m_registry.GetComponent<ui::UiComboBox>(m_sceneCombo); combo.IsSome() && scene) {
			const std::vector<String> &items = combo.Unwrap()->items;
			for (size_t i = 0; i < items.size(); ++i) {
				if (items[i] == scene->name) {
					combo.Unwrap()->selected = int(i);
					break;
				}
			}
		}

		if (auto glyph = m_registry.GetComponent<ui::UiIcon>(m_playIcon); glyph.IsSome())
			glyph.Unwrap()->glyph = ui::Glyphs::ToUtf8(m_runtime.IsPlaying() ? ui::MaterialIcons::STOP
																			: ui::MaterialIcons::PLAY_ARROW);

		// Historique de cadence pour le graphe du profileur (fenêtre glissante).
		m_fpsHistory.push_back(float(m_fps));
		if (m_fpsHistory.size() > FPS_HISTORY_SIZE)
			m_fpsHistory.erase(m_fpsHistory.begin());
	}

	// ── Actions ──────────────────────────────────────────────────────────────

	void SpawnPrimitive(ShapeKind shape) {
		ObjectDesc object;
		object.name = String(DefaultNameFor(shape));
		object.shape = shape;
		object.dimensions = DefaultDimensionsFor(shape);
		object.physics.halfExtents = object.dimensions * 0.5f;
		object.material.baseColor = sdl3::Color{150, 170, 220, 255};
		// Apparition DEVANT la caméra plutôt qu'à l'origine : c'est ce que
		// fait tout éditeur, et ça évite d'empiler les nouveaux objets dans
		// un coin invisible de la scène.
		const render3d::Camera &camera = m_runtime.ActiveCamera();
		math::FVector3 forward = (camera.target - camera.position).Normalize();
		object.transform.position = camera.position + forward * 7.f;
		object.transform.position.y = sdl3::Max(object.transform.position.y, object.dimensions.y * 0.5f);

		Option<String> assigned = m_runtime.SpawnObject(std::move(object));
		if (assigned.IsSome()) {
			(void)m_runtime.Select(assigned.Unwrap());
			m_runtime.LogSuccess(String::Format("Objet ajouté : %s", assigned.Unwrap().CStr()));
		}
	}

	[[nodiscard]] static const char *DefaultNameFor(ShapeKind shape) noexcept {
		switch (shape) {
			case ShapeKind::BOX:
				return "Cube";
			case ShapeKind::SPHERE:
				return "Sphère";
			case ShapeKind::CYLINDER:
				return "Cylindre";
			case ShapeKind::CONE:
				return "Cône";
			case ShapeKind::TORUS:
				return "Tore";
			case ShapeKind::PLANE:
				return "Plan";
			case ShapeKind::ICOSAHEDRON:
				return "Icosaèdre";
			case ShapeKind::TORUS_KNOT:
				return "Nœud de tore";
			case ShapeKind::PORTAL_QUAD:
				return "Portail";
			case ShapeKind::MODEL:
				return "Modèle";
		}
		return "Objet";
	}

	[[nodiscard]] static math::FVector3 DefaultDimensionsFor(ShapeKind shape) noexcept {
		switch (shape) {
			case ShapeKind::TORUS:
			case ShapeKind::TORUS_KNOT:
				return {2.4f, 0.7f, 2.4f};
			case ShapeKind::PLANE:
				return {4.f, 1.f, 4.f};
			case ShapeKind::PORTAL_QUAD:
				return {2.8f, 2.8f, 0.1f};
			default:
				break;
		}
		return {1.5f, 1.5f, 1.5f};
	}

	void OnNewProject() {
		m_runtime.OpenProject(MakeDemoProject());
		Rebuild();
	}

	void OnOpenProject() {
		auto loaded = m_runtime.LoadProjectFile(projectPath);
		if (loaded.IsError()) {
			m_runtime.LogError(String::Format("Ouverture impossible : %s", loaded.Error().CStr()));
			return;
		}
		Rebuild();
	}

	void OnSaveProject() {
		auto saved = m_runtime.SaveProjectFile(projectPath);
		if (saved.IsError())
			m_runtime.LogError(saved.Error());
		else
			m_runtime.LogSuccess(String::Format("Projet enregistré : %s", projectPath.CStr()));
	}

	void OnExportReport() {
		if (onExportReport)
			onExportReport();
		else
			m_runtime.LogWarning(String("Export du rapport indisponible dans ce mode"));
	}

	void OnUndo() {
		if (!m_runtime.Undo())
			SetStatus(String("Rien à annuler"));
	}

	void OnRedo() {
		if (!m_runtime.Redo())
			SetStatus(String("Rien à rétablir"));
	}

	void OnDuplicate() {
		SceneDesc *scene = m_runtime.ActiveScene();
		ObjectDesc *object = Selected();
		if (!scene || !object)
			return;
		ObjectDesc copy = *object;
		copy.transform.position.x += copy.dimensions.x + 0.5f;
		Option<String> assigned = m_runtime.SpawnObject(std::move(copy));
		if (assigned.IsSome())
			(void)m_runtime.Select(assigned.Unwrap());
	}

	void OnDeleteSelection() {
		Option<String> selected = m_runtime.SelectedName();
		if (selected.IsNone())
			return;
		if (m_runtime.RemoveObject(selected.Unwrap()))
			m_runtime.LogInfo(String::Format("Objet supprimé : %s", selected.Unwrap().CStr()));
	}

	void OnFocusSelection() {
		if (Option<String> selected = m_runtime.SelectedName(); selected.IsSome())
			(void)m_runtime.FocusOn(selected.Unwrap());
	}

	void OnRunScript() {
		auto area = m_registry.GetComponent<ui::UiInputArea>(m_scriptEditor);
		if (area.IsNone())
			return;
		String source = area.Unwrap()->text;
		if (source.Trim().IsEmpty())
			return;
		(void)FocusPanel("console", 0);
		auto result = m_runtime.RunToolScript(source);
		if (result.IsError())
			m_runtime.LogError(String::Format("Script : %s", result.Error().Format().CStr()));
		else if (!result.Value().IsNil())
			m_runtime.LogSuccess(String::Format("→ %s", result.Value().ToDisplayString().CStr()));
	}

	void OnClearConsole() {
		m_runtime.ClearLog();
		RefreshConsole();
	}

	void OnShowShortcuts() {
		m_runtime.LogInfo(String("Vue : clic droit orienter + ZQSD/WASD voler, E/Q monter/descendre · "
								 "clic milieu panoramique · molette avancer · Alt+clic gauche orbiter"));
		m_runtime.LogInfo(String("Vue (clavier seul) : flèches déplacer · Page haut/bas monter et descendre · "
								 "Maj accélérer · F cadrer la sélection"));
		m_runtime.LogInfo(String("Édition : clic gauche sélectionner ou tirer un axe · W déplacer · E tourner · "
								 "R redimensionner · X magnétisme"));
		m_runtime.LogInfo(String("Ctrl+Z annuler · Ctrl+Y rétablir · Ctrl+D dupliquer · Suppr supprimer · "
								 "Ctrl+S enregistrer · F5 mode Jeu"));
		(void)FocusPanel("console", 0);
	}

	void OnAbout() {
		m_runtime.LogInfo(String("level_editor_demo — éditeur de niveau 3D bâti sur le wrapper SDL3/C++23 "
								 "(ui::, ecs::, render3d::, physics::, data::script)"));
		(void)FocusPanel("console", 0);
	}

public:
	/// Installé par l'application : exporte le rapport depuis le menu.
	std::function<void()> onExportReport;

	/// Chemin utilisé par Ouvrir/Enregistrer.
	String projectPath = "level_editor_demo_project.json";

private:
	/// Modèles glTF proposés à l'import dans le panneau Ressources. Liste
	/// EXPLICITE plutôt qu'un parcours de dossier : `sdl3::` n'expose pas
	/// d'énumération de répertoire, et une liste courte et lisible vaut mieux
	/// qu'un navigateur de fichiers à moitié fait pour une démo.
	static constexpr const char *GLTF_ASSETS[] = {
		"assets/models/animals/Cow.gltf",   "assets/models/animals/Horse.gltf",
		"assets/models/animals/Sheep.gltf", "assets/models/animals/Pig.gltf",
		"assets/models/animals/Pug.gltf",   "assets/models/animals/Zebra.gltf",
		"assets/models/animals/Llama.gltf", "assets/models/knight/KnightCharacter.gltf",
	};

	static constexpr size_t CONSOLE_LINES = 120;
	static constexpr size_t FPS_HISTORY_SIZE = 120;
	static constexpr const char *DEFAULT_SCRIPT_SNIPPET =
		"# Console de script — Ctrl+Entrée ou « Exécuter »\n"
		"for i in range(0, 4) {\n"
		"    scene.spawn({\n"
		"        name: \"Essai \" .. i,\n"
		"        shape: \"sphere\",\n"
		"        size: [1, 1, 1],\n"
		"        pos: [i * 2 - 3, 4, 0],\n"
		"        color: [240, 160 - i * 30, 90],\n"
		"        body: \"dynamic\"\n"
		"    })\n"
		"}\n"
		"editor.log(\"scène : \" .. scene.count() .. \" objets\")\n";

	Runtime &m_runtime;
	ui::Ui &m_gui;
	ecs::ArchetypeRegistry &m_registry;
	ui::UiFactory &m_factory;

	String m_themeName = "dark";
	String m_statusMessage = "Prêt";
	double m_fps = 0.0;
	long m_tickCount = 0;
	bool m_rightMouseDown = false;
	bool m_middleMouseDown = false; ///< panoramique en cours
	bool m_orbiting = false;        ///< orbite (Alt + clic gauche) en cours
	math::FVector3 m_orbitPivot{};  ///< point figé au début de l'orbite

	bool m_outlinerDirty = false;
	bool m_inspectorDirty = false;
	bool m_consoleDirty = false;
	bool m_worldDirty = false;

	std::vector<float> m_fpsHistory;
	std::vector<ecs::Entity> m_menuPopups;

	ecs::Entity m_root{};
	ecs::Entity m_viewport{};
	ecs::Entity m_leftTabs{}, m_rightTabs{}, m_bottomTabs{};
	ecs::Entity m_outlinerList{}, m_sceneList{}, m_assetList{};
	ecs::Entity m_transformPage{}, m_materialPage{}, m_physicsPage{}, m_worldPage{};
	ecs::Entity m_console{}, m_scriptEditor{}, m_profilerPage{};
	ecs::Entity m_playButton{}, m_playIcon{}, m_modeBadge{}, m_sceneCombo{};
	ecs::Entity m_statusFps{}, m_statusObjects{}, m_statusBodies{}, m_statusScene{}, m_statusMode{};
	ecs::Entity m_statusMessageLabel{};
	ecs::Entity m_statusGizmo{};
};

} // namespace level_editor
