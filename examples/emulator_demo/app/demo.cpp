#include "demo.hpp"

#include <cstdio>

#include "sdl3/filesystem.hpp"
#include "sdl3/image.hpp"
#include "sdl3/time.hpp"

#include "../emulator/settings.hpp"
#include "application.hpp"
#include "emulator_session.hpp"
#include "rom_metadata.hpp"
#include "rom_source.hpp"

namespace emulator_demo {

namespace {

void EnsureParentDirectory(const String &path) {
	size_t slash = path.Rfind('/');
	if (slash != String::NPOS && slash > 0)
		(void)sdl3::filesystem::CreateDirectory(path.Substr(0, slash));
}

[[nodiscard]] String JoinPath(const String &directory, const char *file) {
	if (directory.IsEmpty())
		return String(file);
	return directory.EndsWith("/") ? directory + file : String::Format("%s/%s", directory.CStr(), file);
}

} // namespace

Demo::Demo(CommandLine options, String commandLine) : m_options(std::move(options)) {
	m_report.commandLine = std::move(commandLine);
	m_report.configPath = m_options.configPath;
	app::SetArchivePassword(m_options.archivePassword);
}

Demo::~Demo() = default;

int Demo::Run() {
	if (m_options.showHelp) {
		std::fputs(CommandLine::HelpText(), stdout);
		return 0;
	}
	if (m_options.listButtons) {
		ListButtons();
		return 0;
	}
	if (m_options.listRomsDir.IsSome())
		return ListRoms(m_options.listRomsDir.Value());
	if (m_options.romInfo)
		return PrintRomInfo();

	// Branché avant tout le reste : les messages de démarrage (cœur, BIOS
	// manquant…) doivent atteindre le rapport.
	m_log = std::make_unique<app::LogConsole>(m_options.verbose);
	return m_options.headless ? RunHeadless() : RunWindowed();
}

// ── Modes d'information ─────────────────────────────────────────────────────

void Demo::ListButtons() {
	std::fputs("Boutons utilisables avec --press=N:BOUTON[:DURÉE] :\n ", stdout);
	for (int button = 0; button < BTN_COUNT; ++button)
		std::printf(" %s", ButtonName(button));
	std::fputs("\n(L, R, X et Y n'existent pas sur GB/GBC ; X et Y pas sur GBA.)\n", stdout);
}

int Demo::PrintRomInfo() {
	if (m_options.romPath.IsEmpty()) {
		std::fputs("--rom-info : précisez la ROM (--rom=CHEMIN)\n", stderr);
		return 2;
	}
	std::printf("Fichier      : %s\n", m_options.romPath.CStr());
	if (app::IsArchivePath(m_options.romPath)) {
		auto roms = app::ListArchiveRoms(m_options.romPath);
		if (roms.IsError()) {
			std::fprintf(stderr, "%s\n", roms.Error().CStr());
			return 1;
		}
		std::printf("ROMs         : %d dans l'archive\n", int(roms.Value().size()));
		for (const app::RomCandidate &candidate : roms.Value())
			std::printf("  · %s (%llu octets)\n", candidate.entry.CStr(), static_cast<unsigned long long>(candidate.size));
	}
	auto rom = app::LoadRom(m_options.romPath, m_options.romEntry);
	if (rom.IsError()) {
		std::fprintf(stderr, "%s\n", rom.Error().CStr());
		return 1;
	}
	if (rom.Value().archive.IsSome()) {
		const app::ArchiveOrigin &origin = rom.Value().archive.Value();
		const String stored = origin.storedBytes == 0
								  ? String("bloc compressé partagé")
								  : String::Format("%llu octets stockés", static_cast<unsigned long long>(origin.storedBytes));
		std::printf("Archive      : %s, entrée « %s », %s (%s%s)\n", data::archive::FormatName(origin.format),
					origin.entryName.CStr(), stored.CStr(), origin.method.CStr(), origin.encrypted ? ", chiffrée" : "");
	}
	Option<app::RomMetadata> metadata = app::ReadRomMetadataFromBytes(rom.Value().bytes);
	if (metadata.IsNone()) {
		std::fprintf(stderr, "%s : en-tête de ROM non reconnu\n", rom.Value().DisplayName().CStr());
		return 1;
	}
	for (const auto &[key, value] : app::DescribeRom(metadata.Value())) {
		// Alignement en CARACTÈRES : printf compterait les octets UTF-8.
		String padded = key;
		while (padded.ULength() < 13)
			padded.Concat(" ");
		std::printf("%s: %s\n", padded.CStr(), value.CStr());
	}
	return 0;
}

int Demo::ListRoms(const String &directory) {
	std::vector<String> problems;
	std::vector<app::RomCandidate> roms = app::FindRoms(directory, &problems);
	for (const String &problem : problems)
		std::fprintf(stderr, "archive ignorée : %s\n", problem.CStr());
	if (roms.empty()) {
		std::printf("Aucune ROM (.nds .gba .gbc .gb, ni dans une archive) dans %s\n", directory.CStr());
		return 0;
	}
	std::printf("%-4s %10s  %-28s %-22s %s\n", "SYS", "OCTETS", "TITRE", "ÉDITEUR", "FICHIER");
	for (const app::RomCandidate &candidate : roms) {
		auto rom = app::LoadRom(candidate.path, candidate.entry);
		Option<app::RomMetadata> metadata =
			rom.IsOk() ? app::ReadRomMetadataFromBytes(rom.Value().bytes) : Option<app::RomMetadata>(NONE);
		if (metadata.IsNone()) {
			std::printf("%-4s %10s  %-28s %-22s %s\n", "?", "-",
						rom.IsError() ? "(illisible)" : "(en-tête non reconnu)", "-", candidate.DisplayName().CStr());
			continue;
		}
		const app::RomMetadata &meta = metadata.Value();
		// Les titres de bannière NDS tiennent sur plusieurs lignes.
		String title = meta.title.Replace('\n', ' ');
		std::printf("%-4s %10lld  %-28s %-22s %s\n", app::RomSystemName(meta.system),
					static_cast<long long>(meta.sizeBytes), title.Truncate(28).CStr(),
					meta.publisher.Truncate(22).CStr(), candidate.DisplayName().CStr());
	}
	return 0;
}

// ── Réglages ────────────────────────────────────────────────────────────────

void Demo::LoadSettings() {
	if (!Settings::load(m_options.configPath))
		m_report.warnings.push_back(
			String::Format("réglages absents (%s) : valeurs par défaut", m_options.configPath.CStr()));

	m_saved.directBoot = Settings::getDirectBoot();
	m_saved.arm7Hle = Settings::getArm7Hle();
	m_saved.threaded2D = Settings::getThreaded2D();
	m_saved.threaded3D = Settings::getThreaded3D();
	m_saved.screenLayout = Settings::getScreenLayout();
	m_saved.fpsLimiter = Settings::getFpsLimiter();
	m_saved.highRes3D = Settings::getHighRes3D();
	m_saved.bios9 = Settings::getBios9Path();
	m_saved.bios7 = Settings::getBios7Path();
	m_saved.firmware = Settings::getFirmwarePath();
	m_saved.gbaBios = Settings::getGbaBiosPath();
	ApplyOverrides();
	DescribeSettings();
}

void Demo::ApplyOverrides() {
	if (m_options.directBoot.IsSome())
		Settings::setDirectBoot(m_options.directBoot.Unwrap() ? 1 : 0);
	if (m_options.arm7Hle.IsSome())
		Settings::setArm7Hle(m_options.arm7Hle.Unwrap() ? 1 : 0);
	if (m_options.threaded2D.IsSome())
		Settings::setThreaded2D(m_options.threaded2D.Unwrap() ? 1 : 0);
	if (m_options.threaded3D.IsSome())
		Settings::setThreaded3D(m_options.threaded3D.Unwrap() ? 1 : 0);
	if (m_options.screenLayout.IsSome())
		Settings::setScreenLayout(m_options.screenLayout.Unwrap());
	if (!m_options.saveDir.IsEmpty()) {
		(void)sdl3::filesystem::CreateDirectory(m_options.saveDir);
		Settings::setSaveDirectory(m_options.saveDir);
	}
	if (!m_options.biosDir.IsEmpty()) {
		// Deux dispositions acceptées : fichiers à plat dans le dossier, ou
		// rangés par console comme dans les réglages par défaut
		// (nintendo-nintendo-ds/bios9.bin, nintendo-game-boy-advance/…).
		auto locate = [this](const char *subdirectory, const char *file) {
			String flat = JoinPath(m_options.biosDir, file);
			if (sdl3::filesystem::PathInfo(flat).IsSome())
				return flat;
			String nested = JoinPath(JoinPath(m_options.biosDir, subdirectory), file);
			return sdl3::filesystem::PathInfo(nested).IsSome() ? nested : flat;
		};
		Settings::setBios9Path(locate("nintendo-nintendo-ds", "bios9.bin"));
		Settings::setBios7Path(locate("nintendo-nintendo-ds", "bios7.bin"));
		Settings::setFirmwarePath(locate("nintendo-nintendo-ds", "firmware.bin"));
		Settings::setGbaBiosPath(locate("nintendo-game-boy-advance", "gba_bios.bin"));
	}
}

void Demo::RestoreOverridesBeforeSave() {
	// Rétablit uniquement ce qui a été forcé en ligne de commande ET n'a pas
	// été modifié depuis dans l'interface ; limiteur et haute résolution sont
	// pilotés par la session, jamais par l'utilisateur de la démo.
	auto restore = [](bool overridden, int current, int forced, int saved, void (*setter)(int)) {
		if (overridden && current == forced)
			setter(saved);
	};
	if (m_options.directBoot.IsSome())
		restore(true, Settings::getDirectBoot(), m_options.directBoot.Unwrap() ? 1 : 0, m_saved.directBoot,
				&Settings::setDirectBoot);
	if (m_options.arm7Hle.IsSome())
		restore(true, Settings::getArm7Hle(), m_options.arm7Hle.Unwrap() ? 1 : 0, m_saved.arm7Hle,
				&Settings::setArm7Hle);
	if (m_options.threaded2D.IsSome())
		restore(true, Settings::getThreaded2D(), 1, m_saved.threaded2D, &Settings::setThreaded2D);
	if (m_options.threaded3D.IsSome())
		restore(true, Settings::getThreaded3D(), 1, m_saved.threaded3D, &Settings::setThreaded3D);
	if (m_options.screenLayout.IsSome())
		restore(true, Settings::getScreenLayout(), m_options.screenLayout.Unwrap(), m_saved.screenLayout,
				&Settings::setScreenLayout);
	if (!m_options.biosDir.IsEmpty()) {
		Settings::setBios9Path(m_saved.bios9);
		Settings::setBios7Path(m_saved.bios7);
		Settings::setFirmwarePath(m_saved.firmware);
		Settings::setGbaBiosPath(m_saved.gbaBios);
	}
	Settings::setFpsLimiter(m_saved.fpsLimiter);
	Settings::setHighRes3D(m_saved.highRes3D);
}

void Demo::DescribeSettings() {
	m_report.directBoot = Settings::getDirectBoot() != 0;
	m_report.arm7Hle = Settings::getArm7Hle() != 0;
	m_report.threaded2D = Settings::getThreaded2D() != 0;
	m_report.threaded3D = Settings::getThreaded3D() != 0;
	m_report.screenLayout = Settings::getScreenLayout() != 0 ? "empilés" : "côte à côte";
	m_report.saveDirectory =
		Settings::getSaveDirectory().IsEmpty() ? String("(à côté de la ROM)") : Settings::getSaveDirectory();
	m_report.firmwareFiles.clear();
	for (const String &path : {Settings::getBios9Path(), Settings::getBios7Path(), Settings::getFirmwarePath(),
							   Settings::getGbaBiosPath()})
		m_report.firmwareFiles.emplace_back(path, sdl3::filesystem::PathInfo(path).IsSome());
}

// ── Mode sans écran ─────────────────────────────────────────────────────────

void Demo::WriteFramebufferPng(const app::FrameSnapshot &snapshot, const String &path) {
	ScreenshotRecord record;
	record.frame = snapshot.frame;
	record.path = path;
	record.source = "framebuffer";
	if (snapshot.pixels.empty()) {
		record.error = String("aucune image produite par le cœur");
	} else {
		EnsureParentDirectory(path);
		// Pixels 0xAABBGGRR, soit R,G,B,A en mémoire : RGBA32.
		auto surface = sdl3::Surface::CreateFromPixels(snapshot.width, snapshot.height, sdl3::PixelFormat::RGBA32,
													   snapshot.pixels.data(), snapshot.width * int(sizeof(uint32_t)));
		if (surface.IsError()) {
			record.error = String(surface.Error());
		} else {
			auto saved = sdl3::ImgSavePng(surface.Value(), path);
			if (saved.IsError())
				record.error = String(saved.Error());
			else
				record.written = true;
		}
	}
	m_report.screenshots.push_back(std::move(record));
}

int Demo::RunHeadless() {
	m_report.mode = "sans écran";
	m_report.frameStatsLabel = "calcul d'une image émulée (cadence libre)";
	const uint64_t startTicks = sdl3::GetTicksMS();
	LoadSettings();

	if (m_options.romPath.IsEmpty()) {
		m_report.failure = String("--headless requiert une ROM (--rom=CHEMIN)");
		return Finish(startTicks);
	}
	m_report.romPath = m_options.romPath;

	app::SessionOptions sessionOptions;
	sessionOptions.statePath = m_options.statePath;
	sessionOptions.romEntry = m_options.romEntry;
	sessionOptions.extractDirectory = m_options.extractDir;
	sessionOptions.threaded = false;
	sessionOptions.unlimitedSpeed = true;
	sessionOptions.threads = &m_threads;
	sessionOptions.presses = m_options.presses;
	sessionOptions.stateActions = m_options.stateActions;
	auto created = app::EmulatorSession::Create(m_options.romPath, std::move(sessionOptions));
	if (created.IsError()) {
		m_report.bootError = created.Error();
		m_report.failure = String::Format("démarrage de la ROM impossible : %s", created.Error().CStr());
		// L'en-tête reste utile au diagnostic (BIOS manquant pour telle console…).
		if (auto rom = app::LoadRom(m_options.romPath, m_options.romEntry); rom.IsOk())
			if (Option<app::RomMetadata> metadata = app::ReadRomMetadataFromBytes(rom.Value().bytes); metadata.IsSome())
				app::DescribeRomInReport(m_report, metadata.Value());
		return Finish(startTicks);
	}
	std::unique_ptr<app::EmulatorSession> session = std::move(created).Unwrap();
	m_report.booted = true;
	app::DescribeSessionInReport(m_report, *session);
	DescribeSettings();

	const long budget = m_options.frames > 0 ? m_options.frames : CommandLine::DEFAULT_HEADLESS_FRAMES;
	const double tickRate = double(sdl3::GetPerformanceFrequency());
	const uint64_t emulationStart = sdl3::GetTicksMS();
	std::vector<bool> shotTaken(m_options.screenshots.size(), false);
	app::FrameSnapshot snapshot;
	uint64_t lastHash = 0;
	long distinctFrames = 0;

	for (long frame = 1; frame <= budget; ++frame) {
		const uint64_t frameStart = sdl3::GetPerformanceCounter();
		session->RunFrame();
		m_report.frames.Push(double(sdl3::GetPerformanceCounter() - frameStart) / tickRate);

		if (session->CopyLatestFrame(snapshot)) {
			uint64_t hash = HashPixels(snapshot.pixels);
			if (distinctFrames == 0 || hash != lastHash)
				++distinctFrames;
			lastHash = hash;
		}
		for (size_t i = 0; i < m_options.screenshots.size(); ++i) {
			if (!shotTaken[i] && frame >= m_options.screenshots[i].frame) {
				shotTaken[i] = true;
				WriteFramebufferPng(snapshot, m_options.screenshots[i].path);
			}
		}
		if (m_options.screenshotEvery > 0 && frame % m_options.screenshotEvery == 0)
			WriteFramebufferPng(snapshot,
								String::Format("%s/emulator-%06ld.png", m_options.screenshotDir.CStr(), frame));
		if (frame % 60 == 0) {
			int fps = session->CoreFps();
			if (fps > 0 && frame > 120) {
				m_report.coreFpsMin = m_report.coreFpsMin == 0 ? fps : std::min(m_report.coreFpsMin, fps);
				m_report.coreFpsMax = std::max(m_report.coreFpsMax, fps);
			}
			if (Option<int> threads = ReadProcessThreadCount(); threads.IsSome()) {
				int peak = m_report.processThreadsPeak.IsSome() ? m_report.processThreadsPeak.Unwrap() : 0;
				if (threads.Unwrap() > peak)
					m_report.processThreadsPeak = threads;
			}
		}
		if (m_options.verbose && frame % 600 == 0)
			std::printf("[sans écran] image %ld / %ld\n", frame, budget);
	}

	m_report.emulationWallSeconds = double(sdl3::GetTicksMS() - emulationStart) / 1000.0;
	m_report.emulatedFrames = session->EmulatedFrames();
	m_report.emulatedSeconds = double(m_report.emulatedFrames) / session->NativeRefreshRate();
	m_report.stateOperations = session->StateOperations();
	m_report.inputEdges = session->InputEdges();
	m_report.video = FingerprintFrame(snapshot.pixels, snapshot.width, snapshot.height, snapshot.frame);
	m_report.video.distinctFrameCount = distinctFrames;
	session.reset();
	return Finish(startTicks);
}

// ── Mode fenêtré ────────────────────────────────────────────────────────────

int Demo::RunWindowed() {
	m_report.mode = "fenêtré";
	m_report.frameStatsLabel = "images de la fenêtre";
	const uint64_t startTicks = sdl3::GetTicksMS();

	int exitCode = 0;
	{
		// Construit AVANT le chargement des réglages : GamepadManager y
		// déclare ses affectations de touches.
		app::Application application(m_options, m_report, m_threads, *m_log);
		LoadSettings();
		auto initialized = application.Initialize();
		if (initialized.IsError()) {
			m_report.failure = initialized.Error();
			return Finish(startTicks);
		}
		application.Run();
		application.CollectReport();
		DescribeSettings();
		exitCode = Finish(startTicks);

		// AVANT la destruction de l'application : les réglages de touches
		// pointent dans son GamepadManager.
		if (m_options.saveConfig) {
			RestoreOverridesBeforeSave();
			if (!Settings::save())
				std::fprintf(stderr, "réglages non enregistrés : %s\n", m_options.configPath.CStr());
		}
	}
	return exitCode;
}

// ── Fin d'exécution ─────────────────────────────────────────────────────────

void Demo::CheckRequests() {
	for (const ScreenshotRequest &request : m_options.screenshots) {
		bool found = false;
		for (const ScreenshotRecord &record : m_report.screenshots) {
			if (record.path == request.path) {
				found = true;
				if (!record.written)
					m_report.failedChecks.push_back(
						String::Format("capture %s non écrite : %s", request.path.CStr(), record.error.CStr()));
			}
		}
		if (!found)
			m_report.failedChecks.push_back(String::Format(
				"capture %s jamais prise (image %ld non atteinte)", request.path.CStr(), request.frame));
	}
	if (m_options.screenshotEvery > 0)
		for (const ScreenshotRecord &record : m_report.screenshots)
			if (!record.written && !record.error.IsEmpty())
				m_report.failedChecks.push_back(String::Format("capture %s : %s", record.path.CStr(), record.error.CStr()));

	for (const StateOperationRecord &op : m_report.stateOperations)
		if (op.origin == "script" && !op.ok)
			m_report.failedChecks.push_back(String::Format("%s d'état à l'image %ld : %s",
														   op.kind == StateActionKind::SAVE ? "sauvegarde" : "chargement",
														   op.frame, op.detail.CStr()));
	for (const ScriptedStateAction &action : m_options.stateActions) {
		bool done = false;
		for (const StateOperationRecord &op : m_report.stateOperations)
			done = done || (op.origin == "script" && op.frame == action.frame && op.kind == action.kind);
		if (m_report.booted && !done)
			m_report.failedChecks.push_back(
				String::Format("opération d'état prévue à l'image %ld jamais exécutée", action.frame));
	}

	if (m_options.requireVideo && m_report.booted) {
		if (!m_report.video.HasPicture())
			m_report.failedChecks.push_back(String("--require-video : aucune image produite"));
		else if (m_report.video.IsUniform())
			m_report.failedChecks.push_back(String::Format(
				"--require-video : la dernière image (n°%ld) est uniforme", m_report.video.frame));
	}
}

int Demo::Finish(uint64_t startTicks) {
	m_report.wallClockSeconds = double(sdl3::GetTicksMS() - startTicks) / 1000.0;
	m_report.fpsLimiter = Settings::getFpsLimiter();
	m_report.peakThreads = m_threads.Peak();
	m_report.totalThreads = m_threads.TotalStarted();
	if (m_log) {
		m_report.logWarnings = m_log->Warnings();
		m_report.logErrors = m_log->Errors();
		m_report.logExcerpt = m_log->Excerpt();
	}
	CheckRequests();
	if (m_report.failure.IsEmpty() && !m_report.failedChecks.empty())
		m_report.failure = String::Format("%d vérification(s) en échec", int(m_report.failedChecks.size()));
	m_report.completed = m_report.failure.IsEmpty();

	if (m_options.verbose)
		std::fputs(m_report.ToText().CStr(), stdout);
	if (!m_options.reportPath.IsEmpty()) {
		EnsureParentDirectory(m_options.reportPath);
		auto written = m_report.Write(m_options.reportPath, m_options.reportFormat);
		if (written.IsError())
			std::fprintf(stderr, "%s\n", written.Error().CStr());
		else if (m_options.reportPath != "/dev/stdout")
			std::printf("Rapport écrit : %s\n", m_options.reportPath.CStr());
	}
	if (!m_report.completed)
		std::fprintf(stderr, "emulator_demo : %s\n", m_report.failure.CStr());
	return m_report.completed ? 0 : 1;
}

} // namespace emulator_demo
