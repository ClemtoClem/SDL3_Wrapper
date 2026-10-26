/**
 * audio_signal_generator — 4 oscillateurs paramétrables, ré-implémenté sur
 * les modules `audio::` (lib/audio/dsp.hpp), `sdl3::` (device enumeration +
 * AudioStream::Queued(), Phase 3) et `ui::` (deux graphiques ui::Plot + des
 * ui::Progress pour les VU-mètres) de cette librairie — pas un port littéral de SDL3pp/
 * examples/audio/06_signal_generator.cpp (widgets/API différents), même
 * comportement : 4 cartes oscillateur (forme/fréquence/amplitude/on-off),
 * volume maître, forme d'onde composite + spectre, VU-mètres RMS/Peak.
 * La synthèse par accumulateur de phase (Oscillator::nextSample) est portée
 * directement — c'est de la synthèse harmonique générique, indépendante de
 * SDL3pp comme de ce projet.
 *
 *   cpp-maker run audio_signal_generator
 */
#include <cstdlib>
#include <deque>
#include <format>
#include <iostream>

#include <audio/dsp.hpp>
#include <core/core.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>

static constexpr int WIN_W = 1280;
static constexpr int WIN_H = 860; // 800 de contenu + barres de titre et d'état

static constexpr float K_SAMPLE_RATE = 44100.f;
static constexpr int K_BLOCK_SIZE = 1024; // puissance de 2 : consommable tel quel par ProcessFFT()
static constexpr float K_BUFFER_SEC = 0.10f;
static constexpr size_t K_SPEC_BUF_SIZE = 4096;
static constexpr int K_NUM_OSC = 4;
static constexpr float K_MAX_OSC_FREQ = 10000.0;

enum class OscShape { SINE = 0, SQUARE, TRIANGLE, SAWTOOTH, NOISE, COUNT };
static constexpr int K_NUM_SHAPES = int(OscShape::COUNT);
static constexpr const char *K_SHAPE_LABELS[K_NUM_SHAPES] = {"Sine", "Carré", "Tri", "Dent", "Bruit"};

