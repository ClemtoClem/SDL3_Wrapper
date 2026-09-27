#pragma once
/**
 * data::archive — RAR (lecture seule) : formats RAR 1.5 à 4.x et RAR 5.0/7.0.
 *
 * Lecture :
 *  - signatures RAR 1.5-4.x (« Rar!\x1A\x07\x00 ») et RAR 5 (« …\x01\x00 »),
 *    archives auto-extractibles (signature cherchée dans les 4 premiers Mio) ;
 *    le format RAR 1.4 (« RE~^ ») → `UNSUPPORTED` ;
 *  - en-têtes RAR 4 (CRC 16 bits, noms Unicode compressés, dates étendues,
 *    grands fichiers) et RAR 5 (nombres variables, champs extra : chiffrement,
 *    empreinte, dates précises, liens, propriétaire), en-têtes de service
 *    (commentaire, accès rapide…) ignorés ;
 *  - méthodes : stockée, RAR 1.5, RAR 2.0 (y compris le mode audio), RAR 2.9
 *    / 3.x (LZ, PPMd variante H, filtres standard E8, E8E9, Itanium, Delta,
 *    RGB, Audio) et RAR 5.0/7.0 (LZ, filtres Delta, E8, E8E9, ARM) ;
 *  - archives solides : chaque suite de fichiers solides est un flux de
 *    décompression partagé (gardé en mémoire s'il tient dans
 *    `ReadOptions::memoryCacheLimit`, sinon relu en flux) ;
 *  - chiffrement : RAR 5 (PBKDF2-HMAC-SHA256, AES-256-CBC, valeur de contrôle
 *    du mot de passe, empreintes converties en HMAC), RAR 3.x (dérivation
 *    SHA-1 à 2^18 tours, AES-128-CBC), anciens chiffrements RAR 1.3, 1.5 et
 *    2.0 ; en-têtes chiffrés (RAR 3.x et 5) ;
 *  - contrôle CRC-32 ou BLAKE2sp de chaque fichier ;
 *  - liens symboliques (Unix, Windows, jonctions) et physiques, copies de
 *    fichiers (RAR 5) ;
 *  - archives multi-volumes (.partN.rar, .rar/.r00…) : les volumes suivants
 *    sont demandés à un `RarVolumeOpener`.
 * Écriture : aucune (format propriétaire, voir ci-dessous).
 *
 * Les décompresseurs sont adaptés du code source d'UnRAR (unpack15, unpack20,
 * unpack30, unpack50, rarvm, crypt), dont la licence impose de reproduire le
 * paragraphe suivant :
 *
 *    UnRAR source code may be used in any software to handle RAR archives
 *    without limitations free of charge, but cannot be used to develop RAR
 *    (WinRAR) compatible archiver and to re-create RAR compression
 *    algorithm, which is proprietary. Distribution of modified UnRAR source
 *    code in separate form or as a part of other software is permitted,
 *    provided that full text of this paragraph, starting from "UnRAR source
 *    code" words, is included in license, or in documentation if license is
 *    not available, and in source code comments of resulting package.
 *
 * BLAKE2sp : implémentation de référence (Samuel Neves, CC0).
 */
#include "../../core/core.hpp"
#include "archive_crc.hpp"
#include "archive_crypto.hpp"
#include "archive_fs.hpp"
#include "archive_io.hpp"
#include "archive_ppmd.hpp"
#include "archive_stream.hpp"
#include "archive_types.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace data::archive {

/// Ouvre le volume `index` (0 = premier) d'une archive multi-volumes ;
/// `newNumbering` : nommage « .partN.rar » (sinon « .rar, .r00, .r01… »).
/// NONE si le volume est introuvable.
using RarVolumeOpener = std::function<Option<ArchiveSource>(uint32_t index, bool newNumbering)>;

namespace detail::rar {

inline constexpr uint8_t SIGNATURE4[7] = {'R', 'a', 'r', '!', 0x1A, 0x07, 0x00};
inline constexpr uint8_t SIGNATURE5[8] = {'R', 'a', 'r', '!', 0x1A, 0x07, 0x01, 0x00};
inline constexpr uint64_t MAX_SFX_SIZE = uint64_t(4) << 20;
/// Dictionnaire maximal accepté (RAR 7 permet jusqu'à 64 Gio).
inline constexpr uint64_t MAX_DICTIONARY = uint64_t(1) << 30;
inline constexpr size_t MAX_PASSWORD = 127;

[[nodiscard]] uint32_t Get4(const uint8_t* p) noexcept;
void Put4(uint32_t v, uint8_t* p) noexcept;
[[nodiscard]] uint32_t Rotl32(uint32_t v, unsigned n) noexcept;
[[nodiscard]] uint32_t Rotr32(uint32_t v, unsigned n) noexcept;

/// Table CRC-32 (polynôme 0xEDB88320) : anciens chiffrements RAR.
[[nodiscard]] const std::array<uint32_t, 256>& CrcTable();

// ============================================================================
// BLAKE2sp (empreinte optionnelle de RAR 5)
// ============================================================================

class Blake2s {
public:
	static constexpr size_t BLOCK = 64;

