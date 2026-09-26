#pragma once
/**
 * game_editor — contenu de démonstration : le projet par défaut (trois
 * scènes) et les scripts qui les font vivre.
 *
 * Les scripts sont EMBARQUÉS dans le binaire (pas des fichiers d'assets) pour
 * que la démo soit exécutable telle quelle depuis n'importe quel répertoire,
 * y compris en intégration continue ; `--script=CHEMIN` permet malgré tout
 * d'en charger un depuis le disque, et `editor.save`/`editor.open` écrivent et
 * relisent un projet complet en JSON.
 *
 * Répartition volontaire du travail entre C++ et script :
 *  - le C++ pose la GÉOMÉTRIE (piste, décor, empilements) — déterministe,
 *    typée, reconstructible à l'identique ;
 *  - le script pose le COMPORTEMENT (animations, conduite, tours de piste,
 *    apparitions) — c'est exactement le partage qu'on trouve dans un vrai
 *    moteur, et ce qui rend le langage embarqué utile plutôt que décoratif.
 */
#include <cmath>
#include <unordered_map>
#include <vector>

#include "core/core.hpp"
#include "math/math.hpp"

#include "project.hpp"

namespace game_editor {

// ============================================================================
// Scripts de gameplay / d'animation
// ============================================================================

/// Vitrine : animations pures (flottement, rotations, pulsation de couleur).
/// Montre qu'on programme une animation sans toucher au C++.
inline constexpr const char *SHOWCASE_SCRIPT = R"SLED(
# ── Scène « Vitrine » : animations pilotées par script ──────────────────────
let t = 0
let spin_speed = 45      # degrés / seconde
let bob_height = 0.6

fn on_start() {
    t = 0
    editor.log("Vitrine : animations démarrées")
}

fn on_update(dt) {
    t += dt

    # Balise flottante : sinusoïde verticale autour de sa hauteur de repos.
    let p = object.position("Balise")
    if p {
        object.set_position("Balise", p[0], 1.6 + math.sin(t * 2) * bob_height, p[2])
    }

    # Le tore tourne sur deux axes à des vitesses différentes.
    object.set_rotation("Tore", t * spin_speed * 0.5, t * spin_speed, 0)

    # L'icosaèdre pulse du bleu au cyan : une animation de MATÉRIAU.
    let pulse = (math.sin(t * 3) + 1) / 2
    object.set_color("Cristal", 40 + pulse * 60, 120 + pulse * 120, 255)

    # Les trois plots montent et descendent en décalé (onde progressive).
    for i in range(0, 3) {
        let name = "Plot " .. (i + 1)
        let base = object.position(name)
        if base {
            object.set_position(name, base[0], 0.5 + math.sin(t * 1.5 + i * 0.9) * 0.35, base[2])
        }
    }
}
)SLED";

/// Circuit : conduite, pilote automatique, chronométrage au tour.
///
/// Le pilote automatique n'est pas un gadget : c'est ce qui rend la scène
/// JOUABLE SANS CLAVIER, donc vérifiable en `--headless` et reproductible
/// d'une exécution à l'autre. Dès qu'une touche est pressée, le joueur
/// reprend la main.
inline constexpr const char *CIRCUIT_SCRIPT = R"SLED(
# ── Scène « Circuit » : voiture pilotable + tours chronométrés ──────────────
let car = "Voiture"

# État du véhicule (modèle « arcade » : le script tient le cap et la vitesse,
# la physique s'occupe de la gravité et des collisions avec les barrières).
let heading = 0          # radians, 0 = +Z
let speed = 0            # unités / seconde
let max_speed = 26
let accel = 14
let brake = 22
let drag = 3.5
let turn_rate = 2.0      # radians / seconde à pleine vitesse

# Course
let checkpoints = []
let next_checkpoint = 0
let lap = 0
let total_laps = 0
let lap_time = 0
let best_lap = 0
let elapsed = 0
let manual = false

fn on_start() {
    checkpoints = scene.find_tag("checkpoint")
    # Le premier point est SOUS la voiture au départ : viser celui d'après,
    # sinon il serait validé à la première image et le premier tour serait
    # compté pour rien.
    next_checkpoint = 0
    if len(checkpoints) > 1 { next_checkpoint = 1 }
    lap = 0
    total_laps = 0
    lap_time = 0
    best_lap = 0
    elapsed = 0
    speed = 0
    # Cap initial = tangente à la piste au point de départ, soit +X (la
    # voiture est posée avec le même lacet de 90°, cf. content.hpp).
    heading = math.pi / 2
    manual = false
    editor.log("Circuit : " .. len(checkpoints) .. " points de passage")
    editor.status("Circuit prêt — ZQSD/WASD pour conduire, sinon pilote automatique")
}

# Cap à suivre pour rejoindre un point : atan2 du vecteur voiture -> cible.
fn heading_to(target) {
    let here = object.position(car)
    let there = object.position(target)
    if not here or not there { return heading }
    return math.atan2(there[0] - here[0], there[2] - here[2])
}

# Écart angulaire ramené dans [-pi, pi] : sans ce repliement, un cap qui
# franchit ±pi ferait braquer la voiture dans le mauvais sens pendant un tour
# complet.
fn wrap_angle(a) {
    while a > math.pi { a -= 2 * math.pi }
    while a < -math.pi { a += 2 * math.pi }
    return a
}

fn autopilot_steer() {
    if len(checkpoints) == 0 { return 0 }
    let target = checkpoints[next_checkpoint]
    let wanted = heading_to(target)
    let delta = wrap_angle(wanted - heading)
    return math.clamp(delta * 1.8, -1, 1)
}

fn on_update(dt) {
    elapsed += dt
    lap_time += dt

    let throttle = input.axis("down", "up") + input.axis("s", "w")
    let steer = input.axis("left", "right") + input.axis("a", "d")
    if throttle != 0 or steer != 0 { manual = true }

    if not manual {
        steer = autopilot_steer()
        throttle = 1
    }

    # Vitesse : accélération, freinage, traînée.
    if throttle > 0 {
        speed += accel * throttle * dt
    } else if throttle < 0 {
        speed += brake * throttle * dt
    }
    speed -= drag * dt
    speed = math.clamp(speed, -6, max_speed)

    # On ne braque qu'en roulant (comme une vraie voiture à l'arrêt).
    let grip = math.clamp(math.abs(speed) / 8, 0, 1)
    heading += steer * turn_rate * grip * dt * math.sign(speed)

    let vx = math.sin(heading) * speed
    let vz = math.cos(heading) * speed
    let current = object.velocity(car)
    let vy = 0
    if current { vy = current[1] }
    object.set_velocity(car, vx, vy, vz)
    object.set_rotation(car, 0, math.deg(heading), 0)

    check_progress()
}

fn check_progress() {
    if len(checkpoints) == 0 { return nil }
    let target = checkpoints[next_checkpoint]
    if object.distance(car, target) < 6.5 {
        next_checkpoint += 1
        if next_checkpoint >= len(checkpoints) {
            next_checkpoint = 0
            lap += 1
            total_laps = lap
            if best_lap == 0 or lap_time < best_lap { best_lap = lap_time }
            editor.log(format("Tour {} bouclé en {} s (meilleur : {} s)", lap,
                              int(lap_time * 100) / 100, int(best_lap * 100) / 100))
            editor.status(format("Tour {} — meilleur {} s", lap, int(best_lap * 100) / 100))
            lap_time = 0
        }
    }
}
)SLED";

/// Laboratoire physique : fait tomber des caisses à intervalle régulier, puis
/// tire un projectile — de quoi mesurer le solveur sous charge croissante.
inline constexpr const char *PHYSICS_SCRIPT = R"SLED(
# ── Scène « Laboratoire physique » : charge croissante du solveur ───────────
let t = 0
let dropped = 0
let max_drops = 24
let next_drop = 0.35

fn on_start() {
    t = 0
    dropped = 0
    next_drop = 0.35
    editor.log("Laboratoire : largage de " .. max_drops .. " caisses")
}

fn on_update(dt) {
    t += dt
    if dropped >= max_drops { return nil }
    if t < next_drop { return nil }

    next_drop = t + 0.35
    dropped += 1

    let angle = dropped * 0.7
    scene.spawn({
        name: "Caisse " .. dropped,
        shape: "box",
        size: [0.9, 0.9, 0.9],
        pos: [math.cos(angle) * 2.5, 9 + dropped * 0.4, math.sin(angle) * 2.5],
        color: [80 + dropped * 6, 200 - dropped * 5, 120],
        material: "plastic",
        body: "dynamic",
        mass: 1.5,
        restitution: 0.25,
        friction: 0.6,
        tag: "debris"
    })

    if dropped == max_drops {
        editor.log("Largage terminé : " .. scene.count() .. " objets dans la scène")
    }
}
)SLED";

