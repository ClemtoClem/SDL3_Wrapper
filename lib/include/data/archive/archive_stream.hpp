#pragma once
/**
 * data::archive — flux : tout ce qui se lit ou s'écrit par morceaux.
 *
 * Chaque décompresseur, déchiffreur ou fenêtre sur un fichier est un
 * `sdl3::IOStreamImpl` exposé comme un `sdl3::IOStream` ordinaire (SDL_OpenIO)
 * : on lit une entrée d'archive de plusieurs gigaoctets avec un tampon de
 * quelques kilo-octets, et les flux s'empilent (fenêtre → AES → LZMA2 → BCJ →
 * contrôle CRC).
 *
 * SDL efface le type des flux personnalisés ; pour qu'une erreur précise
 * (mot de passe incorrect, CRC faux…) survive, chaque flux d'archive partage
 * avec son créateur un `StreamState`. `ArchiveStream` réunit les deux.
 *
 *  - `DecoderImpl` : base des flux produits à la demande. Avancer dans le
 *    flux = décoder et jeter ; reculer = recommencer depuis le début
 *    (`Restart`), si le décodeur le permet ;
 *  - `InputBuffer` : tampon de lecture d'un flux source, octet par octet sans
 *    appel virtuel, avec position exacte (les octets lus d'avance peuvent
 *    être rendus : `GiveBack`) ;
 *  - `SubStreamImpl` : fenêtre [début, début + longueur) d'un flux parent ;
 *  - `CheckedStreamImpl` : vérifie taille et CRC-32 à la fin de la lecture ;
 *  - `StreamRead`, `ReadStreamToEnd`, `CopyStream` : lecture sûre d'un
 *    `ArchiveStream`, erreurs typées comprises.
 */
#include "../../core/core.hpp"
#include "../../sdl3/iostream.hpp"
#include "archive_crc.hpp"
#include "archive_io.hpp"
#include "archive_types.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace data::archive {

/// État partagé entre un flux et ceux qui le lisent : la première erreur.
struct StreamState {
	Option<ArchiveError> error = NONE;

	void Fail(ArchiveError failure) {
		if (error.IsNone())
			error = Some(std::move(failure));
	}
};
using StreamStatePtr = std::shared_ptr<StreamState>;

/// Flux d'archive : le flux SDL et son état (erreur typée).
struct ArchiveStream {
	sdl3::IOStream io;
	StreamStatePtr state;

	/// Erreur du flux : celle qu'il a signalée, sinon une erreur d'E/S
	/// générique si SDL rapporte un échec.
	[[nodiscard]] ArchiveError Failure(const char* context = "flux") const {
		if (state && state->error.IsSome())
			return state->error.Value();
		return MakeError(ErrorKind::IO, String::Format("%s : erreur de lecture", context));
	}
};

/// Enveloppe un `IOStreamImpl` dans un `ArchiveStream`.
[[nodiscard]] inline Result<ArchiveStream, ArchiveError>
MakeStream(std::unique_ptr<sdl3::IOStreamImpl> impl, StreamStatePtr state) {
	auto io = sdl3::IOStream::FromImpl(std::move(impl));
	if (io.IsError())
		return Err(MakeError(ErrorKind::IO,
							 String::Format("flux impossible : %s", String(io.Error()).CStr())));
	return Ok(ArchiveStream{std::move(io).Unwrap(), std::move(state)});
}

/// Lecture d'au plus `size` octets ; 0 = fin. Err si le flux a échoué.
[[nodiscard]] inline Result<size_t, ArchiveError> StreamRead(ArchiveStream& stream, void* buffer,
															 size_t size) {
	if (size == 0)
		return Ok(size_t(0));
	auto* bytes = static_cast<uint8_t*>(buffer);
	size_t total = 0;
	while (total < size) {
		const size_t done = stream.io.Read(bytes + total, size - total);
		if (done == 0) {
			if (stream.io.Status() == sdl3::IOStatus::ERROR ||
				(stream.state && stream.state->error.IsSome()))
				return Err(stream.Failure());
			break;
		}
		total += done;
	}
	return Ok(total);
}

/// Tout le reste du flux, au plus `limit` octets (au-delà : `LIMIT`).
[[nodiscard]] inline Result<Bytes, ArchiveError>
ReadStreamToEnd(ArchiveStream& stream, uint64_t limit, uint64_t sizeHint = 0) {
	Bytes out;
	if (sizeHint > 0)
		out.reserve(size_t(std::min<uint64_t>({sizeHint, limit, uint64_t(1) << 26})));
	uint8_t chunk[1 << 16];
	for (;;) {
		auto done = StreamRead(stream, chunk, sizeof(chunk));
		if (done.IsError())
			return Err(done.Error());
		if (done.Value() == 0)
			break;
		if (out.size() + done.Value() > limit)
			return Err(MakeError(ErrorKind::LIMIT,
								 String("données plus grandes que la limite autorisée")));
		out.insert(out.end(), chunk, chunk + done.Value());
	}
	return Ok(std::move(out));
}