	void Init(uint32_t nodeOffset, uint32_t nodeDepth, bool lastNode);
	void Update(const uint8_t* in, size_t size);
	void Final(uint8_t* digest);

private:
	void Increment(uint32_t increment);
	void Compress(const uint8_t* block);

	uint32_t m_h[8] = {}, m_t[2] = {}, m_f[2] = {};
	uint8_t m_buffer[2 * BLOCK] = {};
	size_t m_length = 0;
	bool m_lastNode = false;
};

class Blake2sp {
public:
	static constexpr size_t DEGREE = 8;

	Blake2sp() { Reset(); }
	void Reset();
	void Update(std::span<const uint8_t> data);
	[[nodiscard]] std::array<uint8_t, 32> Final();

private:
	Blake2s m_leaves[DEGREE];
	Blake2s m_root;
	uint8_t m_buffer[DEGREE * Blake2s::BLOCK] = {};
	size_t m_length = 0;
};

// ============================================================================
// Dérivation des clés
// ============================================================================

/// SHA-1 de RAR 2.9/3.x : `UpdateRar29` reproduit un défaut historique (le
/// bloc de travail est réécrit dans les données d'entrée quand elles
/// fournissent des blocs complets), dont dépendent les clés des longs mots
/// de passe.
class Rar29Sha1 {
public:
	void Update(const uint8_t* data, size_t size);
	void UpdateRar29(uint8_t* data, size_t size) { Process(data, size, true); }
	[[nodiscard]] std::array<uint32_t, 5> Digest() const;

private:
	void Process(uint8_t* data, size_t size, bool rar29);
	/// `w` rend l'état final du tableau de travail (16 mots circulaires).
	void Transform(const uint8_t* block, uint32_t w[16]);

	uint32_t m_state[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
	uint64_t m_count = 0;
	uint8_t m_buffer[64] = {};
};

struct Rar3Key {
	std::array<uint8_t, 16> key{}, iv{};
};

/// Clé AES-128 et vecteur initial de RAR 2.9/3.x : mot de passe UTF-16LE et
/// sel, 2^18 tours de SHA-1.
[[nodiscard]] Rar3Key DeriveRar3Key(const String& password, const uint8_t* salt);

struct Rar5Key {
	std::array<uint8_t, 32> key{}, hashKey{};
	std::array<uint8_t, 8> check{};
};

/// RAR 5 : PBKDF2-HMAC-SHA256 (2^lg2 tours) ; la même chaîne prolongée de 16
/// puis 16 tours donne la clé d'empreinte et la valeur de contrôle.
[[nodiscard]] Rar5Key DeriveRar5Key(const String& password, std::span<const uint8_t> salt,
										   uint32_t lg2);

/// Empreinte RAR 5 d'un fichier chiffré avec « HashMAC » : CRC ou BLAKE2sp
/// passé dans un HMAC-SHA256 de la clé d'empreinte.
[[nodiscard]] uint32_t Rar5CrcToMac(uint32_t crc, const std::array<uint8_t, 32>& hashKey);
[[nodiscard]] std::array<uint8_t, 32>
Rar5DigestToMac(const std::array<uint8_t, 32>& digest, const std::array<uint8_t, 32>& hashKey);

/// Clés déjà dérivées (la dérivation coûte cher et se répète : un sel par
/// en-tête chiffré en RAR 3, par fichier en RAR 5).
class KeyCache {
public:
	[[nodiscard]] const Rar3Key& Rar3(const String& password, const uint8_t* salt);
	[[nodiscard]] const Rar5Key& Rar5(const String& password, std::span<const uint8_t> salt,
									  uint32_t lg2);

private:
	struct Entry3 {
		String password;
		Bytes salt;
		Rar3Key key;
	};
	struct Entry5 {
		String password;
		Bytes salt;
		uint32_t lg2 = 0;
		Rar5Key key;
	};
	std::vector<Entry3> m_rar3;
	std::vector<Entry5> m_rar5;
};

// ============================================================================
// Anciens chiffrements (RAR 1.3, 1.5, 2.0)
// ============================================================================

enum class LegacyCipher : uint8_t { RAR13, RAR15, RAR20 };

/// Déchiffreur des anciennes versions (clé tirée du mot de passe en octets).
class LegacyDecryptor {
public:
	LegacyDecryptor(LegacyCipher cipher, const String& password);

