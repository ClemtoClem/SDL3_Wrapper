/**
 * audio_spectrum_analyzer — analyseur de spectre temps réel sur un
 * périphérique d'enregistrement, ré-implémenté sur les modules `audio::`
 * (lib/audio/dsp.hpp), `sdl3::` (device enumeration + AudioStream) et `ui::`
 * (deux ui::Plot mis à jour chaque image via PlotSeries::SetY/SetXy) de
 * cette librairie — pas un port littéral de SDL3pp/examples/audio/
 * 05_spectrum_analyzer.cpp (widgets/API différents), mais le même
 * comportement : liste de périphériques, gain, plage de fréquences, presets
 * taille/fréquence d'échantillonnage/fenêtre, forme d'onde + spectre.
 *
 *   cpp-maker run audio_spectrum_analyzer
 */
#include <deque>
#include <format>
#include <iostream>

#include <audio/dsp.hpp>
#include <core/core.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>

static constexpr int WIN_W = 1280;
static constexpr int WIN_H = 860; // 800 de contenu + barres de titre et d'état

static constexpr int K_SAMPLE_SIZES[] = {256, 512, 1024, 2048, 4096};
static constexpr int K_DEFAULT_SIZE_IDX = 3; // 2048
static constexpr int K_SAMPLE_RATES[] = {8000, 16000, 22050, 44100, 48000};
static constexpr int K_DEFAULT_RATE_IDX = 3; // 44100

struct WindowPreset {
	audio::WindowFunction fn;
	const char *label;
};
static constexpr WindowPreset K_WINDOWS[] = {
	{audio::WindowFunction::Rectangular, "Rect"},
	{audio::WindowFunction::Hann, "Hann"},
	{audio::WindowFunction::Hamming, "Hamming"},
	{audio::WindowFunction::Blackman, "Blackman"},
};
static constexpr int K_DEFAULT_WINDOW_IDX = 1; // Hann

