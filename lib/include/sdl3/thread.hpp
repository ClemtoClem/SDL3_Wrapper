#pragma once
#include <SDL3/SDL.h>
#include <functional>

#include "../core/core.hpp"
#include "error.hpp"

namespace sdl3 {

// ============================================================================
// SpinLock — SDL_SpinLock (verrou d'attente active, sans allocation)
// ============================================================================

class SpinLock {
	mutable SDL_SpinLock lock = 0;

public:
	SpinLock() noexcept = default;
	SpinLock(const SpinLock &) = delete;
	SpinLock &operator=(const SpinLock &) = delete;

	void Lock() const noexcept { SDL_LockSpinlock(&lock); }
	[[nodiscard]] bool TryLock() const noexcept { return SDL_TryLockSpinlock(&lock); }
	void Unlock() const noexcept { SDL_UnlockSpinlock(&lock); }
};

class SpinLockGuard {
	const SpinLock *lock = nullptr;

public:
	explicit SpinLockGuard(const SpinLock &lock) : lock(&lock) { this->lock->Lock(); }
	~SpinLockGuard();

	SpinLockGuard(const SpinLockGuard &) = delete;
	SpinLockGuard &operator=(const SpinLockGuard &) = delete;
	SpinLockGuard(SpinLockGuard &&o) noexcept : lock(o.lock) { o.lock = nullptr; }
};

// ============================================================================
// AtomicInt / AtomicU32 / AtomicPointer<T> — SDL_AtomicInt/SDL_AtomicU32/CAS
// pointeur. Types VALEUR (pas de ressource à détruire), non copiables (même
// sémantique que std::atomic — copier un atomique n'a pas de sens : ce sont
// les OPÉRATIONS qui sont atomiques, pas l'objet lui-même).
// ============================================================================

class AtomicInt {
	mutable SDL_AtomicInt value{};

public:
	AtomicInt() noexcept = default;
	explicit AtomicInt(int v) noexcept { SDL_SetAtomicInt(&value, v); }
	AtomicInt(const AtomicInt &) = delete;
	AtomicInt &operator=(const AtomicInt &) = delete;

	[[nodiscard]] int Load() const noexcept { return SDL_GetAtomicInt(&value); }
	int Store(int v) noexcept { return SDL_SetAtomicInt(&value, v); }
	int FetchAdd(int v) noexcept { return SDL_AddAtomicInt(&value, v); }
	[[nodiscard]] bool CompareExchange(int expected, int desired) noexcept;
};

class AtomicU32 {
	mutable SDL_AtomicU32 value{};

public:
	AtomicU32() noexcept = default;
	explicit AtomicU32(Uint32 v) noexcept { SDL_SetAtomicU32(&value, v); }
	AtomicU32(const AtomicU32 &) = delete;
	AtomicU32 &operator=(const AtomicU32 &) = delete;

	[[nodiscard]] Uint32 Load() const noexcept { return SDL_GetAtomicU32(&value); }
	Uint32 Store(Uint32 v) noexcept { return SDL_SetAtomicU32(&value, v); }
	Uint32 FetchAdd(int v) noexcept { return SDL_AddAtomicU32(&value, v); }
	[[nodiscard]] bool CompareExchange(Uint32 expected, Uint32 desired) noexcept;
};

template <typename T> class AtomicPointer {
	mutable void *value = nullptr;

public:
	AtomicPointer() noexcept = default;
	explicit AtomicPointer(T *v) noexcept { SDL_SetAtomicPointer(&value, v); }
	AtomicPointer(const AtomicPointer &) = delete;
	AtomicPointer &operator=(const AtomicPointer &) = delete;

	[[nodiscard]] T *Load() const noexcept { return static_cast<T *>(SDL_GetAtomicPointer(&value)); }
	T *Store(T *v) noexcept { return static_cast<T *>(SDL_SetAtomicPointer(&value, v)); }
	[[nodiscard]] bool CompareExchange(T *expected, T *desired) noexcept {
		return SDL_CompareAndSwapAtomicPointer(&value, expected, desired);
	}
};

// ============================================================================
// Mutex + MutexGuard — SDL_Mutex (verrou exclusif)
// ============================================================================

class Mutex : public Wrapper<SDL_Mutex, SDL_DestroyMutex> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<Mutex, Error> Create();

	void Lock() { if (m_handle) SDL_LockMutex(m_handle); }
	[[nodiscard]] bool TryLock() { return m_handle && SDL_TryLockMutex(m_handle); }
	void Unlock() { if (m_handle) SDL_UnlockMutex(m_handle); }
};

/// Verrou scopé (lock au constructeur, unlock au destructeur, move-only) —
/// même patron que `PropertiesLock` (misc.hpp).
class MutexGuard {
	SDL_Mutex *m_handle = nullptr;

public:
	explicit MutexGuard(Mutex &m);
	~MutexGuard();

	MutexGuard(const MutexGuard &) = delete;
	MutexGuard &operator=(const MutexGuard &) = delete;
	MutexGuard(MutexGuard &&o) noexcept : m_handle(o.m_handle) { o.m_handle = nullptr; }
};

