#pragma once
/**
 * game_editor::Runtime — la partie « moteur » de l'éditeur : transforme le
 * DOCUMENT (project.hpp) en scène vivante (graphe `render3d::Object3D`,
 * entités ECS, corps `physics::RigidBody`), l'anime, la simule, et expose à
 * l'interface comme aux scripts un jeu de commandes qui maintiennent TOUJOURS
 * les deux en accord.
 *
 * ── Invariant central : le document est la source de vérité ──────────────
 * Toute mutation passe par une commande (`SetPosition`, `SetMaterialColor`,
 * `SpawnNamedNode`…) qui écrit d'ABORD dans le `NodeDesc` du document, PUIS
 * répercute sur le runtime. Rien ne modifie un `Object3D` ou un `RigidBody`
 * directement de l'extérieur. Conséquences : sauvegarder, c'est sérialiser le
 * document (rien à re-collecter depuis la 3D) ; et l'interface, les scripts
 * et le chargement de projet empruntent exactement le même chemin, donc se
 * comportent pareil.
 *
 * Les deux seules choses qui écrivent « à l'envers » sont la simulation
 * physique et l'animation pendant le mode Jeu ; elles sont recopiées dans le
 * document à chaque image (`MirrorSimulationToDocument`), et le mode Jeu
 * restaure de toute façon l'instantané pris au démarrage lorsqu'on l'arrête —
 * jouer ne modifie donc jamais le projet enregistré.
 *
 * ── Fonctionne sans GPU ──────────────────────────────────────────────────
 * `Runtime` ne tient qu'un `render3d::Canvas*` FACULTATIF. Sans lui, tout
 * marche à l'identique (scène, physique, scripts, rapport) sauf le rendu des
 * portails — c'est ce qui rend le mode `--headless` possible, et ce qui rend
 * cette classe testable sans fenêtre (tests/game_editor_smoke_test.cpp).
 *
 * Aucune exception (cf. memory/feedback_no_exceptions.md) : les commandes
 * rendent `bool`/`Option`/`Result`, jamais d'échec silencieux ni de `throw`.
 */
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/core.hpp"
#include "data/gltf.hpp"
#include "data/script.hpp"
#include "data/script/script_ecs.hpp"
#include "data/script/script_generator.hpp"
#include "data/script/script_ui.hpp"
#include "ecs/ecs.hpp"
#include "jobs/job_system.hpp"
#include "math/math.hpp"
#include "physics/world.hpp"
#include "render3d/animation.hpp"
#include "render3d/camera.hpp"
#include "render3d/canvas.hpp"
#include "render3d/ecs_bridge.hpp"
#include "render3d/mesh.hpp"
#include "render3d/object3d.hpp"
#include "render3d/picking.hpp"
#include "render3d/offscreen.hpp"
#include "render3d/portal.hpp"
#include "render3d/shape.hpp"
#include "sdl3/input.hpp"

#include "canvas2d.hpp"
#include "script_outline.hpp"
#include "script_owners.hpp"
#include "../document/project.hpp"
#include "../document/project_fs.hpp"
#include "../document/project_files.hpp"

namespace game_editor {

// ============================================================================
// Composants ECS propres à l'éditeur
// ============================================================================

/// Relie une entité ECS au NŒUD du document qu'elle incarne.
///
/// L'identifiant, et non le nom : depuis que la scène est un arbre
/// (`scene::NodeTree`), un nœud peut être renommé ou déplacé sans cesser
/// d'être le même — un `NodeId` traverse les deux, un nom non. Et il est
/// stable à la suppression d'un voisin, contrairement à un index.
struct SceneNodeRef {
	scene::NodeId node;
};

/// Marqueur de sélection (une seule à la fois, cf. `Select`).
struct Selected {};

/// Un script de la bibliothèque chargé pour la partie en cours : UN
/// interpréteur, partagé par tous les nœuds qui portent ce script. Le script
/// définit UNE classe dérivée de `Behaviour` ; chaque nœud en reçoit une
/// instance (cf. script_owners.hpp).
struct NodeScriptInstance {
	/// Une instance de la classe, attachée à un nœud.
	struct Attached {
		scene::NodeId node;
		data::script::Value object;
		bool detached = false; ///< nœud retiré pendant la partie : instance détruite
	};

	String script;
	std::unique_ptr<data::script::Interpreter> vm;
	std::shared_ptr<data::script::ClassObject> behaviour;
	std::vector<Attached> attached;
	bool ready = false;
	String error; ///< dernière erreur (chargement ou appel), vide si aucune

	/// Nombre de nœuds animés (instances non détachées).
	[[nodiscard]] size_t LiveCount() const noexcept;
};

/// Un objet de script vivant (instance d'une classe dérivée d'une base du
/// moteur), vu par l'interface : lu dans les registres C++, sans exécuter de
/// script.
struct ScriptObjectInfo {
	data::script::Interpreter *vm = nullptr; ///< interpréteur qui l'a créé
	data::script::Value object;
	String origin;	  ///< « scène » ou nom du script de la bibliothèque
	String className; ///< `Torche`
	std::vector<String> bases; ///< bases du moteur, ordre de construction (`Behaviour`, `Light3D`)
	scene::NodeId node;		   ///< nœud porté (invalide : aucun)
	bool attached = false;	   ///< Behaviour posée par l'éditeur (composant Script)
	bool destroyed = false;

	/// « Torche (Behaviour, Light3D) ».
	[[nodiscard]] String Signature() const;
	/// Champs de l'instance, valeurs affichables, dans l'ordre de déclaration.
	[[nodiscard]] std::vector<std::pair<String, String>> Fields() const;
};

/// État d'un script pour l'affichage (superposition du mode Jeu, rapport).
struct ScriptStatus {
	String name;
	String role;   ///< « scène » ou « nœud »
	size_t nodes = 0;
	bool running = false;
	String error;
};

// ============================================================================
// Journal
// ============================================================================

enum class LogLevel : uint8_t { INFO, SUCCESS, WARNING, ERROR_LEVEL, SCRIPT };

struct LogEntry {
	LogLevel level = LogLevel::INFO;
	long frame = 0;
	String text;

	[[nodiscard]] const char *LevelName() const noexcept;
};

// ============================================================================
// Runtime
// ============================================================================

class Runtime {
public:
	/// `jobWorkers < 0` : vivier automatique (cœurs - 1). `0` : tout sur le
	/// fil appelant.
	explicit Runtime(ecs::ArchetypeRegistry &registry, int jobWorkers = -1);

	Runtime(const Runtime &) = delete;
	Runtime &operator=(const Runtime &) = delete;

	/// Détruit les objets des scripts (séquence RAII complète) pendant que le
	/// runtime est encore entier : leurs bases retirent leurs nœuds.
	~Runtime();

	/// Objets des scripts dérivés d'une base du moteur, vivants dans TOUS les
	/// interpréteurs du runtime (scène, nœuds, outil) — lus dans les
	/// registres C++, sans exécuter de script (`editor.owners()`).
	[[nodiscard]] std::vector<data::script::OwnerInfo> LiveOwners() const;

	/// Détruit tous ces objets ; rend combien il y en avait.
	size_t DestroyAllScriptObjects();

	/// Les mêmes objets, un par instance (scène, Behaviour, Mesh3D…), dans
	/// l'ordre : scène, scripts de nœud, outil.
	[[nodiscard]] std::vector<ScriptObjectInfo> LiveScriptObjects();

	/// Ceux qui portent le nœud `id` (Behaviour attachées, Mesh3D créé…).
	[[nodiscard]] std::vector<ScriptObjectInfo> ScriptObjectsOf(scene::NodeId id);

	/// Séquence RAII complète d'un de ces objets ; faux s'il était déjà détruit.
	bool DestroyScriptObject(const ScriptObjectInfo &info);

	/// Ce qu'un script DÉFINIT (classes, bases du moteur, rôle, refus que le
	/// moteur opposerait au lancement) — analyse statique, sans exécution ;
	/// ses `import` sont résolus dans la bibliothèque du projet.
	[[nodiscard]] ScriptOutline OutlineScript(const String &source, ScriptUse use) const;

	/// Source d'un module importable (`import "nom"`) : script de la
	/// bibliothèque en mémoire, sinon `<projet>/scripts/nom.script`.
	[[nodiscard]] Option<String> ModuleSource(const String &specifier) const;

	// ── Branchements facultatifs ─────────────────────────────────────────────

	/// Fournit le device GPU (rendu des portails). Sans cet appel, le runtime
	/// marche à l'identique sans portails (cf. en-tête).
	void AttachCanvas(render3d::Canvas &canvas);

	/// Graine partagée par les deux interpréteurs : deux exécutions avec la
	/// même graine produisent exactement la même partie.
	void SetRandomSeed(uint64_t seed);

	// ── Accès ────────────────────────────────────────────────────────────────

	/// Catalogue des types de nœuds de l'éditeur. C'est LUI qui remplit le
	/// menu « Ajouter », l'icône de l'outliner et le libellé de l'inspecteur :
	/// ajouter un type d'objet à l'éditeur se fait donc ici, en une entrée,
	/// sans toucher à l'interface (cf. scene::NodeTypeRegistry).
	[[nodiscard]] scene::NodeTypeRegistry &TypeRegistry() noexcept { return m_types; }
	[[nodiscard]] const scene::NodeTypeRegistry &TypeRegistry() const noexcept { return m_types; }

	[[nodiscard]] String TypeLabel(const String &type) const { return m_types.LabelOf(type); }

	[[nodiscard]] Project &GetProject() noexcept { return m_project; }
	[[nodiscard]] const Project &GetProject() const noexcept { return m_project; }
	[[nodiscard]] render3d::Object3D &SceneRoot() noexcept { return m_sceneRoot; }
	[[nodiscard]] ecs::ArchetypeRegistry &Registry() noexcept { return m_registry; }
	[[nodiscard]] data::script::Interpreter &GameplayVm() noexcept { return *m_gameplayVm; }

