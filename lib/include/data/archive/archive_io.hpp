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
	[[nodiscard]] uint16_t U16LeBe() noexcept {
		uint16_t little = U16Le();
		uint16_t big = U16Be();
		if (little != big)
			m_ok = false;
		return little;
	}
	[[nodiscard]] uint32_t U32LeBe() noexcept {
		uint32_t little = U32Le();
		uint32_t big = U32Be();
		if (little != big)
			m_ok = false;
		return little;
	}

	/// Lit exactement `size` octets dans `out`.
	bool Read(void* out, size_t size) noexcept {
		if (!m_ok || !m_stream->ReadExact(out, size)) {
			m_ok = false;
			if (size > 0)
				std::memset(out, 0, size);
			return false;
		}
		return true;
	}

	/// `size` octets en vecteur ; refuse (échec) une taille supérieure à ce
	/// qu'il reste dans le flux — un champ de taille corrompu ne doit pas
	/// provoquer une allocation de plusieurs Go.
	[[nodiscard]] Bytes ReadBytes(uint64_t size) {
		// Taille du flux inconnue (flux décompressé) : plafond fixe.
		const uint64_t available = m_stream->GetSize() < 0 ? uint64_t(1) << 26 : Remaining();
		if (!m_ok || size > available) {
			m_ok = false;
			return {};
		}
		Bytes bytes(static_cast<size_t>(size));
		(void)Read(bytes.data(), bytes.size());
		return bytes;
	}

	/// Chaîne brute de `size` octets (sans conversion).
	[[nodiscard]] String ReadString(uint64_t size) {
		Bytes bytes = ReadBytes(size);
		return String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}

	bool Skip(uint64_t count) noexcept { return Seek(Tell() + count); }
	bool Seek(uint64_t position) noexcept {
		if (!m_ok || (m_stream->GetSize() >= 0 && position > Size()) ||
			m_stream->Seek(Sint64(position), SDL_IO_SEEK_SET) < 0)
			m_ok = false;
		return m_ok;
	}
	[[nodiscard]] uint64_t Tell() const noexcept {
		Sint64 position = m_stream->Tell();
		return position < 0 ? 0 : uint64_t(position);
	}
	[[nodiscard]] uint64_t Size() const noexcept {
		Sint64 size = m_stream->GetSize();
		return size < 0 ? 0 : uint64_t(size);
	}
	[[nodiscard]] uint64_t Remaining() const noexcept {
		uint64_t size = Size();
		uint64_t position = Tell();
		return position < size ? size - position : 0;
	}

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
	void U16LeBe(uint16_t v) noexcept {
		U16Le(v);
		U16Be(v);
	}
	void U32LeBe(uint32_t v) noexcept {
		U32Le(v);
		U32Be(v);
	}

	void Write(std::span<const uint8_t> bytes) noexcept {
		Check(m_stream->WriteExact(bytes.data(), bytes.size()));
	}
	void Write(const void* data, size_t size) noexcept { Check(m_stream->WriteExact(data, size)); }
	void WriteString(const String& text) noexcept { Write(text.CStr(), text.GetSize()); }
	/// Chaîne complétée par `pad` (ou tronquée) jusqu'à `width` octets.
	void WriteFixed(const String& text, size_t width, uint8_t pad = ' ') noexcept {
		size_t length = text.GetSize() < width ? text.GetSize() : width;
		Write(text.CStr(), length);
		Fill(width - length, pad);
	}
	void Fill(uint64_t count, uint8_t value = 0) noexcept {
		uint8_t block[512];
		std::memset(block, value, sizeof(block));
		while (count > 0 && m_ok) {
			size_t chunk = count < sizeof(block) ? size_t(count) : sizeof(block);
			Write(block, chunk);
			count -= chunk;
		}
	}
	/// Complète jusqu'à un multiple de `alignment` octets.
	void AlignTo(uint64_t alignment, uint8_t value = 0) noexcept {
		uint64_t remainder = Tell() % alignment;
		if (remainder != 0)
			Fill(alignment - remainder, value);
	}

	bool Seek(uint64_t position) noexcept {
		Check(m_stream->Seek(Sint64(position), SDL_IO_SEEK_SET) >= 0);
		return m_ok;
	}
	[[nodiscard]] uint64_t Tell() const noexcept {
		Sint64 position = m_stream->Tell();
		return position < 0 ? 0 : uint64_t(position);
	}
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
	[[nodiscard]] static Result<MemoryStream, String> FromBytes(Bytes bytes) {
		MemoryStream memory;
		memory.m_bytes = std::make_unique<Bytes>(std::move(bytes));
		// FromConstMemory refuse un tampon nul : un octet factice pour le cas vide.
		static const uint8_t EMPTY = 0;
		const uint8_t* data = memory.m_bytes->empty() ? &EMPTY : memory.m_bytes->data();
		auto stream = sdl3::IOStream::FromConstMemory(data, memory.m_bytes->size());
		if (stream.IsError())
			return Err(
				String::Format("flux mémoire impossible : %s", String(stream.Error()).CStr()));
		memory.m_stream = std::move(stream).Unwrap();
		return Ok(std::move(memory));
	}

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
	[[nodiscard]] static Result<ArchiveSource, String> FromFile(const String& path) {
		auto stream = sdl3::IOStream::FromFile(path, "rb");
		if (stream.IsError())
			return Err(String::Format("ouverture de %s impossible : %s", path.CStr(),
									  String(stream.Error()).CStr()));
		ArchiveSource source;
		source.m_file = Some(std::move(stream).Unwrap());
		return Ok(std::move(source));
	}
	[[nodiscard]] static Result<ArchiveSource, String> FromBytes(Bytes bytes) {
		auto memory = MemoryStream::FromBytes(std::move(bytes));
		if (memory.IsError())
			return Err(memory.Error());
		ArchiveSource source;
		source.m_memory = Some(std::move(memory).Unwrap());
		return Ok(std::move(source));
	}
	[[nodiscard]] sdl3::IOStream& Stream() noexcept {
		return m_file.IsSome() ? m_file.Value() : m_memory.Value().Stream();
	}
	[[nodiscard]] uint64_t Size() noexcept {
		Sint64 size = Stream().GetSize();
		return size < 0 ? 0 : uint64_t(size);
	}

