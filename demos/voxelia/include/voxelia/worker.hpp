#pragma once
/**
 * Worker — un fil de travail que le fil principal orchestre.
 *
 * Le fil principal dépose des tâches (Post) qui s'exécutent dans l'ordre sur
 * le fil du Worker, puis attend qu'elles soient faites (Wait) avant de
 * toucher aux données qu'elles utilisent. Deux Workers servent le jeu :
 *  - « voxel-update » (Session) : la simulation (voxel::Game::Tick, l'ECS) ;
 *  - « voxel-render » (WorldRenderer) : le travail CPU du rendu (maillage
 *    des chunks, sur des instantanés du monde).
 * La soumission au GPU et la présentation restent au fil principal (SDL :
 * fenêtre et renderer appartiennent au fil qui les a créés).
 *
 * Construit sur <sdl3/thread.hpp> : sdl3::Thread, Mutex, Condition. Sans
 * fil (création impossible, ou `threaded` faux), Post exécute la tâche sur
 * place : le comportement reste le même, séquentiel.
 */
#include <deque>
#include <functional>

#include <sdl3/thread.hpp>

namespace voxelia {

class Worker {
public:
	explicit Worker(const String &name, bool threaded = true);
	/// Termine les tâches déposées, puis joint le fil.
	~Worker();
	Worker(const Worker &) = delete;
	Worker &operator=(const Worker &) = delete;

	/// Dépose une tâche (exécutée sur place sans fil).
	void Post(std::function<void()> task);
	/// Attend que toutes les tâches déposées soient terminées.
	void Wait();
	/// Tâches déposées pas encore terminées (en cours comprise).
	[[nodiscard]] size_t Pending();
	[[nodiscard]] bool Threaded() const noexcept { return static_cast<bool>(m_thread); }

private:
	int Loop();

	Option<sdl3::Mutex> m_mutex = NONE;
	Option<sdl3::Condition> m_wake = NONE; ///< une tâche arrive, ou l'arrêt
	Option<sdl3::Condition> m_idle = NONE; ///< plus rien à faire
	std::deque<std::function<void()>> m_tasks;
	bool m_busy = false, m_stop = false;
	sdl3::Thread m_thread; ///< dernier membre : lancé quand tout le reste est prêt
};

} // namespace voxelia
