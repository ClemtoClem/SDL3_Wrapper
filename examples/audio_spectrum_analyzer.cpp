/**
 * audio_spectruanalyzer — analyseur de spectre temps réel sur un
 * périphérique d'enregistrement, ré-implémenté sur les modules `audio::`
 * (lib/audio/dsp.hpp), `sdl3::` (device enumeration + AudioStream) et `ui::`
 * (deux ui::UiPlot mis à jour chaque frame via PlotSeries::setY/setXY) de
 * cette librairie — pas un port littéral de SDL3pp/examples/audio/
 * 05_spectruanalyzer.cpp (widgets/API différents), mais le même
 * comportement : liste de périphériques, gain, plage de fréquences, presets
 * taille/fréquence d'échantillonnage/fenêtre, forme d'onde + spectre.
 *
 *   make examples && ./build/examples/audio_spectruanalyzer
 */
#include <deque>
#include <format>
#include <iostream>

#include "audio/dsp.hpp"
#include "core/core.hpp"
#include "sdl3/sdl3.hpp"
#include "ui/ui.hpp"

static constexpr int WIN_W = 1280;
static constexpr int WIN_H = 800;
static constexpr float FONT_PT = 14.f;

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

static const sdl3::FColor K_ACTIVE_BG = sdl3::FColor::UI_ACCENT_BLUE_PRIMARY();
static const sdl3::FColor K_ACTIVE_BG_HOVER{85 / 255.f, 165 / 255.f, 245 / 255.f, 1.f};
static const sdl3::FColor K_ACTIVE_TEXT{240 / 255.f, 244 / 255.f, 250 / 255.f, 1.f};

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
	auto fontRes = sdl3::Font::FindLocal({"DejaVuSans", "FreeSans", "Arial"}, FONT_PT);
	if (!fontRes) {
		std::cerr << "Font: " << fontRes.Error().CStr() << "\n";
		return 1;
	}
	auto &font = fontRes.Value();

	auto winRes =
		sdl3::Window::Create(u8"audio:: - Analyseur de spectre", WIN_W, WIN_H, sdl3::window_flags::RESIZABLE);
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

	auto named = [&ar](const char *n) -> ecs::Entity {
		auto o = ui::FindByName(ar, String(n));
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

	ecs::Entity lblDevice, lblStatus, lblGain, lblFMin, lblFMax, plotWave, plotSpec;
	ecs::Entity szBtns[std::size(K_SAMPLE_SIZES)], raBtns[std::size(K_SAMPLE_RATES)], winBtns[std::size(K_WINDOWS)];

	auto setStatus = [&](String msg) { setLabel(lblStatus, std::move(msg)); };

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
			setStatus(String("Erreur ouverture : ") + String(res.Error()));
			return;
		}
		recStream = std::move(res.Value());
		recStream.Resume();
		setLabel(lblDevice, devices[size_t(idx)].name);
		setStatus(String("Enregistrement : ") + devices[size_t(idx)].name);
	};

	auto reopenDevice = [&] {
		int prev = selectedDevice;
		closeDevice();
		if (prev >= 0)
			openDevice(prev);
	};

	auto refreshDevices = [&] {
		devices = sdl3::EnumerateRecordingDevices();
		std::vector<String> names;
		names.reserve(devices.size());
		for (auto &d : devices)
			names.push_back(d.name);
		if (auto lb = ar.GetComponent<ui::ListBox>(named("lbDevices")); lb.IsSome()) {
			lb.Unwrap()->items = std::move(names);
			lb.Unwrap()->selected = -1;
			layout.MarkDirty();
		}
		setStatus(String(std::format("{} périphérique(s) trouvé(s)", devices.size()).c_str()));
	};

	auto updateSpecXAxis = [&] {
		if (auto p = ar.GetComponent<ui::UiPlot>(plotSpec); p.IsSome()) {
			p.Unwrap()->xAxis.min = freqMin;
			p.Unwrap()->xAxis.max = freqMax;
		}
	};
	auto updateWaveXAxis = [&] {
		if (auto p = ar.GetComponent<ui::UiPlot>(plotWave); p.IsSome())
			p.Unwrap()->xAxis.max = float(sampleSize);
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
	auto left = f.Column();
	left.Anchor(ui::Anchor::TopLeft)
		.Gap(10.f)
		.Pad(14.f)
		.W(ui::Dimension::Px(320))
		.H(ui::Dimension::Rpct(100))
		.Bg({16, 18, 28});

	std::vector<String> deviceNames;
	for (auto &d : devices)
		deviceNames.push_back(d.name);

	auto lbDevices = f.Listbox(deviceNames, -1);
	lbDevices.Name("lbDevices").H(ui::Dimension::Px(140)).GrowW().OnChange([&](float idx) { openDevice(int(idx)); });

	auto btnRefresh = f.Button("Rafraîchir la liste");
	btnRefresh.GrowW().OnClick(refreshDevices);

	left.Children(f.Label("Périphérique d'entrée").FontSize(15.f), std::move(lbDevices), std::move(btnRefresh),
				  f.Label("Actif :").TextColor({140, 146, 168}).FontSize(12.f),
				  f.Label("(aucun)").Name("lblDevice").TextColor({120, 190, 250}).FontSize(13.f), f.Separator(),

				  f.Label(String(std::format("Gain : {:.1f}x", gain).c_str())).Name("lblGain").FontSize(13.f),
				  f.Slider(0.1f, 10.f, gain, 0.1f).GrowW().OnChange([&](float v) {
					  gain = v;
					  setLabel(lblGain, String(std::format("Gain : {:.1f}x", v).c_str()));
				  }),

				  f.Label(String(std::format("Freq min : {:.0f} Hz", freqMin).c_str())).Name("lblFMin").FontSize(13.f),
				  f.Slider(0.f, 4000.f, freqMin, 1.f).GrowW().OnChange([&](float v) {
					  freqMin = v;
					  if (freqMin >= freqMax - 200.f)
						  freqMin = sdl3::Max(0.f, freqMax - 200.f);
					  setLabel(lblFMin, String(std::format("Freq min : {:.0f} Hz", freqMin).c_str()));
					  updateSpecXAxis();
				  }),

				  f.Label(String(std::format("Freq max : {:.0f} Hz", freqMax).c_str())).Name("lblFMax").FontSize(13.f),
				  f.Slider(1000.f, 20000.f, freqMax, 1.f).GrowW().OnChange([&](float v) {
					  freqMax = v;
					  if (freqMax <= freqMin + 200.f)
						  freqMax = sdl3::Min(20000.f, freqMin + 200.f);
					  setLabel(lblFMax, String(std::format("Freq max : {:.0f} Hz", freqMax).c_str()));
					  updateSpecXAxis();
				  }),
				  f.Separator(),

				  f.Label("Taille FFT :").TextColor({140, 146, 168}).FontSize(12.f),
				  f.Row().Gap(4.f).Children(
					  f.Button("256").Name("btnSz0").GrowW().FontSize(12.f).OnClick([&] {
						  sampleSize = K_SAMPLE_SIZES[0];
						  pcmBuf.clear();
						  updateWaveXAxis();
						  selectActive(szBtns,0);
					  }),
					  f.Button("512").Name("btnSz1").GrowW().FontSize(12.f).OnClick([&] {
						  sampleSize = K_SAMPLE_SIZES[1];
						  pcmBuf.clear();
						  updateWaveXAxis();
						  selectActive(szBtns,1);
					  }),
					  f.Button("1024").Name("btnSz2").GrowW().FontSize(12.f).OnClick([&] {
						  sampleSize = K_SAMPLE_SIZES[2];
						  pcmBuf.clear();
						  updateWaveXAxis();
						  selectActive(szBtns,2);
					  }),
					  f.Button("2048").Name("btnSz3").GrowW().FontSize(12.f).OnClick([&] {
						  sampleSize = K_SAMPLE_SIZES[3];
						  pcmBuf.clear();
						  updateWaveXAxis();
						  selectActive(szBtns,3);
					  }),
					  f.Button("4096").Name("btnSz4").GrowW().FontSize(12.f).OnClick([&] {
						  sampleSize = K_SAMPLE_SIZES[4];
						  pcmBuf.clear();
						  updateWaveXAxis();
						  selectActive(szBtns,4);
					  })),

				  f.Label("Fréquence d'échantillonnage :").TextColor({140, 146, 168}).FontSize(12.f),
				  f.Row().Gap(4.f).Children(
					  f.Button("8k").Name("btnRa0").GrowW().FontSize(12.f).OnClick([&] {
						  sampleRate = float(K_SAMPLE_RATES[0]);
						  selectActive(raBtns, 0);
						  reopenDevice();
					  }),
					  f.Button("16k").Name("btnRa1").GrowW().FontSize(12.f).OnClick([&] {
						  sampleRate = float(K_SAMPLE_RATES[1]);
						  selectActive(raBtns, 1);
						  reopenDevice();
					  }),
					  f.Button("22k").Name("btnRa2").GrowW().FontSize(12.f).OnClick([&] {
						  sampleRate = float(K_SAMPLE_RATES[2]);
						  selectActive(raBtns, 2);
						  reopenDevice();
					  }),
					  f.Button("44k").Name("btnRa3").GrowW().FontSize(12.f).OnClick([&] {
						  sampleRate = float(K_SAMPLE_RATES[3]);
						  selectActive(raBtns, 3);
						  reopenDevice();
					  }),
					  f.Button("48k").Name("btnRa4").GrowW().FontSize(12.f).OnClick([&] {
						  sampleRate = float(K_SAMPLE_RATES[4]);
						  selectActive(raBtns, 4);
						  reopenDevice();
					  })),

				  f.Label("Fenêtre spectrale :").TextColor({140, 146, 168}).FontSize(12.f),
				  f.Row().Gap(4.f).Children(
					  f.Button("Rect").Name("btnWin0").GrowW().FontSize(12.f).OnClick([&] {
						  windowFn = K_WINDOWS[0].fn;
						  selectActive(winBtns, 0);
					  }),
					  f.Button("Hann").Name("btnWin1").GrowW().FontSize(12.f).OnClick([&] {
						  windowFn = K_WINDOWS[1].fn;
						  selectActive(winBtns, 1);
					  }),
					  f.Button("Hamming").Name("btnWin2").GrowW().FontSize(12.f).OnClick([&] {
						  windowFn = K_WINDOWS[2].fn;
						  selectActive(winBtns, 2);
					  }),
					  f.Button("Blackman").Name("btnWin3").GrowW().FontSize(12.f).OnClick([&] {
						  windowFn = K_WINDOWS[3].fn;
						  selectActive(winBtns, 3);
					  })),
				  f.Separator(),
				  f.Label(String(std::format("{} périphérique(s) trouvé(s)", devices.size()).c_str()))
					  .Name("lblStatus")
					  .TextColor({140, 146, 168})
					  .FontSize(12.f));
	left.Spawn();

	// Séries initiales (vides mais de taille non nulle) — le rendu par frame
	// ne fait que RÉÉCRIRE `series[0]` via setY()/setXY(), jamais en ajouter.
	std::vector<float> initWave(size_t(sampleSize), 0.f);
	auto plotWaveB = f.Plot("");
	plotWaveB.AddLineSeries(initWave, "", Some(sdl3::FColor::UI_ACCENT_GREEN()));
	plotWaveB.Name("plotWave").GrowW().H(ui::Dimension::Px(320));

	std::vector<float> initSpecX(size_t(sampleSize / 2), 0.f), initSpecY(size_t(sampleSize / 2), -80.f);
	auto plotSpecB = f.Plot("");
	plotSpecB.AddAreaSeries(initSpecX, initSpecY, "", Some(sdl3::FColor::UI_ACCENT_BLUE_SKY()));
	plotSpecB.Name("plotSpec").GrowW().H(ui::Dimension::Px(320));

	auto right = f.Column();
	right.Anchor(ui::Anchor::TopLeft)
		.Offset(320.f, 0.f)
		.Gap(10.f)
		.Pad(14.f)
		.W(ui::Dimension::Rpct(100.f).Plus(-320.f))
		.H(ui::Dimension::Rpct(100));
	right.Children(f.Label("Forme d'onde").FontSize(15.f), std::move(plotWaveB),
				   f.Label("Spectre de fréquence").FontSize(15.f), std::move(plotSpecB));
	right.Spawn();

	// Résolution des Entity APRÈS spawn (les WidgetBuilder passés à
	// `.Children()` ne deviennent des Entity qu'une fois le parent spawné).
	lblDevice = named("lblDevice");
	lblStatus = named("lblStatus");
	lblGain = named("lblGain");
	lblFMin = named("lblFMin");
	lblFMax = named("lblFMax");
	plotWave = named("plotWave");
	plotSpec = named("plotSpec");
	const char *szNames[] = {"btnSz0", "btnSz1", "btnSz2", "btnSz3", "btnSz4"};
	const char *raNames[] = {"btnRa0", "btnRa1", "btnRa2", "btnRa3", "btnRa4"};
	const char *winNames[] = {"btnWin0", "btnWin1", "btnWin2", "btnWin3"};
	for (size_t i = 0; i < std::size(szBtns); ++i)
		szBtns[i] = named(szNames[i]);
	for (size_t i = 0; i < std::size(raBtns); ++i)
		raBtns[i] = named(raNames[i]);
	for (size_t i = 0; i < std::size(winBtns); ++i)
		winBtns[i] = named(winNames[i]);
	selectActive(szBtns, K_DEFAULT_SIZE_IDX);
	selectActive(raBtns, K_DEFAULT_RATE_IDX);
	selectActive(winBtns, K_DEFAULT_WINDOW_IDX);

	// Plages fixes plutôt qu'auto-fit (défaut de PlotAxis) : un spectre/une
	// forme d'onde dont les axes sautent à chaque frame en fonction du signal
	// du moment serait illisible — la plage est un choix d'analyse (gain,
	// freqMin/freqMax), pas une propriété des données affichées.
	if (auto p = ar.GetComponent<ui::UiPlot>(plotWave); p.IsSome()) {
		p.Unwrap()->yAxis.min = -1.f;
		p.Unwrap()->yAxis.max = 1.f;
		p.Unwrap()->yAxis.autoFit = false;
		p.Unwrap()->xAxis.min = 0.f;
		p.Unwrap()->xAxis.max = float(sampleSize);
		p.Unwrap()->xAxis.autoFit = false;
	}
	if (auto p = ar.GetComponent<ui::UiPlot>(plotSpec); p.IsSome()) {
		p.Unwrap()->yAxis.min = -80.f;
		p.Unwrap()->yAxis.max = 0.f;
		p.Unwrap()->yAxis.autoFit = false;
		p.Unwrap()->xAxis.min = freqMin;
		p.Unwrap()->xAxis.max = freqMax;
		p.Unwrap()->xAxis.autoFit = false;
	}

	// ── Boucle ───────────────────────────────────────────────────────────────
	bool running = true;
	uint64_t lastTick = sdl3::GetTicksMS();

	while (running) {
		uint64_t now = sdl3::GetTicksMS();
		float dt = float(now - lastTick) / 1000.f;
		lastTick = now;

		// ── Audio : draine le flux, applique le gain, alimente le tampon glissant ──
		if (recStream) {
			int avail = recStream.Available();
			if (avail > 0) {
				size_t nSamples = size_t(avail) / sizeof(float);
				std::vector<float> tmp(nSamples);
				int bytesRead = recStream.GetData(std::span<float>(tmp));
				size_t nRead = size_t(bytesRead) / sizeof(float);
				for (size_t i = 0; i < nRead; ++i)
					pcmBuf.push_back(tmp[i] * gain);
				size_t kMaxBuf = size_t(sampleSize) * 8;
				while (pcmBuf.size() > kMaxBuf)
					pcmBuf.pop_front();
			}
		}

		// ── DSP : forme d'onde + spectre ──────────────────────────────────────
		if (int(pcmBuf.size()) >= 16) {
			int wn = sdl3::Min(int(pcmBuf.size()), sampleSize);
			std::vector<float> wave(size_t(wn), 0.f);
			std::copy(pcmBuf.end() - wn, pcmBuf.end(), wave.begin());

			if (auto p = ar.GetComponent<ui::UiPlot>(plotWave); p.IsSome() && !p.Unwrap()->series.empty())
				p.Unwrap()->series[0].SetY(wave);

			// ProcessFFT() exige une puissance de 2 : garde le bloc le plus récent de
			// taille = plus grande puissance de 2 <= wn (pas de rognage O(n²)
			// par erase() répété — un seul calcul de longueur + une copie).
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
				specX.reserve(freqs.size());
				specY.reserve(freqs.size());
				for (size_t k = 0; k < freqs.size(); ++k) {
					if (freqs[k] < freqMin)
						continue;
					if (freqs[k] > freqMax)
						break;
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

		ren.SetDrawColor(sdl3::FColor::UI_WINDOW_BG());
		ren.Clear();
		gui.Render();
		ren.Present();
	}

	closeDevice();
	return 0;
}
