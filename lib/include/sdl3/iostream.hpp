#pragma once
#include <SDL3/SDL.h>
#include <memory>
#include <span>
#include <vector>

#include "../core/core.hpp"
#include "error.hpp"

namespace sdl3 {

enum class IOWhence : int {
    SeekSet = SDL_IO_SEEK_SET,     /**< Seek from the beginning of data */
    SEEK_CURRENT = SDL_IO_SEEK_CUR, /**< Seek relative to current read point */
    SeekEnd = SDL_IO_SEEK_END,     /**< Seek relative to the end of data */
};

/// État d'un flux après une lecture ou une écriture (SDL_IOStatus).
enum class IOStatus : int {
    READY = SDL_IO_STATUS_READY,         ///< tout va bien
    ERROR = SDL_IO_STATUS_ERROR,         ///< erreur de lecture/écriture
    END = SDL_IO_STATUS_EOF,             ///< fin des données
    NOT_READY = SDL_IO_STATUS_NOT_READY, ///< pas de données pour l'instant (non bloquant)
    READONLY = SDL_IO_STATUS_READONLY,   ///< écriture sur un flux en lecture seule
    WRITEONLY = SDL_IO_STATUS_WRITEONLY, ///< lecture sur un flux en écriture seule
};

/// Flux implémenté en C++ : un décompresseur, une fenêtre sur un autre flux…
/// `IOStream::FromImpl` l'expose comme n'importe quel SDL_IOStream (SDL_OpenIO),
/// utilisable ensuite avec ReadU32Le, WriteU64Be, ReadExact, etc.
///
/// `Read` doit remplir tout le tampon demandé tant que des données existent :
/// SDL_ReadU32LE et consorts ne relancent pas une lecture partielle.
class IOStreamImpl {
public:
    virtual ~IOStreamImpl() = default;
    /// Taille totale, -1 si inconnue.
    [[nodiscard]] virtual Sint64 Size() { return -1; }
    /// Nouvelle position, -1 si le déplacement est impossible.
    virtual Sint64 Seek(Sint64 offset, IOWhence whence);
    virtual size_t Read(void *buffer, size_t size, IOStatus &status);
    virtual size_t Write(const void *buffer, size_t size, IOStatus &status);
    virtual bool Flush(IOStatus &status);
    /// Appelé une fois, juste avant la destruction (SDL_CloseIO) : faux si
    /// les dernières données n'ont pas pu être écrites.
    virtual bool Close() { return true; }
};

// ============================================================================
// IOStream — RAII SDL_IOStream
// ============================================================================

class IOStream : public Wrapper<SDL_IOStream, SDL_CloseIO> {
public:
    using Wrapper::Wrapper;

    // ── Constructors ─────────────────────────────────────────────────────────

    [[nodiscard]] static Result<IOStream, StringView> FromFile(const String &path, const char *mode = "rb");

    [[nodiscard]] static Result<IOStream, StringView> FromMemory(void *mem, size_t size);

    [[nodiscard]] static Result<IOStream, StringView> FromConstMemory(const void *mem, size_t size);

    /// Flux mémoire extensible en écriture (SDL_IOFromDynamicMem) : le tampon
    /// grandit à chaque écriture ; `DynamicMemoryBytes()` en rend une copie.
    /// Sert à produire un fichier binaire en mémoire avec les mêmes
    /// Write*Le/Be qu'un fichier disque.
    [[nodiscard]] static Result<IOStream, StringView> FromDynamicMemory();

    /// Flux dont le comportement est fourni par `impl` (possédé par le flux,
    /// détruit à sa fermeture).
    [[nodiscard]] static Result<IOStream, StringView> FromImpl(std::unique_ptr<IOStreamImpl> impl);

    // ── I/O ──────────────────────────────────────────────────────────────────

    size_t Read(void *buf, size_t size) noexcept { return m_handle ? SDL_ReadIO(m_handle, buf, size) : 0; }
    size_t Write(const void *buf, size_t size) noexcept { return m_handle ? SDL_WriteIO(m_handle, buf, size) : 0; }

    template <typename T> size_t Read(std::span<T> buf) noexcept {
        return Read(buf.data(), buf.size_bytes()) / sizeof(T);
    }
    template <typename T> size_t Write(std::span<const T> buf) noexcept {
        return Write(buf.data(), buf.size_bytes()) / sizeof(T);
    }

    // ── Seek / Tell ──────────────────────────────────────────────────────────

    Sint64 Seek(Sint64 offset, SDL_IOWhence whence = SDL_IO_SEEK_SET) noexcept;
    Sint64 Seek(Sint64 offset, IOWhence whence) noexcept;
    Sint64 Tell() const noexcept { return m_handle ? SDL_TellIO(m_handle) : -1; }
    Sint64 GetSize() const noexcept { return m_handle ? SDL_GetIOSize(m_handle) : -1; }
    /// État laissé par la dernière opération (fin de données, erreur…).
    [[nodiscard]] IOStatus Status() const noexcept;

    // ── Typed reads (little-endian) ──────────────────────────────────────────

