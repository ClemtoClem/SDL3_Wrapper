#pragma once
/**
 * ui:: — interface utilisateur retenue (retained-mode), pilotée par l'ECS.
 *
 * Chaque widget est une entité ecs::ecs::Entity. Pour un usage direct des sous-
 * systèmes, le pipeline par frame est :
 *
 *   layout.RunIfNeeded(world, w, h);        // ne recalcule que si dirty
 *   input.HandleEvent(world, ev, layout);   // par évènement SDL (poll loop)
 *   input.Tick(world, layout, dt);          // une fois par frame (anims)
 *   render.Run(world, renderer, &input.Tooltip);
 *
 * `ui::Ui` regroupe ces sous-systèmes pour l'usage courant (voir plus bas).
 *
 * Construction déclarative via UiFactory/WidgetBuilder, navigation entre
 * écrans via SceneManager (scènes retenues, état conservé au masquage).
 */
#include "chrome.hpp"
#include "components.hpp"
#include "factory.hpp"
#include "glyphs.hpp"
#include "interaction.hpp"
#include "nodegraph.hpp"
#include "plot.hpp"
#include "render_backend.hpp"
#include "scene.hpp"
#include "shader_effect.hpp"
#include "styles.hpp"
#include "systems.hpp"
#include "viewport3d.hpp"

// M20 ne faisait que déclarer render3d::Canvas en avant (`Ui::Initialize
// (render3d::Canvas&)` existait déjà mais n'était qu'un stub) pour éviter de
// tirer tout render3d/canvas.hpp dans chaque consommateur de ui:: avant qu'un
// backend GPU réel n'en ait besoin. M22 (widget Viewport3D) EST ce besoin :
// viewport3d.hpp (inclus ci-dessus) tire la définition complète de
// render3d::Canvas — c'est le point d'entrée délibéré où ui:: cesse d'être
// indépendant de render3d:: (cf. rapport de tâche M22).

namespace ui {

// ============================================================================
// UiFrameTimings — coût de chaque étape du rendu, pour le profilage
// ============================================================================

/// Temps passé dans chaque étape de `Ui::Render()`, en millisecondes, pour la
/// DERNIÈRE image rendue.
///
/// Existe parce qu'un profileur applicatif ne peut pas mesurer ces étapes
/// depuis l'extérieur : `Render()` les enchaîne en interne et
/// `Viewport3DSystem`/`ShaderEffectSystem` ne sont pas exposés. Sans ça, la
/// seule chose mesurable est « le rendu prend 46 ms », ce qui ne dit pas
/// lequel des cinq étages coûte. La mesure utilise le compteur haute
/// résolution de SDL et coûte cinq lectures d'horloge par image — négligeable
/// devant ce qu'elle mesure.
struct UiFrameTimings {
	double styleMs = 0.0;         ///< résolution de la cascade de styles
	double layoutMs = 0.0;        ///< calcul de la mise en page
	double viewport3dMs = 0.0;    ///< rendu des widgets Viewport3D (scène 3D + transfert GPU<->CPU)
	double shaderEffectMs = 0.0;  ///< post-traitement des widgets à effet
	double drawMs = 0.0;          ///< dessin 2D de tous les widgets

	[[nodiscard]] double TotalMs() const noexcept;
};


// ============================================================================
// Ui — façade regroupant tout le pipeline pour l'usage courant
// ============================================================================

/// Façade qui regroupe LayoutSystem/InputSystem/RenderSystem/UiFactory pour une appli
/// qui n'a pas besoin d'accéder aux sous-systèmes séparément. Ne possède PAS
/// l'ecs::ArchetypeRegistry (passée par référence : l'appelant peut y ajouter ses
/// propres composants applicatifs) ainsi que la Window et le Renderer SDL.
///
/// @code{.cpp}
/// ui::ecs::ArchetypeRegistry ar;
/// ui::Ui ui(ar, window, ren);           // démarre aussi la saisie de texte
/// ui.SetTextEngine(eng, font);
/// ui.Layout().measureText = ...;
///
/// auto card = ui.Factory().Panel().Children(...);
/// card.Spawn();
///
/// while (running) {
///     while (auto ev = sdl3::PollEvent()) {
///         auto& e = ev.Value();
///         if (e.IsQuit()) { running = false; break; }
///         ui.HandleEvent(e);
///     }
///     ui.Tick(dt);
///
///     ren.SetDrawColor(...); ren.Clear();
///     ui.Render();           // relance le layout si besoin, puis dessine
///     ren.Present();
/// }
/// @endcode
class Ui {
public:
	Ui(ecs::ArchetypeRegistry &world, sdl3::Window &window, sdl3::Renderer &renderer, UiTheme theme = UiTheme::Dark());

