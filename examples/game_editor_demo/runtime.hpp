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
 * `SpawnObject`…) qui écrit d'ABORD dans le `ObjectDesc` du document, PUIS
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

#include "project.hpp"

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
struct SceneObjectRef {
	scene::NodeId node;
};

/// Marqueur de sélection (une seule à la fois, cf. `Select`).
struct Selected {};

/// Un script de la bibliothèque chargé pour la partie en cours : UN
/// interpréteur, partagé par tous les nœuds qui portent ce script.
struct NodeScriptInstance {
	String script;
	std::unique_ptr<data::script::Interpreter> vm;
	std::vector<scene::NodeId> nodes;
	bool ready = false;
	String error; ///< dernière erreur (chargement ou appel), vide si aucune
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

	[[nodiscard]] const char *LevelName() const noexcept {
		switch (level) {
			case LogLevel::INFO:
				return "info";
			case LogLevel::SUCCESS:
				return "ok";
			case LogLevel::WARNING:
				return "warn";
			case LogLevel::ERROR_LEVEL:
				return "err";
			case LogLevel::SCRIPT:
				return "script";
		}
		return "info";
	}
};

// ============================================================================
// Runtime
// ============================================================================

class Runtime {
public:
	/// `jobWorkers < 0` : vivier automatique (cœurs - 1). `0` : tout sur le
	/// fil appelant.
	explicit Runtime(ecs::ArchetypeRegistry &registry, int jobWorkers = -1)
		: m_registry(registry),
		  m_jobs(jobWorkers < 0 ? jobs::JobSystem::RecommendedWorkerCount() : size_t(jobWorkers)),
		  m_mixer(m_sceneRoot), m_world(registry) {
		m_sceneRoot.SetName("Scene Root");
		// La physique répartit sa phase large (O(n²)) et sa génération de
		// manifolds sur ce vivier dès que la scène est assez peuplée pour que
		// ça en vaille la peine ; en dessous, tout reste sur le fil principal
		// (cf. jobs::JobSystem::ParallelFor et son `minimumPerBatch`). Le
		// résultat est identique dans les deux cas, ce qui préserve la
		// reproductibilité promise par `--seed`.
		m_world.jobSystem = &m_jobs;
		InstallHostApi(m_gameplayVm);
		InstallHostApi(m_toolVm);
	}

	Runtime(const Runtime &) = delete;
	Runtime &operator=(const Runtime &) = delete;

	// ── Branchements facultatifs ─────────────────────────────────────────────

	/// Fournit le device GPU (rendu des portails). Sans cet appel, le runtime
	/// marche à l'identique sans portails (cf. en-tête).
	void AttachCanvas(render3d::Canvas &canvas) {
		m_canvas = &canvas;
		ApplyEnvironment();
	}

	/// Graine partagée par les deux interpréteurs : deux exécutions avec la
	/// même graine produisent exactement la même partie.
	void SetRandomSeed(uint64_t seed) {
		m_randomSeed = seed;
		m_gameplayVm.SetRandomSeed(seed);
		m_toolVm.SetRandomSeed(seed ^ 0x5DEECE66Dull);
	}

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
	[[nodiscard]] data::script::Interpreter &GameplayVm() noexcept { return m_gameplayVm; }
	[[nodiscard]] data::script::Interpreter &ToolVm() noexcept { return m_toolVm; }
	[[nodiscard]] const std::vector<LogEntry> &Log() const noexcept { return m_log; }
	[[nodiscard]] bool IsPlaying() const noexcept { return m_playing; }
	[[nodiscard]] long FrameIndex() const noexcept { return m_frameIndex; }
	[[nodiscard]] float PlayTime() const noexcept { return m_playTime; }

	[[nodiscard]] SceneDesc *ActiveScene() noexcept { return m_project.ActiveScene(); }
	[[nodiscard]] const SceneDesc *ActiveScene() const noexcept { return m_project.ActiveScene(); }

	[[nodiscard]] render3d::Camera &ActiveCamera() noexcept { return m_playing ? m_playCamera : m_editCamera; }
	[[nodiscard]] const render3d::Camera &ActiveCamera() const noexcept {
		return m_playing ? m_playCamera : m_editCamera;
	}

	/// Nœud sélectionné (invalide si rien n'est sélectionné).
	[[nodiscard]] scene::NodeId SelectedId() const noexcept { return m_selection; }

	/// Appelé par l'interface pour afficher le nom de l'objet sélectionné.
	[[nodiscard]] Option<String> SelectedName() const {
		const scene::Node *node = SelectedConstObject();
		if (!node)
			return NONE;
		return Some(node->name);
	}

	[[nodiscard]] scene::Node *SelectedObject() noexcept { return FindObject(m_selection); }

	// ── Accès au document par identifiant ────────────────────────────────────
	// Public : l'interface (outliner, inspecteur), les natives de script et
	// les tests raisonnent tous en identifiants de nœuds.

	[[nodiscard]] scene::NodeTree *Tree() noexcept {
		SceneDesc *scene = ActiveScene();
		return scene ? &scene->tree : nullptr;
	}
	[[nodiscard]] const scene::NodeTree *Tree() const noexcept {
		const SceneDesc *scene = ActiveScene();
		return scene ? &scene->tree : nullptr;
	}

	/// Identifiant du nœud portant ce nom dans la scène active.
	/// Nom ou chemin → identifiant. Les scripts désignent les objets par leur
	/// nom, plusieurs fois par image et par script : la recherche linéaire
	/// dans l'arbre est donc mise en cache, et le cache se VALIDE par le
	/// tampon de structure de l'arbre (cf. scene::NodeTree::StructureStamp)
	/// plus une vérification du nom — aucune commande n'a à penser à le vider.
	[[nodiscard]] scene::NodeId ResolveId(const String &objectName) const {
		const SceneDesc *scene = ActiveScene();
		if (!scene)
			return scene::NodeId{};
		if (objectName.IsEmpty() || objectName.Contains('/'))
			return scene->Resolve(objectName);
		if (m_nameCacheTree != &scene->tree || m_nameCacheStamp != scene->tree.StructureStamp()) {
			m_nameCache.clear();
			m_nameCacheTree = &scene->tree;
			m_nameCacheStamp = scene->tree.StructureStamp();
		}
		if (auto it = m_nameCache.find(objectName); it != m_nameCache.end()) {
			const scene::Node *node = scene->tree.Get(it->second);
			if (node && node->name == objectName)
				return it->second;
		}
		const scene::NodeId id = scene->Resolve(objectName);
		if (id.Valid()) // les absences ne sont pas retenues : un nom peut apparaître
			m_nameCache[objectName] = id;
		return id;
	}

	[[nodiscard]] scene::Node *FindObject(const String &objectName) noexcept {
		scene::NodeTree *tree = Tree();
		scene::NodeId id = ResolveId(objectName);
		return tree && id.Valid() ? tree->Get(id) : nullptr;
	}

	[[nodiscard]] scene::Node *FindObject(scene::NodeId id) noexcept {
		scene::NodeTree *tree = Tree();
		return tree ? tree->Get(id) : nullptr;
	}

	/// Entité ECS incarnant ce nœud. Table de correspondance plutôt que
	/// balayage de toutes les entités : c'est l'opération la plus fréquente
	/// du runtime (chaque commande d'édition la fait), et un balayage la
	/// rendait linéaire en nombre d'objets.
	[[nodiscard]] Option<ecs::Entity> FindEntity(scene::NodeId id) const {
		auto found = m_entityOf.find(id);
		if (found == m_entityOf.end())
			return NONE;
		return Some(found->second);
	}

	[[nodiscard]] Option<ecs::Entity> FindEntity(const String &objectName) const {
		return FindEntity(ResolveId(objectName));
	}

	[[nodiscard]] render3d::Object3D *FindNode(scene::NodeId id) {
		Option<ecs::Entity> entity = FindEntity(id);
		if (entity.IsNone())
			return nullptr;
		auto node = m_registry.GetComponent<render3d::SceneNode>(entity.Unwrap());
		return node.IsSome() ? node.Unwrap()->node : nullptr;
	}

	[[nodiscard]] render3d::Object3D *FindNode(const String &objectName) { return FindNode(ResolveId(objectName)); }

	// ── Journal ──────────────────────────────────────────────────────────────

	void Log(LogLevel level, String text) {
		m_log.push_back(LogEntry{level, m_frameIndex, std::move(text)});
		// Le journal d'un éditeur qui tourne longtemps ne doit pas grossir
		// sans fin : on garde une fenêtre glissante des dernières entrées.
		if (m_log.size() > MAX_LOG_ENTRIES)
			m_log.erase(m_log.begin(), m_log.begin() + ptrdiff_t(m_log.size() - MAX_LOG_ENTRIES));
		if (onLog)
			onLog(m_log.back());
	}

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
	/// Notifié quand la sélection change.
	std::function<void()> onSelectionChanged;
	/// Notifié quand une PROPRIÉTÉ de l'objet sélectionné change hors mode
	/// Jeu (manipulateur, script d'outil, annulation) — l'inspecteur doit
	/// alors réafficher ses champs, sinon il montre encore l'ancienne valeur.
	/// Muet pendant le mode Jeu : la physique y écrit à chaque image, et
	/// reconstruire l'inspecteur 60 fois par seconde coûterait plus cher que
	/// tout le reste.
	std::function<void()> onObjectChanged;

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

	/// Remplace le projet courant et reconstruit la scène active.
	void OpenProject(Project project) {
		m_project = std::move(project);
		RebuildRuntime();
		LogSuccess(String::Format("Projet « %s » chargé (%d scènes, %d objets)", m_project.name.CStr(),
								  int(m_project.scenes.size()), int(m_project.TotalObjectCount())));
	}

	[[nodiscard]] Result<bool, String> LoadProjectFile(const String &path) {
		auto text = data::script::LoadScriptFile(path);
		if (text.IsError())
			return Err(text.Error());
		auto project = Project::DecodeJson(text.Value());
		if (project.IsError())
			return Err(project.Error());
		OpenProject(std::move(project).Unwrap());
		return Ok(true);
	}

	[[nodiscard]] Result<bool, String> SaveProjectFile(const String &path) const {
		String text = m_project.EncodeJson();
		if (!sdl3::WriteFile(path, text.CStr(), text.GetSize()))
			return Err(String::Format("écriture impossible : %s", path.CStr()));
		return Ok(true);
	}

	bool SwitchScene(const String &sceneName) {
		if (!m_project.FindScene(sceneName))
			return false;
		if (m_playing)
			Stop();
		m_project.SetActiveScene(sceneName);
		RebuildRuntime();
		LogInfo(String::Format("Scène active : %s", sceneName.CStr()));
		return true;
	}

	/// Détruit puis reconstruit intégralement la scène vivante à partir du
	/// document. Volontairement brutal (tout jeter, tout refaire) plutôt
	/// qu'un diff incrémental : c'est appelé au chargement et au changement
	/// de scène, jamais dans la boucle de rendu, et ça supprime par
	/// construction toute possibilité de désynchronisation document/runtime.
	void RebuildRuntime() {
		ClearRuntime();
		SceneDesc *scene = ActiveScene();
		if (!scene)
			return;

		// Parcours préfixe : un parent est toujours instancié avant ses
		// enfants, donc le lien de parenté ECS peut être posé dans la foulée.
		scene->tree.Traverse(scene->tree.Root(), [&](scene::NodeId id, const scene::Node &node) {
			if (id == scene->tree.Root())
				return;
			InstantiateNode(id, node);
		});
		ApplyParentLinks(*scene);
		LinkPortals(*scene);

		m_editCamera.position = scene->camera.editPosition;
		m_editYaw = scene->camera.editYaw;
		m_editPitch = scene->camera.editPitch;
		m_playCamera.position = scene->camera.playPosition;
		m_playYaw = scene->camera.playYaw;
		m_playPitch = scene->camera.playPitch;
		RefreshCameraTargets();

		ApplyEnvironment();
		render3d::SceneSyncSystem::Sync(m_registry, m_sceneRoot);

		if (onSceneStructureChanged)
			onSceneStructureChanged();
		if (onSelectionChanged)
			onSelectionChanged();
	}

	/// Répercute l'ambiance du document (gravité, soleil, lumière ambiante,
	/// couleur de fond) sur la simulation et sur le rendu. Appelée au
	/// chargement d'une scène ET à chaque édition des réglages du monde —
	/// c'est assez bon marché pour n'avoir jamais besoin d'être différé.
	void ApplyEnvironment() {
		const SceneDesc *scene = ActiveScene();
		if (!scene)
			return;
		m_world.config.gravity = scene->environment.gravity;
		if (!m_canvas)
			return; // mode sans écran : la partie éclairage n'a pas d'objet

		render3d::DirectionalLight sun;
		sun.direction = scene->environment.sunDirection;
		sun.color = scene->environment.sunColor;
		sun.intensity = scene->environment.sunIntensity;
		render3d::AmbientLight ambient;
		ambient.color = scene->environment.ambientColor;
		m_canvas->SetLighting(sun, ambient);
		m_canvas->SetBackgroundColor(scene->environment.backgroundColor);
	}

	// ── Boucle ───────────────────────────────────────────────────────────────

	/// Une image. Ordre : (mode Jeu : scripts, physique, animation, recopie
	/// vers le document, zones) → caméra → synchronisation ECS→Object3D →
	/// lumières → portails.
	/// La synchronisation vient APRÈS toutes les écritures de l'image pour
	/// que le graphe rendu reflète l'état final, et les portails APRÈS elle
	/// car ils rendent la scène déjà à jour.
	void Update(float dt) {
		++m_frameIndex;
		const double tickRate = double(sdl3::GetPerformanceFrequency());
		uint64_t mark = sdl3::GetPerformanceCounter();

		if (m_playing) {
			m_playTime += dt;
			RunGameplayHook(dt);
			UpdateNodeScripts(dt);
			m_mixer.Update(dt);
			UpdatePortalPlanes();
			m_world.Step(dt);
			MirrorSimulationToDocument();
			UpdateTriggers();
		}
		// Le mouvement de souris accumulé depuis l'image précédente a été lu
		// par les scripts de cette image : on repart de zéro.
		m_mouseDelta = {};
		// Caméra APRÈS la simulation : celle qui suit un objet (poursuite,
		// vue subjective) doit le voir là où cette image l'a mis, sinon elle
		// traîne d'une image — un tremblement très visible à la première
		// personne.
		UpdateCamera(dt);

		render3d::SceneSyncSystem::Sync(m_registry, m_sceneRoot);
		UpdateLights();
		// Le manipulateur suit la sélection ET la caméra (sa taille dépend de
		// la distance) : il se replace donc à chaque image, après la
		// synchronisation qui vient de bouger les objets.
		RefreshGizmo();
		uint64_t afterSimulation = sdl3::GetPerformanceCounter();
		m_simulationMs = double(afterSimulation - mark) * 1000.0 / tickRate;

		RenderPortals();
		m_portalMs = double(sdl3::GetPerformanceCounter() - afterSimulation) * 1000.0 / tickRate;
	}

	/// Coût de la dernière image, en millisecondes : simulation (scripts +
	/// physique + animation + synchronisation ECS→Object3D) et rendu des
	/// portails, que le profileur et le rapport publient séparément.
	[[nodiscard]] double SimulationMs() const noexcept { return m_simulationMs; }
	[[nodiscard]] double PortalMs() const noexcept { return m_portalMs; }

	// ── Mode Jeu ─────────────────────────────────────────────────────────────

	/// Démarre le mode Jeu : instantané du document, corps physiques remis à
	/// leur état initial, script de gameplay (re)chargé et `on_start()`
	/// appelé. Rejouer donne donc exactement la même partie.
	void Play() {
		if (m_playing)
			return;
		SceneDesc *scene = ActiveScene();
		if (!scene) {
			LogError(String("Impossible de lancer le mode Jeu : aucune scène active"));
			return;
		}

		m_playSnapshot = scene->tree;
		m_playTime = 0.f;
		m_playing = true;
		m_playCamera.position = scene->camera.playPosition;
		m_playYaw = scene->camera.playYaw;
		m_playPitch = scene->camera.playPitch;
		ResetPhysicsBodies();

		m_gameplayReady = false;
		if (!scene->gameplayScript.IsEmpty()) {
			auto loaded = RunGameplayScript(scene->gameplayScript);
			if (loaded.IsError()) {
				LogError(String::Format("Script de jeu : %s", loaded.Error().Format().CStr()));
			} else {
				m_gameplayReady = true;
				CallGameplayHook(String("on_start"), {});
			}
		}
		SetHelpersVisible(false);
		m_currentCamera = FindCurrentCamera();
		PrepareTriggers();
		StartNodeScripts();
		LogSuccess(String::Format("▶ Mode Jeu — scène « %s »", scene->name.CStr()));
	}

	/// Arrête le mode Jeu et restaure l'instantané : l'édition reprend
	/// exactement où elle en était.
	void Stop() {
		if (!m_playing)
			return;
		m_playing = false;
		m_nodeScripts.clear();
		m_triggerInside.clear();
		m_triggerFired.clear();
		m_triggerCandidates.clear();
		m_currentCamera = scene::NodeId{};
		m_playCamera.fovYRadians = m_editCamera.fovYRadians;
		if (SceneDesc *scene = ActiveScene()) {
			scene->tree = m_playSnapshot;
			RebuildRuntime();
		}
		LogInfo(String::Format("■ Mode Édition (partie de %.1f s)", double(m_playTime)));
		m_playTime = 0.f;
	}

	void TogglePlay() {
		if (m_playing)
			Stop();
		else
			Play();
	}

	// ── Sélection ────────────────────────────────────────────────────────────

	/// Sélection unique. Les entités portant déjà le marqueur sont COLLECTÉES
	/// d'abord puis modifiées ensuite : muter pendant un `Query` invaliderait
	/// l'itération (piège documenté en tête de ecs.hpp).
	/// Sélectionne par identifiant de nœud (forme de référence).
	bool Select(scene::NodeId id) {
		std::vector<ecs::Entity> previous = m_registry.EntitiesWith<Selected>();
		for (ecs::Entity entity : previous)
			m_registry.RemoveComponent<Selected>(entity);

		m_selection = scene::NodeId{};
		bool found = false;
		if (id.Valid() && FindObject(id)) {
			m_selection = id;
			if (Option<ecs::Entity> entity = FindEntity(id); entity.IsSome())
				m_registry.AddComponent(entity.Unwrap(), Selected{});
			found = true;
		}
		RefreshGizmo();
		if (onSelectionChanged)
			onSelectionChanged();
		return found;
	}

	/// Sélectionne par nom ou par chemin — c'est ce qu'emploient les scripts
	/// et l'interface. Une chaîne vide désélectionne.
	bool Select(const String &objectName) {
		return Select(objectName.IsEmpty() ? scene::NodeId{} : ResolveId(objectName));
	}

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
	[[nodiscard]] math::FRay ViewportRay(float x, float y, float width, float height) const {
		return ActiveCamera().ScreenPointToRay(x, y, width, height);
	}

	/// Objet visible le plus proche touché par `ray`. Les objets masqués sont
	/// ignorés (on ne sélectionne pas ce qu'on ne voit pas), le manipulateur
	/// aussi : ses nœuds n'appartiennent pas au document.
	[[nodiscard]] Option<PickHit> PickAt(const math::FRay &ray) {
		const SceneDesc *scene = ActiveScene();
		if (!scene)
			return NONE;
		Option<PickHit> best = NONE;
		for (scene::NodeId id : scene->Objects()) {
			// Visibilité HÉRITÉE : un enfant visible sous un parent caché
			// n'est pas à l'écran, donc pas sélectionnable au clic.
			if (!scene->tree.IsVisibleInTree(id))
				continue;
			const scene::Node *node = scene->tree.Get(id);
			if (!node || node->locked)
				continue; // nœud verrouillé : ignoré par le clic, comme dans tout éditeur
			auto *shape = dynamic_cast<render3d::Shape *>(FindNode(id));
			// Nœud sans forme (lumière, caméra) : on clique son REPÈRE
			// d'édition. Les zones de déclenchement sont traitées à part,
			// ci-dessous : une grande boîte invisible en jeu ne doit pas
			// voler le clic à ce qu'elle entoure.
			if (!shape && !TriggerDesc::Has(*node))
				if (auto helper = m_helperOf.find(id); helper != m_helperOf.end() && !m_playing)
					shape = helper->second;
			if (!shape)
				continue;
			Option<render3d::PickResult> hit = render3d::PickMeshFace(ray, *shape);
			if (hit.IsNone())
				continue;
			if (best.IsNone() || hit.Unwrap().t < best.Value().distance)
				best = Some(PickHit{node->name, id, hit.Unwrap().t, hit.Unwrap().point});
		}
		if (best.IsSome() || m_playing)
			return best;
		// Rien de solide sous le curseur : une zone de déclenchement peut
		// alors être prise (clic dans le vide qu'elle occupe).
		for (scene::NodeId id : m_triggerIds) {
			const scene::Node *node = scene->tree.Get(id);
			if (!node || node->locked || !scene->tree.IsVisibleInTree(id))
				continue;
			auto helper = m_helperOf.find(id);
			if (helper == m_helperOf.end())
				continue;
			Option<render3d::PickResult> hit = render3d::PickMeshFace(ray, *helper->second);
			if (hit.IsSome() && (best.IsNone() || hit.Unwrap().t < best.Value().distance))
				best = Some(PickHit{node->name, id, hit.Unwrap().t, hit.Unwrap().point});
		}
		return best;
	}

	/// Sélectionne l'objet sous le rayon ; un clic dans le vide désélectionne.
	/// Rend vrai si un objet a été touché.
	bool SelectAt(const math::FRay &ray) {
		Option<PickHit> hit = PickAt(ray);
		if (hit.IsNone()) {
			ClearSelection();
			return false;
		}
		(void)Select(hit.Value().node);
		return true;
	}

	[[nodiscard]] GizmoMode GetGizmoMode() const noexcept { return m_gizmoMode; }
	void SetGizmoMode(GizmoMode mode) {
		m_gizmoMode = mode;
		RefreshGizmo();
	}

