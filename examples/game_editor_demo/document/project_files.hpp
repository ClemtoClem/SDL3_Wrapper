#pragma once
/**
 * Stockage d'un projet sur le disque — un DOSSIER par projet :
 *
 *   saves/game_editor_demo/projects/<Nom>/
 *     <Nom>.json          manifeste : nom, scène active, liste des scènes et
 *                         des scripts (chemins relatifs au dossier)
 *     assets/             ressources propres au projet (modèles, textures…)
 *     scenes/<Scène>.scene
 *                         une scène par fichier : ambiance, caméras, arbre
 *     objects/<Objet>.object
 *                         un objet par fichier : arborescence de nœuds
 *                         réutilisable (cf. objects.hpp), sans réglages du
 *                         monde ; ses instances ne sont que des références
 *     scripts/<nom>.script  scripts de la bibliothèque (comportements attachés
 *                         aux nœuds) ; le script de JEU d'une scène est
 *                         `scripts/<Scène>.gameplay.script`
 *
 * Tout le contenu vit donc dans des fichiers de données lisibles (JSON,
 * Script) : rien n'est plus construit en dur dans le code de l'éditeur.
 *
 * Un `.scene` peut aussi être écrit SEUL (« Enregistrer la scène sous… ») :
 * il porte alors son script de jeu en ligne (`gameplay_script`) au lieu d'une
 * référence (`gameplay_script_file`), pour tenir en un seul fichier. La
 * lecture accepte les deux formes.
 *
 * Un manifeste des versions ≤ 4 (scènes et scripts ENTIERS dans le JSON) se
 * lit encore ; l'enregistrement suivant le réécrit éclaté en fichiers.
 */
#include <algorithm>
#include <vector>

#include "core/core.hpp"
#include "data/json.hpp"
#include "data/script.hpp"
#include "sdl3/filesystem.hpp"
#include "sdl3/iostream.hpp"

#include "project.hpp"

