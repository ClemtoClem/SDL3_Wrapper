// Définitions de sdl3/iostream.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sdl3/iostream.hpp"

namespace sdl3 {

// ── IOStreamImpl ─────────────────────────────────────────────────────────────

Sint64 IOStreamImpl::Seek(Sint64 offset, IOWhence whence) {
    (void)offset;
    (void)whence;
    return -1;
}

size_t IOStreamImpl::Read(void *buffer, size_t size, IOStatus &status) {
    (void)buffer;
    (void)size;
    status = IOStatus::WRITEONLY;
    return 0;
}

size_t IOStreamImpl::Write(const void *buffer, size_t size, IOStatus &status) {
    (void)buffer;
    (void)size;
    status = IOStatus::READONLY;
    return 0;
}

bool IOStreamImpl::Flush(IOStatus &status) {
    (void)status;
    return true;
}

// ── IOStream ─────────────────────────────────────────────────────────────────

Result<IOStream, StringView> IOStream::FromFile(const String &path, const char *mode) {
    auto *io = SDL_IOFromFile(path.c_str(), mode);
    if (!io)
        return Err(GetError());
    return Ok(IOStream(io));
}

Result<IOStream, StringView> IOStream::FromMemory(void *mem, size_t size) {
    auto *io = SDL_IOFromMem(mem, size);
    if (!io)
        return Err(GetError());
    return Ok(IOStream(io));
}

Result<IOStream, StringView> IOStream::FromConstMemory(const void *mem, size_t size) {
    auto *io = SDL_IOFromConstMem(mem, size);
    if (!io)
        return Err(GetError());
    return Ok(IOStream(io));
}

Result<IOStream, StringView> IOStream::FromDynamicMemory() {
    auto *io = SDL_IOFromDynamicMem();
    if (!io)
        return Err(GetError());
    return Ok(IOStream(io));
}

Result<IOStream, StringView> IOStream::FromImpl(std::unique_ptr<IOStreamImpl> impl) {
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

Sint64 IOStream::Seek(Sint64 offset, SDL_IOWhence whence) noexcept {
    return m_handle ? SDL_SeekIO(m_handle, offset, whence) : -1;
}

Sint64 IOStream::Seek(Sint64 offset, IOWhence whence) noexcept {
    return m_handle ? SDL_SeekIO(m_handle, offset, static_cast<SDL_IOWhence>(whence)) : -1;
}

IOStatus IOStream::Status() const noexcept {
    return m_handle ? static_cast<IOStatus>(SDL_GetIOStatus(m_handle)) : IOStatus::ERROR;
}

Option<uint8_t> IOStream::ReadU8() noexcept {
    uint8_t v{};
    if (!m_handle || !SDL_ReadU8(m_handle, &v))
        return NONE;
    return Some(v);
}

Option<uint16_t> IOStream::ReadU16Le() noexcept {
    uint16_t v{};
    if (!m_handle || !SDL_ReadU16LE(m_handle, &v))
        return NONE;
    return Some(v);
}

Option<uint32_t> IOStream::ReadU32Le() noexcept {
    uint32_t v{};
    if (!m_handle || !SDL_ReadU32LE(m_handle, &v))
        return NONE;
    return Some(v);
}

Option<uint64_t> IOStream::ReadU64Le() noexcept {
    uint64_t v{};
    if (!m_handle || !SDL_ReadU64LE(m_handle, &v))
        return NONE;
    return Some(v);
}

Option<uint16_t> IOStream::ReadU16Be() noexcept {
    uint16_t v{};
    if (!m_handle || !SDL_ReadU16BE(m_handle, &v))
        return NONE;
    return Some(v);
}

Option<uint32_t> IOStream::ReadU32Be() noexcept {
    uint32_t v{};
    if (!m_handle || !SDL_ReadU32BE(m_handle, &v))
        return NONE;
    return Some(v);
}

Option<uint64_t> IOStream::ReadU64Be() noexcept {
    uint64_t v{};
    if (!m_handle || !SDL_ReadU64BE(m_handle, &v))
        return NONE;
    return Some(v);
}

Option<int8_t> IOStream::ReadS8() noexcept {
    int8_t v{};
    if (!m_handle || !SDL_ReadS8(m_handle, &v))
        return NONE;
    return Some(v);
}

Option<int16_t> IOStream::ReadS16Le() noexcept {
    int16_t v{};
    if (!m_handle || !SDL_ReadS16LE(m_handle, &v))
        return NONE;
    return Some(v);
}

Option<int32_t> IOStream::ReadS32Le() noexcept {
    int32_t v{};
    if (!m_handle || !SDL_ReadS32LE(m_handle, &v))
        return NONE;
    return Some(v);
}

Option<int64_t> IOStream::ReadS64Le() noexcept {
    int64_t v{};
    if (!m_handle || !SDL_ReadS64LE(m_handle, &v))
        return NONE;
    return Some(v);
}

Option<int16_t> IOStream::ReadS16Be() noexcept {
    int16_t v{};
    if (!m_handle || !SDL_ReadS16BE(m_handle, &v))
        return NONE;
    return Some(v);
}

Option<int32_t> IOStream::ReadS32Be() noexcept {
    int32_t v{};
    if (!m_handle || !SDL_ReadS32BE(m_handle, &v))
        return NONE;
    return Some(v);
}

Option<int64_t> IOStream::ReadS64Be() noexcept {
    int64_t v{};
    if (!m_handle || !SDL_ReadS64BE(m_handle, &v))
        return NONE;
    return Some(v);
}

bool IOStream::ReadExact(void *buf, size_t size) noexcept {
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

bool IOStream::WriteExact(const void *buf, size_t size) noexcept {
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

bool IOStream::Close() noexcept {
    SDL_IOStream *handle = Release();
    return handle == nullptr || SDL_CloseIO(handle);
}

std::vector<uint8_t> IOStream::DynamicMemoryBytes() const {
    if (!m_handle)
        return {};
    SDL_PropertiesID props = SDL_GetIOProperties(m_handle);
    auto *data = static_cast<const uint8_t *>(SDL_GetPointerProperty(props, SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
    Sint64 size = SDL_GetIOSize(m_handle);
    if (!data || size <= 0)
        return {};
    return std::vector<uint8_t>(data, data + size);
}

std::vector<uint8_t> IOStream::ReadAll() noexcept {
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

// ── IOStreamView ─────────────────────────────────────────────────────────────

Sint64 IOStreamView::Seek(Sint64 offset, SDL_IOWhence whence) noexcept {
    return m_handle ? SDL_SeekIO(m_handle, offset, whence) : -1;
}

Result<std::vector<uint8_t>, StringView> ReadFile(const String &path) {
    auto res = IOStream::FromFile(path, "rb");
    if (!res)
        return Err(res.Error());
    return Ok(res.Value().ReadAll());
}

bool WriteFile(const String &path, const void *data, size_t size) {
    auto res = IOStream::FromFile(path, "wb");
    if (!res)
        return false;
    return res.Value().Write(data, size) == size;
}

} // namespace sdl3