/// Copie le flux dans `out` ; rend le nombre d'octets copiés.
[[nodiscard]] inline Result<uint64_t, ArchiveError>
CopyStream(ArchiveStream& in, sdl3::IOStream& out, uint64_t limit = UINT64_MAX) {
	std::vector<uint8_t> chunk(1 << 16);
	uint64_t total = 0;
	for (;;) {
		auto done = StreamRead(in, chunk.data(), chunk.size());
		if (done.IsError())
			return Err(done.Error());
		if (done.Value() == 0)
			break;
		total += done.Value();
		if (total > limit)
			return Err(MakeError(ErrorKind::LIMIT,
								 String("données plus grandes que la limite autorisée")));
		if (!out.WriteExact(chunk.data(), done.Value()))
			return Err(MakeError(ErrorKind::IO, String("écriture impossible")));
	}
	return Ok(total);
}

// ============================================================================
// Tampon d'entrée
// ============================================================================

/// Lecture tamponnée d'un flux source à partir de sa position courante, au
/// plus `limit` octets. `Byte` est en ligne : pas d'appel virtuel par octet.
class InputBuffer {
public:
	explicit InputBuffer(sdl3::IOStream& source, StreamStatePtr sourceState = {},
						 uint64_t limit = UINT64_MAX, size_t capacity = size_t(1) << 16)
		: m_source(&source), m_sourceState(std::move(sourceState)), m_limit(limit),
		  m_buffer(capacity) {
		const Sint64 start = source.Tell();
		m_start = start < 0 ? 0 : uint64_t(start);
	}

	[[nodiscard]] inline bool Byte(uint8_t& out) {
		if (m_position == m_end && !Fill())
			return false;
		out = m_buffer[m_position++];
		return true;
	}

	/// Recharge le tampon ; faux s'il n'y a plus rien (fin ou erreur).
	bool Fill() {
		if (m_position < m_end)
			return true;
		m_consumedBefore += m_end;
		m_position = m_end = 0;
		const uint64_t remaining = m_limit - m_fetched;
		if (remaining == 0 || m_failed)
			return false;
		const size_t want = size_t(std::min<uint64_t>(remaining, m_buffer.size()));
		const size_t got = m_source->Read(m_buffer.data(), want);
		if (got == 0) {
			if (m_source->Status() == sdl3::IOStatus::ERROR ||
				(m_sourceState && m_sourceState->error.IsSome()))
				m_failed = true;
			return false;
		}
		m_end = got;
		m_fetched += got;
		return true;
	}

	/// Octets disponibles sans lecture (après un `Fill` éventuel).
	[[nodiscard]] size_t Available() const noexcept { return m_end - m_position; }
	[[nodiscard]] const uint8_t* Data() const noexcept { return m_buffer.data() + m_position; }
	void Advance(size_t count) noexcept { m_position += std::min(count, Available()); }

	/// Copie jusqu'à `size` octets bruts ; rend le nombre copié.
	size_t ReadRaw(uint8_t* out, size_t size) {
		size_t total = 0;
		while (total < size) {
			if (Available() == 0 && !Fill())
				break;
			const size_t take = std::min(size - total, Available());
			std::memcpy(out + total, Data(), take);
			m_position += take;
			total += take;
		}
		return total;
	}

	/// Saute `count` octets ; faux si l'entrée se termine avant.
	bool Skip(uint64_t count) {
		while (count > 0) {
			if (Available() == 0 && !Fill())
				return false;
			const size_t take = size_t(std::min<uint64_t>(count, Available()));
			m_position += take;
			count -= take;
		}
		return true;
	}

	/// Octets consommés depuis la position de départ.
	[[nodiscard]] uint64_t Consumed() const noexcept { return m_consumedBefore + m_position; }
	/// Vrai si l'entrée a échoué (et non simplement pris fin).
	[[nodiscard]] bool Failed() const noexcept { return m_failed; }
	[[nodiscard]] ArchiveError Failure(const char* context) const {
		if (m_sourceState && m_sourceState->error.IsSome())
			return m_sourceState->error.Value();
		return MakeError(
			m_failed ? ErrorKind::IO : ErrorKind::CORRUPT,
			String::Format(m_failed ? "%s : erreur de lecture" : "%s : données tronquées",
						   context));
	}

	/// Repart du début (pour recommencer un décodage).
	bool Rewind() {
		if (m_source->Seek(Sint64(m_start), SDL_IO_SEEK_SET) < 0)
			return false;
		m_position = m_end = 0;
		m_consumedBefore = m_fetched = 0;
		m_failed = false;
		return true;
	}

	/// Replace la source juste après le dernier octet consommé (les octets
	/// lus d'avance lui sont rendus) : utile quand d'autres données suivent.
	bool GiveBack(uint64_t unusedBytes = 0) {
		const uint64_t logical = m_start + Consumed() - unusedBytes;
		return m_source->Seek(Sint64(logical), SDL_IO_SEEK_SET) >= 0;
	}

	/// Réduit/ôte la limite (en octets depuis le départ).
	void SetLimit(uint64_t limit) noexcept { m_limit = std::max(limit, m_fetched); }
	[[nodiscard]] sdl3::IOStream& Source() noexcept { return *m_source; }

private:
	sdl3::IOStream* m_source;
	StreamStatePtr m_sourceState;
	uint64_t m_limit;
	uint64_t m_start = 0;
	uint64_t m_fetched = 0;		   ///< octets lus dans la source
	uint64_t m_consumedBefore = 0; ///< octets consommés avant le tampon courant
	std::vector<uint8_t> m_buffer;
	size_t m_position = 0, m_end = 0;
	bool m_failed = false;
};

