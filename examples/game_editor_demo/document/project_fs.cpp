/* @file document/project_fs.cpp */

#include "project_fs.hpp"
#include "objects.hpp"
#include "project_files.hpp"

#include <algorithm>

namespace game_editor {

// ════════════════════════════════════════════════════════════════════════════
// ProjectPath
// ════════════════════════════════════════════════════════════════════════════

bool ProjectPath::IsFolder() const noexcept {
    switch (kind) {
        case Kind::ROOT:
        case Kind::SCENES_FOLDER:
        case Kind::OBJECTS_FOLDER:
        case Kind::SCRIPTS_FOLDER:
        case Kind::ASSETS_FOLDER:   return true;
        default:                    return false;
    }
}

bool ProjectPath::IsDocument() const noexcept {
    switch (kind) {
        case Kind::SCENE:
        case Kind::OBJECT:
        case Kind::SCRIPT:
        case Kind::GAMEPLAY_SCRIPT:
        case Kind::ASSET:           return true;
        default:                    return false;
    }
}

bool ProjectPath::IsScene() const noexcept {
    return kind == Kind::SCENE || kind == Kind::SCENE_NODE;
}

bool ProjectPath::IsObject() const noexcept {
    return kind == Kind::OBJECT || kind == Kind::OBJECT_NODE;
}

bool ProjectPath::IsScript() const noexcept {
    return kind == Kind::SCRIPT || kind == Kind::GAMEPLAY_SCRIPT;
}

bool ProjectPath::IsGameplayScript() const noexcept {
    return kind == Kind::GAMEPLAY_SCRIPT;
}

bool ProjectPath::IsAsset() const noexcept {
    return kind == Kind::ASSET;
}

bool ProjectPath::IsNode() const noexcept {
    return kind == Kind::SCENE_NODE || kind == Kind::OBJECT_NODE;
}

ProjectPath ProjectPath::Document() const {
    switch (kind) {
        case Kind::SCENE:
        case Kind::SCENE_NODE:      return Scene(document);
        case Kind::OBJECT:
        case Kind::OBJECT_NODE:     return Object(document);
        case Kind::SCRIPT:
        case Kind::GAMEPLAY_SCRIPT:
        case Kind::ASSET:           return *this;  // déjà un document
        default:                    return {Kind::INVALID, {}, {}};
    }
}

ProjectPath ProjectPath::Parent() const {
    switch (kind) {
        case Kind::ROOT:
            return {Kind::INVALID, {}, {}};
        case Kind::SCENES_FOLDER:
        case Kind::OBJECTS_FOLDER:
        case Kind::SCRIPTS_FOLDER:
        case Kind::ASSETS_FOLDER:
            return Root();
        case Kind::SCENE:           return ScenesFolder();
        case Kind::OBJECT:          return ObjectsFolder();
        case Kind::SCRIPT:
        case Kind::GAMEPLAY_SCRIPT: return ScriptsFolder();
        case Kind::ASSET: {
            // "/assets/models/k.gltf" → "/assets/models"
            const size_t slash = document.Rfind('/');
            if (slash == String::NPOS)
                return AssetsFolder();
            return Asset(document.Substr(0, slash));
        }
        case Kind::SCENE_NODE:
        case Kind::OBJECT_NODE: {
            const size_t slash = nodePath.Rfind('/');
            if (slash == String::NPOS)
                return Document();
            const String parent = nodePath.Substr(0, slash);
            return kind == Kind::SCENE_NODE ? SceneNode(document, parent)
                                            : ObjectNode(document, parent);
        }
        default:
            break;
    }
    return {Kind::INVALID, {}, {}};
}

String ProjectPath::ToString() const {
    switch (kind) {
        case Kind::ROOT:             return String("/");
        case Kind::SCENES_FOLDER:    return String("/scenes");
        case Kind::OBJECTS_FOLDER:   return String("/objects");
        case Kind::SCRIPTS_FOLDER:   return String("/scripts");
        case Kind::ASSETS_FOLDER:    return String("/assets");
        case Kind::SCENE:            return String("/scenes/")  + document;
        case Kind::OBJECT:           return String("/objects/") + document;
        case Kind::SCRIPT:           return String("/scripts/") + document;
        case Kind::GAMEPLAY_SCRIPT:  return String("/scripts/@") + document;
        case Kind::ASSET:            return String("/assets/")  + document;
        case Kind::SCENE_NODE:       return String("/scenes/")  + document + "/" + nodePath;
        case Kind::OBJECT_NODE:      return String("/objects/") + document + "/" + nodePath;
        case Kind::INVALID:          break;
    }
    return String("(chemin invalide)");
}

ProjectPath ProjectPath::Root()              { return {Kind::ROOT, {}, {}}; }
ProjectPath ProjectPath::ScenesFolder()      { return {Kind::SCENES_FOLDER, {}, {}}; }
ProjectPath ProjectPath::ObjectsFolder()     { return {Kind::OBJECTS_FOLDER, {}, {}}; }
ProjectPath ProjectPath::ScriptsFolder()     { return {Kind::SCRIPTS_FOLDER, {}, {}}; }
ProjectPath ProjectPath::AssetsFolder()      { return {Kind::ASSETS_FOLDER, {}, {}}; }
ProjectPath ProjectPath::Scene(String n)     { return {Kind::SCENE, std::move(n), {}}; }
ProjectPath ProjectPath::Object(String n)    { return {Kind::OBJECT, std::move(n), {}}; }
ProjectPath ProjectPath::Script(String n)    { return {Kind::SCRIPT, std::move(n), {}}; }
ProjectPath ProjectPath::GameplayScript(String s) { return {Kind::GAMEPLAY_SCRIPT, std::move(s), {}}; }
ProjectPath ProjectPath::Asset(String rel)   { return {Kind::ASSET, std::move(rel), {}}; }
ProjectPath ProjectPath::SceneNode(String s, String n) {
    return {Kind::SCENE_NODE, std::move(s), std::move(n)};
}
ProjectPath ProjectPath::ObjectNode(String o, String n) {
    return {Kind::OBJECT_NODE, std::move(o), std::move(n)};
}

bool ProjectPath::operator==(const ProjectPath& o) const noexcept {
    return kind == o.kind && document == o.document && nodePath == o.nodePath;
}

namespace {

/// Un nom valide : non vide, ne commence pas par `.`, sans séparateur.
bool ValidDocumentName(StringView name) {
    if (name.IsEmpty() || name.StartsWith("."))
        return false;
    for (size_t i = 0; i < name.GetSize(); ++i)
        if (name[i] == '/' || name[i] == '\\')
            return false;
    return true;
}

std::vector<String> SplitPath(StringView text) {
    const String raw(text);
    const String stripped = raw.StartsWith("/") ? raw.Substr(1) : raw;
    std::vector<String> parts;
    for (const String& seg : stripped.Split(StringView("/")))
        if (!seg.IsEmpty())
            parts.push_back(seg);
    return parts;
}

/// Nœud au chemin `path` RELATIF à la racine du document ("Lumières/Soleil") ;
/// vide = la racine. Un chemin reste un chemin : pas de repli sur une
/// recherche par nom ailleurs dans l'arbre (cf. SceneDesc::Resolve).
scene::NodeId NodeAt(const SceneDesc &doc, const String &path) {
	if (path.IsEmpty())
		return doc.tree.Root();
	const String relative = path.StartsWith("/") ? path.Substr(1) : path;
	return doc.tree.Resolve(relative, doc.tree.Root());
}

/// Chemin de `id` relatif à la racine du document ("Lumières/Soleil") ;
/// vide pour la racine (NodeTree::PathOf rend "/Racine/Lumières/Soleil").
String RelativePathOf(const SceneDesc &doc, scene::NodeId id) {
	if (!id.Valid() || !doc.tree.Contains(id) || id == doc.tree.Root())
		return String();
	const String full = doc.tree.PathOf(id);
	const size_t second = full.Find('/', 1);
	return second == String::NPOS ? String() : full.Substr(second + 1);
}

} // namespace

Option<ProjectPath> ProjectPath::Parse(StringView text) {
    const std::vector<String> parts = SplitPath(text);
    if (parts.empty())
        return Some(Root());

    const String& head = parts[0];

    // ── /scenes ─────────────────────────────────────────────────────────────
    if (head == "scenes") {
        if (parts.size() == 1)
            return Some(ScenesFolder());
        if (!ValidDocumentName(parts[1].View()))
            return NONE;
        if (parts.size() == 2)
            return Some(Scene(parts[1]));
        String node;
        for (size_t i = 2; i < parts.size(); ++i) {
            if (i > 2) node.PushBack('/');
            node.Concat(parts[i]);
        }
        return Some(SceneNode(parts[1], std::move(node)));
    }

    // ── /objects ────────────────────────────────────────────────────────────
    if (head == "objects") {
        if (parts.size() == 1)
            return Some(ObjectsFolder());
        if (!ValidDocumentName(parts[1].View()))
            return NONE;
        if (parts.size() == 2)
            return Some(Object(parts[1]));
        String node;
        for (size_t i = 2; i < parts.size(); ++i) {
            if (i > 2) node.PushBack('/');
            node.Concat(parts[i]);
        }
        return Some(ObjectNode(parts[1], std::move(node)));
    }

    // ── /scripts ────────────────────────────────────────────────────────────
    if (head == "scripts") {
        if (parts.size() == 1)
            return Some(ScriptsFolder());
        if (parts.size() != 2)
            return NONE;
        const String& name = parts[1];
        if (name.StartsWith("@")) {
            const String scene = name.Substr(1);
            if (!ValidDocumentName(scene.View()))
                return NONE;
            return Some(GameplayScript(scene));
        }
        if (!ValidDocumentName(name.View()))
            return NONE;
        return Some(Script(name));
    }

    // ── /assets ─────────────────────────────────────────────────────────────
    if (head == "assets") {
        if (parts.size() == 1)
            return Some(AssetsFolder());
        // Sous-chemin LIBRE : "models/knight/Knight.gltf".
        String rel;
        for (size_t i = 1; i < parts.size(); ++i) {
            if (i > 1) rel.PushBack('/');
            rel.Concat(parts[i]);
        }
        return Some(Asset(std::move(rel)));
    }

    return NONE;
}

// ════════════════════════════════════════════════════════════════════════════
// AssetRef
// ════════════════════════════════════════════════════════════════════════════

String AssetRef::Name() const {
    const size_t slash = path.Rfind('/');
    return slash == String::NPOS ? path : path.Substr(slash + 1);
}

String AssetRef::ParentDir() const {
    const size_t slash = path.Rfind('/');
    return slash == String::NPOS ? String() : path.Substr(0, slash);
}

// ════════════════════════════════════════════════════════════════════════════
// ProjectFs
// ════════════════════════════════════════════════════════════════════════════

ProjectFs::ProjectFs(Project& p) noexcept : m_project(&p) {}
ProjectFs::ProjectFs(const Project& p) noexcept : m_project(const_cast<Project*>(&p)) {}

void ProjectFs::SetAssetsRoot(String dir) {
    while (dir.EndsWith("/"))
        dir = dir.Substr(0, dir.GetSize() - 1);
    m_assetsRoot = std::move(dir);
}

// ── Listing ─────────────────────────────────────────────────────────────────

std::vector<String> ProjectFs::ListScenes() const {
    std::vector<String> out;
    for (const SceneDesc& d : m_project->scenes)
        if (!d.IsObject())
            out.push_back(d.name);
    return out;
}

std::vector<String> ProjectFs::ListObjects() const {
    std::vector<String> out;
    for (const SceneDesc& d : m_project->scenes)
        if (d.IsObject())
            out.push_back(d.name);
    return out;
}

std::vector<String> ProjectFs::ListScripts() const {
    std::vector<String> out;
    out.reserve(m_project->scripts.size());
    for (const ScriptAsset& s : m_project->scripts)
        out.push_back(s.name);
    return out;
}

std::vector<String> ProjectFs::ListGameplayScripts() const {
    std::vector<String> out;
    for (const SceneDesc& d : m_project->scenes)
        if (!d.IsObject() && !d.gameplayScript.Trim().IsEmpty())
            out.push_back(d.name);
    return out;
}

std::vector<String> ProjectFs::ListAssets(StringView subfolder) const {
    std::vector<String> out;
    if (m_assetsRoot.IsEmpty())
        return out;
    const String folder = subfolder.IsEmpty() ? m_assetsRoot
                                              : files::Join(m_assetsRoot, String(subfolder));
    if (!files::IsDirectory(folder))
        return out;
    (void)sdl3::filesystem::EnumerateDirectory(
        folder, [&out](const char*, const char* name) {
            out.emplace_back(name);
            return true;
        });
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<String> ProjectFs::List(const ProjectPath& path) const {
    switch (path.kind) {
        case ProjectPath::Kind::ROOT:
            return {String("scenes"), String("objects"),
                    String("scripts"), String("assets")};
        case ProjectPath::Kind::SCENES_FOLDER:
            return ListScenes();
        case ProjectPath::Kind::OBJECTS_FOLDER:
            return ListObjects();
        case ProjectPath::Kind::SCRIPTS_FOLDER: {
            std::vector<String> out = ListScripts();
            for (const String& s : ListGameplayScripts())
                out.push_back(String("@") + s);
            return out;
        }
        case ProjectPath::Kind::ASSETS_FOLDER:
            return ListAssets();
        case ProjectPath::Kind::ASSET:
            return ListAssets(path.document.View());
        case ProjectPath::Kind::SCENE:
        case ProjectPath::Kind::OBJECT: {
            const SceneDesc* d = FindDocument(path);
            std::vector<String> out;
            if (d)
                for (scene::NodeId child : d->tree.ChildrenOf(d->tree.Root()))
                    if (const scene::Node* n = d->tree.Get(child))
                        out.push_back(n->name);
            return out;
        }
        case ProjectPath::Kind::SCENE_NODE:
        case ProjectPath::Kind::OBJECT_NODE: {
            const SceneDesc* d = FindDocument(path.Document());
            const scene::NodeId node = d ? NodeAt(*d, path.nodePath) : scene::NodeId{};
            std::vector<String> out;
            if (d && node.Valid())
                for (scene::NodeId child : d->tree.ChildrenOf(node))
                    if (const scene::Node* n = d->tree.Get(child))
                        out.push_back(n->name);
            return out;
        }
        default:
            return {};
    }
}

// ── Existence ───────────────────────────────────────────────────────────────

bool ProjectFs::Exists(const ProjectPath& path) const {
    switch (path.kind) {
        case ProjectPath::Kind::ROOT:
        case ProjectPath::Kind::SCENES_FOLDER:
        case ProjectPath::Kind::OBJECTS_FOLDER:
        case ProjectPath::Kind::SCRIPTS_FOLDER:
            return true;
        case ProjectPath::Kind::ASSETS_FOLDER:
            return !m_assetsRoot.IsEmpty() && files::IsDirectory(m_assetsRoot);
        case ProjectPath::Kind::SCENE:           return SceneExists(path.document.View());
        case ProjectPath::Kind::OBJECT:          return ObjectExists(path.document.View());
        case ProjectPath::Kind::SCRIPT:          return ScriptExists(path.document.View());
        case ProjectPath::Kind::GAMEPLAY_SCRIPT: return GameplayScriptExists(path.document.View());
        case ProjectPath::Kind::ASSET:           return AssetExists(path.document.View());
        case ProjectPath::Kind::SCENE_NODE:
        case ProjectPath::Kind::OBJECT_NODE: {
            const SceneDesc* d = FindDocument(path.Document());
            return d && NodeAt(*d, path.nodePath).Valid();
        }
        case ProjectPath::Kind::INVALID:
            break;
    }
    return false;
}

bool ProjectFs::SceneExists(StringView name) const {
    for (const SceneDesc& d : m_project->scenes)
        if (!d.IsObject() && d.name == name)
            return true;
    return false;
}

bool ProjectFs::ObjectExists(StringView name) const {
    for (const SceneDesc& d : m_project->scenes)
        if (d.IsObject() && d.name == name)
            return true;
    return false;
}

bool ProjectFs::ScriptExists(StringView name) const {
    for (const ScriptAsset& s : m_project->scripts)
        if (s.name == name)
            return true;
    return false;
}

bool ProjectFs::GameplayScriptExists(StringView scene) const {
    for (const SceneDesc& d : m_project->scenes)
        if (!d.IsObject() && d.name == scene && !d.gameplayScript.Trim().IsEmpty())
            return true;
    return false;
}

bool ProjectFs::AssetExists(StringView relative) const {
    if (m_assetsRoot.IsEmpty())
        return false;
    const String disk = files::Join(m_assetsRoot, String(relative));
    return files::IsFile(disk) || files::IsDirectory(disk);  // fichier ou sous-dossier
}

// ── Résolution ──────────────────────────────────────────────────────────────

Option<ProjectPath> ProjectFs::Resolve(StringView text, bool requireExists) const {
    const String raw(text);
    // Nom nu : scène d'abord, puis objet, puis script.
    if (!raw.Contains('/')) {
        if (SceneExists(raw.View()))  return Some(ProjectPath::Scene(raw));
        if (ObjectExists(raw.View())) return Some(ProjectPath::Object(raw));
        if (ScriptExists(raw.View())) return Some(ProjectPath::Script(raw));
        return NONE;
    }
    auto parsed = ProjectPath::Parse(text);
    if (parsed.IsNone())
        return NONE;
    if (requireExists && !Exists(parsed.Unwrap()))
        return NONE;
    return parsed;
}

// ── Documents ───────────────────────────────────────────────────────────────

SceneDesc* ProjectFs::FindByName(const String& name, bool wantObject) noexcept {
    for (SceneDesc& d : m_project->scenes)
        if (d.IsObject() == wantObject && d.name == name)
            return &d;
    return nullptr;
}
const SceneDesc* ProjectFs::FindByName(const String& name, bool wantObject) const noexcept {
    return const_cast<ProjectFs*>(this)->FindByName(name, wantObject);
}

SceneDesc*       ProjectFs::FindScene(SceneRef r) noexcept       { return FindByName(r.name, false); }
const SceneDesc* ProjectFs::FindScene(SceneRef r) const noexcept { return FindByName(r.name, false); }
SceneDesc*       ProjectFs::FindObject(ObjectRef r) noexcept     { return FindByName(r.name, true); }
const SceneDesc* ProjectFs::FindObject(ObjectRef r) const noexcept{ return FindByName(r.name, true); }

ScriptAsset* ProjectFs::FindScript(ScriptFileRef ref) noexcept {
    if (!ref.IsValid() || ref.IsGameplay())
        return nullptr;
    return m_project->FindScript(ref.name);
}
const ScriptAsset* ProjectFs::FindScript(ScriptFileRef ref) const noexcept {
    return const_cast<ProjectFs*>(this)->FindScript(ref);
}

String ProjectFs::GameplayScriptSource(StringView scene) const {
    for (const SceneDesc& d : m_project->scenes)
        if (!d.IsObject() && d.name == scene)
            return d.gameplayScript;
    return String();
}

SceneDesc* ProjectFs::FindDocument(const ProjectPath& path) noexcept {
    switch (path.kind) {
        case ProjectPath::Kind::SCENE:
        case ProjectPath::Kind::SCENE_NODE:
            return FindByName(path.document, false);
        case ProjectPath::Kind::OBJECT:
        case ProjectPath::Kind::OBJECT_NODE:
            return FindByName(path.document, true);
        default:
            return nullptr;
    }
}
const SceneDesc* ProjectFs::FindDocument(const ProjectPath& path) const noexcept {
    return const_cast<ProjectFs*>(this)->FindDocument(path);
}

// ── Assets ──────────────────────────────────────────────────────────────────

String ProjectFs::AssetDiskPath(StringView relative) const {
    if (m_assetsRoot.IsEmpty() || relative.IsEmpty())
        return String();
    return files::Join(m_assetsRoot, String(relative));
}

String ProjectFs::AssetDisplayPath(StringView diskPath) const {
    if (m_assetsRoot.IsEmpty())
        return String();
    const String full(diskPath);
    const String prefix = m_assetsRoot.EndsWith("/") ? m_assetsRoot
                                                     : m_assetsRoot + String("/");
    return full.StartsWith(prefix.View()) ? full.Substr(prefix.GetSize()) : String();
}

// ── Nœuds ───────────────────────────────────────────────────────────────────

NodeRef ProjectFs::FindNode(const ProjectPath& documentPath, StringView nodePath) const {
    NodeRef ref;
    const ProjectPath doc = documentPath.Document();
    if (!doc.IsDocument() || doc.IsScript() || doc.IsAsset())
        return ref;
    ref.inObject = doc.IsObject();
    ref.document = doc.document;
    const SceneDesc* d = FindDocument(doc);
    if (!d)
        return ref;
    ref.id = NodeAt(*d, String(nodePath));
    if (ref.id.Valid())
        ref.path = RelativePathOf(*d, ref.id);
    return ref;
}

String ProjectFs::NodePathOf(const ProjectPath& documentPath, scene::NodeId id) const {
    const SceneDesc* d = FindDocument(documentPath.Document());
    return d ? RelativePathOf(*d, id) : String();
}

NodeRef ProjectFs::NodeRefOf(const ProjectPath& documentPath, scene::NodeId id) const {
    NodeRef ref;
    const ProjectPath doc = documentPath.Document();
    ref.inObject = doc.IsObject();
    ref.document = doc.document;
    ref.id = id;
    if (const SceneDesc* d = FindDocument(doc))
        ref.path = RelativePathOf(*d, id);
    return ref;
}

NodeRef ProjectFs::NodeRefOf(SceneRef host, scene::NodeId id) const {
    return NodeRefOf(ProjectPath::Scene(host.name), id);
}

// ── Créations ───────────────────────────────────────────────────────────────

String ProjectFs::UniqueSceneName(const String& base) const {
    const String wanted = base.IsEmpty() ? String("Scène") : base;
    if (!SceneExists(wanted.View())) return wanted;
    for (int i = 2; i < 100000; ++i) {
        const String candidate = String::Format("%s %d", wanted.CStr(), i);
        if (!SceneExists(candidate.View())) return candidate;
    }
    return wanted;
}

String ProjectFs::UniqueObjectName(const String& base) const {
    const String wanted = base.IsEmpty() ? String("Objet") : base;
    if (!ObjectExists(wanted.View())) return wanted;
    for (int i = 2; i < 100000; ++i) {
        const String candidate = String::Format("%s %d", wanted.CStr(), i);
        if (!ObjectExists(candidate.View())) return candidate;
    }
    return wanted;
}

String ProjectFs::UniqueScriptName(const String& base) const {
    const String wanted = base.IsEmpty() ? String("script") : base;
    if (!ScriptExists(wanted.View())) return wanted;
    for (int i = 2; i < 100000; ++i) {
        const String candidate = String::Format("%s_%d", wanted.CStr(), i);
        if (!ScriptExists(candidate.View())) return candidate;
    }
    return wanted;
}

SceneRef ProjectFs::CreateScene(String base) {
    SceneDesc scene;
    scene.SetName(UniqueSceneName(base));
    const String name = scene.name;
    m_project->scenes.push_back(std::move(scene));
    return {name};
}

ObjectRef ProjectFs::CreateObject(String base) {
    SceneDesc object;
    object.kind = SceneKind::OBJECT;
    object.SetName(UniqueObjectName(base));
    const String name = object.name;
    m_project->scenes.push_back(std::move(object));
    return {name};
}

ScriptFileRef ProjectFs::CreateScript(String base, String source, String description) {
    const String name = UniqueScriptName(base);
    m_project->scripts.push_back(ScriptAsset{name, std::move(description), std::move(source)});
    return {name};
}

// ── Vérifications ───────────────────────────────────────────────────────────

Option<String> ProjectFs::WhySelfInstance(ObjectRef object, const SceneDesc& host) const {
    const SceneDesc* src = FindObject(object);
    if (!src)
        return Some(String::Format("objet « %s » introuvable", object.name.CStr()));
    if (host.IsObject() && host.name == object.name)
        return Some(String::Format("« %s » ne peut pas s'instancier lui-même", object.name.CStr()));
    if (host.IsObject() && objects::DependsOn(*m_project, src->tree, host.name))
        return Some(String::Format("« %s » contient déjà « %s » : cycle d'objets",
                                   host.name.CStr(), object.name.CStr()));
    return NONE;
}

Option<String> ProjectFs::WhyInvalidName(StringView name) {
    const String clean(name);
    const String trimmed = clean.Trim();
    if (trimmed.IsEmpty())
        return Some(String("nom vide"));
    if (trimmed == "." || trimmed == "..")
        return Some(String("nom réservé"));
    if (trimmed.StartsWith("."))
        return Some(String("un nom ne commence pas par un point"));
    for (size_t i = 0; i < trimmed.GetSize(); ++i) {
        const unsigned char c = static_cast<unsigned char>(trimmed.CStr()[i]);
        if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|')
            return Some(String::Format("caractère interdit « %c »",
                                       c < 0x20 ? '?' : char(c)));
    }
    return NONE;
}

// ════════════════════════════════════════════════════════════════════════════
// Aides d'affichage
// ════════════════════════════════════════════════════════════════════════════

const char* ProjectPathKindName(ProjectPath::Kind kind) noexcept {
    using K = ProjectPath::Kind;
    switch (kind) {
        case K::ROOT:            return "Racine";
        case K::SCENES_FOLDER:   return "Dossier des scènes";
        case K::OBJECTS_FOLDER:  return "Dossier des objets";
        case K::SCRIPTS_FOLDER:  return "Dossier des scripts";
        case K::ASSETS_FOLDER:   return "Dossier des ressources";
        case K::SCENE:           return "Scène";
        case K::OBJECT:          return "Objet";
        case K::SCRIPT:          return "Script";
        case K::GAMEPLAY_SCRIPT: return "Script de jeu";
        case K::ASSET:           return "Ressource";
        case K::SCENE_NODE:      return "Nœud (scène)";
        case K::OBJECT_NODE:     return "Nœud (objet)";
        case K::INVALID:         break;
    }
    return "?";
}

String ProjectPathDocumentName(const ProjectPath& path) {
    if (path.IsDocument() || path.IsNode())
        return path.document;
    return String();
}

// ════════════════════════════════════════════════════════════════════════════
// Project::Fs — déclaré dans project.hpp, défini ici (type complet requis)
// ════════════════════════════════════════════════════════════════════════════

ProjectFs Project::Fs() noexcept { return ProjectFs(*this); }
ProjectFs Project::Fs() const noexcept { return ProjectFs(*this); }

} // namespace game_editor