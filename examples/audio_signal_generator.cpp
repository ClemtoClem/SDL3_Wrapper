/**
 * audio_signal_generator — 4 oscillateurs paramétrables, ré-implémenté sur
 * les modules `audio::` (lib/audio/dsp.hpp), `sdl3::` (device enumeration +
 * AudioStream::Queued(), Phase 3) et `ui::` (deux ui::UiPlot + f.Progress
 * pour les VU-mètres) de cette librairie — pas un port littéral de SDL3pp/
 * examples/audio/06_signal_generator.cpp (widgets/API différents), même
 * comportement : 4 cartes oscillateur (forme/fréquence/amplitude/on-off),
 * volume maître, forme d'onde composite + spectre, VU-mètres RMS/Peak.
 * La synthèse par accumulateur de phase (Oscillator::nextSample) est portée
 * directement — c'est de la synthèse harmonique générique, indépendante de
 * SDL3pp comme de ce projet.
 *
 *   make examples && ./build/examples/audio_signal_generator
 */
#include <cstdlib>
#include <deque>
#include <format>
#include <iostream>

#include "audio/dsp.hpp"
#include "core/core.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

static constexpr int WIN_W = 1280;
static constexpr int WIN_H = 860; // 800 de contenu + barres de titre et d'état
static constexpr float FONT_PT = 14.f;

static constexpr float K_SAMPLE_RATE = 44100.f;
static constexpr int K_BLOCK_SIZE = 1024; // puissance de 2 : consommable tel quel par ProcessFFT()
static constexpr float K_BUFFER_SEC = 0.10f;
static constexpr size_t K_SPEC_BUF_SIZE = 4096;
static constexpr int K_NUM_OSC = 4;
static constexpr float K_MAX_OSC_FREQ = 10000.0;

enum class OscShape { SINE = 0, SQUARE, TRIANGLE, SAWTOOTH, NOISE, COUNT };
static constexpr int K_NUM_SHAPES = int(OscShape::COUNT);
static constexpr const char *K_SHAPE_LABELS[K_NUM_SHAPES] = {"Sine", "Carré", "Tri", "Dent", "Bruit"};