	/// Où les scripts construisent leur interface : l'éditeur y branche son
	/// registre, sa fabrique et le calque posé sur la vue du jeu (cf.
	/// EditorUi). Sans cela, l'interface des scripts reste sans écran.
	[[nodiscard]] const std::shared_ptr<data::script::UiBinding> &GameUi() noexcept { return m_gameUi; }
	/// Appelé quand l'interface du jeu doit repartir de zéro : démarrage
	/// d'une scène jouée (avant son `on_start`) et fin de la partie.
	std::function<void()> onGameUiReset;
	/// Scène où la partie a commencé (vide hors partie).
	[[nodiscard]] const String &SessionOrigin() const noexcept { return m_sessionOrigin; }
	[[nodiscard]] data::script::Interpreter &ToolVm() noexcept { return m_toolVm; }

	/// Traite, pour chaque interpréteur, ce que ses fils `async` attendent du
	/// fil principal (appels à l'API, `print`, erreurs). Appelée par Update.
	void PumpScriptThreads();
	[[nodiscard]] const std::vector<LogEntry> &Log() const noexcept { return m_log; }
	[[nodiscard]] bool IsPlaying() const noexcept { return m_playing; }
	[[nodiscard]] long FrameIndex() const noexcept { return m_frameIndex; }
	[[nodiscard]] float PlayTime() const noexcept { return m_playTime; }

	[[nodiscard]] SceneDesc *ActiveScene() noexcept { return m_project.ActiveScene(); }
	[[nodiscard]] const SceneDesc *ActiveScene() const noexcept { return m_project.ActiveScene(); }

	[[nodiscard]] render3d::Camera &ActiveCamera() noexcept { return m_playing ? m_playCamera : m_editCamera; }
	[[nodiscard]] const render3d::Camera &ActiveCamera() const noexcept;

	/// Nœud sélectionné (invalide si rien n'est sélectionné).
	[[nodiscard]] scene::NodeId SelectedId() const noexcept { return m_selection; }

	/// Appelé par l'interface pour afficher le nom de l'objet sélectionné.
	[[nodiscard]] Option<String> SelectedName() const;

	[[nodiscard]] scene::Node *SelectedNode() noexcept { return FindNode(m_selection); }

	// ── Accès au document par identifiant ────────────────────────────────────
	// Public : l'interface (outliner, inspecteur), les natives de script et
	// les tests raisonnent tous en identifiants de nœuds.

	[[nodiscard]] scene::NodeTree *Tree() noexcept;
	[[nodiscard]] const scene::NodeTree *Tree() const noexcept;

	/// Identifiant du nœud portant ce nom dans la scène active.
	/// Nom ou chemin → identifiant. Les scripts désignent les objets par leur
	/// nom, plusieurs fois par image et par script : la recherche linéaire
	/// dans l'arbre est donc mise en cache, et le cache se VALIDE par le
	/// tampon de structure de l'arbre (cf. scene::NodeTree::StructureStamp)
	/// plus une vérification du nom — aucune commande n'a à penser à le vider.
	[[nodiscard]] scene::NodeId ResolveId(const String &objectName) const;

	[[nodiscard]] scene::Node *FindNode(const String &objectName) noexcept;

	[[nodiscard]] scene::Node *FindNode(scene::NodeId id) noexcept;

	/// Entité ECS incarnant ce nœud. Table de correspondance plutôt que
	/// balayage de toutes les entités : c'est l'opération la plus fréquente
	/// du runtime (chaque commande d'édition la fait), et un balayage la
	/// rendait linéaire en nombre d'objets.
	[[nodiscard]] Option<ecs::Entity> FindEntity(scene::NodeId id) const;

	[[nodiscard]] Option<ecs::Entity> FindEntity(const String &objectName) const;

	[[nodiscard]] render3d::Object3D *FindObject3D(scene::NodeId id);

	[[nodiscard]] render3d::Object3D *FindObject3D(const String &objectName) { return FindObject3D(ResolveId(objectName)); }

	// ── Journal ──────────────────────────────────────────────────────────────

	void Log(LogLevel level, String text);

	void ClearLog() { m_log.clear(); }

	void LogInfo(String text) { Log(LogLevel::INFO, std::move(text)); }
	void LogSuccess(String text) { Log(LogLevel::SUCCESS, std::move(text)); }
	void LogWarning(String text) { Log(LogLevel::WARNING, std::move(text)); }
	void LogError(String text) { Log(LogLevel::ERROR_LEVEL, std::move(text)); }

	/// Notifié à chaque nouvelle entrée (la console de l'interface s'y
	/// abonne pour se rafraîchir sans sonder le journal chaque image).
	std::function<void(const LogEntry &)> onLog;
	/// Notifié quand la composition de la scène change (ajout/suppression/
	/// renommage/changement de scène) — l'outliner s'y reconstruit.
	std::function<void()> onSceneStructureChanged;
	/// Le projet a changé depuis un script (ouvert, créé, fermé, scène
	/// ajoutée) : l'interface doit se reconstruire.
	std::function<void()> onProjectChanged;
	/// Notifié quand la sélection change.
	std::function<void()> onSelectionChanged;
	/// Notifié quand une PROPRIÉTÉ de l'objet sélectionné change hors mode
	/// Jeu (manipulateur, script d'outil, annulation) — l'inspecteur doit
	/// alors réafficher ses champs, sinon il montre encore l'ancienne valeur.
	/// Muet pendant le mode Jeu : la physique y écrit à chaque image, et
	/// reconstruire l'inspecteur 60 fois par seconde coûterait plus cher que
	/// tout le reste.
	std::function<void()> onNodeChanged;

	// ── Points d'accroche vers l'hôte graphique ─────────────────────────────
	// Installés par App/EditorUi quand il y a une fenêtre. Non installés en
	// mode --headless : les fonctions de script correspondantes rendent alors
	// `false` au lieu d'échouer, pour qu'UN MÊME scénario tourne avec ou sans
	// écran (c'est ce qui rend la démo vérifiable en intégration continue).

	/// Capture l'image courante dans un fichier. `false` si impossible.
	std::function<bool(const String &path)> onScreenshot;
	/// Change le thème de l'interface (dark | light | aero).
	std::function<bool(const String &theme)> onThemeChange;
	/// Met au premier plan un panneau/onglet (`"inspector"`, `"console"`…).
	std::function<bool(const String &panel, int tab)> onPanelFocus;
	/// Commande d'interface (`editor.ui("open_script", "torchlight")`…) — ce
	/// qu'un utilisateur ferait à la souris, rejouable par un scénario.
	std::function<bool(const String &command, const String &argument)> onUiCommand;
	/// Écrit un message dans la barre d'état.
	std::function<void(const String &text)> onStatusMessage;
	/// Images par seconde mesurées (le rapport et `editor.fps()` la lisent).
	std::function<double()> onQueryFps;

	// ── Projet / scènes ──────────────────────────────────────────────────────

	/// Remplace le projet courant et reconstruit la scène active. Le projet
	/// n'a pas (encore) de fichier : Enregistrer demandera où l'écrire.
	void OpenProject(Project project);

	/// Dossier où « Nouveau projet » crée les projets (un sous-dossier chacun).
	String projectsRoot = "saves/game_editor_demo/projects";

	/// Vrai dès qu'un projet est ouvert (créé, chargé, ou fourni en mémoire).
	[[nodiscard]] bool HasProject() const noexcept { return m_hasProject; }
	/// Manifeste `.json` du projet ouvert (vide : jamais enregistré).
	[[nodiscard]] const String &ProjectPath() const noexcept { return m_projectPath; }
	/// Fichiers que le projet a lus ou écrits : ceux qu'il réécrit ou efface
	/// au prochain enregistrement (cf. files::SaveProject).
	[[nodiscard]] const files::FileList &ProjectFiles() const noexcept { return m_projectFiles; }
	/// Dossier du projet ouvert (vide : jamais enregistré).
	[[nodiscard]] String ProjectDirectory() const;

	/// Ferme le projet : plus aucune scène (l'interface affiche alors
	/// « Aucun projet ouvert »).
	void CloseProject();

	/// Ouvre un projet : son manifeste `.json`, ou son dossier.
	[[nodiscard]] Result<bool, String> LoadProjectFile(const String &path);

	/// Enregistre le projet éclaté en fichiers autour de `path` (son
	/// manifeste), qui devient le fichier du projet.
	[[nodiscard]] Result<bool, String> SaveProjectFile(const String &path);

	/// Enregistre le projet à son emplacement actuel.
	[[nodiscard]] Result<bool, String> SaveProject();

	/**
	 * Nouveau projet VIERGE dans `root` : dossier `<Nom>/` avec `assets/`,
	 * `scenes/`, `scripts/` et le manifeste `<Nom>.json`, enregistré puis ouvert.
	 */
	[[nodiscard]] Result<bool, String> CreateProject(const String &root, const String &name);

	// ── Scènes et scripts en fichiers isolés ─────────────────────────────────

	/// « Enregistrer la scène sous… » : la scène active en UN fichier `.scene`
	/// (script de jeu compris).
	[[nodiscard]] Result<bool, String> SaveSceneAs(const String &path) const;

	/// Ajoute au projet la scène d'un fichier `.scene` (renommée si son nom
	/// est déjà pris) et l'active. Rend son nom.
	[[nodiscard]] Result<String, String> ImportSceneFile(const String &path);

	/// Ajoute une scène vide au projet et l'active. Rend son nom.
	String AddEmptyScene(const String &base = String("Nouvelle scène"));

	/// « Enregistrer sous… » d'un script de la bibliothèque (`name`) ou, avec
	/// le préfixe `@`, du script de jeu de la scène `@Scène`.
	[[nodiscard]] Result<bool, String> SaveScriptAs(const String &name, const String &path) const;

	/// Ajoute à la bibliothèque le script d'un fichier `.script` (son nom est
	/// celui du fichier ; remplace un script du même nom). Rend ce nom.
	[[nodiscard]] Result<String, String> ImportScriptFile(const String &path);
	/// Écrit l'objet `object` dans un fichier .object autonome.
	[[nodiscard]] Result<bool, String> SaveObjectAs(ObjectRef object, const String &path) const;
	/// Ajoute au projet l'objet d'un fichier .object (nom rendu unique dans
	/// /objects) et l'ouvre ; ses instances suivent dès lors ses modifications.
	[[nodiscard]] Result<ObjectRef, String> ImportObjectFile(const String &path);

	[[nodiscard]] String UniqueSceneName(const String &base) const;

	bool SwitchScene(const String &sceneName);