	/// Taille des blocs traités d'un coup (16 pour RAR 2.0, sinon 1).
	[[nodiscard]] size_t BlockSize() const noexcept;

	void Decrypt(uint8_t* data, size_t size);

private:
	[[nodiscard]] static uint16_t Rotr16(uint16_t v) noexcept;
	[[nodiscard]] uint32_t Subst(uint32_t t) const noexcept;
	void Rounds20(uint8_t* buf, bool decrypt);
	void UpdateKeys20(const uint8_t* buf);
	void EncryptBlock20(uint8_t* buf);
	void DecryptBlock20(uint8_t* buf);

	LegacyCipher m_cipher;
	uint8_t m_key13[3] = {};
	uint16_t m_key15[4] = {};
	uint32_t m_key20[4] = {};
	uint8_t m_subst[256] = {};
};

/// Flux déchiffré par un ancien chiffrement.
class LegacyDecryptImpl final : public DecoderImpl {
public:
	LegacyDecryptImpl(ArchiveStream inner, const LegacyDecryptor& decryptor, uint64_t size,
					  StreamStatePtr state)
		: DecoderImpl(std::move(state), Some(size)), m_inner(std::move(inner)),
		  m_initial(decryptor), m_current(decryptor) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

private:
	ArchiveStream m_inner;
	LegacyDecryptor m_initial, m_current;
	uint8_t m_spare[16] = {};
	size_t m_position = 0, m_end = 0;
};

// ============================================================================
// Décompression (adaptée d'UnRAR : unpack15, unpack20, unpack30, unpack50)
// ============================================================================

inline constexpr uint32_t MAX_LZ_MATCH = 0x1001;
inline constexpr uint32_t MAX_INC_LZ_MATCH = MAX_LZ_MATCH + 3;
inline constexpr uint32_t MAX3_LZ_MATCH = 0x101;
inline constexpr uint32_t MAX3_INC_LZ_MATCH = MAX3_LZ_MATCH + 3;
inline constexpr uint32_t LOW_DIST_REP_COUNT = 16;
inline constexpr uint32_t NC = 306, DCB = 64, DCX = 80, LDC = 16, RC = 44, BC = 20;
inline constexpr uint32_t HUFF_TABLE_SIZEX = NC + DCX + RC + LDC;
inline constexpr uint32_t NC30 = 299, DC30 = 60, LDC30 = 17, RC30 = 28, BC30 = 20;
inline constexpr uint32_t HUFF_TABLE_SIZE30 = NC30 + DC30 + RC30 + LDC30;
inline constexpr uint32_t NC20 = 298, DC20 = 48, RC20 = 28, BC20 = 19, MC20 = 257;
inline constexpr uint32_t LARGEST_TABLE_SIZE = 306;
inline constexpr uint32_t MAX_QUICK_DECODE_BITS = 9;
inline constexpr size_t MAX_UNPACK_FILTERS = 8192;
inline constexpr size_t MAX3_UNPACK_FILTERS = 8192;
inline constexpr uint32_t MAX3_UNPACK_CHANNELS = 1024;
inline constexpr uint32_t MAX_FILTER_BLOCK_SIZE = 0x400000;
inline constexpr size_t UNPACK_MAX_WRITE = 0x400000;
inline constexpr uint32_t VM_MEMSIZE = 0x40000;
inline constexpr uint32_t VM_MEMMASK = VM_MEMSIZE - 1;

enum FilterType : uint8_t {
	FILTER_DELTA = 0,
	FILTER_E8,
	FILTER_E8E9,
	FILTER_ARM,
	FILTER_AUDIO,
	FILTER_RGB,
	FILTER_ITANIUM,
	FILTER_TEXT,
	FILTER_NONE = 10
};

/// Lecture de bits de poids fort en premier dans un tampon d'entrée.
class BitInput {
public:
	static constexpr int MAX_SIZE = 0x8000;

	BitInput() : m_buffer(size_t(MAX_SIZE) + 64, 0) {}

	void Init() noexcept { inAddr = inBit = 0; }
	void AddBits(uint32_t bits) noexcept;
	/// 16 bits à partir de la position courante (le premier en poids fort).
	[[nodiscard]] uint32_t GetBits() const noexcept;
	[[nodiscard]] uint32_t GetBits32() const noexcept;
	[[nodiscard]] uint64_t GetBits64() const noexcept;
	[[nodiscard]] bool Overflow(uint32_t increment) const noexcept;
	[[nodiscard]] uint8_t* Buffer() noexcept { return m_buffer.data(); }

