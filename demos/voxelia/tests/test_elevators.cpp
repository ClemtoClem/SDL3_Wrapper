// Tests — mod « elevators » et niveaux des backrooms, partie complète sans
// fenêtre (scripts réels du contenu livré) : trajet classique, appels
// successifs, plusieurs passagers, redémarrage pendant un trajet, mod seul
// (sans les backrooms), étages / escaliers / gaine d'un niveau, changement de
// niveau, retour à la surface, cas limites. Écrit sous build/tests/tmp/.
#include "test.hpp"

#include <cmath>
#include <filesystem>

#include <voxel/game.hpp>
#include <voxel/scripting.hpp>

using namespace voxel;
namespace fs = std::filesystem;

namespace {

GameOptions Options(const String &saves, std::vector<String> mods = {String("demos/voxelia/mods")}) {
	GameOptions o;
	o.modDirectories = std::move(mods);
	o.savesDirectory = saves;
	o.viewRadius = 2;
	o.verticalRadius = 2;
	o.chunkBudget = 400;
	return o;
}

/// Réglage d'étages du mod backrooms (« number_of_floors_per_level », « floors_level_4 »…).
GameOptions WithFloors(GameOptions o, const char *setting, int floors) {
	if (!o.modSettings)
		o.modSettings = dataio::Node::MakeObject();
	dataio::NodePtr backrooms = o.modSettings->Get(String("backrooms"));
	if (!backrooms) {
		backrooms = dataio::Node::MakeObject();
		o.modSettings->Set("backrooms", backrooms);
	}
	backrooms->Set(setting, dataio::Node::MakeInt(floors));
	return o;
}

String Fresh(const char *name) {
	const fs::path root = fs::path("build/tests/tmp/voxelia/elevators") / name;
	fs::remove_all(root);
	fs::create_directories(root);
	return String(root.string().c_str());
}

/// Seuls « core » et « elevators » (liens vers les mods livrés) : le mod seul,
/// sans destinations extérieures, et un chargement bien plus court que le
/// contenu complet (tests sous ThreadSanitizer). Sauvegardes dans root/saves.
std::vector<String> ElevatorMods(const String &root) {
	const fs::path mods = fs::path(root.CStr()) / "mods";
	fs::create_directories(mods);
	for (const char *m : {"core", "elevators"})
		if (!fs::exists(mods / m))
			fs::create_directory_symlink(fs::absolute(fs::path("demos/voxelia/mods") / m), mods / m);
	return {String(mods.string().c_str())};
}

GameOptions ElevatorOptions(const String &root) { return Options(root + "/saves", ElevatorMods(root)); }

void Run(Game &g, float seconds) {
	for (float t = 0.f; t < seconds; t += 1.f / 30.f)
		g.Tick(1.f / 30.f);
}

/// Valeur (affichée) d'une expression de script ; « let x = … ; expr » :
/// les déclarations d'abord, puis la valeur de la dernière expression.
String Eval(Game &g, const char *expression) {
	std::string src(expression);
	std::string code = "return " + src;
	if (src.rfind("let ", 0) == 0) {
		// « let a = … let b = … expr » : la dernière expression après le dernier « ) world. » / « ) » de déclaration.
		const size_t cut = src.rfind(" world.");
		if (cut != std::string::npos)
			code = src.substr(0, cut) + "\nreturn" + src.substr(cut);
	}
	Result<String, String> r = g.Scripts().RunSource(String(code.c_str()));
	if (r.IsError()) {
		std::printf("script : %s → %s\n", expression, r.Error().CStr());
		return String("<erreur>");
	}
	return r.Value();
}

float Num(Game &g, const char *expression) { return float(std::atof(Eval(g, expression).CStr())); }

/// Fait tourner le jeu jusqu'à ce que l'expression vaille `expected` (au plus `seconds`).
bool Until(Game &g, const char *expression, const char *expected, float seconds) {
	for (float t = 0.f; t < seconds; t += 0.1f) {
		if (Eval(g, expression) == expected)
			return true;
		Run(g, 0.1f);
	}
	std::printf("attendu %s == %s, obtenu %s\n", expression, expected, Eval(g, expression).CStr());
	return false;
}

/// Ascenseurs plus vifs (6 blocs/s, accélération 4, portes 1 s ouvertes et
/// 0,2 s de mouvement) : les tests vérifient l'enchaînement des états, pas les
/// durées réglées — et restent sous le délai des tests sous ThreadSanitizer.
void Brisk(Game &g) {
	(void)g.Scripts().RunSource(
		String("EL.settings.speed = 6.0 EL.settings.accel = 4.0 EL.settings.door_time = 1.0 EL.settings.door_speed = 0.2"));
}

/// Premier ascenseur de la dimension courante (expression de script).
constexpr const char *E = "EL.records[keys(EL.records)[0]]";

void Exec(Game &g, ecs::Entity p, const char *line) {
	Result<String, String> r = g.Execute(String(line), p);
	if (r.IsError())
		std::printf("commande %s → %s\n", line, r.Error().CStr());
}

/// Ascenseur d'exemple (3 étages) à côté du joueur, joueur placé dans la cabine.
void DemoWithRider(Game &g, ecs::Entity p, const char *style = "modern") {
	Exec(g, p, (std::string("/ascenseur demo ") + style + " 3").c_str());
	REQUIRE_EQ(Eval(g, "len(keys(EL.records))"), String("1"));
	(void)g.Scripts().RunSource(String("let e = ") + E +
								" let c = el_cabin_min(e, e.floor) entity.set_pos(entity.players()[0], c.x + 2.5, c.y + 1, c.z + 2.5)");
	Run(g, 0.5f);
}

} // namespace

