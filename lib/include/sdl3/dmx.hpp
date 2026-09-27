#pragma once
/**
 * DMX512 — buffer d'univers (512 canaux) et envoi vers un widget USB-DMX
 * exposé en HID brut.
 *
 * Le DMX512 est un protocole série (RS-485, ~250 kbps, trame break/mark-
 * after-break) — on ne peut pas le générer en bit-bang par-dessus un GPIO
 * générique ; il faut un widget dédié qui fait l'électronique. Ces widgets se
 * répartissent en deux familles : (A) port série/COM virtuel avec un
 * protocole propriétaire (ex: Enttec DMX USB Pro — bien documenté, mais ne
 * passe pas par hidapi, plutôt par `sdl3::IOStream` sur le port série), et
 * (B) HID brut où le firmware attend directement le buffer de 512 canaux
 * dans un rapport HID (quelques widgets bon marché). `dmx::sendToHid()` ci-
 * dessous couvre le cas (B) le plus simple (un seul rapport contenant tout
 * l'univers) — le `reportId` et le découpage exact dépendent du firmware du
 * widget, à ajuster selon sa documentation.
 */
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "../core/core.hpp"
#include "hidapi.hpp"

namespace sdl3 {

inline constexpr int DMX_UNIVERSE_SIZE = 512;

// ============================================================================
// DmxUniverse — buffer de 512 canaux DMX512 (valeurs 0-255)
// ============================================================================

class DmxUniverse {
    std::array<uint8_t, DMX_UNIVERSE_SIZE> channels{};

public:
    DmxUniverse() = default;

    // Canaux DMX numérotés de 1 à 512 (convention DMX standard, pas 0-511).
    // Hors-plage : ignoré silencieusement (aucune trame DMX n'a plus de 512 canaux).
    void Set(int channel, uint8_t value) noexcept;
    [[nodiscard]] uint8_t Get(int channel) const noexcept;

    // Écrit `values` sur des canaux consécutifs à partir de `startChannel` (1-indexé).
    void SetRange(int startChannel, std::span<const uint8_t> values) noexcept;

    void Blackout() noexcept { channels.fill(0); }
    void FullOn() noexcept { channels.fill(255); }

    [[nodiscard]] std::span<const uint8_t> GetData() const noexcept { return channels; }
    [[nodiscard]] static constexpr int GetSize() noexcept { return DMX_UNIVERSE_SIZE; }
};

namespace dmx {

// Envoie l'univers complet à un widget USB-DMX en HID brut via un output
// report : [reportId, canal 1, canal 2, ..., canal 512]. Beaucoup de widgets
// HID multi-fonctions préfixent leurs rapports par un id (souvent 0 si le
// device n'a qu'un seul type de rapport) — vérifie la doc de ton matériel et
// adapte au besoin (certains découpent en plusieurs rapports plus petits).
// Retourne le nombre d'octets écrits (cf. HidDevice::write), -1 en cas d'échec.
int SendToHid(HidDevice &device, const DmxUniverse &universe, uint8_t reportId = 0);

} // namespace dmx

} // namespace sdl3
