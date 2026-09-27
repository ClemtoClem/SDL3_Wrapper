// Définitions de sdl3/tray.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sdl3/tray.hpp"

namespace sdl3 {

// ── TrayEntry ────────────────────────────────────────────────────────────────

void TrayEntry::SetLabel(const String &label) noexcept {
	if (m_handle)
		SDL_SetTrayEntryLabel(m_handle, label.c_str());
}

void TrayEntry::SetChecked(bool checked) noexcept {
	if (m_handle)
		SDL_SetTrayEntryChecked(m_handle, checked);
}

void TrayEntry::SetEnabled(bool enabled) noexcept {
	if (m_handle)
		SDL_SetTrayEntryEnabled(m_handle, enabled);
}

void TrayEntry::Click() noexcept {
	if (m_handle)
		SDL_ClickTrayEntry(m_handle);
}

void TrayEntry::Remove() noexcept {
	if (m_handle) {
		SDL_RemoveTrayEntry(m_handle);
		m_handle = nullptr;
	}
}

void TrayEntry::SetCallback(Callback cb) {
	if (!m_handle)
		return;
	auto *ctx = new Callback(std::move(cb));
	SDL_SetTrayEntryCallback(
		m_handle,
		[](void *userdata, SDL_TrayEntry *entry) { (*static_cast<Callback *>(userdata))(TrayEntry(entry)); }, ctx);
}

// ── TrayMenu ─────────────────────────────────────────────────────────────────

std::vector<TrayEntry> TrayMenu::Entries() const {
	if (!m_handle)
		return {};
	int count = 0;
	const SDL_TrayEntry **raw = SDL_GetTrayEntries(m_handle, &count);
	std::vector<TrayEntry> out;
	out.reserve(size_t(count));
	for (int i = 0; i < count; ++i)
		out.emplace_back(const_cast<SDL_TrayEntry *>(raw[i]));
	return out;
}

TrayEntry TrayMenu::InsertAt(int pos, const String &label, TrayEntryFlags flags) {
	return TrayEntry(m_handle ? SDL_InsertTrayEntryAt(m_handle, pos, label.CStr(), detail::ToSDL(flags)) : nullptr);
}

Option<TrayEntry> TrayMenu::ParentEntry() const {
	if (!m_handle)
		return NONE;
	auto *e = SDL_GetTrayMenuParentEntry(m_handle);
	if (!e)
		return NONE;
	return Some(TrayEntry(e));
}

Option<TrayMenu> TrayEntry::Submenu() const {
	if (!m_handle)
		return NONE;
	auto *m = SDL_GetTraySubmenu(m_handle);
	if (!m)
		return NONE;
	return Some(TrayMenu(m));
}

Option<TrayMenu> TrayEntry::Parent() const {
	if (!m_handle)
		return NONE;
	auto *m = SDL_GetTrayEntryParent(m_handle);
	if (!m)
		return NONE;
	return Some(TrayMenu(m));
}

// ── Tray ─────────────────────────────────────────────────────────────────────

Result<Tray, Error> Tray::Create(const Surface &icon, const String &tooltip) {
	auto *t = SDL_CreateTray(icon.Get(), tooltip.c_str());
	if (!t)
		return Err(GetError());
	return Ok(Tray(t));
}

Result<Tray, StringView> Tray::CreateWithoutIcon(const String &tooltip) {
	auto *t = SDL_CreateTray(nullptr, tooltip.c_str());
	if (!t)
		return Err(GetError());
	return Ok(Tray(t));
}

void Tray::SetIcon(const Surface &icon) noexcept {
	if (m_handle)
		SDL_SetTrayIcon(m_handle, icon.Get());
}

void Tray::SetTooltip(const String &tooltip) noexcept {
	if (m_handle)
		SDL_SetTrayTooltip(m_handle, tooltip.c_str());
}

Option<TrayMenu> Tray::Menu() const {
	if (!m_handle)
		return NONE;
	auto *m = SDL_GetTrayMenu(m_handle);
	if (!m)
		return NONE;
	return Some(TrayMenu(m));
}

} // namespace sdl3
