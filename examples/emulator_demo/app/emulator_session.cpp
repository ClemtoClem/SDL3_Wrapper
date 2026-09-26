#include "emulator_session.hpp"

#include <cstring>

#include "sdl3/filesystem.hpp"
#include "sdl3/iostream.hpp"

#include "../emulator/core.hpp"
#include "../emulator/gbc/gbc_core.hpp"
#include "../emulator/save_states.hpp"
#include "../emulator/settings.hpp"
#include "../emulator/state_archive.hpp"

namespace emulator_demo::app {

namespace {

// Enveloppe de sauvegarde d'état GBC, sur le modèle de SaveStates (étiquette
// + version + tampon StateArchive) : SaveStates est typé pour Core et les
// besoins de GbcCore ne justifient pas d'en faire un modèle générique.
constexpr const char GBC_STATE_TAG[] = "GBCSTATE";
constexpr uint32_t GBC_STATE_VERSION = 1;

bool GbcSaveState(GbcCore &core, const String &path) {
	StateArchive archive(true);
	core.ioState(archive);
	auto opened = sdl3::IOStream::FromFile(path, "wb");
	if (!opened)
		return false;
	sdl3::IOStream file = std::move(opened).Unwrap();
	const size_t tagLength = sizeof(GBC_STATE_TAG) - 1;
	return file.Write(GBC_STATE_TAG, tagLength) == tagLength &&
		   file.Write(&GBC_STATE_VERSION, sizeof(uint32_t)) == sizeof(uint32_t) &&
		   (archive.buffer.empty() || file.Write(archive.buffer.data(), archive.buffer.size()) == archive.buffer.size());
}

bool GbcLoadState(GbcCore &core, const String &path, String &detail) {
	auto opened = sdl3::IOStream::FromFile(path, "rb");
	if (!opened) {
		detail = String::Format("fichier illisible : %s", path.CStr());
		return false;
	}
	sdl3::IOStream file = std::move(opened).Unwrap();
	const size_t tagLength = sizeof(GBC_STATE_TAG) - 1;
	const size_t headerSize = tagLength + sizeof(uint32_t);
	Sint64 total = file.GetSize();
	char tag[sizeof(GBC_STATE_TAG)] = {};
	uint32_t version = 0;
	if (total < 0 || size_t(total) < headerSize || file.Read(tag, tagLength) != tagLength ||
		std::memcmp(tag, GBC_STATE_TAG, tagLength) != 0 || file.Read(&version, sizeof(uint32_t)) != sizeof(uint32_t) ||
		version != GBC_STATE_VERSION) {
		detail = String("en-tête d'état GBC invalide");
		return false;
	}
	std::vector<uint8_t> data(size_t(total) - headerSize);
	if (!data.empty() && file.Read(data.data(), data.size()) != data.size()) {
		detail = String("état GBC tronqué");
		return false;
	}
	// Même stratégie que SaveStates::loadState : instantané de secours,
	// restauré si l'archive se révèle corrompue en cours de lecture.
	StateArchive backup(true);
	core.ioState(backup);
	StateArchive archive(std::move(data));
	core.ioState(archive);
	if (archive.failed) {
		StateArchive restore(std::move(backup.buffer));
		core.ioState(restore);
		detail = String("état GBC corrompu : état précédent restauré");
		return false;
	}
	return true;
}

/// Extension -> console. NONE si l'extension ne dit rien (on se rabat alors
/// sur les octets de l'en-tête).
Option<RomSystem> SystemFromExtension(const String &path) {
	String lower = path.ToLower();
	if (lower.EndsWith(".nds"))
		return Some(RomSystem::NDS);
	if (lower.EndsWith(".gba"))
		return Some(RomSystem::GBA);
	if (lower.EndsWith(".gbc") || lower.EndsWith(".gb"))
		return Some(RomSystem::GBC);
	return NONE;
}

} // namespace

Result<std::unique_ptr<EmulatorSession>, String> EmulatorSession::Create(const String &romPath,
																		  SessionOptions options) {
	// Vérifié avant tout : le cœur contrôle d'abord le BIOS, et une ROM
	// absente était signalée comme « BIOS GBA requis ».
	if (sdl3::filesystem::PathInfo(romPath).IsNone())
		return Err(String::Format("ROM introuvable : %s", romPath.CStr()));

	// ROM simple ou entrée d'archive, en mémoire : sert à la détection de la
	// console, aux métadonnées et, pour une archive, à l'extraction.
	auto loaded = LoadRom(romPath, options.romEntry);
	if (loaded.IsError())
		return Err(loaded.Error());
	LoadedRom rom = std::move(loaded).Unwrap();
	Option<RomMetadata> metadata = ReadRomMetadataFromBytes(rom.bytes);

	Option<RomSystem> system = SystemFromExtension(rom.LogicalPath());
	if (system.IsNone()) {
		if (metadata.IsNone())
			return Err(String::Format("format de ROM non reconnu : %s", rom.DisplayName().CStr()));
		system = Some(metadata.Value().system);
	}

	String extractDirectory = options.extractDirectory.IsEmpty() ? DefaultExtractDirectory() : options.extractDirectory;
	auto bootPath = MaterializeRom(rom, extractDirectory);
	if (bootPath.IsError())
		return Err(bootPath.Error());

	std::unique_ptr<EmulatorSession> session(new EmulatorSession(system.Unwrap(), std::move(options)));
	session->m_romPath = romPath;
	session->m_bootPath = std::move(bootPath).Unwrap();
	session->m_logicalPath = rom.LogicalPath();
	session->m_archive = rom.archive;
	session->m_metadata = std::move(metadata);
	// Figé au lancement : un dossier changé dans la configuration vaut pour la
	// ROM suivante (le fil d'émulation lit ce chemin entre deux images).
	session->m_statePath = session->m_options.statePath.IsEmpty() ? Settings::stateFilePath(session->m_logicalPath)
																   : session->m_options.statePath;
	if (session->m_archive.IsSome()) {
		Settings::registerRomAlias(session->m_bootPath, session->m_logicalPath);
		SDL_Log("emulator_demo : %s extraite vers %s", rom.DisplayName().CStr(), session->m_bootPath.CStr());
	}
	auto booted = session->Boot();
	if (booted.IsError())
		return Err(booted.Error());
	return Ok(std::move(session));
}

EmulatorSession::EmulatorSession(RomSystem system, SessionOptions options)
	: m_system(system), m_options(std::move(options)) {}

EmulatorSession::~EmulatorSession() {
	Stop();
	m_audio.reset(); // la pompe audio appelle le cœur : arrêtée avant lui
	// Les cœurs (détruits après ce corps) ont déjà calculé le chemin de leur
	// .sav au chargement : l'alias peut être retiré dès maintenant.
	if (m_archive.IsSome())
		Settings::unregisterRomAlias(m_bootPath);
}

String EmulatorSession::DisplayName() const {
	if (m_archive.IsNone())
		return m_romPath;
	size_t slash = m_romPath.Rfind('/');
	String archiveName = slash == String::NPOS ? m_romPath : m_romPath.Substr(slash + 1);
	return String::Format("%s › %s", archiveName.CStr(), m_archive.Value().entryName.CStr());
}

Result<bool, String> EmulatorSession::Boot() {
	// Le HUD, les captures et la taille des textures supposent la résolution
	// native : la haute résolution 3D quadruplerait le framebuffer NDS.
	if (Settings::getHighRes3D() != 0) {
		SDL_Log("emulator_demo : highRes3D désactivé (non géré par la démo)");
		Settings::setHighRes3D(0);
	}
	// Limiteur : 1 = l'audio cadence l'émulation ; 0 = pas d'attente dans le
	// cœur (sans audio, c'est l'horloge de la session qui cadence, ou rien du
	// tout en vitesse libre / sans écran).
	Settings::setFpsLimiter((m_options.mixer && !m_options.unlimitedSpeed) ? 1 : 0);

	if (m_system == RomSystem::GBC) {
		auto rom = sdl3::ReadFile(m_bootPath);
		if (!rom)
			return Err(String::Format("lecture de la ROM impossible : %s", String(rom.Error()).CStr()));
		m_gbcCore = std::make_unique<GbcCore>(std::move(rom).Unwrap());
		if (!m_gbcCore->isValid()) {
			m_gbcCore.reset();
			return Err(String("cartouche GB/GBC non prise en charge (type de MBC)"));
		}
		if (m_gbcCore->hasBattery()) {
			// Même convention qu'à l'origine (<rom>.gbc.sav), dans --save-dir si donné.
			// Même convention qu'à l'origine (<rom>.gbc.sav), rapportée au
			// chemin logique (à côté de l'archive) et à --save-dir.
			String extension = m_logicalPath.Substr(m_logicalPath.Rfind('.'));
			String batteryPath = Settings::saveBasePath(m_bootPath) + extension + ".sav";
			(void)m_gbcCore->loadExtRam(batteryPath);
			m_gbcCore->setExtRamPath(batteryPath);
		}
		m_scratch.assign(size_t(gbc::kScreenWidth) * gbc::kScreenHeight, 0xFF000000u);
		if (m_options.mixer)
			m_audio = std::make_unique<AudioOutput>([this](int count) { return m_gbcCore->getApu().getSamples(count); },
													*m_options.mixer, m_options.threads);
	} else {
		auto core = Core::create(m_system == RomSystem::GBA ? ROM_GBA : ROM_NDS, m_bootPath);
		if (core.IsError())
			return Err(String::Format("démarrage du cœur %s impossible : %s", RomSystemName(m_system),
									  Core::errorText(core.Error())));
		m_core = std::move(core).Unwrap();
		m_saveStates = std::make_unique<SaveStates>(m_core.get());
		m_saveStates->setPath(m_statePath, m_system == RomSystem::GBA ? ROM_GBA : ROM_NDS);
		m_scratch.assign(m_system == RomSystem::GBA ? size_t(240) * 160 : size_t(256) * 384, 0xFF000000u);
		if (m_options.netBridge) {
			NetBridge *bridge = m_options.netBridge;
			m_core->wifi.onPacketSent = [bridge](const uint8_t *data, size_t length) {
				bridge->SendPacket(std::span<const uint8_t>(data, length));
			};
		}
		if (m_options.mixer)
			m_audio = std::make_unique<AudioOutput>([this](int count) { return m_core->spu.getSamples(count); },
													*m_options.mixer, m_options.threads);
	}

	std::lock_guard<std::mutex> lock(m_frameMutex);
	m_latest.width = m_system == RomSystem::GBC ? gbc::kScreenWidth : (m_system == RomSystem::GBA ? 240 : 256);
	m_latest.height = m_system == RomSystem::GBC ? gbc::kScreenHeight : (m_system == RomSystem::GBA ? 160 : 384);
	m_latest.pixels.assign(m_scratch.size(), 0xFF000000u);
	return Ok(true);
}

// ── Avancement ───────────────────────────────────────────────────────────────

void EmulatorSession::Start() {
	if (m_thread.joinable())
		return;
	m_nextDeadline = std::chrono::steady_clock::now();
	m_thread = std::jthread([this](std::stop_token stop) { EmulationLoop(stop); });
}

void EmulatorSession::Stop() {
	if (!m_thread.joinable())
		return;
	m_thread.request_stop();
	m_thread.join();
}

void EmulatorSession::RunFrame() {
	if (m_thread.joinable())
		return;
	StepOneFrame();
}

void EmulatorSession::SetPaused(bool paused) {
	m_paused.store(paused, std::memory_order_relaxed);
	if (m_audio)
		m_audio->SetPaused(paused);
}

void EmulatorSession::EmulationLoop(std::stop_token stop) {
	sdl3::ForeignThreadScope sdlThread; // fil std::jthread qui appelle SDL : TLS à libérer
	ThreadTracker::Scope tracked(m_options.threads);
	// Sans audio pour cadencer, une horloge tient la fréquence de la console
	// (sinon le fil tournerait à plein régime : plusieurs centaines d'images
	// par seconde en GBC).
	const bool clockPaced = !m_options.unlimitedSpeed && !m_audio;
	const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
		std::chrono::duration<double>(1.0 / NativeRefreshRate()));