	/// Pas de magnétisme : 0 = désactivé. Distances en unités monde, angles
	/// en degrés, échelle en facteur.
	[[nodiscard]] float TranslateSnap() const noexcept { return m_translateSnap; }
	[[nodiscard]] float RotateSnapDegrees() const noexcept { return m_rotateSnap; }
	[[nodiscard]] float ScaleSnap() const noexcept { return m_scaleSnap; }
	[[nodiscard]] bool SnapEnabled() const noexcept { return m_snapEnabled; }
	void SetSnapEnabled(bool enabled) noexcept { m_snapEnabled = enabled; }
	void SetSnapSteps(float translate, float rotateDegrees, float scale) noexcept {
		m_translateSnap = sdl3::Max(translate, 0.f);
		m_rotateSnap = sdl3::Max(rotateDegrees, 0.f);
		m_scaleSnap = sdl3::Max(scale, 0.f);
	}

	/// Taille du manipulateur en unités monde : proportionnelle à la distance
	/// à la caméra, pour qu'il garde la même taille à l'écran.
	[[nodiscard]] float GizmoSize() const {
		Option<math::FVector3> center = SelectionWorldPosition();
		if (center.IsNone())
			return 0.f;
		const float distance = (center.Unwrap() - ActiveCamera().position).Length();
		return sdl3::Max(distance * 0.14f, 0.25f);
	}

	/// Position MONDE du nœud sélectionné — le manipulateur, le cadrage et
	/// le pivot d'orbite s'en servent tous (un nœud enfant n'est PAS à son
	/// transform local dans la scène).
	[[nodiscard]] Option<math::FVector3> SelectionWorldPosition() const {
		const scene::NodeTree *tree = Tree();
		if (!tree || !m_selection.Valid() || !tree->Contains(m_selection))
			return NONE;
		return Some(tree->GlobalPosition(m_selection));
	}

	/// Axe du manipulateur sous le rayon, NONE si le rayon passe à côté.
	/// Test « distance du rayon au segment de l'axe » avec une tolérance
	/// proportionnelle à la taille affichée : ce qui est visuellement épais
	/// est attrapable.
	[[nodiscard]] GizmoAxis PickGizmoAxis(const math::FRay &ray) const {
		Option<math::FVector3> center = SelectionWorldPosition();
		if (center.IsNone())
			return GizmoAxis::NONE;
		const math::FVector3 origin = center.Unwrap();
		const float size = GizmoSize();
		const float tolerance = size * 0.22f;

		GizmoAxis bestAxis = GizmoAxis::NONE;
		float bestDistance = tolerance;
		for (GizmoAxis axis : {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z}) {
			const math::FVector3 direction = AxisVector(axis);
			float distance = 0.f;
			if (m_gizmoMode == GizmoMode::ROTATE) {
				// Anneau : distance du point d'impact sur le plan de l'anneau
				// au cercle lui-même.
				Option<math::FVector3> point = IntersectPlane(ray, origin, direction);
				if (point.IsNone())
					continue;
				distance = sdl3::Abs((point.Unwrap() - origin).Length() - size);
			} else {
				Option<float> parameter = ClosestParamOnAxis(ray, origin, direction);
				if (parameter.IsNone() || parameter.Unwrap() < 0.f || parameter.Unwrap() > size * 1.15f)
					continue; // au-delà de la flèche : ce n'est pas une prise
				const math::FVector3 onAxis = origin + direction * parameter.Unwrap();
				distance = ray.DistanceTo(onAxis);
			}
			if (distance < bestDistance) {
				bestDistance = distance;
				bestAxis = axis;
			}
		}
		return bestAxis;
	}

	/// Commence un glissé sur `axis`. Mémorise la prise pour que l'objet
	/// suive le curseur sans saut initial.
	bool BeginGizmoDrag(GizmoAxis axis, const math::FRay &ray) {
		const scene::Node *node = SelectedConstObject();
		const scene::NodeTree *tree = Tree();
		if (axis == GizmoAxis::NONE || !node || !tree || node->locked)
			return false;
		const math::FVector3 direction = AxisVector(axis);
		m_dragObject = m_selection;
		m_dragAxis = axis;
		// Le manipulateur est dessiné, et donc tiré, en coordonnées MONDE :
		// c'est la position monde qui sert de référence au geste. Le
		// transform LOCAL, lui, sera recalculé par SetGlobalPosition — sans
		// quoi déplacer l'enfant d'un parent déplacé le ferait sauter.
		m_dragStartPosition = tree->GlobalPosition(m_selection);
		m_dragStartEuler = node->transform.EulerDegrees();
		m_dragStartScale = node->transform.scale;
		m_dragSize = GizmoSize();

		if (m_gizmoMode == GizmoMode::ROTATE) {
			Option<math::FVector3> point = IntersectPlane(ray, m_dragStartPosition, direction);
			if (point.IsNone())
				return false;
			m_dragStartVector = point.Unwrap() - m_dragStartPosition;
			if (m_dragStartVector.Length() < 1e-4f)
				return false;
		} else {
			Option<float> parameter = ClosestParamOnAxis(ray, m_dragStartPosition, direction);
			if (parameter.IsNone())
				return false;
			m_dragStartParam = parameter.Unwrap();
		}
		BeginEdit(GizmoLabel(axis));
		m_dragging = true;
		return true;
	}

	/// Poursuit le glissé : calcule la nouvelle valeur depuis le rayon
	/// courant, applique le magnétisme, et passe par la commande d'édition
	/// ordinaire (donc document d'abord, scène ensuite).
	bool UpdateGizmoDrag(const math::FRay &ray) {
		if (!m_dragging)
			return false;
		const math::FVector3 direction = AxisVector(m_dragAxis);
		switch (m_gizmoMode) {
			case GizmoMode::TRANSLATE: {
				Option<float> parameter = ClosestParamOnAxis(ray, m_dragStartPosition, direction);
				if (parameter.IsNone())
					return false;
				float delta = parameter.Unwrap() - m_dragStartParam;
				math::FVector3 position = m_dragStartPosition + direction * delta;
				if (m_snapEnabled && m_translateSnap > 0.f) {
					// Magnétisme sur la GRILLE du monde (deux objets déplacés
					// séparément s'alignent alors l'un sur l'autre), mais
					// SEULEMENT sur l'axe tiré : sinon tirer X ferait aussi
					// sauter Y et Z, alors qu'on n'a touché qu'un axe.
					if (m_dragAxis == GizmoAxis::X)
						position.x = SnapTo(position.x, m_translateSnap);
					else if (m_dragAxis == GizmoAxis::Y)
						position.y = SnapTo(position.y, m_translateSnap);
					else
						position.z = SnapTo(position.z, m_translateSnap);
				}
				return SetGlobalPosition(m_dragObject, position);
			}
			case GizmoMode::ROTATE: {
				Option<math::FVector3> point = IntersectPlane(ray, m_dragStartPosition, direction);
				if (point.IsNone())
					return false;
				const math::FVector3 current = point.Unwrap() - m_dragStartPosition;
				if (current.Length() < 1e-4f)
					return false;
				// Angle signé entre la prise et la position courante, autour
				// de l'axe : le produit mixte donne le sens.
				const float angle = sdl3::Atan2(m_dragStartVector.Cross(current).Dot(direction),
												m_dragStartVector.Dot(current)) *
									(180.f / 3.14159265f);
				float snapped = m_snapEnabled && m_rotateSnap > 0.f ? SnapTo(angle, m_rotateSnap) : angle;
				math::FVector3 euler = m_dragStartEuler;
				if (m_dragAxis == GizmoAxis::X)
					euler.x += snapped;
				else if (m_dragAxis == GizmoAxis::Y)
					euler.y += snapped;
				else
					euler.z += snapped;
				return SetEulerDegrees(m_dragObject, euler);
			}
			case GizmoMode::SCALE: {
				Option<float> parameter = ClosestParamOnAxis(ray, m_dragStartPosition, direction);
				if (parameter.IsNone() || m_dragSize <= 0.f)
					return false;
				// Un déplacement d'une longueur de manipulateur double la
				// taille : geste prévisible quelle que soit la distance.
				float factor = 1.f + (parameter.Unwrap() - m_dragStartParam) / m_dragSize;
				factor = sdl3::Max(factor, 0.01f);
				if (m_snapEnabled && m_scaleSnap > 0.f)
					factor = sdl3::Max(SnapTo(factor, m_scaleSnap), m_scaleSnap);
				math::FVector3 scale = m_dragStartScale;
				if (m_dragAxis == GizmoAxis::X)
					scale.x = m_dragStartScale.x * factor;
				else if (m_dragAxis == GizmoAxis::Y)
					scale.y = m_dragStartScale.y * factor;
				else
					scale.z = m_dragStartScale.z * factor;
				return SetScale(m_dragObject, scale);
			}
		}
		return false;
	}

	/// Rejoue un glissé complet sur `axis`, décrit en unités MONDE le long de
	/// cet axe (déplacement, échelle) ou en DEGRÉS autour de lui (rotation) —
	/// ce qu'un scénario ou un test peut écrire sans connaître la caméra. Le
	/// geste est découpé en petits pas, comme une vraie souris.
	bool ScriptedGizmoDrag(GizmoAxis axis, float from, float to) {
		Option<math::FVector3> center = SelectionWorldPosition();
		if (center.IsNone() || axis == GizmoAxis::NONE)
			return false;
		const math::FVector3 origin = center.Unwrap();
		const float size = GizmoSize();
		const math::FVector3 direction = AxisVector(axis);
		// Base du plan perpendiculaire à l'axe, pour les gestes de rotation.
		const math::FVector3 reference =
			sdl3::Abs(direction.y) > 0.9f ? math::FVector3{1.f, 0.f, 0.f} : math::FVector3{0.f, 1.f, 0.f};
		const math::FVector3 u = direction.Cross(reference).Normalize();
		const math::FVector3 v = direction.Cross(u);

		auto rayAt = [&](float value) {
			math::FVector3 target = m_gizmoMode == GizmoMode::ROTATE
										? origin + (u * sdl3::Cos(value * 3.14159265f / 180.f) +
													v * sdl3::Sin(value * 3.14159265f / 180.f)) *
													   size
										: origin + direction * value;
			return math::FRay{ActiveCamera().position, target - ActiveCamera().position};
		};

		if (!BeginGizmoDrag(axis, rayAt(from)))
			return false;
		constexpr int STEPS = 8;
		for (int step = 1; step <= STEPS; ++step)
			(void)UpdateGizmoDrag(rayAt(from + (to - from) * float(step) / float(STEPS)));
		EndGizmoDrag();
		return true;
	}

	void EndGizmoDrag() {
		if (!m_dragging)
			return;
		m_dragging = false;
		m_dragAxis = GizmoAxis::NONE;
		EndEdit();
	}

	[[nodiscard]] bool IsGizmoDragging() const noexcept { return m_dragging; }
	[[nodiscard]] GizmoAxis DraggedAxis() const noexcept { return m_dragAxis; }

	/// Axe survolé, mis en avant dans le rendu du manipulateur.
	void SetHoveredAxis(GizmoAxis axis) {
		if (axis == m_hoverAxis)
			return;
		m_hoverAxis = axis;
		RefreshGizmo();
	}
	[[nodiscard]] GizmoAxis HoveredAxis() const noexcept { return m_hoverAxis; }

	/// Replace et redimensionne le manipulateur (appelé à chaque image par
	/// `Update`, et après tout changement de sélection ou de mode).
	void RefreshGizmo() {
		if (!m_canvas)
			return; // mode sans écran : le manipulateur n'a rien à afficher
		EnsureGizmoNodes();
		Option<math::FVector3> center = SelectionWorldPosition();
		const bool show = center.IsSome() && !m_playing;
		m_gizmoRoot->SetVisible(show);
		if (!show)
			return;
		m_gizmoRoot->SetPosition(center.Unwrap());
		const float size = GizmoSize();
		for (size_t i = 0; i < m_gizmoParts.size(); ++i) {
			GizmoPart &part = m_gizmoParts[i];
			const bool active = part.mode == m_gizmoMode;
			part.node->SetVisible(active);
			if (!active)
				continue;
			// L'axe survolé (ou tiré) grossit : le retour visuel ne coûte
			// alors aucun changement de matériau.
			const bool highlighted = part.axis == (m_dragging ? m_dragAxis : m_hoverAxis);
			part.node->SetScale(math::FVector3{size, size, size} * (highlighted ? 1.25f : 1.f));
		}
	}

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
	void BeginEdit(String label) {
		// L'instantané est pris AVANT d'entrer dans le groupe : une fois
		// dedans, `RecordHistory` ne fait plus rien.
		if (m_editDepth == 0)
			RecordHistory(std::move(label));
		++m_editDepth;
	}

	void EndEdit() {
		if (m_editDepth > 0)
			--m_editDepth;
	}

	/// Annule la dernière commande. Faux s'il n'y a rien à annuler.
	bool Undo() {
		if (m_undo.empty())
			return false;
		HistoryStep step = std::move(m_undo.back());
		m_undo.pop_back();
		HistoryStep inverse = Snapshot(step.label);
		if (!RestoreStep(step)) {
			m_undo.push_back(std::move(step)); // scène disparue : on ne perd pas l'étape
			return false;
		}
		m_redo.push_back(std::move(inverse));
		LogInfo(String::Format("Annulé : %s", step.label.CStr()));
		if (onHistoryChanged)
			onHistoryChanged();
		return true;
	}

	/// Rétablit la dernière commande annulée.
	bool Redo() {
		if (m_redo.empty())
			return false;
		HistoryStep step = std::move(m_redo.back());
		m_redo.pop_back();
		HistoryStep inverse = Snapshot(step.label);
		if (!RestoreStep(step)) {
			m_redo.push_back(std::move(step));
			return false;
		}
		m_undo.push_back(std::move(inverse));
		LogInfo(String::Format("Rétabli : %s", step.label.CStr()));
		if (onHistoryChanged)
			onHistoryChanged();
		return true;
	}

	void ClearHistory() {
		m_undo.clear();
		m_redo.clear();
		if (onHistoryChanged)
			onHistoryChanged();
	}

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

	bool SetPosition(scene::NodeId id, const math::FVector3 &position) {
		scene::NodeTree *tree = Tree();
		scene::Node *node = FindObject(id);
		if (!tree || !node)
			return false;
		RecordHistory(String::Format("Déplacer %s", node->name.CStr()));
		(void)tree->SetLocalPosition(id, position);
		return PushTransform(id);
	}
	bool SetPosition(const String &objectName, const math::FVector3 &position) {
		return SetPosition(ResolveId(objectName), position);
	}

	/// Position MONDE : ce que manipulent le manipulateur 3D et les scripts
	/// qui raisonnent en coordonnées de scène. Le local est recalculé à
	/// partir du parent.
	bool SetGlobalPosition(scene::NodeId id, const math::FVector3 &position) {
		scene::NodeTree *tree = Tree();
		scene::Node *node = FindObject(id);
		if (!tree || !node)
			return false;
		RecordHistory(String::Format("Déplacer %s", node->name.CStr()));
		(void)tree->SetGlobalPosition(id, position);
		return PushTransform(id);
	}

	bool SetEulerDegrees(scene::NodeId id, const math::FVector3 &eulerDeg) {
		scene::NodeTree *tree = Tree();
		scene::Node *node = FindObject(id);
		if (!tree || !node)
			return false;
		RecordHistory(String::Format("Tourner %s", node->name.CStr()));
		scene::Transform transform = node->transform;
		transform.SetEulerDegrees(eulerDeg);
		(void)tree->SetLocalTransform(id, transform);
		return PushTransform(id);
	}
	bool SetEulerDegrees(const String &objectName, const math::FVector3 &eulerDeg) {
		return SetEulerDegrees(ResolveId(objectName), eulerDeg);
	}

	bool SetScale(scene::NodeId id, const math::FVector3 &scale) {
		scene::NodeTree *tree = Tree();
		scene::Node *node = FindObject(id);
		if (!tree || !node)
			return false;
		RecordHistory(String::Format("Redimensionner %s", node->name.CStr()));
		// Une échelle nulle produit une matrice singulière (objet invisible et
		// normales dégénérées) : on la borne plutôt que de l'accepter.
		(void)tree->SetLocalScale(id, {sdl3::Max(scale.x, MIN_SCALE), sdl3::Max(scale.y, MIN_SCALE),
									   sdl3::Max(scale.z, MIN_SCALE)});
		return PushTransform(id);
	}
	bool SetScale(const String &objectName, const math::FVector3 &scale) {
		return SetScale(ResolveId(objectName), scale);
	}

	bool SetVisible(scene::NodeId id, bool visible) {
		scene::NodeTree *tree = Tree();
		scene::Node *node = FindObject(id);
		if (!tree || !node)
			return false;
		RecordHistory(String::Format(visible ? "Afficher %s" : "Masquer %s", node->name.CStr()));
		(void)tree->SetVisible(id, visible);
		// Le sous-arbre suit : render3d::Object3D::Traverse saute déjà les
		// sous-arbres invisibles, il suffit donc de poser le nœud lui-même.
		if (render3d::Object3D *object3d = FindNode(id))
			object3d->SetVisible(visible);
		NotifyObjectChanged();
		return true;
	}
	bool SetVisible(const String &objectName, bool visible) { return SetVisible(ResolveId(objectName), visible); }

	bool SetMaterial(scene::NodeId id, const MaterialDesc &material) {
		scene::Node *node = FindObject(id);
		if (!node)
			return false;
		RecordHistory(String::Format("Matériau de %s", node->name.CStr()));
		VisualDesc visual = VisualDesc::Read(*node);
		visual.material = material;
		visual.Write(*node);
		return PushMaterial(id);
	}
	bool SetMaterial(const String &objectName, const MaterialDesc &material) {
		return SetMaterial(ResolveId(objectName), material);
	}

	bool SetMaterialColor(scene::NodeId id, const sdl3::Color &color) {
		scene::Node *node = FindObject(id);
		if (!node)
			return false;
		MaterialDesc material = VisualDesc::Read(*node).material;
		material.baseColor = color;
		return SetMaterial(id, material);
	}
	bool SetMaterialColor(const String &objectName, const sdl3::Color &color) {
		return SetMaterialColor(ResolveId(objectName), color);
	}

	/// Change la forme (et ses dimensions) d'un nœud — reconstruit son
	/// maillage. Un nœud sans apparence en reçoit une.
	/// Change la forme (et le matériau) d'un nœud. Le maillage est remplacé
	/// SUR PLACE quand le nœud est déjà une forme de même nature : un champ
	/// « dimensions » tiré à la souris appelle ceci à chaque mouvement, et
	/// reconstruire toute la scène à chaque fois (des centaines de nœuds
	/// dans le donjon) figeait l'éditeur. La reconstruction complète reste
	/// le chemin d'un nœud qui GAGNE une forme ou change de modèle importé.
	bool SetVisual(scene::NodeId id, const VisualDesc &visual) {
		scene::Node *node = FindObject(id);
		if (!node)
			return false;
		RecordHistory(String::Format("Forme de %s", node->name.CStr()));
		const bool had = VisualDesc::Has(*node);
		const VisualDesc before = VisualDesc::Read(*node);
		visual.Write(*node);
		auto *shape = dynamic_cast<render3d::Shape *>(FindNode(id));
		const bool sameModel = visual.shape != ShapeKind::MODEL ||
							   (before.shape == ShapeKind::MODEL && before.source == visual.source);
		if (had && shape && sameModel) {
			if (visual.shape != ShapeKind::MODEL)
				shape->Geometry() = BuildMesh(visual);
			shape->Materials() = {BuildMaterial(visual.material)};
			NotifyObjectChanged();
			return true;
		}
		RebuildRuntime();
		return true;
	}

	bool SetPhysics(scene::NodeId id, const PhysicsDesc &physics) {
		scene::Node *node = FindObject(id);
		if (!node)
			return false;
		RecordHistory(String::Format("Physique de %s", node->name.CStr()));
		physics.Write(*node);
		return PushPhysics(id);
	}
	bool SetPhysics(const String &objectName, const PhysicsDesc &physics) {
		return SetPhysics(ResolveId(objectName), physics);
	}

	/// Étiquette libre lue par les scripts (« checkpoint », « portal_a »…).
	bool SetTagOf(scene::NodeId id, const String &tag) {
		scene::Node *node = FindObject(id);
		if (!node)
			return false;
		RecordHistory(String::Format("Étiquette de %s", node->name.CStr()));
		SetTag(*node, tag);
		NotifyObjectChanged();
		return true;
	}

	/// Verrouille un nœud : plus sélectionnable au clic ni déplaçable au
	/// manipulateur (le décor d'un niveau qu'on ne veut plus bouger).
	bool SetLocked(scene::NodeId id, bool locked) {
		scene::Node *node = FindObject(id);
		if (!node)
			return false;
		RecordHistory(String::Format(locked ? "Verrouiller %s" : "Déverrouiller %s", node->name.CStr()));
		node->locked = locked;
		if (locked && m_selection == id)
			ClearSelection();
		if (onSceneStructureChanged)
			onSceneStructureChanged();
		return true;
	}

	// ── Composants : lumière, caméra, déclencheur, script ───────────────────

	bool SetLight(scene::NodeId id, const LightDesc &light) {
		scene::Node *node = FindObject(id);
		if (!node)
			return false;
		RecordHistory(String::Format("Lumière de %s", node->name.CStr()));
		light.Write(*node);
		RefreshHelper(id);
		NotifyObjectChanged();
		return true;
	}

	bool SetCameraNode(scene::NodeId id, const CameraNodeDesc &camera) {
		scene::Node *node = FindObject(id);
		if (!node)
			return false;
		RecordHistory(String::Format("Caméra de %s", node->name.CStr()));
		camera.Write(*node);
		RefreshHelper(id);
		NotifyObjectChanged();
		return true;
	}

	bool SetTrigger(scene::NodeId id, const TriggerDesc &trigger) {
		scene::Node *node = FindObject(id);
		if (!node)
			return false;
		RecordHistory(String::Format("Déclencheur de %s", node->name.CStr()));
		trigger.Write(*node);
		RefreshHelper(id);
		NotifyObjectChanged();
		return true;
	}

