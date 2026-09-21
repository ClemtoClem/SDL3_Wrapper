#pragma once
/**
 * Lecture des métadonnées d'une ROM (titre, éditeur, icône NDS) DIRECTEMENT
 * dans les en-têtes de cartouche GBA / NDS / GB-GBC : pas de reniflage du nom
 * de fichier, pas de base de données, seulement les octets présents dans le
 * fichier. Indépendant du cœur d'émulation (examples/emulator_demo/emulator).
 */
#include <cstdint>
#include <utility>
#include <vector>

#include "core/core.hpp"

namespace emulator_demo::app {

/// Console détectée d'après les octets de l'en-tête.
enum class RomSystem : uint8_t { NDS, GBA, GBC };

[[nodiscard]] inline const char *RomSystemName(RomSystem system) {
	switch (system) {
	case RomSystem::NDS:
		return "NDS";
	case RomSystem::GBA:
		return "GBA";
	case RomSystem::GBC:
		return "GBC";
	}
	return "?";
}

/// Icône NDS décodée : 32x32, valeurs 0xAABBGGRR (octets R,G,B,A en mémoire).
struct RomIcon {
	static constexpr int SIZE = 32;
	std::vector<uint32_t> rgba; ///< SIZE*SIZE entrées
};

/// Métadonnées extraites d'un en-tête. Tous les champs sont « au mieux » et
/// ne proviennent que d'octets réellement présents — jamais inventés.
struct RomMetadata {
	RomSystem system = RomSystem::NDS;
	int64_t sizeBytes = 0;
	String title;	  ///< vide si illisible
	String publisher; ///< "Unknown" si le code éditeur n'est pas dans la table
	Option<RomIcon> icon = NONE; ///< NDS uniquement

	String gameCode;   ///< code produit à 4 caractères (NDS/GBA), vide sinon
	String region;	   ///< déduite du code produit ou de l'octet de destination GB
	int version = 0;   ///< révision de la ROM (octet d'en-tête)
	/// Somme de contrôle de l'en-tête : CRC-16 (NDS), complément (GBA),
	/// somme 0x134-0x14C (GB). NONE si l'en-tête est trop court.
	Option<bool> headerChecksumOk = NONE;
	String hardware;   ///< NDS/NDS+DSi/DSi, CGB uniquement/compatible/DMG (+SGB)
	String cartridge;  ///< GB : type de cartouche (MBC…) ; GBA : type de sauvegarde détecté
	String romCapacity; ///< taille de puce annoncée par l'en-tête (NDS, GB)
	uint32_t crc32 = 0; ///< CRC-32 de la ROM entière (identifiant usuel des bases No-Intro)
};

/// Détecte le format d'après les octets (jamais l'extension) et extrait les
/// métadonnées. NONE si les octets ne ressemblent à aucun en-tête connu.
[[nodiscard]] Option<RomMetadata> ReadRomMetadataFromBytes(const std::vector<uint8_t> &bytes);

/// Idem depuis un fichier. Une ARCHIVE (.zip, .tar, .tar.gz, .gz) est
/// acceptée : la première ROM qu'elle contient est lue (cf. rom_source.hpp
/// pour choisir une entrée précise). NONE si illisible ou non reconnu.
[[nodiscard]] Option<RomMetadata> ReadRomMetadata(const String &path);

/// Lignes « Clé : valeur » prêtes à afficher (boîte ROMs, --rom-info).
[[nodiscard]] std::vector<std::pair<String, String>> DescribeRom(const RomMetadata &metadata);

} // namespace emulator_demo::app
