#pragma once
/**
 * level_editor::Runtime — la partie « moteur » de l'éditeur : transforme le
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
 * cette classe testable sans fenêtre (tests/level_editor_smoke_test.cpp).
 *
 * Aucune exception (cf. memory/feedback_no_exceptions.md) : les commandes
 * rendent `bool`/`Option`/`Result`, jamais d'échec silencieux ni de `throw`.
 */
#include <functional>
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

namespace level_editor {

// ============================================================================
// Composants ECS propres à l'éditeur
// ============================================================================

/// Relie une entité ECS à l'objet du DOCUMENT qu'elle incarne. Le nom (et non
/// un index) : les index bougent à chaque suppression, les noms non.
struct SceneObjectRef {
	String name;
};

/// Marqueur de sélection (une seule à la fois, cf. `Select`).
struct Selected {};

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
		m_gameplayVm.SetRandomSeed(seed);
		m_toolVm.SetRandomSeed(seed ^ 0x5DEECE66Dull);
	}

	// ── Accès ────────────────────────────────────────────────────────────────

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

	/// Appelé par l'interface pour afficher le nom de l'objet sélectionné.
	[[nodiscard]] Option<String> SelectedName() const {
		for (ecs::Entity entity : m_registry.EntitiesWith<Selected>())
			if (auto ref = m_registry.GetComponent<SceneObjectRef>(entity); ref.IsSome())
				return Some(ref.Unwrap()->name);
		return NONE;
	}

	[[nodiscard]] ObjectDesc *SelectedObject() noexcept {
		Option<String> name = SelectedName();
		if (name.IsNone())
			return nullptr;
		SceneDesc *scene = ActiveScene();
		return scene ? scene->Find(name.Unwrap()) : nullptr;
	}

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

		for (const ObjectDesc &object : scene->objects)
			InstantiateObject(object);
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

	/// Une image. Ordre : caméra → (mode Jeu : script, physique, animation,
	/// recopie vers le document) → synchronisation ECS→Object3D → portails.
	/// La synchronisation vient APRÈS toutes les écritures de l'image pour
	/// que le graphe rendu reflète l'état final, et les portails APRÈS elle
	/// car ils rendent la scène déjà à jour.
	void Update(float dt) {
		++m_frameIndex;
		const double tickRate = double(sdl3::GetPerformanceFrequency());
		uint64_t mark = sdl3::GetPerformanceCounter();

		UpdateCamera(dt);

		if (m_playing) {
			m_playTime += dt;
			RunGameplayHook(dt);
			m_mixer.Update(dt);
			UpdatePortalPlanes();
			m_world.Step(dt);
			MirrorSimulationToDocument();
		}

		render3d::SceneSyncSystem::Sync(m_registry, m_sceneRoot);
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

		m_playSnapshot = scene->objects;
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
		LogSuccess(String::Format("▶ Mode Jeu — scène « %s »", scene->name.CStr()));
	}

	/// Arrête le mode Jeu et restaure l'instantané : l'édition reprend
	/// exactement où elle en était.
	void Stop() {
		if (!m_playing)
			return;
		m_playing = false;
		if (SceneDesc *scene = ActiveScene()) {
			scene->objects = m_playSnapshot;
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
	bool Select(const String &objectName) {
		std::vector<ecs::Entity> previous = m_registry.EntitiesWith<Selected>();
		for (ecs::Entity entity : previous)
			m_registry.RemoveComponent<Selected>(entity);

		bool found = false;
		if (!objectName.IsEmpty()) {
			Option<ecs::Entity> entity = FindEntity(objectName);
			if (entity.IsSome()) {
				m_registry.AddComponent(entity.Unwrap(), Selected{});
				found = true;
			}
		}
		RefreshGizmo();
		if (onSelectionChanged)
			onSelectionChanged();
		return found;
	}

	void ClearSelection() { (void)Select(String()); }

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
		String name;
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
		for (const ObjectDesc &object : scene->objects) {
			if (!object.visible)
				continue;
			render3d::Object3D *node = FindNode(object.name);
			auto *shape = dynamic_cast<render3d::Shape *>(node);
			if (!shape)
				continue;
			Option<render3d::PickResult> hit = render3d::PickMeshFace(ray, *shape);
			if (hit.IsNone())
				continue;
			if (best.IsNone() || hit.Unwrap().t < best.Value().distance)
				best = Some(PickHit{object.name, hit.Unwrap().t, hit.Unwrap().point});
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
		(void)Select(hit.Value().name);
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
		const ObjectDesc *object = SelectedConstObject();
		if (!object)
			return 0.f;
		const float distance = (object->transform.position - ActiveCamera().position).Length();
		return sdl3::Max(distance * 0.14f, 0.25f);
	}

	/// Axe du manipulateur sous le rayon, NONE si le rayon passe à côté.
	/// Test « distance du rayon au segment de l'axe » avec une tolérance
	/// proportionnelle à la taille affichée : ce qui est visuellement épais
	/// est attrapable.
	[[nodiscard]] GizmoAxis PickGizmoAxis(const math::FRay &ray) const {
		const ObjectDesc *object = SelectedConstObject();
		if (!object)
			return GizmoAxis::NONE;
		const math::FVector3 origin = object->transform.position;
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
		const ObjectDesc *object = SelectedConstObject();
		if (axis == GizmoAxis::NONE || !object)
			return false;
		const math::FVector3 direction = AxisVector(axis);
		m_dragObject = object->name;
		m_dragAxis = axis;
		m_dragStartPosition = object->transform.position;
		m_dragStartEuler = object->transform.eulerDeg;
		m_dragStartScale = object->transform.scale;
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
				return SetPosition(m_dragObject, position);
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
		const ObjectDesc *object = SelectedConstObject();
		if (!object || axis == GizmoAxis::NONE)
			return false;
		const math::FVector3 origin = object->transform.position;
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
		const ObjectDesc *object = SelectedConstObject();
		const bool show = object != nullptr && !m_playing;
		m_gizmoRoot->SetVisible(show);
		if (!show)
			return;
		m_gizmoRoot->SetPosition(object->transform.position);
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
		String label;                    ///< « Déplacer Caisse », affiché dans le menu
		String sceneName;                ///< scène à laquelle l'instantané appartient
		std::vector<ObjectDesc> objects; ///< objets de cette scène avant la commande
		String selection;                ///< sélection avant la commande
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

	bool SetPosition(const String &objectName, const math::FVector3 &position) {
		ObjectDesc *object = FindObject(objectName);
		if (!object)
			return false;
		RecordHistory(String::Format("Déplacer %s", objectName.CStr()));
		object->transform.position = position;
		return PushTransform(*object);
	}

	bool SetEulerDegrees(const String &objectName, const math::FVector3 &eulerDeg) {
		ObjectDesc *object = FindObject(objectName);
		if (!object)
			return false;
		RecordHistory(String::Format("Tourner %s", objectName.CStr()));
		object->transform.eulerDeg = eulerDeg;
		return PushTransform(*object);
	}

	bool SetScale(const String &objectName, const math::FVector3 &scale) {
		ObjectDesc *object = FindObject(objectName);
		if (!object)
			return false;
		RecordHistory(String::Format("Redimensionner %s", objectName.CStr()));
		// Une échelle nulle produit une matrice singulière (objet invisible et
		// normales dégénérées) : on la borne plutôt que de l'accepter.
		object->transform.scale = {sdl3::Max(scale.x, MIN_SCALE), sdl3::Max(scale.y, MIN_SCALE),
								   sdl3::Max(scale.z, MIN_SCALE)};
		return PushTransform(*object);
	}

	bool SetVisible(const String &objectName, bool visible) {
		ObjectDesc *object = FindObject(objectName);
		if (!object)
			return false;
		RecordHistory(String::Format(visible ? "Afficher %s" : "Masquer %s", objectName.CStr()));
		object->visible = visible;
		if (render3d::Object3D *node = FindNode(objectName))
			node->SetVisible(visible);
		return true;
	}

	bool SetMaterial(const String &objectName, const MaterialDesc &material) {
		ObjectDesc *object = FindObject(objectName);
		if (!object)
			return false;
		RecordHistory(String::Format("Matériau de %s", objectName.CStr()));
		object->material = material;
		return PushMaterial(*object);
	}

	bool SetMaterialColor(const String &objectName, const sdl3::Color &color) {
		ObjectDesc *object = FindObject(objectName);
		if (!object)
			return false;
		RecordHistory(String::Format("Couleur de %s", objectName.CStr()));
		object->material.baseColor = color;
		return PushMaterial(*object);
	}

	bool SetPhysics(const String &objectName, const PhysicsDesc &physics) {
		ObjectDesc *object = FindObject(objectName);
		if (!object)
			return false;
		RecordHistory(String::Format("Physique de %s", objectName.CStr()));
		object->physics = physics;
		return PushPhysics(*object);
	}

	/// Ajoute un objet (nom rendu unique) et l'instancie. Rend le nom
	/// effectivement attribué, ou NONE s'il n'y a pas de scène active.
	[[nodiscard]] Option<String> SpawnObject(ObjectDesc object) {
		SceneDesc *scene = ActiveScene();
		if (!scene)
			return NONE;
		RecordHistory(String::Format("Ajouter %s", object.name.CStr()));
		String assigned = scene->Add(std::move(object));
		const ObjectDesc *added = scene->Find(assigned);
		if (!added)
			return NONE;
		InstantiateObject(*added);
		render3d::SceneSyncSystem::Sync(m_registry, m_sceneRoot);
		if (onSceneStructureChanged)
			onSceneStructureChanged();
		return Some(assigned);
	}

	bool RemoveObject(const String &objectName) {
		SceneDesc *scene = ActiveScene();
		if (!scene || !scene->Find(objectName))
			return false;
		RecordHistory(String::Format("Supprimer %s", objectName.CStr()));

		Option<String> selected = SelectedName();
		DestroyEntity(objectName);
		scene->Remove(objectName);
		// Les enfants viennent d'être détachés dans le document : on
		// reconstruit pour que le graphe 3D suive (RemoveChild a déjà
		// supprimé le nœud parent, ses enfants doivent remonter).
		RebuildRuntime();
		if (selected.IsSome() && selected.Unwrap() == objectName)
			ClearSelection();
		return true;
	}

	bool RenameObject(const String &objectName, const String &newName) {
		SceneDesc *scene = ActiveScene();
		if (!scene || newName.IsEmpty())
			return false;
		ObjectDesc *object = scene->Find(objectName);
		if (!object || scene->Find(newName))
			return false; // nom déjà pris : refus explicite plutôt que renommage surprise
		RecordHistory(String::Format("Renommer %s", objectName.CStr()));

		for (ObjectDesc &other : scene->objects)
			if (other.parent == objectName)
				other.parent = newName;
		object->name = newName;

		if (auto entity = FindEntity(objectName); entity.IsSome()) {
			if (auto ref = m_registry.GetComponent<SceneObjectRef>(entity.Unwrap()); ref.IsSome())
				ref.Unwrap()->name = newName;
			if (render3d::Object3D *node = FindNode(objectName))
				node->SetName(newName);
		}
		if (onSceneStructureChanged)
			onSceneStructureChanged();
		return true;
	}

	// ── Physique pilotée par script ──────────────────────────────────────────

	[[nodiscard]] Option<math::FVector3> GetVelocity(const String &objectName) {
		Option<ecs::Entity> entity = FindEntity(objectName);
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
		if (const ObjectDesc *object = SelectedConstObject())
			return object->transform.position;
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
	bool FocusOn(const String &objectName) {
		ObjectDesc *object = FindObject(objectName);
		if (!object)
			return false;
		float radius = sdl3::Max(sdl3::Max(object->dimensions.x, object->dimensions.y), object->dimensions.z);
		radius = sdl3::Max(radius * sdl3::Max(object->transform.scale.x, object->transform.scale.y), 1.f);
		math::FVector3 forward = ForwardFromYawPitch(m_editYaw, m_editPitch);
		m_editCamera.position = object->transform.position - forward * (radius * 4.f);
		m_editCamera.target = object->transform.position;
		return true;
	}

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

	// ── Manipulateur : mathématiques et nœuds ────────────────────────────────

	[[nodiscard]] const ObjectDesc *SelectedConstObject() const {
		Option<String> name = SelectedName();
		const SceneDesc *scene = ActiveScene();
		if (name.IsNone() || !scene)
			return nullptr;
		return scene->Find(name.Unwrap());
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
		return String::Format("%s %s (%s)", action, m_dragObject.CStr(), letter);
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
			step.objects = scene->objects;
		}
		Option<String> selected = SelectedName();
		step.selection = selected.IsSome() ? selected.Unwrap() : String();
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
		scene->objects = step.objects;
		RebuildRuntime();
		if (!step.selection.IsEmpty())
			(void)Select(step.selection);
		m_restoring = false;
		return true;
	}

	// ── Recherche ────────────────────────────────────────────────────────────

	[[nodiscard]] ObjectDesc *FindObject(const String &objectName) noexcept {
		SceneDesc *scene = ActiveScene();
		return scene ? scene->Find(objectName) : nullptr;
	}

	[[nodiscard]] Option<ecs::Entity> FindEntity(const String &objectName) const {
		for (ecs::Entity entity : m_registry.EntitiesWith<SceneObjectRef>()) {
			auto ref = m_registry.GetComponent<SceneObjectRef>(entity);
			if (ref.IsSome() && ref.Unwrap()->name == objectName)
				return Some(entity);
		}
		return NONE;
	}

	[[nodiscard]] render3d::Object3D *FindNode(const String &objectName) {
		Option<ecs::Entity> entity = FindEntity(objectName);
		if (entity.IsNone())
			return nullptr;
		auto node = m_registry.GetComponent<render3d::SceneNode>(entity.Unwrap());
		return node.IsSome() ? node.Unwrap()->node : nullptr;
	}

	// ── Construction ─────────────────────────────────────────────────────────

	[[nodiscard]] static render3d::Mesh BuildMesh(const ObjectDesc &object) {
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
	[[nodiscard]] render3d::Mesh MakeMeshFor(const ObjectDesc &object) {
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

	[[nodiscard]] static physics::Shape BuildCollider(const ObjectDesc &object) {
		const math::FVector3 half = object.physics.halfExtents;
		switch (object.physics.collider) {
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

	ecs::Entity InstantiateObject(const ObjectDesc &object) {
		auto &shape = static_cast<render3d::Shape &>(
			m_sceneRoot.Add(std::make_unique<render3d::Shape>(MakeMeshFor(object), BuildMaterial(object.material))));
		shape.SetName(object.name);
		shape.SetPosition(object.transform.position);
		shape.SetRotation(object.transform.Rotation());
		shape.SetScale(object.transform.scale);
		shape.SetVisible(object.visible);

		ecs::Entity entity = m_registry.Spawn();
		m_registry.AddComponent(entity, SceneObjectRef{object.name});
		m_registry.AddComponent(entity, render3d::SceneNode{&shape});
		m_registry.AddComponent(entity,
								render3d::SceneTransform{object.transform.position, object.transform.Rotation(),
														 object.transform.scale});
		AttachBody(entity, object);
		return entity;
	}

	void AttachBody(ecs::Entity entity, const ObjectDesc &object) {
		if (object.physics.body == BodyKind::NONE) {
			m_registry.RemoveComponent<physics::RigidBody>(entity);
			return;
		}
		physics::RigidBody body =
			object.physics.body == BodyKind::DYNAMIC
				? physics::RigidBody::MakeDynamic(BuildCollider(object), object.transform.position,
												  sdl3::Max(object.physics.mass, 0.001f), object.physics.restitution,
												  object.physics.friction)
				: physics::RigidBody::MakeStatic(BuildCollider(object), object.transform.position,
												 object.transform.Rotation(), object.physics.restitution,
												 object.physics.friction);
		body.orientation = object.transform.Rotation();
		m_registry.AddComponent(entity, body);
	}

	void ApplyParentLinks(const SceneDesc &scene) {
		for (const ObjectDesc &object : scene.objects) {
			if (object.parent.IsEmpty())
				continue;
			Option<ecs::Entity> child = FindEntity(object.name);
			Option<ecs::Entity> parent = FindEntity(object.parent);
			if (child.IsSome() && parent.IsSome())
				render3d::SetSceneParent(m_registry, child.Unwrap(), parent.Unwrap());
		}
	}

	/// Relie une paire de portails repérée par les étiquettes `portal_a` /
	/// `portal_b` — une scène sans ces étiquettes n'a simplement pas de
	/// portail, ce qui est le cas de toutes sauf la vitrine.
	void LinkPortals(const SceneDesc &scene) {
		m_portalA.node = m_portalB.node = nullptr;
		m_portalA.linkedPortal = m_portalB.linkedPortal = nullptr;
		m_portalsReady = false;

		std::vector<const ObjectDesc *> first = scene.WithTag(String("portal_a"));
		std::vector<const ObjectDesc *> second = scene.WithTag(String("portal_b"));
		if (first.empty() || second.empty())
			return;

		m_portalA.node = FindNode(first.front()->name);
		m_portalB.node = FindNode(second.front()->name);
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

		m_portalA.node = m_portalB.node = nullptr;
		m_portalA.linkedPortal = m_portalB.linkedPortal = nullptr;
		m_portalsReady = false;
		m_world.portalPlanes.clear();
	}

	void DestroyEntity(const String &objectName) {
		Option<ecs::Entity> entity = FindEntity(objectName);
		if (entity.IsNone())
			return;
		render3d::Object3D *node = nullptr;
		if (auto sceneNode = m_registry.GetComponent<render3d::SceneNode>(entity.Unwrap()); sceneNode.IsSome())
			node = sceneNode.Unwrap()->node;
		m_registry.Despawn(entity.Unwrap());
		if (node)
			(void)m_sceneRoot.RemoveChild(node);
	}

	// ── Poussée document -> runtime ──────────────────────────────────────────

	bool PushTransform(const ObjectDesc &object) {
		NotifyObjectChanged();
		Option<ecs::Entity> entity = FindEntity(object.name);
		if (entity.IsNone())
			return false;
		if (auto transform = m_registry.GetComponent<render3d::SceneTransform>(entity.Unwrap()); transform.IsSome()) {
			transform.Unwrap()->position = object.transform.position;
			transform.Unwrap()->rotation = object.transform.Rotation();
			transform.Unwrap()->scale = object.transform.scale;
		}
		if (auto body = m_registry.GetComponent<physics::RigidBody>(entity.Unwrap()); body.IsSome()) {
			body.Unwrap()->position = object.transform.position;
			body.Unwrap()->orientation = object.transform.Rotation();
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

	bool PushMaterial(const ObjectDesc &object) {
		NotifyObjectChanged();
		render3d::Object3D *node = FindNode(object.name);
		if (!node)
			return false;
		auto *shape = dynamic_cast<render3d::Shape *>(node);
		if (!shape || shape->Materials().empty())
			return false;
		render3d::Material material = BuildMaterial(object.material);
		for (render3d::Material &slot : shape->Materials())
			slot = material;
		return true;
	}

	bool PushPhysics(const ObjectDesc &object) {
		NotifyObjectChanged();
		Option<ecs::Entity> entity = FindEntity(object.name);
		if (entity.IsNone())
			return false;
		AttachBody(entity.Unwrap(), object);
		return true;
	}

	/// Remet chaque corps à la transformation du document — appelé au
	/// démarrage du mode Jeu pour que chaque partie reparte identique.
	void ResetPhysicsBodies() {
		SceneDesc *scene = ActiveScene();
		if (!scene)
			return;
		for (const ObjectDesc &object : scene->objects)
			if (object.physics.body != BodyKind::NONE)
				(void)PushTransform(object);
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
			transform.Unwrap()->position = body.Unwrap()->position;
			transform.Unwrap()->rotation = body.Unwrap()->orientation;
			if (ObjectDesc *object = scene->Find(ref.Unwrap()->name)) {
				object->transform.position = body.Unwrap()->position;
				object->transform.SetRotation(body.Unwrap()->orientation);
			}
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
			if (const ObjectDesc *target = FollowTarget()) {
				// La caméra se place derrière l'objet SELON SON PROPRE CAP
				// (son lacet), pas selon l'orientation libre de la caméra :
				// c'est ce qui donne une vraie caméra de poursuite. Utiliser
				// m_playYaw laissait la caméra traîner dans une direction
				// fixe du monde, et la voiture partait de côté ou vers
				// l'objectif dès le premier virage.
				constexpr float DEG2RAD = 3.14159265358979323846f / 180.f;
				float targetYaw = target->transform.eulerDeg.y * DEG2RAD;
				// Au-delà d'une vitesse notable, le cap est pris sur le
				// VECTEUR VITESSE plutôt que sur le lacet : c'est la vraie
				// direction de déplacement, insensible aux à-coups de
				// l'orientation lors d'un contact avec un rail.
				Option<math::FVector3> velocity = GetVelocity(target->name);
				if (velocity.IsSome()) {
					math::FVector3 flat{velocity.Unwrap().x, 0.f, velocity.Unwrap().z};
					if (flat.LengthSq() > 4.f)
						targetYaw = sdl3::Atan2(flat.x, flat.z);
				}
				math::FVector3 forward = ForwardFromYawPitch(targetYaw, -0.18f);
				math::FVector3 desired =
					target->transform.position - forward * m_followDistance + math::FVector3{0.f, m_followHeight, 0.f};
				// Lissage exponentiel indépendant de la fréquence d'image.
				float blend = sdl3::Clamp(dt * m_followStiffness, 0.f, 1.f);
				m_playCamera.position += (desired - m_playCamera.position) * blend;
				m_playCamera.target = target->transform.position + math::FVector3{0.f, 1.2f, 0.f};
				// Le cap libre suit la caméra de poursuite : reprendre la main
				// (mode Édition, ou une scène sans cible) ne fait alors pas
				// sauter la vue.
				m_playYaw = targetYaw;
				return;
			}
		}
		RefreshCameraTargets();
	}

	[[nodiscard]] const ObjectDesc *FollowTarget() const {
		const SceneDesc *scene = ActiveScene();
		if (!scene)
			return nullptr;
		std::vector<const ObjectDesc *> targets = scene->WithTag(String("camera_target"));
		return targets.empty() ? nullptr : targets.front();
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
	std::vector<ObjectDesc> m_playSnapshot;

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
	String m_dragObject;
	math::FVector3 m_dragStartPosition, m_dragStartEuler, m_dragStartScale{1.f, 1.f, 1.f};
	math::FVector3 m_dragStartVector;
	float m_dragStartParam = 0.f;
	float m_dragSize = 1.f;

	data::script::Interpreter m_gameplayVm;
	data::script::Interpreter m_toolVm;
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
[[nodiscard]] inline Option<SDL_Scancode> ScancodeFromName(const String &name) {
	struct Entry {
		const char *name;
		SDL_Scancode code;
	};
	static constexpr Entry TABLE[] = {
		{"w", SDL_SCANCODE_W},         {"a", SDL_SCANCODE_A},          {"s", SDL_SCANCODE_S},
		{"d", SDL_SCANCODE_D},         {"q", SDL_SCANCODE_Q},          {"e", SDL_SCANCODE_E},
		{"r", SDL_SCANCODE_R},         {"f", SDL_SCANCODE_F},          {"up", SDL_SCANCODE_UP},
		{"down", SDL_SCANCODE_DOWN},   {"left", SDL_SCANCODE_LEFT},    {"right", SDL_SCANCODE_RIGHT},
		{"space", SDL_SCANCODE_SPACE}, {"shift", SDL_SCANCODE_LSHIFT}, {"ctrl", SDL_SCANCODE_LCTRL},
		{"alt", SDL_SCANCODE_LALT},    {"tab", SDL_SCANCODE_TAB},      {"enter", SDL_SCANCODE_RETURN},
		{"escape", SDL_SCANCODE_ESCAPE},
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
			object.transform.eulerDeg = sd::FieldVec3(map, "rot", math::FVector3{});
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
									   for (const ObjectDesc &object : scene->objects)
										   list->items.push_back(Value::Str(object.name));
								   return Ok(Value::List(std::move(list)));
							   });

	vm.RegisterNamespacedNative(String("scene"), String("count"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   const SceneDesc *scene = self->ActiveScene();
								   return Ok(Value::Number(scene ? double(scene->objects.size()) : 0.0));
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
									   for (const ObjectDesc *object : scene->WithTag(tag.Value()))
										   list->items.push_back(Value::Str(object->name));
								   return Ok(Value::List(std::move(list)));
							   });

	// ── object.* ─────────────────────────────────────────────────────────────

	vm.RegisterNamespacedNative(String("object"), String("position"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "object.position");
								   if (name.IsError())
									   return Err(name.Error());
								   const SceneDesc *scene = self->ActiveScene();
								   const ObjectDesc *object = scene ? scene->Find(name.Value()) : nullptr;
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
								   const ObjectDesc *object = scene ? scene->Find(name.Value()) : nullptr;
								   if (!object)
									   return Ok(Value::Nil());
								   return Ok(sd::Vec3ToValue(object->transform.eulerDeg));
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
								   const ObjectDesc *object = scene ? scene->Find(name.Value()) : nullptr;
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
			ObjectDesc *object = scene ? scene->Find(name.Value()) : nullptr;
			if (!object)
				return Ok(Value::Boolean(false));

			// Partir de l'état COURANT : une table partielle ne modifie que
			// ce qu'elle mentionne (`{roughness: 0.1}` garde la couleur).
			MaterialDesc material = object->material;
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
			ObjectDesc *object = scene ? scene->Find(name.Value()) : nullptr;
			if (!object)
				return Ok(Value::Boolean(false));

			PhysicsDesc physics = object->physics;
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
								   const ObjectDesc *a = scene->Find(first.Value());
								   const ObjectDesc *b = scene->Find(second.Value());
								   if (!a || !b)
									   return Ok(Value::Nil());
								   math::FVector3 delta = a->transform.position - b->transform.position;
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

	// ── input.* ──────────────────────────────────────────────────────────────

	vm.RegisterNamespacedNative(String("input"), String("key"), 1, 1,
							   [](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "input.key");
								   if (name.IsError())
									   return Err(name.Error());
								   Option<SDL_Scancode> code = sd::ScancodeFromName(name.Value());
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
								   Option<SDL_Scancode> low = sd::ScancodeFromName(negative.Value());
								   Option<SDL_Scancode> high = sd::ScancodeFromName(positive.Value());
								   if (low.IsNone() || high.IsNone())
									   return Err(Interpreter::MakeError(String("`input.axis` : touche inconnue")));
								   double value = 0.0;
								   if (sdl3::keyboard::IsPressed(low.Unwrap()))
									   value -= 1.0;
								   if (sdl3::keyboard::IsPressed(high.Unwrap()))
									   value += 1.0;
								   return Ok(Value::Number(value));
							   });
}

} // namespace level_editor
