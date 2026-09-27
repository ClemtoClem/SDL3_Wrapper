#pragma once
/**
 * data::archive — filtres de prétraitement de 7-Zip et xz.
 *
 * Ces filtres ne compressent rien : ils rendent le code machine plus
 * compressible en convertissant les adresses relatives des sauts et appels en
 * adresses absolues (un même appel répété devient une même suite d'octets).
 * 7-Zip les applique automatiquement aux exécutables, xz sur demande
 * (`--x86`, `--arm64`…) ; pour lire ces archives, il faut savoir les défaire.
 *
 *  - `BranchConverter` : x86 (7z 03030103, xz 0x04), PowerPC (03030205, 0x05),
 *    IA-64 (03030401, 0x06), ARM (03030501, 0x07), ARM Thumb (03030701, 0x08),
 *    SPARC (03030805, 0x09), ARM64 (0A, 0x0A). INCRÉMENTAL : `Convert` traite
 *    un morceau et rend le nombre d'octets définitifs ; les derniers octets
 *    d'une instruction coupée attendent le morceau suivant ;
 *  - `DeltaConverter` : différences d'octets à distance fixe (7z 03, xz 0x03) ;
 *  - `OpenFilterStream` : flux défiltré (ou filtré) d'un autre flux ;
 *  - `OpenBcj2Stream` : BCJ2, variante à 4 flux (principal, CALL, JUMP,
 *    codeur d'intervalle) utilisée par 7-Zip en mode ultra.
 *
 * Les fonctions `BcjX86`, `BcjArm`… traitent un tampon complet d'un coup.
 */
#include "../../core/core.hpp"
#include "archive_io.hpp"
#include "archive_stream.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace data::archive {

enum class BranchKind : uint8_t { X86, POWERPC, IA64, ARM, ARM_THUMB, SPARC, ARM64 };

[[nodiscard]] const char* BranchKindName(BranchKind kind) noexcept;

/// Convertisseur d'adresses de branchement, à état (position courante et,
/// pour x86, masque des octets E8/E9 récents).
class BranchConverter {
public:
	BranchConverter(BranchKind kind, bool encoding, uint32_t startOffset = 0) noexcept
		: m_kind(kind), m_encoding(encoding), m_ip(startOffset) {}

	/// Convertit `data` en place ; rend le nombre d'octets traités
	/// définitivement (le reste doit être représenté avec la suite).
	size_t Convert(uint8_t* data, size_t size) noexcept;

	/// Plus petit morceau utile (en deçà, `Convert` peut ne rien traiter).
	[[nodiscard]] size_t Lookahead() const noexcept { return m_kind == BranchKind::IA64 ? 16 : 5; }

private:
	[[nodiscard]] uint32_t Destination(uint32_t source, uint32_t pc) const noexcept;

	size_t X86(uint8_t* data, size_t size) noexcept;

	size_t PowerPc(uint8_t* data, size_t size) noexcept;

	size_t Ia64(uint8_t* data, size_t size) noexcept;

	size_t Arm(uint8_t* data, size_t size) noexcept;

	size_t ArmThumb(uint8_t* data, size_t size) noexcept;

	size_t Sparc(uint8_t* data, size_t size) noexcept;

	size_t Arm64(uint8_t* data, size_t size) noexcept;

	BranchKind m_kind;
	bool m_encoding;
	uint32_t m_ip;
	uint32_t m_x86State = 0;
};

/// Delta : chaque octet est la différence avec celui situé `distance` plus tôt
/// (1 à 256), historique conservé d'un morceau à l'autre.
class DeltaConverter {
public:
	DeltaConverter(size_t distance, bool encoding) noexcept
		: m_distance(distance), m_encoding(encoding) {}
	void Convert(uint8_t* data, size_t size) noexcept;

private:
	size_t m_distance;
	bool m_encoding;
	std::array<uint8_t, 256> m_history{};
	size_t m_position = 0;
};

// ── Tampon complet ──────────────────────────────────────────────────────────