// ============================================================================
// Construction du projet de démonstration
// ============================================================================

namespace content {

constexpr float PI = 3.14159265358979323846f;


/// Script de la scène « Assemblages » : anime la hiérarchie pour rendre
/// VISIBLE ce que l'arbre apporte — faire tourner un groupe emporte tout son
/// sous-arbre, et déplacer un nœud intermédiaire n'emporte que le sien.
inline constexpr const char *ASSEMBLY_SCRIPT = R"SLED(
let t = 0

fn on_update(dt) {
    t += dt
    # Le robot pivote : tête, bras, jambes et effets suivent, sans qu'aucun
    # d'eux ne soit cité ici.
    object.set_rotation("Robot", 0, math.sin(t * 0.4) * 35, 0)
    # Un seul bras s'agite : son avant-bras et sa main suivent, le reste non.
    object.set_rotation("BrasDroit", math.sin(t * 2.2) * 40, 0, 0)
    # Les roues tournent, le véhicule avance : deux niveaux indépendants.
    object.set_rotation("Roues", 0, 0, t * 120)
    object.set_position("Véhicule", math.sin(t * 0.5) * 4, 0, 6)
}
)SLED";

[[nodiscard]] inline ObjectDesc MakeObject(const char *name, ShapeKind shape, math::FVector3 dimensions,
										   math::FVector3 position, sdl3::Color color,
										   MaterialKind material = MaterialKind::PLASTIC) {
	ObjectDesc object;
	object.name = String(name);
	object.shape = shape;
	object.dimensions = dimensions;
	object.transform.position = position;
	object.material.kind = material;
	object.material.baseColor = color;
	object.physics.halfExtents = dimensions * 0.5f;
	return object;
}

/// Scène « Vitrine » : une de chaque famille de matériau, une paire de
/// portails (M30) et de quoi animer depuis un script.
[[nodiscard]] inline SceneDesc MakeShowcaseScene() {
	SceneDesc scene;
	scene.SetName(String("Vitrine"));
	scene.description = "Matériaux, portails et animations pilotées par script";
	scene.gameplayScript = String(SHOWCASE_SCRIPT);
	scene.camera.editPosition = {0.f, 6.5f, -15.f};
	scene.camera.editPitch = -0.3f;
	scene.camera.playPosition = {0.f, 3.f, -12.f};
	scene.camera.playPitch = -0.12f;

	// Soleil franchement au-dessus et ambiante lisible : le sol est la plus
	// grande surface de la scène, c'est lui qui donne l'impression de volume.
	scene.environment.sunDirection = {0.35f, -0.82f, 0.45f};
	scene.environment.sunIntensity = 1.15f;
	scene.environment.ambientColor = sdl3::Color{62, 66, 84, 255};

	ObjectDesc ground = MakeObject("Sol", ShapeKind::BOX, {40.f, 0.5f, 40.f}, {0.f, -0.25f, 0.f},
								   sdl3::Color{96, 102, 118, 255}, MaterialKind::PBR);
	ground.material.roughness = 0.85f;
	ground.physics.body = BodyKind::STATIC;
	ground.tag = "ground";
	(void)scene.Add(std::move(ground));

	(void)scene.Add(MakeObject("Cube plastique", ShapeKind::BOX, {1.6f, 1.6f, 1.6f}, {-4.5f, 0.8f, 0.f},
									   sdl3::Color{80, 140, 220, 255}, MaterialKind::PLASTIC));

	ObjectDesc metal = MakeObject("Sphère métal", ShapeKind::SPHERE, {1.6f, 1.6f, 1.6f}, {-1.8f, 0.8f, 1.6f},
								  sdl3::Color{205, 208, 216, 255}, MaterialKind::METAL);
	metal.segments = 32;
	metal.material.roughness = 0.18f;
	metal.material.metallic = 1.f;
	(void)scene.Add(std::move(metal));

	ObjectDesc pbr = MakeObject("Tore", ShapeKind::TORUS, {2.6f, 0.8f, 2.6f}, {1.6f, 1.1f, -0.4f},
								sdl3::Color{230, 176, 62, 255}, MaterialKind::PBR);
	pbr.segments = 28;
	pbr.material.metallic = 0.85f;
	pbr.material.roughness = 0.28f;
	(void)scene.Add(std::move(pbr));

	ObjectDesc wood = MakeObject("Caisse bois", ShapeKind::BOX, {1.4f, 1.4f, 1.4f}, {4.4f, 0.7f, 1.2f},
								 sdl3::Color{146, 102, 62, 255}, MaterialKind::WOOD);
	wood.physics.body = BodyKind::DYNAMIC;
	wood.physics.mass = 3.f;
	(void)scene.Add(std::move(wood));

	ObjectDesc crystal = MakeObject("Cristal", ShapeKind::ICOSAHEDRON, {1.5f, 1.5f, 1.5f}, {0.f, 2.6f, 3.4f},
									sdl3::Color{60, 200, 255, 255}, MaterialKind::UNLIT);
	crystal.segments = 24;
	(void)scene.Add(std::move(crystal));

	ObjectDesc beacon = MakeObject("Balise", ShapeKind::SPHERE, {0.9f, 0.9f, 0.9f}, {5.f, 1.6f, 5.f},
								   sdl3::Color{70, 235, 130, 255}, MaterialKind::UNLIT);
	beacon.segments = 20;
	(void)scene.Add(std::move(beacon));

	for (int i = 0; i < 3; ++i) {
		ObjectDesc post = MakeObject("Plot", ShapeKind::CYLINDER, {0.7f, 1.f, 0.7f},
									 {-7.f + float(i) * 1.6f, 0.5f, -4.5f}, sdl3::Color{220, 96, 96, 255},
									 MaterialKind::PLASTIC);
		post.name = String::Format("Plot %d", i + 1);
		post.segments = 20;
		(void)scene.Add(std::move(post));
	}

	// Paire de portails : A regarde vers +Z, B est retourné de 180° près de la
	// balise — traverser A ressort donc dans le coin animé de la scène.
	ObjectDesc portalA = MakeObject("Portail A", ShapeKind::PORTAL_QUAD, {2.8f, 2.8f, 0.1f}, {-8.f, 1.4f, 0.f},
									sdl3::Color::WHITE(), MaterialKind::BASIC);
	portalA.tag = "portal_a";
	portalA.material.doubleSided = true;
	(void)scene.Add(std::move(portalA));

	ObjectDesc portalB = MakeObject("Portail B", ShapeKind::PORTAL_QUAD, {2.8f, 2.8f, 0.1f}, {8.f, 1.4f, 4.f},
									sdl3::Color::WHITE(), MaterialKind::BASIC);
	portalB.tag = "portal_b";
	portalB.transform.SetEulerDegrees({0.f, 180.f, 0.f});
	portalB.material.doubleSided = true;
	(void)scene.Add(std::move(portalB));

	ObjectDesc ball = MakeObject("Bille", ShapeKind::SPHERE, {0.8f, 0.8f, 0.8f}, {-8.f, 1.6f, 3.2f},
								 sdl3::Color{235, 86, 86, 255}, MaterialKind::PLASTIC);
	ball.segments = 18;
	ball.physics.body = BodyKind::DYNAMIC;
	ball.physics.collider = ColliderKind::SPHERE;
	ball.physics.halfExtents = {0.4f, 0.4f, 0.4f};
	ball.physics.mass = 1.f;
	ball.physics.restitution = 0.55f;
	(void)scene.Add(std::move(ball));

	return scene;
}

