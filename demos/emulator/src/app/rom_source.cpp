#include <emulator/app/rom_source.hpp>

#include <sdl3/filesystem.hpp>
#include <sdl3/iostream.hpp>

#include <algorithm>
#include <cstdlib>

namespace emulator::app {

namespace {

[[nodiscard]] String BaseName(const String &path) {
	size_t slash = path.Rfind('/');
	return slash == String::NPOS ? path : path.Substr(slash + 1);
}

[[nodiscard]] String DirectoryOf(const String &path) {
	size_t slash = path.Rfind('/');
	return slash == String::NPOS ? String() : path.Substr(0, slash);
}

String g_archivePassword;

[[nodiscard]] archive::ReadOptions ArchiveReadOptions() {
	archive::ReadOptions options;
	options.password = g_archivePassword;
	return options;
}

/// Volume suivant d'un RAR multi-volumes (« jeu.part2.rar ») : seul le
/// premier volume est proposé.
[[nodiscard]] bool IsSecondaryRarVolume(const String &name) {
	const String lower = name.ToLower();
	const size_t part = lower.Rfind(".part");
	if (part == String::NPOS || !lower.EndsWith(".rar"))
		return false;
	const String digits = lower.Substr(part + 5, lower.GetSize() - 4 - part - 5);
	if (digits.IsEmpty())
		return false;
	for (size_t i = 0; i < digits.GetSize(); ++i)
		if (digits.CharAt(i) < '0' || digits.CharAt(i) > '9')
			return false;
	return std::strtoul(digits.CStr(), nullptr, 10) != 1;
}

[[nodiscard]] String Join(const String &directory, const String &name) {
	if (directory.IsEmpty() || directory == ".")
		return name;
	return directory.EndsWith("/") ? directory + name : directory + "/" + name;
}

} // namespace

bool IsRomFileName(const String &name) {
	String lower = name.ToLower();
	return lower.EndsWith(".nds") || lower.EndsWith(".gba") || lower.EndsWith(".gbc") || lower.EndsWith(".gb");
}

bool IsArchivePath(const String &path) { return archive::HasArchiveExtension(path); }

void SetArchivePassword(const String &password) { g_archivePassword = password; }

String LoadedRom::DisplayName() const {
	if (archive.IsNone())
		return sourcePath;
	return String::Format("%s › %s", BaseName(sourcePath).CStr(), archive.Value().entryName.CStr());
}

String LoadedRom::LogicalPath() const {
	if (archive.IsNone())
		return sourcePath;
	return Join(DirectoryOf(sourcePath), BaseName(archive.Value().entryName));
}

String RomCandidate::DisplayName() const {
	return entry.IsEmpty() ? path : String::Format("%s › %s", path.CStr(), entry.CStr());
}

String RomCandidate::ShortName() const { return BaseName(entry.IsEmpty() ? path : entry); }

Option<RomSystem> RomSystemFromName(const String &name) {
	String lower = name.ToLower();
	// « jeu.gba.gz » : le suffixe de compression d'un fichier seul est retiré.
	for (const char *suffix : {".gz", ".xz", ".bz2", ".zst"})
		if (lower.EndsWith(suffix)) {
			lower = lower.Substr(0, lower.GetSize() - String(suffix).GetSize());
			break;
		}
	if (lower.EndsWith(".nds"))
		return Some(RomSystem::NDS);
	if (lower.EndsWith(".gba"))
		return Some(RomSystem::GBA);
	if (lower.EndsWith(".gbc") || lower.EndsWith(".gb"))
		return Some(RomSystem::GBC);
	return NONE;
}

Option<RomSystem> RomCandidate::System() const {
	if (!entry.IsEmpty())
		if (Option<RomSystem> system = RomSystemFromName(entry); system.IsSome())
			return system;
	return RomSystemFromName(path);
}

int RomSystemOrder(Option<RomSystem> system) {
	if (system.IsNone())
		return 3;
	switch (system.Value()) {
	case RomSystem::GBC:
		return 0;
	case RomSystem::GBA:
		return 1;
	case RomSystem::NDS:
		return 2;
	}
	return 3;
}

void SortRomsBySystem(std::vector<RomCandidate> &roms) {
	std::stable_sort(roms.begin(), roms.end(), [](const RomCandidate &a, const RomCandidate &b) {
		const int sa = RomSystemOrder(a.System()), sb = RomSystemOrder(b.System());
		if (sa != sb)
			return sa < sb;
		const String na = a.ShortName().ToLower(), nb = b.ShortName().ToLower();
		if (!(na == nb))
			return std::string_view(na.CStr()) < std::string_view(nb.CStr());
		return std::string_view(a.DisplayName().CStr()) < std::string_view(b.DisplayName().CStr());
	});
}

Result<LoadedRom, String> LoadRom(const String &path, const String &entry) {
	LoadedRom rom;
	rom.sourcePath = path;
	if (!IsArchivePath(path)) {
		auto bytes = sdl3::ReadFile(path);
		if (!bytes)
			return Err(String::Format("lecture de %s impossible : %s", path.CStr(), String(bytes.Error()).CStr()));
		rom.bytes = std::move(bytes).Unwrap();
		return Ok(std::move(rom));
	}

	auto opened = archive::Archive::Open(path, ArchiveReadOptions());
	if (opened.IsError())
		return Err(String::Format("%s : %s%s", path.CStr(), opened.Error().Describe().CStr(),
								  opened.Error().NeedsPassword() ? " (--archive-password=…)" : ""));
	archive::Archive &archive = opened.Value();

	// Entrée demandée, sinon première ROM ; un fichier compressé seul (.gz,
	// .xz, .bz2, .zst) est accepté même si son nom n'a pas d'extension de ROM.
	Option<size_t> chosen = NONE;
	size_t romEntries = 0;
	const auto &entries = archive.Entries();
	for (size_t i = 0; i < entries.size(); ++i) {
		if (entries[i].type != archive::EntryType::FILE)
			continue;
		const bool isRom = IsRomFileName(entries[i].path) || archive::IsSingleFileFormat(archive.GetFormat());
		if (isRom)
			++romEntries;
		if (chosen.IsNone() && (entry.IsEmpty() ? isRom : entries[i].path == entry))
			chosen = Some(i);
	}
	if (chosen.IsNone()) {
		if (!entry.IsEmpty())
			return Err(String::Format("%s : entrée « %s » introuvable", path.CStr(), entry.CStr()));
		return Err(String::Format("%s : aucune ROM (.nds .gba .gbc .gb) dans l'archive %s (%d entrée(s))", path.CStr(),
								  archive::FormatName(archive.GetFormat()), int(entries.size())));
	}

	auto extracted = archive.Extract(chosen.Unwrap());
	if (extracted.IsError())
		return Err(String::Format("%s : %s%s", path.CStr(), extracted.Error().Describe().CStr(),
								  extracted.Error().NeedsPassword() ? " (--archive-password=…)" : ""));
	rom.bytes = std::move(extracted).Unwrap();

	const archive::EntryInfo &selected = entries[chosen.Unwrap()];
	ArchiveOrigin origin;
	origin.format = archive.GetFormat();
	origin.entryName = selected.path;
	origin.storedBytes = selected.packedSize;
	origin.method = selected.method;
	origin.encrypted = selected.encrypted;
	origin.romEntries = romEntries;
	if (Option<sdl3::PathInfo> info = sdl3::filesystem::PathInfo(path); info.IsSome())
		origin.archiveBytes = info.Value().size;
	rom.archive = Some(std::move(origin));
	return Ok(std::move(rom));
}

Result<std::vector<RomCandidate>, String> ListArchiveRoms(const String &archivePath) {
	auto opened = archive::Archive::Open(archivePath, ArchiveReadOptions());
	if (opened.IsError())
		return Err(String::Format("%s : %s", archivePath.CStr(), opened.Error().Describe().CStr()));
	std::vector<RomCandidate> roms;
	const bool singleFile = archive::IsSingleFileFormat(opened.Value().GetFormat());
	for (const archive::EntryInfo &entry : opened.Value().Entries())
		if (entry.type == archive::EntryType::FILE && (singleFile || IsRomFileName(entry.path)))
			roms.push_back(RomCandidate{archivePath, entry.path, entry.size});
	return Ok(std::move(roms));
}

namespace {

/// ROMs d'UN dossier (fichiers et archives), puis de ses sous-dossiers.
void ScanRomDirectory(const String &directory, std::vector<String> *problems, bool recursive, int depth,
					  std::vector<RomCandidate> &out) {
	std::vector<String> names;
	for (const char *pattern : {"*.nds", "*.gba", "*.gbc", "*.gb", "*.zip", "*.7z", "*.rar", "*.tar", "*.tgz", "*.gz",
								"*.txz", "*.xz", "*.tbz2", "*.tbz", "*.bz2", "*.tzst", "*.zst", "*.iso"})
		for (const String &name : sdl3::filesystem::Glob(directory, pattern, true))
			if (!IsSecondaryRarVolume(name) && std::find(names.begin(), names.end(), name) == names.end())
				names.push_back(name); // Glob peut renvoyer un nom pour deux motifs
	for (const String &name : names) {
		String path = Join(directory, name);
		Option<sdl3::PathInfo> info = sdl3::filesystem::PathInfo(path);
		if (info.IsSome() && info.Value().type != sdl3::PathType::FILE)
			continue; // un dossier nommé « jeux.zip » n'est pas une archive
		if (!IsArchivePath(path)) {
			out.push_back(RomCandidate{path, String(), info.IsSome() ? info.Value().size : 0});
			continue;
		}
		auto roms = ListArchiveRoms(path);
		if (roms.IsError()) {
			if (problems)
				problems->push_back(roms.Error());
			continue;
		}
		for (RomCandidate &rom : roms.Value())
			out.push_back(std::move(rom));
	}
	if (!recursive || depth >= MAX_ROM_SCAN_DEPTH)
		return;

	std::vector<String> subdirectories;
	(void)sdl3::filesystem::EnumerateDirectory(directory, [&](const char *, const char *file) {
		const String name(file);
		if (name.IsEmpty() || name.StartsWith("."))
			return true; // dossiers cachés (caches, VCS…) et « . » / « .. »
		const String path = Join(directory, name);
		if (Option<sdl3::PathInfo> info = sdl3::filesystem::PathInfo(path);
			info.IsSome() && info.Value().type == sdl3::PathType::DIRECTORY)
			subdirectories.push_back(path);
		return true;
	});
	std::sort(subdirectories.begin(), subdirectories.end(),
			  [](const String &a, const String &b) { return std::string_view(a.CStr()) < std::string_view(b.CStr()); });
	for (const String &sub : subdirectories)
		ScanRomDirectory(sub, problems, recursive, depth + 1, out);
}

} // namespace

std::vector<RomCandidate> FindRoms(const String &directory, std::vector<String> *problems, bool recursive) {
	std::vector<RomCandidate> out;
	ScanRomDirectory(directory, problems, recursive, 0, out);
	SortRomsBySystem(out);
	return out;
}

Option<RomCandidate> ResolveLogicalRom(const String &logicalPath) {
	if (sdl3::filesystem::PathInfo(logicalPath).IsSome())
		return Some(RomCandidate{logicalPath, String(), 0});
	const String directory = DirectoryOf(logicalPath);
	const String wanted = BaseName(logicalPath);
	// Même dossier seulement : la ROM d'un état est rangée à côté de lui.
	for (const RomCandidate &candidate : FindRoms(directory.IsEmpty() ? String(".") : directory, nullptr, false))
		if (!candidate.entry.IsEmpty() && BaseName(candidate.entry) == wanted)
			return Some(candidate);
	return NONE;
}

Result<String, String> MaterializeRom(const LoadedRom &rom, const String &cacheDirectory) {
	if (rom.archive.IsNone())
		return Ok(rom.sourcePath);
	(void)sdl3::filesystem::CreateDirectory(cacheDirectory);
	const uint32_t crc = archive::Crc32(rom.bytes);
	String path = Join(cacheDirectory, String::Format("%08X-%s", crc, BaseName(rom.archive.Value().entryName).CStr()));
	// Le CRC dans le nom identifie le contenu : une copie de même taille est
	// réutilisée sans être réécrite.
	if (Option<sdl3::PathInfo> info = sdl3::filesystem::PathInfo(path);
		info.IsSome() && info.Value().size == uint64_t(rom.bytes.size()))
		return Ok(path);
	if (!sdl3::WriteFile(path, rom.bytes.data(), rom.bytes.size()))
		return Err(String::Format("écriture de la ROM extraite impossible : %s", path.CStr()));
	return Ok(path);
}

String DefaultExtractDirectory() {
	if (Option<String> pref = sdl3::filesystem::PrefPath("EmulOS", "emulator"); pref.IsSome())
		return pref.Value() + "roms";
	return String(".emulator_cache");
}

} // namespace emulator::app
