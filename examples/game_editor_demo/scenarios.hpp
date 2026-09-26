#pragma once
/**
 * game_editor — scénarios : des SCRIPTS qui pilotent l'éditeur lui-même.
 *
 * Un scénario répond au besoin « rendre l'application manipulable en ligne de
 * commande et observable par captures d'écran ». Plutôt qu'un mini-format de
 * script d'automatisation dédié (une liste d'actions énumérées en C++), un
 * scénario est du code dans le MÊME langage que le gameplay : il a donc
 * boucles, fonctions, variables et tables, et n'importe qui peut en écrire un
 * nouveau dans un fichier et le passer à `--script`.
 *
 * Contrat : l'hôte définit la globale `shot_dir` (dossier des captures) puis
 * exécute la source ; ensuite il appelle `on_frame(numero_d_image)` à chaque
 * image. Tout le reste est du script ordinaire.
 *
 * Les scénarios tournent dans l'interpréteur « outil », séparé de celui du
 * gameplay : un scénario ne peut donc pas écraser par accident une variable
 * du script de jeu qu'il déclenche (et réciproquement).
 */
#include <vector>

#include "core/core.hpp"

namespace game_editor {

struct Scenario {
	const char *name;
	const char *description;
	long suggestedFrames; ///< durée conseillée si `--frames` n'est pas donné
	const char *source;
};

// ============================================================================
// tour — la visite guidée complète
// ============================================================================

inline constexpr const char *SCENARIO_TOUR = R"SLED(
# Visite guidée : ouvre les panneaux, change de thème, édite des propriétés,
# lance le mode Jeu, passe d'une scène à l'autre — et capture chaque étape.
let shots = 0

fn shot(name) {
    shots += 1
    let path = shot_dir .. "/" .. format("{}-{}.png", (shots < 10 and "0" .. shots or shots), name)
    if editor.screenshot(path) {
        editor.log("Capture " .. path)
    }
}

# Table [image, action] : les actions sont de vraies fonctions (valeurs de
# première classe du langage), pas des noms à interpréter.
let timeline = [
    [8, fn() {
        editor.status("Visite guidée — vitrine des matériaux")
        shot("demarrage")
    }],
    [24, fn() {
        editor.open_panel("outliner", 0)
        editor.select("Tore")
        editor.focus("Tore")
        shot("outliner-selection")
    }],
    [44, fn() {
        editor.open_panel("inspector", 0)
        object.set_position("Tore", 1.6, 2.2, -0.4)
        object.set_rotation("Tore", 25, 40, 0)
        object.set_scale("Tore", 1.2, 1.2, 1.2)
        shot("inspecteur-transform")
    }],
    [64, fn() {
        editor.open_panel("inspector", 1)
        object.set_material("Tore", {kind: "pbr", color: [60, 220, 190], metallic: 0.9, roughness: 0.12})
        object.set_material("Cube plastique", {color: [240, 120, 60], roughness: 0.35})
        shot("inspecteur-materiau")
    }],
    [84, fn() {
        editor.open_panel("inspector", 2)
        object.set_physics("Caisse bois", {body: "dynamic", mass: 6, restitution: 0.6, friction: 0.3})
        editor.select("Caisse bois")
        shot("inspecteur-physique")
    }],
    [104, fn() {
        editor.open_panel("console", 0)
        editor.log("Console de l'éditeur : sortie des scripts et des commandes")
        shot("console")
    }],
    [120, fn() {
        editor.set_theme("light")
        editor.status("Thème clair")
        shot("theme-clair")
    }],
    [140, fn() {
        editor.set_theme("aero")
        editor.status("Thème verre (Aero)")
        shot("theme-aero")
    }],
    [160, fn() {
        editor.set_theme("studio")
        editor.status("Construction scriptée d'une tour")
        # Construction procédurale : la démonstration que le langage sert
        # aussi à FABRIQUER du niveau, pas seulement à l'animer.
        for i in range(0, 6) {
            scene.spawn({
                name: "Tour " .. (i + 1),
                shape: "box",
                size: [1.1 - i * 0.1, 0.6, 1.1 - i * 0.1],
                pos: [7.5, 0.3 + i * 0.62, -3],
                color: [70 + i * 28, 200 - i * 20, 240],
                material: "pbr",
                metallic: 0.4,
                roughness: 0.3
            })
        }
        shot("construction-scriptee")
    }],
    [180, fn() {
        editor.play()
        editor.status("Mode Jeu — animations de la vitrine")
    }],
    [230, fn() { shot("animation-en-cours") }],
    [270, fn() {
        editor.stop()
        editor.switch_scene("Circuit")
        editor.status("Scène Circuit")
        shot("circuit-vue-edition")
    }],
    [300, fn() {
        editor.play()
        editor.status("Essai de jouabilité — pilote automatique")
    }],
    [380, fn() { shot("circuit-course") }],
    [560, fn() { shot("circuit-tours") }],
    [600, fn() {
        editor.stop()
        editor.switch_scene("Laboratoire physique")
        editor.play()
        editor.status("Banc d'essai du solveur")
    }],
    [700, fn() { shot("physique-empilement") }],
    [780, fn() {
        editor.stop()
        editor.switch_scene("Vitrine")
        editor.open_panel("profiler", 0)
        shot("profil-final")
        editor.log("Visite guidée terminée : " .. shots .. " captures")
    }]
]

fn on_frame(f) {
    for step in timeline {
        if step[0] == f {
            step[1]()
        }
    }
}
)SLED";

