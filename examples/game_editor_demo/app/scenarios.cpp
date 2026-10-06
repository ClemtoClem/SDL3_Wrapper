// Définitions de scenarios.hpp
#include "scenarios.hpp"

namespace game_editor {

std::vector<Scenario> BuiltinScenarios() {
	return {
		{"tour", "Visite guidée complète : panneaux, thèmes, édition, mode Jeu, 3 scènes", 820, SCENARIO_TOUR},
		{"circuit", "Essai de jouabilité du circuit de voiture (pilote automatique)", 1200, SCENARIO_CIRCUIT},
		{"physics", "Banc d'essai du solveur physique sous charge croissante", 900, SCENARIO_PHYSICS},
		{"themes", "Galerie des trois thèmes d'interface", 160, SCENARIO_THEMES},
		{"stress", "Mesure de performance : vagues d'objets et images par seconde", 600, SCENARIO_STRESS},
		{"edition", "Clic dans le viewport, manipulateur (déplacer/tourner/redimensionner), magnétisme, annulation", 140, SCENARIO_EDITION},
		{"camera", "Navigation dans la vue : orbite, plongée, molette, panoramique, vol libre", 110, SCENARIO_CAMERA},
		{"node_hierarchy", "Hiérarchie de nœuds : composition, reparentage, duplication, annulation, instances, aller-retour fichier", 150, SCENARIO_NODE_HIERARCHY},
		{"smoke", "Vérification minimale de chaque scène (assertions, pour l'intégration continue)", 150, SCENARIO_SMOKE},
		{"interface", "Visite de l'interface dans le donjon : arbre, menu contextuel, lumière, ressources, JSON, script, mode Jeu", 215, SCENARIO_INTERFACE},
	};
}

Option<Scenario> FindScenario(const String &name) {
	for (const Scenario &scenario : BuiltinScenarios())
		if (name == scenario.name)
			return Some(scenario);
	return NONE;
}

} // namespace game_editor
