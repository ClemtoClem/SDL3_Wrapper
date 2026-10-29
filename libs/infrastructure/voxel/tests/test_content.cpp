// Tests — définitions de contenu (voxel/content.hpp) et lecture JSON
// permissive (voxel/json.hpp), avec les exemples du cahier des charges.
#include "test.hpp"

#include <voxel/content.hpp>
#include <voxel/json.hpp>

using namespace voxel;

namespace {

NodePtr Def(const char *text) {
	auto parsed = json::Parse(String(text));
	if (parsed.IsError())
		std::printf("JSON: %s\n", parsed.Error().CStr());
	return parsed.IsOk() ? parsed.Unwrap() : nullptr;
}

const char *GRASS = R"({
  "schemaVersion": 1,
  "id": "core:grass",
  "type": "block",
  "size": {"x": 1, "y": 1, "z": 1 },
  "displayName": "Grass",
  "tags": ["terrain"],
  "graphics": {
    "textures": { "top": "core:grass_top", "bottom": "core:dirt", "side": "core:grass_side" },
    "material": "OPAQUE"
  },
  "physics": { "solid": true, "collision": { "shape": "FULL_CUBE" }, "surface": { "slipperiness": 0.0 } },
  "gameplay": { "hardness": 1.5 }
})";

const char *WATER = R"({
  "schemaVersion": 1, "id": "core:water", "type": "block", "size": {"x": 1, "y": 1, "z": 1 },
  "displayName": "Water", "tags": ["fluid"],
  "graphics": { "texture": "core:water", "material": "TRANSPARENT", "backface_culling": false },
  "physics": { "solid": false, "collision": { "shape": "FULL_CUBE" }, "surface": { "slipperiness": 0.05 } },
  "gameplay": { "hardness": 100.0 }
})";

const char *SAND = R"({
  "schemaVersion": 1, "id": "core:sand", "type": "block", "size": {"x": 1, "y": 1, "z": 1 },
  "displayName": "Sand", "tags": ["terrain", "granular"],
  "graphics": { "texture": "core:sand", "material": "OPAQUE" },
  "physics": { "solid": true, "collision": { "shape": "FULL_CUBE" },
    "surface": { "slipperiness": 0.05, "walkable": true },
    "structural": { "requiresSupport": false, "supportStrength": 0.0 } },
  "simulation": { "falling": { "enabled": true, "behavior": "FALL_AS_BLOCK" } },
  "gameplay": { "hardness": 0.6 }
})";

const char *ICE = R"({
  "schemaVersion": 1, "id": "core:ice", "type": "block", "size": {"x": 1, "y": 1, "z": 1 },
  "displayName": "Ice", "tags": ["terrain", "cold"],
  "graphics": { "texture": "core:ice", "material": "TRANSLUCENT", "backface_culling": false },
  "physics": { "solid": true, "collision": { "shape": "FULL_CUBE" }, "surface": { "slipperiness": 0.95, "walkable": true } },
  "gameplay": { "hardness": 0.5 }
})";

// Exemples nodebox tels qu'écrits (clé sans guillemets, virgule finale).
const char *SHELF_FROM_NODEBOX = R"({
    id: "core:shelf",
    "type": "nodebox",
    "size": {"x": 1, "y": 1, "z": 1 },
    "nodebox": {
        "type": "FIXED",
        "fixed": [
            [-0.5,-0.6,-0.5,0.5,-0.5,0.5],
            [0.45,-0.5,-0.5,0.5,0.5,0.5],
            [-0.5,-1.5,-0.5,0.5,-0.6,-0.45]
        ]
    },
    "physics": { "collision": { "shape": "FROM_NODEBOX" } }
})";