/// Fenêtre circulaire (dictionnaire LZ : LZMA, zstd, RAR). La mémoire croît
/// avec les données réellement décodées (jamais au-delà de la capacité) : une
/// taille de dictionnaire annoncée de 1,5 Gio pour 3 Ko de données n'alloue que
/// 64 Kio.
class SlidingWindow {
public:
	void Reset(uint64_t capacity) {
		m_capacity = size_t(std::clamp<uint64_t>(capacity, 4096, uint64_t(SIZE_MAX / 2)));
		m_buffer.clear();
		m_position = 0;
		m_total = 0;
	}
	inline void Put(uint8_t byte) {
		if (m_position == m_buffer.size())
			m_buffer.resize(std::min(m_capacity, std::max(m_buffer.size() * 2, size_t(1) << 16)));
		m_buffer[m_position] = byte;
		if (++m_position == m_capacity)
			m_position = 0;
		++m_total;
	}
	/// Octet situé `distance` (≥ 1) octets en arrière ; distance vérifiée par
	/// `Has`.
	[[nodiscard]] inline uint8_t Get(uint64_t distance) const {
		return m_buffer[m_position >= distance ? m_position - size_t(distance)
											   : m_position + m_capacity - size_t(distance)];
	}
	[[nodiscard]] bool Has(uint64_t distance) const noexcept {
		return distance >= 1 && distance <= std::min<uint64_t>(m_total, m_capacity);
	}
	[[nodiscard]] uint64_t Total() const noexcept { return m_total; }

private:
	std::vector<uint8_t> m_buffer;
	size_t m_capacity = 4096;
	size_t m_position = 0;
	uint64_t m_total = 0;
};

// ============================================================================
// Base des flux décodés à la demande
// ============================================================================

/// Flux produit par un décodeur. Les dérivés implémentent `Produce` (et
/// `Restart` pour autoriser les retours en arrière).
class DecoderImpl : public sdl3::IOStreamImpl {
public:
	DecoderImpl(StreamStatePtr state, Option<uint64_t> size = NONE)
		: m_state(std::move(state)), m_size(size) {}

	[[nodiscard]] Sint64 Size() override { return m_size.IsSome() ? Sint64(m_size.Value()) : -1; }

	size_t Read(void* buffer, size_t size, sdl3::IOStatus& status) final {
		auto* out = static_cast<uint8_t*>(buffer);
		size_t total = 0;
		while (total < size && !m_ended) {
			if (m_state->error.IsSome())
				break;
			auto produced = Produce(out + total, size - total);
			if (produced.IsError()) {
				m_state->Fail(produced.Error());
				break;
			}
			if (produced.Value() == 0) {
				m_ended = true;
				if (m_size.IsNone())
					m_size = Some(m_position + total); // taille connue une fois le flux lu
				break;
			}
			total += produced.Value();
		}
		m_position += total;
		if (total == 0)
			status = m_state->error.IsSome() ? sdl3::IOStatus::ERROR : sdl3::IOStatus::END;
		return total;
	}

	Sint64 Seek(Sint64 offset, sdl3::IOWhence whence) final {
		Sint64 target = offset;
		if (whence == sdl3::IOWhence::SEEK_CURRENT)
			target = Sint64(m_position) + offset;
		else if (whence == sdl3::IOWhence::SeekEnd) {
			if (m_size.IsNone())
				return -1;
			target = Sint64(m_size.Value()) + offset;
		}
		if (target < 0)
			return -1;
		if (uint64_t(target) < m_position) {
			if (!Restart())
				return -1;
			m_position = 0;
			m_ended = false;
			if (m_state->error.IsSome())
				return -1;
		}
		// Avancer : décoder et jeter.
		uint8_t scratch[1 << 14];
		while (m_position < uint64_t(target)) {
			sdl3::IOStatus status = sdl3::IOStatus::READY;
			const size_t want =
				size_t(std::min<uint64_t>(sizeof(scratch), uint64_t(target) - m_position));
			if (Read(scratch, want, status) == 0)
				return -1;
		}
		return Sint64(m_position);
	}

protected:
	/// Produit au plus `max` octets ; 0 = fin du flux.
	[[nodiscard]] virtual Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) = 0;
	/// Remet le décodeur au début ; faux si impossible.
	virtual bool Restart() { return false; }

	[[nodiscard]] StreamState& State() noexcept { return *m_state; }
	[[nodiscard]] uint64_t Position() const noexcept { return m_position; }

private:
	StreamStatePtr m_state;
	Option<uint64_t> m_size;
	uint64_t m_position = 0;
	bool m_ended = false;
};

// ============================================================================
// Flux élémentaires
// ============================================================================