	while (!stop.stop_requested()) {
		if (m_paused.load(std::memory_order_relaxed)) {
			DrainRequests(); // F5/F9 restent utilisables en pause
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			m_nextDeadline = std::chrono::steady_clock::now();
			continue;
		}
		StepOneFrame();
		if (clockPaced) {
			m_nextDeadline += period;
			auto now = std::chrono::steady_clock::now();
			if (m_nextDeadline > now)
				std::this_thread::sleep_until(m_nextDeadline);
			else if (now - m_nextDeadline > std::chrono::milliseconds(100))
				m_nextDeadline = now; // trop en retard : on ne rattrape pas en rafale
		}
	}
}

void EmulatorSession::StepOneFrame() {
	DrainRequests();
	const long frame = m_frames.load(std::memory_order_relaxed) + 1;
	ApplyButtons(frame);

	if (m_core) {
		if (m_options.netBridge) {
			while (true) {
				Option<std::vector<uint8_t>> packet = m_options.netBridge->PollReceived();
				if (packet.IsNone())
					break;
				m_core->wifi.injectPacket(packet.Value().data(), packet.Value().size());
			}
		}
		m_core->runFrame();
	} else if (m_gbcCore) {
		m_gbcCore->runFrame();
	}

	m_frames.store(frame, std::memory_order_relaxed);
	PublishFrame(frame);
	// Après l'image N : « sauvegarder à l'image N » capture l'état tel qu'il
	// est une fois cette image produite, comme une capture d'écran.
	ApplyScriptedStateActions(frame);
}