// ============================================================================
// circuit — l'essai de jouabilité
// ============================================================================

inline constexpr const char *SCENARIO_CIRCUIT = R"SLED(
# Essai de jouabilité du circuit : bascule sur la scène, lance le mode Jeu,
# laisse le pilote automatique boucler des tours, et capture la progression.
let shots = 0

fn shot(name) {
    shots += 1
    if editor.screenshot(shot_dir .. "/circuit-" .. name .. ".png") {
        editor.log("Capture " .. name)
    }
}

fn on_frame(f) {
    if f == 5 {
        editor.switch_scene("Circuit")
        editor.status("Circuit chargé")
    }
    if f == 20 {
        shot("depart")
        editor.play()
    }
    # Une capture toutes les 200 images : on voit la voiture progresser.
    if f > 20 and f % 200 == 0 {
        shot("tour-" .. int(f / 200))
    }
}
)SLED";

// ============================================================================
// physics — la charge du solveur
// ============================================================================

inline constexpr const char *SCENARIO_PHYSICS = R"SLED(
# Charge le banc de physique et le laisse se remplir, en capturant la montée
# en charge (nombre d'objets simulés) à intervalles réguliers.
fn on_frame(f) {
    if f == 5 {
        editor.switch_scene("Laboratoire physique")
        editor.play()
        editor.status("Largage en cours")
    }
    if f > 5 and f % 150 == 0 {
        editor.log(format("image {} — {} objets, {} img/s", f, scene.count(), int(editor.fps())))
        if not editor.screenshot(shot_dir .. "/physique-" .. int(f / 150) .. ".png") {
            editor.log("(pas de capture en mode sans écran)")
        }
    }
}
)SLED";

// ============================================================================
// themes — la galerie de thèmes
// ============================================================================

inline constexpr const char *SCENARIO_THEMES = R"SLED(
# Capture l'interface dans chacun des quatre thèmes.
let themes = ["studio", "dark", "light", "aero"]
let index = 0

fn on_frame(f) {
    if f < 10 { return nil }
    if f % 40 != 0 { return nil }
    if index >= len(themes) { return nil }

    let theme = themes[index]
    editor.set_theme(theme)
    editor.status("Thème : " .. theme)
    editor.screenshot(shot_dir .. "/theme-" .. theme .. ".png")
    index += 1
}
)SLED";

