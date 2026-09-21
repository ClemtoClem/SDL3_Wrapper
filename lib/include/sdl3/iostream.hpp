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
    virtual Sint64 Seek(Sint64 offset, IOWhence whence) {
        (void)offset;
        (void)whence;
        return -1;
    }
    virtual size_t Read(void *buffer, size_t size, IOStatus &status) {
        (void)buffer;
        (void)size;
        status = IOStatus::WRITEONLY;
        return 0;
    }
    virtual size_t Write(const void *buffer, size_t size, IOStatus &status) {
        (void)buffer;
        (void)size;
        status = IOStatus::READONLY;
        return 0;
    }
    virtual bool Flush(IOStatus &status) {
        (void)status;
        return true;
    }
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

    [[nodiscard]] static Result<IOStream, StringView> FromFile(const String &path, const char *mode = "rb") {
        auto *io = SDL_IOFromFile(path.c_str(), mode);
        if (!io)
            return Err(GetError());
        return Ok(IOStream(io));
    }

    [[nodiscard]] static Result<IOStream, StringView> FromMemory(void *mem, size_t size) {
        auto *io = SDL_IOFromMem(mem, size);
        if (!io)
            return Err(GetError());
        return Ok(IOStream(io));
    }

    [[nodiscard]] static Result<IOStream, StringView> FromConstMemory(const void *mem, size_t size) {
        auto *io = SDL_IOFromConstMem(mem, size);
        if (!io)
            return Err(GetError());
        return Ok(IOStream(io));
    }

    /// Flux mémoire extensible en écriture (SDL_IOFromDynamicMem) : le tampon
    /// grandit à chaque écriture ; `DynamicMemoryBytes()` en rend une copie.
    /// Sert à produire un fichier binaire en mémoire avec les mêmes
    /// Write*Le/Be qu'un fichier disque.
    [[nodiscard]] static Result<IOStream, StringView> FromDynamicMemory() {
        auto *io = SDL_IOFromDynamicMem();
        if (!io)
            return Err(GetError());
        return Ok(IOStream(io));
    }

    /// Flux dont le comportement est fourni par `impl` (possédé par le flux,
    /// détruit à sa fermeture).
    [[nodiscard]] static Result<IOStream, StringView> FromImpl(std::unique_ptr<IOStreamImpl> impl) {
        SDL_IOStreamInterface iface;
        SDL_INIT_INTERFACE(&iface);
        iface.size = [](void *user) -> Sint64 { return static_cast<IOStreamImpl *>(user)->Size(); };
        iface.seek = [](void *user, Sint64 offset, SDL_IOWhence whence) -> Sint64 {
            return static_cast<IOStreamImpl *>(user)->Seek(offset, static_cast<IOWhence>(whence));
        };
        iface.read = [](void *user, void *ptr, size_t size, SDL_IOStatus *status) -> size_t {
            IOStatus state = IOStatus::READY;
            const size_t done = static_cast<IOStreamImpl *>(user)->Read(ptr, size, state);
            *status = static_cast<SDL_IOStatus>(state);
            return done;
        };
        iface.write = [](void *user, const void *ptr, size_t size, SDL_IOStatus *status) -> size_t {
            IOStatus state = IOStatus::READY;
            const size_t done = static_cast<IOStreamImpl *>(user)->Write(ptr, size, state);
            *status = static_cast<SDL_IOStatus>(state);
            return done;
        };
        iface.flush = [](void *user, SDL_IOStatus *status) -> bool {
            IOStatus state = IOStatus::READY;
            const bool done = static_cast<IOStreamImpl *>(user)->Flush(state);
            *status = static_cast<SDL_IOStatus>(state);
            return done;
        };
        iface.close = [](void *user) -> bool {
            auto *owned = static_cast<IOStreamImpl *>(user);
            const bool closed = owned->Close();
            delete owned;
            return closed;
        };
        IOStreamImpl *raw = impl.release();
        auto *io = SDL_OpenIO(&iface, raw);
        if (!io) {
            delete raw;
            return Err(GetError());
        }
        return Ok(IOStream(io));
    }

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

    Sint64 Seek(Sint64 offset, SDL_IOWhence whence = SDL_IO_SEEK_SET) noexcept {
        return m_handle ? SDL_SeekIO(m_handle, offset, whence) : -1;
    }
    Sint64 Seek(Sint64 offset, IOWhence whence) noexcept {
        return m_handle ? SDL_SeekIO(m_handle, offset, static_cast<SDL_IOWhence>(whence)) : -1;
    }
    Sint64 Tell() const noexcept { return m_handle ? SDL_TellIO(m_handle) : -1; }
    Sint64 GetSize() const noexcept { return m_handle ? SDL_GetIOSize(m_handle) : -1; }
    /// État laissé par la dernière opération (fin de données, erreur…).
    [[nodiscard]] IOStatus Status() const noexcept {
        return m_handle ? static_cast<IOStatus>(SDL_GetIOStatus(m_handle)) : IOStatus::ERROR;
    }

    // ── Typed reads (little-endian) ──────────────────────────────────────────

    Option<uint8_t> ReadU8() noexcept {
        uint8_t v{};
        if (!m_handle || !SDL_ReadU8(m_handle, &v))
            return NONE;
        return Some(v);
    }
    Option<uint16_t> ReadU16Le() noexcept {
        uint16_t v{};
        if (!m_handle || !SDL_ReadU16LE(m_handle, &v))
            return NONE;
        return Some(v);
    }
    Option<uint32_t> ReadU32Le() noexcept {
        uint32_t v{};
        if (!m_handle || !SDL_ReadU32LE(m_handle, &v))
            return NONE;
        return Some(v);
    }
    Option<uint64_t> ReadU64Le() noexcept {
        uint64_t v{};
        if (!m_handle || !SDL_ReadU64LE(m_handle, &v))
            return NONE;
        return Some(v);
    }

    // ── Typed reads (big-endian, signés) ─────────────────────────────────────
    // Ordre d'octets IMPOSÉ par le nom de la méthode, jamais celui de l'hôte :
    // SDL convertit (SDL_Swap*) selon l'architecture.

    Option<uint16_t> ReadU16Be() noexcept {
        uint16_t v{};
        if (!m_handle || !SDL_ReadU16BE(m_handle, &v))
            return NONE;
        return Some(v);
    }
    Option<uint32_t> ReadU32Be() noexcept {
        uint32_t v{};
        if (!m_handle || !SDL_ReadU32BE(m_handle, &v))
            return NONE;
        return Some(v);
    }
    Option<uint64_t> ReadU64Be() noexcept {
        uint64_t v{};
        if (!m_handle || !SDL_ReadU64BE(m_handle, &v))
            return NONE;
        return Some(v);
    }
    Option<int8_t> ReadS8() noexcept {
        int8_t v{};
        if (!m_handle || !SDL_ReadS8(m_handle, &v))
            return NONE;
        return Some(v);
    }
    Option<int16_t> ReadS16Le() noexcept {
        int16_t v{};
        if (!m_handle || !SDL_ReadS16LE(m_handle, &v))
            return NONE;
        return Some(v);
    }
    Option<int32_t> ReadS32Le() noexcept {
        int32_t v{};
        if (!m_handle || !SDL_ReadS32LE(m_handle, &v))
            return NONE;
        return Some(v);
    }
    Option<int64_t> ReadS64Le() noexcept {
        int64_t v{};
        if (!m_handle || !SDL_ReadS64LE(m_handle, &v))
            return NONE;
        return Some(v);
    }
    Option<int16_t> ReadS16Be() noexcept {
        int16_t v{};
        if (!m_handle || !SDL_ReadS16BE(m_handle, &v))
            return NONE;
        return Some(v);
    }
    Option<int32_t> ReadS32Be() noexcept {
        int32_t v{};
        if (!m_handle || !SDL_ReadS32BE(m_handle, &v))
            return NONE;
        return Some(v);
    }
    Option<int64_t> ReadS64Be() noexcept {
        int64_t v{};
        if (!m_handle || !SDL_ReadS64BE(m_handle, &v))
            return NONE;
        return Some(v);
    }

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
    [[nodiscard]] bool ReadExact(void *buf, size_t size) noexcept {
        auto *bytes = static_cast<uint8_t *>(buf);
        while (size > 0) {
            if (!m_handle)
                return false;
            const size_t done = SDL_ReadIO(m_handle, bytes, size);
            if (done == 0)
                return false;
            bytes += done;
            size -= done;
        }
        return true;
    }
    /// Écrit EXACTEMENT `size` octets (écritures partielles relancées).
    [[nodiscard]] bool WriteExact(const void *buf, size_t size) noexcept {
        const auto *bytes = static_cast<const uint8_t *>(buf);
        while (size > 0) {
            if (!m_handle)
                return false;
            const size_t done = SDL_WriteIO(m_handle, bytes, size);
            if (done == 0)
                return false;
            bytes += done;
            size -= done;
        }
        return true;
    }
    bool Flush() noexcept { return m_handle && SDL_FlushIO(m_handle); }
    /// Ferme le flux et rend le statut de SDL_CloseIO (écritures en attente
    /// comprises) ; le destructeur, lui, ne peut pas signaler d'échec.
    bool Close() noexcept {
        SDL_IOStream *handle = Release();
        return handle == nullptr || SDL_CloseIO(handle);
    }

    /// Copie du tampon d'un flux créé par FromDynamicMemory() (vide sinon).
    [[nodiscard]] std::vector<uint8_t> DynamicMemoryBytes() const {
        if (!m_handle)
            return {};
        SDL_PropertiesID props = SDL_GetIOProperties(m_handle);
        auto *data = static_cast<const uint8_t *>(SDL_GetPointerProperty(props, SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
        Sint64 size = SDL_GetIOSize(m_handle);
        if (!data || size <= 0)
            return {};
        return std::vector<uint8_t>(data, data + size);
    }

    /// Read the entire remaining stream into a vector (taille inconnue
    /// acceptée : lecture par morceaux jusqu'à la fin).
    [[nodiscard]] std::vector<uint8_t> ReadAll() noexcept {
        if (!m_handle)
            return {};
        Sint64 sz = GetSize();
        Sint64 pos = Tell();
        std::vector<uint8_t> buf;
        if (sz >= 0 && pos >= 0) {
            if (sz <= pos)
                return {};
            buf.resize(static_cast<size_t>(sz - pos));
            size_t filled = 0;
            while (filled < buf.size()) {
                const size_t done = SDL_ReadIO(m_handle, buf.data() + filled, buf.size() - filled);
                if (done == 0)
                    break;
                filled += done;
            }
            buf.resize(filled);
            return buf;
        }
        uint8_t chunk[65536];
        for (;;) {
            const size_t done = SDL_ReadIO(m_handle, chunk, sizeof(chunk));
            if (done == 0)
                break;
            buf.insert(buf.end(), chunk, chunk + done);
        }
        return buf;
    }
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

    Sint64 Seek(Sint64 offset, SDL_IOWhence whence = SDL_IO_SEEK_SET) noexcept {
        return m_handle ? SDL_SeekIO(m_handle, offset, whence) : -1;
    }
    Sint64 Tell() const noexcept { return m_handle ? SDL_TellIO(m_handle) : -1; }
    Sint64 GetSize() const noexcept { return m_handle ? SDL_GetIOSize(m_handle) : -1; }
};

// ============================================================================
// Convenience free functions
// ============================================================================

[[nodiscard]] inline Result<std::vector<uint8_t>, StringView> ReadFile(const String &path) {
    auto res = IOStream::FromFile(path, "rb");
    if (!res)
        return Err(res.Error());
    return Ok(res.Value().ReadAll());
}

[[nodiscard]] inline bool WriteFile(const String &path, const void *data, size_t size) {
    auto res = IOStream::FromFile(path, "wb");
    if (!res)
        return false;
    return res.Value().Write(data, size) == size;
}

} // namespace sdl3