/// Scène « Circuit » : piste fermée générée procéduralement (dalles + rails
/// intérieur/extérieur), points de passage étiquetés, voiture suivie par la
/// caméra. La conduite et le chronométrage sont dans CIRCUIT_SCRIPT.
[[nodiscard]] inline SceneDesc MakeCircuitScene() {
	SceneDesc scene;
	scene.SetName(String("Circuit"));
	scene.description = "Piste fermée jouable — conduite et tours gérés par script";
	scene.gameplayScript = String(CIRCUIT_SCRIPT);
	scene.camera.editPosition = {0.f, 38.f, -6.f};
	scene.camera.editPitch = -1.15f;
	scene.camera.playPosition = {0.f, 3.f, -34.f};

	// Éclairage volontairement généreux : la caméra de poursuite regarde vers
	// l'horizon, donc la piste occupe peu de pixels et un éclairage discret
	// la rendait illisible sur les captures.
	scene.environment.backgroundColor = sdl3::Color{28, 44, 66, 255};
	scene.environment.ambientColor = sdl3::Color{86, 94, 112, 255};
	scene.environment.sunIntensity = 1.25f;
	scene.environment.sunDirection = {0.35f, -0.78f, 0.32f};

	constexpr int SEGMENTS = 40;
	constexpr float RADIUS = 30.f;
	constexpr float TRACK_WIDTH = 9.f;
	constexpr int CHECKPOINTS = 8;
	// Longueur d'une dalle : la corde de l'arc + une marge, pour que deux
	// dalles voisines se recouvrent légèrement au lieu de laisser un trou où
	// la voiture accrocherait.
	const float segmentLength = 2.f * RADIUS * sdl3::Sin(PI / float(SEGMENTS)) + 0.6f;

	ObjectDesc ground = MakeObject("Terrain", ShapeKind::BOX, {96.f, 1.f, 96.f}, {0.f, -1.1f, 0.f},
								   sdl3::Color{62, 104, 70, 255}, MaterialKind::PBR);
	ground.material.roughness = 0.95f;
	ground.physics.body = BodyKind::STATIC;
	ground.tag = "ground";
	(void)scene.Add(std::move(ground));

	for (int i = 0; i < SEGMENTS; ++i) {
		float angle = 2.f * PI * float(i) / float(SEGMENTS);
		float cx = sdl3::Sin(angle) * RADIUS;
		float cz = sdl3::Cos(angle) * RADIUS;
		// Le +90° est ESSENTIEL et vaut qu'on s'y arrête : une rotation de
		// `angle` autour de Y envoie l'axe local +Z sur (sin θ, 0, cos θ),
		// c'est-à-dire la direction RADIALE — pas la tangente. Les dalles
		// posées ainsi sont larges dans le mauvais sens, et surtout les rails
		// (longs sur leur axe local Z) dépassent vers le CENTRE de la piste :
		// la voiture démarre alors à l'intérieur d'un rail et se fait
		// catapulter à la première image. Ajouter 90° envoie +Z sur
		// (cos θ, 0, −sin θ), la vraie tangente.
		float yawDeg = angle * 180.f / PI + 90.f;

		// Dalle de piste : longueur (axe Z local) le long de la piste,
		// largeur (axe X local) en travers.
		ObjectDesc slab = MakeObject("Dalle", ShapeKind::BOX, {TRACK_WIDTH, 0.4f, segmentLength}, {cx, -0.2f, cz},
									 (i % 2 == 0) ? sdl3::Color{104, 106, 114, 255} : sdl3::Color{88, 90, 98, 255},
									 MaterialKind::PBR);
		slab.name = String::Format("Dalle %02d", i + 1);
		slab.transform.SetEulerDegrees({0.f, yawDeg, 0.f});
		slab.material.roughness = 0.9f;
		slab.physics.body = BodyKind::STATIC;
		slab.physics.friction = 0.9f;
		slab.tag = "road";
		(void)scene.Add(std::move(slab));

		// Rails : un pas sur deux suffit visuellement et divise par deux le
		// nombre de corps statiques à tester en phase large.
		if (i % 2 == 0) {
			float outer = RADIUS + TRACK_WIDTH * 0.5f + 0.6f;
			float inner = RADIUS - TRACK_WIDTH * 0.5f - 0.6f;
			for (int side = 0; side < 2; ++side) {
				float r = side == 0 ? outer : inner;
				ObjectDesc rail =
					MakeObject("Rail", ShapeKind::BOX, {0.5f, 1.f, segmentLength * 2.f},
							   {sdl3::Sin(angle) * r, 0.5f, sdl3::Cos(angle) * r},
							   side == 0 ? sdl3::Color{214, 68, 68, 255} : sdl3::Color{236, 236, 240, 255},
							   MaterialKind::PLASTIC);
				rail.name = String::Format("Rail %s %02d", side == 0 ? "ext" : "int", i / 2 + 1);
				rail.transform.SetEulerDegrees({0.f, yawDeg, 0.f});
				rail.physics.body = BodyKind::STATIC;
				rail.physics.restitution = 0.15f;
				rail.tag = "barrier";
				(void)scene.Add(std::move(rail));
			}
		}

		// Points de passage. Leur NOMBRE est ce qui rend le pilote automatique
		// viable : il vise le point suivant en ligne droite, donc l'écart
		// entre la corde et l'arc doit rester dans la demi-largeur de piste.
		// Cet écart vaut R·(1−cos(π/N)) : 8,8 unités pour 4 points (hors
		// piste, la voiture percute le rail intérieur), 2,3 pour 8 points
		// (largement dans les 4,5 de demi-largeur).
		if (i % (SEGMENTS / CHECKPOINTS) == 0) {
			ObjectDesc checkpoint =
				MakeObject("Point", ShapeKind::BOX, {TRACK_WIDTH, 0.12f, 1.2f}, {cx, 0.06f, cz},
						   i == 0 ? sdl3::Color{250, 236, 80, 255} : sdl3::Color{90, 200, 255, 255},
						   MaterialKind::UNLIT);
			checkpoint.name = String::Format("Point %d", i / (SEGMENTS / CHECKPOINTS) + 1);
			checkpoint.transform.SetEulerDegrees({0.f, yawDeg, 0.f});
			checkpoint.tag = "checkpoint";
			(void)scene.Add(std::move(checkpoint));
		}
	}

	// Voiture : sur la dalle de départ (angle 0 => x=0, z=RADIUS), orientée
	// selon la TANGENTE à cet endroit, c'est-à-dire +X — d'où le lacet de 90°
	// (le script part du même cap, cf. CIRCUIT_SCRIPT::on_start).
	ObjectDesc car = MakeObject("Voiture", ShapeKind::BOX, {1.9f, 0.9f, 3.8f}, {0.f, 0.75f, RADIUS},
								sdl3::Color{242, 92, 62, 255}, MaterialKind::PBR);
	car.transform.SetEulerDegrees({0.f, 90.f, 0.f});
	// Carrosserie peu métallique : à 0,6 la voiture ne renvoyait presque que
	// le ciel et se lisait comme une tache noire sur les captures.
	car.material.metallic = 0.25f;
	car.material.roughness = 0.35f;
	car.physics.body = BodyKind::DYNAMIC;
	car.physics.mass = 900.f;
	// Frottement volontairement FAIBLE : le script impose la vitesse de la
	// voiture image par image (modèle arcade), et le solveur applique une
	// impulsion de frottement bornée par µ·impulsion normale. Avec 900 kg sur
	// une piste à µ=0,9, ce frottement annulait intégralement la vitesse
	// imposée et clouait la voiture au sol. Un µ bas modélise ici des ROUES
	// qui roulent — ce qui est exactement la situation.
	car.physics.friction = 0.05f;
	car.physics.restitution = 0.05f;
	car.tag = "camera_target";
	(void)scene.Add(std::move(car));

	// Enfant de la voiture : sa transformation est RELATIVE au parent (c'est
	// le graphe de scène qui compose), pas absolue.
	ObjectDesc cabin = MakeObject("Habitacle", ShapeKind::BOX, {1.5f, 0.6f, 1.6f}, {0.f, 0.75f, -0.2f},
								  sdl3::Color{70, 76, 98, 255}, MaterialKind::PBR);
	cabin.parent = "Voiture";
	cabin.material.roughness = 0.35f;
	(void)scene.Add(std::move(cabin));

	ObjectDesc arch = MakeObject("Arche de départ", ShapeKind::TORUS, {14.f, 1.2f, 14.f}, {0.f, 0.f, RADIUS},
								 sdl3::Color{250, 236, 80, 255}, MaterialKind::UNLIT);
	arch.segments = 24;
	arch.transform.SetEulerDegrees({90.f, 0.f, 0.f});
	arch.transform.scale = {0.55f, 0.55f, 0.55f};
	(void)scene.Add(std::move(arch));

	return scene;
}

