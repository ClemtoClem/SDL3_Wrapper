#pragma once
/**
 * Schedule — ordonnancement des SYSTÈMES d'un monde ECS.
 *
 * Un système est une fonction nommée rattachée à une ÉTAPE ; les étapes
 * s'exécutent dans l'ordre STARTUP (une fois), PRE_UPDATE, FIXED_UPDATE
 * (à pas fixe, rattrapé au plus `maxFixedSteps` fois par image : physique
 * déterministe quelle que soit la cadence), UPDATE, POST_UPDATE. Dans une
 * étape, les systèmes tournent par `order` croissant puis par ordre d'ajout.
 *
 * Après chaque étape, le CommandBuffer partagé est appliqué (les systèmes
 * y différent spawn/despawn/ajouts pendant leurs requêtes) ; en fin
 * d'image, les files d'événements tournent (cf. events.hpp).
 *
 * Chaque système peut être désactivé, retiré, conditionné (`runIf`), et
 * son temps d'exécution est mesuré (profilage : `Stats()`).
 */
#include <chrono>
#include <cstdint>
#include <functional>
#include <vector>

#include "ecs.hpp"
#include "events.hpp"

namespace ecs {

enum class Stage : uint8_t { STARTUP, PRE_UPDATE, FIXED_UPDATE, UPDATE, POST_UPDATE };

struct SystemContext {
	ArchetypeRegistry &world;
	CommandBuffer &commands;
	float dt;         ///< pas de l'étape (fixe en FIXED_UPDATE)
	uint64_t frame;   ///< numéro d'image
};

using SystemFn = std::function<void(SystemContext &)>;

struct SystemStats {
	String name;
	Stage stage;
	bool enabled;
	double lastMicros;  ///< dernière exécution
	double totalMicros; ///< cumul
	uint64_t runs;
};

class Schedule {
public:
	float fixedDt = 1.f / 60.f;
	int maxFixedSteps = 5;

	/// Ajoute (ou remplace, même nom) un système.
	void Add(Stage stage, String name, SystemFn fn, int order = 0, std::function<bool(const ArchetypeRegistry &)> runIf = {}) {
		Remove(name);
		m_systems.push_back(System{stage, std::move(name), std::move(fn), order, m_nextSerial++, std::move(runIf)});
		m_sorted = false;
	}
	bool Remove(const String &name) {
		const size_t before = m_systems.size();
		std::erase_if(m_systems, [&](const System &s) { return s.name == name; });
		return m_systems.size() != before;
	}
	bool SetEnabled(const String &name, bool enabled) {
		for (System &s : m_systems)
			if (s.name == name) {
				s.enabled = enabled;
				return true;
			}
		return false;
	}
	[[nodiscard]] bool Has(const String &name) const {
		return std::any_of(m_systems.begin(), m_systems.end(), [&](const System &s) { return s.name == name; });
	}

	/// Une image : PRE_UPDATE, FIXED_UPDATE × n, UPDATE, POST_UPDATE, puis
	/// rotation des événements. STARTUP tourne à la première image.
	void Update(ArchetypeRegistry &world, float dt) {
		Sort();
		if (!m_started) {
			m_started = true;
			RunStage(world, Stage::STARTUP, 0.f);
		}
		RunStage(world, Stage::PRE_UPDATE, dt);
		m_accumulator += std::min(dt, fixedDt * float(maxFixedSteps));
		int steps = 0;
		while (m_accumulator >= fixedDt && steps < maxFixedSteps) {
			RunStage(world, Stage::FIXED_UPDATE, fixedDt);
			m_accumulator -= fixedDt;
			++steps;
		}
		RunStage(world, Stage::UPDATE, dt);
		RunStage(world, Stage::POST_UPDATE, dt);
		UpdateAllEvents(world);
		++m_frame;
	}

	/// Part de pas fixe en attente (interpolation du rendu), dans [0, 1[.
	[[nodiscard]] float FixedAlpha() const noexcept { return m_accumulator / fixedDt; }
	[[nodiscard]] uint64_t Frame() const noexcept { return m_frame; }
	[[nodiscard]] CommandBuffer &Commands() noexcept { return m_commands; }

	[[nodiscard]] std::vector<SystemStats> Stats() const {
		std::vector<SystemStats> out;
		for (const System &s : m_systems)
			out.push_back({s.name, s.stage, s.enabled, s.lastMicros, s.totalMicros, s.runs});
		return out;
	}

private:
	struct System {
		Stage stage;
		String name;
		SystemFn fn;
		int order;
		uint64_t serial;
		std::function<bool(const ArchetypeRegistry &)> runIf;
		bool enabled = true;
		double lastMicros = 0.0, totalMicros = 0.0;
		uint64_t runs = 0;
	};

	void Sort() {
		if (m_sorted)
			return;
		std::stable_sort(m_systems.begin(), m_systems.end(), [](const System &a, const System &b) {
			if (a.stage != b.stage)
				return a.stage < b.stage;
			if (a.order != b.order)
				return a.order < b.order;
			return a.serial < b.serial;
		});
		m_sorted = true;
	}

	void RunStage(ArchetypeRegistry &world, Stage stage, float dt) {
		SystemContext ctx{world, m_commands, dt, m_frame};
		for (size_t i = 0; i < m_systems.size(); ++i) {
			System &s = m_systems[i];
			if (s.stage != stage || !s.enabled || (s.runIf && !s.runIf(world)))
				continue;
			const auto start = std::chrono::steady_clock::now();
			s.fn(ctx);
			s.lastMicros = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
			s.totalMicros += s.lastMicros;
			++s.runs;
		}
		m_commands.Flush(world);
	}

	std::vector<System> m_systems;
	CommandBuffer m_commands;
	uint64_t m_nextSerial = 0, m_frame = 0;
	float m_accumulator = 0.f;
	bool m_sorted = false, m_started = false;
};

} // namespace ecs
