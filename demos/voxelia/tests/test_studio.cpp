// Tests — Voxelia Studio sans fenêtre : console à code d'état, World Outliner
// et inspecteur sur une VRAIE partie (voxel::Game), rapport d'exécution.
#include "test.hpp"

#include <filesystem>

#include <voxel/components.hpp>
#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <voxelia/studio/console.hpp>
#include <voxelia/studio/outliner.hpp>
#include <voxelia/studio/report.hpp>

using namespace voxelia::studio;

// ── Console ──────────────────────────────────────────────────────────────────

namespace {

ConsoleModel MakeConsole(int *confirmed = nullptr) {
	ConsoleModel console;
	console.Register("ok", "réussit", [](const auto &) { return CommandResult::Ok(String("fait")); });
	console.Register("fail", "échoue", [](const auto &) { return CommandResult::Error(String("raté")); });
	console.Register("warn", "avertit", [](const auto &) { return CommandResult::Warning(String("attention")); });
	console.Register("long", "travail long", [](const auto &) { return CommandResult::Pending(String("en cours")); });
	console.Register("delete", "supprime (confirmation)", [confirmed](const auto &args) {
		const String what = args.size() > 1 ? args[1] : String("?");
		return CommandResult::Confirm(String("Supprimer ") + what + String(" ?"), [confirmed, what] {
			if (confirmed)
				++*confirmed;
			return CommandResult::Ok(String("supprimé : ") + what);
		});
	});
	return console;
}

} // namespace

TEST(StudioConsole_OkClosesErrorStaysOpen) {
	ConsoleModel console = MakeConsole();
	console.Open();
	CHECK(console.Submit("ok").status == CommandStatus::Ok);
	CHECK(!console.Visible()); // Ok : fermée
	CHECK(console.Submit("fail").status == CommandStatus::Error);
	CHECK(console.Visible()); // Error : ouverte
	CHECK(console.Lines().back().kind == "error");
	CHECK(console.Lines().back().text == "raté");
	console.Update(10.f);
	CHECK(console.Visible()); // une erreur ne se ferme pas d'elle-même
	console.autoClose = false;
	CHECK(console.Submit("ok").status == CommandStatus::Ok);
	CHECK(console.Visible()); // auto-fermeture désactivée
}

TEST(StudioConsole_WarningClosesAfterFiveSeconds) {
	ConsoleModel console = MakeConsole();
	CHECK(console.Submit("warn").status == CommandStatus::Warning);
	CHECK(console.Visible());
	CHECK(std::fabs(console.CloseCountdown() - ConsoleModel::WARNING_SECONDS) < 1e-4f);
	console.Update(4.9f);
	CHECK(console.Visible());
	console.Update(0.2f);
	CHECK(!console.Visible());
}

TEST(StudioConsole_ConfirmationPrompt) {
	int confirmed = 0;
	ConsoleModel console = MakeConsole(&confirmed);
	CommandResult r = console.Submit("delete \"mon monde\"");
	CHECK(r.status == CommandStatus::ConfirmRequired);
	CHECK(console.AwaitingConfirmation());
	CHECK(console.Prompt() == "Supprimer mon monde ?");
	CHECK(console.Lines().back().text == "Supprimer mon monde ? [y/N]");
	console.Close();
	CHECK(console.Visible()); // la question retient la console
	// Refus (par défaut : non).
	r = console.Submit("");
	CHECK(r.status == CommandStatus::Ok);
	CHECK_EQ(confirmed, 0);
	CHECK(!console.AwaitingConfirmation());
	// Acceptation : la ligne suivante répond.
	(void)console.Submit("delete x");
	r = console.Submit("oui");
	CHECK(r.status == CommandStatus::Ok);
	CHECK(r.message == "supprimé : x");
	CHECK_EQ(confirmed, 1);
	CHECK(!console.Visible());
	CHECK_EQ(console.Count(CommandStatus::ConfirmRequired), size_t(2));
}

TEST(StudioConsole_PendingUntilResolved) {
	ConsoleModel console = MakeConsole();
	CHECK(console.Submit("long").status == CommandStatus::Pending);
	CHECK(console.PendingWork());
	console.Close();
	CHECK(console.Visible());
	console.Resolve(CommandResult::Ok(String("terminé")));
	CHECK(!console.PendingWork());
	CHECK(!console.Visible());
	CHECK(console.Lines().back().text == "terminé");
}

