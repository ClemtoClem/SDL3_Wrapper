// Tests — éditeur d'entités de Voxelia Studio sans fenêtre : document d'une
// créature du mod core (comportements ordonnés et leurs paramètres, butin,
// skins), validation à blanc (références, physique, IA inconnue,
// apparition), composants d'une entité apparue, enregistrement, et
// rechargement à chaud suivi d'une apparition qui prend la nouvelle définition.
#include "test.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <voxel/scripting.hpp>
#include <voxelia/studio/entity_document.hpp>

using namespace voxelia::studio;
namespace json = voxel::json;

namespace {

voxel::GameOptions EntityOptions(const char *saves) {
	voxel::GameOptions o;
	o.modDirectories = {String("demos/voxelia/mods")};
	o.savesDirectory = String(saves);
	o.viewRadius = 2;
	o.verticalRadius = 2;
	o.chunkBudget = 200;
	return o;
}

bool Has(const std::vector<EntityDiagnostic> &list, const char *text) {
	for (const EntityDiagnostic &d : list)
		if (d.message.Contains(text))
			return true;
	return false;
}

} // namespace

TEST(StudioEntities_DocumentBehaviorsDropsSkins) {
	voxel::Game game;
	(void)game.LoadMods(EntityOptions("build/tests/tmp/voxelia/studio_entities_doc"));
	Option<EntityDocument> opened = EntityDocument::FromEntity(game.Content(), "core:zombie");
	REQUIRE(opened.IsSome());
	EntityDocument doc = std::move(opened).Unwrap();
	CHECK_EQ(doc.Kind(), String("creature"));
	CHECK(doc.SourceFile().Contains("monsters.json"));
	CHECK(EntityDocument::FromEntity(game.Content(), "core:absent").IsNone());
	// Comportements dans l'ordre, paramètres compris.
	std::vector<EntityBehavior> b = doc.Behaviors();
	REQUIRE(b.size() >= size_t(3));
	CHECK_EQ(b[0].id, String("core:chase"));
	REQUIRE(b[0].params != nullptr);
	CHECK_EQ(json::Int(b[0].params, "damage", 0), 3);
	CHECK(b.back().params == nullptr); // « "float" » : sans paramètre
	const size_t count = b.size();
	REQUIRE(doc.AddBehavior(String("look_at_player")).IsOk());
	CHECK_EQ(doc.Behaviors().size(), count + 1);
	REQUIRE(doc.MoveBehavior(count, 0).IsOk());
	CHECK_EQ(doc.Behaviors()[0].id, String("look_at_player"));
	CHECK_EQ(doc.Behaviors()[1].id, String("core:chase"));
	// Un paramètre sur un comportement écrit en raccourci : il devient un objet.
	REQUIRE(doc.SetBehaviorParam(0, String("range"), json::MakeNum(9.0)).IsOk());
	REQUIRE(doc.Behaviors()[0].params != nullptr);
	CHECK_EQ(json::Int(doc.Behaviors()[0].params, "range", 0), 9);
	CHECK_EQ(doc.Behaviors()[0].id, String("look_at_player"));
	REQUIRE(doc.SetBehaviorParam(0, String("range"), nullptr).IsOk());
	CHECK(!json::Has(doc.Behaviors()[0].params, "range"));
	CHECK(doc.SetBehaviorParam(0, String("id"), json::MakeStr(String("x"))).IsError());
	CHECK(doc.SetBehaviorParam(99, String("range"), json::MakeNum(1.0)).IsError());
	REQUIRE(doc.RemoveBehavior(0).IsOk());
	CHECK_EQ(doc.Behaviors().size(), count);
	CHECK(doc.MoveBehavior(0, 99).IsError());
	// Butin.
	const auto drops = doc.Drops();
	REQUIRE(!drops.empty());
	CHECK_EQ(drops[0].first, String("core:rotten_flesh"));
	REQUIRE(doc.AddDrop(String("core:bone"), 3).IsOk());
	CHECK_EQ(doc.Drops().back().second, 3);
	CHECK(doc.AddDrop(String("core:bone"), 0).IsError());
	REQUIRE(doc.RemoveDrop(doc.Drops().size() - 1).IsOk());
	CHECK_EQ(doc.Drops().size(), drops.size());
	// Skins.
	const size_t skins = doc.Skins().size();
	CHECK(skins >= size_t(2));
	REQUIRE(doc.AddSkin(String("core:entities/zombie/zombie")).IsOk());
	CHECK_EQ(doc.Skins().size(), skins + 1);
	REQUIRE(doc.RemoveSkin(skins).IsOk());
	CHECK_EQ(doc.Skins().size(), skins);
	// Tout s'annule.
	while (doc.Undo()) {
	}
	CHECK_EQ(doc.Behaviors()[0].id, String("core:chase"));
	CHECK(!doc.Dirty() || doc.UndoDepth() == 0);
}