/// Fenêtre [base, base + length) d'un flux parent (non possédé : il doit
/// survivre à la fenêtre). La position du parent est rétablie à chaque lecture,
/// plusieurs fenêtres peuvent donc partager le même parent.
class SubStreamImpl : public sdl3::IOStreamImpl {
public:
	SubStreamImpl(sdl3::IOStream& parent, uint64_t base, uint64_t length, StreamStatePtr state,
				  std::shared_ptr<ArchiveStream> keepAlive = {})
		: m_parent(&parent), m_base(base), m_length(length), m_state(std::move(state)),
		  m_keepAlive(std::move(keepAlive)) {}

	[[nodiscard]] Sint64 Size() override { return Sint64(m_length); }
	Sint64 Seek(Sint64 offset, sdl3::IOWhence whence) override {
		Sint64 target = offset;
		if (whence == sdl3::IOWhence::SEEK_CURRENT)
			target += Sint64(m_position);
		else if (whence == sdl3::IOWhence::SeekEnd)
			target += Sint64(m_length);
		if (target < 0)
			return -1;
		m_position = uint64_t(target);
		return target;
	}
	size_t Read(void* buffer, size_t size, sdl3::IOStatus& status) override {
		if (m_position >= m_length) {
			status = sdl3::IOStatus::END;
			return 0;
		}
		const size_t want = size_t(std::min<uint64_t>(size, m_length - m_position));
		if (m_parent->Seek(Sint64(m_base + m_position), SDL_IO_SEEK_SET) < 0 ||
			!m_parent->ReadExact(buffer, want)) {
			// Parent décodé (bloc solide) : son erreur est la vraie cause.
			if (m_keepAlive && m_keepAlive->state && m_keepAlive->state->error.IsSome())
				m_state->Fail(m_keepAlive->state->error.Value());
			else
				m_state->Fail(MakeError(ErrorKind::CORRUPT,
										String("données hors du fichier (archive tronquée ?)")));
			status = sdl3::IOStatus::ERROR;
			return 0;
		}
		m_position += want;
		return want;
	}

private:
	sdl3::IOStream* m_parent;
	uint64_t m_base, m_length, m_position = 0;
	StreamStatePtr m_state;
	std::shared_ptr<ArchiveStream> m_keepAlive;
};

/// Octets en mémoire (partagés : plusieurs flux peuvent lire le même bloc),
/// fenêtre [offset, offset + length), lecture et déplacement libres.
class MemoryStreamImpl : public sdl3::IOStreamImpl {
public:
	explicit MemoryStreamImpl(Bytes bytes)
		: m_shared(std::make_shared<const Bytes>(std::move(bytes))), m_bytes(*m_shared) {}
	MemoryStreamImpl(std::shared_ptr<const Bytes> bytes, uint64_t offset, uint64_t length)
		: m_shared(std::move(bytes)),
		  m_bytes(std::span<const uint8_t>(*m_shared).subspan(size_t(offset), size_t(length))) {}
	[[nodiscard]] Sint64 Size() override { return Sint64(m_bytes.size()); }
	Sint64 Seek(Sint64 offset, sdl3::IOWhence whence) override {
		Sint64 target = offset;
		if (whence == sdl3::IOWhence::SEEK_CURRENT)
			target += Sint64(m_position);
		else if (whence == sdl3::IOWhence::SeekEnd)
			target += Sint64(m_bytes.size());
		if (target < 0)
			return -1;
		m_position = size_t(target);
		return target;
	}
	size_t Read(void* buffer, size_t size, sdl3::IOStatus& status) override {
		if (m_position >= m_bytes.size()) {
			status = sdl3::IOStatus::END;
			return 0;
		}
		const size_t take = std::min(size, m_bytes.size() - m_position);
		std::memcpy(buffer, m_bytes.data() + m_position, take);
		m_position += take;
		return take;
	}

private:
	std::shared_ptr<const Bytes> m_shared;
	std::span<const uint8_t> m_bytes;
	size_t m_position = 0;
};

/// Fenêtre sur un flux parent.
[[nodiscard]] inline Result<ArchiveStream, ArchiveError>
OpenSubStream(sdl3::IOStream& parent, uint64_t offset, uint64_t length) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<SubStreamImpl>(parent, offset, length, state), state);
}

/// Fenêtre sur un flux parent PARTAGÉ : la fenêtre le garde en vie (flux
/// décodé d'un bloc solide, lu par plusieurs entrées).
[[nodiscard]] inline Result<ArchiveStream, ArchiveError>
OpenSharedSubStream(std::shared_ptr<ArchiveStream> parent, uint64_t offset, uint64_t length) {
	auto state = std::make_shared<StreamState>();
	sdl3::IOStream& io = parent->io;
	return MakeStream(std::make_unique<SubStreamImpl>(io, offset, length, state, std::move(parent)),
					  state);
}

/// Flux lisant des octets en mémoire (qu'il possède).
[[nodiscard]] inline Result<ArchiveStream, ArchiveError> OpenMemoryStream(Bytes bytes) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<MemoryStreamImpl>(std::move(bytes)), state);
}

/// Fenêtre sur des octets partagés (bloc décodé gardé en cache).
[[nodiscard]] inline Result<ArchiveStream, ArchiveError>
OpenSharedMemoryStream(std::shared_ptr<const Bytes> bytes, uint64_t offset, uint64_t length) {
	if (offset > bytes->size() || length > bytes->size() - offset)
		return Err(MakeError(ErrorKind::CORRUPT, String("fenêtre hors du bloc décodé")));
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<MemoryStreamImpl>(std::move(bytes), offset, length), state);
}

