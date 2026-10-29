#include "test.hpp"
#include <filesystem>
#include <memory>
#include <fstream>
#include <emulator/emulator.hpp>
#include <emulator/app/rom_source.hpp>
#include <emulator/core/core.hpp>
#include <emulator/core/settings.hpp>

using namespace emulator;

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

TEST(Cli_DefaultsAreWindowedWithAudio) {
	auto parsed = ParseArgs({});
	CHECK(parsed.IsOk());
	const CommandLine &options = parsed.Value();
	CHECK(!options.headless);
	CHECK(options.audio);
	CHECK(options.saveConfig);
	CHECK(options.romPath.IsEmpty());
	CHECK_EQ(options.frames, 0L);
	// Vides : résolus au lancement (réglages dans ./data/emulator_demo/, cf.
	// config_location.hpp et Settings::stateFilePath).
	CHECK(options.configPath.IsEmpty());
	CHECK(options.statePath.IsEmpty());
	CHECK(options.stateDir.IsEmpty());
	CHECK(options.directBoot.IsNone());
	CHECK(options.listRomsDir.IsNone());
}

TEST(Cli_ParsesEveryOption) {
	auto parsed = ParseArgs({"--rom=jeux.zip",		  "--rom-entry=jeu.gba",   "--extract-dir=cache",
							 "--config=reglages.ini", "--no-save-config",	   "--bios-dir=bios",
							 "--boot=bios",			  "--arm7-hle",			   "--threaded-2d",	   "--threaded-3d",
							 "--layout=vertical",	  "--state-path=etat.bin", "--state-dir=etats", "--save-dir=saves", "--width=800",
							 "--height=600",		  "--no-audio",			   "--speed=unlimited", "--hide-hud",
							 "--open=config",		  "--frames=900",		   "--press=120:start:10",
							 "--press=200:A",		  "--save-state=300",	   "--load-state=450", "--report=r.json",
							 "--report-format=json",  "--screenshot=10:a.png", "--screenshot-every=60",
							 "--screenshot-dir=shots", "--require-video",	   "--verbose",		   "--headless"});
	CHECK(parsed.IsOk());
	const CommandLine &options = parsed.Value();
	CHECK_EQ(options.romPath, "jeux.zip");
	CHECK_EQ(options.romEntry, "jeu.gba");
	CHECK_EQ(options.extractDir, "cache");
	CHECK_EQ(options.configPath, "reglages.ini");
	CHECK(!options.saveConfig);
	CHECK_EQ(options.biosDir, "bios");
	CHECK(options.directBoot.IsSome());
	CHECK(!options.directBoot.Unwrap());
	CHECK(options.arm7Hle.IsSome() && options.arm7Hle.Unwrap());
	CHECK(options.threaded2D.IsSome() && options.threaded3D.IsSome());
	CHECK(options.screenLayout.IsSome() && options.screenLayout.Unwrap() == 1);
	CHECK_EQ(options.statePath, "etat.bin");
	CHECK_EQ(options.stateDir, "etats");
	CHECK_EQ(options.saveDir, "saves");
	CHECK_EQ(options.windowWidth, 800);
	CHECK_EQ(options.windowHeight, 600);
	CHECK(!options.audio);
	CHECK(options.unlimitedSpeed);
	CHECK(!options.showHud);
	CHECK_EQ(options.openModal, "config");
	CHECK_EQ(options.frames, 900L);
	CHECK_EQ(options.presses.size(), size_t(2));
	CHECK_EQ(options.presses[0].frame, 120L);
	CHECK_EQ(options.presses[0].button, int(BTN_START)); // insensible à la casse
	CHECK_EQ(options.presses[0].holdFrames, 10L);
	CHECK_EQ(options.presses[1].holdFrames, 6L); // durée par défaut
	CHECK_EQ(options.stateActions.size(), size_t(2));
	CHECK(options.stateActions[0].kind == StateActionKind::SAVE && options.stateActions[0].frame == 300);
	CHECK(options.stateActions[1].kind == StateActionKind::LOAD && options.stateActions[1].frame == 450);
	CHECK_EQ(options.reportPath, "r.json");
	CHECK(options.reportFormat == ReportFormat::JSON);
	CHECK_EQ(options.screenshots.size(), size_t(1));
	CHECK_EQ(options.screenshots[0].path, "a.png");
	CHECK_EQ(options.screenshotEvery, 60L);
	CHECK_EQ(options.screenshotDir, "shots");
	CHECK(options.requireVideo);
	CHECK(options.verbose);
	CHECK(options.headless);
}

