// Définitions de voxel/game.hpp
#include <voxel/game.hpp>
#include <voxel/json.hpp>
#include <voxel/scripting.hpp>

#include <algorithm>
#include <cstring>
#include <chrono>
#include <cmath>
#include <map>
#include <sstream>

#include <ecs/events.hpp>
#include <ecs/relations.hpp>

namespace voxel {

using script::Value;

namespace {

std::vector<String> Split(const String &line) {
	std::vector<String> out;
	std::istringstream in(std::string(line.CStr(), line.GetSize()));
	std::string token;
	while (in >> token)
		out.push_back(String(token.c_str()));
	return out;
}

constexpr float EYE_HEIGHT = 1.62f;

} // namespace

Game::Game() : m_assets(std::make_unique<AssetManager>(1)) {
	m_scripts = std::make_unique<ScriptHost>(*this);
	InstallSystems();
	InstallCommands();
}

Game::~Game() {
	if (m_world)
		CloseWorld();
}

void Game::LogLine(const String &line) {
	m_log.push_back(line);
	if (m_log.size() > 500)
		m_log.erase(m_log.begin(), m_log.begin() + 100);
}

void Game::Message(const String &text, const String &channel) {
	ecs::EventsOf<GameMessage>(m_registry).Send(GameMessage{text, channel});
	LogLine(String::Format("[%s] %s", channel.CStr(), text.CStr()));
}

void Game::PlaySound(const String &sound, const math::FVector3 &position, float volume, bool positional) {
	if (sound.IsEmpty())
		return;
	float v = volume, pitch = 1.f;
	int variant = 0;
	if (const SoundDef *def = m_content.FindSound(sound)) {
		v *= def->volume;
		const uint32_t salt = uint32_t(++m_soundSerial);
		if (def->pitchVariation > 0.f)
			pitch += (RandomAt(salt, int(position.x * 10.f), int(position.y * 10.f), int(m_schedule.Frame()), 99) - 0.5f) * 2.f *
					 def->pitchVariation;
		if (def->paths.size() > 1)
			variant = int(HashPosition(salt, int(position.x), int(position.z), int(m_schedule.Frame()), 7) % def->paths.size());
	}
	ecs::EventsOf<SoundEvent>(m_registry).Send(SoundEvent{sound, position, v, pitch, positional, variant});
}

// ── Modes, difficulté, expérience ────────────────────────────────────────────

const char *GameModeId(GameMode mode) noexcept {
	switch (mode) {
	case GameMode::CREATIVE: return "creative";
	case GameMode::ADVENTURE: return "adventure";
	case GameMode::SPECTATOR: return "spectator";
	default: return "survival";
	}
}

GameMode GameModeFromId(const String &id) noexcept {
	return id == "creative" ? GameMode::CREATIVE : id == "adventure" ? GameMode::ADVENTURE : id == "spectator" ? GameMode::SPECTATOR
																												: GameMode::SURVIVAL;
}

const char *DifficultyId(Difficulty d) noexcept {
	switch (d) {
	case Difficulty::PEACEFUL: return "peaceful";
	case Difficulty::EASY: return "easy";
	case Difficulty::HARD: return "hard";
	default: return "normal";
	}
}

Difficulty DifficultyFromId(const String &id) noexcept {
	return id == "peaceful" ? Difficulty::PEACEFUL : id == "easy" ? Difficulty::EASY : id == "hard" ? Difficulty::HARD : Difficulty::NORMAL;
}

int XpLevel(int xp, float *progress) noexcept {
	int level = 0;
	int need = 7;
	while (xp >= need) {
		xp -= need;
		++level;
		need = 7 + level * 2;
	}
	if (progress)
		*progress = float(xp) / float(need);
	return level;
}

const StatusEffect *StatusEffects::Find(const String &id) const {
	for (const StatusEffect &e : list)
		if (e.id == id)
			return &e;
	return nullptr;
}

int StatusEffects::Level(const String &id) const {
	const StatusEffect *e = Find(id);
	return e ? e->level : 0;
}

void Game::PlayBlockSound(BlockId block, const char *event, const BlockPos &pos) {
	const BlockDef &def = m_content.Block(block);
	String sound = def.sounds.Get(event);
	if (sound.IsEmpty()) // son par défaut du jeu (configuration)
		sound = m_content.ConfigStr((String("sounds.") + String(event)).CStr());
	const String e(event);
	PlaySound(sound, pos.Center(), e == "step" ? 0.4f : e == "hit" ? 0.5f : 1.f);
}

// ── Contenu ──────────────────────────────────────────────────────────────────

ModReport Game::LoadMods(const GameOptions &options) {
	m_options = options;
	m_mods = ModLoader::LoadAll(m_content, options.modDirectories, options.disabledMods);
	// Scripts : dans l'ordre des mods, après les définitions JSON de chacun
	// (un script peut donc compléter ou modifier le contenu chargé).
	for (const ModInfo &mod : m_mods.mods)
		for (const String &path : mod.scripts)
			if (Result<bool, String> r = m_scripts->RunFile(path, mod.id); r.IsError())
				m_mods.errors.push_back(r.Error());
	m_content.Finalize();
	for (const String &w : m_content.Validate())
		m_mods.warnings.push_back(w);
	m_assets->Declare(m_content);
	for (const String &e : m_mods.errors)
		LogLine(String("[erreur] ") + e);
	for (const String &w : m_mods.warnings)
		LogLine(String("[avertissement] ") + w);
	LogLine(String::Format("%d mod(s), %d définition(s)", int(m_mods.mods.size()), int(m_content.DefinitionCount())));
	return m_mods;
}

// ── Monde ────────────────────────────────────────────────────────────────────

Result<bool, String> Game::OpenWorld(const String &name, Option<uint32_t> seed, const String &generator) {
	if (m_world)
		CloseWorld();
	m_save = std::make_unique<WorldSave>(m_content, m_options.savesDirectory + String("/") + name);
	Result<Option<WorldMetadata>, String> meta = m_save->LoadMetadata();
	if (meta.IsError())
		return Err(meta.Error());
	if (meta.Value().IsSome()) {
		m_metadata = meta.Value().Unwrap();
	} else {
		m_metadata = WorldMetadata{};
		m_metadata.name = name;
		m_metadata.seed = seed.IsSome() ? seed.Unwrap()
										: uint32_t(std::chrono::system_clock::now().time_since_epoch().count() & 0x7FFFFFFF);
		m_metadata.generator = generator.IsEmpty() ? m_content.ConfigStr("world.generator") : generator;
		m_metadata.gameTime = m_content.ConfigNum("world.startTime", 0.3) * double(m_options.dayLength);
	}
	ApplySettings();
	m_metadata.mods.clear();
	for (const ModInfo &mod : m_mods.mods)
		m_metadata.mods.push_back({mod.id, mod.version});
	WorldSettings settings{m_metadata.name, m_metadata.seed, m_metadata.generator, m_metadata.settings};
	m_world = std::make_unique<World>(m_content, settings);
	m_world->SetStorage(m_save.get());
	auto terrain = MakeGenerator(m_content, m_metadata.generator, m_metadata.seed, m_scripts->Generator());
	if (m_metadata.generator.IsEmpty())
		m_metadata.generator = terrain->Definition().id;
	m_world->SetGenerator(terrain);
	(void)m_world->onChunkLoaded.Connect([this](const ChunkPos &p) {
		if (m_scripts->HasHandlers("chunk_generated"))
			(void)m_scripts->Emit("chunk_generated", {Value::Int(p.x), Value::Int(p.y), Value::Int(p.z)});
	});
	Result<size_t, String> entities = m_save->LoadEntities(m_registry);
	if (entities.IsError())
		LogLine(String("[erreur] ") + entities.Error());
	for (const String &w : m_save->Warnings())
		LogLine(String("[sauvegarde] ") + w);
	LogLine(String::Format("Monde « %s » (graine %u, générateur %s)", m_metadata.name.CStr(), m_metadata.seed,
						   m_metadata.generator.CStr()));
	return Ok(true);
}

Result<bool, String> Game::Save() {
	if (!m_world)
		return Err(String("aucun monde ouvert"));
	const size_t chunks = m_world->SaveModifiedChunks();
	if (Result<bool, String> r = m_save->SaveEntities(m_registry); r.IsError())
		return r;
	std::vector<ecs::Entity> players;
	m_registry.Query<Player>([&](ecs::Entity e, Player &) { players.push_back(e); });
	for (ecs::Entity p : players)
		if (Result<bool, String> r = m_save->SavePlayer(m_registry, p); r.IsError())
			return r;
	if (Result<bool, String> r = m_save->SaveMetadata(m_metadata); r.IsError())
		return r;
	LogLine(String::Format("Monde enregistré (%d chunk(s) modifié(s))", int(chunks)));
	return Ok(true);
}

void Game::CloseWorld() {
	if (!m_world)
		return;
	(void)Save();
	m_registry.Clear();
	m_world.reset();
	m_save.reset();
	m_localPlayer = ecs::Entity{};
}

void Game::Tick(float dt) {
	if (!m_world)
		return;
	m_metadata.playTime += double(dt);
	if (Rule("daylightCycle"))
		m_metadata.gameTime += double(dt);
	m_schedule.Update(m_registry, dt);
}

float Game::DayFraction() const noexcept {
	const double day = std::max(1.0, double(m_options.dayLength));
	const double f = std::fmod(m_metadata.gameTime / day, 1.0);
	return float(f < 0.0 ? f + 1.0 : f);
}

// ── Systèmes ─────────────────────────────────────────────────────────────────

void Game::InstallSystems() {
	m_schedule.Add(ecs::Stage::PRE_UPDATE, "world.stream", [this](ecs::SystemContext &) {
		std::vector<BlockPos> anchors;
		m_registry.Query<Transform, Player>([&](ecs::Entity, Transform &t, Player &) { anchors.push_back(BlockPos::FromWorld(t.position)); });
		if (!anchors.empty())
			(void)m_world->UpdateLoadedArea(anchors, m_options.viewRadius, m_options.verticalRadius, m_options.chunkBudget);
	});
	m_schedule.Add(ecs::Stage::FIXED_UPDATE, "ai", [this](ecs::SystemContext &c) {
		StepBehaviors(m_registry, *m_world, c.dt, m_metadata.seed,
					  [this](ecs::Entity e, BehaviorState &s, float dt) { return m_scripts->RunBehavior(e, s, dt); });
	}, 0);
	m_schedule.Add(ecs::Stage::FIXED_UPDATE, "physics", [this](ecs::SystemContext &c) { StepPhysics(m_registry, *m_world, c.dt, m_physics); }, 10);
	m_schedule.Add(ecs::Stage::FIXED_UPDATE, "falling_blocks", [this](ecs::SystemContext &) { StepFallingBlocks(); }, 20);
	m_schedule.Add(ecs::Stage::FIXED_UPDATE, "items", [this](ecs::SystemContext &c) { StepItems(c.dt); }, 30);
	m_schedule.Add(ecs::Stage::FIXED_UPDATE, "health", [this](ecs::SystemContext &c) { StepHealth(c.dt); }, 40);
	m_schedule.Add(ecs::Stage::FIXED_UPDATE, "survival", [this](ecs::SystemContext &c) { StepSurvival(c.dt); }, 45);
	m_schedule.Add(ecs::Stage::FIXED_UPDATE, "random_ticks", [this](ecs::SystemContext &c) { StepRandomTicks(c.dt); }, 50);
	m_schedule.Add(ecs::Stage::UPDATE, "scripts.tick", [this](ecs::SystemContext &c) {
		if (m_scripts->HasHandlers("tick"))
			(void)m_scripts->Emit("tick", {Value::Number(c.dt)});
		m_scripts->RunSystems(c.dt);
	});
	m_schedule.Add(ecs::Stage::UPDATE, "sounds", [this](ecs::SystemContext &c) {
		// Cris des créatures, pas des joueurs sur le sol.
		std::vector<std::pair<String, math::FVector3>> sounds;
		m_registry.Query<SoundEmitter, Transform>([&](ecs::Entity e, SoundEmitter &emitter, Transform &t) {
			emitter.idleTimer -= c.dt;
			if (emitter.idleTimer > 0.f)
				return;
			emitter.idleTimer = 5.f + 10.f * RandomAt(m_metadata.seed, int(e.id), int(m_schedule.Frame()), 0, 21);
			if (String idle = emitter.sounds.Get("idle"); !idle.IsEmpty())
				sounds.push_back({idle, t.position});
		});
		m_registry.Query<Player, Transform, PhysicsBody>([&](ecs::Entity e, Player &, Transform &t, PhysicsBody &body) {
			const math::FVector3 d = t.position - t.previous;
			if (!body.onGround)
				return;
			float &walked = m_stepDistance[e.id];
			walked += std::hypot(d.x, d.z);
			if (walked > 2.2f) {
				walked = 0.f;
				PlayBlockSound(body.groundBlock, "step", BlockPos::FromWorld(t.position - math::FVector3{0.f, 0.1f, 0.f}));
			}
		});
		for (const auto &[sound, pos] : sounds)
			PlaySound(sound, pos);
	});
	m_schedule.Add(ecs::Stage::POST_UPDATE, "events.dispatch", [this](ecs::SystemContext &) { DispatchEvents(); });
}

void Game::StepFallingBlocks() {
	for (const BlockPos &p : m_world->TakeUnstableBlocks()) {
		const BlockDef &def = m_world->BlockAt(p);
		const BlockDef &below = m_world->BlockAt(p + BlockPos{0, -1, 0});
		if (below.solid && !below.fluid)
			continue; // soutenu entre-temps
		if (def.falling && def.fallingBehavior == "FALL_AS_BLOCK") {
			const uint8_t param = m_world->GetParam(p);
			(void)m_world->SetBlock(p, AIR, 0, ChangeCause::SIMULATION);
			(void)SpawnFallingBlock(m_registry, m_content, def.numeric, param, p);
		} else if (def.requiresSupport || def.falling) {
			(void)m_world->SetBlock(p, AIR, 0, ChangeCause::SIMULATION);
			if (!def.drops.IsEmpty())
				(void)DropItem(ItemStack{def.drops, def.dropCount, 0, nullptr}, p.Center());
		}
	}
	// Blocs tombés : redeviennent des blocs à l'atterrissage.
	std::vector<std::pair<ecs::Entity, FallingBlock>> landed;
	std::vector<math::FVector3> positions;
	m_registry.Query<FallingBlock, PhysicsBody, Transform>([&](ecs::Entity e, FallingBlock &f, PhysicsBody &body, Transform &t) {
		if (body.onGround || t.position.y < -500.f) {
			landed.push_back({e, f});
			positions.push_back(t.position);
		}
	});
	for (size_t i = 0; i < landed.size(); ++i) {
		const auto &[e, f] = landed[i];
		const BlockPos at = BlockPos::FromWorld(positions[i] + math::FVector3{0.f, 0.2f, 0.f});
		if (m_world->BlockAt(at).replaceable)
			(void)m_world->SetBlock(at, f.block, f.param, ChangeCause::SIMULATION);
		else if (const String drop = m_content.Block(f.block).drops; !drop.IsEmpty())
			(void)DropItem(ItemStack{drop, 1, 0, nullptr}, positions[i]);
		PlayBlockSound(f.block, "place", at);
		(void)ecs::DespawnRecursive(m_registry, e);
	}
}

void Game::StepItems(float dt) {
	struct Pickup {
		ecs::Entity item, player;
	};
	std::vector<ecs::Entity> expired;
	std::vector<Pickup> pickups;
	std::vector<std::pair<ecs::Entity, math::FVector3>> players;
	m_registry.Query<Player, Transform>([&](ecs::Entity e, Player &, Transform &t) { players.push_back({e, t.position}); });
	m_registry.Query<DroppedItem, Transform>([&](ecs::Entity e, DroppedItem &item, Transform &t) {
		item.age += dt;
		item.pickupDelay -= dt;
		if (item.age > float(m_content.ConfigNum("items.lifetime", 300.0)) || t.position.y < -500.f) {
			expired.push_back(e);
			return;
		}
		if (item.pickupDelay > 0.f)
			return;
		for (const auto &[player, position] : players) {
			const math::FVector3 d = position + math::FVector3{0.f, 0.9f, 0.f} - t.position;
			if (d.Dot(d) < 1.6f * 1.6f) {
				pickups.push_back({e, player});
				break;
			}
		}
	});
	for (const Pickup &p : pickups) {
		auto item = m_registry.GetComponent<DroppedItem>(p.item);
		auto inv = m_registry.GetComponent<InventoryComponent>(p.player);
		if (item.IsNone() || inv.IsNone())
			continue;
		const int leftover = inv.Unwrap()->inventory.Add(m_content, item.Unwrap()->stack.item, item.Unwrap()->stack.count,
														 item.Unwrap()->stack.meta);
		if (leftover == item.Unwrap()->stack.count)
			continue; // inventaire plein
		if (auto t = m_registry.GetComponent<Transform>(p.player); t.IsSome())
			PlaySound(m_content.ConfigStr("sounds.pickup"), t.Unwrap()->position, 0.5f);
		if (leftover > 0)
			item.Unwrap()->stack.count = leftover;
		else
			expired.push_back(p.item);
	}
	for (ecs::Entity e : expired)
		if (m_registry.IsAlive(e))
			(void)ecs::DespawnRecursive(m_registry, e);
}

void Game::StepHealth(float dt) {
	// Chutes : dégâts au-delà d'une vitesse d'impact.
	const float threshold = float(m_content.ConfigNum("physics.fallDamageSpeed", 13.0));
	std::vector<std::pair<ecs::Entity, float>> falls;
	std::vector<std::pair<ecs::Entity, float>> burns;
	ecs::EventsOf<BlockContact>(m_registry).Read(m_contactCursor, [&](const BlockContact &c) {
		if (c.landed && c.impactSpeed > threshold && Rule("fallDamage"))
			falls.push_back({c.entity, (c.impactSpeed - threshold) * 1.2f});
		// Bloc qui blesse au contact (lave, cactus…) : « gameplay.damagePerSecond ».
		if (const float dps = float(json::Num(m_content.Block(c.id).raw, "gameplay.damagePerSecond", 0.0)); dps > 0.f)
			burns.push_back({c.entity, dps * dt * 4.f});
	});
	for (const auto &[e, amount] : falls)
		if (m_registry.IsAlive(e))
			Damage(e, amount);
	for (const auto &[e, amount] : burns)
		if (m_registry.IsAlive(e))
			Damage(e, amount);
	std::vector<ecs::Entity> dead;
	m_registry.Query<Health, Transform>([&](ecs::Entity e, Health &h, Transform &t) {
		h.invulnerable = std::max(0.f, h.invulnerable - dt);
		if (t.position.y < -300.f)
			h.current = 0.f; // tombé hors du monde
		if (h.IsDead())
			dead.push_back(e);
	});
	for (ecs::Entity e : dead)
		Kill(e);
}

void Game::DispatchEvents() {
	if (m_scripts->HasHandlers("entity_touch_block")) {
		std::vector<BlockContact> contacts;
		uint64_t cursor = m_contactCursor > 0 ? m_contactCursor - 1 : 0;
		ecs::EventsOf<BlockContact>(m_registry).ForEach([&](const BlockContact &c) { contacts.push_back(c); });
		(void)cursor;
		for (const BlockContact &c : contacts)
			if (m_registry.IsAlive(c.entity))
				(void)m_scripts->Emit("entity_touch_block", {m_scripts->EntityValue(c.entity), Value::Int(c.block.x), Value::Int(c.block.y),
															  Value::Int(c.block.z), Value::Str(m_content.Block(c.id).id),
															  Value::Boolean(c.landed)});
	}
}

// ── Joueurs et entités ───────────────────────────────────────────────────────

math::FVector3 Game::SpawnPoint() {
	if (m_metadata.spawnKnown)
		return m_metadata.spawn;
	int x = 0, z = 0, h = 64;
	if (ChunkGenerator *gen = m_world->Generator()) {
		// Première colonne au-dessus du niveau de la mer, en spirale.
		const auto *terrain = dynamic_cast<TerrainGenerator *>(gen);
		const int sea = terrain && terrain->Definition().kind != "flat" ? terrain->Definition().seaLevel : -1000000;
		h = gen->SurfaceHeight(0, 0); // repli : aucune terre émergée trouvée
		for (int r = 0; r < 512; r += 8) {
			bool found = false;
			for (int a = 0; a < 8 && !found; ++a) {
				const int cx = r == 0 ? 0 : int(float(r) * std::cos(float(a) * 0.785f));
				const int cz = r == 0 ? 0 : int(float(r) * std::sin(float(a) * 0.785f));
				const int ch = gen->SurfaceHeight(cx, cz);
				if (ch >= sea) {
					x = cx;
					z = cz;
					h = ch;
					found = true;
				}
			}
			if (found)
				break;
		}
	}
	m_metadata.spawn = {float(x) + 0.5f, float(h) + 1.05f, float(z) + 0.5f};
	m_metadata.spawnKnown = true;
	return m_metadata.spawn;
}

ecs::Entity Game::JoinPlayer(const String &name) {
	const ecs::Entity e = SpawnPlayer(m_registry, name, SpawnPoint());
	// Modèle et skin du joueur (configuration des mods ; le skin choisi est
	// enregistré avec le joueur).
	if (auto visual = m_registry.GetComponent<Visual>(e); visual.IsSome()) {
		visual.Unwrap()->rig.model = m_content.ConfigStr("player.model");
		visual.Unwrap()->skin = m_content.ConfigStr("player.skin");
	}
	if (!m_save->LoadPlayer(m_registry, e)) {
		// Premier passage : équipement de départ (configuration des mods).
		if (NodePtr kit = json::At(m_content.Config(), "player.starterKit"); kit && kit->IsArray())
			for (size_t i = 0; i < kit->GetSize(); ++i) {
				NodePtr entry = kit->At(i);
				const String item = entry->IsArray() ? json::Str(entry->At(0), "") : json::Str(entry, "item");
				const int count = entry->IsArray() && entry->GetSize() > 1 ? json::Int(entry->At(1), "", 1) : json::Int(entry, "count", 1);
				if (auto inv = m_registry.GetComponent<InventoryComponent>(e); inv.IsSome())
					(void)inv.Unwrap()->inventory.Add(m_content, item, count);
			}
	}
	if (auto intent = m_registry.GetComponent<MoveIntent>(e); intent.IsSome())
		intent.Unwrap()->speed = float(m_content.ConfigNum("player.speed", 4.3));
	if (auto body = m_registry.GetComponent<PhysicsBody>(e); body.IsSome())
		body.Unwrap()->jumpSpeed = float(m_content.ConfigNum("player.jumpSpeed", 8.4));
	if (!m_localPlayer.Valid())
		m_localPlayer = e;
	// Le terrain autour du joueur, tout de suite (pas d'attente à l'arrivée),
	// puis la première place libre au-dessus (pas né dans un arbre).
	if (auto t = m_registry.GetComponent<Transform>(e); t.IsSome()) {
		(void)m_world->UpdateLoadedArea({BlockPos::FromWorld(t.Unwrap()->position)}, 2, 2, 200);
		math::FVector3 &p = t.Unwrap()->position;
		auto blocked = [&](const math::FVector3 &feet) {
			std::vector<Box> boxes;
			CollectBlockBoxes(*m_world, Box::AtFeet(feet, {0.6f, 1.8f, 0.6f}), boxes);
			const Box body = Box::AtFeet(feet, {0.6f, 1.8f, 0.6f});
			return std::any_of(boxes.begin(), boxes.end(), [&](const Box &b) { return b.Intersects(body); });
		};
		for (int i = 0; i < 64 && blocked(p); ++i)
			p.y = std::floor(p.y) + 1.f;
		t.Unwrap()->previous = p;
	}
	SetMode(m_mode); // vol du spectateur…
	(void)m_scripts->Emit("player_join", {m_scripts->EntityValue(e)});
	Message(String::Format("%s a rejoint la partie", name.CStr()));
	return e;
}

ecs::Entity Game::SpawnEntity(const String &id, const math::FVector3 &feet) {
	const EntityDef *def = m_content.FindEntity(id);
	if (!def)
		return ecs::Entity{};
	const ecs::Entity e = SpawnFromDef(m_registry, *def, feet);
	(void)m_scripts->Emit("entity_spawned", {m_scripts->EntityValue(e)});
	return e;
}

ecs::Entity Game::DropItem(const ItemStack &stack, const math::FVector3 &position, const math::FVector3 &velocity) {
	if (stack.IsEmpty() || !m_content.FindItem(stack.item))
		return ecs::Entity{};
	return SpawnDroppedItem(m_registry, m_content, stack, position, velocity);
}

void Game::Damage(ecs::Entity target, float amount, ecs::Entity source) {
	auto h = m_registry.GetComponent<Health>(target);
	if (h.IsNone() || h.Unwrap()->invulnerable > 0.f || amount <= 0.f)
		return;
	const bool isPlayer = m_registry.HasComponent<Player>(target);
	if (m_options.creative && isPlayer)
		return;
	if (isPlayer && source.Valid() && !m_registry.HasComponent<Player>(source)) {
		// Difficulté : coups des créatures sur les joueurs.
		static constexpr float FACTOR[] = {0.f, 0.5f, 1.f, 1.5f};
		amount *= FACTOR[int(m_difficulty)];
		if (amount <= 0.f)
			return;
	}
	// Armure et résistance.
	if (const int armor = ArmorPoints(target); armor > 0)
		amount *= 1.f - std::min(0.8f, float(armor) * 0.04f);
	if (auto fx = m_registry.GetComponent<StatusEffects>(target); fx.IsSome())
		amount *= std::max(0.f, 1.f - 0.2f * float(fx.Unwrap()->Level("resistance")));
	if (source.Valid())
		if (auto fx = m_registry.GetComponent<StatusEffects>(source); fx.IsSome())
			amount += 3.f * float(fx.Unwrap()->Level("strength"));
	if (!m_scripts->Emit("entity_damaged", {m_scripts->EntityValue(target), Value::Number(amount), m_scripts->EntityValue(source)}))
		return;
	h = m_registry.GetComponent<Health>(target); // un script a pu modifier l'entité
	if (h.IsNone())
		return;
	h.Unwrap()->current -= amount;
	h.Unwrap()->invulnerable = 0.4f;
	h.Unwrap()->hurtTime = 0.f;
	if (source.Valid())
		h.Unwrap()->lastAttacker = source;
	if (isPlayer)
		AddExhaustion(target, 0.1f);
	// Usure de l'armure.
	if (auto eq = m_registry.GetComponent<Equipment>(target); eq.IsSome() && !m_options.creative)
		for (ItemStack &piece : eq.Unwrap()->armor)
			if (!piece.IsEmpty())
				if (const ItemDef *item = m_content.FindItem(piece.item); item)
					if (const int durability = json::Int(item->raw, "armor.durability", 0); durability > 0 && ++piece.wear >= durability)
						piece = ItemStack{};
	// Recul : loin de la source.
	auto t = m_registry.GetComponent<Transform>(target);
	auto st = source.Valid() ? m_registry.GetComponent<Transform>(source) : decltype(m_registry.GetComponent<Transform>(source))(NONE);
	if (t.IsSome() && st.IsSome())
		if (auto v = m_registry.GetComponent<Velocity>(target); v.IsSome()) {
			math::FVector3 d = t.Unwrap()->position - st.Unwrap()->position;
			d.y = 0.f;
			d = d.Length() > 1e-4f ? d.Normalize() : math::FVector3{0.f, 0.f, 1.f};
			v.Unwrap()->linear = v.Unwrap()->linear + d * 6.f + math::FVector3{0.f, 5.f, 0.f};
		}
	if (auto emitter = m_registry.GetComponent<SoundEmitter>(target); emitter.IsSome() && t.IsSome())
		PlaySound(emitter.Unwrap()->sounds.Get("hurt"), t.Unwrap()->position);
	else if (t.IsSome() && m_registry.HasComponent<Player>(target))
		PlaySound(m_content.ConfigStr("sounds.hurt"), t.Unwrap()->position);
}

void Game::Kill(ecs::Entity target) {
	if (!m_registry.IsAlive(target))
		return;
	auto kind = m_registry.GetComponent<EntityKind>(target);
	auto t = m_registry.GetComponent<Transform>(target);
	const math::FVector3 at = t.IsSome() ? t.Unwrap()->position : math::FVector3{0.f, 0.f, 0.f};
	const String kindId = kind.IsSome() ? kind.Unwrap()->id : String();
	if (!m_scripts->Emit("entity_died", {m_scripts->EntityValue(target), Value::Str(kindId)})) {
		if (auto h = m_registry.GetComponent<Health>(target); h.IsSome())
			h.Unwrap()->current = std::max(1.f, h.Unwrap()->current); // sauvé par un script
		return;
	}
	ecs::EventsOf<EntityDied>(m_registry).Send(EntityDied{target, kindId, at});
	// Expérience pour le vainqueur.
	if (kind.IsSome() && kind.Unwrap()->def)
		if (auto h = m_registry.GetComponent<Health>(target); h.IsSome() && m_registry.IsAlive(h.Unwrap()->lastAttacker) &&
															   m_registry.HasComponent<Player>(h.Unwrap()->lastAttacker)) {
			const EntityDef *def = kind.Unwrap()->def;
			const bool hostile = std::find(def->tags.begin(), def->tags.end(), String("hostile")) != def->tags.end();
			AddXp(h.Unwrap()->lastAttacker, json::Int(def->raw, "xp", hostile ? 5 : 2));
		}
	if (m_registry.HasComponent<Player>(target)) {
		if (!Rule("keepInventory"))
			if (auto inv = m_registry.GetComponent<InventoryComponent>(target); inv.IsSome()) {
				for (int i = 0; i < inv.Unwrap()->inventory.Size(); ++i)
					if (ItemStack &s = inv.Unwrap()->inventory.Slot(i); !s.IsEmpty()) {
						(void)DropItem(s, at + math::FVector3{0.f, 0.8f, 0.f},
									   {std::sin(float(i)) * 2.f, 4.f, std::cos(float(i)) * 2.f});
						s = ItemStack{};
					}
			}
		if (auto sv = m_registry.GetComponent<Survival>(target); sv.IsSome()) {
			const int xp = sv.Unwrap()->xp;
			*sv.Unwrap() = Survival{};
			sv.Unwrap()->xp = Rule("keepInventory") ? xp : 0;
		}
		if (auto fx = m_registry.GetComponent<StatusEffects>(target); fx.IsSome())
			fx.Unwrap()->list.clear();
		// Un joueur réapparaît (inventaire conservé, à la manière d'un mode simple).
		if (auto h = m_registry.GetComponent<Health>(target); h.IsSome())
			h.Unwrap()->current = h.Unwrap()->max;
		if (auto tr = m_registry.GetComponent<Transform>(target); tr.IsSome())
			tr.Unwrap()->position = tr.Unwrap()->previous = SpawnPoint();
		if (auto v = m_registry.GetComponent<Velocity>(target); v.IsSome())
			v.Unwrap()->linear = {0.f, 0.f, 0.f};
		Message(String("Vous êtes mort."));
		return;
	}
	if (kind.IsSome() && kind.Unwrap()->def) {
		for (const auto &[item, count] : kind.Unwrap()->def->drops)
			(void)DropItem(ItemStack{item, count, 0, nullptr}, at + math::FVector3{0.f, 0.5f, 0.f});
		PlaySound(kind.Unwrap()->def->sounds.Get("death"), at);
	}
	(void)ecs::DespawnRecursive(m_registry, target);
}

bool Game::Interact(ecs::Entity actor, ecs::Entity target) {
	auto kind = m_registry.GetComponent<EntityKind>(target);
	bool handled = false;
	if (kind.IsSome() && kind.Unwrap()->def && !kind.Unwrap()->def->onInteract.IsEmpty())
		if (Option<Value> fn = m_scripts->Vm().GetGlobal(kind.Unwrap()->def->onInteract); fn.IsSome()) {
			Result<Value, script::ScriptError> r = m_scripts->Vm().CallValue(fn.Unwrap(), {m_scripts->EntityValue(target), m_scripts->EntityValue(actor)}, 0, 0);
			if (r.IsError())
				LogLine(String("[script] ") + r.Error().Format());
			handled = true;
		}
	if (m_scripts->HasHandlers("entity_interact")) {
		(void)m_scripts->Emit("entity_interact", {m_scripts->EntityValue(target), m_scripts->EntityValue(actor)});
		handled = true;
	}
	return handled;
}

// ── Blocs ────────────────────────────────────────────────────────────────────

ItemStack *Game::HeldItem(ecs::Entity actor) {
	auto player = m_registry.GetComponent<Player>(actor);
	auto inv = m_registry.GetComponent<InventoryComponent>(actor);
	if (inv.IsNone())
		return nullptr;
	const int slot = player.IsSome() ? std::clamp(player.Unwrap()->selectedSlot, 0, inv.Unwrap()->inventory.Size() - 1) : 0;
	return &inv.Unwrap()->inventory.Slot(slot);
}

Option<BlockHit> Game::TargetBlock(ecs::Entity actor) {
	auto t = m_registry.GetComponent<Transform>(actor);
	if (t.IsNone() || !m_world)
		return NONE;
	auto player = m_registry.GetComponent<Player>(actor);
	const float reach = player.IsSome() ? player.Unwrap()->reach : 5.f;
	const math::FVector3 eye = t.Unwrap()->position + math::FVector3{0.f, EYE_HEIGHT, 0.f};
	return RaycastBlocks(*m_world, eye, t.Unwrap()->Forward(), reach);
}

Option<EntityHit> Game::TargetEntity(ecs::Entity actor) {
	auto t = m_registry.GetComponent<Transform>(actor);
	if (t.IsNone())
		return NONE;
	auto player = m_registry.GetComponent<Player>(actor);
	const float reach = player.IsSome() ? player.Unwrap()->reach : 5.f;
	const math::FVector3 eye = t.Unwrap()->position + math::FVector3{0.f, EYE_HEIGHT, 0.f};
	Option<EntityHit> hit = RaycastEntities(m_registry, eye, t.Unwrap()->Forward(), reach, actor);
	// Un bloc plus proche masque l'entité.
	if (Option<BlockHit> block = TargetBlock(actor); hit.IsSome() && block.IsSome() && block.Value().distance < hit.Value().distance)
		return NONE;
	return hit;
}

float Game::BreakTime(ecs::Entity actor, const BlockPos &pos) {
	const BlockDef &def = m_world->BlockAt(pos);
	if (def.IsAir() || def.hardness < 0.f)
		return -1.f;
	if (m_options.creative)
		return 0.f;
	float time = def.hardness;
	if (const ItemStack *held = HeldItem(actor); held && !held->IsEmpty())
		if (const ItemDef *item = m_content.FindItem(held->item); item && item->tool && item->tool->kind == def.preferredTool)
			time /= std::max(0.1f, item->tool->speed);
	return time;
}

bool Game::BreakBlock(ecs::Entity actor, const BlockPos &pos) {
	if (!m_world)
		return false;
	const BlockDef &def = m_world->BlockAt(pos);
	if (def.IsAir() || def.hardness < 0.f || def.fluid)
		return false;
	if ((m_mode == GameMode::ADVENTURE || m_mode == GameMode::SPECTATOR) && m_registry.HasComponent<Player>(actor))
		return false;
	if (!m_scripts->Emit("block_broken", {Value::Int(pos.x), Value::Int(pos.y), Value::Int(pos.z), Value::Str(def.id), m_scripts->EntityValue(actor)}))
		return false;
	const BlockId id = def.numeric;
	(void)m_world->SetBlock(pos, AIR, 0, ChangeCause::PLAYER);
	PlayBlockSound(id, "break", pos);
	const BlockDef &broken = m_content.Block(id);
	if (!m_options.creative && !broken.drops.IsEmpty() &&
		(broken.dropChance >= 1.f || RandomAt(m_metadata.seed ^ uint32_t(++m_soundSerial), pos.x, pos.y, pos.z, 13) < broken.dropChance))
		(void)DropItem(ItemStack{broken.drops, broken.dropCount, 0, nullptr}, pos.Center());
	if (!m_options.creative && m_registry.HasComponent<Player>(actor)) {
		AddExhaustion(actor, 0.005f);
		if (const int xp = json::Int(broken.raw, "gameplay.xp", 0); xp > 0)
			AddXp(actor, xp);
	}
	// Usure de l'outil.
	if (ItemStack *held = HeldItem(actor); held && !held->IsEmpty() && !m_options.creative)
		if (const ItemDef *item = m_content.FindItem(held->item); item && item->tool && item->tool->durability > 0)
			if (++held->wear >= item->tool->durability) {
				*held = ItemStack{};
				if (auto t = m_registry.GetComponent<Transform>(actor); t.IsSome())
					PlaySound(m_content.ConfigStr("sounds.toolBreak"), t.Unwrap()->position);
			}
	return true;
}

bool Game::PlaceBlock(ecs::Entity actor, const BlockPos &pos, const String &block) {
	if (!m_world)
		return false;
	const BlockDef *def = m_content.FindBlock(block);
	if (!def || def->IsAir() || !m_world->IsLoaded(pos))
		return false;
	if ((m_mode == GameMode::ADVENTURE || m_mode == GameMode::SPECTATOR) && m_registry.HasComponent<Player>(actor))
		return false;
	if (!m_world->BlockAt(pos).replaceable)
		return false;
	// « placement.on » : blocs acceptés dessous (graines sur la terre labourée…).
	if (NodePtr on = json::At(def->raw, "placement.on"); on && on->IsArray()) {
		const String under = m_world->BlockAt(pos + BlockPos{0, -1, 0}).id;
		bool ok = false;
		for (size_t i = 0; i < on->GetSize(); ++i)
			ok = ok || ContentRegistry::Qualify(json::Str(on->At(i), ""), def->mod) == under;
		if (!ok)
			return false;
	}
	// Pas dans une entité (pour un bloc solide).
	if (def->solid && def->collision != CollisionKind::NONE) {
		std::vector<Box> boxes;
		const math::FVector3 o = pos.ToVector();
		if (def->collision == CollisionKind::FULL_CUBE)
			boxes.push_back(Box{o, o + math::FVector3{1.f, 1.f, 1.f}});
		else
			for (const Box &b : def->collisionBoxes)
				boxes.push_back(b.Offset(o));
		bool blocked = false;
		m_registry.Query<Transform, Collider>([&](ecs::Entity e, Transform &t, Collider &c) {
			if (m_registry.HasComponent<DroppedItem>(e) || m_registry.HasComponent<FallingBlock>(e))
				return;
			for (const Box &b : boxes)
				blocked = blocked || c.WorldBox(t.position).Intersects(b);
		});
		if (blocked)
			return false;
	}
	// Orientation : face au joueur (blocs non cubiques et faces différentes).
	uint8_t param = 0;
	if (auto t = m_registry.GetComponent<Transform>(actor); t.IsSome()) {
		const float yaw = t.Unwrap()->yaw;
		param = uint8_t((int(std::lround(yaw / 1.5707963f)) % 4 + 4) % 4);
	}
	if (!m_scripts->Emit("block_placed", {Value::Int(pos.x), Value::Int(pos.y), Value::Int(pos.z), Value::Str(def->id), m_scripts->EntityValue(actor)}))
		return false;
	(void)m_world->SetBlock(pos, def->numeric, param, ChangeCause::PLAYER);
	PlayBlockSound(def->numeric, "place", pos);
	if (!m_options.creative)
		if (ItemStack *held = HeldItem(actor); held && !held->IsEmpty())
			if (const ItemDef *item = m_content.FindItem(held->item); item && item->placesBlock == def->id)
				if (--held->count <= 0)
					*held = ItemStack{};
	return true;
}

bool Game::UseHeldItem(ecs::Entity actor) {
	if (Option<EntityHit> entity = TargetEntity(actor); entity.IsSome() && Interact(actor, entity.Value().entity))
		return true;
	Option<BlockHit> hit = TargetBlock(actor);
	if (hit.IsSome()) {
		const BlockHit &h = hit.Value();
		const BlockDef &target = m_world->BlockAt(h.block);
		if (m_scripts->HasHandlers("block_use") &&
			!m_scripts->Emit("block_use", {Value::Int(h.block.x), Value::Int(h.block.y), Value::Int(h.block.z), Value::Str(target.id),
										   m_scripts->EntityValue(actor)}))
			return true; // un script a pris en charge l'utilisation du bloc
	}
	ItemStack *held = HeldItem(actor);
	if (!held || held->IsEmpty())
		return false;
	const ItemDef *item = m_content.FindItem(held->item);
	if (!item)
		return false;
	if (!item->placesBlock.IsEmpty() && hit.IsSome()) {
		const BlockHit &h = hit.Value();
		const BlockPos at = m_world->BlockAt(h.block).replaceable ? h.block : h.previous;
		return PlaceBlock(actor, at, item->placesBlock);
	}
	bool used = false;
	if (!item->onUse.IsEmpty())
		if (Option<Value> fn = m_scripts->Vm().GetGlobal(item->onUse); fn.IsSome()) {
			Result<Value, script::ScriptError> r = m_scripts->Vm().CallValue(fn.Unwrap(), {m_scripts->EntityValue(actor), Value::Str(item->id)}, 0, 0);
			if (r.IsError())
				LogLine(String("[script] ") + r.Error().Format());
			used = true;
		}
	if (m_scripts->HasHandlers("item_use"))
		used = !m_scripts->Emit("item_use", {m_scripts->EntityValue(actor), Value::Str(item->id)}) || used;
	// Nourriture, armure.
	if (!used && item->food > 0.f)
		used = Eat(actor);
	if (!used && json::Has(item->raw, "armor"))
		used = EquipHeld(actor);
	return used;
}

// ── Règles du monde ──────────────────────────────────────────────────────────

const std::vector<std::pair<const char *, bool>> &Game::DefaultRules() {
	static const std::vector<std::pair<const char *, bool>> rules{
		{"daylightCycle", true},	  {"mobSpawning", true},  {"keepInventory", true}, {"naturalRegeneration", true},
		{"fallDamage", true},		  {"mobGriefing", true},  {"commands", true},	   {"hunger", true},
		{"showCoordinates", true},	  {"fireSpread", true},	  {"weather", true},	   {"announceDeaths", true},
	};
	return rules;
}

void Game::ApplySettings() {
	if (!m_metadata.settings || !m_metadata.settings->IsObject())
		m_metadata.settings = dataio::Node::MakeObject();
	m_mode = GameModeFromId(json::Str(m_metadata.settings, "gameMode", m_options.creative ? "creative" : "survival"));
	m_difficulty = DifficultyFromId(json::Str(m_metadata.settings, "difficulty", "normal"));
	if (const double day = json::Num(m_metadata.settings, "dayLength", 0.0); day > 10.0)
		m_options.dayLength = float(day);
	SetMode(m_mode);
	SetDifficulty(m_difficulty);
}

void Game::SetMode(GameMode mode) {
	m_mode = mode;
	m_options.creative = mode == GameMode::CREATIVE || mode == GameMode::SPECTATOR;
	if (m_metadata.settings)
		m_metadata.settings->Set("gameMode", json::MakeStr(GameModeId(mode)));
	m_registry.Query<Player, PhysicsBody>([&](ecs::Entity, Player &, PhysicsBody &body) {
		if (mode == GameMode::SPECTATOR)
			body.flies = true;
		else if (mode == GameMode::SURVIVAL || mode == GameMode::ADVENTURE)
			body.flies = false;
	});
}

void Game::SetDifficulty(Difficulty difficulty) {
	m_difficulty = difficulty;
	if (m_metadata.settings)
		m_metadata.settings->Set("difficulty", json::MakeStr(DifficultyId(difficulty)));
}

bool Game::Rule(const char *name) const {
	bool fallback = true;
	for (const auto &[rule, value] : DefaultRules())
		if (std::strcmp(rule, name) == 0)
			fallback = value;
	return json::Bool(m_metadata.settings, (String("rules.") + String(name)).CStr(), fallback);
}

double Game::RuleNum(const char *name, double fallback) const {
	return json::Num(m_metadata.settings, (String("rules.") + String(name)).CStr(), fallback);
}

void Game::SetRule(const char *name, bool value) {
	if (!m_metadata.settings || !m_metadata.settings->IsObject())
		m_metadata.settings = dataio::Node::MakeObject();
	NodePtr rules = json::At(m_metadata.settings, "rules");
	if (!rules || !rules->IsObject()) {
		rules = dataio::Node::MakeObject();
		m_metadata.settings->Set("rules", rules);
	}
	rules->Set(name, json::MakeBool(value));
}

// ── Survie ───────────────────────────────────────────────────────────────────

void Game::AddExhaustion(ecs::Entity player, float amount) {
	if (m_options.creative || m_difficulty == Difficulty::PEACEFUL || !Rule("hunger"))
		return;
	if (auto sv = m_registry.GetComponent<Survival>(player); sv.IsSome())
		sv.Unwrap()->exhaustion += amount;
}

void Game::AddXp(ecs::Entity player, int amount) {
	auto sv = m_registry.GetComponent<Survival>(player);
	if (sv.IsNone() || amount <= 0)
		return;
	const int before = XpLevel(sv.Unwrap()->xp);
	sv.Unwrap()->xp += amount;
	const int after = XpLevel(sv.Unwrap()->xp);
	if (auto t = m_registry.GetComponent<Transform>(player); t.IsSome())
		PlaySound(m_content.ConfigStr(after > before ? "sounds.levelUp" : "sounds.xp"), t.Unwrap()->position, 0.6f);
	if (after > before)
		Message(String::Format("Niveau %d atteint !", after), String("event"));
}

void Game::AddEffect(ecs::Entity target, const String &effect, float seconds, int level) {
	auto fx = m_registry.GetComponent<StatusEffects>(target);
	if (fx.IsNone()) {
		m_registry.AddComponent(target, StatusEffects{});
		fx = m_registry.GetComponent<StatusEffects>(target);
	}
	for (StatusEffect &e : fx.Unwrap()->list)
		if (e.id == effect) {
			e.remaining = std::max(e.remaining, seconds);
			e.level = std::max(e.level, level);
			return;
		}
	fx.Unwrap()->list.push_back(StatusEffect{effect, seconds, std::max(1, level), 0.f});
}

bool Game::Eat(ecs::Entity actor) {
	ItemStack *held = HeldItem(actor);
	if (!held || held->IsEmpty())
		return false;
	const ItemDef *item = m_content.FindItem(held->item);
	if (!item || item->food <= 0.f)
		return false;
	auto sv = m_registry.GetComponent<Survival>(actor);
	auto h = m_registry.GetComponent<Health>(actor);
	const bool hungry = sv.IsSome() && sv.Unwrap()->hunger < 20.f;
	const bool always = json::Bool(item->raw, "alwaysEdible", false);
	if (sv.IsSome()) {
		if (!hungry && !always)
			return false;
		sv.Unwrap()->hunger = std::min(20.f, sv.Unwrap()->hunger + item->food);
		sv.Unwrap()->saturation = std::min(sv.Unwrap()->hunger, sv.Unwrap()->saturation +
																   float(json::Num(item->raw, "saturation", double(item->food) * 0.6)));
	} else if (h.IsSome()) { // sans faim (entité scriptée) : soigne
		if (h.Unwrap()->current >= h.Unwrap()->max)
			return false;
		h.Unwrap()->current = std::min(h.Unwrap()->max, h.Unwrap()->current + item->food);
	}
	// Effets de l'aliment : [{"id": "poison", "seconds": 5, "level": 1, "chance": 0.3}]
	if (NodePtr effects = json::At(item->raw, "effects"); effects && effects->IsArray())
		for (size_t i = 0; i < effects->GetSize(); ++i) {
			NodePtr e = effects->At(i);
			if (RandomAt(uint32_t(++m_soundSerial), int(i), 0, 0, 5) <= float(json::Num(e, "chance", 1.0)))
				AddEffect(actor, json::Str(e, "id"), float(json::Num(e, "seconds", 10.0)), json::Int(e, "level", 1));
		}
	if (auto t = m_registry.GetComponent<Transform>(actor); t.IsSome())
		PlaySound(m_content.ConfigStr("sounds.eat"), t.Unwrap()->position, 0.8f);
	if (!m_options.creative && --held->count <= 0)
		*held = ItemStack{};
	return true;
}

int Game::ArmorPoints(ecs::Entity e) {
	auto eq = m_registry.GetComponent<Equipment>(e);
	if (eq.IsNone())
		return 0;
	int points = 0;
	for (const ItemStack &piece : eq.Unwrap()->armor)
		if (!piece.IsEmpty())
			if (const ItemDef *item = m_content.FindItem(piece.item))
				points += json::Int(item->raw, "armor.points", 0);
	return std::min(20, points);
}

bool Game::EquipHeld(ecs::Entity player) {
	ItemStack *held = HeldItem(player);
	auto eq = m_registry.GetComponent<Equipment>(player);
	if (!held || held->IsEmpty() || eq.IsNone())
		return false;
	const ItemDef *item = m_content.FindItem(held->item);
	if (!item)
		return false;
	const String slot = json::Str(item->raw, "armor.slot");
	const int index = slot == "head" ? 0 : slot == "chest" ? 1 : slot == "legs" ? 2 : slot == "feet" ? 3 : -1;
	if (index < 0)
		return false;
	std::swap(eq.Unwrap()->armor[size_t(index)], *held);
	if (auto t = m_registry.GetComponent<Transform>(player); t.IsSome())
		PlaySound(m_content.ConfigStr("sounds.equip"), t.Unwrap()->position, 0.7f);
	return true;
}

Result<bool, String> Game::Trade(ecs::Entity player, const String &giveItem, int giveCount, const String &getItem, int getCount) {
	auto inv = m_registry.GetComponent<InventoryComponent>(player);
	if (inv.IsNone())
		return Err(String("pas d'inventaire"));
	Inventory &bag = inv.Unwrap()->inventory;
	if (!giveItem.IsEmpty() && bag.Count(giveItem) < giveCount)
		return Err(String::Format("il faut %d × %s", giveCount, giveItem.CStr()));
	if (!giveItem.IsEmpty())
		(void)bag.Remove(giveItem, giveCount);
	const int leftover = bag.Add(m_content, getItem, getCount);
	if (leftover > 0)
		if (auto t = m_registry.GetComponent<Transform>(player); t.IsSome())
			(void)DropItem(ItemStack{getItem, leftover, 0, nullptr}, t.Unwrap()->position + math::FVector3{0.f, 1.f, 0.f});
	if (auto t = m_registry.GetComponent<Transform>(player); t.IsSome())
		PlaySound(m_content.ConfigStr("sounds.trade"), t.Unwrap()->position, 0.8f);
	return Ok(true);
}

void Game::StepSurvival(float dt) {
	const bool peaceful = m_difficulty == Difficulty::PEACEFUL;
	const bool regenRule = Rule("naturalRegeneration");
	std::vector<std::pair<ecs::Entity, float>> hurts;
	m_registry.Query<Player, Survival, Health, Transform>([&](ecs::Entity e, Player &, Survival &s, Health &h, Transform &t) {
		h.hurtTime += dt;
		auto intent = m_registry.GetComponent<MoveIntent>(e);
		auto body = m_registry.GetComponent<PhysicsBody>(e);
		// Course : endurance et faim.
		const bool sprinting = intent.IsSome() && intent.Unwrap()->sprint && intent.Unwrap()->wish.Length() > 0.1f;
		if (sprinting && !m_options.creative) {
			s.stamina = std::max(0.f, s.stamina - dt * 0.12f);
			s.exhaustion += dt * 0.25f;
			if (s.stamina <= 0.f || s.hunger <= 6.f)
				intent.Unwrap()->sprint = false;
		} else {
			s.stamina = std::min(1.f, s.stamina + dt * (s.hunger > 6.f ? 0.2f : 0.05f));
		}
		if (intent.IsSome() && intent.Unwrap()->jump && body.IsSome() && body.Unwrap()->onGround)
			s.exhaustion += 0.05f;
		if (peaceful || m_options.creative || !Rule("hunger"))
			s.exhaustion = 0.f;
		while (s.exhaustion >= 4.f) {
			s.exhaustion -= 4.f;
			if (s.saturation > 0.f)
				s.saturation = std::max(0.f, s.saturation - 1.f);
			else
				s.hunger = std::max(0.f, s.hunger - 1.f);
		}
		// Régénération et famine.
		s.regenTimer += dt;
		if (h.current < h.max && (peaceful ? s.regenTimer > 2.f : regenRule && s.hunger >= 18.f && s.regenTimer > 4.f)) {
			h.current = std::min(h.max, h.current + 1.f);
			s.regenTimer = 0.f;
			s.exhaustion += peaceful ? 0.f : 1.5f;
		}
		if (s.hunger <= 0.f) {
			s.starveTimer += dt;
			const float floor = m_difficulty == Difficulty::EASY ? 10.f : m_difficulty == Difficulty::NORMAL ? 1.f : 0.f;
			if (s.starveTimer > 4.f && h.current > floor) {
				s.starveTimer = 0.f;
				hurts.push_back({e, 1.f});
			}
		}
		// Souffle sous l'eau.
		const BlockPos head = BlockPos::FromWorld(t.position + math::FVector3{0.f, EYE_HEIGHT, 0.f});
		if (m_world->BlockAt(head).fluid && !m_options.creative) {
			s.air = std::max(0.f, s.air - dt);
			if (s.air <= 0.f && (s.drownTimer += dt) > 1.f) {
				s.drownTimer = 0.f;
				hurts.push_back({e, 2.f});
			}
		} else {
			s.air = std::min(s.maxAir, s.air + dt * 4.f);
		}
	});
	// Effets de statut.
	m_registry.Query<StatusEffects, Health>([&](ecs::Entity e, StatusEffects &fx, Health &h) {
		for (StatusEffect &effect : fx.list) {
			effect.remaining -= dt;
			effect.timer += dt;
			const float period = std::max(0.25f, 2.5f / float(effect.level));
			if (effect.id == "regeneration" && effect.timer > period) {
				effect.timer = 0.f;
				h.current = std::min(h.max, h.current + 1.f);
			} else if (effect.id == "poison" && effect.timer > period * 0.5f) {
				effect.timer = 0.f;
				if (h.current > 1.f)
					h.current -= 1.f;
			} else if (effect.id == "hunger") {
				AddExhaustion(e, dt * 0.1f * float(effect.level));
			}
		}
		std::erase_if(fx.list, [](const StatusEffect &effect) { return effect.remaining <= 0.f; });
		if (auto intent = m_registry.GetComponent<MoveIntent>(e); intent.IsSome() && m_registry.HasComponent<Player>(e)) {
			const float base = float(m_content.ConfigNum("player.speed", 4.3));
			intent.Unwrap()->speed = base * (1.f + 0.2f * float(fx.Level("speed")) - 0.15f * float(fx.Level("slowness")));
		}
		if (auto body = m_registry.GetComponent<PhysicsBody>(e); body.IsSome() && m_registry.HasComponent<Player>(e))
			body.Unwrap()->jumpSpeed = float(m_content.ConfigNum("player.jumpSpeed", 8.4)) * (1.f + 0.15f * float(fx.Level("jump_boost")));
	});
	for (const auto &[e, amount] : hurts)
		if (auto h = m_registry.GetComponent<Health>(e); h.IsSome()) {
			h.Unwrap()->current -= amount;
			h.Unwrap()->hurtTime = 0.f;
			if (auto t = m_registry.GetComponent<Transform>(e); t.IsSome())
				PlaySound(m_content.ConfigStr("sounds.hurt"), t.Unwrap()->position);
		}
}

// ── Fabrication ──────────────────────────────────────────────────────────────

String Game::RecipeStation(const RecipeDef &recipe) const {
	if (!recipe.station.IsEmpty())
		return recipe.station == "hand" || recipe.station == "core:hand" ? String() : recipe.station;
	if (recipe.kind == RecipeKind::COOKING)
		return m_content.ConfigStr("crafting.cookingStation");
	size_t width = 0;
	for (const String &row : recipe.pattern)
		width = std::max(width, row.GetSize());
	if (recipe.kind == RecipeKind::SHAPED && (width > 2 || recipe.pattern.size() > 2))
		return m_content.ConfigStr("crafting.tableStation");
	return String();
}

std::vector<String> Game::NearbyStations(ecs::Entity player, int radius) {
	std::vector<String> out;
	auto t = m_registry.GetComponent<Transform>(player);
	if (t.IsNone() || !m_world)
		return out;
	const BlockPos c = BlockPos::FromWorld(t.Unwrap()->position);
	for (int dy = -2; dy <= 3; ++dy)
		for (int dz = -radius; dz <= radius; ++dz)
			for (int dx = -radius; dx <= radius; ++dx) {
				const BlockDef &def = m_world->BlockAt(c + BlockPos{dx, dy, dz});
				if (!def.IsAir() && std::find(def.tags.begin(), def.tags.end(), String("station")) != def.tags.end() &&
					std::find(out.begin(), out.end(), def.id) == out.end())
					out.push_back(def.id);
			}
	return out;
}

String Game::CraftBlocker(ecs::Entity player, const RecipeDef &recipe, const std::vector<String> &stations) {
	if (m_mode == GameMode::CREATIVE)
		return String();
	if (recipe.requiresLevel > 0)
		if (auto sv = m_registry.GetComponent<Survival>(player); sv.IsNone() || XpLevel(sv.Unwrap()->xp) < recipe.requiresLevel)
			return String::Format("niveau %d requis", recipe.requiresLevel);
	if (const String station = RecipeStation(recipe); !station.IsEmpty() &&
		std::find(stations.begin(), stations.end(), station) == stations.end()) {
		const BlockDef *def = m_content.FindBlock(station);
		return String::Format("atelier requis : %s", def ? def->displayName.CStr() : station.CStr());
	}
	auto inv = m_registry.GetComponent<InventoryComponent>(player);
	if (inv.IsNone() || !CanCraft(m_content, inv.Unwrap()->inventory, recipe))
		return String("ingrédients manquants");
	if (recipe.kind == RecipeKind::COOKING) {
		bool fuel = false;
		for (int i = 0; i < inv.Unwrap()->inventory.Size() && !fuel; ++i)
			if (const ItemStack &s = inv.Unwrap()->inventory.Slot(i); !s.IsEmpty())
				if (const ItemDef *item = m_content.FindItem(s.item); item && item->HasTag("fuel"))
					fuel = true;
		if (!fuel)
			return String("combustible requis (charbon, bois…)");
	}
	return String();
}

Result<int, String> Game::Craft(ecs::Entity player, const String &recipeId, int times) {
	const RecipeDef *recipe = m_content.FindRecipe(ContentRegistry::Qualify(recipeId, "core"));
	if (!recipe)
		recipe = m_content.FindRecipe(recipeId);
	if (!recipe)
		return Err(String::Format("recette inconnue « %s »", recipeId.CStr()));
	auto inv = m_registry.GetComponent<InventoryComponent>(player);
	if (inv.IsNone())
		return Err(String("pas d'inventaire"));
	const std::vector<String> stations = NearbyStations(player);
	int made = 0;
	for (int i = 0; i < times; ++i) {
		if (m_mode == GameMode::CREATIVE) {
			(void)inv.Unwrap()->inventory.Add(m_content, recipe->result, recipe->resultCount);
			++made;
			continue;
		}
		if (const String blocker = CraftBlocker(player, *recipe, stations); !blocker.IsEmpty()) {
			if (made == 0)
				return Err(blocker);
			break;
		}
		if (recipe->kind == RecipeKind::COOKING) // un combustible par cuisson
			for (int k = 0; k < inv.Unwrap()->inventory.Size(); ++k)
				if (ItemStack &s = inv.Unwrap()->inventory.Slot(k); !s.IsEmpty())
					if (const ItemDef *item = m_content.FindItem(s.item); item && item->HasTag("fuel")) {
						if (--s.count <= 0)
							s = ItemStack{};
						break;
					}
		if (CraftFromInventory(m_content, inv.Unwrap()->inventory, *recipe, 1) <= 0)
			break;
		++made;
	}
	if (made > 0)
		if (auto t = m_registry.GetComponent<Transform>(player); t.IsSome())
			PlaySound(m_content.ConfigStr(recipe->kind == RecipeKind::COOKING ? "sounds.cook" : "sounds.craft"), t.Unwrap()->position, 0.7f);
	return Ok(made);
}

void Game::StepRandomTicks(float dt) {
	// Ticks aléatoires (cultures, pousses…) : « simulation.randomTick » des blocs.
	m_randomTickTimer += dt;
	if (m_randomTickTimer < 0.5f)
		return;
	m_randomTickTimer = 0.f;
	std::vector<ChunkPos> anchors;
	m_registry.Query<Transform, Player>([&](ecs::Entity, Transform &t, Player &) { anchors.push_back(ChunkOf(BlockPos::FromWorld(t.position))); });
	if (anchors.empty())
		return;
	const uint32_t frame = uint32_t(m_schedule.Frame());
	std::vector<std::pair<BlockPos, const BlockDef *>> due;
	for (const ChunkPos &cp : m_world->LoadedChunks()) {
		bool near = false;
		for (const ChunkPos &a : anchors)
			near = near || (std::abs(cp.x - a.x) <= 4 && std::abs(cp.z - a.z) <= 4 && std::abs(cp.y - a.y) <= 2);
		const Chunk *chunk = near ? m_world->GetChunk(cp) : nullptr;
		if (!chunk || chunk->IsEmpty())
			continue;
		for (uint32_t k = 0; k < 8; ++k) {
			const uint64_t h = HashPosition(m_metadata.seed ^ frame, cp.x, cp.y * 31 + int(k), cp.z, 77);
			const BlockPos p = cp.Origin() + BlockPos{int(h & 15), int((h >> 4) & 15), int((h >> 8) & 15)};
			const BlockDef &def = m_world->BlockAt(p);
			if (!def.IsAir() && json::Has(def.raw, "simulation.randomTick"))
				due.push_back({p, &def});
		}
	}
	for (const auto &[p, def] : due) {
		NodePtr tick = json::At(def->raw, "simulation.randomTick");
		if (RandomAt(m_metadata.seed ^ frame, p.x, p.y, p.z, 91) > float(json::Num(tick, "chance", 0.1)))
			continue;
		if (NodePtr below = json::At(tick, "requiresBelow"); below && below->IsArray()) {
			const String under = m_world->BlockAt(p + BlockPos{0, -1, 0}).id;
			bool ok = false;
			for (size_t i = 0; i < below->GetSize(); ++i)
				ok = ok || ContentRegistry::Qualify(json::Str(below->At(i), ""), def->mod) == under;
			if (!ok)
				continue;
		}
		if (const String structure = json::Str(tick, "structure"); !structure.IsEmpty()) {
			if (const StructureDef *tree = m_content.FindStructure(ContentRegistry::Qualify(structure, def->mod))) {
				(void)m_world->SetBlock(p, AIR, 0, ChangeCause::SIMULATION);
				m_world->PlaceStructure(*tree, p, int(HashPosition(m_metadata.seed, p.x, p.y, p.z, 5) % 4), false);
			}
		} else if (const String next = json::Str(tick, "replaceWith"); !next.IsEmpty()) {
			(void)m_world->SetBlock(p, ContentRegistry::Qualify(next, def->mod), m_world->GetParam(p), ChangeCause::SIMULATION);
		}
	}
}

// ── Console ──────────────────────────────────────────────────────────────────

void Game::RegisterCommand(const String &name, const String &usage, const String &help, CommandFn fn) {
	m_commands[name.CStr()] = Command{name, usage, help, std::move(fn)};
}

std::vector<const Game::Command *> Game::Commands() const {
	std::vector<const Command *> out;
	for (const auto &[name, command] : m_commands)
		out.push_back(&command);
	return out;
}

Result<String, String> Game::Execute(const String &rawLine, ecs::Entity sender) {
	String line = rawLine.Trim();
	if (line.StartsWith("/"))
		line = line.Substring(1);
	std::vector<String> args = Split(line);
	if (args.empty())
		return Ok(String());
	const String name = args.front();
	args.erase(args.begin());
	auto it = m_commands.find(name.CStr());
	if (it == m_commands.end())
		return Err(String::Format("commande inconnue « %s » (/help)", name.CStr()));
	return it->second.fn(*this, sender.Valid() ? sender : m_localPlayer, args);
}

std::vector<String> Game::Complete(const String &partial) const {
	std::vector<String> out;
	String text = partial.StartsWith("/") ? partial.Substring(1) : partial;
	const size_t space = text.Find(" ");
	if (space == String::NPOS) {
		for (const auto &[name, command] : m_commands)
			if (command.name.StartsWith(text.CStr()))
				out.push_back(String("/") + command.name);
		return out;
	}
	std::vector<String> words = Split(text);
	const String last = text.EndsWith(" ") ? String() : words.back();
	auto offer = [&](const String &id) {
		if (id.StartsWith(last.CStr()) || (!last.Contains(":") && id.Contains((String(":") + last).CStr())))
			out.push_back(id);
	};
	for (const ItemDef *i : m_content.Items())
		offer(i->id);
	for (const EntityDef *e : m_content.Entities())
		offer(e->id);
	for (const RecipeDef *r : m_content.Recipes())
		offer(r->id);
	for (const StructureDef *s : m_content.Structures())
		offer(s->id);
	std::sort(out.begin(), out.end(), [](const String &a, const String &b) { return std::strcmp(a.CStr(), b.CStr()) < 0; });
	out.erase(std::unique(out.begin(), out.end()), out.end());
	if (out.size() > 30)
		out.resize(30);
	return out;
}

String Game::ResolveShortId(const String &id) const {
	if (id.Contains(":"))
		return id;
	const String core = String("core:") + id;
	if (m_content.FindItem(core) || m_content.FindEntity(core) || m_content.FindRecipe(core) || m_content.FindStructure(core))
		return core;
	const String suffix = String(":") + id;
	for (const ItemDef *i : m_content.Items())
		if (i->id.EndsWith(suffix.CStr()))
			return i->id;
	for (const EntityDef *e : m_content.Entities())
		if (e->id.EndsWith(suffix.CStr()))
			return e->id;
	for (const RecipeDef *r : m_content.Recipes())
		if (r->id.EndsWith(suffix.CStr()))
			return r->id;
	for (const StructureDef *st : m_content.Structures())
		if (st->id.EndsWith(suffix.CStr()))
			return st->id;
	return core;
}

void Game::InstallCommands() {
	auto pos = [](Game &g, ecs::Entity e) -> math::FVector3 {
		auto t = g.Registry().GetComponent<Transform>(e);
		return t.IsSome() ? t.Unwrap()->position : math::FVector3{0.f, 0.f, 0.f};
	};
	// Nom court (« planks ») : celui de n'importe quel mod, « core » d'abord.
	auto qualify = [this](const String &id) { return ResolveShortId(id); };
	RegisterCommand("help", "/help [commande]", "liste des commandes", [](Game &g, ecs::Entity, const std::vector<String> &a) -> Result<String, String> {
		String out;
		for (const Command *c : g.Commands())
			if (a.empty() || c->name == a[0])
				out += String::Format("%s — %s\n", c->usage.CStr(), c->help.CStr());
		return Ok(out);
	});
	RegisterCommand("give", "/give <objet> [nombre]", "donne des objets", [qualify](Game &g, ecs::Entity e, const std::vector<String> &a) -> Result<String, String> {
		if (a.empty())
			return Err(String("/give <objet> [nombre]"));
		const String item = qualify(a[0]);
		if (!g.Content().FindItem(item))
			return Err(String::Format("objet inconnu « %s »", item.CStr()));
		const int count = a.size() > 1 ? std::max(1, std::atoi(a[1].CStr())) : 1;
		auto inv = g.Registry().GetComponent<InventoryComponent>(e);
		if (inv.IsNone())
			return Err(String("pas d'inventaire"));
		const int left = inv.Unwrap()->inventory.Add(g.Content(), item, count);
		return Ok(String::Format("%d × %s", count - left, item.CStr()));
	});
	RegisterCommand("tp", "/tp <x> <y> <z>", "téléporte", [](Game &g, ecs::Entity e, const std::vector<String> &a) -> Result<String, String> {
		if (a.size() < 3)
			return Err(String("/tp <x> <y> <z>"));
		auto t = g.Registry().GetComponent<Transform>(e);
		if (t.IsNone())
			return Err(String("aucune entité"));
		t.Unwrap()->position = t.Unwrap()->previous = {float(std::atof(a[0].CStr())), float(std::atof(a[1].CStr())), float(std::atof(a[2].CStr()))};
		return Ok(String("téléporté"));
	});
	RegisterCommand("spawn", "/spawn <entité> [nombre]", "fait apparaître des entités devant soi",
					[pos, qualify](Game &g, ecs::Entity e, const std::vector<String> &a) -> Result<String, String> {
		if (a.empty())
			return Err(String("/spawn <entité> [nombre]"));
		const String id = qualify(a[0]);
		const int count = a.size() > 1 ? std::clamp(std::atoi(a[1].CStr()), 1, 50) : 1;
		math::FVector3 at = pos(g, e);
		if (auto t = g.Registry().GetComponent<Transform>(e); t.IsSome())
			at = at + math::FVector3{std::sin(t.Unwrap()->yaw), 0.f, std::cos(t.Unwrap()->yaw)} * 3.f + math::FVector3{0.f, 0.5f, 0.f};
		int made = 0;
		for (int i = 0; i < count; ++i)
			made += g.SpawnEntity(id, at + math::FVector3{float(i % 3) * 0.8f, 0.f, float(i / 3) * 0.8f}).Valid();
		if (made == 0)
			return Err(String::Format("entité inconnue « %s »", id.CStr()));
		return Ok(String::Format("%d × %s", made, id.CStr()));
	});
	RegisterCommand("time", "/time [0..1]", "heure du jour (0,5 : midi)", [](Game &g, ecs::Entity, const std::vector<String> &a) -> Result<String, String> {
		if (!a.empty())
			g.SetGameTime(std::atof(a[0].CStr()) * double(g.Options().dayLength));
		return Ok(String::Format("heure : %.2f", double(g.DayFraction())));
	});
	RegisterCommand("seed", "/seed", "graine du monde", [](Game &g, ecs::Entity, const std::vector<String> &) -> Result<String, String> {
		return Ok(String::Format("graine %u", g.Metadata().seed));
	});
	RegisterCommand("save", "/save", "enregistre le monde", [](Game &g, ecs::Entity, const std::vector<String> &) -> Result<String, String> {
		Result<bool, String> r = g.Save();
		return r.IsOk() ? Result<String, String>(Ok(String("monde enregistré"))) : Result<String, String>(Err(r.Error()));
	});
	RegisterCommand("recipes", "/recipes", "recettes réalisables avec l'inventaire", [](Game &g, ecs::Entity e, const std::vector<String> &) -> Result<String, String> {
		auto inv = g.Registry().GetComponent<InventoryComponent>(e);
		if (inv.IsNone())
			return Err(String("pas d'inventaire"));
		String out;
		for (const RecipeDef *r : CraftableRecipes(g.Content(), inv.Unwrap()->inventory))
			out += String::Format("%s → %d × %s\n", r->id.CStr(), r->resultCount, r->result.CStr());
		return Ok(out.IsEmpty() ? String("aucune recette réalisable") : out);
	});
	RegisterCommand("craft", "/craft <recette> [fois]", "fabrique depuis l'inventaire", [qualify](Game &g, ecs::Entity e, const std::vector<String> &a) -> Result<String, String> {
		if (a.empty())
			return Err(String("/craft <recette> [fois]"));
		const RecipeDef *recipe = g.Content().FindRecipe(qualify(a[0]));
		if (!recipe)
			return Err(String::Format("recette inconnue « %s »", a[0].CStr()));
		auto inv = g.Registry().GetComponent<InventoryComponent>(e);
		if (inv.IsNone())
			return Err(String("pas d'inventaire"));
		const int made = CraftFromInventory(g.Content(), inv.Unwrap()->inventory, *recipe, a.size() > 1 ? std::max(1, std::atoi(a[1].CStr())) : 1);
		if (made == 0)
			return Err(String("ingrédients insuffisants"));
		return Ok(String::Format("%d × %s fabriqué(s)", made * recipe->resultCount, recipe->result.CStr()));
	});
	RegisterCommand("fly", "/fly", "vol libre (bascule)", [](Game &g, ecs::Entity e, const std::vector<String> &) -> Result<String, String> {
		auto body = g.Registry().GetComponent<PhysicsBody>(e);
		if (body.IsNone())
			return Err(String("aucune entité"));
		body.Unwrap()->flies = !body.Unwrap()->flies;
		return Ok(String(body.Unwrap()->flies ? "vol activé" : "vol désactivé"));
	});
	RegisterCommand("creative", "/creative", "mode créatif (bascule)", [](Game &g, ecs::Entity, const std::vector<String> &) -> Result<String, String> {
		g.SetCreative(!g.Options().creative);
		return Ok(String(g.Options().creative ? "mode créatif" : "mode survie"));
	});
	RegisterCommand("kill", "/kill [rayon]", "tue les créatures proches (ou soi-même)", [pos](Game &g, ecs::Entity e, const std::vector<String> &a) -> Result<String, String> {
		if (a.empty()) {
			g.Kill(e);
			return Ok(String());
		}
		const float r = float(std::atof(a[0].CStr()));
		const math::FVector3 at = pos(g, e);
		std::vector<ecs::Entity> victims;
		g.Registry().Query<Transform, Health>([&](ecs::Entity x, Transform &t, Health &) {
			if (x != e && !g.Registry().HasComponent<Player>(x) && (t.position - at).Length() <= r)
				victims.push_back(x);
		});
		for (ecs::Entity v : victims)
			g.Kill(v);
		return Ok(String::Format("%d entité(s)", int(victims.size())));
	});
	RegisterCommand("setblock", "/setblock <x> <y> <z> <bloc>", "pose un bloc", [qualify](Game &g, ecs::Entity, const std::vector<String> &a) -> Result<String, String> {
		if (a.size() < 4)
			return Err(String("/setblock <x> <y> <z> <bloc>"));
		const String id = qualify(a[3]);
		if (!g.Content().FindBlock(id))
			return Err(String::Format("bloc inconnu « %s »", id.CStr()));
		(void)g.GetWorld().SetBlock({std::atoi(a[0].CStr()), std::atoi(a[1].CStr()), std::atoi(a[2].CStr())}, id, 0, ChangeCause::COMMAND);
		return Ok(String("bloc posé"));
	});
	RegisterCommand("fill", "/fill <x1> <y1> <z1> <x2> <y2> <z2> <bloc>", "remplit une zone (32768 blocs au plus)",
					[qualify](Game &g, ecs::Entity, const std::vector<String> &a) -> Result<String, String> {
		if (a.size() < 7)
			return Err(String("/fill <x1> <y1> <z1> <x2> <y2> <z2> <bloc>"));
		const String id = qualify(a[6]);
		const BlockDef *def = g.Content().FindBlock(id);
		if (!def)
			return Err(String::Format("bloc inconnu « %s »", id.CStr()));
		int v[6];
		for (int i = 0; i < 6; ++i)
			v[i] = std::atoi(a[size_t(i)].CStr());
		const int x0 = std::min(v[0], v[3]), x1 = std::max(v[0], v[3]), y0 = std::min(v[1], v[4]), y1 = std::max(v[1], v[4]);
		const int z0 = std::min(v[2], v[5]), z1 = std::max(v[2], v[5]);
		if ((int64_t(x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1)) > 32768)
			return Err(String("zone trop grande"));
		int count = 0;
		for (int y = y0; y <= y1; ++y)
			for (int z = z0; z <= z1; ++z)
				for (int x = x0; x <= x1; ++x)
					count += g.GetWorld().SetBlock({x, y, z}, def->numeric, 0, ChangeCause::COMMAND);
		return Ok(String::Format("%d bloc(s)", count));
	});
	RegisterCommand("structure", "/structure <id> [rotation]", "pose une structure devant soi",
					[pos, qualify](Game &g, ecs::Entity e, const std::vector<String> &a) -> Result<String, String> {
		if (a.empty())
			return Err(String("/structure <id> [rotation]"));
		const StructureDef *s = g.Content().FindStructure(qualify(a[0]));
		if (!s)
			return Err(String::Format("structure inconnue « %s »", a[0].CStr()));
		math::FVector3 at = pos(g, e);
		if (auto t = g.Registry().GetComponent<Transform>(e); t.IsSome())
			at = at + math::FVector3{std::sin(t.Unwrap()->yaw), 0.f, std::cos(t.Unwrap()->yaw)} * 5.f;
		g.GetWorld().PlaceStructure(*s, BlockPos::FromWorld(at), a.size() > 1 ? std::atoi(a[1].CStr()) : 0, false);
		return Ok(String::Format("structure %s posée", s->id.CStr()));
	});
	RegisterCommand("pos", "/pos", "position, biome et chunk", [pos](Game &g, ecs::Entity e, const std::vector<String> &) -> Result<String, String> {
		const math::FVector3 p = pos(g, e);
		const BlockPos b = BlockPos::FromWorld(p);
		const BiomeDef *biome = g.GetWorld().Generator() ? g.GetWorld().Generator()->BiomeAt(b.x, b.z) : nullptr;
		return Ok(String::Format("%.1f %.1f %.1f — chunk %s — biome %s", double(p.x), double(p.y), double(p.z), ChunkOf(b).ToString().CStr(),
								 biome ? biome->id.CStr() : "?"));
	});
	RegisterCommand("heal", "/heal", "soigne complètement", [](Game &g, ecs::Entity e, const std::vector<String> &) -> Result<String, String> {
		if (auto h = g.Registry().GetComponent<Health>(e); h.IsSome())
			h.Unwrap()->current = h.Unwrap()->max;
		return Ok(String("soigné"));
	});
	RegisterCommand("clear", "/clear", "vide l'inventaire", [](Game &g, ecs::Entity e, const std::vector<String> &) -> Result<String, String> {
		if (auto inv = g.Registry().GetComponent<InventoryComponent>(e); inv.IsSome())
			inv.Unwrap()->inventory.Clear();
		return Ok(String("inventaire vidé"));
	});
	RegisterCommand("mods", "/mods", "mods chargés", [](Game &g, ecs::Entity, const std::vector<String> &) -> Result<String, String> {
		String out;
		for (const ModInfo &m : g.Mods().mods)
			out += String::Format("%s %s — %d définition(s), %d script(s)\n", m.id.CStr(), m.version.CStr(), m.definitions, int(m.scripts.size()));
		return Ok(out);
	});
	RegisterCommand("gc", "/gc", "libère les ressources inutilisées", [](Game &g, ecs::Entity, const std::vector<String> &) -> Result<String, String> {
		const size_t freed = g.Assets().ReleaseUnused();
		return Ok(String::Format("%d ressource(s) libérée(s), %d en mémoire", int(freed), int(g.Assets().ResidentCount())));
	});
	RegisterCommand("stats", "/stats", "systèmes ECS et chunks", [](Game &g, ecs::Entity, const std::vector<String> &) -> Result<String, String> {
		String out = String::Format("%d entité(s), %d chunk(s) chargé(s), %d généré(s)\n", int(g.Registry().EntityCount()),
									int(g.GetWorld().LoadedChunkCount()), int(g.GetWorld().GeneratedChunkCount()));
		for (const ecs::SystemStats &s : g.GetSchedule().Stats())
			out += String::Format("  %-16s %7.1f µs\n", s.name.CStr(), s.lastMicros);
		return Ok(out);
	});
	RegisterCommand("script", "/script <code>", "exécute du code de script", [](Game &g, ecs::Entity, const std::vector<String> &a) -> Result<String, String> {
		String code;
		for (const String &word : a)
			code += word + String(" ");
		return g.Scripts().RunSource(code);
	});
}

} // namespace voxel