private:
	ArchiveSource() = default;
	Option<sdl3::IOStream> m_file = NONE;
	Option<MemoryStream> m_memory = NONE;
};

/// Flux en lecture sur des octets NON possédés (qui doivent survivre au flux).
[[nodiscard]] inline Result<sdl3::IOStream, String> ViewStream(std::span<const uint8_t> bytes) {
	static const uint8_t EMPTY = 0;
	auto stream =
		sdl3::IOStream::FromConstMemory(bytes.empty() ? &EMPTY : bytes.data(), bytes.size());
	if (stream.IsError())
		return Err(String::Format("flux mémoire impossible : %s", String(stream.Error()).CStr()));
	return Ok(std::move(stream).Unwrap());
}

/// Flux d'écriture en mémoire (croît à la demande).
[[nodiscard]] inline Result<sdl3::IOStream, String> CreateMemoryWriter() {
	auto stream = sdl3::IOStream::FromDynamicMemory();
	if (stream.IsError())
		return Err(String::Format("flux mémoire impossible : %s", String(stream.Error()).CStr()));
	return Ok(std::move(stream).Unwrap());
}

/// Lit `size` octets à partir de `offset`, bornes vérifiées contre la taille
/// du flux (aucune allocation au-delà de ce qui existe réellement).
[[nodiscard]] inline Result<Bytes, String> ReadRange(sdl3::IOStream& stream, uint64_t offset,
													 uint64_t size) {
	Sint64 total = stream.GetSize();
	if (total < 0 || offset > uint64_t(total) || size > uint64_t(total) - offset)
		return Err(
			String::Format("lecture hors du fichier (%llu octets à %llu, fichier de %lld octets)",
						   static_cast<unsigned long long>(size),
						   static_cast<unsigned long long>(offset), static_cast<long long>(total)));
	if (stream.Seek(Sint64(offset), SDL_IO_SEEK_SET) < 0)
		return Err(String("positionnement impossible dans le flux"));
	Bytes bytes(static_cast<size_t>(size));
	if (!stream.ReadExact(bytes.data(), bytes.size()))
		return Err(String("flux tronqué"));
	return Ok(std::move(bytes));
}

