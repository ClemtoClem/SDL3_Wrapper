// Tests unitaires — examples/emulator_demo (émulateur NDS / GBA / GBC).
//
// Couvre les couches en-tête seul, sans fenêtre, sans GPU et sans cœur
// d'émulation : ligne de commande, noms de boutons, statistiques d'images,
// empreinte d'image et rapport d'exécution (texte et JSON relu).
//
// Le cœur et la coquille fenêtrée sont des unités de traduction à part
// (examples/emulator_demo/**/*.cpp) liées au seul binaire de la démo : ils se
// vérifient de bout en bout avec `emulator_demo --headless ... --report=...`,
// dont le code de sortie est non nul si une vérification échoue.
#define USE_TEST

#include "core/test.hpp"

#include "../examples/emulator_demo/app/cli.hpp"
#include "../examples/emulator_demo/app/report.hpp"

using namespace emulator_demo;

namespace {

Result<CommandLine, String> ParseArgs(std::vector<const char *> args) {
	std::vector<char *> argv;
	argv.push_back(const_cast<char *>("emulator_demo"));
	for (const char *arg : args)
		argv.push_back(const_cast<char *>(arg));
	return CommandLine::Parse(int(argv.size()), argv.data());
}

} // namespace

// ============================================================================
// Ligne de commande
// ============================================================================

TEST(Cli, DefaultsAreWindowedWithAudio) {
	auto parsed = ParseArgs({});
	ASSERT_TRUE(parsed.IsOk());
	const CommandLine &options = parsed.Value();
	EXPECT_FALSE(options.headless);
	EXPECT_TRUE(options.audio);
	EXPECT_TRUE(options.saveConfig);
	EXPECT_TRUE(options.romPath.IsEmpty());
	EXPECT_EQ(options.frames, 0L);
	EXPECT_EQ(options.configPath, "config.ini");
	EXPECT_TRUE(options.directBoot.IsNone());
	EXPECT_TRUE(options.listRomsDir.IsNone());
}

TEST(Cli, ParsesEveryOption) {
	auto parsed = ParseArgs({"--rom=jeux.zip",		  "--rom-entry=jeu.gba",   "--extract-dir=cache",
							 "--config=reglages.ini", "--no-save-config",	   "--bios-dir=bios",
							 "--boot=bios",			  "--arm7-hle",			   "--threaded-2d",	   "--threaded-3d",
							 "--layout=vertical",	  "--state-path=etat.bin", "--save-dir=saves", "--width=800",
							 "--height=600",		  "--no-audio",			   "--speed=unlimited", "--hide-hud",
							 "--open=config",		  "--frames=900",		   "--press=120:start:10",
							 "--press=200:A",		  "--save-state=300",	   "--load-state=450", "--report=r.json",
							 "--report-format=json",  "--screenshot=10:a.png", "--screenshot-every=60",
							 "--screenshot-dir=shots", "--require-video",	   "--verbose",		   "--headless"});
	ASSERT_TRUE(parsed.IsOk());
	const CommandLine &options = parsed.Value();
	EXPECT_EQ(options.romPath, "jeux.zip");
	EXPECT_EQ(options.romEntry, "jeu.gba");
	EXPECT_EQ(options.extractDir, "cache");
	EXPECT_EQ(options.configPath, "reglages.ini");
	EXPECT_FALSE(options.saveConfig);
	EXPECT_EQ(options.biosDir, "bios");
	ASSERT_TRUE(options.directBoot.IsSome());
	EXPECT_FALSE(options.directBoot.Unwrap());
	EXPECT_TRUE(options.arm7Hle.IsSome() && options.arm7Hle.Unwrap());
	EXPECT_TRUE(options.threaded2D.IsSome() && options.threaded3D.IsSome());
	EXPECT_TRUE(options.screenLayout.IsSome() && options.screenLayout.Unwrap() == 1);
	EXPECT_EQ(options.statePath, "etat.bin");
	EXPECT_EQ(options.saveDir, "saves");
	EXPECT_EQ(options.windowWidth, 800);
	EXPECT_EQ(options.windowHeight, 600);
	EXPECT_FALSE(options.audio);
	EXPECT_TRUE(options.unlimitedSpeed);
	EXPECT_FALSE(options.showHud);
	EXPECT_EQ(options.openModal, "config");
	EXPECT_EQ(options.frames, 900L);
	ASSERT_EQ(options.presses.size(), size_t(2));
	EXPECT_EQ(options.presses[0].frame, 120L);
	EXPECT_EQ(options.presses[0].button, int(BTN_START)); // insensible à la casse
	EXPECT_EQ(options.presses[0].holdFrames, 10L);
	EXPECT_EQ(options.presses[1].holdFrames, 6L); // durée par défaut
	ASSERT_EQ(options.stateActions.size(), size_t(2));
	EXPECT_TRUE(options.stateActions[0].kind == StateActionKind::SAVE && options.stateActions[0].frame == 300);
	EXPECT_TRUE(options.stateActions[1].kind == StateActionKind::LOAD && options.stateActions[1].frame == 450);
	EXPECT_EQ(options.reportPath, "r.json");
	EXPECT_TRUE(options.reportFormat == ReportFormat::JSON);
	ASSERT_EQ(options.screenshots.size(), size_t(1));
	EXPECT_EQ(options.screenshots[0].path, "a.png");
	EXPECT_EQ(options.screenshotEvery, 60L);
	EXPECT_EQ(options.screenshotDir, "shots");
	EXPECT_TRUE(options.requireVideo);
	EXPECT_TRUE(options.verbose);
	EXPECT_TRUE(options.headless);
}