	/// (Ré)établit le backend de rendu 2D sur un `sdl3::Renderer` classique —
	/// appelé implicitement par le constructeur ci-dessus, mais aussi
	/// utilisable seul pour re-router le rendu après construction (cf.
	/// `ui.Initialize(renderer)`, M20 du plan d'expansion moteur :
	/// abstraction du backend de rendu ui:: derrière `IUiRenderBackend`).
	void Initialize(sdl3::Renderer &renderer);

	/// Enregistre le `render3d::Canvas` (device GPU) que les widgets
	/// `Viewport3D` de cette UI utiliseront pour se rendre (M22, cf.
	/// viewport3d.hpp) — NE change PAS le backend 2D actif : celui-ci reste
	/// `sdl3::Renderer`/`SdlRendererBackend` (cf. `Initialize(sdl3::Renderer&)`
	/// ci-dessus), les deux coexistent (`sdl3::Renderer` et `render3d::Canvas`/
	/// `sdl3::GpuDevice` peuvent bien être créés sur la MÊME fenêtre, confirmé
	/// empiriquement lors de ce jalon — cf. rapport de tâche M22 et
	/// examples/ui_viewport3d_demo.cpp). Sans cet appel, `canvasForViewports`
	/// reste nullptr et `Render()` saute silencieusement Viewport3DSystem::
	/// Update (no-op complet pour toute appli qui n'utilise pas Viewport3D —
	/// aucune régression pour le reste de ui::).
	void Initialize(render3d::Canvas &canvas) { canvasForViewports = &canvas; }

	[[nodiscard]] ecs::ArchetypeRegistry &World() noexcept { return *world; }
	[[nodiscard]] LayoutSystem &Layout() noexcept { return layout; }
	[[nodiscard]] InputSystem &Input() noexcept { return input; }
	[[nodiscard]] ui::RenderSystem &RenderSystem() noexcept { return render; }
	[[nodiscard]] ui::StyleSystem &StyleSystem() noexcept { return style; }
	[[nodiscard]] UiFactory &Factory() noexcept { return factory; }
	[[nodiscard]] UiTheme &Theme() noexcept { return factory.theme; }
	/// Change le thème à chaud (cf. `UiFactory::SetTheme`) — la nouvelle
	/// palette s'applique aussi aux widgets déjà à l'écran.
	/// Pose le thème : les widgets créés ENSUITE par la fabrique en prennent
	/// les couleurs, et les barres de défilement automatiques (dessinées par
	/// RenderSystem, sans widget ni style propre) suivent aussitôt.
	void SetTheme(UiTheme theme);
	/// Classes CSS-like partagées (raccourci pour `factory().sheet`).
	[[nodiscard]] UiStyleSheet &Sheet() noexcept { return factory.sheet; }

	/// Enregistre (ou redéfinit) une classe de style nommée — ex:
	/// `gui.CreateStyleClass("aero", ui::UiStyle::glassButton());`. Marque
	/// dirty tous les widgets qui l'utilisent déjà (redéfinition à chaud).
	void CreateStyleClass(String name, UiStyle style);

