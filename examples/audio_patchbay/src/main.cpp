/**
 * audio_patchbay — routage audio par node-graph, ré-implémenté sur les
 * modules `audio::` (lib/audio/dsp.hpp), `sdl3::` (thread.hpp, AudioStream,
 * AudioDecoder — Phases 1/3) et `ui::nodegraph.hpp` (éditeur de node-graph
 * DÉJÀ existant dans ce projet, réutilisé tel quel plutôt que répliqué à la
 * main comme dans l'original) — pas un port littéral de SDL3pp/examples/
 * audio/07_audio_patchbay.cpp. Le graphe DSP (traitement audio réel) est un
 * modèle de données SÉPARÉ de l'ECS, jamais touché par le thread de fond —
 * seule sa structure (`dsp::Graph`, protégée par `sdl3::Mutex`) est partagée
 * entre le thread UI et le thread audio, cf. les commentaires de `main()`.
 *
 * Blocs : Audio In/Out, Filter, Amp, Mixer, Delay, Oscillator, Scope,
 * Spectrum (ui::Plot au lieu d'un canvas dessiné à la main), Playlist
 * (sdl3::AudioDecoder pour le décodage multi-format, sdl3::dialog pour le
 * choix de fichiers plutôt qu'un explorateur de fichiers fait main).
 *
 *   cpp-maker run audio_patchbay
 */
#include <algorithm>
#include <cstdlib>
#include <format>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <memory>
#include <variant>

#include <audio/dsp.hpp>
#include <core/core.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>

static constexpr int WIN_W = 1440;
static constexpr int WIN_H = 920; // 860 de contenu + barres de titre et d'état

// ============================================================================
// Modèle DSP — PAS de l'ECS : le thread audio de fond ne touche jamais
// `ecs::ArchetypeRegistry` (règle ferme du plan, cf. Décision #7) — il ne lit/
// n'écrit QUE `dsp::Graph`, protégé de bout en bout par un unique
// `sdl3::Mutex` partagé avec le thread UI (topologie + paramètres écrits côté
// UI, télémétrie — Scope/Spectrum/Playlist — écrite côté DSP, les deux sous
// le même verrou : plus simple qu'un double instantané séparé, et tout aussi
// correct puisque les deux sens passent par LE MÊME verrou).
// ============================================================================
namespace dsp {

constexpr int K_SAMPLE_RATE = 44100;
constexpr int K_BUFFER_SIZE = 1024;

enum class BlockType : uint8_t { AUDIO_IN, AUDIO_OUT, FILTER, AMP, MIXER, DELAY, OSC, SCOPE, SPECTRUM, PLAYLIST };
enum class MixMode : uint8_t { ADD, MULTIPLY, AVERAGE };
enum class TriggerMode : uint8_t { FREE, RISING, FALLING };
enum class OscShape : uint8_t { SINE, SQUARE, TRIANGLE, SAWTOOTH, NOISE };

struct AudioBus {
	std::vector<float> samples = std::vector<float>(size_t(K_BUFFER_SIZE), 0.f);
	bool valid = false;
	void Clear() {
		std::fill(samples.begin(), samples.end(), 0.f);
		valid = false;
	}
};

struct AudioInState {
	sdl3::AudioStream stream;
	bool enabled = true;
};
struct AudioOutState {
	sdl3::AudioStream stream;
	bool enabled = true;
};
struct FilterState {
	audio::BiQuadKind mode = audio::BiQuadKind::LOW_PASS;
	float cutoffHz = 1000.f;
	audio::BiQuadState bq{};
};
struct AmpState {
	float gain = 1.f;
};
struct MixerState {
	MixMode mode = MixMode::ADD;
};
struct DelayState {
	std::vector<float> buf;
	float delayMs = 100.f;
	int writeIdx = 0;
};

struct OscState {
	OscShape shape = OscShape::SINE;
	float freq = 440.f;
	float amp = 0.5f;
	float phase = 0.f;
};

struct ScopeState {
	TriggerMode trig = TriggerMode::FREE;
	float trigLevel = 0.f, vScale = 1.f;
	float prevSample = 0.f;
	std::vector<float> display;
	std::vector<float> collectBuf;
	size_t collectCount = 0;
	bool armed = true, collecting = false;
};

struct SpectrumState {
	int winIdx = 1; // audio::WindowFunction
	std::vector<float> displayDb;
};
struct PlaylistState {
	std::vector<String> tracks;
	int currentTrack = -1;
	int pendingLoad = -1;
	std::vector<float> audioData;
	size_t playbackPos = 0;
	bool playing = false, autoplay = true, looping = false;
};

using NodeState = std::variant<AudioInState, AudioOutState, FilterState, AmpState, MixerState, DelayState, OscState,
								ScopeState, SpectrumState, PlaylistState>;

struct Node {
	int id = 0;
	BlockType type{};
	std::vector<std::shared_ptr<AudioBus>> inputs;  // nullptr = port non connecté
	std::vector<std::shared_ptr<AudioBus>> outputs; // possédés par CE nœud, jamais nuls
	NodeState state;
};

struct Connection {
	int fromId = 0, fromPort = 0, toId = 0, toPort = 0;
};

struct Graph {
	std::vector<Node> nodes;
	std::vector<Connection> connections;

	[[nodiscard]] Node *FindNode(int id) noexcept {
		for (auto &n : nodes)
			if (n.id == id)
				return &n;
		return nullptr;
	}

