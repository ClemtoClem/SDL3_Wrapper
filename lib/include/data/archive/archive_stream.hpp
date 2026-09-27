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

	void Fail(ArchiveError failure);
};
using StreamStatePtr = std::shared_ptr<StreamState>;

/// Flux d'archive : le flux SDL et son état (erreur typée).
struct ArchiveStream {
	sdl3::IOStream io;
	StreamStatePtr state;

	/// Erreur du flux : celle qu'il a signalée, sinon une erreur d'E/S
	/// générique si SDL rapporte un échec.
	[[nodiscard]] ArchiveError Failure(const char* context = "flux") const;
};

/// Enveloppe un `IOStreamImpl` dans un `ArchiveStream`.
[[nodiscard]] Result<ArchiveStream, ArchiveError>
MakeStream(std::unique_ptr<sdl3::IOStreamImpl> impl, StreamStatePtr state);

/// Lecture d'au plus `size` octets ; 0 = fin. Err si le flux a échoué.
[[nodiscard]] Result<size_t, ArchiveError> StreamRead(ArchiveStream& stream, void* buffer,
															 size_t size);

/// Tout le reste du flux, au plus `limit` octets (au-delà : `LIMIT`).
[[nodiscard]] Result<Bytes, ArchiveError>
ReadStreamToEnd(ArchiveStream& stream, uint64_t limit, uint64_t sizeHint = 0);

/// Copie le flux dans `out` ; rend le nombre d'octets copiés.
[[nodiscard]] Result<uint64_t, ArchiveError>
CopyStream(ArchiveStream& in, sdl3::IOStream& out, uint64_t limit = UINT64_MAX);

// ============================================================================
// Tampon d'entrée
// ============================================================================

/// Lecture tamponnée d'un flux source à partir de sa position courante, au
/// plus `limit` octets. `Byte` est en ligne : pas d'appel virtuel par octet.
class InputBuffer {
public:
	explicit InputBuffer(sdl3::IOStream& source, StreamStatePtr sourceState = {},
						 uint64_t limit = UINT64_MAX, size_t capacity = size_t(1) << 16);

	[[nodiscard]] bool Byte(uint8_t& out);

	/// Recharge le tampon ; faux s'il n'y a plus rien (fin ou erreur).
	bool Fill();

	/// Octets disponibles sans lecture (après un `Fill` éventuel).
	[[nodiscard]] size_t Available() const noexcept { return m_end - m_position; }
	[[nodiscard]] const uint8_t* Data() const noexcept { return m_buffer.data() + m_position; }
	void Advance(size_t count) noexcept { m_position += std::min(count, Available()); }

	/// Copie jusqu'à `size` octets bruts ; rend le nombre copié.
	size_t ReadRaw(uint8_t* out, size_t size);

	/// Saute `count` octets ; faux si l'entrée se termine avant.
	bool Skip(uint64_t count);

	/// Octets consommés depuis la position de départ.
	[[nodiscard]] uint64_t Consumed() const noexcept { return m_consumedBefore + m_position; }
	/// Vrai si l'entrée a échoué (et non simplement pris fin).
	[[nodiscard]] bool Failed() const noexcept { return m_failed; }
	[[nodiscard]] ArchiveError Failure(const char* context) const;

	/// Repart du début (pour recommencer un décodage).
	bool Rewind();

	/// Replace la source juste après le dernier octet consommé (les octets
	/// lus d'avance lui sont rendus) : utile quand d'autres données suivent.
	bool GiveBack(uint64_t unusedBytes = 0);

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
	void Reset(uint64_t capacity);
	void Put(uint8_t byte);
	/// Octet situé `distance` (≥ 1) octets en arrière ; distance vérifiée par
	/// `Has`.
	[[nodiscard]] uint8_t Get(uint64_t distance) const;
	[[nodiscard]] bool Has(uint64_t distance) const noexcept;
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

	size_t Read(void* buffer, size_t size, sdl3::IOStatus& status) final;