TEST(Cli_PositionalRomAndInformationModes) {
	auto parsed = ParseArgs({"assets/roms/jeu.nds", "--rom-info", "--list-buttons", "--list-roms"});
	CHECK(parsed.IsOk());
	CHECK_EQ(parsed.Value().romPath, "assets/roms/jeu.nds");
	CHECK(parsed.Value().romInfo);
	CHECK(parsed.Value().listButtons);
	CHECK(parsed.Value().listRomsDir.IsSome());
	CHECK_EQ(parsed.Value().listRomsDir.Value(), "assets/roms");

	auto explicitDir = ParseArgs({"--list-roms=/tmp/roms"});
	CHECK(explicitDir.IsOk());
	CHECK_EQ(explicitDir.Value().listRomsDir.Value(), "/tmp/roms");
}

TEST(Cli_ScreenshotPathMayContainColons) {
	auto parsed = ParseArgs({"--screenshot=42:C:/captures/image.png"});
	CHECK(parsed.IsOk());
	CHECK_EQ(parsed.Value().screenshots[0].frame, 42L);
	CHECK_EQ(parsed.Value().screenshots[0].path, "C:/captures/image.png");
}

TEST(Cli_RejectsInvalidInput) {
	// Une faute de frappe ne doit jamais être ignorée silencieusement.
	CHECK(ParseArgs({"--unknown"}).IsError());
	CHECK(ParseArgs({"--unknown=1"}).IsError());
	CHECK(ParseArgs({"--frames=0"}).IsError());
	CHECK(ParseArgs({"--frames=abc"}).IsError());
	CHECK(ParseArgs({"--press=10:Z"}).IsError());
	CHECK(ParseArgs({"--press=10"}).IsError());
	CHECK(ParseArgs({"--press=0:A"}).IsError());
	CHECK(ParseArgs({"--press=10:A:0"}).IsError());
	CHECK(ParseArgs({"--save-state=-5"}).IsError());
	CHECK(ParseArgs({"--screenshot=abc.png"}).IsError());
	CHECK(ParseArgs({"--screenshot=10:"}).IsError());
	CHECK(ParseArgs({"--boot=fast"}).IsError());
	CHECK(ParseArgs({"--layout=diagonal"}).IsError());
	CHECK(ParseArgs({"--speed=turbo"}).IsError());
	CHECK(ParseArgs({"--open=settings"}).IsError());
	CHECK(ParseArgs({"--report-format=xml"}).IsError());
	CHECK(ParseArgs({"a.gba", "b.gba"}).IsError());

	auto error = ParseArgs({"--press=10:Z"});
	CHECK(error.IsError());
	CHECK(error.Error().Contains("--list-buttons")); // le message dit quoi faire
}

TEST(Cli_HelpDocumentsEveryParsedOption) {
	String help(CommandLine::HelpText());
	for (const char *option :
		 {"--rom=", "--rom-entry=", "--extract-dir=", ".tar.gz", "--config=", "--no-save-config", "--bios-dir=",
		  "--boot=", "--arm7-hle", "--threaded-2d",
		  "--layout=", "--state-path=", "--save-dir=", "--width=", "--no-audio", "--speed=", "--hide-hud", "--open=",
		  "--frames=", "--press=", "--save-state=", "--load-state=", "--report=", "--report-format=",
		  "--screenshot=", "--screenshot-every=", "--screenshot-dir=", "--require-video", "--verbose", "--headless",
		  "--rom-info", "--list-roms", "--list-buttons", "--help"})
		CHECK(help.Contains(option));
}

TEST(Cli_RebuildJoinsArguments) {
	char program[] = "emulator_demo";
	char first[] = "--headless";
	char second[] = "--frames=10";
	char *argv[] = {program, first, second};
	CHECK_EQ(CommandLine::Rebuild(3, argv), "emulator_demo --headless --frames=10");
}

// ============================================================================
// Boutons
// ============================================================================

TEST(Buttons_NamesRoundTrip) {
	for (int button = 0; button < BTN_COUNT; ++button) {
		Option<int> parsed = ParseButton(String(ButtonName(button)));
		CHECK(parsed.IsSome());
		CHECK_EQ(parsed.Unwrap(), button);
	}
	CHECK(ParseButton(String("select")).IsSome());
	CHECK(ParseButton(String("HOME")).IsNone());
	CHECK_EQ(String(ButtonName(-1)), "?");
	CHECK_EQ(String(ButtonName(BTN_COUNT)), "?");
}

