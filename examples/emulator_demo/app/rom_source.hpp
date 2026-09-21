#pragma once
/**
 * Source d'une ROM : un fichier `.nds/.gba/.gbc/.gb`, ou une ENTRÉE d'archive
 * (`.zip`, `.7z`, `.tar`, `.tar.gz`, `.tar.xz`, `.gz`, `.xz`, `.iso`) extraite
 * par `data::archive` — y compris chiffrée, avec `SetArchivePassword`.
 *
 * Les cœurs NDS/GBA lisent leur ROM depuis un chemin (chargement par
 * sections pour les grosses cartouches NDS, rechargement lors d'un
 * chargement d'état) : une ROM d'archive est donc MATÉRIALISÉE dans un
 * dossier de cache, sous un nom qui porte son CRC-32 (réutilisée telle quelle
 * au lancement suivant). Ses sauvegardes, en revanche, ne vont PAS dans le
 * cache : elles suivent le « chemin logique » `<dossier de l'archive>/<nom de
 * la ROM>`, exactement comme si la ROM avait été décompressée à côté de son
 * archive (cf. `Settings::registerRomAlias`).
 */
#include <cstdint>
#include <vector>

#include "core/core.hpp"
#include "data/archive.hpp"

namespace emulator_demo::app {

/// Extension de ROM reconnue (insensible à la casse).
[[nodiscard]] bool IsRomFileName(const String &name);
/// Extension d'archive reconnue (insensible à la casse).
[[nodiscard]] bool IsArchivePath(const String &path);
/// Mot de passe des archives chiffrées (zip AES/ZipCrypto, 7z AES, RAR), pour
/// toutes les ouvertures suivantes. Vide = aucun.
void SetArchivePassword(const String &password);

/// Où se trouve la ROM choisie dans son archive.
struct ArchiveOrigin {
	data::archive::Format format = data::archive::Format::ZIP;
	String entryName;		 ///< chemin de l'entrée dans l'archive
	uint64_t archiveBytes = 0;
	uint64_t storedBytes = 0; ///< taille compressée de l'entrée (0 = inconnue : bloc solide, tar.gz…)
	String method;			 ///< « deflate », « lzma2 + aes-256 », « stockée »…
	bool encrypted = false;
	size_t romEntries = 0;	 ///< nombre de ROMs dans l'archive
};

/// ROM chargée en mémoire.
struct LoadedRom {
	String sourcePath; ///< fichier donné par l'utilisateur (ROM ou archive)
	std::vector<uint8_t> bytes;
	Option<ArchiveOrigin> archive = NONE;

	/// « archive.zip › jeu.gba », ou le chemin de la ROM.
	[[nodiscard]] String DisplayName() const;
	/// Chemin sous lequel ranger sauvegardes et états : la ROM elle-même, ou
	/// `<dossier de l'archive>/<nom de base de l'entrée>`.
	[[nodiscard]] String LogicalPath() const;
};

/// Une ROM trouvée sur disque, prête à afficher dans une liste.
struct RomCandidate {
	String path;  ///< fichier ROM ou archive
	String entry; ///< entrée de l'archive, vide pour un fichier simple
	uint64_t size = 0;
	[[nodiscard]] String DisplayName() const;
};

/// Charge `path`. Pour une archive, `entry` désigne l'entrée à extraire ;
/// vide = la première ROM de l'archive. Err = message lisible (archive
/// corrompue, CRC faux, aucune ROM dedans, entrée absente…).
[[nodiscard]] Result<LoadedRom, String> LoadRom(const String &path, const String &entry = "");

/// ROMs d'une archive (entrées à extension de ROM), dans l'ordre de l'archive.
[[nodiscard]] Result<std::vector<RomCandidate>, String> ListArchiveRoms(const String &archivePath);

/// ROMs et ROMs d'archives d'un dossier (non récursif). Les archives
/// illisibles sont signalées dans `problems` sans interrompre la liste.
[[nodiscard]] std::vector<RomCandidate> FindRoms(const String &directory, std::vector<String> *problems = nullptr);

/// Retrouve la ROM d'un chemin LOGIQUE (base d'un état sauvegardé, par
/// exemple `jeux/jeu.gba` pour `jeux/jeu.gba.state0`) : le fichier lui-même
/// s'il existe, sinon une archive du même dossier contenant une ROM de ce nom.
[[nodiscard]] Option<RomCandidate> ResolveLogicalRom(const String &logicalPath);

/// Chemin sur disque utilisable par un cœur : la ROM elle-même, ou une copie
/// extraite dans `cacheDirectory` (créé au besoin, réutilisée si identique).
[[nodiscard]] Result<String, String> MaterializeRom(const LoadedRom &rom, const String &cacheDirectory);

/// Dossier de cache par défaut : dossier de préférences SDL
/// (~/.local/share/EmulOS/emulator_demo/roms/ sous Linux), sinon ./.emulator_demo_cache/.
[[nodiscard]] String DefaultExtractDirectory();

} // namespace emulator_demo::app