// ============================================================================
// stress — la mesure de performance
// ============================================================================

inline constexpr const char *SCENARIO_STRESS = R"SLED(
# Ajoute des objets par vagues et note les images par seconde à chaque palier :
# de quoi lire dans le rapport à partir de quelle charge l'affichage décroche.
let wave = 0
let per_wave = 40

fn on_frame(f) {
    if f < 30 { return nil }
    if f % 90 != 0 { return nil }
    if wave >= 6 {
        return nil
    }

    wave += 1
    for i in range(0, per_wave) {
        let angle = (wave * per_wave + i) * 0.37
        let radius = 4 + (i % 10) * 1.2
        scene.spawn({
            name: format("Charge {}-{}", wave, i),
            shape: "box",
            size: [0.6, 0.6, 0.6],
            pos: [math.cos(angle) * radius, 0.4 + (i % 5) * 0.7, math.sin(angle) * radius],
            color: [60 + wave * 30, 120, 220 - wave * 25],
            material: "plastic"
        })
    }
    editor.log(format("vague {} — {} objets — {} img/s", wave, scene.count(), int(editor.fps())))
    editor.screenshot(shot_dir .. "/charge-" .. wave .. ".png")
}
)SLED";

// ============================================================================
// smoke — la vérification minimale
// ============================================================================

inline constexpr const char *SCENARIO_SMOKE = R"SLED(
# Vérification rapide : chaque scène est chargée, jouée quelques images, puis
# arrêtée. `assert` fait échouer l'exécution (donc le processus) si une scène
# ne se charge pas — c'est le scénario à lancer en intégration continue.
let scenes = editor.scenes()
let index = 0

fn on_frame(f) {
    if f % 30 != 0 { return nil }
    if index >= len(scenes) {
        return nil
    }

    let name = scenes[index]
    assert(editor.switch_scene(name), "scène introuvable : " .. name)
    assert(scene.count() > 0, "scène vide : " .. name)
    editor.play()
    assert(editor.is_playing(), "le mode Jeu n'a pas démarré sur " .. name)
    editor.stop()
    editor.log(format("scène `{}` : {} objets — OK", name, scene.count()))
    index += 1
}
)SLED";

// ============================================================================
// edition — sélection au clic, manipulateur, magnétisme, annulation
// ============================================================================

inline constexpr const char *SCENARIO_EDITION = R"SLED(
# Rejoue le travail d'édition qu'on fait à la souris : viser un objet dans le
# viewport et cliquer, tirer les axes du manipulateur, activer le magnétisme,
# puis tout annuler. Chaque étape est VÉRIFIÉE par `assert` : le scénario
# échoue (code 1) si l'éditeur ne fait pas ce qu'il annonce.
let shots = 0
# Taille de viewport supposée pour les clics : celle du dock central de la
# fenêtre par défaut. Le rayon est calculé par l'éditeur lui-même, donc seuls
# comptent les PROPORTIONS (centre = milieu du viewport).
let vw = 846
let vh = 560

fn shot(name) {
    shots += 1
    let path = shot_dir .. "/" .. format("{}-{}.png", (shots < 10 and "0" .. shots or shots), name)
    if editor.screenshot(path) { editor.log("Capture " .. path) }
}

fn about(value, expected, tolerance) {
    return value > expected - tolerance and value < expected + tolerance
}

