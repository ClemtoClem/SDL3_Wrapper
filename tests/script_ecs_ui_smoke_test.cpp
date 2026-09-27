// Tests unitaires — data::script : enveloppes de l'ECS (script_ecs.hpp), de
// l'interface utilisateur (script_ui.hpp) et de la génération procédurale
// (script_generator.hpp).
//
// Aucune fenêtre : l'interface est construite sur un registre, une mise en
// page et une fabrique (pilote vidéo "dummy"), comme dans les tests de ui::.
#define USE_TEST

#include "core/test.hpp"
#include "data/script.hpp"
#include "data/script/script_ecs.hpp"
#include "data/script/script_generator.hpp"
#include "data/script/script_ui.hpp"

#include <cstdlib>

using namespace data::script;

namespace {

Value Eval(Interpreter &vm, const char *source) {
	auto result = vm.Run(StringView(source));
	if (result.IsError()) {
		test::ReportFailure(__FILE__, __LINE__, result.Error().Format().CStr());
		return Value::Nil();
	}
	return result.Value();
}

String EvalError(Interpreter &vm, const char *source) {
	auto result = vm.Run(StringView(source));
	return result.IsOk() ? String() : result.Error().Format();
}

/// Un composant C++ de l'« hôte ».
struct Health {
	int points = 100;
};

/// Interface sans fenêtre : registre + mise en page + fabrique, et une
/// racine qui remplit l'écran.
struct UiHarness {
	ecs::ArchetypeRegistry registry;
	ui::LayoutSystem layout;
	ui::UiFactory factory{registry, layout};
	ui::StyleSystem styles;
	ecs::Entity root{};

	UiHarness() {
		setenv("SDL_VIDEODRIVER", "dummy", 0);
		styles.sheet = &factory.sheet;
		ui::WidgetBuilder column = factory.Column();
		column.Size(800.f, 600.f);
		root = column.Spawn();
	}
	[[nodiscard]] std::shared_ptr<UiBinding> Binding() {
		auto binding = std::make_shared<UiBinding>();
		binding->registry = &registry;
		binding->factory = &factory;
		binding->layout = &layout;
		binding->root = [this]() { return root; };
		return binding;
	}
	void Layout() {
		styles.Resolve(registry);
		layout.MarkDirty();
		layout.Run(registry, 800.f, 600.f);
	}
	[[nodiscard]] std::vector<ecs::Entity> Children(ecs::Entity e) {
		auto list = registry.GetComponent<ui::UiChildren>(e);
		return list.IsSome() ? list.Unwrap()->list : std::vector<ecs::Entity>{};
	}
};

} // namespace

// ============================================================================
// ECS
// ============================================================================

TEST(ScriptEcs, WorldsSpawnQueryAndRunSystems) {
	Interpreter vm;
	InstallEcsLibrary(vm);
	const char *source = "let monde = ecs.world()\n"
						 "let a = monde.spawn({pos: [0, 0], vit: [1, 2]})\n"
						 "let b = monde.spawn({pos: [5, 5]})\n"
						 "monde.system(\"mouvement\", [\"pos\", \"vit\"], fn(e, pos, vit, dt) {\n"
						 "  pos[0] = pos[0] + vit[0] * dt\n  pos[1] = pos[1] + vit[1] * dt\n})\n"
						 "monde.run(2)\n"
						 "return [a.get(\"pos\"), b.get(\"pos\"), monde.count(\"pos\"), monde.count(\"pos\", \"vit\"), "
						 "len(monde), a.has(\"vit\"), b.has(\"vit\"), a.components()]";
	EXPECT_EQ(Eval(vm, source).ToDisplayString(), "[[2, 4], [5, 5], 2, 1, 2, true, false, [pos, vit]]");
}