	/// Détruit puis reconstruit intégralement la scène vivante à partir du
	/// document. Volontairement brutal (tout jeter, tout refaire) plutôt
	/// qu'un diff incrémental : c'est appelé au chargement et au changement
	/// de scène, jamais dans la boucle de rendu, et ça supprime par
	/// construction toute possibilité de désynchronisation document/runtime.
	void RebuildRuntime();

	/// Répercute l'ambiance du document (gravité, soleil, lumière ambiante,
	/// couleur de fond) sur la simulation et sur le rendu. Appelée au
	/// chargement d'une scène ET à chaque édition des réglages du monde —
	/// c'est assez bon marché pour n'avoir jamais besoin d'être différé.
	void ApplyEnvironment();

	// ── Boucle ───────────────────────────────────────────────────────────────

	/// Une image. Ordre : (mode Jeu : scripts, physique, animation, recopie
	/// vers le document, zones) → caméra → synchronisation ECS→Object3D →
	/// lumières → portails.
	/// La synchronisation vient APRÈS toutes les écritures de l'image pour
	/// que le graphe rendu reflète l'état final, et les portails APRÈS elle
	/// car ils rendent la scène déjà à jour.
	void Update(float dt);

	/// Coût de la dernière image, en millisecondes : simulation (scripts +
	/// physique + animation + synchronisation ECS→Object3D) et rendu des
	/// portails, que le profileur et le rapport publient séparément.
	[[nodiscard]] double SimulationMs() const noexcept { return m_simulationMs; }
	[[nodiscard]] double PortalMs() const noexcept { return m_portalMs; }

	// ── Mode Jeu ─────────────────────────────────────────────────────────────

	/// Démarre le mode Jeu (une PARTIE) dans la scène active : instantané du
	/// document, corps physiques remis à leur état initial, script de la
	/// scène (re)chargé dans un interpréteur neuf et `on_start()` appelé.
	/// Rejouer donne donc exactement la même partie. Une partie peut changer
	/// de scène (`game.load_scene`) ; l'arrêt rétablit la scène de départ.
	void Play();

	/// Arrête la partie : la scène jouée retrouve son instantané, et la scène
	/// de départ redevient active — l'édition reprend exactement où elle en
	/// était.
	void Stop();

	/// Pendant une partie : quitte la scène jouée (rétablie) pour jouer
	/// `sceneName` — menu → chargement → niveau. Hors partie : SwitchScene.
	bool LoadSceneInPlay(const String &sceneName);

	/// Données de la partie, partagées par toutes ses scènes (`game.state()`).
	[[nodiscard]] data::script::Value GameState();

	void TogglePlay();

	// ── Sélection ────────────────────────────────────────────────────────────

	/// Sélection unique. Les entités portant déjà le marqueur sont COLLECTÉES
	/// d'abord puis modifiées ensuite : muter pendant un `Query` invaliderait
	/// l'itération (piège documenté en tête de ecs.hpp).
	/// Sélectionne par identifiant de nœud (forme de référence).
	bool Select(scene::NodeId id);

	/// Sélectionne par nom ou par chemin — c'est ce qu'emploient les scripts
	/// et l'interface. Une chaîne vide désélectionne.
	bool Select(const String &objectName);

	void ClearSelection() { (void)Select(scene::NodeId{}); }

	// ── Sélection par rayon et manipulateur ─────────────────────────────────
	//
	// Le viewport ne sait rien de la 3D : il donne un pixel, le runtime en
	// fait un rayon (`render3d::Camera::ScreenPointToRay`) et interroge la
	// scène (`render3d::PickMeshFace`, un triangle à la fois — précis jusque
	// sur un tore ou un modèle importé, là où une boîte englobante
	// sélectionnerait le vide autour).
	//
	// Le manipulateur est purement mathématique : `BeginGizmoDrag` /
	// `UpdateGizmoDrag` ne touchent QUE des rayons et le document, via les
	// mêmes commandes que l'inspecteur. Il est donc testable sans fenêtre, et
	// un glissé s'annule d'un seul coup (`BeginEdit`).

	enum class GizmoMode : uint8_t { TRANSLATE, ROTATE, SCALE };
	enum class GizmoAxis : uint8_t { NONE, X, Y, Z };

	struct PickHit {
		String name;           ///< nom du nœud touché (pour le journal et les scripts)
		scene::NodeId node;    ///< identité réelle du nœud touché
		float distance = 0.f;  ///< distance le long du rayon
		math::FVector3 point;  ///< point touché, en coordonnées monde
	};

	/// Rayon monde passant par le pixel (`x`, `y`) d'un viewport de
	/// `width` x `height` pixels, origine en HAUT à GAUCHE.
	[[nodiscard]] math::FRay ViewportRay(float x, float y, float width, float height) const;

	/// Objet visible le plus proche touché par `ray`. Les objets masqués sont
	/// ignorés (on ne sélectionne pas ce qu'on ne voit pas), le manipulateur
	/// aussi : ses nœuds n'appartiennent pas au document.
	[[nodiscard]] Option<PickHit> PickAt(const math::FRay &ray);

	/// Sélectionne l'objet sous le rayon ; un clic dans le vide désélectionne.
	/// Rend vrai si un objet a été touché.
	bool SelectAt(const math::FRay &ray);

	[[nodiscard]] GizmoMode GetGizmoMode() const noexcept { return m_gizmoMode; }
	void SetGizmoMode(GizmoMode mode);

	/// Pas de magnétisme : 0 = désactivé. Distances en unités monde, angles
	/// en degrés, échelle en facteur.
	[[nodiscard]] float TranslateSnap() const noexcept { return m_translateSnap; }
	[[nodiscard]] float RotateSnapDegrees() const noexcept { return m_rotateSnap; }
	[[nodiscard]] float ScaleSnap() const noexcept { return m_scaleSnap; }
	[[nodiscard]] bool SnapEnabled() const noexcept { return m_snapEnabled; }
	void SetSnapEnabled(bool enabled) noexcept { m_snapEnabled = enabled; }
	void SetSnapSteps(float translate, float rotateDegrees, float scale) noexcept;

	/// Taille du manipulateur en unités monde : proportionnelle à la distance
	/// à la caméra, pour qu'il garde la même taille à l'écran.
	[[nodiscard]] float GizmoSize() const;

	/// Position MONDE du nœud sélectionné — le manipulateur, le cadrage et
	/// le pivot d'orbite s'en servent tous (un nœud enfant n'est PAS à son
	/// transform local dans la scène).
	[[nodiscard]] Option<math::FVector3> SelectionWorldPosition() const;

	/// Axe du manipulateur sous le rayon, NONE si le rayon passe à côté.
	/// Test « distance du rayon au segment de l'axe » avec une tolérance
	/// proportionnelle à la taille affichée : ce qui est visuellement épais
	/// est attrapable.
	[[nodiscard]] GizmoAxis PickGizmoAxis(const math::FRay &ray) const;

	/// Commence un glissé sur `axis`. Mémorise la prise pour que l'objet
	/// suive le curseur sans saut initial.
	bool BeginGizmoDrag(GizmoAxis axis, const math::FRay &ray);

	/// Poursuit le glissé : calcule la nouvelle valeur depuis le rayon
	/// courant, applique le magnétisme, et passe par la commande d'édition
	/// ordinaire (donc document d'abord, scène ensuite).
	bool UpdateGizmoDrag(const math::FRay &ray);

	/// Rejoue un glissé complet sur `axis`, décrit en unités MONDE le long de
	/// cet axe (déplacement, échelle) ou en DEGRÉS autour de lui (rotation) —
	/// ce qu'un scénario ou un test peut écrire sans connaître la caméra. Le
	/// geste est découpé en petits pas, comme une vraie souris.
	bool ScriptedGizmoDrag(GizmoAxis axis, float from, float to);

	void EndGizmoDrag();

	[[nodiscard]] bool IsGizmoDragging() const noexcept { return m_dragging; }
	[[nodiscard]] GizmoAxis DraggedAxis() const noexcept { return m_dragAxis; }

	/// Axe survolé, mis en avant dans le rendu du manipulateur.
	void SetHoveredAxis(GizmoAxis axis);
	[[nodiscard]] GizmoAxis HoveredAxis() const noexcept { return m_hoverAxis; }

	/// Replace et redimensionne le manipulateur (appelé à chaque image par
	/// `Update`, et après tout changement de sélection ou de mode).
	void RefreshGizmo();

	// ── Historique (annuler / rétablir) ─────────────────────────────────────
	//
	// Instantané du DOCUMENT (objets de la scène + sélection), pas une liste
	// de commandes inverses : les commandes d'édition sont nombreuses et
	// certaines en cascade (supprimer un parent détache ses enfants,
	// renommer réécrit les liens), alors que la scène tient dans quelques
	// dizaines de kilo-octets. Une annulation est donc toujours exacte, et
	// une nouvelle commande n'a rien à déclarer pour être annulable.

	/// Une étape annulable : l'état du document AVANT la commande.
	struct HistoryStep {
		String label;           ///< « Déplacer Caisse », affiché dans le menu
		String sceneName;       ///< scène à laquelle l'instantané appartient
		/// L'ARBRE entier de la scène avant la commande. Copier un arbre est
		/// copier deux vecteurs (cf. scene::NodeTree) : c'est ce qui rend
		/// l'annulation d'une opération HIÉRARCHIQUE (reparentage,
		/// suppression d'un sous-arbre, duplication) aussi simple que celle
		/// d'un déplacement — l'état restauré est exact par construction.
		scene::NodeTree tree;
		scene::NodeId selection; ///< sélection avant la commande
	};

	/// Nombre d'étapes conservées : au-delà, la plus ancienne est oubliée.
	static constexpr size_t MAX_HISTORY = 64;

	[[nodiscard]] bool CanUndo() const noexcept { return !m_undo.empty(); }
	[[nodiscard]] bool CanRedo() const noexcept { return !m_redo.empty(); }
	/// Intitulé de la prochaine annulation (vide s'il n'y a rien à annuler).
	[[nodiscard]] String UndoLabel() const { return m_undo.empty() ? String() : m_undo.back().label; }
	[[nodiscard]] String RedoLabel() const { return m_redo.empty() ? String() : m_redo.back().label; }
	[[nodiscard]] size_t UndoDepth() const noexcept { return m_undo.size(); }

	/// Notifié quand la pile d'annulation change (les menus s'y grisent).
	std::function<void()> onHistoryChanged;