let timeline = [
    [10, fn() {
        editor.switch_scene("Vitrine")
        editor.status("Édition : clic, manipulateur, magnétisme, annulation")
        # Vise le centre du viewport : la caméra d'édition regarde la scène,
        # il y a forcément un objet sous le curseur.
        editor.select("Tore")
        editor.focus("Tore")
        shot("selection-au-clic")
    }],
    [26, fn() {
        let touche = editor.select_at(vw / 2, vh / 2, vw, vh)
        assert(touche != nil, "un clic au centre du viewport n'a rien touché")
        editor.log("Clic au centre : " .. touche)
        shot("clic-viewport")
    }],
    [42, fn() {
        # Déplacement : le manipulateur tire l'objet de 3 unités sur X.
        editor.gizmo_mode("translate")
        editor.select("Tore")
        let avant = object.position("Tore")
        assert(editor.gizmo_drag("x", 0, 3), "le glissé du manipulateur a été refusé")
        let apres = object.position("Tore")
        assert(about(apres[0] - avant[0], 3, 0.05), "déplacement inexact sur X")
        editor.log(format("Déplacement X : {} -> {}", avant[0], apres[0]))
        shot("manipulateur-deplacer")
    }],
    [58, fn() {
        # Rotation : 45° autour de Y.
        editor.gizmo_mode("rotate")
        let avant = object.rotation("Tore")
        assert(editor.gizmo_drag("y", 0, 45), "rotation refusée")
        let apres = object.rotation("Tore")
        assert(about(apres[1] - avant[1], 45, 1), "rotation inexacte sur Y")
        shot("manipulateur-tourner")
    }],
    [74, fn() {
        # Échelle : la poignée grossit l'objet.
        editor.gizmo_mode("scale")
        let avant = object.scale("Tore")
        assert(editor.gizmo_drag("y", 0, 0.6), "mise à l'échelle refusée")
        let apres = object.scale("Tore")
        assert(apres[1] > avant[1], "l'objet n'a pas grandi")
        shot("manipulateur-redimensionner")
    }],
    [90, fn() {
        # Magnétisme : le résultat tombe sur la grille, quel que soit le geste.
        editor.snap(true)
        editor.snap_steps(1, 15, 0.25)
        editor.gizmo_mode("translate")
        assert(editor.gizmo_drag("z", 0, 2.37), "glissé magnétique refusé")
        let p = object.position("Tore")
        assert(about(p[2] - math.floor(p[2]), 0, 0.001), "le magnétisme n'a pas aligné Z sur la grille")
        editor.log(format("Avec magnétisme, Z = {}", p[2]))
        shot("magnetisme")
    }],
    [106, fn() {
        # Annulation : chaque glissé est UNE étape, pas une par image.
        let etapes = 0
        while editor.undo() {
            etapes += 1
            if etapes > 20 { break }
        }
        assert(etapes >= 4, "les glissés n'ont pas été regroupés en étapes annulables")
        editor.log(format("{} étapes annulées", etapes))
        editor.snap(false)
        shot("apres-annulation")
    }],
    [120, fn() {
        assert(editor.redo(), "rien à rétablir")
        editor.log("Rétabli : " .. (editor.undo_label() or "?"))
        shot("apres-retablissement")
        editor.status("Édition vérifiée")
    }],
]

let next = 0
fn on_frame(f) {
    if next >= len(timeline) { return nil }
    let entry = timeline[next]
    if f >= entry[0] {
        entry[1]()
        next += 1
    }
}
)SLED";

// ============================================================================
// camera — navigation libre dans la vue
// ============================================================================

inline constexpr const char *SCENARIO_CAMERA = R"SLED(
# Fait le tour d'un objet, recule, panoramique, plonge : les gestes de
# navigation de l'éditeur, joués un par un et VÉRIFIÉS (la caméra doit
# vraiment avoir bougé, et l'orbite doit conserver la distance au pivot).
let shots = 0

fn shot(name) {
    shots += 1
    let path = shot_dir .. "/" .. format("{}-{}.png", (shots < 10 and "0" .. shots or shots), name)
    if editor.screenshot(path) { editor.log("Capture " .. path) }
}

fn distance(a, b) {
    let dx = a[0] - b[0]
    let dy = a[1] - b[1]
    let dz = a[2] - b[2]
    return math.sqrt(dx * dx + dy * dy + dz * dz)
}