// A — ascenseur classique : appel, trajet, portes, afficheurs, sans commande d'administration.
TEST(Elevators_A_ClassicTripCarriesTheRider) {
	Game g;
	REQUIRE(g.LoadMods(ElevatorOptions(Fresh("a"))).errors.empty());
	REQUIRE(g.OpenWorld("monde", Some(uint32_t(11))).IsOk());
	Brisk(g);
	const ecs::Entity p = g.JoinPlayer("Alix");
	Run(g, 1.f);
	DemoWithRider(g, p);
	CHECK_EQ(Eval(g, (std::string(E) + ".state").c_str()), String("idle"));
	CHECK_EQ(Eval(g, (std::string(E) + ".floor").c_str()), String("0"));
	const float floor2 = Num(g, (std::string(E) + ".floors[2].y").c_str());
	// Demande depuis la cabine (panneau) : l'étage 2.
	(void)g.Scripts().RunSource(String("el_request(") + E + ", 2)");
	REQUIRE(Until(g, (std::string(E) + ".state").c_str(), "moving_up", 4.f));
	// Pendant le trajet : la cabine est une entité, le passager la suit sans tomber.
	Run(g, 1.5f);
	const float midway = Num(g, "entity.pos(entity.players()[0]).y");
	CHECK(midway > Num(g, (std::string(E) + ".floors[0].y").c_str()) + 1.2f);
	CHECK(Eval(g, (std::string("el_doorway_blocked(") + E + ")").c_str()) == "false");
	REQUIRE(Until(g, (std::string(E) + ".state").c_str(), "doors_open", 15.f));
	CHECK_EQ(Eval(g, (std::string(E) + ".floor").c_str()), String("2"));
	CHECK_NEAR(Num(g, "entity.pos(entity.players()[0]).y"), floor2 + 1.f, 0.02f); // sur le plancher, pas à travers
	// Portes ouvertes à l'étage 2 seulement ; afficheur du palier 0 : « 2 ».
	CHECK(Eval(g, (std::string("let l = el_landing(") + E + ", 2) world.get(l.door.x, l.door.y, l.door.z)").c_str()).Contains("_open"));
	CHECK(!Eval(g, (std::string("let l = el_landing(") + E + ", 0) world.get(l.door.x, l.door.y, l.door.z)").c_str()).Contains("_open"));
	CHECK(Eval(g, (std::string("let l = el_landing(") + E + ", 0) world.get(l.display.x, l.display.y, l.display.z)").c_str())
			  .EndsWith("_display_2"));
	// Les portes se referment d'elles-mêmes ; la cabine attend, posée en blocs.
	REQUIRE(Until(g, (std::string(E) + ".state").c_str(), "idle", 10.f));
	CHECK(Eval(g, (std::string("let l = el_landing(") + E + ", 2) world.get(l.door.x, l.door.y, l.door.z)").c_str())
			  .EndsWith("_landing_door"));
	CHECK(Eval(g, (std::string("let c = el_cabin_min(") + E + ", 2) world.get(c.x, c.y, c.z)").c_str()).EndsWith("_cabin_floor"));
	g.CloseWorld();
}

// B — appels successifs depuis plusieurs étages : tous servis, sans état contradictoire.
TEST(Elevators_B_SuccessiveCallsAreAllServed) {
	Game g;
	REQUIRE(g.LoadMods(ElevatorOptions(Fresh("b"))).errors.empty());
	REQUIRE(g.OpenWorld("monde", Some(uint32_t(12))).IsOk());
	Brisk(g);
	const ecs::Entity p = g.JoinPlayer("Alix");
	Run(g, 1.f);
	DemoWithRider(g, p, "industrial");
	(void)g.Scripts().RunSource(String("let e = ") + E + " el_request(e, 2) el_request(e, 1) el_request(e, 2)");
	CHECK_EQ(Eval(g, (std::string("len(") + E + ".queue)").c_str()), String("2")); // pas de doublon
	// En montant, l'étage 1 (dans le sens du trajet) passe avant le 2.
	REQUIRE(Until(g, (std::string(E) + ".state + \":\" + str(" + E + ".floor)").c_str(), "doors_open:1", 20.f));
	REQUIRE(Until(g, (std::string(E) + ".state + \":\" + str(" + E + ".floor)").c_str(), "doors_open:2", 25.f));
	// Appel du rez-de-chaussée pendant que les portes sont ouvertes : servi ensuite.
	(void)g.Scripts().RunSource(String("el_request(") + E + ", 0)");
	REQUIRE(Until(g, (std::string(E) + ".state + \":\" + str(" + E + ".floor)").c_str(), "doors_open:0", 25.f));
	REQUIRE(Until(g, (std::string(E) + ".state").c_str(), "idle", 10.f));
	CHECK_EQ(Eval(g, (std::string("len(") + E + ".queue)").c_str()), String("0"));
	// Toujours un seul assemblage de cabine dans la gaine, aucune entité oubliée.
	CHECK_EQ(Eval(g, "len(keys(EL.active))"), String("0"));
	g.CloseWorld();
}

// F — plusieurs passagers : tous emportés ensemble, posés au même étage.
TEST(Elevators_F_PassengersRideTogether) {
	Game g;
	REQUIRE(g.LoadMods(ElevatorOptions(Fresh("f"))).errors.empty());
	REQUIRE(g.OpenWorld("monde", Some(uint32_t(13))).IsOk());
	Brisk(g);
	const ecs::Entity p = g.JoinPlayer("Alix");
	const ecs::Entity q = g.JoinPlayer("Bao");
	Run(g, 1.f);
	DemoWithRider(g, p);
	(void)g.Scripts().RunSource(String("let e = ") + E +
								" let c = el_cabin_min(e, e.floor) entity.set_pos(entity.players()[1], c.x + 1.5, c.y + 1, c.z + 3.5)");
	Run(g, 0.3f);
	const float floor1 = Num(g, (std::string(E) + ".floors[1].y").c_str());
	(void)g.Scripts().RunSource(String("el_request(") + E + ", 1)");
	REQUIRE(Until(g, (std::string(E) + ".state").c_str(), "doors_open", 20.f));
	CHECK_NEAR(Num(g, "entity.pos(entity.players()[0]).y"), floor1 + 1.f, 0.02f);
	CHECK_NEAR(Num(g, "entity.pos(entity.players()[1]).y"), floor1 + 1.f, 0.02f);
	(void)q;
	g.CloseWorld();
}