	/// Attache (ou, avec un nom vide, détache) un script de la bibliothèque.
	bool SetScriptRef(scene::NodeId id, const String &scriptName) {
		scene::Node *node = FindObject(id);
		if (!node)
			return false;
		RecordHistory(String::Format("Script de %s", node->name.CStr()));
		if (scriptName.IsEmpty())
			ScriptRef::Clear(*node);
		else
			ScriptRef{scriptName}.Write(*node);
		NotifyObjectChanged();
		return true;
	}

	/// Types de composants qu'on peut AJOUTER depuis l'inspecteur, dans
	/// l'ordre du menu.
	[[nodiscard]] static std::vector<String> AddableComponents() {
		return {String(component::VISUAL), String(component::BODY), String(component::LIGHT),
				String(component::CAMERA), String(component::TRIGGER), String(component::SCRIPT)};
	}

	/// Ajoute un composant avec ses valeurs par défaut (sans effet s'il est
	/// déjà là). Rend faux si le nœud n'existe pas ou le type est inconnu.
	bool AddComponentOfType(scene::NodeId id, const String &type) {
		scene::Node *node = FindObject(id);
		if (!node)
			return false;
		if (node->HasComponent(type))
			return true;
		RecordHistory(String::Format("Ajouter %s à %s", type.CStr(), node->name.CStr()));
		if (type == component::VISUAL) {
			VisualDesc{}.Write(*node);
			RebuildRuntime();
		} else if (type == component::BODY) {
			PhysicsDesc body;
			body.body = BodyKind::STATIC;
			body.Write(*node);
			(void)PushPhysics(id);
		} else if (type == component::LIGHT) {
			LightDesc{}.Write(*node);
			RefreshHelper(id);
		} else if (type == component::CAMERA) {
			CameraNodeDesc{}.Write(*node);
			RefreshHelper(id);
		} else if (type == component::TRIGGER) {
			TriggerDesc{}.Write(*node);
			RefreshHelper(id);
		} else if (type == component::SCRIPT) {
			ScriptRef{m_project.scripts.empty() ? String() : m_project.scripts.front().name}.Write(*node);
		} else {
			DropLastHistory();
			return false;
		}
		NotifyObjectChanged();
		return true;
	}

	bool RemoveComponentOfType(scene::NodeId id, const String &type) {
		scene::Node *node = FindObject(id);
		if (!node || !node->HasComponent(type))
			return false;
		RecordHistory(String::Format("Retirer %s de %s", type.CStr(), node->name.CStr()));
		(void)node->RemoveComponent(type);
		if (type == component::VISUAL)
			RebuildRuntime(); // l'Object3D change de nature (forme → pivot)
		else if (type == component::BODY)
			(void)PushPhysics(id);
		else
			RefreshHelper(id);
		NotifyObjectChanged();
		return true;
	}

	/// Remplace TOUTES les propriétés d'un composant — le chemin de l'édition
	/// « en JSON » d'un composant, et celui des propriétés libres du nœud
	/// (`type` vide).
	bool SetComponentProps(scene::NodeId id, const String &type, scene::PropertyMap props) {
		scene::Node *node = FindObject(id);
		if (!node)
			return false;
		RecordHistory(String::Format("Modifier %s de %s", type.IsEmpty() ? "les propriétés" : type.CStr(),
									 node->name.CStr()));
		if (type.IsEmpty()) {
			node->properties = std::move(props);
		} else {
			scene::Component component;
			component.type = type;
			component.props = std::move(props);
			node->SetComponent(std::move(component));
		}
		if (type == component::VISUAL)
			RebuildRuntime();
		else if (type == component::BODY)
			(void)PushPhysics(id);
		else
			RefreshHelper(id);
		NotifyObjectChanged();
		return true;
	}

	// ── Bibliothèque de scripts ──────────────────────────────────────────────

	/// Ajoute un script (nom rendu unique) ; rend le nom retenu.
	String AddScript(const String &baseName, const String &source, const String &description = String()) {
		String name = baseName.IsEmpty() ? String("script") : baseName;
		if (m_project.FindScript(name)) {
			for (int suffix = 2; suffix < 10000; ++suffix) {
				String candidate = String::Format("%s_%d", name.CStr(), suffix);
				if (!m_project.FindScript(candidate)) {
					name = candidate;
					break;
				}
			}
		}
		m_project.scripts.push_back(ScriptAsset{name, description, source});
		LogInfo(String::Format("Script créé : %s", name.CStr()));
		return name;
	}

	/// Remplace la source d'un script de la bibliothèque. La compilation est
	/// vérifiée et l'erreur éventuelle RENDUE (le texte est enregistré quand
	/// même : on ne perd pas un travail en cours pour une faute de frappe).
	Option<data::script::ScriptError> SetScriptSource(const String &scriptName, const String &source) {
		ScriptAsset *asset = m_project.FindScript(scriptName);
		if (!asset)
			return Some(data::script::ScriptError(String("script introuvable"), 0, 0));
		asset->source = source;
		return CheckScript(source);
	}

	/// Remplace le script de JEU d'une scène (même contrat que ci-dessus).
	Option<data::script::ScriptError> SetGameplayScript(const String &sceneName, const String &source) {
		SceneDesc *scene = m_project.FindScene(sceneName);
		if (!scene)
			return Some(data::script::ScriptError(String("scène introuvable"), 0, 0));
		scene->gameplayScript = source;
		return CheckScript(source);
	}

	/// Compile sans exécuter : la première erreur de syntaxe, ou NONE.
	[[nodiscard]] static Option<data::script::ScriptError> CheckScript(const String &source) {
		auto compiled = data::script::Parser::Compile(source.View());
		if (compiled.IsError())
			return Some(compiled.Error());
		return NONE;
	}

	/// Scripts de la partie en cours (ou, hors mode Jeu, ceux que la scène
	/// chargerait) — la superposition du mode Jeu et le rapport les listent.
	[[nodiscard]] std::vector<ScriptStatus> ScriptStatuses() const {
		std::vector<ScriptStatus> out;
		const SceneDesc *scene = ActiveScene();
		if (!scene)
			return out;
		if (!scene->gameplayScript.IsEmpty()) {
			ScriptStatus main;
			main.name = String::Format("%s.main", scene->name.CStr());
			main.role = String("scène");
			main.running = m_playing && m_gameplayReady;
			out.push_back(std::move(main));
		}
		if (m_playing) {
			for (const NodeScriptInstance &instance : m_nodeScripts)
				out.push_back(ScriptStatus{instance.script, String("nœud"), instance.nodes.size(), instance.ready,
										   instance.error});
			return out;
		}
		scene->tree.Traverse(scene->tree.Root(), [&](scene::NodeId, const scene::Node &node) {
			if (!ScriptRef::Has(node))
				return;
			const String name = ScriptRef::Read(node).script;
			for (ScriptStatus &status : out) {
				if (status.role == "nœud" && status.name == name) {
					++status.nodes;
					return;
				}
			}
			out.push_back(ScriptStatus{name, String("nœud"), 1, false, String()});
		});
		return out;
	}

	// ── Lumières (lecture, pour l'interface et les tests) ───────────────────

	[[nodiscard]] const std::vector<render3d::PointLight> &PointLights() const noexcept { return m_pointLights; }
	[[nodiscard]] const std::vector<render3d::SpotLight> &SpotLights() const noexcept { return m_spotLights; }
	[[nodiscard]] size_t TriggerCount() const noexcept { return m_triggerCount; }
	[[nodiscard]] scene::NodeId CurrentCamera() const noexcept { return m_currentCamera; }
	/// Le repère d'édition d'un nœud existe-t-il (et est-il visible) ?
	[[nodiscard]] bool HasVisibleHelper(scene::NodeId id) const {
		auto it = m_helperOf.find(id);
		return it != m_helperOf.end() && it->second->IsVisible();
	}

	/// Mouvement relatif de la souris (mode Jeu en plein écran) : accumulé
	/// par l'hôte, lu par `input.mouse_delta()`, remis à zéro chaque image.
	void AddMouseDelta(float dx, float dy) noexcept {
		m_mouseDelta.x += dx;
		m_mouseDelta.y += dy;
	}

	// ── Commandes HIÉRARCHIQUES ──────────────────────────────────────────────

	/// Ajoute un objet (nom rendu unique) et l'instancie. Rend le nom
	/// effectivement attribué, ou NONE s'il n'y a pas de scène active.
	[[nodiscard]] Option<String> SpawnObject(ObjectDesc object) {
		Option<scene::NodeId> id = SpawnNode(std::move(object));
		if (id.IsNone())
			return NONE;
		const scene::Node *node = FindObject(id.Unwrap());
		if (!node)
			return NONE;
		return Some(node->name);
	}

	/// Même chose, mais rend l'IDENTIFIANT — ce dont l'interface a besoin
	/// pour enchaîner (sélectionner, reparenter, renommer).
	[[nodiscard]] Option<scene::NodeId> SpawnNode(ObjectDesc object, scene::NodeId parent = scene::NodeId{}) {
		SceneDesc *sceneDesc = ActiveScene();
		if (!sceneDesc)
			return NONE;
		RecordHistory(String::Format("Ajouter %s", object.name.CStr()));
		scene::NodeId parentId = parent.Valid() ? parent : sceneDesc->Resolve(object.parent);
		if (!parentId.Valid())
			parentId = sceneDesc->tree.Root();
		object.name = sceneDesc->UniqueName(object.name.IsEmpty() ? String("Object") : object.name);
		scene::NodeId id = sceneDesc->tree.Add(parentId, object.ToNode());
		if (!id.Valid())
			return NONE;
		InstantiateNode(id, *sceneDesc->tree.Get(id));
		ApplyParentLinks(*sceneDesc);
		render3d::SceneSyncSystem::Sync(m_registry, m_sceneRoot);
		if (onSceneStructureChanged)
			onSceneStructureChanged();
		return Some(id);
	}

	/// Crée un nœud VIDE (groupe) — la brique de la composition : on crée un
	/// « Wheels », puis on y glisse les roues.
	[[nodiscard]] Option<scene::NodeId> CreateGroup(const String &name, scene::NodeId parent = scene::NodeId{}) {
		ObjectDesc group = ObjectDesc::Group(name);
		return SpawnNode(std::move(group), parent);
	}

	/// Déplace un nœud (et son sous-arbre) sous un autre parent. Refusé si
	/// cela créerait un cycle — cf. scene::NodeTree::Reparent.
	bool ReparentNode(scene::NodeId child, scene::NodeId newParent,
					  scene::ReparentMode mode = scene::ReparentMode::KEEP_GLOBAL, size_t index = scene::NODE_APPEND) {
		SceneDesc *sceneDesc = ActiveScene();
		scene::Node *node = FindObject(child);
		if (!sceneDesc || !node)
			return false;
		const scene::Node *parentNode = sceneDesc->tree.Get(newParent);
		RecordHistory(String::Format("Reparenter %s", node->name.CStr()));
		if (!sceneDesc->tree.Reparent(child, newParent, mode, index)) {
			// Refus : on retire l'instantané qu'on venait d'empiler pour ne
			// pas laisser une étape d'annulation qui ne défait rien.
			DropLastHistory();
			LogWarning(String::Format("Impossible de mettre « %s » sous « %s » (cela créerait une boucle)",
									  node->name.CStr(), parentNode ? parentNode->name.CStr() : "?"));
			return false;
		}
		RebuildRuntime();
		(void)Select(child);
		if (onSceneStructureChanged)
			onSceneStructureChanged();
		return true;
	}

	/// Réordonne un nœud parmi ses frères (glisser-déposer dans l'outliner).
	bool MoveNodeInParent(scene::NodeId child, size_t index) {
		SceneDesc *sceneDesc = ActiveScene();
		scene::Node *node = FindObject(child);
		if (!sceneDesc || !node)
			return false;
		RecordHistory(String::Format("Réordonner %s", node->name.CStr()));
		if (!sceneDesc->tree.MoveChild(child, index)) {
			DropLastHistory();
			return false;
		}
		if (onSceneStructureChanged)
			onSceneStructureChanged();
		return true;
	}

	/// Duplique un nœud ET son sous-arbre (identifiants neufs, références
	/// internes re-câblées — cf. scene::NodeTree::Duplicate).
	[[nodiscard]] Option<scene::NodeId> DuplicateNode(scene::NodeId id) {
		SceneDesc *sceneDesc = ActiveScene();
		scene::Node *node = FindObject(id);
		if (!sceneDesc || !node)
			return NONE;
		RecordHistory(String::Format("Dupliquer %s", node->name.CStr()));
		scene::NodeId copy = sceneDesc->tree.Duplicate(id);
		if (!copy.Valid()) {
			DropLastHistory();
			return NONE;
		}
		// Seule la RACINE de la copie est renommée. Les nœuds INTERNES gardent
		// leur nom : « Voiture 2 > Roues > AvantGauche » se lit, et un script
		// y accède par chemin exactement comme dans l'original. Les renommer
		// tous donnerait « Roue 7 » au milieu d'une copie, sans rien résoudre
		// — l'unicité entre frères, elle, est acquise par construction.
		if (scene::Node *root = sceneDesc->tree.Get(copy)) {
			String base = root->name;
			root->name = String(); // libère son propre nom avant d'en chercher un libre
			root->name = sceneDesc->UniqueName(base);
		}
		RebuildRuntime();
		(void)Select(copy);
		if (onSceneStructureChanged)
			onSceneStructureChanged();
		return Some(copy);
	}

	/// Supprime un nœud ET son sous-arbre.
	bool RemoveNode(scene::NodeId id) {
		SceneDesc *sceneDesc = ActiveScene();
		scene::Node *node = FindObject(id);
		if (!sceneDesc || !node || id == sceneDesc->tree.Root())
			return false;
		RecordHistory(String::Format("Supprimer %s", node->name.CStr()));
		const bool wasSelected = m_selection.Valid() && sceneDesc->tree.IsAncestorOf(id, m_selection);
		if (!sceneDesc->tree.Remove(id)) {
			DropLastHistory();
			return false;
		}
		RebuildRuntime();
		if (wasSelected)
			ClearSelection();
		if (onSceneStructureChanged)
			onSceneStructureChanged();
		return true;
	}

	bool RemoveObject(const String &objectName) { return RemoveNode(ResolveId(objectName)); }

	bool RenameObject(scene::NodeId id, const String &newName) {
		SceneDesc *sceneDesc = ActiveScene();
		scene::Node *node = FindObject(id);
		if (!sceneDesc || !node || newName.IsEmpty())
			return false;
		if (scene::NodeId existing = sceneDesc->FindId(newName); existing.Valid() && existing != id)
			return false; // nom déjà pris : refus explicite plutôt que renommage surprise
		RecordHistory(String::Format("Renommer %s", node->name.CStr()));
		node->name = newName;
		if (render3d::Object3D *object3d = FindNode(id))
			object3d->SetName(newName);
		if (onSceneStructureChanged)
			onSceneStructureChanged();
		return true;
	}

	bool RenameObject(const String &objectName, const String &newName) {
		return RenameObject(ResolveId(objectName), newName);
	}

	// ── Scènes réutilisables (PackedScene) ───────────────────────────────────
	//
	// Emballer un sous-arbre dans un fichier, puis le réinstancier autant de
	// fois qu'on veut : c'est ce qui transforme « une voiture » en « le
	// modèle de voiture » dont un circuit pose trois exemplaires. Le format
	// est celui de la bibliothèque (scene::PackedScene), pas un format
	// propre à l'éditeur.

	/// Écrit le sous-arbre `id` dans un fichier `.tscene`.
	[[nodiscard]] Result<bool, String> SavePackedScene(scene::NodeId id, const String &path) const {
		const scene::NodeTree *tree = Tree();
		if (!tree || !tree->Contains(id))
			return Err(String("objet introuvable"));
		scene::PackedScene packed = scene::PackedScene::FromSubtree(*tree, id);
		packed.SetSource(path);
		String text = packed.EncodeJson();
		if (!sdl3::WriteFile(path, text.CStr(), text.GetSize()))
			return Err(String::Format("écriture impossible : %s", path.CStr()));
		return Ok(true);
	}

	/// Instancie un fichier `.tscene` sous `parent` (la racine à défaut).
	/// Chaque instance est une COPIE indépendante, avec des identifiants
	/// neufs et ses références internes re-câblées ; sa racine retient d'où
	/// elle vient (cf. scene::PackedScene).
	[[nodiscard]] Result<scene::NodeId, String> InstantiateSceneFile(const String &path,
																	 scene::NodeId parent = scene::NodeId{}) {
		SceneDesc *sceneDesc = ActiveScene();
		if (!sceneDesc)
			return Err(String("aucune scène active"));
		auto text = data::script::LoadScriptFile(path);
		if (text.IsError())
			return Err(text.Error());
		auto packed = scene::PackedScene::DecodeJson(text.Value());
		if (packed.IsError())
			return Err(packed.Error());
		packed.Value().SetSource(path);

		RecordHistory(String::Format("Instancier %s", FileStem(path).CStr()));
		scene::NodeId parentId = parent.Valid() ? parent : sceneDesc->tree.Root();
		scene::NodeId created = packed.Value().InstantiateInto(sceneDesc->tree, parentId);
		if (!created.Valid()) {
			DropLastHistory();
			return Err(String("instanciation impossible"));
		}
		// Même règle que la duplication : seule la racine de l'instance est
		// renommée, son contenu garde les noms du fichier de scène.
		if (scene::Node *root = sceneDesc->tree.Get(created)) {
			String base = root->name;
			root->name = String();
			root->name = sceneDesc->UniqueName(base);
		}
		RebuildRuntime();
		if (onSceneStructureChanged)
			onSceneStructureChanged();
		return Ok(created);
	}

	// ── Physique pilotée par script ──────────────────────────────────────────

	[[nodiscard]] Option<math::FVector3> GetVelocity(const String &objectName) {
		return GetVelocity(ResolveId(objectName));
	}

	[[nodiscard]] Option<math::FVector3> GetVelocity(scene::NodeId id) {
		Option<ecs::Entity> entity = FindEntity(id);
		if (entity.IsNone())
			return NONE;
		auto body = m_registry.GetComponent<physics::RigidBody>(entity.Unwrap());
		if (body.IsNone())
			return NONE;
		return Some(body.Unwrap()->linearVelocity);
	}

	bool SetVelocity(const String &objectName, const math::FVector3 &velocity) {
		Option<ecs::Entity> entity = FindEntity(objectName);
		if (entity.IsNone())
			return false;
		auto body = m_registry.GetComponent<physics::RigidBody>(entity.Unwrap());
		if (body.IsNone())
			return false;
		body.Unwrap()->linearVelocity = velocity;
		return true;
	}

	bool ApplyImpulse(const String &objectName, const math::FVector3 &impulse) {
		Option<ecs::Entity> entity = FindEntity(objectName);
		if (entity.IsNone())
			return false;
		auto body = m_registry.GetComponent<physics::RigidBody>(entity.Unwrap());
		if (body.IsNone() || body.Unwrap()->invMass <= 0.f)
			return false;
		body.Unwrap()->linearVelocity += impulse * body.Unwrap()->invMass;
		return true;
	}

	// ── Caméra ───────────────────────────────────────────────────────────────

	void ApplyLookDelta(float deltaYaw, float deltaPitch) {
		float &yaw = m_playing ? m_playYaw : m_editYaw;
		float &pitch = m_playing ? m_playPitch : m_editPitch;
		yaw += deltaYaw;
		pitch = sdl3::Clamp(pitch + deltaPitch, -1.5f, 1.5f);
	}

	/// Déplacement de la caméra libre, en unités locales : `z` vers l'avant,
	/// `x` vers la droite, `y` vers le HAUT DU MONDE (vol d'éditeur : monter
	/// reste monter, même en regardant vers le sol).
	void MoveCamera(const math::FVector3 &localDelta) {
		render3d::Camera &camera = ActiveCamera();
		const math::FVector3 forward = CameraForward();
		const math::FVector3 right = CameraRight();
		camera.position += forward * localDelta.z + right * localDelta.x + math::FVector3{0.f, localDelta.y, 0.f};
		RefreshCameraTargets();
	}

	/// Panoramique : glisse la caméra DANS SON PLAN D'ÉCRAN (droite et haut
	/// de la caméra, pas du monde) — le geste du clic milieu de tout éditeur
	/// 3D. `right`/`up` sont en unités monde ; les multiplier par
	/// `PivotDistance()` donne un geste qui « colle » au décor quelle que
	/// soit l'échelle de la scène.
	void PanCamera(float right, float up) {
		render3d::Camera &camera = ActiveCamera();
		camera.position += CameraRight() * right + CameraUp() * up;
		RefreshCameraTargets();
	}

	/// Avance ou recule le long de l'axe de visée (molette). Un pas positif
	/// rapproche du décor.
	void DollyCamera(float distance) {
		render3d::Camera &camera = ActiveCamera();
		camera.position += CameraForward() * distance;
		RefreshCameraTargets();
	}

	/// Tourne AUTOUR d'un point : la distance au pivot est conservée et la
	/// caméra continue de le regarder. C'est ce qui permet d'examiner un
	/// objet sous toutes ses faces sans le perdre de vue.
	void OrbitCamera(float deltaYaw, float deltaPitch, const math::FVector3 &pivot) {
		float &yaw = m_playing ? m_playYaw : m_editYaw;
		float &pitch = m_playing ? m_playPitch : m_editPitch;
		render3d::Camera &camera = ActiveCamera();
		const float distance = sdl3::Max((camera.position - pivot).Length(), 0.01f);
		yaw += deltaYaw;
		// Bornes identiques à celles du regard libre : au-delà, la caméra
		// passerait la verticale et la vue se retournerait.
		pitch = sdl3::Clamp(pitch + deltaPitch, -1.5f, 1.5f);
		camera.position = pivot - ForwardFromYawPitch(yaw, pitch) * distance;
		RefreshCameraTargets();
	}

	/// Point autour duquel tourner et échelle des gestes : l'objet
	/// sélectionné s'il y en a un, sinon un point droit devant la caméra.
	[[nodiscard]] math::FVector3 CameraPivot() const {
		if (Option<math::FVector3> center = SelectionWorldPosition(); center.IsSome())
			return center.Unwrap();
		return ActiveCamera().position + CameraForward() * DEFAULT_PIVOT_DISTANCE;
	}