	int inAddr = 0; ///< octet courant
	int inBit = 0;	///< bit courant dans l'octet

private:
	std::vector<uint8_t> m_buffer;
};

struct DecodeTable {
	uint32_t maxNum = 0;
	uint32_t decodeLen[16] = {};
	uint32_t decodePos[16] = {};
	uint32_t quickBits = 0;
	uint8_t quickLen[1 << MAX_QUICK_DECODE_BITS] = {};
	uint16_t quickNum[1 << MAX_QUICK_DECODE_BITS] = {};
	uint16_t decodeNum[LARGEST_TABLE_SIZE] = {};
};

struct BlockHeader {
	int blockSize = 0;
	int blockBitSize = 0;
	int blockStart = 0;
	int headerSize = 0;
	bool lastBlockInFile = false;
	bool tablePresent = false;
};

struct BlockTables {
	DecodeTable ld, dd, ldd, rd, bd;
};

struct Filter5 {
	uint8_t type = FILTER_NONE;
	uint8_t channels = 0;
	bool nextWindow = false;
	size_t blockStart = 0;
	uint32_t blockLength = 0;
};

enum class StandardFilter : uint8_t { NONE, E8, E8E9, ITANIUM, RGB, AUDIO, DELTA };

struct PreparedProgram {
	StandardFilter type = StandardFilter::NONE;
	uint32_t initR[7] = {};
	uint8_t* filteredData = nullptr;
	uint32_t filteredDataSize = 0;
};

struct Filter30 {
	uint32_t blockStart = 0;
	uint32_t blockLength = 0;
	bool nextWindow = false;
	uint32_t parentFilter = 0;
	PreparedProgram program;
};

struct AudioVariables {
	int k1 = 0, k2 = 0, k3 = 0, k4 = 0, k5 = 0;
	int d1 = 0, d2 = 0, d3 = 0, d4 = 0;
	int lastDelta = 0;
	uint32_t dif[11] = {};
	uint32_t byteCount = 0;
	int lastChar = 0;
};

/// « Machine virtuelle » de RAR 3 : seuls les filtres standard (reconnus à
/// leur CRC) existent en pratique ; UnRAR n'exécute rien d'autre.
class RarVm {
public:
	void Init();
	static void Prepare(const uint8_t* code, uint32_t size, PreparedProgram& program);
	static uint32_t ReadData(BitInput& input);
	void SetMemory(size_t position, const uint8_t* data, size_t size);
	void Execute(PreparedProgram& program);

private:
	bool ExecuteStandard(StandardFilter type);
	static uint32_t ItaniumGetBits(const uint8_t* data, uint32_t bitPos, uint32_t bitCount);
	static void ItaniumSetBits(uint8_t* data, uint32_t field, uint32_t bitPos, uint32_t bitCount);

	std::vector<uint8_t> m_mem;
	uint32_t m_r[8] = {};
};

class Unpacker;

/// Décodeur d'intervalle de RAR 3 (Subbotin) lisant dans l'entrée du
/// décompresseur (le PPMd y est entrelacé avec le LZ).
class PpmRangeDecoder {
public:
	explicit PpmRangeDecoder(Unpacker* owner) : m_owner(owner) {}
	void Init();
	[[nodiscard]] uint32_t GetThreshold(uint32_t total);
	void Decode(uint32_t start, uint32_t size);
	/// Contexte binaire : l'intervalle est découpé en 2^14 parts (et non
	/// « le reste » pour le symbole 1, contrairement au codeur de 7z).
	[[nodiscard]] uint32_t DecodeBit(uint32_t size0, uint32_t);

private:
	void Normalize();

	Unpacker* m_owner;
	uint32_t m_low = 0, m_code = 0, m_range = 0;
};

/// Décompresseur RAR (toutes versions). Un même objet sert à toute une suite
/// de fichiers solides : `Begin` démarre un fichier, `Produce` rend sa suite.
class Unpacker {
public:
	Unpacker() : m_rc(this), m_blockTables(std::make_unique<BlockTables>()) {}

	/// `method` : 15, 20, 29, 50 ou 70 ; `window` : taille du dictionnaire
	/// (puissance de deux) ; `destSize` : taille décompressée (UINT64_MAX si
	/// inconnue).
	[[nodiscard]] Result<bool, ArchiveError> Begin(InputBuffer& input, uint64_t destSize,
												   int method, uint64_t window, bool solid);

	/// Suite des données décompressées du fichier en cours ; 0 = fin.
	[[nodiscard]] Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max);

	/// Octet suivant de l'entrée (PPMd).
	uint8_t GetChar();

private:
	// ── Entrée / sortie ─────────────────────────────────────────────────────

