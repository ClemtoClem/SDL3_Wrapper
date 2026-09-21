// Smoke test : system tray (Tray/TrayMenu/TrayEntry) — compile/link check only.
// SDL_CreateTray dlopen's a platform tray backend (appindicator/dbus/gio on
// Linux); in this sandboxed shell that pulls in a mismatched libpthread from
// a snap runtime and crashes the process with a hard dynamic-linker error
// before any SDL/C++ error handling can run — not a bug in this wrapper, just
// an unsuitable environment for actually invoking it. So: exercise the API
// shapes (flags, callback signature) without calling SDL_CreateTray.
#include "sdl3/sdl3.hpp"
#include <iostream>

int main() {
	using namespace sdl3;

	// `TrayEntryFlags` (enum class du wrapper) et non `SDL_TrayEntryFlags` :
	// le test exerçait encore d'anciennes constantes `tray_entry::*` qui
	// n'existent plus, et ne compilait donc plus.
	TrayEntryFlags flags = TrayEntryFlags::BUTTON | TrayEntryFlags::CHECKBOX | TrayEntryFlags::SUBMENU |
						   TrayEntryFlags::DISABLED | TrayEntryFlags::CHECKED;
	if (!HasFlag(flags, TrayEntryFlags::CHECKED)) {
		std::cerr << "combinaison de drapeaux incorrecte\n";
		return 1;
	}

	TrayEntry::Callback cb = [](TrayEntry e) { (void)e.Label(); };
	(void)cb;

	// Constructibles à partir d'un pointeur nul (vues non-possédantes) sans planter.
	TrayEntry entry(static_cast<SDL_TrayEntry *>(nullptr));
	TrayMenu menu(static_cast<SDL_TrayMenu *>(nullptr));
	std::cout << "null TrayEntry label=\"" << entry.Label() << "\" checked=" << entry.Checked() << "\n";
	std::cout << "null TrayMenu entries=" << menu.Entries().size() << "\n";

	std::cout << "smoke test 8 done (tray creation not invoked, see comment above)\n";
	return 0;
}
