#pragma once
#include <SDL3_ttf/SDL_ttf.h>

#include <cctype>
#include <filesystem>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../core/core.hpp"
#include "render.hpp"

namespace sdl3 {

// ============================================================================
// TtfContext — RAII TTF_Init / TTF_Quit
// ============================================================================

class TtfContext {
	bool owns = false;

public:
	TtfContext() = default;
	~TtfContext() {
		if (owns)
			TTF_Quit();
	}

	TtfContext(const TtfContext &) = delete;
	TtfContext &operator=(const TtfContext &) = delete;
	TtfContext(TtfContext &&o) noexcept : owns(o.owns) { o.owns = false; }
	TtfContext &operator=(TtfContext &&o) noexcept {
		if (this != &o) {
			if (owns)
				TTF_Quit();
			owns = o.owns;
			o.owns = false;
		}
		return *this;
	}

	[[nodiscard]] explicit operator bool() const noexcept { return owns; }

	[[nodiscard]] static Result<TtfContext, Error> Create() {
		TtfContext ctx;
		ctx.owns = TTF_Init();
		if (!ctx)
			return Err(GetError());
		return Ok(std::move(ctx));
	}
};

// ============================================================================
// FontStyle flags
// ============================================================================

enum class FontStyle : int {
	NORMAL = TTF_STYLE_NORMAL,
	BOLD = TTF_STYLE_BOLD,
	ITALIC = TTF_STYLE_ITALIC,
	UNDERLINE = TTF_STYLE_UNDERLINE,
	STRIKETHROUGH = TTF_STYLE_STRIKETHROUGH,
};

inline FontStyle operator|(FontStyle a, FontStyle b) { return static_cast<FontStyle>(int(a) | int(b)); }

// ============================================================================
// Font search — cross-platform
// ============================================================================

// Returns the current system user's login name (empty string if unavailable).
[[nodiscard]] inline String GetSystemUserName() noexcept {
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

// Lowercase + strip separators (-/_/ ) for fuzzy name matching.
// "DejaVuSans-Bold" and "dejavusansbold" both normalize to "dejavusansbold".
// "Courier New" and "CourierNew" both normalize to "couriernew".
inline std::string FontNorm(std::string_view s) noexcept {
	std::string r;
	r.reserve(s.size());
	for (unsigned char c : s)
		if (c != '-' && c != '_' && c != ' ')
			r += char(std::tolower(c));
	return r;
}

// Extract font stem (filename without font extension). Handles .gz wrappers:
//   "DejaVuSans-Bold.ttf"  → "DejaVuSans-Bold"
//   "NotoSerif.ttf.gz"     → "NotoSerif"
// Returns "" for non-font files.
inline std::string FontStem(const std::filesystem::path &p) noexcept {
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

// Default platform font directories, searched in order.
inline std::vector<std::filesystem::path> SystemFontDirs() {
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

// Core search: scan dirs in order, return path of first file whose
// normalized stem matches any of the normalized names (in names priority order).
[[nodiscard]] inline Option<String> FontSearch(std::span<const std::string> norms,
											   std::span<const std::filesystem::path> dirs) {
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

// Search platform font directories for the first font matching one of the
// given names (tried in priority order). Matching is case-insensitive and
// ignores separators (-/_/ ) so "CourierNew" also finds "Courier New.ttf".
//
//   auto path = sdl3::findFontPath({"DejaVuSans-Bold", "Arial", "FreeSans"});
[[nodiscard]] inline Option<String> FindFontPath(std::initializer_list<const char *> names) {
	std::vector<std::string> norms;
	for (const char *n : names)
		norms.push_back(detail::FontNorm(n));
	auto dirs = detail::SystemFontDirs();
	return detail::FontSearch(norms, dirs);
}

// Same as above, but searches the given directories in priority order instead
// of the platform defaults. Each string may be a std::string or a literal.
//
//   auto path = sdl3::findFontPath(
//       {"DejaVuSans-Bold", "Arial"},
//       {"./assets/fonts/",
//        std::format("/home/{}/.local/share/fonts", sdl3::getSystemUserName()),
//        "/usr/share/fonts/"});
[[nodiscard]] inline Option<String> FindFontPath(std::initializer_list<const char *> names,
												 std::initializer_list<std::string> dirs) {
	std::vector<std::string> norms;
	for (const char *n : names)
		norms.push_back(detail::FontNorm(n));
	std::vector<std::filesystem::path> fsDirs(dirs.begin(), dirs.end());
	return detail::FontSearch(norms, fsDirs);
}

// ============================================================================
// Font — RAII TTF_Font
// ============================================================================

class Font : public Wrapper<TTF_Font, TTF_CloseFont> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<Font, StringView> Open(const String &path, float ptSize) {
		auto *f = TTF_OpenFont(path.c_str(), ptSize);
		if (!f)
			return Err(GetError());
		return Ok(Font(f));
	}

	// Search platform font directories for the first matching font name.
	// Names are tried in priority order; matching ignores case and separators.
	//
	//   auto font = sdl3::Font::FindLocal({"DejaVuSans-Bold", "Arial", "FreeSans"}, 14.f);
	[[nodiscard]] static Result<Font, StringView> FindLocal(std::initializer_list<const char *> names,
															float ptSize = 14.f) {
		auto path = FindFontPath(names);
		if (!path)
			return Err(StringView("no font found among requested names"));
		return Open(path.Value(), ptSize);
	}

	// Same, but searches the given directories in priority order.
	// Pass std::string or string literals freely (const char* converts implicitly).
	//
	//   auto font = sdl3::Font::FindLocal(
	//       {"DejaVuSans-Bold", "Arial"},
	//       {"./assets/fonts/",
	//        std::format("/home/{}/.local/share/fonts", sdl3::getSystemUserName()),
	//        "/usr/share/fonts/"},
	//       14.f);
	[[nodiscard]] static Result<Font, StringView>
	FindLocal(std::initializer_list<const char *> names, std::initializer_list<std::string> dirs, float ptSize = 14.f) {
		auto path = FindFontPath(names, dirs);
		if (!path)
			return Err(StringView("no font found among requested names"));
		return Open(path.Value(), ptSize);
	}

	// ── Metrics & style ──────────────────────────────────────────────────────

	void SetStyle(FontStyle s) {
		if (m_handle)
			TTF_SetFontStyle(m_handle, int(s));
	}
	void SetOutline(int px) {
		if (m_handle)
			TTF_SetFontOutline(m_handle, px);
	}
	void SetSize(float pt) {
		if (m_handle)
			TTF_SetFontSize(m_handle, pt);
	}
	/// Taille courante, en points.
	[[nodiscard]] float PointSize() const { return m_handle ? TTF_GetFontSize(m_handle) : 0.f; }
	/// Copie INDÉPENDANTE de la police (même fichier, même style) : sa taille
	/// peut changer sans toucher l'originale — ni les textes déjà mis en forme
	/// avec elle, que `SetSize` sur la police partagée re-mettrait en page.
	[[nodiscard]] Result<Font, StringView> Copy() const {
		if (!m_handle)
			return Err(StringView("Font::Copy : police invalide"));
		TTF_Font *copy = TTF_CopyFont(m_handle);
		if (!copy)
			return Err(GetError());
		return Ok(Font(copy));
	}
	void SetHinting(TTF_HintingFlags h) {
		if (m_handle)
			TTF_SetFontHinting(m_handle, h);
	}

	[[nodiscard]] FontStyle Style() const {
		return m_handle ? FontStyle(TTF_GetFontStyle(m_handle)) : FontStyle::NORMAL;
	}
	[[nodiscard]] int Ascent() const { return m_handle ? TTF_GetFontAscent(m_handle) : 0; }
	[[nodiscard]] int Descent() const { return m_handle ? TTF_GetFontDescent(m_handle) : 0; }
	[[nodiscard]] int Height() const { return m_handle ? TTF_GetFontHeight(m_handle) : 0; }
	[[nodiscard]] int LineSkip() const { return m_handle ? TTF_GetFontLineSkip(m_handle) : 0; }
	[[nodiscard]] bool IsFixedWidth() const { return m_handle && TTF_FontIsFixedWidth(m_handle); }

	/// Pixel dimensions of the rendered text.
	[[nodiscard]] Option<Point> Measure(const String &text) const {
		if (!m_handle)
			return NONE;
		int w = 0, h = 0;
		if (!TTF_GetStringSize(m_handle, text.c_str(), text.size(), &w, &h))
			return NONE;
		return Some(Point{w, h});
	}

	// ── Surface rendering ────────────────────────────────────────────────────

	[[nodiscard]] Result<Surface, StringView> RenderSolid(const String &text, Color fg) const {
		if (!m_handle)
			return Err(StringView("null font"));
		auto *s = TTF_RenderText_Solid(m_handle, text.c_str(), text.size(), SDL_Color(fg));
		if (!s)
			return Err(GetError());
		return Ok(Surface(s));
	}
	
	[[nodiscard]] Result<Surface, StringView> RenderSolid(const String &text, FColor fg) const {
		return RenderSolid(text, fg.ToByte());
	}

	[[nodiscard]] Result<Surface, StringView> RenderShaded(const String &text, Color fg, Color bg) const {
		if (!m_handle)
			return Err(StringView("null font"));
		auto *s = TTF_RenderText_Shaded(m_handle, text.c_str(), text.size(), SDL_Color(fg), SDL_Color(bg));
		if (!s)
			return Err(GetError());
		return Ok(Surface(s));
	}

	[[nodiscard]] Result<Surface, StringView> RenderShaded(const String &text, FColor fg, FColor bg) const {
		return RenderShaded(text, fg.ToByte(), bg.ToByte());
	}

	[[nodiscard]] Result<Surface, StringView> RenderBlended(const String &text, Color fg) const {
		if (!m_handle)
			return Err(StringView("null font"));
		auto *s = TTF_RenderText_Blended(m_handle, text.c_str(), text.size(), SDL_Color(fg));
		if (!s)
			return Err(GetError());
		return Ok(Surface(s));
	}

	[[nodiscard]] Result<Surface, StringView> RenderBlended(const String &text, FColor fg) const {
		return RenderBlended(text, fg.ToByte());
	}

	[[nodiscard]] Result<Surface, StringView> RenderLcd(const String &text, Color fg, Color bg) const {
		if (!m_handle)
			return Err(StringView("null font"));
		auto *s = TTF_RenderText_LCD(m_handle, text.c_str(), text.size(), SDL_Color(fg), SDL_Color(bg));
		if (!s)
			return Err(GetError());
		return Ok(Surface(s));
	}

	[[nodiscard]] Result<Surface, StringView> RenderLcd(const String &text, FColor fg, FColor bg) const {
		return RenderLcd(text, fg.ToByte(), bg.ToByte());
	}

	/// Render text directly to a Texture (blended quality, single call).
	[[nodiscard]] Result<Texture, StringView> RenderTexture(Renderer &ren, const String &text, Color fg) const {
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

	[[nodiscard]] Result<Texture, StringView> RenderTexture(Renderer &ren, const String &text, FColor fg) const {
		return RenderTexture(ren, text, fg.ToByte());
	}
};

// ============================================================================
// TextEngine — GPU-accelerated text engine (SDL3_ttf new API)
// ============================================================================

class TextEngine : public Wrapper<TTF_TextEngine, TTF_DestroyRendererTextEngine> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<TextEngine, Error> Create(Renderer &ren) {
		auto *e = TTF_CreateRendererTextEngine(ren.Get());
		if (!e)
			return Err(GetError());
		return Ok(TextEngine(e));
	}
};

// ============================================================================
// Text — a rendered TTF_Text object (backed by TextEngine)
// ============================================================================

class Text : public Wrapper<TTF_Text, TTF_DestroyText> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<Text, Error> Create(TextEngine &engine, Font &font, const String &str) {
		auto *t = TTF_CreateText(engine.Get(), font.Get(), str.c_str(), str.size());
		if (!t)
			return Err(GetError());
		return Ok(Text(t));
	}

	bool SetString(const String &str) { return m_handle && TTF_SetTextString(m_handle, str.c_str(), str.size()); }

	bool SetColor(Color c) { return m_handle && TTF_SetTextColor(m_handle, c.r, c.g, c.b, c.a); }

	bool SetColor(FColor c) { return m_handle && TTF_SetTextColorFloat(m_handle, c.r, c.g, c.b, c.a); }

	/// Enveloppe le texte à `width` pixels (0 = désactive l'enveloppement,
	/// comportement par défaut) — les retours à la ligne explicites ('\n')
	/// restent respectés comme des coupures de paragraphe. Nécessaire pour
	/// un bloc de texte multi-paragraphe de largeur fixe et hauteur adaptée
	/// au contenu : appeler puis lire size() donne la hauteur réellement
	/// occupée après enveloppement, pour dimensionner un widget en
	/// conséquence (cf. UiCanvas — aucun widget ui:: natif ne fait encore
	/// d'enveloppement de PARAGRAPHE, seulement UiInput au niveau mot-par-mot
	/// interne à systems.hpp, indépendant de ce mécanisme SDL_ttf).
	bool SetWrapWidth(int width) { return m_handle && TTF_SetTextWrapWidth(m_handle, width); }

	bool Draw(float x, float y) { return m_handle && TTF_DrawRendererText(m_handle, x, y); }
	bool Draw(FPoint p) { return Draw(p.x, p.y); }

	[[nodiscard]] Option<Point> GetSize() const {
		if (!m_handle)
			return NONE;
		int w = 0, h = 0;
		if (!TTF_GetTextSize(m_handle, &w, &h))
			return NONE;
		return Some(Point{w, h});
	}
};

} // namespace sdl3
