// Smoke test : midi.hpp (MidiMessage/MidiParser — transport-agnostic, aucun
// matériel requis) et dmx.hpp (DmxUniverse — le transport HID n'est testé
// qu'au niveau compilation, cf. commentaire plus bas).
#include "sdl3/sdl3.hpp"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
	using namespace sdl3;

	// --- MidiMessage: round-trip encode -> parse ---
	std::vector<MidiMessage> received;
	MidiParser parser;
	auto onMsg = [&](const MidiMessage &m) { received.push_back(m); };

	auto noteOn = MidiMessage::NoteOn(0, 60, 100);
	auto bytes = noteOn.Bytes();
	std::cout << "noteOn bytes: " << bytes.size() << " (expect 3)\n";
	parser.Feed(bytes, onMsg);

	// Running status: un second Note On sur le même canal peut omettre le statut.
	parser.Feed(std::array<uint8_t, 2>{64, 90}, onMsg); // note=64 vel=90, statut omis

	// Control Change interrompt le running status "Note On" (statut différent explicite).
	auto cc = MidiMessage::ControlChange(0, 7, 127);
	parser.Feed(cc.Bytes(), onMsg);

	// Program Change (1 seul octet de donnée).
	auto pc = MidiMessage::ProgramChange(1, 42);
	parser.Feed(pc.Bytes(), onMsg);

	// Pitch bend + normalisation.
	auto pb = MidiMessage::PitchBend(0, 8192); // centre
	parser.Feed(pb.Bytes(), onMsg);

	// Real-Time (Timing Clock) intercalé au milieu d'un message multi-octets
	// (ne doit pas casser le message en cours).
	auto pb2 = MidiMessage::PitchBend(2, 0);
	auto pb2Bytes = pb2.Bytes();
	std::vector<uint8_t> interleaved = {pb2Bytes[0], 0xF8 /* clock */, pb2Bytes[1], pb2Bytes[2]};
	parser.Feed(interleaved, onMsg);

	// SysEx.
	auto sx = MidiMessage::SysEx({0x7E, 0x00, 0x06, 0x01});
	parser.Feed(sx.Bytes(), onMsg);

	std::cout << "received " << received.size()
			  << " messages (expect 7: notOn, running-status noteOn, "
				 "cc, pc, pitchbend, clock, pitchbend2, sysex -> actually 8)\n";
	for (auto &m : received) {
		std::cout << "  type=0x" << std::hex << int(m.type) << std::dec << " ch=" << int(m.channel)
				  << " d1=" << int(m.data1) << " d2=" << int(m.data2) << " sysexLen=" << m.sysExData.size() << "\n";
	}

	assert(received.size() == 8);
	assert(received[0].type == MidiMessageType::NOTE_ON && received[0].data1 == 60 && received[0].data2 == 100);
	assert(received[1].type == MidiMessageType::NOTE_ON && received[1].data1 == 64 && received[1].data2 == 90);
	assert(received[2].type == MidiMessageType::CONTROL_CHANGE && received[2].data1 == 7);
	assert(received[3].type == MidiMessageType::PROGRAM_CHANGE && received[3].data1 == 42);
	assert(received[4].type == MidiMessageType::PITCH_BEND);
	assert(std::abs(received[4].PitchBendNormalized()) < 0.01f);
	assert(received[5].type == MidiMessageType::TIMING_CLOCK);
	assert(received[6].type == MidiMessageType::PITCH_BEND && received[6].channel == 2);
	assert(received[7].type == MidiMessageType::SYS_EX && received[7].sysExData.size() == 4);
	std::cout << "midi assertions passed\n";

	// --- Convention LV2 : Note On vélocité 0 sérialisé comme un vrai Note Off ---
	auto noteOnZero = MidiMessage::NoteOn(3, 72, 0).Bytes();
	std::cout << "noteOn(vel=0) status byte=0x" << std::hex << int(noteOnZero[0]) << std::dec << "\n";
	assert((noteOnZero[0] & 0xF0) == uint8_t(MidiMessageType::NOTE_OFF));
	assert((noteOnZero[0] & 0x0F) == 3);

	// --- MidiPortFilter ---
	MidiPortFilter filt;
	filt.inputChannel = 2; // ne garde que le canal MIDI "2" (0-indexé: channel==1)
	auto matchOpt = filt.FilterIncoming(MidiMessage::NoteOn(1, 60, 100));
	auto noMatchOpt = filt.FilterIncoming(MidiMessage::NoteOn(0, 60, 100));
	assert(matchOpt.IsSome());
	assert(noMatchOpt.IsNone());

	filt.fixedInputVelocity = Some(uint8_t(99));
	auto fixedVel = filt.FilterIncoming(MidiMessage::NoteOn(1, 60, 5));
	assert(fixedVel.IsSome() && fixedVel.Unwrap().data2 == 99);

	MidiPortFilter outFilt;
	outFilt.outputChannel = 5; // force la voie 5 (0-indexé: channel==4) en sortie
	outFilt.fixedOutputNote = Some(uint8_t(36));
	auto routed = outFilt.PrepareOutgoing(MidiMessage::NoteOn(0, 60, 100));
	std::cout << "routed channel=" << int(routed.channel) << " note=" << int(routed.data1) << "\n";
	assert(routed.channel == 4 && routed.data1 == 36);
	std::cout << "midi port filter assertions passed\n";

	// --- Constantes ---
	std::cout << "midi::MAX_KEY=" << midi::MAX_KEY << " midi::CHANNEL_COUNT=" << midi::CHANNEL_COUNT
			  << " ccToBipolar(64)=" << midi::CcToBipolar(64) << "\n";
	assert(midi::MAX_KEY == 127 && midi::CHANNEL_COUNT == 16);

	// --- DmxUniverse ---
	DmxUniverse universe;
	universe.Set(1, 255);
	universe.Set(512, 128);
	universe.Set(0, 99);   // hors-plage, ignoré
	universe.Set(513, 99); // hors-plage, ignoré
	std::cout << "dmx ch1=" << int(universe.Get(1)) << " ch512=" << int(universe.Get(512))
			  << " ch2=" << int(universe.Get(2)) << " size=" << universe.GetSize() << "\n";
	assert(universe.Get(1) == 255);
	assert(universe.Get(512) == 128);
	assert(universe.Get(2) == 0);
	assert(universe.GetData().size() == 512);

	universe.SetRange(10, std::array<uint8_t, 3>{1, 2, 3});
	assert(universe.Get(10) == 1 && universe.Get(11) == 2 && universe.Get(12) == 3);

	universe.FullOn();
	assert(universe.Get(256) == 255);
	universe.Blackout();
	assert(universe.Get(256) == 0);
	std::cout << "dmx assertions passed\n";

	// dmx::sendToHid() n'est pas invoqué ici (nécessite un vrai widget USB-DMX
	// HID pour être significatif) — juste vérifié à la compilation par le
	// fait que ce fichier inclut sdl3.hpp et compile.

	std::cout << "smoke test 10 done\n";
	return 0;
}