let timeline = [
    [8, fn() {
        editor.switch_scene("Vitrine")
        editor.select("Cristal")
        editor.focus("Cristal")
        editor.status("Navigation : orbite, panoramique, molette, plongée")
        shot("depart")
    }],
    [24, fn() {
        # Orbite d'un quart de tour : la distance au pivot doit être conservée.
        let pivot = camera.pivot()
        let avant = camera.position()
        let rayon = distance(avant, pivot)
        for i in range(0, 12) { camera.orbit(0.13, 0) }
        let apres = camera.position()
        assert(distance(apres, avant) > 1, "l'orbite n'a pas déplacé la caméra")
        assert(math.abs(distance(apres, pivot) - rayon) < 0.1, "l'orbite n'a pas conservé la distance au pivot")
        editor.log(format("Orbite : rayon {} conservé", rayon))
        shot("orbite")
    }],
    [40, fn() {
        # Plongée : tangage NÉGATIF, la caméra monte au-dessus du pivot et
        # regarde l'objet de dessus (même sens que le regard libre).
        let avant = camera.position()
        for i in range(0, 8) { camera.orbit(0, -0.09) }
        assert(camera.position()[1] > avant[1] + 1, "la plongée n'a pas élevé la caméra")
        shot("plongee")
    }],
    [56, fn() {
        # Molette : on recule, la scène entière entre dans le cadre.
        let avant = camera.position()
        for i in range(0, 6) { camera.dolly(-1.6) }
        assert(distance(camera.position(), avant) > 5, "la molette n'a pas reculé la caméra")
        shot("recul")
    }],
    [72, fn() {
        # Panoramique : la vue glisse latéralement et vers le haut.
        let avant = camera.position()
        for i in range(0, 10) { camera.pan(0.55, 0.22) }
        assert(distance(camera.position(), avant) > 3, "le panoramique n'a pas déplacé la caméra")
        shot("panoramique")
    }],
    [88, fn() {
        # Vol libre : avant/droite/haut, sans toucher à l'orientation.
        for i in range(0, 14) { camera.move(0.35, 0.12, 0.5) }
        shot("vol-libre")
        editor.status("Navigation vérifiée")
    }],
]

let next = 0
fn on_frame(f) {
    if next >= len(timeline) { return nil }
    let entry = timeline[next]
    if f >= entry[0] {
        entry[1]()
        next += 1
    }
}
)SLED";

/// Catalogue des scénarios intégrés (`--list-scenarios`).
// ============================================================================
// node_hierarchy — la hiérarchie de nœuds, de bout en bout
// ============================================================================

inline constexpr const char *SCENARIO_NODE_HIERARCHY = R"SLED(
# Construit une hiérarchie, la manipule comme on le ferait à la souris, et
# VÉRIFIE chaque étape : les enfants suivent leur parent, un reparentage
# conserve la position monde, la duplication emporte le sous-arbre, l'annulation
# restitue l'arbre exact, et un aller-retour fichier le rend à l'identique.
# Toute assertion fausse fait échouer le scénario (code de retour non nul).
let shots = 0

fn shot(name) {
    shots += 1
    let path = shot_dir .. "/" .. format("{}-{}.png", (shots < 10 and "0" .. shots or shots), name)
    if editor.screenshot(path) { editor.log("Capture " .. path) }
}

fn about(value, expected, tolerance) {
    return value > expected - tolerance and value < expected + tolerance
}

