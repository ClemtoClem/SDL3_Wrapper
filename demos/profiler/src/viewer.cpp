// Visualiseur du profiler (cf. profiler/app.hpp, RunViewer) — chronologie
// des fils à la manière d'Orbit, statistiques, arbres d'appels, diagnostic.
#include <profiler/app.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <editor/editor.hpp>
#include <profiler/analysis.hpp>
#include <profiler/capture.hpp>
#include <profiler/recorder.hpp>
#include <profiler/settings.hpp>
#include <profiler/symbols.hpp>
#include <sdl3/image.hpp>
#include <sdl3/sdl3.hpp>
#include <ui/ui.hpp>
#include <ui/window_frame.hpp>

namespace profiler {

namespace {

constexpr float GUTTER = 170.f;	  ///< colonne des noms de pistes
constexpr float RULER = 22.f;
constexpr float ROW = 16.f;		  ///< une profondeur de zones
constexpr float STATE_ROW = 5.f;
constexpr float FRAME_TRACK = 40.f;
constexpr float VALUE_TRACK = 34.f;
constexpr size_t MAX_DEPTH_ROWS = 10;

constexpr const char *VIEWER_CSS = R"css(
.pf-finding { padding: 4px 6px; gap: 2px; border-radius: 4px; background-color: var(--ed-well); }
.pf-title { font-size: 13px; font-weight: bold; }
.pf-detail { font-size: 12px; color: var(--muted); }
.pf-critical { color: var(--ed-error); }
.pf-warning { color: var(--ed-warning); }
.pf-info { color: var(--ed-ok); }
.pf-key { font-size: 12px; color: var(--muted); width: 120px; }
.pf-value { font-size: 12px; }
.pf-thread { padding: 2px 4px; gap: 6px; align-items: center; }
.pf-mono { font-family: monospace; font-size: 12px; }
.pf-modal { width: 560px; padding: 14px; gap: 8px; background-color: var(--panel); border-radius: 6px; }
.pf-settings { width: 720px; }
)css";

sdl3::FColor ZoneColor(const String &name) {
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < name.Length(); ++i)
		h = (h ^ uint8_t(name[i])) * 16777619u;
	const float hue = float(h % 360u) / 60.f;
	const float c = 0.55f, x = c * (1.f - std::fabs(std::fmod(hue, 2.f) - 1.f)), m = 0.28f;
	float r = 0, g = 0, b = 0;
	switch (int(hue)) {
	case 0: r = c, g = x; break;
	case 1: r = x, g = c; break;
	case 2: g = c, b = x; break;
	case 3: g = x, b = c; break;
	case 4: r = x, b = c; break;
	default: r = c, b = x; break;
	}
	return {r + m, g + m, b + m, 1.f};
}

sdl3::FColor StateColor(char state) {
	switch (state) {
	case 'R': return ui::Rgba(96, 200, 96);	 // sur le processeur (ou prêt)
	case 'D': return ui::Rgba(236, 88, 72);	 // entrées/sorties
	case 'S': return ui::Rgba(70, 76, 92);	 // en attente
	default: return ui::Rgba(120, 120, 140);
	}
}

String FormatTime(double ns) {
	if (std::fabs(ns) >= 1e9)
		return String::Format("%.3f s", ns / 1e9);
	if (std::fabs(ns) >= 1e6)
		return String::Format("%.2f ms", ns / 1e6);
	if (std::fabs(ns) >= 1e3)
		return String::Format("%.1f µs", ns / 1e3);
	return String::Format("%.0f ns", ns);
}

class Viewer {
public:
	Viewer(sdl3::Window &window, sdl3::Renderer &renderer, Settings settings)
		: m_window(window), m_renderer(renderer), m_settings(std::move(settings)), m_gui(m_world, window, renderer), m_left(m_gui),
		  m_center(m_gui), m_bottom(m_gui), m_right(m_gui) {}

	/// `frames` > 0 : quitte après ce nombre d'images (captures d'écran, tests).
	int Run(const String &initial, long frames, const String &screenshot) {
		Build();
		if (!initial.IsEmpty())
			Open(initial);
		bool running = true;
		uint64_t previous = sdl3::GetTicksNS();
		while (running) {
			while (auto event = sdl3::PollEvent()) {
				sdl3::Event &e = event.Value();
				if (e.IsQuit()) {
					running = false;
					break;
				}
				const bool consumed = m_gui.HandleEvent(e);
				if (e.IsKeyDown(SDLK_COMMA) && (SDL_GetModState() & SDL_KMOD_CTRL) != 0) {
					OpenSettings(); // Ctrl+, : réglages, comme dans la plupart des éditeurs
					continue;
				}
				HandleTimeline(e, consumed);
			}
			PollJob();
			const uint64_t now = sdl3::GetTicksNS();
			m_gui.Update(float(double(now - previous) / 1e9));
			previous = now;
			m_frame.Update();
			if (m_frame.CloseRequested())
				running = false;
			m_gui.Render();
			++m_frameCount;
			if (frames > 0 && m_frameCount >= frames) {
				if (!screenshot.IsEmpty())
					if (auto surface = m_renderer.ReadPixels(); surface)
						(void)sdl3::ImgSavePng(surface.Value(), screenshot);
				running = false;
			}
			m_renderer.Present();
		}
		if (m_job.joinable())
			m_job.join();
		return 0;
	}

private:
	// ── Construction ────────────────────────────────────────────────────────