TEST(Buttons_OrderMatchesConsoleInput) {
	// L'ordre est celui de Input::pressKey (NDS/GBA) et de gbc::GbcButton :
	// un décalage enverrait START quand le script demande A.
	CHECK_EQ(int(BTN_A), 0);
	CHECK_EQ(int(BTN_START), 3);
	CHECK_EQ(int(BTN_DOWN), 7);
	CHECK_EQ(int(BTN_Y), 11);
}

// ============================================================================
// Mesures
// ============================================================================

TEST(FrameStats_FirstFrameIsExcludedFromExtremes) {
	FrameStats stats;
	stats.Push(1.0); // initialisation : exclue
	stats.Push(1.0 / 50.0);
	stats.Push(1.0 / 100.0);
	CHECK_EQ(stats.Count(), 3L);
	CHECK(stats.FirstFrameSeconds() == 1.0);
	CHECK(stats.MinFps() > 49.9 && stats.MinFps() < 50.1);
	CHECK(stats.MaxFps() > 99.9 && stats.MaxFps() < 100.1);
	CHECK(stats.PercentileMs(100.0) > 19.9 && stats.PercentileMs(100.0) < 20.1);
}

TEST(ThreadTracker_CountsPeakConcurrency) {
	ThreadTracker tracker;
	CHECK_EQ(tracker.Peak(), 1); // fil principal
	{
		ThreadTracker::Scope first(&tracker);
		ThreadTracker::Scope second(&tracker);
		CHECK_EQ(tracker.Live(), 3);
	}
	ThreadTracker::Scope later(&tracker);
	CHECK_EQ(tracker.Peak(), 3);
	CHECK_EQ(tracker.TotalStarted(), 4);
	ThreadTracker::Scope untracked(nullptr); // sans suivi : sans effet
	CHECK_EQ(tracker.Live(), 2);
}

