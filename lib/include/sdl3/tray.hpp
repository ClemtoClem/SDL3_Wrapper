#pragma once
#include <SDL3/SDL.h>
#include <functional>
#include <vector>

#include "../core/core.hpp"
#include "render.hpp" // Surface

namespace sdl3 {

// ============================================================================
// TrayEntry flags
// ============================================================================

enum class TrayEntryFlags: Uint32 {
	BUTTON 		= SDL_TRAYENTRY_BUTTON,		// requis
	CHECKBOX 	= SDL_TRAYENTRY_CHECKBOX,	// requis
	SUBMENU 	= SDL_TRAYENTRY_SUBMENU,	// requis
	DISABLED 	= SDL_TRAYENTRY_DISABLED,	// optionnel
	CHECKED 	= SDL_TRAYENTRY_CHECKED		// optionnel (checkbox)
};

/// `TrayEntryFlags` est un jeu de DRAPEAUX (un type d'entrée requis, plus des
/// options facultatives) : sans ces opérateurs, une `enum class` ne se combine
/// pas, et l'appelant devait retomber sur le type C brut `SDL_TrayEntryFlags`
/// — exactement ce que ce wrapper s'interdit (cf.
/// memory/feedback_wrap_c_pointers.md). Manque relevé parce que
/// tests/sdl3_smoke_test_8.cpp ne compilait plus depuis le passage de ces
/// constantes à une `enum class`.
[[nodiscard]] constexpr TrayEntryFlags operator|(TrayEntryFlags a, TrayEntryFlags b) noexcept {
	return static_cast<TrayEntryFlags>(static_cast<Uint32>(a) | static_cast<Uint32>(b));
}
[[nodiscard]] constexpr TrayEntryFlags operator&(TrayEntryFlags a, TrayEntryFlags b) noexcept {
	return static_cast<TrayEntryFlags>(static_cast<Uint32>(a) & static_cast<Uint32>(b));
}
constexpr TrayEntryFlags &operator|=(TrayEntryFlags &a, TrayEntryFlags b) noexcept { return a = a | b; }
/// Vrai si `flags` contient AU MOINS un des bits de `wanted`.
[[nodiscard]] constexpr bool HasFlag(TrayEntryFlags flags, TrayEntryFlags wanted) noexcept {
	return static_cast<Uint32>(flags & wanted) != 0u;
}

namespace detail {
	constexpr SDL_TrayEntryFlags ToSDL(TrayEntryFlags e) {
		return static_cast<SDL_TrayEntryFlags>(e);
	}
}

class TrayMenu;

// ============================================================================
// TrayEntry — vue non-possédante sur une entrée de menu (le Tray possède
// l'arbre entier ; les entrées/menus sont détruits avec lui).
// ============================================================================

class TrayEntry : public Borrowed<SDL_TrayEntry> {
public:
	using Callback = std::function<void(TrayEntry)>;

	using Borrowed::Borrowed;

	void SetLabel(const String &label) noexcept;
	[[nodiscard]] const char *Label() const noexcept { return m_handle ? SDL_GetTrayEntryLabel(m_handle) : ""; }

	void SetChecked(bool checked) noexcept;
	[[nodiscard]] bool Checked() const noexcept { return m_handle && SDL_GetTrayEntryChecked(m_handle); }

	void SetEnabled(bool enabled) noexcept;
	[[nodiscard]] bool Enabled() const noexcept { return m_handle && SDL_GetTrayEntryEnabled(m_handle); }

	void Click() noexcept;

	// Retire l'entrée du menu (la vue devient invalide après l'appel).
	void Remove() noexcept;

	// Nécessite que l'entrée ait été créée avec le flag `tray_entry::SUBMENU`.
	[[nodiscard]] TrayMenu CreateSubmenu();
	[[nodiscard]] Option<TrayMenu> Submenu() const;
	[[nodiscard]] Option<TrayMenu> Parent() const;

	// Le contexte du callback est heap-alloué et vit aussi longtemps que
	// l'entrée (détruite avec le Tray) ; un nouvel appel remplace l'ancien
	// sans le libérer (limitation SDL — pas de callback de nettoyage fourni).
	void SetCallback(Callback cb);
};

// ============================================================================
// TrayMenu — vue non-possédante sur un menu (racine du Tray, ou sous-menu
// d'une TrayEntry)
// ============================================================================

class TrayMenu : public Borrowed<SDL_TrayMenu> {
public:
	using Borrowed::Borrowed;

	[[nodiscard]] std::vector<TrayEntry> Entries() const;

	// `pos` négatif insère en fin de menu.
	[[nodiscard]] TrayEntry InsertAt(int pos, const String &label, TrayEntryFlags flags);
	[[nodiscard]] TrayEntry Append(const String &label, TrayEntryFlags flags) { return InsertAt(-1, label, flags); }

	[[nodiscard]] Option<TrayEntry> ParentEntry() const;
};

inline TrayMenu TrayEntry::CreateSubmenu() { return TrayMenu(m_handle ? SDL_CreateTraySubmenu(m_handle) : nullptr); }



// ============================================================================
// Tray — RAII SDL_Tray (icône système + arbre de menus)
// ============================================================================

class Tray : public Wrapper<SDL_Tray, SDL_DestroyTray> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<Tray, Error> Create(const Surface &icon, const String &tooltip);

	// Certaines plateformes acceptent une icône absente.
	[[nodiscard]] static Result<Tray, StringView> CreateWithoutIcon(const String &tooltip);

	void SetIcon(const Surface &icon) noexcept;
	void SetTooltip(const String &tooltip) noexcept;

	[[nodiscard]] TrayMenu CreateMenu() { return TrayMenu(m_handle ? SDL_CreateTrayMenu(m_handle) : nullptr); }

	[[nodiscard]] Option<TrayMenu> Menu() const;
};

namespace tray {
// À appeler périodiquement sur les plateformes qui n'intègrent pas le
// tray dans la boucle d'événements SDL (voir doc SDL_UpdateTrays).
inline void Update() noexcept { SDL_UpdateTrays(); }
} // namespace tray

} // namespace sdl3