TEST(ScriptEcs, EntitiesComponentsAndLifetime) {
	Interpreter vm;
	InstallEcsLibrary(vm);
	const char *source = "let m = ecs.world()\n"
						 "let e = m.spawn()\n"
						 "e[\"nom\"] = \"slime\"\n"
						 "e.set(\"pv\", 3)\n"
						 "let id = e.id\n"
						 "let même = m.entity(id)\n"
						 "let noms = []\n"
						 "m.query(\"nom\", fn(x, nom) { noms.append(nom) })\n"
						 "let avant = [e == même, e[\"pv\"], e.get(\"absent\", 0), noms]\n"
						 "e.remove(\"pv\")\n"
						 "e.despawn()\n"
						 "return [avant, e.alive(), m.entity(id), len(m)]";
	EXPECT_EQ(Eval(vm, source).ToDisplayString(), "[[true, 3, 0, [slime]], false, nil, 0]");
	// Une requête matérialisée : un système peut détruire des entités en route.
	EXPECT_EQ(Eval(vm, "let w = ecs.world()\nfor (i in range(5)) { w.spawn({n: i}) }\n"
					   "w.query(\"n\", fn(e, n) { if (n % 2 == 0) { e.despawn() } })\nreturn w.count(\"n\")")
				  .ToDisplayString(),
			  "2");
	EXPECT_TRUE(EvalError(vm, "let w1 = ecs.world()\nlet w2 = ecs.world()\nw2.get(w1.spawn(), \"x\")").Contains("autre monde"));
}

TEST(ScriptEcs, ResourcesAndFlows) {
	Interpreter vm;
	InstallEcsLibrary(vm);
	EXPECT_EQ(Eval(vm, "let w = ecs.world()\nw.set_resource(\"gravité\", 9.8);\n({pos: [1, 1]}) -> w\n"
					   "return [w.resource(\"gravité\"), w.resource(\"absent\", 0), len(w), w.systems()]")
				  .ToDisplayString(),
			  "[9.8, 0, 1, []]");
}

TEST(ScriptEcs, TheHostExposesItsCppComponents) {
	ecs::ArchetypeRegistry registry;
	const ecs::Entity hero = registry.Spawn();
	registry.AddComponent(hero, Health{42});
	auto world = std::make_shared<EcsWorld>(registry);
	world->BindComponent<Health>(
		String("health"), [](const Health &h) { return Value::Int(h.points); },
		[](const Value &v, Health &h) -> Option<String> {
			if (!v.IsNumber())
				return Some(String("nombre attendu"));
			h.points = int(v.AsInt64());
			return NONE;
		});
	Interpreter vm;
	InstallEcsLibrary(vm, world);
	EXPECT_EQ(Eval(vm, "let h = ecs.host()\nlet e = h.query(\"health\")[0]\nlet avant = e.get(\"health\")\n"
					   "e.set(\"health\", avant - 2)\ne.set(\"note\", \"héros\")\n"
					   "return [avant, e.get(\"health\"), e.components(), h.host_components()]")
				  .ToDisplayString(),
			  "[42, 40, [health, note], [health]]");
	EXPECT_EQ(registry.GetComponent<Health>(hero).Unwrap()->points, 40); // écrit dans le composant C++
	EXPECT_TRUE(EvalError(vm, "ecs.host().query(\"health\")[0].set(\"health\", \"x\")").Contains("nombre attendu"));
	// Sans monde d'hôte : ecs.host() vaut nil.
	Interpreter bare;
	InstallEcsLibrary(bare);
	EXPECT_TRUE(Eval(bare, "return ecs.host()").IsNil());
}

// ============================================================================
// Interface utilisateur
// ============================================================================

