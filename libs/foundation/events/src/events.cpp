// Définitions de events/signal.hpp et events/event_bus.hpp
#include <events/events.hpp>

namespace events {

// ── Connection ───────────────────────────────────────────────────────────────

void Connection::Disconnect() noexcept {
	if (auto state = m_state.lock())
		state->connected.store(false, std::memory_order_release);
	m_state.reset();
}

bool Connection::IsConnected() const noexcept {
	auto state = m_state.lock();
	return state && state->connected.load(std::memory_order_acquire);
}

// ── EventBus ─────────────────────────────────────────────────────────────────

size_t EventBus::Dispatch() {
	std::vector<std::function<void(EventBus &)>> batch;
	{
		std::lock_guard<std::mutex> lock(m_queueMutex);
		batch.swap(m_queue);
	}
	for (auto &deliver : batch)
		deliver(*this);
	return batch.size();
}

size_t EventBus::PendingCount() const {
	std::lock_guard<std::mutex> lock(m_queueMutex);
	return m_queue.size();
}

void EventBus::ClearQueue() {
	std::lock_guard<std::mutex> lock(m_queueMutex);
	m_queue.clear();
}

} // namespace events
