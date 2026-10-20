#pragma once
/**
 * ScriptHost — les scripts des mods : chargement, VALIDATION (compilation
 * avant exécution, erreurs avec fichier:ligne:colonne), exécution, et
 * intégration avec les entités, le monde et la console.
 *
 * API offerte aux scripts :
 *
 *   voxel.register(def)                 définition (même schéma que le JSON)
 *   voxel.on(event, fn)                 événement ; `fn` rend false pour ANNULER
 *        "block_placed" (pos, block, actor)   "block_broken" (pos, block, actor)
 *        "block_use" (pos, block, actor)      "entity_spawned" (e)
 *        "entity_died" (e, kind)              "entity_damaged" (e, amount, source)
 *        "entity_interact" (e, actor)         "entity_touch_block" (e, pos, block, landed)
 *        "item_use" (actor, item)             "player_join" (player)
 *        "chunk_generated" (cx, cy, cz)       "tick" (dt)
 *   voxel.command(name, usage, help, fn(sender, args))  commande de console
 *   voxel.behavior(id, fn(e, state, dt))  comportement de créature
 *   voxel.generator(id, fn(gen))          générateur de terrain ("script")
 *   voxel.system(name, fn(dt))            système exécuté à chaque image
 *
 *   world.get(x, y, z) / world.set(x, y, z, id) / world.param(x, y, z)
 *   world.surface(x, z) / world.biome(x, z) / world.structure(id, x, y, z [, rot])
 *   world.spawn(id, x, y, z) → e         world.drop(item, count, x, y, z)
 *   world.entities(x, y, z, r [, kind]) → [e…]   world.raycast(x,y,z, dx,dy,dz, dist)
 *
 *   entity.kind(e) / entity.pos(e) / entity.set_pos(e, x, y, z) / entity.forward(e)
 *   entity.velocity(e) / entity.set_velocity(e, x, y, z) / entity.impulse(e, x, y, z)
 *   entity.move(e, dx, dz [, speed]) / entity.jump(e) / entity.look(e, yaw)
 *   entity.health(e) / entity.damage(e, n) / entity.heal(e, n) / entity.remove(e)
 *   entity.prop(e, key) / entity.set_prop(e, key, value)    propriétés libres
 *   entity.give(e, item, count) / entity.take(e, item, count) / entity.count(e, item)
 *   entity.held(e) / entity.nearest_player(e, range) / entity.sound(e, event)
 *   entity.exists(e) / entity.players()
 *
 *   content.get(kind, id) → table / content.list(kind) → [id…] / content.config(path)
 *   game.log(msg) / game.say(msg) / game.time() / game.set_time(t)
 *   game.sound(id, x, y, z) / game.seed() / game.execute(line)
 *
 * Une entité est désignée par un entier opaque (identifiant + génération) :
 * une entité détruite n'est jamais confondue avec celle qui réutilise sa
 * place.
 */
#include <map>
#include <memory>
#include <vector>

#include <ecs/ecs.hpp>
#include <script/script.hpp>

#include "components.hpp"
#include "world.hpp"
#include "worldgen.hpp"

namespace voxel {

class Game;

class ScriptHost {
public:
	explicit ScriptHost(Game &game);
	~ScriptHost();

	/// Compile sans exécuter : NONE si le script est valide.
	[[nodiscard]] Option<String> Validate(const String &source, const String &name) const;
	/// Valide puis exécute un fichier de mod (sous l'identité `mod`).
	Result<bool, String> RunFile(const String &path, const String &mod);
	/// Exécute un extrait (console « /lua … »).
	Result<String, String> RunSource(const String &source, const String &name = String("console"));

	/// Événement : rend faux si un gestionnaire a rendu false (annulation).
	bool Emit(const String &event, std::vector<script::Value> args);
	[[nodiscard]] bool HasHandlers(const String &event) const;

	/// Comportement scripté (BehaviorDef "script" ou voxel.behavior) ; faux s'il n'existe pas.
	bool RunBehavior(ecs::Entity entity, BehaviorState &state, float dt);
	/// Générateur scripté pour le monde.
	[[nodiscard]] TerrainGenerator::ScriptFn Generator() ;
	/// Systèmes des mods (appelés chaque image).
	void RunSystems(float dt);

	[[nodiscard]] script::Value EntityValue(ecs::Entity e) const;
	[[nodiscard]] ecs::Entity EntityOf(const script::Value &v) const;
	[[nodiscard]] script::Interpreter &Vm() noexcept { return m_vm; }
	[[nodiscard]] const String &CurrentMod() const noexcept { return m_currentMod; }

private:
	void InstallApi();
	void InstallWorldApi();
	void InstallEntityApi();
	void InstallContentApi();
	void Report(const String &where, const script::ScriptError &error);
	Result<script::Value, script::ScriptError> Call(const script::Value &fn, std::vector<script::Value> args);

	Game &m_game;
	script::Interpreter m_vm;
	String m_currentMod = "core";
	std::map<std::string, std::vector<script::Value>> m_handlers;
	std::map<std::string, script::Value> m_behaviors, m_generators;
	std::vector<std::pair<String, script::Value>> m_systems;
	// Générateur scripté en cours : le chunk à remplir.
	Chunk *m_genChunk = nullptr;
	World *m_genWorld = nullptr;
};

} // namespace voxel