static const sdl3::FColor K_ACTIVE_BG = sdl3::FColor::UI_ACCENT_BLUE_PRIMARY();
static const sdl3::FColor K_ACTIVE_BG_HOVER{85 / 255.f, 165 / 255.f, 245 / 255.f, 1.f};
static const sdl3::FColor K_ACTIVE_TEXT{240 / 255.f, 244 / 255.f, 250 / 255.f, 1.f};

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
	auto fontRes = sdl3::Font::FindLocal({"DejaVuSans", "FreeSans", "Arial"}, FONT_PT);
	if (!fontRes) {
		std::cerr << "Font: " << fontRes.Error().CStr() << "\n";
		return 1;
	}
	auto &font = fontRes.Value();

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
	auto engRes = sdl3::TextEngine::Create(ren);
	if (!engRes) {
		std::cerr << "TextEngine: " << engRes.Error().CStr() << "\n";
		return 1;
	}
	auto &eng = engRes.Value();

	ecs::ArchetypeRegistry ar;
	ui::Ui gui(ar, window, ren);
	ui::LayoutSystem &layout = gui.Layout();
	ui::UiFactory &f = gui.Factory();

	gui.SetTextEngine(eng, font);
	layout.measureText = [&font](const String &s, float fs) -> sdl3::FPoint {
		if (auto sz = font.Measure(s); sz.IsSome())
			return {float(sz.Unwrap().x) * (fs / FONT_PT), fs * 1.35f};
		return {float(s.size()) * fs * 0.55f, fs * 1.3f};
	};
	gui.CreateStyleClass("preset-active", ui::UiStyle{}
											   .SetBg(K_ACTIVE_BG)
											   .SetBgHovered(K_ACTIVE_BG_HOVER)
											   .SetTextColor(K_ACTIVE_TEXT));

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

	// ── État ──────────────────────────────────────────────────────────────────
	Oscillator osc[K_NUM_OSC];
	osc[0] = {false, OscShape::SINE, 261.63f, 0.5f, 0.f};
	osc[1] = {false, OscShape::TRIANGLE, 329.63f, 0.5f, 0.f};
	osc[2] = {false, OscShape::SQUARE, 392.00f, 0.4f, 0.f};
	osc[3] = {false, OscShape::SAWTOOTH, 220.00f, 0.3f, 0.f};

	float master = 0.7f;
	std::vector<sdl3::AudioDevice> devices = sdl3::EnumeratePlaybackDevices();
	sdl3::AudioStream stream;
	int selectedDevice = -1; // -1 = périphérique par défaut

	std::deque<float> specBuf;
	std::vector<float> lastBlock;

	ecs::Entity lblStatus, lblMaster, plotWave, plotSpec, progRms, progPeak, lblRms, lblPeak;
	ecs::Entity oscToggle[K_NUM_OSC], oscShapeBtns[K_NUM_OSC][K_NUM_SHAPES], oscLblFreq[K_NUM_OSC], oscLblAmp[K_NUM_OSC];

	auto setStatus = [&](String msg) { setLabel(lblStatus, std::move(msg)); };

	auto closeDevice = [&] {
		if (stream) {
			stream.Pause();
			stream = sdl3::AudioStream();
		}
		selectedDevice = -1;
	};

	auto openDefaultDevice = [&] {
		closeDevice();
		sdl3::AudioSpec spec{sdl3::AudioFormat::F32, 1, int(K_SAMPLE_RATE)};
		auto res = sdl3::AudioStream::OpenPlayback(spec);
		if (!res) {
			setStatus(String("Erreur périphérique : ") + String(res.Error()));
			return;
		}
		stream = std::move(res.Value());
		stream.Resume();
		setStatus(String("Sortie : périphérique par défaut"));
	};

	auto openDevice = [&](int idx) {
		closeDevice();
		if (idx < 0 || idx >= int(devices.size())) {
			openDefaultDevice();
			return;
		}
		selectedDevice = idx;
		sdl3::AudioSpec spec{sdl3::AudioFormat::F32, 1, int(K_SAMPLE_RATE)};
		auto res = sdl3::AudioStream::OpenPlayback(spec, devices[size_t(idx)].id);
		if (!res) {
			setStatus(String("Erreur périphérique : ") + String(res.Error()));
			return;
		}
		stream = std::move(res.Value());
		stream.Resume();
		setStatus(String("Sortie : ") + devices[size_t(idx)].name);
	};

	auto refreshDevices = [&] {
		devices = sdl3::EnumeratePlaybackDevices();
		std::vector<String> names;
		names.reserve(devices.size() + 1);
		names.push_back(String("Défaut système"));
		for (auto &d : devices)
			names.push_back(d.name);
		if (auto lb = ar.GetComponent<ui::ListBox>(named("lbDevices")); lb.IsSome()) {
			lb.Unwrap()->items = std::move(names);
			lb.Unwrap()->selected = 0;
			layout.MarkDirty();
		}
	};

	auto selectActive = [&](std::span<ecs::Entity> group, int idx) {
		for (size_t i = 0; i < group.size(); ++i) {
			if (!group[i].Valid())
				continue;
			if (int(i) == idx)
				ui::AddClass(ar, group[i], String("preset-active"));
			else
				ui::RemoveClass(ar, group[i], String("preset-active"));
		}
	};

	// ── Construction UI ──────────────────────────────────────────────────────
	auto buildOscCard = [&](int idx) -> ui::WidgetBuilder {
		String idBase = String(std::format("osc{}", idx).c_str());
		Oscillator &o = osc[idx];

		auto card = f.Column();
		card.Gap(6.f).Pad(10.f).Bg(sdl3::Color{20, 22, 34}).W(ui::Dimension::Pct(100));

		auto hdr = f.Row();
		hdr.Gap(6.f).Align(ui::CrossAlign::Center);
		auto title = f.Label(String(std::format("Oscillateur {}", idx + 1).c_str()));
		title.GrowW().FontSize(13.f);
		auto tog = f.Toggle(false);
		tog.Name(idBase + String("_tog")).Size(40.f, 20.f).OnToggle([&osc, idx](bool on) {
			osc[idx].enabled = on;
			if (on)
				osc[idx].ResetPhase();
		});
		hdr.Children(std::move(title), std::move(tog));

		std::vector<ui::WidgetBuilder> shapeBtns;
		for (int s = 0; s < K_NUM_SHAPES; ++s) {
			auto btn = f.Button(String(K_SHAPE_LABELS[s]));
			btn.Name(idBase + String(std::format("_sh{}", s).c_str())).GrowW().FontSize(10.f).OnClick([&osc, idx, s, &selectActive, &oscShapeBtns] {
				osc[idx].shape = OscShape(s);
				selectActive(oscShapeBtns[idx], s);
			});
			shapeBtns.push_back(std::move(btn));
		}

		auto lblFreq = f.Label(String(std::format("Fréq : {:.0f} Hz", o.freq).c_str()));
		lblFreq.Name(idBase + String("_flbl")).FontSize(11.f);
		auto sldFreq = f.Slider(20.f, K_MAX_OSC_FREQ, o.freq, 1.f);
		sldFreq.GrowW().H(ui::Dimension::Px(16)).OnChange([&, idx](float v) {
			osc[idx].freq = v;
			setLabel(oscLblFreq[idx], String(std::format("Fréq : {:.0f} Hz", v).c_str()));
		});

		auto lblAmp = f.Label(String(std::format("Amp : {:.2f}", o.amplitude).c_str()));
		lblAmp.Name(idBase + String("_albl")).FontSize(11.f);
		auto sldAmp = f.Slider(0.f, 1.f, o.amplitude, 0.01f);
		sldAmp.GrowW().H(ui::Dimension::Px(16)).OnChange([&, idx](float v) {
			osc[idx].amplitude = v;
			setLabel(oscLblAmp[idx], String(std::format("Amp : {:.2f}", v).c_str()));
		});

		auto shapeRowWithKids = f.Row();
		shapeRowWithKids.Gap(3.f);
		for (auto &b : shapeBtns)
			shapeRowWithKids.Children(std::move(b));

		card.Children(std::move(hdr), std::move(shapeRowWithKids), std::move(lblFreq), std::move(sldFreq),
					  std::move(lblAmp), std::move(sldAmp));
		return card;
	};

	// ── Encadrement de fenêtre : barre de titre, corps en deux colonnes,
	// barre d'état avec poignée de redimensionnement ─────────────────────────
	ui::WindowFrame frame;
	frame.Build(gui, window, {.title = "audio:: - Générateur de signal", .appIcon = Some(ui::MaterialIcons::GRAPHIC_EQ),
							  .status = "Trois oscillateurs mixés vers la sortie audio."});
	auto body = f.Row();
	body.GrowW().GrowH().Parent(frame.Content());
	ecs::Entity bodyE = body.Spawn();

	auto left = f.Column();
	left.Gap(8.f).Pad(12.f).W(ui::Dimension::Px(320)).GrowH().Bg(sdl3::Color{16, 18, 28}).Parent(bodyE);

	std::vector<String> deviceNames;
	deviceNames.push_back(String("Défaut système"));
	for (auto &d : devices)
		deviceNames.push_back(d.name);

	auto lbDevices = f.Listbox(deviceNames, 0);
	lbDevices.Name("lbDevices").H(ui::Dimension::Px(90)).GrowW().OnChange([&](float idx) {
		int i = int(idx) - 1;
		if (i < 0)
			openDefaultDevice();
		else
			openDevice(i);
	});
	auto btnRefresh = f.Button("Rafraîchir");
	btnRefresh.GrowW().OnClick(refreshDevices);

	left.Children(f.Label("Sortie audio").FontSize(15.f), std::move(lbDevices), std::move(btnRefresh),
				  buildOscCard(0), buildOscCard(1), buildOscCard(2), buildOscCard(3),
				  f.Label("Défaut système actif").Name("lblStatus").TextColor(sdl3::Color{140, 146, 168}).FontSize(11.f));
	left.Spawn();

	std::vector<float> initWave(size_t(K_BLOCK_SIZE), 0.f);
	auto plotWaveB = f.Plot("");
	plotWaveB.AddLineSeries(initWave, "", Some(sdl3::FColor::UI_ACCENT_GREEN()));
	plotWaveB.Name("plotWave").GrowW().H(ui::Dimension::Px(260));

	std::vector<float> initSpecX(K_SPEC_BUF_SIZE / 2, 0.f), initSpecY(K_SPEC_BUF_SIZE / 2, -80.f);
	auto plotSpecB = f.Plot("");
	plotSpecB.AddAreaSeries(initSpecX, initSpecY, "", Some(sdl3::FColor::UI_ACCENT_BLUE_SKY()));
	plotSpecB.Name("plotSpec").GrowW().H(ui::Dimension::Px(260));

	auto masterRow = f.Row();
	masterRow.Gap(8.f).Align(ui::CrossAlign::Center);
	auto lblMasterB = f.Label(String(std::format("Volume : {:.0f} %", master * 100.f).c_str()));
	lblMasterB.Name("lblMaster").W(ui::Dimension::Px(140)).FontSize(13.f);
	auto sldMaster = f.Slider(0.f, 1.f, master, 0.01f);
	sldMaster.GrowW().OnChange([&](float v) {
		master = v;
		setLabel(lblMaster, String(std::format("Volume : {:.0f} %", v * 100.f).c_str()));
	});
	masterRow.Children(std::move(lblMasterB), std::move(sldMaster));

	auto rmsRow = f.Row();
	rmsRow.Gap(8.f).Align(ui::CrossAlign::Center);
	auto lblRmsB = f.Label("RMS  -80.0 dB");
	lblRmsB.Name("lblRms").W(ui::Dimension::Px(140)).FontSize(12.f);
	auto progRmsB = f.Progress(0.f, 1.f, 0.f);
	progRmsB.Name("progRms").GrowW().H(ui::Dimension::Px(10)).Style(ui::UiStyle{}.SetBgChecked(sdl3::Color{70, 210, 140, 255}));
	rmsRow.Children(std::move(lblRmsB), std::move(progRmsB));

	auto peakRow = f.Row();
	peakRow.Gap(8.f).Align(ui::CrossAlign::Center);
	auto lblPeakB = f.Label("Peak -80.0 dB");
	lblPeakB.Name("lblPeak").W(ui::Dimension::Px(140)).FontSize(12.f);
	auto progPeakB = f.Progress(0.f, 1.f, 0.f);
	progPeakB.Name("progPeak").GrowW().H(ui::Dimension::Px(10)).Style(ui::UiStyle{}.SetBgChecked(sdl3::Color{220, 90, 80, 255}));
	peakRow.Children(std::move(lblPeakB), std::move(progPeakB));

	auto right = f.Column();
	right.Gap(10.f).Pad(14.f).GrowW().GrowH().Parent(bodyE);
	right.Children(std::move(masterRow), f.Label("Forme d'onde — signal composite").FontSize(14.f), std::move(plotWaveB),
				   f.Label("Spectre FFT (Hanning)").FontSize(14.f), std::move(plotSpecB), std::move(rmsRow), std::move(peakRow));
	right.Spawn();

	// ── Résolution des Entity après spawn ────────────────────────────────────
	lblStatus = named("lblStatus");
	lblMaster = named("lblMaster");
	plotWave  = named("plotWave");
	plotSpec  = named("plotSpec");
	progRms   = named("progRms");
	progPeak  = named("progPeak");
	lblRms    = named("lblRms");
	lblPeak   = named("lblPeak");
	for (int i = 0; i < K_NUM_OSC; ++i) {
		String idBase = String(std::format("osc{}", i).c_str());
		oscToggle[i] = named(idBase + String("_tog"));
		oscLblFreq[i] = named(idBase + String("_flbl"));
		oscLblAmp[i] = named(idBase + String("_albl"));
		for (int s = 0; s < K_NUM_SHAPES; ++s)
			oscShapeBtns[i][s] = named(idBase + String(std::format("_sh{}", s).c_str()));
		selectActive(oscShapeBtns[i], int(osc[i].shape));
	}

	if (auto p = ar.GetComponent<ui::UiPlot>(plotWave); p.IsSome()) {
		p.Unwrap()->yAxis.min = -1.f;
		p.Unwrap()->yAxis.max = 1.f;
		p.Unwrap()->yAxis.autoFit = false;
		p.Unwrap()->xAxis.min = 0.f;
		p.Unwrap()->xAxis.max = float(K_BLOCK_SIZE);
		p.Unwrap()->xAxis.autoFit = false;
	}
	if (auto p = ar.GetComponent<ui::UiPlot>(plotSpec); p.IsSome()) {
		p.Unwrap()->yAxis.min = -80.f;
		p.Unwrap()->yAxis.max = 0.f;
		p.Unwrap()->yAxis.autoFit = false;
		p.Unwrap()->xAxis.min = 0.f;
		p.Unwrap()->xAxis.max = K_MAX_OSC_FREQ;
		p.Unwrap()->xAxis.autoFit = false;
	}

	openDefaultDevice();

	// ── Boucle ───────────────────────────────────────────────────────────────
	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();

	while (running) {
		uint64_t now = sdl3::GetTicksMS();
		float dt = float(now - lastTick) / 1000.f;
		lastTick = now;

		// ── Génération audio : remplit le flux tant qu'il reste sous le seuil ──
		if (stream) {
			int minQueuedBytes = int(K_SAMPLE_RATE * K_BUFFER_SEC * sizeof(float));
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
			if (auto p = ar.GetComponent<ui::UiPlot>(plotWave); p.IsSome() && !p.Unwrap()->series.empty())
				p.Unwrap()->series[0].SetY(lastBlock);

			float rms = audio::GetRMS(lastBlock);
			float peak = audio::GetPeak(lastBlock);
			float rmsDb = rms > 1e-7f ? 20.f * sdl3::Log10(rms) : -80.f;
			float peakDb = peak > 1e-7f ? 20.f * sdl3::Log10(peak) : -80.f;
			if (auto pr = ar.GetComponent<ui::UiProgress>(progRms); pr.IsSome())
				pr.Unwrap()->value = sdl3::Clamp((rmsDb + 80.f) / 80.f, 0.f, 1.f);
			if (auto pr = ar.GetComponent<ui::UiProgress>(progPeak); pr.IsSome())
				pr.Unwrap()->value = sdl3::Clamp((peakDb + 80.f) / 80.f, 0.f, 1.f);
			setLabel(lblRms, String(std::format("RMS  {:+.1f} dB", rmsDb).c_str()));
			setLabel(lblPeak, String(std::format("Peak {:+.1f} dB", peakDb).c_str()));
		}

		if (specBuf.size() >= 512) {
			size_t sn = sdl3::Min(specBuf.size(), K_SPEC_BUF_SIZE);
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
				specX.reserve(freqs.size());
				specY.reserve(freqs.size());
				for (size_t k = 0; k < freqs.size() && freqs[k] <= 20000.f; ++k) {
					specX.push_back(freqs[k]);
					specY.push_back(sdl3::Max(-80.f, magDb[k]));
				}
				if (auto p = ar.GetComponent<ui::UiPlot>(plotSpec); p.IsSome() && !p.Unwrap()->series.empty())
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
		gui.Tick(dt);
		frame.Update();
		if (frame.CloseRequested())
			running = false;

		ren.SetDrawColor(sdl3::FColor::UI_WINDOW_BG());
		ren.Clear();
		gui.Render();
		ren.Present();
	}

	closeDevice();
	return 0;
}
