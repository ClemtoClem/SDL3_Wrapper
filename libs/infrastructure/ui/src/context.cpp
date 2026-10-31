// Définitions de ui/context.hpp
#include <ui/context.hpp>

#include <ui/systems.hpp>

namespace ui {

sdl3::FPoint Context::WindowViewport() const {
	if (!window)
		return viewport;
	if (contentScale == 1.f || !renderer) {
		const sdl3::Point size = window->GetSize();
		return {float(size.x), float(size.y)};
	}
	// SDL_GetRenderOutputSize ignore l'échelle de rendu : pixels réels.
	int w = 0, h = 0;
	if (!SDL_GetRenderOutputSize(renderer->Get(), &w, &h) || w <= 0 || h <= 0) {
		const sdl3::Point size = window->GetSize();
		return {float(size.x), float(size.y)};
	}
	return {float(w) / contentScale, float(h) / contentScale};
}

Context::Context() {
	painter.SetFonts(&fonts);
	painter.SetTextures(&textures);
}

Context::~Context() {
	// Textes mis en forme et textures avant les polices et le renderer.
	painter.SetRenderer(nullptr);
	textures.Clear();
}

void Context::RegisterFonts(const StyleSheet &sheet) {
	for (const FontFace &face : sheet.FontFaces())
		fonts.AddFace(face);
}

Result<StyleSheetPtr, String> Context::LoadSheet(const String &path) {
	if (auto it = sheetCache.find(path); it != sheetCache.end())
		return Ok(it->second);
	auto sheet = StyleSheet::Load(path);
	if (sheet.IsError())
		return Err(sheet.Error());
	StyleSheetPtr ptr = sheet.Value();
	RegisterFonts(*ptr);
	sheetCache.emplace(path, ptr);
	return Ok(ptr);
}

Context *ContextOf(ecs::ArchetypeRegistry &world) {
	if (auto h = world.resources.Get<ContextHandle>(); h.IsSome())
		return h.Unwrap()->context.get();
	return nullptr;
}

const Context *ContextOf(const ecs::ArchetypeRegistry &world) {
	if (auto h = world.resources.Get<ContextHandle>(); h.IsSome())
		return h.Unwrap()->context.get();
	return nullptr;
}

void MarkStyleDirty(ecs::ArchetypeRegistry &world, ecs::Entity e) {
	if (Context *ctx = ContextOf(world)) {
		ctx->MarkStyleDirty(e);
		ctx->MarkLayoutDirty();
	}
}

void MarkLayoutDirty(ecs::ArchetypeRegistry &world) {
	if (Context *ctx = ContextOf(world))
		ctx->MarkLayoutDirty();
}

void RegisterExtension(Context &ctx, Extension ext) {
	for (Extension &e : ctx.extensions)
		if (e.name == ext.name) {
			e = std::move(ext);
			return;
		}
	ctx.extensions.push_back(std::move(ext));
}

bool DefaultKeyPassThrough(const sdl3::Event &ev) noexcept {
	if (!ev.IsKeyDown() && !ev.IsKeyUp())
		return false; // texte saisi, composition IME : toujours au champ
	const SDL_Keycode key = ev.Keycode();
	if ((key >= SDLK_F1 && key <= SDLK_F12) || (key >= SDLK_F13 && key <= SDLK_F24))
		return true;
	const SDL_Keymod mod = ev.raw.key.mod;
	// AltGr compose des caractères (@, #, {…) : ce n'est pas un raccourci.
	const bool command =
		(mod & (SDL_KMOD_CTRL | SDL_KMOD_LALT | SDL_KMOD_GUI)) != 0 && (mod & (SDL_KMOD_RALT | SDL_KMOD_MODE)) == 0;
	if (!command)
		return false;
	switch (key) {
	case SDLK_A:
	case SDLK_C:
	case SDLK_V:
	case SDLK_X:
	case SDLK_Z:
	case SDLK_Y:
	case SDLK_LEFT:
	case SDLK_RIGHT:
	case SDLK_UP:
	case SDLK_DOWN:
	case SDLK_HOME:
	case SDLK_END:
	case SDLK_BACKSPACE:
	case SDLK_DELETE:
		return false;
	default:
		return true;
	}
}

} // namespace ui