// G — arrêt du jeu pendant un trajet : au retour, la cabine est posée à l'étage
// visé, l'ascenseur est sûr (au repos, portes fermées) et sa configuration gardée.
TEST(Elevators_G_RestartDuringATripRestoresASafeState) {
	const String saves = Fresh("g");
	{
		Game g;
		REQUIRE(g.LoadMods(ElevatorOptions(saves)).errors.empty());
		REQUIRE(g.OpenWorld("monde", Some(uint32_t(14))).IsOk());
		Brisk(g);
		const ecs::Entity p = g.JoinPlayer("Alix");
		Run(g, 1.f);
		DemoWithRider(g, p);
		(void)g.Scripts().RunSource(String("let e = ") + E + " e.floors[1].name = \"Atelier\" el_save()");
		(void)g.Scripts().RunSource(String("el_request(") + E + ", 2)");
		REQUIRE(Until(g, (std::string(E) + ".state").c_str(), "moving_up", 4.f));
		Run(g, 0.5f);
		g.CloseWorld(); // en pleine montée
	}
	Game g;
	REQUIRE(g.LoadMods(ElevatorOptions(saves)).errors.empty());
	REQUIRE(g.OpenWorld("monde").IsOk());
	Brisk(g);
	(void)g.JoinPlayer("Alix");
	Run(g, 1.f);
	CHECK_EQ(Eval(g, (std::string(E) + ".state").c_str()), String("idle"));
	CHECK_EQ(Eval(g, (std::string(E) + ".floor").c_str()), String("2"));
	CHECK_EQ(Eval(g, (std::string(E) + ".floors[1].name").c_str()), String("Atelier"));
	CHECK(Eval(g, (std::string("let c = el_cabin_min(") + E + ", 2) world.get(c.x, c.y, c.z)").c_str()).EndsWith("_cabin_floor"));
	CHECK(Eval(g, (std::string("let c = el_cabin_min(") + E + ", 0) world.get(c.x, c.y + 1, c.z + 1)").c_str()) == "core:air");
	// Et il repart normalement.
	(void)g.Scripts().RunSource(String("el_request(") + E + ", 0)");
	CHECK(Until(g, (std::string(E) + ".state + \":\" + str(" + E + ".floor)").c_str(), "doors_open:0", 25.f));
	g.CloseWorld();
}

// H — le mod « elevators » sans les backrooms.
TEST(Elevators_H_WorkWithoutTheBackroomsMod) {
	Game g;
	const ModReport report = g.LoadMods(ElevatorOptions(Fresh("h")));
	for (const String &e : report.errors)
		std::printf("ERREUR %s\n", e.CStr());
	REQUIRE(report.errors.empty());
	REQUIRE_EQ(report.mods.size(), size_t(2));
	REQUIRE(g.OpenWorld("monde", Some(uint32_t(15))).IsOk());
	Brisk(g);
	const ecs::Entity p = g.JoinPlayer("Alix");
	Run(g, 1.f);
	DemoWithRider(g, p, "backrooms");
	CHECK_EQ(Eval(g, (std::string("len(el_external_for(") + E + "))").c_str()), String("0")); // aucune destination déclarée
	(void)g.Scripts().RunSource(String("el_request(") + E + ", 1)");
	CHECK(Until(g, (std::string(E) + ".state + \":\" + str(" + E + ".floor)").c_str(), "doors_open:1", 20.f));
	g.CloseWorld();
}

// C — un niveau des backrooms : plusieurs étages, escaliers, gaine d'ascenseur
// traversant tous les étages ; déplacement interne sans changer de niveau.
TEST(Backrooms_C_LevelFloorsStairsAndElevator) {
	Game g;
	REQUIRE(g.LoadMods(WithFloors(Options(Fresh("c")), "floors_level_4", 3)).errors.empty());
	REQUIRE(g.OpenWorld("monde", Some(uint32_t(16))).IsOk());
	Brisk(g);
	const ecs::Entity p = g.JoinPlayer("Alix");
	Run(g, 0.5f);
	REQUIRE(g.TravelTo(p, "backrooms:level_4").IsOk());
	Run(g, 2.f);
	CHECK(g.CurrentDimension() == "backrooms:level_4");
	// Trois étages (réglage du niveau 4 ; planchers en 0, 6, 12) : une porte palière à chacun.
	for (int y : {1, 7, 13})
		CHECK(Eval(g, (std::string("world.get(10, ") + std::to_string(y) + ", 6)").c_str()).Contains("landing_door"));
	CHECK_EQ(Eval(g, "world.get(10, 24, 3)"), String("elevators:motor"));
	// Mis en service tout seul, cabine au plus bas.
	REQUIRE(Until(g, "len(keys(EL.records))", "1", 5.f));
	CHECK_EQ(Eval(g, (std::string("len(") + E + ".floors)").c_str()), String("3"));
	CHECK_EQ(Eval(g, (std::string(E) + ".floor").c_str()), String("0"));
	// Son tableau électrique : étage le plus bas, à droite de l'encadrement (paliers au sud).
	CHECK_EQ(Eval(g, "world.get(8, 2, 6)"), String("elevators:switchboard"));
	// Étage 1 : les salles de soins (faïence) ; étages 0 et 2 : les couloirs (liste répétée).
	CHECK_EQ(Eval(g, "world.count(-30, 7, -30, 30, 9, 30)[\"backrooms:hospital_wall_tiles\"] > 0"), String("true"));
	CHECK_EQ(Eval(g, "has(world.count(-30, 1, -30, 30, 3, 30), \"backrooms:hospital_wall\")"), String("true"));
	CHECK_EQ(Eval(g, "has(world.count(-30, 7, -30, 30, 9, 30), \"backrooms:hospital_wall\")"), String("false"));
	// Des grilles de ventilation encastrées dans le plafond (y = 5 au rez-de-chaussée), pas au sol.
	CHECK_EQ(Eval(g, "has(world.count(-48, 5, -48, 48, 5, 48), \"backrooms:ventilation\")"), String("true"));
	CHECK_EQ(Eval(g, "has(world.count(-48, 1, -48, 48, 1, 48), \"backrooms:ventilation\")"), String("false"));
	// Des escaliers relient les étages (marches orientées vers +x).
	CHECK_EQ(Eval(g, "has(world.count(-48, 1, -48, 48, 11, 48), \"core:white_concrete_stair\")"), String("true"));
	// Trajet interne : l'étage 2, dans la même dimension.
	(void)g.Scripts().RunSource(String("let e = ") + E +
								" let c = el_cabin_min(e, e.floor) entity.set_pos(entity.players()[0], c.x + 2.5, c.y + 1, c.z + 2.5)");
	(void)g.Scripts().RunSource(String("el_request(") + E + ", 2)");
	REQUIRE(Until(g, (std::string(E) + ".state + \":\" + str(" + E + ".floor)").c_str(), "doors_open:2", 25.f));
	CHECK(g.CurrentDimension() == "backrooms:level_4");
	CHECK_NEAR(Num(g, "entity.pos(entity.players()[0]).y"), 13.f, 0.02f);
	g.CloseWorld();
}