/// Vérifie, quand la lecture atteint la fin, la taille et le CRC-32 du
/// contenu. `mismatch` : type d'erreur à rapporter (CORRUPT, ou
/// WRONG_PASSWORD pour une entrée chiffrée sans autre contrôle).
class CheckedStreamImpl : public DecoderImpl {
public:
	CheckedStreamImpl(ArchiveStream inner, Option<uint64_t> size, Option<uint32_t> crc, String name,
					  ErrorKind mismatch, StreamStatePtr state)
		: DecoderImpl(std::move(state), size), m_inner(std::move(inner)), m_expectedSize(size),
		  m_expectedCrc(crc), m_name(std::move(name)), m_mismatch(mismatch) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override {
		if (m_expectedSize.IsSome())
			max = size_t(std::min<uint64_t>(
				max, m_expectedSize.Value() - std::min(m_count, m_expectedSize.Value()) + 1));
		auto done = StreamRead(m_inner, out, max);
		if (done.IsError()) {
			ArchiveError error = done.Error();
			// Données chiffrées indécodables : presque toujours la clé.
			if (m_mismatch == ErrorKind::WRONG_PASSWORD && error.kind == ErrorKind::CORRUPT) {
				error.kind = ErrorKind::WRONG_PASSWORD;
				error.message =
					String::Format("%s (mot de passe incorrect ?)", error.message.CStr());
			}
			if (!error.message.StartsWith(m_name + " :"))
				error.message = String::Format("%s : %s", m_name.CStr(), error.message.CStr());
			return Err(error);
		}
		if (done.Value() == 0) {
			if (m_expectedSize.IsSome() && m_count != m_expectedSize.Value())
				return Err(MakeError(
					ErrorKind::CORRUPT,
					String::Format("%s : %llu octets au lieu de %llu", m_name.CStr(),
								   static_cast<unsigned long long>(m_count),
								   static_cast<unsigned long long>(m_expectedSize.Value()))));
			if (m_expectedCrc.IsSome() && m_crc != m_expectedCrc.Value())
				return Err(
					MakeError(m_mismatch, String::Format("%s : CRC-32 incorrect%s", m_name.CStr(),
														 m_mismatch == ErrorKind::WRONG_PASSWORD
															 ? " (mot de passe incorrect ?)"
															 : "")));
			return Ok(size_t(0));
		}
		m_count += done.Value();
		if (m_expectedSize.IsSome() && m_count > m_expectedSize.Value())
			return Err(MakeError(
				ErrorKind::CORRUPT,
				String::Format("%s : plus de données que la taille annoncée", m_name.CStr())));
		m_crc = Crc32(std::span<const uint8_t>(out, done.Value()), m_crc);
		return done;
	}
	bool Restart() override {
		if (m_inner.io.Seek(0, SDL_IO_SEEK_SET) != 0)
			return false;
		m_count = 0;
		m_crc = 0;
		return true;
	}

private:
	ArchiveStream m_inner;
	Option<uint64_t> m_expectedSize;
	Option<uint32_t> m_expectedCrc;
	String m_name;
	ErrorKind m_mismatch;
	uint64_t m_count = 0;
	uint32_t m_crc = 0;
};

[[nodiscard]] inline Result<ArchiveStream, ArchiveError>
OpenCheckedStream(ArchiveStream inner, Option<uint64_t> size, Option<uint32_t> crc,
				  const String& name, ErrorKind mismatch = ErrorKind::CORRUPT) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(
		std::make_unique<CheckedStreamImpl>(std::move(inner), size, crc, name, mismatch, state),
		state);
}

/// Lit exactement `size` octets d'un flux d'archive.
[[nodiscard]] inline Result<Bytes, ArchiveError> StreamReadExact(ArchiveStream& stream,
																 uint64_t size) {
	Bytes bytes(static_cast<size_t>(size));
	auto done = StreamRead(stream, bytes.data(), bytes.size());
	if (done.IsError())
		return Err(done.Error());
	if (done.Value() != size)
		return Err(MakeError(ErrorKind::CORRUPT, String("données tronquées")));
	return Ok(std::move(bytes));
}

/// Flux de lecture d'un fichier du disque.
[[nodiscard]] inline Result<ArchiveStream, ArchiveError> OpenFileStream(const String& path) {
	auto io = sdl3::IOStream::FromFile(path, "rb");
	if (io.IsError())
		return Err(
			MakeError(ErrorKind::IO, String::Format("ouverture de %s impossible : %s", path.CStr(),
													String(io.Error()).CStr())));
	return Ok(ArchiveStream{std::move(io).Unwrap(), std::make_shared<StreamState>()});
}

/// Flux sur des octets NON possédés (qui doivent survivre au flux).
[[nodiscard]] inline Result<ArchiveStream, ArchiveError>
OpenViewStream(std::span<const uint8_t> bytes) {
	auto io = ViewStream(bytes);
	if (io.IsError())
		return Err(MakeError(ErrorKind::IO, io.Error()));
	return Ok(ArchiveStream{std::move(io).Unwrap(), std::make_shared<StreamState>()});
}

