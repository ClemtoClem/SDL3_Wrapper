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
[[nodiscard]] String WideToString(const wchar_t *w);

[[nodiscard]] std::wstring StringToWide(const String &s);

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
    ~HidContext();

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

    [[nodiscard]] static Result<HidContext, Error> Create();
};

namespace hid {

// Incrémenté à chaque changement de la liste de périphériques HID connectés
// (permet d'éviter de ré-énumérer à chaque frame).
[[nodiscard]] inline uint32_t DeviceChangeCount() noexcept { return SDL_hid_device_change_count(); }

// vendorId/productId à 0 signifie "tous" (filtre optionnel).
[[nodiscard]] std::vector<HidDeviceInfo> Enumerate(uint16_t vendorId = 0, uint16_t productId = 0);

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
                                                            const String &serial = "");

    [[nodiscard]] static Result<HidDevice, StringView> OpenPath(const String &path);

    [[nodiscard]] SDL_PropertiesID Properties() const noexcept;

    // ── I/O ──────────────────────────────────────────────────────────────────

    int Write(const void *data, size_t len) noexcept;
    int Write(std::span<const uint8_t> data) noexcept { return Write(data.data(), data.size()); }

    int ReadTimeout(void *data, size_t len, int milliseconds) noexcept;
    int ReadTimeout(std::span<uint8_t> data, int milliseconds) noexcept;

    int Read(void *data, size_t len) noexcept;
    int Read(std::span<uint8_t> data) noexcept { return Read(data.data(), data.size()); }

    bool SetNonBlocking(bool nonblock) noexcept;

    int SendFeatureReport(std::span<const uint8_t> data) noexcept;
    int GetFeatureReport(std::span<uint8_t> data) noexcept;
    int GetInputReport(std::span<uint8_t> data) noexcept;

    // ── Chaînes descriptives (converties en UTF-8) ───────────────────────────

    [[nodiscard]] Option<String> ManufacturerString() const;
    [[nodiscard]] Option<String> ProductString() const;
    [[nodiscard]] Option<String> SerialNumberString() const;
    [[nodiscard]] Option<String> IndexedString(int index) const;

    [[nodiscard]] Option<HidDeviceInfo> DeviceInfo() const;

    [[nodiscard]] Result<std::vector<uint8_t>, StringView> ReportDescriptor(size_t maxSize = 4096) const;
};

} // namespace sdl3