/// Scène « Laboratoire physique » : un empilement stable au départ, que le
/// script bombarde ensuite — sert de banc de mesure au solveur.
[[nodiscard]] inline SceneDesc MakePhysicsScene() {
	SceneDesc scene;
	scene.SetName(String("Laboratoire physique"));
	scene.description = "Banc d'essai du solveur : empilement + largage scripté";
	scene.gameplayScript = String(PHYSICS_SCRIPT);
	scene.camera.editPosition = {0.f, 7.f, -16.f};
	scene.camera.editPitch = -0.22f;
	scene.camera.playPosition = {0.f, 6.f, -14.f};
	scene.camera.playPitch = -0.2f;
	scene.environment.backgroundColor = sdl3::Color{22, 22, 30, 255};

	ObjectDesc ground = MakeObject("Sol", ShapeKind::BOX, {30.f, 1.f, 30.f}, {0.f, -0.5f, 0.f},
								   sdl3::Color{48, 50, 58, 255}, MaterialKind::PBR);
	ground.material.roughness = 0.9f;
	ground.physics.body = BodyKind::STATIC;
	ground.physics.friction = 0.9f;
	ground.tag = "ground";
	(void)scene.Add(std::move(ground));

	// Pyramide 5-4-3-2-1 : stable au repos, donc toute agitation visible
	// vient du largage — utile pour juger le solveur à l'œil.
	constexpr int LEVELS = 5;
	constexpr float BRICK = 1.f;
	for (int level = 0; level < LEVELS; ++level) {
		int count = LEVELS - level;
		for (int i = 0; i < count; ++i) {
			float x = (float(i) - float(count - 1) * 0.5f) * (BRICK + 0.05f);
			ObjectDesc brick =
				MakeObject("Brique", ShapeKind::BOX, {BRICK, BRICK, BRICK},
						   {x, BRICK * 0.5f + float(level) * BRICK, 0.f},
						   sdl3::Color{uint8_t(90 + level * 28), uint8_t(150 - level * 14), 210, 255},
						   MaterialKind::PLASTIC);
			brick.name = String::Format("Brique %d-%d", level + 1, i + 1);
			brick.physics.body = BodyKind::DYNAMIC;
			brick.physics.mass = 2.f;
			brick.physics.friction = 0.7f;
			brick.physics.restitution = 0.05f;
			brick.tag = "stack";
			(void)scene.Add(std::move(brick));
		}
	}

	ObjectDesc wall = MakeObject("Mur", ShapeKind::BOX, {14.f, 3.f, 0.6f}, {0.f, 1.5f, 6.f},
								 sdl3::Color{70, 74, 92, 255}, MaterialKind::PBR);
	wall.physics.body = BodyKind::STATIC;
	(void)scene.Add(std::move(wall));

	return scene;
}

// ============================================================================
// Scène « Assemblages » — la démonstration de la hiérarchie
// ============================================================================
//
// Quatre objets composés, délibérément de natures DIFFÉRENTES : un robot
// articulé (4 niveaux de profondeur), un véhicule (un groupe de roues), un
// personnage (visuel / collision / caméra séparés) et un bâtiment (structure
// + mobilier). L'intérêt n'est pas le rendu — ce sont des boîtes — mais de
// montrer qu'un SEUL mécanisme de composition sert ces quatre cas, et que
// déplacer le nœud racine emporte tout le reste.
//
// Construite directement sur `scene::NodeTree` plutôt que via
// `SceneDesc::Add` : les noms y sont alors naturels et uniques ENTRE FRÈRES
// (deux « Tête », une par personnage), ce qui est exactement la règle d'un
// arbre — et ce qui oblige à désigner un nœud par son CHEMIN quand le nom
// seul devient ambigu.

/// Petit assembleur local : crée un nœud enfant et rend son identifiant.
[[nodiscard]] inline scene::NodeId AddPart(SceneDesc &scene, scene::NodeId parent, const char *name,
                                           math::FVector3 size, math::FVector3 position, sdl3::Color color,
                                           bool visual = true) {
	ObjectDesc desc;
	desc.name = String(name);
	desc.hasVisual = visual;
	desc.shape = ShapeKind::BOX;
	desc.dimensions = size;
	desc.transform.position = position;
	desc.material.kind = MaterialKind::PLASTIC;
	desc.material.baseColor = color;
	desc.physics.halfExtents = size * 0.5f;
	if (!visual)
		desc.type = String(node_kind::GROUP);
	return scene.tree.Add(parent, desc.ToNode());
}

/// Nœud purement structurel (pas de géométrie) — un pivot, un repère.
[[nodiscard]] inline scene::NodeId AddGroup(SceneDesc &scene, scene::NodeId parent, const char *name,
                                            math::FVector3 position = {}) {
	return AddPart(scene, parent, name, {1.f, 1.f, 1.f}, position, sdl3::Color{255, 255, 255, 255}, false);
}