	void Build() {
		const ui::Palette &theme = m_gui.CurrentPalette();
		m_gui.AddStyleSheet(editor::EditorCss(theme).View());
		m_gui.AddStyleSheet(StringView(VIEWER_CSS));
		m_frame.Build(m_gui, m_window, {.title = "Profiler", .appIcon = Some(ui::MaterialIcons::TIMELINE), .status = "Aucune capture"});
		m_root = m_frame.Content().Append(ui::Column().Class("editor-base").Gap(0.f).Padding(0.f).Grow());
		BuildMenus();
		ui::Widget body = m_root.Append(ui::Row().Gap(0.f).Padding(math::Sides{4.f, 2.f, 4.f, 2.f}).GrowWidth().GrowHeight());
		ui::Widget main = body.Append(ui::Column().Gap(0.f).Padding(0.f).GrowWidth().GrowHeight());
		ui::Widget top = main.Append(ui::Row().Gap(0.f).Padding(0.f).GrowWidth().GrowHeight());

		m_left.Build(top, [](ui::WidgetBuilder &b) { b.Width(250.f).GrowHeight(); });
		m_threadList = m_left.AddPage(String("Fils"), true);
		m_info = m_left.AddPage(String("Capture"), true);
		m_left.SetActive(0);
		(void)editor::ResizeHandle(theme, top, ui::Orientation::Horizontal, m_left.Frame(), true, 160.f, 500.f);

		m_center.Build(top, [](ui::WidgetBuilder &b) { b.GrowWidth().GrowHeight(); });
		ui::Widget page = m_center.AddPage(String("Chronologie"), false, false, 0.f);
		m_timeline = page.Append(ui::Canvas([this](ui::Painter &p, const sdl3::FRect &r) { DrawTimeline(p, r); })
									 .Id("timeline")
									 .AppSurface()
									 .Grow());

		ui::Widget bottom = main.Append(ui::Row().Gap(0.f).Padding(0.f).GrowWidth().Height(300.f));
		(void)editor::ResizeHandle(theme, main, ui::Orientation::Vertical, bottom, false, 100.f, 800.f);
		m_bottom.Build(bottom, [](ui::WidgetBuilder &b) { b.GrowWidth().GrowHeight(); });
		m_findings = m_bottom.AddPage(String("Diagnostic"), true);
		m_zonesPage = m_bottom.AddPage(String("Fonctions instrumentées"), false);
		m_functionsPage = m_bottom.AddPage(String("Échantillonnage"), false);
		m_topDownPage = m_bottom.AddPage(String("Arbre descendant"), true);
		m_bottomUpPage = m_bottom.AddPage(String("Arbre ascendant"), true);
		m_bottom.SetActive(0);

		ui::Widget right = body.Append(ui::Column().Gap(0.f).Padding(0.f).Width(330.f).GrowHeight());
		(void)editor::ResizeHandle(theme, body, ui::Orientation::Horizontal, right, false, 220.f, 700.f);
		m_right.Build(right, [](ui::WidgetBuilder &b) { b.GrowWidth().GrowHeight(); });
		m_selection = m_right.AddPage(String("Sélection"), true);
		ShowSelection();

		BuildModal();
		BuildSettings();
	}

	void BuildMenus() {
		auto action = [](const char *text, const char *shortcut, std::function<void()> fn) {
			ui::WidgetBuilder b = ui::MenuItem(String(text), String(shortcut));
			b.OnClick(std::move(fn));
			return b;
		};
		ui::Widget bar = m_root.Append(ui::MenuBar().GrowWidth().Padding(math::Sides{6.f, 2.f}).Class("editor-header"));
		m_recentMenu = ui::Widget();
		bar.Append(ui::Menu(String("Fichier")).Children({
			action("Ouvrir une capture…", "", [this] { AskOpen(); }),
			ui::SubMenu(String("Captures récentes")).Ref(m_recentMenu),
			action("Comparer avec une capture…", "", [this] { AskCompare(); }),
			action("Exporter pour Perfetto / Chrome…", "", [this] { AskExport(); }),
			ui::MenuSeparator(),
			action("Réglages…", "Ctrl+,", [this] { OpenSettings(); }),
			ui::MenuSeparator(),
			action("Quitter", "", [this] { m_frame.RequestClose(); }),
		}));
		RefreshRecent();
		bar.Append(ui::Menu(String("Capture")).Children({
			action("Lancer un programme et capturer…", "", [this] { AskRecord(); }),
			action("Capturer un processus en cours…", "", [this] { AskAttach(); }),
		}));
		bar.Append(ui::Menu(String("Affichage")).Children({
			action("Tout afficher", "Origine", [this] { FitAll(); }),
			action("Cadrer la sélection", "F", [this] { FitSelection(); }),
			action("Effacer la sélection", "Échap", [this] { ClearSelection(); }),
		}));
	}

	/// Captures récentes (réglages), puis celles du dossier des captures.
	void RefreshRecent() {
		if (!m_recentMenu.Valid())
			return;
		m_recentMenu.Clear();
		std::vector<String> paths = m_settings.recentCaptures;
		for (const String &p : m_settings.Captures())
			if (paths.size() < 15 && std::find(paths.begin(), paths.end(), p) == paths.end())
				paths.push_back(p);
		if (paths.empty())
			m_recentMenu.Append(ui::MenuItem(String("(aucune capture dans ") + m_settings.capturesDirectory + String(")")));
		for (const String &p : paths) {
			ui::WidgetBuilder item = ui::MenuItem(p);
			item.OnClick([this, p] { Open(p); });
			m_recentMenu.Append(item);
		}
	}

	/// Une boîte de saisie réutilisée par toutes les demandes (ouvrir,
	/// lancer, s'attacher, exporter).
	void BuildModal() {
		m_modal = m_root.Append(ui::Modal(ui::Column().Class("pf-modal").Children({
			ui::Label("").Class("pf-title").Ref(m_modalTitle),
			ui::Label("").Class("pf-detail").TextWrap().GrowWidth().Ref(m_modalHelp),
			ui::Input("").GrowWidth().Ref(m_modalInput).OnSubmit([this](const String &) { AcceptModal(); }),
			ui::Input("").GrowWidth().Ref(m_modalSecond).OnSubmit([this](const String &) { AcceptModal(); }),
			ui::Row().Gap(6.f).Children({
				ui::Spacer(),
				ui::Button("Annuler").OnClick([this] { m_modal.Close(); }),
				ui::Button("Valider").OnClick([this] { AcceptModal(); }),
			}),
		})));
	}

	// ── Réglages (data/profiler/settings.json) ──────────────────────────────

	/// Une ligne « libellé | champ » de la boîte des réglages.
	ui::WidgetBuilder SettingRow(const char *label, const char *tooltip, ui::Widget &field, const char *placeholder) {
		return ui::Row().Gap(8.f).GrowWidth().Children({
			ui::Label(label).Class("pf-key").Width(210.f).Tooltip(tooltip),
			ui::Input(placeholder).Grow().Ref(field).Tooltip(tooltip),
		});
	}

