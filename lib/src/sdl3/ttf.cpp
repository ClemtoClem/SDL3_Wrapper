// Définitions de sdl3/ttf.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sdl3/ttf.hpp"

namespace sdl3 {

// ── TtfContext ───────────────────────────────────────────────────────────────

TtfContext::~TtfContext() {
	if (owns)
		TTF_Quit();
}

Result<TtfContext, Error> TtfContext::Create() {
	TtfContext ctx;
	ctx.owns = TTF_Init();
	if (!ctx)
		return Err(GetError());
	return Ok(std::move(ctx));
}

String GetSystemUserName() noexcept {
#if defined(_WIN32)
#include <windows.h>
	char buf[256] = {};
	DWORD n = sizeof(buf);
	GetUserNameA(buf, &n);
	return String(buf);
#else
	if (const char *u = std::getenv("USER"))
		return String(u);
	if (const char *u = std::getenv("LOGNAME"))
		return String(u);
	if (const char *home = std::getenv("HOME")) {
		std::string_view sv(home);
		auto pos = sv.rfind('/');
		if (pos != std::string_view::npos)
			return String(std::string(sv.substr(pos + 1)).c_str());
	}
	return String("");
#endif
}

namespace detail {

std::string FontNorm(std::string_view s) noexcept {
	std::string r;
	r.reserve(s.size());
	for (unsigned char c : s)
		if (c != '-' && c != '_' && c != ' ')
			r += char(std::tolower(c));
	return r;
}

std::string FontStem(const std::filesystem::path &p) noexcept {
	auto ext = p.extension().string();
	for (auto &c : ext)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

	if (ext == ".gz") {
		auto inner = p.stem();
		auto iext = inner.extension().string();
		for (auto &c : iext)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		if (iext == ".ttf" || iext == ".otf" || iext == ".ttc")
			return inner.stem().string();
		return {};
	}
	if (ext == ".ttf" || ext == ".otf" || ext == ".ttc")
		return p.stem().string();
	return {};
}

std::vector<std::filesystem::path> SystemFontDirs() {
	std::vector<std::filesystem::path> dirs;
	namespace fs = std::filesystem;

#if defined(_WIN32)
	if (const char *d = std::getenv("WINDIR"))
		dirs.emplace_back(fs::path(d) / "Fonts");
	else
		dirs.emplace_back("C:/Windows/Fonts");
	if (const char *d = std::getenv("LOCALAPPDATA"))
		dirs.emplace_back(fs::path(d) / "Microsoft/Windows/Fonts");

#elif defined(__APPLE__)
	if (const char *home = std::getenv("HOME"))
		dirs.emplace_back(fs::path(home) / "Library/Fonts");
	dirs.emplace_back("/Library/Fonts");
	dirs.emplace_back("/System/Library/Fonts");
	dirs.emplace_back("/Network/Library/Fonts");

#else // Linux / BSD / Unix
	if (const char *home = std::getenv("HOME")) {
		dirs.emplace_back(fs::path(home) / ".local/share/fonts");
		dirs.emplace_back(fs::path(home) / ".fonts");
	}
	dirs.emplace_back("/usr/share/fonts");
	dirs.emplace_back("/usr/local/share/fonts");
	if (const char *xdg = std::getenv("XDG_DATA_DIRS")) {
		std::string_view sv(xdg);
		while (!sv.empty()) {
			auto pos = sv.find(':');
			auto part = sv.substr(0, pos);
			if (!part.empty())
				dirs.emplace_back(fs::path(std::string(part)) / "fonts");
			sv = (pos == std::string_view::npos) ? "" : sv.substr(pos + 1);
		}
	}
#endif
	return dirs;
}

Option<String> FontSearch(std::span<const std::string> norms, std::span<const std::filesystem::path> dirs) {
	namespace fs = std::filesystem;

	std::unordered_map<std::string, std::string> index; // norstem → full_path
	std::error_code ec;
	for (const auto &dir : dirs) {
		if (!fs::exists(dir, ec))
			continue;
		for (const auto &entry :
			 fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
			if (!entry.is_regular_file(ec))
				continue;
			auto stem = FontStem(entry.path());
			if (stem.empty())
				continue;
			index.emplace(FontNorm(stem), entry.path().string());
		}
	}

	for (const auto &n : norms) {
		auto it = index.find(n);
		if (it != index.end())
			return Some(String(it->second.c_str()));
	}
	return NONE;
}

} // namespace detail

Option<String> FindFontPath(std::initializer_list<const char *> names) {
	std::vector<std::string> norms;
	for (const char *n : names)
		norms.push_back(detail::FontNorm(n));
	auto dirs = detail::SystemFontDirs();
	return detail::FontSearch(norms, dirs);
}

Option<String> FindFontPath(std::initializer_list<const char *> names, std::initializer_list<std::string> dirs) {
	std::vector<std::string> norms;
	for (const char *n : names)
		norms.push_back(detail::FontNorm(n));
	std::vector<std::filesystem::path> fsDirs(dirs.begin(), dirs.end());
	return detail::FontSearch(norms, fsDirs);
}

// ── Font ─────────────────────────────────────────────────────────────────────

Result<Font, StringView> Font::Open(const String &path, float ptSize) {
	auto *f = TTF_OpenFont(path.c_str(), ptSize);
	if (!f)
		return Err(GetError());
	return Ok(Font(f));
}

Result<Font, StringView> Font::FindLocal(std::initializer_list<const char *> names, float ptSize) {
	auto path = FindFontPath(names);
	if (!path)
		return Err(StringView("no font found among requested names"));
	return Open(path.Value(), ptSize);
}

Result<Font, StringView> Font::FindLocal(std::initializer_list<const char *> names, std::initializer_list<std::string> dirs, float ptSize) {
	auto path = FindFontPath(names, dirs);
	if (!path)
		return Err(StringView("no font found among requested names"));
	return Open(path.Value(), ptSize);
}

void Font::SetStyle(FontStyle s) {
	if (m_handle)
		TTF_SetFontStyle(m_handle, int(s));
}

void Font::SetOutline(int px) {
	if (m_handle)
		TTF_SetFontOutline(m_handle, px);
}

void Font::SetSize(float pt) {
	if (m_handle)
		TTF_SetFontSize(m_handle, pt);
}

Result<Font, StringView> Font::Copy() const {
	if (!m_handle)
		return Err(StringView("Font::Copy : police invalide"));
	TTF_Font *copy = TTF_CopyFont(m_handle);
	if (!copy)
		return Err(GetError());
	return Ok(Font(copy));
}

void Font::SetHinting(TTF_HintingFlags h) {
	if (m_handle)
		TTF_SetFontHinting(m_handle, h);
}

FontStyle Font::Style() const {
	return m_handle ? FontStyle(TTF_GetFontStyle(m_handle)) : FontStyle::NORMAL;
}

Option<Point> Font::Measure(const String &text) const {
	if (!m_handle)
		return NONE;
	int w = 0, h = 0;
	if (!TTF_GetStringSize(m_handle, text.c_str(), text.size(), &w, &h))
		return NONE;
	return Some(Point{w, h});
}

Result<Surface, StringView> Font::RenderSolid(const String &text, Color fg) const {
	if (!m_handle)
		return Err(StringView("null font"));
	auto *s = TTF_RenderText_Solid(m_handle, text.c_str(), text.size(), SDL_Color(fg));
	if (!s)
		return Err(GetError());
	return Ok(Surface(s));
}

Result<Surface, StringView> Font::RenderSolid(const String &text, FColor fg) const {
	return RenderSolid(text, fg.ToByte());
}

Result<Surface, StringView> Font::RenderShaded(const String &text, Color fg, Color bg) const {
	if (!m_handle)
		return Err(StringView("null font"));
	auto *s = TTF_RenderText_Shaded(m_handle, text.c_str(), text.size(), SDL_Color(fg), SDL_Color(bg));
	if (!s)
		return Err(GetError());
	return Ok(Surface(s));
}

Result<Surface, StringView> Font::RenderShaded(const String &text, FColor fg, FColor bg) const {
	return RenderShaded(text, fg.ToByte(), bg.ToByte());
}

Result<Surface, StringView> Font::RenderBlended(const String &text, Color fg) const {
	if (!m_handle)
		return Err(StringView("null font"));
	auto *s = TTF_RenderText_Blended(m_handle, text.c_str(), text.size(), SDL_Color(fg));
	if (!s)
		return Err(GetError());
	return Ok(Surface(s));
}

Result<Surface, StringView> Font::RenderBlended(const String &text, FColor fg) const {
	return RenderBlended(text, fg.ToByte());
}

Result<Surface, StringView> Font::RenderLcd(const String &text, Color fg, Color bg) const {
	if (!m_handle)
		return Err(StringView("null font"));
	auto *s = TTF_RenderText_LCD(m_handle, text.c_str(), text.size(), SDL_Color(fg), SDL_Color(bg));
	if (!s)
		return Err(GetError());
	return Ok(Surface(s));
}

Result<Surface, StringView> Font::RenderLcd(const String &text, FColor fg, FColor bg) const {
	return RenderLcd(text, fg.ToByte(), bg.ToByte());
}

Result<Texture, StringView> Font::RenderTexture(Renderer &ren, const String &text, Color fg) const {
	if (!m_handle)
		return Err(StringView("null font"));
	auto *s = TTF_RenderText_Blended(m_handle, text.c_str(), text.size(), SDL_Color(fg));
	if (!s)
		return Err(GetError());
	auto *t = SDL_CreateTextureFromSurface(ren.Get(), s);
	SDL_DestroySurface(s);
	if (!t)
		return Err(GetError());
	return Ok(Texture(t));
}

Result<Texture, StringView> Font::RenderTexture(Renderer &ren, const String &text, FColor fg) const {
	return RenderTexture(ren, text, fg.ToByte());
}

// ── TextEngine ───────────────────────────────────────────────────────────────

Result<TextEngine, Error> TextEngine::Create(Renderer &ren) {
	auto *e = TTF_CreateRendererTextEngine(ren.Get());
	if (!e)
		return Err(GetError());
	return Ok(TextEngine(e));
}

// ── Text ─────────────────────────────────────────────────────────────────────

Result<Text, Error> Text::Create(TextEngine &engine, Font &font, const String &str) {
	auto *t = TTF_CreateText(engine.Get(), font.Get(), str.c_str(), str.size());
	if (!t)
		return Err(GetError());
	return Ok(Text(t));
}

Option<Point> Text::GetSize() const {
	if (!m_handle)
		return NONE;
	int w = 0, h = 0;
	if (!TTF_GetTextSize(m_handle, &w, &h))
		return NONE;
	return Some(Point{w, h});
}

} // namespace sdl3
