// Smoke test : locale, guid, filesystem (accès direct, sans sandbox — cf.
// smoke_test_1 pour l'API Storage sandboxée) et power (déjà couvert
// partiellement par smoke_test_2, ici on vérifie surtout guid/locale/filesystem).
#include "sdl3/sdl3.hpp"
#include <iostream>

int main() {
	using namespace sdl3;

	// --- locale ---
	auto locales = locale::Preferred();
	std::cout << "preferred locales: " << locales.size() << "\n";
	for (auto &l : locales) {
		std::cout << "  " << l.language.c_str() << "-" << (l.country.IsSome() ? l.country.Unwrap().c_str() : "??")
				  << "\n";
	}

	// --- guid ---
	Guid g = Guid::FromString("030000005e0400008e02000000007200"); // GUID exemple (32 chars hex)
	String s = g.ToString();
	std::cout << "guid roundtrip: " << s.c_str() << "\n";
	Guid g2 = Guid::FromString(s);
	std::cout << "guid equal after roundtrip: " << (g == g2) << "\n";

	// --- filesystem ---
	std::cout << "base path: " << filesystem::BasePath().c_str() << "\n";
	std::cout << "current dir: " << filesystem::CurrentDirectory().c_str() << "\n";
	std::cout << "documents folder: " << filesystem::UserFolder(Folder::DOCUMENTS) << "\n";

	auto pref = filesystem::PrefPath("SDL3Wrapper", "SmokeTest7");
	if (pref.IsSome())
		std::cout << "pref path: " << pref.Unwrap().c_str() << "\n";

	filesystem::CreateDirectory("build/smoke7_tmp");
	filesystem::CopyFile("assets/sounds/SOUNDS.md", "build/smoke7_tmp/copy.md");
	auto info = filesystem::PathInfo("build/smoke7_tmp/copy.md");
	if (info.IsSome())
		std::cout << "copied file size=" << info.Unwrap().size << "\n";

	auto matches = filesystem::Glob("assets/textures", "*.png");
	std::cout << "glob png matches: " << matches.size() << "\n";

	filesystem::EnumerateDirectory("build/smoke7_tmp", [](const char *dir, const char *fname) {
		std::cout << "  entry: " << dir << fname << "\n";
		return true;
	});

	filesystem::Remove("build/smoke7_tmp/copy.md");
	filesystem::Remove("build/smoke7_tmp");

	// --- power ---
	auto pw = power::Info();
	std::cout << "power state=" << int(pw.state) << "\n";

	std::cout << "smoke test 7 done\n";
	return 0;
}
