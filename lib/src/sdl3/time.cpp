// Définitions de sdl3/time.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include "sdl3/sdl3.hpp"
#include "sdl3/time.hpp"

namespace sdl3 {

std::chrono::nanoseconds GetTicks() {
	return std::chrono::nanoseconds(SDL_GetTicksNS());
}

Uint64 GetPerformanceFrequency() {
	return SDL_GetPerformanceFrequency();
}

void Delay(std::chrono::nanoseconds duration) {
	SDL_DelayNS(duration.count());
}

void DelayPrecise(std::chrono::nanoseconds duration) {
	SDL_DelayPrecise(duration.count());
}

int GetDayOfWeek(int year, int month, int day) noexcept {
	return SDL_GetDayOfWeek(year, month, day);
}

// ── DateTime ─────────────────────────────────────────────────────────────────

void DateTime::UpdateFromTime(bool localTime) noexcept {
	if (!SDL_TimeToDateTime(time, &dt, localTime)) {
		// En cas d'erreur, on initialise à zéro pour éviter un comportement indéfini
		dt = {};
	}
}

DateTime::DateTime() noexcept {
	SDL_GetCurrentTime(&time);
	UpdateFromTime(true);
}

// ── Timer ────────────────────────────────────────────────────────────────────

Uint32 SDLCALL Timer::Trampoline(void *userdata, SDL_TimerID timerID, Uint32 interval) {
	(void)timerID;
	auto *p = static_cast<CallbackPayload *>(userdata);
	if (p && p->func) {
		// Le retour de la fonction détermine le prochain intervalle (0 pour arrêter)
		return p->func(interval);
	}
	return 0;
}

Timer::Timer(uint32_t interval, std::function<uint32_t(uint32_t)> callback) {
	if (!callback)
		return;

	payload = std::make_unique<CallbackPayload>(std::move(callback));
	id = SDL_AddTimer(interval, Trampoline, payload.get());

	if (id == 0) {
		payload.reset(); // Échec de création du timer
	}
}

void Timer::Cancel() noexcept {
	if (IsRunning()) {
		SDL_RemoveTimer(id);
		id = 0;
		// SDL_RemoveTimer garantit que le callback ne tourne plus quand il retourne,
		// il est donc sûr de détruire le payload ici.
		payload.reset();
	}
}

// ── Stopwatch ────────────────────────────────────────────────────────────────

void Stopwatch::Start() noexcept {
	isPaused = false;
	startTicks = SDL_GetTicksNS();
	pausedTicks = 0;
}

void Stopwatch::Pause() noexcept {
	if (!isPaused) {
		pausedTicks = SDL_GetTicksNS();
		isPaused = true;
	}
}

void Stopwatch::Resume() noexcept {
	if (isPaused) {
		// On compense le temps passé en pause
		startTicks += SDL_GetTicksNS() - pausedTicks;
		isPaused = false;
	}
}

uint64_t Stopwatch::ElapsedNs() const noexcept {
	if (isPaused)
		return pausedTicks - startTicks;
	return SDL_GetTicksNS() - startTicks;
}

// ── FrameTimestep ────────────────────────────────────────────────────────────

FrameTimestep::FrameTimestep(float targetFPS)
	: m_targetFPS(targetFPS) {
}

void FrameTimestep::Begin() {
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

void FrameTimestep::End() {
	Uint64 now    = GetTicksNS();
	m_computeTime = now - m_frameStart;

	Uint64 targetNS = Uint64(1'000'000'000.0 / double(m_targetFPS));
	if (m_computeTime < targetNS) {
		DelayPrecise(targetNS - m_computeTime);
	}
}

void FrameTimestep::Reset() {
	m_frameStart     = 0;
	m_prevBeginTime  = 0;
	m_computeTime    = 0;
	m_delta          = 0.f;
	m_fps            = 0.f;
	m_time           = 0.f;
	m_fpsFrameCount  = 0;
	m_fpsWindowStart = 0;
}

} // namespace sdl3