TEST(ScriptUi, WidgetsWorkWithoutAScreen) {
	Interpreter vm;
	InstallUiLibrary(vm, nullptr);
	const char *source = "let clics = 0\n"
						 "let menu = ui.column({gap: 8})\n"
						 "let titre = ui.label(\"Menu\", {parent: menu, font_size: 30})\n"
						 "let jouer = ui.button(\"Jouer\", fn(b) { clics += 1; b.text = \"Encore\" }, {parent: menu, name: \"jouer\"})\n"
						 "let barre = ui.progress(0.25, 1, {parent: menu})\n"
						 "jouer.click()\njouer.click()\n"
						 "barre <- 0.75\n"
						 "return [ui.available(), clics, jouer.text, barre.value, len(menu), ui.find(\"jouer\") == jouer, "
						 "titre.on_screen]";
	EXPECT_EQ(Eval(vm, source).ToDisplayString(), "[false, 2, Encore, 0.75, 3, true, false]");
	EXPECT_TRUE(EvalError(vm, "ui.label(\"x\", {inconnue: 1})").Contains("option `inconnue` inconnue"));
	EXPECT_TRUE(EvalError(vm, "ui.label(\"x\", {bg: \"rouge\"})").Contains("couleur"));
}

TEST(ScriptUi, WidgetsLiveInTheHostInterface) {
	UiHarness h;
	Interpreter vm;
	auto binding = h.Binding();
	InstallUiLibrary(vm, binding);
	Eval(vm, "var reçu = nil\n"
			 "let menu = ui.column({anchor: \"center\", gap: 10, pad: 16, bg: \"#101820e0\", radius: 8})\n"
			 "let titre = ui.label(\"Titre\", {parent: menu, bold: true, color: [255, 220, 120]})\n"
			 "let ok = ui.button(\"OK\", fn(b) { reçu = b.text }, {parent: menu, w: 200})\n"
			 "let jauge = ui.progress(0, 10, {parent: menu, w: \"80%\"})\n"
			 "titre.set_text(\"Nouveau titre\")\njauge.value = 4\n");
	h.Layout();
	const std::vector<ecs::Entity> top = h.Children(h.root);
	ASSERT_TRUE(top.size() == 1u);
	const std::vector<ecs::Entity> items = h.Children(top[0]);
	ASSERT_TRUE(items.size() == 3u);
	EXPECT_EQ(h.registry.GetComponent<ui::UiLabel>(items[0]).Unwrap()->text, "Nouveau titre");
	EXPECT_EQ(h.registry.GetComponent<ui::UiButton>(items[1]).Unwrap()->text, "OK");
	EXPECT_TRUE(h.registry.GetComponent<ui::UiProgress>(items[2]).Unwrap()->value == 4.f);
	EXPECT_TRUE(Eval(vm, "return ui.available()").AsBoolean());

	// Un clic de l'utilisateur : mis en file, exécuté par Flush().
	auto callbacks = h.registry.GetComponent<ui::UiCallbacks>(items[1]);
	ASSERT_TRUE(callbacks.IsSome() && callbacks.Unwrap()->onClick);
	callbacks.Unwrap()->onClick();
	EXPECT_TRUE(Eval(vm, "return reçu").IsNil());
	EXPECT_EQ(binding->Flush(), size_t(1));
	EXPECT_EQ(Eval(vm, "return reçu").ToDisplayString(), "OK");

	// Masquer, retirer, tout effacer.
	Eval(vm, "titre.hide()");
	EXPECT_TRUE(h.registry.HasComponent<ui::UiHidden>(items[0]));
	Eval(vm, "ok.remove()");
	EXPECT_FALSE(h.registry.IsAlive(items[1]));
	Eval(vm, "ui.clear()");
	EXPECT_TRUE(h.Children(h.root).empty());
	// L'écran a disparu (entités mortes) : les widgets survivent en objets.
	EXPECT_EQ(Eval(vm, "titre.set_text(\"hors écran\")\nreturn [titre.text, titre.on_screen]").ToDisplayString(),
			  "[hors écran, false]");
}

TEST(ScriptUi, CallbacksNeverOutliveTheirInterpreter) {
	UiHarness h;
	auto binding = h.Binding();
	ecs::Entity button{};
	{
		Interpreter vm;
		InstallUiLibrary(vm, binding);
		Eval(vm, "ui.button(\"x\", fn(b) { print(\"clic\") })");
		button = h.Children(h.root)[0];
		h.registry.GetComponent<ui::UiCallbacks>(button).Unwrap()->onClick();
	}
	// L'interpréteur est détruit : le rappel en file ne fait plus rien.
	EXPECT_EQ(binding->Flush(), size_t(1));
	h.registry.GetComponent<ui::UiCallbacks>(button).Unwrap()->onClick();
	EXPECT_EQ(binding->Flush(), size_t(1));
}