    Option<uint8_t> ReadU8() noexcept;
    Option<uint16_t> ReadU16Le() noexcept;
    Option<uint32_t> ReadU32Le() noexcept;
    Option<uint64_t> ReadU64Le() noexcept;

    // ── Typed reads (big-endian, signés) ─────────────────────────────────────
    // Ordre d'octets IMPOSÉ par le nom de la méthode, jamais celui de l'hôte :
    // SDL convertit (SDL_Swap*) selon l'architecture.

    Option<uint16_t> ReadU16Be() noexcept;
    Option<uint32_t> ReadU32Be() noexcept;
    Option<uint64_t> ReadU64Be() noexcept;
    Option<int8_t> ReadS8() noexcept;
    Option<int16_t> ReadS16Le() noexcept;
    Option<int32_t> ReadS32Le() noexcept;
    Option<int64_t> ReadS64Le() noexcept;
    Option<int16_t> ReadS16Be() noexcept;
    Option<int32_t> ReadS32Be() noexcept;
    Option<int64_t> ReadS64Be() noexcept;

    bool WriteU8(uint8_t v) noexcept { return m_handle && SDL_WriteU8(m_handle, v); }
    bool WriteU16Le(uint16_t v) noexcept { return m_handle && SDL_WriteU16LE(m_handle, v); }
    bool WriteU32Le(uint32_t v) noexcept { return m_handle && SDL_WriteU32LE(m_handle, v); }
    bool WriteU64Le(uint64_t v) noexcept { return m_handle && SDL_WriteU64LE(m_handle, v); }
    bool WriteU16Be(uint16_t v) noexcept { return m_handle && SDL_WriteU16BE(m_handle, v); }
    bool WriteU32Be(uint32_t v) noexcept { return m_handle && SDL_WriteU32BE(m_handle, v); }
    bool WriteU64Be(uint64_t v) noexcept { return m_handle && SDL_WriteU64BE(m_handle, v); }
    bool WriteS8(int8_t v) noexcept { return m_handle && SDL_WriteS8(m_handle, v); }
    bool WriteS16Le(int16_t v) noexcept { return m_handle && SDL_WriteS16LE(m_handle, v); }
    bool WriteS32Le(int32_t v) noexcept { return m_handle && SDL_WriteS32LE(m_handle, v); }
    bool WriteS64Le(int64_t v) noexcept { return m_handle && SDL_WriteS64LE(m_handle, v); }
    bool WriteS16Be(int16_t v) noexcept { return m_handle && SDL_WriteS16BE(m_handle, v); }
    bool WriteS32Be(int32_t v) noexcept { return m_handle && SDL_WriteS32BE(m_handle, v); }
    bool WriteS64Be(int64_t v) noexcept { return m_handle && SDL_WriteS64BE(m_handle, v); }

    // ── Helpers ──────────────────────────────────────────────────────────────

    /// Lit EXACTEMENT `size` octets (faux si le flux se termine avant).
    /// Relance les lectures partielles (flux personnalisés, tubes) ; s'arrête
    /// à la fin des données ou sur une erreur.
    [[nodiscard]] bool ReadExact(void *buf, size_t size) noexcept;
    /// Écrit EXACTEMENT `size` octets (écritures partielles relancées).
    [[nodiscard]] bool WriteExact(const void *buf, size_t size) noexcept;
    bool Flush() noexcept { return m_handle && SDL_FlushIO(m_handle); }
    /// Ferme le flux et rend le statut de SDL_CloseIO (écritures en attente
    /// comprises) ; le destructeur, lui, ne peut pas signaler d'échec.
    bool Close() noexcept;

    /// Copie du tampon d'un flux créé par FromDynamicMemory() (vide sinon).
    [[nodiscard]] std::vector<uint8_t> DynamicMemoryBytes() const;

    /// Read the entire remaining stream into a vector (taille inconnue
    /// acceptée : lecture par morceaux jusqu'à la fin).
    [[nodiscard]] std::vector<uint8_t> ReadAll() noexcept;
};

// ============================================================================
// IOStreamView — vue non-possédante sur un SDL_IOStream prêté (ex: stdio d'un
// sous-processus). Ne ferme jamais le flux sous-jacent.
// ============================================================================

class IOStreamView : public Borrowed<SDL_IOStream> {
public:
    using Borrowed::Borrowed;

    size_t Read(void *buf, size_t size) noexcept { return m_handle ? SDL_ReadIO(m_handle, buf, size) : 0; }
    size_t Write(const void *buf, size_t size) noexcept { return m_handle ? SDL_WriteIO(m_handle, buf, size) : 0; }

    Sint64 Seek(Sint64 offset, SDL_IOWhence whence = SDL_IO_SEEK_SET) noexcept;
    Sint64 Tell() const noexcept { return m_handle ? SDL_TellIO(m_handle) : -1; }
    Sint64 GetSize() const noexcept { return m_handle ? SDL_GetIOSize(m_handle) : -1; }
};

// ============================================================================
// Convenience free functions
// ============================================================================

[[nodiscard]] Result<std::vector<uint8_t>, StringView> ReadFile(const String &path);

[[nodiscard]] bool WriteFile(const String &path, const void *data, size_t size);

} // namespace sdl3