	/// Distance au pivot : sert d'échelle aux gestes (panoramique et molette
	/// avancent d'autant plus vite que la scène est loin).
	[[nodiscard]] float PivotDistance() const {
		return sdl3::Clamp((ActiveCamera().position - CameraPivot()).Length(), 0.5f, 400.f);
	}

	/// Vecteurs de base de la caméra courante.
	[[nodiscard]] math::FVector3 CameraForward() const {
		return ForwardFromYawPitch(m_playing ? m_playYaw : m_editYaw, m_playing ? m_playPitch : m_editPitch);
	}
	[[nodiscard]] math::FVector3 CameraRight() const {
		return CameraForward().Cross({0.f, 1.f, 0.f}).Normalize();
	}
	[[nodiscard]] math::FVector3 CameraUp() const { return CameraRight().Cross(CameraForward()).Normalize(); }

	void SetCameraPosition(const math::FVector3 &position) {
		ActiveCamera().position = position;
		RefreshCameraTargets();
	}

	/// Place la caméra d'édition de façon à cadrer `objectName` (« frame
	/// selected », raccourci F dans tous les éditeurs 3D).
	bool FocusOn(scene::NodeId id) {
		const scene::NodeTree *tree = Tree();
		const scene::Node *node = tree ? tree->Get(id) : nullptr;
		if (!node)
			return false;
		// Rayon apparent : les dimensions du maillage, mises à l'échelle
		// MONDE (celle du nœud multipliée par celles de tous ses parents) —
		// cadrer un objet dans une voiture agrandie doit tenir compte de
		// l'agrandissement.
		const VisualDesc visual = VisualDesc::Read(*node);
		const scene::Transform world = tree->GlobalTransform(id);
		float radius = sdl3::Max(sdl3::Max(visual.dimensions.x, visual.dimensions.y), visual.dimensions.z);
		radius = sdl3::Max(radius * sdl3::Max(sdl3::Abs(world.scale.x), sdl3::Abs(world.scale.y)), 1.f);
		math::FVector3 forward = ForwardFromYawPitch(m_editYaw, m_editPitch);
		m_editCamera.position = world.position - forward * (radius * 4.f);
		m_editCamera.target = world.position;
		return true;
	}

	bool FocusOn(const String &objectName) { return FocusOn(ResolveId(objectName)); }

	void SetViewportAspect(float aspect) {
		if (aspect > 0.01f) {
			m_editCamera.aspect = aspect;
			m_playCamera.aspect = aspect;
		}
	}

	// ── Scripts ──────────────────────────────────────────────────────────────

	/// Exécute une source dans l'interpréteur « outil » (scénarios, console
	/// de l'éditeur) — celui qui pilote l'ÉDITEUR, par opposition à
	/// l'interpréteur de gameplay qui pilote la PARTIE.
	[[nodiscard]] Result<data::script::Value, data::script::ScriptError> RunToolScript(const String &source) {
		++m_scriptRunCount;
		return m_toolVm.Run(source.View());
	}

	/// Appelle un rappel du script outil s'il existe (`on_frame`…).
	void CallToolHook(const String &hook, std::vector<data::script::Value> args) {
		auto called = m_toolVm.CallGlobalIfPresent(hook, std::move(args));
		if (called.IsNone())
			return;
		++m_scriptCallCount;
		auto result = called.Unwrap();
		if (result.IsError()) {
			++m_scriptErrorCount;
			LogError(String::Format("Scénario `%s` : %s", hook.CStr(), result.Error().Format().CStr()));
		}
	}

	[[nodiscard]] size_t ScriptRunCount() const noexcept { return m_scriptRunCount; }
	[[nodiscard]] size_t ScriptCallCount() const noexcept { return m_scriptCallCount; }
	[[nodiscard]] size_t ScriptErrorCount() const noexcept { return m_scriptErrorCount; }
	[[nodiscard]] const std::vector<String> &LoadedScripts() const noexcept { return m_loadedScripts; }

	/// Valeur d'une globale du script de jeu — le rapport y lit les
	/// compteurs de la partie (tours bouclés, chronos…).
	[[nodiscard]] Option<data::script::Value> GameplayGlobal(const String &name) {
		return m_gameplayVm.GetGlobal(name);
	}

	// ── Métriques ────────────────────────────────────────────────────────────

	/// Vivier de tâches partagé par la simulation (et disponible pour tout
	/// travail parallèle de l'éditeur).
	[[nodiscard]] jobs::JobSystem &Jobs() noexcept { return m_jobs; }
	[[nodiscard]] const jobs::JobSystem &Jobs() const noexcept { return m_jobs; }

	[[nodiscard]] size_t RuntimeObjectCount() const { return m_registry.EntitiesWith<SceneObjectRef>().size(); }
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
	[[nodiscard]] static scene::NodeTypeRegistry MakeTypeRegistry() {
		scene::NodeTypeRegistry registry = scene::NodeTypeRegistry::WithBuiltins();

		scene::NodeTypeInfo mesh;
		mesh.name = String(node_kind::MESH);
		mesh.label = String("Objet visible");
		mesh.category = String("3D");
		mesh.icon = String("▣");
		registry.Register(std::move(mesh));

		scene::NodeTypeInfo group;
		group.name = String(node_kind::GROUP);
		group.label = String("Groupe");
		group.category = String("Structure");
		group.icon = String("▤");
		registry.Register(std::move(group));

		scene::NodeTypeInfo body;
		body.name = String(node_kind::BODY);
		body.label = String("Corps physique");
		body.category = String("Physique");
		body.icon = String("◉");
		registry.Register(std::move(body));

		scene::NodeTypeInfo spawn;
		spawn.name = String(node_kind::SPAWN);
		spawn.label = String("Point d'apparition");
		spawn.category = String("Repères");
		spawn.icon = String("✛");
		spawn.acceptsChildren = false;
		registry.Register(std::move(spawn));

		scene::NodeTypeInfo folder;
		folder.name = String(node_kind::FOLDER);
		folder.label = String("Dossier");
		folder.category = String("Structure");
		folder.icon = String("▤");
		registry.Register(std::move(folder));

		scene::NodeTypeInfo light;
		light.name = String(node_kind::LIGHT);
		light.label = String("Lumière");
		light.category = String("Lumière");
		light.icon = String("☀");
		registry.Register(std::move(light));

		scene::NodeTypeInfo camera;
		camera.name = String(node_kind::CAMERA);
		camera.label = String("Caméra");
		camera.category = String("3D");
		camera.icon = String("◰");
		registry.Register(std::move(camera));

		scene::NodeTypeInfo trigger;
		trigger.name = String(node_kind::TRIGGER);
		trigger.label = String("Déclencheur");
		trigger.category = String("Logique");
		trigger.icon = String("⬚");
		registry.Register(std::move(trigger));
		return registry;
	}

	// ── Manipulateur : mathématiques et nœuds ────────────────────────────────

	/// Accès CONST au nœud sélectionné — par identifiant, directement (passer
	/// par le nom ferait un aller-retour inutile… et une récursion infinie
	/// avec SelectedName, qui s'appuie sur cette fonction).
	[[nodiscard]] const scene::Node *SelectedConstObject() const {
		const scene::NodeTree *tree = Tree();
		return tree ? tree->Get(m_selection) : nullptr;
	}

	[[nodiscard]] static math::FVector3 AxisVector(GizmoAxis axis) noexcept {
		switch (axis) {
			case GizmoAxis::X:
				return {1.f, 0.f, 0.f};
			case GizmoAxis::Y:
				return {0.f, 1.f, 0.f};
			case GizmoAxis::Z:
				return {0.f, 0.f, 1.f};
			case GizmoAxis::NONE:
				break;
		}
		return {0.f, 0.f, 0.f};
	}

	[[nodiscard]] String GizmoLabel(GizmoAxis axis) const {
		const char *action = m_gizmoMode == GizmoMode::TRANSLATE  ? "Déplacer"
							 : m_gizmoMode == GizmoMode::ROTATE ? "Tourner"
																: "Redimensionner";
		const char *letter = axis == GizmoAxis::X ? "X" : (axis == GizmoAxis::Y ? "Y" : "Z");
		const scene::NodeTree *tree = Tree();
		const scene::Node *node = tree ? tree->Get(m_dragObject) : nullptr;
		return String::Format("%s %s (%s)", action, node ? node->name.CStr() : "?", letter);
	}

	/// Paramètre, le long de la droite (`origin`, `axis`), du point le plus
	/// proche de `ray` — la prise du manipulateur. NONE quand le rayon est
	/// parallèle à l'axe (le geste n'a alors aucun sens).
	[[nodiscard]] static Option<float> ClosestParamOnAxis(const math::FRay &ray, const math::FVector3 &origin,
														  const math::FVector3 &axis) noexcept {
		const math::FVector3 w = origin - ray.origin;
		const float a = axis.Dot(axis);          // 1 : axes unitaires
		const float b = axis.Dot(ray.direction); // cos de l'angle entre les droites
		const float denominator = a - b * b;
		if (sdl3::Abs(denominator) < 1e-6f)
			return NONE;
		const float d = axis.Dot(w);
		const float e = ray.direction.Dot(w);
		return Some((b * e - d) / denominator);
	}

	/// Intersection du rayon avec le plan (`point`, `normal`).
	[[nodiscard]] static Option<math::FVector3> IntersectPlane(const math::FRay &ray, const math::FVector3 &point,
															   const math::FVector3 &normal) noexcept {
		const float denominator = normal.Dot(ray.direction);
		if (sdl3::Abs(denominator) < 1e-6f)
			return NONE; // rayon rasant : l'impact partirait à l'infini
		const float t = normal.Dot(point - ray.origin) / denominator;
		if (t < 0.f)
			return NONE;
		return Some(ray.At(t));
	}

	[[nodiscard]] static float SnapTo(float value, float step) noexcept {
		return step > 0.f ? sdl3::Round(value / step) * step : value;
	}

	/// Construit les nœuds du manipulateur (une fois) : trois flèches, trois
	/// anneaux, trois poignées d'échelle. Ils vivent DANS le graphe de scène
	/// mais PAS dans le document : ils ne sont ni enregistrés, ni listés dans
	/// l'outliner, ni sélectionnables.
	void EnsureGizmoNodes() {
		if (m_gizmoRoot)
			return;
		m_gizmoRoot = &m_sceneRoot.Add(std::make_unique<render3d::Object3D>());
		m_gizmoRoot->SetName(String("__gizmo"));

		auto material = [](sdl3::Color color) {
			render3d::Material material;
			material.baseColor = color;
			// Non éclairé : un manipulateur doit garder la même couleur quelle
			// que soit l'orientation du soleil, sinon l'axe dans l'ombre
			// devient noir et illisible.
			material.lit = false;
			// Dessiné PAR-DESSUS la scène : on attrape un axe même quand il
			// est à l'intérieur de l'objet qu'il manipule (le nœud du
			// manipulateur est ajouté en dernier, donc dessiné en dernier).
			material.depthTest = false;
			return material;
		};
		struct AxisSetup {
			GizmoAxis axis;
			sdl3::Color color;
			math::FQuaternion rotation;
		};
		// Les primitives sont bâties le long de +Y : chaque axe n'est donc
		// qu'une rotation de ce modèle.
		const AxisSetup setups[] = {
			{GizmoAxis::X, sdl3::Color{220, 70, 70, 255},
			 math::FQuaternion::FromAxisAngle({0.f, 0.f, 1.f}, -1.57079633f)},
			{GizmoAxis::Y, sdl3::Color{90, 210, 90, 255}, math::FQuaternion::Identity()},
			{GizmoAxis::Z, sdl3::Color{80, 130, 235, 255},
			 math::FQuaternion::FromAxisAngle({1.f, 0.f, 0.f}, 1.57079633f)},
		};

		for (const AxisSetup &setup : setups) {
			// Déplacement : tige + pointe.
			auto &arrow = m_gizmoRoot->Add(std::make_unique<render3d::Object3D>());
			arrow.SetRotation(setup.rotation);
			auto &shaft = static_cast<render3d::Shape &>(
				arrow.Add(std::make_unique<render3d::Shape>(render3d::Mesh::Cylinder(0.02f, 0.02f, 0.8f, 12),
														   material(setup.color))));
			shaft.SetPosition({0.f, 0.4f, 0.f});
			auto &head = static_cast<render3d::Shape &>(
				arrow.Add(std::make_unique<render3d::Shape>(render3d::Mesh::Cone(0.07f, 0.22f, 14), material(setup.color))));
			head.SetPosition({0.f, 0.9f, 0.f});
			m_gizmoParts.push_back({GizmoMode::TRANSLATE, setup.axis, &arrow});

			// Rotation : un anneau dans le plan perpendiculaire à l'axe.
			auto &ring = m_gizmoRoot->Add(std::make_unique<render3d::Object3D>());
			ring.SetRotation(setup.rotation);
			auto &torus = static_cast<render3d::Shape &>(ring.Add(std::make_unique<render3d::Shape>(
				render3d::Mesh::Torus(1.f, 0.022f, 10, 48), material(setup.color))));
			// Le tore de Mesh:: est dans le plan XY : on le couche pour qu'il
			// entoure l'axe +Y du modèle.
			torus.SetRotation(math::FQuaternion::FromAxisAngle({1.f, 0.f, 0.f}, 1.57079633f));
			m_gizmoParts.push_back({GizmoMode::ROTATE, setup.axis, &ring});

			// Échelle : tige + cube.
			auto &handle = m_gizmoRoot->Add(std::make_unique<render3d::Object3D>());
			handle.SetRotation(setup.rotation);
			auto &stem = static_cast<render3d::Shape &>(
				handle.Add(std::make_unique<render3d::Shape>(render3d::Mesh::Cylinder(0.02f, 0.02f, 0.85f, 12),
															material(setup.color))));
			stem.SetPosition({0.f, 0.425f, 0.f});
			auto &cube = static_cast<render3d::Shape &>(
				handle.Add(std::make_unique<render3d::Shape>(render3d::Mesh::Cube(0.13f), material(setup.color))));
			cube.SetPosition({0.f, 0.92f, 0.f});
			m_gizmoParts.push_back({GizmoMode::SCALE, setup.axis, &handle});
		}
	}

	// ── Historique ───────────────────────────────────────────────────────────

	/// Prévient l'interface qu'une propriété vient de changer (cf.
	/// `onObjectChanged`).
	void NotifyObjectChanged() {
		if (!m_playing && onObjectChanged)
			onObjectChanged();
	}

	[[nodiscard]] HistoryStep Snapshot(String label) const {
		HistoryStep step;
		step.label = std::move(label);
		if (const SceneDesc *scene = ActiveScene()) {
			step.sceneName = scene->name;
			step.tree = scene->tree;
		}
		step.selection = m_selection;
		return step;
	}

	/// Empile l'état courant avant une mutation. Sans effet pendant le mode
	/// Jeu (les scripts de gameplay et la physique écrivent dans le document
	/// à chaque image), pendant une restauration, et à l'intérieur d'un
	/// groupe ouvert par `BeginEdit` (seule la première commande compte).
	void RecordHistory(String label) {
		if (m_playing || m_restoring || m_editDepth > 0)
			return;
		m_undo.push_back(Snapshot(std::move(label)));
		if (m_undo.size() > MAX_HISTORY)
			m_undo.erase(m_undo.begin());
		m_redo.clear(); // une nouvelle branche d'édition abandonne l'ancienne
		if (onHistoryChanged)
			onHistoryChanged();
	}

	/// Retire le dernier instantané empilé : utilisé quand la commande qui
	/// venait de l'empiler ÉCHOUE (reparentage refusé, duplication
	/// impossible) — laisser une étape d'annulation qui ne défait rien
	/// désoriente plus qu'elle n'aide.
	void DropLastHistory() {
		if (!m_undo.empty())
			m_undo.pop_back();
		if (onHistoryChanged)
			onHistoryChanged();
	}

	/// Remet le document dans l'état de `step` et reconstruit la scène.
	bool RestoreStep(const HistoryStep &step) {
		SceneDesc *scene = m_project.FindScene(step.sceneName);
		if (!scene)
			return false;
		if (!ActiveScene() || ActiveScene()->name != step.sceneName)
			(void)SwitchScene(step.sceneName);
		scene = m_project.FindScene(step.sceneName);
		if (!scene)
			return false;

		m_restoring = true;
		scene->tree = step.tree;
		RebuildRuntime();
		// La sélection est un identifiant : l'arbre restauré étant une copie
		// exacte (identifiants compris), il désigne encore le bon nœud.
		(void)Select(step.selection);
		m_restoring = false;
		return true;
	}

	// ── Recherche ────────────────────────────────────────────────────────────

	// ── Construction ─────────────────────────────────────────────────────────

	[[nodiscard]] static render3d::Mesh BuildMesh(const VisualDesc &object) {
		const math::FVector3 &d = object.dimensions;
		int segments = sdl3::Clamp(object.segments, 3, 128);
		switch (object.shape) {
			case ShapeKind::BOX:
				return render3d::Mesh::Box(d.x, d.y, d.z);
			case ShapeKind::SPHERE:
				return render3d::Mesh::Sphere(d.x * 0.5f, segments, sdl3::Max(segments / 2, 3));
			case ShapeKind::CYLINDER:
				return render3d::Mesh::Cylinder(d.x * 0.5f, d.z * 0.5f, d.y, segments);
			case ShapeKind::CONE:
				return render3d::Mesh::Cone(d.x * 0.5f, d.y, segments);
			case ShapeKind::TORUS:
				return render3d::Mesh::Torus(d.x * 0.5f, d.y * 0.5f, sdl3::Max(segments / 2, 3), segments);
			case ShapeKind::PLANE:
				return render3d::Mesh::Plane(d.x, d.z);
			case ShapeKind::ICOSAHEDRON:
				return render3d::Mesh::Icosahedron(d.x * 0.5f, sdl3::Clamp(object.segments / 12, 0, 3));
			case ShapeKind::TORUS_KNOT:
				return render3d::Mesh::TorusKnot(d.x * 0.5f, d.y * 0.25f, segments * 2, sdl3::Max(segments / 3, 3));
			case ShapeKind::PORTAL_QUAD:
				return PortalQuad(d.x * 0.5f, d.y * 0.5f);
			case ShapeKind::MODEL:
				break; // traité par MakeMeshFor (a besoin du journal en cas d'échec)
		}
		return render3d::Mesh::Box(d.x, d.y, d.z);
	}

	/// Maillage d'un objet, y compris les modèles importés. Un fichier
	/// illisible donne un cube témoin ET une ligne dans la console : un
	/// objet silencieusement invisible serait bien plus déroutant qu'un
	/// cube manifestement faux.
	[[nodiscard]] render3d::Mesh MakeMeshFor(const VisualDesc &object) {
		if (object.shape != ShapeKind::MODEL)
			return BuildMesh(object);
		auto mesh = LoadGltfMesh(object.source);
		if (mesh.IsError()) {
			LogError(String::Format("Import glTF « %s » : %s", object.source.CStr(), mesh.Error().CStr()));
			return render3d::Mesh::Box(1.f, 1.f, 1.f);
		}
		return std::move(mesh).Unwrap();
	}

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
	[[nodiscard]] static Result<render3d::Mesh, String> LoadGltfMesh(const String &path) {
		if (path.IsEmpty())
			return Err(String("aucun fichier source"));
		auto document = data::gltf::LoadFile(path);
		if (document.IsError())
			return Err(document.Error());
		const data::gltf::Document &gltf = document.Value();

		if (gltf.meshes.empty() || gltf.meshes.front().primitives.empty())
			return Err(String("le document ne contient aucune primitive"));
		const data::gltf::Primitive &primitive = gltf.meshes.front().primitives.front();

		int positionAccessor = primitive.Find("POSITION");
		if (positionAccessor < 0)
			return Err(String("primitive sans attribut POSITION"));
		auto positions = gltf.ReadFloats(positionAccessor);
		if (positions.IsError())
			return Err(positions.Error());
		const std::vector<float> &xyz = positions.Value();
		size_t vertexCount = xyz.size() / 3;
		if (vertexCount == 0)
			return Err(String("primitive sans sommet"));

		// Normales et coordonnées de texture sont FACULTATIVES : un modèle
		// qui n'en a pas reste affichable (normale par défaut vers le haut).
		std::vector<float> normals;
		if (int accessor = primitive.Find("NORMAL"); accessor >= 0)
			if (auto read = gltf.ReadFloats(accessor); read.IsOk() && read.Value().size() >= vertexCount * 3)
				normals = std::move(read).Unwrap();
		std::vector<float> uvs;
		if (int accessor = primitive.Find("TEXCOORD_0"); accessor >= 0)
			if (auto read = gltf.ReadFloats(accessor); read.IsOk() && read.Value().size() >= vertexCount * 2)
				uvs = std::move(read).Unwrap();

		std::vector<render3d::Vertex3D> vertices;
		vertices.reserve(vertexCount);
		for (size_t i = 0; i < vertexCount; ++i) {
			math::FVector3 position{xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2]};
			math::FVector3 normal = normals.empty()
										? math::FVector3{0.f, 1.f, 0.f}
										: math::FVector3{normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]};
			math::FVector2 uv = uvs.empty() ? math::FVector2{0.f, 0.f} : math::FVector2{uvs[i * 2], uvs[i * 2 + 1]};
			vertices.push_back({position, normal, uv, sdl3::Color::WHITE()});
		}

		std::vector<uint32_t> indices;
		if (primitive.indices >= 0) {
			auto read = gltf.ReadIndices(primitive.indices);
			if (read.IsError())
				return Err(read.Error());
			indices = std::move(read).Unwrap();
		} else {
			// Primitive non indexée : les sommets sont déjà dans l'ordre des
			// triangles.
			indices.reserve(vertexCount);
			for (size_t i = 0; i < vertexCount; ++i)
				indices.push_back(uint32_t(i));
		}
		return Ok(render3d::Mesh(std::move(vertices), std::move(indices)));
	}

	/// Importe un `.gltf` comme nouvel objet de la scène active. L'échelle est
	/// ajustée pour que le modèle tienne dans `targetSize` unités : les
	/// modèles exportés vont du centimètre au kilomètre, et arriver à une
	/// échelle exploitable est ce qui distingue un import utilisable d'une
	/// tache invisible ou d'un mur.
	[[nodiscard]] Result<String, String> ImportGltf(const String &path, float targetSize = 3.f) {
		auto mesh = LoadGltfMesh(path);
		if (mesh.IsError())
			return Err(mesh.Error());

		// Boîte englobante des sommets, pour en déduire l'échelle et recentrer.
		math::FVector3 minimum{1e30f, 1e30f, 1e30f}, maximum{-1e30f, -1e30f, -1e30f};
		for (const render3d::Vertex3D &vertex : mesh.Value().Vertices()) {
			minimum = {sdl3::Min(minimum.x, vertex.position.x), sdl3::Min(minimum.y, vertex.position.y),
					   sdl3::Min(minimum.z, vertex.position.z)};
			maximum = {sdl3::Max(maximum.x, vertex.position.x), sdl3::Max(maximum.y, vertex.position.y),
					   sdl3::Max(maximum.z, vertex.position.z)};
		}
		math::FVector3 extent = maximum - minimum;
		float largest = sdl3::Max(sdl3::Max(extent.x, extent.y), extent.z);
		float scale = largest > 1e-5f ? targetSize / largest : 1.f;

		ObjectDesc object;
		object.name = FileStem(path);
		object.shape = ShapeKind::MODEL;
		object.source = path;
		object.dimensions = extent;
		object.transform.scale = {scale, scale, scale};
		object.transform.position = ActiveCamera().position + ForwardFromYawPitch(m_editYaw, m_editPitch) * 8.f;
		object.transform.position.y = sdl3::Max(object.transform.position.y, extent.y * scale * 0.5f);
		object.material.kind = MaterialKind::PBR;
		object.material.baseColor = sdl3::Color{210, 210, 214, 255};
		object.material.roughness = 0.6f;
		object.physics.halfExtents = extent * (0.5f * scale);
		object.tag = "imported";

		Option<String> assigned = SpawnObject(std::move(object));
		if (assigned.IsNone())
			return Err(String("aucune scène active"));
		LogSuccess(String::Format("Modèle importé : %s (%d sommets, échelle %.3f)", assigned.Unwrap().CStr(),
								  int(mesh.Value().Vertices().size()), double(scale)));
		return Ok(assigned.Unwrap());
	}

	/// Nom de fichier sans dossier ni extension — sert de nom d'objet par
	/// défaut à l'import.
	[[nodiscard]] static String FileStem(const String &path) {
		size_t slash = path.Rfind('/');
		String name = slash == String::NPOS ? path : path.Substr(slash + 1);
		size_t dot = name.Rfind('.');
		if (dot != String::NPOS && dot > 0)
			name = name.Substr(0, dot);
		return name.IsEmpty() ? String("Modèle") : name;
	}

