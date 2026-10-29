#include "voxelia/worker.hpp"

#include <cstdio>

namespace voxelia {

Worker::Worker(const String &name, bool threaded) {
	if (!threaded)
		return;
	auto mutex = sdl3::Mutex::Create();
	auto wake = sdl3::Condition::Create();
	auto idle = sdl3::Condition::Create();
	if (!mutex || !wake || !idle) {
		std::printf("[fil %s] primitives indisponibles : exécution sur le fil principal\n", name.CStr());
		return;
	}
	m_mutex = Some(std::move(mutex.Value()));
	m_wake = Some(std::move(wake.Value()));
	m_idle = Some(std::move(idle.Value()));
	auto thread = sdl3::Thread::Create([this]() { return Loop(); }, name);
	if (!thread) {
		std::printf("[fil %s] création impossible (%s) : exécution sur le fil principal\n", name.CStr(), String(thread.Error()).CStr());
		return;
	}
	m_thread = std::move(thread.Value());
}

Worker::~Worker() {
	if (!m_thread)
		return;
	{
		sdl3::MutexGuard lock(m_mutex.Value());
		m_stop = true;
		m_wake.Value().Signal();
	}
	(void)m_thread.Wait(); // les tâches restantes sont faites avant l'arrêt
}

void Worker::Post(std::function<void()> task) {
	if (!m_thread) {
		task();
		return;
	}
	sdl3::MutexGuard lock(m_mutex.Value());
	m_tasks.push_back(std::move(task));
	m_wake.Value().Signal();
}

void Worker::Wait() {
	if (!m_thread)
		return;
	sdl3::MutexGuard lock(m_mutex.Value());
	while (m_busy || !m_tasks.empty())
		m_idle.Value().Wait(m_mutex.Value());
}

size_t Worker::Pending() {
	if (!m_thread)
		return 0;
	sdl3::MutexGuard lock(m_mutex.Value());
	return m_tasks.size() + (m_busy ? 1 : 0);
}

int Worker::Loop() {
	sdl3::Mutex &mutex = m_mutex.Value();
	for (;;) {
		std::function<void()> task;
		{
			sdl3::MutexGuard lock(mutex);
			while (m_tasks.empty() && !m_stop)
				m_wake.Value().Wait(mutex);
			if (m_tasks.empty()) // arrêt demandé, plus rien à faire
				return 0;
			task = std::move(m_tasks.front());
			m_tasks.pop_front();
			m_busy = true;
		}
		task();
		{
			sdl3::MutexGuard lock(mutex);
			m_busy = false;
			if (m_tasks.empty())
				m_idle.Value().Broadcast();
		}
	}
}

} // namespace voxelia