// ============================================================================
// Flux en écriture (compresseurs, chiffreurs)
// ============================================================================

/// Transmet lectures, écritures et déplacements à un flux NON possédé (il
/// doit survivre) : permet de chaîner des encodeurs vers un fichier ouvert
/// par l'appelant.
class BorrowedStreamImpl : public sdl3::IOStreamImpl {
public:
	explicit BorrowedStreamImpl(sdl3::IOStream& target) : m_target(&target) {}
	[[nodiscard]] Sint64 Size() override { return m_target->GetSize(); }
	Sint64 Seek(Sint64 offset, sdl3::IOWhence whence) override {
		return m_target->Seek(offset, whence);
	}
	size_t Read(void* buffer, size_t size, sdl3::IOStatus& status) override {
		const size_t done = m_target->Read(buffer, size);
		if (done == 0)
			status = m_target->Status();
		return done;
	}
	size_t Write(const void* buffer, size_t size, sdl3::IOStatus& status) override {
		if (!m_target->WriteExact(buffer, size)) {
			status = sdl3::IOStatus::ERROR;
			return 0;
		}
		return size;
	}
	bool Flush(sdl3::IOStatus&) override { return m_target->Flush(); }

private:
	sdl3::IOStream* m_target;
};

[[nodiscard]] inline Result<ArchiveStream, ArchiveError>
OpenBorrowedStream(sdl3::IOStream& target) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<BorrowedStreamImpl>(target), state);
}

/// Base des flux en écriture qui transforment ce qu'on leur écrit (compression,
/// chiffrement) et l'envoient dans `sink` (possédé, fermé avec eux). Les
/// données en attente sont émises à la FERMETURE : utiliser `FinishStream`.
class EncoderImpl : public sdl3::IOStreamImpl {
public:
	EncoderImpl(ArchiveStream sink, StreamStatePtr state)
		: m_sink(std::move(sink)), m_state(std::move(state)) {}

	/// Octets reçus jusqu'ici (position d'écriture).
	[[nodiscard]] Sint64 Size() override { return Sint64(m_received); }
	Sint64 Seek(Sint64 offset, sdl3::IOWhence whence) override {
		return offset == 0 && whence == sdl3::IOWhence::SEEK_CURRENT ? Sint64(m_received) : -1;
	}
	size_t Write(const void* buffer, size_t size, sdl3::IOStatus& status) final {
		if (m_state->error.IsSome() || m_finished) {
			status = sdl3::IOStatus::ERROR;
			return 0;
		}
		auto consumed = Consume(static_cast<const uint8_t*>(buffer), size);
		if (consumed.IsError()) {
			m_state->Fail(consumed.Error());
			status = sdl3::IOStatus::ERROR;
			return 0;
		}
		m_received += size;
		return size;
	}
	bool Close() final {
		if (!m_finished && m_state->error.IsNone()) {
			m_finished = true;
			auto finished = Finish();
			if (finished.IsError())
				m_state->Fail(finished.Error());
		}
		if (!m_sink.io.Close() && m_state->error.IsNone())
			m_state->Fail(m_sink.Failure("écriture"));
		if (m_state->error.IsNone() && m_sink.state && m_sink.state->error.IsSome())
			m_state->Fail(m_sink.state->error.Value());
		return m_state->error.IsNone();
	}

protected:
	virtual Result<bool, ArchiveError> Consume(const uint8_t* data, size_t size) = 0;
	/// Émet tout ce qui reste (dernier bloc, pied, contrôle).
	virtual Result<bool, ArchiveError> Finish() = 0;

	/// Écrit dans le flux de sortie.
	[[nodiscard]] Result<bool, ArchiveError> Emit(const void* data, size_t size) {
		if (size == 0)
			return Ok(true);
		if (!m_sink.io.WriteExact(data, size))
			return Err(m_sink.Failure("écriture"));
		m_emitted += size;
		return Ok(true);
	}
	[[nodiscard]] uint64_t Emitted() const noexcept { return m_emitted; }
	[[nodiscard]] uint64_t Received() const noexcept { return m_received; }

private:
	ArchiveStream m_sink;
	StreamStatePtr m_state;
	uint64_t m_received = 0, m_emitted = 0;
	bool m_finished = false;
};

/// Ferme un flux en écriture (émission des données en attente) et rend son
/// erreur éventuelle.
[[nodiscard]] inline Result<bool, ArchiveError> FinishStream(ArchiveStream& stream) {
	const bool closed = stream.io.Close();
	if (stream.state && stream.state->error.IsSome())
		return Err(stream.state->error.Value());
	if (!closed)
		return Err(MakeError(ErrorKind::IO, String("fermeture du flux impossible")));
	return Ok(true);
}

/// Écrit tout `data` dans un flux en écriture.
[[nodiscard]] inline Result<bool, ArchiveError> StreamWrite(ArchiveStream& stream,
															std::span<const uint8_t> data) {
	if (data.empty())
		return Ok(true);
	if (!stream.io.WriteExact(data.data(), data.size()))
		return Err(stream.Failure("écriture"));
	return Ok(true);
}