// D + E — changer de niveau en ascenseur (le joueur reste dans la cabine, la
// Nombre d'étages par niveau : réglage général du mod, réglage propre à un
// niveau, gaine et destination d'ascenseur qui suivent ; un monde garde la
// valeur avec laquelle ses niveaux ont été générés.
TEST(Backrooms_FloorsFollowTheModSettings) {
	const String saves = Fresh("floors");
	{
		Game g;
		GameOptions o = WithFloors(Options(saves), "number_of_floors_per_level", 5);
		o = WithFloors(o, "floors_level_4", 7);
		REQUIRE(g.LoadMods(o).errors.empty());
		REQUIRE(g.OpenWorld("monde", Some(uint32_t(19))).IsOk());
		// Moteur au-dessus de la réserve du dernier étage : 6 × étages + 6.
		CHECK_EQ(Eval(g, "world.maze_shaft(\"backrooms:backrooms\").y"), String("36")); // général : 5
		CHECK_EQ(Eval(g, "world.maze_shaft(\"backrooms:level_4\").y"), String("48"));   // niveau 4 : 7
		CHECK_EQ(Eval(g, "world.maze_shaft(\"backrooms:level_4\").x"), String("10"));
		CHECK_EQ(Eval(g, "el_destinations()[\"backrooms_level_4\"].motor[1]"), String("48"));
		const ecs::Entity p = g.JoinPlayer("Alix");
		Run(g, 0.5f);
		REQUIRE(g.TravelTo(p, "backrooms:level_4").IsOk());
		Run(g, 2.f);
		CHECK_EQ(Eval(g, "world.get(10, 48, 3)"), String("elevators:motor"));
		CHECK(Eval(g, "world.get(10, 37, 6)").Contains("landing_door")); // palier du 7e étage (plancher en 36)
		REQUIRE(Until(g, "len(keys(EL.records))", "1", 5.f));
		CHECK_EQ(Eval(g, (std::string("len(") + E + ".floors)").c_str()), String("7"));
		g.CloseWorld();
	}
	// Réglages changés ensuite : ce monde garde ses étages.
	Game g;
	GameOptions o = WithFloors(Options(saves), "number_of_floors_per_level", 3);
	o = WithFloors(o, "floors_level_4", 0);
	REQUIRE(g.LoadMods(o).errors.empty());
	REQUIRE(g.OpenWorld("monde").IsOk());
	CHECK_EQ(Eval(g, "world.maze_shaft(\"backrooms:level_4\").y"), String("48"));
	CHECK_EQ(Eval(g, "world.maze_shaft(\"backrooms:backrooms\").y"), String("36"));
	g.CloseWorld();
}

