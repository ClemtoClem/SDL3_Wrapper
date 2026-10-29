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
	// Une paroi de cabine manquante : la vérification le dit.
	(void)g.Scripts().RunSource(String("let e = ") + E + " let c = el_cabin_min(e, e.floor) world.set(c.x, c.y + 1, c.z + 1, \"core:air\")");
	const String problems = Eval(g, (std::string("join(el_scan(") + E + ".motor.x, " + E + ".motor.y, " + E + ".motor.z).problems, \" | \")").c_str());
	CHECK(problems.Contains("cabine incomplète"));
	(void)g.Scripts().RunSource(String("let e = ") + E + " let c = el_cabin_min(e, e.floor) world.set(c.x, c.y + 1, c.z + 1, \"elevators:modern_cabin_wall\")");
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