	void BuildSettings() {
		m_settingsModal = m_root.Append(ui::Modal(ui::Column().Class("pf-modal").Class("pf-settings").Children({
			ui::Label("Réglages du profiler").Class("pf-title"),
			ui::Label("").Class("pf-detail").TextWrap().GrowWidth().Ref(m_settingsFile),
			SettingRow("Dossier des captures", "Où « Lancer un programme et capturer » écrit les fichiers .vcap", m_setDirectory,
					   DEFAULT_CAPTURES),
			SettingRow("Échantillonnage (Hz)", "Piles d'appels par seconde de temps CPU (0 : aucun échantillonnage)", m_setSampling, "1000"),
			SettingRow("Objectif (img/s)", "Cadence visée par le diagnostic (budget d'une image)", m_setTarget, "144"),
			SettingRow("Délai avant capture (s)", "Capture pilotée : attente avant de démarrer (chargement exclu)", m_setDelay, "0"),
			SettingRow("Durée de capture (s)", "Capture pilotée : durée ; 0 : toute l'exécution du programme", m_setDuration, "0"),
			SettingRow("Lignes des rapports", "Nombre de lignes des tableaux du rapport texte (profiler report)", m_setTop, "20"),
			ui::Checkbox("Arrêter le programme après une capture pilotée", true).Ref(m_setStop),
			ui::Label("").Class("pf-critical").TextWrap().GrowWidth().Ref(m_settingsError),
			ui::Row().Gap(6.f).Children({
				ui::Button("Valeurs par défaut").OnClick([this] { FillSettings(Settings{}); }),
				ui::Button("Oublier les récentes").Tooltip("Vide la liste Fichier › Captures récentes").OnClick([this] {
					m_settings.recentCaptures.clear();
					(void)m_settings.Save(m_settings.file);
					RefreshRecent();
					Status(String("captures récentes oubliées"));
				}),
				ui::Spacer(),
				ui::Button("Annuler").OnClick([this] { m_settingsModal.Close(); }),
				ui::Button("Enregistrer").OnClick([this] { ApplySettings(); }),
			}),
		})));
	}

	void FillSettings(const Settings &s) {
		m_setDirectory.SetText(s.capturesDirectory);
		m_setSampling.SetText(String::Format("%d", s.samplingHz));
		m_setTarget.SetText(String::Format("%g", s.targetFps));
		m_setDelay.SetText(String::Format("%g", s.delaySeconds));
		m_setDuration.SetText(String::Format("%g", s.durationSeconds));
		m_setTop.SetText(String::Format("%zu", s.top));
		m_setStop.SetChecked(s.stopAfterCapture);
		m_settingsError.SetText(String());
	}

	void OpenSettings() {
		FillSettings(m_settings);
		m_settingsFile.SetText(String("Fichier : ") + m_settings.file);
		m_settingsModal.Open();
		m_setDirectory.Focus();
	}

	/// Nombre lu dans un champ ; NONE (et message) s'il n'en est pas un ou
	/// sort de [min, max].
	Option<double> ReadNumber(ui::Widget field, const char *label, double min, double max) {
		const String text = field.Text().Trim();
		char *end = nullptr;
		const double v = std::strtod(text.CStr(), &end);
		if (text.IsEmpty() || !end || *end != '\0' || v < min || v > max) {
			m_settingsError.SetText(String::Format("%s : nombre entre %g et %g attendu", label, min, max));
			return NONE;
		}
		return Some(v);
	}

	void ApplySettings() {
		const Option<double> sampling = ReadNumber(m_setSampling, "Échantillonnage", 0, 20000);
		const Option<double> target = sampling.IsSome() ? ReadNumber(m_setTarget, "Objectif", 1, 1000) : NONE;
		const Option<double> delay = target.IsSome() ? ReadNumber(m_setDelay, "Délai", 0, 3600) : NONE;
		const Option<double> duration = delay.IsSome() ? ReadNumber(m_setDuration, "Durée", 0, 3600) : NONE;
		const Option<double> top = duration.IsSome() ? ReadNumber(m_setTop, "Lignes", 1, 1000) : NONE;
		if (top.IsNone())
			return;
		const String directory = m_setDirectory.Text().Trim();
		if (directory.IsEmpty()) {
			m_settingsError.SetText(String("Dossier des captures : chemin attendu"));
			return;
		}
		const bool targetChanged = target.Value() != m_settings.targetFps;
		m_settings.capturesDirectory = directory;
		m_settings.samplingHz = int(sampling.Value());
		m_settings.targetFps = target.Value();
		m_settings.delaySeconds = delay.Value();
		m_settings.durationSeconds = duration.Value();
		m_settings.top = size_t(top.Value());
		m_settings.stopAfterCapture = m_setStop.Checked();
		if (Result<bool, String> saved = m_settings.Save(m_settings.file); saved.IsError()) {
			m_settingsError.SetText(saved.Error());
			return;
		}
		m_settingsModal.Close();
		RefreshRecent(); // le dossier des captures a pu changer
		if (targetChanged && m_capture) {
			m_options.targetFps = m_settings.targetFps;
			Reanalyze(); // le diagnostic dépend de l'objectif
		}
		Status(String("réglages enregistrés dans ") + m_settings.file);
	}

	void Ask(const char *title, const char *help, const String &value, const String &second, bool showSecond,
			 std::function<void(const String &, const String &)> accept) {
		m_modalTitle.SetText(String(title));
		m_modalHelp.SetText(String(help));
		m_modalInput.SetText(value);
		m_modalSecond.SetText(second);
		m_modalSecond.SetVisible(showSecond);
		m_modalAccept = std::move(accept);
		m_modal.Open();
		m_modalInput.Focus();
	}

	void AcceptModal() {
		m_modal.Close();
		if (m_modalAccept)
			m_modalAccept(m_modalInput.Text().Trim(), m_modalSecond.Text().Trim());
	}

	void AskOpen() {
		const std::vector<String> captures = m_settings.Captures();
		const String suggestion = !m_path.IsEmpty() ? m_path : !captures.empty() ? captures.front() : m_settings.capturesDirectory + String("/");
		Ask("Ouvrir une capture", "Chemin d'un fichier .vcap (écrit par diagnostics::trace) — les captures sont rangées dans "
								  "data/profiler/captures/.",
			suggestion, String(), false,
			[this](const String &path, const String &) { Open(path); });
	}

	void AskCompare() {
		if (!m_capture) {
			Status(String("ouvrez d'abord une capture"));
			return;
		}
		Ask("Comparer avec une capture", "Capture « avant » : les écarts s'affichent dans le diagnostic.", String(), String(), false,
			[this](const String &path, const String &) {
				Result<Capture, String> other = LoadCapture(path);
				if (other.IsError()) {
					Status(other.Error());
					return;
				}
				Symbolizer symbols(other.Value());
				const Report before = Analyze(other.Value(), symbols, AnalysisOptions{});
				m_comparison = CompareReports(before, m_report);
				RefreshFindings();
				m_bottom.SetActive(0);
			});
	}

	void AskExport() {
		if (!m_capture)
			return;
		Ask("Exporter (Chrome Trace Event)", "À ouvrir dans chrome://tracing ou ui.perfetto.dev.", m_path + String(".json"), String(),
			false, [this](const String &path, const String &) {
				const Result<bool, String> r = ExportChromeTrace(*m_capture, path);
				Status(r.IsOk() ? String("exporté : ") + path : r.Error());
			});
	}