	int UnpRead(uint8_t* buffer, size_t size);
	void UnpWrite(const uint8_t* data, size_t size);

	[[nodiscard]] size_t WrapDown(size_t position) const noexcept;
	[[nodiscard]] size_t WrapUp(size_t position) const noexcept;

	// ── Commun ──────────────────────────────────────────────────────────────

	void InsertOldDist(size_t distance);

	void CopyString(uint32_t length, size_t distance);

	uint32_t DecodeNumber(BitInput& inp, const DecodeTable& dec);

	static void MakeDecodeTables(const uint8_t* lengthTable, DecodeTable& dec, uint32_t size);

	void UnpInitData(bool solid);

	// ── RAR 5 ───────────────────────────────────────────────────────────────

	uint32_t SlotToLength(BitInput& inp, uint32_t slot);

	void Unpack5(bool solid);

	uint32_t ReadFilterData(BitInput& inp);

	bool ReadFilter(BitInput& inp, Filter5& filter);

	bool AddFilter(Filter5& filter);

	bool UnpReadBuf();

	void UnpWriteBuf();

	const uint8_t* ApplyFilter(uint8_t* data, uint32_t dataSize, const Filter5& flt);

	void UnpWriteArea(size_t start, size_t end);

	void UnpWriteData(const uint8_t* data, size_t size);

	bool ReadBlockHeader(BitInput& inp, BlockHeader& header);

	bool ReadTables(BitInput& inp, BlockHeader& header, BlockTables& tables);

	// ── RAR 2.9 / 3.x ───────────────────────────────────────────────────────

	int SafePpmDecodeChar();
	int PpmDecodeChar();
	bool PpmDecodeInit();

	void Unpack29(bool solid);

	bool ReadEndOfBlock();

	bool ReadVmCode();

	bool ReadVmCodePpm();

	bool AddVmCode(uint32_t firstByte, const uint8_t* code, uint32_t codeSize);

	bool UnpReadBuf30();

	void UnpWriteBuf30();

	void ExecuteCode(PreparedProgram& program);

	bool ReadTables30();

	void UnpInitData30(bool solid);

	void InitFilters30(bool solid);

	// ── RAR 2.0 ─────────────────────────────────────────────────────────────

	void CopyString20(uint32_t length, uint32_t distance);

	void Unpack20(bool solid);

	void UnpWriteBuf20();

	bool ReadTables20();

	void ReadLastTables();

	void UnpInitData20(bool solid);

	uint8_t DecodeAudio(int delta);

	// ── RAR 1.5 ─────────────────────────────────────────────────────────────

	void Unpack15(bool solid);

	struct Huff15Table {
		uint32_t start;
		uint32_t dec[12];
		uint32_t pos[13];
	};
	static constexpr Huff15Table L1 = {
		2,
		{0x8000, 0xa000, 0xc000, 0xd000, 0xe000, 0xea00, 0xee00, 0xf000, 0xf200, 0xf200, 0xffff},
		{0, 0, 0, 2, 3, 5, 7, 11, 16, 20, 24, 32, 32}};
	static constexpr Huff15Table L2 = {
		3,
		{0xa000, 0xc000, 0xd000, 0xe000, 0xea00, 0xee00, 0xf000, 0xf200, 0xf240, 0xffff},
		{0, 0, 0, 0, 5, 7, 9, 13, 18, 22, 26, 34, 36}};
	static constexpr Huff15Table HF0 = {
		4,
		{0x8000, 0xc000, 0xe000, 0xf200, 0xf200, 0xf200, 0xf200, 0xf200, 0xffff},
		{0, 0, 0, 0, 0, 8, 16, 24, 33, 33, 33, 33, 33}};
	static constexpr Huff15Table HF1 = {
		5,
		{0x2000, 0xc000, 0xe000, 0xf000, 0xf200, 0xf200, 0xf7e0, 0xffff},
		{0, 0, 0, 0, 0, 0, 4, 44, 60, 76, 80, 80, 127}};
	static constexpr Huff15Table HF2 = {
		5,
		{0x1000, 0x2400, 0x8000, 0xc000, 0xfa00, 0xffff, 0xffff, 0xffff},
		{0, 0, 0, 0, 0, 0, 2, 7, 53, 117, 233, 0, 0}};
	static constexpr Huff15Table HF3 = {6,
										{0x800, 0x2400, 0xee00, 0xfe80, 0xffff, 0xffff, 0xffff},
										{0, 0, 0, 0, 0, 0, 0, 2, 16, 218, 251, 0, 0}};
	static constexpr Huff15Table HF4 = {8,
										{0xff00, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff},
										{0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 0, 0}};

	uint32_t DecodeNum(uint32_t num, const Huff15Table& table);

	void ShortLz();