	Sint64 Seek(Sint64 offset, sdl3::IOWhence whence) final;

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
	Sint64 Seek(Sint64 offset, sdl3::IOWhence whence) override;
	size_t Read(void* buffer, size_t size, sdl3::IOStatus& status) override;

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
	Sint64 Seek(Sint64 offset, sdl3::IOWhence whence) override;
	size_t Read(void* buffer, size_t size, sdl3::IOStatus& status) override;

private:
	std::shared_ptr<const Bytes> m_shared;
	std::span<const uint8_t> m_bytes;
	size_t m_position = 0;
};

/// Fenêtre sur un flux parent.
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenSubStream(sdl3::IOStream& parent, uint64_t offset, uint64_t length);

/// Fenêtre sur un flux parent PARTAGÉ : la fenêtre le garde en vie (flux
/// décodé d'un bloc solide, lu par plusieurs entrées).
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenSharedSubStream(std::shared_ptr<ArchiveStream> parent, uint64_t offset, uint64_t length);

/// Flux lisant des octets en mémoire (qu'il possède).
[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenMemoryStream(Bytes bytes);

/// Fenêtre sur des octets partagés (bloc décodé gardé en cache).
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenSharedMemoryStream(std::shared_ptr<const Bytes> bytes, uint64_t offset, uint64_t length);

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
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

private:
	ArchiveStream m_inner;
	Option<uint64_t> m_expectedSize;
	Option<uint32_t> m_expectedCrc;
	String m_name;
	ErrorKind m_mismatch;
	uint64_t m_count = 0;
	uint32_t m_crc = 0;
};