private:

	/// Quad plat dans le plan XY local, normale +Z — la convention EXIGÉE par
	/// `render3d::Portal::WorldPlaneNormal()` (et donc PAS celle de
	/// `Mesh::Plane()`, qui produit un sol de normale +Y).
	[[nodiscard]] static render3d::Mesh PortalQuad(float halfWidth, float halfHeight) {
		std::vector<render3d::Vertex3D> vertices = {
			{{-halfWidth, -halfHeight, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f}, sdl3::Color::WHITE()},
			{{halfWidth, -halfHeight, 0.f}, {0.f, 0.f, 1.f}, {1.f, 0.f}, sdl3::Color::WHITE()},
			{{halfWidth, halfHeight, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f}, sdl3::Color::WHITE()},
			{{-halfWidth, halfHeight, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f}, sdl3::Color::WHITE()},
		};
		std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
		return render3d::Mesh(std::move(vertices), std::move(indices));
	}

	[[nodiscard]] static render3d::Material BuildMaterial(const MaterialDesc &desc) {
		render3d::Material material;
		switch (desc.kind) {
			case MaterialKind::PLASTIC:
				material = render3d::Material::Plastic(desc.baseColor);
				break;
			case MaterialKind::METAL:
				material = render3d::Material::Metal(desc.baseColor);
				break;
			case MaterialKind::WOOD:
				material = render3d::Material::Wood(desc.baseColor);
				break;
			case MaterialKind::PBR:
				material = render3d::Material::Pbr(desc.baseColor, desc.metallic, desc.roughness);
				break;
			case MaterialKind::UNLIT:
				material = render3d::Material::Unlit(desc.baseColor);
				break;
			case MaterialKind::BASIC:
				material = render3d::Material::Default();
				material.baseColor = desc.baseColor;
				break;
		}
		// Les réglages fins écrasent toujours le préréglage de famille : ce
		// que montre l'inspecteur est ce qui est rendu.
		material.metallic = desc.metallic;
		material.roughness = desc.roughness;
		material.doubleSided = desc.doubleSided;
		material.wireframe = desc.wireframe;
		return material;
	}

	[[nodiscard]] static physics::Shape BuildCollider(const PhysicsDesc &physics) {
		const math::FVector3 half = physics.halfExtents;
		switch (physics.collider) {
			case ColliderKind::SPHERE:
				return physics::Sphere{math::FVector3{}, sdl3::Max(half.x, 0.01f)};
			case ColliderKind::CAPSULE:
				// Ordre des champs de physics::Capsule : center, halfHeight,
				// radius, orientation (cf. physics/shapes.hpp).
				return physics::Capsule{math::FVector3{}, sdl3::Max(half.y, 0.01f), sdl3::Max(half.x, 0.01f),
										math::FQuaternion::Identity()};
			case ColliderKind::BOX:
				break;
		}
		// physics::Box : center, halfExtents, orientation — l'orientation
		// réelle est posée par AttachBody depuis le transform du document.
		return physics::Box{math::FVector3{},
							{sdl3::Max(half.x, 0.01f), sdl3::Max(half.y, 0.01f), sdl3::Max(half.z, 0.01f)},
							math::FQuaternion::Identity()};
	}

	/**
	 * Fabrique l'entité ECS et le nœud de rendu d'UN nœud du document.
	 *
	 * Un nœud SANS composant d'apparence (un groupe, un pivot) reçoit quand
	 * même un `render3d::Object3D` nu : il faut un point d'accroche pour ses
	 * enfants, et son transform doit continuer de se propager. C'est ce qui
	 * permet de construire « Car > Wheels > FrontLeft » où seul le dernier
	 * niveau a une géométrie.
	 */
	ecs::Entity InstantiateNode(scene::NodeId id, const scene::Node &node) {
		render3d::Object3D *object3d = nullptr;
		if (VisualDesc::Has(node)) {
			VisualDesc visual = VisualDesc::Read(node);
			object3d = &m_sceneRoot.Add(
				std::make_unique<render3d::Shape>(MakeMeshFor(visual), BuildMaterial(visual.material)));
		} else {
			object3d = &m_sceneRoot.Add(std::make_unique<render3d::Object3D>());
		}
		object3d->SetName(node.name);
		object3d->SetPosition(node.transform.position);
		object3d->SetRotation(node.transform.rotation);
		object3d->SetScale(node.transform.scale);
		object3d->SetVisible(node.visible);
		AttachHelper(id, node, *object3d);
		if (LightDesc::Has(node))
			m_lightIds.push_back(id);
		if (TriggerDesc::Has(node))
			m_triggerIds.push_back(id);

		ecs::Entity entity = m_registry.Spawn();
		m_registry.AddComponent(entity, SceneObjectRef{id});
		m_registry.AddComponent(entity, render3d::SceneNode{object3d});
		m_registry.AddComponent(
			entity, render3d::SceneTransform{node.transform.position, node.transform.rotation, node.transform.scale});
		m_entityOf[id] = entity;
		AttachBody(entity, id, node);
		return entity;
	}

	// ── Repères d'édition (lumière, caméra, déclencheur) ────────────────────
	//
	// Un nœud sans forme resterait invisible et donc inatteignable à la
	// souris : l'éditeur lui donne un REPÈRE, enfant de son Object3D (il suit
	// donc le nœud sans aucune synchronisation), masqué en mode Jeu. Ce n'est
	// pas un objet du document — il n'existe que dans la scène vivante.

	void AttachHelper(scene::NodeId id, const scene::Node &node, render3d::Object3D &owner) {
		const bool hasVisual = VisualDesc::Has(node);
		std::unique_ptr<render3d::Shape> helper;
		if (TriggerDesc::Has(node)) {
			const math::FVector3 half = TriggerDesc::Read(node).halfExtents;
			render3d::Material material = render3d::Material::Unlit(sdl3::Color{255, 120, 60, 255});
			material.wireframe = true;
			helper = std::make_unique<render3d::Shape>(
				render3d::Mesh::Box(half.x * 2.f, half.y * 2.f, half.z * 2.f), std::move(material));
		} else if (LightDesc::Has(node) && !hasVisual) {
			const LightDesc light = LightDesc::Read(node);
			helper = std::make_unique<render3d::Shape>(render3d::Mesh::Icosahedron(0.16f, 1),
													   render3d::Material::Unlit(light.color));
		} else if (CameraNodeDesc::Has(node) && !hasVisual) {
			render3d::Material material = render3d::Material::Unlit(sdl3::Color{120, 170, 255, 255});
			material.wireframe = true;
			helper = std::make_unique<render3d::Shape>(render3d::Mesh::Box(0.36f, 0.26f, 0.5f), std::move(material));
		}
		if (!helper)
			return;
		helper->SetName(String::Format("%s (repère)", node.name.CStr()));
		helper->SetVisible(!m_playing);
		m_helperOf[id] = static_cast<render3d::Shape *>(&owner.Add(std::move(helper)));
	}

	/// Recrée le repère d'un nœud après une modification de ses composants
	/// (couleur d'une lumière, taille d'une zone) et tient à jour les listes
	/// de lumières et de zones.
	void RefreshHelper(scene::NodeId id) {
		const scene::Node *node = FindObject(id);
		render3d::Object3D *owner = FindNode(id);
		if (auto it = m_helperOf.find(id); it != m_helperOf.end()) {
			if (owner)
				(void)owner->RemoveChild(it->second);
			m_helperOf.erase(it);
		}
		std::erase(m_lightIds, id);
		std::erase(m_triggerIds, id);
		if (!node || !owner)
			return;
		AttachHelper(id, *node, *owner);
		if (LightDesc::Has(*node))
			m_lightIds.push_back(id);
		if (TriggerDesc::Has(*node))
			m_triggerIds.push_back(id);
	}

	void SetHelpersVisible(bool visible) {
		for (auto &[id, helper] : m_helperOf)
			helper->SetVisible(visible);
	}

	/// Lumières de la scène, en MONDE, recalculées à chaque image : une
	/// torche portée par un personnage qui marche doit éclairer là où il est,
	/// et un script peut en changer l'intensité à tout moment.
	void UpdateLights() {
		m_pointLights.clear();
		m_spotLights.clear();
		const SceneDesc *scene = ActiveScene();
		if (!scene)
			return;
		bool shadowTaken = false;
		for (scene::NodeId id : m_lightIds) {
			const scene::Node *node = scene->tree.Get(id);
			if (!node || !scene->tree.IsVisibleInTree(id))
				continue;
			const LightDesc light = LightDesc::Read(*node);
			const scene::Transform world = scene->tree.GlobalTransform(id);
			if (light.kind == LightKind::POINT) {
				render3d::PointLight point;
				point.position = world.position;
				point.color = light.color;
				point.intensity = light.intensity;
				point.distance = light.range;
				point.castShadow = light.castShadow && !shadowTaken;
				point.shadowFar = sdl3::Max(1.f, light.range);
				shadowTaken = shadowTaken || point.castShadow;
				m_pointLights.push_back(point);
			} else {
				constexpr float DEG2RAD = 3.14159265358979323846f / 180.f;
				render3d::SpotLight spot;
				spot.position = world.position;
				spot.direction = world.rotation.Rotate(math::FVector3{0.f, -1.f, 0.f});
				spot.color = light.color;
				spot.intensity = light.intensity;
				spot.distance = light.range;
				spot.angle = sdl3::Clamp(light.spotAngle, 1.f, 89.f) * DEG2RAD;
				spot.penumbra = sdl3::Clamp(light.penumbra, 0.f, 1.f);
				spot.castShadow = light.castShadow;
				m_spotLights.push_back(spot);
			}
		}
		if (m_canvas)
			m_canvas->SetLights(m_pointLights, m_spotLights);
	}

	/**
	 * Pose (ou retire) le corps physique d'un nœud.
	 *
	 * Le corps est placé à la position MONDE du nœud, pas à son transform
	 * local : le solveur ne connaît que le monde, et un nœud enfant a un
	 * local relatif à son parent. Sans cette conversion, une roue parentée à
	 * une voiture tomberait depuis l'origine du monde.
	 */
	void AttachBody(ecs::Entity entity, scene::NodeId id, const scene::Node &node) {
		PhysicsDesc physics = PhysicsDesc::Read(node);
		if (physics.body == BodyKind::NONE) {
			m_registry.RemoveComponent<physics::RigidBody>(entity);
			return;
		}
		scene::Transform world = node.transform;
		if (const scene::NodeTree *tree = Tree(); tree && tree->Contains(id))
			world = tree->GlobalTransform(id);
		physics::RigidBody body =
			physics.body == BodyKind::DYNAMIC
				? physics::RigidBody::MakeDynamic(BuildCollider(physics), world.position,
												  sdl3::Max(physics.mass, 0.001f), physics.restitution,
												  physics.friction)
				: physics::RigidBody::MakeStatic(BuildCollider(physics), world.position, world.rotation,
												 physics.restitution, physics.friction);
		body.orientation = world.rotation;
		m_registry.AddComponent(entity, body);
	}

	/// Recopie la parenté du DOCUMENT dans l'ECS (`render3d::SceneParent`),
	/// que `SceneSyncSystem::Sync` traduit ensuite en re-parentage réel du
	/// graphe `Object3D`. L'arbre du document reste la référence ; l'ECS n'en
	/// est qu'une projection.
	void ApplyParentLinks(const SceneDesc &sceneDesc) {
		sceneDesc.tree.Traverse(sceneDesc.tree.Root(), [&](scene::NodeId id, const scene::Node &node) {
			if (id == sceneDesc.tree.Root() || node.parent == sceneDesc.tree.Root())
				return;
			Option<ecs::Entity> child = FindEntity(id);
			Option<ecs::Entity> parent = FindEntity(node.parent);
			if (child.IsSome() && parent.IsSome())
				render3d::SetSceneParent(m_registry, child.Unwrap(), parent.Unwrap());
		});
	}

	/// Relie une paire de portails repérée par les étiquettes `portal_a` /
	/// `portal_b` — une scène sans ces étiquettes n'a simplement pas de
	/// portail, ce qui est le cas de toutes sauf la vitrine.
	void LinkPortals(const SceneDesc &sceneDesc) {
		m_portalA.node = m_portalB.node = nullptr;
		m_portalA.linkedPortal = m_portalB.linkedPortal = nullptr;
		m_portalsReady = false;

		std::vector<scene::NodeId> first = sceneDesc.WithTag(String("portal_a"));
		std::vector<scene::NodeId> second = sceneDesc.WithTag(String("portal_b"));
		if (first.empty() || second.empty())
			return;

		m_portalA.node = FindNode(first.front());
		m_portalB.node = FindNode(second.front());
		if (!m_portalA.node || !m_portalB.node)
			return;
		m_portalA.linkedPortal = &m_portalB;
		m_portalB.linkedPortal = &m_portalA;
		m_portalA.UpdateDelta();
		m_portalB.UpdateDelta();

		if (!m_canvas)
			return; // sans GPU : portails logiques (téléportation) sans rendu
		auto sizedA = m_portalA_target.EnsureSize(m_canvas->Device(), PORTAL_TARGET_SIZE, PORTAL_TARGET_SIZE,
												  PORTAL_COLOR_FORMAT, PORTAL_DEPTH_FORMAT);
		auto sizedB = m_portalB_target.EnsureSize(m_canvas->Device(), PORTAL_TARGET_SIZE, PORTAL_TARGET_SIZE,
												  PORTAL_COLOR_FORMAT, PORTAL_DEPTH_FORMAT);
		m_portalsReady = sizedA.IsOk() && sizedB.IsOk();
		if (!m_portalsReady)
			LogWarning(String("Portails : cible de rendu indisponible, rendu désactivé"));
	}

	void ClearRuntime() {
		// Collecte AVANT toute mutation (invalidation d'itérateur, cf.
		// ecs.hpp) : entités d'abord, nœuds ensuite, destruction en dernier.
		std::vector<ecs::Entity> entities = m_registry.EntitiesWith<SceneObjectRef>();
		std::vector<render3d::Object3D *> nodes;
		nodes.reserve(entities.size());
		for (ecs::Entity entity : entities) {
			auto node = m_registry.GetComponent<render3d::SceneNode>(entity);
			nodes.push_back(node.IsSome() ? node.Unwrap()->node : nullptr);
		}
		for (ecs::Entity entity : entities)
			m_registry.Despawn(entity);
		for (render3d::Object3D *node : nodes)
			if (node)
				(void)m_sceneRoot.RemoveChild(node);
		m_entityOf.clear();
		m_helperOf.clear();
		m_lightIds.clear();
		m_triggerIds.clear();
		m_pointLights.clear();
		m_spotLights.clear();

		m_portalA.node = m_portalB.node = nullptr;
		m_portalA.linkedPortal = m_portalB.linkedPortal = nullptr;
		m_portalsReady = false;
		m_world.portalPlanes.clear();
	}

	void DestroyEntity(scene::NodeId id) {
		Option<ecs::Entity> entity = FindEntity(id);
		if (entity.IsNone())
			return;
		render3d::Object3D *node = nullptr;
		if (auto sceneNode = m_registry.GetComponent<render3d::SceneNode>(entity.Unwrap()); sceneNode.IsSome())
			node = sceneNode.Unwrap()->node;
		m_registry.Despawn(entity.Unwrap());
		m_entityOf.erase(id);
		m_helperOf.erase(id);
		std::erase(m_lightIds, id);
		std::erase(m_triggerIds, id);
		if (node)
			(void)m_sceneRoot.RemoveChild(node);
	}

	// ── Poussée document -> runtime ──────────────────────────────────────────

	/// Répercute le transform d'un nœud sur l'ECS et sur son corps physique.
	/// Le transform ECS est LOCAL (c'est ce que `SceneSyncSystem` recopie
	/// dans l'Object3D, dont le parent applique le reste), alors que le corps
	/// physique veut du MONDE — d'où les deux valeurs distinctes ci-dessous.
	bool PushTransform(scene::NodeId id) {
		NotifyObjectChanged();
		const scene::NodeTree *tree = Tree();
		const scene::Node *node = tree ? tree->Get(id) : nullptr;
		Option<ecs::Entity> entity = FindEntity(id);
		if (!node || entity.IsNone())
			return false;
		if (auto transform = m_registry.GetComponent<render3d::SceneTransform>(entity.Unwrap()); transform.IsSome()) {
			transform.Unwrap()->position = node->transform.position;
			transform.Unwrap()->rotation = node->transform.rotation;
			transform.Unwrap()->scale = node->transform.scale;
		}
		if (auto body = m_registry.GetComponent<physics::RigidBody>(entity.Unwrap()); body.IsSome()) {
			const scene::Transform world = tree->GlobalTransform(id);
			body.Unwrap()->position = world.position;
			body.Unwrap()->orientation = world.rotation;
			// Vitesse LINÉAIRE : remise à zéro hors mode Jeu seulement —
			// déplacer un objet dans l'éditeur doit le laisser au repos,
			// alors qu'un script de gameplay qui réoriente un véhicule à
			// chaque image (cf. le script du circuit) perdrait toute sa
			// vitesse si on l'effaçait, et le corps resterait cloué au sol.
			if (!m_playing)
				body.Unwrap()->linearVelocity = {};
			// Vitesse ANGULAIRE : toujours remise à zéro. Écrire une
			// orientation explicite veut dire « c'est moi qui décide de
			// l'orientation » ; laisser une rotation résiduelle la ferait
			// dériver entre deux écritures, et cette dérive ressortait
			// directement dans la caméra de poursuite (qui lit le lacet de
			// sa cible) sous forme de secousses.
			body.Unwrap()->angularVelocity = {};
		}
		return true;
	}

	bool PushMaterial(scene::NodeId id) {
		NotifyObjectChanged();
		const scene::Node *node = FindObject(id);
		render3d::Object3D *object3d = FindNode(id);
		if (!node || !object3d)
			return false;
		auto *shape = dynamic_cast<render3d::Shape *>(object3d);
		if (!shape || shape->Materials().empty())
			return false; // nœud structurel : aucun matériau à poser
		render3d::Material material = BuildMaterial(VisualDesc::Read(*node).material);
		for (render3d::Material &slot : shape->Materials())
			slot = material;
		return true;
	}

	bool PushPhysics(scene::NodeId id) {
		NotifyObjectChanged();
		const scene::Node *node = FindObject(id);
		Option<ecs::Entity> entity = FindEntity(id);
		if (!node || entity.IsNone())
			return false;
		AttachBody(entity.Unwrap(), id, *node);
		return true;
	}

	/// Remet chaque corps à la transformation du document — appelé au
	/// démarrage du mode Jeu pour que chaque partie reparte identique.
	void ResetPhysicsBodies() {
		SceneDesc *scene = ActiveScene();
		if (!scene)
			return;
		scene->tree.Traverse(scene->tree.Root(), [&](scene::NodeId id, const scene::Node &node) {
			if (PhysicsDesc::Has(node))
				(void)PushTransform(id);
		});
	}

	// ── Simulation -> document ───────────────────────────────────────────────

	/// Recopie dans le DOCUMENT ce que la physique et l'animation viennent
	/// d'écrire, afin que l'inspecteur, l'outliner et le rapport voient la
	/// même chose que la 3D pendant une partie (cf. en-tête : c'est le seul
	/// flux « à l'envers », et il est annulé par Stop()).
	void MirrorSimulationToDocument() {
		SceneDesc *scene = ActiveScene();
		if (!scene)
			return;
		for (ecs::Entity entity : m_registry.EntitiesWith<physics::RigidBody>()) {
			auto ref = m_registry.GetComponent<SceneObjectRef>(entity);
			auto body = m_registry.GetComponent<physics::RigidBody>(entity);
			auto transform = m_registry.GetComponent<render3d::SceneTransform>(entity);
			if (ref.IsNone() || body.IsNone() || transform.IsNone())
				continue;
			const scene::NodeId id = ref.Unwrap()->node;
			if (!scene->tree.Contains(id))
				continue;

			// Le solveur travaille en MONDE, le document en LOCAL : poser
			// directement la position du corps dans le transform du nœud
			// serait juste pour un objet à la racine et faux pour tout nœud
			// parenté (la roue d'une voiture se retrouverait à la position
			// monde de la voiture PLUS la sienne). `SetGlobalTransform`
			// recalcule le local à partir du parent — c'est exactement la
			// conversion qui manquait tant que la scène était plate.
			scene::Transform world;
			world.position = body.Unwrap()->position;
			world.rotation = body.Unwrap()->orientation;
			world.scale = scene->tree.GlobalTransform(id).scale;
			(void)scene->tree.SetGlobalTransform(id, world);

			const scene::Transform &local = scene->tree.Get(id)->transform;
			transform.Unwrap()->position = local.position;
			transform.Unwrap()->rotation = local.rotation;
		}
	}

	void UpdatePortalPlanes() {
		m_world.portalPlanes.clear();
		if (!m_portalA.node || !m_portalB.node)
			return;
		m_portalA.UpdateDelta();
		m_portalB.UpdateDelta();
		m_world.portalPlanes.push_back(MakePortalPlane(m_portalA));
		m_world.portalPlanes.push_back(MakePortalPlane(m_portalB));
	}

	/// `physics::PortalPlane` ne dépend volontairement de rien de render3d::
	/// (cf. portal_teleport.hpp) — c'est l'appelant qui fait le pont.
	[[nodiscard]] static physics::PortalPlane MakePortalPlane(const render3d::Portal &portal) {
		physics::PortalPlane plane;
		plane.position = portal.WorldPlanePos();
		plane.normal = portal.WorldPlaneNormal();
		plane.delta = portal.delta;
		plane.deltaInv = portal.deltaInv;
		return plane;
	}

	void RenderPortals() {
		if (!m_canvas || !m_portalsReady || !m_portalA.node || !m_portalB.node)
			return;
		m_portalA.UpdateDelta();
		m_portalB.UpdateDelta();
		render3d::RenderPortalRecursive(*m_canvas, m_sceneRoot, ActiveCamera(), m_portalA, 0, 0, m_portalA_target);
		render3d::RenderPortalRecursive(*m_canvas, m_sceneRoot, ActiveCamera(), m_portalB, 0, 0, m_portalB_target);
	}

	// ── Caméra ───────────────────────────────────────────────────────────────

	[[nodiscard]] static math::FVector3 ForwardFromYawPitch(float yaw, float pitch) noexcept {
		return math::FVector3{sdl3::Cos(pitch) * sdl3::Sin(yaw), sdl3::Sin(pitch), sdl3::Cos(pitch) * sdl3::Cos(yaw)}
			.Normalize();
	}

	void RefreshCameraTargets() {
		m_editCamera.target = m_editCamera.position + ForwardFromYawPitch(m_editYaw, m_editPitch);
		m_playCamera.target = m_playCamera.position + ForwardFromYawPitch(m_playYaw, m_playPitch);
	}

	/// En mode Jeu, si la scène désigne un objet suivi (étiquette
	/// `camera_target`), la caméra le poursuit à la troisième personne ; sinon
	/// elle reste libre. C'est ce qui donne une vraie caméra de jeu de
	/// voiture sans une ligne de C++ spécifique au circuit.
	void UpdateCamera(float dt) {
		(void)dt;
		if (m_playing) {
			const scene::NodeId targetId = FollowTarget();
			const scene::Node *target = FindObject(targetId);
			if (target) {
				// La caméra se place derrière l'objet SELON SON PROPRE CAP
				// (son lacet), pas selon l'orientation libre de la caméra :
				// c'est ce qui donne une vraie caméra de poursuite. Utiliser
				// m_playYaw laissait la caméra traîner dans une direction
				// fixe du monde, et la voiture partait de côté ou vers
				// l'objectif dès le premier virage.
				constexpr float DEG2RAD = 3.14159265358979323846f / 180.f;
				float targetYaw = target->transform.EulerDegrees().y * DEG2RAD;
				// Au-delà d'une vitesse notable, le cap est pris sur le
				// VECTEUR VITESSE plutôt que sur le lacet : c'est la vraie
				// direction de déplacement, insensible aux à-coups de
				// l'orientation lors d'un contact avec un rail.
				Option<math::FVector3> velocity = GetVelocity(targetId);
				if (velocity.IsSome()) {
					math::FVector3 flat{velocity.Unwrap().x, 0.f, velocity.Unwrap().z};
					if (flat.LengthSq() > 4.f)
						targetYaw = sdl3::Atan2(flat.x, flat.z);
				}
				math::FVector3 forward = ForwardFromYawPitch(targetYaw, -0.18f);
				// Position MONDE de la cible : un véhicule peut très bien
				// être l'enfant d'un groupe (« Voiture > Carrosserie »).
				const math::FVector3 targetWorld = Tree()->GlobalPosition(targetId);
				math::FVector3 desired =
					targetWorld - forward * m_followDistance + math::FVector3{0.f, m_followHeight, 0.f};
				// Lissage exponentiel indépendant de la fréquence d'image.
				float blend = sdl3::Clamp(dt * m_followStiffness, 0.f, 1.f);
				m_playCamera.position += (desired - m_playCamera.position) * blend;
				m_playCamera.target = targetWorld + math::FVector3{0.f, 1.2f, 0.f};
				// Le cap libre suit la caméra de poursuite : reprendre la main
				// (mode Édition, ou une scène sans cible) ne fait alors pas
				// sauter la vue.
				m_playYaw = targetYaw;
				return;
			}
			// Sans objet poursuivi : la caméra COURANTE de la scène, si elle
			// en a une (vue à la première personne, caméra fixe de salle).
			if (const scene::Node *cameraNode = FindObject(m_currentCamera)) {
				constexpr float DEG2RAD = 3.14159265358979323846f / 180.f;
				const scene::Transform world = Tree()->GlobalTransform(m_currentCamera);
				const math::FVector3 forward = world.rotation.Rotate(math::FVector3{0.f, 0.f, 1.f}).Normalize();
				m_playCamera.position = world.position;
				m_playCamera.target = world.position + forward;
				m_playCamera.fovYRadians =
					sdl3::Clamp(CameraNodeDesc::Read(*cameraNode).fovDegrees, 20.f, 120.f) * DEG2RAD;
				// Cap libre recalé sur la vue : l'arrêt du jeu ne fait pas sauter
				// la caméra.
				m_playYaw = sdl3::Atan2(forward.x, forward.z);
				m_playPitch = sdl3::Asin(sdl3::Clamp(forward.y, -1.f, 1.f));
				return;
			}
		}
		RefreshCameraTargets();
	}

	// ── Caméras, déclencheurs et scripts de nœud (mode Jeu) ─────────────────

	/// Première caméra marquée `current` de la scène, dans l'ordre de l'arbre.
	[[nodiscard]] scene::NodeId FindCurrentCamera() const {
		const SceneDesc *scene = ActiveScene();
		if (!scene)
			return scene::NodeId{};
		scene::NodeId found;
		scene->tree.Traverse(scene->tree.Root(), [&](scene::NodeId id, const scene::Node &node) {
			if (!found.Valid() && CameraNodeDesc::Has(node) && CameraNodeDesc::Read(node).current)
				found = id;
		});
		return found;
	}

	/// Objets susceptibles d'entrer dans une zone : ceux étiquetés `player`
	/// et tous les corps dynamiques. Figé au lancement : c'est le décor de
	/// la partie qui se joue.
	void PrepareTriggers() {
		m_triggerCandidates.clear();
		m_triggerInside.clear();
		m_triggerFired.clear();
		const SceneDesc *scene = ActiveScene();
		if (!scene)
			return;
		scene->tree.Traverse(scene->tree.Root(), [&](scene::NodeId id, const scene::Node &node) {
			if (id == scene->tree.Root() || TriggerDesc::Has(node))
				return;
			if (TagOf(node) == "player" || PhysicsDesc::Read(node).body == BodyKind::DYNAMIC)
				m_triggerCandidates.push_back(id);
		});
	}

	/// Test d'appartenance de chaque candidat à chaque zone (boîte alignée
	/// sur les axes, mise à l'échelle du nœud) ; seules les TRANSITIONS
	/// sont notifiées — entrer, puis sortir — et non la présence.
	void UpdateTriggers() {
		const SceneDesc *scene = ActiveScene();
		if (!scene || m_triggerIds.empty() || m_triggerCandidates.empty())
			return;
		for (scene::NodeId zone : m_triggerIds) {
			const scene::Node *zoneNode = scene->tree.Get(zone);
			if (!zoneNode)
				continue;
			const TriggerDesc trigger = TriggerDesc::Read(*zoneNode);
			const scene::Transform world = scene->tree.GlobalTransform(zone);
			const math::FVector3 half{trigger.halfExtents.x * sdl3::Abs(world.scale.x),
									  trigger.halfExtents.y * sdl3::Abs(world.scale.y),
									  trigger.halfExtents.z * sdl3::Abs(world.scale.z)};
			for (scene::NodeId other : m_triggerCandidates) {
				const scene::Node *otherNode = scene->tree.Get(other);
				if (!otherNode)
					continue;
				const math::FVector3 p = scene->tree.GlobalPosition(other) - world.position;
				const bool inside = sdl3::Abs(p.x) <= half.x && sdl3::Abs(p.y) <= half.y && sdl3::Abs(p.z) <= half.z;
				const uint64_t key = (uint64_t(zone.index) << 32) | uint64_t(other.index);
				const bool wasInside = m_triggerInside.contains(key);
				if (inside == wasInside)
					continue;
				if (inside) {
					m_triggerInside.insert(key);
					if (trigger.once && m_triggerFired.contains(zone.index))
						continue;
					m_triggerFired.insert(zone.index);
					++m_triggerCount;
					LogInfo(String::Format("Déclencheur « %s » : %s entre%s%s", zoneNode->name.CStr(),
										   otherNode->name.CStr(), trigger.event.IsEmpty() ? "" : " → ",
										   trigger.event.CStr()));
					FireTrigger(String("on_trigger"), zone, zoneNode->name, otherNode->name, trigger.event);
				} else {
					m_triggerInside.erase(key);
					FireTrigger(String("on_trigger_exit"), zone, zoneNode->name, otherNode->name, trigger.event);
				}
			}
		}
	}

	/// Notifie le script de la scène `hook(zone, objet, évènement)` et les
	/// scripts attachés à la zone `hook(self, objet, évènement)`.
	void FireTrigger(const String &hook, scene::NodeId zone, const String &zoneName, const String &otherName,
					 const String &event) {
		using data::script::Value;
		if (m_gameplayReady)
			CallGameplayHook(hook, {Value::Str(zoneName), Value::Str(otherName), Value::Str(event)});
		for (NodeScriptInstance &instance : m_nodeScripts) {
			if (!instance.ready)
				continue;
			for (scene::NodeId node : instance.nodes)
				if (node == zone)
					CallNodeHook(instance, node, hook,
								 {Value::Str(zoneName), Value::Str(otherName), Value::Str(event)});
		}
	}

	/// Charge chaque script de la bibliothèque utilisé dans la scène, UNE
	/// fois (un interpréteur par script, partagé par tous les nœuds qui le
	/// portent — chacun le reçoit en `self`), puis appelle `on_start(self)`.
	void StartNodeScripts() {
		m_nodeScripts.clear();
		const SceneDesc *scene = ActiveScene();
		if (!scene)
			return;
		scene->tree.Traverse(scene->tree.Root(), [&](scene::NodeId id, const scene::Node &node) {
			if (!ScriptRef::Has(node))
				return;
			const String name = ScriptRef::Read(node).script;
			if (name.IsEmpty())
				return;
			for (NodeScriptInstance &instance : m_nodeScripts) {
				if (instance.script == name) {
					instance.nodes.push_back(id);
					return;
				}
			}
			NodeScriptInstance instance;
			instance.script = name;
			instance.nodes.push_back(id);
			m_nodeScripts.push_back(std::move(instance));
		});

		uint64_t seedOffset = 1;
		for (NodeScriptInstance &instance : m_nodeScripts) {
			const ScriptAsset *asset = m_project.FindScript(instance.script);
			if (!asset) {
				instance.error = String("script introuvable dans le projet");
				LogWarning(String::Format("Script « %s » : introuvable dans la bibliothèque du projet",
										  instance.script.CStr()));
				continue;
			}
			instance.vm = std::make_unique<data::script::Interpreter>();
			InstallHostApi(*instance.vm);
			instance.vm->SetRandomSeed(m_randomSeed + 0x9E3779B97F4A7C15ull * seedOffset++);
			++m_scriptRunCount;
			m_loadedScripts.push_back(String::Format("script:%s", instance.script.CStr()));
			auto loaded = instance.vm->Run(asset->source.View());
			if (loaded.IsError()) {
				++m_scriptErrorCount;
				instance.error = loaded.Error().Format();
				LogError(String::Format("Script « %s » : %s", instance.script.CStr(), instance.error.CStr()));
				continue;
			}
			instance.ready = true;
			for (scene::NodeId node : instance.nodes)
				CallNodeHook(instance, node, String("on_start"), {});
		}
	}

	void UpdateNodeScripts(float dt) {
		using data::script::Value;
		for (NodeScriptInstance &instance : m_nodeScripts) {
			if (!instance.ready)
				continue;
			for (scene::NodeId node : instance.nodes) {
				CallNodeHook(instance, node, String("on_update"), {Value::Number(double(dt))});
				if (!instance.ready)
					break; // désactivé par une erreur à l'instant
			}
		}
	}

	/// Appelle `hook(self, args…)` pour un nœud. Une erreur DÉSACTIVE le
	/// script pour la partie (même raison que pour le script de scène : ne
	/// pas répéter la même erreur 60 fois par seconde).
	void CallNodeHook(NodeScriptInstance &instance, scene::NodeId node, const String &hook,
					  std::vector<data::script::Value> args) {
		const scene::Node *target = FindObject(node);
		if (!target || !instance.vm)
			return;
		args.insert(args.begin(), data::script::Value::Str(target->name));
		auto called = instance.vm->CallGlobalIfPresent(hook, std::move(args));
		if (called.IsNone())
			return;
		++m_scriptCallCount;
		if (called.Unwrap().IsError()) {
			++m_scriptErrorCount;
			instance.ready = false;
			instance.error = called.Unwrap().Error().Format();
			LogError(String::Format("Script « %s » (%s) `%s` : %s", instance.script.CStr(), target->name.CStr(),
									hook.CStr(), instance.error.CStr()));
			LogWarning(String::Format("Script « %s » désactivé jusqu'au prochain démarrage du mode Jeu",
									  instance.script.CStr()));
		}
	}

	[[nodiscard]] scene::NodeId FollowTarget() const {
		const SceneDesc *sceneDesc = ActiveScene();
		if (!sceneDesc)
			return scene::NodeId{};
		std::vector<scene::NodeId> targets = sceneDesc->WithTag(String("camera_target"));
		return targets.empty() ? scene::NodeId{} : targets.front();
	}

	// ── Scripts ──────────────────────────────────────────────────────────────

	[[nodiscard]] Result<data::script::Value, data::script::ScriptError> RunGameplayScript(const String &source) {
		++m_scriptRunCount;
		m_loadedScripts.push_back(String::Format("gameplay:%s", ActiveScene() ? ActiveScene()->name.CStr() : "?"));
		return m_gameplayVm.Run(source.View());
	}

	void CallGameplayHook(const String &hook, std::vector<data::script::Value> args) {
		auto called = m_gameplayVm.CallGlobalIfPresent(hook, std::move(args));
		if (called.IsNone())
			return;
		++m_scriptCallCount;
		auto result = called.Unwrap();
		if (result.IsError()) {
			++m_scriptErrorCount;
			LogError(String::Format("Script de jeu `%s` : %s", hook.CStr(), result.Error().Format().CStr()));
			// Un script qui échoue est DÉSACTIVÉ pour le reste de la partie :
			// sans ça, la même erreur se répéterait à 60 Hz et noierait la
			// console (et le rapport) sous des milliers de lignes identiques.
			m_gameplayReady = false;
			LogWarning(String("Script de jeu désactivé jusqu'au prochain démarrage du mode Jeu"));
		}
	}

	void RunGameplayHook(float dt) {
		if (!m_gameplayReady)
			return;
		CallGameplayHook(String("on_update"), {data::script::Value::Number(double(dt))});
	}

	void InstallHostApi(data::script::Interpreter &vm);

	// ── Membres ──────────────────────────────────────────────────────────────
	// Ordre imposé : m_sceneRoot AVANT m_mixer (AnimationMixer garde une
	// référence dessus), sinon -Wreorder sous -Werror.

	static constexpr uint32_t PORTAL_TARGET_SIZE = 512;
	static constexpr sdl3::GpuTextureFormat PORTAL_COLOR_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	static constexpr sdl3::GpuTextureFormat PORTAL_DEPTH_FORMAT = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

	ecs::ArchetypeRegistry &m_registry;
	jobs::JobSystem m_jobs;
	Project m_project;
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
	scene::NodeId m_dragObject;
	math::FVector3 m_dragStartPosition, m_dragStartEuler, m_dragStartScale{1.f, 1.f, 1.f};
	math::FVector3 m_dragStartVector;
	float m_dragStartParam = 0.f;
	float m_dragSize = 1.f;

	data::script::Interpreter m_gameplayVm;
	data::script::Interpreter m_toolVm;
	uint64_t m_randomSeed = 0x2545F4914F6CDD1Dull;
	/// Scripts de nœud chargés pour la partie en cours (cf. StartNodeScripts).
	std::vector<NodeScriptInstance> m_nodeScripts;
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
	std::vector<String> m_loadedScripts;
	size_t m_scriptRunCount = 0;
	size_t m_scriptCallCount = 0;
	size_t m_scriptErrorCount = 0;

	std::vector<LogEntry> m_log;
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
[[nodiscard]] inline Value Vec3ToValue(const math::FVector3 &v) {
	auto list = std::make_shared<data::script::ListObject>();
	list->items.push_back(Value::Number(double(v.x)));
	list->items.push_back(Value::Number(double(v.y)));
	list->items.push_back(Value::Number(double(v.z)));
	return Value::List(std::move(list));
}

