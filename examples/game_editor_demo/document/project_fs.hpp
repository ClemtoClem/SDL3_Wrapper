/* @file document/project_fs.hpp */

#pragma once
/**
 * game_editor::ProjectFs — le projet vu comme un SYSTÈME DE FICHIERS.
 *
 * ── Le problème résolu ──────────────────────────────────────────────────
 * Un projet mêle CINQ natures d'éléments qui étaient toutes manipulées par
 * la même classe (`SceneDesc`, distinguée par un champ `kind`), la même
 * liste (`Project::scenes`) et des accesseurs ambigus (`FindScene` pouvait
 * rendre un objet ; `SceneNames` l'excluait, `scenes` non). Impossible de
 * savoir, à la lecture d'un nom, ce qu'il désignait — et chaque nouveau
 * venu réintroduisait la confusion :
 *
 *   - une SCÈNE  : document ÉDITABLE, unité finale du jeu (Vitrine).
 *   - un OBJET   : sous-arbre RÉUTILISABLE (Torche), instancié par référence.
 *   - un SCRIPT  : de bibliothèque (attaché aux nœuds) ou de jeu (lié à une scène).
 *   - une RESSOURCE : fichier du dossier `assets/` (modèle, texture, son…).
 *   - un NŒUD    : PIÈCE d'un document, identifiée DANS son document.
 *
 * ── La solution : adresser chaque élément par un CHEMIN ─────────────────
 *
 *     /                                racine du projet
 *     /scenes                          dossier des scènes
 *     /scenes/Vitrine                  une SCÈNE
 *     /objects/Torche                  un OBJET
 *     /scripts                         dossier des scripts
 *     /scripts/torchlight              un SCRIPT de bibliothèque
 *     /scripts/@Donjon                 le script de JEU de la scène Donjon
 *     /assets                          dossier des ressources du projet
 *     /assets/models/knight.gltf       une RESSOURCE
 *     /scenes/Vitrine/Lumières/Soleil  un NŒUD de cette scène
 *     /objects/Torche/Flammes          un NŒUD de cet objet
 *
 * et des TYPES FORTS qui rendent la confusion IMPOSSIBLE À COMPILER :
 *
 *     SceneRef         désigne une scène  (jamais un objet, jamais un script)
 *     ObjectRef        désigne un objet   (jamais une scène)
 *     ScriptFileRef    désigne un script  (bibliothèque OU jeu, cf. Path())
 *     AssetRef         désigne une ressource (chemin relatif à /assets)
 *     NodeRef          désigne un nœud, DANS un document identifié (scène
 *                      ou objet — jamais « quelque part », cf. `IsInScene`)
 *
 * ── Invariant : UN SEUL chemin d'accès ─────────────────────────────────
 * Tout code métier (Runtime, EditorUi, scripts, tests) passe par `ProjectFs`.
 * `Project::FindScene`, `Project::FindObject`, `Project::SceneNames`,
 * `Project::ObjectNames`… restent pour la compatibilité mais sont *dépréciés*
 * — le nouveau code ne les appelle plus. `Project::scenes` redevient un détail
 * d'implémentation.
 *
 * ── Migration ───────────────────────────────────────────────────────────
 *     project.Fs().ListScenes()                        // jamais d'objets
 *     project.Fs().ListObjects()                       // jamais de scènes
 *     project.Fs().Resolve("/scenes/X/Tore")           // un chemin typé
 *     project.Fs().FindNode(ProjectPath::Scene("X"), "Tore")  // un NodeRef
 *     project.Fs().AssetDiskPath(AssetRef{"models/k.gltf"})   // un chemin disque
 */
#include <vector>

#include "core/core.hpp"
#include "scene/scene.hpp"

#include "project.hpp"

