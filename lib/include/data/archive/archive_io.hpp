#pragma once
/**
 * data::archive — entrées/sorties binaires communes à tous les formats.
 *
 * Tous les champs binaires des conteneurs (en-têtes zip, 7z, ISO 9660, xz…)
 * sont lus et écrits par `sdl3::IOStream` avec un ordre d'octets IMPOSÉ par
 * le nom de la méthode (`ReadU32Le`, `ReadU16Be`, `WriteU64Le`…), jamais celui
 * de la machine hôte. `BinaryReader`/`BinaryWriter` ajoutent à ce wrapper une
 * ERREUR COLLANTE : la première lecture ratée (flux tronqué) met le lecteur en
 * échec et toutes les suivantes rendent 0, ce qui permet d'écrire un en-tête
 * champ par champ puis de tester `Ok()` une seule fois — sans exception et
 * sans risque de lire des octets indéterminés.
 *
 * Les flux COMPRESSÉS (bits Huffman, codeur de plage LZMA) sont en revanche
 * lus d'un bloc (`ReadRange`) puis décodés en mémoire : un appel SDL par octet
 * rendrait ces décodeurs des dizaines de fois plus lents.
 *
 * On trouve aussi ici les conversions de chaînes (UTF-16 LE pour 7z, UTF-16
 * BE pour Joliet) et de dates (DOS, FILETIME Windows, ISO 9660).
 */
#include "../../core/core.hpp"
#include "../../core/string_unicode.hpp"
#include "../../sdl3/iostream.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace data::archive {

using Bytes = std::vector<uint8_t>;

// ============================================================================
// Lecture
// ============================================================================

/// Lecteur binaire à erreur collante, non possédant, sur un `sdl3::IOStream`.
class BinaryReader {
public:
	explicit BinaryReader(sdl3::IOStream& stream) noexcept : m_stream(&stream) {}

	[[nodiscard]] uint8_t U8() noexcept { return Take(m_stream->ReadU8()); }
	[[nodiscard]] uint16_t U16Le() noexcept { return Take(m_stream->ReadU16Le()); }
	[[nodiscard]] uint32_t U32Le() noexcept { return Take(m_stream->ReadU32Le()); }
	[[nodiscard]] uint64_t U64Le() noexcept { return Take(m_stream->ReadU64Le()); }
	[[nodiscard]] uint16_t U16Be() noexcept { return Take(m_stream->ReadU16Be()); }
	[[nodiscard]] uint32_t U32Be() noexcept { return Take(m_stream->ReadU32Be()); }
	[[nodiscard]] uint64_t U64Be() noexcept { return Take(m_stream->ReadU64Be()); }

	/// Champ « both-endian » (ISO 9660) : la valeur LE puis la même en BE. Une
	/// incohérence entre les deux copies met le lecteur en échec.
	[[nodiscard]] uint16_t U16LeBe() noexcept;
	[[nodiscard]] uint32_t U32LeBe() noexcept;

	/// Lit exactement `size` octets dans `out`.
	bool Read(void* out, size_t size) noexcept;

	/// `size` octets en vecteur ; refuse (échec) une taille supérieure à ce
	/// qu'il reste dans le flux — un champ de taille corrompu ne doit pas
	/// provoquer une allocation de plusieurs Go.
	[[nodiscard]] Bytes ReadBytes(uint64_t size);

	/// Chaîne brute de `size` octets (sans conversion).
	[[nodiscard]] String ReadString(uint64_t size);

	bool Skip(uint64_t count) noexcept { return Seek(Tell() + count); }
	bool Seek(uint64_t position) noexcept;
	[[nodiscard]] uint64_t Tell() const noexcept;
	[[nodiscard]] uint64_t Size() const noexcept;
	[[nodiscard]] uint64_t Remaining() const noexcept;

	[[nodiscard]] bool Ok() const noexcept { return m_ok; }
	void Fail() noexcept { m_ok = false; }
	[[nodiscard]] sdl3::IOStream& Stream() noexcept { return *m_stream; }

private:
	template <typename T> [[nodiscard]] T Take(Option<T> value) noexcept {
		if (!m_ok || value.IsNone()) {
			m_ok = false;
			return T{};
		}
		return value.Unwrap();
	}

	sdl3::IOStream* m_stream;
	bool m_ok = true;
};

// ============================================================================
// Écriture
// ============================================================================

/// Écrivain binaire à erreur collante, non possédant.
class BinaryWriter {
public:
	explicit BinaryWriter(sdl3::IOStream& stream) noexcept : m_stream(&stream) {}