	void LongLz();

	void HuffDecode();

	void GetFlagsBuf();

	void UnpInitData15(bool solid);

	void InitHuff();

	static void CorrHuff(uint16_t* charSet, uint8_t* numToPlace);

	void CopyString15(uint32_t distance, uint32_t length);

	friend class PpmRangeDecoder;

	// ── État ────────────────────────────────────────────────────────────────

	InputBuffer* m_input = nullptr;
	int m_method = 50;
	bool m_solid = false, m_started = false, m_finished = false, m_suspended = false;
	std::vector<uint8_t> m_pending;
	size_t m_pendingPosition = 0;
	uint64_t m_remaining = 0; ///< octets encore à rendre pour ce fichier

	BitInput m_inp, m_vmCodeInp;
	std::vector<uint8_t> m_window;
	size_t m_maxWinSize = 0, m_maxWinMask = 0;
	size_t m_unpPtr = 0, m_prevPtr = 0, m_wrPtr = 0, m_writeBorder = 0;
	bool m_firstWinDone = false;
	int m_readTop = 0, m_readBorder = 0;
	int64_t m_destUnpSize = 0, m_writtenFileSize = 0;
	bool m_extraDist = false;

	size_t m_oldDist[4] = {}, m_oldDistPtr = 0;
	uint32_t m_lastLength = 0, m_lastDist = 0;

	PpmRangeDecoder m_rc;
	std::unique_ptr<BlockTables> m_blockTables;
	BlockHeader m_blockHeader;
	bool m_tablesRead2 = false, m_tablesRead3 = false, m_tablesRead5 = false;
	std::vector<Filter5> m_filters;
	std::vector<uint8_t> m_filterSrc, m_filterDst;

	// RAR 3
	std::unique_ptr<Ppmd7Model> m_ppm;
	bool m_ppmReady = false, m_blockPpm = false;
	int m_ppmEscChar = 2;
	int m_prevLowDist = 0;
	uint32_t m_lowDistRepCount = 0;
	uint8_t m_unpOldTable[HUFF_TABLE_SIZE30] = {};
	RarVm m_vm;
	std::vector<std::unique_ptr<Filter30>> m_filters30;
	std::vector<std::unique_ptr<Filter30>> m_prgStack;
	std::vector<uint32_t> m_oldFilterLengths;
	uint32_t m_lastFilter = 0;

	// RAR 2.0
	DecodeTable m_md[4];
	uint8_t m_unpOldTable20[MC20 * 4] = {};
	bool m_unpAudioBlock = false;
	uint32_t m_unpChannels = 1, m_unpCurChannel = 0;
	int m_unpChannelDelta = 0;
	AudioVariables m_audV[4];

	// RAR 1.5
	uint16_t m_chSet[256] = {}, m_chSetA[256] = {}, m_chSetB[256] = {}, m_chSetC[256] = {};
	uint8_t m_nToPl[256] = {}, m_nToPlB[256] = {}, m_nToPlC[256] = {};
	uint32_t m_flagBuf = 0, m_avrPlc = 0, m_avrPlcB = 0, m_avrLn1 = 0, m_avrLn2 = 0, m_avrLn3 = 0;
	int m_buf60 = 0, m_numHuf = 0, m_stMode = 0, m_lCount = 0, m_flagsCnt = 0;
	uint32_t m_nhfb = 0, m_nlzb = 0, m_maxDist3 = 0;
};





// ============================================================================
// Structures de l'archive
// ============================================================================

enum class Crypt : uint8_t { NONE, RAR13, RAR15, RAR20, RAR30, RAR50 };
enum class Redirect : uint8_t { NONE, UNIX_SYMLINK, WIN_SYMLINK, JUNCTION, HARDLINK, FILECOPY };

/// Morceau des données d'un fichier (un par volume qu'il traverse).
struct Part {
	uint32_t volume = 0;
	uint64_t offset = 0;
	uint64_t size = 0;
};

struct FileRecord {
	std::vector<Part> parts;
	uint64_t unpSize = 0;
	bool unknownSize = false;
	int method = 0; ///< 0 = stockée, sinon 15, 20, 29, 50, 70
	uint64_t window = 0;
	bool solid = false; ///< reprend l'état du décompresseur du fichier précédent
	bool directory = false;
	bool continued = false; ///< suite des données dans un volume absent
	size_t chainStart = 0;	///< premier fichier de la suite solide
	Crypt crypt = Crypt::NONE;
	std::array<uint8_t, 16> salt{};
	bool hasSalt = false;
	std::array<uint8_t, 16> iv{};
	uint32_t lg2 = 0;
	bool hasPswCheck = false;
	std::array<uint8_t, 8> pswCheck{};
	bool hashMac = false;
	Option<uint32_t> crc = NONE;
	Option<std::array<uint8_t, 32>> blake = NONE;
	Redirect redirect = Redirect::NONE;
	String redirectTarget;
	bool rar4Symlink = false;
};

[[nodiscard]] bool IsValidUtf8(std::span<const uint8_t> bytes) noexcept;

/// Nom Unicode compressé des en-têtes RAR 3 (`EncodeFileName::Decode`).
[[nodiscard]] String DecodeRar4UnicodeName(std::span<const uint8_t> name,
												  std::span<const uint8_t> encoded);

/// Lecture d'un en-tête décodé (petit-boutiste, nombres variables RAR 5).
class HeaderReader {
public:
	explicit HeaderReader(std::span<const uint8_t> bytes) : m_bytes(bytes) {}
	[[nodiscard]] bool Ok() const noexcept { return m_ok; }
	[[nodiscard]] size_t Position() const noexcept { return m_position; }
	[[nodiscard]] size_t Left() const noexcept;
	void Seek(size_t position) noexcept;
	uint8_t U8() { return Take(1) ? m_bytes[m_position - 1] : 0; }
	uint16_t U16();
	uint32_t U32() { return Take(4) ? Get4(m_bytes.data() + m_position - 4) : 0; }
	uint64_t U64();
	uint64_t V();
	std::span<const uint8_t> Bytes(size_t size);

private:
	bool Take(size_t size);

