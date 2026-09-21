#pragma once

#include <SDL3/SDL.h>
#include <functional>
#include <memory>

namespace sdl3 {

// ============================================================================
// Timer helpers (Fonctions globales simples - SDL_timer.h)
// ============================================================================

/**
 * Get the time elapsed since SDL library initialization.
 *
 * @returns a std::chrono::nanoseconds value representing the number of
 *          nanoseconds since the SDL library initialized.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa GetTicksMS
 * @sa GetTicksNS
 */
inline std::chrono::nanoseconds GetTicks() {
	return std::chrono::nanoseconds(SDL_GetTicksNS());
}

/**
 * Get the number of milliseconds that have elapsed since the SDL library
 * initialization.
 *
 * @returns an unsigned 64‑bit integer that represents the number of
 *          milliseconds that have elapsed since the SDL library was initialized
 *          (typically via a call to Init).
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa GetTicksNS
 */
inline Uint64 GetTicksMS() { return SDL_GetTicks(); }

/**
 * Get the number of nanoseconds since SDL library initialization.
 *
 * @returns an unsigned 64-bit value representing the number of nanoseconds
 *          since the SDL library initialized.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 */
inline Uint64 GetTicksNS() { return SDL_GetTicksNS(); }

/**
 * Get the current value of the high resolution counter.
 *
 * This function is typically used for profiling.
 *
 * The counter values are only meaningful relative to each other. Differences
 * between values can be converted to times by using GetPerformanceFrequency().
 *
 * @returns the current counter value.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa GetPerformanceFrequency
 */
inline Uint64 GetPerformanceCounter() { return SDL_GetPerformanceCounter(); }

/**
 * Get the count per second of the high resolution counter.
 *
 * @returns a platform-specific count per second.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa GetPerformanceCounter
 */
inline Uint64 GetPerformanceFrequency() {
	return SDL_GetPerformanceFrequency();
}

/**
 * Wait a specified number of milliseconds before returning.
 *
 * This function waits a specified number of milliseconds before returning. It
 * waits at least the specified time, but possibly longer due to OS scheduling.
 *
 * @param ms the number of milliseconds to delay.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa Delay(std::chrono::nanoseconds)
 * @sa DelayNS
 * @sa DelayPrecise
 */
inline void DelayMS(Uint32 ms) { SDL_Delay(ms); }

/**
 * Wait a specified duration before returning.
 *
 * This function waits a specified duration before returning. It
 * waits at least the specified time, but possibly longer due to OS scheduling.
 *
 * @param duration the duration to delay, with Max precision in ns.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa DelayNS
 * @sa DelayPrecise(std::chrono::nanoseconds)
 */
inline void Delay(std::chrono::nanoseconds duration) {
	SDL_DelayNS(duration.count());
}

/**
 * Wait a specified number of nanoseconds before returning.
 *
 * This function waits a specified number of nanoseconds before returning. It
 * waits at least the specified time, but possibly longer due to OS scheduling.
 *
 * @param ns the number of nanoseconds to delay.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa Delay
 * @sa DelayPrecise(std::chrono::nanoseconds)
 */
inline void DelayNS(Uint64 ns) { SDL_DelayNS(ns); }

/**
 * Wait a specified number of nanoseconds before returning.
 *
 * This function waits a specified number of nanoseconds before returning. It
 * will attempt to wait as close to the requested time as possible, busy waiting
 * if necessary, but could return later due to OS scheduling.
 *
 * @param ns the number of nanoseconds to delay.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa Delay
 * @sa DelayNS
 * @sa DelayPrecise(std::chrono::nanoseconds)
 */
inline void DelayPrecise(Uint64 ns) { SDL_DelayPrecise(ns); }

/**
 * Wait a specified duration before returning.
 *
 * This function waits a specified duration before returning. It
 * will attempt to wait as close to the requested time as possible, busy waiting
 * if necessary, but could return later due to OS scheduling.
 *
 * @param duration the duration to delay.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa Delay(Uint32)
 * @sa Delay(std::chrono::nanoseconds)
 * @sa DelayNS
 * @sa DelayPrecise(Uint64)
 */
inline void DelayPrecise(std::chrono::nanoseconds duration) {
	SDL_DelayPrecise(duration.count());
}

// ============================================================================
// DateTime (SDL_time.h)
// ============================================================================

using Time = SDL_Time;

// --- Fonctions utilitaires statiques de SDL_time.h ---

[[nodiscard]] inline int GetDayOfYear(int year, int month, int day) { return SDL_GetDayOfYear(year, month, day); }

[[nodiscard]] inline int GetDaysInMonth(int year, int month) noexcept { return SDL_GetDaysInMonth(year, month); }

[[nodiscard]] inline int GetDayOfWeek(int year, int month, int day) noexcept {
	return SDL_GetDayOfWeek(year, month, day);
}

class DateTime {
	Time time{0};
	SDL_DateTime dt{};