	void U8(uint8_t v) noexcept { Check(m_stream->WriteU8(v)); }
	void U16Le(uint16_t v) noexcept { Check(m_stream->WriteU16Le(v)); }
	void U32Le(uint32_t v) noexcept { Check(m_stream->WriteU32Le(v)); }
	void U64Le(uint64_t v) noexcept { Check(m_stream->WriteU64Le(v)); }
	void U16Be(uint16_t v) noexcept { Check(m_stream->WriteU16Be(v)); }
	void U32Be(uint32_t v) noexcept { Check(m_stream->WriteU32Be(v)); }
	void U64Be(uint64_t v) noexcept { Check(m_stream->WriteU64Be(v)); }
	/// Champ « both-endian » (ISO 9660).
	void U16LeBe(uint16_t v) noexcept;
	void U32LeBe(uint32_t v) noexcept;

	void Write(std::span<const uint8_t> bytes) noexcept;
	void Write(const void* data, size_t size) noexcept { Check(m_stream->WriteExact(data, size)); }
	void WriteString(const String& text) noexcept { Write(text.CStr(), text.GetSize()); }
	/// Chaîne complétée par `pad` (ou tronquée) jusqu'à `width` octets.
	void WriteFixed(const String& text, size_t width, uint8_t pad = ' ') noexcept;
	void Fill(uint64_t count, uint8_t value = 0) noexcept;
	/// Complète jusqu'à un multiple de `alignment` octets.
	void AlignTo(uint64_t alignment, uint8_t value = 0) noexcept;

	bool Seek(uint64_t position) noexcept;
	[[nodiscard]] uint64_t Tell() const noexcept;
	[[nodiscard]] bool Ok() const noexcept { return m_ok; }
	[[nodiscard]] sdl3::IOStream& Stream() noexcept { return *m_stream; }

private:
	void Check(bool written) noexcept { m_ok = m_ok && written; }

	sdl3::IOStream* m_stream;
	bool m_ok = true;
};

// ============================================================================
// Flux mémoire
// ============================================================================

/// Flux en lecture qui POSSÈDE ses octets (le tampon est sur le tas : le
/// flux SDL qui pointe dessus reste valide quand l'objet est déplacé).
class MemoryStream {
public:
	[[nodiscard]] static Result<MemoryStream, String> FromBytes(Bytes bytes);

	[[nodiscard]] sdl3::IOStream& Stream() noexcept { return m_stream; }
	[[nodiscard]] const Bytes& Data() const noexcept { return *m_bytes; }

private:
	MemoryStream() = default;
	std::unique_ptr<Bytes> m_bytes;
	sdl3::IOStream m_stream;
};

/// Source d'une archive : fichier sur disque OU octets en mémoire, possédée
/// par le lecteur qui la parcourt.
class ArchiveSource {
public:
	[[nodiscard]] static Result<ArchiveSource, String> FromFile(const String& path);
	[[nodiscard]] static Result<ArchiveSource, String> FromBytes(Bytes bytes);
	[[nodiscard]] sdl3::IOStream& Stream() noexcept;
	[[nodiscard]] uint64_t Size() noexcept;

private:
	ArchiveSource() = default;
	Option<sdl3::IOStream> m_file = NONE;
	Option<MemoryStream> m_memory = NONE;
};

/// Flux en lecture sur des octets NON possédés (qui doivent survivre au flux).
[[nodiscard]] Result<sdl3::IOStream, String> ViewStream(std::span<const uint8_t> bytes);

/// Flux d'écriture en mémoire (croît à la demande).
[[nodiscard]] Result<sdl3::IOStream, String> CreateMemoryWriter();

/// Lit `size` octets à partir de `offset`, bornes vérifiées contre la taille
/// du flux (aucune allocation au-delà de ce qui existe réellement).
[[nodiscard]] Result<Bytes, String> ReadRange(sdl3::IOStream& stream, uint64_t offset,
													 uint64_t size);

// ============================================================================
// Chaînes
// ============================================================================

[[nodiscard]] Bytes Utf8ToUtf16Le(const String& text, bool nullTerminated = false);

[[nodiscard]] Bytes Utf8ToUtf16Be(const String& text);

/// Décode des unités UTF-16 (s'arrête au premier NUL).
[[nodiscard]] String Utf16ToUtf8(std::span<const uint8_t> bytes, bool bigEndian);