[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenCheckedStream(ArchiveStream inner, Option<uint64_t> size, Option<uint32_t> crc,
				  const String& name, ErrorKind mismatch = ErrorKind::CORRUPT);

/// Lit exactement `size` octets d'un flux d'archive.
[[nodiscard]] Result<Bytes, ArchiveError> StreamReadExact(ArchiveStream& stream,
																 uint64_t size);

/// Flux de lecture d'un fichier du disque.
[[nodiscard]] Result<ArchiveStream, ArchiveError> OpenFileStream(const String& path);

/// Flux sur des octets NON possédés (qui doivent survivre au flux).
[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenViewStream(std::span<const uint8_t> bytes);

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
	Sint64 Seek(Sint64 offset, sdl3::IOWhence whence) override;
	size_t Read(void* buffer, size_t size, sdl3::IOStatus& status) override;
	size_t Write(const void* buffer, size_t size, sdl3::IOStatus& status) override;
	bool Flush(sdl3::IOStatus&) override { return m_target->Flush(); }

private:
	sdl3::IOStream* m_target;
};

[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenBorrowedStream(sdl3::IOStream& target);

/// Base des flux en écriture qui transforment ce qu'on leur écrit (compression,
/// chiffrement) et l'envoient dans `sink` (possédé, fermé avec eux). Les
/// données en attente sont émises à la FERMETURE : utiliser `FinishStream`.
class EncoderImpl : public sdl3::IOStreamImpl {
public:
	EncoderImpl(ArchiveStream sink, StreamStatePtr state)
		: m_sink(std::move(sink)), m_state(std::move(state)) {}

	/// Octets reçus jusqu'ici (position d'écriture).
	[[nodiscard]] Sint64 Size() override { return Sint64(m_received); }
	Sint64 Seek(Sint64 offset, sdl3::IOWhence whence) override;
	size_t Write(const void* buffer, size_t size, sdl3::IOStatus& status) final;
	bool Close() final;

protected:
	virtual Result<bool, ArchiveError> Consume(const uint8_t* data, size_t size) = 0;
	/// Émet tout ce qui reste (dernier bloc, pied, contrôle).
	virtual Result<bool, ArchiveError> Finish() = 0;

	/// Écrit dans le flux de sortie.
	[[nodiscard]] Result<bool, ArchiveError> Emit(const void* data, size_t size);
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
[[nodiscard]] Result<bool, ArchiveError> FinishStream(ArchiveStream& stream);

/// Écrit tout `data` dans un flux en écriture.
[[nodiscard]] Result<bool, ArchiveError> StreamWrite(ArchiveStream& stream,
															std::span<const uint8_t> data);

/// Copie un flux d'entrée vers un flux d'archive en écriture.
[[nodiscard]] Result<uint64_t, ArchiveError> CopyToStream(ArchiveStream& in,
																 ArchiveStream& out);

/// Lit `inner`, puis, arrivé à sa fin, lit `tail` jusqu'au bout (sans rien
/// rendre) : un contrôle placé en fin de `tail` (code d'authentification)
/// s'exécute même si le décodeur de `inner` n'a pas eu besoin de tout lire.
class DrainImpl final : public DecoderImpl {
public:
	DrainImpl(ArchiveStream inner, std::shared_ptr<ArchiveStream> tail, StreamStatePtr state)
		: DecoderImpl(std::move(state), NONE), m_inner(std::move(inner)), m_tail(std::move(tail)) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override { return m_inner.io.Seek(0, SDL_IO_SEEK_SET) == 0; }

private:
	ArchiveStream m_inner;
	std::shared_ptr<ArchiveStream> m_tail;
};

[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenDrainingStream(ArchiveStream inner, std::shared_ptr<ArchiveStream> tail);

/// Copie exactement `size` octets (taille déjà inscrite dans un en-tête) ;
/// un contenu d'une autre taille (fichier modifié pendant l'écriture) est une
/// erreur : l'archive serait illisible.
[[nodiscard]] Result<bool, ArchiveError>
CopyStreamExactly(ArchiveStream& content, sdl3::IOStream& out, uint64_t size, const String& path);

/// Parties lues à la suite (extents ISO, volumes).
class ConcatStreamImpl final : public DecoderImpl {
public:
	ConcatStreamImpl(std::vector<ArchiveStream> parts, Option<uint64_t> size, StreamStatePtr state)
		: DecoderImpl(std::move(state), size), m_parts(std::move(parts)) {}

protected:
	Result<size_t, ArchiveError> Produce(uint8_t* out, size_t max) override;
	bool Restart() override;

private:
	std::vector<ArchiveStream> m_parts;
	size_t m_current = 0;
};

[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenConcatStream(std::vector<ArchiveStream> parts, Option<uint64_t> size = NONE);

/// Transmet les écritures à `target` (non possédé) en comptant les octets :
/// `Tell` rend le nombre d'octets écrits depuis la création.
class CountingSinkImpl : public sdl3::IOStreamImpl {
public:
	CountingSinkImpl(sdl3::IOStream& target, std::shared_ptr<uint64_t> counter)
		: m_target(&target), m_counter(std::move(counter)) {}
	[[nodiscard]] Sint64 Size() override { return Sint64(*m_counter); }
	Sint64 Seek(Sint64 offset, sdl3::IOWhence whence) override;
	size_t Write(const void* buffer, size_t size, sdl3::IOStatus& status) override;
	bool Flush(sdl3::IOStatus&) override { return m_target->Flush(); }

private:
	sdl3::IOStream* m_target;
	std::shared_ptr<uint64_t> m_counter;
};

[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenCountingSink(sdl3::IOStream& target, std::shared_ptr<uint64_t> counter);

/// Flux en écriture vers un tampon mémoire partagé (lisible après fermeture).
class MemorySinkImpl : public sdl3::IOStreamImpl {
public:
	explicit MemorySinkImpl(std::shared_ptr<Bytes> target) : m_target(std::move(target)) {}
	[[nodiscard]] Sint64 Size() override { return Sint64(m_target->size()); }
	Sint64 Seek(Sint64 offset, sdl3::IOWhence whence) override;
	size_t Write(const void* buffer, size_t size, sdl3::IOStatus&) override;

private:
	std::shared_ptr<Bytes> m_target;
};

[[nodiscard]] Result<ArchiveStream, ArchiveError>
OpenMemorySink(std::shared_ptr<Bytes> target);

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
	[[nodiscard]] Result<Bytes, ArchiveError> Extract(size_t index);

	[[nodiscard]] bool HasEncryptedEntries() const noexcept;
	[[nodiscard]] Option<size_t> Find(const String& path) const;
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