	/// Reconstruit les alias `inputs[i]` depuis `connections` — appelée par le
	/// thread UI après tout changement de topologie (résolu depuis les
	/// `GraphConnection` du node-graph visuel, cf. `resyncConnections()`).
	void RebuildBuses() {
		for (auto &n : nodes)
			for (auto &in : n.inputs)
				in = nullptr;
		for (auto &c : connections) {
			Node *from = FindNode(c.fromId);
			Node *to = FindNode(c.toId);
			if (from && to && c.fromPort < int(from->outputs.size()) && c.toPort < int(to->inputs.size()))
				to->inputs[size_t(c.toPort)] = from->outputs[size_t(c.fromPort)];
		}
	}
};

/// Traite tous les nœuds une fois, dans l'ordre topologique (DFS post-ordre
/// inversé). Contrairement à l'original porté (SDL3pp 07_audio_patchbay.cpp),
/// Oscillator/Playlist régénèrent un bloc FRAIS à CHAQUE appel plutôt que de
/// n'avancer que lorsque leur bus de sortie a été spécifiquement invalidé par
/// le nœud directement en aval : ce dernier schéma ne fonctionne que pour une
/// chaîne à un seul maillon (Osc -> AudioOut direct) — dès qu'un nœud
/// intermédiaire s'intercale (Osc -> Filter -> AudioOut), seul le bus
/// directement lu par AudioOut est invalidé, donc Osc ne régénère plus JAMAIS
/// après le premier appel (Filter refiltre indéfiniment le même bloc figé).
/// Corrigé en simplifiant : tout le graphe est régénéré à chaque appel,
/// cadencé par l'appelant à ~kBufferSize/kSampleRate secondes (cf. main()) —
/// AudioOut reste la seule garde de contre-pression via Queued().
inline void ProcessGraph(Graph &g) {
	std::unordered_map<int, std::vector<int>> adj;
	for (auto &n : g.nodes)
		adj[n.id] = {};
	for (auto &c : g.connections)
		adj[c.fromId].push_back(c.toId);

	std::unordered_set<int> visited;
	std::vector<int> order;
	std::function<void(int)> dfs = [&](int id) {
		if (visited.count(id))
			return;
		visited.insert(id);
		for (int next : adj[id])
			dfs(next);
		order.push_back(id);
	};
	for (auto &n : g.nodes)
		dfs(n.id);
	std::reverse(order.begin(), order.end());

	for (int id : order) {
		Node *node = g.FindNode(id);
		if (!node)
			continue;
		switch (node->type) {
		case BlockType::AUDIO_IN: {
			auto &st = std::get<AudioInState>(node->state);
			auto &bus = *node->outputs[0];
			if (!st.enabled || !st.stream) {
				bus.Clear();
				break;
			}
			if (st.stream.Available() >= int(bus.samples.size() * sizeof(float))) {
				st.stream.GetData(std::span<float>(bus.samples));
				bus.valid = true;
			} else
				bus.Clear();
			break;
		}
		case BlockType::AUDIO_OUT: {
			auto &st = std::get<AudioOutState>(node->state);
			auto inBus = node->inputs[0];
			if (st.enabled && st.stream && inBus && inBus->valid) {
				if (st.stream.Queued() < int(float(K_SAMPLE_RATE) * 0.1f * sizeof(float))) {
					st.stream.PutData(std::span<const float>(inBus->samples));
					inBus->valid = false;
				}
			}
			break;
		}
		case BlockType::FILTER: {
			auto &st = std::get<FilterState>(node->state);
			auto &out = *node->outputs[0];
			auto inBus = node->inputs[0];
			if (!inBus || !inBus->valid) {
				out.Clear();
				break;
			}
			audio::BiQuadSetDesign(st.bq, st.mode, float(K_SAMPLE_RATE),
									sdl3::Clamp(st.cutoffHz, 20.f, float(K_SAMPLE_RATE) * 0.49f));
			for (size_t i = 0; i < out.samples.size(); ++i)
				out.samples[i] = audio::BiQuadStep(st.bq, inBus->samples[i]);
			out.valid = true;
			break;
		}
		case BlockType::AMP: {
			auto &st = std::get<AmpState>(node->state);
			auto &out = *node->outputs[0];
			auto inBus = node->inputs[0];
			if (!inBus || !inBus->valid) {
				out.Clear();
				break;
			}
			for (size_t i = 0; i < out.samples.size(); ++i)
				out.samples[i] = inBus->samples[i] * st.gain;
			out.valid = true;
			break;
		}
		case BlockType::MIXER: {
			auto &st = std::get<MixerState>(node->state);
			auto &out = *node->outputs[0];
			std::fill(out.samples.begin(), out.samples.end(), st.mode == MixMode::MULTIPLY ? 1.f : 0.f);
			int processed = 0;
			for (auto &in : node->inputs) {
				if (!in || !in->valid)
					continue;
				++processed;
				for (size_t i = 0; i < out.samples.size(); ++i) {
					if (st.mode == MixMode::MULTIPLY)
						out.samples[i] *= in->samples[i];
					else
						out.samples[i] += in->samples[i];
				}
			}
			if (st.mode == MixMode::AVERAGE && processed > 0)
				for (float &s : out.samples)
					s /= float(processed);
			out.valid = processed > 0;
			break;
		}
		case BlockType::DELAY: {
			auto &st = std::get<DelayState>(node->state);
			auto &out = *node->outputs[0];
			auto inBus = node->inputs[0];
			if (!inBus || !inBus->valid) {
				out.Clear();
				break;
			}
			int dSz = int(st.delayMs / 1000.f * float(K_SAMPLE_RATE));
			if (dSz <= 0) {
				out.samples = inBus->samples;
				out.valid = true;
				break;
			}
			if (int(st.buf.size()) != dSz) {
				st.buf.assign(size_t(dSz), 0.f);
				st.writeIdx = 0;
			}
			for (size_t i = 0; i < out.samples.size(); ++i) {
				float s = inBus->samples[i];
				out.samples[i] = st.buf[size_t(st.writeIdx)] * 0.6f + s;
				st.buf[size_t(st.writeIdx)] = s + out.samples[i] * 0.3f;
				st.writeIdx = (st.writeIdx + 1) % dSz;
			}
			out.valid = true;
			break;
		}
		case BlockType::OSC: {
			auto &st = std::get<OscState>(node->state);
			auto &out = *node->outputs[0];
			for (size_t i = 0; i < out.samples.size(); ++i) {
				float s = 0.f;
				switch (st.shape) {
				case OscShape::SINE: s = sdl3::Sin(st.phase); break;
				case OscShape::SQUARE: s = st.phase < sdl3::PI_F ? 1.f : -1.f; break;
				case OscShape::TRIANGLE: s = 1.f - 4.f * sdl3::Abs(st.phase / (2.f * sdl3::PI_F) - 0.5f); break;
				case OscShape::SAWTOOTH: s = st.phase / sdl3::PI_F - 1.f; break;
				case OscShape::NOISE: s = (float(std::rand()) / float(RAND_MAX)) * 2.f - 1.f; break;
				}
				out.samples[i] = st.amp * s;
				st.phase += 2.f * sdl3::PI_F * st.freq / float(K_SAMPLE_RATE);
				if (st.phase >= 2.f * sdl3::PI_F)
					st.phase -= 2.f * sdl3::PI_F;
			}
			out.valid = true;
			break;
		}
		case BlockType::SCOPE: {
			auto &st = std::get<ScopeState>(node->state);
			auto inBus = node->inputs[0];
			if (!inBus || !inBus->valid)
				break;
			const auto &src = inBus->samples;
			if (st.trig == TriggerMode::FREE) {
				st.display = src;
				break;
			}
			if (st.collectBuf.size() != src.size())
				st.collectBuf.assign(src.size(), 0.f);
			for (float s : src) {
				if (st.collecting) {
					st.collectBuf[st.collectCount++] = s;
					if (st.collectCount >= st.collectBuf.size()) {
						st.display = st.collectBuf;
						st.collecting = false;
						st.armed = true;
						st.collectCount = 0;
					}
				} else if (st.armed) {
					bool trig = st.trig == TriggerMode::RISING ? (st.prevSample < st.trigLevel && s >= st.trigLevel)
																: (st.prevSample > st.trigLevel && s <= st.trigLevel);
					if (trig) {
						st.collectBuf[0] = s;
						st.collectCount = 1;
						st.collecting = true;
						st.armed = false;
					}
				}
				st.prevSample = s;
			}
			break;
		}
		case BlockType::SPECTRUM: {
			auto &st = std::get<SpectrumState>(node->state);
			auto inBus = node->inputs[0];
			if (!inBus || !inBus->valid)
				break;
			std::vector<float> buf = inBus->samples;
			audio::ApplyWindow(buf, audio::WindowFunction(st.winIdx));
			auto spectrum = audio::ProcessFFT(buf);
			st.displayDb = audio::ProcessFFTMagnitudeDb(spectrum);
			break;
		}
		case BlockType::PLAYLIST: {
			auto &st = std::get<PlaylistState>(node->state);
			auto &out = *node->outputs[0];
			if (!st.playing || st.audioData.empty()) {
				out.Clear();
				break;
			}
			size_t toCopy = out.samples.size();
			size_t available = st.audioData.size() > st.playbackPos ? st.audioData.size() - st.playbackPos : 0;
			if (available < toCopy) {
				if (available > 0)
					std::copy(st.audioData.begin() + long(st.playbackPos), st.audioData.end(), out.samples.begin());
				std::fill(out.samples.begin() + long(available), out.samples.end(), 0.f);
				st.playbackPos = st.audioData.size();
				st.playing = false;
				if (st.autoplay) {
					int next = st.currentTrack + 1;
					if (next >= int(st.tracks.size()))
						next = st.looping ? 0 : -1;
					if (next != -1)
						st.pendingLoad = next;
				} else if (st.looping)
					st.pendingLoad = st.currentTrack;
			} else {
				std::copy(st.audioData.begin() + long(st.playbackPos), st.audioData.begin() + long(st.playbackPos + toCopy),
						  out.samples.begin());
				st.playbackPos += toCopy;
			}
			out.valid = true;
			break;
		}
		}
	}
}

} // namespace dsp

