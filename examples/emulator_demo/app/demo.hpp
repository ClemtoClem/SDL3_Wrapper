#pragma once
/**
 * emulator_demo::Demo — point d'entrée unique de la démo : interprète la ligne
 * de commande, charge les réglages, lance le mode demandé et produit le
 * rapport d'exécution.
 *
 * Modes :
 *  - `--help`, `--list-buttons`, `--list-roms`, `--rom-info` : informations,
 *    puis sortie ;
 *  - `--headless` : le cœur seul, image par image sur le fil principal, sans
 *    fenêtre, sans GPU ni audio, à cadence libre. Les captures sont alors
 *    celles du framebuffer émulé. Utilisable sur une machine sans écran ;
 *  - par défaut : la coquille fenêtrée `app::Application`.
 *
 * Code de sortie : 0 si tout s'est bien passé ; 1 si la ROM n'a pas démarré
 * ou si une vérification demandée a échoué (capture non écrite, opération
 * d'état en échec, `--require-video` sur une image uniforme) ; 2 si la ligne
 * de commande est invalide (cf. main).
 */
#include <memory>

#include "core/core.hpp"

#include "cli.hpp"
#include "log_console.hpp"
#include "report.hpp"

namespace emulator_demo {

namespace app {
struct FrameSnapshot;
}

class Demo {
public:
	Demo(CommandLine options, String commandLine);
	~Demo();

	Demo(const Demo &) = delete;
	Demo &operator=(const Demo &) = delete;

	[[nodiscard]] int Run();

private:
	/// Réglages modifiables en ligne de commande, mémorisés pour ne PAS les
	/// écrire dans le fichier de réglages en quittant (une option de test ne
	/// doit pas changer durablement la configuration de l'utilisateur).
	struct SavedSettings {
		int directBoot = 1;
		int arm7Hle = 0;
		int threaded2D = 0;
		int threaded3D = 0;
		int screenLayout = 0;
		int fpsLimiter = 1;
		int highRes3D = 0;
		String bios9;
		String bios7;
		String firmware;
		String gbaBios;
	};

	[[nodiscard]] int PrintRomInfo();
	[[nodiscard]] int ListRoms(const String &directory);
	void ListButtons();

	void LoadSettings();
	void ApplyOverrides();
	void RestoreOverridesBeforeSave();
	void DescribeSettings();

	[[nodiscard]] int RunHeadless();
	[[nodiscard]] int RunWindowed();

	void WriteFramebufferPng(const app::FrameSnapshot &snapshot, const String &path);
	void CheckRequests();
	[[nodiscard]] int Finish(uint64_t startTicks);

	CommandLine m_options;
	RunReport m_report;
	ThreadTracker m_threads;
	std::unique_ptr<app::LogConsole> m_log;
	SavedSettings m_saved;
};

} // namespace emulator_demo