TEST(StudioConsole_FallbackHistoryCompletion) {
	ConsoleModel console = MakeConsole();
	String seen;
	console.SetFallback([&](const String &line) {
		seen = line;
		return CommandResult::Error(String("inconnue"));
	});
	CHECK(console.Submit("  /give core:stone 4 ").status == CommandStatus::Error);
	CHECK(seen == "/give core:stone 4");
	(void)console.Submit("ok");
	(void)console.Submit("ok"); // doublon consécutif : une seule entrée
	CHECK_EQ(console.History().size(), size_t(2));
	CHECK(console.HistoryStep(-1) == "ok");
	CHECK(console.HistoryStep(-1) == "/give core:stone 4");
	CHECK(console.HistoryStep(-1) == "/give core:stone 4"); // butée
	CHECK(console.HistoryStep(1) == "ok");
	CHECK(console.HistoryStep(1).IsEmpty());

	console.SetCompletionWords({String("core:stone"), String("core:sand"), String("backrooms:wall")});
	std::vector<String> c = console.Complete("de");
	REQUIRE(c.size() == size_t(1));
	CHECK(c[0] == "delete");
	c = console.Complete("delete core:s");
	CHECK_EQ(c.size(), size_t(2));
	CHECK(console.Submit("help").message.Find("warn — avertit") != String::NPOS);
	const std::vector<String> words = SplitCommandLine("a \"b c\" d");
	REQUIRE(words.size() == size_t(3));
	CHECK(words[1] == "b c");
}

// ── Outliner et inspecteur sur une vraie partie ─────────────────────────────

namespace {

voxel::GameOptions StudioOptions(const char *saves) {
	voxel::GameOptions o;
	o.modDirectories = {String("demos/voxelia/mods")};
	o.savesDirectory = String(saves);
	o.viewRadius = 2;
	o.verticalRadius = 2;
	o.chunkBudget = 200;
	return o;
}

} // namespace

TEST(StudioOutliner_ListsLiveEntities) {
	const char *saves = "build/tests/tmp/voxelia/studio_outliner";
	std::filesystem::remove_all(saves);
	voxel::Game game;
	(void)game.LoadMods(StudioOptions(saves));
	REQUIRE(game.OpenWorld("outliner", Some(uint32_t(4242))).IsOk());
	const ecs::Entity player = game.JoinPlayer("Inspectrice");
	for (int i = 0; i < 60; ++i)
		game.Tick(1.f / 60.f);
	const math::FVector3 feet = game.Registry().GetComponent<voxel::Transform>(player).Unwrap()->position;
	const ecs::Entity pig = game.SpawnEntity("core:pig", {feet.x + 3.f, feet.y, feet.z});
	REQUIRE(pig.Valid());
	voxel::ItemStack stones;
	stones.item = String("core:stone");
	stones.count = 3;
	const ecs::Entity item = game.DropItem(stones, {feet.x - 2.f, feet.y + 1.f, feet.z});
	REQUIRE(item.Valid());
	game.Tick(1.f / 60.f);

	std::vector<OutlinerEntry> all = CollectEntities(game, {}, feet);
	auto find = [&](ecs::Entity e) -> const OutlinerEntry * {
		for (const OutlinerEntry &x : all)
			if (x.entity == e)
				return &x;
		return nullptr;
	};
	const OutlinerEntry *p = find(player), *c = find(pig), *d = find(item);
	REQUIRE(p != nullptr);
	REQUIRE(c != nullptr);
	REQUIRE(d != nullptr);
	CHECK(p->category == EntityCategory::Player);
	CHECK(p->name == "Inspectrice");
	CHECK(c->category == EntityCategory::Creature);
	CHECK(c->id == "core:pig");
	CHECK(d->category == EntityCategory::Item);
	CHECK(p->dimension == game.CurrentDimension());
	CHECK(p->distance < 0.5f);
	CHECK(std::fabs(c->distance - 3.f) < 1.5f);
	// Ordre : par catégorie (joueurs d'abord).
	CHECK(all.front().category == EntityCategory::Player);
	for (size_t i = 1; i < all.size(); ++i)
		CHECK(all[i - 1].category <= all[i].category);

	// Filtres : texte, catégorie, distance, vivants.
	OutlinerFilter byText;
	byText.text = "PIG";
	all = CollectEntities(game, byText, feet);
	REQUIRE(!all.empty());
	for (const OutlinerEntry &e : all)
		CHECK(e.id.Find("pig") != String::NPOS || e.name.ToLower().Find("pig") != String::NPOS);
	OutlinerFilter players;
	players.category = Some(EntityCategory::Player);
	all = CollectEntities(game, players, feet);
	REQUIRE(all.size() == size_t(1));
	CHECK(all[0].entity == player);
	OutlinerFilter near;
	near.maxDistance = 0.5f;
	all = CollectEntities(game, near, feet);
	CHECK(find(pig) == nullptr);

	game.Kill(pig);
	game.Tick(1.f / 60.f);
	OutlinerFilter alive;
	alive.aliveOnly = true;
	all = CollectEntities(game, alive, feet);
	CHECK(find(pig) == nullptr);
}

