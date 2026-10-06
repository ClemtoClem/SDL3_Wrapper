// Définitions de sdl3/hidapi.hpp
// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include "sdl3/sdl3.hpp"
#include "sdl3/hidapi.hpp"

namespace sdl3 {

namespace hid::detail {

String WideToString(const wchar_t *w) {
    if (!w)
        return String();
    if constexpr (sizeof(wchar_t) == 2) {
        return String(unicode::FromUtf16(std::u16string_view(reinterpret_cast<const char16_t *>(w))));
    } else {
        return String(unicode::FromUtf32(std::u32string_view(reinterpret_cast<const char32_t *>(w))));
    }
}

std::wstring StringToWide(const String &s) {
    if constexpr (sizeof(wchar_t) == 2) {
        auto u16 = unicode::ToUtf16(s.View());
        return std::wstring(u16.begin(), u16.end());
    } else {
        auto u32 = unicode::ToUtf32(s.View());
        return std::wstring(u32.begin(), u32.end());
    }
}

} // namespace hid::detail

// ── HidContext ───────────────────────────────────────────────────────────────

HidContext::~HidContext() {
    if (owns)
        SDL_hid_exit();
}

Result<HidContext, Error> HidContext::Create() {
    HidContext ctx;
    ctx.owns = (SDL_hid_init() == 0);
    if (!ctx)
        return Err(GetError());
    return Ok(std::move(ctx));
}

namespace hid {

std::vector<HidDeviceInfo> Enumerate(uint16_t vendorId, uint16_t productId) {
    SDL_hid_device_info *list = SDL_hid_enumerate(vendorId, productId);
    std::vector<HidDeviceInfo> out;
    for (auto *d = list; d; d = d->next)
        out.emplace_back(*d);
    if (list)
        SDL_hid_free_enumeration(list);
    return out;
}

} // namespace hid

// ── HidDevice ────────────────────────────────────────────────────────────────

Result<HidDevice, StringView> HidDevice::Open(uint16_t vendorId, uint16_t productId, const String &serial) {
    std::wstring wserial = serial.IsEmpty() ? std::wstring() : hid::detail::StringToWide(serial);
    auto *d = SDL_hid_open(vendorId, productId, serial.IsEmpty() ? nullptr : wserial.c_str());
    if (!d)
        return Err(GetError());
    return Ok(HidDevice(d));
}

Result<HidDevice, StringView> HidDevice::OpenPath(const String &path) {
    auto *d = SDL_hid_open_path(path.c_str());
    if (!d)
        return Err(GetError());
    return Ok(HidDevice(d));
}

SDL_PropertiesID HidDevice::Properties() const noexcept {
    return m_handle ? SDL_hid_get_properties(m_handle) : 0;
}

int HidDevice::Write(const void *data, size_t len) noexcept {
    return m_handle ? SDL_hid_write(m_handle, static_cast<const unsigned char *>(data), len) : -1;
}

int HidDevice::ReadTimeout(void *data, size_t len, int milliseconds) noexcept {
    return m_handle ? SDL_hid_read_timeout(m_handle, static_cast<unsigned char *>(data), len, milliseconds) : -1;
}

int HidDevice::ReadTimeout(std::span<uint8_t> data, int milliseconds) noexcept {
    return ReadTimeout(data.data(), data.size(), milliseconds);
}

int HidDevice::Read(void *data, size_t len) noexcept {
    return m_handle ? SDL_hid_read(m_handle, static_cast<unsigned char *>(data), len) : -1;
}

bool HidDevice::SetNonBlocking(bool nonblock) noexcept {
    return m_handle && SDL_hid_set_nonblocking(m_handle, nonblock ? 1 : 0) == 0;
}

int HidDevice::SendFeatureReport(std::span<const uint8_t> data) noexcept {
    return m_handle ? SDL_hid_send_feature_report(m_handle, data.data(), data.size()) : -1;
}

int HidDevice::GetFeatureReport(std::span<uint8_t> data) noexcept {
    return m_handle ? SDL_hid_get_feature_report(m_handle, data.data(), data.size()) : -1;
}

int HidDevice::GetInputReport(std::span<uint8_t> data) noexcept {
    return m_handle ? SDL_hid_get_input_report(m_handle, data.data(), data.size()) : -1;
}

Option<String> HidDevice::ManufacturerString() const {
    wchar_t buf[256]{};
    if (!m_handle || SDL_hid_get_manufacturer_string(m_handle, buf, 256) != 0)
        return NONE;
    return Some(hid::detail::WideToString(buf));
}

Option<String> HidDevice::ProductString() const {
    wchar_t buf[256]{};
    if (!m_handle || SDL_hid_get_product_string(m_handle, buf, 256) != 0)
        return NONE;
    return Some(hid::detail::WideToString(buf));
}

Option<String> HidDevice::SerialNumberString() const {
    wchar_t buf[256]{};
    if (!m_handle || SDL_hid_get_serial_number_string(m_handle, buf, 256) != 0)
        return NONE;
    return Some(hid::detail::WideToString(buf));
}

Option<String> HidDevice::IndexedString(int index) const {
    wchar_t buf[256]{};
    if (!m_handle || SDL_hid_get_indexed_string(m_handle, index, buf, 256) != 0)
        return NONE;
    return Some(hid::detail::WideToString(buf));
}

Option<HidDeviceInfo> HidDevice::DeviceInfo() const {
    if (!m_handle)
        return NONE;
    auto *info = SDL_hid_get_device_info(m_handle);
    if (!info)
        return NONE;
    return Some(HidDeviceInfo(*info));
}

Result<std::vector<uint8_t>, StringView> HidDevice::ReportDescriptor(size_t maxSize) const {
    if (!m_handle)
        return Err(StringView("device invalide"));
    std::vector<uint8_t> buf(maxSize);
    int n = SDL_hid_get_report_descriptor(m_handle, buf.data(), buf.size());
    if (n < 0)
        return Err(GetError());
    buf.resize(size_t(n));
    return Ok(std::move(buf));
}

} // namespace sdl3
