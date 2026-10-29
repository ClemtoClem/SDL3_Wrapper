// Définitions de voxel/entities.hpp
#include <voxel/entities.hpp>
#include <voxel/schematic.hpp>
#include <voxel/json.hpp>
#include <voxel/worldgen.hpp>

#include <cmath>

#include <ecs/events.hpp>

namespace voxel {

namespace {

Health MakeHealth(float max) {
	Health h;
	h.current = h.max = max;
	return h;
}

Player MakePlayer(const String &name) {
	Player p;
	p.name = name;
	return p;
}

} // namespace


namespace {

float Param(const BehaviorState &s, const char *key, float fallback) { return float(json::Num(s.params, key, fallback)); }

float YawTowards(const math::FVector3 &from, const math::FVector3 &to) { return std::atan2(to.x - from.x, to.z - from.z); }

void Steer(MoveIntent &intent, Transform &t, const math::FVector3 &towards, float speed) {
	math::FVector3 d{towards.x - t.position.x, 0.f, towards.z - t.position.z};
	const float len = d.Length();
	if (len < 0.3f) {
		intent.wish = {0.f, 0.f, 0.f};
		return;
	}
	intent.wish = d * (1.f / len);
	intent.speed = speed;
	t.yaw = std::atan2(d.x, d.z);
}

} // namespace

ecs::Entity SpawnFromDef(ecs::ArchetypeRegistry &registry, const EntityDef &def, const math::FVector3 &feet) {
	Transform t;
	t.position = t.previous = feet;
	Collider collider;
	collider.size = def.colliderSize;
	collider.collidesWithEntities = def.collidesWithEntities;
	collider.pushable = def.pushable;
	PhysicsBody body;
	body.mass = def.mass;
	body.gravityScale = def.gravityScale;
	body.friction = def.friction;
	body.bounciness = def.bounciness;
	body.drag = def.drag;
	body.stepHeight = def.stepHeight;
	body.jumpSpeed = def.jumpSpeed;
	body.maxSpeed = def.maxSpeed;
	body.swims = def.swims;
	body.flies = def.flies;
	MoveIntent intent;
	intent.speed = def.maxSpeed;
	Visual visual{def.shape, def.look, AIR, 1.f, true, def.rig, String(), nullptr, 0};
	if (!def.rig.skins.empty()) // variante tirée selon la position d'apparition
		visual.skin = def.rig.skins[HashPosition(0x5eedu, int(std::floor(feet.x)), int(std::floor(feet.y)), int(std::floor(feet.z)), 3) %
									def.rig.skins.size()];
	Behaviors behaviors;
	for (const BehaviorRef &b : def.behaviors)
		behaviors.list.push_back(BehaviorState{b.id, b.params});
	const ecs::Entity e = registry.SpawnBundle(t, Velocity{}, collider, body, intent, EntityKind{def.id, def.kind, &def},
											   MakeHealth(def.health), visual,
											   Properties{def.raw ? def.raw->Clone() : dataio::Node::MakeObject()},
											   SoundEmitter{def.sounds, 3.f}, behaviors);
	if (def.persistent)
		registry.AddComponent(e, Persistent{Uuid::Generate()});
	ecs::EventsOf<ecs::EntitySpawned>(registry).Send({e});
	return e;
}

ecs::Entity SpawnPlayer(ecs::ArchetypeRegistry &registry, const String &name, const math::FVector3 &feet) {
	Transform t;
	t.position = t.previous = feet;
	Collider collider;
	collider.size = {0.6f, 1.8f, 0.6f};
	PhysicsBody body;
	body.jumpSpeed = 8.4f;
	body.maxSpeed = 4.3f;
	Visual visual;
	visual.visible = false; // vue à la première personne
	const ecs::Entity e = registry.SpawnBundle(t, Velocity{}, collider, body, MoveIntent{}, EntityKind{PLAYER_KIND, "player", nullptr},
											   MakeHealth(20.f), visual, MakePlayer(name), InventoryComponent{Inventory{36}}, Survival{},
											   StatusEffects{}, Equipment{},
											   Named{name}, Properties{dataio::Node::MakeObject()});
	ecs::EventsOf<ecs::EntitySpawned>(registry).Send({e});
	return e;
}

ecs::Entity SpawnDroppedItem(ecs::ArchetypeRegistry &registry, const ContentRegistry &content, ItemStack stack,
							 const math::FVector3 &position, const math::FVector3 &velocity) {
	Transform t;
	t.position = t.previous = position;
	Collider collider;
	collider.size = {0.25f, 0.25f, 0.25f};
	collider.collidesWithEntities = false;
	PhysicsBody body;
	body.drag = 0.5f;
	body.stepHeight = 0.f;
	Visual visual;
	visual.scale = 0.25f;
	if (const ItemDef *item = content.FindItem(stack.item); item && !item->placesBlock.IsEmpty())
		visual.block = content.BlockIdOf(item->placesBlock);
	visual.shape.kind = ShapeKind::CUBE;
	if (const ItemDef *item = content.FindItem(stack.item))
		for (String &face : visual.look.faces)
			face = item->icon;
	return registry.SpawnBundle(t, Velocity{velocity}, collider, body, EntityKind{ITEM_KIND, "item", nullptr}, visual,
								DroppedItem{std::move(stack), 0.6f, 0.f}, Persistent{Uuid::Generate()});
}

ecs::Entity SpawnStructure(ecs::ArchetypeRegistry &registry, std::shared_ptr<const Schematic> structure, const math::FVector3 &corner,
						   float floor) {
	static uint64_t serial = 0;
	Transform t;
	t.position = t.previous = corner;
	const BlockPos size = structure->Size();
	Carrier carrier;
	carrier.ride = Box{{0.f, 0.f, 0.f}, {float(size.x), float(size.y), float(size.z)}};
	carrier.floor = floor;
	Visual visual;
	visual.shape.kind = ShapeKind::NONE;
	visual.structure = std::move(structure);
	visual.structureSerial = ++serial;
	return registry.SpawnBundle(t, Velocity{}, carrier, EntityKind{STRUCTURE_KIND, "structure", nullptr}, visual);
}

ecs::Entity SpawnFallingBlock(ecs::ArchetypeRegistry &registry, const ContentRegistry &content, BlockId block, uint8_t param,
							  const BlockPos &from) {
	Transform t;
	t.position = t.previous = {float(from.x) + 0.5f, float(from.y), float(from.z) + 0.5f};
	Collider collider;
	collider.size = {0.98f, 0.98f, 0.98f};
	collider.collidesWithEntities = false;
	PhysicsBody body;
	body.stepHeight = 0.f;
	Visual visual;
	visual.block = block;
	visual.shape = content.Block(block).shape;
	visual.look = content.Block(block).look;
	return registry.SpawnBundle(t, Velocity{}, collider, body, EntityKind{FALLING_BLOCK_KIND, "falling_block", nullptr}, visual,
								FallingBlock{block, param});
}

Option<ecs::Entity> NearestPlayer(ecs::ArchetypeRegistry &registry, const math::FVector3 &from, float range) {
	Option<ecs::Entity> best = NONE;
	float bestDistance = range * range;
	registry.Query<Transform, Player>([&](ecs::Entity e, Transform &t, Player &) {
		const math::FVector3 d = t.position - from;
		const float d2 = d.Dot(d);
		if (d2 <= bestDistance) {
			bestDistance = d2;
			best = Some(e);
		}
	});
	return best;
}

void StepBehaviors(ecs::ArchetypeRegistry &registry, const World &world, float dt, uint32_t seed, const ScriptBehavior &scriptBehavior) {
	// Positions des joueurs et objet en main (follow), relevées une fois.
	struct PlayerInfo {
		ecs::Entity entity;
		math::FVector3 position;
		String held;
	};
	std::vector<PlayerInfo> players;
	registry.Query<Transform, Player, InventoryComponent>([&](ecs::Entity e, Transform &t, Player &p, InventoryComponent &inv) {
		const ItemStack &slot = inv.inventory.Slot(std::clamp(p.selectedSlot, 0, inv.inventory.Size() - 1));
		players.push_back({e, t.position, slot.item});
	});
	auto nearest = [&](const math::FVector3 &from, float range) -> const PlayerInfo * {
		const PlayerInfo *best = nullptr;
		float bestDistance = range * range;
		for (const PlayerInfo &p : players) {
			const math::FVector3 d = p.position - from;
			if (d.Dot(d) <= bestDistance) {
				bestDistance = d.Dot(d);
				best = &p;
			}
		}
		return best;
	};
	uint64_t counter = 0;
	std::vector<std::pair<ecs::Entity, size_t>> deferred;
	registry.Query<Transform, MoveIntent, Behaviors, PhysicsBody>([&](ecs::Entity e, Transform &t, MoveIntent &intent,
																	  Behaviors &behaviors, PhysicsBody &body) {
		intent.jump = false;
		bool steered = false; // un comportement prioritaire (suivre, fuir) a décidé du mouvement
		for (BehaviorState &s : behaviors.list) {
			s.timer -= dt;
			const String &id = s.id;
			if (id == "follow") {
				const PlayerInfo *p = nearest(t.position, Param(s, "range", 8.f));
				const String item = ContentRegistry::Qualify(json::Str(s.params, "item"), "core");
				if (p && (json::Str(s.params, "item").IsEmpty() || p->held == item)) {
					Steer(intent, t, p->position, Param(s, "speed", intent.speed));
					steered = true;
				}
			} else if (id == "flee") {
				if (const PlayerInfo *p = nearest(t.position, Param(s, "range", 6.f))) {
					const math::FVector3 away = t.position * 2.f - p->position;
					Steer(intent, t, away, Param(s, "speed", intent.speed * 1.4f));
					steered = true;
				}
			} else if (id == "wander" && !steered) {
				if (s.timer <= 0.f) {
					const float r = RandomAt(seed, int(e.id), int(++counter), int(t.position.x * 7.f), 3);
					if (r < 0.4f) {
						s.hasTarget = false; // pause
					} else {
						const float radius = Param(s, "radius", 6.f);
						const float angle = RandomAt(seed, int(e.id), int(counter), 5, 11) * 6.2831853f;
						s.target = t.position + math::FVector3{std::sin(angle) * radius, 0.f, std::cos(angle) * radius};
						s.hasTarget = true;
					}
					s.timer = Param(s, "pause", 3.f) * (0.5f + RandomAt(seed, int(e.id), int(counter), 9, 13));
				}
				if (s.hasTarget) {
					Steer(intent, t, s.target, Param(s, "speed", intent.speed * 0.5f));
					if ((t.position - s.target).Length() < 0.6f)
						s.hasTarget = false;
					// Bloqué contre un obstacle d'un bloc : sauter.
					if (body.hitWall && body.onGround)
						intent.jump = true;
				} else {
					intent.wish = {0.f, 0.f, 0.f};
				}
			} else if (id == "hop") {
				if (s.timer <= 0.f && body.onGround) {
					intent.jump = true;
					s.timer = Param(s, "interval", 1.5f);
				}
			} else if (id == "float") {
				if (body.inFluid)
					intent.jump = true;
			} else if (id == "look_at_player") {
				if (const PlayerInfo *p = nearest(t.position, Param(s, "range", 8.f)); p && !steered)
					t.yaw = YawTowards(t.position, p->position);
			} else if (id == "idle") {
			} else if (scriptBehavior) {
				deferred.push_back({e, size_t(&s - behaviors.list.data())});
			}
		}
		(void)world;
	});
	// Comportements des mods : APRÈS la requête (un script peut créer ou
	// détruire des entités, ce qui invaliderait l'itération).
	for (const auto &[e, index] : deferred) {
		auto behaviors = registry.GetComponent<Behaviors>(e);
		if (behaviors.IsNone() || index >= behaviors.Unwrap()->list.size())
			continue;
		BehaviorState state = behaviors.Unwrap()->list[index];
		(void)scriptBehavior(e, state, dt);
		if (auto again = registry.GetComponent<Behaviors>(e); again.IsSome() && index < again.Unwrap()->list.size())
			again.Unwrap()->list[index] = std::move(state);
	}
}

} // namespace voxel