	/// Pose UNE propriété de style par chemin `"cible.propriete"` (ex:
	/// `gui.setStyleProperty("menu_1.background-color", sdl3::FColor::UI_ACCENT_BLUE_PRIMARY());`).
	/// `cible` est cherchée d'abord comme nom de widget (UiName) — édite
	/// alors son style inline — sinon comme classe de UiStyleSheet, créée à
	/// la volée si absente. `propriete` est un nom CSS-like résolu par
	/// styles.hpp (cf. `setStyleProp`) — no-op silencieux si inconnu pour ce
	/// type de valeur (`Color`/`float`/`TextAlign`/`Sides`).
	template <typename T> void SetStyleProperty(const String &path, T value) {
		size_t dot = path.Find('.');
		if (dot == String::npos)
			return;
		String target = path.Substr(0, dot);
		String propName = path.Substr(dot + 1);

		if (auto e = FindByName(*world, target); e.IsSome()) {
			UiStyle s;
			if (auto existing = world->GetComponent<UiStyle>(e.Unwrap()); existing.IsSome())
				s = *existing.Unwrap();
			if (setStyleProp(s, propName, value))
				ui::SetInlineStyle(*world, e.Unwrap(), std::move(s));
			return;
		}
		UiStyle s;
		if (auto existing = factory.sheet.Lookup(target); existing.IsSome())
			s = *existing.Unwrap();
		if (setStyleProp(s, propName, value)) {
			factory.sheet.Define(target, s);
			factory.sheet.DirtyClassUsers(*world, target);
		}
	}

	/// Nécessaire pour que le moindre texte soit dessiné.
	void SetTextEngine(sdl3::TextEngine &engine, sdl3::Font &font) { render.SetTextEngine(&engine, &font); }

	/// Ouvre un popup ancré (non modal, cf. UiFactory::Popup()) — `trigger`
	/// sert à la fermeture au clic extérieur.
	void OpenPopup(ecs::Entity popupRoot, ecs::Entity trigger) { ui::OpenPopup(*world, layout, popupRoot, trigger); }
	/// Ouvre un popup (menu contextuel, cf. UiFactory::ContextMenu) avec son
	/// coin haut-gauche au point écran donné — typiquement la position reçue
	/// par `OnContextMenu`. Le popup reste dans la fenêtre (cf.
	/// LayoutSystem::Run).
	void OpenPopupAt(ecs::Entity popupRoot, sdl3::FPoint at) { ui::OpenPopupAt(*world, layout, popupRoot, at); }
	/// Ferme un popup ouvert via `openPopup`.
	void ClosePopup(ecs::Entity popupRoot) { ui::ClosePopup(*world, layout, popupRoot); }
	/// Ouvre une boîte de dialogue modale (cf. UiFactory::Modal()) — bloque
	/// le reste de l'UI et s'ajoute à la pile modale tant qu'ouverte.
	void OpenModal(ecs::Entity modalRoot) { input.OpenModal(*world, layout, modalRoot); }
	/// Ferme la modale du sommet de la pile si elle correspond à `modalRoot`.
	void CloseModal(ecs::Entity modalRoot) { input.CloseModal(*world, layout, modalRoot); }
	[[nodiscard]] bool HasOpenModal() const noexcept { return input.HasOpenModal(); }

	/// Enregistre une police d'icônes (cf. ui::Glyphs::FontFamily<E>() /
	/// UiFactory::Icon<E>) sous son nom de famille, pour que les UiIcon s'y
	/// résolvent au rendu.
	void RegisterFont(StringView family, sdl3::Font &font) { render.RegisterFont(family, font); }
	/// Police à chasse fixe des zones de texte en mode éditeur de code (cf.
	/// UiInputArea::monospace). Doit vivre aussi longtemps que l'interface.
	void RegisterMonospaceFont(sdl3::Font &font) { render.RegisterMonospaceFont(font); }

