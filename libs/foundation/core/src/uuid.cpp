// Définitions de core/uuid.hpp
#include "core/uuid.hpp"

#include <random>

namespace {

/// Hachage FNV-1a sur 64 bits, avec une graine : deux passes de graines
/// différentes donnent les 128 bits d'un Uuid déterministe.
uint64_t Fnv1a(uint64_t seed, const uint8_t *data, size_t size) noexcept {
	uint64_t hash = 0xcbf29ce484222325ull ^ seed;
	for (size_t i = 0; i < size; ++i) {
		hash ^= data[i];
		hash *= 0x100000001b3ull;
	}
	// Mélange final (splitmix64) : FNV seul diffuse mal les derniers octets.
	hash ^= hash >> 30;
	hash *= 0xbf58476d1ce4e5b9ull;
	hash ^= hash >> 27;
	hash *= 0x94d049bb133111ebull;
	hash ^= hash >> 31;
	return hash;
}

/// Pose la version et la variante RFC 4122.
Uuid Stamp(Uuid id, int version) noexcept {
	id.high = (id.high & ~0xF000ull) | (uint64_t(version) << 12);
	id.low = (id.low & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
	return id;
}

int HexValue(char c) noexcept {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

} // namespace

Uuid Uuid::Generate() {
	thread_local std::mt19937_64 engine{std::random_device{}() ^ (uint64_t(std::random_device{}()) << 32)};
	Uuid id{engine(), engine()};
	id = Stamp(id, 4);
	return id.IsNil() ? Generate() : id;
}

Uuid Uuid::FromName(const Uuid &space, StringView name) noexcept {
	uint8_t prefix[16];
	for (int i = 0; i < 8; ++i) {
		prefix[i] = uint8_t(space.high >> (56 - 8 * i));
		prefix[8 + i] = uint8_t(space.low >> (56 - 8 * i));
	}
	const uint8_t *bytes = reinterpret_cast<const uint8_t *>(name.GetData());
	uint64_t high = Fnv1a(0x51ED270B27A9C4F1ull, prefix, 16);
	high = Fnv1a(high, bytes, name.GetSize());
	uint64_t low = Fnv1a(0x2545F4914F6CDD1Dull, prefix, 16);
	low = Fnv1a(low ^ high, bytes, name.GetSize());
	return Stamp(Uuid{high, low}, 5);
}

Option<Uuid> Uuid::Parse(StringView text) noexcept {
	Uuid id;
	int digits = 0;
	for (size_t i = 0; i < text.GetSize(); ++i) {
		const char c = text[i];
		if (c == '-')
			continue;
		const int value = HexValue(c);
		if (value < 0 || digits >= 32)
			return NONE;
		if (digits < 16)
			id.high = (id.high << 4) | uint64_t(value);
		else
			id.low = (id.low << 4) | uint64_t(value);
		++digits;
	}
	if (digits != 32)
		return NONE;
	return Some(id);
}

String Uuid::ToString() const {
	return String::Format("%08x-%04x-%04x-%04x-%012llx", unsigned(high >> 32), unsigned((high >> 16) & 0xFFFF),
						  unsigned(high & 0xFFFF), unsigned(low >> 48),
						  static_cast<unsigned long long>(low & 0xFFFFFFFFFFFFull));
}
