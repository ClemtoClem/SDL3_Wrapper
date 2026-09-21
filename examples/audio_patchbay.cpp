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
 * Spectrum (ui::UiPlot au lieu d'un canvas dessiné à la main), Playlist
 * (sdl3::AudioDecoder pour le décodage multi-format, sdl3::dialog pour le
 * choix de fichiers plutôt qu'un explorateur de fichiers fait main).
 *
 *   make examples && ./build/examples/audio_patchbay
 */
#include <algorithm>
#include <cstdlib>
#include <format>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "audio/dsp.hpp"
#include "core/core.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

static constexpr int WIN_W = 1440;
static constexpr int WIN_H = 860;
static constexpr float FONT_PT = 13.f;

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
	auto fontRes = sdl3::Font::FindLocal({"DejaVuSans", "FreeSans", "Arial"}, FONT_PT);
	if (!fontRes) {
		std::cerr << "Font: " << fontRes.Error().CStr() << "\n";
		return 1;
	}
	auto &font = fontRes.Value();

	auto winRes = sdl3::Window::Create(u8"audio:: - Patchbay audio", WIN_W, WIN_H, sdl3::window_flags::RESIZABLE);
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
	ui::SdlRendererBackend renBackend(ren);
	auto engRes = sdl3::TextEngine::Create(ren);
	if (!engRes) {
		std::cerr << "TextEngine: " << engRes.Error().CStr() << "\n";
		return 1;
	}
	auto &eng = engRes.Value();

	// ── Pipeline UI manuel (systèmes séparés, pas la façade ui::Ui) — le
	// node-graph doit s'intercaler entre layout et rendu, cf. ui_node_graph_demo.cpp ──
	ecs::ArchetypeRegistry ar;
	ui::LayoutSystem layout;
	ui::InputSystem input;
	ui::RenderSystem render;
	ui::StyleSystem style;
	ui::UiFactory f(ar, layout);
	ui::NodeGraphSystem nodeGraph;
	style.sheet = &f.sheet;

	window.StartTextInput();
	render.SetTextEngine(&eng, &font);
	nodeGraph.SetTextEngine(eng, font);
	layout.measureText = [&font](const String &s, float fs) -> sdl3::FPoint {
		if (auto sz = font.Measure(s); sz.IsSome())
			return {float(sz.Unwrap().x) * (fs / FONT_PT), fs * 1.35f};
		return {float(s.size()) * fs * 0.55f, fs * 1.3f};
	};
	auto named = [&ar](const String &n) -> ecs::Entity {
		auto o = ui::FindByName(ar, n);
		return o.IsSome() ? o.Unwrap() : ecs::Entity{};
	};
	auto setLabel = [&](ecs::Entity e, String text) {
		if (!e.Valid())
			return;
		if (auto l = ar.GetComponent<ui::UiLabel>(e); l.IsSome()) {
			if (l.Unwrap()->text == text)
				return;
			l.Unwrap()->text = std::move(text);
			layout.MarkDirty();
		}
	};

	// ── Canvas plein écran ───────────────────────────────────────────────────
	auto canvas = ui::CanvasBuilder(f);
	canvas.Anchor(ui::Anchor::TopLeft).Offset(0.f, 36.f).W(ui::Dimension::Rpct(100.f)).H(ui::Dimension::Rpct(100.f).Plus(-36.f));
	ecs::Entity canvasE = canvas.Spawn();
	ui::AttachNodeGraphCanvas(ar, canvasE);

	// ── Graphe DSP + threading (Décision #7 du plan) ────────────────────────
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

	// Entity <-> id du graphe DSP, et pin -> (id, port, isOutput) pour
	// dériver `graph.connections` des `GraphConnection` visuelles chaque frame.
	struct PinInfo {
		int nodeId = 0;
		int port = 0;
		bool isOutput = false;
	};
	std::unordered_map<uint64_t, int> nodeEntityToId;
	std::unordered_map<int, ecs::Entity> idToNodeEntity;
	std::unordered_map<uint64_t, PinInfo> pinInfo;
	std::unordered_map<int, ecs::Entity> nodePlotEntity; // Scope/Spectrum uniquement

	auto entityKey = [](ecs::Entity e) -> uint64_t { return e.id; };

	auto resyncConnections = [&] {
		sdl3::MutexGuard lock(graphMutex);
		auto cv = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE);
		if (cv.IsNone())
			return;
		std::vector<dsp::Connection> newConns;
		for (auto &gc : cv.Unwrap()->connections) {
			auto itFrom = pinInfo.find(entityKey(gc.fromPin));
			auto itTo = pinInfo.find(entityKey(gc.toPin));
			if (itFrom == pinInfo.end() || itTo == pinInfo.end())
				continue;
			PinInfo a = itFrom->second, b = itTo->second;
			if (a.isOutput == b.isOutput)
				continue; // les deux côtés du même type (sortie-sortie/entrée-entrée) : ignoré
			PinInfo &outPin = a.isOutput ? a : b;
			PinInfo &inPin = a.isOutput ? b : a;
			newConns.push_back({outPin.nodeId, outPin.port, inPin.nodeId, inPin.port});
		}
		// Un port d'entrée n'accepte qu'UNE source (comme l'original) : garde
		// la première trouvée par port d'entrée.
		std::vector<dsp::Connection> deduped;
		for (auto &c : newConns) {
			bool dup = false;
			for (auto &d : deduped)
				if (d.toId == c.toId && d.toPort == c.toPort) {
					dup = true;
					break;
				}
			if (!dup)
				deduped.push_back(c);
		}
		graph.connections = std::move(deduped);
		graph.RebuildBuses();
	};

	// ── (Ré)ouverture des flux Audio In/Out — index 0 = périphérique par
	// défaut, index i>0 = recDevices[i-1]/playDevices[i-1] (même décalage que
	// les items du combo, "Défaut système" en tête). Verrouille `graph` elle-
	// même : le nœud doit déjà y être poussé avant le premier appel.
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
		auto res = (idx <= 0) ? sdl3::AudioStream::OpenRecording(spec)
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
		auto res = (idx <= 0) ? sdl3::AudioStream::OpenPlayback(spec)
							  : sdl3::AudioStream::OpenPlayback(spec, playDevices[size_t(idx - 1)].id);
		if (res) {
			st.stream = std::move(res.Value());
			st.stream.Resume();
		}
	};

	// ── Construction d'un bloc ───────────────────────────────────────────────
	auto addBlock = [&](dsp::BlockType type) {
		int id = nextId++;
		String bid = String(std::format("{}", id).c_str());

		const char *title = "";
		sdl3::FPoint size{200.f, 160.f};
		sdl3::FColor headerColor{sdl3::FColor::UI_NODE_HEADER_NEUTRAL()};
		int numIn = 0, numOut = 0;
		switch (type) {
		case dsp::BlockType::AUDIO_IN: title = "Audio In"; size = {230.f, 140.f}; headerColor = sdl3::FColor::UI_ACCENT_BLUE_PRIMARY(); numOut = 1; break;
		case dsp::BlockType::AUDIO_OUT: title = "Audio Out"; size = {230.f, 110.f}; headerColor = sdl3::FColor{155 / 255.f, 75 / 255.f, 220 / 255.f, 1.f}; numIn = 1; break;
		case dsp::BlockType::FILTER: title = "Filter"; size = {200.f, 150.f}; headerColor = sdl3::FColor{230 / 255.f, 145 / 255.f, 30 / 255.f, 1.f}; numIn = 1; numOut = 1; break;
		case dsp::BlockType::AMP: title = "Amp"; size = {180.f, 90.f}; headerColor = sdl3::FColor{45 / 255.f, 195 / 255.f, 110 / 255.f, 1.f}; numIn = 1; numOut = 1; break;
		case dsp::BlockType::MIXER: title = "Mixer"; size = {180.f, 130.f}; headerColor = sdl3::FColor{45 / 255.f, 180 / 255.f, 180 / 255.f, 1.f}; numIn = 3; numOut = 1; break;
		case dsp::BlockType::DELAY: title = "Delay"; size = {200.f, 100.f}; headerColor = sdl3::FColor{155 / 255.f, 75 / 255.f, 220 / 255.f, 1.f}; numIn = 1; numOut = 1; break;
		case dsp::BlockType::OSC: title = "Oscillator"; size = {220.f, 190.f}; headerColor = sdl3::FColor{200 / 255.f, 60 / 255.f, 50 / 255.f, 1.f}; numOut = 1; break;
		case dsp::BlockType::SCOPE: title = "Scope"; size = {320.f, 260.f}; headerColor = sdl3::FColor{45 / 255.f, 195 / 255.f, 110 / 255.f, 1.f}; numIn = 1; break;
		case dsp::BlockType::SPECTRUM: title = "Spectrum"; size = {340.f, 280.f}; headerColor = sdl3::FColor::UI_ACCENT_BLUE_PRIMARY(); numIn = 1; break;
		case dsp::BlockType::PLAYLIST: title = "Playlist"; size = {320.f, 360.f}; headerColor = sdl3::FColor{230 / 255.f, 145 / 255.f, 30 / 255.f, 1.f}; numOut = 1; break;
		}

		sdl3::FPoint pos{60.f + float((id - 1) % 4) * 260.f, 60.f + float(((id - 1) / 4) % 3) * 220.f};

		dsp::Node node;
		node.id = id;
		node.type = type;
		node.inputs.assign(size_t(numIn), nullptr);
		node.outputs.clear();
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

		// Poussé dans le graphe MAINTENANT (pas en fin de fonction) : les
		// callbacks ci-dessous (device combos) et l'ouverture initiale du
		// flux Audio In/Out doivent pouvoir retrouver ce nœud par id.
		{
			sdl3::MutexGuard lock(graphMutex);
			graph.nodes.push_back(std::move(node));
		}
		if (type == dsp::BlockType::AUDIO_IN)
			openAudioIn(id, 0);
		else if (type == dsp::BlockType::AUDIO_OUT)
			openAudioOut(id, 0);

		// ── Contenu visuel par type ──────────────────────────────────────────
		auto content = f.Column();
		content.Gap(4.f).W(ui::Dimension::Pct(100));

		switch (type) {
		case dsp::BlockType::AUDIO_IN: {
			auto tog = f.Toggle(true);
			tog.Name(String("in_") + bid + String("_tog")).OnToggle([&graph, &graphMutex, id](bool v) {
				sdl3::MutexGuard lock(graphMutex);
				if (auto *n = graph.FindNode(id))
					std::get<dsp::AudioInState>(n->state).enabled = v;
			});
			auto dev = f.Combo(recNames, 0);
			dev.Name(String("in_") + bid + String("_dev")).GrowW().OnChange([&openAudioIn, id](float v) { openAudioIn(id, int(v)); });
			content.Children(f.Label("Périphérique :").FontSize(10.f), std::move(dev), std::move(tog));
			break;
		}
		case dsp::BlockType::AUDIO_OUT: {
			auto tog = f.Toggle(true);
			tog.Name(String("out_") + bid + String("_tog")).OnToggle([&graph, &graphMutex, id](bool v) {
				sdl3::MutexGuard lock(graphMutex);
				if (auto *n = graph.FindNode(id))
					std::get<dsp::AudioOutState>(n->state).enabled = v;
			});
			auto dev = f.Combo(playNames, 0);
			dev.Name(String("out_") + bid + String("_dev")).GrowW().OnChange([&openAudioOut, id](float v) { openAudioOut(id, int(v)); });
			content.Children(f.Label("Périphérique :").FontSize(10.f), std::move(dev), std::move(tog));
			break;
		}
		case dsp::BlockType::FILTER: {
			auto lp = f.Radio(String("filtmode_") + bid, String("Passe-bas"), true);
			lp.OnToggle([&graph, &graphMutex, id](bool v) {
				if (!v)
					return;
				sdl3::MutexGuard lock(graphMutex);
				if (auto *n = graph.FindNode(id))
					std::get<dsp::FilterState>(n->state).mode = audio::BiQuadKind::LOW_PASS;
			});
			auto hp = f.Radio(String("filtmode_") + bid, String("Passe-haut"), false);
			hp.OnToggle([&graph, &graphMutex, id](bool v) {
				if (!v)
					return;
				sdl3::MutexGuard lock(graphMutex);
				if (auto *n = graph.FindNode(id))
					std::get<dsp::FilterState>(n->state).mode = audio::BiQuadKind::HIGH_PASS;
			});
			auto lbl = f.Label("Coupure : 1000 Hz");
			lbl.Name(String("filt_") + bid + String("_lbl")).FontSize(10.f);
			auto cut = f.Slider(0.f, 1.f, 0.5f, 0.005f);
			cut.GrowW().OnChange([&, id](float v) {
				float hz = 20.f * sdl3::Pow(1000.f, v);
				{
					sdl3::MutexGuard lock(graphMutex);
					if (auto *n = graph.FindNode(id))
						std::get<dsp::FilterState>(n->state).cutoffHz = hz;
				}
				setLabel(named(String("filt_") + bid + String("_lbl")), String(std::format("Coupure : {:.0f} Hz", hz).c_str()));
			});
			content.Children(std::move(lp), std::move(hp), std::move(lbl), std::move(cut));
			break;
		}
		case dsp::BlockType::AMP: {
			auto lbl = f.Label("Gain : 1.00x");
			lbl.Name(String("amp_") + bid + String("_lbl")).FontSize(10.f);
			auto sl = f.Slider(0.f, 5.f, 1.f, 0.01f);
			sl.GrowW().OnChange([&, id](float v) {
				{
					sdl3::MutexGuard lock(graphMutex);
					if (auto *n = graph.FindNode(id))
						std::get<dsp::AmpState>(n->state).gain = v;
				}
				setLabel(named(String("amp_") + bid + String("_lbl")), String(std::format("Gain : {:.2f}x", v).c_str()));
			});
			content.Children(std::move(lbl), std::move(sl));
			break;
		}
		case dsp::BlockType::MIXER: {
			auto add = f.Radio(String("mix_") + bid, String("Addition"), true);
			add.OnToggle([&graph, &graphMutex, id](bool v) {
				if (!v)
					return;
				sdl3::MutexGuard lock(graphMutex);
				if (auto *n = graph.FindNode(id))
					std::get<dsp::MixerState>(n->state).mode = dsp::MixMode::ADD;
			});
			auto mul = f.Radio(String("mix_") + bid, String("Multiplication"), false);
			mul.OnToggle([&graph, &graphMutex, id](bool v) {
				if (!v)
					return;
				sdl3::MutexGuard lock(graphMutex);
				if (auto *n = graph.FindNode(id))
					std::get<dsp::MixerState>(n->state).mode = dsp::MixMode::MULTIPLY;
			});
			auto avg = f.Radio(String("mix_") + bid, String("Moyenne"), false);
			avg.OnToggle([&graph, &graphMutex, id](bool v) {
				if (!v)
					return;
				sdl3::MutexGuard lock(graphMutex);
				if (auto *n = graph.FindNode(id))
					std::get<dsp::MixerState>(n->state).mode = dsp::MixMode::AVERAGE;
			});
			content.Children(std::move(add), std::move(mul), std::move(avg));
			break;
		}
		case dsp::BlockType::DELAY: {
			auto lbl = f.Label("Délai : 100 ms");
			lbl.Name(String("dly_") + bid + String("_lbl")).FontSize(10.f);
			auto sl = f.Slider(0.f, 2000.f, 100.f, 1.f);
			sl.GrowW().OnChange([&, id](float v) {
				{
					sdl3::MutexGuard lock(graphMutex);
					if (auto *n = graph.FindNode(id))
						std::get<dsp::DelayState>(n->state).delayMs = v;
				}
				setLabel(named(String("dly_") + bid + String("_lbl")), String(std::format("Délai : {:.0f} ms", v).c_str()));
			});
			content.Children(std::move(lbl), std::move(sl));
			break;
		}
		case dsp::BlockType::OSC: {
			static constexpr const char *K_SHAPE_LABELS[5] = {"Sin", "Sqr", "Tri", "Saw", "Nse"};
			auto shapeRow = f.Row();
			shapeRow.Gap(2.f);
			for (int s = 0; s < 5; ++s) {
				auto r = f.Radio(String("oscshape_") + bid, String(K_SHAPE_LABELS[s]), s == 0);
				r.FontSize(9.f).OnToggle([&graph, &graphMutex, id, s](bool v) {
					if (!v)
						return;
					sdl3::MutexGuard lock(graphMutex);
					if (auto *n = graph.FindNode(id))
						std::get<dsp::OscState>(n->state).shape = dsp::OscShape(s);
				});
				shapeRow.Children(std::move(r));
			}
			auto lblF = f.Label("Fréq : 440 Hz");
			lblF.Name(String("osc_") + bid + String("_flbl")).FontSize(9.f);
			auto slF = f.Slider(0.f, 1.f, 0.5f, 0.005f);
			slF.GrowW().OnChange([&, id](float v) {
				float hz = 20.f * sdl3::Pow(1000.f, v);
				{
					sdl3::MutexGuard lock(graphMutex);
					if (auto *n = graph.FindNode(id))
						std::get<dsp::OscState>(n->state).freq = hz;
				}
				setLabel(named(String("osc_") + bid + String("_flbl")), String(std::format("Fréq : {:.0f} Hz", hz).c_str()));
			});
			auto lblA = f.Label("Amp : 0.50");
			lblA.Name(String("osc_") + bid + String("_albl")).FontSize(9.f);
			auto slA = f.Slider(0.f, 1.f, 0.5f, 0.01f);
			slA.GrowW().OnChange([&, id](float v) {
				{
					sdl3::MutexGuard lock(graphMutex);
					if (auto *n = graph.FindNode(id))
						std::get<dsp::OscState>(n->state).amp = v;
				}
				setLabel(named(String("osc_") + bid + String("_albl")), String(std::format("Amp : {:.2f}", v).c_str()));
			});
			content.Children(std::move(shapeRow), std::move(lblF), std::move(slF), std::move(lblA), std::move(slA));
			break;
		}
		case dsp::BlockType::SCOPE: {
			std::vector<float> initY(size_t(dsp::K_BUFFER_SIZE), 0.f);
			auto plot = f.Plot("");
			plot.AddLineSeries(initY, "", Some(sdl3::FColor::UI_ACCENT_GREEN()));
			plot.Name(String("scope_") + bid + String("_plot")).GrowW().H(ui::Dimension::Px(140));

			auto trigRow = f.Row();
			trigRow.Gap(3.f);
			static constexpr const char *K_TRIG_LABELS[3] = {"Libre", "Montant", "Descendant"};
			for (int t = 0; t < 3; ++t) {
				auto r = f.Radio(String("trig_") + bid, String(K_TRIG_LABELS[t]), t == 0);
				r.FontSize(9.f).OnToggle([&graph, &graphMutex, id, t](bool v) {
					if (!v)
						return;
					sdl3::MutexGuard lock(graphMutex);
					if (auto *n = graph.FindNode(id)) {
						auto &st = std::get<dsp::ScopeState>(n->state);
						st.trig = dsp::TriggerMode(t);
						st.armed = true;
						st.collecting = false;
						st.collectCount = 0;
					}
				});
				trigRow.Children(std::move(r));
			}
			auto lblLvl = f.Label("Niveau : +0.00");
			lblLvl.Name(String("scope_") + bid + String("_lvllbl")).FontSize(9.f);
			auto slLvl = f.Slider(-1.f, 1.f, 0.f, 0.01f);
			slLvl.GrowW().OnChange([&, id](float v) {
				{
					sdl3::MutexGuard lock(graphMutex);
					if (auto *n = graph.FindNode(id))
						std::get<dsp::ScopeState>(n->state).trigLevel = v;
				}
				setLabel(named(String("scope_") + bid + String("_lvllbl")), String(std::format("Niveau : {:+.2f}", v).c_str()));
			});
			content.Children(std::move(plot), std::move(trigRow), std::move(lblLvl), std::move(slLvl));
			break;
		}
		case dsp::BlockType::SPECTRUM: {
			std::vector<float> initX(dsp::K_BUFFER_SIZE / 2, 0.f), initY(dsp::K_BUFFER_SIZE / 2, -80.f);
			auto plot = f.Plot("");
			plot.AddAreaSeries(initX, initY, "", Some(sdl3::FColor::UI_ACCENT_BLUE_SKY()));
			plot.Name(String("spec_") + bid + String("_plot")).GrowW().H(ui::Dimension::Px(180));

			auto win = f.Combo(std::vector<String>{String("Rect"), String("Hann"), String("Hamming"), String("Blackman")}, 1);
			win.GrowW().OnChange([&graph, &graphMutex, id](float v) {
				sdl3::MutexGuard lock(graphMutex);
				if (auto *n = graph.FindNode(id))
					std::get<dsp::SpectrumState>(n->state).winIdx = int(v);
			});
			content.Children(f.Label("Fenêtre :").FontSize(10.f), std::move(win), std::move(plot));
			break;
		}
		case dsp::BlockType::PLAYLIST: {
			auto lst = f.Listbox(std::vector<String>{}, -1);
			lst.Name(String("plist_") + bid).GrowW().H(ui::Dimension::Px(90));

			auto btnAdd = f.Button("Ajouter...");
			btnAdd.GrowW().OnClick([&, id] {
				std::vector<sdl3::DialogFilter> filters{{String("Audio"), String("wav;ogg;flac;mp3")}};
				sdl3::dialog::ShowOpenFile(
					[&, id](const sdl3::DialogResult &res, int) {
						if (!res.Ok())
							return;
						sdl3::MutexGuard lock(graphMutex);
						auto *n = graph.FindNode(id);
						if (!n)
							return;
						auto &st = std::get<dsp::PlaylistState>(n->state);
						for (auto &path : res.files)
							st.tracks.push_back(path);
						// Le listbox est resynchronisé chaque frame depuis
						// st.tracks (cf. boucle principale) — pas ici, pour
						// rester cohérent avec un seul point de vérité.
					},
					window, filters, String(), true);
			});
			auto btnClear = f.Button("Vider");
			btnClear.GrowW().OnClick([&graph, &graphMutex, id] {
				sdl3::MutexGuard lock(graphMutex);
				if (auto *n = graph.FindNode(id)) {
					auto &st = std::get<dsp::PlaylistState>(n->state);
					st.tracks.clear();
					st.playing = false;
					st.audioData.clear();
					st.currentTrack = -1;
				}
			});
			auto rowTop = f.Row();
			rowTop.Gap(2.f).Children(std::move(btnAdd), std::move(btnClear));

			auto btnPlay = f.Button(u8"▶");
			btnPlay.W(ui::Dimension::Px(32)).OnClick([&, id] {
				int sel = -1;
				if (auto lb = ar.GetComponent<ui::ListBox>(named(String("plist_") + bid)); lb.IsSome())
					sel = lb.Unwrap()->selected;
				sdl3::MutexGuard lock(graphMutex);
				auto *n = graph.FindNode(id);
				if (!n)
					return;
				auto &st = std::get<dsp::PlaylistState>(n->state);
				if (sel == -1 && !st.tracks.empty())
					sel = 0;
				if (sel != -1)
					st.pendingLoad = sel;
			});
			auto btnStop = f.Button(u8"■");
			btnStop.W(ui::Dimension::Px(32)).OnClick([&graph, &graphMutex, id] {
				sdl3::MutexGuard lock(graphMutex);
				if (auto *n = graph.FindNode(id)) {
					auto &st = std::get<dsp::PlaylistState>(n->state);
					st.playing = false;
					st.playbackPos = 0;
				}
			});
			auto togAuto = f.Toggle(true);
			togAuto.OnToggle([&graph, &graphMutex, id](bool v) {
				sdl3::MutexGuard lock(graphMutex);
				if (auto *n = graph.FindNode(id))
					std::get<dsp::PlaylistState>(n->state).autoplay = v;
			});
			auto togLoop = f.Toggle(false);
			togLoop.OnToggle([&graph, &graphMutex, id](bool v) {
				sdl3::MutexGuard lock(graphMutex);
				if (auto *n = graph.FindNode(id))
					std::get<dsp::PlaylistState>(n->state).looping = v;
			});
			auto rowCtrl = f.Row();
			rowCtrl.Gap(4.f).Align(ui::CrossAlign::Center).Children(std::move(btnPlay), std::move(btnStop), std::move(togAuto),
																	std::move(togLoop));

			auto lblTime = f.Label("00:00 / 00:00");
			lblTime.Name(String("plist_") + bid + String("_time")).FontSize(10.f);

			content.Children(std::move(lst), std::move(rowTop), std::move(rowCtrl), std::move(lblTime));
			break;
		}
		}

		// ── Spawn du nœud visuel + pins ──────────────────────────────────────
		ecs::Entity nodeE = ui::AddGraphNode(f, canvasE, String(title), pos, size, std::move(content), headerColor);
		for (int i = 0; i < numIn; ++i) {
			float off = float(i + 1) / float(numIn + 1);
			ecs::Entity pin = ui::AddGraphPin(ar, nodeE, ui::PinSide::Left, off, ui::PinShape::CIRCLE,
											 sdl3::FColor::UI_ACCENT_BLUE_PRIMARY());
			pinInfo[entityKey(pin)] = {id, i, false};
		}
		for (int i = 0; i < numOut; ++i) {
			float off = float(i + 1) / float(numOut + 1);
			ecs::Entity pin = ui::AddGraphPin(ar, nodeE, ui::PinSide::Right, off, ui::PinShape::TRIANGLE,
											 sdl3::FColor{45/255.f, 195/255.f, 110/255.f, 255/255.f});
			pinInfo[entityKey(pin)] = {id, i, true};
		}
		// Contournement d'un bug confirmé de ui::nodegraph.hpp (cf. mémoire du
		// projet) : un nœud sans AUCUN pin sur un côté (Gauche OU Droite) voit
		// son PROPRE UiComputed.screen mal résolu par LayoutSystem (largeur/
		// hauteur en pixels explicites totalement ignorées, remplacées par des
		// valeurs minuscules) — reproduit même en gardant strictement le même
		// contenu, isolé au SEUL fait que `numIn==0` ou `numOut==0`. Un pin
		// invisible (`connectable=false`, alpha 0) sur le côté manquant
		// contourne le bug sans changer le nombre de ports DSP réels.
		if (numIn == 0) {
			ecs::Entity spacer = ui::AddGraphPin(ar, nodeE, ui::PinSide::Left, 0.5f, ui::PinShape::CIRCLE,
												sdl3::FColor{0, 0, 0, 0});
			if (auto p = ar.GetComponent<ui::UiGraphPin>(spacer); p.IsSome())
				p.Unwrap()->connectable = false;
		}
		if (numOut == 0) {
			ecs::Entity spacer = ui::AddGraphPin(ar, nodeE, ui::PinSide::Right, 0.5f, ui::PinShape::CIRCLE,
												sdl3::FColor{0, 0, 0, 0});
			if (auto p = ar.GetComponent<ui::UiGraphPin>(spacer); p.IsSome())
				p.Unwrap()->connectable = false;
		}
		nodeEntityToId[entityKey(nodeE)] = id;
		idToNodeEntity[id] = nodeE;
		if (type == dsp::BlockType::SCOPE)
			nodePlotEntity[id] = named(String("scope_") + bid + String("_plot"));
		else if (type == dsp::BlockType::SPECTRUM)
			nodePlotEntity[id] = named(String("spec_") + bid + String("_plot"));
		layout.MarkDirty();
		// Résolution synchrone immédiate (pas juste markDirty + attendre la
		// prochaine frame) : un nœud nouvellement ajouté doit avoir un
		// UiComputed.screen correct dès CETTE frame, avant le premier rendu.
		nodeGraph.Prepass(ar);
		layout.Run(ar, float(window.GetSize().x), float(window.GetSize().y));
		nodeGraph.UpdatePins(ar);
	};

	// ── Barre d'outils "Ajouter un bloc" ─────────────────────────────────────
	auto toolbar = f.Row();
	toolbar.Anchor(ui::Anchor::TopLeft).Gap(4.f).Pad(4.f).W(ui::Dimension::Rpct(100.f)).H(ui::Dimension::Px(36)).Bg({16, 18, 28});
	struct ToolbarBtn {
		const char *label;
		dsp::BlockType type;
	};
	static constexpr ToolbarBtn K_TOOLBAR_BTNS[] = {
		{"+ Audio In", dsp::BlockType::AUDIO_IN}, {"+ Audio Out", dsp::BlockType::AUDIO_OUT},
		{"+ Filter", dsp::BlockType::FILTER},    {"+ Amp", dsp::BlockType::AMP},
		{"+ Mixer", dsp::BlockType::MIXER},      {"+ Delay", dsp::BlockType::DELAY},
		{"+ Oscillator", dsp::BlockType::OSC},   {"+ Scope", dsp::BlockType::SCOPE},
		{"+ Spectrum", dsp::BlockType::SPECTRUM}, {"+ Playlist", dsp::BlockType::PLAYLIST},
	};
	for (auto &tb : K_TOOLBAR_BTNS) {
		auto btn = f.Button(String(tb.label));
		dsp::BlockType t = tb.type;
		btn.FontSize(11.f).OnClick([&addBlock, t] { addBlock(t); });
		toolbar.Children(std::move(btn));
	}
	toolbar.Spawn();

	// ── Menu contextuel (clic droit) : supprimer un nœud / déconnecter un pin ──
	auto rightClickedNode = std::make_shared<ecs::Entity>();
	auto rightClickedPin = std::make_shared<ecs::Entity>();

	auto delBtn = f.Button("Supprimer le bloc");
	delBtn.OnClick([&, rightClickedNode] {
		if (!rightClickedNode->Valid())
			return;
		auto it = nodeEntityToId.find(entityKey(*rightClickedNode));
		if (it != nodeEntityToId.end()) {
			int id = it->second;
			sdl3::MutexGuard lock(graphMutex);
			graph.nodes.erase(std::remove_if(graph.nodes.begin(), graph.nodes.end(),
											 [id](const dsp::Node &n) { return n.id == id; }),
							  graph.nodes.end());
			graph.connections.erase(std::remove_if(graph.connections.begin(), graph.connections.end(),
												   [id](const dsp::Connection &c) { return c.fromId == id || c.toId == id; }),
									graph.connections.end());
			idToNodeEntity.erase(id);
			nodePlotEntity.erase(id);
		}
		ui::DestroyGraphNode(ar, *rightClickedNode);
		*rightClickedNode = ecs::Entity{};
		layout.MarkDirty();
	});
	auto discBtn = f.Button(u8"Déconnecter");
	discBtn.OnClick([&, rightClickedPin] {
		if (!rightClickedPin->Valid())
			return;
		if (auto cv = ar.GetComponent<ui::UiNodeGraphCanvas>(canvasE); cv.IsSome()) {
			auto &conns = cv.Unwrap()->connections;
			conns.erase(std::remove_if(conns.begin(), conns.end(),
									   [&](const ui::GraphConnection &c) {
										   return c.fromPin == *rightClickedPin || c.toPin == *rightClickedPin;
									   }),
					   conns.end());
		}
		*rightClickedPin = ecs::Entity{};
		resyncConnections();
	});
	auto ctxMenu = f.Popup();
	ctxMenu.Gap(2.f).Pad(4.f).Children(std::move(delBtn), std::move(discBtn));
	ecs::Entity ctxMenuE = ctxMenu.Spawn();

	ui::NodeGraphCallbacks cb;
	cb.onContextMenu = [&, ctxMenuE, rightClickedNode, rightClickedPin](ecs::Entity node, ecs::Entity pin, sdl3::FPoint screenPos) {
		*rightClickedNode = node;
		*rightClickedPin = pin;
		if (auto rect = ar.GetComponent<ui::UiRect>(ctxMenuE); rect.IsSome())
			rect.Unwrap()->offset = {screenPos.x, screenPos.y};
		ui::OpenPopup(ar, layout, ctxMenuE, ctxMenuE);
	};
	ar.AddComponent(canvasE, std::move(cb));

	// ── Thread audio de fond (Décision #7 : ne touche QUE `graph`, jamais `ar`) ──
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
		uint64_t now = sdl3::GetTicksMS();
		float dt = float(now - lastTick) / 1000.f;
		lastTick = now;

		nodeGraph.Prepass(ar);
		layout.RunIfNeeded(ar, float(window.GetSize().x), float(window.GetSize().y));
		nodeGraph.UpdatePins(ar);

		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			if (e.IsQuit()) {
				running = false;
				break;
			}
			if (e.IsKeyDown(SDLK_ESCAPE) && !input.HasOpenModal()) {
				running = false;
				break;
			}
			input.HandleEvent(ar, e, layout);
			nodeGraph.HandleEvent(ar, e, layout);
		}
		input.Tick(ar, layout, dt);
		nodeGraph.Tick(ar, layout, dt);

		// Topologie : re-dérive `graph.connections` des `GraphConnection`
		// visuelles (l'utilisateur a pu glisser une nouvelle connexion).
		resyncConnections();

		// Chargement différé d'une piste sélectionnée (décodage HORS du
		// verrou audio — un AudioDecoder complet prend plusieurs ms).
		{
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
				bool ok = DecodeToMonoFloat(path, data);
				sdl3::MutexGuard lock(graphMutex);
				if (auto *n = graph.FindNode(id)) {
					auto &st = std::get<dsp::PlaylistState>(n->state);
					st.audioData = ok ? std::move(data) : std::vector<float>{};
					st.playbackPos = 0;
					st.playing = ok;
				}
			}
		}

		// Télémétrie DSP -> UI (Scope/Spectrum/Playlist), sous verrou.
		{
			sdl3::MutexGuard lock(graphMutex);
			for (auto &n : graph.nodes) {
				if (n.type == dsp::BlockType::SCOPE) {
					auto &st = std::get<dsp::ScopeState>(n.state);
					auto it = nodePlotEntity.find(n.id);
					if (it != nodePlotEntity.end() && !st.display.empty()) {
						if (auto p = ar.GetComponent<ui::UiPlot>(it->second); p.IsSome() && !p.Unwrap()->series.empty())
							p.Unwrap()->series[0].SetY(st.display);
					}
				} else if (n.type == dsp::BlockType::SPECTRUM) {
					auto &st = std::get<dsp::SpectrumState>(n.state);
					auto it = nodePlotEntity.find(n.id);
					if (it != nodePlotEntity.end() && !st.displayDb.empty()) {
						auto freqs = audio::ProcessFFTFrequencies(size_t(dsp::K_BUFFER_SIZE), dsp::K_SAMPLE_RATE);
						std::vector<float> y(freqs.size());
						for (size_t k = 0; k < freqs.size() && k < st.displayDb.size(); ++k)
							y[k] = sdl3::Max(-80.f, st.displayDb[k]);
						if (auto p = ar.GetComponent<ui::UiPlot>(it->second); p.IsSome() && !p.Unwrap()->series.empty())
							p.Unwrap()->series[0].SetXy(freqs, y);
					}
				} else if (n.type == dsp::BlockType::PLAYLIST) {
					auto &st = std::get<dsp::PlaylistState>(n.state);
					String bid = String(std::format("{}", n.id).c_str());
					if (auto lb = ar.GetComponent<ui::ListBox>(named(String("plist_") + bid)); lb.IsSome()) {
						std::vector<String> names;
						names.reserve(st.tracks.size());
						for (auto &t : st.tracks)
							names.push_back(FileNameOf(t));
						if (lb.Unwrap()->items.size() != names.size())
							lb.Unwrap()->items = names;
					}
					float durSec = st.audioData.empty() ? 0.f : float(st.audioData.size()) / float(dsp::K_SAMPLE_RATE);
					float posSec = float(st.playbackPos) / float(dsp::K_SAMPLE_RATE);
					setLabel(named(String("plist_") + bid + String("_time")),
							 String(std::format("{:02.0f}:{:02.0f} / {:02.0f}:{:02.0f}", sdl3::Floor(posSec / 60.f),
												sdl3::Fmod(posSec, 60.f), sdl3::Floor(durSec / 60.f), sdl3::Fmod(durSec, 60.f))
										.c_str()));
				}
			}
		}

		style.Resolve(ar);
		layout.RunIfNeeded(ar, float(window.GetSize().x), float(window.GetSize().y));

		ren.SetDrawColor(sdl3::FColor::UI_WINDOW_BG());
		ren.Clear();
		nodeGraph.RenderGrid(ar, renBackend);
		nodeGraph.RenderConnections(ar, renBackend);
		render.Run(ar, renBackend, &input.tooltip);
		nodeGraph.RenderHeaders(ar, renBackend);
		nodeGraph.RenderPins(ar, renBackend);
		nodeGraph.RenderMarquee(renBackend);
		ren.Present();
	}

	audioRunning.Store(0);
	audioThread.Wait();
	return 0;
}