	/// Regroupe toutes les commandes jusqu'à `EndEdit` en UNE étape : un
	/// glissé de manipulateur émet une commande par image, qui doivent
	/// s'annuler d'un seul coup. Réentrant : seul le premier `BeginEdit`
	/// compte (un script appelé pendant un glissé ne coupe pas le groupe).
	void BeginEdit(String label);

	void EndEdit();

	/// Annule la dernière commande. Faux s'il n'y a rien à annuler.
	bool Undo();

	/// Rétablit la dernière commande annulée.
	bool Redo();

	void ClearHistory();

	// ── Commandes d'édition (document d'abord, runtime ensuite) ──────────────
	//
	// Chacune enregistre l'état précédent dans l'historique AVANT de muter,
	// sauf pendant le mode Jeu (la physique et les scripts de gameplay
	// écrivent dans le document à chaque image, et `Stop` restaure de toute
	// façon l'instantané de départ).

	// Chaque commande existe en deux formes : par IDENTIFIANT (la forme de
	// référence, employée par l'interface qui a déjà le nœud sous la main) et
	// par NOM ou CHEMIN (la forme employée par les scripts et les tests). La
	// seconde ne fait que résoudre puis appeler la première.

	bool SetPosition(scene::NodeId id, const math::FVector3 &position);
	bool SetPosition(const String &objectName, const math::FVector3 &position);

	/// Position MONDE : ce que manipulent le manipulateur 3D et les scripts
	/// qui raisonnent en coordonnées de scène. Le local est recalculé à
	/// partir du parent.
	bool SetGlobalPosition(scene::NodeId id, const math::FVector3 &position);

	bool SetEulerDegrees(scene::NodeId id, const math::FVector3 &eulerDeg);
	bool SetEulerDegrees(const String &objectName, const math::FVector3 &eulerDeg);

	bool SetScale(scene::NodeId id, const math::FVector3 &scale);
	bool SetScale(const String &objectName, const math::FVector3 &scale);

	bool SetVisible(scene::NodeId id, bool visible);
	bool SetVisible(const String &objectName, bool visible) { return SetVisible(ResolveId(objectName), visible); }

	bool SetMaterial(scene::NodeId id, const MaterialDesc &material);
	bool SetMaterial(const String &objectName, const MaterialDesc &material);

	bool SetMaterialColor(scene::NodeId id, const sdl3::Color &color);
	bool SetMaterialColor(const String &objectName, const sdl3::Color &color);

	/// Change la forme (et ses dimensions) d'un nœud — reconstruit son
	/// maillage. Un nœud sans apparence en reçoit une.
	/// Change la forme (et le matériau) d'un nœud. Le maillage est remplacé
	/// SUR PLACE quand le nœud est déjà une forme de même nature : un champ
	/// « dimensions » tiré à la souris appelle ceci à chaque mouvement, et
	/// reconstruire toute la scène à chaque fois (des centaines de nœuds
	/// dans le donjon) figeait l'éditeur. La reconstruction complète reste
	/// le chemin d'un nœud qui GAGNE une forme ou change de modèle importé.
	bool SetVisual(scene::NodeId id, const VisualDesc &visual);

	bool SetPhysics(scene::NodeId id, const PhysicsDesc &physics);
	bool SetPhysics(const String &objectName, const PhysicsDesc &physics);

	/// Étiquette libre lue par les scripts (« checkpoint », « portal_a »…).
	bool SetTagOf(scene::NodeId id, const String &tag);

	/// Verrouille un nœud : plus sélectionnable au clic ni déplaçable au
	/// manipulateur (le décor d'un niveau qu'on ne veut plus bouger).
	bool SetLocked(scene::NodeId id, bool locked);

	// ── Composants : lumière, caméra, déclencheur, script ───────────────────

	bool SetLight(scene::NodeId id, const LightDesc &light);

	bool SetCameraNode(scene::NodeId id, const CameraNodeDesc &camera);

	bool SetTrigger(scene::NodeId id, const TriggerDesc &trigger);

	/// Attache (ou, avec un nom vide, détache) un script de la bibliothèque.
	bool SetScriptRef(scene::NodeId id, const String &scriptName);

	/// Types de composants qu'on peut AJOUTER depuis l'inspecteur, dans
	/// l'ordre du menu.
	[[nodiscard]] static std::vector<String> AddableComponents();

	/// Ajoute un composant avec ses valeurs par défaut (sans effet s'il est
	/// déjà là). Rend faux si le nœud n'existe pas ou le type est inconnu.
	bool AddComponentOfType(scene::NodeId id, const String &type);

	bool RemoveComponentOfType(scene::NodeId id, const String &type);

	/// Remplace TOUTES les propriétés d'un composant — le chemin de l'édition
	/// « en JSON » d'un composant, et celui des propriétés libres du nœud
	/// (`type` vide).
	bool SetComponentProps(scene::NodeId id, const String &type, scene::PropertyMap props);

	// ── Bibliothèque de scripts ──────────────────────────────────────────────

	/// Ajoute un script (nom rendu unique) ; rend le nom retenu.
	String AddScript(const String &baseName, const String &source, const String &description = String());

	// ── Scènes et scripts du projet comme ÉLÉMENTS (navigateur de ressources) ─
	//
	// Le projet écrit lui-même leurs fichiers à l'enregistrement
	// (`scenes/<nom>.scene`, `scripts/<nom>.script`, `<scène>.gameplay.script`)
	// et supprime ceux qu'il ne possède plus : on agit donc sur le PROJET, pas
	// sur le disque. Refusé pendant une partie.

	/// Renomme une scène (nom rendu unique) ; rend le nom retenu.
	[[nodiscard]] Result<String, String> RenameScene(const String &sceneName, const String &wanted);

	/// Retire une scène du projet (pas la dernière ; la scène active cède la
	/// place à une autre).
	[[nodiscard]] Result<bool, String> RemoveScene(const String &sceneName);

	/// Copie d'une scène (et de son script de jeu) ; rend le nom de la copie.
	[[nodiscard]] Result<String, String> DuplicateScene(const String &sceneName);

	/// Renomme un script de la bibliothèque ; les nœuds qui le portent, dans
	/// TOUTES les scènes, suivent. Rend le nom retenu.
	[[nodiscard]] Result<String, String> RenameScript(const String &scriptName, const String &wanted);

	/// Retire un script de la bibliothèque ; rend le nombre de nœuds qui le
	/// portaient (leur composant Script reste, « introuvable »).
	[[nodiscard]] Result<size_t, String> RemoveScript(const String &scriptName);

	/// Copie d'un script de la bibliothèque ; rend le nom de la copie.
	[[nodiscard]] Result<String, String> DuplicateScript(const String &scriptName);

	/// Remplace la source d'un script de la bibliothèque. La compilation est
	/// vérifiée et l'erreur éventuelle RENDUE (le texte est enregistré quand
	/// même : on ne perd pas un travail en cours pour une faute de frappe).
	Option<data::script::ScriptError> SetScriptSource(const String &scriptName, const String &source);

	/// Remplace le script de JEU d'une scène (même contrat que ci-dessus).
	Option<data::script::ScriptError> SetGameplayScript(const String &sceneName, const String &source);

	/// Compile sans exécuter : la première erreur de syntaxe, ou NONE.
	[[nodiscard]] static Option<data::script::ScriptError> CheckScript(const String &source);

	/// Scripts de la partie en cours (ou, hors mode Jeu, ceux que la scène
	/// chargerait) — la superposition du mode Jeu et le rapport les listent.
	[[nodiscard]] std::vector<ScriptStatus> ScriptStatuses() const;

	// ── 2D (cf. canvas2d.hpp) ────────────────────────────────────────────────

	/// Ce que montre le calque 2D de la scène active : ses nœuds 2D et les
	/// dessins des scripts de l'image en cours (`draw2d.*`), dans l'ordre de
	/// dessin.
	[[nodiscard]] Canvas2DFrame Frame2D() const;

	/// Les nœuds 2D seuls (sans les dessins des scripts) ; vide sans scène.
	[[nodiscard]] Canvas2DFrame SceneFrame2D() const;

	/// Résolution de référence de la scène active.
	[[nodiscard]] math::FVector2 Reference2D() const;

	/// Vue 2D du JEU dans `viewport` (caméra 2D courante de `frame`).
	[[nodiscard]] View2D GameView2D(sdl3::FRect viewport, const Canvas2DFrame &frame) const;

	/// Rectangle (fenêtre) où le jeu est affiché — posé à chaque image par
	/// l'interface, lu par `canvas.mouse()`.
	void SetView2DRect(sdl3::FRect viewport) noexcept { m_view2dRect = viewport; }
	[[nodiscard]] sdl3::FRect View2DRect() const noexcept { return m_view2dRect; }

	/// Mesure des textes 2D (la vraie police quand l'interface en a une).
	void SetTextMeasure(TextMeasure measure) { m_textMeasure = std::move(measure); }
	[[nodiscard]] const TextMeasure &TextMeasurer() const noexcept { return m_textMeasure; }

	/// Dessins des scripts pour l'image en cours.
	[[nodiscard]] const std::vector<DrawItem2D> &ScriptDrawings2D() const noexcept { return m_draw2d; }

	/// Point de la fenêtre → repère 2D (`space`) du jeu ; NONE hors de la vue.
	[[nodiscard]] Option<math::FVector2> PointerTo2D(float x, float y, Space2D space) const;

	bool SetCanvasItem(scene::NodeId id, const CanvasItemDesc &item);

	bool SetCamera2D(scene::NodeId id, const Camera2DDesc &camera);

	/// Réglages 2D de la scène active (résolution, rendu 3D, fond).
	bool SetCanvas2D(const Canvas2DDesc &canvas);

	// ── Lumières (lecture, pour l'interface et les tests) ───────────────────

	[[nodiscard]] const std::vector<render3d::PointLight> &PointLights() const noexcept { return m_pointLights; }
	[[nodiscard]] const std::vector<render3d::SpotLight> &SpotLights() const noexcept { return m_spotLights; }
	[[nodiscard]] size_t TriggerCount() const noexcept { return m_triggerCount; }
	[[nodiscard]] scene::NodeId CurrentCamera() const noexcept { return m_currentCamera; }
	/// Le repère d'édition d'un nœud existe-t-il (et est-il visible) ?
	[[nodiscard]] bool HasVisibleHelper(scene::NodeId id) const;