// ============================================================================
// RWLock + RWLockReadGuard/RWLockWriteGuard — SDL_RWLock (verrou lecture/
// écriture : plusieurs lecteurs simultanés OU un seul rédacteur exclusif)
// ============================================================================

class RWLock : public Wrapper<SDL_RWLock, SDL_DestroyRWLock> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<RWLock, Error> Create();

	void LockRead() { if (m_handle) SDL_LockRWLockForReading(m_handle); }
	void LockWrite() { if (m_handle) SDL_LockRWLockForWriting(m_handle); }
	[[nodiscard]] bool TryLockRead() { return m_handle && SDL_TryLockRWLockForReading(m_handle); }
	[[nodiscard]] bool TryLockWrite() { return m_handle && SDL_TryLockRWLockForWriting(m_handle); }
	void Unlock() { if (m_handle) SDL_UnlockRWLock(m_handle); }
};

class RWLockReadGuard {
	SDL_RWLock *m_handle = nullptr;

public:
	explicit RWLockReadGuard(RWLock &l);
	~RWLockReadGuard();

	RWLockReadGuard(const RWLockReadGuard &) = delete;
	RWLockReadGuard &operator=(const RWLockReadGuard &) = delete;
	RWLockReadGuard(RWLockReadGuard &&o) noexcept : m_handle(o.m_handle) { o.m_handle = nullptr; }
};

class RWLockWriteGuard {
	SDL_RWLock *m_handle = nullptr;

public:
	explicit RWLockWriteGuard(RWLock &l);
	~RWLockWriteGuard();

	RWLockWriteGuard(const RWLockWriteGuard &) = delete;
	RWLockWriteGuard &operator=(const RWLockWriteGuard &) = delete;
	RWLockWriteGuard(RWLockWriteGuard &&o) noexcept : m_handle(o.m_handle) { o.m_handle = nullptr; }
};

// ============================================================================
// Semaphore — SDL_Semaphore
// ============================================================================

class Semaphore : public Wrapper<SDL_Semaphore, SDL_DestroySemaphore> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<Semaphore, Error> Create(Uint32 initialValue);

	void Wait() { if (m_handle) SDL_WaitSemaphore(m_handle); }
	[[nodiscard]] bool TryWait() { return m_handle && SDL_TryWaitSemaphore(m_handle); }
	[[nodiscard]] bool WaitTimeout(Sint32 timeoutMs) { return m_handle && SDL_WaitSemaphoreTimeout(m_handle, timeoutMs); }
	void Signal() { if (m_handle) SDL_SignalSemaphore(m_handle); }
	[[nodiscard]] Uint32 Value() const { return m_handle ? SDL_GetSemaphoreValue(m_handle) : 0; }
};

// ============================================================================
// Condition — SDL_Condition (variable de condition, toujours utilisée avec
// un Mutex déjà verrouillé par l'appelant — mêmes règles que
// std::condition_variable)
// ============================================================================

class Condition : public Wrapper<SDL_Condition, SDL_DestroyCondition> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<Condition, Error> Create();

	void Signal() { if (m_handle) SDL_SignalCondition(m_handle); }
	void Broadcast() { if (m_handle) SDL_BroadcastCondition(m_handle); }
	/// `mutex` doit être verrouillé par l'appelant AVANT d'appeler wait() —
	/// relâché pendant l'attente, reverrouillé avant le retour (même contrat
	/// que SDL_WaitCondition/std::condition_variable::Wait).
	void Wait(Mutex &mutex);
	[[nodiscard]] bool WaitTimeout(Mutex &mutex, Sint32 timeoutMs);
};

// ============================================================================
// InitState — SDL_InitState (patron d'initialisation paresseuse thread-safe
// à usage unique, ex: une bibliothèque tierce initialisée au premier appel)
// ============================================================================

class InitState {
	mutable SDL_InitState state{};

public:
	InitState() noexcept = default;
	InitState(const InitState &) = delete;
	InitState &operator=(const InitState &) = delete;

	/// Vrai si CET appelant doit effectuer l'initialisation (un seul thread
	/// parmi ceux qui appellent simultanément la recevra) — appeler
	/// setInitialized(true) une fois l'initialisation terminée.
	[[nodiscard]] bool ShouldInit() const noexcept { return SDL_ShouldInit(&state); }
	/// Symétrique pour la désinitialisation.
	[[nodiscard]] bool ShouldQuit() const noexcept { return SDL_ShouldQuit(&state); }
	void SetInitialized(bool initialized) noexcept { SDL_SetInitialized(&state, initialized); }
};

// ============================================================================
// TLS<T> — SDL_TLS (stockage local au thread, typé)
// ============================================================================

template <typename T> class TLS {
	mutable SDL_TLSID id{};

public:
	TLS() noexcept = default;
	TLS(const TLS &) = delete;
	TLS &operator=(const TLS &) = delete;

	[[nodiscard]] T *Get() const noexcept { return static_cast<T *>(SDL_GetTLS(&id)); }
	bool Set(T *value, SDL_TLSDestructorCallback destructor = nullptr) noexcept {
		return SDL_SetTLS(&id, value, destructor);
	}
};

