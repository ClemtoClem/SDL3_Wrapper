// Définitions de data/archive/archive_rar.hpp
#include "data/archive/archive_rar.hpp"

namespace data::archive {

namespace detail::rar {

uint32_t Get4(const uint8_t* p) noexcept {
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

void Put4(uint32_t v, uint8_t* p) noexcept {
	p[0] = uint8_t(v);
	p[1] = uint8_t(v >> 8);
	p[2] = uint8_t(v >> 16);
	p[3] = uint8_t(v >> 24);
}

uint32_t Rotl32(uint32_t v, unsigned n) noexcept {
	return (v << n) | (v >> ((32 - n) & 31));
}

uint32_t Rotr32(uint32_t v, unsigned n) noexcept {
	return (v >> n) | (v << ((32 - n) & 31));
}

const std::array<uint32_t, 256>& CrcTable() {
	static const std::array<uint32_t, 256> TABLE = [] {
		std::array<uint32_t, 256> table{};
		for (uint32_t i = 0; i < 256; ++i) {
			uint32_t c = i;
			for (int k = 0; k < 8; ++k)
				c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
			table[i] = c;
		}
		return table;
	}();
	return TABLE;
}

// ── Blake2s ──────────────────────────────────────────────────────────────────

void Blake2s::Init(uint32_t nodeOffset, uint32_t nodeDepth, bool lastNode) {
	static constexpr uint32_t IV[8] = {0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
									   0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u};
	for (int i = 0; i < 8; ++i)
		m_h[i] = IV[i];
	m_h[0] ^= 0x02080020u; // paramètres BLAKE2sp
	m_h[2] ^= nodeOffset;
	m_h[3] ^= (nodeDepth << 16) | 0x20000000u;
	m_t[0] = m_t[1] = m_f[0] = m_f[1] = 0;
	m_length = 0;
	m_lastNode = lastNode;
}

void Blake2s::Update(const uint8_t* in, size_t size) {
	while (size > 0) {
		const size_t fill = 2 * BLOCK - m_length;
		if (size > fill) {
			std::memcpy(m_buffer + m_length, in, fill);
			m_length += fill;
			Increment(BLOCK);
			Compress(m_buffer);
			std::memcpy(m_buffer, m_buffer + BLOCK, BLOCK);
			m_length -= BLOCK;
			in += fill;
			size -= fill;
		} else {
			std::memcpy(m_buffer + m_length, in, size);
			m_length += size;
			size = 0;
		}
	}
}

void Blake2s::Final(uint8_t* digest) {
	if (m_length > BLOCK) {
		Increment(BLOCK);
		Compress(m_buffer);
		m_length -= BLOCK;
		std::memmove(m_buffer, m_buffer + BLOCK, m_length);
	}
	Increment(uint32_t(m_length));
	if (m_lastNode)
		m_f[1] = ~0u;
	m_f[0] = ~0u;
	std::memset(m_buffer + m_length, 0, 2 * BLOCK - m_length);
	Compress(m_buffer);
	for (int i = 0; i < 8; ++i)
		Put4(m_h[i], digest + 4 * i);
}

void Blake2s::Increment(uint32_t increment) {
	m_t[0] += increment;
	m_t[1] += m_t[0] < increment;
}

void Blake2s::Compress(const uint8_t* block) {
	static constexpr uint32_t IV[8] = {0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
									   0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u};
	static constexpr uint8_t SIGMA[10][16] = {
		{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
		{14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
		{11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
		{7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
		{9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
		{2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
		{12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
		{13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
		{6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
		{10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0}};
	uint32_t m[16], v[16];
	for (int i = 0; i < 16; ++i)
		m[i] = Get4(block + 4 * i);
	for (int i = 0; i < 8; ++i) {
		v[i] = m_h[i];
		v[i + 8] = IV[i];
	}
	v[12] ^= m_t[0];
	v[13] ^= m_t[1];
	v[14] ^= m_f[0];
	v[15] ^= m_f[1];
	auto g = [&](int r, int i, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
		a = a + b + m[SIGMA[r][2 * i]];
		d = Rotr32(d ^ a, 16);
		c = c + d;
		b = Rotr32(b ^ c, 12);
		a = a + b + m[SIGMA[r][2 * i + 1]];
		d = Rotr32(d ^ a, 8);
		c = c + d;
		b = Rotr32(b ^ c, 7);
	};
	for (int r = 0; r < 10; ++r) {
		g(r, 0, v[0], v[4], v[8], v[12]);
		g(r, 1, v[1], v[5], v[9], v[13]);
		g(r, 2, v[2], v[6], v[10], v[14]);
		g(r, 3, v[3], v[7], v[11], v[15]);
		g(r, 4, v[0], v[5], v[10], v[15]);
		g(r, 5, v[1], v[6], v[11], v[12]);
		g(r, 6, v[2], v[7], v[8], v[13]);
		g(r, 7, v[3], v[4], v[9], v[14]);
	}
	for (int i = 0; i < 8; ++i)
		m_h[i] ^= v[i] ^ v[i + 8];
}

// ── Blake2sp ─────────────────────────────────────────────────────────────────

void Blake2sp::Reset() {
	m_root.Init(0, 1, true);
	for (uint32_t i = 0; i < DEGREE; ++i)
		m_leaves[i].Init(i, 0, i == DEGREE - 1);
	m_length = 0;
}

void Blake2sp::Update(std::span<const uint8_t> data) {
	const uint8_t* in = data.data();
	size_t size = data.size();
	size_t left = m_length;
	const size_t fill = sizeof(m_buffer) - left;
	if (left > 0 && size >= fill) {
		std::memcpy(m_buffer + left, in, fill);
		for (size_t i = 0; i < DEGREE; ++i)
			m_leaves[i].Update(m_buffer + i * Blake2s::BLOCK, Blake2s::BLOCK);
		in += fill;
		size -= fill;
		left = 0;
	}
	for (size_t i = 0; i < DEGREE; ++i) {
		const uint8_t* at = in + i * Blake2s::BLOCK;
		for (size_t remaining = size; remaining >= DEGREE * Blake2s::BLOCK;
			 remaining -= DEGREE * Blake2s::BLOCK, at += DEGREE * Blake2s::BLOCK)
			m_leaves[i].Update(at, Blake2s::BLOCK);
	}
	in += size - size % (DEGREE * Blake2s::BLOCK);
	size %= DEGREE * Blake2s::BLOCK;
	if (size > 0)
		std::memcpy(m_buffer + left, in, size);
	m_length = left + size;
}

std::array<uint8_t, 32> Blake2sp::Final() {
	uint8_t hashes[DEGREE][32];
	for (size_t i = 0; i < DEGREE; ++i) {
		if (m_length > i * Blake2s::BLOCK)
			m_leaves[i].Update(m_buffer + i * Blake2s::BLOCK,
							   std::min<size_t>(m_length - i * Blake2s::BLOCK, Blake2s::BLOCK));
		m_leaves[i].Final(hashes[i]);
	}
	for (size_t i = 0; i < DEGREE; ++i)
		m_root.Update(hashes[i], 32);
	std::array<uint8_t, 32> digest{};
	m_root.Final(digest.data());
	return digest;
}

// ── Rar29Sha1 ────────────────────────────────────────────────────────────────

void Rar29Sha1::Update(const uint8_t* data, size_t size) {
	Process(const_cast<uint8_t*>(data), size, false);
}

std::array<uint32_t, 5> Rar29Sha1::Digest() const {
	Rar29Sha1 copy = *this;
	const uint64_t bits = copy.m_count * 8;
	uint8_t padding[72] = {0x80};
	const size_t used = size_t(copy.m_count & 63);
	const size_t padLength = used < 56 ? 56 - used : 120 - used;
	copy.Update(padding, padLength);
	uint8_t length[8];
	for (int i = 0; i < 8; ++i)
		length[i] = uint8_t(bits >> (56 - 8 * i));
	copy.Update(length, 8);
	return {copy.m_state[0], copy.m_state[1], copy.m_state[2], copy.m_state[3],
			copy.m_state[4]};
}

void Rar29Sha1::Process(uint8_t* data, size_t size, bool rar29) {
	size_t j = size_t(m_count & 63), i = 0;
	m_count += size;
	if (j + size > 63) {
		i = 64 - j;
		std::memcpy(m_buffer + j, data, i);
		uint32_t workspace[16];
		Transform(m_buffer, workspace);
		for (; i + 63 < size; i += 64) {
			Transform(data + i, workspace);
			if (rar29)
				for (int k = 0; k < 16; ++k)
					Put4(workspace[k], data + i + size_t(k) * 4);
		}
		j = 0;
	}
	if (size > i)
		std::memcpy(m_buffer + j, data + i, size - i);
}

void Rar29Sha1::Transform(const uint8_t* block, uint32_t w[16]) {
	for (int t = 0; t < 16; ++t)
		w[t] = (uint32_t(block[4 * t]) << 24) | (uint32_t(block[4 * t + 1]) << 16) |
			   (uint32_t(block[4 * t + 2]) << 8) | block[4 * t + 3];
	uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3], e = m_state[4];
	for (int t = 0; t < 80; ++t) {
		if (t >= 16)
			w[t & 15] =
				Rotl32(w[(t + 13) & 15] ^ w[(t + 8) & 15] ^ w[(t + 2) & 15] ^ w[t & 15], 1);
		uint32_t f, k;
		if (t < 20) {
			f = (b & (c ^ d)) ^ d;
			k = 0x5A827999u;
		} else if (t < 40) {
			f = b ^ c ^ d;
			k = 0x6ED9EBA1u;
		} else if (t < 60) {
			f = ((b | c) & d) | (b & c);
			k = 0x8F1BBCDCu;
		} else {
			f = b ^ c ^ d;
			k = 0xCA62C1D6u;
		}
		const uint32_t temp = Rotl32(a, 5) + f + e + k + w[t & 15];
		e = d;
		d = c;
		c = Rotl32(b, 30);
		b = a;
		a = temp;
	}
	m_state[0] += a;
	m_state[1] += b;
	m_state[2] += c;
	m_state[3] += d;
	m_state[4] += e;
}

Rar3Key DeriveRar3Key(const String& password, const uint8_t* salt) {
	Bytes raw = Utf8ToUtf16Le(password);
	raw.resize(std::min(raw.size(), MAX_PASSWORD * 2));
	if (salt != nullptr)
		raw.insert(raw.end(), salt, salt + 8);
	Rar29Sha1 sha;
	static constexpr uint32_t ROUNDS = 0x40000;
	Rar3Key result;
	for (uint32_t i = 0; i < ROUNDS; ++i) {
		sha.UpdateRar29(raw.data(), raw.size());
		const uint8_t number[3] = {uint8_t(i), uint8_t(i >> 8), uint8_t(i >> 16)};
		sha.Update(number, 3);
		if (i % (ROUNDS / 16) == 0)
			result.iv[i / (ROUNDS / 16)] = uint8_t(sha.Digest()[4]);
	}
	const auto digest = sha.Digest();
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			result.key[size_t(i * 4 + j)] = uint8_t(digest[size_t(i)] >> (j * 8));
	return result;
}

Rar5Key DeriveRar5Key(const String& password, std::span<const uint8_t> salt, uint32_t lg2) {
	// 127 caractères au plus, comme RAR.
	String utf8;
	size_t characters = 0;
	for (size_t i = 0; i < password.GetSize(); ++i) {
		const uint8_t c = uint8_t(password.CharAt(i));
		if ((c & 0xC0) != 0x80 && characters++ == MAX_PASSWORD)
			break;
		utf8.PushBack(char(c));
	}
	const std::span<const uint8_t> pw(reinterpret_cast<const uint8_t*>(utf8.CStr()),
									  utf8.GetSize());
	const Hmac<Sha256> keyed(pw);
	Bytes first(salt.begin(), salt.end());
	first.insert(first.end(), {0, 0, 0, 1});
	Hmac<Sha256> mac = keyed;
	mac.Update(first);
	std::array<uint8_t, 32> u = mac.Final();
	std::array<uint8_t, 32> f = u;
	Rar5Key result;
	const uint32_t counts[3] = {(1u << lg2) - 1, 16, 16};
	std::array<uint8_t, 32> values[3];
	for (int stage = 0; stage < 3; ++stage) {
		for (uint32_t n = 0; n < counts[stage]; ++n) {
			Hmac<Sha256> round = keyed;
			round.Update(u);
			u = round.Final();
			for (size_t k = 0; k < 32; ++k)
				f[k] ^= u[k];
		}
		values[stage] = f;
	}
	result.key = values[0];
	result.hashKey = values[1];
	for (size_t i = 0; i < 32; ++i)
		result.check[i % 8] ^= values[2][i];
	return result;
}

uint32_t Rar5CrcToMac(uint32_t crc, const std::array<uint8_t, 32>& hashKey) {
	uint8_t raw[4];
	Put4(crc, raw);
	Hmac<Sha256> mac(hashKey);
	mac.Update(std::span<const uint8_t>(raw, 4));
	const auto digest = mac.Final();
	uint32_t result = 0;
	for (size_t i = 0; i < digest.size(); ++i)
		result ^= uint32_t(digest[i]) << ((i & 3) * 8);
	return result;
}

std::array<uint8_t, 32> Rar5DigestToMac(const std::array<uint8_t, 32>& digest, const std::array<uint8_t, 32>& hashKey) {
	Hmac<Sha256> mac(hashKey);
	mac.Update(digest);
	return mac.Final();
}

// ── KeyCache ─────────────────────────────────────────────────────────────────

const Rar3Key& KeyCache::Rar3(const String& password, const uint8_t* salt) {
	const Bytes saltBytes = salt != nullptr ? Bytes(salt, salt + 8) : Bytes();
	for (const auto& entry : m_rar3)
		if (entry.password == password && entry.salt == saltBytes)
			return entry.key;
	if (m_rar3.size() >= 8)
		m_rar3.erase(m_rar3.begin());
	m_rar3.push_back({password, saltBytes, DeriveRar3Key(password, salt)});
	return m_rar3.back().key;
}

const Rar5Key& KeyCache::Rar5(const String& password, std::span<const uint8_t> salt, uint32_t lg2) {
	for (const auto& entry : m_rar5)
		if (entry.password == password && entry.lg2 == lg2 &&
			entry.salt.size() == salt.size() &&
			std::equal(salt.begin(), salt.end(), entry.salt.begin()))
			return entry.key;
	if (m_rar5.size() >= 8)
		m_rar5.erase(m_rar5.begin());
	m_rar5.push_back(
		{password, Bytes(salt.begin(), salt.end()), lg2, DeriveRar5Key(password, salt, lg2)});
	return m_rar5.back().key;
}

// ── LegacyDecryptor ──────────────────────────────────────────────────────────

LegacyDecryptor::LegacyDecryptor(LegacyCipher cipher, const String& password) : m_cipher(cipher) {
	std::string pw(password.CStr(), std::min(password.GetSize(), MAX_PASSWORD));
	const auto& crc = CrcTable();
	switch (cipher) {
	case LegacyCipher::RAR13:
		for (unsigned char p : pw) {
			m_key13[0] = uint8_t(m_key13[0] + p);
			m_key13[1] ^= p;
			m_key13[2] = uint8_t(m_key13[2] + p);
			m_key13[2] = uint8_t((m_key13[2] << 1) | (m_key13[2] >> 7));
		}
		break;
	case LegacyCipher::RAR15: {
		// CRC32(0xffffffff, …) d'UnRAR : le CRC sans l'inversion finale.
		const uint32_t pswCrc = ~Crc32(
			std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(pw.data()), pw.size()));
		m_key15[0] = uint16_t(pswCrc & 0xFFFF);
		m_key15[1] = uint16_t(pswCrc >> 16);
		for (unsigned char p : pw) {
			m_key15[2] = uint16_t(m_key15[2] ^ p ^ crc[p]);
			m_key15[3] = uint16_t(m_key15[3] + uint16_t(p + (crc[p] >> 16)));
		}
		break;
	}
	case LegacyCipher::RAR20: {
		static constexpr uint8_t INIT_SUBST[256] = {
			215, 19,  149, 35,	73,	 197, 192, 205, 249, 28,  16,  119, 48,	 221, 2,   42,
			232, 1,	  177, 233, 14,	 88,  219, 25,	223, 195, 244, 90,	87,	 239, 153, 137,
			255, 199, 147, 70,	92,	 66,  246, 13,	216, 40,  62,  29,	217, 230, 86,  6,
			71,	 24,  171, 196, 101, 113, 218, 123, 93,	 91,  163, 178, 202, 67,  44,  235,
			107, 250, 75,  234, 49,	 167, 125, 211, 83,	 114, 157, 144, 32,	 193, 143, 36,
			158, 124, 247, 187, 89,	 214, 141, 47,	121, 228, 61,  130, 213, 194, 174, 251,
			97,	 110, 54,  229, 115, 57,  152, 94,	105, 243, 212, 55,	209, 245, 63,  11,
			164, 200, 31,  156, 81,	 176, 227, 21,	76,	 99,  139, 188, 127, 17,  248, 51,
			207, 120, 189, 210, 8,	 226, 41,  72,	183, 203, 135, 165, 166, 60,  98,  7,
			122, 38,  155, 170, 69,	 172, 252, 238, 39,	 134, 59,  128, 236, 27,  240, 80,
			131, 3,	  85,  206, 145, 79,  154, 142, 159, 220, 201, 133, 74,	 64,  20,  129,
			224, 185, 138, 103, 173, 182, 43,  34,	254, 82,  198, 151, 231, 180, 58,  10,
			118, 26,  102, 12,	50,	 132, 22,  191, 136, 111, 162, 179, 45,	 4,	  148, 108,
			161, 56,  78,  126, 242, 222, 15,  175, 146, 23,  33,  241, 181, 190, 77,  225,
			0,	 46,  169, 186, 68,	 95,  237, 65,	53,	 208, 253, 168, 9,	 18,  100, 52,
			116, 184, 160, 96,	109, 37,  30,  106, 140, 104, 150, 5,	204, 117, 112, 84};
		std::memcpy(m_subst, INIT_SUBST, 256);
		m_key20[0] = 0xD3A3B879u;
		m_key20[1] = 0x3F6D12F7u;
		m_key20[2] = 0x7515A235u;
		m_key20[3] = 0xA4E7F123u;
		std::vector<uint8_t> psw(pw.begin(), pw.end());
		psw.resize((pw.size() | 15) + 2,
				   0); // Password[I+1] peut lire le NUL final
		for (uint32_t j = 0; j < 256; ++j)
			for (size_t i = 0; i < pw.size(); i += 2) {
				uint32_t n1 = uint8_t(crc[(psw[i] - j) & 0xFF]);
				const uint32_t n2 = uint8_t(crc[(psw[i + 1] + j) & 0xFF]);
				for (uint32_t k = 1; n1 != n2; n1 = (n1 + 1) & 0xFF, ++k)
					std::swap(m_subst[n1], m_subst[(n1 + i + k) & 0xFF]);
			}
		// Le dernier bloc incomplet du mot de passe est complété par des zéros.
		if ((pw.size() & 15) != 0)
			for (size_t i = pw.size(); i <= (pw.size() | 15); ++i)
				psw[i] = 0;
		for (size_t i = 0; i < pw.size(); i += 16)
			EncryptBlock20(psw.data() + i);
		break;
	}
	}
}

size_t LegacyDecryptor::BlockSize() const noexcept {
	return m_cipher == LegacyCipher::RAR20 ? 16 : 1;
}

void LegacyDecryptor::Decrypt(uint8_t* data, size_t size) {
	const auto& crc = CrcTable();
	switch (m_cipher) {
	case LegacyCipher::RAR13:
		while (size--) {
			m_key13[1] = uint8_t(m_key13[1] + m_key13[2]);
			m_key13[0] = uint8_t(m_key13[0] + m_key13[1]);
			*data = uint8_t(*data - m_key13[0]);
			++data;
		}
		break;
	case LegacyCipher::RAR15:
		while (size--) {
			m_key15[0] = uint16_t(m_key15[0] + 0x1234);
			m_key15[1] = uint16_t(m_key15[1] ^ crc[(m_key15[0] & 0x1FE) >> 1]);
			m_key15[2] = uint16_t(m_key15[2] - uint16_t(crc[(m_key15[0] & 0x1FE) >> 1] >> 16));
			m_key15[0] ^= m_key15[2];
			m_key15[3] = uint16_t(Rotr16(m_key15[3]) ^ m_key15[1]);
			m_key15[3] = Rotr16(m_key15[3]);
			m_key15[0] ^= m_key15[3];
			*data ^= uint8_t(m_key15[0] >> 8);
			++data;
		}
		break;
	case LegacyCipher::RAR20:
		for (size_t at = 0; at + 16 <= size; at += 16)
			DecryptBlock20(data + at);
		break;
	}
}

uint16_t LegacyDecryptor::Rotr16(uint16_t v) noexcept {
	return uint16_t((v >> 1) | (v << 15));
}

uint32_t LegacyDecryptor::Subst(uint32_t t) const noexcept {
	return uint32_t(m_subst[t & 255]) | (uint32_t(m_subst[(t >> 8) & 255]) << 8) |
		   (uint32_t(m_subst[(t >> 16) & 255]) << 16) |
		   (uint32_t(m_subst[(t >> 24) & 255]) << 24);
}

void LegacyDecryptor::Rounds20(uint8_t* buf, bool decrypt) {
	uint32_t a = Get4(buf) ^ m_key20[0], b = Get4(buf + 4) ^ m_key20[1],
			 c = Get4(buf + 8) ^ m_key20[2], d = Get4(buf + 12) ^ m_key20[3];
	for (int n = 0; n < 32; ++n) {
		const int i = decrypt ? 31 - n : n;
		uint32_t t = (c + Rotl32(d, 11)) ^ m_key20[i & 3];
		const uint32_t ta = a ^ Subst(t);
		t = (d ^ Rotl32(c, 17)) + m_key20[i & 3];
		const uint32_t tb = b ^ Subst(t);
		a = c;
		b = d;
		c = ta;
		d = tb;
	}
	Put4(c ^ m_key20[0], buf);
	Put4(d ^ m_key20[1], buf + 4);
	Put4(a ^ m_key20[2], buf + 8);
	Put4(b ^ m_key20[3], buf + 12);
}

void LegacyDecryptor::UpdateKeys20(const uint8_t* buf) {
	const auto& crc = CrcTable();
	for (int i = 0; i < 16; i += 4) {
		m_key20[0] ^= crc[buf[i]];
		m_key20[1] ^= crc[buf[i + 1]];
		m_key20[2] ^= crc[buf[i + 2]];
		m_key20[3] ^= crc[buf[i + 3]];
	}
}

void LegacyDecryptor::EncryptBlock20(uint8_t* buf) {
	Rounds20(buf, false);
	UpdateKeys20(buf);
}

void LegacyDecryptor::DecryptBlock20(uint8_t* buf) {
	uint8_t cipher[16];
	std::memcpy(cipher, buf, 16);
	Rounds20(buf, true);
	UpdateKeys20(cipher);
}

// ── LegacyDecryptImpl ────────────────────────────────────────────────────────

Result<size_t, ArchiveError> LegacyDecryptImpl::Produce(uint8_t* out, size_t max) {
	const size_t block = m_current.BlockSize();
	if (max < block) {
		if (m_position == m_end) {
			auto got = StreamRead(m_inner, m_spare, block);
			if (got.IsError())
				return got;
			m_current.Decrypt(m_spare, got.Value());
			m_position = 0;
			m_end = got.Value();
		}
	}
	if (m_position < m_end) {
		const size_t take = std::min(max, m_end - m_position);
		std::memcpy(out, m_spare + m_position, take);
		m_position += take;
		return Ok(take);
	}
	auto got = StreamRead(m_inner, out, max / block * block);
	if (got.IsError())
		return got;
	m_current.Decrypt(out, got.Value());
	return got;
}

bool LegacyDecryptImpl::Restart() {
	if (m_inner.io.Seek(0, SDL_IO_SEEK_SET) != 0)
		return false;
	m_current = m_initial;
	m_position = m_end = 0;
	return true;
}

// ── BitInput ─────────────────────────────────────────────────────────────────

void BitInput::AddBits(uint32_t bits) noexcept {
	bits += uint32_t(inBit);
	inAddr += int(bits >> 3);
	inBit = int(bits & 7);
}

uint32_t BitInput::GetBits() const noexcept {
	const uint8_t* p = m_buffer.data() + inAddr;
	const uint32_t field = (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
	return (field >> (8 - inBit)) & 0xFFFF;
}

uint32_t BitInput::GetBits32() const noexcept {
	const uint8_t* p = m_buffer.data() + inAddr;
	uint32_t field =
		(uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
	field <<= inBit;
	field |= uint32_t(p[4]) >> (8 - inBit);
	return field;
}

uint64_t BitInput::GetBits64() const noexcept {
	const uint8_t* p = m_buffer.data() + inAddr;
	uint64_t field = 0;
	for (int i = 0; i < 8; ++i)
		field = (field << 8) | p[i];
	field <<= inBit;
	field |= uint64_t(p[8]) >> (8 - inBit);
	return field;
}

bool BitInput::Overflow(uint32_t increment) const noexcept {
	return inAddr + int(increment) >= MAX_SIZE;
}

// ── RarVm ────────────────────────────────────────────────────────────────────

void RarVm::Init() {
	if (m_mem.empty())
		m_mem.assign(VM_MEMSIZE + 4, 0);
}

void RarVm::Prepare(const uint8_t* code, uint32_t size, PreparedProgram& program) {
	uint8_t xorSum = 0;
	for (uint32_t i = 1; i < size; ++i)
		xorSum ^= code[i];
	if (size == 0 || xorSum != code[0])
		return;
	struct Standard {
		uint32_t length, crc;
		StandardFilter type;
	};
	static constexpr Standard LIST[] = {
		{53, 0xAD576887u, StandardFilter::E8},		 {57, 0x3CD7E57Eu, StandardFilter::E8E9},
		{120, 0x3769893Fu, StandardFilter::ITANIUM}, {29, 0x0E06077Du, StandardFilter::DELTA},
		{149, 0x1C2C5DC8u, StandardFilter::RGB},	 {216, 0xBC85E701u, StandardFilter::AUDIO}};
	const uint32_t crc = Crc32(std::span<const uint8_t>(code, size));
	for (const Standard& item : LIST)
		if (item.crc == crc && item.length == size) {
			program.type = item.type;
			break;
		}
}

uint32_t RarVm::ReadData(BitInput& input) {
	uint32_t data = input.GetBits();
	switch (data & 0xC000) {
	case 0:
		input.AddBits(6);
		return (data >> 10) & 0xF;
	case 0x4000:
		if ((data & 0x3C00) == 0) {
			data = 0xFFFFFF00u | ((data >> 2) & 0xFF);
			input.AddBits(14);
		} else {
			data = (data >> 6) & 0xFF;
			input.AddBits(10);
		}
		return data;
	case 0x8000:
		input.AddBits(2);
		data = input.GetBits();
		input.AddBits(16);
		return data;
	default:
		input.AddBits(2);
		data = input.GetBits() << 16;
		input.AddBits(16);
		data |= input.GetBits();
		input.AddBits(16);
		return data;
	}
}

void RarVm::SetMemory(size_t position, const uint8_t* data, size_t size) {
	if (position < VM_MEMSIZE && data != m_mem.data() + position) {
		const size_t copy = std::min(size, size_t(VM_MEMSIZE) - position);
		if (copy != 0)
			std::memmove(m_mem.data() + position, data, copy);
	}
}

void RarVm::Execute(PreparedProgram& program) {
	std::memcpy(m_r, program.initR, sizeof(program.initR));
	program.filteredData = nullptr;
	if (program.type != StandardFilter::NONE) {
		const bool success = ExecuteStandard(program.type);
		const uint32_t blockSize = program.initR[4] & VM_MEMMASK;
		program.filteredDataSize = blockSize;
		if (program.type == StandardFilter::DELTA || program.type == StandardFilter::RGB ||
			program.type == StandardFilter::AUDIO)
			program.filteredData = 2 * blockSize > VM_MEMSIZE || !success
									   ? m_mem.data()
									   : m_mem.data() + blockSize;
		else
			program.filteredData = m_mem.data();
	}
}

bool RarVm::ExecuteStandard(StandardFilter type) {
	uint8_t* mem = m_mem.data();
	switch (type) {
	case StandardFilter::E8:
	case StandardFilter::E8E9: {
		uint8_t* data = mem;
		const uint32_t dataSize = m_r[4], fileOffset = m_r[6];
		if (dataSize > VM_MEMSIZE || dataSize < 4)
			return false;
		const uint32_t fileSize = 0x1000000;
		const uint8_t cmpByte2 = type == StandardFilter::E8E9 ? 0xE9 : 0xE8;
		for (uint32_t curPos = 0; curPos < dataSize - 4;) {
			const uint8_t curByte = *(data++);
			++curPos;
			if (curByte == 0xE8 || curByte == cmpByte2) {
				const uint32_t offset = curPos + fileOffset;
				const uint32_t addr = Get4(data);
				if ((addr & 0x80000000u) != 0) {
					if (((addr + offset) & 0x80000000u) == 0)
						Put4(addr + fileSize, data);
				} else if (((addr - fileSize) & 0x80000000u) != 0) {
					Put4(addr - offset, data);
				}
				data += 4;
				curPos += 4;
			}
		}
		break;
	}
	case StandardFilter::ITANIUM: {
		uint8_t* data = mem;
		const uint32_t dataSize = m_r[4];
		uint32_t fileOffset = m_r[6];
		if (dataSize > VM_MEMSIZE || dataSize < 21)
			return false;
		uint32_t curPos = 0;
		fileOffset >>= 4;
		while (curPos < dataSize - 21) {
			const int byte = (data[0] & 0x1F) - 0x10;
			if (byte >= 0) {
				static constexpr uint8_t MASKS[16] = {4, 4, 6, 6, 0, 0, 7, 7,
													  4, 4, 0, 0, 4, 4, 0, 0};
				const uint8_t cmdMask = MASKS[byte];
				if (cmdMask != 0)
					for (uint32_t i = 0; i <= 2; ++i)
						if (cmdMask & (1u << i)) {
							const uint32_t startPos = i * 41 + 5;
							const uint32_t opType = ItaniumGetBits(data, startPos + 37, 4);
							if (opType == 5) {
								const uint32_t offset = ItaniumGetBits(data, startPos + 13, 20);
								ItaniumSetBits(data, (offset - fileOffset) & 0xFFFFF,
											   startPos + 13, 20);
							}
						}
			}
			data += 16;
			curPos += 16;
			++fileOffset;
		}
		break;
	}
	case StandardFilter::DELTA: {
		const uint32_t dataSize = m_r[4], channels = m_r[0], border = dataSize * 2;
		uint32_t srcPos = 0;
		if (dataSize > VM_MEMSIZE / 2 || channels > MAX3_UNPACK_CHANNELS || channels == 0)
			return false;
		for (uint32_t channel = 0; channel < channels; ++channel) {
			uint8_t prev = 0;
			for (uint32_t dest = dataSize + channel; dest < border; dest += channels)
				mem[dest] = (prev = uint8_t(prev - mem[srcPos++]));
		}
		break;
	}
	case StandardFilter::RGB: {
		const uint32_t dataSize = m_r[4], width = m_r[0] - 3, posR = m_r[1];
		if (dataSize > VM_MEMSIZE / 2 || dataSize < 3 || width > dataSize || posR > 2)
			return false;
		const uint8_t* src = mem;
		uint8_t* dest = mem + dataSize;
		for (uint32_t channel = 0; channel < 3; ++channel) {
			uint32_t prev = 0;
			for (uint32_t i = channel; i < dataSize; i += 3) {
				uint32_t predicted;
				if (i >= width + 3) {
					const uint8_t* upper = dest + i - width;
					const uint32_t upperByte = upper[0];
					const uint32_t upperLeft = *(upper - 3);
					predicted = prev + upperByte - upperLeft;
					const int pa = std::abs(int(predicted - prev));
					const int pb = std::abs(int(predicted - upperByte));
					const int pc = std::abs(int(predicted - upperLeft));
					if (pa <= pb && pa <= pc)
						predicted = prev;
					else if (pb <= pc)
						predicted = upperByte;
					else
						predicted = upperLeft;
				} else {
					predicted = prev;
				}
				prev = dest[i] = uint8_t(predicted - *(src++));
			}
		}
		for (uint32_t i = posR, border = dataSize - 2; i < border; i += 3) {
			const uint8_t g = dest[i + 1];
			dest[i] = uint8_t(dest[i] + g);
			dest[i + 2] = uint8_t(dest[i + 2] + g);
		}
		break;
	}
	case StandardFilter::AUDIO: {
		const uint32_t dataSize = m_r[4], channels = m_r[0];
		const uint8_t* src = mem;
		uint8_t* dest = mem + dataSize;
		if (dataSize > VM_MEMSIZE / 2 || channels > 128 || channels == 0)
			return false;
		for (uint32_t channel = 0; channel < channels; ++channel) {
			uint32_t prevByte = 0, prevDelta = 0, dif[7] = {};
			int d1 = 0, d2 = 0, d3;
			int k1 = 0, k2 = 0, k3 = 0;
			for (uint32_t i = channel, byteCount = 0; i < dataSize;
				 i += channels, ++byteCount) {
				d3 = d2;
				d2 = int(prevDelta) - d1;
				d1 = int(prevDelta);
				uint32_t predicted = 8 * prevByte + uint32_t(k1 * d1 + k2 * d2 + k3 * d3);
				predicted = (predicted >> 3) & 0xFF;
				const uint32_t curByte = *(src++);
				predicted -= curByte;
				dest[i] = uint8_t(predicted);
				prevDelta = uint32_t(int8_t(uint8_t(predicted - prevByte)));
				prevByte = predicted & 0xFF;
				const int d = int(uint32_t(int(int8_t(uint8_t(curByte)))) << 3);
				dif[0] += uint32_t(std::abs(d));
				dif[1] += uint32_t(std::abs(d - d1));
				dif[2] += uint32_t(std::abs(d + d1));
				dif[3] += uint32_t(std::abs(d - d2));
				dif[4] += uint32_t(std::abs(d + d2));
				dif[5] += uint32_t(std::abs(d - d3));
				dif[6] += uint32_t(std::abs(d + d3));
				if ((byteCount & 0x1F) == 0) {
					uint32_t minDif = dif[0], numMinDif = 0;
					dif[0] = 0;
					for (uint32_t j = 1; j < 7; ++j) {
						if (dif[j] < minDif) {
							minDif = dif[j];
							numMinDif = j;
						}
						dif[j] = 0;
					}
					switch (numMinDif) {
					case 1:
						if (k1 >= -16)
							--k1;
						break;
					case 2:
						if (k1 < 16)
							++k1;
						break;
					case 3:
						if (k2 >= -16)
							--k2;
						break;
					case 4:
						if (k2 < 16)
							++k2;
						break;
					case 5:
						if (k3 >= -16)
							--k3;
						break;
					case 6:
						if (k3 < 16)
							++k3;
						break;
					}
				}
			}
		}
		break;
	}
	case StandardFilter::NONE:
		break;
	}
	return true;
}

uint32_t RarVm::ItaniumGetBits(const uint8_t* data, uint32_t bitPos, uint32_t bitCount) {
	uint32_t at = bitPos / 8;
	const uint32_t bit = bitPos & 7;
	uint32_t field = uint32_t(data[at]) | (uint32_t(data[at + 1]) << 8) |
					 (uint32_t(data[at + 2]) << 16) | (uint32_t(data[at + 3]) << 24);
	field >>= bit;
	return field & (0xFFFFFFFFu >> (32 - bitCount));
}

void RarVm::ItaniumSetBits(uint8_t* data, uint32_t field, uint32_t bitPos, uint32_t bitCount) {
	const uint32_t at = bitPos / 8;
	const uint32_t bit = bitPos & 7;
	uint32_t andMask = 0xFFFFFFFFu >> (32 - bitCount);
	andMask = ~(andMask << bit);
	field <<= bit;
	for (uint32_t i = 0; i < 4; ++i) {
		data[at + i] &= uint8_t(andMask);
		data[at + i] |= uint8_t(field);
		andMask = (andMask >> 8) | 0xFF000000u;
		field >>= 8;
	}
}

// ── PpmRangeDecoder ──────────────────────────────────────────────────────────

uint32_t PpmRangeDecoder::GetThreshold(uint32_t total) {
	m_range /= total;
	return (m_code - m_low) / m_range;
}

void PpmRangeDecoder::Decode(uint32_t start, uint32_t size) {
	m_low += start * m_range;
	m_range *= size;
	Normalize();
}

uint32_t PpmRangeDecoder::DecodeBit(uint32_t size0, uint32_t) {
	const uint32_t unit = m_range >> 14;
	const uint32_t bound = unit * size0;
	uint32_t symbol;
	if (m_code - m_low < bound) {
		symbol = 0;
		m_range = bound;
	} else {
		symbol = 1;
		m_low += bound;
		m_range = unit * ((1u << 14) - size0);
	}
	Normalize();
	return symbol;
}

// ── Unpacker ─────────────────────────────────────────────────────────────────

Result<bool, ArchiveError> Unpacker::Begin(InputBuffer& input, uint64_t destSize,
		int method, uint64_t window, bool solid) {
	window = std::max<uint64_t>(window, 0x40000);
	if (window > MAX_DICTIONARY)
		return Err(
			MakeError(ErrorKind::UNSUPPORTED,
					  String::Format("RAR : dictionnaire de %llu Mio au-delà de la limite",
									 static_cast<unsigned long long>(window >> 20))));
	if (!solid || m_window.empty()) {
		m_maxWinSize = size_t(window);
		m_maxWinMask = m_maxWinSize - 1;
	}
	if (window > m_window.size()) {
		if (solid && !m_window.empty())
			return Err(MakeError(ErrorKind::CORRUPT,
								 String("RAR : dictionnaire agrandi dans un bloc solide")));
		m_window.assign(size_t(window), 0);
	}
	m_input = &input;
	m_method = method;
	m_solid = solid;
	m_destUnpSize = destSize >= uint64_t(INT64_MAX) ? INT64_MAX : int64_t(destSize);
	m_remaining = destSize;
	m_started = false;
	m_finished = false;
	m_pending.clear();
	m_pendingPosition = 0;
	m_extraDist = method == 70;
	return Ok(true);
}

Result<size_t, ArchiveError> Unpacker::Produce(uint8_t* out, size_t max) {
	while (m_pendingPosition == m_pending.size()) {
		if (m_finished || m_remaining == 0)
			return Ok(size_t(0));
		m_pending.clear();
		m_pendingPosition = 0;
		m_suspended = false;
		switch (m_method) {
		case 15:
			Unpack15(m_solid);
			break;
		case 20:
			Unpack20(m_solid);
			break;
		case 29:
			Unpack29(m_solid);
			break;
		default:
			Unpack5(m_solid);
			break;
		}
		if (m_input->Failed())
			return Err(m_input->Failure("RAR"));
		if (!m_suspended)
			m_finished = true;
	}
	const size_t take = std::min(max, m_pending.size() - m_pendingPosition);
	std::memcpy(out, m_pending.data() + m_pendingPosition, take);
	m_pendingPosition += take;
	return Ok(take);
}

uint8_t Unpacker::GetChar() {
	if (m_inp.inAddr > BitInput::MAX_SIZE - 30) {
		UnpReadBuf();
		if (m_inp.inAddr >= BitInput::MAX_SIZE)
			return 0;
	}
	return m_inp.Buffer()[m_inp.inAddr++];
}

int Unpacker::UnpRead(uint8_t* buffer, size_t size) {
	if (m_input->Failed())
		return -1;
	return int(m_input->ReadRaw(buffer, size));
}

void Unpacker::UnpWrite(const uint8_t* data, size_t size) {
	const size_t take = size_t(std::min<uint64_t>(size, m_remaining));
	m_pending.insert(m_pending.end(), data, data + take);
	m_remaining -= take;
}

size_t Unpacker::WrapDown(size_t position) const noexcept {
	return position >= m_maxWinSize ? position + m_maxWinSize : position;
}

size_t Unpacker::WrapUp(size_t position) const noexcept {
	return position >= m_maxWinSize ? position - m_maxWinSize : position;
}

void Unpacker::InsertOldDist(size_t distance) {
	m_oldDist[3] = m_oldDist[2];
	m_oldDist[2] = m_oldDist[1];
	m_oldDist[1] = m_oldDist[0];
	m_oldDist[0] = distance;
}

void Unpacker::CopyString(uint32_t length, size_t distance) {
	uint8_t* window = m_window.data();
	size_t srcPtr = m_unpPtr - distance;
	if (distance > m_unpPtr) {
		srcPtr += m_maxWinSize;
		if (distance > m_maxWinSize || !m_firstWinDone) {
			while (length-- > 0) {
				window[m_unpPtr] = 0;
				m_unpPtr = WrapUp(m_unpPtr + 1);
			}
			return;
		}
	}
	if (srcPtr < m_maxWinSize - MAX_INC_LZ_MATCH &&
		m_unpPtr < m_maxWinSize - MAX_INC_LZ_MATCH) {
		const uint8_t* src = window + srcPtr;
		uint8_t* dest = window + m_unpPtr;
		m_unpPtr += length;
		while (length-- > 0)
			*dest++ = *src++;
	} else {
		while (length-- > 0) {
			window[m_unpPtr] = window[WrapUp(srcPtr++)];
			m_unpPtr = WrapUp(m_unpPtr + 1);
		}
	}
}

uint32_t Unpacker::DecodeNumber(BitInput& inp, const DecodeTable& dec) {
	const uint32_t bitField = inp.GetBits() & 0xFFFE;
	if (bitField < dec.decodeLen[dec.quickBits]) {
		const uint32_t code = bitField >> (16 - dec.quickBits);
		inp.AddBits(dec.quickLen[code]);
		return dec.quickNum[code];
	}
	uint32_t bits = 15;
	for (uint32_t i = dec.quickBits + 1; i < 15; ++i)
		if (bitField < dec.decodeLen[i]) {
			bits = i;
			break;
		}
	inp.AddBits(bits);
	uint32_t dist = bitField - dec.decodeLen[bits - 1];
	dist >>= (16 - bits);
	uint32_t pos = dec.decodePos[bits] + dist;
	if (pos >= dec.maxNum)
		pos = 0;
	return dec.decodeNum[pos];
}

void Unpacker::MakeDecodeTables(const uint8_t* lengthTable, DecodeTable& dec, uint32_t size) {
	dec.maxNum = size;
	uint32_t lengthCount[16] = {};
	for (uint32_t i = 0; i < size; ++i)
		++lengthCount[lengthTable[i] & 0xF];
	lengthCount[0] = 0;
	std::memset(dec.decodeNum, 0, size * sizeof(dec.decodeNum[0]));
	dec.decodePos[0] = 0;
	dec.decodeLen[0] = 0;
	uint32_t upperLimit = 0;
	for (uint32_t i = 1; i < 16; ++i) {
		upperLimit += lengthCount[i];
		const uint32_t leftAligned = upperLimit << (16 - i);
		upperLimit *= 2;
		dec.decodeLen[i] = leftAligned;
		dec.decodePos[i] = dec.decodePos[i - 1] + lengthCount[i - 1];
	}
	uint32_t copyPos[16];
	std::memcpy(copyPos, dec.decodePos, sizeof(copyPos));
	for (uint32_t i = 0; i < size; ++i) {
		const uint8_t bitLength = lengthTable[i] & 0xF;
		if (bitLength != 0)
			dec.decodeNum[copyPos[bitLength]++] = uint16_t(i);
	}
	dec.quickBits = (size == NC || size == NC20 || size == NC30) ? MAX_QUICK_DECODE_BITS
																 : MAX_QUICK_DECODE_BITS - 3;
	const uint32_t quickSize = 1u << dec.quickBits;
	uint32_t curBitLength = 1;
	for (uint32_t code = 0; code < quickSize; ++code) {
		const uint32_t bitField = code << (16 - dec.quickBits);
		while (curBitLength < 16 && bitField >= dec.decodeLen[curBitLength])
			++curBitLength;
		dec.quickLen[code] = uint8_t(curBitLength);
		uint32_t dist = bitField - dec.decodeLen[curBitLength - 1];
		dist >>= (16 - curBitLength);
		uint32_t pos;
		if (curBitLength < 16 && (pos = dec.decodePos[curBitLength] + dist) < size)
			dec.quickNum[code] = dec.decodeNum[pos];
		else
			dec.quickNum[code] = 0;
	}
}

void Unpacker::UnpInitData(bool solid) {
	if (!solid) {
		m_oldDist[0] = m_oldDist[1] = m_oldDist[2] = m_oldDist[3] = size_t(-1);
		m_oldDistPtr = 0;
		m_lastDist = uint32_t(-1);
		m_lastLength = 0;
		*m_blockTables = BlockTables{};
		m_unpPtr = m_wrPtr = 0;
		m_prevPtr = 0;
		m_firstWinDone = false;
		m_writeBorder = std::min(m_maxWinSize, UNPACK_MAX_WRITE);
	}
	m_filters.clear();
	m_inp.Init();
	m_writtenFileSize = 0;
	m_readTop = 0;
	m_readBorder = 0;
	m_blockHeader = BlockHeader{};
	m_blockHeader.blockSize = -1;
	UnpInitData20(solid);
	UnpInitData30(solid);
	if (!solid)
		m_tablesRead5 = false;
}

uint32_t Unpacker::SlotToLength(BitInput& inp, uint32_t slot) {
	uint32_t lBits, length = 2;
	if (slot < 8) {
		lBits = 0;
		length += slot;
	} else {
		lBits = slot / 4 - 1;
		length += (4 | (slot & 3)) << lBits;
	}
	if (lBits > 0) {
		length += inp.GetBits() >> (16 - lBits);
		inp.AddBits(lBits);
	}
	return length;
}

void Unpacker::Unpack5(bool solid) {
	BlockTables& t = *m_blockTables;
	if (!m_started) {
		m_started = true;
		UnpInitData(solid);
		if (!UnpReadBuf())
			return;
		// Tables lues au moins une fois, même si le premier bloc n'en a pas.
		if (!ReadBlockHeader(m_inp, m_blockHeader) || !ReadTables(m_inp, m_blockHeader, t) ||
			!m_tablesRead5)
			return;
	}
	for (;;) {
		m_unpPtr = WrapUp(m_unpPtr);
		m_firstWinDone |= (m_prevPtr > m_unpPtr);
		m_prevPtr = m_unpPtr;
		if (m_inp.inAddr >= m_readBorder) {
			bool fileDone = false;
			while (m_inp.inAddr > m_blockHeader.blockStart + m_blockHeader.blockSize - 1 ||
				   (m_inp.inAddr == m_blockHeader.blockStart + m_blockHeader.blockSize - 1 &&
					m_inp.inBit >= m_blockHeader.blockBitSize)) {
				if (m_blockHeader.lastBlockInFile) {
					fileDone = true;
					break;
				}
				if (!ReadBlockHeader(m_inp, m_blockHeader) ||
					!ReadTables(m_inp, m_blockHeader, t))
					return;
			}
			if (fileDone || !UnpReadBuf())
				break;
		}
		if (WrapDown(m_writeBorder - m_unpPtr) <= MAX_INC_LZ_MATCH &&
			m_writeBorder != m_unpPtr) {
			UnpWriteBuf();
			if (m_writtenFileSize > m_destUnpSize)
				return;
			if (!m_pending.empty()) {
				m_suspended = true;
				return;
			}
		}
		const uint32_t mainSlot = DecodeNumber(m_inp, t.ld);
		if (mainSlot < 256) {
			m_window[m_unpPtr++] = uint8_t(mainSlot);
			continue;
		}
		if (mainSlot >= 262) {
			uint32_t length = SlotToLength(m_inp, mainSlot - 262);
			size_t distance = 1;
			uint32_t dBits;
			const uint32_t distSlot = DecodeNumber(m_inp, t.dd);
			if (distSlot < 4) {
				dBits = 0;
				distance += distSlot;
			} else {
				dBits = distSlot / 2 - 1;
				distance += size_t(2 | (distSlot & 1)) << dBits;
			}
			if (dBits > 0) {
				if (dBits >= 4) {
					if (dBits > 4) {
						if (dBits > 36)
							distance += (size_t(m_inp.GetBits64()) >> (68 - dBits)) << 4;
						else
							distance += (size_t(m_inp.GetBits32()) >> (36 - dBits)) << 4;
						m_inp.AddBits(dBits - 4);
					}
					distance += DecodeNumber(m_inp, t.ldd);
				} else {
					distance += m_inp.GetBits() >> (16 - dBits);
					m_inp.AddBits(dBits);
				}
			}
			if (distance > 0x100) {
				++length;
				if (distance > 0x2000) {
					++length;
					if (distance > 0x40000)
						++length;
				}
			}
			InsertOldDist(distance);
			m_lastLength = length;
			CopyString(length, distance);
			continue;
		}
		if (mainSlot == 256) {
			Filter5 filter;
			if (!ReadFilter(m_inp, filter) || !AddFilter(filter))
				break;
			continue;
		}
		if (mainSlot == 257) {
			if (m_lastLength != 0)
				CopyString(m_lastLength, m_oldDist[0]);
			continue;
		}
		if (mainSlot < 262) {
			const uint32_t distNum = mainSlot - 258;
			const size_t distance = m_oldDist[distNum];
			for (uint32_t i = distNum; i > 0; --i)
				m_oldDist[i] = m_oldDist[i - 1];
			m_oldDist[0] = distance;
			const uint32_t lengthSlot = DecodeNumber(m_inp, t.rd);
			const uint32_t length = SlotToLength(m_inp, lengthSlot);
			m_lastLength = length;
			CopyString(length, distance);
			continue;
		}
	}
	UnpWriteBuf();
}

uint32_t Unpacker::ReadFilterData(BitInput& inp) {
	const uint32_t byteCount = (inp.GetBits() >> 14) + 1;
	inp.AddBits(2);
	uint32_t data = 0;
	for (uint32_t i = 0; i < byteCount; ++i) {
		data += (inp.GetBits() >> 8) << (i * 8);
		inp.AddBits(8);
	}
	return data;
}

bool Unpacker::ReadFilter(BitInput& inp, Filter5& filter) {
	if (inp.inAddr > m_readTop - 16)
		if (!UnpReadBuf())
			return false;
	filter.blockStart = ReadFilterData(inp);
	filter.blockLength = ReadFilterData(inp);
	if (filter.blockLength > MAX_FILTER_BLOCK_SIZE)
		filter.blockLength = 0;
	filter.type = uint8_t(inp.GetBits() >> 13);
	inp.AddBits(3);
	if (filter.type == FILTER_DELTA) {
		filter.channels = uint8_t((inp.GetBits() >> 11) + 1);
		inp.AddBits(5);
	}
	return true;
}

bool Unpacker::AddFilter(Filter5& filter) {
	if (m_filters.size() >= MAX_UNPACK_FILTERS) {
		UnpWriteBuf();
		if (m_filters.size() >= MAX_UNPACK_FILTERS)
			m_filters.clear();
	}
	filter.nextWindow =
		m_wrPtr != m_unpPtr && WrapDown(m_wrPtr - m_unpPtr) <= filter.blockStart;
	filter.blockStart = (filter.blockStart + m_unpPtr) % m_maxWinSize;
	m_filters.push_back(filter);
	return true;
}

bool Unpacker::UnpReadBuf() {
	int dataSize = m_readTop - m_inp.inAddr;
	if (dataSize < 0)
		return false;
	m_blockHeader.blockSize -= m_inp.inAddr - m_blockHeader.blockStart;
	if (m_inp.inAddr > BitInput::MAX_SIZE / 2) {
		if (dataSize > 0)
			std::memmove(m_inp.Buffer(), m_inp.Buffer() + m_inp.inAddr, size_t(dataSize));
		m_inp.inAddr = 0;
		m_readTop = dataSize;
	} else {
		dataSize = m_readTop;
	}
	int readCode = 0;
	if (BitInput::MAX_SIZE != dataSize)
		readCode = UnpRead(m_inp.Buffer() + dataSize, size_t(BitInput::MAX_SIZE - dataSize));
	if (readCode > 0)
		m_readTop += readCode;
	m_readBorder = m_readTop - 30;
	m_blockHeader.blockStart = m_inp.inAddr;
	if (m_blockHeader.blockSize != -1)
		m_readBorder =
			std::min(m_readBorder, m_blockHeader.blockStart + m_blockHeader.blockSize - 1);
	return readCode != -1;
}

void Unpacker::UnpWriteBuf() {
	size_t writtenBorder = m_wrPtr;
	const size_t fullWriteSize = WrapDown(m_unpPtr - writtenBorder);
	size_t writeSizeLeft = fullWriteSize;
	bool notAllFiltersProcessed = false;
	for (size_t i = 0; i < m_filters.size(); ++i) {
		Filter5* flt = &m_filters[i];
		if (flt->type == FILTER_NONE)
			continue;
		if (flt->nextWindow) {
			if (WrapDown(flt->blockStart - m_wrPtr) <= fullWriteSize)
				flt->nextWindow = false;
			continue;
		}
		const size_t blockStart = flt->blockStart;
		const uint32_t blockLength = flt->blockLength;
		if (WrapDown(blockStart - writtenBorder) < writeSizeLeft) {
			if (writtenBorder != blockStart) {
				UnpWriteArea(writtenBorder, blockStart);
				writtenBorder = blockStart;
				writeSizeLeft = WrapDown(m_unpPtr - writtenBorder);
			}
			if (blockLength <= writeSizeLeft) {
				if (blockLength > 0) {
					const size_t blockEnd = WrapUp(blockStart + blockLength);
					m_filterSrc.resize(blockLength);
					uint8_t* mem = m_filterSrc.data();
					if (blockStart < blockEnd || blockEnd == 0) {
						std::memcpy(mem, m_window.data() + blockStart, blockLength);
					} else {
						const size_t firstPart = m_maxWinSize - blockStart;
						std::memcpy(mem, m_window.data() + blockStart, firstPart);
						std::memcpy(mem + firstPart, m_window.data(), blockEnd);
					}
					const uint8_t* outMem = ApplyFilter(mem, blockLength, *flt);
					m_filters[i].type = FILTER_NONE;
					if (outMem != nullptr)
						UnpWrite(outMem, blockLength);
					m_writtenFileSize += blockLength;
					writtenBorder = blockEnd;
					writeSizeLeft = WrapDown(m_unpPtr - writtenBorder);
				}
			} else {
				m_wrPtr = writtenBorder;
				for (size_t j = i; j < m_filters.size(); ++j)
					if (m_filters[j].type != FILTER_NONE)
						m_filters[j].nextWindow = false;
				notAllFiltersProcessed = true;
				break;
			}
		}
	}
	size_t emptyCount = 0;
	for (size_t i = 0; i < m_filters.size(); ++i) {
		if (emptyCount > 0)
			m_filters[i - emptyCount] = m_filters[i];
		if (m_filters[i].type == FILTER_NONE)
			++emptyCount;
	}
	if (emptyCount > 0)
		m_filters.resize(m_filters.size() - emptyCount);
	if (!notAllFiltersProcessed) {
		UnpWriteArea(writtenBorder, m_unpPtr);
		m_wrPtr = m_unpPtr;
	}
	m_writeBorder = WrapUp(m_unpPtr + std::min(m_maxWinSize, UNPACK_MAX_WRITE));
	if (m_writeBorder == m_unpPtr ||
		(m_wrPtr != m_unpPtr &&
		 WrapDown(m_wrPtr - m_unpPtr) < WrapDown(m_writeBorder - m_unpPtr)))
		m_writeBorder = m_wrPtr;
}

const uint8_t* Unpacker::ApplyFilter(uint8_t* data, uint32_t dataSize, const Filter5& flt) {
	switch (flt.type) {
	case FILTER_E8:
	case FILTER_E8E9: {
		const uint32_t fileOffset = uint32_t(m_writtenFileSize);
		const uint32_t fileSize = 0x1000000;
		const uint8_t cmpByte2 = flt.type == FILTER_E8E9 ? 0xE9 : 0xE8;
		uint8_t* p = data;
		for (uint32_t curPos = 0; curPos + 4 < dataSize;) {
			const uint8_t curByte = *(p++);
			++curPos;
			if (curByte == 0xE8 || curByte == cmpByte2) {
				const uint32_t offset = (curPos + fileOffset) % fileSize;
				const uint32_t addr = Get4(p);
				if ((addr & 0x80000000u) != 0) {
					if (((addr + offset) & 0x80000000u) == 0)
						Put4(addr + fileSize, p);
				} else if (((addr - fileSize) & 0x80000000u) != 0) {
					Put4(addr - offset, p);
				}
				p += 4;
				curPos += 4;
			}
		}
		return data;
	}
	case FILTER_ARM: {
		const uint32_t fileOffset = uint32_t(m_writtenFileSize);
		for (uint32_t curPos = 0; curPos + 3 < dataSize; curPos += 4) {
			uint8_t* d = data + curPos;
			if (d[3] == 0xEB) {
				uint32_t offset = d[0] + uint32_t(d[1]) * 0x100 + uint32_t(d[2]) * 0x10000;
				offset -= (fileOffset + curPos) / 4;
				d[0] = uint8_t(offset);
				d[1] = uint8_t(offset >> 8);
				d[2] = uint8_t(offset >> 16);
			}
		}
		return data;
	}
	case FILTER_DELTA: {
		const uint32_t channels = flt.channels;
		uint32_t srcPos = 0;
		m_filterDst.resize(dataSize);
		uint8_t* dst = m_filterDst.data();
		for (uint32_t channel = 0; channel < channels; ++channel) {
			uint8_t prev = 0;
			for (uint32_t dest = channel; dest < dataSize; dest += channels)
				dst[dest] = (prev = uint8_t(prev - data[srcPos++]));
		}
		return dst;
	}
	default:
		return nullptr;
	}
}

void Unpacker::UnpWriteArea(size_t start, size_t end) {
	if (end < start) {
		UnpWriteData(m_window.data() + start, m_maxWinSize - start);
		UnpWriteData(m_window.data(), end);
	} else {
		UnpWriteData(m_window.data() + start, end - start);
	}
}

void Unpacker::UnpWriteData(const uint8_t* data, size_t size) {
	if (m_writtenFileSize >= m_destUnpSize)
		return;
	size_t writeSize = size;
	const int64_t left = m_destUnpSize - m_writtenFileSize;
	if (int64_t(writeSize) > left)
		writeSize = size_t(left);
	UnpWrite(data, writeSize);
	m_writtenFileSize += int64_t(size);
}

bool Unpacker::ReadBlockHeader(BitInput& inp, BlockHeader& header) {
	header.headerSize = 0;
	if (inp.inAddr > m_readTop - 7)
		if (!UnpReadBuf())
			return false;
	inp.AddBits((8 - uint32_t(inp.inBit)) & 7);
	const uint8_t blockFlags = uint8_t(inp.GetBits() >> 8);
	inp.AddBits(8);
	const uint32_t byteCount = ((blockFlags >> 3) & 3) + 1;
	if (byteCount == 4)
		return false;
	header.headerSize = int(2 + byteCount);
	header.blockBitSize = (blockFlags & 7) + 1;
	const uint8_t savedCheckSum = uint8_t(inp.GetBits() >> 8);
	inp.AddBits(8);
	int blockSize = 0;
	for (uint32_t i = 0; i < byteCount; ++i) {
		blockSize += int(inp.GetBits() >> 8) << (i * 8);
		inp.AddBits(8);
	}
	header.blockSize = blockSize;
	const uint8_t checkSum =
		uint8_t(0x5A ^ blockFlags ^ blockSize ^ (blockSize >> 8) ^ (blockSize >> 16));
	if (checkSum != savedCheckSum)
		return false;
	header.blockStart = inp.inAddr;
	m_readBorder = std::min(m_readBorder, header.blockStart + header.blockSize - 1);
	header.lastBlockInFile = (blockFlags & 0x40) != 0;
	header.tablePresent = (blockFlags & 0x80) != 0;
	return true;
}

bool Unpacker::ReadTables(BitInput& inp, BlockHeader& header, BlockTables& tables) {
	if (!header.tablePresent)
		return true;
	if (inp.inAddr > m_readTop - 25)
		if (!UnpReadBuf())
			return false;
	uint8_t bitLength[BC];
	for (uint32_t i = 0; i < BC; ++i) {
		const uint32_t length = uint8_t(inp.GetBits() >> 12);
		inp.AddBits(4);
		if (length == 15) {
			uint32_t zeroCount = uint8_t(inp.GetBits() >> 12);
			inp.AddBits(4);
			if (zeroCount == 0) {
				bitLength[i] = 15;
			} else {
				zeroCount += 2;
				while (zeroCount-- > 0 && i < BC)
					bitLength[i++] = 0;
				--i;
			}
		} else {
			bitLength[i] = uint8_t(length);
		}
	}
	MakeDecodeTables(bitLength, tables.bd, BC);
	uint8_t table[HUFF_TABLE_SIZEX];
	const uint32_t dCodes = m_extraDist ? DCX : DCB;
	const uint32_t tableSize = NC + dCodes + RC + LDC;
	for (uint32_t i = 0; i < tableSize;) {
		if (inp.inAddr > m_readTop - 5)
			if (!UnpReadBuf())
				return false;
		const uint32_t number = DecodeNumber(inp, tables.bd);
		if (number < 16) {
			table[i] = uint8_t(number);
			++i;
		} else if (number < 18) {
			uint32_t n;
			if (number == 16) {
				n = (inp.GetBits() >> 13) + 3;
				inp.AddBits(3);
			} else {
				n = (inp.GetBits() >> 9) + 11;
				inp.AddBits(7);
			}
			if (i == 0)
				return false;
			while (n-- > 0 && i < tableSize) {
				table[i] = table[i - 1];
				++i;
			}
		} else {
			uint32_t n;
			if (number == 18) {
				n = (inp.GetBits() >> 13) + 3;
				inp.AddBits(3);
			} else {
				n = (inp.GetBits() >> 9) + 11;
				inp.AddBits(7);
			}
			while (n-- > 0 && i < tableSize)
				table[i++] = 0;
		}
	}
	m_tablesRead5 = true;
	if (inp.inAddr > m_readTop)
		return false;
	MakeDecodeTables(&table[0], tables.ld, NC);
	MakeDecodeTables(&table[NC], tables.dd, dCodes);
	MakeDecodeTables(&table[NC + dCodes], tables.ldd, LDC);
	MakeDecodeTables(&table[NC + dCodes + LDC], tables.rd, RC);
	return true;
}

int Unpacker::SafePpmDecodeChar() {
	const int ch = PpmDecodeChar();
	if (ch == -1) {
		m_ppmReady = false; // modèle possiblement corrompu : réinitialisé au prochain bloc
		m_blockPpm = false;
	}
	return ch;
}

int Unpacker::PpmDecodeChar() {
	if (!m_ppmReady)
		return -1;
	const int symbol = m_ppm->DecodeSymbol(m_rc);
	return symbol < 0 ? -1 : symbol;
}

bool Unpacker::PpmDecodeInit() {
	int maxOrder = GetChar();
	const bool reset = (maxOrder & 0x20) != 0;
	int maxMb = 0;
	if (reset)
		maxMb = GetChar();
	else if (!m_ppmReady)
		return false;
	if (maxOrder & 0x40)
		m_ppmEscChar = GetChar();
	m_rc.Init();
	if (reset) {
		maxOrder = (maxOrder & 0x1F) + 1;
		if (maxOrder > 16)
			maxOrder = 16 + (maxOrder - 16) * 3;
		if (maxOrder == 1) {
			m_ppmReady = false;
			return false;
		}
		if (!m_ppm)
			m_ppm = std::make_unique<Ppmd7Model>();
		m_ppm->Init(uint32_t(maxMb + 1) << 20, unsigned(maxOrder));
		m_ppmReady = true;
	}
	return m_ppmReady;
}

void Unpacker::Unpack29(bool solid) {
	static constexpr uint8_t L_DECODE[] = {0,  1,  2,  3,	4,	 5,	  6,   7,  8,  10,
										   12, 14, 16, 20,	24,	 28,  32,  40, 48, 56,
										   64, 80, 96, 112, 128, 160, 192, 224};
	static constexpr uint8_t L_BITS[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2,
										 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5};
	static constexpr uint8_t SD_DECODE[] = {0, 4, 8, 16, 32, 64, 128, 192};
	static constexpr uint8_t SD_BITS[] = {2, 2, 3, 4, 5, 6, 6, 6};
	struct DistanceTables {
		uint32_t decode[DC30] = {};
		uint8_t bits[DC30] = {};
	};
	static const DistanceTables DIST = [] {
		DistanceTables t;
		static constexpr int COUNTS[] = {4, 2, 2, 2, 2, 2, 2,  2, 2, 2,
										 2, 2, 2, 2, 2, 2, 14, 0, 12};
		uint32_t dist = 0;
		int slot = 0;
		for (int i = 0, bitLength = 0; i < int(std::size(COUNTS)); ++i, ++bitLength)
			for (int j = 0; j < COUNTS[i]; ++j, ++slot, dist += (1u << bitLength)) {
				t.decode[slot] = dist;
				t.bits[slot] = uint8_t(bitLength);
			}
		return t;
	}();
	BlockTables& t = *m_blockTables;
	uint32_t bits;
	if (!m_started) {
		m_started = true;
		UnpInitData(solid);
		if (!UnpReadBuf30())
			return;
		if ((!solid || !m_tablesRead3) && !ReadTables30())
			return;
	}
	for (;;) {
		m_unpPtr &= m_maxWinMask;
		m_firstWinDone |= (m_prevPtr > m_unpPtr);
		m_prevPtr = m_unpPtr;
		if (m_inp.inAddr > m_readBorder)
			if (!UnpReadBuf30())
				break;
		if (((m_wrPtr - m_unpPtr) & m_maxWinMask) <= MAX3_INC_LZ_MATCH && m_wrPtr != m_unpPtr) {
			UnpWriteBuf30();
			if (m_writtenFileSize > m_destUnpSize)
				return;
			if (!m_pending.empty()) {
				m_suspended = true;
				return;
			}
		}
		if (m_blockPpm) {
			const int ch = PpmDecodeChar();
			if (ch == -1) {
				m_ppmReady = false;
				m_blockPpm = false;
				break;
			}
			if (ch == m_ppmEscChar) {
				const int nextCh = SafePpmDecodeChar();
				if (nextCh == 0) {
					if (!ReadTables30())
						break;
					continue;
				}
				if (nextCh == -1 || nextCh == 2)
					break;
				if (nextCh == 3) {
					if (!ReadVmCodePpm())
						break;
					continue;
				}
				if (nextCh == 4) {
					uint32_t distance = 0, length = 0;
					bool failed = false;
					for (int i = 0; i < 4 && !failed; ++i) {
						const int c = SafePpmDecodeChar();
						if (c == -1)
							failed = true;
						else if (i == 3)
							length = uint8_t(c);
						else
							distance = (distance << 8) + uint8_t(c);
					}
					if (failed)
						break;
					CopyString(length + 32, distance + 2);
					continue;
				}
				if (nextCh == 5) {
					const int length = SafePpmDecodeChar();
					if (length == -1)
						break;
					CopyString(uint32_t(length + 4), 1);
					continue;
				}
				// 1 : l'octet d'échappement lui-même.
			}
			m_window[m_unpPtr++] = uint8_t(ch);
			continue;
		}
		uint32_t number = DecodeNumber(m_inp, t.ld);
		if (number < 256) {
			m_window[m_unpPtr++] = uint8_t(number);
			continue;
		}
		if (number >= 271) {
			number -= 271;
			uint32_t length = L_DECODE[number] + 3;
			if ((bits = L_BITS[number]) > 0) {
				length += m_inp.GetBits() >> (16 - bits);
				m_inp.AddBits(bits);
			}
			const uint32_t distNumber = DecodeNumber(m_inp, t.dd);
			uint32_t distance = DIST.decode[distNumber] + 1;
			if ((bits = DIST.bits[distNumber]) > 0) {
				if (distNumber > 9) {
					if (bits > 4) {
						distance += (m_inp.GetBits() >> (20 - bits)) << 4;
						m_inp.AddBits(bits - 4);
					}
					if (m_lowDistRepCount > 0) {
						--m_lowDistRepCount;
						distance += uint32_t(m_prevLowDist);
					} else {
						const uint32_t lowDist = DecodeNumber(m_inp, t.ldd);
						if (lowDist == 16) {
							m_lowDistRepCount = LOW_DIST_REP_COUNT - 1;
							distance += uint32_t(m_prevLowDist);
						} else {
							distance += lowDist;
							m_prevLowDist = int(lowDist);
						}
					}
				} else {
					distance += m_inp.GetBits() >> (16 - bits);
					m_inp.AddBits(bits);
				}
			}
			if (distance >= 0x2000) {
				++length;
				if (distance >= 0x40000)
					++length;
			}
			InsertOldDist(distance);
			m_lastLength = length;
			CopyString(length, distance);
			continue;
		}
		if (number == 256) {
			if (!ReadEndOfBlock())
				break;
			continue;
		}
		if (number == 257) {
			if (!ReadVmCode())
				break;
			continue;
		}
		if (number == 258) {
			if (m_lastLength != 0)
				CopyString(m_lastLength, m_oldDist[0]);
			continue;
		}
		if (number < 263) {
			const uint32_t distNum = number - 259;
			const uint32_t distance = uint32_t(m_oldDist[distNum]);
			for (uint32_t i = distNum; i > 0; --i)
				m_oldDist[i] = m_oldDist[i - 1];
			m_oldDist[0] = distance;
			const uint32_t lengthNumber = DecodeNumber(m_inp, t.rd);
			uint32_t length = L_DECODE[lengthNumber] + 2;
			if ((bits = L_BITS[lengthNumber]) > 0) {
				length += m_inp.GetBits() >> (16 - bits);
				m_inp.AddBits(bits);
			}
			m_lastLength = length;
			CopyString(length, distance);
			continue;
		}
		if (number < 272) {
			number -= 263;
			uint32_t distance = SD_DECODE[number] + 1;
			if ((bits = SD_BITS[number]) > 0) {
				distance += m_inp.GetBits() >> (16 - bits);
				m_inp.AddBits(bits);
			}
			InsertOldDist(distance);
			m_lastLength = 2;
			CopyString(2, distance);
			continue;
		}
	}
	UnpWriteBuf30();
}

bool Unpacker::ReadEndOfBlock() {
	const uint32_t bitField = m_inp.GetBits();
	bool newTable, newFile = false;
	if (bitField & 0x8000) {
		newTable = true;
		m_inp.AddBits(1);
	} else {
		newFile = true;
		newTable = (bitField & 0x4000) != 0;
		m_inp.AddBits(2);
	}
	m_tablesRead3 = !newTable;
	if (newFile)
		return false;
	return ReadTables30();
}

bool Unpacker::ReadVmCode() {
	const uint32_t firstByte = m_inp.GetBits() >> 8;
	m_inp.AddBits(8);
	uint32_t length = (firstByte & 7) + 1;
	if (length == 7) {
		length = (m_inp.GetBits() >> 8) + 7;
		m_inp.AddBits(8);
	} else if (length == 8) {
		length = m_inp.GetBits();
		m_inp.AddBits(16);
	}
	if (length == 0)
		return false;
	std::vector<uint8_t> code(length);
	for (uint32_t i = 0; i < length; ++i) {
		if (m_inp.inAddr >= m_readTop - 1 && !UnpReadBuf30() && i < length - 1)
			return false;
		code[i] = uint8_t(m_inp.GetBits() >> 8);
		m_inp.AddBits(8);
	}
	return AddVmCode(firstByte, code.data(), length);
}

bool Unpacker::ReadVmCodePpm() {
	const int first = SafePpmDecodeChar();
	if (first == -1)
		return false;
	const uint32_t firstByte = uint32_t(first);
	uint32_t length = (firstByte & 7) + 1;
	if (length == 7) {
		const int b1 = SafePpmDecodeChar();
		if (b1 == -1)
			return false;
		length = uint32_t(b1) + 7;
	} else if (length == 8) {
		const int b1 = SafePpmDecodeChar();
		if (b1 == -1)
			return false;
		const int b2 = SafePpmDecodeChar();
		if (b2 == -1)
			return false;
		length = uint32_t(b1 * 256 + b2);
	}
	if (length == 0)
		return false;
	std::vector<uint8_t> code(length);
	for (uint32_t i = 0; i < length; ++i) {
		const int ch = SafePpmDecodeChar();
		if (ch == -1)
			return false;
		code[i] = uint8_t(ch);
	}
	return AddVmCode(firstByte, code.data(), length);
}

bool Unpacker::AddVmCode(uint32_t firstByte, const uint8_t* code, uint32_t codeSize) {
	m_vmCodeInp.Init();
	std::memcpy(m_vmCodeInp.Buffer(), code, std::min<uint32_t>(BitInput::MAX_SIZE, codeSize));
	m_vm.Init();
	uint32_t filtPos;
	if (firstByte & 0x80) {
		filtPos = RarVm::ReadData(m_vmCodeInp);
		if (filtPos == 0)
			InitFilters30(false);
		else
			--filtPos;
	} else {
		filtPos = m_lastFilter;
	}
	if (filtPos > m_filters30.size() || filtPos > m_oldFilterLengths.size())
		return false;
	m_lastFilter = filtPos;
	const bool newFilter = filtPos == m_filters30.size();
	auto stackFilter = std::make_unique<Filter30>();
	Filter30* filter;
	if (newFilter) {
		if (filtPos > MAX3_UNPACK_FILTERS)
			return false;
		stackFilter->parentFilter = uint32_t(m_filters30.size());
		m_filters30.push_back(std::make_unique<Filter30>());
		filter = m_filters30.back().get();
		m_oldFilterLengths.push_back(0);
	} else {
		filter = m_filters30[filtPos].get();
		stackFilter->parentFilter = filtPos;
	}
	size_t emptyCount = 0;
	for (size_t i = 0; i < m_prgStack.size(); ++i) {
		const bool isNull = m_prgStack[i] == nullptr;
		if (emptyCount > 0)
			m_prgStack[i - emptyCount] = std::move(m_prgStack[i]);
		if (isNull)
			++emptyCount;
	}
	if (emptyCount == 0) {
		if (m_prgStack.size() > MAX3_UNPACK_FILTERS)
			return false;
		m_prgStack.resize(m_prgStack.size() + 1);
		emptyCount = 1;
	}
	Filter30* sf = stackFilter.get();
	m_prgStack[m_prgStack.size() - emptyCount] = std::move(stackFilter);
	uint32_t blockStart = RarVm::ReadData(m_vmCodeInp);
	if (firstByte & 0x40)
		blockStart += 258;
	sf->blockStart = uint32_t((blockStart + m_unpPtr) & m_maxWinMask);
	if (firstByte & 0x20) {
		sf->blockLength = RarVm::ReadData(m_vmCodeInp);
		m_oldFilterLengths[filtPos] = sf->blockLength;
	} else {
		sf->blockLength = filtPos < m_oldFilterLengths.size() ? m_oldFilterLengths[filtPos] : 0;
	}
	sf->nextWindow = m_wrPtr != m_unpPtr && ((m_wrPtr - m_unpPtr) & m_maxWinMask) <= blockStart;
	std::memset(sf->program.initR, 0, sizeof(sf->program.initR));
	sf->program.initR[4] = sf->blockLength;
	if (firstByte & 0x10) {
		const uint32_t initMask = m_vmCodeInp.GetBits() >> 9;
		m_vmCodeInp.AddBits(7);
		for (uint32_t i = 0; i < 7; ++i)
			if (initMask & (1u << i))
				sf->program.initR[i] = RarVm::ReadData(m_vmCodeInp);
	}
	if (newFilter) {
		const uint32_t vmCodeSize = RarVm::ReadData(m_vmCodeInp);
		if (vmCodeSize >= 0x10000 || vmCodeSize == 0 ||
			uint32_t(m_vmCodeInp.inAddr) + vmCodeSize > codeSize)
			return false;
		std::vector<uint8_t> vmCode(vmCodeSize);
		for (uint32_t i = 0; i < vmCodeSize; ++i) {
			if (m_vmCodeInp.Overflow(3))
				return false;
			vmCode[i] = uint8_t(m_vmCodeInp.GetBits() >> 8);
			m_vmCodeInp.AddBits(8);
		}
		RarVm::Prepare(vmCode.data(), vmCodeSize, filter->program);
	}
	sf->program.type = filter->program.type;
	return true;
}

bool Unpacker::UnpReadBuf30() {
	int dataSize = m_readTop - m_inp.inAddr;
	if (dataSize < 0)
		return false;
	if (m_inp.inAddr > BitInput::MAX_SIZE / 2) {
		if (dataSize > 0)
			std::memmove(m_inp.Buffer(), m_inp.Buffer() + m_inp.inAddr, size_t(dataSize));
		m_inp.inAddr = 0;
		m_readTop = dataSize;
	} else {
		dataSize = m_readTop;
	}
	const int readCode =
		UnpRead(m_inp.Buffer() + dataSize, size_t(BitInput::MAX_SIZE - dataSize));
	if (readCode > 0)
		m_readTop += readCode;
	m_readBorder = m_readTop - 30;
	return readCode != -1;
}

void Unpacker::UnpWriteBuf30() {
	uint32_t writtenBorder = uint32_t(m_wrPtr);
	uint32_t writeSize = uint32_t((m_unpPtr - writtenBorder) & m_maxWinMask);
	for (size_t i = 0; i < m_prgStack.size(); ++i) {
		Filter30* flt = m_prgStack[i].get();
		if (flt == nullptr)
			continue;
		if (flt->nextWindow) {
			flt->nextWindow = false;
			continue;
		}
		const uint32_t blockStart = flt->blockStart;
		const uint32_t blockLength = flt->blockLength;
		if (((blockStart - writtenBorder) & m_maxWinMask) < writeSize) {
			if (writtenBorder != blockStart) {
				UnpWriteArea(writtenBorder, blockStart);
				writtenBorder = blockStart;
				writeSize = uint32_t((m_unpPtr - writtenBorder) & m_maxWinMask);
			}
			if (blockLength <= writeSize) {
				const uint32_t blockEnd = uint32_t((blockStart + blockLength) & m_maxWinMask);
				if (blockStart < blockEnd || blockEnd == 0) {
					m_vm.SetMemory(0, m_window.data() + blockStart, blockLength);
				} else {
					const uint32_t firstPart = uint32_t(m_maxWinSize - blockStart);
					m_vm.SetMemory(0, m_window.data() + blockStart, firstPart);
					m_vm.SetMemory(firstPart, m_window.data(), blockEnd);
				}
				PreparedProgram* prg = &flt->program;
				ExecuteCode(*prg);
				uint8_t* filteredData = prg->filteredData;
				uint32_t filteredSize = prg->filteredDataSize;
				m_prgStack[i].reset();
				while (i + 1 < m_prgStack.size()) {
					Filter30* next = m_prgStack[i + 1].get();
					if (next == nullptr || next->blockStart != blockStart ||
						next->blockLength != filteredSize || next->nextWindow)
						break;
					m_vm.SetMemory(0, filteredData, filteredSize);
					ExecuteCode(next->program);
					filteredData = next->program.filteredData;
					filteredSize = next->program.filteredDataSize;
					++i;
					m_prgStack[i].reset();
				}
				if (filteredData != nullptr)
					UnpWrite(filteredData, filteredSize);
				m_writtenFileSize += filteredSize;
				writtenBorder = blockEnd;
				writeSize = uint32_t((m_unpPtr - writtenBorder) & m_maxWinMask);
			} else {
				for (size_t j = i; j < m_prgStack.size(); ++j)
					if (m_prgStack[j] && m_prgStack[j]->nextWindow)
						m_prgStack[j]->nextWindow = false;
				m_wrPtr = writtenBorder;
				return;
			}
		}
	}
	UnpWriteArea(writtenBorder, m_unpPtr);
	m_wrPtr = m_unpPtr;
}

void Unpacker::ExecuteCode(PreparedProgram& program) {
	program.initR[6] = uint32_t(m_writtenFileSize);
	m_vm.Execute(program);
}

bool Unpacker::ReadTables30() {
	BlockTables& t = *m_blockTables;
	uint8_t bitLength[BC30];
	uint8_t table[HUFF_TABLE_SIZE30];
	if (m_inp.inAddr > m_readTop - 25)
		if (!UnpReadBuf30())
			return false;
	m_inp.AddBits((8 - uint32_t(m_inp.inBit)) & 7);
	const uint32_t bitField = m_inp.GetBits();
	if (bitField & 0x8000) {
		m_blockPpm = true;
		return PpmDecodeInit();
	}
	m_blockPpm = false;
	m_prevLowDist = 0;
	m_lowDistRepCount = 0;
	if (!(bitField & 0x4000))
		std::memset(m_unpOldTable, 0, sizeof(m_unpOldTable));
	m_inp.AddBits(2);
	for (uint32_t i = 0; i < BC30; ++i) {
		const uint32_t length = uint8_t(m_inp.GetBits() >> 12);
		m_inp.AddBits(4);
		if (length == 15) {
			uint32_t zeroCount = uint8_t(m_inp.GetBits() >> 12);
			m_inp.AddBits(4);
			if (zeroCount == 0) {
				bitLength[i] = 15;
			} else {
				zeroCount += 2;
				while (zeroCount-- > 0 && i < BC30)
					bitLength[i++] = 0;
				--i;
			}
		} else {
			bitLength[i] = uint8_t(length);
		}
	}
	MakeDecodeTables(bitLength, t.bd, BC30);
	for (uint32_t i = 0; i < HUFF_TABLE_SIZE30;) {
		if (m_inp.inAddr > m_readTop - 5)
			if (!UnpReadBuf30())
				return false;
		const uint32_t number = DecodeNumber(m_inp, t.bd);
		if (number < 16) {
			table[i] = uint8_t((number + m_unpOldTable[i]) & 0xF);
			++i;
		} else if (number < 18) {
			uint32_t n;
			if (number == 16) {
				n = (m_inp.GetBits() >> 13) + 3;
				m_inp.AddBits(3);
			} else {
				n = (m_inp.GetBits() >> 9) + 11;
				m_inp.AddBits(7);
			}
			if (i == 0)
				return false;
			while (n-- > 0 && i < HUFF_TABLE_SIZE30) {
				table[i] = table[i - 1];
				++i;
			}
		} else {
			uint32_t n;
			if (number == 18) {
				n = (m_inp.GetBits() >> 13) + 3;
				m_inp.AddBits(3);
			} else {
				n = (m_inp.GetBits() >> 9) + 11;
				m_inp.AddBits(7);
			}
			while (n-- > 0 && i < HUFF_TABLE_SIZE30)
				table[i++] = 0;
		}
	}
	m_tablesRead3 = true;
	if (m_inp.inAddr > m_readTop)
		return false;
	MakeDecodeTables(&table[0], t.ld, NC30);
	MakeDecodeTables(&table[NC30], t.dd, DC30);
	MakeDecodeTables(&table[NC30 + DC30], t.ldd, LDC30);
	MakeDecodeTables(&table[NC30 + DC30 + LDC30], t.rd, RC30);
	std::memcpy(m_unpOldTable, table, sizeof(m_unpOldTable));
	return true;
}

void Unpacker::UnpInitData30(bool solid) {
	if (!solid) {
		m_tablesRead3 = false;
		std::memset(m_unpOldTable, 0, sizeof(m_unpOldTable));
		m_ppmEscChar = 2;
		m_blockPpm = false;
	}
	InitFilters30(solid);
}

void Unpacker::InitFilters30(bool solid) {
	if (!solid) {
		m_oldFilterLengths.clear();
		m_lastFilter = 0;
		m_filters30.clear();
	}
	m_prgStack.clear();
}

void Unpacker::CopyString20(uint32_t length, uint32_t distance) {
	m_lastDist = distance;
	m_oldDist[m_oldDistPtr++] = distance;
	m_oldDistPtr &= 3;
	m_lastLength = length;
	m_destUnpSize -= length;
	CopyString(length, distance);
}

void Unpacker::Unpack20(bool solid) {
	static constexpr uint8_t L_DECODE[] = {0,  1,  2,  3,	4,	 5,	  6,   7,  8,  10,
										   12, 14, 16, 20,	24,	 28,  32,  40, 48, 56,
										   64, 80, 96, 112, 128, 160, 192, 224};
	static constexpr uint8_t L_BITS[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2,
										 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5};
	static constexpr uint32_t D_DECODE[] = {
		0,		1,		2,		3,		4,		6,		8,		12,		16,		24,
		32,		48,		64,		96,		128,	192,	256,	384,	512,	768,
		1024,	1536,	2048,	3072,	4096,	6144,	8192,	12288,	16384,	24576,
		32768,	49152,	65536,	98304,	131072, 196608, 262144, 327680, 393216, 458752,
		524288, 589824, 655360, 720896, 786432, 851968, 917504, 983040};
	static constexpr uint8_t D_BITS[] = {0,	 0,	 0,	 0,	 1,	 1,	 2,	 2,	 3,	 3,	 4,	 4,
										 5,	 5,	 6,	 6,	 7,	 7,	 8,	 8,	 9,	 9,	 10, 10,
										 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16,
										 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
	static constexpr uint8_t SD_DECODE[] = {0, 4, 8, 16, 32, 64, 128, 192};
	static constexpr uint8_t SD_BITS[] = {2, 2, 3, 4, 5, 6, 6, 6};
	BlockTables& t = *m_blockTables;
	uint32_t bits;
	if (!m_started) {
		m_started = true;
		UnpInitData(solid);
		if (!UnpReadBuf())
			return;
		if ((!solid || !m_tablesRead2) && !ReadTables20())
			return;
		--m_destUnpSize;
	}
	while (m_destUnpSize >= 0) {
		m_unpPtr &= m_maxWinMask;
		m_firstWinDone |= (m_prevPtr > m_unpPtr);
		m_prevPtr = m_unpPtr;
		if (m_inp.inAddr > m_readTop - 30)
			if (!UnpReadBuf())
				break;
		if (((m_wrPtr - m_unpPtr) & m_maxWinMask) < 270 && m_wrPtr != m_unpPtr) {
			UnpWriteBuf20();
			if (!m_pending.empty()) {
				m_suspended = true;
				return;
			}
		}
		if (m_unpAudioBlock) {
			const uint32_t audioNumber = DecodeNumber(m_inp, m_md[m_unpCurChannel]);
			if (audioNumber == 256) {
				if (!ReadTables20())
					break;
				continue;
			}
			m_window[m_unpPtr++] = DecodeAudio(int(audioNumber));
			if (++m_unpCurChannel == m_unpChannels)
				m_unpCurChannel = 0;
			--m_destUnpSize;
			continue;
		}
		uint32_t number = DecodeNumber(m_inp, t.ld);
		if (number < 256) {
			m_window[m_unpPtr++] = uint8_t(number);
			--m_destUnpSize;
			continue;
		}
		if (number > 269) {
			number -= 270;
			uint32_t length = L_DECODE[number] + 3;
			if ((bits = L_BITS[number]) > 0) {
				length += m_inp.GetBits() >> (16 - bits);
				m_inp.AddBits(bits);
			}
			const uint32_t distNumber = DecodeNumber(m_inp, t.dd);
			uint32_t distance = D_DECODE[distNumber] + 1;
			if ((bits = D_BITS[distNumber]) > 0) {
				distance += m_inp.GetBits() >> (16 - bits);
				m_inp.AddBits(bits);
			}
			if (distance >= 0x2000) {
				++length;
				if (distance >= 0x40000)
					++length;
			}
			CopyString20(length, distance);
			continue;
		}
		if (number == 269) {
			if (!ReadTables20())
				break;
			continue;
		}
		if (number == 256) {
			CopyString20(m_lastLength, m_lastDist);
			continue;
		}
		if (number < 261) {
			const uint32_t distance = uint32_t(m_oldDist[(m_oldDistPtr - (number - 256)) & 3]);
			const uint32_t lengthNumber = DecodeNumber(m_inp, t.rd);
			uint32_t length = L_DECODE[lengthNumber] + 2;
			if ((bits = L_BITS[lengthNumber]) > 0) {
				length += m_inp.GetBits() >> (16 - bits);
				m_inp.AddBits(bits);
			}
			if (distance >= 0x101) {
				++length;
				if (distance >= 0x2000) {
					++length;
					if (distance >= 0x40000)
						++length;
				}
			}
			CopyString20(length, distance);
			continue;
		}
		if (number < 270) {
			number -= 261;
			uint32_t distance = SD_DECODE[number] + 1;
			if ((bits = SD_BITS[number]) > 0) {
				distance += m_inp.GetBits() >> (16 - bits);
				m_inp.AddBits(bits);
			}
			CopyString20(2, distance);
			continue;
		}
	}
	ReadLastTables();
	UnpWriteBuf20();
}

void Unpacker::UnpWriteBuf20() {
	if (m_unpPtr < m_wrPtr) {
		UnpWrite(m_window.data() + m_wrPtr, (0 - m_wrPtr) & m_maxWinMask);
		UnpWrite(m_window.data(), m_unpPtr);
	} else {
		UnpWrite(m_window.data() + m_wrPtr, m_unpPtr - m_wrPtr);
	}
	m_wrPtr = m_unpPtr;
}

bool Unpacker::ReadTables20() {
	BlockTables& t = *m_blockTables;
	uint8_t bitLength[BC20];
	uint8_t table[MC20 * 4];
	if (m_inp.inAddr > m_readTop - 25)
		if (!UnpReadBuf())
			return false;
	const uint32_t bitField = m_inp.GetBits();
	m_unpAudioBlock = (bitField & 0x8000) != 0;
	if (!(bitField & 0x4000))
		std::memset(m_unpOldTable20, 0, sizeof(m_unpOldTable20));
	m_inp.AddBits(2);
	uint32_t tableSize;
	if (m_unpAudioBlock) {
		m_unpChannels = ((bitField >> 12) & 3) + 1;
		if (m_unpCurChannel >= m_unpChannels)
			m_unpCurChannel = 0;
		m_inp.AddBits(2);
		tableSize = MC20 * m_unpChannels;
	} else {
		tableSize = NC20 + DC20 + RC20;
	}
	for (uint32_t i = 0; i < BC20; ++i) {
		bitLength[i] = uint8_t(m_inp.GetBits() >> 12);
		m_inp.AddBits(4);
	}
	MakeDecodeTables(bitLength, t.bd, BC20);
	for (uint32_t i = 0; i < tableSize;) {
		if (m_inp.inAddr > m_readTop - 5)
			if (!UnpReadBuf())
				return false;
		const uint32_t number = DecodeNumber(m_inp, t.bd);
		if (number < 16) {
			table[i] = uint8_t((number + m_unpOldTable20[i]) & 0xF);
			++i;
		} else if (number == 16) {
			uint32_t n = (m_inp.GetBits() >> 14) + 3;
			m_inp.AddBits(2);
			if (i == 0)
				return false;
			while (n-- > 0 && i < tableSize) {
				table[i] = table[i - 1];
				++i;
			}
		} else {
			uint32_t n;
			if (number == 17) {
				n = (m_inp.GetBits() >> 13) + 3;
				m_inp.AddBits(3);
			} else {
				n = (m_inp.GetBits() >> 9) + 11;
				m_inp.AddBits(7);
			}
			while (n-- > 0 && i < tableSize)
				table[i++] = 0;
		}
	}
	m_tablesRead2 = true;
	if (m_inp.inAddr > m_readTop)
		return true;
	if (m_unpAudioBlock) {
		for (uint32_t i = 0; i < m_unpChannels; ++i)
			MakeDecodeTables(&table[i * MC20], m_md[i], MC20);
	} else {
		MakeDecodeTables(&table[0], t.ld, NC20);
		MakeDecodeTables(&table[NC20], t.dd, DC20);
		MakeDecodeTables(&table[NC20 + DC20], t.rd, RC20);
	}
	std::memcpy(m_unpOldTable20, table, tableSize);
	return true;
}

void Unpacker::ReadLastTables() {
	if (m_readTop >= m_inp.inAddr + 5) {
		if (m_unpAudioBlock) {
			if (DecodeNumber(m_inp, m_md[m_unpCurChannel]) == 256)
				ReadTables20();
		} else if (DecodeNumber(m_inp, m_blockTables->ld) == 269) {
			ReadTables20();
		}
	}
}

void Unpacker::UnpInitData20(bool solid) {
	if (!solid) {
		m_tablesRead2 = false;
		m_unpAudioBlock = false;
		m_unpChannelDelta = 0;
		m_unpCurChannel = 0;
		m_unpChannels = 1;
		for (AudioVariables& v : m_audV)
			v = AudioVariables{};
		std::memset(m_unpOldTable20, 0, sizeof(m_unpOldTable20));
		for (DecodeTable& table : m_md)
			table = DecodeTable{};
	}
}

uint8_t Unpacker::DecodeAudio(int delta) {
	AudioVariables* v = &m_audV[m_unpCurChannel];
	++v->byteCount;
	v->d4 = v->d3;
	v->d3 = v->d2;
	v->d2 = v->lastDelta - v->d1;
	v->d1 = v->lastDelta;
	int pch = 8 * v->lastChar + v->k1 * v->d1 + v->k2 * v->d2 + v->k3 * v->d3 + v->k4 * v->d4 +
			  v->k5 * m_unpChannelDelta;
	pch = (pch >> 3) & 0xFF;
	const uint32_t ch = uint32_t(pch - delta);
	const int d = int(uint32_t(int(int8_t(uint8_t(delta)))) << 3);
	v->dif[0] += uint32_t(std::abs(d));
	v->dif[1] += uint32_t(std::abs(d - v->d1));
	v->dif[2] += uint32_t(std::abs(d + v->d1));
	v->dif[3] += uint32_t(std::abs(d - v->d2));
	v->dif[4] += uint32_t(std::abs(d + v->d2));
	v->dif[5] += uint32_t(std::abs(d - v->d3));
	v->dif[6] += uint32_t(std::abs(d + v->d3));
	v->dif[7] += uint32_t(std::abs(d - v->d4));
	v->dif[8] += uint32_t(std::abs(d + v->d4));
	v->dif[9] += uint32_t(std::abs(d - m_unpChannelDelta));
	v->dif[10] += uint32_t(std::abs(d + m_unpChannelDelta));
	m_unpChannelDelta = v->lastDelta = int8_t(uint8_t(ch - uint32_t(v->lastChar)));
	v->lastChar = int(ch & 0xFF);
	if ((v->byteCount & 0x1F) == 0) {
		uint32_t minDif = v->dif[0], numMinDif = 0;
		v->dif[0] = 0;
		for (uint32_t i = 1; i < 11; ++i) {
			if (v->dif[i] < minDif) {
				minDif = v->dif[i];
				numMinDif = i;
			}
			v->dif[i] = 0;
		}
		int* k[5] = {&v->k1, &v->k2, &v->k3, &v->k4, &v->k5};
		if (numMinDif > 0) {
			int& coefficient = *k[(numMinDif - 1) / 2];
			if (numMinDif & 1) {
				if (coefficient >= -16)
					--coefficient;
			} else if (coefficient < 16) {
				++coefficient;
			}
		}
	}
	return uint8_t(ch);
}

void Unpacker::Unpack15(bool solid) {
	if (!m_started) {
		m_started = true;
		UnpInitData(solid);
		UnpInitData15(solid);
		UnpReadBuf();
		if (!solid) {
			InitHuff();
			m_unpPtr = 0;
		} else {
			m_unpPtr = m_wrPtr;
		}
		--m_destUnpSize;
		if (m_destUnpSize >= 0) {
			GetFlagsBuf();
			m_flagsCnt = 8;
		}
	}
	while (m_destUnpSize >= 0) {
		m_unpPtr &= m_maxWinMask;
		m_firstWinDone |= (m_prevPtr > m_unpPtr);
		m_prevPtr = m_unpPtr;
		if (m_inp.inAddr > m_readTop - 30 && !UnpReadBuf())
			break;
		if (((m_wrPtr - m_unpPtr) & m_maxWinMask) < 270 && m_wrPtr != m_unpPtr) {
			UnpWriteBuf20();
			if (!m_pending.empty()) {
				m_suspended = true;
				return;
			}
		}
		if (m_stMode) {
			HuffDecode();
			continue;
		}
		if (--m_flagsCnt < 0) {
			GetFlagsBuf();
			m_flagsCnt = 7;
		}
		if (m_flagBuf & 0x80) {
			m_flagBuf <<= 1;
			if (m_nlzb > m_nhfb)
				LongLz();
			else
				HuffDecode();
		} else {
			m_flagBuf <<= 1;
			if (--m_flagsCnt < 0) {
				GetFlagsBuf();
				m_flagsCnt = 7;
			}
			if (m_flagBuf & 0x80) {
				m_flagBuf <<= 1;
				if (m_nlzb > m_nhfb)
					HuffDecode();
				else
					LongLz();
			} else {
				m_flagBuf <<= 1;
				ShortLz();
			}
		}
	}
	UnpWriteBuf20();
}

uint32_t Unpacker::DecodeNum(uint32_t num, const Huff15Table& table) {
	uint32_t startPos = table.start;
	int i = 0;
	for (num &= 0xFFF0; table.dec[i] <= num; ++i)
		++startPos;
	m_inp.AddBits(startPos);
	return ((num - (i ? table.dec[i - 1] : 0)) >> (16 - startPos)) + table.pos[startPos];
}

void Unpacker::ShortLz() {
	static constexpr uint32_t SHORT_LEN1[] = {1, 3, 4, 4, 5, 6, 7, 8, 8, 4, 4, 5, 6, 6, 4, 0};
	static constexpr uint32_t SHORT_XOR1[] = {0,	0xa0, 0xd0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe,
											  0xff, 0xc0, 0x80, 0x90, 0x98, 0x9c, 0xb0};
	static constexpr uint32_t SHORT_LEN2[] = {2, 3, 3, 3, 4, 4, 5, 6, 6, 4, 4, 5, 6, 6, 4, 0};
	static constexpr uint32_t SHORT_XOR2[] = {0,	0x40, 0x60, 0xa0, 0xd0, 0xe0, 0xf0, 0xf8,
											  0xfc, 0xc0, 0x80, 0x90, 0x98, 0x9c, 0xb0};
	auto shortLen1 = [this](uint32_t pos) {
		return pos == 1 ? uint32_t(m_buf60 + 3) : SHORT_LEN1[pos];
	};
	auto shortLen2 = [this](uint32_t pos) {
		return pos == 3 ? uint32_t(m_buf60 + 3) : SHORT_LEN2[pos];
	};
	uint32_t length, saveLength, lastDistance, distance;
	int distancePlace;
	m_numHuf = 0;
	uint32_t bitField = m_inp.GetBits();
	if (m_lCount == 2) {
		m_inp.AddBits(1);
		if (bitField >= 0x8000) {
			CopyString15(m_lastDist, m_lastLength);
			return;
		}
		bitField <<= 1;
		m_lCount = 0;
	}
	bitField >>= 8;
	if (m_avrLn1 < 37) {
		for (length = 0; length < 15; ++length)
			if (((bitField ^ SHORT_XOR1[length]) & (~(0xFFu >> shortLen1(length)))) == 0)
				break;
		m_inp.AddBits(shortLen1(length));
	} else {
		for (length = 0; length < 15; ++length)
			if (((bitField ^ SHORT_XOR2[length]) & (~(0xFFu >> shortLen2(length)))) == 0)
				break;
		m_inp.AddBits(shortLen2(length));
	}
	if (length >= 9) {
		if (length == 9) {
			++m_lCount;
			CopyString15(m_lastDist, m_lastLength);
			return;
		}
		if (length == 14) {
			m_lCount = 0;
			length = DecodeNum(m_inp.GetBits(), L2) + 5;
			distance = (m_inp.GetBits() >> 1) | 0x8000;
			m_inp.AddBits(15);
			m_lastLength = length;
			m_lastDist = distance;
			CopyString15(distance, length);
			return;
		}
		m_lCount = 0;
		saveLength = length;
		distance = uint32_t(m_oldDist[(m_oldDistPtr - (length - 9)) & 3]);
		length = DecodeNum(m_inp.GetBits(), L1) + 2;
		if (length == 0x101 && saveLength == 10) {
			m_buf60 ^= 1;
			return;
		}
		if (distance > 256)
			++length;
		if (distance >= m_maxDist3)
			++length;
		m_oldDist[m_oldDistPtr++] = distance;
		m_oldDistPtr &= 3;
		m_lastLength = length;
		m_lastDist = distance;
		CopyString15(distance, length);
		return;
	}
	m_lCount = 0;
	m_avrLn1 += length;
	m_avrLn1 -= m_avrLn1 >> 4;
	distancePlace = int(DecodeNum(m_inp.GetBits(), HF2) & 0xFF);
	distance = m_chSetA[distancePlace];
	if (--distancePlace != -1) {
		lastDistance = m_chSetA[distancePlace];
		m_chSetA[distancePlace + 1] = uint16_t(lastDistance);
		m_chSetA[distancePlace] = uint16_t(distance);
	}
	length += 2;
	m_oldDist[m_oldDistPtr++] = ++distance;
	m_oldDistPtr &= 3;
	m_lastLength = length;
	m_lastDist = distance;
	CopyString15(distance, length);
}

void Unpacker::LongLz() {
	uint32_t length, distance, distancePlace, newDistancePlace;
	m_numHuf = 0;
	m_nlzb += 16;
	if (m_nlzb > 0xFF) {
		m_nlzb = 0x90;
		m_nhfb >>= 1;
	}
	const uint32_t oldAvr2 = m_avrLn2;
	uint32_t bitField = m_inp.GetBits();
	if (m_avrLn2 >= 122) {
		length = DecodeNum(bitField, L2);
	} else if (m_avrLn2 >= 64) {
		length = DecodeNum(bitField, L1);
	} else if (bitField < 0x100) {
		length = bitField;
		m_inp.AddBits(16);
	} else {
		for (length = 0; ((bitField << length) & 0x8000) == 0; ++length)
			;
		m_inp.AddBits(length + 1);
	}
	m_avrLn2 += length;
	m_avrLn2 -= m_avrLn2 >> 5;
	bitField = m_inp.GetBits();
	if (m_avrPlcB > 0x28FF)
		distancePlace = DecodeNum(bitField, HF2);
	else if (m_avrPlcB > 0x6FF)
		distancePlace = DecodeNum(bitField, HF1);
	else
		distancePlace = DecodeNum(bitField, HF0);
	m_avrPlcB += distancePlace;
	m_avrPlcB -= m_avrPlcB >> 8;
	for (;;) {
		distance = m_chSetB[distancePlace & 0xFF];
		newDistancePlace = m_nToPlB[distance++ & 0xFF]++;
		if (!(distance & 0xFF))
			CorrHuff(m_chSetB, m_nToPlB);
		else
			break;
	}
	m_chSetB[distancePlace & 0xFF] = m_chSetB[newDistancePlace];
	m_chSetB[newDistancePlace] = uint16_t(distance);
	distance = ((distance & 0xFF00) | (m_inp.GetBits() >> 8)) >> 1;
	m_inp.AddBits(7);
	const uint32_t oldAvr3 = m_avrLn3;
	if (length != 1 && length != 4) {
		if (length == 0 && distance <= m_maxDist3) {
			++m_avrLn3;
			m_avrLn3 -= m_avrLn3 >> 8;
		} else if (m_avrLn3 > 0) {
			--m_avrLn3;
		}
	}
	length += 3;
	if (distance >= m_maxDist3)
		++length;
	if (distance <= 256)
		length += 8;
	if (oldAvr3 > 0xB0 || (m_avrPlc >= 0x2A00 && oldAvr2 < 0x40))
		m_maxDist3 = 0x7F00;
	else
		m_maxDist3 = 0x2001;
	m_oldDist[m_oldDistPtr++] = distance;
	m_oldDistPtr &= 3;
	m_lastLength = length;
	m_lastDist = distance;
	CopyString15(distance, length);
}

void Unpacker::HuffDecode() {
	uint32_t curByte, newBytePlace, length, distance;
	int bytePlace;
	uint32_t bitField = m_inp.GetBits();
	if (m_avrPlc > 0x75FF)
		bytePlace = int(DecodeNum(bitField, HF4));
	else if (m_avrPlc > 0x5DFF)
		bytePlace = int(DecodeNum(bitField, HF3));
	else if (m_avrPlc > 0x35FF)
		bytePlace = int(DecodeNum(bitField, HF2));
	else if (m_avrPlc > 0x0DFF)
		bytePlace = int(DecodeNum(bitField, HF1));
	else
		bytePlace = int(DecodeNum(bitField, HF0));
	bytePlace &= 0xFF;
	if (m_stMode) {
		if (bytePlace == 0 && bitField > 0xFFF)
			bytePlace = 0x100;
		if (--bytePlace == -1) {
			bitField = m_inp.GetBits();
			m_inp.AddBits(1);
			if (bitField & 0x8000) {
				m_numHuf = m_stMode = 0;
				return;
			}
			length = (bitField & 0x4000) ? 4 : 3;
			m_inp.AddBits(1);
			distance = DecodeNum(m_inp.GetBits(), HF2);
			distance = (distance << 5) | (m_inp.GetBits() >> 11);
			m_inp.AddBits(5);
			CopyString15(distance, length);
			return;
		}
	} else if (m_numHuf++ >= 16 && m_flagsCnt == 0) {
		m_stMode = 1;
	}
	m_avrPlc += uint32_t(bytePlace);
	m_avrPlc -= m_avrPlc >> 8;
	m_nhfb += 16;
	if (m_nhfb > 0xFF) {
		m_nhfb = 0x90;
		m_nlzb >>= 1;
	}
	m_window[m_unpPtr++] = uint8_t(m_chSet[bytePlace] >> 8);
	--m_destUnpSize;
	for (;;) {
		curByte = m_chSet[bytePlace];
		newBytePlace = m_nToPl[curByte++ & 0xFF]++;
		if ((curByte & 0xFF) > 0xA1)
			CorrHuff(m_chSet, m_nToPl);
		else
			break;
	}
	m_chSet[bytePlace] = m_chSet[newBytePlace];
	m_chSet[newBytePlace] = uint16_t(curByte);
}

void Unpacker::GetFlagsBuf() {
	uint32_t flags, newFlagsPlace;
	const uint32_t flagsPlace = DecodeNum(m_inp.GetBits(), HF2);
	if (flagsPlace >= 256)
		return;
	for (;;) {
		flags = m_chSetC[flagsPlace];
		m_flagBuf = flags >> 8;
		newFlagsPlace = m_nToPlC[flags++ & 0xFF]++;
		if ((flags & 0xFF) != 0)
			break;
		CorrHuff(m_chSetC, m_nToPlC);
	}
	m_chSetC[flagsPlace] = m_chSetC[newFlagsPlace];
	m_chSetC[newFlagsPlace] = uint16_t(flags);
}

void Unpacker::UnpInitData15(bool solid) {
	if (!solid) {
		m_avrPlcB = m_avrLn1 = m_avrLn2 = m_avrLn3 = 0;
		m_numHuf = m_buf60 = 0;
		m_avrPlc = 0x3500;
		m_maxDist3 = 0x2001;
		m_nhfb = m_nlzb = 0x80;
	}
	m_flagsCnt = 0;
	m_flagBuf = 0;
	m_stMode = 0;
	m_lCount = 0;
	m_readTop = 0;
}

void Unpacker::InitHuff() {
	for (uint32_t i = 0; i < 256; ++i) {
		m_chSet[i] = m_chSetB[i] = uint16_t(i << 8);
		m_chSetA[i] = uint16_t(i);
		m_chSetC[i] = uint16_t(((~i + 1) & 0xFF) << 8);
	}
	std::memset(m_nToPl, 0, sizeof(m_nToPl));
	std::memset(m_nToPlB, 0, sizeof(m_nToPlB));
	std::memset(m_nToPlC, 0, sizeof(m_nToPlC));
	CorrHuff(m_chSetB, m_nToPlB);
}

void Unpacker::CorrHuff(uint16_t* charSet, uint8_t* numToPlace) {
	for (int i = 7; i >= 0; --i)
		for (int j = 0; j < 32; ++j, ++charSet)
			*charSet = uint16_t((*charSet & ~0xFF) | i);
	std::memset(numToPlace, 0, 256);
	for (int i = 6; i >= 0; --i)
		numToPlace[i] = uint8_t((7 - i) * 32);
}

void Unpacker::CopyString15(uint32_t distance, uint32_t length) {
	m_destUnpSize -= length;
	if ((!m_firstWinDone && distance > m_unpPtr) || distance > m_maxWinSize || distance == 0) {
		while (length-- > 0) {
			m_window[m_unpPtr] = 0;
			m_unpPtr = (m_unpPtr + 1) & m_maxWinMask;
		}
	} else {
		while (length-- > 0) {
			m_window[m_unpPtr] = m_window[(m_unpPtr - distance) & m_maxWinMask];
			m_unpPtr = (m_unpPtr + 1) & m_maxWinMask;
		}
	}
}

void PpmRangeDecoder::Init() {
	m_low = m_code = 0;
	m_range = 0xFFFFFFFFu;
	for (int i = 0; i < 4; ++i)
		m_code = (m_code << 8) | m_owner->GetChar();
}

void PpmRangeDecoder::Normalize() {
	static constexpr uint32_t TOP = 1u << 24, BOT = 1u << 15;
	for (;;) {
		if ((m_low ^ (m_low + m_range)) >= TOP) {
			if (m_range >= BOT)
				break;
			m_range = (0u - m_low) & (BOT - 1);
		}
		m_code = (m_code << 8) | m_owner->GetChar();
		m_range <<= 8;
		m_low <<= 8;
	}
}

bool IsValidUtf8(std::span<const uint8_t> bytes) noexcept {
	for (size_t i = 0; i < bytes.size();) {
		const uint8_t c = bytes[i];
		const size_t extra = c < 0x80			  ? 0
							 : (c & 0xE0) == 0xC0 ? 1
							 : (c & 0xF0) == 0xE0 ? 2
							 : (c & 0xF8) == 0xF0 ? 3
												  : 9;
		if (extra == 9)
			return false;
		for (size_t k = 1; k <= extra; ++k)
			if (i + k >= bytes.size() || (bytes[i + k] & 0xC0) != 0x80)
				return false;
		i += extra + 1;
	}
	return true;
}

String DecodeRar4UnicodeName(std::span<const uint8_t> name, std::span<const uint8_t> encoded) {
	std::vector<uint16_t> wide;
	size_t encPos = 0, decPos = 0;
	uint32_t flags = 0, flagBits = 0;
	const uint8_t highByte = encPos < encoded.size() ? encoded[encPos++] : 0;
	auto put = [&wide](size_t at, uint16_t value) {
		if (wide.size() <= at)
			wide.resize(at + 1);
		wide[at] = value;
	};
	while (encPos < encoded.size()) {
		if (flagBits == 0) {
			flags = encoded[encPos++];
			flagBits = 8;
		}
		switch (flags >> 6) {
		case 0:
			if (encPos >= encoded.size())
				break;
			put(decPos++, encoded[encPos++]);
			break;
		case 1:
			if (encPos >= encoded.size())
				break;
			put(decPos++, uint16_t(encoded[encPos++] + (highByte << 8)));
			break;
		case 2:
			if (encPos + 1 >= encoded.size())
				break;
			put(decPos++, uint16_t(encoded[encPos] + (encoded[encPos + 1] << 8)));
			encPos += 2;
			break;
		case 3: {
			if (encPos >= encoded.size())
				break;
			int length = encoded[encPos++];
			if (length & 0x80) {
				if (encPos >= encoded.size())
					break;
				const uint8_t correction = encoded[encPos++];
				for (length = (length & 0x7F) + 2; length > 0 && decPos < name.size();
					 --length, ++decPos)
					put(decPos, uint16_t(((name[decPos] + correction) & 0xFF) + (highByte << 8)));
			} else {
				for (length += 2; length > 0 && decPos < name.size(); --length, ++decPos)
					put(decPos, name[decPos]);
			}
			break;
		}
		}
		flags = (flags << 2) & 0xFF;
		flagBits -= 2;
	}
	Bytes le;
	for (uint16_t c : wide) {
		if (c == 0)
			break;
		le.push_back(uint8_t(c));
		le.push_back(uint8_t(c >> 8));
	}
	return Utf16ToUtf8(le, false);
}

// ── HeaderReader ─────────────────────────────────────────────────────────────

size_t HeaderReader::Left() const noexcept {
	return m_position < m_bytes.size() ? m_bytes.size() - m_position : 0;
}

void HeaderReader::Seek(size_t position) noexcept {
	if (position > m_bytes.size())
		m_ok = false;
	else
		m_position = position;
}

uint16_t HeaderReader::U16() {
	return Take(2) ? uint16_t(m_bytes[m_position - 2] | (m_bytes[m_position - 1] << 8)) : 0;
}

uint64_t HeaderReader::U64() {
	const uint64_t low = U32();
	return low | (uint64_t(U32()) << 32);
}

uint64_t HeaderReader::V() {
	uint64_t value = 0;
	for (unsigned shift = 0; shift < 64; shift += 7) {
		const uint8_t byte = U8();
		if (!m_ok)
			return 0;
		value |= uint64_t(byte & 0x7F) << shift;
		if (!(byte & 0x80))
			return value;
	}
	m_ok = false;
	return 0;
}

std::span<const uint8_t> HeaderReader::Bytes(size_t size) {
	if (!Take(size))
		return {};
	return m_bytes.subspan(m_position - size, size);
}

bool HeaderReader::Take(size_t size) {
	if (!m_ok || size > Left()) {
		m_ok = false;
		return false;
	}
	m_position += size;
	return true;
}

// ── CheckImpl ────────────────────────────────────────────────────────────────

Result<size_t, ArchiveError> CheckImpl::Produce(uint8_t* out, size_t max) {
	auto got = StreamRead(m_inner, out, max);
	if (got.IsError()) {
		ArchiveError error = got.Error();
		if (m_mismatch == ErrorKind::WRONG_PASSWORD && error.kind == ErrorKind::CORRUPT) {
			error.kind = ErrorKind::WRONG_PASSWORD;
			error.message =
				String::Format("%s (mot de passe incorrect ?)", error.message.CStr());
		}
		if (!error.message.StartsWith(m_name + " :"))
			error.message = String::Format("%s : %s", m_name.CStr(), error.message.CStr());
		return Err(error);
	}
	if (got.Value() == 0) {
		if (m_size.IsSome() && m_count != m_size.Value())
			return Err(
				MakeError(ErrorKind::CORRUPT,
						  String::Format("%s : %llu octets au lieu de %llu", m_name.CStr(),
										 static_cast<unsigned long long>(m_count),
										 static_cast<unsigned long long>(m_size.Value()))));
		bool ok = true;
		if (m_crc.IsSome()) {
			const uint32_t crc =
				m_hashKey.IsSome() ? Rar5CrcToMac(m_computed, m_hashKey.Value()) : m_computed;
			ok = crc == m_crc.Value();
		}
		if (ok && m_blake.IsSome()) {
			auto digest = m_hasher.Final();
			if (m_hashKey.IsSome())
				digest = Rar5DigestToMac(digest, m_hashKey.Value());
			ok = digest == m_blake.Value();
		}
		if (!ok)
			return Err(
				MakeError(m_mismatch, String::Format("%s : %s incorrect%s", m_name.CStr(),
													 m_blake.IsSome() ? "BLAKE2sp" : "CRC-32",
													 m_mismatch == ErrorKind::WRONG_PASSWORD
														 ? " (mot de passe incorrect ?)"
														 : "")));
		return got;
	}
	const std::span<const uint8_t> chunk(out, got.Value());
	m_count += chunk.size();
	if (m_crc.IsSome())
		m_computed = Crc32(chunk, m_computed);
	if (m_blake.IsSome())
		m_hasher.Update(chunk);
	return got;
}

bool CheckImpl::Restart() {
	if (m_inner.io.Seek(0, SDL_IO_SEEK_SET) != 0)
		return false;
	m_count = 0;
	m_computed = 0;
	m_hasher.Reset();
	return true;
}

// ── ChainImpl ────────────────────────────────────────────────────────────────

Result<size_t, ArchiveError> ChainImpl::Produce(uint8_t* out, size_t max) {
	for (;;) {
		if (m_current >= m_files.size())
			return Ok(size_t(0));
		const FileRecord& record = m_source->Record(m_files[m_current]);
		if (!m_open) {
			auto packed = m_source->OpenPacked(m_files[m_current]);
			if (packed.IsError())
				return Err(packed.Error());
			m_packed = std::make_unique<ArchiveStream>(std::move(packed).Unwrap());
			m_buffer = std::make_unique<InputBuffer>(m_packed->io, m_packed->state);
			if (record.method != 0) {
				if (!m_unpacker)
					m_unpacker = std::make_unique<Unpacker>();
				const uint64_t destSize = record.unknownSize ? UINT64_MAX : record.unpSize;
				auto begun = m_unpacker->Begin(*m_buffer, destSize, record.method,
											   record.window, record.solid && m_current > 0);
				if (begun.IsError())
					return Err(begun.Error());
			}
			m_open = true;
			m_produced = 0;
		}
		const uint64_t left = record.unknownSize ? UINT64_MAX : record.unpSize - m_produced;
		size_t done = 0;
		if (left > 0) {
			const size_t want = size_t(std::min<uint64_t>(max, left));
			if (record.method == 0) {
				done = m_buffer->ReadRaw(out, want);
				if (m_buffer->Failed())
					return Err(m_buffer->Failure("RAR"));
			} else {
				auto produced = m_unpacker->Produce(out, want);
				if (produced.IsError())
					return produced;
				done = produced.Value();
			}
		}
		if (done > 0) {
			m_produced += done;
			return Ok(done);
		}
		if (!record.unknownSize && m_produced < record.unpSize)
			return Err(
				MakeError(ErrorKind::CORRUPT,
						  String::Format("%s : données compressées tronquées ou corrompues",
										 m_source->Name(m_files[m_current]).CStr())));
		// Fichier suivant de la suite.
		m_open = false;
		m_buffer.reset();
		m_packed.reset();
		++m_current;
	}
}

bool ChainImpl::Restart() {
	m_current = 0;
	m_open = false;
	m_buffer.reset();
	m_packed.reset();
	m_unpacker.reset();
	return true;
}

const char* MethodName(int method) noexcept {
	switch (method) {
	case 0:
		return "stockée";
	case 15:
		return "rar 1.5";
	case 20:
		return "rar 2.0";
	case 29:
		return "rar 2.9";
	case 50:
		return "rar 5";
	case 70:
		return "rar 7";
	default:
		return "rar ?";
	}
}

const char* CryptName(Crypt crypt) noexcept {
	switch (crypt) {
	case Crypt::RAR13:
		return " + rar 1.3";
	case Crypt::RAR15:
		return " + rar 1.5";
	case Crypt::RAR20:
		return " + rar 2.0";
	case Crypt::RAR30:
		return " + aes-128";
	case Crypt::RAR50:
		return " + aes-256";
	default:
		return "";
	}
}

uint64_t RoundUpPow2(uint64_t value) noexcept {
	uint64_t power = 1;
	while (power < value)
		power <<= 1;
	return power;
}

} // namespace detail::rar

String RarVolumeName(const String& firstPath, uint32_t index, bool newNumbering) {
	const String lower = firstPath.ToLower();
	if (newNumbering) {
		// « nom.partN.rar » : N garde sa largeur (part01, part001…).
		if (!lower.EndsWith(".rar"))
			return String();
		size_t end = lower.GetSize() - 4, start = end;
		while (start > 0 && lower.CharAt(start - 1) >= '0' && lower.CharAt(start - 1) <= '9')
			--start;
		if (start == end || start < 5 || lower.Substr(start - 5, 5) != ".part")
			return String();
		String digits = String::Format("%u", index + 1);
		while (digits.GetSize() < end - start)
			digits = "0" + digits;
		return firstPath.Substr(0, start) + digits + firstPath.Substr(end);
	}
	// Ancien nommage : .rar, .r00 … .r99, .s00…
	const size_t dot = lower.Rfind('.');
	if (dot == String::NPOS)
		return String();
	if (index == 0)
		return firstPath;
	const uint32_t n = index - 1;
	const bool upper = firstPath.GetSize() > dot + 1 && firstPath.CharAt(dot + 1) == 'R';
	const char letter = char((upper ? 'R' : 'r') + n / 100);
	return firstPath.Substr(0, dot + 1) + String::Format("%c%02u", letter, n % 100);
}

// ── RarReader ────────────────────────────────────────────────────────────────

Result<std::unique_ptr<RarReader>, ArchiveError> RarReader::Open(ArchiveSource source, const ReadOptions& options, RarVolumeOpener opener) {
	std::unique_ptr<RarReader> reader(new RarReader(options, std::move(opener)));
	reader->m_volumes.push_back(std::make_unique<ArchiveSource>(std::move(source)));
	if (auto error = reader->Index(); error.IsSome())
		return Err(error.Unwrap());
	return Ok(std::move(reader));
}

bool RarReader::LooksLikeRar(std::span<const uint8_t> head) noexcept {
	return head.size() >= 7 && std::memcmp(head.data(), detail::rar::SIGNATURE4, 6) == 0;
}

const std::vector<EntryInfo>& RarReader::Entries() const noexcept {
	return m_entries;
}

void RarReader::SetPassword(const String& password) {
	m_options.password = password;
	m_cachedChain = NONE;
	m_cachedBytes.reset();
	m_cachedStream.reset();
	ResolveLinkTargets();
}

Result<ArchiveStream, ArchiveError> RarReader::OpenEntry(size_t index) {
	return OpenFollowing(index, 0);
}

const detail::rar::FileRecord& RarReader::Record(size_t file) const {
	return m_records[file];
}

Result<ArchiveStream, ArchiveError> RarReader::OpenPacked(size_t file) {
	using namespace detail::rar;
	const FileRecord& record = m_records[file];
	const EntryInfo& entry = m_entries[file];
	if (record.continued)
		return Err(MakeError(ErrorKind::IO, String::Format("%s : suite dans un volume absent",
														   entry.path.CStr())));
	std::vector<ArchiveStream> parts;
	uint64_t total = 0;
	for (const Part& part : record.parts) {
		ArchiveSource& volume = *m_volumes[part.volume];
		if (part.offset > volume.Size() || part.size > volume.Size() - part.offset)
			return Err(MakeError(
				ErrorKind::CORRUPT,
				String::Format("%s : données hors du volume (tronqué ?)", entry.path.CStr())));
		auto window = OpenSubStream(volume.Stream(), part.offset, part.size);
		if (window.IsError())
			return window;
		parts.push_back(std::move(window).Unwrap());
		total += part.size;
	}
	Result<ArchiveStream, ArchiveError> packed =
		parts.size() == 1 ? Result<ArchiveStream, ArchiveError>(Ok(std::move(parts[0])))
						  : OpenConcatStream(std::move(parts), Some(total));
	if (packed.IsError() || record.crypt == Crypt::NONE)
		return packed;
	if (m_options.password.IsEmpty())
		return Err(MakeError(ErrorKind::PASSWORD_REQUIRED,
							 String::Format("%s est chiffré", entry.path.CStr())));
	auto state = std::make_shared<StreamState>();
	switch (record.crypt) {
	case Crypt::RAR50: {
		const Rar5Key& key = m_keys.Rar5(m_options.password, record.salt, record.lg2);
		if (record.hasPswCheck && key.check != record.pswCheck)
			return Err(
				MakeError(ErrorKind::WRONG_PASSWORD,
						  String::Format("%s : mot de passe incorrect", entry.path.CStr())));
		auto aes = Aes::Create(key.key);
		if (aes.IsError())
			return Err(MakeError(ErrorKind::CORRUPT, aes.Error()));
		return MakeStream(std::make_unique<AesCbcDecryptImpl>(
							  std::move(packed).Unwrap(), aes.Value(), record.iv, total, state),
						  state);
	}
	case Crypt::RAR30: {
		const Rar3Key& key =
			m_keys.Rar3(m_options.password, record.hasSalt ? record.salt.data() : nullptr);
		auto aes = Aes::Create(key.key);
		if (aes.IsError())
			return Err(MakeError(ErrorKind::CORRUPT, aes.Error()));
		return MakeStream(std::make_unique<AesCbcDecryptImpl>(
							  std::move(packed).Unwrap(), aes.Value(), key.iv, total, state),
						  state);
	}
	default: {
		const LegacyCipher cipher = record.crypt == Crypt::RAR13   ? LegacyCipher::RAR13
									: record.crypt == Crypt::RAR15 ? LegacyCipher::RAR15
																   : LegacyCipher::RAR20;
		return MakeStream(std::make_unique<LegacyDecryptImpl>(
							  std::move(packed).Unwrap(),
							  LegacyDecryptor(cipher, m_options.password), total, state),
						  state);
	}
	}
}

Result<ArchiveStream, ArchiveError> RarReader::OpenFollowing(size_t index, int depth) {
	using namespace detail::rar;
	if (index >= m_entries.size())
		return Err(MakeError(ErrorKind::INVALID_ARGUMENT, String("entrée inexistante")));
	const EntryInfo& entry = m_entries[index];
	const FileRecord& record = m_records[index];
	if (entry.type == EntryType::DIRECTORY)
		return OpenMemoryStream(Bytes());
	if (record.method < 0)
		return Err(MakeError(ErrorKind::UNSUPPORTED,
							 String::Format("%s : méthode %s non gérée", entry.path.CStr(),
											entry.method.CStr())));
	if (record.redirect == Redirect::UNIX_SYMLINK || record.redirect == Redirect::WIN_SYMLINK ||
		record.redirect == Redirect::JUNCTION)
		return OpenMemoryStream(Bytes(entry.linkTarget.CStr(),
									  entry.linkTarget.CStr() + entry.linkTarget.GetSize()));
	if (record.redirect == Redirect::HARDLINK || record.redirect == Redirect::FILECOPY) {
		if (depth > 16)
			return Err(MakeError(ErrorKind::CORRUPT,
								 String::Format("%s : boucle de liens", entry.path.CStr())));
		for (size_t i = index; i-- > 0;)
			if (m_entries[i].path == entry.linkTarget)
				return OpenFollowing(i, depth + 1);
		return Err(MakeError(ErrorKind::CORRUPT,
							 String::Format("%s : cible « %s » absente", entry.path.CStr(),
											entry.linkTarget.CStr())));
	}
	// Chaîne solide : du premier fichier de la suite jusqu'au dernier.
	std::vector<size_t> chain;
	uint64_t offset = 0, total = 0;
	bool sizeKnown = true;
	for (size_t i = record.chainStart; i < m_records.size(); ++i) {
		if (!HasData(i))
			continue;
		if (i != record.chainStart && m_records[i].chainStart != record.chainStart)
			break;
		if (i == index)
			offset = total;
		chain.push_back(i);
		if (m_records[i].unknownSize)
			sizeKnown = false;
		total += m_records[i].unpSize;
	}
	const ErrorKind mismatch = record.crypt != Crypt::NONE && !record.hasPswCheck
								   ? ErrorKind::WRONG_PASSWORD
								   : ErrorKind::CORRUPT;
	Result<ArchiveStream, ArchiveError> data = Err(ArchiveError{});
	if (chain.size() == 1) {
		auto state = std::make_shared<StreamState>();
		data = MakeStream(
			std::make_unique<ChainImpl>(static_cast<detail::rar::PackedSource*>(this), chain,
										record.unknownSize ? Option<uint64_t>(NONE)
														   : Option<uint64_t>(Some(total)),
										state),
			state);
	} else {
		if (!sizeKnown)
			return Err(MakeError(
				ErrorKind::UNSUPPORTED,
				String::Format("%s : taille inconnue dans un bloc solide", entry.path.CStr())));
		data = OpenSolid(record.chainStart, chain, total, offset, record.unpSize);
	}
	if (data.IsError())
		return data;
	Option<std::array<uint8_t, 32>> hashKey = NONE;
	if (record.crypt == Crypt::RAR50 && record.hashMac && !m_options.password.IsEmpty())
		hashKey = Some(m_keys.Rar5(m_options.password, record.salt, record.lg2).hashKey);
	const bool verify = m_options.verifyChecksums && !record.continued;
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<CheckImpl>(
						  std::move(data).Unwrap(),
						  record.unknownSize ? Option<uint64_t>(NONE)
											 : Option<uint64_t>(Some(record.unpSize)),
						  verify ? record.crc : Option<uint32_t>(NONE),
						  verify ? record.blake : Option<std::array<uint8_t, 32>>(NONE),
						  hashKey, entry.path, mismatch, state),
					  state);
}

bool RarReader::HasData(size_t i) const {
	const detail::rar::FileRecord& record = m_records[i];
	return !record.directory && record.redirect == detail::rar::Redirect::NONE;
}

Result<ArchiveStream, ArchiveError> RarReader::OpenSolid(size_t chainStart,
		const std::vector<size_t>& chain,
		uint64_t total, uint64_t offset,
		uint64_t size) {
	using namespace detail::rar;
	if (m_cachedChain.IsNone() || m_cachedChain.Unwrap() != chainStart) {
		m_cachedBytes.reset();
		m_cachedStream.reset();
		m_cachedChain = NONE;
		auto state = std::make_shared<StreamState>();
		auto stream = MakeStream(
			std::make_unique<ChainImpl>(static_cast<detail::rar::PackedSource*>(this), chain,
										Some(total), state),
			state);
		if (stream.IsError())
			return stream;
		if (total <= m_options.memoryCacheLimit) {
			auto bytes = ReadStreamToEnd(stream.Value(), m_options.memoryCacheLimit, total);
			if (bytes.IsError())
				return Err(bytes.Error());
			m_cachedBytes = std::make_shared<const Bytes>(std::move(bytes).Unwrap());
		} else {
			m_cachedStream = std::make_shared<ArchiveStream>(std::move(stream).Unwrap());
		}
		m_cachedChain = Some(chainStart);
	}
	if (m_cachedBytes)
		return OpenSharedMemoryStream(m_cachedBytes, offset, size);
	return OpenSharedSubStream(m_cachedStream, offset, size);
}

void RarReader::ResolveLinkTargets() {
	for (size_t i = 0; i < m_entries.size(); ++i) {
		if (!m_records[i].rar4Symlink || !m_entries[i].linkTarget.IsEmpty() ||
			m_entries[i].size > 4096 ||
			(m_entries[i].encrypted && m_options.password.IsEmpty()))
			continue;
		auto target = Extract(i);
		if (target.IsOk())
			m_entries[i].linkTarget = String(
				reinterpret_cast<const char*>(target.Value().data()), target.Value().size());
	}
}

ArchiveError RarReader::Corrupt(const char* what) {
	return MakeError(ErrorKind::CORRUPT, String::Format("RAR : %s", what));
}

Result<std::pair<uint64_t, int>, ArchiveError> RarReader::FindSignature(ArchiveSource& source) {
	using namespace detail::rar;
	const uint64_t size = source.Size();
	const uint64_t scan = std::min<uint64_t>(size, MAX_SFX_SIZE);
	const uint64_t chunk = 1 << 16;
	for (uint64_t base = 0; base < scan; base += chunk) {
		auto bytes =
			ReadRange(source.Stream(), base, std::min<uint64_t>(chunk + 8, size - base));
		if (bytes.IsError())
			break;
		const Bytes& b = bytes.Value();
		if (base == 0 && b.size() >= 4 && std::memcmp(b.data(), "RE~^", 4) == 0)
			return Err(MakeError(ErrorKind::UNSUPPORTED, String("RAR : format 1.4 non géré")));
		for (size_t at = 0; at + 8 <= b.size() && at < chunk; ++at) {
			if (b[at] != 'R' || std::memcmp(b.data() + at, SIGNATURE4, 6) != 0)
				continue;
			if (b[at + 6] == 0x00)
				return Ok(std::make_pair(base + at, 4));
			if (b[at + 6] == 0x01 && b[at + 7] == 0x00)
				return Ok(std::make_pair(base + at, 5));
		}
	}
	return Err(MakeError(ErrorKind::FORMAT, String("RAR : signature absente")));
}

Option<ArchiveError> RarReader::Index() {
	auto signature = FindSignature(*m_volumes[0]);
	if (signature.IsError())
		return Some(signature.Error());
	const auto [position, version] = signature.Value();
	auto error = version == 5 ? Index5(position + 8) : Index4(position + 7);
	if (error.IsSome())
		return error;
	Finish();
	return NONE;
}

Result<bool, ArchiveError> RarReader::NextVolume(bool newNumbering, uint64_t& position, int version) {
	if (!m_opener)
		return Ok(false);
	auto source = m_opener(uint32_t(m_volumes.size()), newNumbering);
	if (source.IsNone())
		return Ok(false);
	m_volumes.push_back(std::make_unique<ArchiveSource>(std::move(source).Unwrap()));
	auto signature = FindSignature(*m_volumes.back());
	if (signature.IsError() || signature.Value().second != version)
		return Err(MakeError(ErrorKind::CORRUPT,
							 String::Format("RAR : volume %zu invalide", m_volumes.size())));
	position = signature.Value().first + (version == 5 ? 8 : 7);
	return Ok(true);
}

void RarReader::AddRecord(EntryInfo entry, detail::rar::FileRecord record, bool splitBefore, bool splitAfter) {
	if (splitBefore && !m_records.empty() && m_records.back().continued &&
		m_entries.back().path == entry.path) {
		detail::rar::FileRecord& previous = m_records.back();
		previous.parts.insert(previous.parts.end(), record.parts.begin(), record.parts.end());
		previous.continued = splitAfter;
		// Empreinte du fichier entier : dans l'en-tête de la dernière partie.
		if (!splitAfter) {
			previous.crc = record.crc;
			previous.blake = record.blake;
		}
		m_entries.back().packedSize += record.parts.empty() ? 0 : record.parts.back().size;
		return;
	}
	if (splitBefore)
		return; // suite d'un fichier du volume précédent (archive ouverte en
				// cours de route)
	record.continued = splitAfter;
	m_entries.push_back(std::move(entry));
	m_records.push_back(std::move(record));
}

void RarReader::Finish() {
	size_t previous = SIZE_MAX;
	for (size_t i = 0; i < m_records.size(); ++i) {
		detail::rar::FileRecord& record = m_records[i];
		record.chainStart = i;
		if (!HasData(i))
			continue;
		if (record.solid && previous != SIZE_MAX) {
			record.chainStart = m_records[previous].chainStart;
			m_solid = true;
		}
		previous = i;
	}
	// Fichier seul (non suivi d'un fichier solide) : dictionnaire réduit à
	// sa taille, les distances ne pouvant pas la dépasser.
	for (size_t i = 0; i < m_records.size(); ++i) {
		detail::rar::FileRecord& record = m_records[i];
		if (!HasData(i) || record.chainStart != i || record.unknownSize)
			continue;
		bool followed = false;
		for (size_t j = i + 1; j < m_records.size() && !followed; ++j)
			followed = HasData(j) && m_records[j].chainStart == i;
		if (!followed)
			record.window =
				std::min(record.window,
						 detail::rar::RoundUpPow2(std::max<uint64_t>(record.unpSize, 0x40000)));
	}
	ResolveLinkTargets();
}

void RarReader::SetPath(EntryInfo& entry, String name) {
	auto normalized = NormalizePath(name);
	entry.path = normalized.IsOk() ? normalized.Value() : name;
}

Result<Bytes, ArchiveError> RarReader::DecryptHeader(ArchiveSource& source, uint64_t position, const Aes& aes,
		const std::array<uint8_t, 16>& iv, size_t aligned) {
	auto bytes = ReadRange(source.Stream(), position, aligned);
	if (bytes.IsError())
		return Err(Corrupt("en-tête chiffré tronqué"));
	Bytes data = std::move(bytes).Unwrap();
	if (AesCbcDecrypt(aes, std::span<const uint8_t, 16>(iv), data).IsError())
		return Err(Corrupt("en-tête chiffré invalide"));
	return Ok(std::move(data));
}

Option<ArchiveError> RarReader::Index4(uint64_t position) {
	using namespace detail::rar;
	uint32_t volume = 0;
	bool archiveSolid = false, volumeArchive = false, newNumbering = false,
		 encryptedHeaders = false;
	bool mainSeen = false, endSeen = false, nextVolumeFlag = false;
	size_t fileCount = 0;
	for (;;) {
		ArchiveSource& source = *m_volumes[volume];
		const uint64_t size = source.Size();
		bool volumeDone = position + 7 > size;
		if (!volumeDone) {
			Bytes header;
			uint64_t headerTotal = 0;
			if (encryptedHeaders && mainSeen) {
				if (m_options.password.IsEmpty())
					return Some(MakeError(
						ErrorKind::PASSWORD_REQUIRED,
						String("RAR : liste des fichiers chiffrée, mot de passe requis")));
				auto salt = ReadRange(source.Stream(), position, 8);
				if (salt.IsError())
					return Some(Corrupt("sel d'en-tête tronqué"));
				const Rar3Key& key = m_keys.Rar3(m_options.password, salt.Value().data());
				auto aes = Aes::Create(key.key);
				if (aes.IsError())
					return Some(Corrupt("clé AES invalide"));
				auto first = DecryptHeader(source, position + 8, aes.Value(), key.iv, 16);
				if (first.IsError())
					return Some(first.Error());
				const uint16_t headSize = uint16_t(first.Value()[5] | (first.Value()[6] << 8));
				const size_t aligned = (size_t(headSize) + 15) / 16 * 16;
				// Clé fausse : taille incohérente.
				if (headSize < 7 || position + 8 + aligned > size)
					return Some(MakeError(ErrorKind::WRONG_PASSWORD,
										  String("RAR : mot de passe incorrect")));
				auto full = DecryptHeader(source, position + 8, aes.Value(), key.iv, aligned);
				if (full.IsError())
					return Some(full.Error());
				header = std::move(full).Unwrap();
				header.resize(headSize);
				headerTotal = 8 + aligned;
			} else {
				auto head = ReadRange(source.Stream(), position, 7);
				if (head.IsError())
					return Some(Corrupt("en-tête tronqué"));
				const uint16_t headSize = uint16_t(head.Value()[5] | (head.Value()[6] << 8));
				if (headSize < 7)
					return Some(Corrupt("en-tête invalide"));
				// En-tête coupé par la fin du fichier (archive tronquée) : les
				// entrées complètes restent lisibles, comme avec UnRAR.
				if (position + headSize > size)
					break;
				auto full = ReadRange(source.Stream(), position, headSize);
				if (full.IsError())
					return Some(Corrupt("en-tête tronqué"));
				header = std::move(full).Unwrap();
				headerTotal = headSize;
			}
			HeaderReader r(header);
			const uint16_t headCrc = r.U16();
			const uint8_t type = r.U8();
			const uint16_t flags = r.U16();
			(void)r.U16();
			size_t crcLength = header.size();
			if ((type == 0x73 && (flags & 0x02)) || type == 0x75)
				crcLength = std::min<size_t>(13, crcLength);
			const bool crcOk =
				(Crc32(std::span<const uint8_t>(header).subspan(2, crcLength - 2)) & 0xFFFF) ==
				headCrc;
			if (!crcOk && type != 0x79 && type != 0x76 && type != 0x77 &&
				!(type == 0x74 && (flags & 0x08))) {
				if (encryptedHeaders && mainSeen)
					return Some(MakeError(ErrorKind::WRONG_PASSWORD,
										  String("RAR : mot de passe incorrect")));
				return Some(Corrupt("CRC d'en-tête incorrect"));
			}
			uint64_t dataSize = 0;
			switch (type) {
			case 0x73: // principal
				mainSeen = true;
				volumeArchive = (flags & 0x0001) != 0;
				archiveSolid = (flags & 0x0008) != 0;
				newNumbering = (flags & 0x0010) != 0;
				encryptedHeaders = (flags & 0x0080) != 0;
				m_headersEncrypted |= encryptedHeaders;
				break;
			case 0x74:	 // fichier
			case 0x7A: { // service (commentaire, ACL…) : ignoré
				const uint32_t packLow = r.U32(), unpLow = r.U32();
				const uint8_t host = r.U8();
				const uint32_t fileCrc = r.U32();
				const uint32_t fileTime = r.U32();
				const uint8_t unpVer = r.U8();
				const uint8_t methodByte = r.U8();
				const uint16_t nameSize = r.U16();
				const uint32_t attr = r.U32();
				uint64_t packSize = packLow, unpSize = unpLow;
				bool unknown = unpLow == 0xFFFFFFFFu;
				if (flags & 0x0100) {
					packSize |= uint64_t(r.U32()) << 32;
					const uint32_t high = r.U32();
					unpSize |= uint64_t(high) << 32;
					unknown = unpLow == 0xFFFFFFFFu && high == 0xFFFFFFFFu;
				}
				const auto nameBytes = r.Bytes(nameSize);
				std::array<uint8_t, 16> salt{};
				if (flags & 0x0400) {
					const auto s = r.Bytes(8);
					if (r.Ok())
						std::memcpy(salt.data(), s.data(), 8);
				}
				int64_t modified = UnixFromDos(uint16_t(fileTime >> 16), uint16_t(fileTime));
				if (flags & 0x1000) {
					const uint16_t timeFlags = r.U16();
					for (int i = 0; i < 4; ++i) {
						const uint32_t rmode = uint32_t(timeFlags) >> ((3 - i) * 4);
						if (!(rmode & 8))
							continue;
						if (i != 0)
							(void)r.U32();
						else if (rmode & 4)
							++modified;
						(void)r.Bytes(rmode & 3);
					}
				}
				if (!r.Ok())
					return Some(Corrupt("en-tête de fichier tronqué"));
				dataSize = packSize;
				if (type != 0x74)
					break;
				FileRecord record;
				EntryInfo entry;
				record.parts.push_back({volume, position + headerTotal, packSize});
				record.unpSize = unknown ? 0 : unpSize;
				record.unknownSize = unknown;
				record.directory = (flags & 0x00E0) == 0x00E0 || (unpVer < 20 && (attr & 0x10));
				record.window = uint64_t(0x10000) << ((flags & 0x00E0) >> 5);
				const int method = methodByte - 0x30;
				record.method = method == 0	   ? 0
								: unpVer <= 15 ? 15
								: unpVer < 29  ? 20
								: unpVer == 29 ? 29
											   : -1;
				record.solid =
					unpVer <= 15 ? archiveSolid && fileCount > 0 : (flags & 0x0010) != 0;
				record.crc = Some(fileCrc);
				record.hasSalt = (flags & 0x0400) != 0;
				record.salt = salt;
				if (flags & 0x0004)
					record.crypt = unpVer == 13						? Crypt::RAR13
								   : unpVer == 15					? Crypt::RAR15
								   : (unpVer == 20 || unpVer == 26) ? Crypt::RAR20
																	: Crypt::RAR30;
				// Nom : Unicode compressé, sinon UTF-8 ou page de code OEM.
				String name;
				const auto nul = std::find(nameBytes.begin(), nameBytes.end(), uint8_t(0));
				if ((flags & 0x0200) && nul != nameBytes.end()) {
					const size_t ascii = size_t(nul - nameBytes.begin());
					name = DecodeRar4UnicodeName(nameBytes.subspan(0, ascii),
												 nameBytes.subspan(ascii + 1));
				}
				if (name.IsEmpty()) {
					const auto plain = nameBytes.subspan(0, size_t(nul - nameBytes.begin()));
					name =
						IsValidUtf8(plain)
							? String(reinterpret_cast<const char*>(plain.data()), plain.size())
							: Cp437ToUtf8(plain);
				}
				if (host <= 2)
					name = name.Replace("\\", "/");
				SetPath(entry, name);
				const bool unixHost = host == 3 || host == 5;
				record.rar4Symlink =
					host == 3 && (attr & 0xF000) == 0xA000 && !record.directory;
				entry.type = record.directory
								 ? EntryType::DIRECTORY
								 : (record.rar4Symlink ? EntryType::SYMLINK : EntryType::FILE);
				entry.mode = unixHost
								 ? (attr & 07777)
								 : (record.directory ? 0755u : ((attr & 1) ? 0444u : 0644u));
				entry.modifiedTime = modified;
				entry.size = record.directory ? 0 : record.unpSize;
				entry.packedSize = packSize;
				entry.crc32 = record.directory ? Option<uint32_t>(NONE) : record.crc;
				entry.encrypted = record.crypt != Crypt::NONE;
				entry.method = String(MethodName(record.method < 0 ? 29 : record.method));
				if (record.method < 0)
					entry.method = String::Format("rar (version %u)", unsigned(unpVer));
				entry.method.Concat(CryptName(record.crypt));
				if (!(flags & 0x0001))
					++fileCount;
				AddRecord(std::move(entry), std::move(record), (flags & 0x0001) != 0,
						  (flags & 0x0002) != 0);
				break;
			}
			case 0x7B: // fin d'archive
				endSeen = true;
				nextVolumeFlag = (flags & 0x0001) != 0;
				volumeDone = true;
				break;
			default:
				if (flags & 0x8000)
					dataSize = r.U32();
				break;
			}
			if (!volumeDone) {
				position += headerTotal + dataSize;
				continue;
			}
		}
		// Fin du volume : le suivant s'il y a lieu.
		const bool continued = !m_records.empty() && m_records.back().continued;
		if (!volumeArchive || !(continued || nextVolumeFlag || !endSeen))
			break;
		auto next = NextVolume(newNumbering, position, 4);
		if (next.IsError())
			return Some(next.Error());
		if (!next.Value())
			break;
		++volume;
		mainSeen = endSeen = nextVolumeFlag = false;
	}
	return NONE;
}

Option<ArchiveError> RarReader::Index5(uint64_t position) {
	using namespace detail::rar;
	uint32_t volume = 0;
	bool volumeArchive = false;
	Option<Aes> headerAes = NONE;
	for (;;) {
		ArchiveSource& source = *m_volumes[volume];
		const uint64_t size = source.Size();
		bool volumeDone = position + 7 > size;
		bool nextVolumeFlag = false;
		if (!volumeDone) {
			Bytes header;
			uint64_t headerTotal = 0;
			if (headerAes.IsSome()) {
				auto ivBytes = ReadRange(source.Stream(), position, 16);
				if (ivBytes.IsError())
					return Some(Corrupt("en-tête chiffré tronqué"));
				std::array<uint8_t, 16> iv{};
				std::memcpy(iv.data(), ivBytes.Value().data(), 16);
				auto first = DecryptHeader(source, position + 16, headerAes.Value(), iv, 16);
				if (first.IsError())
					return Some(first.Error());
				HeaderReader probe(first.Value());
				(void)probe.U32();
				const uint64_t blockSize = probe.V();
				if (!probe.Ok() || blockSize == 0 || blockSize > (uint64_t(2) << 20))
					return Some(Corrupt("en-tête chiffré invalide"));
				const size_t total = probe.Position() + size_t(blockSize);
				const size_t aligned = (total + 15) / 16 * 16;
				if (position + 16 + aligned > size)
					break;
				auto full =
					DecryptHeader(source, position + 16, headerAes.Value(), iv, aligned);
				if (full.IsError())
					return Some(full.Error());
				header = std::move(full).Unwrap();
				header.resize(total);
				headerTotal = 16 + aligned;
			} else {
				auto head = ReadRange(source.Stream(), position,
									  std::min<uint64_t>(7, size - position));
				if (head.IsError())
					return Some(Corrupt("en-tête tronqué"));
				HeaderReader probe(head.Value());
				(void)probe.U32();
				const uint64_t blockSize = probe.V();
				if (!probe.Ok() || blockSize == 0 || blockSize > (uint64_t(2) << 20))
					return Some(Corrupt("en-tête invalide"));
				const size_t total = probe.Position() + size_t(blockSize);
				if (position + total > size)
					break;
				auto full = ReadRange(source.Stream(), position, total);
				if (full.IsError())
					return Some(Corrupt("en-tête tronqué"));
				header = std::move(full).Unwrap();
				headerTotal = total;
			}
			HeaderReader r(header);
			const uint32_t headCrc = r.U32();
			(void)r.V();
			if (Crc32(std::span<const uint8_t>(header).subspan(4)) != headCrc) {
				if (headerAes.IsSome())
					return Some(MakeError(ErrorKind::WRONG_PASSWORD,
										  String("RAR : mot de passe incorrect")));
				return Some(Corrupt("CRC d'en-tête incorrect"));
			}
			const uint64_t type = r.V();
			const uint64_t flags = r.V();
			const uint64_t extraSize = (flags & 0x0001) ? r.V() : 0;
			const uint64_t dataSize = (flags & 0x0002) ? r.V() : 0;
			if (!r.Ok() || extraSize >= header.size())
				return Some(Corrupt("en-tête mal formé"));
			switch (type) {
			case 4: { // chiffrement des en-têtes
				const uint64_t version = r.V();
				const uint64_t encryptionFlags = r.V();
				const uint32_t lg2 = r.U8();
				const auto salt = r.Bytes(16);
				std::array<uint8_t, 8> check{};
				bool hasCheck = false;
				if (encryptionFlags & 1) {
					const auto c = r.Bytes(8);
					const auto sum = r.Bytes(4);
					if (r.Ok()) {
						std::memcpy(check.data(), c.data(), 8);
						Sha256 sha;
						sha.Update(c);
						const auto digest = sha.Final();
						hasCheck = std::memcmp(digest.data(), sum.data(), 4) == 0;
					}
				}
				if (!r.Ok())
					return Some(Corrupt("en-tête de chiffrement tronqué"));
				if (version > 0 || lg2 > 24)
					return Some(MakeError(ErrorKind::UNSUPPORTED,
										  String("RAR : version de chiffrement inconnue")));
				m_headersEncrypted = true;
				if (m_options.password.IsEmpty())
					return Some(MakeError(
						ErrorKind::PASSWORD_REQUIRED,
						String("RAR : liste des fichiers chiffrée, mot de passe requis")));
				const Rar5Key& key = m_keys.Rar5(m_options.password, salt, lg2);
				if (hasCheck && key.check != check)
					return Some(MakeError(ErrorKind::WRONG_PASSWORD,
										  String("RAR : mot de passe incorrect")));
				auto aes = Aes::Create(key.key);
				if (aes.IsError())
					return Some(Corrupt("clé AES invalide"));
				headerAes = Some(std::move(aes).Unwrap());
				break;
			}
			case 1: { // principal
				const uint64_t archiveFlags = r.V();
				volumeArchive = (archiveFlags & 0x0001) != 0;
				const uint64_t volumeNumber = (archiveFlags & 0x0002) ? r.V() : 0;
				if (volume == 0 && volumeNumber > 0)
					return Some(MakeError(
						ErrorKind::UNSUPPORTED,
						String::Format("RAR : volume %llu d'une archive, ouvrez le premier",
									   static_cast<unsigned long long>(volumeNumber + 1))));
				break;
			}
			case 2: // fichier
				if (auto error = ParseFile5(r, header, extraSize, flags, volume,
											position + headerTotal, dataSize);
					error.IsSome())
					return error;
				break;
			case 5: { // fin d'archive
				const uint64_t endFlags = r.V();
				nextVolumeFlag = (endFlags & 0x0001) != 0;
				volumeDone = true;
				break;
			}
			default: // service (commentaire, accès rapide…) et types inconnus
				break;
			}
			if (!volumeDone) {
				position += headerTotal + dataSize;
				continue;
			}
		}
		const bool continued = !m_records.empty() && m_records.back().continued;
		if (!volumeArchive || !(nextVolumeFlag || continued))
			break;
		auto next = NextVolume(true, position, 5);
		if (next.IsError())
			return Some(next.Error());
		if (!next.Value())
			break;
		++volume;
		headerAes = NONE; // chaque volume a son propre en-tête de chiffrement
	}
	return NONE;
}

Option<ArchiveError> RarReader::ParseFile5(detail::rar::HeaderReader& r, const Bytes& header,
		uint64_t extraSize, uint64_t blockFlags,
		uint32_t volume, uint64_t dataOffset,
		uint64_t dataSize) {
	using namespace detail::rar;
	FileRecord record;
	EntryInfo entry;
	const uint64_t fileFlags = r.V();
	const uint64_t unpSize = r.V();
	const uint64_t attr = r.V();
	if (fileFlags & 0x0002)
		entry.modifiedTime = int64_t(r.U32());
	if (fileFlags & 0x0004)
		record.crc = Some(r.U32());
	const uint64_t compInfo = r.V();
	const uint64_t host = r.V();
	const uint64_t nameSize = r.V();
	if (!r.Ok() || nameSize > r.Left())
		return Some(Corrupt("en-tête de fichier tronqué"));
	const auto nameBytes = r.Bytes(size_t(nameSize));
	String name(reinterpret_cast<const char*>(nameBytes.data()), nameBytes.size());
	if (!r.Ok())
		return Some(Corrupt("en-tête de fichier tronqué"));

	// Champs extra, à la fin de l'en-tête.
	if (extraSize > 0) {
		r.Seek(header.size() - size_t(extraSize));
		while (r.Ok() && r.Left() >= 2) {
			const uint64_t fieldSize = r.V();
			if (!r.Ok() || fieldSize == 0 || fieldSize > r.Left())
				break;
			const size_t next = r.Position() + size_t(fieldSize);
			const uint64_t fieldType = r.V();
			switch (fieldType) {
			case 1: { // chiffrement
				const uint64_t version = r.V();
				const uint64_t cryptFlags = r.V();
				record.lg2 = r.U8();
				const auto salt = r.Bytes(16);
				const auto iv = r.Bytes(16);
				if (!r.Ok() || version > 0 || record.lg2 > 24) {
					record.method = -2; // chiffrement inconnu
					break;
				}
				std::memcpy(record.salt.data(), salt.data(), 16);
				std::memcpy(record.iv.data(), iv.data(), 16);
				if (cryptFlags & 1) {
					const auto check = r.Bytes(8);
					const auto sum = r.Bytes(4);
					if (r.Ok()) {
						std::memcpy(record.pswCheck.data(), check.data(), 8);
						Sha256 sha;
						sha.Update(check);
						const auto digest = sha.Final();
						record.hasPswCheck = std::memcmp(digest.data(), sum.data(), 4) == 0;
					}
				}
				record.hashMac = (cryptFlags & 2) != 0;
				record.crypt = Crypt::RAR50;
				break;
			}
			case 2: // empreinte
				if (r.V() == 0) {
					const auto digest = r.Bytes(32);
					if (r.Ok()) {
						std::array<uint8_t, 32> value{};
						std::memcpy(value.data(), digest.data(), 32);
						record.blake = Some(value);
					}
				}
				break;
			case 3: { // dates précises
				const uint64_t timeFlags = r.V();
				if (timeFlags & 0x02)
					entry.modifiedTime =
						(timeFlags & 0x01) ? int64_t(r.U32()) : UnixFromFileTime(r.U64());
				break;
			}
			case 4: { // version du fichier
				(void)r.V();
				const uint64_t version = r.V();
				if (r.Ok() && version != 0)
					name = name +
						   String::Format(";%llu", static_cast<unsigned long long>(version));
				break;
			}
			case 5: { // redirection (liens)
				const uint64_t redirectType = r.V();
				(void)r.V();
				const uint64_t targetSize = r.V();
				if (!r.Ok() || targetSize == 0 || targetSize > r.Left())
					break;
				const auto target = r.Bytes(size_t(targetSize));
				if (redirectType >= 1 && redirectType <= 5) {
					record.redirect = Redirect(redirectType);
					record.redirectTarget =
						String(reinterpret_cast<const char*>(target.data()), target.size());
				}
				break;
			}
			default: // propriétaire, sous-données : ignorés
				break;
			}
			r.Seek(next);
		}
	}

	record.parts.push_back({volume, dataOffset, dataSize});
	record.unpSize = unpSize;
	record.unknownSize = (fileFlags & 0x0008) != 0;
	record.directory = (fileFlags & 0x0001) != 0;
	record.solid = (compInfo & 0x0040) != 0;
	const uint64_t version = compInfo & 0x3F;
	const uint64_t method = (compInfo >> 7) & 7;
	if (record.method != -2) {
		if (method == 0)
			record.method = 0;
		else if (version == 0)
			record.method = 50;
		else if (version == 1)
			record.method = (compInfo & 0x100000) ? 50 : 70;
		else
			record.method = -1;
	}
	if (!record.directory && version <= 1) {
		uint64_t window = uint64_t(0x20000)
						  << ((compInfo >> 10) & (version == 0 ? 0x0F : 0x1F));
		if (version == 1)
			window += window / 32 * ((compInfo >> 15) & 0x1F);
		record.window = RoundUpPow2(window);
	}
	SetPath(entry, name);
	switch (record.redirect) {
	case Redirect::UNIX_SYMLINK:
	case Redirect::WIN_SYMLINK:
	case Redirect::JUNCTION: {
		String target = record.redirectTarget;
		if (record.redirect != Redirect::UNIX_SYMLINK) {
			target = target.Replace("\\", "/");
			if (target.StartsWith("/\?\?/"))
				target = target.Substr(4);
		}
		entry.type = EntryType::SYMLINK;
		entry.linkTarget = target;
		entry.size = target.GetSize();
		break;
	}
	case Redirect::HARDLINK:
	case Redirect::FILECOPY: {
		entry.type = EntryType::FILE;
		auto target = NormalizePath(record.redirectTarget);
		entry.linkTarget = target.IsOk() ? target.Value() : record.redirectTarget;
		for (size_t i = m_entries.size(); i-- > 0;)
			if (m_entries[i].path == entry.linkTarget) {
				entry.size = m_entries[i].size;
				break;
			}
		break;
	}
	case Redirect::NONE:
		entry.type = record.directory ? EntryType::DIRECTORY : EntryType::FILE;
		entry.size = record.directory ? 0 : unpSize;
		break;
	}
	if (host == 1)
		entry.mode = uint32_t(attr & 07777);
	else
		entry.mode = record.directory ? 0755u : ((attr & 1) ? 0444u : 0644u);
	entry.packedSize = dataSize;
	entry.encrypted = record.crypt != Crypt::NONE;
	entry.crc32 = record.hashMac || record.directory ? Option<uint32_t>(NONE) : record.crc;
	entry.method = record.method >= 0 ? String(MethodName(record.method))
									  : String("rar (version inconnue)");
	entry.method.Concat(CryptName(record.crypt));
	AddRecord(std::move(entry), std::move(record), (blockFlags & 0x0008) != 0,
			  (blockFlags & 0x0010) != 0);
	return NONE;
}

} // namespace data::archive