namespace game_editor {

// ============================================================================
// ProjectPath — une adresse dans le projet
// ============================================================================

struct ProjectPath {
    enum class Kind : uint8_t {
        INVALID,
        ROOT,             ///< "/"
        SCENES_FOLDER,    ///< "/scenes"
        OBJECTS_FOLDER,   ///< "/objects"
        SCRIPTS_FOLDER,   ///< "/scripts"
        ASSETS_FOLDER,    ///< "/assets"
        SCENE,            ///< "/scenes/<Nom>"
        OBJECT,           ///< "/objects/<Nom>"
        SCRIPT,           ///< "/scripts/<Nom>"
        GAMEPLAY_SCRIPT,  ///< "/scripts/@<Scène>" — script de jeu d'une scène
        ASSET,            ///< "/assets/<chemin relatif>"
        SCENE_NODE,       ///< "/scenes/<Nom>/<chemin>"
        OBJECT_NODE,      ///< "/objects/<Nom>/<chemin>"
    };

    Kind kind = Kind::INVALID;
    /// Pour SCENE/OBJECT/SCRIPT/GAMEPLAY_SCRIPT : le NOM du document.
    /// Pour ASSET : le chemin relatif à `/assets` ("models/knight.gltf").
    String document;
    /// Pour *_NODE uniquement : le chemin du nœud dans son document.
    String nodePath;

    // ── Prédicats ────────────────────────────────────────────────────────
    [[nodiscard]] bool IsValid()     const noexcept { return kind != Kind::INVALID; }
    [[nodiscard]] bool IsFolder()    const noexcept;
    [[nodiscard]] bool IsDocument()  const noexcept;  ///< SCENE, OBJECT, SCRIPT, ASSET
    [[nodiscard]] bool IsScene()     const noexcept;  ///< SCENE ou SCENE_NODE
    [[nodiscard]] bool IsObject()    const noexcept;  ///< OBJECT ou OBJECT_NODE
    [[nodiscard]] bool IsScript()    const noexcept;  ///< SCRIPT ou GAMEPLAY_SCRIPT
    [[nodiscard]] bool IsGameplayScript() const noexcept;
    [[nodiscard]] bool IsAsset()     const noexcept;
    [[nodiscard]] bool IsNode()      const noexcept;  ///< SCENE_NODE ou OBJECT_NODE

    // ── Navigation ───────────────────────────────────────────────────────
    /// Le DOCUMENT qui porte ce chemin : la scène ou l'objet d'un nœud,
    /// soi-même pour un document (scène, objet, script, ressource), INVALID
    /// pour la racine et les dossiers.
    [[nodiscard]] ProjectPath Document() const;
    /// Le dossier parent.
    [[nodiscard]] ProjectPath Parent() const;

    /// Forme canonique ("/scenes/Vitrine/Lumières/Soleil").
    [[nodiscard]] String ToString() const;

    // ── Constructeurs directs ────────────────────────────────────────────
    [[nodiscard]] static ProjectPath Root();
    [[nodiscard]] static ProjectPath ScenesFolder();
    [[nodiscard]] static ProjectPath ObjectsFolder();
    [[nodiscard]] static ProjectPath ScriptsFolder();
    [[nodiscard]] static ProjectPath AssetsFolder();
    [[nodiscard]] static ProjectPath Scene(String name);
    [[nodiscard]] static ProjectPath Object(String name);
    [[nodiscard]] static ProjectPath Script(String name);
    [[nodiscard]] static ProjectPath GameplayScript(String scene);
    [[nodiscard]] static ProjectPath Asset(String relative);
    [[nodiscard]] static ProjectPath SceneNode(String scene, String node);
    [[nodiscard]] static ProjectPath ObjectNode(String object, String node);

    /// Analyse un chemin. Le "/" initial est facultatif.
    /// NONE si la chaîne n'est pas un chemin de projet valide.
    [[nodiscard]] static Option<ProjectPath> Parse(StringView text);