void BcjX86(std::span<uint8_t> data, bool encoding) noexcept;
void BcjArm(std::span<uint8_t> data, bool encoding) noexcept;
void BcjArmThumb(std::span<uint8_t> data, bool encoding) noexcept;
void BcjArm64(std::span<uint8_t> data, bool encoding) noexcept;
void DeltaDecode(std::span<uint8_t> data, size_t distance) noexcept;
void DeltaEncode(std::span<uint8_t> data, size_t distance) noexcept;

// ── Flux ────────────────────────────────────────────────────────────────────

/// Un filtre dans une chaîne : branchements OU delta.
struct FilterSpec {
	enum class Type : uint8_t { BRANCH, DELTA } type = Type::BRANCH;
	BranchKind branch = BranchKind::X86;
	size_t deltaDistance = 1;
	uint32_t startOffset = 0;
};

/// Étape de filtrage incrémentale : reçoit des octets, rend ceux qui sont
/// définitifs, garde la fin d'une instruction coupée.
class FilterStage {
public:
	FilterStage(const FilterSpec& spec, bool encoding)
		: m_spec(spec), m_encoding(encoding), m_branch(spec.branch, encoding, spec.startOffset),
		  m_delta(spec.deltaDistance, encoding) {}

	/// Ajoute `input` ; les octets convertis sont AJOUTÉS à `output`. `final` :
	/// plus rien ne suivra (les octets en attente sortent tels quels).
	void Push(std::span<const uint8_t> input, Bytes& output, bool final);

	void Reset();

private:
	FilterSpec m_spec;
	bool m_encoding;
	BranchConverter m_branch;
	DeltaConverter m_delta;
	Bytes m_pending;
};

namespace detail::filters {

class FilterStreamImpl : public DecoderImpl {
public:
	FilterStreamImpl(ArchiveStream input, const FilterSpec& spec, bool encoding,
					 Option<uint64_t> size, StreamStatePtr state)
		: DecoderImpl(std::move(state), size), m_input(std::move(input)), m_stage(spec, encoding) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

private:
	ArchiveStream m_input;
	FilterStage m_stage;
	Bytes m_ready;
	size_t m_readyAt = 0;
	bool m_inputDone = false;
};

/// BCJ2 (décodage) : 4 flux d'entrée → le flux x86 d'origine.
class Bcj2StreamImpl : public DecoderImpl {
public:
	Bcj2StreamImpl(ArchiveStream main, ArchiveStream call, ArchiveStream jump, ArchiveStream rc,
				   uint64_t outSize, StreamStatePtr state);

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;

private:
	static constexpr uint32_t TOP = 1u << 24;
	static constexpr uint32_t MODEL_TOTAL = 1u << 11;
	static constexpr int MOVE_BITS = 5;

	ArchiveStream m_main, m_call, m_jump, m_rc;
	InputBuffer m_mainIn, m_callIn, m_jumpIn, m_rcIn;
	uint64_t m_outSize;
	uint64_t m_total = 0;
	std::array<uint16_t, 258> m_probabilities{};
	uint32_t m_range = 0xFFFFFFFFu, m_code = 0;
	uint8_t m_previous = 0;
	uint32_t m_address = 0;
	int m_addressLeft = 0;
	bool m_started = false;
};

} // namespace detail::filters

/// Flux `input` défiltré (`encoding` faux) ou filtré (`encoding` vrai).
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenFilterStream(ArchiveStream input, const FilterSpec& spec, bool encoding = false,
				 Option<uint64_t> size = NONE);

[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenBcj2Stream(ArchiveStream main, ArchiveStream call, ArchiveStream jump, ArchiveStream rc,
			   uint64_t outSize);

/// BCJ2 sur tampons complets.
[[nodiscard]] Result<Bytes, String>
Bcj2Decode(std::span<const uint8_t> main, std::span<const uint8_t> call,
		   std::span<const uint8_t> jump, std::span<const uint8_t> rc, uint64_t outSize);

} // namespace data::archive
