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
	~TtfContext();

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

	[[nodiscard]] static Result<TtfContext, Error> Create();
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
[[nodiscard]] String GetSystemUserName() noexcept;

namespace detail {

// Lowercase + strip separators (-/_/ ) for fuzzy name matching.
// "DejaVuSans-Bold" and "dejavusansbold" both normalize to "dejavusansbold".
// "Courier New" and "CourierNew" both normalize to "couriernew".
std::string FontNorm(std::string_view s) noexcept;

// Extract font stem (filename without font extension). Handles .gz wrappers:
//   "DejaVuSans-Bold.ttf"  → "DejaVuSans-Bold"
//   "NotoSerif.ttf.gz"     → "NotoSerif"
// Returns "" for non-font files.
std::string FontStem(const std::filesystem::path &p) noexcept;

// Default platform font directories, searched in order.
std::vector<std::filesystem::path> SystemFontDirs();

// Core search: scan dirs in order, return path of first file whose
// normalized stem matches any of the normalized names (in names priority order).
[[nodiscard]] Option<String> FontSearch(std::span<const std::string> norms,
											   std::span<const std::filesystem::path> dirs);

} // namespace detail

// Search platform font directories for the first font matching one of the
// given names (tried in priority order). Matching is case-insensitive and
// ignores separators (-/_/ ) so "CourierNew" also finds "Courier New.ttf".
//
//   auto path = sdl3::findFontPath({"DejaVuSans-Bold", "Arial", "FreeSans"});
[[nodiscard]] Option<String> FindFontPath(std::initializer_list<const char *> names);

// Same as above, but searches the given directories in priority order instead
// of the platform defaults. Each string may be a std::string or a literal.
//
//   auto path = sdl3::findFontPath(
//       {"DejaVuSans-Bold", "Arial"},
//       {"./assets/fonts/",
//        std::format("/home/{}/.local/share/fonts", sdl3::getSystemUserName()),
//        "/usr/share/fonts/"});
[[nodiscard]] Option<String> FindFontPath(std::initializer_list<const char *> names,
												 std::initializer_list<std::string> dirs);

// ============================================================================
// Font — RAII TTF_Font
// ============================================================================

class Font : public Wrapper<TTF_Font, TTF_CloseFont> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<Font, StringView> Open(const String &path, float ptSize);

	// Search platform font directories for the first matching font name.
	// Names are tried in priority order; matching ignores case and separators.
	//
	//   auto font = sdl3::Font::FindLocal({"DejaVuSans-Bold", "Arial", "FreeSans"}, 14.f);
	[[nodiscard]] static Result<Font, StringView> FindLocal(std::initializer_list<const char *> names,
															float ptSize = 14.f);

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
	FindLocal(std::initializer_list<const char *> names, std::initializer_list<std::string> dirs, float ptSize = 14.f);

	// ── Metrics & style ──────────────────────────────────────────────────────

	void SetStyle(FontStyle s);
	void SetOutline(int px);
	void SetSize(float pt);
	/// Taille courante, en points.
	[[nodiscard]] float PointSize() const { return m_handle ? TTF_GetFontSize(m_handle) : 0.f; }
	/// Copie INDÉPENDANTE de la police (même fichier, même style) : sa taille
	/// peut changer sans toucher l'originale — ni les textes déjà mis en forme
	/// avec elle, que `SetSize` sur la police partagée re-mettrait en page.
	[[nodiscard]] Result<Font, StringView> Copy() const;
	void SetHinting(TTF_HintingFlags h);

	[[nodiscard]] FontStyle Style() const;
	[[nodiscard]] int Ascent() const { return m_handle ? TTF_GetFontAscent(m_handle) : 0; }
	[[nodiscard]] int Descent() const { return m_handle ? TTF_GetFontDescent(m_handle) : 0; }
	[[nodiscard]] int Height() const { return m_handle ? TTF_GetFontHeight(m_handle) : 0; }
	[[nodiscard]] int LineSkip() const { return m_handle ? TTF_GetFontLineSkip(m_handle) : 0; }
	[[nodiscard]] bool IsFixedWidth() const { return m_handle && TTF_FontIsFixedWidth(m_handle); }

	/// Pixel dimensions of the rendered text.
	[[nodiscard]] Option<Point> Measure(const String &text) const;

	// ── Surface rendering ────────────────────────────────────────────────────

	[[nodiscard]] Result<Surface, StringView> RenderSolid(const String &text, Color fg) const;
	
	[[nodiscard]] Result<Surface, StringView> RenderSolid(const String &text, FColor fg) const;

	[[nodiscard]] Result<Surface, StringView> RenderShaded(const String &text, Color fg, Color bg) const;

	[[nodiscard]] Result<Surface, StringView> RenderShaded(const String &text, FColor fg, FColor bg) const;

	[[nodiscard]] Result<Surface, StringView> RenderBlended(const String &text, Color fg) const;

	[[nodiscard]] Result<Surface, StringView> RenderBlended(const String &text, FColor fg) const;

	[[nodiscard]] Result<Surface, StringView> RenderLcd(const String &text, Color fg, Color bg) const;

	[[nodiscard]] Result<Surface, StringView> RenderLcd(const String &text, FColor fg, FColor bg) const;

	/// Render text directly to a Texture (blended quality, single call).
	[[nodiscard]] Result<Texture, StringView> RenderTexture(Renderer &ren, const String &text, Color fg) const;

	[[nodiscard]] Result<Texture, StringView> RenderTexture(Renderer &ren, const String &text, FColor fg) const;
};

// ============================================================================
// TextEngine — GPU-accelerated text engine (SDL3_ttf new API)
// ============================================================================

class TextEngine : public Wrapper<TTF_TextEngine, TTF_DestroyRendererTextEngine> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<TextEngine, Error> Create(Renderer &ren);
};

// ============================================================================
// Text — a rendered TTF_Text object (backed by TextEngine)
// ============================================================================

class Text : public Wrapper<TTF_Text, TTF_DestroyText> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<Text, Error> Create(TextEngine &engine, Font &font, const String &str);

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

	[[nodiscard]] Option<Point> GetSize() const;
};

} // namespace sdl3