/// Copie un flux d'entrée vers un flux d'archive en écriture.
[[nodiscard]] inline Result<uint64_t, ArchiveError> CopyToStream(ArchiveStream& in,
																 ArchiveStream& out) {
	std::vector<uint8_t> chunk(1 << 16);
	uint64_t total = 0;
	for (;;) {
		auto done = StreamRead(in, chunk.data(), chunk.size());
		if (done.IsError())
			return Err(done.Error());
		if (done.Value() == 0)
			return Ok(total);
		auto written = StreamWrite(out, std::span<const uint8_t>(chunk.data(), done.Value()));
		if (written.IsError())
			return Err(written.Error());
		total += done.Value();
	}
}

/// Lit `inner`, puis, arrivé à sa fin, lit `tail` jusqu'au bout (sans rien
/// rendre) : un contrôle placé en fin de `tail` (code d'authentification)
/// s'exécute même si le décodeur de `inner` n'a pas eu besoin de tout lire.
class DrainImpl final : public DecoderImpl {
public:
	DrainImpl(ArchiveStream inner, std::shared_ptr<ArchiveStream> tail, StreamStatePtr state)
		: DecoderImpl(std::move(state), NONE), m_inner(std::move(inner)), m_tail(std::move(tail)) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override {
		auto got = StreamRead(m_inner, out, max);
		if (got.IsError() || got.Value() > 0)
			return got;
		uint8_t scratch[1 << 12];
		for (;;) {
			auto drained = StreamRead(*m_tail, scratch, sizeof(scratch));
			if (drained.IsError())
				return drained;
			if (drained.Value() == 0)
				return Ok(size_t(0));
		}
	}
	bool Restart() override { return m_inner.io.Seek(0, SDL_IO_SEEK_SET) == 0; }

private:
	ArchiveStream m_inner;
	std::shared_ptr<ArchiveStream> m_tail;
};

[[nodiscard]] inline Result<ArchiveStream, ArchiveError>
OpenDrainingStream(ArchiveStream inner, std::shared_ptr<ArchiveStream> tail) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<DrainImpl>(std::move(inner), std::move(tail), state), state);
}

/// Copie exactement `size` octets (taille déjà inscrite dans un en-tête) ;
/// un contenu d'une autre taille (fichier modifié pendant l'écriture) est une
/// erreur : l'archive serait illisible.
[[nodiscard]] inline Result<bool, ArchiveError>
CopyStreamExactly(ArchiveStream& content, sdl3::IOStream& out, uint64_t size, const String& path) {
	std::vector<uint8_t> chunk(1 << 16);
	uint64_t copied = 0;
	for (;;) {
		const size_t want = size_t(std::min<uint64_t>(chunk.size(), size - copied + 1));
		auto done = StreamRead(content, chunk.data(), want);
		if (done.IsError())
			return Err(done.Error());
		if (done.Value() == 0)
			break;
		if (copied + done.Value() > size)
			return Err(
				MakeError(ErrorKind::INVALID_ARGUMENT,
						  String::Format("%s est plus grand que sa taille annoncée", path.CStr())));
		if (!out.WriteExact(chunk.data(), done.Value()))
			return Err(MakeError(ErrorKind::IO, String("écriture impossible")));
		copied += done.Value();
	}
	if (copied != size)
		return Err(
			MakeError(ErrorKind::INVALID_ARGUMENT,
					  String::Format("%s est plus petit que sa taille annoncée", path.CStr())));
	return Ok(true);
}

/// Parties lues à la suite (extents ISO, volumes).
class ConcatStreamImpl final : public DecoderImpl {
public:
	ConcatStreamImpl(std::vector<ArchiveStream> parts, Option<uint64_t> size, StreamStatePtr state)
		: DecoderImpl(std::move(state), size), m_parts(std::move(parts)) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override {
		while (m_current < m_parts.size()) {
			auto got = StreamRead(m_parts[m_current], out, max);
			if (got.IsError() || got.Value() > 0)
				return got;
			++m_current;
		}
		return Ok(size_t(0));
	}
	bool Restart() override {
		for (ArchiveStream& part : m_parts)
			if (part.io.Seek(0, SDL_IO_SEEK_SET) != 0)
				return false;
		m_current = 0;
		return true;
	}

private:
	std::vector<ArchiveStream> m_parts;
	size_t m_current = 0;
};

[[nodiscard]] inline Result<ArchiveStream, ArchiveError>
OpenConcatStream(std::vector<ArchiveStream> parts, Option<uint64_t> size = NONE) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<ConcatStreamImpl>(std::move(parts), size, state), state);
}