	void UpdateFromTime(bool localTime) noexcept {
		if (!SDL_TimeToDateTime(time, &dt, localTime)) {
			// En cas d'erreur, on initialise à zéro pour éviter un comportement indéfini
			dt = {};
		}
	}

public:
	// Constructeur par défaut : récupère la date/heure actuelle (locale par défaut)
	DateTime() noexcept {
		SDL_GetCurrentTime(&time);
		UpdateFromTime(true);
	}

	// Constructeur depuis un SDL_Time spécifique (choix du mode local/UTC)
	explicit DateTime(Time t, bool localTime = true) noexcept : time(t) { UpdateFromTime(localTime); }

	[[nodiscard]] int Year() const noexcept { return dt.year; }
	[[nodiscard]] int Month() const noexcept { return dt.month; }
	[[nodiscard]] int Day() const noexcept { return dt.day; }
	[[nodiscard]] int Hour() const noexcept { return dt.hour; }
	[[nodiscard]] int Minute() const noexcept { return dt.minute; }
	[[nodiscard]] int Second() const noexcept { return dt.second; }
	[[nodiscard]] int Millisecond() const noexcept { return dt.nanosecond / 1'000'000; }
	[[nodiscard]] int Nanosecond() const noexcept { return dt.nanosecond; }
	[[nodiscard]] int DayOfWeek() const noexcept { return dt.day_of_week; }
	[[nodiscard]] int UtcOffset() const noexcept { return dt.utc_offset; }
	[[nodiscard]] int DayOfYear() const noexcept { return GetDayOfYear(dt.year, dt.month, dt.day); }
	[[nodiscard]] int DaysInMonth() const noexcept { return GetDaysInMonth(dt.year, dt.month); }

	// Accès aux données brutes si besoin
	[[nodiscard]] SDL_Time GetRawTime() const noexcept { return time; }
	[[nodiscard]] const SDL_DateTime &GetRawDateTime() const noexcept { return dt; }
};

// ============================================================================
// Timer asynchrone (SDL_timer.h : SDL_AddTimer / SDL_RemoveTimer)
// ============================================================================

class Timer {
	SDL_TimerID id{0};

	// Structure pour faire le pont entre le callback C de SDL et le std::function C++
	struct CallbackPayload {
		std::function<uint32_t(uint32_t interval)> func;
	};

	std::unique_ptr<CallbackPayload> payload;

	// Fonction statique trampoline pour SDL
	static Uint32 SDLCALL Trampoline(void *userdata, SDL_TimerID timerID, Uint32 interval) {
		(void)timerID;
		auto *p = static_cast<CallbackPayload *>(userdata);
		if (p && p->func) {
			// Le retour de la fonction détermine le prochain intervalle (0 pour arrêter)
			return p->func(interval);
		}
		return 0;
	}

public:
	Timer() noexcept = default;