TEST(Cli, PositionalRomAndInformationModes) {
	auto parsed = ParseArgs({"assets/roms/jeu.nds", "--rom-info", "--list-buttons", "--list-roms"});
	ASSERT_TRUE(parsed.IsOk());
	EXPECT_EQ(parsed.Value().romPath, "assets/roms/jeu.nds");
	EXPECT_TRUE(parsed.Value().romInfo);
	EXPECT_TRUE(parsed.Value().listButtons);
	ASSERT_TRUE(parsed.Value().listRomsDir.IsSome());
	EXPECT_EQ(parsed.Value().listRomsDir.Value(), "assets/roms");

	auto explicitDir = ParseArgs({"--list-roms=/tmp/roms"});
	ASSERT_TRUE(explicitDir.IsOk());
	EXPECT_EQ(explicitDir.Value().listRomsDir.Value(), "/tmp/roms");
}

TEST(Cli, ScreenshotPathMayContainColons) {
	auto parsed = ParseArgs({"--screenshot=42:C:/captures/image.png"});
	ASSERT_TRUE(parsed.IsOk());
	EXPECT_EQ(parsed.Value().screenshots[0].frame, 42L);
	EXPECT_EQ(parsed.Value().screenshots[0].path, "C:/captures/image.png");
}

TEST(Cli, RejectsInvalidInput) {
	// Une faute de frappe ne doit jamais être ignorée silencieusement.
	EXPECT_TRUE(ParseArgs({"--unknown"}).IsError());
	EXPECT_TRUE(ParseArgs({"--unknown=1"}).IsError());
	EXPECT_TRUE(ParseArgs({"--frames=0"}).IsError());
	EXPECT_TRUE(ParseArgs({"--frames=abc"}).IsError());
	EXPECT_TRUE(ParseArgs({"--press=10:Z"}).IsError());
	EXPECT_TRUE(ParseArgs({"--press=10"}).IsError());
	EXPECT_TRUE(ParseArgs({"--press=0:A"}).IsError());
	EXPECT_TRUE(ParseArgs({"--press=10:A:0"}).IsError());
	EXPECT_TRUE(ParseArgs({"--save-state=-5"}).IsError());
	EXPECT_TRUE(ParseArgs({"--screenshot=abc.png"}).IsError());
	EXPECT_TRUE(ParseArgs({"--screenshot=10:"}).IsError());
	EXPECT_TRUE(ParseArgs({"--boot=fast"}).IsError());
	EXPECT_TRUE(ParseArgs({"--layout=diagonal"}).IsError());
	EXPECT_TRUE(ParseArgs({"--speed=turbo"}).IsError());
	EXPECT_TRUE(ParseArgs({"--open=settings"}).IsError());
	EXPECT_TRUE(ParseArgs({"--report-format=xml"}).IsError());
	EXPECT_TRUE(ParseArgs({"a.gba", "b.gba"}).IsError());

	auto error = ParseArgs({"--press=10:Z"});
	ASSERT_TRUE(error.IsError());
	EXPECT_TRUE(error.Error().Contains("--list-buttons")); // le message dit quoi faire
}

