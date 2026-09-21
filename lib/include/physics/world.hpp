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
	void Step(float dt) {
		if (dt <= 0.f)
			return;

		// 1. External forces — semi-implicit Euler updates velocity from forces
		//    BEFORE the constraint solve (and the later position integration)
		//    ever sees it.
		m_registry.Query<RigidBody>([&](ecs::Entity, RigidBody &rb) {
			if (rb.invMass > 0.f)
				rb.linearVelocity += config.gravity * dt;
		});

		m_registry.Query<SpringJoint>([&](ecs::Entity, SpringJoint &spring) {
			Option<RefMut<RigidBody>> oa = m_registry.GetComponent<RigidBody>(spring.a);
			Option<RefMut<RigidBody>> ob = m_registry.GetComponent<RigidBody>(spring.b);
			if (oa.IsSome() && ob.IsSome())
				ApplySpringForce(spring, oa.Value(), ob.Value(), dt);
		});

		// 2. Broad-phase (also refreshes each body's shape transform + AABB).
		std::vector<BroadPhasePair> pairs = BroadPhase(m_registry, jobSystem);

		// 3. Narrow-phase -> contact manifolds.
		//
		// Chaque paire est indépendante : elle LIT deux corps et écrit un
		// manifold dans SA case. La compaction finale garde l'ordre des
		// paires, donc la liste de contacts remise au solveur est la même,
		// parallèle ou non (cf. BroadPhase pour l'enjeu de cet ordre).
		//
		// `GetComponent` n'est appelé que pour LIRE : aucune mutation
		// structurelle de l'ECS n'a lieu ici, donc rien à synchroniser.
		std::vector<Manifold> perPair(pairs.size());
		std::vector<char> hasContact(pairs.size(), 0);
		auto buildManifold = [&](size_t index) {
			const BroadPhasePair &pair = pairs[index];
			Option<RefMut<RigidBody>> oa = m_registry.GetComponent<RigidBody>(pair.a);
			Option<RefMut<RigidBody>> ob = m_registry.GetComponent<RigidBody>(pair.b);
			if (!oa.IsSome() || !ob.IsSome())
				return;
			RigidBody &rbA = oa.Value();
			RigidBody &rbB = ob.Value();
			if (GenerateManifold(pair.a, rbA.shape, pair.b, rbB.shape, perPair[index]))
				hasContact[index] = 1;
		};

		constexpr size_t MINIMUM_PAIRS_PER_BATCH = 64;
		if (jobSystem)
			jobSystem->ParallelFor(pairs.size(), MINIMUM_PAIRS_PER_BATCH, buildManifold);
		else
			for (size_t index = 0; index < pairs.size(); ++index)
				buildManifold(index);

		std::vector<Manifold> manifolds;
		manifolds.reserve(pairs.size());
		for (size_t index = 0; index < pairs.size(); ++index)
			if (hasContact[index])
				manifolds.push_back(perPair[index]);

		// 4. Build contact constraints (this is also where warm-start impulses
		//    from last step get applied — see BuildContactConstraints()).
		std::vector<ContactConstraint> constraints;
		BuildContactConstraints(m_registry, manifolds, m_warmStartCache, constraints, config.solver, dt);

		// 5. Sequential-impulse solve — contacts, then joints, fixed iteration counts.
		for (int it = 0; it < config.solverIterations; ++it)
			SolveContactConstraints(constraints);

		for (int it = 0; it < config.jointIterations; ++it) {
			m_registry.Query<HingeJoint>([&](ecs::Entity, HingeJoint &j) {
				Option<RefMut<RigidBody>> oa = m_registry.GetComponent<RigidBody>(j.a);
				Option<RefMut<RigidBody>> ob = m_registry.GetComponent<RigidBody>(j.b);
				if (oa.IsSome() && ob.IsSome())
					SolveHingeJoint(j, oa.Value(), ob.Value(), dt);
			});
			m_registry.Query<FixedJoint>([&](ecs::Entity, FixedJoint &j) {
				Option<RefMut<RigidBody>> oa = m_registry.GetComponent<RigidBody>(j.a);
				Option<RefMut<RigidBody>> ob = m_registry.GetComponent<RigidBody>(j.b);
				if (oa.IsSome() && ob.IsSome())
					SolveFixedJoint(j, oa.Value(), ob.Value(), dt);
			});
		}

		// 6. Persist accumulated impulses for next frame's warm start.
		m_warmStartCache = BuildWarmStartCache(constraints);

		// 7. Integrate — semi-implicit Euler: position/orientation are advanced
		//    using the velocity the solve loop just finished updating.
		m_registry.Query<RigidBody>([&](ecs::Entity, RigidBody &rb) {
			if (rb.invMass <= 0.f)
				return;

			math::FVector3 prevPosition = rb.position;
			rb.position += rb.linearVelocity * dt;

			math::FQuaternion spin =
				math::FQuaternion{rb.angularVelocity.x, rb.angularVelocity.y, rb.angularVelocity.z, 0.f} *
				rb.orientation;
			rb.orientation = math::FQuaternion{
				rb.orientation.x + 0.5f * dt * spin.x,
				rb.orientation.y + 0.5f * dt * spin.y,
				rb.orientation.z + 0.5f * dt * spin.z,
				rb.orientation.w + 0.5f * dt * spin.w,
			}.Normalize();

			// 8. Portal teleport (M30, optional — see portalPlanes' own doc
			//    comment above). Must run AFTER position/orientation have
			//    been integrated this step: TeleportBodiesThroughPortals
			//    needs the actual prevPosition -> rb.position motion segment.
			TeleportBodiesThroughPortals(rb, prevPosition, portalPlanes);
		});
	}
};

} // namespace physics