// dimension change pendant le trajet), puis remonter à la surface.
TEST(Backrooms_DE_ElevatorChangesLevelThenReturnsToTheSurface) {
	Game g;
	REQUIRE(g.LoadMods(Options(Fresh("de"))).errors.empty());
	REQUIRE(g.OpenWorld("monde", Some(uint32_t(17))).IsOk());
	Brisk(g);
	const ecs::Entity p = g.JoinPlayer("Alix");
	Run(g, 0.5f);
	REQUIRE(g.TravelTo(p, "backrooms:level_5").IsOk());
	Run(g, 1.f);
	REQUIRE(Until(g, "len(keys(EL.records))", "1", 5.f));
	(void)g.Scripts().RunSource(String("let e = ") + E +
								" let c = el_cabin_min(e, e.floor) entity.set_pos(entity.players()[0], c.x + 2.5, c.y + 1, c.z + 2.5)");
	Run(g, 0.3f);
	// Proposé dans l'hôtel : le niveau 37 (et pas la surface).
	const String offered = Eval(g, (std::string("el_external_for(") + E + ")").c_str());
	CHECK(offered.Contains("backrooms_level_37"));
	CHECK(!offered.Contains("backrooms_surface"));
	(void)g.Scripts().RunSource(String("el_request_external(") + E + ", \"backrooms_level_37\")");
	// Pendant le trajet, la dimension change ; le joueur arrive DANS la cabine en marche.
	bool sawMovingInDestination = false;
	for (int i = 0; i < 300 && !sawMovingInDestination; ++i) {
		Run(g, 0.1f);
		if (g.CurrentDimension() == "backrooms:level_37")
			sawMovingInDestination = Eval(g, (std::string(E) + ".state").c_str()) == "transfer";
	}
	CHECK(sawMovingInDestination);
	REQUIRE(Until(g, (std::string(E) + ".state").c_str(), "doors_open", 20.f));
	CHECK(g.CurrentDimension() == "backrooms:level_37");
	CHECK_NEAR(Num(g, "entity.pos(entity.players()[0]).y"), 1.f, 0.02f); // plancher de l'étage 0
	CHECK_NEAR(Num(g, "entity.pos(entity.players()[0]).x"), 10.5f, 0.05f); // même place dans la cabine
	// Deux ascenseurs au niveau 37 : l'ordinaire (arrivée, tous les étages) et la
	// sortie vers la surface, une gaine qui ne dessert que le dernier étage.
	(void)g.Scripts().RunSource(String("fn t_shaft(id) { let m = el_shaft_motor(world.dimension(), id) return EL.records[el_key(m[0], m[1], m[2])] }"));
	const char *MAIN = "t_shaft(\"main\")";
	const char *EXIT = "t_shaft(\"surface\")";
	const String here = Eval(g, (std::string("el_external_for(") + MAIN + ")").c_str());
	CHECK(here.Contains("__back")); // retour d'où l'on vient
	CHECK(!here.Contains("backrooms_surface"));
	REQUIRE(Until(g, "len(keys(EL.records))", "2", 5.f)); // la sortie, mise en service toute seule
	CHECK_EQ(Eval(g, (std::string("len(") + EXIT + ".floors)").c_str()), String("1"));
	CHECK_EQ(Eval(g, (std::string(EXIT) + ".floors[0].y == " + MAIN + ".floors[len(" + MAIN + ".floors) - 1].y").c_str()),
			 String("true")); // au dernier étage
	CHECK(Eval(g, (std::string("el_external_for(") + EXIT + ")").c_str()).Contains("backrooms_surface"));
	(void)g.Scripts().RunSource(String("let e = ") + EXIT +
								" let c = el_cabin_min(e, e.floor) entity.set_pos(entity.players()[0], c.x + 2.5, c.y + 1, c.z + 2.5)");
	Run(g, 0.3f);
	(void)g.Scripts().RunSource(String("el_request_external(") + EXIT + ", \"backrooms_surface\")");
	REQUIRE(Until(g, "world.dimension()", "core:overworld", 30.f));
	// Un kiosque d'ascenseur est bâti là où l'on arrive ; la cabine s'y pose au sol.
	REQUIRE(Until(g, (std::string(E) + ".state").c_str(), "doors_open", 25.f));
	const float y = Num(g, "entity.pos(entity.players()[0]).y");
	CHECK_NEAR(y, Num(g, (std::string(E) + ".floors[0].y").c_str()) + 1.f, 0.02f);
	CHECK(Eval(g, (std::string("el_external_for(") + E + ")").c_str()).Contains("__back")); // on peut redescendre
	g.CloseWorld();
}

// I — cas limites : destination inconnue, étage inexistant, gaine incomplète,
// porte palière arrachée pendant un trajet.
TEST(Elevators_I_EdgeCasesAreRefusedOrRepaired) {
	Game g;
	REQUIRE(g.LoadMods(ElevatorOptions(Fresh("i"))).errors.empty());
	REQUIRE(g.OpenWorld("monde", Some(uint32_t(18))).IsOk());
	Brisk(g);
	const ecs::Entity p = g.JoinPlayer("Alix");
	Run(g, 1.f);
	DemoWithRider(g, p);
	CHECK_EQ(Eval(g, (std::string("el_request_external(") + E + ", \"nulle_part\")").c_str()), String("false"));
	CHECK_EQ(Eval(g, (std::string("el_request(") + E + ", 9)").c_str()), String("false"));
	CHECK_EQ(Eval(g, (std::string("el_request(") + E + ", -1)").c_str()), String("false"));
	const std::string scan = std::string("join(el_scan(") + E + ".motor.x, " + E + ".motor.y, " + E + ".motor.z).problems, \" | \")";
	// Les parois de cabine sont libres : n'importe quel bloc convient.
	(void)g.Scripts().RunSource(String("let e = ") + E + " let c = el_cabin_min(e, e.floor) world.set(c.x, c.y + 1, c.z + 1, \"core:stone\")");
	CHECK_EQ(Eval(g, scan.c_str()), String(""));
	// Un plafond troué : la vérification le dit.
	(void)g.Scripts().RunSource(String("let e = ") + E + " let c = el_cabin_min(e, e.floor) world.set(c.x, c.y + 3, c.z + 1, \"core:air\")");
	CHECK(Eval(g, scan.c_str()).Contains("plafond de cabine incomplet"));
	(void)g.Scripts().RunSource(String("let e = ") + E + " let c = el_cabin_min(e, e.floor) world.set(c.x, c.y + 3, c.z + 1, \"elevators:modern_cabin_ceiling\")");
	CHECK_EQ(Eval(g, scan.c_str()), String(""));
	// Porte palière arrachée pendant la montée : remise en place aussitôt.
	(void)g.Scripts().RunSource(String("el_request(") + E + ", 2)");
	REQUIRE(Until(g, (std::string(E) + ".state").c_str(), "moving_up", 4.f));
	(void)g.Scripts().RunSource(String("let l = el_landing(") + E + ", 1) world.set(l.door.x, l.door.y, l.door.z, \"core:air\")");
	Run(g, 0.2f);
	CHECK(Eval(g, (std::string("let l = el_landing(") + E + ", 1) world.get(l.door.x, l.door.y, l.door.z)").c_str()).Contains("landing_door"));
	// Un départ porte ouverte est impossible : la transition est refusée.
	REQUIRE(Until(g, (std::string(E) + ".state").c_str(), "doors_open", 20.f));
	CHECK_EQ(Eval(g, (std::string("el_set_state(") + E + ", \"moving_down\")").c_str()), String("false"));
	g.CloseWorld();
}

namespace {

/// Ascenseur d'exemple construit par la commande `line`, joueur au milieu de la cabine.
void BuildWithRider(Game &g, ecs::Entity p, const char *line) {
	Exec(g, p, line);
	REQUIRE_EQ(Eval(g, "len(keys(EL.records))"), String("1"));
	(void)g.Scripts().RunSource(String("let e = ") + E +
								" let c = el_cabin_min(e, e.floor) entity.set_pos(entity.players()[0], c.x + e.half + 0.5, c.y + 1, c.z + e.half + 0.5)");
	// Porte de cabine de la face `side` à l'arrêt courant (essais).
	(void)g.Scripts().RunSource(String("fn t_cabin_door(e, side) { for (d in e.cabin.doors) { if (d.side == side) { "
									   "return el_cabin_door_pos(e, e.floor, d) } } return nil }"));
	Run(g, 0.5f);
}

} // namespace

