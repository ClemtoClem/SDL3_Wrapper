#include "rom_source.hpp"

#include "sdl3/filesystem.hpp"
#include "sdl3/iostream.hpp"

#include <cstdlib>

namespace emulator_demo::app {

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

[[nodiscard]] data::archive::ReadOptions ArchiveReadOptions() {
	data::archive::ReadOptions options;
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

bool IsArchivePath(const String &path) { return data::archive::HasArchiveExtension(path); }

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

	auto opened = data::archive::Archive::Open(path, ArchiveReadOptions());
	if (opened.IsError())
		return Err(String::Format("%s : %s%s", path.CStr(), opened.Error().Describe().CStr(),
								  opened.Error().NeedsPassword() ? " (--archive-password=…)" : ""));
	data::archive::Archive &archive = opened.Value();

	// Entrée demandée, sinon première ROM ; un fichier compressé seul (.gz,
	// .xz, .bz2, .zst) est accepté même si son nom n'a pas d'extension de ROM.
	Option<size_t> chosen = NONE;
	size_t romEntries = 0;
	const auto &entries = archive.Entries();
	for (size_t i = 0; i < entries.size(); ++i) {
		if (entries[i].type != data::archive::EntryType::FILE)
			continue;
		const bool isRom = IsRomFileName(entries[i].path) || data::archive::IsSingleFileFormat(archive.GetFormat());
		if (isRom)
			++romEntries;
		if (chosen.IsNone() && (entry.IsEmpty() ? isRom : entries[i].path == entry))
			chosen = Some(i);
	}
	if (chosen.IsNone()) {
		if (!entry.IsEmpty())
			return Err(String::Format("%s : entrée « %s » introuvable", path.CStr(), entry.CStr()));
		return Err(String::Format("%s : aucune ROM (.nds .gba .gbc .gb) dans l'archive %s (%d entrée(s))", path.CStr(),
								  data::archive::FormatName(archive.GetFormat()), int(entries.size())));
	}

	auto extracted = archive.Extract(chosen.Unwrap());
	if (extracted.IsError())
		return Err(String::Format("%s : %s%s", path.CStr(), extracted.Error().Describe().CStr(),
								  extracted.Error().NeedsPassword() ? " (--archive-password=…)" : ""));
	rom.bytes = std::move(extracted).Unwrap();

	const data::archive::EntryInfo &selected = entries[chosen.Unwrap()];
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
	auto opened = data::archive::Archive::Open(archivePath, ArchiveReadOptions());
	if (opened.IsError())
		return Err(String::Format("%s : %s", archivePath.CStr(), opened.Error().Describe().CStr()));
	std::vector<RomCandidate> roms;
	const bool singleFile = data::archive::IsSingleFileFormat(opened.Value().GetFormat());
	for (const data::archive::EntryInfo &entry : opened.Value().Entries())
		if (entry.type == data::archive::EntryType::FILE && (singleFile || IsRomFileName(entry.path)))
			roms.push_back(RomCandidate{archivePath, entry.path, entry.size});
	return Ok(std::move(roms));
}

std::vector<RomCandidate> FindRoms(const String &directory, std::vector<String> *problems) {
	std::vector<RomCandidate> out;
	std::vector<String> names;
	for (const char *pattern : {"*.nds", "*.gba", "*.gbc", "*.gb", "*.zip", "*.7z", "*.rar", "*.tar", "*.tgz", "*.gz",
								"*.txz", "*.xz", "*.tbz2", "*.tbz", "*.bz2", "*.tzst", "*.zst", "*.iso"})
		for (const String &name : sdl3::filesystem::Glob(directory, pattern, true))
			if (!IsSecondaryRarVolume(name))
				names.push_back(name);
	for (const String &name : names) {
		// Doublons éventuels entre motifs (sécurité : Glob peut se recouvrir).
		bool duplicate = false;
		for (const RomCandidate &known : out)
			duplicate = duplicate || known.path == Join(directory, name);
		if (duplicate)
			continue;
		String path = Join(directory, name);
		if (!IsArchivePath(path)) {
			uint64_t size = 0;
			if (Option<sdl3::PathInfo> info = sdl3::filesystem::PathInfo(path); info.IsSome())
				size = info.Value().size;
			out.push_back(RomCandidate{path, String(), size});
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
	return out;
}

Option<RomCandidate> ResolveLogicalRom(const String &logicalPath) {
	if (sdl3::filesystem::PathInfo(logicalPath).IsSome())
		return Some(RomCandidate{logicalPath, String(), 0});
	const String directory = DirectoryOf(logicalPath);
	const String wanted = BaseName(logicalPath);
	for (const RomCandidate &candidate : FindRoms(directory.IsEmpty() ? String(".") : directory))
		if (!candidate.entry.IsEmpty() && BaseName(candidate.entry) == wanted)
			return Some(candidate);
	return NONE;
}

Result<String, String> MaterializeRom(const LoadedRom &rom, const String &cacheDirectory) {
	if (rom.archive.IsNone())
		return Ok(rom.sourcePath);
	(void)sdl3::filesystem::CreateDirectory(cacheDirectory);
	const uint32_t crc = data::archive::Crc32(rom.bytes);
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
	if (Option<String> pref = sdl3::filesystem::PrefPath("EmulOS", "emulator_demo"); pref.IsSome())
		return pref.Value() + "roms";
	return String(".emulator_demo_cache");
}

} // namespace emulator_demo::app
