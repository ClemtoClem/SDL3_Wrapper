// Définitions de physics/world.hpp
#include "physics/world.hpp"

namespace physics {

// ── World ────────────────────────────────────────────────────────────────────

void World::Step(float dt) {
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

} // namespace physics
