// Définitions de sdl3/thread.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sdl3/thread.hpp"

namespace sdl3 {

// ── SpinLockGuard ────────────────────────────────────────────────────────────

SpinLockGuard::~SpinLockGuard() {
	if (lock)
		lock->Unlock();
}

// ── AtomicInt ────────────────────────────────────────────────────────────────

bool AtomicInt::CompareExchange(int expected, int desired) noexcept {
	return SDL_CompareAndSwapAtomicInt(&value, expected, desired);
}

// ── AtomicU32 ────────────────────────────────────────────────────────────────

bool AtomicU32::CompareExchange(Uint32 expected, Uint32 desired) noexcept {
	return SDL_CompareAndSwapAtomicU32(&value, expected, desired);
}

// ── Mutex ────────────────────────────────────────────────────────────────────

Result<Mutex, Error> Mutex::Create() {
	auto *m = SDL_CreateMutex();
	if (!m)
		return Err(GetError());
	return Ok(Mutex(m));
}

// ── MutexGuard ───────────────────────────────────────────────────────────────

MutexGuard::MutexGuard(Mutex &m) : m_handle(m.Get()) {
	if (m_handle)
		SDL_LockMutex(m_handle);
}

MutexGuard::~MutexGuard() {
	if (m_handle)
		SDL_UnlockMutex(m_handle);
}

// ── RWLock ───────────────────────────────────────────────────────────────────

Result<RWLock, Error> RWLock::Create() {
	auto *l = SDL_CreateRWLock();
	if (!l)
		return Err(GetError());
	return Ok(RWLock(l));
}

// ── RWLockReadGuard ──────────────────────────────────────────────────────────

RWLockReadGuard::RWLockReadGuard(RWLock &l) : m_handle(l.Get()) {
	if (m_handle)
		SDL_LockRWLockForReading(m_handle);
}

RWLockReadGuard::~RWLockReadGuard() {
	if (m_handle)
		SDL_UnlockRWLock(m_handle);
}

// ── RWLockWriteGuard ─────────────────────────────────────────────────────────

RWLockWriteGuard::RWLockWriteGuard(RWLock &l) : m_handle(l.Get()) {
	if (m_handle)
		SDL_LockRWLockForWriting(m_handle);
}

RWLockWriteGuard::~RWLockWriteGuard() {
	if (m_handle)
		SDL_UnlockRWLock(m_handle);
}

// ── Semaphore ────────────────────────────────────────────────────────────────

Result<Semaphore, Error> Semaphore::Create(Uint32 initialValue) {
	auto *s = SDL_CreateSemaphore(initialValue);
	if (!s)
		return Err(GetError());
	return Ok(Semaphore(s));
}

// ── Condition ────────────────────────────────────────────────────────────────

Result<Condition, Error> Condition::Create() {
	auto *c = SDL_CreateCondition();
	if (!c)
		return Err(GetError());
	return Ok(Condition(c));
}

void Condition::Wait(Mutex &mutex) {
	if (m_handle && mutex.Get())
		SDL_WaitCondition(m_handle, mutex.Get());
}

bool Condition::WaitTimeout(Mutex &mutex, Sint32 timeoutMs) {
	return m_handle && mutex.Get() && SDL_WaitConditionTimeout(m_handle, mutex.Get(), timeoutMs);
}

namespace detail {

int SDLCALL ThreadTrampoline(void *data) {
	auto *ctx = static_cast<ThreadCtx *>(data);
	int result = ctx->fn ? ctx->fn() : 0;
	delete ctx;
	return result;
}

} // namespace detail

// ── Thread ───────────────────────────────────────────────────────────────────

Thread::~Thread() {
	if (m_handle)
		SDL_WaitThread(m_handle, nullptr);
}

Result<Thread, Error> Thread::Create(std::function<int()> fn, const String &name) {
	auto *ctx = new detail::ThreadCtx{std::move(fn)};
	auto *t = SDL_CreateThread(detail::ThreadTrampoline, name.IsEmpty() ? nullptr : name.c_str(), ctx);
	if (!t) {
		delete ctx;
		return Err(GetError());
	}
	return Ok(Thread(t));
}

ThreadState Thread::State() const noexcept {
	return m_handle ? ThreadState(SDL_GetThreadState(m_handle)) : ThreadState::UNKNOWN;
}

Option<int> Thread::Wait() noexcept {
	if (!m_handle)
		return NONE;
	int status = 0;
	SDL_WaitThread(m_handle, &status);
	m_handle = nullptr;
	return Some(status);
}

void Thread::Detach() noexcept {
	if (m_handle) {
		SDL_DetachThread(m_handle);
		m_handle = nullptr;
	}
}

bool Thread::SetCurrentPriority(ThreadPriority p) noexcept {
	return SDL_SetCurrentThreadPriority(detail::ToSDL(p));
}

} // namespace sdl3