TEST(StudioInspector_ShowsLiveComponents) {
	const char *saves = "build/tests/tmp/voxelia/studio_inspector";
	std::filesystem::remove_all(saves);
	voxel::Game game;
	(void)game.LoadMods(StudioOptions(saves));
	REQUIRE(game.OpenWorld("inspector", Some(uint32_t(99))).IsOk());
	const ecs::Entity player = game.JoinPlayer("Joueur");
	for (int i = 0; i < 30; ++i)
		game.Tick(1.f / 60.f);
	auto sectionOf = [](const std::vector<InspectorSection> &sections, const char *title) -> const InspectorSection * {
		for (const InspectorSection &s : sections)
			if (s.title == title)
				return &s;
		return nullptr;
	};
	auto rowOf = [](const InspectorSection *s, const char *label) -> String {
		if (s)
			for (const InspectorRow &r : s->rows)
				if (r.label == label)
					return r.value;
		return String("<absent>");
	};
	std::vector<InspectorSection> sections = InspectEntity(game, player);
	REQUIRE(sectionOf(sections, "Transform") != nullptr);
	REQUIRE(sectionOf(sections, "Physique") != nullptr);
	REQUIRE(sectionOf(sections, "Santé") != nullptr);
	REQUIRE(sectionOf(sections, "Survie") != nullptr);
	const math::FVector3 at = game.Registry().GetComponent<voxel::Transform>(player).Unwrap()->position;
	CHECK(rowOf(sectionOf(sections, "Transform"), "Position") ==
		  String::Format("%.2f, %.2f, %.2f", double(at.x), double(at.y), double(at.z)));

	// Valeurs EN DIRECT : un dégât se voit à la lecture suivante.
	game.Damage(player, 3.f, ecs::Entity{}, String("test"));
	sections = InspectEntity(game, player);
	const String hp = rowOf(sectionOf(sections, "Santé"), "PV");
	auto health = game.Registry().GetComponent<voxel::Health>(player);
	CHECK(hp == String::Format("%.1f / %.1f", double(health.Unwrap()->current), double(health.Unwrap()->max)));
	CHECK(health.Unwrap()->current < health.Unwrap()->max);

	game.AddEffect(player, String("poison"), 5.f, 2);
	sections = InspectEntity(game, player);
	if (const InspectorSection *fx = sectionOf(sections, "Effets"))
		CHECK(rowOf(fx, "poison").StartsWith("niveau 2"));

	// Entité disparue : aucune section.
	const ecs::Entity gone = game.SpawnEntity("core:pig", at);
	REQUIRE(gone.Valid());
	CHECK(!InspectEntity(game, gone).empty());
	(void)game.Registry().Despawn(gone);
	CHECK(InspectEntity(game, gone).empty());
	CHECK(InspectEntity(game, ecs::Entity{}).empty());
}

// ── Rapport ──────────────────────────────────────────────────────────────────

TEST(StudioReport_FeaturesFramesAndJson) {
	StudioReport report;
	report.Visit("outliner.refresh");
	report.Visit("outliner.refresh");
	report.Visit("console.command", FeatureStatus::Warning, String("warning"));
	report.Visit("console.command", FeatureStatus::Ok, String("ok")); // le pire statut reste
	for (int i = 1; i <= 100; ++i)
		report.Frame(double(i));
	report.Threads(3);
	report.Threads(2);
	report.Capture("cap.png");
	report.Count("console.ok");
	report.Count("console.ok", 2);
	CHECK(report.Passed());
	CHECK_EQ(report.Features().at(String("outliner.refresh")).visits, size_t(2));
	CHECK(report.Features().at(String("console.command")).status == FeatureStatus::Warning);
	CHECK(report.Features().at(String("console.command")).detail == "warning");
	CHECK_EQ(report.MaxThreads(), 3);
	CHECK_EQ(report.Counter(String("console.ok")), 3);
	const FrameSummary f = report.Frames();
	CHECK_EQ(f.frames, size_t(100));
	CHECK(std::fabs(f.minMs - 1.0) < 1e-9);
	CHECK(std::fabs(f.maxMs - 100.0) < 1e-9);
	CHECK(std::fabs(f.avgMs - 50.5) < 1e-9);
	CHECK(std::fabs(f.p95Ms - 96.0) < 1e-9);

	const String path("build/tests/tmp/voxelia/studio_report/rapport.txt");
	REQUIRE(report.Write(path).IsOk());
	auto parsed = voxel::json::ReadFile(path + String(".json"));
	REQUIRE(parsed.IsOk());
	CHECK(voxel::json::Bool(parsed.Value(), "passed") == true);
	// Les noms de fonctionnalités contiennent des points : lecture par clé.
	const dataio::NodePtr features = voxel::json::At(parsed.Value(), "features");
	REQUIRE(features != nullptr);
	REQUIRE(features->Has(String("console.command")));
	CHECK(voxel::json::Str(features->Get(String("console.command")), "status") == "warning");
	CHECK(voxel::json::Int(parsed.Value(), "maxThreads") == 3);

	report.Visit("capture.studio", FeatureStatus::Error, String("studio fermé"));
	CHECK(!report.Passed());
	CHECK(report.ToText().Find("[error  ] capture.studio") != String::NPOS);
}
