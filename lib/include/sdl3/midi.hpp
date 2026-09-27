#pragma once
/**
 * MIDI — messages, (dé)sérialisation et parseur de flux, indépendants du
 * transport.
 *
 * Ce module ne dépend PAS de hidapi.hpp : la grande majorité des interfaces
 * MIDI USB ("USB-MIDI class") ne s'énumèrent pas comme des périphériques
 * HID — elles passent par l'API MIDI de l'OS (ALSA séquenceur/rawmidi sous
 * Linux, CoreMIDI sous macOS, WinMM/WinRT MIDI sous Windows), qu'SDL3 ne
 * fournit pas du tout. Seuls quelques contrôleurs "control surface"
 * propriétaires exposent des messages MIDI-like via HID.
 *
 * Utilisation typique : peu importe d'où viennent les octets (un
 * `sdl3::HidDevice::read()`, un `sdl3::IOStream` branché sur un
 * /dev/snd/midiCx ou un port série DIN-MIDI via adaptateur, un buffer réseau
 * SysEx...), on les passe à `MidiParser::Feed()`.
 *
 * @code{.cpp}
 * sdl3::MidiParser parser;
 * std::array<uint8_t, 64> buf;
 * int n = someTransport.read(buf); // HidDevice, IOStream, etc.
 * parser.Feed(std::span(buf.data(), n), [](const sdl3::MidiMessage& msg) {
 *     if (msg.type == sdl3::MidiMessageType::NOTE_ON) { ... }
 * });
 * @endcode
 */
#include <algorithm>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "../core/core.hpp"

namespace sdl3 {

// ============================================================================
// MidiMessageType — statuts MIDI (nibble haut pour les messages "channel
// voice", octet complet pour les messages système)
// ============================================================================

enum class MidiMessageType : uint8_t {
    // messages
    NOTE_OFF = 0x80,
    NOTE_ON = 0x90,
    POLY_AFTERTOUCH = 0xA0,
    CONTROL_CHANGE = 0xB0,
    PROGRAM_CHANGE = 0xC0,
    CHANNEL_AFTERTOUCH = 0xD0,
    PITCH_BEND = 0xE0,

    // system exclusive
    SYS_EX = 0xF0,

    // system common - never in midi files
    MTC_QUARTER_FRAME = 0xF1,
    SONG_POSITION = 0xF2,
    SONG_SELECT = 0xF3,
    TUNE_REQUEST = 0xF6,
    EOX = 0xF7,

    // system real-time - never in midi files
    TIMING_CLOCK = 0xF8,
    TICK = 0xF9,
    Start = 0xFA,
    CONTINUE = 0xFB,
    STOP = 0xFC,
    ACTIVE_SENSING = 0xFE,
    SYSTEM_RESET = 0xFF,