TEST(StudioEntities_ValidationAndComponents) {
	voxel::Game game;
	(void)game.LoadMods(EntityOptions("build/tests/tmp/voxelia/studio_entities_validate"));
	const std::vector<String> scripted = game.Scripts().BehaviorIds();
	CHECK(std::find(scripted.begin(), scripted.end(), String("core:chase")) != scripted.end());
	EntityDocument doc = EntityDocument::FromEntity(game.Content(), "core:zombie").Unwrap();
	CHECK(doc.Validate(game.Content(), scripted).empty()); // le zombie du mod est correct
	doc.SetField("physics.collider", json::Parse(String("[0.6, 0, 0.6]")).Value());
	doc.SetField("health", json::MakeNum(0.0));
	doc.SetField("rig.model", json::MakeStr(String("core:pas_de_modele")));
	doc.SetField("spawn.chance", json::MakeNum(2.0));
	doc.SetField("spawn.biomes", json::Parse(String(R"(["core:plains", "core:nulle_part"])")).Value());
	doc.SetField("sounds.idle", json::MakeStr(String("core:silence_absent")));
	REQUIRE(doc.AddBehavior(String("core:danse")).IsOk());
	REQUIRE(doc.AddDrop(String("core:tresor_absent"), 1).IsOk());
	const std::vector<EntityDiagnostic> diags = doc.Validate(game.Content(), scripted);
	CHECK(Has(diags, "boîte de collision"));
	CHECK(Has(diags, "points de vie"));
	CHECK(Has(diags, "pas_de_modele"));
	CHECK(Has(diags, "chance d'apparition"));
	CHECK(Has(diags, "nulle_part"));
	CHECK(!Has(diags, "core:plains"));
	CHECK(Has(diags, "silence_absent"));
	CHECK(Has(diags, "core:danse"));
	CHECK(Has(diags, "tresor_absent"));
	// Le contenu vivant n'a pas bougé.
	CHECK(game.Content().FindEntity("core:zombie")->health > 0.f);
	// Une définition qui ne se charge pas : une erreur.
	CHECK(doc.SetJson(String(R"({"type": "creature"})")).IsOk());
	const std::vector<EntityDiagnostic> broken = doc.Validate(game.Content(), scripted);
	REQUIRE(!broken.empty());
	CHECK(broken.front().severity == EntityDiagnostic::Severity::Error);
	// Composants : ceux de SpawnFromDef.
	EntityDocument pig = EntityDocument::FromEntity(game.Content(), "core:pig").Unwrap();
	const auto components = pig.Components(game.Content());
	auto component = [&](const char *name) {
		for (const auto &[n, summary] : components)
			if (n == name)
				return summary;
		return String("—absent—");
	};
	CHECK(component("Collider").Contains("0.90"));
	CHECK(component("Health").Contains("10 PV"));
	CHECK(component("Visual").Contains("core:pig"));
	CHECK(component("Behaviors").Contains("wander"));
	CHECK(component("Behaviors").Contains("follow"));
	CHECK(component("Persistent") != String("—absent—"));
}

TEST(StudioEntities_NewSaveAndHotReload) {
	const char *saves = "build/tests/tmp/voxelia/studio_entities_reload";
	std::filesystem::remove_all(saves);
	voxel::Game game;
	(void)game.LoadMods(EntityOptions(saves));
	REQUIRE(game.OpenWorld("entites", Some(uint32_t(5)), String("core:flat")).IsOk());
	(void)game.JoinPlayer("Testeur");
	for (int i = 0; i < 60; ++i)
		game.Tick(1.f / 60.f);
	// Nouvelle créature, écrite dans un fichier temporaire.
	const std::filesystem::path dir("build/tests/tmp/voxelia/studio_entities_reload/mod");
	std::filesystem::create_directories(dir);
	const String file((dir / "studio.json").string().c_str());
	EntityDocument doc = EntityDocument::New(String("golem_test"), String("core"), String(dir.string().c_str()), file);
	CHECK_EQ(doc.Id(), String("core:golem_test"));
	CHECK(doc.Validate(game.Content(), game.Scripts().BehaviorIds()).empty());
	REQUIRE(doc.Save().IsOk());
	CHECK(std::filesystem::exists(dir / "studio.json"));
	// À chaud : la créature existe, une apparition prend la définition.
	REQUIRE(game.ReloadDefinition(doc.Json(), doc.Context()).IsOk());
	const ecs::Entity first = game.SpawnEntity("core:golem_test", {4.f, 13.f, 4.f});
	REQUIRE(first.Valid());
	CHECK(std::fabs(game.Registry().GetComponent<voxel::Collider>(first).Unwrap()->size.y - 1.8f) < 1e-4f);
	// Modifiée et rechargée : une NOUVELLE apparition la prend ; l'ancienne
	// garde ses composants (copiés à l'apparition), et son pointeur de
	// définition reste valide.
	doc.SetField("physics.collider", json::Parse(String("[1.2, 2.5, 1.2]")).Value());
	doc.SetField("health", json::MakeNum(40.0));
	REQUIRE(doc.SetBehaviorParam(0, String("pause"), json::MakeNum(1.5)).IsOk()); // « wander » décide plus souvent (3 s par défaut)
	REQUIRE(game.ReloadDefinition(doc.Json(), doc.Context()).IsOk());
	const ecs::Entity second = game.SpawnEntity("core:golem_test", {6.f, 13.f, 4.f});
	REQUIRE(second.Valid());
	CHECK(std::fabs(game.Registry().GetComponent<voxel::Collider>(second).Unwrap()->size.y - 2.5f) < 1e-4f);
	CHECK(std::fabs(game.Registry().GetComponent<voxel::Health>(second).Unwrap()->max - 40.f) < 1e-4f);
	CHECK(std::fabs(game.Registry().GetComponent<voxel::Collider>(first).Unwrap()->size.y - 1.8f) < 1e-4f);
	auto kind = game.Registry().GetComponent<voxel::EntityKind>(first);
	REQUIRE(kind.IsSome() && kind.Unwrap()->def != nullptr);
	CHECK(std::fabs(kind.Unwrap()->def->health - 40.f) < 1e-4f);
	// L'IA tourne : « wander » fait bouger la créature en quelques secondes.
	const math::FVector3 start = game.Registry().GetComponent<voxel::Transform>(second).Unwrap()->position;
	for (int i = 0; i < 900; ++i)
		game.Tick(1.f / 60.f);
	const math::FVector3 end = game.Registry().GetComponent<voxel::Transform>(second).Unwrap()->position;
	CHECK((end - start).Length() > 0.1f);
	game.CloseWorld();
}