TEST(Fingerprint_DetectsUniformAndColorfulFrames) {
	std::vector<uint32_t> black(16, 0xFF000000u);
	VideoRecord uniform = FingerprintFrame(black, 4, 4, 7);
	CHECK(uniform.HasPicture());
	CHECK(uniform.IsUniform());
	CHECK_EQ(uniform.frame, 7L);
	CHECK(uniform.nonBlackRatio == 0.0);

	std::vector<uint32_t> picture = black;
	picture[0] = 0xFF0000FFu;
	picture[5] = 0xFF00FF00u;
	picture[6] = 0xFF00FF00u;
	VideoRecord colorful = FingerprintFrame(picture, 4, 4, 8);
	CHECK(!colorful.IsUniform());
	CHECK_EQ(colorful.distinctColors, 3);
	CHECK(colorful.nonBlackRatio > 0.18 && colorful.nonBlackRatio < 0.19);
	CHECK(colorful.checksum != uniform.checksum);
	CHECK(HashPixels(picture) == colorful.checksum);

	VideoRecord empty = FingerprintFrame({}, 0, 0, 0);
	CHECK(!empty.HasPicture());
	CHECK(!empty.IsUniform()); // « pas d'image » n'est pas « image uniforme »
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

TEST(Report_S_SpeedIsRelativeToRealTime) {
	RunReport report = SampleReport();
	CHECK(report.SpeedPercent() > 199.9 && report.SpeedPercent() < 200.1);
	RunReport idle;
	CHECK(idle.SpeedPercent() == 0.0);
}

TEST(Report_TextContainsEverySection) {
	String text = SampleReport().ToText();
	for (const char *section : {"RAPPORT D'EXÉCUTION", "--- ROM", "--- Réglages effectifs", "--- Émulation", "--- Performance", "--- Interface et captures", "--- Journal"}) {
        CHECK(text.Contains(section));
    }
	CHECK(text.Contains("TITRE SUR DEUX LIGNES")); // titre de bannière aplati
	CHECK(text.Contains("START"));
	CHECK(text.Contains("Archive         : zip, entrée « roms/jeu.gbc »"));
	CHECK(text.Contains("AXVF (France)"));
	CHECK(text.Contains("sauvegarde"));
	CHECK(text.Contains("terminé normalement"));
}

TEST(Report_JsonRoundTripsThroughDataModule) {
	RunReport report = SampleReport();
	dataio::JsonDocument document;
	auto error = document.DecodeStr(report.ToJson());
	CHECK(error.IsNone());
	dataio::NodePtr root = document.GetRoot();
	CHECK(root != nullptr);
	CHECK_EQ(root->Get("format")->stringValue, "emulator.report");
	CHECK(root->Get("run")->Get("completed")->boolValue);
	CHECK_EQ(root->Get("rom")->Get("system")->stringValue, "GBC");
	CHECK(root->Get("rom")->Get("booted")->boolValue);
	CHECK_EQ(root->Get("rom")->Get("crc32")->stringValue, "5449CAC0");
	CHECK_EQ(root->Get("rom")->Get("game_code")->stringValue, "AXVF");
	CHECK(root->Get("rom")->Get("header_checksum_ok")->boolValue);
	dataio::NodePtr archive = root->Get("rom")->Get("archive");
	CHECK(archive != nullptr);
	CHECK_EQ(archive->Get("format")->stringValue, "zip");
	CHECK_EQ(archive->Get("entry")->stringValue, "roms/jeu.gbc");
	CHECK(archive->Get("stored_bytes")->intValue == 60504);

	dataio::NodePtr emulation = root->Get("emulation");
	CHECK(emulation->Get("frames")->intValue == 600);
	CHECK(emulation->Get("speed_percent")->floatValue > 199.0);
	CHECK(emulation->Get("input_edges")->GetSize() == 1);
	CHECK_EQ(emulation->Get("input_edges")->At(0)->Get("button")->stringValue, "START");
	CHECK_EQ(emulation->Get("state_operations")->At(0)->Get("kind")->stringValue, "save");
	CHECK(!emulation->Get("last_frame")->Get("uniform")->boolValue);
	CHECK(emulation->Get("last_frame")->Get("distinct_colors")->intValue == 2);
	CHECK(emulation->Get("last_frame")->Get("distinct_frames")->intValue == 12);

	CHECK(root->Get("performance")->Get("threads_peak_concurrent")->intValue == 2);
	CHECK(root->Get("performance")->Get("process_threads_peak")->intValue == 9);
	CHECK(root->Get("screenshots")->GetSize() == 1);
	CHECK(root->Get("screenshots")->At(0)->Get("written")->boolValue);
}

TEST(Report_FailedChecksAppearInBothFormats) {
	RunReport report = SampleReport();
	report.completed = false;
	report.failure = "1 vérification(s) en échec";
	report.failedChecks.push_back("--require-video : la dernière image (n°600) est uniforme");
	CHECK(report.ToText().Contains("[ÉCHEC] --require-video"));

	dataio::JsonDocument document;
	CHECK(document.DecodeStr(report.ToJson()).IsNone());
	dataio::NodePtr run = document.GetRoot()->Get("run");
	CHECK(!run->Get("completed")->boolValue);
	CHECK(run->Get("failed_checks")->GetSize() == 1);
}

// ============================================================================
// Recherche des ROMs : sous-dossiers et tri par console
// ============================================================================

namespace {

/// Arborescence de ROMs factices (le contenu n'est pas lu par FindRoms).
String MakeRomTree() {
	namespace fs = std::filesystem;
	const fs::path root = fs::temp_directory_path() / "emulator_rom_tree";
	fs::remove_all(root);
	for (const char *file : {"zelda.gbc", "gba/Metroid.gba", "gba/deep/er/Advance Wars.GBA", "nds/Mario Kart.nds",
							 "nds/patch.xdelta", "gb/Tetris.gb", "notes.txt", ".cache/hidden.gba"}) {
		fs::create_directories((root / file).parent_path());
		std::ofstream(root / file) << "rom";
	}
	return String(root.string().c_str());
}

std::vector<String> Names(const std::vector<app::RomCandidate> &roms) {
	std::vector<String> names;
	for (const app::RomCandidate &rom : roms)
		names.push_back(rom.ShortName());
	return names;
}

} // namespace

TEST(Roms_SystemIsDeducedFromTheExtension) {
	CHECK(app::RomSystemFromName("jeu.NDS") == Some(app::RomSystem::NDS));
	CHECK(app::RomSystemFromName("a/b/jeu.gba") == Some(app::RomSystem::GBA));
	CHECK(app::RomSystemFromName("jeu.gbc") == Some(app::RomSystem::GBC));
	CHECK(app::RomSystemFromName("tetris.gb") == Some(app::RomSystem::GBC)); // Game Boy : cœur GBC
	CHECK(app::RomSystemFromName("jeu.gba.gz") == Some(app::RomSystem::GBA));
	CHECK(app::RomSystemFromName("patch.bps").IsNone());
	// Une entrée d'archive l'emporte sur le nom de l'archive.
	CHECK((app::RomCandidate{"pack.zip", "dossier/jeu.nds", 0}.System() == Some(app::RomSystem::NDS)));
	CHECK((app::RomCandidate{"jeu.gbc.xz", "", 0}.System() == Some(app::RomSystem::GBC)));
}

TEST(Roms_FindRomsWalksSubdirectoriesAndSortsByConsole) {
	const String root = MakeRomTree();
	std::vector<app::RomCandidate> roms = app::FindRoms(root);
	// GBC (gb compris) puis GBA puis NDS, chaque groupe par nom sans casse ;
	// ni .xdelta, ni .txt, ni dossier caché.
	const std::vector<String> expected = {"Tetris.gb", "zelda.gbc", "Advance Wars.GBA", "Metroid.gba",
										  "Mario Kart.nds"};
	CHECK(Names(roms) == expected);
	REQUIRE_EQ(roms.size(), expected.size());
	CHECK(roms[2].path.EndsWith("gba/deep/er/Advance Wars.GBA"));
}

TEST(Roms_NonRecursiveSearchStaysInTheDirectory) {
	const String root = MakeRomTree();
	std::vector<app::RomCandidate> roms = app::FindRoms(root, nullptr, false);
	CHECK(Names(roms) == std::vector<String>{"zelda.gbc"});
}

TEST(Roms_SortPutsUnknownConsolesLast) {
	std::vector<app::RomCandidate> roms = {
		{"x.zip", "readme.bin", 0}, {"b.nds", "", 0}, {"a.gba", "", 0}, {"c.gbc", "", 0}};
	app::SortRomsBySystem(roms);
	CHECK(Names(roms) == (std::vector<String>{"c.gbc", "a.gba", "b.nds", "readme.bin"}));
}

// ============================================================================
// Cœur NDS : flash du firmware (SPI) et horloge RTC
// ============================================================================
// Les jeux Nintendo Wi-Fi Connection (Mario Kart DS, Pokémon…) réécrivent la
// configuration Wi-Fi du firmware au démarrage (WREN → RDSR → écriture de
// page) et écrivent/relisent l'horloge : sans ces commandes, ils bouclaient
// sur un écran fixe, sans son.

namespace {

/// Cœur sans BIOS ni ROM : seuls les périphériques sont utilisés. Sur le
/// tas : un Core (RAM, VRAM…) dépasse largement la taille d'une pile.
struct BareCore {
	std::unique_ptr<emulator::Core> owned = std::make_unique<emulator::Core>(emulator::ROM_NDS, "");
	emulator::Core &core = *owned;
	static void Configure() {
		emulator::Settings::setNdsBios9Path("/inexistant/bios9.bin");
		emulator::Settings::setNdsBios7Path("/inexistant/bios7.bin");
		emulator::Settings::setFirmwarePath("/inexistant/firmware.bin");
	}
};

/// Transfert SPI vers la flash du firmware ; rend les octets reçus.
std::vector<uint8_t> FirmwareTransfer(emulator::Spi &spi, std::vector<uint8_t> bytes) {
	std::vector<uint8_t> in;
	for (size_t i = 0; i < bytes.size(); i++) {
		const bool hold = i + 1 < bytes.size(); // CS maintenu jusqu'au dernier octet
		spi.writeSpiCnt(0xFFFF, uint16_t(BIT(15) | (1 << 8) | (hold ? BIT(11) : 0)));
		spi.writeSpiData(bytes[i]);
		in.push_back(spi.readSpiData());
	}
	return in;
}

/// Pilote le bus série de la RTC (registre 0x4000138) bit par bit.
struct RtcBus {
	emulator::Rtc &rtc;
	void Bit(bool value, bool write) {
		const uint8_t dirs = uint8_t(BIT(6) | BIT(5) | (write ? BIT(4) : 0)); // CS, SCK, SIO en sortie ?
		rtc.writeRtc(uint8_t(dirs | BIT(2) | BIT(1) | (value ? 1 : 0))); // SCK bas
		rtc.writeRtc(uint8_t(dirs | BIT(2) | (value ? 1 : 0)));           // front montant
	}
	void End() { rtc.writeRtc(uint8_t(BIT(6) | BIT(5))); } // CS bas
	void Command(int reg, bool read) {
		const uint8_t cmd = uint8_t(0x60 | (reg << 1) | (read ? 1 : 0));
		for (int i = 7; i >= 0; i--)
			Bit((cmd >> i) & 1, true);
	}
	void Write(int reg, std::vector<uint8_t> bytes) {
		Command(reg, false);
		for (uint8_t b : bytes)
			for (int i = 0; i < 8; i++)
				Bit((b >> i) & 1, true);
		End();
	}
	std::vector<uint8_t> Read(int reg, size_t count) {
		Command(reg, true);
		std::vector<uint8_t> out(count, 0);
		for (size_t n = 0; n < count; n++)
			for (int i = 0; i < 8; i++) {
				Bit(false, false);
				out[n] |= uint8_t((rtc.readRtc() & 1) << i);
			}
		End();
		return out;
	}
};

} // namespace

TEST(NdsFirmware_WriteEnableLatchAndPageWrite) {
	BareCore::Configure();
	BareCore bare;
	emulator::Spi &spi = bare.core.spi;
	REQUIRE(!spi.loadFirmware()); // firmware minimal généré (0x20000 octets)

	CHECK_EQ(FirmwareTransfer(spi, {0x05, 0x00})[1] & BIT(1), 0); // WEL à 0
	// Sans WREN, l'écriture est ignorée.
	(void)FirmwareTransfer(spi, {0x0A, 0x01, 0x23, 0x00, 0xAB});
	CHECK(FirmwareTransfer(spi, {0x03, 0x01, 0x23, 0x00, 0x00})[4] != 0xAB);

	(void)FirmwareTransfer(spi, {0x06});                                  // WREN
	CHECK_EQ(FirmwareTransfer(spi, {0x05, 0x00})[1] & BIT(1), BIT(1));    // WEL lu par RDSR
	(void)FirmwareTransfer(spi, {0x0A, 0x01, 0x23, 0x00, 0xAB, 0xCD});   // PW
	const std::vector<uint8_t> back = FirmwareTransfer(spi, {0x03, 0x01, 0x23, 0x00, 0x00, 0x00});
	CHECK_EQ(back[4], 0xAB);
	CHECK_EQ(back[5], 0xCD);
	CHECK_EQ(FirmwareTransfer(spi, {0x05, 0x00})[1] & BIT(1), 0); // l'écriture consomme WEL

	(void)FirmwareTransfer(spi, {0x06});
	(void)FirmwareTransfer(spi, {0xDB, 0x01, 0x23, 0x00}); // effacement de la page
	CHECK_EQ(FirmwareTransfer(spi, {0x03, 0x01, 0x23, 0x00, 0x00})[4], 0xFF);
	const std::vector<uint8_t> id = FirmwareTransfer(spi, {0x9F, 0x00, 0x00, 0x00});
	CHECK(id[1] == 0x20 && id[2] == 0x40 && id[3] == 0x12);
}

TEST(NdsRtc_WrittenDateTimeAndRegistersReadBack) {
	BareCore::Configure();
	BareCore bare;
	RtcBus bus{bare.core.rtc};
	bus.Write(0, {0x02});                                    // Status 1 : mode 24 h
	bus.Write(2, {0x31, 0x04, 0x05, 0x00, 0x13, 0x45, 0x30}); // 2031-04-05 13:45:30
	const std::vector<uint8_t> dt = bus.Read(2, 7);
	CHECK_EQ(dt[0], 0x31);
	CHECK_EQ(dt[1], 0x04);
	CHECK_EQ(dt[2], 0x05);
	CHECK_EQ(dt[4] & 0x3F, 0x13);
	CHECK_EQ(dt[5], 0x45);
	CHECK(dt[6] == 0x30 || dt[6] == 0x31); // la seconde peut avoir tourné

	bus.Write(1, {0x5A}); // Status 2
	CHECK_EQ(bus.Read(1, 1)[0], 0x5A);
	bus.Write(5, {0x12, 0x34, 0x56}); // alarme 2
	CHECK(bus.Read(5, 3) == (std::vector<uint8_t>{0x12, 0x34, 0x56}));
	bus.Write(7, {0xC3}); // registre libre
	CHECK_EQ(bus.Read(7, 1)[0], 0xC3);
}