    // meta event - for midi files only
    META_EVENT = 0xFF
};

enum class MidiMetaEventTypes : uint8_t {
    META_INVALID = 0x00,
    COPYRIGHT = 0x02,
    TRACK_NAME = 0x03,
    INST_NAME = 0x04,
    LYRIC = 0x05,
    MARKER = 0x06,
    CUE_POINT = 0x07,
    PORT_NUMBER = 0x21,
    EOT = 0x2f,
    SET_TEMPO = 0x51,
    SMPTE_OFFSET = 0x54,
    TIME_SIGNATURE = 0x58,
    KEY_SIGNATURE = 0x59,
    SEQUENCER_EVENT = 0x7f,
    META_CUSTOM = 0x80,
    NOTE_PANNING = 0x81
};

enum class MidiStandardControllers : uint8_t {
    BANK_SELECT = 0,
    MODULATION_WHEEL = 1,
    BREATH_CONTROLLER = 2,
    FOOT_CONTROLLER = 4,
    PORTAMENTO_TIME = 5,
    DATA_ENTRY = 6,
    MAIN_VOLUME = 7,
    BALANCE = 8,
    PAN = 10,
    EFFECT_CONTROL1 = 12,
    EFFECT_CONTROL2 = 13,
    SUSTAIN = 64,
    PORTAMENTO = 65,
    SOSTENUTO = 66,
    SOFT_PEDAL = 67,
    LEGATO_FOOTSWITCH = 68,
    REGISTERED_PARAMETER_NUMBER_LSB = 100,
    REGISTERED_PARAMETER_NUMBER_MSB = 101,
    // Channel Mode Messages are controllers too...
    ALL_SOUND_OFF = 120,
    RESET_ALL_CONTROLLERS = 121,
    LOCAL_CONTROL = 122,
    ALL_NOTES_OFF = 123,
    OMNI_ON = 124,
    OMNI_OFF = 125,
    MONO_ON = 126,
    POLY_ON = 127,
};

enum class MidiControllerRegisteredParameterNumbers : uint16_t {
    PITCH_BEND_SENSITIVITY_RPN = 0x0000,
    CHANNEL_FINE_TUNING_RPN = 0x0001,
    CHANNEL_COARSE_TUNING_RPN = 0x0002,
    TUNING_PROGRAM_CHANGE_RPN = 0x0003,
    TUNING_BANK_SELECT_RPN = 0x0004,
    MODULATION_DEPTH_RANGE_RPN = 0x0005,
    NULL_FUNCTION_NUMBER_RPN = 0x7F7F
};

// ============================================================================
// Constantes protocolaires (cf. Midi.h de LMMS)
// ============================================================================

namespace midi {
inline constexpr int CHANNEL_COUNT = 16;
inline constexpr int CONTROLLER_COUNT = 128;
inline constexpr int PROGRAM_COUNT = 128;
inline constexpr int MAX_VELOCITY = 127;
inline constexpr int DEFAULT_VELOCITY = MAX_VELOCITY / 2;
inline constexpr int MAX_CONTROLLER_VALUE = 127;
inline constexpr int MAX_KEY = 127;

inline constexpr int MAX_PANNING = 127;
inline constexpr int MIN_PANNING = -128;

inline constexpr int MIN_PITCH_BEND = 0;
inline constexpr int MAX_PITCH_BEND = 16383;
inline constexpr int CENTER_PITCH_BEND = 8192;

// Convertit une valeur de contrôleur MIDI (0-127) en [0, 1].
[[nodiscard]] constexpr float CcToNormalized(uint8_t value) noexcept {
    return float(value & 0x7F) / float(MAX_CONTROLLER_VALUE);
}
// Convertit une valeur de contrôleur MIDI (0-127) en [-1, 1] (ex: pan, balance).
[[nodiscard]] constexpr float CcToBipolar(uint8_t value) noexcept { return CcToNormalized(value) * 2.f - 1.f; }
} // namespace midi

namespace midi::detail {

// Nombre d'octets de données attendus après un octet de statut donné
// (0 pour les messages système sans donnée et les Real-Time).
[[nodiscard]] constexpr int ExpectedDataBytes(uint8_t status) noexcept {
    switch (status & 0xF0) {
    case 0x80:
    case 0x90:
    case 0xA0:
    case 0xB0:
    case 0xE0:
        return 2;
    case 0xC0:
    case 0xD0:
        return 1;
    default:
        break;
    }
    switch (status) {
    case 0xF1:
        return 1; // MTC quarter frame
    case 0xF2:
        return 2; // song position pointer
    case 0xF3:
        return 1; // song select
    default:
        return 0; // tune request, sysex-end, real-time...
    }
}

[[nodiscard]] constexpr bool IsRealTime(uint8_t status) noexcept { return status >= 0xF8; }
[[nodiscard]] constexpr bool IsSystemCommon(uint8_t status) noexcept { return status >= 0xF1 && status <= 0xF6; }

} // namespace midi::detail

// ============================================================================
// MidiMessage
// ============================================================================

struct MidiMessage {
    MidiMessageType type = MidiMessageType::NOTE_OFF;
    uint8_t channel = 0;            // 0-15 ; valide seulement si isChannelVoice()
    uint8_t data1 = 0, data2 = 0;   // sens dépend de `type` (cf. constructeurs nommés)
    std::vector<uint8_t> sysExData; // valide seulement si type == SysEx (sans le F0/F7)

    // Valide seulement quand `type == MidiMessageType::META_EVENT` — c'est-à-dire
    // uniquement lors de la lecture d'un Standard MIDI File (SMF), jamais sur
    // un flux MIDI temps réel. Note : `MetaEvent` et `SystemReset` partagent
    // la même valeur d'octet (0xFF) — c'est le protocole MIDI lui-même qui
    // réutilise cet octet avec un sens différent selon le contexte (flux live
    // vs fichier .mid) ; ce module ne fait que du flux live, `metaEvent` reste
    // donc à `MetaInvalid` en pratique tant qu'aucun lecteur SMF n'existe ici.
    MidiMetaEventTypes metaEvent = MidiMetaEventTypes::META_INVALID;

    [[nodiscard]] bool IsChannelVoice() const noexcept { return uint8_t(type) < 0xF0; }
    [[nodiscard]] bool IsRealTime() const noexcept { return midi::detail::IsRealTime(uint8_t(type)); }