// ── Entrées ─────────────────────────────────────────────────────────────────

void EmulatorSession::SetTouch(int x, int y, bool pressed) {
	m_touchX.store(x, std::memory_order_relaxed);
	m_touchY.store(y, std::memory_order_relaxed);
	m_touchPressed.store(pressed, std::memory_order_relaxed);
}

void EmulatorSession::ApplyButtons(long frame) {
	uint32_t scripted = 0;
	for (const ScriptedPress &press : m_options.presses)
		if (frame >= press.frame && frame < press.frame + press.holdFrames)
			scripted |= 1u << press.button;

	if (scripted != m_scriptedButtons) {
		std::lock_guard<std::mutex> lock(m_recordMutex);
		for (int button = 0; button < BTN_COUNT; ++button) {
			bool before = (m_scriptedButtons >> button) & 1u;
			bool after = (scripted >> button) & 1u;
			if (before != after)
				m_inputEdges.push_back(InputEdgeRecord{frame, button, after});
		}
		m_scriptedButtons = scripted;
	}

	uint32_t wanted = scripted | m_hostButtons.load(std::memory_order_relaxed);
	const int buttonCount = m_gbcCore ? int(gbc::GBC_BTN_COUNT) : int(BTN_COUNT);
	for (int button = 0; button < buttonCount; ++button) {
		bool pressed = (wanted >> button) & 1u;
		bool was = (m_appliedButtons >> button) & 1u;
		if (pressed == was)
			continue;
		if (m_core) {
			if (pressed)
				m_core->input.pressKey(button);
			else
				m_core->input.releaseKey(button);
		} else if (m_gbcCore) {
			if (pressed)
				m_gbcCore->pressKey(gbc::GbcButton(button));
			else
				m_gbcCore->releaseKey(gbc::GbcButton(button));
		}
	}
	m_appliedButtons = wanted;

	if (m_core && m_system == RomSystem::NDS) {
		bool touch = m_touchPressed.load(std::memory_order_relaxed);
		if (touch)
			m_core->spi.setTouch(m_touchX.load(std::memory_order_relaxed), m_touchY.load(std::memory_order_relaxed));
		if (touch != m_appliedTouch) {
			if (touch) {
				m_core->input.pressScreen();
			} else {
				m_core->input.releaseScreen();
				m_core->spi.clearTouch();
			}
			m_appliedTouch = touch;
		}
	}
}