    [[nodiscard]] bool operator==(const ProjectPath& o) const noexcept;
    [[nodiscard]] bool operator!=(const ProjectPath& o) const noexcept { return !(*this == o); }
};

// ============================================================================
// Types forts — un par NATURE, impossible à confondre
// ============================================================================

/// Une SCÈNE du projet. Ne peut JAMAIS désigner un objet ni un script.
struct SceneRef {
    String name;
    [[nodiscard]] bool IsValid() const noexcept { return !name.IsEmpty(); }
    [[nodiscard]] ProjectPath Path() const { return ProjectPath::Scene(name); }
    [[nodiscard]] bool operator==(const SceneRef& o) const noexcept { return name == o.name; }
    [[nodiscard]] bool operator!=(const SceneRef& o) const noexcept { return !(*this == o); }
};

/// Un OBJET du projet. Ne peut JAMAIS désigner une scène.
struct ObjectRef {
    String name;
    [[nodiscard]] bool IsValid() const noexcept { return !name.IsEmpty(); }
    [[nodiscard]] ProjectPath Path() const { return ProjectPath::Object(name); }
    [[nodiscard]] bool operator==(const ObjectRef& o) const noexcept { return name == o.name; }
    [[nodiscard]] bool operator!=(const ObjectRef& o) const noexcept { return !(*this == o); }
};

/// Un SCRIPT du projet (de bibliothèque OU script de jeu).
/// Ne peut JAMAIS désigner une scène ni un objet.
/// Nom = nom de bibliothèque (`torchlight`) OU `@<Scène>` pour un script de jeu.
struct ScriptFileRef {
    String name;
    [[nodiscard]] bool IsValid() const noexcept { return !name.IsEmpty(); }
    [[nodiscard]] bool IsGameplay() const noexcept { return name.StartsWith("@"); }
    /// La scène dont ceci est le script de jeu (`@Donjon` → "Donjon"),
    /// vide pour un script de bibliothèque.
    [[nodiscard]] String HostScene() const {
        return IsGameplay() ? name.Substr(1) : String();
    }
    [[nodiscard]] ProjectPath Path() const {
        return IsGameplay() ? ProjectPath::GameplayScript(HostScene())
                            : ProjectPath::Script(name);
    }
    [[nodiscard]] bool operator==(const ScriptFileRef& o) const noexcept { return name == o.name; }
};

/// Une RESSOURCE du projet. `path` est relatif à `/assets`.
/// Un fichier ou un dossier — `IsFolder` n'est pas ici (il faut le disque).
struct AssetRef {
    String path;   ///< "models/knight.gltf", "textures/metal.png"…
    [[nodiscard]] bool IsValid() const noexcept { return !path.IsEmpty(); }
    [[nodiscard]] String Name() const;    ///< dernier segment
    [[nodiscard]] String ParentDir() const;  ///< dossier parent (relatif)
    [[nodiscard]] ProjectPath Path() const { return ProjectPath::Asset(path); }
    [[nodiscard]] bool operator==(const AssetRef& o) const noexcept { return path == o.path; }
};

/// Un NŒUD : un élément de l'arbre d'UN document identifié.
struct NodeRef {
    bool   inObject = false;  ///< false : scène ; true : objet
    String document;          ///< nom du document
    scene::NodeId id;         ///< identifiant DANS l'arbre de CE document
    String path;              ///< chemin relatif à la racine du document

    [[nodiscard]] bool IsValid()     const noexcept { return id.Valid() && !document.IsEmpty(); }
    [[nodiscard]] bool IsInScene()   const noexcept { return !inObject; }
    [[nodiscard]] bool IsInObject()  const noexcept { return inObject; }
    [[nodiscard]] ProjectPath FullPath() const {
        return inObject ? ProjectPath::ObjectNode(document, path)
                        : ProjectPath::SceneNode(document, path);
    }
    [[nodiscard]] SceneRef  AsScene()  const { return {document}; }
    [[nodiscard]] ObjectRef AsObject() const { return {document}; }
    [[nodiscard]] bool operator==(const NodeRef& o) const noexcept {
        return inObject == o.inObject && document == o.document && id == o.id;
    }
};

// ============================================================================
// ProjectFs — la façade « système de fichiers » d'un projet
// ============================================================================

/**
 * Toutes les opérations de LISTING, d'EXISTENCE et de RÉSOLUTION d'un projet,
 * sous une forme qui ne laisse aucune place à l'ambiguïté.
 *
 * Le projet STOCKE toujours ses documents dans une seule liste ; c'est un
 * détail d'implémentation invisible d'ici. Le code métier ne doit JAMAIS
 * toucher `Project::scenes` directement.
 *
 * `ProjectFs` est une VUE : elle ne possède rien, tient un pointeur non
 * propriétaire vers `Project` (et le cas échéant vers le dossier des
 * ressources), et se copie sans coût.
 */
class ProjectFs {
public:
    explicit ProjectFs(Project& p) noexcept;
    explicit ProjectFs(const Project& p) noexcept;