	void AskRecord() {
		Ask("Lancer un programme et capturer",
			"Ligne de commande (programme instrumenté), puis « délai durée » en secondes (vide : toute l'exécution). "
			"Exemple : build/host/release/bin/voxelia --world=perf, puis 20 5.",
			m_settings.lastCommand.IsEmpty() ? String("build/host/release/bin/voxelia --world=perf --studio") : m_settings.lastCommand,
			m_settings.durationSeconds > 0 ? String::Format("%g %g", m_settings.delaySeconds, m_settings.durationSeconds) : String("20 5"),
			true, [this](const String &command, const String &timing) {
				RecordOptions o;
				for (const String &part : command.Split(' '))
					if (!part.IsEmpty())
						o.command.push_back(part);
				const std::vector<String> t = timing.Split(' ');
				if (!t.empty() && !t[0].IsEmpty())
					o.delaySeconds = std::atof(t[0].CStr());
				if (t.size() > 1)
					o.durationSeconds = std::atof(t[1].CStr());
				if (o.command.empty())
					return;
				o.samplingHz = m_settings.samplingHz;
				o.stopAfterCapture = m_settings.stopAfterCapture;
				o.output = m_settings.NewCapturePath(o.command[0]);
				m_settings.lastCommand = command;
				m_settings.delaySeconds = o.delaySeconds;
				m_settings.durationSeconds = o.durationSeconds;
				(void)m_settings.Save(m_settings.file);
				const String output = o.output;
				StartJob(String("capture de ") + command, [o, output]() -> Result<String, String> {
					Result<String, String> r = Record(o);
					if (r.IsError() || r.Value() == output)
						return r;
					// Capture pilotée : « <sortie>.1.vcap » ramenée au nom choisi.
					std::error_code ec;
					std::filesystem::rename(r.Value().CStr(), output.CStr(), ec);
					return ec ? r : Result<String, String>(Ok(output));
				});
			});
	}

	void AskAttach() {
		Ask("Capturer un processus en cours",
			"Numéro du processus (lancé avec DIAG_TRACE=… et DIAG_TRACE_CONTROL=1), puis la durée en secondes.", String(), String("5"),
			true, [this](const String &pid, const String &duration) {
				const long p = std::atol(pid.CStr());
				const double d = std::max(0.5, std::atof(duration.CStr()));
				StartJob(String::Format("capture du processus %ld", p), [p, d] { return Attach(p, d); });
			});
	}

	/// Capture dans un fil (le programme tourne pendant ce temps), ouverte à la fin.
	void StartJob(const String &label, std::function<Result<String, String>()> job) {
		if (m_job.joinable()) {
			Status(String("une capture est déjà en cours"));
			return;
		}
		Status(label + String("…"));
		m_jobDone.store(false);
		m_job = std::thread([this, job = std::move(job)] {
			Result<String, String> r = job();
			std::lock_guard<std::mutex> lock(m_jobMutex);
			m_jobResult = r.IsOk() ? r.Value() : String();
			m_jobError = r.IsOk() ? String() : r.Error();
			m_jobDone.store(true);
		});
	}

	void PollJob() {
		if (!m_job.joinable() || !m_jobDone.load())
			return;
		m_job.join();
		std::lock_guard<std::mutex> lock(m_jobMutex);
		if (m_jobError.IsEmpty())
			Open(m_jobResult);
		else
			Status(m_jobError);
	}

	void Status(const String &text) { m_frame.SetStatus(text); }

	// ── Capture ─────────────────────────────────────────────────────────────

	void Open(const String &path) {
		Result<Capture, String> loaded = LoadCapture(path);
		if (loaded.IsError()) {
			Status(loaded.Error());
			return;
		}
		m_path = path;
		m_settings.Remember(path);
		(void)m_settings.Save(m_settings.file);
		RefreshRecent();
		m_capture = std::make_unique<Capture>(std::move(loaded.Value()));
		m_symbols = std::make_unique<Symbolizer>(*m_capture);
		m_hidden.clear();
		m_selectedZone = -1;
		m_comparison = String();
		m_options = AnalysisOptions{};
		m_options.targetFps = m_settings.targetFps;
		// Profondeur de zones par fil (hauteur des pistes).
		m_depths.clear();
		for (const ZoneEvent &z : m_capture->zones)
			m_depths[z.tid] = std::max(m_depths[z.tid], z.depth + 1);
		FitAll();
		Reanalyze();
		RefreshThreads();
		RefreshInfo();
		m_frame.SetTitle(String("Profiler — ") + path);
	}

	void Reanalyze() {
		if (!m_capture)
			return;
		m_report = Analyze(*m_capture, *m_symbols, m_options);
		RefreshFindings();
		RefreshZones();
		RefreshFunctions();
		RefreshTree(m_topDownPage, false);
		RefreshTree(m_bottomUpPage, true);
		Status(String::Format("%s — %.1f img/s, %zu échantillons, plage %s%s", m_path.CStr(), m_report.frames.fps, m_report.samples,
							  FormatTime(double(m_options.range.Length(*m_capture))).CStr(),
							  m_options.range.begin > 0 || m_options.range.end < m_capture->durationNs ? " (sélection)" : ""));
	}

	void RefreshThreads() {
		m_threadList.Clear();
		for (long tid : m_capture->ThreadIds()) {
			double share = 0;
			for (const ThreadStats &t : m_report.threads)
				if (t.tid == tid)
					share = t.cpuShare;
			const long id = tid;
			m_threadList.Append(ui::Row().Class("pf-thread").GrowWidth().Children({
				ui::Checkbox("", true).OnToggle([this, id](bool on) {
					if (on)
						m_hidden.erase(id);
					else
						m_hidden[id] = true;
				}),
				ui::Label(m_capture->ThreadName(tid)).Grow().Tooltip(String::Format("fil %ld", tid)),
				ui::Label(String::Format("%.0f %%", share * 100.0)).Class("pf-detail"),
			}));
		}
	}

	void Row(ui::Widget parent, const String &key, const String &value) {
		parent.Append(ui::Row().Gap(4.f).Children({ui::Label(key).Class("pf-key"), ui::Label(value).Class("pf-value").Grow()}));
	}