let timeline = [
    [8, fn() {
        editor.switch_scene("Assemblages")
        editor.status("Hiérarchie : composition, reparentage, duplication, annulation")
        # La scène livrée est déjà un arbre profond : on le vérifie avant tout.
        assert(node.parent("Œil") == "Tête", "l'œil du robot devrait être sous sa tête")
        assert(node.path("Main") != nil, "la main du robot devrait avoir un chemin")
        editor.select("Robot")
        editor.focus("Robot")
        shot("arbre-de-depart")
    }],
    [24, fn() {
        # 1. Déplacer le PARENT : tout le sous-arbre suit, sans que rien ne
        #    soit cité nommément.
        let avant = node.world_position("Main")
        object.set_position("Robot", -7, 0, 6)
        let apres = node.world_position("Main")
        assert(about(apres[2] - avant[2], 6, 0.001), "la main n'a pas suivi le robot")
        editor.log("Le sous-arbre a suivi le parent : dz = " .. (apres[2] - avant[2]))
        shot("parent-deplace")
    }],
    [40, fn() {
        # 2. Modifier le transform LOCAL d'un enfant ne bouge que lui.
        let tete = node.world_position("Tête")
        object.set_position("Œil", 0, 0.3, -0.55)
        let apres = node.world_position("Tête")
        assert(about(apres[1] - tete[1], 0, 0.001), "modifier l'œil a bougé la tête")
        editor.select("Œil")
        shot("enfant-local")
    }],
    [56, fn() {
        # 3. Reparentage en gardant la position MONDE : l'objet ne bouge pas
        #    à l'écran, seul son transform local est recalculé.
        let avant = node.world_position("Lampe")
        node.reparent("Lampe", "Véhicule")
        let apres = node.world_position("Lampe")
        assert(node.parent("Lampe") == "Véhicule", "la lampe n'a pas changé de parent")
        assert(about(apres[0] - avant[0], 0, 0.01), "la lampe a bougé alors qu'elle ne devait pas")
        assert(about(apres[1] - avant[1], 0, 0.01), "la lampe a bougé en hauteur")
        editor.log("Reparentage sans déplacement visible")
        shot("reparentage")
    }],
    [72, fn() {
        # 4. Duplication d'un sous-arbre entier.
        let avant = scene.count()
        let copie = node.duplicate("Personnage")
        assert(copie != nil, "la duplication a échoué")
        assert(scene.count() > avant + 3, "le sous-arbre n'a pas été copié en entier")
        # La copie a ses PROPRES enfants, distincts de ceux de l'original.
        assert(len(node.children(copie)) == len(node.children("Personnage")),
               "la copie n'a pas la même forme que l'original")
        object.set_position(copie, 10, 0, 0)
        editor.log("Copie « " .. copie .. " » : " .. scene.count() .. " objets")
        shot("duplication")
    }],
    [88, fn() {
        # 5. Suppression du sous-arbre, puis annulation.
        let avant = scene.count()
        editor.select("Bâtiment")
        scene.remove("Bâtiment")
        assert(scene.count() < avant, "la suppression n'a rien retiré")
        assert(not scene.exists("Toit"), "le toit aurait dû partir avec le bâtiment")
        shot("sous-arbre-supprime")

        assert(editor.undo(), "annulation refusée")
        assert(scene.count() == avant, "l'annulation n'a pas tout restitué")
        assert(scene.exists("Toit"), "le toit n'est pas revenu")
        assert(node.parent("Toit") == "Structure", "le toit est revenu au mauvais endroit")
        editor.log("Annulation : arbre restitué à l'identique")
        shot("annulation")
    }],
    [104, fn() {
        # 6. Rétablir, puis re-annuler : l'historique traverse les opérations
        #    hiérarchiques comme les autres.
        assert(editor.redo(), "rétablissement refusé")
        assert(not scene.exists("Toit"), "le rétablissement n'a pas re-supprimé")
        assert(editor.undo(), "seconde annulation refusée")
        assert(scene.exists("Toit"), "seconde annulation sans effet")
        shot("retablir-annuler")
    }],
    [120, fn() {
        # 7. Scène réutilisable : emballer un sous-arbre, l'instancier deux
        #    fois, vérifier que les instances sont indépendantes.
        assert(node.save_scene("Véhicule", saves_dir .. "/scenes/vehicule.tscene"), "sauvegarde de scène refusée")
        let a = node.instantiate(saves_dir .. "/scenes/vehicule.tscene")
        let b = node.instantiate(saves_dir .. "/scenes/vehicule.tscene")
        assert(a != nil and b != nil and a != b, "les deux instances devraient être distinctes")
        object.set_position(a, -14, 0, 12)
        object.set_position(b, 14, 0, 12)
        let pa = node.world_position(a)
        let pb = node.world_position(b)
        assert(about(pa[0], -14, 0.01) and about(pb[0], 14, 0.01), "les instances ne sont pas indépendantes")
        editor.log("Deux instances de la même scène : " .. a .. " et " .. b)
        shot("instances")
    }],
    [136, fn() {
        # 8. Aller-retour fichier : l'arbre doit revenir identique.
        let avant = scene.count()
        let chemin = saves_dir .. "/projects/hierarchie.json"
        assert(editor.save(chemin), "sauvegarde du projet refusée")
        assert(editor.open(chemin), "rechargement refusé")
        assert(scene.count() == avant, "le nombre d'objets a changé au rechargement")
        assert(node.parent("Œil") == "Tête", "la hiérarchie n'a pas survécu au rechargement")
        assert(node.path("Main") != nil, "le chemin de la main a disparu")
        editor.log("Aller-retour fichier : " .. scene.count() .. " objets, hiérarchie intacte")
        shot("apres-rechargement")
    }],
]