	/// Un évènement SDL discret (à appeler depuis la boucle de poll), passé
	/// d'abord à l'interface : s'il la concerne seule (frappe dans le champ
	/// qui a le focus, Échap qui ferme une modale…), il est marqué consommé
	/// (sdl3::Event::IsConsumed) et le reste de l'application doit l'ignorer.
	/// Rend vrai s'il est consommé.
	bool HandleEvent(sdl3::Event &ev) { return input.HandleEvent(*world, ev, layout); }
	/// Variante pour un évènement non modifiable : rend seulement l'avis.
	bool HandleEvent(const sdl3::Event &ev) { return input.HandleEvent(*world, ev, layout); }

	/// Widget qui a le focus clavier (entité invalide sinon).
	[[nodiscard]] ecs::Entity KeyboardFocus() const { return input.KeyboardFocus(*world); }
	/// Retire le focus clavier.
	void ClearKeyboardFocus() { input.ClearKeyboardFocus(*world); }
	/// Règle de transmission des touches quand un widget a le focus : vrai
	/// = l'évènement continue vers l'application (cf.
	/// InputSystem::DefaultKeyPassThrough, utilisée si `rule` est vide).
	void SetKeyPassThrough(std::function<bool(const sdl3::Event &, ecs::Entity)> rule);

	/// Mises à jour continues indépendantes des évènements (animations,
	/// infobulle) — à appeler une fois par frame.
	void Tick(float dt) { input.Tick(*world, layout, dt); }
	/// Alias de `Tick()` (nommage `ui.Update(dt)`, cf. le plan d'expansion
	/// moteur, M20) — ajouté en plus de `Tick()`, pas à sa place.
	void Update(float dt) { Tick(dt); }

	/// Résout la cascade de styles (classes + style inline + héritage) si des
	/// entités sont marquées dirty (UiStyleDirty — no-op sinon, cf. styles.hpp),
	/// relance le layout si besoin, rend les widgets Viewport3D (M22) puis les
	/// effets shader de widget (M23) puis dessine — une fois par frame, juste
	/// avant renderer.Present(). L'ordre (style → layout → viewport3d →
	/// shaderEffect → render 2D) est celui documenté par StyleSystem::Resolve
	/// pour les deux premières étapes (la cascade doit être à jour avant que
	/// le layout ne résolve les unités Em/Rem basées sur le fontSize effectif) ;
	/// viewport3d::Update puis shaderEffect::Update viennent ensuite, APRÈS
	/// layout (ont besoin de UiComputed.screen déjà résolu) mais AVANT
	/// render.Run — shaderEffect::Update APRÈS viewport3d spécifiquement pour
	/// qu'un widget à effet contenant un Viewport3D capture (étape (a) de
	/// ShaderEffectSystem, cf. shader_effect.hpp) le Viewport3D déjà à jour ce
	/// frame, pas celui du frame précédent. Les deux partagent le même garde
	/// `canvasForViewports` (aucun render3d::Canvas enregistré => les deux
	/// restent no-op, aucune régression pour une appli ui:: qui n'utilise ni
	/// Viewport3D ni UiShaderEffect).
	void Render();

	/// Coût de chaque étape de la dernière image (cf. UiFrameTimings).
	[[nodiscard]] const UiFrameTimings &LastFrameTimings() const noexcept { return timings; }
	/// Alias de `Render()` (nommage `ui.Draw()`, cf. le plan d'expansion
	/// moteur, M20) — ajouté en plus de `Render()`, pas à sa place.
	void Draw() { Render(); }

private:
	ecs::ArchetypeRegistry *world;
	sdl3::Window *window;
	Option<SdlRendererBackend> sdlBackend = NONE;
	IUiRenderBackend *backend = nullptr;
	render3d::Canvas *canvasForViewports = nullptr; ///< posé par Initialize(render3d::Canvas&), NONE par défaut
	LayoutSystem layout;
	InputSystem input;
	ui::RenderSystem render;
	ui::StyleSystem style;
	ui::Viewport3DSystem viewport3d; ///< M22 — no-op tant que canvasForViewports est nullptr, cf. Render()
	ui::ShaderEffectSystem shaderEffect; ///< M23 — idem, no-op tant que canvasForViewports est nullptr
	UiFactory factory;
	UiFrameTimings timings;
};

} // namespace ui