/// Transmet les écritures à `target` (non possédé) en comptant les octets :
/// `Tell` rend le nombre d'octets écrits depuis la création.
class CountingSinkImpl : public sdl3::IOStreamImpl {
public:
	CountingSinkImpl(sdl3::IOStream& target, std::shared_ptr<uint64_t> counter)
		: m_target(&target), m_counter(std::move(counter)) {}
	[[nodiscard]] Sint64 Size() override { return Sint64(*m_counter); }
	Sint64 Seek(Sint64 offset, sdl3::IOWhence whence) override {
		return offset == 0 && whence == sdl3::IOWhence::SEEK_CURRENT ? Sint64(*m_counter) : -1;
	}
	size_t Write(const void* buffer, size_t size, sdl3::IOStatus& status) override {
		if (!m_target->WriteExact(buffer, size)) {
			status = sdl3::IOStatus::ERROR;
			return 0;
		}
		*m_counter += size;
		return size;
	}
	bool Flush(sdl3::IOStatus&) override { return m_target->Flush(); }

private:
	sdl3::IOStream* m_target;
	std::shared_ptr<uint64_t> m_counter;
};

[[nodiscard]] inline Result<ArchiveStream, ArchiveError>
OpenCountingSink(sdl3::IOStream& target, std::shared_ptr<uint64_t> counter) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<CountingSinkImpl>(target, std::move(counter)), state);
}

/// Flux en écriture vers un tampon mémoire partagé (lisible après fermeture).
class MemorySinkImpl : public sdl3::IOStreamImpl {
public:
	explicit MemorySinkImpl(std::shared_ptr<Bytes> target) : m_target(std::move(target)) {}
	[[nodiscard]] Sint64 Size() override { return Sint64(m_target->size()); }
	Sint64 Seek(Sint64 offset, sdl3::IOWhence whence) override {
		return offset == 0 && whence == sdl3::IOWhence::SEEK_CURRENT ? Sint64(m_target->size())
																	 : -1;
	}
	size_t Write(const void* buffer, size_t size, sdl3::IOStatus&) override {
		const auto* bytes = static_cast<const uint8_t*>(buffer);
		m_target->insert(m_target->end(), bytes, bytes + size);
		return size;
	}

private:
	std::shared_ptr<Bytes> m_target;
};

[[nodiscard]] inline Result<ArchiveStream, ArchiveError>
OpenMemorySink(std::shared_ptr<Bytes> target) {
	auto state = std::make_shared<StreamState>();
	return MakeStream(std::make_unique<MemorySinkImpl>(std::move(target)), state);
}

// ============================================================================
// Interfaces des formats
// ============================================================================

class VirtualFs;

/// Lecteur d'archive ouvert. Les entrées sont indexées dans l'ordre de
/// l'archive ; `OpenEntry` rend le contenu d'une entrée en FLUX (décompressé,
/// déchiffré et vérifié au fil de la lecture), valable tant que le lecteur
/// existe. `Extract` le lit entièrement en mémoire.
class ArchiveReader {
public:
	virtual ~ArchiveReader() = default;

	[[nodiscard]] virtual Format GetFormat() const noexcept = 0;
	[[nodiscard]] virtual const std::vector<EntryInfo>& Entries() const noexcept = 0;
	/// Contenu d'une entrée (fichier ; cible pour un lien ; vide pour un
	/// dossier).
	[[nodiscard]] virtual Result<ArchiveStream, ArchiveError> OpenEntry(size_t index) = 0;
	/// Remplace le mot de passe (nouvel essai après WRONG_PASSWORD).
	virtual void SetPassword(const String& password) = 0;
	/// Taille maximale d'une entrée lue en mémoire (`ReadOptions::maxEntrySize`).
	[[nodiscard]] virtual uint64_t EntryLimit() const noexcept = 0;

	/// Contenu complet d'une entrée en mémoire (plafonné par `EntryLimit`).
	[[nodiscard]] Result<Bytes, ArchiveError> Extract(size_t index) {
		if (index >= Entries().size())
			return Err(MakeError(ErrorKind::INVALID_ARGUMENT, String("entrée inexistante")));
		const EntryInfo& entry = Entries()[index];
		if (entry.size > EntryLimit())
			return Err(
				MakeError(ErrorKind::LIMIT,
						  String::Format("%s : %llu octets dépassent la limite", entry.path.CStr(),
										 static_cast<unsigned long long>(entry.size))));
		auto stream = OpenEntry(index);
		if (stream.IsError())
			return Err(stream.Error());
		auto bytes = ReadStreamToEnd(stream.Value(), EntryLimit(), entry.size);
		if (bytes.IsError()) {
			ArchiveError error = bytes.Error();
			if (error.kind == ErrorKind::LIMIT)
				error.message = String::Format("%s : %s", entry.path.CStr(), error.message.CStr());
			return Err(error);
		}
		return bytes;
	}

	[[nodiscard]] bool HasEncryptedEntries() const noexcept {
		for (const EntryInfo& entry : Entries())
			if (entry.encrypted)
				return true;
		return false;
	}
	[[nodiscard]] Option<size_t> Find(const String& path) const {
		for (size_t i = 0; i < Entries().size(); ++i)
			if (Entries()[i].path == path)
				return Some(i);
		return NONE;
	}
};

/// Écrivain d'archive : sérialise un arbre complet dans un flux.
class ArchiveWriter {
public:
	virtual ~ArchiveWriter() = default;
	[[nodiscard]] virtual Format GetFormat() const noexcept = 0;
	[[nodiscard]] virtual Result<bool, ArchiveError> Write(const VirtualFs& fs, sdl3::IOStream& out,
														   const WriteOptions& options) = 0;
};

} // namespace data::archive