TEST(Cli, HelpDocumentsEveryParsedOption) {
	String help(CommandLine::HelpText());
	for (const char *option :
		 {"--rom=", "--rom-entry=", "--extract-dir=", ".tar.gz", "--config=", "--no-save-config", "--bios-dir=",
		  "--boot=", "--arm7-hle", "--threaded-2d",
		  "--layout=", "--state-path=", "--save-dir=", "--width=", "--no-audio", "--speed=", "--hide-hud", "--open=",
		  "--frames=", "--press=", "--save-state=", "--load-state=", "--report=", "--report-format=",
		  "--screenshot=", "--screenshot-every=", "--screenshot-dir=", "--require-video", "--verbose", "--headless",
		  "--rom-info", "--list-roms", "--list-buttons", "--help"})
		EXPECT_TRUE(help.Contains(option));
}

TEST(Cli, RebuildJoinsArguments) {
	char program[] = "emulator_demo";
	char first[] = "--headless";
	char second[] = "--frames=10";
	char *argv[] = {program, first, second};
	EXPECT_EQ(CommandLine::Rebuild(3, argv), "emulator_demo --headless --frames=10");
}

// ============================================================================
// Boutons
// ============================================================================

TEST(Buttons, NamesRoundTrip) {
	for (int button = 0; button < BTN_COUNT; ++button) {
		Option<int> parsed = ParseButton(String(ButtonName(button)));
		ASSERT_TRUE(parsed.IsSome());
		EXPECT_EQ(parsed.Unwrap(), button);
	}
	EXPECT_TRUE(ParseButton(String("select")).IsSome());
	EXPECT_TRUE(ParseButton(String("HOME")).IsNone());
	EXPECT_EQ(String(ButtonName(-1)), "?");
	EXPECT_EQ(String(ButtonName(BTN_COUNT)), "?");
}

TEST(Buttons, OrderMatchesConsoleInput) {
	// L'ordre est celui de Input::pressKey (NDS/GBA) et de gbc::GbcButton :
	// un décalage enverrait START quand le script demande A.
	EXPECT_EQ(int(BTN_A), 0);
	EXPECT_EQ(int(BTN_START), 3);
	EXPECT_EQ(int(BTN_DOWN), 7);
	EXPECT_EQ(int(BTN_Y), 11);
}

// ============================================================================
// Mesures
// ============================================================================

TEST(FrameStats, FirstFrameIsExcludedFromExtremes) {
	FrameStats stats;
	stats.Push(1.0); // initialisation : exclue
	stats.Push(1.0 / 50.0);
	stats.Push(1.0 / 100.0);
	EXPECT_EQ(stats.Count(), 3L);
	EXPECT_TRUE(stats.FirstFrameSeconds() == 1.0);
	EXPECT_TRUE(stats.MinFps() > 49.9 && stats.MinFps() < 50.1);
	EXPECT_TRUE(stats.MaxFps() > 99.9 && stats.MaxFps() < 100.1);
	EXPECT_TRUE(stats.PercentileMs(100.0) > 19.9 && stats.PercentileMs(100.0) < 20.1);
}