// ============================================================================
// Aide au décodage — sdl3::AudioDecoder (Phase 3), tout format supporté par
// SDL3_mixer (WAV/OGG/FLAC/MP3/...), converti en mono F32 au sampleRate du
// graphe. Tourne sur le thread UI (déclenché par le bouton Lire), PAS sur le
// thread audio — un décodage complet peut prendre plusieurs millisecondes,
// inacceptable dans la boucle temps réel.
// ============================================================================
[[nodiscard]] static bool DecodeToMonoFloat(const String &path, std::vector<float> &out) {
	out.clear();
	auto decRes = sdl3::AudioDecoder::Create(path);
	if (!decRes)
		return false;
	sdl3::AudioDecoder decoder = std::move(decRes.Value());
	SDL_AudioSpec target{SDL_AUDIO_F32, 1, dsp::K_SAMPLE_RATE};
	std::vector<float> chunk(8192);
	constexpr size_t K_MAX_SAMPLES = size_t(dsp::K_SAMPLE_RATE) * 600; // plafond 10 min
	while (out.size() < K_MAX_SAMPLES) {
		int got = decoder.Decode(chunk.data(), int(chunk.size() * sizeof(float)), &target);
		if (got <= 0)
			break;
		size_t n = size_t(got) / sizeof(float);
		out.insert(out.end(), chunk.begin(), chunk.begin() + long(n));
	}
	return !out.empty();
}

[[nodiscard]] static String FileNameOf(const String &path) {
	StringView sv = path.View();
	size_t slash = sv.Rfind('/');
	if (slash == StringView::NPOS)
		slash = sv.Rfind('\\');
	if (slash == StringView::NPOS)
		return path;
	return String(sv.Substr(slash + 1));
}

// ============================================================================
// Interface
// ============================================================================

static constexpr const char *PATCHBAY_CSS = R"css(
#toolbar { height: 36px; padding: 4px; gap: 4px; background-color: #10121c; }
#toolbar > button { font-size: 11px; }
.block { gap: 4px; width: 100%; }
.small { font-size: 10px; }
.tiny { font-size: 9px; }
.transport { width: 32px; }
)css";