/// Lit trois arguments numériques consécutifs comme un vecteur.
[[nodiscard]] inline Result<math::FVector3, ScriptError> ArgVec3(const std::vector<Value> &args, size_t first,
																 const char *fnName) {
	auto x = data::script::detail::ArgNumber(args, first, fnName);
	if (x.IsError())
		return Err(x.Error());
	auto y = data::script::detail::ArgNumber(args, first + 1, fnName);
	if (y.IsError())
		return Err(y.Error());
	auto z = data::script::detail::ArgNumber(args, first + 2, fnName);
	if (z.IsError())
		return Err(z.Error());
	return Ok(math::FVector3{float(x.Unwrap()), float(y.Unwrap()), float(z.Unwrap())});
}

/// Lit un champ d'une table de script comme vecteur (`{pos: [1, 2, 3]}`).
[[nodiscard]] inline math::FVector3 FieldVec3(const data::script::MapObject &map, const char *key,
											  math::FVector3 fallback) {
	const Value *found = map.Find(String(key));
	if (!found || !found->IsList() || !found->AsList() || found->AsList()->items.size() < 3)
		return fallback;
	const std::vector<Value> &items = found->AsList()->items;
	if (!items[0].IsNumber() || !items[1].IsNumber() || !items[2].IsNumber())
		return fallback;
	return {items[0].AsFloat(), items[1].AsFloat(), items[2].AsFloat()};
}

[[nodiscard]] inline float FieldFloat(const data::script::MapObject &map, const char *key, float fallback) {
	const Value *found = map.Find(String(key));
	return (found && found->IsNumber()) ? found->AsFloat() : fallback;
}

[[nodiscard]] inline bool FieldBool(const data::script::MapObject &map, const char *key, bool fallback) {
	const Value *found = map.Find(String(key));
	return found ? found->IsTruthy() : fallback;
}

[[nodiscard]] inline String FieldString(const data::script::MapObject &map, const char *key, const char *fallback) {
	const Value *found = map.Find(String(key));
	return (found && found->IsString()) ? found->AsString() : String(fallback);
}

[[nodiscard]] inline Option<sdl3::Color> FieldColor(const data::script::MapObject &map, const char *key) {
	const Value *found = map.Find(String(key));
	if (!found || !found->IsList() || !found->AsList() || found->AsList()->items.size() < 3)
		return NONE;
	const std::vector<Value> &items = found->AsList()->items;
	auto channel = [](const Value &v) -> uint8_t {
		double raw = v.IsNumber() ? v.AsNumber() : 255.0;
		return uint8_t(raw < 0.0 ? 0 : (raw > 255.0 ? 255 : raw));
	};
	return Some(sdl3::Color{channel(items[0]), channel(items[1]), channel(items[2]), 255});
}

/// Nom de touche -> scancode SDL. Table volontairement courte et explicite :
/// exposer `SDL_GetScancodeFromName` laisserait un script écrire n'importe
/// quel identifiant SDL, ce qui lierait la syntaxe des scripts à SDL.
[[nodiscard]] inline Option<SDL_Keycode> ScancodeFromName(const String &name) {
	struct Entry {
		const char *name;
		SDL_Keycode code; ///< code de TOUCHE (suit la disposition du clavier)
	};
	static constexpr Entry TABLE[] = {
		{"w", SDLK_W},         {"a", SDLK_A},          {"s", SDLK_S},
		{"d", SDLK_D},         {"q", SDLK_Q},          {"e", SDLK_E},
		{"r", SDLK_R},         {"f", SDLK_F},          {"up", SDLK_UP},
		{"down", SDLK_DOWN},   {"left", SDLK_LEFT},    {"right", SDLK_RIGHT},
		{"space", SDLK_SPACE}, {"shift", SDLK_LSHIFT}, {"ctrl", SDLK_LCTRL},
		{"alt", SDLK_LALT},    {"tab", SDLK_TAB},      {"enter", SDLK_RETURN},
		{"escape", SDLK_ESCAPE},
	};
	String key = name.ToLower();
	for (const Entry &entry : TABLE)
		if (key == entry.name)
			return Some(entry.code);
	return NONE;
}

} // namespace script_detail

