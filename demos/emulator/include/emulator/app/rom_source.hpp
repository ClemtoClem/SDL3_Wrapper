#pragma once
/**
 * Source d'une ROM : un fichier `.nds/.gba/.gbc/.gb`, ou une ENTRÉE d'archive
 * (`.zip`, `.7z`, `.tar`, `.tar.gz`, `.tar.xz`, `.gz`, `.xz`, `.iso`) extraite
 * par `archive` — y compris chiffrée, avec `SetArchivePassword`.
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

#include <core/core.hpp>
#include <archive/archive.hpp>

#include "rom_metadata.hpp"

namespace emulator::app {

/// Extension de ROM reconnue (insensible à la casse).
[[nodiscard]] bool IsRomFileName(const String &name);
/// Extension d'archive reconnue (insensible à la casse).
[[nodiscard]] bool IsArchivePath(const String &path);
/// Mot de passe des archives chiffrées (zip AES/ZipCrypto, 7z AES, RAR), pour
/// toutes les ouvertures suivantes. Vide = aucun.
void SetArchivePassword(const String &password);

/// Où se trouve la ROM choisie dans son archive.
struct ArchiveOrigin {
	archive::Format format = archive::Format::ZIP;
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

/// Console visée d'après l'extension d'un nom de fichier : .nds → NDS,
/// .gba → GBA, .gbc et .gb → GBC (le cœur GBC fait aussi tourner les jeux
/// Game Boy). Les suffixes de compression d'un fichier seul (« jeu.gba.gz »)
/// sont ignorés. NONE pour une extension inconnue.
[[nodiscard]] Option<RomSystem> RomSystemFromName(const String &name);

/// Une ROM trouvée sur disque, prête à afficher dans une liste.
struct RomCandidate {
	String path;  ///< fichier ROM ou archive
	String entry; ///< entrée de l'archive, vide pour un fichier simple
	uint64_t size = 0;
	[[nodiscard]] String DisplayName() const;
	/// Nom affiché dans une liste : l'entrée (ou le fichier) sans dossier.
	[[nodiscard]] String ShortName() const;
	/// Console visée, d'après le nom de l'entrée puis celui du fichier
	/// (rapide : aucune lecture d'en-tête, aucune décompression).
	[[nodiscard]] Option<RomSystem> System() const;
};

/// Ordre d'affichage des consoles : Game Boy (Color), Game Boy Advance, DS ;
/// les ROMs de console inconnue viennent en dernier.
[[nodiscard]] int RomSystemOrder(Option<RomSystem> system);

/// Trie par console (RomSystemOrder) puis par nom, sans tenir compte de la
/// casse ; à nom égal, par chemin (ordre stable d'une exécution à l'autre).
void SortRomsBySystem(std::vector<RomCandidate> &roms);

/// Charge `path`. Pour une archive, `entry` désigne l'entrée à extraire ;
/// vide = la première ROM de l'archive. Err = message lisible (archive
/// corrompue, CRC faux, aucune ROM dedans, entrée absente…).
[[nodiscard]] Result<LoadedRom, String> LoadRom(const String &path, const String &entry = "");

/// ROMs d'une archive (entrées à extension de ROM), dans l'ordre de l'archive.
[[nodiscard]] Result<std::vector<RomCandidate>, String> ListArchiveRoms(const String &archivePath);

/// ROMs et ROMs d'archives d'un dossier et, si `recursive`, de tous ses
/// sous-dossiers (dossiers cachés « .xxx » exclus, profondeur bornée à
/// MAX_ROM_SCAN_DEPTH contre les boucles de liens symboliques), triées par
/// SortRomsBySystem. Les archives illisibles sont signalées dans `problems`
/// sans interrompre la liste.
inline constexpr int MAX_ROM_SCAN_DEPTH = 16;
[[nodiscard]] std::vector<RomCandidate> FindRoms(const String &directory, std::vector<String> *problems = nullptr,
												 bool recursive = true);

/// Retrouve la ROM d'un chemin LOGIQUE (base d'un état sauvegardé, par
/// exemple `jeux/jeu.gba` pour `jeux/jeu.gba.state0`) : le fichier lui-même
/// s'il existe, sinon une archive du même dossier contenant une ROM de ce nom.
[[nodiscard]] Option<RomCandidate> ResolveLogicalRom(const String &logicalPath);

/// Chemin sur disque utilisable par un cœur : la ROM elle-même, ou une copie
/// extraite dans `cacheDirectory` (créé au besoin, réutilisée si identique).
[[nodiscard]] Result<String, String> MaterializeRom(const LoadedRom &rom, const String &cacheDirectory);

/// Dossier de cache par défaut : dossier de préférences SDL
/// (~/.local/share/EmulOS/emulator/roms/ sous Linux), sinon ./.emulator_cache/.
[[nodiscard]] String DefaultExtractDirectory();

} // namespace emulator::app