// J — grande gaine 9 × 9 (cabine utile 5 × 5), portes doubles 2 × 2, style vitré.
TEST(Elevators_J_LargeShaftWithDoubleDoors) {
	Game g;
	REQUIRE(g.LoadMods(ElevatorOptions(Fresh("j"))).errors.empty());
	REQUIRE(g.OpenWorld("monde", Some(uint32_t(19))).IsOk());
	Brisk(g);
	const ecs::Entity p = g.JoinPlayer("Alix");
	Run(g, 1.f);
	BuildWithRider(g, p, "/elevator build glass 3 9 wide");
	CHECK_EQ(Eval(g, (std::string(E) + ".half").c_str()), String("3"));
	CHECK_EQ(Eval(g, (std::string(E) + ".style").c_str()), String("glass"));
	CHECK_EQ(Eval(g, (std::string(E) + ".cabin.doors[0].width").c_str()), String("2"));
	CHECK_EQ(Eval(g, (std::string(E) + ".floors[1].doors[0].width").c_str()), String("2"));
	const float floor1 = Num(g, (std::string(E) + ".floors[1].y").c_str());
	(void)g.Scripts().RunSource(String("el_request(") + E + ", 1)");
	REQUIRE(Until(g, (std::string(E) + ".state + \":\" + str(" + E + ".floor)").c_str(), "doors_open:1", 20.f));
	CHECK_NEAR(Num(g, "entity.pos(entity.players()[0]).y"), floor1 + 1.f, 0.02f);
	// Les deux battants de la porte palière et de la porte de cabine sont ouverts.
	CHECK(Eval(g, (std::string("let d = ") + E + ".floors[1].doors[0] world.get(d.x, d.y, d.z)").c_str()).EndsWith("_wide_open"));
	CHECK(Eval(g, (std::string("let d = ") + E + ".floors[1].doors[0] world.get(d.x, d.y, d.z + 1)").c_str()).EndsWith("_wide_right_open"));
	CHECK(Eval(g, (std::string("let c = t_cabin_door(") + E + ", \"w\") world.get(c.x, c.y + 1, c.z + 1)").c_str())
			  .EndsWith("_wide_right_top_open"));
	CHECK(!Eval(g, (std::string("let d = ") + E + ".floors[0].doors[0] world.get(d.x, d.y, d.z)").c_str()).Contains("_open"));
	// Une gaine plus grande que le réglage est refusée.
	REQUIRE(Until(g, (std::string(E) + ".state").c_str(), "idle", 10.f));
	(void)g.Scripts().RunSource(String("EL.settings.shaft_max = 7"));
	CHECK(Eval(g, (std::string("join(el_scan(") + E + ".motor.x, " + E + ".motor.y, " + E + ".motor.z).problems, \" | \")").c_str())
			  .Contains("gaine 9 × 9"));
	g.CloseWorld();
}

// K — portes palières sur deux faces : seules les portes de cabine en face
// d'une porte palière de l'étage s'ouvrent ; sans porte de cabine alignée,
// l'installation est refusée.
TEST(Elevators_K_SeveralLandingDoorsPerFloor) {
	Game g;
	REQUIRE(g.LoadMods(ElevatorOptions(Fresh("k"))).errors.empty());
	REQUIRE(g.OpenWorld("monde", Some(uint32_t(20))).IsOk());
	Brisk(g);
	const ecs::Entity p = g.JoinPlayer("Alix");
	Run(g, 1.f);
	BuildWithRider(g, p, "/elevator build modern 3 7 multi");
	CHECK_EQ(Eval(g, (std::string("len(") + E + ".cabin.doors)").c_str()), String("2"));
	CHECK_EQ(Eval(g, (std::string("len(") + E + ".floors[0].doors)").c_str()), String("2"));
	CHECK_EQ(Eval(g, (std::string("len(") + E + ".floors[1].doors)").c_str()), String("1"));
	// Étage 1 : palier à l'ouest seulement.
	(void)g.Scripts().RunSource(String("el_request(") + E + ", 1)");
	REQUIRE(Until(g, (std::string(E) + ".state + \":\" + str(" + E + ".floor)").c_str(), "doors_open:1", 20.f));
	CHECK(Eval(g, (std::string("let c = t_cabin_door(") + E + ", \"w\") world.get(c.x, c.y, c.z)").c_str()).EndsWith("_open"));
	CHECK(!Eval(g, (std::string("let c = t_cabin_door(") + E + ", \"e\") world.get(c.x, c.y, c.z)").c_str()).Contains("_open"));
	// Étage 2 : deux paliers, les deux portes de cabine s'ouvrent.
	(void)g.Scripts().RunSource(String("el_request(") + E + ", 2)");
	REQUIRE(Until(g, (std::string(E) + ".state + \":\" + str(" + E + ".floor)").c_str(), "doors_open:2", 20.f));
	CHECK(Eval(g, (std::string("let c = t_cabin_door(") + E + ", \"w\") world.get(c.x, c.y, c.z)").c_str()).EndsWith("_open"));
	CHECK(Eval(g, (std::string("let c = t_cabin_door(") + E + ", \"e\") world.get(c.x, c.y, c.z)").c_str()).EndsWith("_open"));
	REQUIRE(Until(g, (std::string(E) + ".state").c_str(), "idle", 10.f));
	// La porte de cabine est remplacée par une paroi : plus de porte en face du palier est.
	(void)g.Scripts().RunSource(String("let c = t_cabin_door(") + E + ", \"e\") world.set(c.x, c.y, c.z, \"elevators:modern_cabin_wall\") "
								"world.set(c.x, c.y + 1, c.z, \"elevators:modern_cabin_wall\")");
	CHECK(Eval(g, (std::string("join(el_scan(") + E + ".motor.x, " + E + ".motor.y, " + E + ".motor.z).problems, \" | \")").c_str())
			  .Contains("sans porte de cabine alignée"));
	g.CloseWorld();
}