// ============================================================================
// Génération procédurale
// ============================================================================

TEST(ScriptGen, NoiseHeightmapsAndTerrain) {
	Interpreter vm;
	InstallGeneratorLibrary(vm);
	EXPECT_EQ(Eval(vm, "let b = gen.noise({type: \"simplex\", fractal: \"ridged\", seed: 3, frequency: 0.05})\n"
					   "let c = gen.heightmap(32, 32).fill(b).normalize()\n"
					   "let t = c.copy().terrace(4, 1)\n"
					   "return [c.width, c.min(), c.max(), b.sample(1, 2) == b.sample(1, 2), t.get(5, 5) * 4 == math.round(t.get(5, 5) * 4)]")
				  .ToDisplayString(),
			  "[32, 0, 1, true, true]");
	EXPECT_EQ(Eval(vm, "let a = gen.heightmap(4, 4, 1)\nlet b = gen.heightmap(4, 4, 2)\nlet s = a + b\nlet d = s * 2 - 1\n"
					   "return [s.get(0, 0), d.get(3, 3), (a + 0.5).mean()]")
				  .ToDisplayString(),
			  "[3, 5, 1.5]");
	EXPECT_EQ(Eval(vm, "let t = gen.terrain({width: 33, height: 33, seed: 4, island: 2, erosion: {droplets: 500}, sea_level: 0.25})\n"
					   "let m = t.mesh({step: 2, biomes: 0.3})\nreturn [t.width, t.min() >= 0.25, m.vertex_count, m.triangle_count, "
					   "len(m.colors) == len(m.positions)]")
				  .ToDisplayString(),
			  "[33, true, 289, 512, true]");
	EXPECT_EQ(Eval(vm, "return [gen.biome(0.95, 3), gen.biome(0.5, 60)]").ToDisplayString(), "[neige, falaise]");
	EXPECT_TRUE(EvalError(vm, "gen.noise({typo: 1})").Contains("option `typo` inconnue"));
	EXPECT_TRUE(EvalError(vm, "gen.noise({type: \"bleu\"})").Contains("type de bruit inconnu"));
}

TEST(ScriptGen, DungeonsScatteringAndGrammars) {
	Interpreter vm;
	InstallGeneratorLibrary(vm);
	EXPECT_EQ(Eval(vm, "let d = gen.dungeon({width: 41, height: 31, seed: 5})\n"
					   "let e = d.entrance\nlet s = d.exit\n"
					   "return [d.width, len(d.rows()), len(d.rooms()) >= 3, d.get(e[0], e[1]), d.distance(e[0], e[1], s[0], s[1]) > 0, "
					   "d.count(\"exit\")]")
				  .ToDisplayString(),
			  "[41, 31, true, entrance, true, 1]");
	EXPECT_EQ(Eval(vm, "let m = gen.maze(21, 21, {algorithm: \"prim\", seed: 2})\nlet c = gen.caves({seed: 3})\nlet b = gen.bsp({seed: 4})\n"
					   "return [m.count(\"wall\") > 0, c.count(\"floor\") > 100, len(b.rooms()) >= 2]")
				  .ToDisplayString(),
			  "[true, true, true]");
	EXPECT_EQ(Eval(vm, "let p = gen.poisson(50, 50, 5, 1, fn(x, y) { return x > 25 })\nlet gauche = 0\n"
					   "for (q in p) { if (q[0] < 25) { gauche += 1 } }\nreturn [len(p) > 10, gauche]")
				  .ToDisplayString(),
			  "[true, 0]");
	EXPECT_EQ(Eval(vm, "return [gen.lsystem(\"A\", {A: \"AB\", B: \"A\"}, 4), len(gen.turtle(\"F[+F]F\", {angle: 90}))]").ToDisplayString(),
			  "[ABAABABA, 3]");
	EXPECT_EQ(Eval(vm, "let rows = gen.wfc([{name: \"a\", sides: [\"x\", \"x\", \"x\", \"x\"]}], 3, 2)\nreturn rows").ToDisplayString(),
			  "[[a, a, a], [a, a, a]]");
	EXPECT_TRUE(EvalError(vm, "gen.wfc([{name: \"a\", sides: [\"a\", \"b\", \"c\", \"b\"]}], 3, 3)").Contains("contradiction"));
	EXPECT_TRUE(Eval(vm, "let n = gen.names([\"aldoria\", \"belmora\", \"cendrial\", \"dravonne\"], 2, 7)\nlet x = n.generate(4, 10)\n"
						 "return len(x) >= 4 and len(x) <= 10")
					.AsBoolean());
	// Génération sur un fil `async` (écran de chargement).
	EXPECT_EQ(Eval(vm, "async fn générer() { return gen.dungeon({seed: 8}).count(\"exit\") }\nreturn await générer()").ToDisplayString(),
			  "1");
}