    /// Copie triviale — c'est une VUE, pas un propriétaire.
    ProjectFs(const ProjectFs&) = default;
    ProjectFs& operator=(const ProjectFs&) = default;

    // ── Configuration ───────────────────────────────────────────────────
    /// Dossier des ressources du projet (là où aboutit `/assets`). Vide :
    /// `/assets` n'existe pas et `AssetExists`/`AssetDiskPath` rendent vide.
    void SetAssetsRoot(String dir);
    [[nodiscard]] const String& AssetsRoot() const noexcept { return m_assetsRoot; }

    // ── Listing ─────────────────────────────────────────────────────────

    [[nodiscard]] std::vector<String> ListScenes() const;
    [[nodiscard]] std::vector<String> ListObjects() const;
    [[nodiscard]] std::vector<String> ListScripts() const;
    /// Noms des scènes dont le script de jeu n'est PAS vide.
    [[nodiscard]] std::vector<String> ListGameplayScripts() const;
    /// Enfants d'un dossier d'assets (vide : la racine).
    [[nodiscard]] std::vector<String> ListAssets(StringView subfolder = StringView()) const;

    /// Enfants d'un dossier ou d'un nœud :
    ///   ROOT          → ["scenes", "objects", "scripts", "assets"]
    ///   SCENES_FOLDER → noms de scènes
    ///   OBJECTS_FOLDER→ noms d'objets
    ///   SCRIPTS_FOLDER→ noms de scripts, puis "@<Scène>" pour les scripts de jeu
    ///   ASSETS_FOLDER → noms de fichiers/dossiers
    ///   SCENE/OBJECT  → noms des nœuds enfants de la racine
    ///   *_NODE        → noms des nœuds enfants du nœud désigné
    [[nodiscard]] std::vector<String> List(const ProjectPath& path) const;

    // ── Existence ───────────────────────────────────────────────────────

    [[nodiscard]] bool Exists(const ProjectPath& path) const;
    [[nodiscard]] bool SceneExists(StringView name) const;
    [[nodiscard]] bool ObjectExists(StringView name) const;
    [[nodiscard]] bool ScriptExists(StringView name) const;
    [[nodiscard]] bool GameplayScriptExists(StringView scene) const;
    /// Fichier OU sous-dossier sous la racine des ressources.
    [[nodiscard]] bool AssetExists(StringView relative) const;

    // ── Résolution ──────────────────────────────────────────────────────

    /// Résout un chemin OU un nom nu. Un nom nu (« Vitrine ») cherche dans
    /// cet ordre : SCÈNE, puis OBJET, puis SCRIPT — la scène gagne, parce
    /// que c'est l'unité finale et qu'un nom nu dans un script désigne le
    /// plus souvent elle. NONE si rien ne correspond.
    [[nodiscard]] Option<ProjectPath> Resolve(StringView text, bool requireExists = false) const;

    // ── Documents ───────────────────────────────────────────────────────

    [[nodiscard]] SceneDesc* FindScene(SceneRef ref) noexcept;
    [[nodiscard]] const SceneDesc* FindScene(SceneRef ref) const noexcept;
    [[nodiscard]] SceneDesc* FindObject(ObjectRef ref) noexcept;
    [[nodiscard]] const SceneDesc* FindObject(ObjectRef ref) const noexcept;

    /// ScriptAsset par son nom de bibliothèque. `@Scène` → nullptr (le
    /// script de jeu n'est pas un ScriptAsset : il vit dans la scène).
    [[nodiscard]] ScriptAsset* FindScript(ScriptFileRef ref) noexcept;
    [[nodiscard]] const ScriptAsset* FindScript(ScriptFileRef ref) const noexcept;
    /// Source du script de JEU d'une scène (vide si aucun).
    [[nodiscard]] String GameplayScriptSource(StringView scene) const;