const char *SHELF_FIXED = R"({
    id: "core:shelf_fixed",
    "type": "nodebox",
    "size": {"x": 1, "y": 1, "z": 1 },
    "nodebox": {
        "shape": "FIXED",
        "fixed": [
            [-0.5,-0.6,-0.5,0.5,-0.5,0.5],
            [0.45,-0.5,-0.5,0.5,0.5,0.5],
            [-0.5,-1.5,-0.5,0.5,-0.6,-0.45]
        ]
    },
    "physics": { "collision": { "shape": "FIXED", "fixed": [
            [-0.5,-0.6,-0.5,0.5,-0.5,0.5],
            [0.45,-0.5,-0.5,0.5,0.5,0.5],
        ], } }
})";

} // namespace

TEST(Json_RelaxedSyntaxIsNormalized) {
	auto parsed = json::Parse(String("{ type: 'obj', path: \"a.obj\", // commentaire\n list: [1, 2,], nested: {a: 1,}, }"));
	REQUIRE(parsed.IsOk());
	CHECK(json::Str(parsed.Value(), "type") == "obj");
	CHECK(json::Str(parsed.Value(), "path") == "a.obj");
	CHECK_EQ(json::NumRows(parsed.Value(), "list").front().size(), size_t(2));
	CHECK_EQ(json::Int(parsed.Value(), "nested.a"), 1);
	// Les « : » et « , » dans les chaînes ne sont pas touchés.
	auto text = json::Parse(String("{ id: \"core:a, b\" }"));
	REQUIRE(text.IsOk());
	CHECK(json::Str(text.Value(), "id") == "core:a, b");
}

TEST(Content_SpecificationBlocksAreReadAsWritten) {
	ContentRegistry content;
	for (const char *text : {GRASS, WATER, SAND, ICE})
		REQUIRE(content.Load(Def(text), {}).IsOk());
	const BlockDef *grass = content.FindBlock("core:grass");
	REQUIRE(grass != nullptr);
	CHECK(grass->numeric != AIR);
	CHECK(grass->look.Texture(Face::TOP) == "core:grass_top");
	CHECK(grass->look.Texture(Face::BOTTOM) == "core:dirt");
	CHECK(grass->look.Texture(Face::NORTH) == "core:grass_side");
	CHECK(grass->IsOpaqueCube());
	CHECK_NEAR(grass->hardness, 1.5f, 1e-6f);
	CHECK(content.FindItem("core:grass") != nullptr); // objet posable automatique

	const BlockDef *water = content.FindBlock("core:water");
	CHECK(!water->solid && water->fluid && water->replaceable);
	CHECK(water->look.material == RenderMaterial::TRANSPARENT && !water->look.backfaceCulling);
	CHECK(water->drops.IsEmpty());

	const BlockDef *sand = content.FindBlock("core:sand");
	CHECK(sand->falling && sand->fallingBehavior == "FALL_AS_BLOCK");
	CHECK(sand->HasTag("granular"));

	const BlockDef *ice = content.FindBlock("core:ice");
	CHECK_NEAR(ice->slipperiness, 0.95f, 1e-6f);
	CHECK(ice->look.material == RenderMaterial::TRANSLUCENT);
}

TEST(Content_NodeBoxesAndCollisionShapes) {
	ContentRegistry content;
	REQUIRE(content.Load(Def(SHELF_FROM_NODEBOX), {}).IsOk());
	REQUIRE(content.Load(Def(SHELF_FIXED), {}).IsOk());
	const BlockDef *shelf = content.FindBlock("core:shelf");
	REQUIRE(shelf != nullptr);
	CHECK(shelf->shape.kind == ShapeKind::NODEBOX);
	REQUIRE_EQ(shelf->shape.boxes.size(), size_t(3));
	// Convention centrée → locale : [-0.5,-0.6,-0.5] devient [0,-0.1,0].
	CHECK_NEAR(shelf->shape.boxes[0].min.y, -0.1f, 1e-5f);
	CHECK_NEAR(shelf->shape.boxes[1].min.x, 0.95f, 1e-5f);
	CHECK(shelf->collision == CollisionKind::BOXES);
	CHECK_EQ(shelf->collisionBoxes.size(), size_t(3));
	const BlockDef *fixed = content.FindBlock("core:shelf_fixed");
	CHECK_EQ(fixed->shape.boxes.size(), size_t(3));
	CHECK_EQ(fixed->collisionBoxes.size(), size_t(2)); // sa propre liste
}