    // ── Constructeurs nommés — messages "channel voice" ──────────────────────

    [[nodiscard]] static MidiMessage NoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    [[nodiscard]] static MidiMessage NoteOff(uint8_t channel, uint8_t note, uint8_t velocity = 0);
    [[nodiscard]] static MidiMessage PolyAftertouch(uint8_t channel, uint8_t note, uint8_t pressure);
    [[nodiscard]] static MidiMessage ControlChange(uint8_t channel, uint8_t controller, uint8_t value);
    [[nodiscard]] static MidiMessage ProgramChange(uint8_t channel, uint8_t program);
    [[nodiscard]] static MidiMessage ChannelAftertouch(uint8_t channel, uint8_t pressure);
    // `value14` dans [0, 16383], 8192 = centre.
    [[nodiscard]] static MidiMessage PitchBend(uint8_t channel, int value14);
    [[nodiscard]] float PitchBendNormalized() const noexcept;

    // ── Constructeurs nommés — messages système ───────────────────────────────

    [[nodiscard]] static MidiMessage SysEx(std::vector<uint8_t> data);
    [[nodiscard]] static MidiMessage SongPosition(uint16_t beats);
    [[nodiscard]] static MidiMessage SongSelect(uint8_t song);
    [[nodiscard]] static MidiMessage SystemRealTime(MidiMessageType t);

    // Reconstruit un message à partir d'un octet de statut brut + données
    // (utilisé par MidiParser — public pour permettre un parsing manuel).
    [[nodiscard]] static MidiMessage FromStatus(uint8_t status, uint8_t d1, uint8_t d2);

    // Sérialise en octets bruts prêts à être envoyés sur le fil.
    [[nodiscard]] std::vector<uint8_t> Bytes() const;

private:
    [[nodiscard]] static MidiMessage ChannelVoice(MidiMessageType t, uint8_t channel, uint8_t d1, uint8_t d2);
};

// ============================================================================
// MidiParser — décodeur de flux avec running status et accumulation SysEx
// ============================================================================

class MidiParser {
public:
    using Callback = std::function<void(const MidiMessage &)>;

    void Feed(std::span<const uint8_t> bytes, const Callback &onMessage);

    // Réinitialise l'état (statut courant, SysEx en cours d'accumulation).
    void Reset() noexcept;

private:
    uint8_t status = 0;         // dernier statut "channel voice" (running status)
    std::vector<uint8_t> data;  // octets de données accumulés pour status
    std::vector<uint8_t> sysEx; // accumulation SysEx (sans F0/F7)
    bool inSysEx = false;

    void FeedByte(uint8_t b, const Callback &onMessage);
};

// ============================================================================
// MidiPortFilter — remappage de voie / vélocité fixe, inspiré de la logique
// de routage de MidiPort (LMMS), sans toute la machinerie Qt/séquenceur —
// juste une transformation valeur-à-valeur sur des MidiMessage, réutilisable
// pour router un contrôleur d'entrée vers une voie de sortie fixe, forcer
// une note/vélocité de sortie (pad "déclencheur" par ex.), ou ne filtrer
// qu'une voie précise en entrée.
// ============================================================================

class MidiPortFilter {
public:
    // 0 = "toutes les voies" (passthrough) ; 1-16 = force cette voie précise.
    int inputChannel = 0;
    int outputChannel = 0;

    Option<uint8_t> fixedInputVelocity = NONE;
    Option<uint8_t> fixedOutputVelocity = NONE;
    Option<uint8_t> fixedOutputNote = NONE;

    // Facteur d'échelle appliqué à la vélocité de sortie (0..127 -> 0..127),
    // par ex. pour compenser un contrôleur qui envoie toujours des vélocités
    // faibles/fortes. 64 = neutre (LMMS: baseVelocity).
    uint8_t baseVelocity = 64;

    // Applique le filtre d'entrée. Retourne NONE si le message ne correspond
    // pas au canal configuré (ignoré) ; sinon le message éventuellement modifié
    // (vélocité fixe). Les messages non "channel voice" passent toujours.
    [[nodiscard]] Option<MidiMessage> FilterIncoming(const MidiMessage &ev) const;

    // Prépare un message pour l'envoi : force le canal/la note/la vélocité de
    // sortie si configuré, et applique l'échelle de vélocité pour les Note On.
    [[nodiscard]] MidiMessage PrepareOutgoing(MidiMessage ev) const;
};

} // namespace sdl3