// ============================================================================
// Chaînes
// ============================================================================

[[nodiscard]] inline Bytes Utf8ToUtf16Le(const String& text, bool nullTerminated = false) {
	std::u16string units = unicode::ToUtf16(text.View());
	Bytes out;
	out.reserve((units.size() + 1) * 2);
	for (char16_t unit : units) {
		out.push_back(uint8_t(unit & 0xFF));
		out.push_back(uint8_t(unit >> 8));
	}
	if (nullTerminated) {
		out.push_back(0);
		out.push_back(0);
	}
	return out;
}

[[nodiscard]] inline Bytes Utf8ToUtf16Be(const String& text) {
	std::u16string units = unicode::ToUtf16(text.View());
	Bytes out;
	out.reserve(units.size() * 2);
	for (char16_t unit : units) {
		out.push_back(uint8_t(unit >> 8));
		out.push_back(uint8_t(unit & 0xFF));
	}
	return out;
}

/// Décode des unités UTF-16 (s'arrête au premier NUL).
[[nodiscard]] inline String Utf16ToUtf8(std::span<const uint8_t> bytes, bool bigEndian) {
	std::u16string units;
	units.reserve(bytes.size() / 2);
	for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
		char16_t unit = bigEndian ? char16_t((bytes[i] << 8) | bytes[i + 1])
								  : char16_t(bytes[i] | (bytes[i + 1] << 8));
		if (unit == 0)
			break;
		units.push_back(unit);
	}
	return String(unicode::FromUtf16(units));
}

/// Code page 437 (noms zip sans drapeau UTF-8) vers UTF-8.
/// ISO 8859-1 (noms d'origine gzip) → UTF-8.
[[nodiscard]] inline String Latin1ToUtf8(const String& text) {
	String out;
	for (size_t i = 0; i < text.GetSize(); ++i) {
		const uint8_t c = uint8_t(text.CharAt(i));
		if (c < 0x80) {
			out.PushBack(char(c));
		} else {
			out.PushBack(char(0xC0 | (c >> 6)));
			out.PushBack(char(0x80 | (c & 0x3F)));
		}
	}
	return out;
}

[[nodiscard]] inline String Cp437ToUtf8(std::span<const uint8_t> bytes) {
	static constexpr char16_t HIGH[128] = {
		0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8,
		0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5, 0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2,
		0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, 0x00E1,
		0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD,
		0x00BC, 0x00A1, 0x00AB, 0x00BB, 0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562,
		0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, 0x2514, 0x2534,
		0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560,
		0x2550, 0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
		0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580, 0x03B1, 0x00DF, 0x0393,
		0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6,
		0x03B5, 0x2229, 0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0,
		0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};
	std::u16string units;
	units.reserve(bytes.size());
	for (uint8_t byte : bytes)
		units.push_back(byte < 0x80 ? char16_t(byte) : HIGH[byte - 0x80]);
	return String(unicode::FromUtf16(units));
}

[[nodiscard]] inline bool IsAscii(const String& text) {
	for (size_t i = 0; i < text.GetSize(); ++i)
		if (uint8_t(text.CharAt(i)) >= 0x80)
			return false;
	return true;
}

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
[[nodiscard]] inline int64_t CurrentUnixTime() noexcept {
	SDL_Time now = 0;
	if (!SDL_GetCurrentTime(&now))
		return 0;
	return int64_t(now / 1000000000LL);
}

} // namespace data::archive