// L — la plus petite gaine : 5 × 5, cabine utile 1 × 1.
TEST(Elevators_L_SmallestShaft) {
	Game g;
	REQUIRE(g.LoadMods(ElevatorOptions(Fresh("l"))).errors.empty());
	REQUIRE(g.OpenWorld("monde", Some(uint32_t(21))).IsOk());
	Brisk(g);
	const ecs::Entity p = g.JoinPlayer("Alix");
	Run(g, 1.f);
	BuildWithRider(g, p, "/elevator build industrial 2 5");
	CHECK_EQ(Eval(g, (std::string(E) + ".half").c_str()), String("1"));
	const float floor1 = Num(g, (std::string(E) + ".floors[1].y").c_str());
	(void)g.Scripts().RunSource(String("el_request(") + E + ", 1)");
	REQUIRE(Until(g, (std::string(E) + ".state + \":\" + str(" + E + ".floor)").c_str(), "doors_open:1", 20.f));
	CHECK_NEAR(Num(g, "entity.pos(entity.players()[0]).y"), floor1 + 1.f, 0.02f);
	g.CloseWorld();
}

// M — tableau électrique : diagnostic, numérotation, essais ; étages trop proches.
TEST(Elevators_M_SwitchboardAndFloorSpacing) {
	Game g;
	REQUIRE(g.LoadMods(ElevatorOptions(Fresh("m"))).errors.empty());
	REQUIRE(g.OpenWorld("monde", Some(uint32_t(22))).IsOk());
	Brisk(g);
	const ecs::Entity p = g.JoinPlayer("Alix");
	Run(g, 1.f);
	DemoWithRider(g, p);
	const std::string m = std::string(E) + ".motor";
	// Le tableau, posé contre la gaine, trouve son moteur.
	(void)g.Scripts().RunSource(String("let m = ") + m.c_str() + " world.set(m.x - 4, m.y - 2, m.z + 4, \"elevators:switchboard\")");
	CHECK_EQ(Eval(g, (std::string("el_board_motor(") + m + ".x - 4, " + m + ".y - 2, " + m + ".z + 4).y == " + m + ".y").c_str()),
			 String("true"));
	CHECK(Eval(g, (std::string("el_diagnose(") + m + ").text").c_str()).Contains("INSTALLATION : ✓ en ordre"));
	CHECK_EQ(Eval(g, (std::string("el_board_test(") + m + ", -1)").c_str()), String("déjà au premier arrêt."));
	CHECK(Eval(g, (std::string("el_board_test(") + m + ", 1)").c_str()).Contains("trajet demandé"));
	REQUIRE(Until(g, (std::string(E) + ".state + \":\" + str(" + E + ".floor)").c_str(), "doors_open:1", 20.f));
	REQUIRE(Until(g, (std::string(E) + ".state").c_str(), "idle", 10.f));
	// Numérotation : les numéros reviennent à 0, 1, 2.
	(void)g.Scripts().RunSource(String("let e = ") + E + " e.floors[1].label = \"7\"");
	(void)g.Scripts().RunSource(String("el_renumber(") + m.c_str() + ")");
	CHECK_EQ(Eval(g, (std::string(E) + ".floors[1].label").c_str()), String("1"));
	// Une porte palière 2 blocs au-dessus d'une autre : étages trop proches.
	(void)g.Scripts().RunSource(String("let e = ") + E + " let d = e.floors[0].doors[0] el_place_door(d.x, d.y + 2, d.z, d.side, d.base)");
	const String diag = Eval(g, (std::string("el_diagnose(") + m + ").text").c_str());
	CHECK(diag.Contains("trop proches"));
	CHECK(diag.Contains("défaillance"));
	g.CloseWorld();
}

// N — réglages du mod : ceux de l'utilisateur au chargement, puis modifiés en
// cours de partie (pour tous les mondes, ou pour ce monde, qui l'emporte).
TEST(Elevators_N_SettingsChangesAreApplied) {
	GameOptions o = ElevatorOptions(Fresh("n"));
	o.modSettings = dataio::Node::MakeObject();
	dataio::NodePtr mine = dataio::Node::MakeObject();
	mine->Set("max_height", dataio::Node::MakeInt(512));
	o.modSettings->Set("elevators", mine);
	Game g;
	REQUIRE(g.LoadMods(o).errors.empty());
	REQUIRE(g.OpenWorld("monde", Some(uint32_t(23))).IsOk());
	(void)g.JoinPlayer("Alix");
	Run(g, 0.5f);
	CHECK_EQ(Eval(g, "EL.settings.max_height"), String("512"));
	auto values = [](int h) {
		dataio::NodePtr v = dataio::Node::MakeObject();
		v->Set("max_height", dataio::Node::MakeInt(h));
		return v;
	};
	g.UpdateModSettings("elevators", values(64), false);
	CHECK_EQ(Eval(g, "EL.settings.max_height"), String("64"));
	g.UpdateModSettings("elevators", values(100), true);
	CHECK_EQ(Eval(g, "EL.settings.max_height"), String("100"));
	g.UpdateModSettings("elevators", values(200), false); // le réglage du monde reste prioritaire
	CHECK_EQ(Eval(g, "EL.settings.max_height"), String("100"));
	g.CloseWorld();
}