	// Constructeur : lance le timer immédiatement
	// func : prend l'intervalle actuel en paramètre, retourne le prochain intervalle (0 pour stopper)
	Timer(uint32_t interval, std::function<uint32_t(uint32_t)> callback) {
		if (!callback)
			return;

		payload = std::make_unique<CallbackPayload>(std::move(callback));
		id = SDL_AddTimer(interval, Trampoline, payload.get());

		if (id == 0) {
			payload.reset(); // Échec de création du timer
		}
	}

	~Timer() { Cancel(); }

	// Suppression de la copie
	Timer(const Timer &) = delete;
	Timer &operator=(const Timer &) = delete;

	// Autorisation du déplacement
	Timer(Timer &&other) noexcept : id(other.id), payload(std::move(other.payload)) { other.id = 0; }

	Timer &operator=(Timer &&other) noexcept {
		if (this != &other) {
			Cancel();
			id = other.id;
			payload = std::move(other.payload);
			other.id = 0;
		}
		return *this;
	}

	void Cancel() noexcept {
		if (IsRunning()) {
			SDL_RemoveTimer(id);
			id = 0;
			// SDL_RemoveTimer garantit que le callback ne tourne plus quand il retourne,
			// il est donc sûr de détruire le payload ici.
			payload.reset();
		}
	}

	[[nodiscard]] bool IsRunning() const noexcept { return id != 0; }
};

// ============================================================================
// Stopwatch / Chronomètre (Utilitaire OO basé sur SDL_timer.h)
// ============================================================================

class Stopwatch {
	uint64_t startTicks{0};
	uint64_t pausedTicks{0};
	bool isPaused{false};

public:
	Stopwatch() noexcept { Start(); }

	void Start() noexcept {
		isPaused = false;
		startTicks = SDL_GetTicksNS();
		pausedTicks = 0;
	}

	void Pause() noexcept {
		if (!isPaused) {
			pausedTicks = SDL_GetTicksNS();
			isPaused = true;
		}
	}

	void Resume() noexcept {
		if (isPaused) {
			// On compense le temps passé en pause
			startTicks += SDL_GetTicksNS() - pausedTicks;
			isPaused = false;
		}
	}

	[[nodiscard]] uint64_t ElapsedNs() const noexcept {
		if (isPaused)
			return pausedTicks - startTicks;
		return SDL_GetTicksNS() - startTicks;
	}

	[[nodiscard]] uint64_t ElapsedMs() const noexcept { return ElapsedNs() / 1'000'000; }
	[[nodiscard]] float ElapsedSec() const noexcept { return static_cast<float>(ElapsedNs()) / 1'000'000'000.f; }
};

// ============================================================================
// FrameTimestep - Limiteur de frame basé sur le temps système en nanocesonde
// ============================================================================

/**
 * @brief Manage the Loop TimingFixed Timestep and measures real FPS over a 1-second window.
 *
 * Typical usage:
 * @code
 * SDL::FrameTimestep timestep;               // default 60 FPS target
 * while (running) {
 *   timestep.Begin();
 *   // update / render ...
 *   timestep.End();                       // sleeps if frame was too fast
 *   float delta = timestep.GetDelta();    // use for physics / animation
 *   float fps   = timestep.GetFPS();      // measured FPS (updated every second)
 * }
 * @endcode
 *
 * @sa SetTargetFPS
 * @sa GetDelta
 * @sa GetFPS
 */
class FrameTimestep {
	Uint64 m_frameStart     = 0;    ///< ns – timestamp of the current Begin()
	Uint64 m_prevBeginTime  = 0;    ///< ns – timestamp of the previous Begin()
	Uint64 m_computeTime    = 0;    ///< ns – computation time (Begin→End)
	float  m_delta          = 0.f;  ///< seconds between consecutive Begin() calls
	float  m_targetFPS      = 60.f; ///< target frames per second
	float  m_time           = 0.f;  ///< counted time in second
	float  m_fps            = 0.f;  ///< measured FPS (1-second window)
	Uint32 m_fpsFrameCount  = 0;    ///< frames counted in the current window
	Uint64 m_fpsWindowStart = 0;    ///< ns – start of the current measurement window

public:
	/**
	 * @brief Construct a FrameTimestep with an optional target FPS.
	 *
	 * @param targetFPS desired frames per second (default 60).
	 */
	explicit FrameTimestep(float targetFPS = 60.f)
		: m_targetFPS(targetFPS) {
	}