TEST(Content_AllKindsMigrationsAndValidation) {
	ContentRegistry content;
	ContentRegistry::LoadContext ctx{"demo", "/mods/demo", "test"};
	// Schéma 0 (champs à la Minetest) : migré.
	REQUIRE(content.Load(Def(R"({"id": "stone", "tiles": ["stone"], "description": "Pierre"})"), ctx).IsOk());
	const BlockDef *stone = content.FindBlock("demo:stone");
	REQUIRE(stone != nullptr);
	CHECK(stone->look.Texture(Face::EAST) == "demo:stone");
	CHECK(stone->displayName == "Pierre");
	CHECK_EQ(json::Int(stone->raw, "schemaVersion"), SCHEMA_VERSION);

	REQUIRE(content.Load(Def(R"({"type": "texture", "id": "stone", "path": "textures/stone.png"})"), ctx).IsOk());
	CHECK(content.FindTexture("demo:stone")->path == "/mods/demo/textures/stone.png");
	REQUIRE(content.Load(Def(R"({"type": "tool", "id": "pick", "tool": {"kind": "pickaxe", "speed": 4, "durability": 60}})"), ctx).IsOk());
	CHECK(content.FindItem("demo:pick")->tool.has_value());
	CHECK_EQ(content.FindItem("demo:pick")->maxStack, 1);
	REQUIRE(content.Load(Def(R"({"type": "creature", "id": "pig", "physics": {"collider": [0.9, 0.9, 0.9], "mass": 20},
		"behaviors": ["wander", {"id": "follow", "item": "demo:carrot"}], "drops": [["demo:stone", 2]],
		"sounds": {"idle": "oink"}})"), ctx).IsOk());
	const EntityDef *pig = content.FindEntity("demo:pig");
	REQUIRE(pig != nullptr);
	CHECK(pig->kind == "creature");
	CHECK_NEAR(pig->colliderSize.x, 0.9f, 1e-6f);
	CHECK_EQ(pig->behaviors.size(), size_t(2));
	CHECK(pig->behaviors[0].id == "wander"); // intégré : non qualifié
	CHECK(pig->sounds.Get("idle") == "demo:oink");
	REQUIRE(content.Load(Def(R"({"type": "recipe", "id": "pick", "pattern": ["SSS", " | ", " | "],
		"key": {"S": "stone", "|": {"tag": "stick"}}, "result": {"item": "pick", "count": 1}})"), ctx).IsOk());
	CHECK(content.FindRecipe("demo:pick")->kind == RecipeKind::SHAPED);
	REQUIRE(content.Load(Def(R"({"type": "structure", "id": "pillar", "palette": {"#": "stone"},
		"layers": [["#"], ["#"], ["#"]]})"), ctx).IsOk());
	CHECK_EQ(content.FindStructure("demo:pillar")->size.y, 3);
	REQUIRE(content.Load(Def(R"({"type": "config", "values": {"player": {"speed": 5.5}}})"), ctx).IsOk());
	CHECK_NEAR(content.ConfigNum("player.speed"), 5.5, 1e-9);
	CHECK(content.Load(Def(R"({"type": "spaceship", "id": "x"})"), ctx).IsError());
	CHECK(content.Load(Def(R"({"type": "recipe", "id": "broken"})"), ctx).IsError());

	const std::vector<String> warnings = content.Validate();
	// Son « demo:oink » et tag « stick » inconnus, objet « carrot » non défini (paramètre libre : pas vérifié).
	bool sound = false, tag = false;
	for (const String &w : warnings) {
		sound = sound || w.Contains("demo:oink");
		tag = tag || w.Contains("stick");
	}
	CHECK(sound && tag);
}