// ============================================================================
// Thread — SDL_Thread. Cas particulier : PAS de Wrapper<T,Deleter> (SDL_Thread
// n'a pas de destructeur unique — il faut SDL_WaitThread [join] OU
// SDL_DetachThread, jamais les deux) — gère son propre état move-only, avec
// un destructeur qui joint par défaut (sécurité, comme std::jthread — pas
// std::thread qui std::terminate() sans join/detach explicite).
// ============================================================================

enum class ThreadPriority: Uint8 {
	LOW				= SDL_THREAD_PRIORITY_LOW,
	NORMAL			= SDL_THREAD_PRIORITY_NORMAL,
	HIGH			= SDL_THREAD_PRIORITY_HIGH,
	TIME_CRITICAL	= SDL_THREAD_PRIORITY_TIME_CRITICAL,
};

enum class ThreadState: Uint8 {
	UNKNOWN			= SDL_THREAD_UNKNOWN,
	ALIVE			= SDL_THREAD_ALIVE,
	DETACHED		= SDL_THREAD_DETACHED,
	COMPLETE		= SDL_THREAD_COMPLETE,
};

namespace detail {
	constexpr SDL_ThreadPriority ToSDL(ThreadPriority e) {
		return static_cast<SDL_ThreadPriority>(e);
	}
	constexpr SDL_ThreadState ToSDL(ThreadState e) {
		return static_cast<SDL_ThreadState>(e);
	}
}

namespace detail {
struct ThreadCtx {
	std::function<int()> fn;
};
int SDLCALL ThreadTrampoline(void *data);
} // namespace detail

/// Libère les données locales de fil de SDL (tampon d'erreur de
/// SDL_SetError, valeurs SDL_SetTLS…) pour le fil APPELANT. SDL le fait seul
/// pour ses propres fils (sdl3::Thread) ; un fil créé autrement
/// (std::thread/std::jthread, pool externe) DOIT l'appeler avant de se
/// terminer, sinon ces allocations fuient — relevé par LeakSanitizer sur les
/// fils d'émulation et de pompe audio de examples/emulator_demo.
inline void CleanupTls() noexcept { SDL_CleanupTLS(); }

/// Garde RAII : appelle CleanupTls() à la sortie de portée. À placer en tête
/// du corps d'un fil qui n'a pas été créé par SDL mais appelle SDL.
class ForeignThreadScope {
public:
	ForeignThreadScope() noexcept = default;
	ForeignThreadScope(const ForeignThreadScope &) = delete;
	ForeignThreadScope &operator=(const ForeignThreadScope &) = delete;
	~ForeignThreadScope() { CleanupTls(); }
};

class Thread {
	SDL_Thread *m_handle = nullptr;

public:
	Thread() noexcept = default;
	explicit Thread(SDL_Thread *m_handle) noexcept : m_handle(m_handle) {}

	Thread(const Thread &) = delete;
	Thread &operator=(const Thread &) = delete;
	Thread(Thread &&o) noexcept : m_handle(o.m_handle) { o.m_handle = nullptr; }
	Thread &operator=(Thread &&o) noexcept {
		if (this != &o) {
			if (m_handle)
				SDL_WaitThread(m_handle, nullptr);
			m_handle = o.m_handle;
			o.m_handle = nullptr;
		}
		return *this;
	}
	/// Joint le thread par défaut si ni wait() ni detach() n'ont déjà été
	/// appelés — jamais de thread orphelin non joint/détaché à la sortie de
	/// portée (contrairement à `std::thread`, qui `std::terminate()` dans ce
	/// cas : ici on privilégie la sécurité, cohérent avec le reste du projet).
	~Thread();

	/// `fn` s'exécute sur le nouveau thread ; sa valeur de retour devient le
	/// code de sortie récupérable via wait(). `name` (optionnel) est visible
	/// dans un débogueur/profileur sur les plateformes qui le supportent.
	[[nodiscard]] static Result<Thread, Error> Create(std::function<int()> fn, const String &name = String());

	[[nodiscard]] String Name() const { return m_handle ? String(SDL_GetThreadName(m_handle)) : String(); }
	[[nodiscard]] SDL_ThreadID GetId() const noexcept { return m_handle ? SDL_GetThreadID(m_handle) : 0; }
	[[nodiscard]] ThreadState State() const noexcept;

	/// Attend la fin du thread (bloquant) et renvoie son code de sortie —
	/// `NONE` si déjà joint/détaché (mêmes conventions que `Process::Wait()`).
	/// Après cet appel, l'objet ne représente plus aucun thread.
	Option<int> Wait() noexcept;

	/// Détache le thread (libéré automatiquement à sa fin, sans jamais
	/// pouvoir être joint) — usage fire-and-forget. Après cet appel, l'objet
	/// ne représente plus aucun thread.
	void Detach() noexcept;

	[[nodiscard]] explicit operator bool() const noexcept { return m_handle != nullptr; }

	[[nodiscard]] static SDL_ThreadID CurrentId() noexcept { return SDL_GetCurrentThreadID(); }
	static bool SetCurrentPriority(ThreadPriority p) noexcept;
};

} // namespace sdl3
