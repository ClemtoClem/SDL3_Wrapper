// Définitions de ui/ui.hpp
#include <ui/ui.hpp>

#include <SDL3_ttf/SDL_ttf.h>

namespace ui {

Ui::Ui(ecs::ArchetypeRegistry &world, sdl3::Window &window, sdl3::Renderer &renderer, Theme theme)
	: m_world(&world), m_ctx(std::make_shared<Context>()) {
	m_ctx->window = &window;
	m_ctx->renderer = &renderer;
	const sdl3::Point size = window.GetSize();
	m_ctx->viewport = {float(size.x), float(size.y)};
	m_ctx->painter.SetRenderer(&renderer);
	m_ctx->textures.SetRenderer(&renderer);
	window.StartTextInput();
	Setup(theme);
	if (TTF_WasInit() > 0)
		(void)LoadDefaultFonts(m_palette.fontSize);
}

Ui::Ui(ecs::ArchetypeRegistry &world, sdl3::Renderer &renderer, sdl3::FPoint viewport, Theme theme)
	: m_world(&world), m_ctx(std::make_shared<Context>()) {
	m_ctx->renderer = &renderer;
	m_ctx->viewport = viewport;
	m_ctx->painter.SetRenderer(&renderer);
	m_ctx->textures.SetRenderer(&renderer);
	Setup(theme);
	if (TTF_WasInit() > 0)
		(void)LoadDefaultFonts(m_palette.fontSize);
}

Ui::Ui(ecs::ArchetypeRegistry &world, sdl3::FPoint viewport, Theme theme)
	: m_world(&world), m_ctx(std::make_shared<Context>()) {
	m_ctx->viewport = viewport;
	Setup(theme);
}

void Ui::Setup(Theme theme) {
	m_world->resources.Insert(ContextHandle{m_ctx});
	m_ctx->moduleSheets = {StyleSheet::Parse(StructureCss().View(), String(), String("<ui:structure>")),
						   StyleSheet::Parse(String().View(), String(), String("<ui:theme>"))};
	SetTheme(theme);
	RegisterPlotExtension(*m_ctx);
	RegisterNodeGraphExtension(*m_ctx);
	RegisterViewport3DExtension(*m_ctx);
	RegisterShaderEffectExtension(*m_ctx);
	m_ctx->root = Box("root").Build(*m_world).Entity();
}

Ui::~Ui() {
	if (m_world->IsAlive(m_ctx->root))
		(void)ecs::DespawnRecursive(*m_world, m_ctx->root);
	// Les rappels des widgets détruits ne doivent plus voir ce contexte.
	(void)m_world->resources.Remove<ContextHandle>();
}

Widget Ui::Mount(const AnyWidget &widget) { return widget.Build(*m_world, m_ctx->root); }

Widget Ui::Mount(const AnyWidget &widget, Widget parent) {
	return widget.Build(*m_world, parent.Valid() ? parent.Entity() : m_ctx->root);
}

Option<Widget> Ui::Find(StringView selector) { return Query(*m_world, selector); }
std::vector<Widget> Ui::FindAll(StringView selector) { return QueryAll(*m_world, selector); }

Result<bool, String> Ui::LoadStyleSheet(const String &path) {
	auto sheet = m_ctx->LoadSheet(path);
	if (sheet.IsError())
		return Err(sheet.Error());
	m_ctx->sheets.push_back(sheet.Value());
	m_ctx->MarkAllDirty();
	return Ok(true);
}

void Ui::AddStyleSheet(StringView css, const String &baseDir) {
	AddStyleSheet(StyleSheet::Parse(css, baseDir, String("<css>")));
}

void Ui::AddStyleSheet(StyleSheetPtr sheet) {
	if (!sheet)
		return;
	m_ctx->RegisterFonts(*sheet);
	m_ctx->sheets.push_back(std::move(sheet));
	m_ctx->MarkAllDirty();
}

void Ui::RemoveStyleSheet(const StyleSheetPtr &sheet) {
	if (std::erase(m_ctx->sheets, sheet) > 0)
		m_ctx->MarkAllDirty();
}

void Ui::ClearStyleSheets() {
	m_ctx->sheets.clear();
	m_ctx->MarkAllDirty();
}

void Ui::SetTheme(Theme theme) { SetTheme(Palette::Of(theme)); }

void Ui::SetTheme(const Palette &palette) {
	m_palette = palette;
	m_ctx->rootFontSize = palette.fontSize;
	m_ctx->moduleSheets.resize(2);
	m_ctx->moduleSheets[1] = StyleSheet::Parse(ThemeCss(palette).View(), String(), String("<ui:theme>"));
	m_ctx->MarkAllDirty();
}

Result<bool, String> Ui::LoadFont(StringView family, const String &path, int weight, FontStyle style, float size) {
	auto r = m_ctx->fonts.Load(family, path, weight, style, size);
	m_ctx->MarkAllDirty();
	return r;
}

void Ui::RegisterFont(StringView family, sdl3::Font &font, int weight, FontStyle style) {
	m_ctx->fonts.Register(family, font, weight, style);
	m_ctx->MarkAllDirty();
}

int Ui::LoadDefaultFonts(float size) {
	int loaded = 0;
	auto load = [&](StringView family, std::initializer_list<const char *> names, int weight, FontStyle style) {
		if (auto path = sdl3::FindFontPath(names, {"./assets/fonts/", "/usr/share/fonts/", "/usr/local/share/fonts/"});
			path.IsSome())
			if (m_ctx->fonts.Load(family, path.Unwrap(), weight, style, size).IsOk())
				++loaded;
	};
	load("DejaVu Sans", {"DejaVuSans", "FreeSans", "LiberationSans-Regular", "Arial"}, 400, FontStyle::Normal);
	load("DejaVu Sans", {"DejaVuSans-Bold", "FreeSansBold", "LiberationSans-Bold", "Arial Bold"}, 700, FontStyle::Normal);
	load("DejaVu Sans", {"DejaVuSans-Oblique", "FreeSansOblique", "LiberationSans-Italic"}, 400, FontStyle::Italic);
	load("DejaVu Sans", {"DejaVuSans-BoldOblique", "FreeSansBoldOblique", "LiberationSans-BoldItalic"}, 700,
		 FontStyle::Italic);
	load("DejaVu Sans Mono", {"DejaVuSansMono", "FreeMono", "LiberationMono-Regular"}, 400, FontStyle::Normal);
	m_ctx->fonts.SetDefaultFamily("dejavu sans");
	m_ctx->fonts.SetMonospaceFamily("DejaVu Sans Mono");
	// Polices d'icônes : répertoire courant, puis à côté de l'exécutable.
	std::vector<String> dirs = {String("assets/fonts/")};
	if (const char *base = SDL_GetBasePath()) {
		dirs.push_back(String(base) + "assets/fonts/");
		dirs.push_back(String(base) + "../../assets/fonts/");
	}
	auto loadIcons = [&](StringView family, const char *file) {
		for (const String &dir : dirs)
			if (m_ctx->fonts.Load(family, dir + file, 400, FontStyle::Normal, 22.f).IsOk()) {
				++loaded;
				return;
			}
	};
	loadIcons(MATERIALICONS_FONTNAME, MATERIALICONS_FILENAME);
	loadIcons(CUPERTINOICONS_FONTNAME, CUPERTINOICONS_FILENAME);
	m_ctx->MarkAllDirty();
	return loaded;
}

bool Ui::HandleEvent(sdl3::Event &ev) {
	if (ev.IsConsumed())
		return true;
	if (InputSystem::HandleEvent(*m_world, *m_ctx, ev))
		ev.Consume();
	return ev.IsConsumed();
}

bool Ui::HandleEvent(const sdl3::Event &ev) { return InputSystem::HandleEvent(*m_world, *m_ctx, ev); }

void Ui::Update(float dt) {
	if (!m_ctx->queued.empty()) {
		std::vector<sdl3::Event> events;
		events.swap(m_ctx->queued);
		for (const sdl3::Event &ev : events)
			(void)InputSystem::HandleEvent(*m_world, *m_ctx, ev);
	}
	AnimationSystem::Run(*m_world, *m_ctx, dt);
}

void Ui::SetViewport(sdl3::FPoint size) {
	if (size.x != m_ctx->viewport.x || size.y != m_ctx->viewport.y) {
		m_ctx->viewport = size;
		m_ctx->MarkLayoutDirty();
	}
}

void Ui::Layout() {
	if (m_ctx->window) {
		const sdl3::Point size = m_ctx->window->GetSize();
		SetViewport({float(size.x), float(size.y)});
	}
	const double rate = double(SDL_GetPerformanceFrequency());
	uint64_t mark = SDL_GetPerformanceCounter();
	auto elapsed = [&] {
		const uint64_t now = SDL_GetPerformanceCounter();
		const double ms = double(now - mark) * 1000.0 / rate;
		mark = now;
		return ms;
	};
	StyleSystem::Run(*m_world, *m_ctx);
	m_ctx->timings.styleMs = elapsed();
	(void)LayoutSystem::Run(*m_world, *m_ctx);
	// Une mise en page peut changer des états (défilement borné…) : styles
	// et géométrie convergent en une seconde passe au besoin.
	if (m_ctx->styleDirtyAll || !m_ctx->styleDirty.empty()) {
		StyleSystem::Run(*m_world, *m_ctx);
		(void)LayoutSystem::Run(*m_world, *m_ctx);
	}
	m_ctx->timings.layoutMs = elapsed();
}

void Ui::Render() {
	Layout();
	const double rate = double(SDL_GetPerformanceFrequency());
	uint64_t mark = SDL_GetPerformanceCounter();
	for (Extension &ext : m_ctx->extensions)
		if (ext.prepare)
			ext.prepare(*m_world, *m_ctx);
	const uint64_t afterPrepare = SDL_GetPerformanceCounter();
	m_ctx->timings.viewport3dMs = double(afterPrepare - mark) * 1000.0 / rate;
	mark = afterPrepare;
	RenderSystem::Run(*m_world, *m_ctx);
	m_ctx->timings.drawMs = double(SDL_GetPerformanceCounter() - mark) * 1000.0 / rate;
}

bool Ui::PointerOverUi() const {
	return m_ctx->input.overUi || m_ctx->input.drag != Interaction::Drag::None;
}

Widget Ui::Hovered() { return Widget(*m_world, m_ctx->input.hovered); }
Widget Ui::Focused() { return Widget(*m_world, FocusedWidget(*m_world)); }

} // namespace ui