	/// Mouvement relatif de la souris (mode Jeu en plein écran) : accumulé
	/// par l'hôte, lu par `input.mouse_delta()`, remis à zéro chaque image.
	void AddMouseDelta(float dx, float dy) noexcept;

	// ── Souris du jeu ────────────────────────────────────────────────────────

	/// Comment la partie tient la souris :
	///  - FREE : curseur visible et libre — l'interface du jeu se clique ;
	///  - CAPTURED : souris relative, curseur caché, seuls les déplacements
	///    comptent (`input.mouse_delta()`, vue subjective) ;
	///  - CONFINED : curseur visible mais retenu dans la fenêtre.
	enum class MouseMode : uint8_t { FREE, CAPTURED, CONFINED };

	[[nodiscard]] static const char *MouseModeName(MouseMode mode) noexcept;
	[[nodiscard]] static Option<MouseMode> MouseModeFromName(const String &name);

	/// Mode que l'hôte doit appliquer : libre hors partie ; sinon celui choisi
	/// par les scripts ou la touche de bascule ; à défaut, capturé en plein
	/// écran et libre dans la vue de l'éditeur.
	[[nodiscard]] MouseMode EffectiveMouseMode() const noexcept;

	/// Choix explicite du mode (scripts, touche de bascule) ; `on_mouse_mode`
	/// est appelé au début de l'image suivante s'il change.
	void SetMouseMode(MouseMode mode);

	/// Bascule capturé ↔ libre (confiné compte comme libre). Rend le nouveau mode.
	MouseMode ToggleMouseCapture();

	/// Touche qui bascule la capture pendant une partie (F7 par défaut,
	/// `input.set_mouse_toggle_key` la change ; NONE la désactive).
	[[nodiscard]] Option<SDL_Keycode> MouseToggleKey() const noexcept { return m_mouseToggleKey; }
	void SetMouseToggleKey(Option<SDL_Keycode> key) noexcept { m_mouseToggleKey = key; }

	/// L'hôte signale que le jeu occupe tout l'écran (mode Jeu plein écran).
	void SetHostFullscreen(bool fullscreen) noexcept { m_hostFullscreen = fullscreen; }

	// ── Commandes HIÉRARCHIQUES ──────────────────────────────────────────────

	/// Ajoute un objet (nom rendu unique) et l'instancie. Rend le nom
	/// effectivement attribué, ou NONE s'il n'y a pas de scène active.
	[[nodiscard]] Option<String> SpawnNamedNode(NodeDesc object);

	/// Même chose, mais rend l'IDENTIFIANT — ce dont l'interface a besoin
	/// pour enchaîner (sélectionner, reparenter, renommer).
	[[nodiscard]] Option<scene::NodeId> SpawnNode(NodeDesc object, scene::NodeId parent = scene::NodeId{});

	/// Crée un nœud VIDE (groupe) — la brique de la composition : on crée un
	/// « Wheels », puis on y glisse les roues.
	[[nodiscard]] Option<scene::NodeId> CreateGroup(const String &name, scene::NodeId parent = scene::NodeId{});

	/// Déplace un nœud (et son sous-arbre) sous un autre parent. Refusé si
	/// cela créerait un cycle — cf. scene::NodeTree::Reparent.
	bool ReparentNode(scene::NodeId child, scene::NodeId newParent,
					  scene::ReparentMode mode = scene::ReparentMode::KEEP_GLOBAL, size_t index = scene::NODE_APPEND);

	/// Réordonne un nœud parmi ses frères (glisser-déposer dans l'outliner).
	bool MoveNodeInParent(scene::NodeId child, size_t index);

	/// Duplique un nœud ET son sous-arbre (identifiants neufs, références
	/// internes re-câblées — cf. scene::NodeTree::Duplicate).
	[[nodiscard]] Option<scene::NodeId> DuplicateNode(scene::NodeId id);

	/// Supprime un nœud ET son sous-arbre.
	bool RemoveNode(scene::NodeId id);

	bool RemoveNode(const String &objectName) { return RemoveNode(ResolveId(objectName)); }

	bool RenameNode(scene::NodeId id, const String &newName);

	bool RenameNode(const String &objectName, const String &newName);

	// ── Objets réutilisables (.object, cf. document/objects.hpp) ─────────────
	//
	// Un objet est un document du projet (arbre de nœuds sans réglages du
	// monde) ; ses INSTANCES, dans les scènes et les autres objets, ne
	// retiennent que sa référence et leur transform. Leur contenu est
	// régénéré à chaque changement de document actif : modifier un objet
	// puis revenir à une scène montre la modification partout.

	/// Le document actif est un objet (et non une scène).
	[[nodiscard]] bool IsEditingObject() const;

	/// Re-développe toutes les instances d'objets du projet (objets d'abord,
	/// dans l'ordre des dépendances, puis scènes) ; journalise les erreurs
	/// (objet introuvable, cycle). Sans effet pendant une partie.
	void RefreshObjectInstances();

	/// Ajoute un objet vide au projet et l'ouvre. Rend son nom.
	String AddEmptyObject(const String &base = String("Nouvel objet"));

	/// « Créer un objet à partir de la sélection » : le sous-arbre `id` de la
	/// scène active devient le contenu d'un nouvel objet (`name`, nom du nœud
	/// à défaut), et le nœud est remplacé par une INSTANCE de cet objet au
	/// même endroit. Rend le nom de l'objet.
	[[nodiscard]] Result<String, String> CreateObjectFromNode(scene::NodeId id, const String &name = String());

	/// Pose une instance de l'objet `objectName` dans le document actif, sous
	/// `parent` (la racine à défaut), au transform `placement` (identité à
	/// défaut). En partie, seul le sous-arbre nouveau est construit et ses
	/// scripts démarrent (comme InstantiateSceneFile).
	[[nodiscard]] Result<scene::NodeId, String> InstantiateObject(const String &objectName,
																  scene::NodeId parent = scene::NodeId{},
																  Option<scene::Transform> placement = NONE);

	/// « Rendre indépendant » : le contenu de l'instance `id` devient des
	/// nœuds ordinaires de la scène, qui ne suivront plus l'objet.
	[[nodiscard]] Result<bool, String> DetachInstance(scene::NodeId id);

	/// Le contenu d'un objet devient celui du sous-arbre `id` du document
	/// actif (copie ; l'objet est créé s'il n'existe pas). Pour les scripts :
	/// définir un objet à partir de nœuds construits en jeu.
	[[nodiscard]] Result<String, String> DefineObjectFromNode(const String &objectName, scene::NodeId id);

	// ── Scènes réutilisables (PackedScene) ───────────────────────────────────
	//
	// Emballer un sous-arbre dans un fichier, puis le réinstancier autant de
	// fois qu'on veut : c'est ce qui transforme « une voiture » en « le
	// modèle de voiture » dont un circuit pose trois exemplaires. Le format
	// est celui de la bibliothèque (scene::PackedScene), pas un format
	// propre à l'éditeur.

	/// Écrit le sous-arbre `id` dans un fichier `.scene`.
	[[nodiscard]] Result<bool, String> SavePackedScene(scene::NodeId id, const String &path) const;

	/// Instancie un fichier `.scene` sous `parent` (la racine à défaut).
	/// Chaque instance est une COPIE indépendante, avec des identifiants
	/// neufs et ses références internes re-câblées ; sa racine retient d'où
	/// elle vient (cf. scene::PackedScene).
	///
	/// `placement` (facultatif) remplace le transform de la racine AVANT la
	/// construction des objets : les corps physiques naissent au bon endroit
	/// — c'est ainsi qu'un script assemble un niveau pièce par pièce. Seul le
	/// sous-arbre nouveau est construit (pas toute la scène), et, en mode Jeu,
	/// ses scripts de nœud démarrent et ses zones de déclenchement s'activent.
	/// Un chemin relatif est cherché d'abord dans le dossier du projet.
	[[nodiscard]] Result<scene::NodeId, String> InstantiateSceneFile(const String &path,
																	 scene::NodeId parent = scene::NodeId{},
																	 Option<scene::Transform> placement = NONE);

	/// Chemin d'un fichier du projet : relatif au dossier du projet s'il y
	/// existe, tel quel sinon.
	[[nodiscard]] String ResolveProjectPath(const String &path) const;

	/// Ce qu'un script de nœud reçoit en `self` : le nom du nœud s'il est
	/// unique dans la scène, son CHEMIN sinon (une pièce instanciée plusieurs
	/// fois répète ses noms : « Torche » désignerait toujours la première).
	[[nodiscard]] String ScriptHandle(scene::NodeId id) const;

	// ── Physique pilotée par script ──────────────────────────────────────────

	[[nodiscard]] Option<math::FVector3> GetVelocity(const String &objectName);

	[[nodiscard]] Option<math::FVector3> GetVelocity(scene::NodeId id);

	bool SetVelocity(const String &objectName, const math::FVector3 &velocity);

	bool SetVelocity(scene::NodeId id, const math::FVector3 &velocity);

	bool ApplyImpulse(const String &objectName, const math::FVector3 &impulse);

	bool ApplyImpulse(scene::NodeId id, const math::FVector3 &impulse);

	// ── Caméra ───────────────────────────────────────────────────────────────

	void ApplyLookDelta(float deltaYaw, float deltaPitch);

	/// Déplacement de la caméra libre, en unités locales : `z` vers l'avant,
	/// `x` vers la droite, `y` vers le HAUT DU MONDE (vol d'éditeur : monter
	/// reste monter, même en regardant vers le sol).
	void MoveCamera(const math::FVector3 &localDelta);

	/// Panoramique : glisse la caméra DANS SON PLAN D'ÉCRAN (droite et haut
	/// de la caméra, pas du monde) — le geste du clic milieu de tout éditeur
	/// 3D. `right`/`up` sont en unités monde ; les multiplier par
	/// `PivotDistance()` donne un geste qui « colle » au décor quelle que
	/// soit l'échelle de la scène.
	void PanCamera(float right, float up);

	/// Avance ou recule le long de l'axe de visée (molette). Un pas positif
	/// rapproche du décor.
	void DollyCamera(float distance);

	/// Tourne AUTOUR d'un point : la distance au pivot est conservée et la
	/// caméra continue de le regarder. C'est ce qui permet d'examiner un
	/// objet sous toutes ses faces sans le perdre de vue.
	void OrbitCamera(float deltaYaw, float deltaPitch, const math::FVector3 &pivot);