    /// La scène ou l'objet désigné (ou qui porte le nœud désigné) ; nullptr
    /// pour tout le reste (script, ressource, dossier).
    [[nodiscard]] SceneDesc* FindDocument(const ProjectPath& path) noexcept;
    [[nodiscard]] const SceneDesc* FindDocument(const ProjectPath& path) const noexcept;

    // ── Assets ──────────────────────────────────────────────────────────

    /// `/assets/models/k.gltf` → `<assetsRoot>/models/k.gltf`.
    /// Vide si `assetsRoot` n'est pas défini.
    [[nodiscard]] String AssetDiskPath(StringView relative) const;
    [[nodiscard]] String AssetDiskPath(AssetRef ref) const { return AssetDiskPath(ref.path.View()); }

    /// L'inverse : chemin disque → chemin relatif à /assets.
    /// Vide si le fichier n'est pas sous la racine des assets.
    [[nodiscard]] String AssetDisplayPath(StringView diskPath) const;

    // ── Nœuds ───────────────────────────────────────────────────────────

    /// Résout un chemin de nœud DANS un document désigné. Le document lu
    /// détermine si on cherche une scène ou un objet — jamais de mélange.
    [[nodiscard]] NodeRef FindNode(const ProjectPath& documentPath, StringView nodePath) const;

    /// Chemin d'un nœud dans son document ("Lumières/Soleil").
    [[nodiscard]] String NodePathOf(const ProjectPath& documentPath, scene::NodeId id) const;

    /// Identité COMPLÈTE d'un nœud : document + ID + chemin.
    [[nodiscard]] NodeRef NodeRefOf(const ProjectPath& documentPath, scene::NodeId id) const;

    /// Raccourci de NodeRefOf pour un nœud d'une SCÈNE.
    [[nodiscard]] NodeRef NodeRefOf(SceneRef host, scene::NodeId id) const;

    // ── Créations ───────────────────────────────────────────────────────

    /// Crée une scène VIERGE, nom unique calculé (`base` ou `base 2`…).
    [[nodiscard]] SceneRef  CreateScene(String base);
    [[nodiscard]] ObjectRef CreateObject(String base);
    /// Ajoute un script à la bibliothèque ; nom unique calculé.
    [[nodiscard]] ScriptFileRef CreateScript(String base, String source,
                                             String description = String());

    /// Nom libre dans le dossier visé : `base`, sinon `base 2`, `base 3`…
    /// (scripts : `base_2`). Les scènes et les objets sont deux dossiers :
    /// un objet peut porter le nom d'une scène.
    [[nodiscard]] String UniqueSceneName(const String& base) const;
    [[nodiscard]] String UniqueObjectName(const String& base) const;
    [[nodiscard]] String UniqueScriptName(const String& base) const;

    // ── Vérifications ───────────────────────────────────────────────────

    /// Refuse (message lisible) un objet qui s'instancierait dans son propre
    /// contenu — la seule erreur structurelle que le moteur signale.
    [[nodiscard]] Option<String> WhySelfInstance(ObjectRef object, const SceneDesc& host) const;

    /// Refuse un nom vide, `.`/`..`, commençant par `.`, ou contenant un
    /// séparateur. NONE si le nom est utilisable.
    [[nodiscard]] static Option<String> WhyInvalidName(StringView name);

private:
    [[nodiscard]] SceneDesc* FindByName(const String& name, bool wantObject) noexcept;
    [[nodiscard]] const SceneDesc* FindByName(const String& name, bool wantObject) const noexcept;

    Project* m_project;
    String   m_assetsRoot;
};

// ============================================================================
// Aide : nature affichable d'un chemin (rapports, journaux, tests)
// ============================================================================

/// « Scène », « Objet », « Script », « Script de jeu », « Ressource », « Nœud »,
/// « Dossier », « Racine ».
[[nodiscard]] const char* ProjectPathKindName(ProjectPath::Kind kind) noexcept;

/// Le nom court du document (« Vitrine », « Torche », « torchlight »,
/// « models/knight.gltf ») — vide pour un dossier.
[[nodiscard]] String ProjectPathDocumentName(const ProjectPath& path);

} // namespace game_editor