TEST(ScriptUi, UnreferencedWidgetsStillCallBack) {
	// Le cas le plus courant d'un menu : le résultat de `ui.button` n'est
	// gardé nulle part — le bouton doit quand même appeler son rappel.
	UiHarness h;
	Interpreter vm;
	auto binding = h.Binding();
	InstallUiLibrary(vm, binding);
	Eval(vm, "var clics = 0\n"
			 "fn creer() {\n"
			 "    let panneau = ui.column({anchor: \"center\"})\n"
			 "    ui.button(\"Jouer\", fn(b) { clics += 1 }, {parent: panneau, name: \"jouer\"})\n"
			 "    ui.label(\"Titre\", {parent: panneau, name: \"titre\"})\n"
			 "}\n"
			 "creer()\n");
	h.Layout();
	const std::vector<ecs::Entity> top = h.Children(h.root);
	ASSERT_TRUE(top.size() == 1u);
	const std::vector<ecs::Entity> items = h.Children(top[0]);
	ASSERT_TRUE(items.size() == 2u);
	auto callbacks = h.registry.GetComponent<ui::UiCallbacks>(items[0]);
	ASSERT_TRUE(callbacks.IsSome() && callbacks.Unwrap()->onClick);
	callbacks.Unwrap()->onClick();
	callbacks.Unwrap()->onClick();
	EXPECT_EQ(binding->Flush(), size_t(2));
	EXPECT_EQ(Eval(vm, "return clics").ToDisplayString(), "2");
	// Retrouvables par leur nom, même sans référence dans le script.
	EXPECT_EQ(Eval(vm, "return ui.find(\"titre\").text").ToDisplayString(), "Titre");
	// Effacés : plus rien n'est gardé en vie.
	Eval(vm, "ui.clear()");
	EXPECT_TRUE(Eval(vm, "return ui.find(\"jouer\")").IsNil());
}

TEST(ScriptUi, LabelGrowsWithItsText) {
	UiHarness h;
	Interpreter vm;
	InstallUiLibrary(vm, h.Binding());
	Eval(vm, "let panneau = ui.column({anchor: \"center\", align: \"center\", w: 480})\n"
			 "let texte = ui.label(\"Préparation\", {parent: panneau, font_size: 14})\n");
	h.Layout();
	const ecs::Entity label = h.Children(h.Children(h.root)[0])[0];
	const float before = h.registry.GetComponent<ui::UiComputed>(label).Unwrap()->screen.w;
	Eval(vm, "texte.set_text(\"Plantation des arbres le long du circuit\")");
	h.Layout();
	const float after = h.registry.GetComponent<ui::UiComputed>(label).Unwrap()->screen.w;
	EXPECT_TRUE(after > before * 2.f);
}

int main() { return RUN_ALL_TESTS(); }
