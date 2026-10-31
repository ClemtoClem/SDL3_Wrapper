#pragma once
/**
 * ui:: — interface utilisateur déclarative sur l'ECS.
 *
 *  - briques atomiques (components.hpp) : chaque widget est une entité (ou
 *    un petit arbre d'entités) de l'`ecs::ArchetypeRegistry` ;
 *  - builders fluents façon Flutter (builder.hpp, widgets.hpp) : on décrit
 *    l'arbre, `Mount` le matérialise ;
 *  - styles CSS (css.hpp, style.hpp) : feuilles globales ou propres à un
 *    widget (`Import`), sélecteurs de type / `#id` / `.classe` / `this`,
 *    héritage, polices et images chargées depuis les feuilles ;
 *  - évènements façon JavaScript (events.hpp) : `OnBind`, `OffBind`,
 *    `HasBind`, `GetBind`, `GetBinds`, bouillonnement, tous les
 *    périphériques de SDL3 ;
 *  - systèmes ECS (systems.hpp) : `ui_input`, `ui_animation`, `ui_style`,
 *    `ui_layout`, `ui_render` pour `ecs::Schedule`.
 *
 * `ui::Ui` installe le contexte dans le monde et offre la boucle courante :
 *
 * @code{.cpp}
 * ecs::ArchetypeRegistry world;
 * ui::Ui gui(world, window, renderer);           // thème sombre, polices par défaut
 * gui.LoadStyleSheet("assets/ui/app.css");
 *
 * ui::Widget viewport;
 * gui.Mount(ui::Column().Fill().Children({
 *     ui::Button("Quitter").Id("quit").Class("sidebar-icon")
 *         .OnBind({sdl3::MouseButton::Left, sdl3::ButtonEvent::MultiClicks, 4},
 *                 {sdl3::GamepadButton::EAST, sdl3::ButtonEvent::Clic, 0},
 *                 [&](ui::WidgetEvent) { viewport.Hide(); }),
 *     ui::Box("viewport").Grow().Ref(viewport),
 * }));
 *
 * while (running) {
 *     while (auto ev = sdl3::PollEvent())
 *         if (!gui.HandleEvent(ev.Value())) { ... }   // non consommé par l'interface
 *     gui.Update(dt);
 *     renderer.Clear();
 *     gui.Render();
 *     renderer.Present();
 * }
 * @endcode
 */
#include "builder.hpp"
#include "components.hpp"
#include "context.hpp"
#include "css.hpp"
#include "events.hpp"
#include "fonts.hpp"
#include "geometry.hpp"
#include "glyphs.hpp"
#include "nodegraph.hpp"
#include "painter.hpp"
#include "scene.hpp"
#include "chrome.hpp"
#include "plot.hpp"
#include "shader_effect.hpp"
#include "style.hpp"
#include "style_setters.hpp"
#include "systems.hpp"
#include "text.hpp"
#include "theme.hpp"
#include "viewport3d.hpp"
#include "window_frame.hpp"
#include "widget.hpp"
#include "widgets.hpp"

namespace ui {

class Ui {
public:
	/// Interface d'une fenêtre (rendu 2D par `renderer`).
	Ui(ecs::ArchetypeRegistry &world, sdl3::Window &window, sdl3::Renderer &renderer, Theme theme = Theme::Dark);
	/// Interface rendue sur un renderer sans fenêtre (surface logicielle).
	Ui(ecs::ArchetypeRegistry &world, sdl3::Renderer &renderer, sdl3::FPoint viewport, Theme theme = Theme::Dark);
	/// Interface sans rendu (tests, mode sans écran).
	explicit Ui(ecs::ArchetypeRegistry &world, sdl3::FPoint viewport = {800.f, 600.f}, Theme theme = Theme::Dark);
	Ui(const Ui &) = delete;
	Ui &operator=(const Ui &) = delete;
	~Ui();

	[[nodiscard]] ecs::ArchetypeRegistry &World() noexcept { return *m_world; }
	[[nodiscard]] Context &Ctx() noexcept { return *m_ctx; }
	[[nodiscard]] Widget Root() { return Widget(*m_world, m_ctx->root); }