[[nodiscard]] inline SceneDesc MakeAssemblyScene() {
	SceneDesc scene;
	scene.SetName(String("Assemblages"));
	scene.description = "Hiérarchies profondes : robot articulé, véhicule, personnage, bâtiment";
	scene.gameplayScript = String(ASSEMBLY_SCRIPT);
	scene.camera.editPosition = {0.f, 7.f, -18.f};
	scene.camera.editPitch = -0.22f;
	scene.camera.playPosition = {0.f, 5.f, -14.f};
	scene.environment.sunIntensity = 1.05f;

	const scene::NodeId root = scene.tree.Root();
	ObjectDesc ground;
	ground.name = "Sol";
	ground.shape = ShapeKind::BOX;
	ground.dimensions = {60.f, 0.5f, 60.f};
	ground.transform.position = {0.f, -0.25f, 0.f};
	ground.material.kind = MaterialKind::PBR;
	ground.material.baseColor = sdl3::Color{92, 98, 112, 255};
	ground.physics.body = BodyKind::STATIC;
	ground.physics.halfExtents = {30.f, 0.25f, 30.f};
	ground.tag = "ground";
	(void)scene.tree.Add(root, ground.ToNode());

	// ── Robot : Robot > Corps > Tête > Œil, soit 4 niveaux ──────────────────
	const sdl3::Color metal{176, 182, 198, 255};
	const sdl3::Color accent{242, 138, 74, 255};
	scene::NodeId robot = AddGroup(scene, root, "Robot", {-7.f, 0.f, 0.f});
	scene::NodeId torso = AddPart(scene, robot, "Corps", {1.6f, 2.2f, 1.f}, {0.f, 2.6f, 0.f}, metal);
	scene::NodeId head = AddPart(scene, torso, "Tête", {1.f, 0.9f, 1.f}, {0.f, 1.6f, 0.f}, metal);
	(void)AddPart(scene, head, "Œil", {0.5f, 0.2f, 0.1f}, {0.f, 0.1f, -0.55f}, accent);
	// La « caméra » du robot est un repère SANS géométrie : un nœud n'a pas
	// besoin d'être visible pour avoir une place dans le monde.
	(void)AddGroup(scene, head, "Caméra", {0.f, 0.f, -0.8f});

	struct Limb {
		const char *name;
		float x;
		float y;
		bool arm;
	};
	for (const Limb &limb : {Limb{"BrasGauche", -1.2f, 0.7f, true}, Limb{"BrasDroit", 1.2f, 0.7f, true},
	                         Limb{"JambeGauche", -0.5f, -1.4f, false}, Limb{"JambeDroite", 0.5f, -1.4f, false}}) {
		scene::NodeId shoulder = AddGroup(scene, torso, limb.name, {limb.x, limb.y, 0.f});
		scene::NodeId upper =
			AddPart(scene, shoulder, limb.arm ? "Bras" : "Cuisse", {0.35f, 1.f, 0.35f}, {0.f, -0.5f, 0.f}, metal);
		scene::NodeId lower =
			AddPart(scene, upper, limb.arm ? "Avant-bras" : "Tibia", {0.3f, 1.f, 0.3f}, {0.f, -1.f, 0.f}, metal);
		(void)AddPart(scene, lower, limb.arm ? "Main" : "Pied", {0.34f, 0.3f, 0.5f}, {0.f, -0.65f, 0.f}, accent);
	}
	scene::NodeId effects = AddGroup(scene, robot, "Effets", {0.f, 3.5f, 0.f});
	(void)AddPart(scene, effects, "Lumière", {0.3f, 0.3f, 0.3f}, {0.f, 1.2f, 0.f}, sdl3::Color{255, 226, 140, 255});
	(void)AddGroup(scene, effects, "Audio", {0.f, 0.6f, 0.f});

	// ── Véhicule ────────────────────────────────────────────────────────────
	scene::NodeId vehicle = AddGroup(scene, root, "Véhicule", {0.f, 0.f, 6.f});
	(void)AddPart(scene, vehicle, "Châssis", {3.4f, 0.8f, 1.8f}, {0.f, 0.8f, 0.f}, sdl3::Color{206, 78, 74, 255});
	scene::NodeId wheels = AddGroup(scene, vehicle, "Roues");
	const float wheelX[4] = {-1.2f, 1.2f, -1.2f, 1.2f};
	const float wheelZ[4] = {-0.95f, -0.95f, 0.95f, 0.95f};
	const char *wheelNames[4] = {"AvantGauche", "AvantDroit", "ArrièreGauche", "ArrièreDroit"};
	for (int i = 0; i < 4; ++i) {
		scene::NodeId wheel = AddGroup(scene, wheels, wheelNames[i], {wheelX[i], 0.45f, wheelZ[i]});
		ObjectDesc rim;
		rim.name = "Jante";
		rim.shape = ShapeKind::CYLINDER;
		rim.dimensions = {0.9f, 0.4f, 0.9f};
		rim.transform.SetEulerDegrees({0.f, 0.f, 90.f});
		rim.material.baseColor = sdl3::Color{42, 44, 52, 255};
		(void)scene.tree.Add(wheel, rim.ToNode());
	}
	scene::NodeId engine = AddGroup(scene, vehicle, "Moteur", {1.f, 1.f, 0.f});
	(void)AddGroup(scene, engine, "Audio");

	// ── Personnage ──────────────────────────────────────────────────────────
	scene::NodeId character = AddGroup(scene, root, "Personnage", {6.f, 0.f, 0.f});
	scene::NodeId visual = AddGroup(scene, character, "Visuel");
	scene::NodeId charBody =
		AddPart(scene, visual, "Corps", {0.9f, 1.6f, 0.6f}, {0.f, 1.6f, 0.f}, sdl3::Color{86, 132, 196, 255});
	(void)AddPart(scene, charBody, "Tête", {0.6f, 0.6f, 0.6f}, {0.f, 1.1f, 0.f}, sdl3::Color{226, 190, 156, 255});
	(void)AddPart(scene, charBody, "Arme", {0.2f, 0.2f, 1.4f}, {0.55f, 0.1f, 0.4f}, sdl3::Color{60, 62, 70, 255});
	(void)AddGroup(scene, character, "Collision", {0.f, 1.f, 0.f});
	(void)AddGroup(scene, character, "Caméra", {0.f, 2.4f, -3.f});
	(void)AddGroup(scene, character, "Scripts");

	// ── Bâtiment ────────────────────────────────────────────────────────────
	scene::NodeId house = AddGroup(scene, root, "Bâtiment", {14.f, 0.f, 4.f});
	scene::NodeId structure = AddGroup(scene, house, "Structure");
	(void)AddPart(scene, structure, "Sol", {6.f, 0.3f, 6.f}, {0.f, 0.15f, 0.f}, sdl3::Color{150, 132, 110, 255});
	(void)AddPart(scene, structure, "Murs", {6.f, 3.f, 6.f}, {0.f, 1.8f, 0.f}, sdl3::Color{198, 186, 166, 255});
	ObjectDesc roof;
	roof.name = "Toit";
	roof.shape = ShapeKind::CONE;
	roof.dimensions = {8.f, 2.2f, 8.f};
	roof.segments = 4;
	roof.transform.position = {0.f, 4.4f, 0.f};
	roof.material.baseColor = sdl3::Color{132, 72, 60, 255};
	(void)scene.tree.Add(structure, roof.ToNode());
	scene::NodeId furniture = AddGroup(scene, house, "Mobilier", {0.f, 0.3f, 0.f});
	(void)AddPart(scene, furniture, "Table", {1.6f, 0.8f, 1.f}, {-1.f, 0.4f, 0.f}, sdl3::Color{140, 96, 62, 255});
	(void)AddPart(scene, furniture, "Chaise", {0.6f, 1.f, 0.6f}, {0.4f, 0.5f, 0.f}, sdl3::Color{120, 84, 56, 255});
	(void)AddPart(scene, furniture, "Lampe", {0.3f, 1.2f, 0.3f}, {1.6f, 0.6f, 1.6f}, sdl3::Color{240, 226, 170, 255});

	return scene;
}

// ============================================================================
// Donjon : éclairage dynamique, scripts de nœud, déclencheurs, vue subjective
// ============================================================================
//
// La scène montre ce que la bibliothèque de scripts apporte par rapport au
// seul script de scène : CHAQUE torche porte le même script `torchlight`
// (paramétré par ses propriétés), le coffre et la porte ont le leur, le
// joueur est un corps physique piloté par `player`, et le script de SCÈNE ne
// fait qu'orchestrer — il réagit aux zones de déclenchement et pose des
// propriétés que les scripts de nœud lisent.

/// Script de scène : l'histoire (coffre, porte, sortie).
inline constexpr const char *DUNGEON_SCRIPT = R"SLED(
# ── Donjon : orchestration de la partie ─────────────────────────────────────
# Les scripts de NŒUD (torches, joueur, coffre, porte, squelette) font vivre
# la scène ; celui-ci réagit aux zones de déclenchement et pose des
# propriétés (node.prop) que ces scripts lisent.
let chest_open = false
let door_open = false
let escaped = false
let escape_time = 0

fn on_start() {
    chest_open = false
    door_open = false
    escaped = false
    editor.log("Donjon : " .. len(light.list()) .. " torches allumées")
    editor.status("Donjon — ZQSD/WASD + souris pour explorer, sinon visite guidée")
}

fn on_trigger(zone, other, event) {
    if event == "open_chest" and not chest_open {
        chest_open = true
        node.prop("Coffre", "open", true)
        editor.log("Le coffre s'ouvre : un trésor scintille")
    } else if event == "open_door" and not door_open {
        door_open = true
        node.prop("Porte", "open", true)
        editor.log("La herse se lève")
    } else if event == "exit" and not escaped {
        escaped = true
        escape_time = editor.time()
        editor.status("Sortie atteinte en " .. int(escape_time) .. " s")
        editor.log("Sortie du donjon atteinte")
    }
}
)SLED";

/// Flamme vacillante : l'intensité suit un bruit lisse (sinus déphasés),
/// réglé par les propriétés du nœud — un seul script pour toutes les
/// torches, chacune avec son caractère.
inline constexpr const char *TORCHLIGHT_SCRIPT = R"SLED(
# torchlight — flamme vacillante (attaché à chaque torche)
# Propriétés lues sur le nœud : flicker_speed, flicker_min, flicker_max,
# seed, flame (nom de la flamme à animer).
let base = map()

fn prop_or(self, key, fallback) {
    let value = node.prop(self, key)
    if value == nil { return fallback }
    return value
}

fn on_start(self) {
    base[self] = light.intensity(self)
}

fn on_update(self, dt) {
    let t = editor.time() * prop_or(self, "flicker_speed", 1.5) + prop_or(self, "seed", 0)
    # Somme de trois sinus incommensurables : un bruit sans motif visible.
    let n = (math.sin(t * 7.3) + math.sin(t * 13.1 + 1.7) * 0.5 + math.sin(t * 23.7 + 0.4) * 0.25) / 1.75
    let k = math.lerp(prop_or(self, "flicker_min", 0.75), prop_or(self, "flicker_max", 1.2), n * 0.5 + 0.5)
    light.set_intensity(self, base[self] * k)
    let flame = node.prop(self, "flame")
    if flame != nil {
        object.set_scale(flame, 1, 0.85 + k * 0.2, 1)
    }
}
)SLED";

/// Joueur à la première personne : corps physique (les murs arrêtent), vue
/// portée par une caméra enfant. Sans clavier ni souris, il suit les points
/// étiquetés `waypoint` — la visite guidée qui rend la scène vérifiable sans
/// écran.
inline constexpr const char *PLAYER_SCRIPT = R"SLED(
# player — contrôleur à la première personne
let yaw = 0
let pitch = 0
let manual = false
let waypoints = []
let next_point = 0
let walk_speed = 4

fn wrap_angle(a) {
    while a > math.pi { a -= 2 * math.pi }
    while a < -math.pi { a += 2 * math.pi }
    return a
}

fn on_start(self) {
    waypoints = scene.find_tag("waypoint")
    next_point = 0
    yaw = 0
    pitch = 0
    manual = false
}