fn on_frame(frame) {
    for entry in timeline {
        if entry[0] == frame { entry[1]() }
    }
}
)SLED";

// ============================================================================
// interface — la visite de l'interface, écran par écran
// ============================================================================

inline constexpr const char *SCENARIO_INTERFACE = R"SLED(
# Rejoue, dans le donjon, les écrans de référence de l'interface : arbre et
# inspecteur, menu contextuel, lumière sélectionnée, navigateur de ressources,
# propriétés en JSON, éditeur de script, bibliothèque de scripts, mode Jeu en
# plein écran. Chaque étape VÉRIFIE ce qu'elle montre ; sans fenêtre,
# editor.ui() rend faux et les étapes visuelles sont simplement sautées.
let shots = 0
let ui_ok = true

fn shot(name) {
    shots += 1
    let path = shot_dir .. "/" .. format("{}-{}.png", (shots < 10 and "0" .. shots or shots), name)
    if editor.screenshot(path) { editor.log("Capture " .. path) }
}

# Une commande d'interface ; son échec n'est une faute qu'avec une fenêtre.
fn ui(command, argument) {
    let done = editor.ui(command, argument)
    if not done and ui_ok {
        editor.warn("editor.ui(" .. command .. ", " .. str(argument) .. ") sans effet")
    }
    return done
}