	std::span<const uint8_t> m_bytes;
	size_t m_position = 0;
	bool m_ok = true;
};

/// Vérifie taille et empreinte (CRC-32 ou BLAKE2sp, éventuellement
/// converties en HMAC) à la fin de la lecture.
class CheckImpl final : public DecoderImpl {
public:
	CheckImpl(ArchiveStream inner, Option<uint64_t> size, Option<uint32_t> crc,
			  Option<std::array<uint8_t, 32>> blake, Option<std::array<uint8_t, 32>> hashKey,
			  String name, ErrorKind mismatch, StreamStatePtr state)
		: DecoderImpl(std::move(state), size), m_inner(std::move(inner)), m_size(size), m_crc(crc),
		  m_blake(blake), m_hashKey(hashKey), m_name(std::move(name)), m_mismatch(mismatch) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

private:
	ArchiveStream m_inner;
	Option<uint64_t> m_size;
	Option<uint32_t> m_crc;
	Option<std::array<uint8_t, 32>> m_blake;
	Option<std::array<uint8_t, 32>> m_hashKey;
	String m_name;
	ErrorKind m_mismatch;
	uint64_t m_count = 0;
	uint32_t m_computed = 0;
	Blake2sp m_hasher;
};

/// Fabrique les flux compressés (déchiffrés) des fichiers d'une archive.
class PackedSource {
public:
	virtual ~PackedSource() = default;
	[[nodiscard]] virtual Result<ArchiveStream, ArchiveError> OpenPacked(size_t file) = 0;
	[[nodiscard]] virtual const FileRecord& Record(size_t file) const = 0;
	[[nodiscard]] virtual const String& Name(size_t file) const = 0;
};

/// Données décompressées d'une suite de fichiers solides mises bout à bout.
class ChainImpl final : public DecoderImpl {
public:
	ChainImpl(PackedSource* source, std::vector<size_t> files, Option<uint64_t> size,
			  StreamStatePtr state)
		: DecoderImpl(std::move(state), size), m_source(source), m_files(std::move(files)) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

private:
	PackedSource* m_source;
	std::vector<size_t> m_files;
	size_t m_current = 0;
	bool m_open = false;
	uint64_t m_produced = 0;
	std::unique_ptr<ArchiveStream> m_packed;
	std::unique_ptr<InputBuffer> m_buffer;
	std::unique_ptr<Unpacker> m_unpacker;
};

[[nodiscard]] const char* MethodName(int method) noexcept;

[[nodiscard]] const char* CryptName(Crypt crypt) noexcept;

[[nodiscard]] uint64_t RoundUpPow2(uint64_t value) noexcept;

} // namespace detail::rar

/// Nom du volume `index` (0 = premier) d'après celui du premier volume ;
/// chaîne vide si le nom ne suit pas la convention.
[[nodiscard]] String RarVolumeName(const String& firstPath, uint32_t index,
										  bool newNumbering);

// ============================================================================
// Lecture
// ============================================================================

class RarReader final : public ArchiveReader, private detail::rar::PackedSource {
public:
	/// `opener` : volumes suivants d'une archive multi-volumes (facultatif).
	[[nodiscard]] static Result<std::unique_ptr<RarReader>, ArchiveError>
	Open(ArchiveSource source, const ReadOptions& options, RarVolumeOpener opener = {});

