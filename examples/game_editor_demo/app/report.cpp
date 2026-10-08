// Définitions de report.hpp
#include "report.hpp"

namespace game_editor {

// ── ThreadTracker ────────────────────────────────────────────────────────────

ThreadTracker::ThreadTracker() {
	m_live.Store(1);
	m_peak.Store(1);
	m_started.Store(1);
}

void ThreadTracker::Enter() noexcept {
	int live = m_live.FetchAdd(1) + 1;
	m_started.FetchAdd(1);
	// Boucle de compare-échange : deux fils qui démarrent en même temps
	// pourraient sinon écraser mutuellement le maximum.
	for (;;) {
		int peak = m_peak.Load();
		if (live <= peak || m_peak.CompareExchange(peak, live))
			break;
	}
}

// ── FrameStats ───────────────────────────────────────────────────────────────

void FrameStats::Push(double seconds) {
	++m_count;
	// La première image est mesurée mais EXCLUE des extrêmes (cf. en-tête).
	if (m_count == 1) {
		m_firstFrameSeconds = seconds;
		return;
	}
	if (seconds <= 0.0)
		return;
	m_totalSeconds += seconds;
	++m_sampleCount;
	if (m_sampleCount == 1 || seconds < m_minSeconds)
		m_minSeconds = seconds;
	if (m_sampleCount == 1 || seconds > m_maxSeconds)
		m_maxSeconds = seconds;
	m_samples.push_back(seconds);
}

double FrameStats::AverageFps() const noexcept {
	return m_totalSeconds > 0.0 ? double(m_sampleCount) / m_totalSeconds : 0.0;
}

double FrameStats::RecentFps(size_t window) const {
	if (m_samples.empty())
		return 0.0;
	size_t take = m_samples.size() < window ? m_samples.size() : window;
	double total = 0.0;
	for (size_t i = m_samples.size() - take; i < m_samples.size(); ++i)
		total += m_samples[i];
	return total > 0.0 ? double(take) / total : 0.0;
}

double FrameStats::PercentileMs(double percentile) const {
	if (m_samples.empty())
		return 0.0;
	std::vector<double> sorted = m_samples;
	// Tri par insertion : quelques centaines d'échantillons, appelé une
	// seule fois à la fin — inutile de tirer <algorithm> pour ça.
	for (size_t i = 1; i < sorted.size(); ++i) {
		double key = sorted[i];
		size_t j = i;
		while (j > 0 && sorted[j - 1] > key) {
			sorted[j] = sorted[j - 1];
			--j;
		}
		sorted[j] = key;
	}
	double rank = percentile * double(sorted.size() - 1) / 100.0;
	size_t index = size_t(rank < 0.0 ? 0.0 : rank);
	if (index >= sorted.size())
		index = sorted.size() - 1;
	return sorted[index] * 1000.0;
}

// ── PhaseStats ───────────────────────────────────────────────────────────────

const char * PhaseStats::Name(int phase) noexcept {
	switch (phase) {
		case SCENARIO:
			return "scénario";
		case SIMULATION:
			return "simulation";
		case PORTALS:
			return "portails";
		case UI_STYLE:
			return "styles";
		case UI_LAYOUT:
			return "mise en page";
		case UI_VIEWPORT:
			return "viewport 3D";
		case UI_EFFECTS:
			return "effets";
		case UI_DRAW:
			return "dessin 2D";
		case PRESENT:
			return "présentation";
		default:
			break;
	}
	return "?";
}

void PhaseStats::Add(int phase, double milliseconds) {
	if (phase < 0 || phase >= COUNT)
		return;
	m_total[size_t(phase)] += milliseconds;
	if (milliseconds > m_peak[size_t(phase)])
		m_peak[size_t(phase)] = milliseconds;
}

double PhaseStats::AverageMs(int phase) const noexcept {
	if (phase < 0 || phase >= COUNT || m_frames == 0)
		return 0.0;
	return m_total[size_t(phase)] / double(m_frames);
}

double PhaseStats::PeakMs(int phase) const noexcept {
	return (phase < 0 || phase >= COUNT) ? 0.0 : m_peak[size_t(phase)];
}

double PhaseStats::TotalAverageMs() const noexcept {
	double sum = 0.0;
	for (int phase = 0; phase < COUNT; ++phase)
		sum += AverageMs(phase);
	return sum;
}

// ── RunReport ────────────────────────────────────────────────────────────────

String RunReport::ToText(const Project &project) const {
	String out;
	auto line = [&out](const String &text) {
		out.Concat(text);
		out.Concat("\n");
	};

	line(String("================================================================"));
	line(String(" RAPPORT D'EXÉCUTION — game_editor_demo"));
	line(String("================================================================"));
	line(String::Format("Commande        : %s", commandLine.CStr()));
	line(String::Format("Mode            : %s", mode.CStr()));
	line(String::Format("Thème           : %s", theme.IsEmpty() ? "(défaut)" : theme.CStr()));
	line(String::Format("Scénario        : %s", scenario.IsEmpty() ? "(aucun)" : scenario.CStr()));
	line(String::Format("Graine          : %llu", static_cast<unsigned long long>(randomSeed)));
	line(String::Format("Durée totale    : %.2f s", wallClockSeconds));
	line(String::Format("Issue           : %s", completed ? "terminé normalement" : failure.CStr()));
	line(String());

	line(String("--- Projet -----------------------------------------------------"));
	line(String::Format("Nom             : %s", project.name.CStr()));
	line(String::Format("Scènes          : %d", int(project.scenes.size())));
	for (const SceneDesc &scene : project.scenes) {
		bool active = project.ActiveScene() && project.ActiveScene()->name == scene.name;
		line(String::Format("  %s %-24s %4d objets  %s", active ? "*" : " ", scene.name.CStr(),
							int(scene.NodeCount()), scene.description.CStr()));
	}
	line(String::Format("Objets (total)  : %d", int(project.TotalNodeCount())));
	line(String());

	if (const SceneDesc *scene = project.ActiveScene()) {
		line(String::Format("--- Objets de la scène active « %s » ---", scene->name.CStr()));
		line(String::Format("%-26s %-12s %-9s %-22s %s", "NOM", "FORME", "CORPS", "POSITION MONDE", "MATÉRIAU"));
		// Parcours de l'ARBRE : les noms sont indentés selon leur
		// profondeur, et la position rapportée est la position MONDE —
		// c'est celle qu'on lit dans le viewport, alors que le transform
		// d'un nœud enfant est relatif à son parent.
		scene->tree.Traverse(scene->tree.Root(), [&](scene::NodeId id, const scene::Node &object) {
			if (id == scene->tree.Root())
				return;
			const size_t depth = scene->tree.DepthOf(id) - 1;
			String indent;
			for (size_t i = 0; i < depth; ++i)
				indent.Concat("  ");
			const VisualDesc visual = VisualDesc::Read(object);
			const PhysicsDesc physics = PhysicsDesc::Read(object);
			const math::FVector3 p = scene->tree.GlobalPosition(id);
			line(String::Format("%-26s %-12s %-9s %-22s %s",
								String::Format("%s%s", indent.CStr(), object.name.CStr()).CStr(),
								VisualDesc::Has(object) ? ShapeKindName(visual.shape) : "(groupe)",
								BodyKindName(physics.body),
								String::Format("%.2f %.2f %.2f", double(p.x), double(p.y), double(p.z)).CStr(),
								VisualDesc::Has(object) ? MaterialKindName(visual.material.kind) : "—"));
		});
		line(String());
	}

	line(String("--- Performance ------------------------------------------------"));
	line(String::Format("Images rendues  : %ld", frames.Count()));
	line(String::Format("Première image  : %.1f ms (exclue des extrêmes)", frames.FirstFrameSeconds() * 1000.0));
	line(String::Format("Cadence min     : %.1f img/s", frames.MinFps()));
	line(String::Format("Cadence max     : %.1f img/s", frames.MaxFps()));
	line(String::Format("Cadence moyenne : %.1f img/s", frames.AverageFps()));
	line(String::Format("Durée d'image   : médiane %.2f ms · p95 %.2f ms · p99 %.2f ms",
						frames.PercentileMs(50.0), frames.PercentileMs(95.0), frames.PercentileMs(99.0)));
	line(String::Format("Fils simultanés : %d (maximum) · %d démarrés au total", peakThreads, totalThreads));
	line(String::Format("Vivier de tâches: %d fil(s) · %d tâche(s) exécutées en parallèle au maximum",
						jobWorkers, jobPeakConcurrent));
	if (phases.Frames() > 0) {
		line(String());
		line(String::Format("Ventilation d'une image (moyenne sur %ld images) :", phases.Frames()));
		for (int phase = 0; phase < PhaseStats::COUNT; ++phase) {
			double average = phases.AverageMs(phase);
			if (average < 0.001)
				continue;
			double share = phases.TotalAverageMs() > 0.0 ? average * 100.0 / phases.TotalAverageMs() : 0.0;
			line(String::Format("  %-14s %7.2f ms  (%4.1f %%)  pic %7.2f ms", PhaseStats::Name(phase), average,
								share, phases.PeakMs(phase)));
		}
		line(String::Format("  %-14s %7.2f ms", "TOTAL", phases.TotalAverageMs()));
	}
	line(String());

	line(String("--- Scripts ----------------------------------------------------"));
	line(String::Format("Exécutions      : %d", int(scriptRuns)));
	line(String::Format("Appels de rappel: %d", int(scriptCalls)));
	line(String::Format("Erreurs         : %d", int(scriptErrors)));
	for (const String &script : scriptsExecuted)
		line(String::Format("  · %s", script.CStr()));
	if (!gameplayCounters.empty()) {
		line(String("Compteurs de jeu :"));
		for (const auto &counter : gameplayCounters)
			line(String::Format("  %-18s %s", counter.first.CStr(), counter.second.CStr()));
	}
	line(String());

	line(String("--- Sessions de jeu --------------------------------------------"));
	if (playSessions.empty()) {
		line(String("  (aucune)"));
	} else {
		for (const PlaySessionRecord &session : playSessions)
			line(String::Format("  %-24s images %ld → %ld (%.2f s)", session.scene.CStr(), session.startFrame,
								session.endFrame, session.duration));
	}
	line(String());

	line(String("--- Captures d'écran -------------------------------------------"));
	if (screenshots.empty()) {
		line(String("  (aucune)"));
	} else {
		for (const ScreenshotRecord &shot : screenshots)
			line(String::Format("  [%s] image %-6ld %s%s", shot.written ? "ok " : "ÉCHEC", shot.frame,
								shot.path.CStr(),
								shot.error.IsEmpty() ? "" : String::Format(" (%s)", shot.error.CStr()).CStr()));
	}
	line(String());

	if (!warnings.empty() || !errors.empty()) {
		line(String("--- Avertissements et erreurs ----------------------------------"));
		for (const String &warning : warnings)
			line(String::Format("  [warn] %s", warning.CStr()));
		for (const String &error : errors)
			line(String::Format("  [err ] %s", error.CStr()));
		line(String());
	}

	return out;
}

String RunReport::ToJson(const Project &project) const {
	auto root = data::Node::MakeObject();
	root->Set("format", data::Node::MakeString("game_editor.report"));
	root->Set("version", data::Node::MakeInt(1));

	auto run = data::Node::MakeObject();
	run->Set("command_line", data::Node::MakeString(commandLine));
	run->Set("mode", data::Node::MakeString(mode));
	run->Set("theme", data::Node::MakeString(theme));
	run->Set("scenario", data::Node::MakeString(scenario));
	run->Set("random_seed", data::Node::MakeInt(int64_t(randomSeed)));
	run->Set("wall_clock_seconds", data::Node::MakeFloat(wallClockSeconds));
	run->Set("completed", data::Node::MakeBool(completed));
	run->Set("failure", data::Node::MakeString(failure));
	root->Set("run", run);

	auto projectNode = data::Node::MakeObject();
	projectNode->Set("name", data::Node::MakeString(project.name));
	projectNode->Set("scene_count", data::Node::MakeInt(int64_t(project.scenes.size())));
	projectNode->Set("object_count", data::Node::MakeInt(int64_t(project.TotalNodeCount())));
	projectNode->Set("active_scene",
					 data::Node::MakeString(project.ActiveScene() ? project.ActiveScene()->name : String()));

	auto scenesNode = data::Node::MakeArray();
	for (const SceneDesc &scene : project.scenes) {
		auto sceneNode = data::Node::MakeObject();
		sceneNode->Set("name", data::Node::MakeString(scene.name));
		sceneNode->Set("description", data::Node::MakeString(scene.description));
		sceneNode->Set("object_count", data::Node::MakeInt(int64_t(scene.NodeCount())));
		sceneNode->Set("has_gameplay_script", data::Node::MakeBool(!scene.gameplayScript.IsEmpty()));

		auto objectsNode = data::Node::MakeArray();
		scene.tree.Traverse(scene.tree.Root(), [&](scene::NodeId id, const scene::Node &object) {
			if (id == scene.tree.Root())
				return;
			const VisualDesc visual = VisualDesc::Read(object);
			const PhysicsDesc physics = PhysicsDesc::Read(object);
			auto objectNode = data::Node::MakeObject();
			objectNode->Set("name", data::Node::MakeString(object.name));
			objectNode->Set("type", data::Node::MakeString(object.type));
			// Le CHEMIN plutôt qu'un simple nom de parent : c'est ce qui
			// permet de relire la hiérarchie depuis le rapport.
			objectNode->Set("path", data::Node::MakeString(scene.tree.PathOf(id)));
			objectNode->Set("shape", data::Node::MakeString(ShapeKindName(visual.shape)));
			objectNode->Set("tag", data::Node::MakeString(TagOf(object)));
			objectNode->Set("position", json::FromVec3(object.transform.position));
			objectNode->Set("world_position", json::FromVec3(scene.tree.GlobalPosition(id)));
			objectNode->Set("rotation", json::FromVec3(object.transform.EulerDegrees()));
			objectNode->Set("scale", json::FromVec3(object.transform.scale));
			objectNode->Set("material", data::Node::MakeString(MaterialKindName(visual.material.kind)));
			objectNode->Set("body", data::Node::MakeString(BodyKindName(physics.body)));
			objectNode->Set("mass", data::Node::MakeFloat(double(physics.mass)));
			objectsNode->Push(objectNode);
		});
		sceneNode->Set("objects", objectsNode);
		scenesNode->Push(sceneNode);
	}
	projectNode->Set("scenes", scenesNode);
	root->Set("project", projectNode);

	auto performance = data::Node::MakeObject();
	performance->Set("frames", data::Node::MakeInt(frames.Count()));
	performance->Set("first_frame_ms", data::Node::MakeFloat(frames.FirstFrameSeconds() * 1000.0));
	performance->Set("fps_min", data::Node::MakeFloat(frames.MinFps()));
	performance->Set("fps_max", data::Node::MakeFloat(frames.MaxFps()));
	performance->Set("fps_average", data::Node::MakeFloat(frames.AverageFps()));
	performance->Set("frame_ms_p50", data::Node::MakeFloat(frames.PercentileMs(50.0)));
	performance->Set("frame_ms_p95", data::Node::MakeFloat(frames.PercentileMs(95.0)));
	performance->Set("frame_ms_p99", data::Node::MakeFloat(frames.PercentileMs(99.0)));
	performance->Set("threads_peak_concurrent", data::Node::MakeInt(peakThreads));
	performance->Set("threads_total_started", data::Node::MakeInt(totalThreads));
	performance->Set("job_workers", data::Node::MakeInt(jobWorkers));
	performance->Set("job_peak_concurrent", data::Node::MakeInt(jobPeakConcurrent));

	auto phaseNode = data::Node::MakeObject();
	for (int phase = 0; phase < PhaseStats::COUNT; ++phase) {
		auto entry = data::Node::MakeObject();
		entry->Set("average_ms", data::Node::MakeFloat(phases.AverageMs(phase)));
		entry->Set("peak_ms", data::Node::MakeFloat(phases.PeakMs(phase)));
		phaseNode->Set(String(PhaseStats::Name(phase)), entry);
	}
	performance->Set("phases", phaseNode);
	root->Set("performance", performance);

	auto scripts = data::Node::MakeObject();
	scripts->Set("runs", data::Node::MakeInt(int64_t(scriptRuns)));
	scripts->Set("hook_calls", data::Node::MakeInt(int64_t(scriptCalls)));
	scripts->Set("errors", data::Node::MakeInt(int64_t(scriptErrors)));
	auto executed = data::Node::MakeArray();
	for (const String &script : scriptsExecuted)
		executed->Push(data::Node::MakeString(script));
	scripts->Set("executed", executed);
	auto counters = data::Node::MakeObject();
	for (const auto &counter : gameplayCounters)
		counters->Set(counter.first, data::Node::MakeString(counter.second));
	scripts->Set("gameplay_counters", counters);
	root->Set("scripts", scripts);

	auto sessions = data::Node::MakeArray();
	for (const PlaySessionRecord &session : playSessions) {
		auto sessionNode = data::Node::MakeObject();
		sessionNode->Set("scene", data::Node::MakeString(session.scene));
		sessionNode->Set("start_frame", data::Node::MakeInt(session.startFrame));
		sessionNode->Set("end_frame", data::Node::MakeInt(session.endFrame));
		sessionNode->Set("duration_seconds", data::Node::MakeFloat(session.duration));
		sessions->Push(sessionNode);
	}
	root->Set("play_sessions", sessions);

	auto shots = data::Node::MakeArray();
	for (const ScreenshotRecord &shot : screenshots) {
		auto shotNode = data::Node::MakeObject();
		shotNode->Set("frame", data::Node::MakeInt(shot.frame));
		shotNode->Set("path", data::Node::MakeString(shot.path));
		shotNode->Set("written", data::Node::MakeBool(shot.written));
		shotNode->Set("error", data::Node::MakeString(shot.error));
		shots->Push(shotNode);
	}
	root->Set("screenshots", shots);

	auto diagnostics = data::Node::MakeObject();
	auto warningsNode = data::Node::MakeArray();
	for (const String &warning : warnings)
		warningsNode->Push(data::Node::MakeString(warning));
	auto errorsNode = data::Node::MakeArray();
	for (const String &error : errors)
		errorsNode->Push(data::Node::MakeString(error));
	diagnostics->Set("warnings", warningsNode);
	diagnostics->Set("errors", errorsNode);
	root->Set("diagnostics", diagnostics);

	data::JsonDocument document;
	document.SetRoot(root);
	return document.EncodeStr();
}

String RunReport::Render(const Project &project, ReportFormat format) const {
	return format == ReportFormat::JSON ? ToJson(project) : ToText(project);
}

Result<bool, String> RunReport::Write(const Project &project, const String &path, ReportFormat format) const {
	String text = Render(project, format);
	if (!sdl3::WriteFile(path, text.CStr(), text.GetSize()))
		return Err(String::Format("écriture du rapport impossible : %s", path.CStr()));
	return Ok(true);
}

void RunReport::CollectDiagnostics(const Runtime &runtime) {
	for (const LogEntry &entry : runtime.Log()) {
		if (entry.level == LogLevel::WARNING)
			warnings.push_back(String::Format("image %ld : %s", entry.frame, entry.text.CStr()));
		else if (entry.level == LogLevel::ERROR_LEVEL)
			errors.push_back(String::Format("image %ld : %s", entry.frame, entry.text.CStr()));
	}
}

} // namespace game_editor
