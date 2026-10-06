// Définitions de sdl3/midi.hpp
// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include "sdl3/sdl3.hpp"
#include "sdl3/midi.hpp"

namespace sdl3 {

// ── MidiMessage ──────────────────────────────────────────────────────────────

MidiMessage MidiMessage::NoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    return ChannelVoice(MidiMessageType::NOTE_ON, channel, note & 0x7F, velocity & 0x7F);
}

MidiMessage MidiMessage::NoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    return ChannelVoice(MidiMessageType::NOTE_OFF, channel, note & 0x7F, velocity & 0x7F);
}

MidiMessage MidiMessage::PolyAftertouch(uint8_t channel, uint8_t note, uint8_t pressure) {
    return ChannelVoice(MidiMessageType::POLY_AFTERTOUCH, channel, note & 0x7F, pressure & 0x7F);
}

MidiMessage MidiMessage::ControlChange(uint8_t channel, uint8_t controller, uint8_t value) {
    return ChannelVoice(MidiMessageType::CONTROL_CHANGE, channel, controller & 0x7F, value & 0x7F);
}

MidiMessage MidiMessage::ProgramChange(uint8_t channel, uint8_t program) {
    return ChannelVoice(MidiMessageType::PROGRAM_CHANGE, channel, program & 0x7F, 0);
}

MidiMessage MidiMessage::ChannelAftertouch(uint8_t channel, uint8_t pressure) {
    return ChannelVoice(MidiMessageType::CHANNEL_AFTERTOUCH, channel, pressure & 0x7F, 0);
}

MidiMessage MidiMessage::PitchBend(uint8_t channel, int value14) {
    return ChannelVoice(MidiMessageType::PITCH_BEND, channel, uint8_t(value14 & 0x7F),
                        uint8_t((value14 >> 7) & 0x7F));
}

float MidiMessage::PitchBendNormalized() const noexcept {
    int v = (int(data2) << 7) | int(data1);
    return (float(v) - 8192.f) / 8192.f; // -1..~1
}

MidiMessage MidiMessage::SysEx(std::vector<uint8_t> data) {
    MidiMessage m;
    m.type = MidiMessageType::SYS_EX;
    m.sysExData = std::move(data);
    return m;
}

MidiMessage MidiMessage::SongPosition(uint16_t beats) {
    MidiMessage m;
    m.type = MidiMessageType::SONG_POSITION;
    m.data1 = uint8_t(beats & 0x7F);
    m.data2 = uint8_t((beats >> 7) & 0x7F);
    return m;
}

MidiMessage MidiMessage::SongSelect(uint8_t song) {
    MidiMessage m;
    m.type = MidiMessageType::SONG_SELECT;
    m.data1 = song & 0x7F;
    return m;
}

MidiMessage MidiMessage::SystemRealTime(MidiMessageType t) {
    MidiMessage m;
    m.type = t;
    return m; // TimingClock/Start/Continue/Stop/ActiveSensing/SystemReset/TuneRequest
}

MidiMessage MidiMessage::FromStatus(uint8_t status, uint8_t d1, uint8_t d2) {
    MidiMessage m;
    if (status < 0xF0) {
        m.type = MidiMessageType(status & 0xF0);
        m.channel = status & 0x0F;
    } else {
        m.type = MidiMessageType(status);
    }
    m.data1 = d1;
    m.data2 = d2;
    return m;
}

std::vector<uint8_t> MidiMessage::Bytes() const {
    if (type == MidiMessageType::SYS_EX) {
        std::vector<uint8_t> out;
        out.reserve(sysExData.size() + 2);
        out.push_back(0xF0);
        out.insert(out.end(), sysExData.begin(), sysExData.end());
        out.push_back(0xF7);
        return out;
    }

    bool noteOnAsOff = (type == MidiMessageType::NOTE_ON && data2 == 0);
    uint8_t effectiveType = noteOnAsOff ? uint8_t(MidiMessageType::NOTE_OFF) : uint8_t(type);
    uint8_t status = IsChannelVoice() ? uint8_t(effectiveType | (channel & 0x0F)) : effectiveType;

    std::vector<uint8_t> out{status};
    int n = midi::detail::ExpectedDataBytes(status);
    if (n >= 1)
        out.push_back(data1);
    if (n >= 2)
        out.push_back(data2);
    return out;
}