// O — hauteur des cabines construites (4 ou 5) et afficheurs de cabine :
// au-dessus de la porte (5), sur une paroi latérale (4), ou posés n'importe
// où après la mise en service — tous suivent l'étage.
TEST(Elevators_O_CabinHeightAndDisplaysAnywhere) {
	Game g;
	REQUIRE(g.LoadMods(ElevatorOptions(Fresh("o"))).errors.empty());
	REQUIRE(g.OpenWorld("monde", Some(uint32_t(24))).IsOk());
	Brisk(g);
	const ecs::Entity p = g.JoinPlayer("Alix");
	Run(g, 1.f);
	BuildWithRider(g, p, "/elevator build industrial 3 7 5");
	CHECK_EQ(Eval(g, (std::string(E) + ".h").c_str()), String("5"));
	// Cabine de 5 : l'afficheur est juste au-dessus de la porte (rang 3).
	CHECK(Eval(g, (std::string("let c = t_cabin_door(") + E + ", \"w\") world.get(c.x, c.y + 2, c.z)").c_str()).Contains("_display"));
	CHECK_EQ(Eval(g, (std::string(E) + ".cabin.displays[0].y").c_str()), String("3"));
	// Un second afficheur posé après coup, sur la paroi du fond : relu au pas suivant.
	(void)g.Scripts().RunSource(String("let e = ") + E + " let m = el_cabin_min(e, e.floor) world.set(m.x + 2 * e.half, m.y + 2, m.z + e.half, \"elevators:modern_display\") e.cabin_dirty = true");
	Run(g, 0.2f);
	CHECK_EQ(Eval(g, (std::string("len(") + E + ".cabin.displays)").c_str()), String("2"));
	(void)g.Scripts().RunSource(String("el_request(") + E + ", 2)");
	REQUIRE(Until(g, (std::string(E) + ".state + \":\" + str(" + E + ".floor)").c_str(), "doors_open:2", 25.f));
	CHECK(Eval(g, (std::string("let c = t_cabin_door(") + E + ", \"w\") world.get(c.x, c.y + 2, c.z)").c_str()).EndsWith("_display_2"));
	CHECK(Eval(g, (std::string("let e = ") + E + " let m = el_cabin_min(e, e.floor) world.get(m.x + 2 * e.half, m.y + 2, m.z + e.half)").c_str())
			  .EndsWith("modern_display_2"));
	g.CloseWorld();
}

// O bis — cabine de 4 : l'afficheur est sur une paroi latérale, au rang haut.
TEST(Elevators_O_LowCabinDisplayOnTheSide) {
	Game g;
	REQUIRE(g.LoadMods(ElevatorOptions(Fresh("o4"))).errors.empty());
	REQUIRE(g.OpenWorld("monde", Some(uint32_t(25))).IsOk());
	Brisk(g);
	const ecs::Entity p = g.JoinPlayer("Alix");
	Run(g, 1.f);
	BuildWithRider(g, p, "/elevator build modern 2 7 4");
	CHECK_EQ(Eval(g, (std::string(E) + ".h").c_str()), String("4"));
	CHECK_EQ(Eval(g, (std::string("len(") + E + ".cabin.displays)").c_str()), String("1"));
	CHECK_EQ(Eval(g, (std::string(E) + ".cabin.displays[0].y").c_str()), String("2"));       // rang haut de la paroi
	CHECK(Eval(g, (std::string(E) + ".cabin.displays[0].z").c_str()) != String("2"));         // pas sur la paroi de la porte (ouest, x = 0)
	CHECK_EQ(Eval(g, (std::string(E) + ".cabin.displays[0].x").c_str()), String("1"));       // contre la paroi de la porte
	(void)g.Scripts().RunSource(String("el_request(") + E + ", 1)");
	REQUIRE(Until(g, (std::string(E) + ".state + \":\" + str(" + E + ".floor)").c_str(), "doors_open:1", 20.f));
	CHECK(Eval(g, (std::string("let e = ") + E + " let d = e.cabin.displays[0] let m = el_cabin_min(e, e.floor) world.get(m.x + d.x, m.y + d.y, m.z + d.z)").c_str())
			  .EndsWith("_display_1"));
	g.CloseWorld();
}

// P — tableau électrique posé à la construction (étage le plus bas, à droite
// de l'encadrement de la porte palière) : il commande et configure tout, et
// signale un moteur disparu ; le moteur n'a plus d'interface.
TEST(Elevators_P_BuiltElevatorHasItsSwitchboard) {
	Game g;
	REQUIRE(g.LoadMods(ElevatorOptions(Fresh("p"))).errors.empty());
	REQUIRE(g.OpenWorld("monde", Some(uint32_t(26))).IsOk());
	Brisk(g);
	const ecs::Entity p = g.JoinPlayer("Alix");
	Run(g, 1.f);
	DemoWithRider(g, p);
	// Porte palière côté ouest : la droite (vue du palier) est vers −z ; encadrement en −1, tableau en −2.
	const std::string board = std::string("let e = ") + E + " let c = el_face_cell(e.motor.x, e.motor.z, \"w\", e.half + 1, -2) ";
	CHECK_EQ(Eval(g, (board + "world.get(c.x, e.floors[0].y + 2, c.z)").c_str()), String("elevators:switchboard"));
	CHECK_EQ(Eval(g, (board + "world.param(c.x, e.floors[0].y + 2, c.z)").c_str()), String("1")); // face avant vers l'ouest
	(void)g.Scripts().RunSource((board + "let m = el_board_motor(c.x, e.floors[0].y + 2, c.z) EL.t_board = m.y == e.motor.y").c_str());
	CHECK_EQ(Eval(g, "EL.t_board"), String("true"));
	const std::string m = std::string(E) + ".motor";
	CHECK(Eval(g, (std::string("el_diagnose(") + m + ").text").c_str()).Contains("peut fonctionner"));
	// Configuration depuis le tableau.
	(void)g.Scripts().RunSource(String("EL.t_msg = el_apply_config(") + E + ", entity.players()[0], {\"name:1\": \"Atelier\", door_time: 2, maintenance: true})");
	CHECK_EQ(Eval(g, "EL.t_msg"), String("configuration enregistrée."));
	CHECK_EQ(Eval(g, (std::string(E) + ".floors[1].name").c_str()), String("Atelier"));
	CHECK_EQ(Eval(g, (std::string(E) + ".state").c_str()), String("maintenance"));
	// Le moteur disparaît : le tableau le dit.
	(void)g.Scripts().RunSource(String("let m = ") + m.c_str() + " world.set(m.x, m.y, m.z, \"core:air\")");
	CHECK(Eval(g, (std::string("el_diagnose(") + m + ").text").c_str()).Contains("MOTEUR : ✗ absent"));
	g.CloseWorld();
}