/// Code page 437 (noms zip sans drapeau UTF-8) vers UTF-8.
/// ISO 8859-1 (noms d'origine gzip) → UTF-8.
[[nodiscard]] String Latin1ToUtf8(const String& text);

[[nodiscard]] String Cp437ToUtf8(std::span<const uint8_t> bytes);

[[nodiscard]] bool IsAscii(const String& text);

// ============================================================================
// Dates
// ============================================================================

/// Jours depuis le 1970-01-01 d'une date civile (algorithme de H. Hinnant).
[[nodiscard]] constexpr int64_t DaysFromCivil(int64_t year, unsigned month, unsigned day) noexcept {
	year -= month <= 2 ? 1 : 0;
	const int64_t era = (year >= 0 ? year : year - 399) / 400;
	const unsigned yearOfEra = unsigned(year - era * 400);
	const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
	return era * 146097 + int64_t(dayOfEra) - 719468;
}

struct CivilTime {
	int64_t year = 1970;
	unsigned month = 1, day = 1, hour = 0, minute = 0, second = 0;
};

[[nodiscard]] constexpr CivilTime CivilFromUnix(int64_t seconds) noexcept {
	int64_t days = seconds >= 0 ? seconds / 86400 : (seconds - 86399) / 86400;
	int64_t rest = seconds - days * 86400;
	days += 719468;
	const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
	const unsigned dayOfEra = unsigned(days - era * 146097);
	const unsigned yearOfEra =
		(dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
	const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
	const unsigned mp = (5 * dayOfYear + 2) / 153;
	CivilTime civil;
	civil.day = dayOfYear - (153 * mp + 2) / 5 + 1;
	civil.month = mp < 10 ? mp + 3 : mp - 9;
	civil.year = int64_t(yearOfEra) + era * 400 + (civil.month <= 2 ? 1 : 0);
	civil.hour = unsigned(rest / 3600);
	civil.minute = unsigned(rest % 3600 / 60);
	civil.second = unsigned(rest % 60);
	return civil;
}

[[nodiscard]] constexpr int64_t UnixFromCivil(const CivilTime& civil) noexcept {
	return DaysFromCivil(civil.year, civil.month, civil.day) * 86400 + civil.hour * 3600 +
		   civil.minute * 60 + civil.second;
}

/// Date MS-DOS (zip) : pas de fuseau dans le format, interprétée en UTC.
[[nodiscard]] constexpr int64_t UnixFromDos(uint16_t date, uint16_t time) noexcept {
	CivilTime civil;
	civil.year = 1980 + (date >> 9);
	civil.month = (date >> 5) & 0x0F;
	civil.day = date & 0x1F;
	civil.hour = (time >> 11) & 0x1F;
	civil.minute = (time >> 5) & 0x3F;
	civil.second = unsigned((time & 0x1F) * 2);
	if (civil.month < 1 || civil.month > 12 || civil.day < 1)
		return 0;
	return UnixFromCivil(civil);
}

/// Date MS-DOS depuis un temps Unix (bornée à 1980–2107, précision 2 s).
constexpr void DosFromUnix(int64_t seconds, uint16_t& date, uint16_t& time) noexcept {
	CivilTime civil = CivilFromUnix(seconds);
	if (civil.year < 1980)
		civil = CivilTime{1980, 1, 1, 0, 0, 0};
	if (civil.year > 2107)
		civil = CivilTime{2107, 12, 31, 23, 59, 58};
	date = uint16_t(((civil.year - 1980) << 9) | (civil.month << 5) | civil.day);
	time = uint16_t((civil.hour << 11) | (civil.minute << 5) | (civil.second / 2));
}

/// FILETIME Windows (100 ns depuis 1601) ↔ secondes Unix.
inline constexpr uint64_t FILETIME_UNIX_EPOCH = 116444736000000000ull;
[[nodiscard]] constexpr int64_t UnixFromFileTime(uint64_t fileTime) noexcept {
	return int64_t(fileTime / 10000000ull) - int64_t(FILETIME_UNIX_EPOCH / 10000000ull);
}
[[nodiscard]] constexpr uint64_t FileTimeFromUnix(int64_t seconds) noexcept {
	return uint64_t(seconds + int64_t(FILETIME_UNIX_EPOCH / 10000000ull)) * 10000000ull;
}

/// Temps Unix courant (secondes), 0 si l'horloge est indisponible.
[[nodiscard]] int64_t CurrentUnixTime() noexcept;

} // namespace data::archive