fn on_update(self, dt) {
    let mouse = input.mouse_delta()
    if mouse[0] != 0 or mouse[1] != 0 { manual = true }
    yaw += mouse[0] * 0.0025
    pitch = math.clamp(pitch - mouse[1] * 0.0025, -1.2, 1.2)

    let forward = input.axis("s", "w") + input.axis("down", "up")
    let strafe = input.axis("a", "d") + input.axis("left", "right")
    if forward != 0 or strafe != 0 { manual = true }

    # Visite guidée : cap vers le prochain point, en avant toute.
    if not manual and next_point < len(waypoints) {
        let here = object.position(self)
        let there = object.position(waypoints[next_point])
        let dx = there[0] - here[0]
        let dz = there[2] - here[2]
        if dx * dx + dz * dz < 0.5 {
            next_point += 1
        } else {
            yaw += math.clamp(wrap_angle(math.atan2(dx, dz) - yaw), -2.5 * dt, 2.5 * dt)
            forward = 1
        }
        pitch = math.lerp(pitch, 0, math.min(1, dt * 2))
    }

    let speed = walk_speed
    if input.key("shift") { speed = walk_speed * 1.8 }
    let v = object.velocity(self)
    let vy = 0
    if v { vy = v[1] }
    # Droite = (-cos, 0, sin) quand l'avant est (sin, 0, cos).
    let vx = (math.sin(yaw) * forward - math.cos(yaw) * strafe) * speed
    let vz = (math.cos(yaw) * forward + math.sin(yaw) * strafe) * speed
    object.set_velocity(self, vx, vy, vz)
    object.set_rotation(self, 0, yaw * 180 / math.pi, 0)
    let eye = node.prop(self, "eye")
    if eye != nil { object.set_rotation(eye, -pitch * 180 / math.pi, 0, 0) }
}
)SLED";

/// Coffre : le couvercle pivote quand la propriété `open` passe à vrai.
inline constexpr const char *CHEST_SCRIPT = R"SLED(
# chest — ouvre le couvercle (propriété `lid`) quand `open` est vrai
let angle = map()

fn on_start(self) { angle[self] = 0 }

fn on_update(self, dt) {
    if node.prop(self, "open") == true and angle[self] < 105 {
        angle[self] = math.min(105, angle[self] + dt * 80)
        object.set_rotation(node.prop(self, "lid"), -angle[self], 0, 0)
    }
}
)SLED";

/// Herse : monte quand `open` est vrai.
inline constexpr const char *DOOR_SCRIPT = R"SLED(
# door — la herse se lève quand la propriété `open` est vraie
let base = map()
let lift = map()

fn on_start(self) {
    base[self] = object.position(self)
    lift[self] = 0
}

fn on_update(self, dt) {
    if node.prop(self, "open") == true and lift[self] < 1 {
        lift[self] = math.min(1, lift[self] + dt * 0.7)
        let p = base[self]
        object.set_position(self, p[0], p[1] + lift[self] * 3.8, p[2])
    }
}
)SLED";

/// Squelette : patrouille le long du couloir en se balançant.
inline constexpr const char *SKELETON_SCRIPT = R"SLED(
# skeleton — patrouille entre patrol_min et patrol_max (axe Z)
let heading = map()

fn on_start(self) { heading[self] = 1 }

fn on_update(self, dt) {
    let p = object.position(self)
    let lo = node.prop(self, "patrol_min")
    let hi = node.prop(self, "patrol_max")
    let z = p[2] + heading[self] * 1.2 * dt
    if z > hi {
        z = hi
        heading[self] = -1
    }
    if z < lo {
        z = lo
        heading[self] = 1
    }
    object.set_position(self, p[0], p[1], z)
    let facing = 0
    if heading[self] < 0 { facing = 180 }
    object.set_rotation(self, 0, facing, math.sin(editor.time() * 6) * 4)
}
)SLED";

namespace detail {

/// Pseudo-aléa déterministe (LCG) : le dallage et l'appareillage des murs
/// varient d'une pierre à l'autre, mais identiquement à chaque chargement.
struct StoneRng {
	uint32_t state = 0x9E3779B9u;
	float Next() noexcept {
		state = state * 1664525u + 1013904223u;
		return float(state >> 8) / float(1u << 24);
	}
	float Range(float lo, float hi) noexcept { return lo + (hi - lo) * Next(); }
};

[[nodiscard]] inline sdl3::Color Tint(sdl3::Color base, float factor) noexcept {
	auto channel = [factor](uint8_t v) { return uint8_t(sdl3::Clamp(float(v) * factor, 0.f, 255.f)); };
	return sdl3::Color{channel(base.r), channel(base.g), channel(base.b), 255};
}

[[nodiscard]] inline ObjectDesc Part(const char *name, ShapeKind shape, math::FVector3 size, math::FVector3 position,
									 sdl3::Color color, MaterialKind material = MaterialKind::PLASTIC) {
	ObjectDesc desc;
	desc.name = String(name);
	desc.shape = shape;
	desc.dimensions = size;
	desc.transform.position = position;
	desc.material.kind = material;
	desc.material.baseColor = color;
	desc.material.roughness = 0.85f;
	desc.physics.halfExtents = size * 0.5f;
	return desc;
}

/// Noms uniques dans la scène, SANS recherche : « Pavé », « Pavé 2 »… par
/// compteur. `SceneDesc::UniqueName` parcourt tout l'arbre à chaque appel —
/// quadratique sur les ~450 pièces du donjon, soit plusieurs secondes à
/// chaque construction du projet de démo en build instrumentée.
struct Namer {
	std::unordered_map<String, int> used;
	[[nodiscard]] String Next(const String &base) {
		const int count = ++used[base];
		return count == 1 ? base : String::Format("%s %d", base.CStr(), count);
	}
};

[[nodiscard]] inline scene::NodeId Add(SceneDesc &scene, Namer &names, scene::NodeId parent, ObjectDesc desc) {
	desc.name = names.Next(desc.name);
	return scene.tree.Add(parent, desc.ToNode());
}

} // namespace detail