// Feuille de l'exemple : forme active, cartes, VU-mètres.
static constexpr const char *GENERATOR_CSS = R"css(
#sidebar { width: 320px; padding: 12px; gap: 8px; background-color: #10121c; overflow-y: auto; }
#main { flex-grow: 1; padding: 14px; gap: 10px; }
.osc-card { padding: 10px; gap: 6px; background-color: #141622; border-radius: 6px; width: 100%; }
.osc-title { flex-grow: 1; font-size: 13px; }
.shape { flex-grow: 1; font-size: 10px; padding: 3px 4px; }
.shape.active { background-color: var(--accent); color: #f0f4fa; }
.shape.active:hover { background-color: #55a5f5; }
.small { font-size: 11px; }
.meter-label { width: 140px; font-size: 12px; }
.meter { flex-grow: 1; height: 10px; }
#prog-rms .progress-fill { background-color: #46d28c; }
#prog-peak .progress-fill { background-color: #dc5a50; }
#status { color: #8c92a8; font-size: 11px; }
plot { width: 100%; height: 260px; }
)css";

// ============================================================================
// Oscillateur — synthèse par accumulateur de phase, harmoniques limitées en
// bande (séries de Fourier tronquées à 31/32 harmoniques) pour Square/
// Triangle/Sawtooth, portée directement de SDL3pp/examples/audio/
// 06_signal_generator.cpp (math générique, aucune dépendance SDL3pp).
// ============================================================================
struct Oscillator {
	bool enabled = false;
	OscShape shape = OscShape::SINE;
	float freq = 440.f;
	float amplitude = 0.5f;
	float phase = 0.f; // radians, [0, 2π)

	float NextSample(float sr) noexcept {
		if (!enabled)
			return 0.f;
		float s = Wave(shape, phase);
		phase += 2.f * sdl3::PI_F * freq / sr;
		if (phase >= 2.f * sdl3::PI_F)
			phase -= 2.f * sdl3::PI_F;
		return s * amplitude;
	}

	void ResetPhase() noexcept { phase = 0.f; }

private:
	static float Wave(OscShape sh, float p) noexcept {
		switch (sh) {
		case OscShape::SINE:
			return sdl3::Sin(p);
		case OscShape::SQUARE: {
			float s = 0.f;
			for (int h = 1; h <= 31; h += 2)
				s += (1.f / float(h)) * sdl3::Sin(float(h) * p);
			return s * (4.f / sdl3::PI_F);
		}
		case OscShape::TRIANGLE: {
			float s = 0.f, sgn = 1.f;
			for (int h = 1; h <= 31; h += 2, sgn = -sgn)
				s += sgn / float(h * h) * sdl3::Sin(float(h) * p);
			return s * (8.f / (sdl3::PI_F * sdl3::PI_F));
		}
		case OscShape::SAWTOOTH: {
			float s = 0.f;
			for (int h = 1; h <= 32; ++h)
				s += ((h & 1) ? -1.f : 1.f) / float(h) * sdl3::Sin(float(h) * p);
			return s * (2.f / sdl3::PI_F);
		}
		case OscShape::NOISE:
			return (float(std::rand()) / float(RAND_MAX)) * 2.f - 1.f;
		default:
			return 0.f;
		}
	}
};

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
	auto winRes = sdl3::Window::Create(u8"audio:: - Générateur de signal", WIN_W, WIN_H, ui::WindowFrame::WINDOW_FLAGS);
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
	gui.AddStyleSheet(GENERATOR_CSS);

	// ── État ──────────────────────────────────────────────────────────────────
	Oscillator osc[K_NUM_OSC];
	osc[0] = {false, OscShape::SINE, 261.63f, 0.5f, 0.f};
	osc[1] = {false, OscShape::TRIANGLE, 329.63f, 0.5f, 0.f};
	osc[2] = {false, OscShape::SQUARE, 392.00f, 0.4f, 0.f};
	osc[3] = {false, OscShape::SAWTOOTH, 220.00f, 0.3f, 0.f};

	float master = 0.7f;
	std::vector<sdl3::AudioDevice> devices = sdl3::EnumeratePlaybackDevices();
	sdl3::AudioStream stream;

	std::deque<float> specBuf;
	std::vector<float> lastBlock;

	ui::Widget lblStatus, lblMaster, plotWave, plotSpec, progRms, progPeak, lblRms, lblPeak, lbDevices;
	ui::Widget oscShapeBtns[K_NUM_OSC][K_NUM_SHAPES], oscLblFreq[K_NUM_OSC], oscLblAmp[K_NUM_OSC];

	auto setStatus = [&](String msg) { lblStatus.SetText(std::move(msg)); };

	auto closeDevice = [&] {
		if (stream) {
			stream.Pause();
			stream = sdl3::AudioStream();
		}
	};
	// idx < 0 : périphérique par défaut.
	auto openDevice = [&](int idx) {
		closeDevice();
		const bool byDefault = idx < 0 || idx >= int(devices.size());
		sdl3::AudioSpec spec{sdl3::AudioFormat::F32, 1, int(K_SAMPLE_RATE)};
		auto res = byDefault ? sdl3::AudioStream::OpenPlayback(spec)
							 : sdl3::AudioStream::OpenPlayback(spec, devices[size_t(idx)].id);
		if (!res) {
			setStatus(String("Erreur périphérique : ") + String(res.Error()));
			return;
		}
		stream = std::move(res.Value());
		stream.Resume();
		setStatus(byDefault ? String("Sortie : périphérique par défaut")
							: String("Sortie : ") + devices[size_t(idx)].name);
	};
	auto deviceNames = [&] {
		std::vector<String> names{String("Défaut système")};
		for (auto &d : devices)
			names.push_back(d.name);
		return names;
	};
	auto refreshDevices = [&] {
		devices = sdl3::EnumeratePlaybackDevices();
		lbDevices.SetItems(deviceNames());
		lbDevices.SetSelected(0);
	};
	auto selectShape = [&](int idx, int shape) {
		osc[idx].shape = OscShape(shape);
		for (int s = 0; s < K_NUM_SHAPES; ++s)
			if (s == shape)
				oscShapeBtns[idx][s].AddClass("active");
			else
				oscShapeBtns[idx][s].RemoveClass("active");
	};

	// ── Construction UI ──────────────────────────────────────────────────────
	auto oscCard = [&](int idx) -> ui::AnyWidget {
		const Oscillator &o = osc[idx];
		std::vector<ui::AnyWidget> shapes;
		for (int s = 0; s < K_NUM_SHAPES; ++s) {
			ui::WidgetBuilder button = ui::Button(K_SHAPE_LABELS[s]);
			button.Class("shape").Ref(oscShapeBtns[idx][s]).OnClick([&selectShape, idx, s] { selectShape(idx, s); });
			if (s == int(o.shape))
				button.Class("active");
			shapes.push_back(button);
		}
		return ui::Column().Class("osc-card").Children({
			ui::Row().Children({
				ui::Label(std::format("Oscillateur {}", idx + 1)).Class("osc-title"),
				ui::Switch(false).OnToggle([&osc, idx](bool on) {
					osc[idx].enabled = on;
					if (on)
						osc[idx].ResetPhase();
				}),
			}),
			ui::Row().Gap(3.f).Children(std::move(shapes)),
			ui::Label(std::format("Fréq : {:.0f} Hz", o.freq)).Class("small").Ref(oscLblFreq[idx]),
			ui::Slider(20.f, K_MAX_OSC_FREQ, o.freq, 1.f).FillWidth().OnChange([&, idx](float v) {
				osc[idx].freq = v;
				oscLblFreq[idx].SetText(std::format("Fréq : {:.0f} Hz", v));
			}),
			ui::Label(std::format("Amp : {:.2f}", o.amplitude)).Class("small").Ref(oscLblAmp[idx]),
			ui::Slider(0.f, 1.f, o.amplitude, 0.01f).FillWidth().OnChange([&, idx](float v) {
				osc[idx].amplitude = v;
				oscLblAmp[idx].SetText(std::format("Amp : {:.2f}", v));
			}),
		});
	};

	ui::WindowFrame frame;
	frame.Build(gui, window,
				{.title = "audio:: - Générateur de signal", .appIcon = Some(ui::MaterialIcons::GRAPHIC_EQ),
				 .status = "Quatre oscillateurs mixés vers la sortie audio."});

	const std::vector<float> initWave(size_t(K_BLOCK_SIZE), 0.f);
	const std::vector<float> initSpecX(K_SPEC_BUF_SIZE / 2, 0.f), initSpecY(K_SPEC_BUF_SIZE / 2, -80.f);
	ui::PlotBuilder waveBuilder = ui::Plot();
	waveBuilder.AddLineSeries(initWave, "", Some(ui::Rgba(80, 200, 120)))
		.XRange(0.f, float(K_BLOCK_SIZE))
		.YRange(-1.f, 1.f)
		.ShowLegend(false)
		.Ref(plotWave);
	ui::PlotBuilder specBuilder = ui::Plot();
	specBuilder.AddAreaSeries(initSpecX, initSpecY, "", Some(ui::Rgba(90, 180, 255)))
		.XRange(0.f, K_MAX_OSC_FREQ)
		.YRange(-80.f, 0.f)
		.ShowLegend(false)
		.Ref(plotSpec);

	frame.Content().Append(ui::Row().Grow().FillWidth().AlignItems(ui::Align::Stretch).Children({
		ui::Column().Id("sidebar").Children({
			ui::Label("Sortie audio").FontSize(15.f),
			ui::ListBox(deviceNames(), 0).Height(90.f).FillWidth().Ref(lbDevices).OnSelect([&](int i) {
				openDevice(i - 1);
			}),
			ui::Button("Rafraîchir").FillWidth().OnClick(refreshDevices),
			oscCard(0),
			oscCard(1),
			oscCard(2),
			oscCard(3),
			ui::Label("Défaut système actif").Id("status").Ref(lblStatus),
		}),
		ui::Column().Id("main").Children({
			ui::Row().Children({
				ui::Label(std::format("Volume : {:.0f} %", master * 100.f)).Class("meter-label").Ref(lblMaster),
				ui::Slider(0.f, 1.f, master, 0.01f).Grow().OnChange([&](float v) {
					master = v;
					lblMaster.SetText(std::format("Volume : {:.0f} %", v * 100.f));
				}),
			}),
			ui::Label("Forme d'onde — signal composite").FontSize(14.f),
			waveBuilder,
			ui::Label("Spectre FFT (Hanning)").FontSize(14.f),
			specBuilder,
			ui::Row().Children({ui::Label("RMS  -80.0 dB").Class("meter-label").Ref(lblRms),
								ui::Progress(0.f, 1.f, 0.f).Id("prog-rms").Class("meter").Ref(progRms)}),
			ui::Row().Children({ui::Label("Peak -80.0 dB").Class("meter-label").Ref(lblPeak),
								ui::Progress(0.f, 1.f, 0.f).Id("prog-peak").Class("meter").Ref(progPeak)}),
		}),
	}));

	openDevice(-1);

	// ── Boucle ───────────────────────────────────────────────────────────────
	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();
	while (running) {
		const uint64_t now = sdl3::GetTicksMS();
		const float dt = float(now - lastTick) / 1000.f;
		lastTick = now;

		// ── Génération audio : remplit le flux tant qu'il reste sous le seuil ──
		if (stream) {
			const int minQueuedBytes = int(K_SAMPLE_RATE * K_BUFFER_SEC * sizeof(float));
			while (stream.Queued() < minQueuedBytes) {
				std::vector<float> block(size_t(K_BLOCK_SIZE), 0.f);
				for (int i = 0; i < K_BLOCK_SIZE; ++i) {
					float s = 0.f;
					for (auto &o : osc)
						s += o.NextSample(K_SAMPLE_RATE);
					block[size_t(i)] = s * master;
				}
				audio::SoftClip(block, 0.98f);
				lastBlock = block;
				for (float v : block) {
					specBuf.push_back(v);
					if (specBuf.size() > K_SPEC_BUF_SIZE)
						specBuf.pop_front();
				}
				stream.PutData(std::span<const float>(block));
			}
		}

		// ── Affichage : forme d'onde + spectre + VU-mètres ───────────────────
		if (!lastBlock.empty()) {
			if (auto p = plotWave.Get<ui::PlotModel>(); p.IsSome() && !p.Unwrap()->series.empty())
				p.Unwrap()->series[0].SetY(lastBlock);
			const float rms = audio::GetRMS(lastBlock);
			const float peak = audio::GetPeak(lastBlock);
			const float rmsDb = rms > 1e-7f ? 20.f * sdl3::Log10(rms) : -80.f;
			const float peakDb = peak > 1e-7f ? 20.f * sdl3::Log10(peak) : -80.f;
			progRms.SetValue(sdl3::Clamp((rmsDb + 80.f) / 80.f, 0.f, 1.f));
			progPeak.SetValue(sdl3::Clamp((peakDb + 80.f) / 80.f, 0.f, 1.f));
			lblRms.SetText(std::format("RMS  {:+.1f} dB", rmsDb));
			lblPeak.SetText(std::format("Peak {:+.1f} dB", peakDb));
		}
		if (specBuf.size() >= 512) {
			const size_t sn = sdl3::Min(specBuf.size(), K_SPEC_BUF_SIZE);
			size_t fftLen = 1;
			while (fftLen * 2 <= sn)
				fftLen *= 2;
			if (fftLen >= 2) {
				std::vector<float> windowed(specBuf.end() - long(fftLen), specBuf.end());
				audio::WindowHann(windowed);
				auto spectrum = audio::ProcessFFT(windowed);
				auto magDb = audio::ProcessFFTMagnitudeDb(spectrum);
				auto freqs = audio::ProcessFFTFrequencies(windowed.size(), int(K_SAMPLE_RATE));
				std::vector<float> specX, specY;
				for (size_t k = 0; k < freqs.size() && freqs[k] <= 20000.f; ++k) {
					specX.push_back(freqs[k]);
					specY.push_back(sdl3::Max(-80.f, magDb[k]));
				}
				if (auto p = plotSpec.Get<ui::PlotModel>(); p.IsSome() && !p.Unwrap()->series.empty())
					p.Unwrap()->series[0].SetXy(specX, specY);
			}
		}

		while (auto ev = sdl3::PollEvent()) {
			auto &e = ev.Value();
			if (e.IsQuit() || e.IsKeyDown(SDLK_ESCAPE)) {
				running = false;
				break;
			}
			gui.HandleEvent(e);
		}
		gui.Update(dt);
		frame.Update();
		if (frame.CloseRequested())
			running = false;

		ren.SetDrawColor(gui.CurrentPalette().appBg);
		ren.Clear();
		gui.Render();
		ren.Present();
	}
	closeDevice();
	return 0;
}