	void RefreshInfo() {
		m_info.Clear();
		const Capture &c = *m_capture;
		Row(m_info, String("Programme"), c.executable);
		Row(m_info, String("Processus"), String::Format("%ld", c.pid));
		Row(m_info, String("Durée"), FormatTime(double(c.durationNs)));
		Row(m_info, String("Images"), String::Format("%zu", c.frames.size()));
		Row(m_info, String("Zones"), String::Format("%zu (%zu perdues)", c.zones.size(), c.zonesDropped));
		Row(m_info, String("Échantillons"), String::Format("%zu à %d Hz (%zu perdus)", c.samples.size(), c.samplingHz, c.samplesDropped));
		Row(m_info, String("Fils"), String::Format("%zu", c.ThreadIds().size()));
		Row(m_info, String("Modules"), String::Format("%zu", c.modules.size()));
		Row(m_info, String("Compteurs matériels"), c.hardwareAvailable ? String("oui") : c.hardwareReason);
		for (const String &w : m_symbols->Warnings())
			Row(m_info, String("Symboles"), w);
	}

	void RefreshFindings() {
		m_findings.Clear();
		if (!m_comparison.IsEmpty()) {
			m_findings.Append(ui::Label("Comparaison avec la capture « avant »").Class("pf-title"));
			m_findings.Append(ui::Label(m_comparison).Class("pf-mono"));
		}
		for (const Finding &f : m_report.findings) {
			const char *cls = f.severity == Severity::Critical ? "pf-critical" : f.severity == Severity::Warning ? "pf-warning" : "pf-info";
			m_findings.Append(ui::Column().Class("pf-finding").GrowWidth().Children({
				ui::Label(String::Format("%s · %s — %s", SeverityLabel(f.severity), f.category.CStr(), f.title.CStr()))
					.Class("pf-title")
					.Class(cls),
				ui::Label(f.detail).Class("pf-detail").TextWrap().GrowWidth(),
			}));
		}
	}

	void RefreshZones() {
		m_zonesPage.Clear();
		std::vector<std::vector<String>> rows;
		for (const ZoneStats &z : m_report.zones)
			rows.push_back({z.label, String::Format("%llu", (unsigned long long)z.count), String::Format("%.3f", double(z.selfNs) / 1e6),
							String::Format("%.3f", double(z.totalNs) / 1e6), String::Format("%.4f", z.meanNs / 1e6),
							String::Format("%.4f", double(z.minNs) / 1e6), String::Format("%.4f", double(z.maxNs) / 1e6),
							String::Format("%.4f", z.stddevNs / 1e6), String::Format("%.3f", z.perFrameMs)});
		m_zonesPage.Append(ui::Table({{String("zone"), 220.f}, {String("nombre"), 70.f}, {String("propre ms"), 90.f},
									  {String("total ms"), 90.f}, {String("moy. ms"), 80.f}, {String("min ms"), 80.f},
									  {String("max ms"), 80.f}, {String("σ ms"), 80.f}, {String("ms/image"), 80.f}},
									 rows, false)
							   .Grow()
							   .OnSelect([this](int row) { SelectZoneStats(row); }));
	}

	void RefreshFunctions() {
		m_functionsPage.Clear();
		std::vector<std::vector<String>> rows;
		for (size_t i = 0; i < m_report.functions.size() && i < 400; ++i) {
			const FunctionStats &f = m_report.functions[i];
			rows.push_back({String::Format("%.2f", f.selfShare * 100.0), String::Format("%.2f", f.inclusiveShare * 100.0),
							String::Format("%llu", (unsigned long long)f.selfSamples), f.name, f.module});
		}
		m_functionsPage.Append(ui::Table({{String("propre %"), 75.f}, {String("inclus %"), 75.f}, {String("échant."), 70.f},
										  {String("fonction"), 520.f}, {String("module"), 140.f}},
										 rows, false)
								   .Grow()
								   .OnSelect([this](int row) { SelectFunction(row); }));
	}

	void AddTree(ui::Widget parent, const CallNode &node, uint64_t total, int depth) {
		size_t shown = 0;
		for (const CallNode &child : node.children) {
			const double share = total ? double(child.inclusive) / double(total) : 0.0;
			if (share < 0.005 || ++shown > 40)
				break;
			const String title = String::Format("%5.1f %%  (propre %4.1f %%)  %s", share * 100.0,
												total ? double(child.self) / double(total) * 100.0 : 0.0,
												m_symbols->Function(child.function).CStr());
			ui::Widget item = parent.Append(ui::TreeNode(title, int(child.function), depth < 2 && share > 0.05));
			if (depth < 24)
				AddTree(item, child, total, depth + 1);
		}
	}

	void RefreshTree(ui::Widget page, bool bottomUp) {
		page.Clear();
		const CallNode root = BuildCallTree(*m_capture, *m_symbols, m_options, bottomUp);
		page.Append(ui::Label(String::Format(bottomUp ? "Des fonctions qui consomment vers leurs appelants — %llu échantillons"
													  : "De l'entrée du fil vers les feuilles — %llu échantillons",
											 (unsigned long long)root.inclusive))
						.Class("pf-detail"));
		AddTree(page, root, root.inclusive, 0);
	}

	// ── Sélection ───────────────────────────────────────────────────────────

	void ShowSelection() {
		m_selection.Clear();
		if (!m_capture) {
			m_selection.Append(ui::Label("Ouvrez une capture (Fichier) ou lancez-en une (Capture).").Class("pf-detail").TextWrap().GrowWidth());
			return;
		}
		if (m_selectedZone < 0) {
			m_selection.Append(ui::Label("Cliquez une zone de la chronologie, une ligne des tables ; glissez pour choisir une plage.")
								   .Class("pf-detail")
								   .TextWrap()
								   .GrowWidth());
			return;
		}
		const ZoneEvent &z = m_capture->zones[size_t(m_selectedZone)];
		const String &name = m_capture->Name(z.name);
		m_selection.Append(ui::Label(name).Class("pf-title"));
		Row(m_selection, String("Fil"), m_capture->ThreadName(z.tid));
		Row(m_selection, String("Début"), FormatTime(double(z.start)));
		Row(m_selection, String("Durée"), FormatTime(double(z.Duration())));
		Row(m_selection, String("Profondeur"), String::Format("%u", z.depth));
		for (const ZoneStats &s : m_report.zones)
			if (s.name == z.name) {
				Row(m_selection, String("Appels"), String::Format("%llu", (unsigned long long)s.count));
				Row(m_selection, String("Moyenne"), FormatTime(s.meanNs));
				Row(m_selection, String("Min / max"), FormatTime(double(s.minNs)) + String(" / ") + FormatTime(double(s.maxNs)));
				Row(m_selection, String("Écart type"), FormatTime(s.stddevNs));
				Row(m_selection, String("Par image"), String::Format("%.3f ms", s.perFrameMs));
			}
		m_selection.Append(ui::Button("Cadrer cette zone").OnClick([this] {
			const ZoneEvent &zone = m_capture->zones[size_t(m_selectedZone)];
			const int64_t margin = std::max<int64_t>(1000, zone.Duration() / 4);
			m_viewBegin = double(zone.start - margin);
			m_viewEnd = double(zone.end + margin);
		}));
	}

