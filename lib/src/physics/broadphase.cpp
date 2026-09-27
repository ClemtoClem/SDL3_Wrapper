// Définitions de physics/broadphase.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "physics/broadphase.hpp"

namespace physics {

std::vector<BroadPhasePair> BroadPhase(ecs::ArchetypeRegistry &registry, jobs::JobSystem *jobSystem) {
	std::vector<ecs::Entity> entities;
	std::vector<math::FAABB> aabbs;
	std::vector<bool> isStatic;

	registry.Query<RigidBody>([&](ecs::Entity e, RigidBody &rb) {
		SyncShapeTransform(rb.shape, rb.position, rb.orientation);
		rb.worldAABB = ShapeWorldAABB(rb.shape);
		entities.push_back(e);
		aabbs.push_back(rb.worldAABB);
		isStatic.push_back(rb.invMass <= 0.f);
	});

	const size_t count = entities.size();
	if (count < 2)
		return {};

	// Chaque LIGNE `i` du triangle supérieur est indépendante des autres :
	// elle ne lit que `aabbs`/`isStatic` et n'écrit que dans SA propre case de
	// `rows`. C'est ce qui autorise la répartition sur le vivier.
	//
	// La concaténation finale se fait dans l'ordre croissant des lignes, donc
	// la liste de paires produite est RIGOUREUSEMENT LA MÊME qu'en série. Ça
	// n'est pas cosmétique : le solveur à impulsions séquentielles traite les
	// contacts dans l'ordre, un ordre différent donnerait un résultat
	// numériquement différent, et la démo promet des exécutions rejouables.
	std::vector<std::vector<BroadPhasePair>> rows(count);
	auto buildRow = [&](size_t i) {
		std::vector<BroadPhasePair> &row = rows[i];
		for (size_t j = i + 1; j < count; ++j) {
			if (isStatic[i] && isStatic[j])
				continue; // two immovable bodies never need solving
			if (!aabbs[i].Intersects(aabbs[j]))
				continue;

			ecs::Entity ea = entities[i], eb = entities[j];
			if (eb.id < ea.id)
				std::swap(ea, eb);
			row.push_back({ea, eb});
		}
	};

	// Seuil : en dessous, la synchronisation d'un lot coûte plus que le
	// travail lui-même (quelques dizaines de corps = quelques microsecondes).
	constexpr size_t MINIMUM_ROWS_PER_BATCH = 32;
	if (jobSystem)
		jobSystem->ParallelFor(count, MINIMUM_ROWS_PER_BATCH, buildRow);
	else
		for (size_t i = 0; i < count; ++i)
			buildRow(i);

	size_t total = 0;
	for (const auto &row : rows)
		total += row.size();
	std::vector<BroadPhasePair> pairs;
	pairs.reserve(total);
	for (const auto &row : rows)
		pairs.insert(pairs.end(), row.begin(), row.end());
	return pairs;
}

} // namespace physics