MidiMessage MidiMessage::ChannelVoice(MidiMessageType t, uint8_t channel, uint8_t d1, uint8_t d2) {
    MidiMessage m;
    m.type = t;
    m.channel = channel & 0x0F;
    m.data1 = d1;
    m.data2 = d2;
    return m;
}

// ── MidiParser ───────────────────────────────────────────────────────────────

void MidiParser::Feed(std::span<const uint8_t> bytes, const Callback &onMessage) {
    for (uint8_t b : bytes)
        FeedByte(b, onMessage);
}

void MidiParser::Reset() noexcept {
    status = 0;
    data.clear();
    sysEx.clear();
    inSysEx = false;
}

void MidiParser::FeedByte(uint8_t b, const Callback &onMessage) {
    // Les messages Real-Time (1 octet) peuvent apparaître n'importe où,
    // y compris au milieu d'un SysEx ou d'un message multi-octets, sans
    // perturber l'état en cours.
    if (midi::detail::IsRealTime(b)) {
        onMessage(MidiMessage::SystemRealTime(MidiMessageType(b)));
        return;
    }

    if (inSysEx) {
        if (b == 0xF7) {
            onMessage(MidiMessage::SysEx(sysEx));
            sysEx.clear();
            inSysEx = false;
        } else if (b & 0x80) {
            // Statut inattendu sans F7 préalable (flux malformé / coupure) :
            // on abandonne le SysEx en cours et on retraite l'octet normalement.
            inSysEx = false;
            sysEx.clear();
            FeedByte(b, onMessage);
        } else {
            sysEx.push_back(b);
        }
        return;
    }

    if (b == 0xF0) {
        inSysEx = true;
        sysEx.clear();
        return;
    }

    if (b & 0x80) {
        status = b;
        data.clear();
        if (midi::detail::ExpectedDataBytes(b) == 0) {
            onMessage(MidiMessage::FromStatus(status, 0, 0));
            if (midi::detail::IsSystemCommon(status))
                status = 0; // pas de running status pour System Common
        }
        return;
    }

    // Octet de donnée
    if (status == 0)
        return; // octet errant avant tout statut — ignoré
    data.push_back(b);
    int need = midi::detail::ExpectedDataBytes(status);
    if (int(data.size()) >= need) {
        onMessage(MidiMessage::FromStatus(status, data.size() > 0 ? data[0] : 0,
                                          data.size() > 1 ? data[1] : 0));
        data.clear();
        if (midi::detail::IsSystemCommon(status))
            status = 0;
        // Les messages "channel voice" conservent status pour le running status.
    }
}

// ── MidiPortFilter ───────────────────────────────────────────────────────────

Option<MidiMessage> MidiPortFilter::FilterIncoming(const MidiMessage &ev) const {
    if (!ev.IsChannelVoice())
        return Some(ev);
    if (inputChannel != 0 && ev.channel != uint8_t(inputChannel - 1))
        return NONE;

    MidiMessage out = ev;
    bool isNote = (ev.type == MidiMessageType::NOTE_ON || ev.type == MidiMessageType::NOTE_OFF);
    if (isNote && fixedInputVelocity.IsSome())
        out.data2 = fixedInputVelocity.Unwrap();
    return Some(out);
}

MidiMessage MidiPortFilter::PrepareOutgoing(MidiMessage ev) const {
    if (ev.IsChannelVoice() && outputChannel != 0)
        ev.channel = uint8_t(outputChannel - 1);

    if (ev.type == MidiMessageType::NOTE_ON) {
        if (fixedOutputNote.IsSome())
            ev.data1 = fixedOutputNote.Unwrap();
        if (fixedOutputVelocity.IsSome())
            ev.data2 = fixedOutputVelocity.Unwrap();
        else if (baseVelocity != 64)
            ev.data2 = uint8_t(sdl3::Min(127, int(ev.data2) * int(baseVelocity) / 64));
    }
    return ev;
}

} // namespace sdl3