	/// Point autour duquel tourner et échelle des gestes : l'objet
	/// sélectionné s'il y en a un, sinon un point droit devant la caméra.
	[[nodiscard]] math::FVector3 CameraPivot() const;

	/// Distance au pivot : sert d'échelle aux gestes (panoramique et molette
	/// avancent d'autant plus vite que la scène est loin).
	[[nodiscard]] float PivotDistance() const;

	/// Vecteurs de base de la caméra courante.
	[[nodiscard]] math::FVector3 CameraForward() const;
	[[nodiscard]] math::FVector3 CameraRight() const;
	[[nodiscard]] math::FVector3 CameraUp() const { return CameraRight().Cross(CameraForward()).Normalize(); }

	void SetCameraPosition(const math::FVector3 &position);

	/// Place la caméra d'édition de façon à cadrer `objectName` (« frame
	/// selected », raccourci F dans tous les éditeurs 3D).
	bool FocusOn(scene::NodeId id);

	bool FocusOn(const String &objectName) { return FocusOn(ResolveId(objectName)); }

	void SetViewportAspect(float aspect);

	// ── Scripts ──────────────────────────────────────────────────────────────

	/// Exécute une source dans l'interpréteur « outil » (scénarios, console
	/// de l'éditeur) — celui qui pilote l'ÉDITEUR, par opposition à
	/// l'interpréteur de gameplay qui pilote la PARTIE.
	[[nodiscard]] Result<data::script::Value, data::script::ScriptError> RunToolScript(const String &source);

	/// Appelle un rappel du script outil s'il existe (`on_frame`…).
	void CallToolHook(const String &hook, std::vector<data::script::Value> args);

	[[nodiscard]] size_t ScriptRunCount() const noexcept { return m_scriptRunCount; }
	[[nodiscard]] size_t ScriptCallCount() const noexcept { return m_scriptCallCount; }
	[[nodiscard]] size_t ScriptErrorCount() const noexcept { return m_scriptErrorCount; }
	[[nodiscard]] const std::vector<String> &LoadedScripts() const noexcept { return m_loadedScripts; }

	/// Valeur d'un champ de l'objet `Scene` de la partie (ou, à défaut, d'une
	/// globale du script de scène) — le rapport y lit les compteurs de la
	/// partie (tours bouclés, chronos…).
	[[nodiscard]] Option<data::script::Value> GameplayGlobal(const String &name);

	/// L'objet `Scene` de la partie en cours (nil hors partie, ou si le script
	/// de scène n'en définit pas).
	[[nodiscard]] const data::script::Value &SceneObject() const noexcept { return m_sceneObject; }

	/// Changement de scène demandé par un script : appliqué au début de
	/// l'image suivante en partie, tout de suite sinon. Faux si la scène
	/// n'existe pas.
	bool RequestScene(const String &sceneName);

	/// Fin de partie demandée par un script (au début de l'image suivante).
	void RequestQuit();

	/// Nœud auquel s'attache la `Behaviour` en cours de construction par le
	/// moteur (invalide hors de cette construction).
	[[nodiscard]] scene::NodeId BehaviourBindingNode() const noexcept { return m_bindingNode; }

	// ── Métriques ────────────────────────────────────────────────────────────

	/// Vivier de tâches partagé par la simulation (et disponible pour tout
	/// travail parallèle de l'éditeur).
	[[nodiscard]] jobs::JobSystem &Jobs() noexcept { return m_jobs; }
	[[nodiscard]] const jobs::JobSystem &Jobs() const noexcept { return m_jobs; }

	[[nodiscard]] size_t RuntimeNodeCount() const { return m_registry.EntitiesWith<SceneNodeRef>().size(); }
	[[nodiscard]] size_t RigidBodyCount() const { return m_registry.EntitiesWith<physics::RigidBody>().size(); }

private:
	static constexpr size_t MAX_LOG_ENTRIES = 500;
	static constexpr float MIN_SCALE = 0.001f;
	/// Distance du pivot d'orbite quand rien n'est sélectionné : assez loin
	/// pour tourner autour de « ce qu'on regarde », pas de son nez.
	static constexpr float DEFAULT_PIVOT_DISTANCE = 12.f;

	/// Types de nœuds que l'éditeur connaît. La bibliothèque n'en impose
	/// aucun (cf. scene::NodeTypeRegistry) : ceux-ci sont propres à CET
	/// éditeur, et un greffon pourrait en déclarer d'autres au démarrage.
	[[nodiscard]] static scene::NodeTypeRegistry MakeTypeRegistry();

	// ── Manipulateur : mathématiques et nœuds ────────────────────────────────

	/// Accès CONST au nœud sélectionné — par identifiant, directement (passer
	/// par le nom ferait un aller-retour inutile… et une récursion infinie
	/// avec SelectedName, qui s'appuie sur cette fonction).
	[[nodiscard]] const scene::Node *SelectedConstNode() const;

	[[nodiscard]] static math::FVector3 AxisVector(GizmoAxis axis) noexcept;

	[[nodiscard]] String GizmoLabel(GizmoAxis axis) const;

	/// Paramètre, le long de la droite (`origin`, `axis`), du point le plus
	/// proche de `ray` — la prise du manipulateur. NONE quand le rayon est
	/// parallèle à l'axe (le geste n'a alors aucun sens).
	[[nodiscard]] static Option<float> ClosestParamOnAxis(const math::FRay &ray, const math::FVector3 &origin,
														  const math::FVector3 &axis) noexcept;

	/// Intersection du rayon avec le plan (`point`, `normal`).
	[[nodiscard]] static Option<math::FVector3> IntersectPlane(const math::FRay &ray, const math::FVector3 &point,
															   const math::FVector3 &normal) noexcept;

	[[nodiscard]] static float SnapTo(float value, float step) noexcept;

	/// Construit les nœuds du manipulateur (une fois) : trois flèches, trois
	/// anneaux, trois poignées d'échelle. Ils vivent DANS le graphe de scène
	/// mais PAS dans le document : ils ne sont ni enregistrés, ni listés dans
	/// l'outliner, ni sélectionnables.
	void EnsureGizmoNodes();

	// ── Historique ───────────────────────────────────────────────────────────

	/// Prévient l'interface qu'une propriété vient de changer (cf.
	/// `onNodeChanged`).
	void NotifyNodeChanged();

	[[nodiscard]] HistoryStep Snapshot(String label) const;

	/// Empile l'état courant avant une mutation. Sans effet pendant le mode
	/// Jeu (les scripts de gameplay et la physique écrivent dans le document
	/// à chaque image), pendant une restauration, et à l'intérieur d'un
	/// groupe ouvert par `BeginEdit` (seule la première commande compte).
	void RecordHistory(String label);

	/// Retire le dernier instantané empilé : utilisé quand la commande qui
	/// venait de l'empiler ÉCHOUE (reparentage refusé, duplication
	/// impossible) — laisser une étape d'annulation qui ne défait rien
	/// désoriente plus qu'elle n'aide.
	void DropLastHistory();

	/// Remet le document dans l'état de `step` et reconstruit la scène.
	bool RestoreStep(const HistoryStep &step);

	// ── Recherche ────────────────────────────────────────────────────────────

	// ── Construction ─────────────────────────────────────────────────────────

	[[nodiscard]] static render3d::Mesh BuildMesh(const VisualDesc &object);

	/// Maillage d'un objet, y compris les modèles importés. Un fichier
	/// illisible donne un cube témoin ET une ligne dans la console : un
	/// objet silencieusement invisible serait bien plus déroutant qu'un
	/// cube manifestement faux.
	[[nodiscard]] render3d::Mesh MakeMeshFor(const VisualDesc &object);

public:
	/// Convertit la PREMIÈRE primitive du premier maillage d'un `.gltf` en
	/// `render3d::Mesh`.
	///
	/// Une seule primitive : `render3d::Shape` porte un maillage et une liste
	/// de matériaux par GROUPE, alors qu'un glTF associe un matériau par
	/// primitive avec des tampons d'indices séparés — fusionner correctement
	/// les deux demanderait de réindexer tous les attributs, ce qui dépasse
	/// ce que cette démo a besoin de montrer. Limite explicite, signalée dans
	/// la console lors de l'import.
	[[nodiscard]] static Result<render3d::Mesh, String> LoadGltfMesh(const String &path);

	/// Importe un `.gltf` comme nouvel objet de la scène active. L'échelle est
	/// ajustée pour que le modèle tienne dans `targetSize` unités : les
	/// modèles exportés vont du centimètre au kilomètre, et arriver à une
	/// échelle exploitable est ce qui distingue un import utilisable d'une
	/// tache invisible ou d'un mur.
	[[nodiscard]] Result<String, String> ImportGltf(const String &path, float targetSize = 3.f);

	/// Nom de fichier sans dossier ni extension — sert de nom d'objet par
	/// défaut à l'import.
	[[nodiscard]] static String FileStem(const String &path);

private:

	/// Quad plat dans le plan XY local, normale +Z — la convention EXIGÉE par
	/// `render3d::Portal::WorldPlaneNormal()` (et donc PAS celle de
	/// `Mesh::Plane()`, qui produit un sol de normale +Y).
	[[nodiscard]] static render3d::Mesh PortalQuad(float halfWidth, float halfHeight);

	[[nodiscard]] static render3d::Material BuildMaterial(const MaterialDesc &desc);

	[[nodiscard]] static physics::Shape BuildCollider(const PhysicsDesc &physics);

	/**
	 * Fabrique l'entité ECS et le nœud de rendu d'UN nœud du document.
	 *
	 * Un nœud SANS composant d'apparence (un groupe, un pivot) reçoit quand
	 * même un `render3d::Object3D` nu : il faut un point d'accroche pour ses
	 * enfants, et son transform doit continuer de se propager. C'est ce qui
	 * permet de construire « Car > Wheels > FrontLeft » où seul le dernier
	 * niveau a une géométrie.
	 */
	ecs::Entity InstantiateNode(scene::NodeId id, const scene::Node &node);

	// ── Repères d'édition (lumière, caméra, déclencheur) ────────────────────
	//
	// Un nœud sans forme resterait invisible et donc inatteignable à la
	// souris : l'éditeur lui donne un REPÈRE, enfant de son Object3D (il suit
	// donc le nœud sans aucune synchronisation), masqué en mode Jeu. Ce n'est
	// pas un objet du document — il n'existe que dans la scène vivante.