// ── Sauvegardes d'état ──────────────────────────────────────────────────────

void EmulatorSession::RequestQuickSave() {
	if (!m_thread.joinable()) {
		String detail;
		bool ok = SaveStateNow(detail);
		RecordStateOperation(EmulatedFrames(), StateActionKind::SAVE, ok, "interface", std::move(detail));
		return;
	}
	std::lock_guard<std::mutex> lock(m_requestMutex);
	m_requests.push_back(Request::SAVE);
}

void EmulatorSession::RequestQuickLoad() {
	if (!m_thread.joinable()) {
		String detail;
		bool ok = LoadStateNow(detail);
		RecordStateOperation(EmulatedFrames(), StateActionKind::LOAD, ok, "interface", std::move(detail));
		return;
	}
	std::lock_guard<std::mutex> lock(m_requestMutex);
	m_requests.push_back(Request::LOAD);
}

bool EmulatorSession::HasStateFile() const { return sdl3::filesystem::PathInfo(m_statePath).IsSome(); }

void EmulatorSession::DrainRequests() {
	std::vector<Request> pending;
	{
		std::lock_guard<std::mutex> lock(m_requestMutex);
		pending.swap(m_requests);
	}
	for (Request request : pending) {
		String detail;
		const bool save = request == Request::SAVE;
		bool ok = save ? SaveStateNow(detail) : LoadStateNow(detail);
		RecordStateOperation(EmulatedFrames(), save ? StateActionKind::SAVE : StateActionKind::LOAD, ok, "interface",
							 std::move(detail));
	}
}