static constexpr const char *ANALYZER_CSS = R"css(
#sidebar { width: 320px; padding: 14px; gap: 10px; background-color: #10121c; overflow-y: auto; }
#main { flex-grow: 1; padding: 14px; gap: 10px; }
.caption { color: #8c92a8; font-size: 12px; }
.value { font-size: 13px; }
#device { color: #78befa; font-size: 13px; }
.preset { flex-grow: 1; font-size: 12px; padding: 3px 4px; }
.preset.active { background-color: var(--accent); color: #f0f4fa; }
.preset.active:hover { background-color: #55a5f5; }
plot { width: 100%; height: 320px; }
)css";

/// Rangée de préréglages exclusifs : le bouton choisi porte la classe `active`.
static ui::AnyWidget PresetRow(std::vector<String> labels, int active, std::vector<ui::Widget> &refs,
							   std::function<void(int)> onPick) {
	refs.assign(labels.size(), ui::Widget());
	std::vector<ui::AnyWidget> buttons;
	for (size_t i = 0; i < labels.size(); ++i) {
		ui::WidgetBuilder b = ui::Button(labels[i]);
		b.Class("preset").Ref(refs[i]).OnClick([&refs, onPick, i] {
			for (size_t k = 0; k < refs.size(); ++k)
				if (k == i)
					refs[k].AddClass("active");
				else
					refs[k].RemoveClass("active");
			onPick(int(i));
		});
		if (int(i) == active)
			b.Class("active");
		buttons.push_back(b);
	}
	return ui::Row().Gap(4.f).Children(std::move(buttons));
}

int main() {
	// ── Init SDL / TTF / audio ───────────────────────────────────────────────
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
	auto winRes =
		sdl3::Window::Create(u8"audio:: - Analyseur de spectre", WIN_W, WIN_H, ui::WindowFrame::WINDOW_FLAGS);
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
	gui.AddStyleSheet(ANALYZER_CSS);

	// ── État analyse / audio ─────────────────────────────────────────────────
	std::vector<sdl3::AudioDevice> devices = sdl3::EnumerateRecordingDevices();
	sdl3::AudioStream recStream;
	int selectedDevice = -1;
	std::deque<float> pcmBuf;

	float gain = 1.f;
	float freqMin = 0.f;
	float freqMax = 8000.f;
	int sampleSize = K_SAMPLE_SIZES[K_DEFAULT_SIZE_IDX];
	float sampleRate = float(K_SAMPLE_RATES[K_DEFAULT_RATE_IDX]);
	audio::WindowFunction windowFn = K_WINDOWS[K_DEFAULT_WINDOW_IDX].fn;

	ui::Widget lbDevices, lblDevice, lblStatus, lblGain, lblFMin, lblFMax, plotWave, plotSpec;
	std::vector<ui::Widget> sizeButtons, rateButtons, windowButtons;

	auto closeDevice = [&] {
		if (recStream) {
			recStream.Pause();
			recStream = sdl3::AudioStream();
		}
		selectedDevice = -1;
		pcmBuf.clear();
	};
	auto openDevice = [&](int idx) {
		closeDevice();
		if (idx < 0 || idx >= int(devices.size()))
			return;
		selectedDevice = idx;
		sdl3::AudioSpec spec{sdl3::AudioFormat::F32, 1, int(sampleRate)};
		auto res = sdl3::AudioStream::OpenRecording(spec, devices[size_t(idx)].id);
		if (!res) {
			lblStatus.SetText(String("Erreur ouverture : ") + String(res.Error()));
			return;
		}
		recStream = std::move(res.Value());
		recStream.Resume();
		lblDevice.SetText(devices[size_t(idx)].name);
		lblStatus.SetText(String("Enregistrement : ") + devices[size_t(idx)].name);
	};
	auto reopenDevice = [&] {
		const int prev = selectedDevice;
		closeDevice();
		if (prev >= 0)
			openDevice(prev);
	};
	auto deviceNames = [&] {
		std::vector<String> names;
		for (auto &d : devices)
			names.push_back(d.name);
		return names;
	};
	auto refreshDevices = [&] {
		devices = sdl3::EnumerateRecordingDevices();
		lbDevices.SetItems(deviceNames());
		lbDevices.SetSelected(-1);
		lblStatus.SetText(std::format("{} périphérique(s) trouvé(s)", devices.size()));
	};
	auto updateSpecXAxis = [&] {
		if (auto p = plotSpec.Get<ui::PlotModel>(); p.IsSome()) {
			p.Unwrap()->xAxis.min = freqMin;
			p.Unwrap()->xAxis.max = freqMax;
		}
	};
	auto updateWaveXAxis = [&] {
		if (auto p = plotWave.Get<ui::PlotModel>(); p.IsSome())
			p.Unwrap()->xAxis.max = float(sampleSize);
	};

	// ── Construction UI ──────────────────────────────────────────────────────
	ui::WindowFrame frame;
	frame.Build(gui, window,
				{.title = "audio:: - Analyseur de spectre", .appIcon = Some(ui::MaterialIcons::EQUALIZER),
				 .status = "Capture du périphérique d'entrée, FFT en continu."});

	// Séries initiales de taille non nulle ; la boucle ne fait que réécrire series[0].
	// Plages fixes : des axes qui sautent avec le signal seraient illisibles.
	const std::vector<float> initWave(size_t(sampleSize), 0.f);
	const std::vector<float> initSpecX(size_t(sampleSize / 2), 0.f), initSpecY(size_t(sampleSize / 2), -80.f);
	ui::PlotBuilder waveBuilder = ui::Plot();
	waveBuilder.AddLineSeries(initWave, "", Some(ui::Rgba(80, 200, 120)))
		.XRange(0.f, float(sampleSize))
		.YRange(-1.f, 1.f)
		.ShowLegend(false)
		.Ref(plotWave);
	ui::PlotBuilder specBuilder = ui::Plot();
	specBuilder.AddAreaSeries(initSpecX, initSpecY, "", Some(ui::Rgba(90, 180, 255)))
		.XRange(freqMin, freqMax)
		.YRange(-80.f, 0.f)
		.ShowLegend(false)
		.Ref(plotSpec);

	std::vector<String> sizeLabels, rateLabels, windowLabels;
	for (int sz : K_SAMPLE_SIZES)
		sizeLabels.push_back(String::From(sz));
	for (int ra : K_SAMPLE_RATES)
		rateLabels.push_back(String::From(ra / 1000) + "k");
	for (const WindowPreset &w : K_WINDOWS)
		windowLabels.push_back(w.label);

	frame.Content().Append(ui::Row().Grow().FillWidth().AlignItems(ui::Align::Stretch).Children({
		ui::Column().Id("sidebar").Children({
			ui::Label("Périphérique d'entrée").FontSize(15.f),
			ui::ListBox(deviceNames(), -1).Height(140.f).FillWidth().Ref(lbDevices).OnSelect([&](int i) {
				openDevice(i);
			}),
			ui::Button("Rafraîchir la liste").FillWidth().OnClick(refreshDevices),
			ui::Label("Actif :").Class("caption"),
			ui::Label("(aucun)").Id("device").Ref(lblDevice),
			ui::Separator(),
			ui::Label(std::format("Gain : {:.1f}x", gain)).Class("value").Ref(lblGain),
			ui::Slider(0.1f, 10.f, gain, 0.1f).FillWidth().OnChange([&](float v) {
				gain = v;
				lblGain.SetText(std::format("Gain : {:.1f}x", v));
			}),
			ui::Label(std::format("Freq min : {:.0f} Hz", freqMin)).Class("value").Ref(lblFMin),
			ui::Slider(0.f, 4000.f, freqMin, 1.f).FillWidth().OnChange([&](float v) {
				freqMin = v >= freqMax - 200.f ? sdl3::Max(0.f, freqMax - 200.f) : v;
				lblFMin.SetText(std::format("Freq min : {:.0f} Hz", freqMin));
				updateSpecXAxis();
			}),
			ui::Label(std::format("Freq max : {:.0f} Hz", freqMax)).Class("value").Ref(lblFMax),
			ui::Slider(1000.f, 20000.f, freqMax, 1.f).FillWidth().OnChange([&](float v) {
				freqMax = v <= freqMin + 200.f ? sdl3::Min(20000.f, freqMin + 200.f) : v;
				lblFMax.SetText(std::format("Freq max : {:.0f} Hz", freqMax));
				updateSpecXAxis();
			}),
			ui::Separator(),
			ui::Label("Taille FFT :").Class("caption"),
			PresetRow(sizeLabels, K_DEFAULT_SIZE_IDX, sizeButtons,
					  [&](int i) {
						  sampleSize = K_SAMPLE_SIZES[i];
						  pcmBuf.clear();
						  updateWaveXAxis();
					  }),
			ui::Label("Fréquence d'échantillonnage :").Class("caption"),
			PresetRow(rateLabels, K_DEFAULT_RATE_IDX, rateButtons,
					  [&](int i) {
						  sampleRate = float(K_SAMPLE_RATES[i]);
						  reopenDevice();
					  }),
			ui::Label("Fenêtre spectrale :").Class("caption"),
			PresetRow(windowLabels, K_DEFAULT_WINDOW_IDX, windowButtons, [&](int i) { windowFn = K_WINDOWS[i].fn; }),
			ui::Separator(),
			ui::Label(std::format("{} périphérique(s) trouvé(s)", devices.size())).Class("caption").Ref(lblStatus),
		}),
		ui::Column().Id("main").Children({
			ui::Label("Forme d'onde").FontSize(15.f),
			waveBuilder,
			ui::Label("Spectre de fréquence").FontSize(15.f),
			specBuilder,
		}),
	}));

	// ── Boucle ───────────────────────────────────────────────────────────────
	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();
	while (running) {
		const uint64_t now = sdl3::GetTicksMS();
		const float dt = float(now - lastTick) / 1000.f;
		lastTick = now;

		// ── Audio : draine le flux, applique le gain, alimente le tampon glissant ──
		if (recStream) {
			const int avail = recStream.Available();
			if (avail > 0) {
				std::vector<float> tmp(size_t(avail) / sizeof(float));
				const int bytesRead = recStream.GetData(std::span<float>(tmp));
				const size_t nRead = size_t(bytesRead) / sizeof(float);
				for (size_t i = 0; i < nRead; ++i)
					pcmBuf.push_back(tmp[i] * gain);
				while (pcmBuf.size() > size_t(sampleSize) * 8)
					pcmBuf.pop_front();
			}
		}

		// ── DSP : forme d'onde + spectre ──────────────────────────────────────
		if (int(pcmBuf.size()) >= 16) {
			const int wn = sdl3::Min(int(pcmBuf.size()), sampleSize);
			std::vector<float> wave(pcmBuf.end() - wn, pcmBuf.end());
			if (auto p = plotWave.Get<ui::PlotModel>(); p.IsSome() && !p.Unwrap()->series.empty())
				p.Unwrap()->series[0].SetY(wave);
			// ProcessFFT() exige une puissance de 2 : bloc le plus récent.
			size_t fftLen = 1;
			while (fftLen * 2 <= size_t(wn))
				fftLen *= 2;
			if (fftLen >= 2) {
				std::vector<float> windowed(wave.end() - long(fftLen), wave.end());
				audio::ApplyWindow(windowed, windowFn);
				auto spectrum = audio::ProcessFFT(windowed);
				auto magDb = audio::ProcessFFTMagnitudeDb(spectrum);
				auto freqs = audio::ProcessFFTFrequencies(windowed.size(), int(sampleRate));
				std::vector<float> specX, specY;
				for (size_t k = 0; k < freqs.size(); ++k) {
					if (freqs[k] < freqMin)
						continue;
					if (freqs[k] > freqMax)
						break;
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