let timeline = [
    [5, fn() {
        assert(editor.switch_scene("Donjon"), "scène Donjon introuvable")
        ui_ok = editor.ui("close_menus", "")
        editor.status("Visite de l'interface")
    }],
    [16, fn() {
        # 1. Vue d'ensemble : un maillage sélectionné, ses composants à droite.
        assert(editor.select("Porte"), "la porte devrait être sélectionnable")
        ui("library", 0)
    }],
    [26, fn() { shot("vue-ensemble") }],
    [30, fn() {
        # 2. Menu contextuel de l'arbre, sous-menu de création déployé.
        ui("context_menu", "Pilier 3")
    }],
    [34, fn() { ui("context_submenu", "") }],
    [38, fn() { shot("menu-contextuel") }],
    [40, fn() {
        # ... et il crée vraiment : une lumière enfant du pilier.
        assert(editor.selected() == "Pilier 3", "le clic droit sélectionne la ligne")
        ui("close_menus", "")
        let before = scene.count()
        if ui("create", "point_light") {
            assert(scene.count() == before + 1, "la création n'a rien ajouté")
            assert(node.parent(editor.selected()) == "Pilier 3", "la lumière devrait être l'enfant du pilier")
            assert(editor.undo(), "l'ajout devrait s'annuler")
        }
    }],
    [50, fn() {
        # 3. Une torche : sections Lumière et Ombres, gizmo sur la flamme.
        assert(editor.select("Torche 3"), "torche introuvable")
        editor.focus("Torche 3")
        ui("section", "Lumière")
    }],
    [60, fn() { shot("lumiere") }],
    [62, fn() {
        # 4. Navigateur de ressources : modèles glTF en vignettes 3D.
        ui("browse", "models/animals")
        ui("select_asset", "Horse.gltf")
    }],
    [80, fn() { shot("ressources") }],
    [82, fn() {
        # 5. Les propriétés de la torche, éditées en JSON (réglages du vacillement).
        let opened = ui("open_json", "Torche 3")
        assert(opened or not ui_ok, "document JSON non ouvert")
    }],
    [90, fn() { shot("proprietes-json") }],
    [92, fn() {
        # 6. Le script attaché, avec coloration et numéros de ligne.
        ui("open_script", "torchlight")
    }],
    [100, fn() { shot("script") }],
    [102, fn() {
        # 7. La bibliothèque de scripts : état de compilation de chacun.
        ui("document", 0)
        ui("browse", "projet:/scripts")
        ui("library", 1)
    }],
    [110, fn() { shot("scripts") }],
    [112, fn() {
        # 8. Mode Jeu plein écran : la visite guidée du joueur, torches animées.
        editor.select("Joueur")
        if not ui("run_mode", "on") { editor.play() }
        assert(editor.is_playing(), "le mode Jeu devrait tourner")
    }],
    [200, fn() { shot("mode-jeu") }],
    [206, fn() {
        ui("run_mode", "off")
        editor.stop()
        assert(not editor.is_playing(), "Échap/arrêt devrait revenir à l'édition")
        editor.log("Visite de l'interface : " .. shots .. " captures")
    }],
]

fn on_frame(frame) {
    for entry in timeline {
        if entry[0] == frame { entry[1]() }
    }
}
)SLED";

[[nodiscard]] inline std::vector<Scenario> BuiltinScenarios() {
	return {
		{"tour", "Visite guidée complète : panneaux, thèmes, édition, mode Jeu, 3 scènes", 820, SCENARIO_TOUR},
		{"circuit", "Essai de jouabilité du circuit de voiture (pilote automatique)", 1200, SCENARIO_CIRCUIT},
		{"physics", "Banc d'essai du solveur physique sous charge croissante", 900, SCENARIO_PHYSICS},
		{"themes", "Galerie des trois thèmes d'interface", 160, SCENARIO_THEMES},
		{"stress", "Mesure de performance : vagues d'objets et images par seconde", 600, SCENARIO_STRESS},
		{"edition", "Clic dans le viewport, manipulateur (déplacer/tourner/redimensionner), magnétisme, annulation",
		 140, SCENARIO_EDITION},
		{"camera", "Navigation dans la vue : orbite, plongée, molette, panoramique, vol libre", 110,
		 SCENARIO_CAMERA},
		{"node_hierarchy",
		 "Hiérarchie de nœuds : composition, reparentage, duplication, annulation, instances, aller-retour fichier",
		 150, SCENARIO_NODE_HIERARCHY},
		{"smoke", "Vérification minimale de chaque scène (assertions, pour l'intégration continue)", 150,
		 SCENARIO_SMOKE},
		{"interface",
		 "Visite de l'interface dans le donjon : arbre, menu contextuel, lumière, ressources, JSON, script, mode Jeu",
		 215, SCENARIO_INTERFACE},
	};
}

[[nodiscard]] inline Option<Scenario> FindScenario(const String &name) {
	for (const Scenario &scenario : BuiltinScenarios())
		if (name == scenario.name)
			return Some(scenario);
	return NONE;
}

} // namespace game_editor