	/// Vrai si le flux commence (ou contient, pour un auto-extractible) une
	/// signature RAR.
	[[nodiscard]] static bool LooksLikeRar(std::span<const uint8_t> head) noexcept;

	[[nodiscard]] Format GetFormat() const noexcept override { return Format::RAR; }
	[[nodiscard]] const std::vector<EntryInfo>& Entries() const noexcept override;
	[[nodiscard]] uint64_t EntryLimit() const noexcept override { return m_options.maxEntrySize; }
	void SetPassword(const String& password) override;
	/// Archive solide (au moins une suite de fichiers solides).
	[[nodiscard]] bool IsSolid() const noexcept { return m_solid; }
	/// Nombre de volumes lus.
	[[nodiscard]] size_t VolumeCount() const noexcept { return m_volumes.size(); }
	[[nodiscard]] bool HeadersEncrypted() const noexcept { return m_headersEncrypted; }

	[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenEntry(size_t index) override;

private:
	RarReader(ReadOptions options, RarVolumeOpener opener)
		: m_options(std::move(options)), m_opener(std::move(opener)) {}

	// ── PackedSource ────────────────────────────────────────────────────────

	[[nodiscard]] const detail::rar::FileRecord& Record(size_t file) const override;
	[[nodiscard]] const String& Name(size_t file) const override { return m_entries[file].path; }

	[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenPacked(size_t file) override;

	// ── Entrées ─────────────────────────────────────────────────────────────

	[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenFollowing(size_t index, int depth);

	[[nodiscard]] bool HasData(size_t i) const;

	/// Flux partagé d'une suite solide : en mémoire si elle tient dans
	/// `memoryCacheLimit`, sinon flux de décompression (lire dans l'ordre
	/// évite de recommencer).
	[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenSolid(size_t chainStart,
																const std::vector<size_t>& chain,
																uint64_t total, uint64_t offset,
																uint64_t size);

	/// Cible des liens symboliques RAR 4 (contenu de l'entrée).
	void ResolveLinkTargets();

	// ── Indexation ──────────────────────────────────────────────────────────

	[[nodiscard]] static ArchiveError Corrupt(const char* what);

	/// Position de la signature (0, ou plus loin dans un auto-extractible) et
	/// version du format (4 ou 5).
	[[nodiscard]] Result<std::pair<uint64_t, int>, ArchiveError>
	FindSignature(ArchiveSource& source);

	[[nodiscard]] Option<ArchiveError> Index();

	/// Ouvre le volume suivant ; faux s'il est absent.
	[[nodiscard]] Result<bool, ArchiveError> NextVolume(bool newNumbering, uint64_t& position,
														int version);

	/// Ajoute une entrée, ou la suite d'une entrée coupée entre deux volumes.
	void AddRecord(EntryInfo entry, detail::rar::FileRecord record, bool splitBefore,
				   bool splitAfter);

	/// Assemble les suites solides et termine les entrées.
	void Finish();

	/// Nom (déjà en UTF-8) → chemin normalisé.
	static void SetPath(EntryInfo& entry, String name);

	/// En-tête chiffré (AES-CBC) : `total` octets en clair à partir de
	/// `position` ; `ivOrSalt` précède les données chiffrées.
	[[nodiscard]] static Result<Bytes, ArchiveError>
	DecryptHeader(ArchiveSource& source, uint64_t position, const Aes& aes,
				  const std::array<uint8_t, 16>& iv, size_t aligned);

	// ── RAR 1.5 à 4.x ───────────────────────────────────────────────────────

	[[nodiscard]] Option<ArchiveError> Index4(uint64_t position);

	// ── RAR 5 ───────────────────────────────────────────────────────────────

	[[nodiscard]] Option<ArchiveError> Index5(uint64_t position);

	[[nodiscard]] Option<ArchiveError> ParseFile5(detail::rar::HeaderReader& r, const Bytes& header,
												  uint64_t extraSize, uint64_t blockFlags,
												  uint32_t volume, uint64_t dataOffset,
												  uint64_t dataSize);

	ReadOptions m_options;
	RarVolumeOpener m_opener;
	std::vector<std::unique_ptr<ArchiveSource>> m_volumes;
	std::vector<EntryInfo> m_entries;
	std::vector<detail::rar::FileRecord> m_records;
	detail::rar::KeyCache m_keys;
	bool m_solid = false;
	bool m_headersEncrypted = false;
	Option<size_t> m_cachedChain = NONE;
	std::shared_ptr<const Bytes> m_cachedBytes;
	std::shared_ptr<ArchiveStream> m_cachedStream;
};

} // namespace data::archive