namespace game_editor::files {

inline constexpr const char *PROJECT_FORMAT = "game_editor.project";
inline constexpr const char *SCENE_FORMAT = "game_editor.scene";
/// Format d'un objet réutilisable (`objects/<Nom>.object`).
inline constexpr const char *OBJECT_FORMAT = "game_editor.object";
inline constexpr int OBJECT_VERSION = 1;
/// Format des sous-arbres réutilisables (« Enregistrer la sélection comme
/// scène », cf. Runtime::SavePackedScene) — même extension `.scene`.
inline constexpr const char *PACKED_FORMAT = "scene.packed";
/// Version 5 : projet éclaté en fichiers (scènes `.scene`, scripts `.script`).
inline constexpr int PROJECT_VERSION = 5;
inline constexpr int SCENE_VERSION = 1;

inline constexpr const char *SCENES_DIR = "scenes";
inline constexpr const char *SCRIPTS_DIR = "scripts";
inline constexpr const char *OBJECTS_DIR = "objects";
inline constexpr const char *OBJECT_EXTENSION = ".object";
inline constexpr const char *ASSETS_DIR = "assets";
inline constexpr const char *GAMEPLAY_SUFFIX = ".gameplay.script";

// ============================================================================
// Chemins
// ============================================================================

[[nodiscard]] String Join(const String &directory, const String &name);

[[nodiscard]] String DirectoryOf(const String &path);

/// Chemin sans segments `.` ni `..` (`a/scenes/../scripts/x` → `a/scripts/x`) :
/// forme canonique pour comparer deux chemins du même fichier.
[[nodiscard]] String NormalizePath(const String &path);

[[nodiscard]] String BaseName(const String &path);

/// Nom de fichier sans son extension (`scripts/torchlight.script` → `torchlight`).
[[nodiscard]] String Stem(const String &path);

[[nodiscard]] bool IsDirectory(const String &path);

[[nodiscard]] bool IsFile(const String &path);

/// Nom utilisable comme nom de fichier ou de dossier : les séparateurs et
/// caractères réservés deviennent `_`, les accents sont gardés.
[[nodiscard]] String SafeFileName(const String &name);

/// Chemin sans `.` ni `..` ni séparateur doublé (`scenes/../scripts/x` →
/// `scripts/x`) : la forme sous laquelle on compare deux chemins.
[[nodiscard]] String NormalizePath(const String &path);

/// Chemin relatif de `path` sous `directory` (tel quel s'il n'y est pas).
[[nodiscard]] String RelativeTo(const String &directory, const String &path);

// ============================================================================
// Lecture / écriture brutes
// ============================================================================

[[nodiscard]] inline Result<String, String> ReadText(const String &path) { return data::script::LoadScriptFile(path); }

/// Écrit `text` dans `path`, en créant son dossier au besoin.
[[nodiscard]] Result<bool, String> WriteText(const String &path, const String &text);

[[nodiscard]] Result<data::NodePtr, String> ReadJson(const String &path);

[[nodiscard]] Result<bool, String> WriteJson(const String &path, const data::NodePtr &root);

/// Champ `format` d'un fichier JSON (vide s'il n'en a pas ou s'il est illisible).
[[nodiscard]] String FormatOf(const String &path);

// ============================================================================
// Scènes (.scene)
// ============================================================================

/**
 * JSON d'un fichier de scène. `gameplayFile` : chemin du script de jeu
 * RELATIF au fichier de scène ; vide = script écrit en ligne.
 */
[[nodiscard]] data::NodePtr SceneFileJson(const SceneDesc &scene, const String &gameplayFile);

/// Écrit une scène AUTONOME (script de jeu en ligne) dans `path`.
[[nodiscard]] Result<bool, String> SaveSceneFile(const SceneDesc &scene, const String &path);

/// Lit un fichier de scène ; un script de jeu référencé est lu à côté.
[[nodiscard]] Result<SceneDesc, String> LoadSceneFile(const String &path);

// ============================================================================
// Objets (.object)
// ============================================================================

/// JSON d'un fichier d'objet : nom, description, arbre (sans nœuds générés).
[[nodiscard]] data::NodePtr ObjectFileJson(const SceneDesc &object);

[[nodiscard]] Result<bool, String> SaveObjectFile(const SceneDesc &object, const String &path);

/// Lit un fichier `.object` : un document de genre OBJECT (instances non
/// développées — cf. objects::ExpandProject).
[[nodiscard]] Result<SceneDesc, String> LoadObjectFile(const String &path);

// ============================================================================
// Scripts (.script)
// ============================================================================

[[nodiscard]] Result<bool, String> SaveScriptFile(const String &source, const String &path);

/// Script de bibliothèque lu depuis un fichier : son nom est celui du fichier.
[[nodiscard]] Result<ScriptAsset, String> LoadScriptFile(const String &path);

// ============================================================================
// Projets
// ============================================================================

/// Un projet trouvé dans le dossier des projets.
struct ProjectEntry {
	String name;     ///< nom lu dans le manifeste
	String manifest; ///< chemin du `.json`
};

/// Manifeste d'un chemin donné par l'utilisateur : le fichier lui-même, ou
/// pour un DOSSIER de projet, son `<dossier>.json` (à défaut, le seul `.json`
/// qu'il contient). Vide si rien ne convient.
[[nodiscard]] String ResolveManifest(const String &path);

/// Projets présents dans `root` (un sous-dossier par projet), triés par nom.
[[nodiscard]] std::vector<ProjectEntry> ListProjects(const String &root);

/// Projet vierge : une scène vide, aucun script.
[[nodiscard]] Project MakeBlankProject(const String &name);

/**
 * Crée le dossier d'un nouveau projet sous `root` : `<Nom>/` avec `assets/`,
 * `scenes/` et `scripts/`. Rend le chemin du manifeste à écrire
 * (`<Nom>/<Nom>.json`). Refuse un dossier qui existe déjà.
 */
[[nodiscard]] Result<String, String> CreateProjectDirectory(const String &root, const String &name);

/// Résultat d'un chargement ou d'un enregistrement : les fichiers qui
/// constituent le projet (manifeste exclu). Le prochain enregistrement
/// supprime ceux qui n'en font plus partie (scène renommée ou supprimée).
using FileList = std::vector<String>;

/**
 * Lit un projet. `manifestPath` : le `.json` du projet (cf. ResolveManifest).
 * Les manifestes des versions ≤ 4 (tout en un) sont acceptés.
 */
[[nodiscard]] Result<Project, String> LoadProject(const String &manifestPath, FileList *files = nullptr);

/**
 * Enregistre `project` éclaté en fichiers autour de `manifestPath` (dossiers
 * créés au besoin). `previous` : les fichiers du dernier chargement ou
 * enregistrement — ceux qui ne sont pas réécrits cette fois (scène renommée
 * ou supprimée) sont effacés. Rend la nouvelle liste.
 */
[[nodiscard]] Result<FileList, String> SaveProject(const Project &project, const String &manifestPath,
														   const FileList &previous = {});

} // namespace game_editor::files