[[nodiscard]] inline SceneDesc MakeDungeonScene() {
	using detail::Add;
	using detail::Part;
	SceneDesc scene;
	scene.SetName(String("Donjon"));
	scene.description = "Couloir aux torches : lumières dynamiques, scripts de nœud, déclencheurs, vue subjective";
	scene.gameplayScript = String(DUNGEON_SCRIPT);
	scene.camera.editPosition = {0.5f, 3.1f, -1.2f};
	scene.camera.editYaw = 0.f;
	scene.camera.editPitch = -0.16f;
	scene.camera.playPosition = {0.f, 1.7f, 1.2f};
	scene.environment.sunIntensity = 0.08f;
	scene.environment.sunColor = sdl3::Color{120, 130, 170, 255};
	scene.environment.ambientColor = sdl3::Color{52, 40, 32, 255};
	scene.environment.backgroundColor = sdl3::Color{8, 6, 6, 255};

	constexpr float LENGTH = 50.f;   // couloir de z = -1 à z = 49
	constexpr float CENTER_Z = 24.f;
	constexpr float HALF_WIDTH = 2.6f;
	constexpr float HEIGHT = 4.2f;
	const sdl3::Color stone{118, 104, 92, 255};
	const sdl3::Color mortar{58, 50, 44, 255};
	const sdl3::Color wood{120, 76, 44, 255};
	detail::StoneRng rng;
	detail::Namer names;

	const scene::NodeId root = scene.tree.Root();

	// ── Environnement ───────────────────────────────────────────────────────
	const scene::NodeId environment = Add(scene, names, root, ObjectDesc::Folder(String("Environnement")));

	// Sol : une dalle porteuse (le corps physique) + un dallage décoratif.
	ObjectDesc slab = Part("Sol pavé", ShapeKind::BOX, {HALF_WIDTH * 2.f + 0.6f, 0.4f, LENGTH},
						   {0.f, -0.2f, CENTER_Z}, mortar);
	slab.physics.body = BodyKind::STATIC;
	slab.physics.friction = 0.9f;
	const scene::NodeId floor = Add(scene, names, environment, slab);
	for (int row = 0; row < 36; ++row) {
		for (int column = 0; column < 4; ++column) {
			const float z = -0.6f + float(row) * 1.37f + (column % 2 == 0 ? 0.f : 0.6f);
			if (z > 48.4f)
				continue;
			ObjectDesc cobble = Part("Pavé", ShapeKind::BOX,
									 {rng.Range(1.05f, 1.22f), rng.Range(0.10f, 0.16f), rng.Range(1.1f, 1.28f)},
									 {-1.95f + float(column) * 1.3f + rng.Range(-0.05f, 0.05f), 0.2f + rng.Range(-0.02f, 0.02f),
									  z - CENTER_Z},
									 detail::Tint(stone, rng.Range(0.78f, 1.08f)));
			cobble.transform.SetEulerDegrees({0.f, rng.Range(-4.f, 4.f), 0.f});
			(void)Add(scene, names, floor, cobble);
		}
	}

	// Murs : le corps physique est le mur lui-même (sombre, comme un joint),
	// les pierres en saillie sont ses enfants.
	for (int side = 0; side < 2; ++side) {
		const float sign = side == 0 ? -1.f : 1.f;
		ObjectDesc wallDesc = Part(side == 0 ? "Mur gauche" : "Mur droit", ShapeKind::BOX, {0.6f, HEIGHT + 0.4f, LENGTH},
								   {sign * (HALF_WIDTH + 0.3f), HEIGHT * 0.5f, CENTER_Z}, mortar);
		wallDesc.physics.body = BodyKind::STATIC;
		const scene::NodeId wall = Add(scene, names, environment, wallDesc);
		for (int course = 0; course < 5; ++course) {
			const float offset = course % 2 == 0 ? 0.f : 1.f;
			for (int block = 0; block < 25; ++block) {
				const float z = -0.4f + float(block) * 2.f + offset;
				if (z > 48.6f)
					continue;
				ObjectDesc brick = Part("Pierre", ShapeKind::BOX,
										{0.14f, rng.Range(0.74f, 0.82f), rng.Range(1.8f, 1.92f)},
										{-sign * 0.31f, -HEIGHT * 0.5f + 0.45f + float(course) * 0.86f, z - CENTER_Z},
										detail::Tint(stone, rng.Range(0.72f, 1.05f)));
				(void)Add(scene, names, wall, brick);
			}
		}
	}

	ObjectDesc ceilingDesc = Part("Plafond", ShapeKind::BOX, {HALF_WIDTH * 2.f + 1.2f, 0.5f, LENGTH},
								  {0.f, HEIGHT + 0.25f, CENTER_Z}, mortar);
	const scene::NodeId ceiling = Add(scene, names, environment, ceilingDesc);
	for (int beam = 0; beam < 13; ++beam)
		(void)Add(scene, names, ceiling,
				  Part("Poutre", ShapeKind::BOX, {HALF_WIDTH * 2.f, 0.35f, 0.4f},
					   {0.f, -0.42f, float(beam) * 4.f - CENTER_Z}, detail::Tint(wood, 0.7f), MaterialKind::WOOD));

	for (float z : {-1.2f, 49.2f}) {
		ObjectDesc end = Part(z < 0.f ? "Mur d'entrée" : "Mur du fond", ShapeKind::BOX,
							  {HALF_WIDTH * 2.f + 1.2f, HEIGHT + 0.4f, 0.6f}, {0.f, HEIGHT * 0.5f, z}, detail::Tint(stone, 0.8f));
		end.physics.body = BodyKind::STATIC;
		(void)Add(scene, names, environment, end);
	}

	// Piliers : le fût porte la base et le chapiteau (un maillage avec
	// enfants, comme dans la maquette).
	const scene::NodeId pillars = Add(scene, names, environment, ObjectDesc::Group(String("Piliers")));
	int pillarIndex = 0;
	for (float z : {4.f, 12.f, 20.f, 28.f, 36.f, 44.f}) {
		for (int side = 0; side < 2; ++side) {
			const float x = side == 0 ? -(HALF_WIDTH - 0.3f) : (HALF_WIDTH - 0.3f);
			ObjectDesc shaft = Part("Pilier", ShapeKind::BOX, {0.55f, HEIGHT - 0.8f, 0.55f}, {x, HEIGHT * 0.5f, z},
									detail::Tint(stone, 0.95f));
			shaft.name = String::Format("Pilier %d", ++pillarIndex);
			shaft.physics.body = BodyKind::STATIC;
			const scene::NodeId pillar = Add(scene, names, pillars, shaft);
			(void)Add(scene, names, pillar, Part("Base", ShapeKind::BOX, {0.8f, 0.4f, 0.8f}, {0.f, -HEIGHT * 0.5f + 0.6f, 0.f},
										  detail::Tint(stone, 0.85f)));
			(void)Add(scene, names, pillar, Part("Chapiteau", ShapeKind::BOX, {0.8f, 0.4f, 0.8f}, {0.f, HEIGHT * 0.5f - 0.6f, 0.f},
										  detail::Tint(stone, 0.85f)));
		}
	}

	// Torches : un nœud LUMIÈRE, avec le manche et la flamme en enfants.
	const scene::NodeId torches = Add(scene, names, environment, ObjectDesc::Group(String("Torches")));
	int torchIndex = 0;
	for (float z : {8.f, 16.f, 24.f, 32.f, 40.f}) {
		const float sign = torchIndex % 2 == 0 ? -1.f : 1.f;
		++torchIndex;
		LightDesc flame;
		flame.color = sdl3::Color{255, 146, 58, 255};
		flame.intensity = 3.2f;
		flame.range = 11.f;
		ObjectDesc torchDesc = ObjectDesc::Light(String::Format("Torche %d", torchIndex), flame);
		torchDesc.transform.position = {sign * (HALF_WIDTH - 0.25f), 2.3f, z};
		torchDesc.script = String("torchlight");
		scene::Node torchNode = torchDesc.ToNode();
		torchNode.name = names.Next(torchNode.name);
		// Valeurs décimales EXACTES (arrondies au centième) : ce sont elles que
		// l'utilisateur relit dans l'inspecteur et le JSON.
		auto decimal = [](double v) { return double(std::lround(v * 100.0)) / 100.0; };
		torchNode.Set(String("flicker_speed"), scene::PropertyValue::Float(decimal(1.2 + 0.15 * torchIndex)));
		torchNode.Set(String("flicker_min"), scene::PropertyValue::Float(0.72));
		torchNode.Set(String("flicker_max"), scene::PropertyValue::Float(1.18));
		torchNode.Set(String("seed"), scene::PropertyValue::Float(decimal(3.7 * torchIndex)));
		torchNode.Set(String("flame"), scene::PropertyValue::Str(String::Format("Flamme %d", torchIndex)));
		const scene::NodeId torch = scene.tree.Add(torches, std::move(torchNode));

		ObjectDesc handle = Part("Manche", ShapeKind::CYLINDER, {0.1f, 0.7f, 0.08f}, {sign * 0.08f, -0.3f, 0.f},
								 detail::Tint(wood, 0.8f), MaterialKind::WOOD);
		handle.name = String::Format("Manche %d", torchIndex);
		handle.transform.SetEulerDegrees({0.f, 0.f, -sign * 18.f});
		(void)Add(scene, names, torch, handle);
		ObjectDesc fire = Part("Flamme", ShapeKind::CONE, {0.24f, 0.42f, 0.24f}, {0.f, 0.18f, 0.f},
							   sdl3::Color{255, 176, 70, 255}, MaterialKind::UNLIT);
		fire.name = String::Format("Flamme %d", torchIndex);
		(void)Add(scene, names, torch, fire);
	}

	// ── Objets dynamiques ──────────────────────────────────────────────────
	const scene::NodeId dynamic = Add(scene, names, root, ObjectDesc::Folder(String("Objets dynamiques")));

	ObjectDesc chestDesc = ObjectDesc::Group(String("Coffre"));
	chestDesc.transform.position = {1.55f, 0.26f, 26.f};
	chestDesc.transform.SetEulerDegrees({0.f, -90.f, 0.f});
	chestDesc.script = String("chest");
	chestDesc.name = names.Next(chestDesc.name);
	scene::Node chestNode = chestDesc.ToNode();
	chestNode.Set(String("lid"), scene::PropertyValue::Str(String("Couvercle")));
	chestNode.Set(String("open"), scene::PropertyValue::Bool(false));
	const scene::NodeId chest = scene.tree.Add(dynamic, std::move(chestNode));
	(void)Add(scene, names, chest, Part("Caisse", ShapeKind::BOX, {1.3f, 0.7f, 0.85f}, {0.f, 0.35f, 0.f}, wood, MaterialKind::WOOD));
	(void)Add(scene, names, chest, Part("Trésor", ShapeKind::ICOSAHEDRON, {0.5f, 0.5f, 0.5f}, {0.f, 0.72f, 0.f},
								 sdl3::Color{255, 206, 70, 255}, MaterialKind::METAL));
	ObjectDesc lidPivot = ObjectDesc::Group(String("Couvercle"));
	lidPivot.transform.position = {0.f, 0.7f, -0.42f};
	const scene::NodeId lid = Add(scene, names, chest, lidPivot);
	(void)Add(scene, names, lid, Part("Planche", ShapeKind::BOX, {1.34f, 0.26f, 0.88f}, {0.f, 0.13f, 0.44f},
							   detail::Tint(wood, 1.1f), MaterialKind::WOOD));
	for (float x : {-0.45f, 0.45f})
		(void)Add(scene, names, lid, Part("Ferrure", ShapeKind::BOX, {0.1f, 0.3f, 0.92f}, {x, 0.14f, 0.44f},
								   sdl3::Color{196, 160, 70, 255}, MaterialKind::METAL));

	ObjectDesc skeletonDesc = ObjectDesc::Group(String("Squelette"));
	skeletonDesc.transform.position = {-1.3f, 0.f, 17.f};
	skeletonDesc.script = String("skeleton");
	skeletonDesc.name = names.Next(skeletonDesc.name);
	scene::Node skeletonNode = skeletonDesc.ToNode();
	skeletonNode.Set(String("patrol_min"), scene::PropertyValue::Float(14.5));
	skeletonNode.Set(String("patrol_max"), scene::PropertyValue::Float(21.5));
	const scene::NodeId skeleton = scene.tree.Add(dynamic, std::move(skeletonNode));
	const sdl3::Color bone{226, 218, 196, 255};
	(void)Add(scene, names, skeleton, Part("Crâne", ShapeKind::SPHERE, {0.36f, 0.36f, 0.36f}, {0.f, 1.72f, 0.f}, bone));
	(void)Add(scene, names, skeleton, Part("Cage thoracique", ShapeKind::CYLINDER, {0.46f, 0.6f, 0.36f}, {0.f, 1.25f, 0.f}, bone));
	(void)Add(scene, names, skeleton, Part("Bassin", ShapeKind::BOX, {0.4f, 0.16f, 0.2f}, {0.f, 0.9f, 0.f}, bone));
	for (int side = 0; side < 2; ++side) {
		const float sign = side == 0 ? -1.f : 1.f;
		ObjectDesc arm = Part(side == 0 ? "Bras gauche" : "Bras droit", ShapeKind::CYLINDER, {0.08f, 0.7f, 0.08f},
							  {sign * 0.32f, 1.2f, 0.f}, bone);
		arm.transform.SetEulerDegrees({0.f, 0.f, sign * 10.f});
		(void)Add(scene, names, skeleton, arm);
		(void)Add(scene, names, skeleton,
				  Part(side == 0 ? "Jambe gauche" : "Jambe droite", ShapeKind::CYLINDER, {0.1f, 0.85f, 0.1f},
					   {sign * 0.13f, 0.43f, 0.f}, bone));
	}

	// Joueur : corps dynamique (capsule) + caméra à hauteur d'yeux.
	ObjectDesc player = ObjectDesc::Group(String("Joueur"));
	player.type = String(node_kind::BODY);
	player.tag = String("player");
	player.transform.position = {0.f, 1.0f, 1.2f};
	player.physics.body = BodyKind::DYNAMIC;
	player.physics.collider = ColliderKind::CAPSULE;
	player.physics.halfExtents = {0.35f, 0.5f, 0.35f};
	player.physics.mass = 70.f;
	player.physics.friction = 0.f;
	player.physics.restitution = 0.f;
	player.script = String("player");
	player.name = names.Next(player.name);
	scene::Node playerNode = player.ToNode();
	playerNode.Set(String("eye"), scene::PropertyValue::Str(String("Vue")));
	const scene::NodeId playerId = scene.tree.Add(dynamic, std::move(playerNode));
	ObjectDesc eye = ObjectDesc::Group(String("Vue"));
	eye.type = String(node_kind::CAMERA);
	eye.camera = Some(CameraNodeDesc{75.f, true});
	eye.transform.position = {0.f, 0.65f, 0.f};
	(void)Add(scene, names, playerId, eye);

	// Herse au bout du couloir : un corps statique qu'un script soulève.
	ObjectDesc door = Part("Porte", ShapeKind::BOX, {HALF_WIDTH * 2.f, HEIGHT - 0.2f, 0.25f}, {0.f, (HEIGHT - 0.2f) * 0.5f, 41.f},
						   detail::Tint(wood, 0.8f), MaterialKind::WOOD);
	door.physics.body = BodyKind::STATIC;
	door.script = String("door");
	scene::Node doorNode = door.ToNode();
	doorNode.name = names.Next(doorNode.name);
	doorNode.Set(String("open"), scene::PropertyValue::Bool(false));
	(void)scene.tree.Add(dynamic, std::move(doorNode));

	// ── Déclencheurs ───────────────────────────────────────────────────────
	const scene::NodeId triggers = Add(scene, names, root, ObjectDesc::Folder(String("Déclencheurs")));
	struct Zone {
		const char *name;
		math::FVector3 position, half;
		const char *event;
	};
	const Zone zones[] = {
		{"Zone du coffre", {0.4f, 1.f, 26.f}, {1.8f, 1.5f, 1.4f}, "open_chest"},
		{"Seuil de la porte", {0.f, 1.f, 38.5f}, {2.5f, 1.5f, 1.2f}, "open_door"},
		{"Sortie", {0.f, 1.f, 46.f}, {2.5f, 1.5f, 1.f}, "exit"},
	};
	for (const Zone &zone : zones) {
		ObjectDesc desc = ObjectDesc::Group(String(zone.name));
		desc.transform.position = zone.position;
		desc.trigger = Some(TriggerDesc{zone.half, String(zone.event), true});
		desc.type = String();
		(void)Add(scene, names, triggers, desc);
	}

	// ── Chemin de la visite guidée ─────────────────────────────────────────
	const scene::NodeId path = Add(scene, names, root, ObjectDesc::Folder(String("Chemin")));
	int pointIndex = 0;
	for (float z : {6.f, 25.8f, 38.f, 46.5f}) {
		ObjectDesc point = ObjectDesc::Group(String::Format("Étape %d", ++pointIndex));
		point.type = String(node_kind::SPAWN);
		point.tag = String("waypoint");
		point.transform.position = {0.f, 1.f, z};
		(void)Add(scene, names, path, point);
	}
	return scene;
}