inline void Runtime::InstallHostApi(data::script::Interpreter &vm) {
	using data::script::Interpreter;
	using data::script::ScriptError;
	using data::script::Value;
	namespace sd = script_detail;

	// Les codecs data:: (parse/encode/read_file/write_file/load) : un script
	// d'éditeur lit ainsi directement un .gltf, un .json de projet ou une
	// table de réglages YAML.
	data::script::InstallDataLibrary(vm);

	// `print` d'un script atterrit dans la console de l'éditeur, pas sur la
	// sortie standard — c'est l'endroit où l'utilisateur le cherche.
	vm.onPrint = [this](const String &line) { Log(LogLevel::SCRIPT, line); };

	Runtime *self = this;

	// ── editor.* ─────────────────────────────────────────────────────────────

	vm.RegisterNamespacedNative(String("editor"), String("log"), 1, -1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   String line;
								   for (size_t i = 0; i < args.size(); ++i) {
									   if (i > 0)
										   line.Concat(" ");
									   line.Concat(args[i].ToDisplayString());
								   }
								   self->Log(LogLevel::SCRIPT, std::move(line));
								   return Ok(Value::Nil());
							   });

	vm.RegisterNamespacedNative(String("editor"), String("warn"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   self->LogWarning(args[0].ToDisplayString());
								   return Ok(Value::Nil());
							   });

	vm.RegisterNamespacedNative(String("editor"), String("frame"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   return Ok(Value::Number(double(self->FrameIndex())));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("time"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   return Ok(Value::Number(double(self->PlayTime())));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("fps"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   return Ok(Value::Number(self->onQueryFps ? self->onQueryFps() : 0.0));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("play"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   self->Play();
								   return Ok(Value::Boolean(self->IsPlaying()));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("stop"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   self->Stop();
								   return Ok(Value::Boolean(!self->IsPlaying()));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("is_playing"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   return Ok(Value::Boolean(self->IsPlaying()));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("select"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "editor.select");
								   if (name.IsError())
									   return Err(name.Error());
								   return Ok(Value::Boolean(self->Select(name.Value())));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("selected"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   Option<String> name = self->SelectedName();
								   if (name.IsNone())
									   return Ok(Value::Nil());
								   return Ok(Value::Str(name.Unwrap()));
							   });

	// ── Édition : historique, sélection au rayon, manipulateur ──────────────
	// Exposés aux scripts pour que les scénarios vérifient EXACTEMENT ce que
	// fait la souris : `editor.select_at` prend un pixel du viewport, comme
	// un clic ; `editor.gizmo_drag` rejoue un glissé complet.

	vm.RegisterNamespacedNative(String("editor"), String("undo"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   return Ok(Value::Boolean(self->Undo()));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("redo"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   return Ok(Value::Boolean(self->Redo()));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("undo_label"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   return Ok(Value::Str(self->UndoLabel()));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("gizmo_mode"), 0, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   if (!args.empty()) {
									   auto mode = data::script::detail::ArgString(args, 0, "editor.gizmo_mode");
									   if (mode.IsError())
										   return Err(mode.Error());
									   const String key = mode.Value().ToLower();
									   if (key == "translate" || key == "move")
										   self->SetGizmoMode(GizmoMode::TRANSLATE);
									   else if (key == "rotate")
										   self->SetGizmoMode(GizmoMode::ROTATE);
									   else if (key == "scale")
										   self->SetGizmoMode(GizmoMode::SCALE);
									   else
										   return Err(ScriptError(
											   String::Format(
												   "editor.gizmo_mode : mode inconnu « %s » (translate|rotate|scale)",
												   mode.Value().CStr()),
											   0, 0));
								   }
								   const GizmoMode mode = self->GetGizmoMode();
								   return Ok(Value::Str(String(mode == GizmoMode::TRANSLATE  ? "translate"
															   : mode == GizmoMode::ROTATE ? "rotate"
																						   : "scale")));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("snap"), 0, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   if (!args.empty())
									   self->SetSnapEnabled(args[0].IsTruthy());
								   return Ok(Value::Boolean(self->SnapEnabled()));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("snap_steps"), 3, 3,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto translate = data::script::detail::ArgNumber(args, 0, "editor.snap_steps");
								   auto rotate = data::script::detail::ArgNumber(args, 1, "editor.snap_steps");
								   auto scale = data::script::detail::ArgNumber(args, 2, "editor.snap_steps");
								   if (translate.IsError())
									   return Err(translate.Error());
								   if (rotate.IsError())
									   return Err(rotate.Error());
								   if (scale.IsError())
									   return Err(scale.Error());
								   self->SetSnapSteps(float(translate.Value()), float(rotate.Value()),
													  float(scale.Value()));
								   return Ok(Value::Boolean(true));
							   });

	/// `editor.select_at(x, y, largeur, hauteur)` — sélectionne comme un clic
	/// au pixel (x, y) d'un viewport de cette taille. Rend le nom touché, ou
	/// nil si le rayon n'a rien rencontré.
	vm.RegisterNamespacedNative(String("editor"), String("select_at"), 4, 4,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   float values[4];
								   for (int i = 0; i < 4; ++i) {
									   auto value = data::script::detail::ArgNumber(args, size_t(i), "editor.select_at");
									   if (value.IsError())
										   return Err(value.Error());
									   values[i] = float(value.Value());
								   }
								   const math::FRay ray = self->ViewportRay(values[0], values[1], values[2], values[3]);
								   Option<PickHit> hit = self->PickAt(ray);
								   if (hit.IsNone()) {
									   self->ClearSelection();
									   return Ok(Value::Nil());
								   }
								   (void)self->Select(hit.Value().name);
								   return Ok(Value::Str(hit.Value().name));
							   });

	/// `editor.gizmo_drag("x", depart, arrivee)` — rejoue un glissé complet
	/// sur un axe, en unités monde le long de cet axe (le scénario n'a donc
	/// pas à fabriquer des rayons).
	vm.RegisterNamespacedNative(String("editor"), String("gizmo_drag"), 3, 3,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto axisName = data::script::detail::ArgString(args, 0, "editor.gizmo_drag");
								   if (axisName.IsError())
									   return Err(axisName.Error());
								   auto from = data::script::detail::ArgNumber(args, 1, "editor.gizmo_drag");
								   auto to = data::script::detail::ArgNumber(args, 2, "editor.gizmo_drag");
								   if (from.IsError())
									   return Err(from.Error());
								   if (to.IsError())
									   return Err(to.Error());
								   const String key = axisName.Value().ToLower();
								   const GizmoAxis axis = key == "x"   ? GizmoAxis::X
														  : key == "y" ? GizmoAxis::Y
														  : key == "z" ? GizmoAxis::Z
																	   : GizmoAxis::NONE;
								   if (axis == GizmoAxis::NONE)
									   return Err(ScriptError(
										   String::Format("editor.gizmo_drag : axe inconnu « %s » (x|y|z)",
														  axisName.Value().CStr()),
										   0, 0));
								   return Ok(Value::Boolean(
									   self->ScriptedGizmoDrag(axis, float(from.Value()), float(to.Value()))));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("focus"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "editor.focus");
								   if (name.IsError())
									   return Err(name.Error());
								   return Ok(Value::Boolean(self->FocusOn(name.Value())));
							   });

	// Délègue à l'hôte graphique (App) : en mode --headless, aucun rappel
	// n'est installé et la fonction rend `false` au lieu d'échouer, pour
	// qu'un même scénario tourne avec ou sans écran.
	vm.RegisterNamespacedNative(String("editor"), String("screenshot"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto path = data::script::detail::ArgString(args, 0, "editor.screenshot");
								   if (path.IsError())
									   return Err(path.Error());
								   if (!self->onScreenshot)
									   return Ok(Value::Boolean(false));
								   return Ok(Value::Boolean(self->onScreenshot(path.Value())));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("set_theme"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto theme = data::script::detail::ArgString(args, 0, "editor.set_theme");
								   if (theme.IsError())
									   return Err(theme.Error());
								   if (!self->onThemeChange)
									   return Ok(Value::Boolean(false));
								   return Ok(Value::Boolean(self->onThemeChange(theme.Value())));
							   });

	/// `editor.ui(commande, argument?)` : pilote l'interface comme la souris
	/// le ferait (cf. EditorUi::UiCommand pour la liste). Faux sans fenêtre.
	vm.RegisterNamespacedNative(String("editor"), String("ui"), 1, 2,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto command = data::script::detail::ArgString(args, 0, "editor.ui");
								   if (command.IsError())
									   return Err(command.Error());
								   String argument;
								   if (args.size() > 1)
									   argument = args[1].ToDisplayString();
								   if (!self->onUiCommand)
									   return Ok(Value::Boolean(false));
								   return Ok(Value::Boolean(self->onUiCommand(command.Value(), argument)));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("open_panel"), 1, 2,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto panel = data::script::detail::ArgString(args, 0, "editor.open_panel");
								   if (panel.IsError())
									   return Err(panel.Error());
								   int tab = 0;
								   if (args.size() > 1) {
									   auto index = data::script::detail::ArgNumber(args, 1, "editor.open_panel");
									   if (index.IsError())
										   return Err(index.Error());
									   tab = int(index.Unwrap());
								   }
								   if (!self->onPanelFocus)
									   return Ok(Value::Boolean(false));
								   return Ok(Value::Boolean(self->onPanelFocus(panel.Value(), tab)));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("status"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   if (self->onStatusMessage)
									   self->onStatusMessage(args[0].ToDisplayString());
								   return Ok(Value::Nil());
							   });

	vm.RegisterNamespacedNative(String("editor"), String("scene"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   const SceneDesc *scene = self->ActiveScene();
								   return Ok(scene ? Value::Str(scene->name) : Value::Nil());
							   });

	vm.RegisterNamespacedNative(String("editor"), String("scenes"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   auto list = std::make_shared<data::script::ListObject>();
								   for (const String &name : self->GetProject().SceneNames())
									   list->items.push_back(Value::Str(name));
								   return Ok(Value::List(std::move(list)));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("switch_scene"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "editor.switch_scene");
								   if (name.IsError())
									   return Err(name.Error());
								   return Ok(Value::Boolean(self->SwitchScene(name.Value())));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("save"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto path = data::script::detail::ArgString(args, 0, "editor.save");
								   if (path.IsError())
									   return Err(path.Error());
								   auto saved = self->SaveProjectFile(path.Value());
								   if (saved.IsError()) {
									   self->LogError(saved.Error());
									   return Ok(Value::Boolean(false));
								   }
								   self->LogSuccess(String::Format("Projet enregistré : %s", path.Value().CStr()));
								   return Ok(Value::Boolean(true));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("open"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto path = data::script::detail::ArgString(args, 0, "editor.open");
								   if (path.IsError())
									   return Err(path.Error());
								   auto loaded = self->LoadProjectFile(path.Value());
								   if (loaded.IsError()) {
									   self->LogError(loaded.Error());
									   return Ok(Value::Boolean(false));
								   }
								   return Ok(Value::Boolean(true));
							   });

	// ── scene.* ──────────────────────────────────────────────────────────────

	vm.RegisterNamespacedNative(
		String("scene"), String("spawn"), 1, 1,
		[self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
			auto table = data::script::detail::ArgMap(args, 0, "scene.spawn");
			if (table.IsError())
				return Err(table.Error());
			const data::script::MapObject &map = *table.Value();

			ObjectDesc object;
			object.name = sd::FieldString(map, "name", "Objet");
			object.parent = sd::FieldString(map, "parent", "");
			object.tag = sd::FieldString(map, "tag", "");
			object.shape = ShapeKindFromName(sd::FieldString(map, "shape", "box")).UnwrapOr(ShapeKind::BOX);
			object.dimensions = sd::FieldVec3(map, "size", math::FVector3{1.f, 1.f, 1.f});
			object.segments = int(sd::FieldFloat(map, "segments", 24.f));
			object.visible = sd::FieldBool(map, "visible", true);
			object.transform.position = sd::FieldVec3(map, "pos", math::FVector3{});
			object.transform.SetEulerDegrees(sd::FieldVec3(map, "rot", math::FVector3{}));
			object.transform.scale = sd::FieldVec3(map, "scale", math::FVector3{1.f, 1.f, 1.f});

			object.material.kind =
				MaterialKindFromName(sd::FieldString(map, "material", "plastic")).UnwrapOr(MaterialKind::PLASTIC);
			if (auto color = sd::FieldColor(map, "color"); color.IsSome())
				object.material.baseColor = color.Unwrap();
			object.material.metallic = sd::FieldFloat(map, "metallic", 0.f);
			object.material.roughness = sd::FieldFloat(map, "roughness", 0.5f);
			object.material.doubleSided = sd::FieldBool(map, "double_sided", false);
			object.material.wireframe = sd::FieldBool(map, "wireframe", false);

			object.physics.body = BodyKindFromName(sd::FieldString(map, "body", "none")).UnwrapOr(BodyKind::NONE);
			object.physics.collider =
				ColliderKindFromName(sd::FieldString(map, "collider", "box")).UnwrapOr(ColliderKind::BOX);
			// Par défaut, le volume de collision épouse la forme affichée :
			// un script n'a à préciser `half_extents` que s'il veut l'en
			// détacher (une piste plate qui collisionne plus épais, etc.).
			object.physics.halfExtents =
				sd::FieldVec3(map, "half_extents", object.dimensions * 0.5f * object.transform.scale);
			object.physics.mass = sd::FieldFloat(map, "mass", 1.f);
			object.physics.restitution = sd::FieldFloat(map, "restitution", 0.3f);
			object.physics.friction = sd::FieldFloat(map, "friction", 0.5f);

			Option<String> assigned = self->SpawnObject(std::move(object));
			if (assigned.IsNone())
				return Ok(Value::Nil());
			return Ok(Value::Str(assigned.Unwrap()));
		});

	vm.RegisterNamespacedNative(String("scene"), String("remove"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "scene.remove");
								   if (name.IsError())
									   return Err(name.Error());
								   return Ok(Value::Boolean(self->RemoveObject(name.Value())));
							   });

	vm.RegisterNamespacedNative(String("scene"), String("rename"), 2, 2,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto from = data::script::detail::ArgString(args, 0, "scene.rename");
								   if (from.IsError())
									   return Err(from.Error());
								   auto to = data::script::detail::ArgString(args, 1, "scene.rename");
								   if (to.IsError())
									   return Err(to.Error());
								   return Ok(Value::Boolean(self->RenameObject(from.Value(), to.Value())));
							   });

	vm.RegisterNamespacedNative(String("scene"), String("names"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   auto list = std::make_shared<data::script::ListObject>();
								   if (const SceneDesc *scene = self->ActiveScene())
									   for (scene::NodeId id : scene->Objects())
										   list->items.push_back(Value::Str(scene->tree.Get(id)->name));
								   return Ok(Value::List(std::move(list)));
							   });

	vm.RegisterNamespacedNative(String("scene"), String("count"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   const SceneDesc *scene = self->ActiveScene();
								   return Ok(Value::Number(scene ? double(scene->ObjectCount()) : 0.0));
							   });

	vm.RegisterNamespacedNative(String("scene"), String("exists"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "scene.exists");
								   if (name.IsError())
									   return Err(name.Error());
								   const SceneDesc *scene = self->ActiveScene();
								   return Ok(Value::Boolean(scene && scene->Find(name.Value()) != nullptr));
							   });

	vm.RegisterNamespacedNative(String("scene"), String("find_tag"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto tag = data::script::detail::ArgString(args, 0, "scene.find_tag");
								   if (tag.IsError())
									   return Err(tag.Error());
								   auto list = std::make_shared<data::script::ListObject>();
								   if (const SceneDesc *scene = self->ActiveScene())
									   for (scene::NodeId id : scene->WithTag(tag.Value()))
										   list->items.push_back(Value::Str(scene->tree.Get(id)->name));
								   return Ok(Value::List(std::move(list)));
							   });

	// ── object.* ─────────────────────────────────────────────────────────────

	vm.RegisterNamespacedNative(String("object"), String("position"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "object.position");
								   if (name.IsError())
									   return Err(name.Error());
								   const SceneDesc *scene = self->ActiveScene();
								   const scene::Node *object = scene ? scene->Find(name.Value()) : nullptr;
								   if (!object)
									   return Ok(Value::Nil());
								   return Ok(sd::Vec3ToValue(object->transform.position));
							   });

	vm.RegisterNamespacedNative(String("object"), String("set_position"), 4, 4,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "object.set_position");
								   if (name.IsError())
									   return Err(name.Error());
								   auto position = sd::ArgVec3(args, 1, "object.set_position");
								   if (position.IsError())
									   return Err(position.Error());
								   return Ok(Value::Boolean(self->SetPosition(name.Value(), position.Value())));
							   });

	vm.RegisterNamespacedNative(String("object"), String("rotation"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "object.rotation");
								   if (name.IsError())
									   return Err(name.Error());
								   const SceneDesc *scene = self->ActiveScene();
								   const scene::Node *object = scene ? scene->Find(name.Value()) : nullptr;
								   if (!object)
									   return Ok(Value::Nil());
								   return Ok(sd::Vec3ToValue(object->transform.EulerDegrees()));
							   });

	vm.RegisterNamespacedNative(String("object"), String("set_rotation"), 4, 4,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "object.set_rotation");
								   if (name.IsError())
									   return Err(name.Error());
								   auto euler = sd::ArgVec3(args, 1, "object.set_rotation");
								   if (euler.IsError())
									   return Err(euler.Error());
								   return Ok(Value::Boolean(self->SetEulerDegrees(name.Value(), euler.Value())));
							   });

	vm.RegisterNamespacedNative(String("object"), String("scale"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "object.scale");
								   if (name.IsError())
									   return Err(name.Error());
								   const SceneDesc *scene = self->ActiveScene();
								   const scene::Node *object = scene ? scene->Find(name.Value()) : nullptr;
								   if (!object)
									   return Ok(Value::Nil());
								   return Ok(sd::Vec3ToValue(object->transform.scale));
							   });

	vm.RegisterNamespacedNative(String("object"), String("set_scale"), 4, 4,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "object.set_scale");
								   if (name.IsError())
									   return Err(name.Error());
								   auto scale = sd::ArgVec3(args, 1, "object.set_scale");
								   if (scale.IsError())
									   return Err(scale.Error());
								   return Ok(Value::Boolean(self->SetScale(name.Value(), scale.Value())));
							   });

	vm.RegisterNamespacedNative(String("object"), String("set_color"), 4, 4,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "object.set_color");
								   if (name.IsError())
									   return Err(name.Error());
								   auto rgb = sd::ArgVec3(args, 1, "object.set_color");
								   if (rgb.IsError())
									   return Err(rgb.Error());
								   auto channel = [](float v) -> uint8_t {
									   return uint8_t(v < 0.f ? 0 : (v > 255.f ? 255 : v));
								   };
								   sdl3::Color color{channel(rgb.Value().x), channel(rgb.Value().y),
													 channel(rgb.Value().z), 255};
								   return Ok(Value::Boolean(self->SetMaterialColor(name.Value(), color)));
							   });

	vm.RegisterNamespacedNative(
		String("object"), String("set_material"), 2, 2,
		[self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
			auto name = data::script::detail::ArgString(args, 0, "object.set_material");
			if (name.IsError())
				return Err(name.Error());
			auto table = data::script::detail::ArgMap(args, 1, "object.set_material");
			if (table.IsError())
				return Err(table.Error());

			SceneDesc *scene = self->ActiveScene();
			scene::Node *object = scene ? scene->Find(name.Value()) : nullptr;
			if (!object)
				return Ok(Value::Boolean(false));

			// Partir de l'état COURANT : une table partielle ne modifie que
			// ce qu'elle mentionne (`{roughness: 0.1}` garde la couleur).
			MaterialDesc material = VisualDesc::Read(*object).material;
			const data::script::MapObject &map = *table.Value();
			material.kind = MaterialKindFromName(sd::FieldString(map, "kind", MaterialKindName(material.kind)))
								.UnwrapOr(material.kind);
			if (auto color = sd::FieldColor(map, "color"); color.IsSome())
				material.baseColor = color.Unwrap();
			material.metallic = sd::FieldFloat(map, "metallic", material.metallic);
			material.roughness = sd::FieldFloat(map, "roughness", material.roughness);
			material.doubleSided = sd::FieldBool(map, "double_sided", material.doubleSided);
			material.wireframe = sd::FieldBool(map, "wireframe", material.wireframe);
			return Ok(Value::Boolean(self->SetMaterial(name.Value(), material)));
		});

	vm.RegisterNamespacedNative(
		String("object"), String("set_physics"), 2, 2,
		[self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
			auto name = data::script::detail::ArgString(args, 0, "object.set_physics");
			if (name.IsError())
				return Err(name.Error());
			auto table = data::script::detail::ArgMap(args, 1, "object.set_physics");
			if (table.IsError())
				return Err(table.Error());

			SceneDesc *scene = self->ActiveScene();
			scene::Node *object = scene ? scene->Find(name.Value()) : nullptr;
			if (!object)
				return Ok(Value::Boolean(false));

			PhysicsDesc physics = PhysicsDesc::Read(*object);
			const data::script::MapObject &map = *table.Value();
			physics.body = BodyKindFromName(sd::FieldString(map, "body", BodyKindName(physics.body)))
							   .UnwrapOr(physics.body);
			physics.collider =
				ColliderKindFromName(sd::FieldString(map, "collider", ColliderKindName(physics.collider)))
					.UnwrapOr(physics.collider);
			physics.halfExtents = sd::FieldVec3(map, "half_extents", physics.halfExtents);
			physics.mass = sd::FieldFloat(map, "mass", physics.mass);
			physics.restitution = sd::FieldFloat(map, "restitution", physics.restitution);
			physics.friction = sd::FieldFloat(map, "friction", physics.friction);
			return Ok(Value::Boolean(self->SetPhysics(name.Value(), physics)));
		});

	vm.RegisterNamespacedNative(String("object"), String("set_visible"), 2, 2,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "object.set_visible");
								   if (name.IsError())
									   return Err(name.Error());
								   return Ok(Value::Boolean(self->SetVisible(name.Value(), args[1].IsTruthy())));
							   });

	vm.RegisterNamespacedNative(String("object"), String("velocity"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "object.velocity");
								   if (name.IsError())
									   return Err(name.Error());
								   Option<math::FVector3> velocity = self->GetVelocity(name.Value());
								   if (velocity.IsNone())
									   return Ok(Value::Nil());
								   return Ok(sd::Vec3ToValue(velocity.Unwrap()));
							   });

	vm.RegisterNamespacedNative(String("object"), String("set_velocity"), 4, 4,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "object.set_velocity");
								   if (name.IsError())
									   return Err(name.Error());
								   auto velocity = sd::ArgVec3(args, 1, "object.set_velocity");
								   if (velocity.IsError())
									   return Err(velocity.Error());
								   return Ok(Value::Boolean(self->SetVelocity(name.Value(), velocity.Value())));
							   });

	vm.RegisterNamespacedNative(String("object"), String("impulse"), 4, 4,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "object.impulse");
								   if (name.IsError())
									   return Err(name.Error());
								   auto impulse = sd::ArgVec3(args, 1, "object.impulse");
								   if (impulse.IsError())
									   return Err(impulse.Error());
								   return Ok(Value::Boolean(self->ApplyImpulse(name.Value(), impulse.Value())));
							   });

	vm.RegisterNamespacedNative(String("object"), String("distance"), 2, 2,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto first = data::script::detail::ArgString(args, 0, "object.distance");
								   if (first.IsError())
									   return Err(first.Error());
								   auto second = data::script::detail::ArgString(args, 1, "object.distance");
								   if (second.IsError())
									   return Err(second.Error());
								   const SceneDesc *scene = self->ActiveScene();
								   if (!scene)
									   return Ok(Value::Nil());
								   // Distance MONDE : deux objets peuvent être
								   // à des niveaux différents de la hiérarchie.
								   scene::NodeId a = scene->FindId(first.Value());
								   scene::NodeId b = scene->FindId(second.Value());
								   if (!a.Valid() || !b.Valid())
									   return Ok(Value::Nil());
								   math::FVector3 delta =
									   scene->tree.GlobalPosition(a) - scene->tree.GlobalPosition(b);
								   return Ok(Value::Number(double(delta.Length())));
							   });

	vm.RegisterNamespacedNative(String("scene"), String("import"), 1, 2,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto path = data::script::detail::ArgString(args, 0, "scene.import");
								   if (path.IsError())
									   return Err(path.Error());
								   float size = 3.f;
								   if (args.size() > 1) {
									   auto wanted = data::script::detail::ArgNumber(args, 1, "scene.import");
									   if (wanted.IsError())
										   return Err(wanted.Error());
									   size = float(wanted.Unwrap());
								   }
								   auto imported = self->ImportGltf(path.Value(), size);
								   if (imported.IsError()) {
									   self->LogError(String::Format("scene.import : %s", imported.Error().CStr()));
									   return Ok(Value::Nil());
								   }
								   return Ok(Value::Str(imported.Value()));
							   });

	// Inventaire d'un `.gltf` SANS charger sa géométrie : de quoi écrire un
	// script qui explore un dossier de modèles.
	vm.RegisterNamespacedNative(
		String("scene"), String("model_info"), 1, 1,
		[](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
			auto path = data::script::detail::ArgString(args, 0, "scene.model_info");
			if (path.IsError())
				return Err(path.Error());
			auto document = data::gltf::LoadFile(path.Value(), /*loadBuffers*/ false);
			if (document.IsError())
				return Ok(Value::Nil());

			auto info = std::make_shared<data::script::MapObject>();
			info->SetKey(String("version"), Value::Str(document.Value().version));
			info->SetKey(String("meshes"), Value::Number(double(document.Value().meshes.size())));
			info->SetKey(String("materials"), Value::Number(double(document.Value().materials.size())));
			info->SetKey(String("nodes"), Value::Number(double(document.Value().nodes.size())));
			auto images = std::make_shared<data::script::ListObject>();
			for (const data::gltf::Image &image : document.Value().images)
				images->items.push_back(Value::Str(document.Value().ResolveUri(image.uri)));
			info->SetKey(String("textures"), Value::List(std::move(images)));
			return Ok(Value::Map(std::move(info)));
		});

	// ── camera.* ─────────────────────────────────────────────────────────────

	vm.RegisterNamespacedNative(String("camera"), String("position"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   return Ok(sd::Vec3ToValue(self->ActiveCamera().position));
							   });

	vm.RegisterNamespacedNative(String("camera"), String("set_position"), 3, 3,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto position = sd::ArgVec3(args, 0, "camera.set_position");
								   if (position.IsError())
									   return Err(position.Error());
								   self->SetCameraPosition(position.Value());
								   return Ok(Value::Nil());
							   });

	vm.RegisterNamespacedNative(String("camera"), String("look"), 2, 2,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto yaw = data::script::detail::ArgNumber(args, 0, "camera.look");
								   if (yaw.IsError())
									   return Err(yaw.Error());
								   auto pitch = data::script::detail::ArgNumber(args, 1, "camera.look");
								   if (pitch.IsError())
									   return Err(pitch.Error());
								   self->ApplyLookDelta(float(yaw.Unwrap()), float(pitch.Unwrap()));
								   return Ok(Value::Nil());
							   });

	vm.RegisterNamespacedNative(String("camera"), String("move"), 3, 3,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto delta = sd::ArgVec3(args, 0, "camera.move");
								   if (delta.IsError())
									   return Err(delta.Error());
								   self->MoveCamera(delta.Value());
								   return Ok(Value::Nil());
							   });

	/// `camera.pan(droite, haut)` — glisse la caméra dans son plan d'écran.
	vm.RegisterNamespacedNative(String("camera"), String("pan"), 2, 2,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto right = data::script::detail::ArgNumber(args, 0, "camera.pan");
								   auto up = data::script::detail::ArgNumber(args, 1, "camera.pan");
								   if (right.IsError())
									   return Err(right.Error());
								   if (up.IsError())
									   return Err(up.Error());
								   self->PanCamera(float(right.Value()), float(up.Value()));
								   return Ok(Value::Nil());
							   });

	/// `camera.dolly(distance)` — avance (positif) ou recule le long de la visée.
	vm.RegisterNamespacedNative(String("camera"), String("dolly"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto distance = data::script::detail::ArgNumber(args, 0, "camera.dolly");
								   if (distance.IsError())
									   return Err(distance.Error());
								   self->DollyCamera(float(distance.Value()));
								   return Ok(Value::Nil());
							   });

	/// `camera.orbit(lacet, tangage)` — tourne autour de la sélection (ou de
	/// ce que la caméra regarde), en radians.
	vm.RegisterNamespacedNative(String("camera"), String("orbit"), 2, 2,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto yaw = data::script::detail::ArgNumber(args, 0, "camera.orbit");
								   auto pitch = data::script::detail::ArgNumber(args, 1, "camera.orbit");
								   if (yaw.IsError())
									   return Err(yaw.Error());
								   if (pitch.IsError())
									   return Err(pitch.Error());
								   self->OrbitCamera(float(yaw.Value()), float(pitch.Value()), self->CameraPivot());
								   return Ok(Value::Nil());
							   });

	/// `camera.pivot()` — point autour duquel `camera.orbit` tourne.
	vm.RegisterNamespacedNative(String("camera"), String("pivot"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   return Ok(sd::Vec3ToValue(self->CameraPivot()));
							   });

	// ── node.* : la HIÉRARCHIE vue par les scripts ──────────────────────────
	//
	// Les fonctions `object.*` ci-dessus désignent un objet par son nom et
	// ignorent l'arbre : elles restent la façon la plus courte d'écrire un
	// script. `node.*` complète, et ne double pas : parent, enfants, chemin,
	// création, reparentage, duplication — tout ce qui n'existe que parce
	// qu'une scène est un arbre. Un nœud s'y désigne par son NOM ou par son
	// CHEMIN (« /Scene/Voiture/Roues/AvantGauche »), au choix.

	vm.RegisterNamespacedNative(String("node"), String("path"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "node.path");
								   if (name.IsError())
									   return Err(name.Error());
								   const SceneDesc *sceneDesc = self->ActiveScene();
								   scene::NodeId id = sceneDesc ? sceneDesc->Resolve(name.Value()) : scene::NodeId{};
								   if (!id.Valid())
									   return Ok(Value::Nil());
								   return Ok(Value::Str(sceneDesc->tree.PathOf(id)));
							   });

	vm.RegisterNamespacedNative(String("node"), String("parent"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "node.parent");
								   if (name.IsError())
									   return Err(name.Error());
								   const SceneDesc *sceneDesc = self->ActiveScene();
								   scene::NodeId id = sceneDesc ? sceneDesc->Resolve(name.Value()) : scene::NodeId{};
								   if (!id.Valid())
									   return Ok(Value::Nil());
								   scene::NodeId parent = sceneDesc->tree.ParentOf(id);
								   // La racine de la scène n'est pas un objet :
								   // un objet de premier niveau n'a donc « pas
								   // de parent » du point de vue du script.
								   if (!parent.Valid() || parent == sceneDesc->tree.Root())
									   return Ok(Value::Nil());
								   return Ok(Value::Str(sceneDesc->tree.Get(parent)->name));
							   });

	vm.RegisterNamespacedNative(String("node"), String("children"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "node.children");
								   if (name.IsError())
									   return Err(name.Error());
								   auto list = std::make_shared<data::script::ListObject>();
								   const SceneDesc *sceneDesc = self->ActiveScene();
								   scene::NodeId id = sceneDesc ? sceneDesc->Resolve(name.Value()) : scene::NodeId{};
								   if (id.Valid())
									   for (scene::NodeId child : sceneDesc->tree.ChildrenOf(id))
										   list->items.push_back(Value::Str(sceneDesc->tree.Get(child)->name));
								   return Ok(Value::List(std::move(list)));
							   });

	vm.RegisterNamespacedNative(String("node"), String("find"), 1, 2,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto path = data::script::detail::ArgString(args, 0, "node.find");
								   if (path.IsError())
									   return Err(path.Error());
								   const SceneDesc *sceneDesc = self->ActiveScene();
								   if (!sceneDesc)
									   return Ok(Value::Nil());
								   // Second argument facultatif : le nœud de
								   // départ d'un chemin RELATIF (« ../Moteur »).
								   scene::NodeId base = sceneDesc->tree.Root();
								   if (args.size() > 1 && args[1].IsString())
									   base = sceneDesc->Resolve(args[1].AsString());
								   scene::NodeId found = sceneDesc->tree.Resolve(path.Value(), base);
								   if (!found.Valid())
									   return Ok(Value::Nil());
								   return Ok(Value::Str(sceneDesc->tree.Get(found)->name));
							   });

	vm.RegisterNamespacedNative(String("node"), String("world_position"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "node.world_position");
								   if (name.IsError())
									   return Err(name.Error());
								   const SceneDesc *sceneDesc = self->ActiveScene();
								   scene::NodeId id = sceneDesc ? sceneDesc->Resolve(name.Value()) : scene::NodeId{};
								   if (!id.Valid())
									   return Ok(Value::Nil());
								   return Ok(sd::Vec3ToValue(sceneDesc->tree.GlobalPosition(id)));
							   });

	vm.RegisterNamespacedNative(String("node"), String("create_group"), 1, 2,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "node.create_group");
								   if (name.IsError())
									   return Err(name.Error());
								   scene::NodeId parent;
								   if (args.size() > 1 && args[1].IsString())
									   parent = self->ResolveId(args[1].AsString());
								   Option<scene::NodeId> created = self->CreateGroup(name.Value(), parent);
								   if (created.IsNone())
									   return Ok(Value::Nil());
								   return Ok(Value::Str(self->FindObject(created.Unwrap())->name));
							   });

	vm.RegisterNamespacedNative(String("node"), String("reparent"), 2, 3,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto child = data::script::detail::ArgString(args, 0, "node.reparent");
								   if (child.IsError())
									   return Err(child.Error());
								   auto parent = data::script::detail::ArgString(args, 1, "node.reparent");
								   if (parent.IsError())
									   return Err(parent.Error());
								   // Troisième argument : « local » pour garder
								   // le transform LOCAL (le nœud suit son
								   // nouveau parent), sinon la position MONDE
								   // est préservée — le défaut, parce que c'est
								   // ce qu'on attend en réorganisant une scène.
								   scene::ReparentMode mode = scene::ReparentMode::KEEP_GLOBAL;
								   if (args.size() > 2 && args[2].IsString() && args[2].AsString() == String("local"))
									   mode = scene::ReparentMode::KEEP_LOCAL;
								   return Ok(Value::Boolean(self->ReparentNode(self->ResolveId(child.Value()),
																			  self->ResolveId(parent.Value()), mode)));
							   });

	vm.RegisterNamespacedNative(String("node"), String("duplicate"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "node.duplicate");
								   if (name.IsError())
									   return Err(name.Error());
								   Option<scene::NodeId> copy = self->DuplicateNode(self->ResolveId(name.Value()));
								   if (copy.IsNone())
									   return Ok(Value::Nil());
								   return Ok(Value::Str(self->FindObject(copy.Unwrap())->name));
							   });

	vm.RegisterNamespacedNative(String("node"), String("set_tag"), 2, 2,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "node.set_tag");
								   if (name.IsError())
									   return Err(name.Error());
								   auto tag = data::script::detail::ArgString(args, 1, "node.set_tag");
								   if (tag.IsError())
									   return Err(tag.Error());
								   return Ok(Value::Boolean(
									   self->SetTagOf(self->ResolveId(name.Value()), tag.Value())));
							   });

	/// Propriété LIBRE d'un nœud (`max_speed`, `team`…) : lecture avec deux
	/// arguments, écriture avec trois. C'est ce qui permet à un script de
	/// gameplay de lire des réglages posés dans l'inspecteur, sans qu'aucun
	/// des deux côtés ne connaisse le nom de la propriété à la compilation.
	vm.RegisterNamespacedNative(String("node"), String("prop"), 2, 3,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "node.prop");
								   if (name.IsError())
									   return Err(name.Error());
								   auto key = data::script::detail::ArgString(args, 1, "node.prop");
								   if (key.IsError())
									   return Err(key.Error());
								   scene::Node *node = self->FindObject(self->ResolveId(name.Value()));
								   if (!node)
									   return Ok(Value::Nil());
								   if (args.size() == 2) {
									   const scene::PropertyValue *value = node->Get(key.Value());
									   if (!value)
										   return Ok(Value::Nil());
									   switch (value->Type()) {
										   case scene::PropertyType::BOOL:
											   return Ok(Value::Boolean(value->AsBool()));
										   case scene::PropertyType::INT:
										   case scene::PropertyType::FLOAT:
											   return Ok(Value::Number(double(value->AsFloat())));
										   case scene::PropertyType::VEC3:
											   return Ok(sd::Vec3ToValue(value->AsVec3()));
										   default:
											   return Ok(Value::Str(value->ToDisplayString()));
									   }
								   }
								   // Écriture : le TYPE vient de la valeur du
								   // script, pas d'un argument supplémentaire.
								   const Value &value = args[2];
								   if (value.IsBoolean())
									   node->Set(key.Value(), scene::PropertyValue::Bool(value.IsTruthy()));
								   else if (value.IsNumber())
									   node->Set(key.Value(), scene::PropertyValue::Float(value.AsFloat()));
								   else
									   node->Set(key.Value(), scene::PropertyValue::Str(value.ToDisplayString()));
								   return Ok(Value::Boolean(true));
							   });

	vm.RegisterNamespacedNative(String("node"), String("save_scene"), 2, 2,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "node.save_scene");
								   if (name.IsError())
									   return Err(name.Error());
								   auto path = data::script::detail::ArgString(args, 1, "node.save_scene");
								   if (path.IsError())
									   return Err(path.Error());
								   auto saved = self->SavePackedScene(self->ResolveId(name.Value()), path.Value());
								   return Ok(Value::Boolean(saved.IsOk()));
							   });

	vm.RegisterNamespacedNative(String("node"), String("instantiate"), 1, 2,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto path = data::script::detail::ArgString(args, 0, "node.instantiate");
								   if (path.IsError())
									   return Err(path.Error());
								   scene::NodeId parent;
								   if (args.size() > 1 && args[1].IsString())
									   parent = self->ResolveId(args[1].AsString());
								   auto created = self->InstantiateSceneFile(path.Value(), parent);
								   if (created.IsError())
									   return Ok(Value::Nil());
								   return Ok(Value::Str(self->FindObject(created.Value())->name));
							   });

	// ── input.* ──────────────────────────────────────────────────────────────

	vm.RegisterNamespacedNative(String("input"), String("key"), 1, 1,
							   [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "input.key");
								   if (name.IsError())
									   return Err(name.Error());
								   Option<SDL_Keycode> code = sd::ScancodeFromName(name.Value());
								   if (code.IsNone())
									   return Err(Interpreter::MakeError(
										   String::Format("`input.key` : touche inconnue `%s`",
														  name.Value().CStr())));
								   return Ok(Value::Boolean(sdl3::keyboard::IsPressed(code.Unwrap())));
							   });

	/// Axe -1/0/+1 composé de deux touches — évite le `if a { -1 } else ...`
	/// répété dans tout script de contrôle de véhicule.
	vm.RegisterNamespacedNative(String("input"), String("axis"), 2, 2,
							   [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto negative = data::script::detail::ArgString(args, 0, "input.axis");
								   if (negative.IsError())
									   return Err(negative.Error());
								   auto positive = data::script::detail::ArgString(args, 1, "input.axis");
								   if (positive.IsError())
									   return Err(positive.Error());
								   Option<SDL_Keycode> low = sd::ScancodeFromName(negative.Value());
								   Option<SDL_Keycode> high = sd::ScancodeFromName(positive.Value());
								   if (low.IsNone() || high.IsNone())
									   return Err(Interpreter::MakeError(String("`input.axis` : touche inconnue")));
								   double value = 0.0;
								   if (sdl3::keyboard::IsPressed(low.Unwrap()))
									   value -= 1.0;
								   if (sdl3::keyboard::IsPressed(high.Unwrap()))
									   value += 1.0;
								   return Ok(Value::Number(value));
							   });

	/// `input.mouse_delta()` → [dx, dy] : mouvement de souris depuis l'image
	/// précédente, en pixels (non nul seulement en mode Jeu plein écran, où
	/// la souris est capturée pour regarder autour de soi).
	vm.RegisterNamespacedNative(String("input"), String("mouse_delta"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   auto list = std::make_shared<data::script::ListObject>();
								   list->items.push_back(Value::Number(double(self->m_mouseDelta.x)));
								   list->items.push_back(Value::Number(double(self->m_mouseDelta.y)));
								   return Ok(Value::List(std::move(list)));
							   });

	// ── light.* ──────────────────────────────────────────────────────────────
	// Les lumières se pilotent comme le reste : par le NOM du nœud, en
	// écrivant dans le document (le mode Jeu restaure tout à l'arrêt).

	/// Accès commun : le nœud nommé, s'il porte une lumière.
	auto lightNode = [self](const std::vector<Value> &args, const char *fnName) -> Result<scene::Node *, ScriptError> {
		auto name = data::script::detail::ArgString(args, 0, fnName);
		if (name.IsError())
			return Err(name.Error());
		scene::Node *node = self->FindObject(self->ResolveId(name.Value()));
		return Ok(node && LightDesc::Has(*node) ? node : nullptr);
	};

	vm.RegisterNamespacedNative(String("light"), String("intensity"), 1, 1,
							   [lightNode](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto node = lightNode(args, "light.intensity");
								   if (node.IsError())
									   return Err(node.Error());
								   if (!node.Value())
									   return Ok(Value::Nil());
								   return Ok(Value::Number(double(LightDesc::Read(*node.Value()).intensity)));
							   });

	vm.RegisterNamespacedNative(String("light"), String("set_intensity"), 2, 2,
							   [self, lightNode](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto node = lightNode(args, "light.set_intensity");
								   if (node.IsError())
									   return Err(node.Error());
								   auto value = data::script::detail::ArgNumber(args, 1, "light.set_intensity");
								   if (value.IsError())
									   return Err(value.Error());
								   if (!node.Value())
									   return Ok(Value::Boolean(false));
								   LightDesc light = LightDesc::Read(*node.Value());
								   light.intensity = float(sdl3::Max(0.0, value.Value()));
								   // Écriture directe (pas de SetLight) : appelé à chaque
								   // image, ça ne doit ni empiler d'historique ni recréer
								   // le repère.
								   light.Write(*node.Value());
								   (void)self;
								   return Ok(Value::Boolean(true));
							   });

	vm.RegisterNamespacedNative(String("light"), String("set_color"), 4, 4,
							   [lightNode](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto node = lightNode(args, "light.set_color");
								   if (node.IsError())
									   return Err(node.Error());
								   auto color = sd::ArgVec3(args, 1, "light.set_color");
								   if (color.IsError())
									   return Err(color.Error());
								   if (!node.Value())
									   return Ok(Value::Boolean(false));
								   auto channel = [](float v) { return uint8_t(sdl3::Clamp(v, 0.f, 255.f)); };
								   LightDesc light = LightDesc::Read(*node.Value());
								   light.color = sdl3::Color{channel(color.Value().x), channel(color.Value().y),
															 channel(color.Value().z), 255};
								   light.Write(*node.Value());
								   return Ok(Value::Boolean(true));
							   });

	/// `light.list()` : noms de toutes les lumières de la scène.
	vm.RegisterNamespacedNative(String("light"), String("list"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   auto list = std::make_shared<data::script::ListObject>();
								   const SceneDesc *scene = self->ActiveScene();
								   if (scene)
									   for (scene::NodeId id : self->m_lightIds)
										   if (const scene::Node *node = scene->tree.Get(id))
											   list->items.push_back(Value::Str(node->name));
								   return Ok(Value::List(std::move(list)));
							   });
}

} // namespace game_editor
