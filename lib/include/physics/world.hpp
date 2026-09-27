#pragma once
/**
 * @file world.hpp
 * @brief physics::World — owns the persistent solver state and drives one
 * broad -> narrow -> solve -> integrate step per `Step(dt)` call.
 */
#include "../core/core.hpp"
#include "../ecs/ecs.hpp"
#include "broadphase.hpp"
#include "joints.hpp"
#include "narrowphase.hpp"
#include "portal_teleport.hpp"
#include "rigidbody.hpp"
#include "solver.hpp"
#include <vector>

namespace physics {

struct WorldConfig {
	math::FVector3 gravity{0.f, -9.81f, 0.f};
	int solverIterations = 8;
	int jointIterations = 4;
	SolverConfig solver;
};

/**
 * Holds a NON-OWNING `ecs::ArchetypeRegistry&` (matching `ui::Ui`'s own
 * non-owning-registry pattern — the caller keeps ownership and can freely mix
 * in its own application components) plus the one piece of state that must
 * persist across `Step()` calls: the warm-starting impulse cache.
 */
class World {
	ecs::ArchetypeRegistry &m_registry;
	WarmStartCache m_warmStartCache;

public:
	WorldConfig config;

	/// Ordonnanceur de tâches FACULTATIF, non possédé. Sans lui, tout se fait
	/// sur le fil appelant, exactement comme avant — c'est le comportement par
	/// défaut, et il reste le bon tant que la scène compte quelques dizaines
	/// de corps. Avec lui, la phase large et la génération de manifolds sont
	/// réparties, ce qui compte dès quelques centaines de corps (la phase
	/// large est en O(n²)).
	///
	/// Le résultat est IDENTIQUE dans les deux cas, ordre des paires et des
	/// contacts compris (cf. BroadPhase) : le parallélisme ne change pas ce
	/// que la simulation produit, seulement le temps qu'elle met.
	jobs::JobSystem *jobSystem = nullptr;

	/// Optional portal-teleport support (M30, Phase 9 — see portal_teleport.hpp).
	/// Empty by default: genuinely inert (one skipped-loop check per dynamic
	/// body per step, zero behaviour change) unless the caller populates it —
	/// typically once per frame, from render3d::Portal's own world-space
	/// plane/delta data, in lockstep with Portal::UpdateDelta().
	std::vector<PortalPlane> portalPlanes;

	explicit World(ecs::ArchetypeRegistry &registry) noexcept : m_registry(registry) {}

	[[nodiscard]] ecs::ArchetypeRegistry &Registry() noexcept { return m_registry; }
	[[nodiscard]] const WarmStartCache &GetWarmStartCache() const noexcept { return m_warmStartCache; }

	/// One fixed-size step: broad-phase -> narrow-phase -> solve (with warm
	/// starting) -> integrate (semi-implicit Euler). A single `dt`-sized step
	/// per call, no internal accumulator/substep loop — simpler, and the
	/// module's tests pass reliably with the caller choosing a stable dt.
	void Step(float dt);
};

} // namespace physics
