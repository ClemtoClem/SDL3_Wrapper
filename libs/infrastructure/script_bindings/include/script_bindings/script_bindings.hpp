#pragma once
/**
 * script_bindings — point d'entrée unifié pour toutes les bibliothèques
 * `Script` du dépôt.
 *
 * Chaque module expose son propre espace de noms via une fonction
 * `Install*Library` autonome :
 *
 *   | Module     | Espace de noms | Dépendance hôte requise            |
 *   |------------|----------------|------------------------------------|
 *   | archive    | `archive`      | (aucune — fichiers uniquement)     |
 *   | audio      | `audio`        | audio::AudioEngine                 |
 *   | ecs        | `ecs`          | (monde possédé ou hôte)            |
 *   | dataio     | `dataio`       | (aucune — documents JSON/YAML/CSV…) |
 *   | generators | `gen`          | (aucune — pur calcul)              |
 *   | physics    | `physics`      | physics::World                     |
 *   | render3d   | `render3d`     | render3d::Canvas + Object3D racine |
 *   | sql        | `sql`          | sql::Database                      |
 *   | ui         | `ui`           | UiBinding                          |
 *
 * `InstallAll(vm, config)` installe d'un coup toutes les bibliothèques dont
 * les dépendances sont présentes dans la configuration. Les modules dont les
 * dépendances manquent sont simplement ignorés — un script de test sans
 * fenêtre GPU n'a donc pas besoin des bindings `render3d`, et un script qui
 * ne fait que du calcul n'a besoin d'aucun module.
 *
 * Les outils partagés par plusieurs bibliothèques (lecture des tables
 * d'options, vecteurs, octets, couleurs) sont dans `script_common.hpp` et
 * `script_color.hpp` — espace `scriptlib`.
 *
 * Les modules `jobs`, `platform` et `resources` exposent aussi leurs propres
 * headers `script_jobs.hpp`, `script_platform.hpp`, `script_resources.hpp`
 * (voir leurs fichiers respectifs) — ils suivent exactement le même patron.
 */
#include <memory>
#include <vector>

#include <script/script.hpp>

#include "script_archive.hpp"
#include "script_audio.hpp"
#include "script_ecs.hpp"
#include "script_dataio.hpp"
#include "script_generator.hpp"
#include "script_physics.hpp"
#include "script_render3d.hpp"
#include "script_sql.hpp"
#include "script_ui.hpp"

namespace script {

struct BindingsConfig {
    // ── archive (aucune dépendance : fichiers du disque seulement) ────────
    bool archive = false;

    // ── audio ─────────────────────────────────────────────────────────────
    audio::AudioEngine *audioEngine = nullptr;
    AudioPathResolver audioResolve{};

    // ── ecs ───────────────────────────────────────────────────────────────
    std::shared_ptr<EcsWorld> ecsWorld;

    // ── generators (pas de dépendance) ────────────────────────────────────
    bool generators = false;

    // ── dataio (pas de dépendance) ────────────────────────────────────────
    bool dataio = false;

    // ── physics ───────────────────────────────────────────────────────────
    physics::World *physicsWorld = nullptr;

    // ── render3d ──────────────────────────────────────────────────────────
    render3d::Canvas *canvas3d = nullptr;
    render3d::Object3D *sceneRoot = nullptr;

    // ── sql ───────────────────────────────────────────────────────────────
    sql::Database *sqlDatabase = nullptr;

    // ── ui ────────────────────────────────────────────────────────────────
    std::shared_ptr<UiBinding> uiBinding;
};

/// Installe chaque bibliothèque dont les dépendances sont présentes dans
/// `config`. Rend la liste des espaces de noms effectivement installés (pour
/// le journal de bord ou la console d'un éditeur).
std::vector<String> InstallAll(Interpreter &vm, const BindingsConfig &config = {});

} // namespace script