	// ── Arbre ───────────────────────────────────────────────────────────────
	/// Matérialise un builder sous la racine (ou sous `parent`).
	Widget Mount(const AnyWidget &widget);
	Widget Mount(const AnyWidget &widget, Widget parent);
	[[nodiscard]] Option<Widget> Find(StringView selector);
	[[nodiscard]] std::vector<Widget> FindAll(StringView selector);

	// ── Styles ──────────────────────────────────────────────────────────────
	/// Feuille globale (et ses @import, @font-face).
	Result<bool, String> LoadStyleSheet(const String &path);
	/// Feuille globale depuis un texte CSS.
	void AddStyleSheet(StringView css, const String &baseDir = String());
	void AddStyleSheet(StyleSheetPtr sheet);
	/// Retire une feuille ajoutée par AddStyleSheet (feuille régénérée).
	void RemoveStyleSheet(const StyleSheetPtr &sheet);
	void ClearStyleSheets();
	void SetTheme(Theme theme);
	void SetTheme(const Palette &palette);
	[[nodiscard]] const Palette &CurrentPalette() const noexcept { return m_palette; }

	// ── Ressources ──────────────────────────────────────────────────────────
	[[nodiscard]] Fonts &GetFonts() noexcept { return m_ctx->fonts; }
	[[nodiscard]] Textures &GetTextures() noexcept { return m_ctx->textures; }
	Result<bool, String> LoadFont(StringView family, const String &path, int weight = 400,
								  FontStyle style = FontStyle::Normal, float size = 16.f);
	void RegisterFont(StringView family, sdl3::Font &font, int weight = 400, FontStyle style = FontStyle::Normal);
	/// DejaVu Sans (normale, grasse, oblique), DejaVu Sans Mono et les
	/// polices d'icônes d'assets/fonts — appelé par les constructeurs à
	/// renderer si SDL_ttf est initialisé. Rend le nombre de polices chargées.
	int LoadDefaultFonts(float size = 14.f);
	/// Device GPU des widgets Viewport3D / effets shader (cf. viewport3d.hpp).
	void Initialize(render3d::Canvas &canvas) { m_ctx->canvas3d = &canvas; }

	// ── Boucle ──────────────────────────────────────────────────────────────
	/// Un évènement SDL ; vrai s'il est consommé par l'interface (le marque
	/// alors consommé : sdl3::Event::IsConsumed).
	bool HandleEvent(sdl3::Event &ev);
	bool HandleEvent(const sdl3::Event &ev);
	/// Animations, minuteries (infobulles, appuis longs, sous-menus).
	void Update(float dt);
	/// Styles et mise en page à jour (sans dessiner) — tests, mesures.
	void Layout();
	/// Styles, mise en page, rendus hors écran, dessin 2D.
	void Render();
	/// Ajoute les systèmes ui au Schedule (alternative à Update/Render).
	void Install(ecs::Schedule &schedule, bool withRender = true) { ui::Install(schedule, withRender); }
	void SetViewport(sdl3::FPoint size);
	/// Échelle HiDPI (devicePixelRatio) : le renderer dessine à `scale`, la
	/// mise en page travaille en unités logiques (sortie / scale) et les
	/// coordonnées du pointeur sont converties (cf. WindowManager::Dispatch,
	/// SDL_ConvertEventToRenderCoordinates). 1 : comportement historique.
	void SetContentScale(float scale);
	[[nodiscard]] float ContentScale() const noexcept { return m_ctx->contentScale; }
	[[nodiscard]] sdl3::FPoint Viewport() const noexcept { return m_ctx->viewport; }

	// ── Saisie ──────────────────────────────────────────────────────────────
	[[nodiscard]] bool PointerOverUi() const;
	[[nodiscard]] Widget Hovered();
	[[nodiscard]] Widget Focused();
	void ClearFocus() { ui::ClearFocus(*m_world); }
	[[nodiscard]] bool HasOpenModal() const { return ui::HasOpenModal(*m_world); }
	void CloseAllPopups() { ui::CloseAllPopups(*m_world); }
	void SetKeyPassThrough(std::function<bool(const sdl3::Event &, ecs::Entity)> rule) {
		m_ctx->keyPassThrough = std::move(rule);
	}

	[[nodiscard]] const FrameTimings &Timings() const noexcept { return m_ctx->timings; }

private:
	void Setup(Theme theme);

	ecs::ArchetypeRegistry *m_world;
	std::shared_ptr<Context> m_ctx;
	Palette m_palette;
};

} // namespace ui