	void SelectZoneStats(int row) {
		if (row < 0 || size_t(row) >= m_report.zones.size())
			return;
		const uint32_t name = m_report.zones[size_t(row)].name;
		// La plus longue occurrence de la zone dans la plage.
		int64_t best = -1;
		for (size_t i = 0; i < m_capture->zones.size(); ++i) {
			const ZoneEvent &z = m_capture->zones[i];
			if (z.name == name && m_options.range.Contains(z.start) &&
				(best < 0 || z.Duration() > m_capture->zones[size_t(best)].Duration()))
				best = int64_t(i);
		}
		m_selectedZone = best;
		ShowSelection();
	}

	void SelectFunction(int row) {
		if (row < 0 || size_t(row) >= m_report.functions.size())
			return;
		const FunctionStats &f = m_report.functions[size_t(row)];
		m_selection.Clear();
		m_selection.Append(ui::Label(f.name).Class("pf-title").TextWrap().GrowWidth());
		Row(m_selection, String("Module"), f.module);
		Row(m_selection, String("Temps propre"), String::Format("%.2f %% (%llu échantillons)", f.selfShare * 100.0, (unsigned long long)f.selfSamples));
		Row(m_selection, String("Temps inclus"), String::Format("%.2f %%", f.inclusiveShare * 100.0));
		// Ligne de source la plus échantillonnée (addr2line).
		std::unordered_map<uint64_t, size_t> hits;
		for (const StackSample &s : m_capture->samples)
			if (s.depth > 0 && m_options.range.Contains(s.time)) {
				const uint64_t address = m_capture->stackFrames[s.first];
				if (m_symbols->Resolve(address).function == f.function)
					++hits[address];
			}
		std::vector<std::pair<size_t, uint64_t>> ranked;
		for (const auto &[address, count] : hits)
			ranked.push_back({count, address});
		std::sort(ranked.rbegin(), ranked.rend());
		if (!ranked.empty())
			m_selection.Append(ui::Label("Lignes les plus échantillonnées").Class("pf-title"));
		for (size_t i = 0; i < ranked.size() && i < 8; ++i) {
			const String line = m_symbols->SourceLine(ranked[i].second);
			Row(m_selection, String::Format("%zu échant.", ranked[i].first),
				line.IsEmpty() ? String::Format("0x%llx (sans informations de débogage)", (unsigned long long)ranked[i].second) : line);
		}
	}

	// ── Chronologie ─────────────────────────────────────────────────────────

	void FitAll() {
		if (!m_capture)
			return;
		m_viewBegin = 0.0;
		m_viewEnd = double(std::max<int64_t>(1, m_capture->durationNs));
	}

	void FitSelection() {
		if (m_options.range.begin > 0 || m_options.range.end < (m_capture ? m_capture->durationNs : 0)) {
			m_viewBegin = double(m_options.range.begin);
			m_viewEnd = double(std::min<int64_t>(m_options.range.end, m_capture->durationNs));
		}
	}

	void ClearSelection() {
		if (!m_capture)
			return;
		m_options.range = TimeRange{};
		m_options.targetFps = m_settings.targetFps;
		m_selecting = false;
		Reanalyze();
	}

	[[nodiscard]] double TimeAt(float x, const sdl3::FRect &r) const {
		const float w = std::max(1.f, r.w - GUTTER);
		return m_viewBegin + double(x - r.x - GUTTER) / double(w) * (m_viewEnd - m_viewBegin);
	}
	[[nodiscard]] float XAt(double t, const sdl3::FRect &r) const {
		const float w = std::max(1.f, r.w - GUTTER);
		return r.x + GUTTER + float((t - m_viewBegin) / (m_viewEnd - m_viewBegin) * double(w));
	}

	void HandleTimeline(const sdl3::Event &e, bool consumed) {
		if (!m_capture)
			return;
		const sdl3::FRect r = m_timeline.Bounds();
		auto inside = [&](float x, float y) { return x >= r.x + GUTTER && y >= r.y && x < r.x + r.w && y < r.y + r.h; };
		const uint32_t type = e.raw.type;
		if (type == SDL_EVENT_MOUSE_WHEEL && !consumed && inside(e.raw.wheel.mouse_x, e.raw.wheel.mouse_y)) {
			const double pivot = TimeAt(e.raw.wheel.mouse_x, r);
			const double k = std::pow(0.8, double(e.raw.wheel.y));
			const double span = std::max(1000.0, (m_viewEnd - m_viewBegin) * k);
			const double f = (pivot - m_viewBegin) / (m_viewEnd - m_viewBegin);
			m_viewBegin = pivot - f * span;
			m_viewEnd = m_viewBegin + span;
		} else if (type == SDL_EVENT_MOUSE_BUTTON_DOWN && !consumed && inside(e.raw.button.x, e.raw.button.y)) {
			if (e.raw.button.button == SDL_BUTTON_LEFT) {
				m_selecting = true;
				m_pressX = e.raw.button.x;
				m_selectFrom = m_selectTo = TimeAt(e.raw.button.x, r);
			} else {
				m_panning = true;
				m_pressX = e.raw.button.x;
			}
		} else if (type == SDL_EVENT_MOUSE_MOTION) {
			if (m_selecting)
				m_selectTo = TimeAt(e.raw.motion.x, r);
			else if (m_panning) {
				const double perPixel = (m_viewEnd - m_viewBegin) / double(std::max(1.f, r.w - GUTTER));
				m_viewBegin -= double(e.raw.motion.xrel) * perPixel;
				m_viewEnd -= double(e.raw.motion.xrel) * perPixel;
			}
			m_mouse = {e.raw.motion.x, e.raw.motion.y};
		} else if (type == SDL_EVENT_MOUSE_BUTTON_UP) {
			if (m_selecting && e.raw.button.button == SDL_BUTTON_LEFT) {
				m_selecting = false;
				if (std::fabs(e.raw.button.x - m_pressX) < 4.f) {
					PickZone(e.raw.button.x, e.raw.button.y);
				} else {
					m_options.range.begin = int64_t(std::max(0.0, std::min(m_selectFrom, m_selectTo)));
					m_options.range.end = int64_t(std::max(m_selectFrom, m_selectTo));
					Reanalyze();
				}
			}
			m_panning = false;
		} else if (type == SDL_EVENT_KEY_DOWN && !consumed && !m_modal.IsOpen() && !m_settingsModal.IsOpen()) {
			const double span = m_viewEnd - m_viewBegin;
			switch (e.raw.key.key) {
			case SDLK_A: m_viewBegin -= span * 0.1, m_viewEnd -= span * 0.1; break;
			case SDLK_D: m_viewBegin += span * 0.1, m_viewEnd += span * 0.1; break;
			case SDLK_W: m_viewBegin += span * 0.15, m_viewEnd -= span * 0.15; break;
			case SDLK_S: m_viewBegin -= span * 0.2, m_viewEnd += span * 0.2; break;
			case SDLK_F: FitSelection(); break;
			case SDLK_HOME: FitAll(); break;
			case SDLK_ESCAPE: ClearSelection(); break;
			default: break;
			}
		}
	}