void EmulatorSession::ApplyScriptedStateActions(long frame) {
	for (const ScriptedStateAction &action : m_options.stateActions) {
		if (action.frame != frame)
			continue;
		String detail;
		bool ok = action.kind == StateActionKind::SAVE ? SaveStateNow(detail) : LoadStateNow(detail);
		RecordStateOperation(frame, action.kind, ok, "script", std::move(detail));
		// Un chargement remplace l'image affichable : la republier évite de
		// capturer à cette même image le contenu d'AVANT le chargement.
		if (ok && action.kind == StateActionKind::LOAD && m_gbcCore)
			PublishFrame(frame);
	}
}

bool EmulatorSession::SaveStateNow(String &detail) {
	// La pompe audio lit le double tampon du SPU/APU : pas de sérialisation
	// pendant qu'elle y copie un bloc.
	const bool audioWasPaused = m_audio && m_audio->IsPaused();
	if (m_audio)
		m_audio->SetPaused(true);
	bool ok = false;
	// Premier état de la session dans ce dossier : il peut ne pas exister.
	if (size_t slash = m_statePath.Rfind('/'); slash != String::NPOS && slash > 0)
		(void)sdl3::filesystem::CreateDirectory(m_statePath.Substr(0, slash));
	if (m_saveStates)
		ok = m_saveStates->saveState();
	else if (m_gbcCore)
		ok = GbcSaveState(*m_gbcCore, m_statePath);
	if (m_audio)
		m_audio->SetPaused(audioWasPaused);
	detail = ok ? m_statePath : String::Format("écriture impossible : %s", m_statePath.CStr());
	return ok;
}

