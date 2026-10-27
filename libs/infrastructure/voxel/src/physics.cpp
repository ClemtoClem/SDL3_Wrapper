// Définitions de voxel/physics.hpp
#include <voxel/fluids.hpp>
#include <voxel/physics.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include <ecs/events.hpp>

namespace voxel {

namespace {

constexpr float EPS = 1e-4f;

float Axis(const math::FVector3 &v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }
void SetAxis(math::FVector3 &v, int axis, float value) {
	(axis == 0 ? v.x : axis == 1 ? v.y : v.z) = value;
}

/// Rogne un déplacement `delta` sur l'axe `axis` contre `boxes`.
float ClipAxis(const Box &box, float delta, int axis, const std::vector<Box> &boxes) {
	if (delta == 0.f)
		return 0.f;
	const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
	for (const Box &b : boxes) {
		// Chevauchement strict sur les deux autres axes.
		if (Axis(box.max, a1) <= Axis(b.min, a1) + EPS || Axis(box.min, a1) >= Axis(b.max, a1) - EPS)
			continue;
		if (Axis(box.max, a2) <= Axis(b.min, a2) + EPS || Axis(box.min, a2) >= Axis(b.max, a2) - EPS)
			continue;
		if (delta > 0.f && Axis(box.max, axis) <= Axis(b.min, axis) + EPS)
			delta = std::min(delta, Axis(b.min, axis) - Axis(box.max, axis));
		else if (delta < 0.f && Axis(box.min, axis) >= Axis(b.max, axis) - EPS)
			delta = std::max(delta, Axis(b.max, axis) - Axis(box.min, axis));
	}
	return delta;
}

Box Expand(const Box &box, const math::FVector3 &delta) {
	Box out = box;
	for (int a = 0; a < 3; ++a) {
		const float d = Axis(delta, a);
		if (d < 0.f)
			SetAxis(out.min, a, Axis(out.min, a) + d);
		else
			SetAxis(out.max, a, Axis(out.max, a) + d);
	}
	return out;
}

} // namespace

math::FVector3 Transform::Forward() const noexcept {
	return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
}

Box RotateBlockBox(const Box &box, uint8_t param) noexcept {
	Box out = box;
	for (int r = 0; r < (param & 3); ++r) {
		// Quart de tour autour de l'axe vertical passant par le centre du bloc.
		const float x0 = 1.f - out.max.z, x1 = 1.f - out.min.z, z0 = out.min.x, z1 = out.max.x;
		out.min.x = x0;
		out.max.x = x1;
		out.min.z = z0;
		out.max.z = z1;
	}
	return out;
}

BlockPos RotateOffset(const BlockPos &offset, uint8_t param) noexcept {
	BlockPos out = offset;
	for (int r = 0; r < (param & 3); ++r)
		out = BlockPos{-out.z, out.y, out.x}; // centre (x+½, z+½) → (1−z−½, x+½)
	return out;
}

uint8_t FacingParam(float yaw) noexcept {
	// Un quart de tour de RotateBlockBox va de +z vers −x, le regard de +z
	// vers +x : sens opposés.
	const int quarter = ((int(std::lround(yaw / 1.5707963f)) % 4) + 4) % 4;
	return uint8_t((4 - quarter) % 4);
}

void CollectBlockBoxes(const World &world, const Box &region, std::vector<Box> &out) {
	const ContentRegistry &content = world.Content();
	// Une nodebox peut dépasser de sa cellule (jusqu'à un bloc) : marge d'un bloc.
	const int x0 = int(std::floor(region.min.x)) - 1, x1 = int(std::floor(region.max.x)) + 1;
	const int y0 = int(std::floor(region.min.y)) - 1, y1 = int(std::floor(region.max.y)) + 1;
	const int z0 = int(std::floor(region.min.z)) - 1, z1 = int(std::floor(region.max.z)) + 1;
	for (int y = y0; y <= y1; ++y)
		for (int z = z0; z <= z1; ++z)
			for (int x = x0; x <= x1; ++x) {
				const BlockPos p{x, y, z};
				const math::FVector3 o = p.ToVector();
				if (!world.IsLoaded(p)) {
					const Box full{o, o + math::FVector3{1.f, 1.f, 1.f}};
					if (full.Intersects(region))
						out.push_back(full);
					continue;
				}
				const BlockDef &def = content.Block(world.GetBlock(p));
				if (!def.solid || def.collision == CollisionKind::NONE)
					continue;
				if (def.collision == CollisionKind::FULL_CUBE) {
					const Box full{o, o + math::FVector3{1.f, 1.f, 1.f}};
					if (full.Intersects(region))
						out.push_back(full);
					continue;
				}
				const uint8_t param = world.GetParam(p);
				for (const Box &local : def.collisionBoxes) {
					const Box b = RotateBlockBox(local, param).Offset(o);
					if (b.Intersects(region))
						out.push_back(b);
				}
			}
}

std::vector<Box> SelectionBoxes(const World &world, const BlockPos &p) {
	std::vector<Box> out;
	const BlockDef &def = world.BlockAt(p);
	const math::FVector3 o = p.ToVector();
	if (def.IsAir())
		return out;
	if (def.shape.kind == ShapeKind::NODEBOX && !def.shape.boxes.empty()) {
		for (const Box &b : def.shape.boxes)
			out.push_back(RotateBlockBox(b, world.GetParam(p)).Offset(o));
	} else if (def.collision == CollisionKind::BOXES && !def.collisionBoxes.empty()) {
		for (const Box &b : def.collisionBoxes)
			out.push_back(RotateBlockBox(b, world.GetParam(p)).Offset(o));
	} else if (def.shape.kind == ShapeKind::CROSS) {
		out.push_back(Box{o + math::FVector3{0.15f, 0.f, 0.15f}, o + math::FVector3{0.85f, 0.8f, 0.85f}});
	} else {
		out.push_back(Box{o, o + math::FVector3{1.f, 1.f, 1.f}});
	}
	return out;
}

MoveResult MoveBox(const World &world, Box &box, const math::FVector3 &delta, float stepHeight, bool onGround) {
	std::vector<Box> boxes;
	Box region = Expand(box, delta);
	if (stepHeight > 0.f) // la marche éventuelle doit être dans la liste
		region = Expand(region, {0.f, stepHeight, 0.f});
	CollectBlockBoxes(world, region, boxes);

	// Un essai : Y d'abord, puis l'axe horizontal dominant (glisser le long
	// des murs reste naturel), puis l'autre.
	auto attempt = [&](const Box &start, const math::FVector3 &d, MoveResult &r) {
		Box b = start;
		const float dy = ClipAxis(b, d.y, 1, boxes);
		b = b.Offset({0.f, dy, 0.f});
		r.hitY = std::fabs(dy - d.y) > 1e-6f;
		r.landed = r.hitY && d.y < 0.f;
		const int first = std::fabs(d.x) >= std::fabs(d.z) ? 0 : 2, second = 2 - first;
		for (int axis : {first, second}) {
			const float want = Axis(d, axis);
			const float got = ClipAxis(b, want, axis, boxes);
			math::FVector3 step{0.f, 0.f, 0.f};
			SetAxis(step, axis, got);
			b = b.Offset(step);
			if (std::fabs(got - want) > 1e-6f)
				(axis == 0 ? r.hitX : r.hitZ) = true;
		}
		return b;
	};

	MoveResult result;
	Box final = attempt(box, delta, result);
	// Montée de marche : bloqué à l'horizontale en étant au sol → on rejoue
	// le mouvement horizontal depuis `stepHeight` plus haut, puis on
	// redescend ; retenu s'il va plus loin.
	if (stepHeight > 0.f && (onGround || result.landed) && (result.hitX || result.hitZ)) {
		Box raised = box;
		const float rise = ClipAxis(raised, stepHeight, 1, boxes);
		raised = raised.Offset({0.f, rise, 0.f});
		MoveResult horizontal;
		Box stepped = attempt(raised, {delta.x, 0.f, delta.z}, horizontal);
		const float down = ClipAxis(stepped, -rise + std::min(delta.y, 0.f), 1, boxes);
		stepped = stepped.Offset({0.f, down, 0.f});
		const float gainedStep = std::hypot(stepped.min.x - box.min.x, stepped.min.z - box.min.z);
		const float gainedFlat = std::hypot(final.min.x - box.min.x, final.min.z - box.min.z);
		if (gainedStep > gainedFlat + 1e-4f) {
			result.hitX = horizontal.hitX;
			result.hitZ = horizontal.hitZ;
			result.hitY = true;
			result.landed = true;
			result.stepped = true;
			final = stepped;
		}
	}
	result.moved = {final.min.x - box.min.x, final.min.y - box.min.y, final.min.z - box.min.z};
	box = final;
	return result;
}

Option<float> RayBox(const math::FVector3 &origin, const math::FVector3 &direction, const Box &box, Face *face) {
	float tMin = -1e30f, tMax = 1e30f;
	int enterAxis = 1;
	bool enterPositive = false;
	for (int a = 0; a < 3; ++a) {
		const float o = Axis(origin, a), d = Axis(direction, a), lo = Axis(box.min, a), hi = Axis(box.max, a);
		if (std::fabs(d) < 1e-9f) {
			if (o < lo || o > hi)
				return NONE;
			continue;
		}
		float t0 = (lo - o) / d, t1 = (hi - o) / d;
		bool positive = false; // entrée par la face « max » de l'axe
		if (t0 > t1) {
			std::swap(t0, t1);
			positive = true;
		}
		if (t0 > tMin) {
			tMin = t0;
			enterAxis = a;
			enterPositive = positive;
		}
		tMax = std::min(tMax, t1);
		if (tMin > tMax)
			return NONE;
	}
	if (tMax < 0.f)
		return NONE;
	if (face) {
		const Face faces[3][2] = {{Face::WEST, Face::EAST}, {Face::BOTTOM, Face::TOP}, {Face::NORTH, Face::SOUTH}};
		*face = faces[enterAxis][enterPositive ? 1 : 0];
	}
	return Some(std::max(tMin, 0.f));
}

Option<BlockHit> RaycastBlocks(const World &world, const math::FVector3 &origin, const math::FVector3 &dirIn, float maxDistance,
							   bool includeFluids, const std::function<bool(const BlockDef &)> &filter) {
	const float length = dirIn.Length();
	if (length < 1e-9f)
		return NONE;
	const math::FVector3 dir = dirIn * (1.f / length);
	BlockPos cell = BlockPos::FromWorld(origin);
	BlockPos previous = cell;
	const int step[3] = {dir.x > 0 ? 1 : -1, dir.y > 0 ? 1 : -1, dir.z > 0 ? 1 : -1};
	float tNext[3], tDelta[3];
	for (int a = 0; a < 3; ++a) {
		const float d = Axis(dir, a), o = Axis(origin, a);
		const int c = a == 0 ? cell.x : a == 1 ? cell.y : cell.z;
		tDelta[a] = std::fabs(d) < 1e-9f ? 1e30f : std::fabs(1.f / d);
		tNext[a] = std::fabs(d) < 1e-9f ? 1e30f : ((d > 0 ? float(c + 1) - o : o - float(c)) * tDelta[a]);
	}
	float t = 0.f;
	for (int guard = 0; guard < 4096 && t <= maxDistance; ++guard) {
		const BlockDef &def = world.BlockAt(cell);
		const bool candidate = !def.IsAir() && def.shape.kind != ShapeKind::NONE && def.selectable && (includeFluids || !def.fluid) &&
							   (!filter || filter(def));
		if (candidate) {
			Option<BlockHit> best = NONE;
			for (const Box &b : SelectionBoxes(world, cell)) {
				Face face = Face::TOP;
				if (Option<float> hit = RayBox(origin, dir, b, &face); hit.IsSome() && hit.Value() <= maxDistance)
					if (best.IsNone() || hit.Value() < best.Value().distance)
						best = Some(BlockHit{cell, previous, face, origin + dir * hit.Value(), hit.Value()});
			}
			if (best.IsSome()) {
				best.Value().previous = cell.Neighbor(best.Value().face);
				return best;
			}
		}
		previous = cell;
		const int axis = tNext[0] < tNext[1] ? (tNext[0] < tNext[2] ? 0 : 2) : (tNext[1] < tNext[2] ? 1 : 2);
		t = tNext[axis];
		tNext[axis] += tDelta[axis];
		(axis == 0 ? cell.x : axis == 1 ? cell.y : cell.z) += step[axis];
	}
	return NONE;
}

Option<EntityHit> RaycastEntities(ecs::ArchetypeRegistry &registry, const math::FVector3 &origin, const math::FVector3 &direction,
								  float maxDistance, ecs::Entity ignore) {
	Option<EntityHit> best = NONE;
	const math::FVector3 dir = direction.Normalize();
	registry.Query<Transform, Collider>([&](ecs::Entity e, Transform &t, Collider &c) {
		if (e == ignore)
			return;
		if (Option<float> d = RayBox(origin, dir, c.WorldBox(t.position)); d.IsSome() && d.Value() <= maxDistance)
			if (best.IsNone() || d.Value() < best.Value().distance)
				best = Some(EntityHit{e, d.Value(), origin + dir * d.Value()});
	});
	return best;
}

void StepPhysics(ecs::ArchetypeRegistry &registry, const World &world, float dt, const PhysicsSettings &settings) {
	const ContentRegistry &content = world.Content();
	auto &blockContacts = ecs::EventsOf<BlockContact>(registry);
	registry.Query<Transform, Velocity, Collider, PhysicsBody>([&](ecs::Entity e, Transform &t, Velocity &v, Collider &c,
																   PhysicsBody &body) {
		t.previous = t.position;
		if (!world.IsLoaded(BlockPos::FromWorld(t.position)))
			return; // en attente du terrain
		// Milieu : fluide et échelle au niveau du corps.
		const BlockPos mid = BlockPos::FromWorld(t.position + math::FVector3{0.f, c.size.y * 0.4f, 0.f});
		const BlockDef &medium = world.BlockAt(mid);
		const BlockDef &feet = world.BlockAt(BlockPos::FromWorld(t.position + math::FVector3{0.f, 0.05f, 0.f}));
		body.inFluid = medium.fluid || feet.fluid;
		body.fluidViscosity = std::max(medium.fluid ? medium.viscosity : 0.f, feet.fluid ? feet.viscosity : 0.f);
		body.onClimbable = medium.climbable || feet.climbable;
		if (feet.fluid || (!feet.IsAir() && !feet.solid))
			blockContacts.Send(BlockContact{e, BlockPos::FromWorld(t.position), feet.numeric, false, 0.f});
		const float gdir = body.gravityDirection < 0.f ? -1.f : 1.f;
		// Sables mouvants : enfoncement lent, presque aucune prise.
		const float sinking = std::max(medium.sinking, feet.sinking);
		body.inSinking = sinking > 0.f && !body.flies && !body.noclip;
		// Pris dans un bloc plein (sable tombé sur la tête, téléporté dans un
		// mur…) : immobilisé jusqu'à ce que le bloc soit cassé.
		const float headOffset = gdir > 0.f ? c.size.y * 0.85f : c.size.y * 0.15f;
		const BlockPos headCell = BlockPos::FromWorld(t.position + math::FVector3{0.f, headOffset, 0.f});
		const BlockDef &head = world.BlockAt(headCell);
		body.trapped = !body.noclip && !body.flies && world.IsLoaded(headCell) && head.solid && !head.fluid &&
					   head.collision == CollisionKind::FULL_CUBE && registry.HasComponent<Player>(e);
		if (body.trapped) {
			v.linear = {0.f, 0.f, 0.f};
			body.onGround = true;
			body.fallDistance = 0.f;
			return;
		}

		// Intention de déplacement → accélération horizontale.
		MoveIntent intent;
		const bool hasIntent = registry.GetComponent<MoveIntent>(e).IsSome();
		if (hasIntent)
			intent = *registry.GetComponent<MoveIntent>(e).Unwrap();
		const float wishLength = std::min(1.f, std::hypot(intent.wish.x, intent.wish.z));
		const float speed = (hasIntent ? intent.speed : 0.f) * (intent.sprint ? 1.3f : 1.f) * (intent.sneak ? 0.35f : 1.f) *
							(body.inFluid ? (1.f - body.fluidViscosity * 0.6f) : 1.f) * (body.inSinking ? 0.15f : 1.f);
		math::FVector3 target{0.f, 0.f, 0.f};
		if (wishLength > 1e-4f)
			target = math::FVector3{intent.wish.x, 0.f, intent.wish.z} * (speed / std::max(std::hypot(intent.wish.x, intent.wish.z), 1e-6f) * wishLength);
		// Courant : un liquide qui coule entraîne ce qui s'y trouve (objets,
		// créatures, joueur qui ne nage pas contre).
		body.current = {0.f, 0.f, 0.f};
		if (body.inFluid && !body.flies && !body.noclip) {
			const BlockPos cell = medium.fluid ? mid : BlockPos::FromWorld(t.position + math::FVector3{0.f, 0.05f, 0.f});
			const math::FVector3 flow = LiquidFlow(world, cell);
			const float strength = world.BlockAt(cell).liquid.current;
			body.current = flow * strength;
			target.x += flow.x * strength;
			target.z += flow.z * strength;
		}
		// Contrôle : plein au sol (réduit par la glissance), partiel en l'air.
		float control = body.onGround ? settings.groundAcceleration * (1.f - 0.92f * body.groundSlipperiness)
									  : settings.groundAcceleration * settings.airControl;
		if (body.inFluid || body.flies)
			control = settings.groundAcceleration * 0.5f;
		const float k = std::min(1.f, control * dt);
		v.linear.x += (target.x - v.linear.x) * k;
		v.linear.z += (target.z - v.linear.z) * k;

		// Vertical : gravité, fluide, vol, échelle, saut.
		if (body.flies) {
			const float vertical = intent.jump ? speed : intent.descend ? -speed : 0.f;
			v.linear.y += (vertical - v.linear.y) * std::min(1.f, 8.f * dt);
		} else if (body.inSinking) {
			// On s'enfonce ; en se débattant (saut), on remonte lentement tant
			// que les yeux sont à l'air — la tête prise, il est trop tard.
			const BlockPos eyes = BlockPos::FromWorld(t.position + math::FVector3{0.f, gdir > 0.f ? c.size.y * 0.9f : c.size.y * 0.1f, 0.f});
			const bool submerged = world.BlockAt(eyes).sinking > 0.f;
			v.linear.y = (intent.jump && !submerged ? sinking * 1.6f : -sinking) * gdir;
			v.linear.x *= std::max(0.f, 1.f - 6.f * dt);
			v.linear.z *= std::max(0.f, 1.f - 6.f * dt);
		} else if (body.onClimbable && (intent.jump || intent.wish.Length() > 0.1f)) {
			v.linear.y = (intent.descend || intent.sneak ? -2.f : 2.5f) * gdir;
		} else {
			float g = settings.gravity * body.gravityScale;
			if (body.inFluid)
				g *= 1.f - settings.fluidBuoyancy;
			v.linear.y = std::clamp(v.linear.y - g * dt * gdir, -settings.terminalVelocity, settings.terminalVelocity);
			if (body.inFluid) {
				v.linear.y *= std::max(0.f, 1.f - body.fluidViscosity * 4.f * dt);
				v.linear.y += body.current.y * 6.f * dt; // une chute d'eau pousse vers le bas
				if (intent.jump && body.swims)
					v.linear.y = gdir > 0.f ? std::min(v.linear.y + 30.f * dt, 3.f) : std::max(v.linear.y - 30.f * dt, -3.f);
			} else if (intent.jump && body.onGround) {
				v.linear.y = body.jumpSpeed * gdir;
			}
		}
		if (body.drag > 0.f) {
			const float d = std::max(0.f, 1.f - body.drag * dt);
			v.linear = v.linear * d;
		}

		// Déplacement contre les blocs.
		Box box = c.WorldBox(t.position);
		if (body.noclip) {
			t.position = t.position + v.linear * dt;
			return;
		}
		// Gravité inversée : pas de montée de marche (elle suppose le sol en bas).
		const float stepHeight = intent.sneak || gdir < 0.f ? 0.f : body.stepHeight;
		const MoveResult moved = MoveBox(world, box, v.linear * dt, stepHeight, body.onGround);
		t.position = {box.Center().x, box.min.y, box.Center().z};
		const bool wasOnGround = body.onGround;
		// « Sol » : du côté où tire la gravité.
		const bool landed = gdir > 0.f ? moved.landed : moved.hitY && v.linear.y > 0.f;
		body.onGround = landed;
		body.hitWall = moved.hitX || moved.hitZ;
		body.hitCeiling = moved.hitY && !landed;
		if (moved.hitX)
			v.linear.x = -v.linear.x * body.bounciness;
		if (moved.hitZ)
			v.linear.z = -v.linear.z * body.bounciness;
		const float impact = landed ? std::fabs(v.linear.y) : 0.f;
		if (moved.hitY) {
			v.linear.y = landed && std::fabs(v.linear.y) > 1.f ? -v.linear.y * body.bounciness : 0.f;
			if (std::fabs(v.linear.y) < 0.5f)
				v.linear.y = 0.f;
		}
		// Sol : bloc sous les pieds (ou au-dessus de la tête, gravité
		// inversée) — glissance, événements d'atterrissage.
		if (body.onGround) {
			const BlockPos below = gdir > 0.f ? BlockPos::FromWorld(t.position - math::FVector3{0.f, 0.05f, 0.f})
											  : BlockPos::FromWorld(t.position + math::FVector3{0.f, c.size.y + 0.05f, 0.f});
			const BlockDef &ground = world.BlockAt(below);
			body.groundBlock = ground.numeric;
			body.groundSlipperiness = ground.slipperiness;
			if (!wasOnGround) {
				body.lastImpactSpeed = impact;
				blockContacts.Send(BlockContact{e, below, ground.numeric, true, body.lastImpactSpeed});
				body.fallDistance = 0.f;
			}
		} else {
			body.groundSlipperiness = 0.f;
			if (moved.moved.y * gdir < 0.f && !body.inFluid && !body.inSinking)
				body.fallDistance += std::fabs(moved.moved.y);
			else if (body.inFluid)
				body.fallDistance = 0.f;
		}
		(void)content;
	});

	// Contacts entre entités : grille de hachage (cellules de 2 blocs).
	struct Item {
		ecs::Entity e;
		Box box;
		bool pushable;
	};
	std::vector<Item> items;
	registry.Query<Transform, Collider>([&](ecs::Entity e, Transform &t, Collider &c) {
		if (c.collidesWithEntities)
			items.push_back({e, c.WorldBox(t.position), c.pushable});
	});
	std::unordered_map<int64_t, std::vector<size_t>> grid;
	auto key = [](int x, int z) { return (int64_t(x) << 32) ^ int64_t(uint32_t(z)); };
	for (size_t i = 0; i < items.size(); ++i) {
		const Box &b = items[i].box;
		for (int x = int(std::floor(b.min.x / 2.f)); x <= int(std::floor(b.max.x / 2.f)); ++x)
			for (int z = int(std::floor(b.min.z / 2.f)); z <= int(std::floor(b.max.z / 2.f)); ++z)
				grid[key(x, z)].push_back(i);
	}
	auto &contacts = ecs::EventsOf<EntityContact>(registry);
	std::vector<std::pair<size_t, size_t>> seen;
	for (auto &[cell, list] : grid)
		for (size_t i = 0; i < list.size(); ++i)
			for (size_t j = i + 1; j < list.size(); ++j) {
				const size_t a = std::min(list[i], list[j]), b = std::max(list[i], list[j]);
				if (!items[a].box.Intersects(items[b].box))
					continue;
				if (std::find(seen.begin(), seen.end(), std::make_pair(a, b)) != seen.end())
					continue;
				seen.push_back({a, b});
				contacts.Send(EntityContact{items[a].e, items[b].e});
				// Repousse horizontalement, chacun de la moitié (si poussable).
				const math::FVector3 ca = items[a].box.Center(), cb = items[b].box.Center();
				math::FVector3 d{ca.x - cb.x, 0.f, ca.z - cb.z};
				const float len = d.Length();
				d = len > 1e-4f ? d * (1.f / len) : math::FVector3{1.f, 0.f, 0.f};
				const float push = 2.5f * dt;
				for (auto [index, sign] : {std::pair{a, 1.f}, std::pair{b, -1.f}})
					if (items[index].pushable)
						if (auto v = registry.GetComponent<Velocity>(items[index].e); v.IsSome())
							v.Unwrap()->linear = v.Unwrap()->linear + d * (sign * push * 20.f);
			}
}

} // namespace voxel