	void AttachHelper(scene::NodeId id, const scene::Node &node, render3d::Object3D &owner);

	/// Recrée le repère d'un nœud après une modification de ses composants
	/// (couleur d'une lumière, taille d'une zone) et tient à jour les listes
	/// de lumières et de zones.
	void RefreshHelper(scene::NodeId id);

	void SetHelpersVisible(bool visible);

	/// Lumières de la scène, en MONDE, recalculées à chaque image : une
	/// torche portée par un personnage qui marche doit éclairer là où il est,
	/// et un script peut en changer l'intensité à tout moment.
	void UpdateLights();

	/**
	 * Pose (ou retire) le corps physique d'un nœud.
	 *
	 * Le corps est placé à la position MONDE du nœud, pas à son transform
	 * local : le solveur ne connaît que le monde, et un nœud enfant a un
	 * local relatif à son parent. Sans cette conversion, une roue parentée à
	 * une voiture tomberait depuis l'origine du monde.
	 */
	void AttachBody(ecs::Entity entity, scene::NodeId id, const scene::Node &node);

	/// Recopie la parenté du DOCUMENT dans l'ECS (`render3d::SceneParent`),
	/// que `SceneSyncSystem::Sync` traduit ensuite en re-parentage réel du
	/// graphe `Object3D`. L'arbre du document reste la référence ; l'ECS n'en
	/// est qu'une projection.
	void ApplyParentLinks(const SceneDesc &sceneDesc);

	/// Relie une paire de portails repérée par les étiquettes `portal_a` /
	/// `portal_b` — une scène sans ces étiquettes n'a simplement pas de
	/// portail, ce qui est le cas de toutes sauf la vitrine.
	void LinkPortals(const SceneDesc &sceneDesc);

	void ClearRuntime();

	void DestroyEntity(scene::NodeId id);

	// ── Poussée document -> runtime ──────────────────────────────────────────

	/// Répercute le transform d'un nœud sur l'ECS et sur son corps physique.
	/// Le transform ECS est LOCAL (c'est ce que `SceneSyncSystem` recopie
	/// dans l'Object3D, dont le parent applique le reste), alors que le corps
	/// physique veut du MONDE — d'où les deux valeurs distinctes ci-dessous.
	bool PushTransform(scene::NodeId id);

	bool PushMaterial(scene::NodeId id);

	bool PushPhysics(scene::NodeId id);

	/// Remet chaque corps à la transformation du document — appelé au
	/// démarrage du mode Jeu pour que chaque partie reparte identique.
	void ResetPhysicsBodies();

	// ── Simulation -> document ───────────────────────────────────────────────

	/// Recopie dans le DOCUMENT ce que la physique et l'animation viennent
	/// d'écrire, afin que l'inspecteur, l'outliner et le rapport voient la
	/// même chose que la 3D pendant une partie (cf. en-tête : c'est le seul
	/// flux « à l'envers », et il est annulé par Stop()).
	void MirrorSimulationToDocument();

	void UpdatePortalPlanes();

	/// `physics::PortalPlane` ne dépend volontairement de rien de render3d::
	/// (cf. portal_teleport.hpp) — c'est l'appelant qui fait le pont.
	[[nodiscard]] static physics::PortalPlane MakePortalPlane(const render3d::Portal &portal);

	void RenderPortals();

	// ── Caméra ───────────────────────────────────────────────────────────────

	[[nodiscard]] static math::FVector3 ForwardFromYawPitch(float yaw, float pitch) noexcept;

	void RefreshCameraTargets();

	/// En mode Jeu, si la scène désigne un objet suivi (étiquette
	/// `camera_target`), la caméra le poursuit à la troisième personne ; sinon
	/// elle reste libre. C'est ce qui donne une vraie caméra de jeu de
	/// voiture sans une ligne de C++ spécifique au circuit.
	void UpdateCamera(float dt);

	// ── Caméras, déclencheurs et scripts de nœud (mode Jeu) ─────────────────

	/// Première caméra marquée `current` de la scène, dans l'ordre de l'arbre.
	[[nodiscard]] scene::NodeId FindCurrentCamera() const;

	/// Objets susceptibles d'entrer dans une zone : ceux étiquetés `player`
	/// et tous les corps dynamiques. Recalculé quand la scène gagne des
	/// nœuds en cours de partie ; `resetState` remet aussi à zéro les
	/// présences et les zones `once` déjà déclenchées (début de scène
	/// seulement : une pièce ajoutée ne doit pas réarmer les autres zones).
	void PrepareTriggers(bool resetState);

	/// Test d'appartenance de chaque candidat à chaque zone (boîte alignée
	/// sur les axes, mise à l'échelle du nœud) ; seules les TRANSITIONS
	/// sont notifiées — entrer, puis sortir — et non la présence.
	void UpdateTriggers();

	/// Notifie l'objet `Scene` `hook(zone, objet, évènement)` et les
	/// Behaviour attachées à la zone `hook(objet, évènement)`.
	void FireTrigger(const String &hook, scene::NodeId zone, const String &zoneName, const String &otherName,
					 const String &event);

	/// Démarre les scripts de nœud de toute la scène (cf. StartNodeScriptsOf).
	void StartNodeScripts();

	/// Mode Jeu : attache une instance de la Behaviour de son script à chaque
	/// nœud du sous-arbre `root` qui en porte un, puis appelle leurs
	/// `on_start` — APRÈS les avoir toutes construites, pour qu'un `on_start`
	/// trouve les autres nœuds déjà équipés.
	void StartNodeScriptsOf(scene::NodeId root);

	/// Interpréteur partagé du script de bibliothèque `name` (chargé au
	/// besoin, classe Behaviour trouvée) ; nullptr si le script manque, ne
	/// se charge pas ou ne définit pas de Behaviour.
	NodeScriptInstance *NodeScriptFor(const String &name);

	/// `on_update(dt)` de chaque objet vivant des interpréteurs de nœud.
	void UpdateNodeScripts(float dt);

	/// Appelle la méthode `hook` d'une instance attachée. Une erreur
	/// DÉSACTIVE le script pour la partie (même raison que pour le script de
	/// scène : ne pas répéter la même erreur 60 fois par seconde).
	void CallNodeHook(NodeScriptInstance &instance, size_t index, const String &hook,
					  std::vector<data::script::Value> args);

	/// Désactive un script de nœud après une erreur (journal + état).
	void DisableNodeScript(NodeScriptInstance &instance, const String &where, const String &hook,
						   const data::script::ScriptError &error);

	/// Nœuds sur le point d'être retirés : les objets de script qui les
	/// portent (Behaviour attachées, Mesh3D…, dans tous les interpréteurs)
	/// sont détruits — séquence RAII — et les attachements marqués détachés.
	void DestroyObjectsOfNodes(const std::vector<scene::NodeId> &doomed);

	/// Retire les instances détachées des listes (hors de tout parcours).
	void CompactNodeScripts();

	[[nodiscard]] scene::NodeId FollowTarget() const;

	// ── Scripts ──────────────────────────────────────────────────────────────

	[[nodiscard]] Result<data::script::Value, data::script::ScriptError> RunGameplayScript(const String &source);

	/// Appelle la méthode `hook` de l'objet `Scene` de la partie.
	void CallGameplayHook(const String &hook, std::vector<data::script::Value> args);

	/// Instancie la classe du script de scène dérivée de `Scene`.
	bool CreateSceneObject();

	void RunGameplayHook(float dt);

	void InstallHostApi(data::script::Interpreter &vm);

	/// Joue la scène ACTIVE (dans la partie en cours ou au début d'une partie).
	void StartScenePlay();

	/// Quitte la scène jouée : ses scripts s'arrêtent, son document retrouve
	/// l'instantané pris à son démarrage.
	void EndScenePlay();

	/// Interpréteur de jeu neuf (API de l'éditeur, `ecs`, `ui`, `game`).
	void ResetGameplayVm();

	/// Le registre de l'éditeur vu par `ecs.host()` : chaque objet de la
	/// scène y porte `node` (son nom, lecture seule) et `transform`.
	void BindEcsWorld();

	// ── Membres ──────────────────────────────────────────────────────────────
	// Ordre imposé : m_sceneRoot AVANT m_mixer (AnimationMixer garde une
	// référence dessus), sinon -Wreorder sous -Werror.

	static constexpr uint32_t PORTAL_TARGET_SIZE = 512;
	static constexpr sdl3::GpuTextureFormat PORTAL_COLOR_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	static constexpr sdl3::GpuTextureFormat PORTAL_DEPTH_FORMAT = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

	ecs::ArchetypeRegistry &m_registry;
	jobs::JobSystem m_jobs;
	Project m_project;
	bool m_hasProject = false;
	/// Manifeste du projet ouvert, et les fichiers qui le composent (cf.
	/// files::SaveProject, qui efface ceux qui n'en font plus partie).
	String m_projectPath;
	files::FileList m_projectFiles;
	render3d::Object3D m_sceneRoot;
	render3d::AnimationMixer m_mixer;
	physics::World m_world;
	render3d::Canvas *m_canvas = nullptr;

	render3d::Camera m_editCamera{};
	render3d::Camera m_playCamera{};
	float m_editYaw = 0.f, m_editPitch = -0.25f;
	float m_playYaw = 0.f, m_playPitch = -0.1f;
	float m_followDistance = 13.f;
	float m_followHeight = 5.5f;
	float m_followStiffness = 6.f;

	render3d::Portal m_portalA, m_portalB;
	render3d::OffscreenTarget m_portalA_target, m_portalB_target;
	bool m_portalsReady = false;

	bool m_playing = false;
	bool m_gameplayReady = false;
	float m_playTime = 0.f;
	long m_frameIndex = 0;
	double m_simulationMs = 0.0;
	double m_portalMs = 0.0;
	scene::NodeTree m_playSnapshot;

	/// Nœud sélectionné — l'identité vit ICI (le marqueur ECS `Selected`
	/// n'en est qu'un reflet, pour les systèmes qui itèrent des entités).
	scene::NodeId m_selection;
	scene::NodeTypeRegistry m_types = MakeTypeRegistry();