TEST(ThreadTracker, CountsPeakConcurrency) {
	ThreadTracker tracker;
	EXPECT_EQ(tracker.Peak(), 1); // fil principal
	{
		ThreadTracker::Scope first(&tracker);
		ThreadTracker::Scope second(&tracker);
		EXPECT_EQ(tracker.Live(), 3);
	}
	ThreadTracker::Scope later(&tracker);
	EXPECT_EQ(tracker.Peak(), 3);
	EXPECT_EQ(tracker.TotalStarted(), 4);
	ThreadTracker::Scope untracked(nullptr); // sans suivi : sans effet
	EXPECT_EQ(tracker.Live(), 2);
}

TEST(Fingerprint, DetectsUniformAndColorfulFrames) {
	std::vector<uint32_t> black(16, 0xFF000000u);
	VideoRecord uniform = FingerprintFrame(black, 4, 4, 7);
	EXPECT_TRUE(uniform.HasPicture());
	EXPECT_TRUE(uniform.IsUniform());
	EXPECT_EQ(uniform.frame, 7L);
	EXPECT_TRUE(uniform.nonBlackRatio == 0.0);

	std::vector<uint32_t> picture = black;
	picture[0] = 0xFF0000FFu;
	picture[5] = 0xFF00FF00u;
	picture[6] = 0xFF00FF00u;
	VideoRecord colorful = FingerprintFrame(picture, 4, 4, 8);
	EXPECT_FALSE(colorful.IsUniform());
	EXPECT_EQ(colorful.distinctColors, 3);
	EXPECT_TRUE(colorful.nonBlackRatio > 0.18 && colorful.nonBlackRatio < 0.19);
	EXPECT_TRUE(colorful.checksum != uniform.checksum);
	EXPECT_TRUE(HashPixels(picture) == colorful.checksum);

	VideoRecord empty = FingerprintFrame({}, 0, 0, 0);
	EXPECT_FALSE(empty.HasPicture());
	EXPECT_FALSE(empty.IsUniform()); // « pas d'image » n'est pas « image uniforme »
}

// ============================================================================
// Rapport
// ============================================================================

namespace {

RunReport SampleReport() {
	RunReport report;
	report.commandLine = "emulator_demo --headless --rom=jeu.gbc";
	report.mode = "sans écran";
	report.romPath = "jeu.gbc";
	report.system = "GBC";
	report.romTitle = "TITRE\nSUR DEUX LIGNES";
	report.romGameCode = "AXVF";
	report.romRegion = "France";
	report.romHeaderChecksumOk = Some(true);
	report.romCrc32 = 0x5449CAC0u;
	report.archiveFormat = "zip";
	report.archiveEntry = "roms/jeu.gbc";
	report.archiveBytes = 61110;
	report.archiveStoredBytes = 60504;
	report.archiveMethod = "deflate";
	report.extractedPath = "cache/5449CAC0-jeu.gbc";
	report.booted = true;
	report.emulatedFrames = 600;
	report.emulatedSeconds = 600.0 / 59.7275;
	report.emulationWallSeconds = report.emulatedSeconds / 2.0;
	report.inputEdges.push_back(InputEdgeRecord{120, BTN_START, true});
	report.stateOperations.push_back(StateOperationRecord{300, StateActionKind::SAVE, true, "script", "etat.bin"});
	report.screenshots.push_back(ScreenshotRecord{600, "a.png", "framebuffer", true, ""});
	std::vector<uint32_t> pixels(4, 0xFF123456u);
	pixels[1] = 0xFFFFFFFFu;
	report.video = FingerprintFrame(pixels, 2, 2, 600);
	report.video.distinctFrameCount = 12;
	report.frames.Push(0.1);
	report.frames.Push(0.004);
	report.peakThreads = 2;
	report.processThreadsPeak = Some(9);
	report.completed = true;
	return report;
}

} // namespace

TEST(Report, SpeedIsRelativeToRealTime) {
	RunReport report = SampleReport();
	EXPECT_TRUE(report.SpeedPercent() > 199.9 && report.SpeedPercent() < 200.1);
	RunReport idle;
	EXPECT_TRUE(idle.SpeedPercent() == 0.0);
}

