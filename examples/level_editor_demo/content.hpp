#pragma once
/**
 * level_editor — contenu de démonstration : le projet par défaut (trois
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
#include <vector>

#include "core/core.hpp"
#include "math/math.hpp"

#include "project.hpp"

namespace level_editor {

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
        object.set_position("Balise", p[0], 1.6 + sin(t * 2) * bob_height, p[2])
    }

    # Le tore tourne sur deux axes à des vitesses différentes.
    object.set_rotation("Tore", t * spin_speed * 0.5, t * spin_speed, 0)

    # L'icosaèdre pulse du bleu au cyan : une animation de MATÉRIAU.
    let pulse = (sin(t * 3) + 1) / 2
    object.set_color("Cristal", 40 + pulse * 60, 120 + pulse * 120, 255)

    # Les trois plots montent et descendent en décalé (onde progressive).
    for i in range(0, 3) {
        let name = "Plot " .. (i + 1)
        let base = object.position(name)
        if base {
            object.set_position(name, base[0], 0.5 + sin(t * 1.5 + i * 0.9) * 0.35, base[2])
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
    heading = pi() / 2
    manual = false
    editor.log("Circuit : " .. len(checkpoints) .. " points de passage")
    editor.status("Circuit prêt — ZQSD/WASD pour conduire, sinon pilote automatique")
}

# Cap à suivre pour rejoindre un point : atan2 du vecteur voiture -> cible.
fn heading_to(target) {
    let here = object.position(car)
    let there = object.position(target)
    if not here or not there { return heading }
    return atan2(there[0] - here[0], there[2] - here[2])
}

# Écart angulaire ramené dans [-pi, pi] : sans ce repliement, un cap qui
# franchit ±pi ferait braquer la voiture dans le mauvais sens pendant un tour
# complet.
fn wrap_angle(a) {
    while a > pi() { a -= 2 * pi() }
    while a < -pi() { a += 2 * pi() }
    return a
}

fn autopilot_steer() {
    if len(checkpoints) == 0 { return 0 }
    let target = checkpoints[next_checkpoint]
    let wanted = heading_to(target)
    let delta = wrap_angle(wanted - heading)
    return clamp(delta * 1.8, -1, 1)
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
    speed = clamp(speed, -6, max_speed)

    # On ne braque qu'en roulant (comme une vraie voiture à l'arrêt).
    let grip = clamp(abs(speed) / 8, 0, 1)
    heading += steer * turn_rate * grip * dt * sign(speed)

    let vx = sin(heading) * speed
    let vz = cos(heading) * speed
    let current = object.velocity(car)
    let vy = 0
    if current { vy = current[1] }
    object.set_velocity(car, vx, vy, vz)
    object.set_rotation(car, 0, deg(heading), 0)

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
        pos: [cos(angle) * 2.5, 9 + dropped * 0.4, sin(angle) * 2.5],
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
	scene.name = "Vitrine";
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
	scene.objects.push_back(std::move(ground));

	scene.objects.push_back(MakeObject("Cube plastique", ShapeKind::BOX, {1.6f, 1.6f, 1.6f}, {-4.5f, 0.8f, 0.f},
									   sdl3::Color{80, 140, 220, 255}, MaterialKind::PLASTIC));

	ObjectDesc metal = MakeObject("Sphère métal", ShapeKind::SPHERE, {1.6f, 1.6f, 1.6f}, {-1.8f, 0.8f, 1.6f},
								  sdl3::Color{205, 208, 216, 255}, MaterialKind::METAL);
	metal.segments = 32;
	metal.material.roughness = 0.18f;
	metal.material.metallic = 1.f;
	scene.objects.push_back(std::move(metal));

	ObjectDesc pbr = MakeObject("Tore", ShapeKind::TORUS, {2.6f, 0.8f, 2.6f}, {1.6f, 1.1f, -0.4f},
								sdl3::Color{230, 176, 62, 255}, MaterialKind::PBR);
	pbr.segments = 28;
	pbr.material.metallic = 0.85f;
	pbr.material.roughness = 0.28f;
	scene.objects.push_back(std::move(pbr));

	ObjectDesc wood = MakeObject("Caisse bois", ShapeKind::BOX, {1.4f, 1.4f, 1.4f}, {4.4f, 0.7f, 1.2f},
								 sdl3::Color{146, 102, 62, 255}, MaterialKind::WOOD);
	wood.physics.body = BodyKind::DYNAMIC;
	wood.physics.mass = 3.f;
	scene.objects.push_back(std::move(wood));

	ObjectDesc crystal = MakeObject("Cristal", ShapeKind::ICOSAHEDRON, {1.5f, 1.5f, 1.5f}, {0.f, 2.6f, 3.4f},
									sdl3::Color{60, 200, 255, 255}, MaterialKind::UNLIT);
	crystal.segments = 24;
	scene.objects.push_back(std::move(crystal));

	ObjectDesc beacon = MakeObject("Balise", ShapeKind::SPHERE, {0.9f, 0.9f, 0.9f}, {5.f, 1.6f, 5.f},
								   sdl3::Color{70, 235, 130, 255}, MaterialKind::UNLIT);
	beacon.segments = 20;
	scene.objects.push_back(std::move(beacon));

	for (int i = 0; i < 3; ++i) {
		ObjectDesc post = MakeObject("Plot", ShapeKind::CYLINDER, {0.7f, 1.f, 0.7f},
									 {-7.f + float(i) * 1.6f, 0.5f, -4.5f}, sdl3::Color{220, 96, 96, 255},
									 MaterialKind::PLASTIC);
		post.name = String::Format("Plot %d", i + 1);
		post.segments = 20;
		scene.objects.push_back(std::move(post));
	}

	// Paire de portails : A regarde vers +Z, B est retourné de 180° près de la
	// balise — traverser A ressort donc dans le coin animé de la scène.
	ObjectDesc portalA = MakeObject("Portail A", ShapeKind::PORTAL_QUAD, {2.8f, 2.8f, 0.1f}, {-8.f, 1.4f, 0.f},
									sdl3::Color::WHITE(), MaterialKind::BASIC);
	portalA.tag = "portal_a";
	portalA.material.doubleSided = true;
	scene.objects.push_back(std::move(portalA));

	ObjectDesc portalB = MakeObject("Portail B", ShapeKind::PORTAL_QUAD, {2.8f, 2.8f, 0.1f}, {8.f, 1.4f, 4.f},
									sdl3::Color::WHITE(), MaterialKind::BASIC);
	portalB.tag = "portal_b";
	portalB.transform.eulerDeg = {0.f, 180.f, 0.f};
	portalB.material.doubleSided = true;
	scene.objects.push_back(std::move(portalB));

	ObjectDesc ball = MakeObject("Bille", ShapeKind::SPHERE, {0.8f, 0.8f, 0.8f}, {-8.f, 1.6f, 3.2f},
								 sdl3::Color{235, 86, 86, 255}, MaterialKind::PLASTIC);
	ball.segments = 18;
	ball.physics.body = BodyKind::DYNAMIC;
	ball.physics.collider = ColliderKind::SPHERE;
	ball.physics.halfExtents = {0.4f, 0.4f, 0.4f};
	ball.physics.mass = 1.f;
	ball.physics.restitution = 0.55f;
	scene.objects.push_back(std::move(ball));

	return scene;
}

/// Scène « Circuit » : piste fermée générée procéduralement (dalles + rails
/// intérieur/extérieur), points de passage étiquetés, voiture suivie par la
/// caméra. La conduite et le chronométrage sont dans CIRCUIT_SCRIPT.
[[nodiscard]] inline SceneDesc MakeCircuitScene() {
	SceneDesc scene;
	scene.name = "Circuit";
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
	scene.objects.push_back(std::move(ground));

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
		slab.transform.eulerDeg = {0.f, yawDeg, 0.f};
		slab.material.roughness = 0.9f;
		slab.physics.body = BodyKind::STATIC;
		slab.physics.friction = 0.9f;
		slab.tag = "road";
		scene.objects.push_back(std::move(slab));

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
				rail.transform.eulerDeg = {0.f, yawDeg, 0.f};
				rail.physics.body = BodyKind::STATIC;
				rail.physics.restitution = 0.15f;
				rail.tag = "barrier";
				scene.objects.push_back(std::move(rail));
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
			checkpoint.transform.eulerDeg = {0.f, yawDeg, 0.f};
			checkpoint.tag = "checkpoint";
			scene.objects.push_back(std::move(checkpoint));
		}
	}

	// Voiture : sur la dalle de départ (angle 0 => x=0, z=RADIUS), orientée
	// selon la TANGENTE à cet endroit, c'est-à-dire +X — d'où le lacet de 90°
	// (le script part du même cap, cf. CIRCUIT_SCRIPT::on_start).
	ObjectDesc car = MakeObject("Voiture", ShapeKind::BOX, {1.9f, 0.9f, 3.8f}, {0.f, 0.75f, RADIUS},
								sdl3::Color{242, 92, 62, 255}, MaterialKind::PBR);
	car.transform.eulerDeg = {0.f, 90.f, 0.f};
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
	scene.objects.push_back(std::move(car));

	// Enfant de la voiture : sa transformation est RELATIVE au parent (c'est
	// le graphe de scène qui compose), pas absolue.
	ObjectDesc cabin = MakeObject("Habitacle", ShapeKind::BOX, {1.5f, 0.6f, 1.6f}, {0.f, 0.75f, -0.2f},
								  sdl3::Color{70, 76, 98, 255}, MaterialKind::PBR);
	cabin.parent = "Voiture";
	cabin.material.roughness = 0.35f;
	scene.objects.push_back(std::move(cabin));

	ObjectDesc arch = MakeObject("Arche de départ", ShapeKind::TORUS, {14.f, 1.2f, 14.f}, {0.f, 0.f, RADIUS},
								 sdl3::Color{250, 236, 80, 255}, MaterialKind::UNLIT);
	arch.segments = 24;
	arch.transform.eulerDeg = {90.f, 0.f, 0.f};
	arch.transform.scale = {0.55f, 0.55f, 0.55f};
	scene.objects.push_back(std::move(arch));

	return scene;
}

/// Scène « Laboratoire physique » : un empilement stable au départ, que le
/// script bombarde ensuite — sert de banc de mesure au solveur.
[[nodiscard]] inline SceneDesc MakePhysicsScene() {
	SceneDesc scene;
	scene.name = "Laboratoire physique";
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
	scene.objects.push_back(std::move(ground));

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
			scene.objects.push_back(std::move(brick));
		}
	}

	ObjectDesc wall = MakeObject("Mur", ShapeKind::BOX, {14.f, 3.f, 0.6f}, {0.f, 1.5f, 6.f},
								 sdl3::Color{70, 74, 92, 255}, MaterialKind::PBR);
	wall.physics.body = BodyKind::STATIC;
	scene.objects.push_back(std::move(wall));

	return scene;
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
	project.activeScene = project.scenes.front().name;
	return project;
}

} // namespace level_editor
