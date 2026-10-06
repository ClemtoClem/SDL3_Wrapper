

#include "runtime.hpp"

namespace game_editor {

// ── LogEntry ─────────────────────────────────────────────────────────────────

const char * LogEntry::LevelName() const noexcept {
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

// ── Runtime ──────────────────────────────────────────────────────────────────

Runtime::Runtime(ecs::ArchetypeRegistry &registry, int jobWorkers)
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
	BindEcsWorld();
	ResetGameplayVm();
	InstallHostApi(m_toolVm);
}

Runtime::~Runtime() {
	// L'interface qui a branché ces rappels est peut-être déjà détruite :
	// plus aucune notification vers elle.
	onGameUiReset = nullptr;
	onLog = nullptr;
	onSceneStructureChanged = nullptr;
	onProjectChanged = nullptr;
	onSelectionChanged = nullptr;
	onObjectChanged = nullptr;
	onScreenshot = nullptr;
	onThemeChange = nullptr;
	onPanelFocus = nullptr;
	onUiCommand = nullptr;
	onStatusMessage = nullptr;
	onQueryFps = nullptr;
	onHistoryChanged = nullptr;
	// Les bases des objets de script retirent leurs nœuds et leurs corps : il
	// faut un runtime ENTIER pour cela, donc avant la destruction des membres.
	if (m_playing) {
		EndScenePlay();
		m_playing = false;
	}
	(void)DestroyAllScriptObjects();
}

size_t NodeScriptInstance::LiveCount() const noexcept {
	size_t n = 0;
	for (const Attached &entry : attached)
		n += entry.detached ? 0 : 1;
	return n;
}

std::vector<data::script::OwnerInfo> Runtime::LiveOwners() const {
	std::vector<data::script::OwnerInfo> out;
	auto append = [&out](const data::script::Interpreter &vm) {
		std::vector<data::script::OwnerInfo> part = vm.Owners().Describe();
		out.insert(out.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
	};
	if (m_gameplayVm)
		append(*m_gameplayVm);
	for (const auto &instance : m_nodeScripts)
		if (instance->vm)
			append(*instance->vm);
	append(m_toolVm);
	return out;
}

String ScriptObjectInfo::Signature() const {
	String list;
	for (const String &base : bases) {
		list.Concat(list.IsEmpty() ? "" : ", ");
		list.Concat(base);
	}
	return list.IsEmpty() ? className : String::Format("%s (%s)", className.CStr(), list.CStr());
}

std::vector<std::pair<String, String>> ScriptObjectInfo::Fields() const {
	std::vector<std::pair<String, String>> out;
	if (!object.IsInstance() || !object.AsInstance())
		return out;
	for (const data::script::FieldSlot &slot : object.AsInstance()->fields.Snapshot())
		out.emplace_back(slot.name, slot.value.ToDisplayString());
	return out;
}

std::vector<ScriptObjectInfo> Runtime::LiveScriptObjects() {
	std::vector<ScriptObjectInfo> out;
	auto collect = [&](data::script::Interpreter &vm, const String &origin, const NodeScriptInstance *script) {
		for (const auto &instance : vm.Owners().Snapshot()) {
			ScriptObjectInfo info;
			info.vm = &vm;
			info.object = data::script::Value::Instance(instance);
			info.origin = origin;
			info.className = instance->klass ? instance->klass->Name() : String("?");
			info.destroyed = instance->destroyed;
			for (const auto &base : instance->owners) {
				if (!base || !base->type)
					continue;
				info.bases.push_back(engine_base::ShortName(base->type->name).UnwrapOr(base->type->name));
				if (auto carrier = std::dynamic_pointer_cast<NodeOwner>(base); carrier && carrier->Node())
					info.node = carrier->node;
			}
			if (script)
				for (const NodeScriptInstance::Attached &entry : script->attached)
					if (!entry.detached && entry.object.IsInstance() && entry.object.AsInstance() == instance)
						info.attached = true;
			out.push_back(std::move(info));
		}
	};
	if (m_gameplayVm)
		collect(*m_gameplayVm, String("scène"), nullptr);
	for (const auto &script : m_nodeScripts)
		if (script->vm)
			collect(*script->vm, script->script, script.get());
	collect(m_toolVm, String("outil"), nullptr);
	return out;
}

std::vector<ScriptObjectInfo> Runtime::ScriptObjectsOf(scene::NodeId id) {
	std::vector<ScriptObjectInfo> out;
	if (!id.Valid())
		return out;
	for (ScriptObjectInfo &info : LiveScriptObjects())
		if (info.node == id)
			out.push_back(std::move(info));
	return out;
}

bool Runtime::DestroyScriptObject(const ScriptObjectInfo &info) {
	if (!info.vm || !info.object.IsInstance() || !info.object.AsInstance())
		return false;
	const std::shared_ptr<data::script::InstanceObject> instance = info.object.AsInstance();
	if (instance->destroyed)
		return false;
	// L'interpréteur doit encore exister : on ne détruit que ce qu'un
	// registre vivant tient.
	bool known = m_gameplayVm && info.vm == m_gameplayVm.get();
	for (const auto &script : m_nodeScripts)
		known = known || info.vm == script->vm.get();
	known = known || info.vm == &m_toolVm;
	if (!known)
		return false;
	info.vm->DestroyInstance(instance);
	if (onSceneStructureChanged)
		onSceneStructureChanged();
	return true;
}

Option<String> Runtime::ModuleSource(const String &specifier) const {
	String name = specifier;
	if (name.EndsWith(".script"))
		name = name.Substring(0, name.GetSize() - 7);
	if (const ScriptAsset *asset = m_project.FindScript(name))
		return Some(asset->source);
	const String directory = ProjectDirectory();
	if (directory.IsEmpty())
		return NONE;
	auto text = data::script::LoadScriptFile(directory + String("/scripts/") + name + String(".script"));
	if (text.IsError())
		return NONE;
	return Some(text.Unwrap());
}

ScriptOutline Runtime::OutlineScript(const String &source, ScriptUse use) const {
	return game_editor::OutlineScript(source, use, [this](const String &specifier) { return ModuleSource(specifier); });
}

size_t Runtime::DestroyAllScriptObjects() {
	size_t count = 0;
	// Ordre inverse de la création : nœuds, puis scène, puis outil.
	for (auto it = m_nodeScripts.rbegin(); it != m_nodeScripts.rend(); ++it)
		if ((*it)->vm) {
			count += (*it)->vm->Owners().Count();
			(*it)->vm->Owners().DestroyAll(*(*it)->vm);
		}
	if (m_gameplayVm) {
		count += m_gameplayVm->Owners().Count();
		m_gameplayVm->Owners().DestroyAll(*m_gameplayVm);
	}
	count += m_toolVm.Owners().Count();
	m_toolVm.Owners().DestroyAll(m_toolVm);
	return count;
}

void Runtime::AttachCanvas(render3d::Canvas &canvas) {
	m_canvas = &canvas;
	ApplyEnvironment();
}

void Runtime::SetRandomSeed(uint64_t seed) {
	m_randomSeed = seed;
	m_gameplayVm->SetRandomSeed(seed);
	m_toolVm.SetRandomSeed(seed ^ 0x5DEECE66Dull);
}

void Runtime::PumpScriptThreads() {
	m_toolVm.PumpMainThread();
	m_gameplayVm->PumpMainThread();
	for (size_t i = 0; i < m_nodeScripts.size(); ++i)
		if (m_nodeScripts[i]->vm)
			m_nodeScripts[i]->vm->PumpMainThread();
}

const render3d::Camera & Runtime::ActiveCamera() const noexcept {
	return m_playing ? m_playCamera : m_editCamera;
}

Option<String> Runtime::SelectedName() const {
	const scene::Node *node = SelectedConstObject();
	if (!node)
		return NONE;
	return Some(node->name);
}

scene::NodeTree * Runtime::Tree() noexcept {
	SceneDesc *scene = ActiveScene();
	return scene ? &scene->tree : nullptr;
}

const scene::NodeTree * Runtime::Tree() const noexcept {
	const SceneDesc *scene = ActiveScene();
	return scene ? &scene->tree : nullptr;
}

scene::NodeId Runtime::ResolveId(const String &objectName) const {
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

scene::Node * Runtime::FindObject(const String &objectName) noexcept {
	scene::NodeTree *tree = Tree();
	scene::NodeId id = ResolveId(objectName);
	return tree && id.Valid() ? tree->Get(id) : nullptr;
}

scene::Node * Runtime::FindObject(scene::NodeId id) noexcept {
	scene::NodeTree *tree = Tree();
	return tree ? tree->Get(id) : nullptr;
}

Option<ecs::Entity> Runtime::FindEntity(scene::NodeId id) const {
	auto found = m_entityOf.find(id);
	if (found == m_entityOf.end())
		return NONE;
	return Some(found->second);
}

Option<ecs::Entity> Runtime::FindEntity(const String &objectName) const {
	return FindEntity(ResolveId(objectName));
}

render3d::Object3D * Runtime::FindNode(scene::NodeId id) {
	Option<ecs::Entity> entity = FindEntity(id);
	if (entity.IsNone())
		return nullptr;
	auto node = m_registry.GetComponent<render3d::SceneNode>(entity.Unwrap());
	return node.IsSome() ? node.Unwrap()->node : nullptr;
}

void Runtime::Log(LogLevel level, String text) {
	m_log.push_back(LogEntry{level, m_frameIndex, std::move(text)});
	// Le journal d'un éditeur qui tourne longtemps ne doit pas grossir
	// sans fin : on garde une fenêtre glissante des dernières entrées.
	if (m_log.size() > MAX_LOG_ENTRIES)
		m_log.erase(m_log.begin(), m_log.begin() + ptrdiff_t(m_log.size() - MAX_LOG_ENTRIES));
	if (onLog)
		onLog(m_log.back());
}

void Runtime::OpenProject(Project project) {
	m_project = std::move(project);
	m_hasProject = true;
	m_projectPath = String();
	m_projectFiles.clear();
	RebuildRuntime();
	LogSuccess(String::Format("Projet « %s » chargé (%d scènes, %d objets)", m_project.name.CStr(),
							  int(m_project.scenes.size()), int(m_project.TotalObjectCount())));
}

String Runtime::ProjectDirectory() const {
	return m_projectPath.IsEmpty() ? String() : files::DirectoryOf(m_projectPath);
}

void Runtime::CloseProject() {
	if (m_playing)
		Stop();
	m_project = Project{};
	m_hasProject = false;
	m_projectPath = String();
	m_projectFiles.clear();
	RebuildRuntime();
}

Result<bool, String> Runtime::LoadProjectFile(const String &path) {
	const String manifest = files::ResolveManifest(path);
	if (manifest.IsEmpty())
		return Err(String::Format("aucun projet trouvé : %s", path.CStr()));
	files::FileList projectFiles;
	auto project = files::LoadProject(manifest, &projectFiles);
	if (project.IsError())
		return Err(project.Error());
	OpenProject(std::move(project).Unwrap());
	m_projectPath = manifest;
	m_projectFiles = std::move(projectFiles);
	return Ok(true);
}

Result<bool, String> Runtime::SaveProjectFile(const String &path) {
	if (!m_hasProject)
		return Err(String("aucun projet ouvert"));
	auto written = files::SaveProject(m_project, path, path == m_projectPath ? m_projectFiles : files::FileList{});
	if (written.IsError())
		return Err(written.Error());
	m_projectPath = path;
	m_projectFiles = std::move(written).Unwrap();
	return Ok(true);
}

Result<bool, String> Runtime::SaveProject() {
	if (m_projectPath.IsEmpty())
		return Err(String("le projet n'a pas encore d'emplacement : créez-le ou ouvrez-le depuis le disque"));
	return SaveProjectFile(m_projectPath);
}

Result<bool, String> Runtime::CreateProject(const String &root, const String &name) {
	const String clean = name.Trim();
	if (clean.IsEmpty())
		return Err(String("nom de projet vide"));
	auto manifest = files::CreateProjectDirectory(root, clean);
	if (manifest.IsError())
		return Err(manifest.Error());
	OpenProject(files::MakeBlankProject(clean));
	auto saved = SaveProjectFile(manifest.Value());
	if (saved.IsError())
		return saved;
	LogSuccess(String::Format("Projet « %s » créé : %s", clean.CStr(), manifest.Value().CStr()));
	return Ok(true);
}

Result<bool, String> Runtime::SaveSceneAs(const String &path) const {
	const SceneDesc *scene = ActiveScene();
	if (!scene)
		return Err(String("aucune scène active"));
	return files::SaveSceneFile(*scene, path);
}

Result<String, String> Runtime::ImportSceneFile(const String &path) {
	if (!m_hasProject)
		return Err(String("aucun projet ouvert"));
	auto loaded = files::LoadSceneFile(path);
	if (loaded.IsError())
		return Err(loaded.Error());
	SceneDesc scene = std::move(loaded).Unwrap();
	scene.SetName(UniqueSceneName(scene.name));
	const String name = scene.name;
	m_project.scenes.push_back(std::move(scene));
	(void)SwitchScene(name);
	return Ok(name);
}

String Runtime::AddEmptyScene(const String &base) {
	SceneDesc scene;
	scene.SetName(UniqueSceneName(base));
	const String name = scene.name;
	m_project.scenes.push_back(std::move(scene));
	(void)SwitchScene(name);
	return name;
}

Result<bool, String> Runtime::SaveScriptAs(const String &name, const String &path) const {
	if (name.StartsWith("@")) {
		const SceneDesc *scene = m_project.FindScene(name.Substr(1));
		if (!scene)
			return Err(String::Format("scène introuvable : %s", name.Substr(1).CStr()));
		return files::SaveScriptFile(scene->gameplayScript, path);
	}
	const ScriptAsset *script = m_project.FindScript(name);
	if (!script)
		return Err(String::Format("script introuvable : %s", name.CStr()));
	return files::SaveScriptFile(script->source, path);
}

Result<String, String> Runtime::ImportScriptFile(const String &path) {
	if (!m_hasProject)
		return Err(String("aucun projet ouvert"));
	auto loaded = files::LoadScriptFile(path);
	if (loaded.IsError())
		return Err(loaded.Error());
	ScriptAsset asset = std::move(loaded).Unwrap();
	const String name = asset.name;
	if (ScriptAsset *existing = m_project.FindScript(name))
		existing->source = asset.source;
	else
		m_project.scripts.push_back(std::move(asset));
	return Ok(name);
}

String Runtime::UniqueSceneName(const String &base) const {
	if (!m_project.FindScene(base))
		return base;
	for (int i = 2;; ++i) {
		String candidate = String::Format("%s %d", base.CStr(), i);
		if (!m_project.FindScene(candidate))
			return candidate;
	}
}

bool Runtime::SwitchScene(const String &sceneName) {
	if (!m_project.FindScene(sceneName))
		return false;
	if (m_playing)
		Stop();
	// Un identifiant de nœud ne vaut que dans SON arbre : gardée, la sélection
	// désignerait un nœud quelconque de la nouvelle scène.
	const bool changed = !ActiveScene() || ActiveScene()->name != sceneName;
	if (changed)
		m_selection = scene::NodeId{};
	m_project.SetActiveScene(sceneName);
	RebuildRuntime();
	if (changed)
		ClearSelection(); // marqueur ECS et notification à l'interface
	LogInfo(String::Format("Scène active : %s", sceneName.CStr()));
	return true;
}

void Runtime::RebuildRuntime() {
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

void Runtime::ApplyEnvironment() {
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

void Runtime::Update(float dt) {
	++m_frameIndex;
	// Fonctions `async` des scripts : leurs appels à l'API de l'éditeur
	// s'exécutent ici, sur le fil principal (cf. Interpreter::PumpMainThread).
	PumpScriptThreads();
	const double tickRate = double(sdl3::GetPerformanceFrequency());
	uint64_t mark = sdl3::GetPerformanceCounter();

	// Rappels de l'interface des scripts (clics…), hors de la boucle
	// d'évènements de l'interface (cf. UiBinding::Flush).
	if (m_playing)
		m_gameUi->Flush();
	// Changement de mode de la souris (script ou touche) : prévenir le jeu,
	// hors de tout script en cours.
	if (m_playing && m_mouseModeChanged && m_gameplayReady) {
		m_mouseModeChanged = false;
		CallGameplayHook(String("on_mouse_mode"), {data::script::Value::Str(String(MouseModeName(EffectiveMouseMode())))});
	}
	// Changement de scène ou arrêt demandé par un script à l'image
	// précédente : c'est le moment (aucun script ne s'exécute).
	if (m_playing && m_pendingQuit) {
		m_pendingQuit = false;
		Stop();
	} else if (m_playing && m_pendingScene.IsSome()) {
		const String next = m_pendingScene.Unwrap();
		m_pendingScene = NONE;
		LoadSceneInPlay(next);
	}

	m_previousMouseButtons = m_mouseButtons;
	m_mouseButtons = uint32_t(SDL_GetMouseState(nullptr, nullptr));
	if (m_playing) {
		m_playTime += dt;
		m_draw2d.clear(); // les scripts redessinent leur 2D à chaque image
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

void Runtime::Play() {
	if (m_playing)
		return;
	SceneDesc *scene = ActiveScene();
	if (!scene) {
		LogError(String("Impossible de lancer le mode Jeu : aucune scène active"));
		return;
	}
	m_sessionOrigin = scene->name;
	m_gameState = data::script::Value::EmptyMap();
	m_pendingScene = NONE;
	m_pendingQuit = false;
	m_playTime = 0.f;
	// Chaque partie repart du comportement par défaut de la souris.
	m_mouseMode = NONE;
	m_mouseToggleKey = Some(SDL_Keycode(SDLK_F7));
	m_mouseModeChanged = false;
	StartScenePlay();
	LogSuccess(String::Format("▶ Mode Jeu — scène « %s »", scene->name.CStr()));
}

void Runtime::Stop() {
	if (!m_playing)
		return;
	// EndScenePlay AVANT de quitter la partie : les objets des scripts y sont
	// détruits et retirent leurs nœuds par le chemin « partie » (ni
	// historique d'annulation, ni reconstruction du runtime).
	EndScenePlay();
	m_playing = false;
	if (!m_sessionOrigin.IsEmpty() && m_project.FindScene(m_sessionOrigin))
		m_project.SetActiveScene(m_sessionOrigin);
	RebuildRuntime();
	m_sessionOrigin = String();
	m_gameState = data::script::Value::Nil();
	m_pendingScene = NONE;
	m_pendingQuit = false;
	if (onGameUiReset)
		onGameUiReset();
	LogInfo(String::Format("■ Mode Édition (partie de %.1f s)", double(m_playTime)));
	m_playTime = 0.f;
}

bool Runtime::LoadSceneInPlay(const String &sceneName) {
	if (!m_project.FindScene(sceneName)) {
		LogError(String::Format("Scène « %s » introuvable dans le projet", sceneName.CStr()));
		return false;
	}
	if (!m_playing)
		return SwitchScene(sceneName);
	EndScenePlay();
	m_selection = scene::NodeId{}; // cf. SwitchScene
	m_project.SetActiveScene(sceneName);
	RebuildRuntime();
	ClearSelection();
	StartScenePlay();
	LogInfo(String::Format("▶ Scène « %s »", sceneName.CStr()));
	return true;
}

data::script::Value Runtime::GameState() {
	if (!m_gameState.IsMap())
		m_gameState = data::script::Value::EmptyMap();
	return m_gameState;
}

void Runtime::TogglePlay() {
	if (m_playing)
		Stop();
	else
		Play();
}

bool Runtime::Select(scene::NodeId id) {
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

bool Runtime::Select(const String &objectName) {
	return Select(objectName.IsEmpty() ? scene::NodeId{} : ResolveId(objectName));
}

math::FRay Runtime::ViewportRay(float x, float y, float width, float height) const {
	return ActiveCamera().ScreenPointToRay(x, y, width, height);
}

Option<Runtime::PickHit> Runtime::PickAt(const math::FRay &ray) {
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

bool Runtime::SelectAt(const math::FRay &ray) {
	Option<PickHit> hit = PickAt(ray);
	if (hit.IsNone()) {
		ClearSelection();
		return false;
	}
	(void)Select(hit.Value().node);
	return true;
}

void Runtime::SetGizmoMode(GizmoMode mode) {
	m_gizmoMode = mode;
	RefreshGizmo();
}

void Runtime::SetSnapSteps(float translate, float rotateDegrees, float scale) noexcept {
	m_translateSnap = sdl3::Max(translate, 0.f);
	m_rotateSnap = sdl3::Max(rotateDegrees, 0.f);
	m_scaleSnap = sdl3::Max(scale, 0.f);
}

float Runtime::GizmoSize() const {
	Option<math::FVector3> center = SelectionWorldPosition();
	if (center.IsNone())
		return 0.f;
	const float distance = (center.Unwrap() - ActiveCamera().position).Length();
	return sdl3::Max(distance * 0.14f, 0.25f);
}

Option<math::FVector3> Runtime::SelectionWorldPosition() const {
	const scene::NodeTree *tree = Tree();
	if (!tree || !m_selection.Valid() || !tree->Contains(m_selection))
		return NONE;
	return Some(tree->GlobalPosition(m_selection));
}

Runtime::GizmoAxis Runtime::PickGizmoAxis(const math::FRay &ray) const {
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

bool Runtime::BeginGizmoDrag(GizmoAxis axis, const math::FRay &ray) {
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

bool Runtime::UpdateGizmoDrag(const math::FRay &ray) {
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

bool Runtime::ScriptedGizmoDrag(GizmoAxis axis, float from, float to) {
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

void Runtime::EndGizmoDrag() {
	if (!m_dragging)
		return;
	m_dragging = false;
	m_dragAxis = GizmoAxis::NONE;
	EndEdit();
}

void Runtime::SetHoveredAxis(GizmoAxis axis) {
	if (axis == m_hoverAxis)
		return;
	m_hoverAxis = axis;
	RefreshGizmo();
}

void Runtime::RefreshGizmo() {
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

void Runtime::BeginEdit(String label) {
	// L'instantané est pris AVANT d'entrer dans le groupe : une fois
	// dedans, `RecordHistory` ne fait plus rien.
	if (m_editDepth == 0)
		RecordHistory(std::move(label));
	++m_editDepth;
}

void Runtime::EndEdit() {
	if (m_editDepth > 0)
		--m_editDepth;
}

bool Runtime::Undo() {
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

bool Runtime::Redo() {
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

void Runtime::ClearHistory() {
	m_undo.clear();
	m_redo.clear();
	if (onHistoryChanged)
		onHistoryChanged();
}

bool Runtime::SetPosition(scene::NodeId id, const math::FVector3 &position) {
	scene::NodeTree *tree = Tree();
	scene::Node *node = FindObject(id);
	if (!tree || !node)
		return false;
	RecordHistory(String::Format("Déplacer %s", node->name.CStr()));
	(void)tree->SetLocalPosition(id, position);
	return PushTransform(id);
}

bool Runtime::SetPosition(const String &objectName, const math::FVector3 &position) {
	return SetPosition(ResolveId(objectName), position);
}

bool Runtime::SetGlobalPosition(scene::NodeId id, const math::FVector3 &position) {
	scene::NodeTree *tree = Tree();
	scene::Node *node = FindObject(id);
	if (!tree || !node)
		return false;
	RecordHistory(String::Format("Déplacer %s", node->name.CStr()));
	(void)tree->SetGlobalPosition(id, position);
	return PushTransform(id);
}

bool Runtime::SetEulerDegrees(scene::NodeId id, const math::FVector3 &eulerDeg) {
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

bool Runtime::SetEulerDegrees(const String &objectName, const math::FVector3 &eulerDeg) {
	return SetEulerDegrees(ResolveId(objectName), eulerDeg);
}

bool Runtime::SetScale(scene::NodeId id, const math::FVector3 &scale) {
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

bool Runtime::SetScale(const String &objectName, const math::FVector3 &scale) {
	return SetScale(ResolveId(objectName), scale);
}

bool Runtime::SetVisible(scene::NodeId id, bool visible) {
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

bool Runtime::SetMaterial(scene::NodeId id, const MaterialDesc &material) {
	scene::Node *node = FindObject(id);
	if (!node)
		return false;
	RecordHistory(String::Format("Matériau de %s", node->name.CStr()));
	VisualDesc visual = VisualDesc::Read(*node);
	visual.material = material;
	visual.Write(*node);
	return PushMaterial(id);
}

bool Runtime::SetMaterial(const String &objectName, const MaterialDesc &material) {
	return SetMaterial(ResolveId(objectName), material);
}

bool Runtime::SetMaterialColor(scene::NodeId id, const sdl3::Color &color) {
	scene::Node *node = FindObject(id);
	if (!node)
		return false;
	MaterialDesc material = VisualDesc::Read(*node).material;
	material.baseColor = color;
	return SetMaterial(id, material);
}

bool Runtime::SetMaterialColor(const String &objectName, const sdl3::Color &color) {
	return SetMaterialColor(ResolveId(objectName), color);
}

bool Runtime::SetVisual(scene::NodeId id, const VisualDesc &visual) {
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

bool Runtime::SetPhysics(scene::NodeId id, const PhysicsDesc &physics) {
	scene::Node *node = FindObject(id);
	if (!node)
		return false;
	RecordHistory(String::Format("Physique de %s", node->name.CStr()));
	physics.Write(*node);
	return PushPhysics(id);
}

bool Runtime::SetPhysics(const String &objectName, const PhysicsDesc &physics) {
	return SetPhysics(ResolveId(objectName), physics);
}

bool Runtime::SetTagOf(scene::NodeId id, const String &tag) {
	scene::Node *node = FindObject(id);
	if (!node)
		return false;
	RecordHistory(String::Format("Étiquette de %s", node->name.CStr()));
	SetTag(*node, tag);
	NotifyObjectChanged();
	return true;
}

bool Runtime::SetLocked(scene::NodeId id, bool locked) {
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

bool Runtime::SetLight(scene::NodeId id, const LightDesc &light) {
	scene::Node *node = FindObject(id);
	if (!node)
		return false;
	RecordHistory(String::Format("Lumière de %s", node->name.CStr()));
	light.Write(*node);
	RefreshHelper(id);
	NotifyObjectChanged();
	return true;
}

bool Runtime::SetCameraNode(scene::NodeId id, const CameraNodeDesc &camera) {
	scene::Node *node = FindObject(id);
	if (!node)
		return false;
	RecordHistory(String::Format("Caméra de %s", node->name.CStr()));
	camera.Write(*node);
	RefreshHelper(id);
	NotifyObjectChanged();
	return true;
}

bool Runtime::SetTrigger(scene::NodeId id, const TriggerDesc &trigger) {
	scene::Node *node = FindObject(id);
	if (!node)
		return false;
	RecordHistory(String::Format("Déclencheur de %s", node->name.CStr()));
	trigger.Write(*node);
	RefreshHelper(id);
	NotifyObjectChanged();
	return true;
}

bool Runtime::SetScriptRef(scene::NodeId id, const String &scriptName) {
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

std::vector<String> Runtime::AddableComponents() {
	return {String(component::VISUAL),	  String(component::BODY),		  String(component::LIGHT),
			String(component::CAMERA),	  String(component::TRIGGER),	  String(component::SCRIPT),
			String(component::CANVAS_ITEM), String(component::CAMERA_2D)};
}

bool Runtime::AddComponentOfType(scene::NodeId id, const String &type) {
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
	} else if (type == component::CANVAS_ITEM) {
		CanvasItemDesc{}.Write(*node);
	} else if (type == component::CAMERA_2D) {
		Camera2DDesc{}.Write(*node);
	} else {
		DropLastHistory();
		return false;
	}
	NotifyObjectChanged();
	return true;
}

bool Runtime::RemoveComponentOfType(scene::NodeId id, const String &type) {
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

bool Runtime::SetComponentProps(scene::NodeId id, const String &type, scene::PropertyMap props) {
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

String Runtime::AddScript(const String &baseName, const String &source, const String &description) {
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

Result<String, String> Runtime::RenameScene(const String &sceneName, const String &wanted) {
	if (m_playing)
		return Err(String("impossible pendant une partie"));
	SceneDesc *scene = m_project.FindScene(sceneName);
	if (!scene)
		return Err(String::Format("scène « %s » introuvable", sceneName.CStr()));
	const String clean = wanted.Trim();
	if (clean.IsEmpty())
		return Err(String("nom vide"));
	if (clean == sceneName)
		return Ok(clean);
	const String name = UniqueSceneName(clean);
	const bool active = m_project.activeScene == sceneName;
	scene->SetName(name);
	if (active)
		m_project.activeScene = name;
	if (onProjectChanged)
		onProjectChanged();
	if (onSceneStructureChanged)
		onSceneStructureChanged();
	LogInfo(String::Format("Scène renommée : %s → %s", sceneName.CStr(), name.CStr()));
	return Ok(name);
}

Result<bool, String> Runtime::RemoveScene(const String &sceneName) {
	if (m_playing)
		return Err(String("impossible pendant une partie"));
	auto it = std::find_if(m_project.scenes.begin(), m_project.scenes.end(),
						   [&](const SceneDesc &scene) { return scene.name == sceneName; });
	if (it == m_project.scenes.end())
		return Err(String::Format("scène « %s » introuvable", sceneName.CStr()));
	if (m_project.scenes.size() == 1)
		return Err(String("un projet garde au moins une scène"));
	if (m_project.activeScene == sceneName) {
		const String other = (it == m_project.scenes.begin() ? it + 1 : m_project.scenes.begin())->name;
		(void)SwitchScene(other);
		it = std::find_if(m_project.scenes.begin(), m_project.scenes.end(),
						  [&](const SceneDesc &scene) { return scene.name == sceneName; });
	}
	m_project.scenes.erase(it);
	ClearHistory(); // l'historique pouvait viser la scène retirée
	if (onProjectChanged)
		onProjectChanged();
	LogInfo(String::Format("Scène retirée du projet : %s", sceneName.CStr()));
	return Ok(true);
}

Result<String, String> Runtime::DuplicateScene(const String &sceneName) {
	if (m_playing)
		return Err(String("impossible pendant une partie"));
	const SceneDesc *scene = m_project.FindScene(sceneName);
	if (!scene)
		return Err(String::Format("scène « %s » introuvable", sceneName.CStr()));
	SceneDesc copy = *scene;
	copy.SetName(UniqueSceneName(sceneName + String(" (copie)")));
	const String name = copy.name;
	m_project.scenes.push_back(std::move(copy));
	if (onProjectChanged)
		onProjectChanged();
	LogInfo(String::Format("Scène dupliquée : %s", name.CStr()));
	return Ok(name);
}

Result<String, String> Runtime::RenameScript(const String &scriptName, const String &wanted) {
	if (m_playing)
		return Err(String("impossible pendant une partie"));
	ScriptAsset *asset = m_project.FindScript(scriptName);
	if (!asset)
		return Err(String::Format("script « %s » introuvable", scriptName.CStr()));
	String clean = wanted.Trim();
	if (clean.EndsWith(".script"))
		clean = clean.Substring(0, clean.GetSize() - 7);
	if (clean.IsEmpty())
		return Err(String("nom vide"));
	if (clean == scriptName)
		return Ok(clean);
	String name = clean;
	for (int suffix = 2; m_project.FindScript(name) && suffix < 10000; ++suffix)
		name = String::Format("%s_%d", clean.CStr(), suffix);
	asset->name = name;
	// Les nœuds qui portent ce script le portent sous son nouveau nom.
	size_t nodes = 0;
	for (SceneDesc &scene : m_project.scenes)
		scene.tree.Traverse(scene.tree.Root(), [&](scene::NodeId id, const scene::Node &) {
			scene::Node *node = scene.tree.Get(id);
			if (!node || !ScriptRef::Has(*node) || ScriptRef::Read(*node).script != scriptName)
				return;
			ScriptRef{name}.Write(*node);
			++nodes;
		});
	if (onProjectChanged)
		onProjectChanged();
	if (onObjectChanged)
		onObjectChanged();
	LogInfo(String::Format("Script renommé : %s → %s (%d nœud%s mis à jour)", scriptName.CStr(), name.CStr(),
						   int(nodes), nodes > 1 ? "s" : ""));
	return Ok(name);
}

Result<size_t, String> Runtime::RemoveScript(const String &scriptName) {
	if (m_playing)
		return Err(String("impossible pendant une partie"));
	auto it = std::find_if(m_project.scripts.begin(), m_project.scripts.end(),
						   [&](const ScriptAsset &script) { return script.name == scriptName; });
	if (it == m_project.scripts.end())
		return Err(String::Format("script « %s » introuvable", scriptName.CStr()));
	m_project.scripts.erase(it);
	size_t nodes = 0;
	for (const SceneDesc &scene : m_project.scenes)
		scene.tree.Traverse(scene.tree.Root(), [&](scene::NodeId, const scene::Node &node) {
			nodes += ScriptRef::Has(node) && ScriptRef::Read(node).script == scriptName ? 1 : 0;
		});
	if (onProjectChanged)
		onProjectChanged();
	if (nodes > 0)
		LogWarning(String::Format("Script « %s » retiré : %d nœud%s le portai%s encore", scriptName.CStr(), int(nodes),
								  nodes > 1 ? "s" : "", nodes > 1 ? "ent" : "t"));
	else
		LogInfo(String::Format("Script retiré : %s", scriptName.CStr()));
	return Ok(nodes);
}

Result<String, String> Runtime::DuplicateScript(const String &scriptName) {
	if (m_playing)
		return Err(String("impossible pendant une partie"));
	const ScriptAsset *asset = m_project.FindScript(scriptName);
	if (!asset)
		return Err(String::Format("script « %s » introuvable", scriptName.CStr()));
	const ScriptAsset copy = *asset;
	const String name = AddScript(copy.name + String("_copie"), copy.source, copy.description);
	if (onProjectChanged)
		onProjectChanged();
	return Ok(name);
}

Option<data::script::ScriptError> Runtime::SetScriptSource(const String &scriptName, const String &source) {
	ScriptAsset *asset = m_project.FindScript(scriptName);
	if (!asset)
		return Some(data::script::ScriptError(String("script introuvable"), 0, 0));
	asset->source = source;
	return CheckScript(source);
}

Option<data::script::ScriptError> Runtime::SetGameplayScript(const String &sceneName, const String &source) {
	SceneDesc *scene = m_project.FindScene(sceneName);
	if (!scene)
		return Some(data::script::ScriptError(String("scène introuvable"), 0, 0));
	scene->gameplayScript = source;
	return CheckScript(source);
}

Option<data::script::ScriptError> Runtime::CheckScript(const String &source) {
	auto compiled = data::script::Parser::Compile(source.View());
	if (compiled.IsError())
		return Some(compiled.Error());
	return NONE;
}

std::vector<ScriptStatus> Runtime::ScriptStatuses() const {
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
		for (const auto &instance : m_nodeScripts)
			out.push_back(ScriptStatus{instance->script, String("nœud"), instance->LiveCount(), instance->ready,
									   instance->error});
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

Canvas2DFrame Runtime::Frame2D() const {
	Canvas2DFrame frame = SceneFrame2D();
	const size_t base = frame.items.size();
	for (size_t i = 0; i < m_draw2d.size(); ++i) {
		frame.items.push_back(m_draw2d[i]);
		frame.items.back().order = base + i;
	}
	frame.Sort();
	return frame;
}

Canvas2DFrame Runtime::SceneFrame2D() const {
	const SceneDesc *scene = ActiveScene();
	return scene ? BuildCanvasFrame(*scene) : Canvas2DFrame{};
}

math::FVector2 Runtime::Reference2D() const {
	const SceneDesc *scene = ActiveScene();
	return scene ? scene->canvas.Size() : Canvas2DDesc{}.Size();
}

View2D Runtime::GameView2D(sdl3::FRect viewport, const Canvas2DFrame &frame) const {
	return View2D::Game(viewport, Reference2D(), frame.camera);
}

Option<math::FVector2> Runtime::PointerTo2D(float x, float y, Space2D space) const {
	const sdl3::FRect r = m_view2dRect;
	if (r.w <= 0.f || r.h <= 0.f || x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h)
		return NONE;
	Option<Transform2D> inverse = GameView2D(r, Frame2D()).For(space).Inverse();
	if (inverse.IsNone())
		return NONE;
	return Some(inverse.Value().Apply({x, y}));
}

bool Runtime::SetCanvasItem(scene::NodeId id, const CanvasItemDesc &item) {
	scene::Node *node = FindObject(id);
	if (!node)
		return false;
	RecordHistory(String::Format("Apparence 2D de %s", node->name.CStr()));
	item.Write(*node);
	NotifyObjectChanged();
	return true;
}

bool Runtime::SetCamera2D(scene::NodeId id, const Camera2DDesc &camera) {
	scene::Node *node = FindObject(id);
	if (!node)
		return false;
	RecordHistory(String::Format("Caméra 2D de %s", node->name.CStr()));
	camera.Write(*node);
	NotifyObjectChanged();
	return true;
}

bool Runtime::SetCanvas2D(const Canvas2DDesc &canvas) {
	SceneDesc *scene = ActiveScene();
	if (!scene)
		return false;
	scene->canvas = canvas;
	NotifyObjectChanged();
	return true;
}

bool Runtime::HasVisibleHelper(scene::NodeId id) const {
	auto it = m_helperOf.find(id);
	return it != m_helperOf.end() && it->second->IsVisible();
}

const char *Runtime::MouseModeName(MouseMode mode) noexcept {
	switch (mode) {
		case MouseMode::CAPTURED:
			return "captured";
		case MouseMode::CONFINED:
			return "confined";
		default:
			return "free";
	}
}

Option<Runtime::MouseMode> Runtime::MouseModeFromName(const String &name) {
	const String key = name.ToLower();
	if (key == "free")
		return Some(MouseMode::FREE);
	if (key == "captured")
		return Some(MouseMode::CAPTURED);
	if (key == "confined")
		return Some(MouseMode::CONFINED);
	return NONE;
}

Runtime::MouseMode Runtime::EffectiveMouseMode() const noexcept {
	if (!m_playing)
		return MouseMode::FREE;
	if (m_mouseMode.IsSome())
		return m_mouseMode.Value();
	return m_hostFullscreen ? MouseMode::CAPTURED : MouseMode::FREE;
}

void Runtime::SetMouseMode(MouseMode mode) {
	const MouseMode before = EffectiveMouseMode();
	m_mouseMode = Some(mode);
	if (EffectiveMouseMode() != before)
		m_mouseModeChanged = true;
	m_mouseDelta = {}; // un déplacement d'avant le changement ne compte pas
}

Runtime::MouseMode Runtime::ToggleMouseCapture() {
	SetMouseMode(EffectiveMouseMode() == MouseMode::CAPTURED ? MouseMode::FREE : MouseMode::CAPTURED);
	return EffectiveMouseMode();
}

void Runtime::AddMouseDelta(float dx, float dy) noexcept {
	m_mouseDelta.x += dx;
	m_mouseDelta.y += dy;
}

Option<String> Runtime::SpawnObject(ObjectDesc object) {
	Option<scene::NodeId> id = SpawnNode(std::move(object));
	if (id.IsNone())
		return NONE;
	const scene::Node *node = FindObject(id.Unwrap());
	if (!node)
		return NONE;
	return Some(node->name);
}

Option<scene::NodeId> Runtime::SpawnNode(ObjectDesc object, scene::NodeId parent) {
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

Option<scene::NodeId> Runtime::CreateGroup(const String &name, scene::NodeId parent) {
	ObjectDesc group = ObjectDesc::Group(name);
	return SpawnNode(std::move(group), parent);
}

bool Runtime::ReparentNode(scene::NodeId child, scene::NodeId newParent, scene::ReparentMode mode, size_t index) {
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

bool Runtime::MoveNodeInParent(scene::NodeId child, size_t index) {
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

Option<scene::NodeId> Runtime::DuplicateNode(scene::NodeId id) {
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

bool Runtime::RemoveNode(scene::NodeId id) {
	SceneDesc *sceneDesc = ActiveScene();
	scene::Node *node = FindObject(id);
	if (!sceneDesc || !node || id == sceneDesc->tree.Root())
		return false;
	RecordHistory(String::Format("Supprimer %s", node->name.CStr()));
	const bool wasSelected = m_selection.Valid() && sceneDesc->tree.IsAncestorOf(id, m_selection);
	if (m_playing) {
		// En partie : retirer le seul sous-arbre. Reconstruire tout le
		// runtime remettrait chaque corps physique à son état initial (une
		// voiture lancée s'arrêterait net parce qu'un débris disparaît).
		std::vector<scene::NodeId> doomed;
		sceneDesc->tree.Traverse(id, [&](scene::NodeId n, const scene::Node &) { doomed.push_back(n); });
		DestroyObjectsOfNodes(doomed);
		if (!sceneDesc->tree.Contains(id)) // un `on_destroy` l'a déjà retiré
			return true;
		for (auto it = doomed.rbegin(); it != doomed.rend(); ++it)
			DestroyEntity(*it);
		if (!sceneDesc->tree.Remove(id))
			return false;
		std::erase_if(m_triggerCandidates, [&](scene::NodeId n) { return !sceneDesc->tree.Contains(n); });
		LinkPortals(*sceneDesc); // un portail a pu partir avec le sous-arbre
		if (wasSelected)
			ClearSelection();
		if (onSceneStructureChanged)
			onSceneStructureChanged();
		return true;
	}
	std::vector<scene::NodeId> doomed;
	sceneDesc->tree.Traverse(id, [&](scene::NodeId n, const scene::Node &) { doomed.push_back(n); });
	DestroyObjectsOfNodes(doomed);
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

bool Runtime::RenameObject(scene::NodeId id, const String &newName) {
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

bool Runtime::RenameObject(const String &objectName, const String &newName) {
	return RenameObject(ResolveId(objectName), newName);
}

Result<bool, String> Runtime::SavePackedScene(scene::NodeId id, const String &path) const {
	const scene::NodeTree *tree = Tree();
	if (!tree || !tree->Contains(id))
		return Err(String("objet introuvable"));
	scene::PackedScene packed = scene::PackedScene::FromSubtree(*tree, id);
	packed.SetSource(path);
	return files::WriteText(path, packed.EncodeJson());
}

String Runtime::ResolveProjectPath(const String &path) const {
	if (path.IsEmpty() || path.CStr()[0] == '/')
		return path;
	const String directory = ProjectDirectory();
	if (!directory.IsEmpty()) {
		const String inProject = files::Join(directory, path);
		if (files::IsFile(inProject))
			return inProject;
	}
	return path;
}

Result<scene::NodeId, String> Runtime::InstantiateSceneFile(const String &path, scene::NodeId parent,
															 Option<scene::Transform> placement) {
	SceneDesc *sceneDesc = ActiveScene();
	if (!sceneDesc)
		return Err(String("aucune scène active"));
	const String file = ResolveProjectPath(path);
	auto text = data::script::LoadScriptFile(file);
	if (text.IsError())
		return Err(text.Error());
	auto packed = scene::PackedScene::DecodeJson(text.Value());
	if (packed.IsError())
		return Err(packed.Error());
	packed.Value().SetSource(path);

	RecordHistory(String::Format("Instancier %s", FileStem(path).CStr()));
	scene::NodeId parentId = parent.Valid() && sceneDesc->tree.Contains(parent) ? parent : sceneDesc->tree.Root();
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
		if (placement.IsSome())
			root->transform = placement.Unwrap();
	}
	// Seul le sous-arbre nouveau est construit : assembler un niveau de
	// dizaines de pièces ne reconstruit pas toute la scène à chaque pièce.
	sceneDesc->tree.Traverse(created, [&](scene::NodeId id, const scene::Node &node) { InstantiateNode(id, node); });
	ApplyParentLinks(*sceneDesc);
	render3d::SceneSyncSystem::Sync(m_registry, m_sceneRoot);
	if (m_playing) {
		SetHelpersVisible(false);
		StartNodeScriptsOf(created);
		// Les zones sont relues à chaque image (m_triggerIds) ; seuls les
		// candidats (joueur, corps dynamiques) sont à compléter — sans
		// réarmer les zones `once` déjà franchies.
		PrepareTriggers(false);
	}
	if (onSceneStructureChanged)
		onSceneStructureChanged();
	return Ok(created);
}

Option<math::FVector3> Runtime::GetVelocity(const String &objectName) {
	return GetVelocity(ResolveId(objectName));
}

Option<math::FVector3> Runtime::GetVelocity(scene::NodeId id) {
	Option<ecs::Entity> entity = FindEntity(id);
	if (entity.IsNone())
		return NONE;
	auto body = m_registry.GetComponent<physics::RigidBody>(entity.Unwrap());
	if (body.IsNone())
		return NONE;
	return Some(body.Unwrap()->linearVelocity);
}

bool Runtime::SetVelocity(const String &objectName, const math::FVector3 &velocity) {
	return SetVelocity(ResolveId(objectName), velocity);
}

bool Runtime::ApplyImpulse(const String &objectName, const math::FVector3 &impulse) {
	return ApplyImpulse(ResolveId(objectName), impulse);
}

bool Runtime::SetVelocity(scene::NodeId id, const math::FVector3 &velocity) {
	Option<ecs::Entity> entity = FindEntity(id);
	if (entity.IsNone())
		return false;
	auto body = m_registry.GetComponent<physics::RigidBody>(entity.Unwrap());
	if (body.IsNone())
		return false;
	body.Unwrap()->linearVelocity = velocity;
	return true;
}

bool Runtime::ApplyImpulse(scene::NodeId id, const math::FVector3 &impulse) {
	Option<ecs::Entity> entity = FindEntity(id);
	if (entity.IsNone())
		return false;
	auto body = m_registry.GetComponent<physics::RigidBody>(entity.Unwrap());
	if (body.IsNone() || body.Unwrap()->invMass <= 0.f)
		return false;
	body.Unwrap()->linearVelocity += impulse * body.Unwrap()->invMass;
	return true;
}

void Runtime::ApplyLookDelta(float deltaYaw, float deltaPitch) {
	float &yaw = m_playing ? m_playYaw : m_editYaw;
	float &pitch = m_playing ? m_playPitch : m_editPitch;
	yaw += deltaYaw;
	pitch = sdl3::Clamp(pitch + deltaPitch, -1.5f, 1.5f);
}

void Runtime::MoveCamera(const math::FVector3 &localDelta) {
	render3d::Camera &camera = ActiveCamera();
	const math::FVector3 forward = CameraForward();
	const math::FVector3 right = CameraRight();
	camera.position += forward * localDelta.z + right * localDelta.x + math::FVector3{0.f, localDelta.y, 0.f};
	RefreshCameraTargets();
}

void Runtime::PanCamera(float right, float up) {
	render3d::Camera &camera = ActiveCamera();
	camera.position += CameraRight() * right + CameraUp() * up;
	RefreshCameraTargets();
}

void Runtime::DollyCamera(float distance) {
	render3d::Camera &camera = ActiveCamera();
	camera.position += CameraForward() * distance;
	RefreshCameraTargets();
}

void Runtime::OrbitCamera(float deltaYaw, float deltaPitch, const math::FVector3 &pivot) {
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

math::FVector3 Runtime::CameraPivot() const {
	if (Option<math::FVector3> center = SelectionWorldPosition(); center.IsSome())
		return center.Unwrap();
	return ActiveCamera().position + CameraForward() * DEFAULT_PIVOT_DISTANCE;
}

float Runtime::PivotDistance() const {
	return sdl3::Clamp((ActiveCamera().position - CameraPivot()).Length(), 0.5f, 400.f);
}

math::FVector3 Runtime::CameraForward() const {
	return ForwardFromYawPitch(m_playing ? m_playYaw : m_editYaw, m_playing ? m_playPitch : m_editPitch);
}

math::FVector3 Runtime::CameraRight() const {
	return CameraForward().Cross({0.f, 1.f, 0.f}).Normalize();
}

void Runtime::SetCameraPosition(const math::FVector3 &position) {
	ActiveCamera().position = position;
	RefreshCameraTargets();
}

bool Runtime::FocusOn(scene::NodeId id) {
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

void Runtime::SetViewportAspect(float aspect) {
	if (aspect > 0.01f) {
		m_editCamera.aspect = aspect;
		m_playCamera.aspect = aspect;
	}
}

Result<data::script::Value, data::script::ScriptError> Runtime::RunToolScript(const String &source) {
	++m_scriptRunCount;
	return m_toolVm.Run(source.View());
}

void Runtime::CallToolHook(const String &hook, std::vector<data::script::Value> args) {
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

Option<data::script::Value> Runtime::GameplayGlobal(const String &name) {
	if (m_sceneObject.IsInstance() && m_sceneObject.AsInstance())
		if (Option<data::script::Value> field = m_sceneObject.AsInstance()->fields.Get(name); field.IsSome())
			return field;
	return m_gameplayVm->GetGlobal(name);
}

bool Runtime::RequestScene(const String &sceneName) {
	if (!m_project.FindScene(sceneName))
		return false;
	if (m_playing)
		m_pendingScene = Some(sceneName);
	else
		(void)SwitchScene(sceneName);
	return true;
}

void Runtime::RequestQuit() {
	if (m_playing)
		m_pendingQuit = true;
}

scene::NodeTypeRegistry Runtime::MakeTypeRegistry() {
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

	// 2D (cf. canvas2d.hpp).
	struct Kind2D {
		const char *name, *label, *icon;
		bool children;
	};
	for (const Kind2D &kind : {Kind2D{node_kind::NODE2D, "Nœud 2D", "◇", true},
							   Kind2D{node_kind::SHAPE2D, "Forme 2D", "■", true},
							   Kind2D{node_kind::SPRITE2D, "Image 2D", "▨", true},
							   Kind2D{node_kind::LABEL2D, "Texte 2D", "T", true},
							   Kind2D{node_kind::CAMERA2D, "Caméra 2D", "◰", false},
							   Kind2D{node_kind::CANVAS_LAYER, "Calque d'interface", "▭", true}}) {
		scene::NodeTypeInfo info;
		info.name = String(kind.name);
		info.label = String(kind.label);
		info.category = String("2D");
		info.icon = String(kind.icon);
		info.acceptsChildren = kind.children;
		registry.Register(std::move(info));
	}
	return registry;
}

const scene::Node * Runtime::SelectedConstObject() const {
	const scene::NodeTree *tree = Tree();
	return tree ? tree->Get(m_selection) : nullptr;
}

math::FVector3 Runtime::AxisVector(GizmoAxis axis) noexcept {
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

String Runtime::GizmoLabel(GizmoAxis axis) const {
	const char *action = m_gizmoMode == GizmoMode::TRANSLATE  ? "Déplacer"
						 : m_gizmoMode == GizmoMode::ROTATE ? "Tourner"
															: "Redimensionner";
	const char *letter = axis == GizmoAxis::X ? "X" : (axis == GizmoAxis::Y ? "Y" : "Z");
	const scene::NodeTree *tree = Tree();
	const scene::Node *node = tree ? tree->Get(m_dragObject) : nullptr;
	return String::Format("%s %s (%s)", action, node ? node->name.CStr() : "?", letter);
}

Option<float> Runtime::ClosestParamOnAxis(const math::FRay &ray, const math::FVector3 &origin,
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

Option<math::FVector3> Runtime::IntersectPlane(const math::FRay &ray, const math::FVector3 &point,
		const math::FVector3 &normal) noexcept {
	const float denominator = normal.Dot(ray.direction);
	if (sdl3::Abs(denominator) < 1e-6f)
		return NONE; // rayon rasant : l'impact partirait à l'infini
	const float t = normal.Dot(point - ray.origin) / denominator;
	if (t < 0.f)
		return NONE;
	return Some(ray.At(t));
}

float Runtime::SnapTo(float value, float step) noexcept {
	return step > 0.f ? sdl3::Round(value / step) * step : value;
}

void Runtime::EnsureGizmoNodes() {
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

void Runtime::NotifyObjectChanged() {
	if (!m_playing && onObjectChanged)
		onObjectChanged();
}

Runtime::HistoryStep Runtime::Snapshot(String label) const {
	HistoryStep step;
	step.label = std::move(label);
	if (const SceneDesc *scene = ActiveScene()) {
		step.sceneName = scene->name;
		step.tree = scene->tree;
	}
	step.selection = m_selection;
	return step;
}

void Runtime::RecordHistory(String label) {
	if (m_playing || m_restoring || m_editDepth > 0)
		return;
	m_undo.push_back(Snapshot(std::move(label)));
	if (m_undo.size() > MAX_HISTORY)
		m_undo.erase(m_undo.begin());
	m_redo.clear(); // une nouvelle branche d'édition abandonne l'ancienne
	if (onHistoryChanged)
		onHistoryChanged();
}

void Runtime::DropLastHistory() {
	if (!m_undo.empty())
		m_undo.pop_back();
	if (onHistoryChanged)
		onHistoryChanged();
}

bool Runtime::RestoreStep(const HistoryStep &step) {
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

render3d::Mesh Runtime::BuildMesh(const VisualDesc &object) {
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

render3d::Mesh Runtime::MakeMeshFor(const VisualDesc &object) {
	if (object.shape != ShapeKind::MODEL)
		return BuildMesh(object);
	auto mesh = LoadGltfMesh(object.source);
	if (mesh.IsError()) {
		LogError(String::Format("Import glTF « %s » : %s", object.source.CStr(), mesh.Error().CStr()));
		return render3d::Mesh::Box(1.f, 1.f, 1.f);
	}
	return std::move(mesh).Unwrap();
}

Result<render3d::Mesh, String> Runtime::LoadGltfMesh(const String &path) {
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

Result<String, String> Runtime::ImportGltf(const String &path, float targetSize) {
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

String Runtime::FileStem(const String &path) {
	size_t slash = path.Rfind('/');
	String name = slash == String::NPOS ? path : path.Substr(slash + 1);
	size_t dot = name.Rfind('.');
	if (dot != String::NPOS && dot > 0)
		name = name.Substr(0, dot);
	return name.IsEmpty() ? String("Modèle") : name;
}

render3d::Mesh Runtime::PortalQuad(float halfWidth, float halfHeight) {
	std::vector<render3d::Vertex3D> vertices = {
		{{-halfWidth, -halfHeight, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f}, sdl3::Color::WHITE()},
		{{halfWidth, -halfHeight, 0.f}, {0.f, 0.f, 1.f}, {1.f, 0.f}, sdl3::Color::WHITE()},
		{{halfWidth, halfHeight, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f}, sdl3::Color::WHITE()},
		{{-halfWidth, halfHeight, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f}, sdl3::Color::WHITE()},
	};
	std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
	return render3d::Mesh(std::move(vertices), std::move(indices));
}

render3d::Material Runtime::BuildMaterial(const MaterialDesc &desc) {
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

physics::Shape Runtime::BuildCollider(const PhysicsDesc &physics) {
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

ecs::Entity Runtime::InstantiateNode(scene::NodeId id, const scene::Node &node) {
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

void Runtime::AttachHelper(scene::NodeId id, const scene::Node &node, render3d::Object3D &owner) {
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

void Runtime::RefreshHelper(scene::NodeId id) {
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

void Runtime::SetHelpersVisible(bool visible) {
	for (auto &[id, helper] : m_helperOf)
		helper->SetVisible(visible);
}

void Runtime::UpdateLights() {
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

void Runtime::AttachBody(ecs::Entity entity, scene::NodeId id, const scene::Node &node) {
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

void Runtime::ApplyParentLinks(const SceneDesc &sceneDesc) {
	sceneDesc.tree.Traverse(sceneDesc.tree.Root(), [&](scene::NodeId id, const scene::Node &node) {
		if (id == sceneDesc.tree.Root() || node.parent == sceneDesc.tree.Root())
			return;
		Option<ecs::Entity> child = FindEntity(id);
		Option<ecs::Entity> parent = FindEntity(node.parent);
		if (child.IsSome() && parent.IsSome())
			render3d::SetSceneParent(m_registry, child.Unwrap(), parent.Unwrap());
	});
}

void Runtime::LinkPortals(const SceneDesc &sceneDesc) {
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

void Runtime::ClearRuntime() {
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

void Runtime::DestroyEntity(scene::NodeId id) {
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

bool Runtime::PushTransform(scene::NodeId id) {
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

bool Runtime::PushMaterial(scene::NodeId id) {
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

bool Runtime::PushPhysics(scene::NodeId id) {
	NotifyObjectChanged();
	const scene::Node *node = FindObject(id);
	Option<ecs::Entity> entity = FindEntity(id);
	if (!node || entity.IsNone())
		return false;
	AttachBody(entity.Unwrap(), id, *node);
	return true;
}

void Runtime::ResetPhysicsBodies() {
	SceneDesc *scene = ActiveScene();
	if (!scene)
		return;
	scene->tree.Traverse(scene->tree.Root(), [&](scene::NodeId id, const scene::Node &node) {
		if (PhysicsDesc::Has(node))
			(void)PushTransform(id);
	});
}

void Runtime::MirrorSimulationToDocument() {
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

void Runtime::UpdatePortalPlanes() {
	m_world.portalPlanes.clear();
	if (!m_portalA.node || !m_portalB.node)
		return;
	m_portalA.UpdateDelta();
	m_portalB.UpdateDelta();
	m_world.portalPlanes.push_back(MakePortalPlane(m_portalA));
	m_world.portalPlanes.push_back(MakePortalPlane(m_portalB));
}

physics::PortalPlane Runtime::MakePortalPlane(const render3d::Portal &portal) {
	physics::PortalPlane plane;
	plane.position = portal.WorldPlanePos();
	plane.normal = portal.WorldPlaneNormal();
	plane.delta = portal.delta;
	plane.deltaInv = portal.deltaInv;
	return plane;
}

void Runtime::RenderPortals() {
	if (!m_canvas || !m_portalsReady || !m_portalA.node || !m_portalB.node)
		return;
	m_portalA.UpdateDelta();
	m_portalB.UpdateDelta();
	render3d::RenderPortalRecursive(*m_canvas, m_sceneRoot, ActiveCamera(), m_portalA, 0, 0, m_portalA_target);
	render3d::RenderPortalRecursive(*m_canvas, m_sceneRoot, ActiveCamera(), m_portalB, 0, 0, m_portalB_target);
}

math::FVector3 Runtime::ForwardFromYawPitch(float yaw, float pitch) noexcept {
	return math::FVector3{sdl3::Cos(pitch) * sdl3::Sin(yaw), sdl3::Sin(pitch), sdl3::Cos(pitch) * sdl3::Cos(yaw)}
		.Normalize();
}

void Runtime::RefreshCameraTargets() {
	m_editCamera.target = m_editCamera.position + ForwardFromYawPitch(m_editYaw, m_editPitch);
	m_playCamera.target = m_playCamera.position + ForwardFromYawPitch(m_playYaw, m_playPitch);
}

void Runtime::UpdateCamera(float dt) {
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

scene::NodeId Runtime::FindCurrentCamera() const {
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

void Runtime::PrepareTriggers(bool resetState) {
	m_triggerCandidates.clear();
	if (resetState) {
		m_triggerInside.clear();
		m_triggerFired.clear();
	}
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

void Runtime::UpdateTriggers() {
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

void Runtime::FireTrigger(const String &hook, scene::NodeId zone, const String &zoneName, const String &otherName,
		const String &event) {
	using data::script::Value;
	if (m_gameplayReady)
		CallGameplayHook(hook, {Value::Str(zoneName), Value::Str(otherName), Value::Str(event)});
	++m_nodeScriptWalk;
	for (size_t s = 0; s < m_nodeScripts.size(); ++s) {
		NodeScriptInstance &instance = *m_nodeScripts[s];
		for (size_t i = 0; i < instance.attached.size() && instance.ready; ++i)
			if (!instance.attached[i].detached && instance.attached[i].node == zone)
				CallNodeHook(instance, i, hook, {Value::Str(otherName), Value::Str(event)});
	}
	--m_nodeScriptWalk;
	CompactNodeScripts();
}

void Runtime::StartNodeScripts() {
	m_nodeScripts.clear();
	const SceneDesc *scene = ActiveScene();
	if (!scene)
		return;
	StartNodeScriptsOf(scene->tree.Root());
}

NodeScriptInstance *Runtime::NodeScriptFor(const String &name) {
	for (const auto &instance : m_nodeScripts)
		if (instance->script == name)
			return instance->ready ? instance.get() : nullptr;
	auto instance = std::make_unique<NodeScriptInstance>();
	instance->script = name;
	NodeScriptInstance &loaded = *instance;
	m_nodeScripts.push_back(std::move(instance));

	const ScriptAsset *asset = m_project.FindScript(name);
	if (!asset) {
		loaded.error = String("script introuvable dans le projet");
		LogWarning(String::Format("Script « %s » : introuvable dans la bibliothèque du projet", name.CStr()));
		return nullptr;
	}
	loaded.vm = std::make_unique<data::script::Interpreter>();
	InstallHostApi(*loaded.vm);
	loaded.vm->SetRandomSeed(m_randomSeed + 0x9E3779B97F4A7C15ull * uint64_t(m_nodeScripts.size()));
	++m_scriptRunCount;
	m_loadedScripts.push_back(String::Format("script:%s", name.CStr()));
	auto run = loaded.vm->Run(asset->source.View());
	if (run.IsError()) {
		++m_scriptErrorCount;
		loaded.error = run.Error().Format();
		LogError(String::Format("Script « %s » : %s", name.CStr(), loaded.error.CStr()));
		return nullptr;
	}
	// Le script DÉFINIT un comportement ; c'est le moteur qui l'instancie,
	// une fois par nœud.
	auto classes = loaded.vm->ClassesDerivedFrom(String(engine_base::BEHAVIOUR));
	if (classes.size() != 1) {
		++m_scriptErrorCount;
		loaded.error = classes.empty()
						   ? String("le script ne définit aucune classe dérivée de `Behaviour` "
									"(class MonComportement extends Behaviour { fn on_update(dt) { … } })")
						   : String::Format("le script définit %d classes dérivées de `Behaviour` : une seule est "
											"attachée aux nœuds (rendez les autres `abstract`)",
											int(classes.size()));
		LogError(String::Format("Script « %s » : %s", name.CStr(), loaded.error.CStr()));
		return nullptr;
	}
	loaded.behaviour = classes.front();
	loaded.ready = true;
	return &loaded;
}

String Runtime::ScriptHandle(scene::NodeId id) const {
	const SceneDesc *scene = ActiveScene();
	const scene::Node *node = scene ? scene->tree.Get(id) : nullptr;
	if (!node)
		return String();
	if (m_nameCountsTree != &scene->tree || m_nameCountsStamp != scene->tree.StructureStamp()) {
		m_nameCounts.clear();
		scene->tree.Traverse(scene->tree.Root(), [&](scene::NodeId, const scene::Node &n) { ++m_nameCounts[n.name]; });
		m_nameCountsTree = &scene->tree;
		m_nameCountsStamp = scene->tree.StructureStamp();
	}
	auto it = m_nameCounts.find(node->name);
	return it != m_nameCounts.end() && it->second > 1 ? scene->tree.PathOf(id) : node->name;
}

void Runtime::StartNodeScriptsOf(scene::NodeId root) {
	using data::script::Value;
	const SceneDesc *scene = ActiveScene();
	if (!scene || !m_playing)
		return;
	std::vector<std::pair<String, scene::NodeId>> carriers;
	scene->tree.Traverse(root, [&](scene::NodeId id, const scene::Node &node) {
		if (ScriptRef::Has(node) && !ScriptRef::Read(node).script.IsEmpty())
			carriers.emplace_back(ScriptRef::Read(node).script, id);
	});
	// 1) Construire toutes les instances (leur `init`)…
	std::vector<std::pair<NodeScriptInstance *, size_t>> created;
	for (const auto &[name, id] : carriers) {
		NodeScriptInstance *instance = NodeScriptFor(name);
		if (!instance || !FindObject(id))
			continue;
		m_bindingNode = id;
		++m_scriptCallCount;
		auto made = instance->vm->CallValue(Value::Class(instance->behaviour), {}, 0, 0);
		m_bindingNode = scene::NodeId{};
		if (made.IsError()) {
			++m_scriptErrorCount;
			instance->error = made.Error().Format();
			LogError(String::Format("Script « %s » (%s) : construction : %s", name.CStr(),
									FindObject(id) ? FindObject(id)->name.CStr() : "?", instance->error.CStr()));
			continue;
		}
		instance->attached.push_back(NodeScriptInstance::Attached{id, made.Unwrap(), false});
		created.emplace_back(instance, instance->attached.size() - 1);
	}
	// 2) …puis leurs `on_start` : chacune trouve les autres déjà en place.
	++m_nodeScriptWalk;
	for (const auto &[instance, index] : created) {
		if (!instance->ready || instance->attached[index].detached)
			continue;
		CallNodeHook(*instance, index, String("on_start"), {});
	}
	--m_nodeScriptWalk;
	CompactNodeScripts();
}

void Runtime::UpdateNodeScripts(float dt) {
	using data::script::Value;
	// Tout objet vivant de l'interpréteur d'un script de nœud reçoit
	// `on_update` : les Behaviour attachées par l'éditeur ET ce qu'elles ont
	// créé (projectiles, effets…). Par indices, tailles relues : un script
	// peut charger une pièce (et donc d'autres scripts) pendant son appel.
	++m_nodeScriptWalk;
	for (size_t s = 0; s < m_nodeScripts.size(); ++s) {
		NodeScriptInstance &instance = *m_nodeScripts[s];
		if (!instance.ready || !instance.vm)
			continue;
		for (const auto &object : instance.vm->Owners().Snapshot()) {
			if (!instance.ready)
				break;
			if (object->destroyed)
				continue;
			auto called = instance.vm->CallMethodIfPresent(Value::Instance(object), String("on_update"),
														   {Value::Number(double(dt))});
			if (called.IsNone())
				continue;
			++m_scriptCallCount;
			if (called.Unwrap().IsError())
				DisableNodeScript(instance, object->klass->Name(), String("on_update"), called.Unwrap().Error());
		}
	}
	--m_nodeScriptWalk;
	CompactNodeScripts();
}

void Runtime::DisableNodeScript(NodeScriptInstance &instance, const String &className, const String &hook,
								const data::script::ScriptError &error) {
	++m_scriptErrorCount;
	instance.ready = false;
	instance.error = error.Format();
	LogError(String::Format("Script « %s » (%s) `%s` : %s", instance.script.CStr(), className.CStr(), hook.CStr(),
							instance.error.CStr()));
	LogWarning(String::Format("Script « %s » désactivé jusqu'au prochain démarrage du mode Jeu", instance.script.CStr()));
}

void Runtime::CallNodeHook(NodeScriptInstance &instance, size_t index, const String &hook,
		std::vector<data::script::Value> args) {
	if (!instance.vm || index >= instance.attached.size())
		return;
	const NodeScriptInstance::Attached entry = instance.attached[index]; // copie : la liste peut grandir
	const scene::Node *target = FindObject(entry.node);
	if (!target || entry.detached)
		return;
	const String targetName = target->name;
	auto called = instance.vm->CallMethodIfPresent(entry.object, hook, std::move(args));
	if (called.IsNone())
		return;
	++m_scriptCallCount;
	if (called.Unwrap().IsError())
		DisableNodeScript(instance, targetName, hook, called.Unwrap().Error());
}

void Runtime::DestroyObjectsOfNodes(const std::vector<scene::NodeId> &doomed) {
	if (doomed.empty())
		return;
	const std::unordered_set<scene::NodeId> ids(doomed.begin(), doomed.end());
	const scene::NodeTree *tree = Tree();
	// Une ressource dont le nœud disparaît est DÉTRUITE (séquence RAII
	// complète), quel que soit l'interpréteur qui l'a créée.
	auto sweep = [&](data::script::Interpreter &vm) {
		for (const auto &object : vm.Owners().Snapshot()) {
			if (object->destroyed)
				continue;
			bool hit = false;
			for (const auto &base : object->owners) {
				auto carrier = std::dynamic_pointer_cast<NodeOwner>(base);
				if (!carrier || carrier->tree != tree || !ids.contains(carrier->node))
					continue;
				hit = true;
				carrier->ownsNode = false; // son retrait est déjà en cours
			}
			if (hit)
				vm.DestroyInstance(object);
		}
	};
	++m_nodeScriptWalk;
	for (size_t s = 0; s < m_nodeScripts.size(); ++s) {
		NodeScriptInstance &instance = *m_nodeScripts[s];
		for (NodeScriptInstance::Attached &entry : instance.attached)
			if (!entry.detached && ids.contains(entry.node)) {
				entry.detached = true;
				m_nodeScriptsDirty = true;
			}
		if (instance.vm)
			sweep(*instance.vm);
	}
	if (m_gameplayVm)
		sweep(*m_gameplayVm);
	sweep(m_toolVm);
	--m_nodeScriptWalk;
	CompactNodeScripts();
}

void Runtime::CompactNodeScripts() {
	if (m_nodeScriptWalk > 0 || !m_nodeScriptsDirty)
		return;
	m_nodeScriptsDirty = false;
	for (const auto &instance : m_nodeScripts)
		std::erase_if(instance->attached, [](const NodeScriptInstance::Attached &entry) { return entry.detached; });
}

scene::NodeId Runtime::FollowTarget() const {
	const SceneDesc *sceneDesc = ActiveScene();
	if (!sceneDesc)
		return scene::NodeId{};
	std::vector<scene::NodeId> targets = sceneDesc->WithTag(String("camera_target"));
	return targets.empty() ? scene::NodeId{} : targets.front();
}

Result<data::script::Value, data::script::ScriptError> Runtime::RunGameplayScript(const String &source) {
	++m_scriptRunCount;
	m_loadedScripts.push_back(String::Format("gameplay:%s", ActiveScene() ? ActiveScene()->name.CStr() : "?"));
	return m_gameplayVm->Run(source.View());
}

bool Runtime::CreateSceneObject() {
	auto classes = m_gameplayVm->ClassesDerivedFrom(String(engine_base::SCENE));
	if (classes.size() != 1) {
		++m_scriptErrorCount;
		LogError(classes.empty()
					 ? String("Script de jeu : aucune classe dérivée de `Scene` (class MaScène extends Scene { "
							  "fn on_start() { … } fn on_update(dt) { … } })")
					 : String::Format("Script de jeu : %d classes dérivées de `Scene` — une seule est instanciée "
									  "(rendez les autres `abstract`)",
									  int(classes.size())));
		return false;
	}
	++m_scriptCallCount;
	auto made = m_gameplayVm->CallValue(data::script::Value::Class(classes.front()), {}, 0, 0);
	if (made.IsError()) {
		++m_scriptErrorCount;
		LogError(String::Format("Script de jeu : construction de `%s` : %s", classes.front()->Name().CStr(),
								made.Error().Format().CStr()));
		return false;
	}
	m_sceneObject = made.Unwrap();
	return true;
}

void Runtime::CallGameplayHook(const String &hook, std::vector<data::script::Value> args) {
	if (m_sceneObject.IsNil())
		return;
	auto called = m_gameplayVm->CallMethodIfPresent(m_sceneObject, hook, std::move(args));
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

void Runtime::RunGameplayHook(float dt) {
	using data::script::Value;
	// L'objet Scene (créé le premier, donc en tête du registre) puis tous les
	// objets que le script de scène a créés et qui définissent `on_update`.
	for (const auto &object : m_gameplayVm->Owners().Snapshot()) {
		if (!m_gameplayReady)
			return;
		if (object->destroyed)
			continue;
		auto called = m_gameplayVm->CallMethodIfPresent(Value::Instance(object), String("on_update"),
														{Value::Number(double(dt))});
		if (called.IsNone())
			continue;
		++m_scriptCallCount;
		if (called.Unwrap().IsError()) {
			++m_scriptErrorCount;
			LogError(String::Format("Script de jeu (%s) `on_update` : %s", object->klass->Name().CStr(),
									called.Unwrap().Error().Format().CStr()));
			m_gameplayReady = false;
			LogWarning(String("Script de jeu désactivé jusqu'au prochain démarrage du mode Jeu"));
		}
	}
}

void Runtime::StartScenePlay() {
	SceneDesc *scene = ActiveScene();
	if (!scene)
		return;
	m_playSnapshot = scene->tree;
	m_playing = true;
	m_playCamera.position = scene->camera.playPosition;
	m_playYaw = scene->camera.playYaw;
	m_playPitch = scene->camera.playPitch;
	m_followDistance = 13.f; // cf. camera.follow
	m_followHeight = 5.5f;
	m_followStiffness = 6.f;
	ResetPhysicsBodies();
	ResetGameplayVm();
	if (onGameUiReset)
		onGameUiReset(); // calque d'interface vierge AVANT `on_start`

	m_gameplayReady = false;
	m_sceneObject = data::script::Value::Nil();
	if (!scene->gameplayScript.IsEmpty()) {
		auto loaded = RunGameplayScript(scene->gameplayScript);
		if (loaded.IsError()) {
			++m_scriptErrorCount;
			LogError(String::Format("Script de jeu : %s", loaded.Error().Format().CStr()));
		} else if (CreateSceneObject()) {
			m_gameplayReady = true;
			CallGameplayHook(String("on_start"), {});
		}
	}
	SetHelpersVisible(false);
	m_currentCamera = FindCurrentCamera();
	PrepareTriggers(true);
	StartNodeScripts();
	// Les objets de script viennent de naître : l'arbre affiche leurs classes.
	if (onSceneStructureChanged)
		onSceneStructureChanged();
}

void Runtime::EndScenePlay() {
	// Fin de scène = destruction collective, dans l'ordre RAII
	// (`on_destroy` → `deinit` → bases du moteur en ordre inverse) : les
	// Behaviour des nœuds, puis l'objet Scene et ce que les scripts ont créé.
	// Aucun script n'a à s'en soucier ; un objet déjà détruit à la main
	// (`destroy()`) ne repasse pas.
	for (auto it = m_nodeScripts.rbegin(); it != m_nodeScripts.rend(); ++it)
		if ((*it)->vm)
			(*it)->vm->Owners().DestroyAll(*(*it)->vm);
	if (m_gameplayVm)
		m_gameplayVm->Owners().DestroyAll(*m_gameplayVm);
	m_sceneObject = data::script::Value::Nil();
	m_gameplayReady = false;

	m_nodeScripts.clear();
	m_draw2d.clear();
	m_triggerInside.clear();
	m_triggerFired.clear();
	m_triggerCandidates.clear();
	m_currentCamera = scene::NodeId{};
	m_playCamera.fovYRadians = m_editCamera.fovYRadians;
	if (SceneDesc *scene = ActiveScene())
		scene->tree = m_playSnapshot;
}

void Runtime::ResetGameplayVm() {
	m_sceneObject = data::script::Value::Nil();
	m_gameplayVm = std::make_unique<data::script::Interpreter>();
	InstallHostApi(*m_gameplayVm);
	m_gameplayVm->SetRandomSeed(m_randomSeed);
}

void Runtime::BindEcsWorld() {
	using data::script::Value;
	m_ecsWorld = std::make_shared<data::script::EcsWorld>(m_registry);
	m_ecsWorld->BindComponent<SceneObjectRef>(String("node"), [this](const SceneObjectRef &ref) -> Value {
		const scene::Node *node = FindObject(ref.node);
		return node ? Value::Str(node->name) : Value::Nil();
	});
	auto list = [](std::initializer_list<float> values) {
		auto out = std::make_shared<data::script::ListObject>();
		for (float v : values)
			out->items.push_back(Value::Float(v, data::script::NumberType::F32));
		return Value::List(std::move(out));
	};
	m_ecsWorld->BindComponent<render3d::SceneTransform>(
		String("transform"),
		[list](const render3d::SceneTransform &t) -> Value {
			auto map = std::make_shared<data::script::MapObject>();
			map->SetKey(String("position"), list({t.position.x, t.position.y, t.position.z}));
			map->SetKey(String("rotation"), list({t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w}));
			map->SetKey(String("scale"), list({t.scale.x, t.scale.y, t.scale.z}));
			return Value::Map(std::move(map));
		},
		[](const Value &v, render3d::SceneTransform &t) -> Option<String> {
			if (!v.IsMap() || !v.AsMap())
				return Some(String("table {position, rotation, scale} attendue"));
			auto read = [&](const char *key, float *out, size_t n) -> bool {
				Option<Value> item = v.AsMap()->Get(String(key));
				if (item.IsNone())
					return true;
				if (!item.Value().IsList() || item.Value().AsList()->Size() != n)
					return false;
				for (size_t i = 0; i < n; ++i)
					out[i] = item.Value().AsList()->At(i).Unwrap().AsFloat();
				return true;
			};
			float p[3] = {t.position.x, t.position.y, t.position.z};
			float r[4] = {t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w};
			float sc[3] = {t.scale.x, t.scale.y, t.scale.z};
			if (!read("position", p, 3) || !read("rotation", r, 4) || !read("scale", sc, 3))
				return Some(String("position/scale : 3 nombres, rotation : 4 (quaternion)"));
			t.position = {p[0], p[1], p[2]};
			t.rotation = math::FQuaternion(r[0], r[1], r[2], r[3]);
			t.scale = {sc[0], sc[1], sc[2]};
			return NONE;
		});
}

namespace script_detail {

Value Vec3ToValue(const math::FVector3 &v) {
	auto list = std::make_shared<data::script::ListObject>();
	list->items.push_back(Value::Number(double(v.x)));
	list->items.push_back(Value::Number(double(v.y)));
	list->items.push_back(Value::Number(double(v.z)));
	return Value::List(std::move(list));
}

Result<math::FVector3, ScriptError> ArgVec3(const std::vector<Value> &args, size_t first, const char *fnName) {
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

math::FVector3 FieldVec3(const data::script::MapObject &map, const char *key, math::FVector3 fallback) {
	const Value *found = map.Find(String(key));
	if (!found || !found->IsList() || !found->AsList() || found->AsList()->items.size() < 3)
		return fallback;
	const std::vector<Value> &items = found->AsList()->items;
	if (!items[0].IsNumber() || !items[1].IsNumber() || !items[2].IsNumber())
		return fallback;
	return {items[0].AsFloat(), items[1].AsFloat(), items[2].AsFloat()};
}

float FieldFloat(const data::script::MapObject &map, const char *key, float fallback) {
	const Value *found = map.Find(String(key));
	return (found && found->IsNumber()) ? found->AsFloat() : fallback;
}

bool FieldBool(const data::script::MapObject &map, const char *key, bool fallback) {
	const Value *found = map.Find(String(key));
	return found ? found->IsTruthy() : fallback;
}

String FieldString(const data::script::MapObject &map, const char *key, const char *fallback) {
	const Value *found = map.Find(String(key));
	return (found && found->IsString()) ? found->AsString() : String(fallback);
}

Option<sdl3::Color> FieldColor(const data::script::MapObject &map, const char *key) {
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

sdl3::Color ColorFromVec3(const math::FVector3 &rgb) {
	auto channel = [](float v) { return uint8_t(sdl3::Clamp(v, 0.f, 255.f)); };
	return sdl3::Color{channel(rgb.x), channel(rgb.y), channel(rgb.z), 255};
}

Value PropertyToValue(const scene::PropertyValue *value) {
	if (!value)
		return Value::Nil();
	switch (value->Type()) {
		case scene::PropertyType::BOOL:
			return Value::Boolean(value->AsBool());
		case scene::PropertyType::INT:
		case scene::PropertyType::FLOAT:
			return Value::Number(double(value->AsFloat()));
		case scene::PropertyType::VEC3:
			return Vec3ToValue(value->AsVec3());
		default:
			return Value::Str(value->ToDisplayString());
	}
}

void WriteProperty(scene::Node &node, const String &key, const Value &value) {
	if (value.IsBoolean())
		node.Set(key, scene::PropertyValue::Bool(value.IsTruthy()));
	else if (value.IsNumber())
		node.Set(key, scene::PropertyValue::Float(value.AsFloat()));
	else if (value.IsNil())
		(void)node.properties.Remove(key);
	else
		node.Set(key, scene::PropertyValue::Str(value.ToDisplayString()));
}

void ReadMaterialTable(const data::script::MapObject &map, MaterialDesc &material) {
	// `kind` (forme de set_material) ou `material` (forme de scene.spawn).
	const String kind = FieldString(map, "kind", FieldString(map, "material", "").CStr());
	if (!kind.IsEmpty())
		material.kind = MaterialKindFromName(kind).UnwrapOr(material.kind);
	if (auto color = FieldColor(map, "color"); color.IsSome())
		material.baseColor = color.Unwrap();
	material.metallic = FieldFloat(map, "metallic", material.metallic);
	material.roughness = FieldFloat(map, "roughness", material.roughness);
	material.doubleSided = FieldBool(map, "double_sided", material.doubleSided);
	material.wireframe = FieldBool(map, "wireframe", material.wireframe);
}

void ReadPhysicsTable(const data::script::MapObject &map, PhysicsDesc &physics) {
	if (const Value *body = map.Find(String("body")); body && body->IsMap() && body->AsMap())
		ReadPhysicsTable(*body->AsMap(), physics);
	const String kind = FieldString(map, "body", FieldString(map, "kind", "").CStr());
	if (!kind.IsEmpty())
		physics.body = BodyKindFromName(kind).UnwrapOr(physics.body);
	physics.collider =
		ColliderKindFromName(FieldString(map, "collider", ColliderKindName(physics.collider))).UnwrapOr(physics.collider);
	physics.halfExtents = FieldVec3(map, "half_extents", physics.halfExtents);
	physics.mass = FieldFloat(map, "mass", physics.mass);
	physics.restitution = FieldFloat(map, "restitution", physics.restitution);
	physics.friction = FieldFloat(map, "friction", physics.friction);
}

ObjectDesc ObjectFromTable(const data::script::MapObject &map) {
	ObjectDesc object;
	object.name = FieldString(map, "name", "Objet");
	object.parent = FieldString(map, "parent", "");
	object.tag = FieldString(map, "tag", "");
	object.shape = ShapeKindFromName(FieldString(map, "shape", "box")).UnwrapOr(ShapeKind::BOX);
	const String model = FieldString(map, "model", "");
	if (!model.IsEmpty()) {
		object.shape = ShapeKind::MODEL;
		object.source = model; // relatif au projet : résolu par l'appelant
	}
	object.dimensions = FieldVec3(map, "size", math::FVector3{1.f, 1.f, 1.f});
	object.segments = int(FieldFloat(map, "segments", 24.f));
	object.visible = FieldBool(map, "visible", true);
	object.transform.position = FieldVec3(map, "pos", math::FVector3{});
	object.transform.SetEulerDegrees(FieldVec3(map, "rot", math::FVector3{}));
	object.transform.scale = FieldVec3(map, "scale", math::FVector3{1.f, 1.f, 1.f});

	object.material.kind = MaterialKind::PLASTIC;
	ReadMaterialTable(map, object.material);
	if (!map.Find(String("roughness")))
		object.material.roughness = 0.5f;

	object.physics.body = BodyKind::NONE;
	object.physics.collider = ColliderKind::BOX;
	object.physics.mass = 1.f;
	object.physics.restitution = 0.3f;
	object.physics.friction = 0.5f;
	// Par défaut, le volume de collision épouse la forme affichée : un
	// script n'a à préciser `half_extents` que s'il veut l'en détacher (une
	// piste plate qui collisionne plus épais, etc.).
	object.physics.halfExtents = object.dimensions * 0.5f * object.transform.scale;
	ReadPhysicsTable(map, object.physics);
	return object;
}

LightDesc LightFromTable(const data::script::MapObject &map, LightDesc base) {
	const String kind = FieldString(map, "kind", "");
	if (!kind.IsEmpty())
		base.kind = LightKindFromName(kind).UnwrapOr(base.kind);
	if (auto color = FieldColor(map, "color"); color.IsSome())
		base.color = color.Unwrap();
	base.intensity = sdl3::Max(0.f, FieldFloat(map, "intensity", base.intensity));
	base.range = sdl3::Max(0.f, FieldFloat(map, "range", base.range));
	base.spotAngle = FieldFloat(map, "spot_angle", base.spotAngle);
	base.penumbra = FieldFloat(map, "penumbra", base.penumbra);
	base.castShadow = FieldBool(map, "shadow", base.castShadow);
	return base;
}

Option<scene::Transform> PlacementFromTable(const Runtime &runtime, const data::script::MapObject &map,
											scene::NodeId &parent) {
	const String parentName = FieldString(map, "parent", "");
	if (!parentName.IsEmpty())
		parent = runtime.ResolveId(parentName);
	if (!map.Find(String("pos")) && !map.Find(String("rot")) && !map.Find(String("scale")))
		return NONE;
	scene::Transform t;
	t.position = FieldVec3(map, "pos", math::FVector3{});
	t.SetEulerDegrees(FieldVec3(map, "rot", math::FVector3{}));
	t.scale = FieldVec3(map, "scale", math::FVector3{1.f, 1.f, 1.f});
	return Some(t);
}

Option<SDL_Keycode> ScancodeFromName(const String &name) {
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
		{"f1", SDLK_F1},       {"f2", SDLK_F2},        {"f3", SDLK_F3},
		{"f4", SDLK_F4},       {"f5", SDLK_F5},        {"f6", SDLK_F6},
		{"f7", SDLK_F7},       {"f8", SDLK_F8},        {"f9", SDLK_F9},
		{"f10", SDLK_F10},     {"f11", SDLK_F11},      {"f12", SDLK_F12},
	};
	String key = name.ToLower();
	for (const Entry &entry : TABLE)
		if (key == entry.name)
			return Some(entry.code);
	// Une lettre ou un chiffre seul : son code de touche est son caractère.
	if (key.GetSize() == 1) {
		const char c = key.CStr()[0];
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
			return Some(SDL_Keycode(c));
	}
	return NONE;
}

} // namespace script_detail

void Runtime::InstallHostApi(data::script::Interpreter &vm) {
	using data::script::Interpreter;
	using data::script::ScriptError;
	using data::script::Value;
	namespace sd = script_detail;

	// Les classes de base du moteur (`Scene`, `Behaviour`, `Mesh3D`…) dont
	// dérivent les classes des scripts de jeu (cf. script_owners.hpp).
	InstallEngineBases(vm, *this);
	// `import "commun"` : un script de la bibliothèque du projet, lu EN
	// MÉMOIRE (les modifications non enregistrées comptent) — sinon un
	// fichier du dossier `scripts/` du projet. C'est ainsi que des scènes
	// partagent une classe de base commune.
	vm.SetImportResolver([this](const String &specifier) -> Option<std::pair<String, String>> {
		String name = specifier;
		if (name.EndsWith(".script"))
			name = name.Substring(0, name.GetSize() - 7);
		if (const ScriptAsset *asset = m_project.FindScript(name))
			return Some(std::make_pair(String::Format("project:%s", name.CStr()), asset->source));
		return NONE; // l'interpréteur cherchera le fichier (cf. AddImportPath)
	});
	if (const String directory = ProjectDirectory(); !directory.IsEmpty())
		vm.AddImportPath(directory + String("/scripts"));
	// Les codecs data:: (parse/encode/read_file/write_file/load) : un script
	// d'éditeur lit ainsi directement un .gltf, un .json de projet ou une
	// table de réglages YAML.
	data::script::InstallDataLibrary(vm);
	// L'ECS de l'éditeur (`ecs.host()`, objets de la scène : `node`,
	// `transform`) et l'interface des scripts (`ui.*`, cf. GameUi()).
	data::script::InstallEcsLibrary(vm, m_ecsWorld);
	data::script::InstallUiLibrary(vm, m_gameUi);
	data::script::InstallGeneratorLibrary(vm); // `gen.*` : bruits, terrains, donjons…
	Install2DApi(vm);                          // `node2d.*`, `canvas.*`, `draw2d.*` (runtime2d.hpp)

	// ── game.* : la PARTIE (enchaînement des scènes d'un jeu) ────────────────
	Runtime *game = this;
	// `game.load_scene(nom)` : pendant une partie, la scène change au début
	// de l'image suivante (le script en cours finit son image) ; hors partie,
	// comme `editor.switch_scene`.
	vm.RegisterNamespacedNative(String("game"), String("load_scene"), 1, 1,
							   [game](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "game.load_scene");
								   if (name.IsError())
									   return Err(name.Error());
								   if (!game->RequestScene(name.Value()))
									   return Err(Interpreter::MakeError(String::Format(
										   "`game.load_scene` : scène « %s » introuvable", name.Value().CStr())));
								   return Ok(Value::Boolean(true));
							   });
	vm.RegisterNamespacedNative(String("game"), String("quit"), 0, 0,
							   [game](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   game->RequestQuit();
								   return Ok(Value::Nil());
							   });
	vm.RegisterNamespacedNative(String("game"), String("state"), 0, 0,
							   [game](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   return Ok(game->GameState());
							   });
	vm.RegisterNamespacedNative(String("game"), String("scene"), 0, 0,
							   [game](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   const SceneDesc *scene = game->ActiveScene();
								   return Ok(scene ? Value::Str(scene->name) : Value::Nil());
							   });
	vm.RegisterNamespacedNative(String("game"), String("scenes"), 0, 0,
							   [game](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   auto list = std::make_shared<data::script::ListObject>();
								   for (const SceneDesc &scene : game->m_project.scenes)
									   list->items.push_back(Value::Str(scene.name));
								   return Ok(Value::List(std::move(list)));
							   });
	vm.RegisterNamespacedNative(String("game"), String("time"), 0, 0,
							   [game](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   return Ok(Value::Number(double(game->m_playTime)));
							   });
	vm.RegisterNamespacedNative(String("game"), String("is_playing"), 0, 0,
							   [game](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   return Ok(Value::Boolean(game->m_playing));
							   });
	vm.RegisterNamespacedNative(String("game"), String("origin"), 0, 0,
							   [game](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   return Ok(Value::Str(game->m_sessionOrigin));
							   });

	// `print` d'un script atterrit dans la console de l'éditeur, pas sur la
	// sortie standard — c'est l'endroit où l'utilisateur le cherche.
	vm.onPrint = [this](const String &line) { Log(LogLevel::SCRIPT, line); };
	// Erreur d'une fonction `async` que personne n'a attendue : jamais avalée.
	vm.onAsyncError = [this](const ScriptError &error) {
		++m_scriptErrorCount;
		LogError(String::Format("Script async : %s", error.Format().CStr()));
	};

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
	/// `editor.owners()` → liste de tables {type, script, name, display,
	/// alive} : les objets des scripts dérivés d'une base du moteur, lus dans
	/// les registres C++ (un script bogué ne fige pas l'outliner).
	vm.RegisterNamespacedNative(String("editor"), String("owners"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   auto list = std::make_shared<data::script::ListObject>();
								   for (const data::script::OwnerInfo &info : self->LiveOwners()) {
									   auto entry = std::make_shared<data::script::MapObject>();
									   entry->SetKey(String("type"), Value::Str(info.typeName));
									   entry->SetKey(String("script"), Value::Str(info.scriptName));
									   entry->SetKey(String("name"), Value::Str(info.name));
									   entry->SetKey(String("display"), Value::Str(info.display));
									   entry->SetKey(String("alive"), Value::Boolean(info.alive));
									   list->items.push_back(Value::Map(std::move(entry)));
								   }
								   return Ok(Value::List(std::move(list)));
							   });

	/// `editor.owner_count(type?)` → nombre de bases vivantes (toutes, ou du
	/// type qualifié donné : `editor.owner_count("game.Mesh3D")`).
	vm.RegisterNamespacedNative(String("editor"), String("owner_count"), 0, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   const std::vector<data::script::OwnerInfo> all = self->LiveOwners();
								   if (args.empty())
									   return Ok(Value::Number(double(all.size())));
								   auto wanted = data::script::detail::ArgString(args, 0, "editor.owner_count");
								   if (wanted.IsError())
									   return Err(wanted.Error());
								   size_t n = 0;
								   for (const auto &info : all)
									   n += info.typeName == wanted.Value() ? 1 : 0;
								   return Ok(Value::Number(double(n)));
							   });

	/// `editor.destroy_all_owners()` → nombre d'objets détruits. Hors partie
	/// uniquement : en partie, la fin de scène s'en charge, et détruire
	/// l'objet Scene en cours d'appel n'aurait pas de sens.
	vm.RegisterNamespacedNative(String("editor"), String("destroy_all_owners"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   if (self->IsPlaying())
									   return Err(Interpreter::MakeError(String(
										   "`editor.destroy_all_owners` : pas pendant une partie (la fin de scène "
										   "détruit déjà tout)")));
								   return Ok(Value::Number(double(self->DestroyAllScriptObjects())));
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

	// ── Projet : fichiers ────────────────────────────────────────────────────
	auto boolResult = [self](Result<bool, String> result, const String &success) -> Result<Value, ScriptError> {
		if (result.IsError()) {
			self->LogError(result.Error());
			return Ok(Value::Boolean(false));
		}
		if (!success.IsEmpty())
			self->LogSuccess(success);
		return Ok(Value::Boolean(true));
	};

	// editor.save() : à l'emplacement du projet ; editor.save(chemin) : sous
	// ce manifeste (qui devient celui du projet).
	vm.RegisterNamespacedNative(String("editor"), String("save"), 0, 1,
							   [self, boolResult](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   if (args.empty())
									   return boolResult(self->SaveProject(),
														 String::Format("Projet enregistré : %s", self->ProjectPath().CStr()));
								   auto path = data::script::detail::ArgString(args, 0, "editor.save");
								   if (path.IsError())
									   return Err(path.Error());
								   return boolResult(self->SaveProjectFile(path.Value()),
													 String::Format("Projet enregistré : %s", path.Value().CStr()));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("open"), 1, 1,
							   [self, boolResult](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto path = data::script::detail::ArgString(args, 0, "editor.open");
								   if (path.IsError())
									   return Err(path.Error());
								   auto loaded = self->LoadProjectFile(path.Value());
								   if (loaded.IsOk() && self->onProjectChanged)
									   self->onProjectChanged();
								   return boolResult(std::move(loaded), String());
							   });

	vm.RegisterNamespacedNative(String("editor"), String("new_project"), 1, 2,
							   [self, boolResult](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "editor.new_project");
								   if (name.IsError())
									   return Err(name.Error());
								   String root = self->projectsRoot;
								   if (args.size() > 1) {
									   auto given = data::script::detail::ArgString(args, 1, "editor.new_project");
									   if (given.IsError())
										   return Err(given.Error());
									   root = given.Value();
								   }
								   auto created = self->CreateProject(root, name.Value());
								   if (created.IsOk() && self->onProjectChanged)
									   self->onProjectChanged();
								   return boolResult(std::move(created), String());
							   });

	vm.RegisterNamespacedNative(String("editor"), String("close_project"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   self->CloseProject();
								   if (self->onProjectChanged)
									   self->onProjectChanged();
								   return Ok(Value::Nil());
							   });

	vm.RegisterNamespacedNative(String("editor"), String("project_path"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   return Ok(Value::Str(self->ProjectPath()));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("save_scene_as"), 1, 1,
							   [self, boolResult](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto path = data::script::detail::ArgString(args, 0, "editor.save_scene_as");
								   if (path.IsError())
									   return Err(path.Error());
								   return boolResult(self->SaveSceneAs(path.Value()),
													 String::Format("Scène enregistrée : %s", path.Value().CStr()));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("import_scene"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto path = data::script::detail::ArgString(args, 0, "editor.import_scene");
								   if (path.IsError())
									   return Err(path.Error());
								   auto imported = self->ImportSceneFile(path.Value());
								   if (imported.IsError()) {
									   self->LogError(imported.Error());
									   return Ok(Value::Nil());
								   }
								   if (self->onProjectChanged)
									   self->onProjectChanged();
								   return Ok(Value::Str(imported.Value()));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("new_scene"), 0, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   String base("Nouvelle scène");
								   if (!args.empty()) {
									   auto given = data::script::detail::ArgString(args, 0, "editor.new_scene");
									   if (given.IsError())
										   return Err(given.Error());
									   base = given.Value();
								   }
								   const String name = self->AddEmptyScene(base);
								   if (self->onProjectChanged)
									   self->onProjectChanged();
								   return Ok(Value::Str(name));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("save_script_as"), 2, 2,
							   [self, boolResult](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "editor.save_script_as");
								   auto path = data::script::detail::ArgString(args, 1, "editor.save_script_as");
								   if (name.IsError())
									   return Err(name.Error());
								   if (path.IsError())
									   return Err(path.Error());
								   return boolResult(self->SaveScriptAs(name.Value(), path.Value()),
													 String::Format("Script enregistré : %s", path.Value().CStr()));
							   });

	vm.RegisterNamespacedNative(String("editor"), String("import_script"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto path = data::script::detail::ArgString(args, 0, "editor.import_script");
								   if (path.IsError())
									   return Err(path.Error());
								   auto imported = self->ImportScriptFile(path.Value());
								   if (imported.IsError()) {
									   self->LogError(imported.Error());
									   return Ok(Value::Nil());
								   }
								   return Ok(Value::Str(imported.Value()));
							   });

	// ── scene.* ──────────────────────────────────────────────────────────────

	vm.RegisterNamespacedNative(
		String("scene"), String("spawn"), 1, 1,
		[self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
			auto table = data::script::detail::ArgMap(args, 0, "scene.spawn");
			if (table.IsError())
				return Err(table.Error());
			ObjectDesc object = sd::ObjectFromTable(*table.Value());
			if (object.shape == ShapeKind::MODEL)
				object.source = self->ResolveProjectPath(object.source);

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
								   return Ok(Value::Boolean(
									   self->SetMaterialColor(name.Value(), sd::ColorFromVec3(rgb.Value()))));
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
			sd::ReadMaterialTable(*table.Value(), material);
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
			sd::ReadPhysicsTable(*table.Value(), physics);
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

	// Caméra de poursuite (objet taggé `camera_target`) : distance derrière
	// lui, hauteur et raideur du ressort — `camera.follow(8, 12)` pour une
	// vue plongeante. Remis aux valeurs par défaut à chaque scène jouée.
	vm.RegisterNamespacedNative(String("camera"), String("follow"), 2, 3,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   for (const Value &arg : args)
									   if (!arg.IsNumber())
										   return Err(Interpreter::MakeError(String(
											   "`camera.follow(distance, hauteur[, raideur])` : nombres attendus")));
								   self->m_followDistance = sdl3::Clamp(args[0].AsFloat(), 0.5f, 500.f);
								   self->m_followHeight = sdl3::Clamp(args[1].AsFloat(), -50.f, 500.f);
								   if (args.size() > 2)
									   self->m_followStiffness = sdl3::Clamp(args[2].AsFloat(), 0.1f, 60.f);
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
								   if (args.size() == 2)
									   return Ok(sd::PropertyToValue(node->Get(key.Value())));
								   sd::WriteProperty(*node, key.Value(), args[2]);
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
								   // node.instantiate(chemin[, parent | {parent, pos, rot, scale}])
								   scene::NodeId parent;
								   Option<scene::Transform> placement = NONE;
								   if (args.size() > 1 && args[1].IsString())
									   parent = self->ResolveId(args[1].AsString());
								   if (args.size() > 1 && args[1].IsMap() && args[1].AsMap())
									   placement = sd::PlacementFromTable(*self, *args[1].AsMap(), parent);
								   auto created = self->InstantiateSceneFile(path.Value(), parent, placement);
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
	// ── Souris du jeu : libre (interface cliquable), capturée, confinée ──────

	/// `input.set_mouse_mode("free" | "captured" | "confined")`.
	vm.RegisterNamespacedNative(String("input"), String("set_mouse_mode"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   auto name = data::script::detail::ArgString(args, 0, "input.set_mouse_mode");
								   if (name.IsError())
									   return Err(name.Error());
								   Option<MouseMode> mode = MouseModeFromName(name.Value());
								   if (mode.IsNone())
									   return Err(Interpreter::MakeError(String::Format(
										   "`input.set_mouse_mode` : mode `%s` inconnu (free, captured, confined)",
										   name.Value().CStr())));
								   self->SetMouseMode(mode.Unwrap());
								   return Ok(Value::Nil());
							   });
	vm.RegisterNamespacedNative(String("input"), String("mouse_mode"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   return Ok(Value::Str(String(MouseModeName(self->EffectiveMouseMode()))));
							   });
	/// Bascule capturée ↔ libre ; rend le nouveau mode.
	vm.RegisterNamespacedNative(String("input"), String("toggle_mouse_mode"), 0, 0,
							   [self](Interpreter &, std::vector<Value> &) -> Result<Value, ScriptError> {
								   return Ok(Value::Str(String(MouseModeName(self->ToggleMouseCapture()))));
							   });
	/// `input.set_mouse_toggle_key("tab")` : touche qui bascule la capture
	/// pendant la partie (F7 par défaut) ; `nil` la désactive.
	vm.RegisterNamespacedNative(String("input"), String("set_mouse_toggle_key"), 1, 1,
							   [self](Interpreter &, std::vector<Value> &args) -> Result<Value, ScriptError> {
								   if (args[0].IsNil()) {
									   self->SetMouseToggleKey(NONE);
									   return Ok(Value::Nil());
								   }
								   auto name = data::script::detail::ArgString(args, 0, "input.set_mouse_toggle_key");
								   if (name.IsError())
									   return Err(name.Error());
								   Option<SDL_Keycode> code = sd::ScancodeFromName(name.Value());
								   if (code.IsNone())
									   return Err(Interpreter::MakeError(String::Format(
										   "`input.set_mouse_toggle_key` : touche inconnue `%s`", name.Value().CStr())));
								   self->SetMouseToggleKey(code);
								   return Ok(Value::Nil());
							   });

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
								   LightDesc light = LightDesc::Read(*node.Value());
								   light.color = sd::ColorFromVec3(color.Value());
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
