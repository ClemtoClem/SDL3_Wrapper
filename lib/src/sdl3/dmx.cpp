// Définitions de sdl3/dmx.hpp
// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include "sdl3/sdl3.hpp"
#include "sdl3/dmx.hpp"

namespace sdl3 {

// ── DmxUniverse ──────────────────────────────────────────────────────────────

void DmxUniverse::Set(int channel, uint8_t value) noexcept {
    if (channel >= 1 && channel <= DMX_UNIVERSE_SIZE)
        channels[size_t(channel - 1)] = value;
}

uint8_t DmxUniverse::Get(int channel) const noexcept {
    return (channel >= 1 && channel <= DMX_UNIVERSE_SIZE) ? channels[size_t(channel - 1)] : 0;
}

void DmxUniverse::SetRange(int startChannel, std::span<const uint8_t> values) noexcept {
    for (size_t i = 0; i < values.size(); ++i)
        Set(startChannel + int(i), values[i]);
}

namespace dmx {

int SendToHid(HidDevice &device, const DmxUniverse &universe, uint8_t reportId) {
    std::vector<uint8_t> report;
    report.reserve(1 + size_t(DmxUniverse::GetSize()));
    report.push_back(reportId);
    auto span = universe.GetData();
    report.insert(report.end(), span.begin(), span.end());
    return device.Write(report);
}

} // namespace dmx

} // namespace sdl3