TEST(Report, TextContainsEverySection) {
	String text = SampleReport().ToText();
	for (const char *section : {"RAPPORT D'EXÉCUTION", "--- ROM", "--- Réglages effectifs", "--- Émulation",
								"--- Performance", "--- Interface et captures", "--- Journal"})
		EXPECT_TRUE(text.Contains(section));
	EXPECT_TRUE(text.Contains("TITRE SUR DEUX LIGNES")); // titre de bannière aplati
	EXPECT_TRUE(text.Contains("START"));
	EXPECT_TRUE(text.Contains("Archive         : zip, entrée « roms/jeu.gbc »"));
	EXPECT_TRUE(text.Contains("AXVF (France)"));
	EXPECT_TRUE(text.Contains("sauvegarde"));
	EXPECT_TRUE(text.Contains("terminé normalement"));
}

TEST(Report, JsonRoundTripsThroughDataModule) {
	RunReport report = SampleReport();
	data::JsonDocument document;
	auto error = document.DecodeStr(report.ToJson());
	ASSERT_TRUE(error.IsNone());
	data::NodePtr root = document.GetRoot();
	ASSERT_TRUE(root != nullptr);
	EXPECT_EQ(root->Get("format")->stringValue, "emulator_demo.report");
	EXPECT_TRUE(root->Get("run")->Get("completed")->boolValue);
	EXPECT_EQ(root->Get("rom")->Get("system")->stringValue, "GBC");
	EXPECT_TRUE(root->Get("rom")->Get("booted")->boolValue);
	EXPECT_EQ(root->Get("rom")->Get("crc32")->stringValue, "5449CAC0");
	EXPECT_EQ(root->Get("rom")->Get("game_code")->stringValue, "AXVF");
	EXPECT_TRUE(root->Get("rom")->Get("header_checksum_ok")->boolValue);
	data::NodePtr archive = root->Get("rom")->Get("archive");
	ASSERT_TRUE(archive != nullptr);
	EXPECT_EQ(archive->Get("format")->stringValue, "zip");
	EXPECT_EQ(archive->Get("entry")->stringValue, "roms/jeu.gbc");
	EXPECT_TRUE(archive->Get("stored_bytes")->intValue == 60504);

	data::NodePtr emulation = root->Get("emulation");
	EXPECT_TRUE(emulation->Get("frames")->intValue == 600);
	EXPECT_TRUE(emulation->Get("speed_percent")->floatValue > 199.0);
	EXPECT_TRUE(emulation->Get("input_edges")->GetSize() == 1);
	EXPECT_EQ(emulation->Get("input_edges")->At(0)->Get("button")->stringValue, "START");
	EXPECT_EQ(emulation->Get("state_operations")->At(0)->Get("kind")->stringValue, "save");
	EXPECT_FALSE(emulation->Get("last_frame")->Get("uniform")->boolValue);
	EXPECT_TRUE(emulation->Get("last_frame")->Get("distinct_colors")->intValue == 2);
	EXPECT_TRUE(emulation->Get("last_frame")->Get("distinct_frames")->intValue == 12);

	EXPECT_TRUE(root->Get("performance")->Get("threads_peak_concurrent")->intValue == 2);
	EXPECT_TRUE(root->Get("performance")->Get("process_threads_peak")->intValue == 9);
	EXPECT_TRUE(root->Get("screenshots")->GetSize() == 1);
	EXPECT_TRUE(root->Get("screenshots")->At(0)->Get("written")->boolValue);
}

TEST(Report, FailedChecksAppearInBothFormats) {
	RunReport report = SampleReport();
	report.completed = false;
	report.failure = "1 vérification(s) en échec";
	report.failedChecks.push_back("--require-video : la dernière image (n°600) est uniforme");
	EXPECT_TRUE(report.ToText().Contains("[ÉCHEC] --require-video"));

	data::JsonDocument document;
	ASSERT_TRUE(document.DecodeStr(report.ToJson()).IsNone());
	data::NodePtr run = document.GetRoot()->Get("run");
	EXPECT_FALSE(run->Get("completed")->boolValue);
	EXPECT_TRUE(run->Get("failed_checks")->GetSize() == 1);
}

int main() { return RUN_ALL_TESTS(); }