/// Scripts de la bibliothèque du projet (attachés aux nœuds par leur nom).
[[nodiscard]] inline std::vector<ScriptAsset> MakeScriptLibrary() {
	// Les littéraux R"SLED( commencent par un saut de ligne : retiré, pour
	// que la ligne 1 de l'éditeur de code soit la première ligne du script.
	auto source = [](const char *text) { return String(text[0] == '\n' ? text + 1 : text); };
	return {
		ScriptAsset{String("torchlight"), String("Flamme vacillante (intensité + flamme)"), source(TORCHLIGHT_SCRIPT)},
		ScriptAsset{String("player"), String("Contrôleur à la première personne + visite guidée"), source(PLAYER_SCRIPT)},
		ScriptAsset{String("chest"), String("Ouvre le couvercle quand `open` est vrai"), source(CHEST_SCRIPT)},
		ScriptAsset{String("door"), String("Lève la herse quand `open` est vrai"), source(DOOR_SCRIPT)},
		ScriptAsset{String("skeleton"), String("Patrouille entre deux bornes"), source(SKELETON_SCRIPT)},
	};
}

} // namespace content

/// Le projet livré avec la démo. Trois scènes volontairement très
/// différentes : une vitrine de rendu, un niveau jouable, un banc de physique
/// — de quoi exercer chaque sous-système de la bibliothèque.
[[nodiscard]] inline Project MakeDemoProject() {
	Project project;
	project.name = "Démo éditeur de niveau";
	project.scenes.push_back(content::MakeShowcaseScene());
	project.scenes.push_back(content::MakeCircuitScene());
	project.scenes.push_back(content::MakePhysicsScene());
	project.scenes.push_back(content::MakeAssemblyScene());
	project.scenes.push_back(content::MakeDungeonScene());
	project.scripts = content::MakeScriptLibrary();
	project.activeScene = project.scenes.front().name;
	return project;
}

} // namespace game_editor