/// Widgets d'un bloc mis à jour par la boucle (adresse stable : référencés par `Ref`).
struct BlockUi {
	ui::Widget plot, playlist, time;
	size_t shownTracks = 0;
};

// ============================================================================
// main()
// ============================================================================
int main() {
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO | sdl3::init_flags::EVENTS | sdl3::init_flags::AUDIO);
	if (!sdl) {
		std::cerr << "SDL init: " << sdl.Error().CStr() << "\n";
		return 1;
	}
	auto ttf = sdl3::TtfContext::Create();
	if (!ttf) {
		std::cerr << "TTF init: " << ttf.Error().CStr() << "\n";
		return 1;
	}
	auto mixerCtx = sdl3::MixerContext::Create(); // requis par sdl3::AudioDecoder (MIX_Init)
	if (!mixerCtx) {
		std::cerr << "Mixer init: " << mixerCtx.Error().CStr() << "\n";
		return 1;
	}
	auto winRes = sdl3::Window::Create(u8"audio:: - Patchbay audio", WIN_W, WIN_H, ui::WindowFrame::WINDOW_FLAGS);
	if (!winRes) {
		std::cerr << "Window: " << winRes.Error().CStr() << "\n";
		return 1;
	}
	auto &window = winRes.Value();
	auto renRes = sdl3::Renderer::Create(window);
	if (!renRes) {
		std::cerr << "Renderer: " << renRes.Error().CStr() << "\n";
		return 1;
	}
	auto &ren = renRes.Value();

	ecs::ArchetypeRegistry world;
	ui::Ui gui(world, window, ren);
	gui.AddStyleSheet(PATCHBAY_CSS);

	// ── Encadrement : barre de titre, barre d'outils + canevas, barre d'état ──
	ui::WindowFrame frame;
	frame.Build(gui, window,
				{.title = "audio:: - Patchbay audio", .appIcon = Some(ui::MaterialIcons::CABLE),
				 .status = "Ajouter des blocs, relier leurs broches · Clic droit : menu · Suppr : effacer"});
	ui::Widget toolbar = frame.Content().Append(ui::Row().Id("toolbar").FillWidth());
	ui::Widget canvas = frame.Content().Append(ui::NodeGraph().Grow().FillWidth());

	// ── Graphe DSP + threading (le thread audio ne touche jamais l'ECS) ───────
	dsp::Graph graph;
	auto graphMutexRes = sdl3::Mutex::Create();
	sdl3::Mutex graphMutex = std::move(graphMutexRes).Unwrap();
	sdl3::AtomicInt audioRunning{1};
	int nextId = 1;

	std::vector<sdl3::AudioDevice> recDevices = sdl3::EnumerateRecordingDevices();
	std::vector<sdl3::AudioDevice> playDevices = sdl3::EnumeratePlaybackDevices();
	std::vector<String> recNames{String("Défaut système")}, playNames{String("Défaut système")};
	for (auto &d : recDevices)
		recNames.push_back(d.name);
	for (auto &d : playDevices)
		playNames.push_back(d.name);

	// Entité <-> id du graphe DSP, et pin -> (id, port, sortie ?) pour dériver
	// `graph.connections` des connexions visuelles chaque image.
	struct PinInfo {
		int nodeId = 0;
		int port = 0;
		bool isOutput = false;
	};
	std::unordered_map<ecs::Entity, int> nodeEntityToId;
	std::unordered_map<ecs::Entity, PinInfo> pinInfo;
	std::map<int, std::unique_ptr<BlockUi>> blockUi;

	auto resyncConnections = [&] {
		sdl3::MutexGuard lock(graphMutex);
		auto cv = canvas.Get<ui::GraphCanvas>();
		if (cv.IsNone())
			return;
		std::vector<dsp::Connection> conns;
		for (auto &gc : cv.Unwrap()->connections) {
			auto itFrom = pinInfo.find(gc.fromPin);
			auto itTo = pinInfo.find(gc.toPin);
			if (itFrom == pinInfo.end() || itTo == pinInfo.end())
				continue;
			const PinInfo a = itFrom->second, b = itTo->second;
			if (a.isOutput == b.isOutput)
				continue; // sortie-sortie ou entrée-entrée : ignoré
			const PinInfo &out = a.isOutput ? a : b;
			const PinInfo &in = a.isOutput ? b : a;
			// Un port d'entrée n'accepte qu'UNE source : la première trouvée.
			const bool taken = std::any_of(conns.begin(), conns.end(), [&](const dsp::Connection &c) {
				return c.toId == in.nodeId && c.toPort == in.port;
			});
			if (!taken)
				conns.push_back({out.nodeId, out.port, in.nodeId, in.port});
		}
		graph.connections = std::move(conns);
		graph.RebuildBuses();
	};

	// ── (Ré)ouverture des flux Audio In/Out — index 0 = périphérique par défaut.
	auto openAudioIn = [&](int id, int idx) {
		sdl3::MutexGuard lock(graphMutex);
		auto *n = graph.FindNode(id);
		if (!n)
			return;
		auto &st = std::get<dsp::AudioInState>(n->state);
		if (st.stream) {
			st.stream.Pause();
			st.stream = sdl3::AudioStream();
		}
		sdl3::AudioSpec spec{sdl3::AudioFormat::F32, 1, dsp::K_SAMPLE_RATE};
		auto res = idx <= 0 ? sdl3::AudioStream::OpenRecording(spec)
							: sdl3::AudioStream::OpenRecording(spec, recDevices[size_t(idx - 1)].id);
		if (res) {
			st.stream = std::move(res.Value());
			st.stream.Resume();
		}
	};
	auto openAudioOut = [&](int id, int idx) {
		sdl3::MutexGuard lock(graphMutex);
		auto *n = graph.FindNode(id);
		if (!n)
			return;
		auto &st = std::get<dsp::AudioOutState>(n->state);
		if (st.stream) {
			st.stream.Pause();
			st.stream = sdl3::AudioStream();
		}
		sdl3::AudioSpec spec{sdl3::AudioFormat::F32, 1, dsp::K_SAMPLE_RATE};
		auto res = idx <= 0 ? sdl3::AudioStream::OpenPlayback(spec)
							: sdl3::AudioStream::OpenPlayback(spec, playDevices[size_t(idx - 1)].id);
		if (res) {
			st.stream = std::move(res.Value());
			st.stream.Resume();
		}
	};

	// Modifie l'état DSP d'un nœud sous verrou.
	auto withState = [&]<typename State>(int id, auto &&fn) {
		sdl3::MutexGuard lock(graphMutex);
		if (auto *n = graph.FindNode(id))
			fn(std::get<State>(n->state));
	};

	// ── Construction d'un bloc ───────────────────────────────────────────────
	auto addBlock = [&](dsp::BlockType type) {
		const int id = nextId++;
		const String bid = String::From(id);
		const char *title = "";
		sdl3::FPoint size{200.f, 160.f};
		ui::Color header = ui::Rgba(70, 74, 86);
		int numIn = 0, numOut = 0;
		switch (type) {
		case dsp::BlockType::AUDIO_IN: title = "Audio In"; size = {230.f, 140.f}; header = ui::Rgba(55, 115, 210); numOut = 1; break;
		case dsp::BlockType::AUDIO_OUT: title = "Audio Out"; size = {230.f, 110.f}; header = ui::Rgba(155, 75, 220); numIn = 1; break;
		case dsp::BlockType::FILTER: title = "Filter"; size = {200.f, 150.f}; header = ui::Rgba(230, 145, 30); numIn = 1; numOut = 1; break;
		case dsp::BlockType::AMP: title = "Amp"; size = {180.f, 90.f}; header = ui::Rgba(45, 195, 110); numIn = 1; numOut = 1; break;
		case dsp::BlockType::MIXER: title = "Mixer"; size = {180.f, 130.f}; header = ui::Rgba(45, 180, 180); numIn = 3; numOut = 1; break;
		case dsp::BlockType::DELAY: title = "Delay"; size = {200.f, 100.f}; header = ui::Rgba(155, 75, 220); numIn = 1; numOut = 1; break;
		case dsp::BlockType::OSC: title = "Oscillator"; size = {220.f, 190.f}; header = ui::Rgba(200, 60, 50); numOut = 1; break;
		case dsp::BlockType::SCOPE: title = "Scope"; size = {320.f, 260.f}; header = ui::Rgba(45, 195, 110); numIn = 1; break;
		case dsp::BlockType::SPECTRUM: title = "Spectrum"; size = {340.f, 280.f}; header = ui::Rgba(55, 115, 210); numIn = 1; break;
		case dsp::BlockType::PLAYLIST: title = "Playlist"; size = {320.f, 360.f}; header = ui::Rgba(230, 145, 30); numOut = 1; break;
		}
		const sdl3::FPoint pos{60.f + float((id - 1) % 4) * 260.f, 60.f + float(((id - 1) / 4) % 3) * 220.f};

		dsp::Node node;
		node.id = id;
		node.type = type;
		node.inputs.assign(size_t(numIn), nullptr);
		for (int i = 0; i < numOut; ++i)
			node.outputs.push_back(std::make_shared<dsp::AudioBus>());
		switch (type) {
		case dsp::BlockType::AUDIO_IN: node.state = dsp::AudioInState{}; break;
		case dsp::BlockType::AUDIO_OUT: node.state = dsp::AudioOutState{}; break;
		case dsp::BlockType::FILTER: node.state = dsp::FilterState{}; break;
		case dsp::BlockType::AMP: node.state = dsp::AmpState{}; break;
		case dsp::BlockType::MIXER: node.state = dsp::MixerState{}; break;
		case dsp::BlockType::DELAY: node.state = dsp::DelayState{}; break;
		case dsp::BlockType::OSC: node.state = dsp::OscState{}; break;
		case dsp::BlockType::SCOPE: node.state = dsp::ScopeState{}; break;
		case dsp::BlockType::SPECTRUM: node.state = dsp::SpectrumState{}; break;
		case dsp::BlockType::PLAYLIST: node.state = dsp::PlaylistState{}; break;
		}
		// Poussé dans le graphe tout de suite : les rappels ci-dessous et
		// l'ouverture initiale des flux retrouvent le nœud par id.
		{
			sdl3::MutexGuard lock(graphMutex);
			graph.nodes.push_back(std::move(node));
		}
		if (type == dsp::BlockType::AUDIO_IN)
			openAudioIn(id, 0);
		else if (type == dsp::BlockType::AUDIO_OUT)
			openAudioOut(id, 0);

		BlockUi &bui = *(blockUi[id] = std::make_unique<BlockUi>());

		// Libellé + curseur : le libellé suit la valeur (format appliqué à `map(v)`).
		auto labelledSlider = [](const char *cls, String initial, float min, float max, float value, float step,
								 std::function<String(float)> onChange) {
			auto label = std::make_shared<ui::Widget>();
			return std::vector<ui::AnyWidget>{
				ui::Label(std::move(initial)).Class(cls).Ref(*label),
				ui::Slider(min, max, value, step).FillWidth().OnChange([label, onChange](float v) {
					label->SetText(onChange(v));
				}),
			};
		};
		std::vector<ui::AnyWidget> content;
		auto append = [&content](std::vector<ui::AnyWidget> more) {
			for (auto &w : more)
				content.push_back(std::move(w));
		};

		switch (type) {
		case dsp::BlockType::AUDIO_IN:
		case dsp::BlockType::AUDIO_OUT: {
			const bool in = type == dsp::BlockType::AUDIO_IN;
			content.push_back(ui::Label("Périphérique :").Class("small"));
			content.push_back(ui::Combo(in ? recNames : playNames, 0).FillWidth().OnSelect([&, id, in](int i) {
				if (in)
					openAudioIn(id, i);
				else
					openAudioOut(id, i);
			}));
			content.push_back(ui::Switch(true).OnToggle([&, id, in](bool v) {
				if (in)
					withState.operator()<dsp::AudioInState>(id, [v](auto &st) { st.enabled = v; });
				else
					withState.operator()<dsp::AudioOutState>(id, [v](auto &st) { st.enabled = v; });
			}));
			break;
		}
		case dsp::BlockType::FILTER: {
			const String group = String("filter-") + bid;
			content.push_back(ui::Radio("Passe-bas", group, true).OnToggle([&, id](bool v) {
				if (v)
					withState.operator()<dsp::FilterState>(id, [](auto &st) { st.mode = audio::BiQuadKind::LOW_PASS; });
			}));
			content.push_back(ui::Radio("Passe-haut", group).OnToggle([&, id](bool v) {
				if (v)
					withState.operator()<dsp::FilterState>(id, [](auto &st) { st.mode = audio::BiQuadKind::HIGH_PASS; });
			}));
			append(labelledSlider("small", "Coupure : 1000 Hz", 0.f, 1.f, 0.5f, 0.005f, [&, id](float v) {
				const float hz = 20.f * sdl3::Pow(1000.f, v);
				withState.operator()<dsp::FilterState>(id, [hz](auto &st) { st.cutoffHz = hz; });
				return String(std::format("Coupure : {:.0f} Hz", hz));
			}));
			break;
		}
		case dsp::BlockType::AMP:
			append(labelledSlider("small", "Gain : 1.00x", 0.f, 5.f, 1.f, 0.01f, [&, id](float v) {
				withState.operator()<dsp::AmpState>(id, [v](auto &st) { st.gain = v; });
				return String(std::format("Gain : {:.2f}x", v));
			}));
			break;
		case dsp::BlockType::MIXER: {
			const String group = String("mix-") + bid;
			const std::pair<const char *, dsp::MixMode> modes[] = {
				{"Addition", dsp::MixMode::ADD}, {"Multiplication", dsp::MixMode::MULTIPLY}, {"Moyenne", dsp::MixMode::AVERAGE}};
			for (auto [label, mode] : modes)
				content.push_back(ui::Radio(label, group, mode == dsp::MixMode::ADD).OnToggle([&, id, mode](bool v) {
					if (v)
						withState.operator()<dsp::MixerState>(id, [mode](auto &st) { st.mode = mode; });
				}));
			break;
		}
		case dsp::BlockType::DELAY:
			append(labelledSlider("small", "Délai : 100 ms", 0.f, 2000.f, 100.f, 1.f, [&, id](float v) {
				withState.operator()<dsp::DelayState>(id, [v](auto &st) { st.delayMs = v; });
				return String(std::format("Délai : {:.0f} ms", v));
			}));
			break;
		case dsp::BlockType::OSC: {
			static constexpr const char *K_SHAPES[5] = {"Sin", "Sqr", "Tri", "Saw", "Nse"};
			const String group = String("osc-") + bid;
			std::vector<ui::AnyWidget> shapes;
			for (int sh = 0; sh < 5; ++sh)
				shapes.push_back(ui::Radio(K_SHAPES[sh], group, sh == 0).Class("tiny").OnToggle([&, id, sh](bool v) {
					if (v)
						withState.operator()<dsp::OscState>(id, [sh](auto &st) { st.shape = dsp::OscShape(sh); });
				}));
			content.push_back(ui::Wrap().Gap(4.f).Children(std::move(shapes)));
			append(labelledSlider("tiny", "Fréq : 440 Hz", 0.f, 1.f, 0.5f, 0.005f, [&, id](float v) {
				const float hz = 20.f * sdl3::Pow(1000.f, v);
				withState.operator()<dsp::OscState>(id, [hz](auto &st) { st.freq = hz; });
				return String(std::format("Fréq : {:.0f} Hz", hz));
			}));
			append(labelledSlider("tiny", "Amp : 0.50", 0.f, 1.f, 0.5f, 0.01f, [&, id](float v) {
				withState.operator()<dsp::OscState>(id, [v](auto &st) { st.amp = v; });
				return String(std::format("Amp : {:.2f}", v));
			}));
			break;
		}
		case dsp::BlockType::SCOPE: {
			const std::vector<float> initY(size_t(dsp::K_BUFFER_SIZE), 0.f);
			ui::PlotBuilder plot = ui::Plot();
			plot.AddLineSeries(initY, "", Some(ui::Rgba(80, 200, 120)))
				.YRange(-1.f, 1.f)
				.ShowLegend(false).FillWidth().Height(140.f).Ref(bui.plot);
			content.push_back(plot);
			static constexpr const char *K_TRIGGERS[3] = {"Libre", "Montant", "Descendant"};
			const String group = String("trig-") + bid;
			std::vector<ui::AnyWidget> triggers;
			for (int t = 0; t < 3; ++t)
				triggers.push_back(ui::Radio(K_TRIGGERS[t], group, t == 0).Class("tiny").OnToggle([&, id, t](bool v) {
					if (v)
						withState.operator()<dsp::ScopeState>(id, [t](auto &st) {
							st.trig = dsp::TriggerMode(t);
							st.armed = true;
							st.collecting = false;
							st.collectCount = 0;
						});
				}));
			content.push_back(ui::Wrap().Gap(4.f).Children(std::move(triggers)));
			append(labelledSlider("tiny", "Niveau : +0.00", -1.f, 1.f, 0.f, 0.01f, [&, id](float v) {
				withState.operator()<dsp::ScopeState>(id, [v](auto &st) { st.trigLevel = v; });
				return String(std::format("Niveau : {:+.2f}", v));
			}));
			break;
		}
		case dsp::BlockType::SPECTRUM: {
			const std::vector<float> initX(dsp::K_BUFFER_SIZE / 2, 0.f), initY(dsp::K_BUFFER_SIZE / 2, -80.f);
			ui::PlotBuilder plot = ui::Plot();
			plot.AddAreaSeries(initX, initY, "", Some(ui::Rgba(90, 180, 255)))
				.XRange(0.f, 20000.f)
				.YRange(-80.f, 0.f)
				.ShowLegend(false)
				.FillWidth()
				.Height(180.f)
				.Ref(bui.plot);
			content.push_back(ui::Label("Fenêtre :").Class("small"));
			content.push_back(ui::Combo(std::vector<String>{"Rect", "Hann", "Hamming", "Blackman"}, 1)
								  .FillWidth()
								  .OnSelect([&, id](int i) {
									  withState.operator()<dsp::SpectrumState>(id, [i](auto &st) { st.winIdx = i; });
								  }));
			content.push_back(plot);
			break;
		}
		case dsp::BlockType::PLAYLIST: {
			content.push_back(ui::ListBox({}, -1).FillWidth().Height(90.f).Ref(bui.playlist));
			content.push_back(ui::Row().Gap(2.f).Children({
				ui::Button("Ajouter...").Grow().OnClick([&, id] {
					std::vector<sdl3::DialogFilter> filters{{String("Audio"), String("wav;ogg;flac;mp3")}};
					sdl3::dialog::ShowOpenFile(
						[&, id](const sdl3::DialogResult &res, int) {
							if (res.Ok())
								withState.operator()<dsp::PlaylistState>(id, [&res](auto &st) {
									for (auto &path : res.files)
										st.tracks.push_back(path);
								});
						},
						window, filters, String(), true);
				}),
				ui::Button("Vider").Grow().OnClick([&, id] {
					withState.operator()<dsp::PlaylistState>(id, [](auto &st) {
						st.tracks.clear();
						st.playing = false;
						st.audioData.clear();
						st.currentTrack = -1;
					});
				}),
			}));
			content.push_back(ui::Row().Gap(4.f).Children({
				ui::Button("\xe2\x96\xb6").Class("transport").Tooltip("Lire").OnClick([&, id] {
					int sel = blockUi[id]->playlist.Selected();
					withState.operator()<dsp::PlaylistState>(id, [&sel](auto &st) {
						if (sel < 0 && !st.tracks.empty())
							sel = 0;
						if (sel >= 0)
							st.pendingLoad = sel;
					});
				}),
				ui::Button("\xe2\x96\xa0").Class("transport").Tooltip("Arrêter").OnClick([&, id] {
					withState.operator()<dsp::PlaylistState>(id, [](auto &st) {
						st.playing = false;
						st.playbackPos = 0;
					});
				}),
				ui::Switch(true).Tooltip("Lecture automatique").OnToggle([&, id](bool v) {
					withState.operator()<dsp::PlaylistState>(id, [v](auto &st) { st.autoplay = v; });
				}),
				ui::Switch(false).Tooltip("Boucle").OnToggle([&, id](bool v) {
					withState.operator()<dsp::PlaylistState>(id, [v](auto &st) { st.looping = v; });
				}),
			}));
			content.push_back(ui::Label("00:00 / 00:00").Class("small").Ref(bui.time));
			break;
		}
		}

		// ── Nœud visuel + pins ───────────────────────────────────────────────
		ui::Widget nodeW = ui::AddGraphNode(canvas, title, pos, size, ui::Column().Class("block").Children(std::move(content)),
											header);
		for (int i = 0; i < numIn; ++i) {
			ui::Widget pin = ui::AddGraphPin(nodeW, ui::PinSide::Left, float(i + 1) / float(numIn + 1),
											 ui::PinShape::CIRCLE, ui::Rgba(55, 115, 210));
			pinInfo[pin.Entity()] = {id, i, false};
		}
		for (int i = 0; i < numOut; ++i) {
			ui::Widget pin = ui::AddGraphPin(nodeW, ui::PinSide::Right, float(i + 1) / float(numOut + 1),
											 ui::PinShape::TRIANGLE, ui::Rgba(45, 195, 110));
			pinInfo[pin.Entity()] = {id, i, true};
		}
		nodeEntityToId[nodeW.Entity()] = id;
	};

	// ── Barre d'outils « Ajouter un bloc » ───────────────────────────────────
	struct ToolbarBtn {
		const char *label;
		dsp::BlockType type;
	};
	static constexpr ToolbarBtn K_TOOLBAR_BTNS[] = {
		{"+ Audio In", dsp::BlockType::AUDIO_IN}, {"+ Audio Out", dsp::BlockType::AUDIO_OUT},
		{"+ Filter", dsp::BlockType::FILTER},	  {"+ Amp", dsp::BlockType::AMP},
		{"+ Mixer", dsp::BlockType::MIXER},		  {"+ Delay", dsp::BlockType::DELAY},
		{"+ Oscillator", dsp::BlockType::OSC},	  {"+ Scope", dsp::BlockType::SCOPE},
		{"+ Spectrum", dsp::BlockType::SPECTRUM}, {"+ Playlist", dsp::BlockType::PLAYLIST},
	};
	for (const ToolbarBtn &tb : K_TOOLBAR_BTNS) {
		const dsp::BlockType t = tb.type;
		toolbar.Append(ui::Button(tb.label).OnClick([&addBlock, t] { addBlock(t); }));
	}

	// ── Menu contextuel (clic droit) : supprimer un bloc / déconnecter un pin ──
	ecs::Entity rightClickedNode{}, rightClickedPin{};
	ui::Widget menu = gui.Mount(ui::ContextMenu().Children({
		ui::MenuItem("Supprimer le bloc").OnClick([&] {
			if (!world.IsAlive(rightClickedNode))
				return;
			if (auto it = nodeEntityToId.find(rightClickedNode); it != nodeEntityToId.end()) {
				const int id = it->second;
				{
					sdl3::MutexGuard lock(graphMutex);
					std::erase_if(graph.nodes, [id](const dsp::Node &n) { return n.id == id; });
					std::erase_if(graph.connections,
								  [id](const dsp::Connection &c) { return c.fromId == id || c.toId == id; });
				}
				blockUi.erase(id);
				nodeEntityToId.erase(it);
			}
			ui::DestroyGraphNode(world, rightClickedNode);
			rightClickedNode = ecs::Entity{};
		}),
		ui::MenuItem("Déconnecter").OnClick([&] {
			if (!world.IsAlive(rightClickedPin))
				return;
			if (auto cv = canvas.Get<ui::GraphCanvas>(); cv.IsSome())
				std::erase_if(cv.Unwrap()->connections, [&](const ui::GraphConnection &c) {
					return c.fromPin == rightClickedPin || c.toPin == rightClickedPin;
				});
			rightClickedPin = ecs::Entity{};
			resyncConnections();
		}),
	}));
	ui::GraphCallbacks callbacks;
	callbacks.onContextMenu = [&](ecs::Entity node, ecs::Entity pin, sdl3::FPoint at) {
		rightClickedNode = node;
		rightClickedPin = pin;
		menu.OpenAt(at);
	};
	ui::SetGraphCallbacks(canvas, std::move(callbacks));

	// ── Thread audio de fond : ne touche QUE `graph`, jamais l'ECS ───────────
	auto audioThreadRes = sdl3::Thread::Create(
		[&audioRunning, &graph, &graphMutex]() -> int {
			while (audioRunning.Load() != 0) {
				{
					sdl3::MutexGuard lock(graphMutex);
					dsp::ProcessGraph(graph);
				}
				sdl3::DelayMS(uint32_t(1000 * dsp::K_BUFFER_SIZE / dsp::K_SAMPLE_RATE));
			}
			return 0;
		},
		"dsp");
	sdl3::Thread audioThread = std::move(audioThreadRes).Unwrap();

	// ── Boucle ───────────────────────────────────────────────────────────────
	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();
	while (running) {
		const uint64_t now = sdl3::GetTicksMS();
		const float dt = float(now - lastTick) / 1000.f;
		lastTick = now;

		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			if (e.IsQuit()) {
				running = false;
				break;
			}
			if (gui.HandleEvent(e))
				continue;
			if (e.IsKeyDown(SDLK_ESCAPE))
				running = false;
		}
		gui.Update(dt);
		frame.Update();
		if (frame.CloseRequested())
			running = false;

		// Topologie : re-dérive `graph.connections` des connexions visuelles.
		resyncConnections();

		// Chargement différé d'une piste (décodage HORS du verrou audio).
		std::vector<std::pair<int, String>> toLoad;
		{
			sdl3::MutexGuard lock(graphMutex);
			for (auto &n : graph.nodes) {
				if (n.type != dsp::BlockType::PLAYLIST)
					continue;
				auto &st = std::get<dsp::PlaylistState>(n.state);
				if (st.pendingLoad >= 0 && st.pendingLoad < int(st.tracks.size())) {
					toLoad.push_back({n.id, st.tracks[size_t(st.pendingLoad)]});
					st.currentTrack = st.pendingLoad;
					st.pendingLoad = -1;
				}
			}
		}
		for (auto &[id, path] : toLoad) {
			std::vector<float> data;
			const bool ok = DecodeToMonoFloat(path, data);
			withState.operator()<dsp::PlaylistState>(id, [&](auto &st) {
				st.audioData = ok ? std::move(data) : std::vector<float>{};
				st.playbackPos = 0;
				st.playing = ok;
			});
		}

		// Télémétrie DSP -> interface (Scope / Spectrum / Playlist), sous verrou.
		{
			sdl3::MutexGuard lock(graphMutex);
			for (auto &n : graph.nodes) {
				auto it = blockUi.find(n.id);
				if (it == blockUi.end())
					continue;
				BlockUi &bui = *it->second;
				if (n.type == dsp::BlockType::SCOPE) {
					auto &st = std::get<dsp::ScopeState>(n.state);
					if (auto p = bui.plot.Get<ui::PlotModel>(); p.IsSome() && !st.display.empty() && !p.Unwrap()->series.empty())
						p.Unwrap()->series[0].SetY(st.display);
				} else if (n.type == dsp::BlockType::SPECTRUM) {
					auto &st = std::get<dsp::SpectrumState>(n.state);
					if (auto p = bui.plot.Get<ui::PlotModel>(); p.IsSome() && !st.displayDb.empty() && !p.Unwrap()->series.empty()) {
						auto freqs = audio::ProcessFFTFrequencies(size_t(dsp::K_BUFFER_SIZE), dsp::K_SAMPLE_RATE);
						std::vector<float> y(freqs.size(), -80.f);
						for (size_t k = 0; k < freqs.size() && k < st.displayDb.size(); ++k)
							y[k] = sdl3::Max(-80.f, st.displayDb[k]);
						p.Unwrap()->series[0].SetXy(freqs, y);
					}
				} else if (n.type == dsp::BlockType::PLAYLIST) {
					auto &st = std::get<dsp::PlaylistState>(n.state);
					if (bui.playlist && bui.shownTracks != st.tracks.size()) {
						std::vector<String> names;
						for (auto &t : st.tracks)
							names.push_back(FileNameOf(t));
						bui.playlist.SetItems(std::move(names));
						bui.shownTracks = st.tracks.size();
					}
					const float durSec = st.audioData.empty() ? 0.f : float(st.audioData.size()) / float(dsp::K_SAMPLE_RATE);
					const float posSec = float(st.playbackPos) / float(dsp::K_SAMPLE_RATE);
					bui.time.SetText(std::format("{:02.0f}:{:02.0f} / {:02.0f}:{:02.0f}", sdl3::Floor(posSec / 60.f),
												 sdl3::Fmod(posSec, 60.f), sdl3::Floor(durSec / 60.f),
												 sdl3::Fmod(durSec, 60.f)));
				}
			}
		}

		ren.SetDrawColor(gui.CurrentPalette().appBg);
		ren.Clear();
		gui.Render();
		ren.Present();
	}

	audioRunning.Store(0);
	audioThread.Wait();
	return 0;
}