	/// Table nœud -> entité ECS, entretenue par InstantiateNode/DestroyEntity.
	/// Sans elle, retrouver l'entité d'un nœud balayait toutes les entités —
	/// or c'est l'opération la plus fréquente du runtime.
	std::unordered_map<scene::NodeId, ecs::Entity> m_entityOf;
	/// Cache nom → identifiant de ResolveId, valide tant que l'arbre (et son
	/// tampon de structure) est le même.
	mutable std::unordered_map<String, scene::NodeId> m_nameCache;
	mutable const scene::NodeTree *m_nameCacheTree = nullptr;
	mutable uint64_t m_nameCacheStamp = 0;
	/// Nombre de nœuds par nom (cf. ScriptHandle), même cycle de vie que m_nameCache.
	mutable std::unordered_map<String, size_t> m_nameCounts;
	mutable const scene::NodeTree *m_nameCountsTree = nullptr;
	mutable uint64_t m_nameCountsStamp = 0;
	/// Repère d'édition de chaque nœud qui en a un (cf. AttachHelper) —
	/// possédé par l'Object3D du nœud, jamais par cette table.
	std::unordered_map<scene::NodeId, render3d::Shape *> m_helperOf;
	/// Nœuds portant une lumière / une zone de déclenchement, pour ne pas
	/// parcourir tout l'arbre à chaque image.
	std::vector<scene::NodeId> m_lightIds;
	std::vector<scene::NodeId> m_triggerIds;
	std::vector<render3d::PointLight> m_pointLights;
	std::vector<render3d::SpotLight> m_spotLights;

	std::vector<HistoryStep> m_undo, m_redo;
	int m_editDepth = 0;
	bool m_restoring = false;

	/// Une pièce du manipulateur : le nœud racine d'un axe, pour un mode.
	struct GizmoPart {
		GizmoMode mode = GizmoMode::TRANSLATE;
		GizmoAxis axis = GizmoAxis::NONE;
		render3d::Object3D *node = nullptr;
	};
	render3d::Object3D *m_gizmoRoot = nullptr;
	std::vector<GizmoPart> m_gizmoParts;
	GizmoMode m_gizmoMode = GizmoMode::TRANSLATE;
	GizmoAxis m_hoverAxis = GizmoAxis::NONE;
	GizmoAxis m_dragAxis = GizmoAxis::NONE;
	bool m_dragging = false;
	bool m_snapEnabled = false;
	float m_translateSnap = 0.5f, m_rotateSnap = 15.f, m_scaleSnap = 0.1f;
	scene::NodeId m_dragNode;
	math::FVector3 m_dragStartPosition, m_dragStartEuler, m_dragStartScale{1.f, 1.f, 1.f};
	math::FVector3 m_dragStartVector;
	float m_dragStartParam = 0.f;
	float m_dragSize = 1.f;

	/// Interpréteur du script de la scène jouée : NEUF à chaque démarrage de
	/// scène (cf. ResetGameplayVm) — une scène n'hérite ni des fonctions ni
	/// des variables de la précédente ; ce qui doit passer d'une scène à
	/// l'autre va dans `game.state()`.
	std::unique_ptr<data::script::Interpreter> m_gameplayVm;
	data::script::Interpreter m_toolVm;
	/// Interface des scripts (calque posé sur la vue du jeu par l'éditeur ;
	/// sans écran, les widgets restent des objets) et monde ECS de l'hôte.
	std::shared_ptr<data::script::UiBinding> m_gameUi = std::make_shared<data::script::UiBinding>();
	std::shared_ptr<data::script::EcsWorld> m_ecsWorld;
	/// Partie : scène de départ (rétablie à l'arrêt), état partagé entre les
	/// scènes, changement de scène / arrêt demandés par un script (traités en
	/// début d'image, jamais pendant l'exécution du script qui les demande).
	String m_sessionOrigin;
	data::script::Value m_gameState;
	Option<String> m_pendingScene = NONE;
	bool m_pendingQuit = false;
	uint64_t m_randomSeed = 0x2545F4914F6CDD1Dull;
	/// Objet `Scene` de la partie (instance de la classe du script de scène).
	data::script::Value m_sceneObject;
	/// Scripts de nœud chargés pour la partie en cours (cf. StartNodeScripts).
	/// Des `unique_ptr` : un script chargé PENDANT un parcours (instanciation
	/// d'une pièce) ne déplace pas ceux en cours d'appel.
	std::vector<std::unique_ptr<NodeScriptInstance>> m_nodeScripts;
	/// Profondeur des parcours de m_nodeScripts en cours (la compaction
	/// attend qu'elle revienne à zéro).
	int m_nodeScriptWalk = 0;
	bool m_nodeScriptsDirty = false;
	/// Cf. BehaviourBindingNode.
	scene::NodeId m_bindingNode;
	/// Zones : paires (zone, objet) actuellement « dedans », zones déjà
	/// déclenchées (pour `once`), objets surveillés, déclenchements comptés.
	std::unordered_set<uint64_t> m_triggerInside;
	std::unordered_set<uint32_t> m_triggerFired;
	std::vector<scene::NodeId> m_triggerCandidates;
	size_t m_triggerCount = 0;
	scene::NodeId m_currentCamera;
	/// Mouvement de souris accumulé depuis l'image précédente (mode Jeu en
	/// plein écran), lu par `input.mouse_delta()`.
	math::FVector2 m_mouseDelta{};
	Option<MouseMode> m_mouseMode = NONE;   ///< choisi par script / bascule (NONE : défaut)
	Option<SDL_Keycode> m_mouseToggleKey = Some(SDL_Keycode(SDLK_F7));
	bool m_hostFullscreen = false;
	bool m_mouseModeChanged = false;        ///< `on_mouse_mode` à appeler
	std::vector<String> m_loadedScripts;
	size_t m_scriptRunCount = 0;
	size_t m_scriptCallCount = 0;
	size_t m_scriptErrorCount = 0;

	std::vector<LogEntry> m_log;

	// ── 2D ──
	void Install2DApi(data::script::Interpreter &vm);
	std::vector<DrawItem2D> m_draw2d; ///< `draw2d.*` de l'image en cours
	sdl3::FRect m_view2dRect{};
	TextMeasure m_textMeasure;
	uint32_t m_mouseButtons = 0, m_previousMouseButtons = 0; ///< pour `canvas.mouse_pressed`
};


// ============================================================================
// API hôte exposée aux scripts
// ============================================================================
//
// Organisée en quatre tables (`editor.`, `scene.`, `object.`, `camera.`,
// `input.`) plutôt qu'en fonctions globales : un script lit alors comme du
// code d'éditeur ordinaire (`object.set_position("Voiture", x, 0, z)`), et
// l'espace de noms global reste celui de l'utilisateur.
//
// Chaque fonction rend `nil`/`false` plutôt qu'une erreur quand l'objet visé
// n'existe pas, SAUF quand l'argument lui-même est mal typé — un script qui
// interroge un objet supprimé doit pouvoir tester le résultat, alors qu'un
// `object.set_position(3, "x")` est une faute de programmation à signaler.

namespace script_detail {

using data::script::Interpreter;
using data::script::ScriptError;
using data::script::Value;

/// Liste de 3 nombres — la forme sous laquelle un vecteur circule côté script.
[[nodiscard]] Value Vec3ToValue(const math::FVector3 &v);

/// Lit trois arguments numériques consécutifs comme un vecteur.
[[nodiscard]] Result<math::FVector3, ScriptError> ArgVec3(const std::vector<Value> &args, size_t first,
																 const char *fnName);

/// Lit un champ d'une table de script comme vecteur (`{pos: [1, 2, 3]}`).
[[nodiscard]] math::FVector3 FieldVec3(const data::script::MapObject &map, const char *key,
											  math::FVector3 fallback);

[[nodiscard]] float FieldFloat(const data::script::MapObject &map, const char *key, float fallback);

[[nodiscard]] bool FieldBool(const data::script::MapObject &map, const char *key, bool fallback);

[[nodiscard]] String FieldString(const data::script::MapObject &map, const char *key, const char *fallback);

[[nodiscard]] Option<sdl3::Color> FieldColor(const data::script::MapObject &map, const char *key);

/// Composantes 0–255 (bornées) d'une couleur opaque.
[[nodiscard]] sdl3::Color ColorFromVec3(const math::FVector3 &rgb);

/// Propriété libre d'un nœud → valeur de script (nil si absente).
[[nodiscard]] Value PropertyToValue(const scene::PropertyValue *value);

/// Écrit une propriété libre ; son TYPE vient de la valeur du script.
void WriteProperty(scene::Node &node, const String &key, const Value &value);

/// Objet décrit par une table (`scene.spawn`, bases Node3D / Mesh3D) : name,
/// parent, tag, shape, model, size, segments, visible, pos, rot, scale,
/// material, color, metallic, roughness, double_sided, wireframe, body,
/// collider, half_extents, mass, restitution, friction.
[[nodiscard]] NodeDesc NodeFromTable(const data::script::MapObject &map);

/// Modifie `material` selon les clés présentes (kind ou material, color,
/// metallic, roughness, double_sided, wireframe) : une table partielle ne
/// change que ce qu'elle mentionne.
void ReadMaterialTable(const data::script::MapObject &map, MaterialDesc &material);

/// Idem pour un corps : `body` (nom de type, ou table imbriquée lue de la
/// même façon), `kind`, collider, half_extents, mass, restitution, friction.
void ReadPhysicsTable(const data::script::MapObject &map, PhysicsDesc &physics);

/// Lumière : kind, color, intensity, range, spot_angle, penumbra, shadow.
[[nodiscard]] LightDesc LightFromTable(const data::script::MapObject &map, LightDesc base);

/// Placement d'une instance (`{parent, pos, rot, scale}`) ; `parent` reçoit
/// le nœud parent nommé, s'il y en a un. NONE si ni pos, ni rot, ni scale.
[[nodiscard]] Option<scene::Transform> PlacementFromTable(const Runtime &runtime, const data::script::MapObject &map,
														  scene::NodeId &parent);

/// Nom de touche -> scancode SDL. Table volontairement courte et explicite :
/// exposer `SDL_GetScancodeFromName` laisserait un script écrire n'importe
/// quel identifiant SDL, ce qui lierait la syntaxe des scripts à SDL.
[[nodiscard]] Option<SDL_Keycode> ScancodeFromName(const String &name);

} // namespace script_detail



} // namespace game_editor

#include "runtime2d.hpp"
