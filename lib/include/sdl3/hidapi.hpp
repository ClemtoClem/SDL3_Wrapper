#pragma once
#include <SDL3/SDL.h>
#include <span>
#include <vector>

#include "../core/core.hpp"
#include "../core/string_unicode.hpp"
#include "../core/wrapper.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// HidBusType
// ============================================================================

enum class HidBusType : int {
    UNKNOWN = SDL_HID_API_BUS_UNKNOWN,
    USB = SDL_HID_API_BUS_USB,
    BLUETOOTH = SDL_HID_API_BUS_BLUETOOTH,
    I2C = SDL_HID_API_BUS_I2C,
    SPI = SDL_HID_API_BUS_SPI,
};

namespace hid::detail {

// hidapi renvoie des chaînes wchar_t natives (UTF-16 sous Windows, UTF-32
// ailleurs) — on les convertit systématiquement en UTF-8 (`String`) pour ne
// jamais exposer wchar_t* dans l'API publique.
[[nodiscard]] inline String WideToString(const wchar_t *w) {
    if (!w)
        return String();
    if constexpr (sizeof(wchar_t) == 2) {
        return String(unicode::FromUtf16(std::u16string_view(reinterpret_cast<const char16_t *>(w))));
    } else {
        return String(unicode::FromUtf32(std::u32string_view(reinterpret_cast<const char32_t *>(w))));
    }
}

[[nodiscard]] inline std::wstring StringToWide(const String &s) {
    if constexpr (sizeof(wchar_t) == 2) {
        auto u16 = unicode::ToUtf16(s.View());
        return std::wstring(u16.begin(), u16.end());
    } else {
        auto u32 = unicode::ToUtf32(s.View());
        return std::wstring(u32.begin(), u32.end());
    }
}

} // namespace hid::detail

// ============================================================================
// HidDeviceInfo — wrap de SDL_hid_device_info (une entrée ; la liste chaînée
// C est aplatie en std::vector par hid::Enumerate())
// ============================================================================

struct HidDeviceInfo {
    String path;
    uint16_t vendorId = 0, productId = 0;
    String serialNumber;
    uint16_t releaseNumber = 0;
    String manufacturer, product;
    uint16_t usagePage = 0, usage = 0;
    int interfaceNumber = -1;
    int interfaceClass = 0, interfaceSubclass = 0, interfaceProtocol = 0;
    HidBusType busType = HidBusType::UNKNOWN;

    HidDeviceInfo() = default;
    explicit HidDeviceInfo(const SDL_hid_device_info &d)
        : path(d.path ? d.path : ""), vendorId(d.vendor_id), productId(d.product_id),
          serialNumber(hid::detail::WideToString(d.serial_number)), releaseNumber(d.release_number),
          manufacturer(hid::detail::WideToString(d.manufacturer_string)),
          product(hid::detail::WideToString(d.product_string)), usagePage(d.usage_page), usage(d.usage),
          interfaceNumber(d.interface_number), interfaceClass(d.interface_class),
          interfaceSubclass(d.interface_subclass), interfaceProtocol(d.interface_protocol),
          busType(HidBusType(d.bus_type)) {}
};

// ============================================================================
// HidContext — RAII SDL_hid_init / SDL_hid_exit
// ============================================================================

class HidContext {
    bool owns = false;

public:
    HidContext() = default;
    ~HidContext() {
        if (owns)
            SDL_hid_exit();
    }

    HidContext(const HidContext &) = delete;
    HidContext &operator=(const HidContext &) = delete;
    HidContext(HidContext &&o) noexcept : owns(o.owns) { o.owns = false; }
    HidContext &operator=(HidContext &&o) noexcept {
        if (this != &o) {
            if (owns)
                SDL_hid_exit();
            owns = o.owns;
            o.owns = false;
        }
        return *this;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return owns; }

    [[nodiscard]] static Result<HidContext, Error> Create() {
        HidContext ctx;
        ctx.owns = (SDL_hid_init() == 0);
        if (!ctx)
            return Err(GetError());
        return Ok(std::move(ctx));
    }
};

namespace hid {

// Incrémenté à chaque changement de la liste de périphériques HID connectés
// (permet d'éviter de ré-énumérer à chaque frame).
[[nodiscard]] inline uint32_t DeviceChangeCount() noexcept { return SDL_hid_device_change_count(); }

// vendorId/productId à 0 signifie "tous" (filtre optionnel).
[[nodiscard]] inline std::vector<HidDeviceInfo> Enumerate(uint16_t vendorId = 0, uint16_t productId = 0) {
    SDL_hid_device_info *list = SDL_hid_enumerate(vendorId, productId);
    std::vector<HidDeviceInfo> out;
    for (auto *d = list; d; d = d->next)
        out.emplace_back(*d);
    if (list)
        SDL_hid_free_enumeration(list);
    return out;
}

// Démarre/arrête un scan Bluetooth LE actif (plateformes qui le supportent).
inline void BleScan(bool active) noexcept { SDL_hid_ble_scan(active); }

} // namespace hid