bool EmulatorSession::LoadStateNow(String &detail) {
	const bool audioWasPaused = m_audio && m_audio->IsPaused();
	if (m_audio)
		m_audio->SetPaused(true);
	bool ok = false;
	if (m_saveStates) {
		StateResult check = m_saveStates->checkState();
		if (check != STATE_SUCCESS) {
			detail = check == STATE_FILE_FAIL		 ? String::Format("fichier illisible : %s", m_statePath.CStr())
					 : check == STATE_FORMAT_FAIL ? String("format d'état inconnu")
												  : String("version d'état incompatible");
		} else {
			ok = m_saveStates->loadState();
			detail = ok ? m_statePath : String("état corrompu : état précédent restauré");
		}
	} else if (m_gbcCore) {
		ok = GbcLoadState(*m_gbcCore, m_statePath, detail);
		if (ok)
			detail = m_statePath;
	}
	if (m_audio)
		m_audio->SetPaused(audioWasPaused);
	return ok;
}

void EmulatorSession::RecordStateOperation(long frame, StateActionKind kind, bool ok, const char *origin,
										   String detail) {
	SDL_Log("emulator_demo : %s d'état à l'image %ld : %s (%s)", kind == StateActionKind::SAVE ? "sauvegarde" : "chargement",
			frame, ok ? "réussi" : "échec", detail.CStr());
	std::lock_guard<std::mutex> lock(m_recordMutex);
	m_stateOperations.push_back(StateOperationRecord{frame, kind, ok, String(origin), std::move(detail)});
}

// ── Observation ─────────────────────────────────────────────────────────────

void EmulatorSession::PublishFrame(long frame) {
	bool produced = false;
	if (m_core) {
		// En mode GBA, `gbaCrop` extrait directement l'image 240x160 ; en NDS
		// on reçoit les deux écrans l'un sous l'autre.
		produced = m_core->gpu.getFrame(m_scratch.data(), m_system == RomSystem::GBA);
	} else if (m_gbcCore) {
		m_gbcCore->getFrame(m_scratch.data());
		produced = true;
	}
	if (!produced)
		return;
	std::lock_guard<std::mutex> lock(m_frameMutex);
	m_latest.pixels.swap(m_scratch);
	m_latest.frame = frame;
	// `m_scratch` récupère l'ancien tampon, de même taille : réécrit à la
	// prochaine image.
}

bool EmulatorSession::CopyLatestFrame(FrameSnapshot &out) const {
	std::lock_guard<std::mutex> lock(m_frameMutex);
	if (m_latest.frame == 0 || (out.frame == m_latest.frame && out.pixels.size() == m_latest.pixels.size()))
		return false;
	out = m_latest;
	return true;
}

int EmulatorSession::CoreFps() const {
	if (m_core)
		return m_core->getFps();
	if (m_gbcCore)
		return m_gbcCore->getFps();
	return 0;
}

double EmulatorSession::NativeRefreshRate() const {
	// NDS : 33,8688 MHz / (6 × 355 × 263) ; GBA et GB : 4,194304 MHz / 70224.
	return m_system == RomSystem::NDS ? 59.8261 : 59.7275;
}

std::vector<StateOperationRecord> EmulatorSession::StateOperations() const {
	std::lock_guard<std::mutex> lock(m_recordMutex);
	return m_stateOperations;
}

std::vector<InputEdgeRecord> EmulatorSession::InputEdges() const {
	std::lock_guard<std::mutex> lock(m_recordMutex);
	return m_inputEdges;
}

ActionReplay *EmulatorSession::Cheats() const {
	return (m_core && m_system == RomSystem::NDS) ? &m_core->actionReplay : nullptr;
}

} // namespace emulator_demo::app