	/**
	 * @brief Mark the beginning of a frame.
	 *
	 * Call once at the top of your main loop. On the very first call GetDelta()
	 * returns `1/targetFPS` as a sensible default; the FPS measurement window is
	 * also initialised here.
	 */
	void Begin() {
		Uint64 now = GetTicksNS();

		if (m_prevBeginTime > 0) {
			m_delta = float(now - m_prevBeginTime) * 1e-9f;

			// Accumulate frames; refresh the measured FPS every second.
			++m_fpsFrameCount;
			if (now - m_fpsWindowStart >= 1'000'000'000ULL) {
				m_fps =
					float(m_fpsFrameCount) / (float(now - m_fpsWindowStart) * 1e-9f);
				m_fpsWindowStart = now;
				m_fpsFrameCount  = 0;
			}
		} else {
			// First call: initialise the measurement window.
			m_fpsWindowStart = now;
			m_delta          = 1.f / m_targetFPS;
		}

		m_time         += m_delta;
		m_prevBeginTime = now;
		m_frameStart    = now;
	}

	/**
	 * @brief Mark the end of a frame.
	 *
	 * Computes the computation time (Begin→End). If the frame finished faster
	 * than the target rate, the thread is suspended with nanosecond precision
	 * (via DelayPrecise()) for the remaining budget.
	 */
	void End() {
		Uint64 now    = GetTicksNS();
		m_computeTime = now - m_frameStart;

		Uint64 targetNS = Uint64(1'000'000'000.0 / double(m_targetFPS));
		if (m_computeTime < targetNS) {
			DelayPrecise(targetNS - m_computeTime);
		}
	}

	/**
	 * @brief Return the computation time of the last frame (Begin→End), in seconds.
	 *
	 * This excludes any sleep added by End(). Useful for profiling how long the
	 * actual work took.
	 *
	 * @returns elapsed computation time in seconds.
	 */
	float GetTimer() const { return float(m_computeTime) * 1e-9f; }

	/**
	 * @brief Return the current time elapsed from the first frame
	 * 
	 * @returns elapsed time in seconds.
	 */
	float GetTime() const { return m_time; }

	/**
	 * @brief Return the total duration of the last frame (including sleep), in seconds.
	 *
	 * This is the time between two consecutive Begin() calls and is what you
	 * should use to advance physics or animations.
	 *
	 * @returns delta time in seconds.
	 */
	float GetDelta() const { return m_delta; }

	/**
	 * @brief Set the target frames per second.
	 *
	 * @param tfps desired frame rate (default 60).
	 */
	void SetTargetFPS(float tfps = 60.f) { m_targetFPS = tfps; }

	/**
	 * @brief Return the target frames per second.
	 *
	 * @returns target FPS as set by SetTargetFPS() or the constructor.
	 */
	float GetTargetFPS() const { return m_targetFPS; }

	/**
	 * @brief Return the measured FPS averaged over the last second.
	 *
	 * The value is updated once per second inside Begin(). Returns 0 until the
	 * first full measurement window has elapsed.
	 *
	 * @returns measured FPS.
	 */
	float GetFPS() const { return m_fps; }

	/**
	 * @brief Reset all timer state.
	 *
	 * Clears all counters, timestamps, and the measured FPS. The target FPS set
	 * by SetTargetFPS() (or the constructor) is preserved.
	 */
	void Reset() {
		m_frameStart     = 0;
		m_prevBeginTime  = 0;
		m_computeTime    = 0;
		m_delta          = 0.f;
		m_fps            = 0.f;
		m_time           = 0.f;
		m_fpsFrameCount  = 0;
		m_fpsWindowStart = 0;
	}
};
} // namespace sdl3