	void PickZone(float x, float y) {
		m_selectedZone = -1;
		for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it)
			if (it->first.Contains(sdl3::FPoint{x, y})) {
				m_selectedZone = int64_t(it->second);
				break;
			}
		ShowSelection();
	}

	void DrawTimeline(ui::Painter &p, const sdl3::FRect &r) {
		m_hits.clear();
		p.FillRect(r, ui::Rgba(24, 26, 32));
		sdl3::Font *font = p.GetFonts() ? p.GetFonts()->Resolve(StringView(""), 12.f) : nullptr;
		if (!m_capture) {
			if (font)
				p.Text(String("Aucune capture — Fichier › Ouvrir, ou Capture › Lancer un programme."), font, ui::Rgba(150, 156, 178),
					   r.x + 20.f, r.y + 20.f);
			return;
		}
		const Capture &c = *m_capture;
		const float left = r.x + GUTTER, right = r.x + r.w;
		auto text = [&](const String &s, float x, float y, sdl3::FColor color) {
			if (font)
				p.Text(s, font, color, x, y);
		};

		// Règle graduée.
		const double span = m_viewEnd - m_viewBegin;
		double step = std::pow(10.0, std::floor(std::log10(std::max(1.0, span / 8.0))));
		if (span / step > 16)
			step *= 5;
		else if (span / step > 8)
			step *= 2;
		p.FillRect({r.x, r.y, r.w, RULER}, ui::Rgba(32, 35, 43));
		for (double t = std::ceil(m_viewBegin / step) * step; t < m_viewEnd; t += step) {
			const float x = XAt(t, r);
			p.Line({x, r.y + RULER - 6.f}, {x, r.y + r.h}, 1.f, ui::Rgba(44, 48, 58));
			text(FormatTime(t), x + 3.f, r.y + 3.f, ui::Rgba(150, 156, 178));
		}
		float y = r.y + RULER + 4.f;
		p.PushClip({r.x, r.y + RULER, r.w, r.h - RULER});

		// Images : barres de durée, rouge au-delà du budget.
		const double budgetMs = 1000.0 / std::max(1.0, m_report.targetFps);
		const double scaleMs = std::max(budgetMs * 2.0, m_report.frames.p99Ms * 1.2);
		text(String::Format("Images (budget %.1f ms)", budgetMs), r.x + 6.f, y + 2.f, ui::Rgba(210, 214, 226));
		for (size_t i = 1; i < c.frames.size(); ++i) {
			const double a = double(c.frames[i - 1]), b = double(c.frames[i]);
			if (b < m_viewBegin || a > m_viewEnd)
				continue;
			const double ms = (b - a) / 1e6;
			const float x0 = XAt(a, r), x1 = std::max(x0 + 1.f, XAt(b, r) - 1.f);
			const float h = float(std::min(1.0, ms / scaleMs)) * (FRAME_TRACK - 4.f);
			p.FillRect({x0, y + FRAME_TRACK - h, x1 - x0, h}, ms > budgetMs ? ui::Rgba(220, 90, 70) : ui::Rgba(96, 190, 110));
		}
		const float budgetY = y + FRAME_TRACK - float(budgetMs / scaleMs) * (FRAME_TRACK - 4.f);
		p.Line({left, budgetY}, {right, budgetY}, 1.f, ui::Rgba(240, 200, 80, 180));
		y += FRAME_TRACK + 6.f;

		// Mémoire résidente.
		if (!c.process.empty()) {
			double peak = 1.0;
			for (const ProcessSample &s : c.process)
				peak = std::max(peak, double(s.rss));
			text(String::Format("Mémoire (pic %.0f Mo)", peak / 1048576.0), r.x + 6.f, y + 2.f, ui::Rgba(210, 214, 226));
			Option<sdl3::FPoint> last = NONE;
			for (const ProcessSample &s : c.process) {
				if (double(s.time) < m_viewBegin - span || double(s.time) > m_viewEnd + span)
					continue;
				const sdl3::FPoint pt{XAt(double(s.time), r), y + VALUE_TRACK - 2.f - float(double(s.rss) / peak) * (VALUE_TRACK - 6.f)};
				if (last.IsSome())
					p.Line(last.Value(), pt, 1.5f, ui::Rgba(120, 170, 240));
				last = Some(pt);
			}
			y += VALUE_TRACK + 6.f;
		}

		// Fils : état, zones empilées, échantillons.
		for (long tid : c.ThreadIds()) {
			if (m_hidden.count(tid))
				continue;
			const uint32_t depth = std::min<uint32_t>(MAX_DEPTH_ROWS, m_depths.count(tid) ? m_depths[tid] : 0);
			const float height = STATE_ROW + 4.f + float(depth) * ROW + 6.f;
			if (y > r.y + r.h)
				break;
			p.FillRect({r.x, y, r.w, 1.f}, ui::Rgba(44, 48, 58));
			text(c.ThreadName(tid), r.x + 6.f, y + 3.f, ui::Rgba(220, 224, 236));
			// États observés (bande).
			const TaskSample *prev = nullptr;
			for (const TaskSample &s : c.tasks) {
				if (s.tid != tid)
					continue;
				if (prev && double(s.time) >= m_viewBegin && double(prev->time) <= m_viewEnd) {
					const float x0 = XAt(double(prev->time), r), x1 = XAt(double(s.time), r);
					// Part du processeur sur l'intervalle : plus clair = plus occupé.
					const double busy = s.time > prev->time ? double(s.cpuNs - prev->cpuNs) / double(s.time - prev->time) : 0.0;
					sdl3::FColor col = StateColor(s.state);
					if (s.state == 'R' || busy > 0.05)
						col = editor::Mix(ui::Rgba(70, 76, 92), ui::Rgba(96, 200, 96), float(std::min(1.0, busy)));
					p.FillRect({std::max(left, x0), y + 2.f, std::max(1.f, std::min(right, x1) - std::max(left, x0)), STATE_ROW}, col);
				}
				prev = &s;
			}
			const float zonesY = y + STATE_ROW + 4.f;
			// Zones (triées par fil puis début) : seules les visibles.
			auto first = std::lower_bound(c.zones.begin(), c.zones.end(), tid, [](const ZoneEvent &z, long t) { return z.tid < t; });
			for (auto it = first; it != c.zones.end() && it->tid == tid; ++it) {
				const ZoneEvent &z = *it;
				if (double(z.end) < m_viewBegin || double(z.start) > m_viewEnd || z.depth >= MAX_DEPTH_ROWS)
					continue;
				const float x0 = std::max(left, XAt(double(z.start), r)), x1 = std::min(right, XAt(double(z.end), r));
				if (x1 - x0 < 0.4f)
					continue;
				const sdl3::FRect box{x0, zonesY + float(z.depth) * ROW, std::max(1.f, x1 - x0 - 0.5f), ROW - 2.f};
				const size_t index = size_t(it - c.zones.begin());
				const String &name = c.Name(z.name);
				p.FillRect(box, int64_t(index) == m_selectedZone ? ui::Rgba(255, 220, 90) : ZoneColor(name));
				if (box.w > 40.f) {
					const size_t chars = size_t(box.w / 6.5f);
					text(name.Length() > chars ? name.Substr(0, chars > 1 ? chars - 1 : 0) + String("…") : name, box.x + 3.f, box.y + 1.f,
						 ui::Rgba(16, 18, 22));
				}
				if (m_hits.size() < 200000)
					m_hits.push_back({box, index});
			}
			// Échantillons de pile : petites marques sous les zones.
			for (const StackSample &s : c.samples)
				if (s.tid == tid && double(s.time) >= m_viewBegin && double(s.time) <= m_viewEnd) {
					const float x = XAt(double(s.time), r);
					p.FillRect({x, zonesY + float(depth) * ROW + 1.f, 1.f, 3.f}, ui::Rgba(230, 230, 240, 140));
				}
			y += height;
		}
		p.PopClip();

		// Plage sélectionnée (ou en cours de sélection).
		auto shade = [&](double a, double b) {
			const float x0 = std::max(left, XAt(std::min(a, b), r)), x1 = std::min(right, XAt(std::max(a, b), r));
			if (x1 > x0)
				p.FillRect({x0, r.y + RULER, x1 - x0, r.h - RULER}, ui::Rgba(90, 140, 230, 50));
		};
		if (m_selecting)
			shade(m_selectFrom, m_selectTo);
		else if (m_options.range.begin > 0 || m_options.range.end < c.durationNs)
			shade(double(m_options.range.begin), double(std::min(m_options.range.end, c.durationNs)));
		// Curseur : temps sous la souris.
		if (m_mouse.x >= left && m_mouse.x < right && m_mouse.y >= r.y && m_mouse.y < r.y + r.h) {
			p.Line({m_mouse.x, r.y + RULER}, {m_mouse.x, r.y + r.h}, 1.f, ui::Rgba(255, 255, 255, 60));
			text(FormatTime(TimeAt(m_mouse.x, r)), m_mouse.x + 4.f, r.y + RULER + 2.f, ui::Rgba(255, 255, 255));
		}
	}

	// ── État ────────────────────────────────────────────────────────────────

	sdl3::Window &m_window;
	sdl3::Renderer &m_renderer;
	Settings m_settings;
	ecs::ArchetypeRegistry m_world;
	long m_frameCount = 0;
	ui::Ui m_gui;
	ui::WindowFrame m_frame;
	editor::DockPanel m_left, m_center, m_bottom, m_right;
	ui::Widget m_root, m_threadList, m_info, m_timeline, m_findings, m_zonesPage, m_functionsPage, m_topDownPage, m_bottomUpPage, m_selection;
	ui::Widget m_recentMenu;
	ui::Widget m_settingsModal, m_settingsFile, m_settingsError, m_setDirectory, m_setSampling, m_setTarget, m_setDelay, m_setDuration,
		m_setTop, m_setStop;
	ui::Widget m_modal, m_modalTitle, m_modalHelp, m_modalInput, m_modalSecond;
	std::function<void(const String &, const String &)> m_modalAccept;

	String m_path;
	std::unique_ptr<Capture> m_capture;
	std::unique_ptr<Symbolizer> m_symbols;
	AnalysisOptions m_options;
	Report m_report;
	String m_comparison;
	std::unordered_map<long, uint32_t> m_depths;
	std::unordered_map<long, bool> m_hidden;

	double m_viewBegin = 0.0, m_viewEnd = 1.0;
	bool m_selecting = false, m_panning = false;
	double m_selectFrom = 0.0, m_selectTo = 0.0;
	float m_pressX = 0.f;
	sdl3::FPoint m_mouse{-1.f, -1.f};
	std::vector<std::pair<sdl3::FRect, size_t>> m_hits;
	int64_t m_selectedZone = -1;

	std::thread m_job;
	std::mutex m_jobMutex;
	std::atomic<bool> m_jobDone{false};
	String m_jobResult, m_jobError;
};

} // namespace

int RunViewer(const String &capture, long frames, const String &screenshot, const String &settingsPath) {
	auto sdl = sdl3::SdlContext::Create(sdl3::init_flags::VIDEO | sdl3::init_flags::EVENTS);
	if (!sdl) {
		std::fprintf(stderr, "SDL : %s\n", sdl.Error().CStr());
		return 1;
	}
	auto ttf = sdl3::TtfContext::Create();
	if (!ttf) {
		std::fprintf(stderr, "TTF : %s\n", ttf.Error().CStr());
		return 1;
	}
	auto window = sdl3::Window::Create("Profiler", 1500, 920, ui::WindowFrame::WINDOW_FLAGS);
	if (!window) {
		std::fprintf(stderr, "fenêtre : %s\n", window.Error().CStr());
		return 1;
	}
	auto renderer = sdl3::Renderer::Create(window.Value());
	if (!renderer) {
		std::fprintf(stderr, "renderer : %s\n", renderer.Error().CStr());
		return 1;
	}
	(void)renderer.Value().SetVSync(sdl3::Renderer::VSync::EVERY_REFRESH);
	Viewer viewer(window.Value(), renderer.Value(), Settings::Load(settingsPath));
	return viewer.Run(capture, frames, screenshot);
}

} // namespace profiler