// ============================================================================
// HidDevice — RAII SDL_hid_device
// ============================================================================

class HidDevice : public Wrapper<SDL_hid_device, SDL_hid_close> {
public:
    using Wrapper::Wrapper;

    // `serial` vide == n'importe quel numéro de série pour ce vendor/product.
    [[nodiscard]] static Result<HidDevice, StringView> Open(uint16_t vendorId, uint16_t productId,
                                                            const String &serial = "") {
        std::wstring wserial = serial.IsEmpty() ? std::wstring() : hid::detail::StringToWide(serial);
        auto *d = SDL_hid_open(vendorId, productId, serial.IsEmpty() ? nullptr : wserial.c_str());
        if (!d)
            return Err(GetError());
        return Ok(HidDevice(d));
    }

    [[nodiscard]] static Result<HidDevice, StringView> OpenPath(const String &path) {
        auto *d = SDL_hid_open_path(path.c_str());
        if (!d)
            return Err(GetError());
        return Ok(HidDevice(d));
    }

    [[nodiscard]] SDL_PropertiesID Properties() const noexcept {
        return m_handle ? SDL_hid_get_properties(m_handle) : 0;
    }

    // ── I/O ──────────────────────────────────────────────────────────────────

    int Write(const void *data, size_t len) noexcept {
        return m_handle ? SDL_hid_write(m_handle, static_cast<const unsigned char *>(data), len) : -1;
    }
    int Write(std::span<const uint8_t> data) noexcept { return Write(data.data(), data.size()); }

    int ReadTimeout(void *data, size_t len, int milliseconds) noexcept {
        return m_handle ? SDL_hid_read_timeout(m_handle, static_cast<unsigned char *>(data), len, milliseconds) : -1;
    }
    int ReadTimeout(std::span<uint8_t> data, int milliseconds) noexcept {
        return ReadTimeout(data.data(), data.size(), milliseconds);
    }

    int Read(void *data, size_t len) noexcept {
        return m_handle ? SDL_hid_read(m_handle, static_cast<unsigned char *>(data), len) : -1;
    }
    int Read(std::span<uint8_t> data) noexcept { return Read(data.data(), data.size()); }

    bool SetNonBlocking(bool nonblock) noexcept {
        return m_handle && SDL_hid_set_nonblocking(m_handle, nonblock ? 1 : 0) == 0;
    }

    int SendFeatureReport(std::span<const uint8_t> data) noexcept {
        return m_handle ? SDL_hid_send_feature_report(m_handle, data.data(), data.size()) : -1;
    }
    int GetFeatureReport(std::span<uint8_t> data) noexcept {
        return m_handle ? SDL_hid_get_feature_report(m_handle, data.data(), data.size()) : -1;
    }
    int GetInputReport(std::span<uint8_t> data) noexcept {
        return m_handle ? SDL_hid_get_input_report(m_handle, data.data(), data.size()) : -1;
    }

    // ── Chaînes descriptives (converties en UTF-8) ───────────────────────────

    [[nodiscard]] Option<String> ManufacturerString() const {
        wchar_t buf[256]{};
        if (!m_handle || SDL_hid_get_manufacturer_string(m_handle, buf, 256) != 0)
            return NONE;
        return Some(hid::detail::WideToString(buf));
    }
    [[nodiscard]] Option<String> ProductString() const {
        wchar_t buf[256]{};
        if (!m_handle || SDL_hid_get_product_string(m_handle, buf, 256) != 0)
            return NONE;
        return Some(hid::detail::WideToString(buf));
    }
    [[nodiscard]] Option<String> SerialNumberString() const {
        wchar_t buf[256]{};
        if (!m_handle || SDL_hid_get_serial_number_string(m_handle, buf, 256) != 0)
            return NONE;
        return Some(hid::detail::WideToString(buf));
    }
    [[nodiscard]] Option<String> IndexedString(int index) const {
        wchar_t buf[256]{};
        if (!m_handle || SDL_hid_get_indexed_string(m_handle, index, buf, 256) != 0)
            return NONE;
        return Some(hid::detail::WideToString(buf));
    }

    [[nodiscard]] Option<HidDeviceInfo> DeviceInfo() const {
        if (!m_handle)
            return NONE;
        auto *info = SDL_hid_get_device_info(m_handle);
        if (!info)
            return NONE;
        return Some(HidDeviceInfo(*info));
    }

    [[nodiscard]] Result<std::vector<uint8_t>, StringView> ReportDescriptor(size_t maxSize = 4096) const {
        if (!m_handle)
            return Err(StringView("device invalide"));
        std::vector<uint8_t> buf(maxSize);
        int n = SDL_hid_get_report_descriptor(m_handle, buf.data(), buf.size());
        if (n < 0)
            return Err(GetError());
        buf.resize(size_t(n));
        return Ok(std::move(buf));
    }
};

} // namespace sdl3
