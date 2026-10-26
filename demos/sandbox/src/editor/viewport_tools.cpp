// EditorUi — barre d'outils de la vue 3D (cf. panels.hpp, « Barre d'outils de
// la vue ») : un seul jeu de boutons dont la visibilité et l'effet suivent
// la fonctionnalité active (scène/objet ou modélisation), le mètre de scène,
// la surbrillance de la sélection, l'esquisse sur un plan et la navigation.
#include <sandbox/editor/panels.hpp>

#include <modeling/tools.hpp>
#include <sandbox/engine/overlay_ribbons.hpp>

namespace sandbox {

namespace {

const sdl3::FColor CLEAR{0.f, 0.f, 0.f, 0.f};
const sdl3::Color SELECTION_COLOR{60, 140, 255, 255};
const sdl3::Color SELECTION_OUTLINE{10, 20, 45, 255};
const sdl3::Color TAPE_COLOR{255, 196, 40, 255};

/// Clés de surbrillance (cf. RefreshViewportTools) hors outils de modeling::.
constexpr float ICON = 24.f; ///< taille des boutons de la barre
constexpr int KEY_ORBIT = 100, KEY_PAN = 101, KEY_SELECT = 200, KEY_GIZMO = 210, KEY_TAPE = 220;

/// Position MONDE de l'origine d'un nœud.
Option<math::FVector3> NodeOrigin(Runtime &rt, scene::NodeId id) {
	if (const render3d::Object3D *object = rt.FindObject3D(id))
		return Some(object->WorldMatrix().TransformPoint({0.f, 0.f, 0.f}));
	return NONE;
}

} // namespace

// ── Construction ─────────────────────────────────────────────────────────────

void EditorUi::BuildViewportTools(ui::Widget bar) {
	m_modelToolButtons.clear();
	m_modelOnlyButtons.clear();
	m_sceneOnlyButtons.clear();
	auto separator = [&](std::vector<ui::Widget> *group) {
		ui::WidgetBuilder line = ui::Row();
		line.Padding(0.f);
		line.Size(1.f, 20.f).Background(kit::PaletteOf(m_ctx).separator).Margin(math::Sides{4.f, 0.f});
		ui::Widget entity = bar.Append(line);
		if (group)
			group->push_back(entity);
	};

	// Espace de travail 2D / 3D. En modélisation, « 2D » ouvre une esquisse
	// (plan d'axes ou face d'un nœud) et « 3D » la referme.
	for (int i = 0; i < 2; ++i) {
		ui::WidgetBuilder button = ui::Button(String(i == 0 ? "2D" : "3D"));
		button.Size(34.f, 24.f).FontSize(12.f).Bold().BorderRadius(4.f);
		button.Tooltip(String(i == 0 ? "Espace de travail 2D (en modélisation : esquisse sur un plan ou une face)"
									 : "Espace de travail 3D (en modélisation : fin de l'esquisse)"));
		button.OnClick([this, i] {
			if (m_modeling.IsActive())
				(void)(i == 0 ? OpenSketchMenu() : EndSketch());
			else
				Set2DView(i == 0);
		});
		m_workspaceButtons[i] = bar.Append(button);
	}
	separator(nullptr);

	// Outils communs : sélection, déplacer / tourner / redimensionner.
	m_selectButton = kit::IconButton(m_ctx, bar, ui::MaterialIcons::NEAR_ME, String("Sélection (Espace)"), [this] {
		if (m_modeling.IsActive()) {
			m_modeling.SetTool(modeling::ToolKind::SELECT);
			m_navTool = NavTool::NONE;
		} else {
			SetSceneTool(SceneTool::SELECT);
		}
	}, ICON);
	struct Transform {
		ui::MaterialIcons icon;
		const char *tooltip;
		Runtime::GizmoMode gizmo;
		modeling::ToolKind tool;
	};
	const Transform transforms[3] = {
		{ui::MaterialIcons::OPEN_WITH, "Déplacer (W ; en modélisation : M)", Runtime::GizmoMode::TRANSLATE,
		 modeling::ToolKind::MOVE},
		{ui::MaterialIcons::ROTATE_RIGHT, "Tourner (E ; en modélisation : Q)", Runtime::GizmoMode::ROTATE,
		 modeling::ToolKind::ROTATE},
		{ui::MaterialIcons::ZOOM_OUT_MAP, "Redimensionner (R ; en modélisation : S)", Runtime::GizmoMode::SCALE,
		 modeling::ToolKind::SCALE},
	};
	for (int i = 0; i < 3; ++i) {
		const Transform t = transforms[i];
		m_gizmoButtons[i] = kit::IconButton(m_ctx, bar, t.icon, String(t.tooltip), [this, t] {
			if (m_modeling.IsActive()) {
				m_modeling.SetTool(t.tool);
				m_navTool = NavTool::NONE;
			} else {
				SetGizmoMode(t.gizmo);
			}
		}, ICON);
	}
	m_snapButton = kit::IconButton(m_ctx, bar, ui::MaterialIcons::GRID_ON, String("Magnétisme (X)"),
								   [this] { ToggleSnap(); }, ICON);
	m_sceneOnlyButtons.push_back(m_snapButton);

	// Outils de dessin et de modification propres à la modélisation.
	separator(&m_modelOnlyButtons);
	struct Drawing {
		modeling::ToolKind kind;
		ui::MaterialIcons icon;
	};
	const Drawing drawing[] = {
		{modeling::ToolKind::LINE, ui::MaterialIcons::EDIT},
		{modeling::ToolKind::RECTANGLE, ui::MaterialIcons::CROP_SQUARE},
		{modeling::ToolKind::CIRCLE, ui::MaterialIcons::RADIO_BUTTON_UNCHECKED},
		{modeling::ToolKind::POLYGON, ui::MaterialIcons::HEXAGON},
		{modeling::ToolKind::PUSH_PULL, ui::MaterialIcons::UNFOLD_MORE},
		{modeling::ToolKind::OFFSET, ui::MaterialIcons::FILTER_CENTER_FOCUS},
		{modeling::ToolKind::FOLLOW_ME, ui::MaterialIcons::ROUTE},
		{modeling::ToolKind::ERASER, ui::MaterialIcons::BACKSPACE},
		{modeling::ToolKind::PAINT, ui::MaterialIcons::FORMAT_COLOR_FILL},
	};
	for (const Drawing &d : drawing) {
		const char key = modeling::ToolShortcut(d.kind);
		const String tooltip = String::Format("%s (%c)", modeling::ToolName(d.kind), char(key - 'a' + 'A'));
		ui::Widget button = kit::IconButton(m_ctx, bar, d.icon, tooltip, [this, kind = d.kind] {
			m_modeling.SetTool(kind);
			m_navTool = NavTool::NONE;
		}, ICON);
		m_modelToolButtons.push_back({d.kind, button});
		m_modelOnlyButtons.push_back(button);
	}
	separator(nullptr);

	// Mètre (distance entre deux nœuds, ou cotes du modèle) et éclairage.
	m_tapeButton = kit::IconButton(m_ctx, bar, ui::MaterialIcons::STRAIGHTEN,
								   String("Mètre (T) : distance entre deux nœuds ; en modélisation, mesure et guides"), [this] {
		if (m_modeling.IsActive()) {
			m_modeling.SetTool(modeling::ToolKind::TAPE);
			m_navTool = NavTool::NONE;
		} else {
			SetSceneTool(SceneTool::TAPE);
		}
	}, ICON);
	m_modelLightButton = kit::IconButton(
		m_ctx, bar, ui::MaterialIcons::LIGHTBULB,
		String("Éclairage d'atelier : remplace la lumière du monde par un éclairage neutre pendant l'édition"),
		[this] { SetModelingLight(!m_modelingLight); }, ICON);
	separator(nullptr);

	// Caméra, comme la barre « Caméra » de SketchUp.
	m_orbitButton = kit::IconButton(m_ctx, bar, ui::MaterialIcons::THREED_ROTATION,
									String("Orbite (O) — aussi : bouton du milieu"),
									[this] { SetNavTool(m_navTool == NavTool::ORBIT ? NavTool::NONE : NavTool::ORBIT); }, ICON);
	m_panButton = kit::IconButton(m_ctx, bar, ui::MaterialIcons::PAN_TOOL,
								  String("Panoramique (H) — aussi : Maj + bouton du milieu"),
								  [this] { SetNavTool(m_navTool == NavTool::PAN ? NavTool::NONE : NavTool::PAN); }, ICON);
	(void)kit::IconButton(m_ctx, bar, ui::MaterialIcons::FIT_SCREEN, String("Zoom étendu (Maj+Z)"),
						  [this] { ZoomExtents(); }, ICON);

	// Scène : jouer, plein écran, cadrer. Modélisation : terminer.
	separator(&m_sceneOnlyButtons);
	m_sceneOnlyButtons.push_back(kit::IconButton(m_ctx, bar, ui::MaterialIcons::PLAY_ARROW,
												 String("Lancer / arrêter dans la vue (F5)"), [this] { Rt().TogglePlay(); },
												 26.f, &m_playIcon, kit::PaletteOf(m_ctx).ok));
	m_sceneOnlyButtons.push_back(kit::IconButton(m_ctx, bar, ui::MaterialIcons::SPORTS_ESPORTS,
												 String("Lancer en plein écran (F6)"), [this] { EnterRunMode(); }, ICON));
	m_sceneOnlyButtons.push_back(kit::IconButton(m_ctx, bar, ui::MaterialIcons::CENTER_FOCUS_STRONG,
												 String("Cadrer la sélection (F)"), [this] { OnFocusSelection(); }, ICON));
	separator(&m_modelOnlyButtons);
	m_modelOnlyButtons.push_back(kit::IconButton(m_ctx, bar, ui::MaterialIcons::CHECK,
												 String("Terminer la modélisation (Échap)"), [this] { EndModeling(); }, ICON,
												 nullptr, kit::PaletteOf(m_ctx).ok));

	// Menu des plans d'esquisse (bouton « 2D » en modélisation).
	m_sketchMenu = m_ctx.gui.Mount(ui::ContextMenu());
	m_menuPopups.push_back(m_sketchMenu);
	AddItem(m_sketchMenu, "Plan du sol (rouge / bleu)", "", [this] { BeginSketch(0); });
	AddItem(m_sketchMenu, "Plan de face (rouge / vert)", "", [this] { BeginSketch(1); });
	AddItem(m_sketchMenu, "Plan de côté (vert / bleu)", "", [this] { BeginSketch(2); });
	AddItem(m_sketchMenu, "Sur une face… (cliquez-la)", "", [this] { BeginSketch(3); });

	m_shownToolMode = m_shownToolKey = m_shownModelingLight = -1;
}

// ── Mise à jour (chaque image) ───────────────────────────────────────────────

void EditorUi::RefreshViewportTools() {
	const bool modeling = m_modeling.IsActive();
	if (int(modeling) != m_shownToolMode) {
		m_shownToolMode = int(modeling);
		for (ui::Widget e : m_modelOnlyButtons)
			kit::SetHidden(m_ctx, e, !modeling);
		for (ui::Widget e : m_sceneOnlyButtons)
			kit::SetHidden(m_ctx, e, modeling);
		m_shownToolKey = -1;
		m_shownView2d = !m_shownView2d; // force le surlignage de 2D / 3D
	}
	// Manipulateur visible (dans son mode W/E/R) pour la sélection comme
	// pour le manipulateur ; masqué pour le mètre, la navigation et la
	// modélisation. Éclairage d'atelier pendant toute l'édition.
	Rt().SetGizmoSuppressed(modeling || m_sceneTool == SceneTool::TAPE || m_navTool != NavTool::NONE);
	Rt().SetNeutralLighting(m_modelingLight && !Rt().IsPlaying());
	if (int(m_modelingLight) != m_shownModelingLight) {
		m_shownModelingLight = int(m_modelingLight);
		kit::SetBackground(m_ctx, m_modelLightButton, m_modelingLight ? kit::PaletteOf(m_ctx).selection : CLEAR);
	}

	int key = 0;
	if (m_navTool != NavTool::NONE)
		key = m_navTool == NavTool::ORBIT ? KEY_ORBIT : KEY_PAN;
	else if (modeling)
		key = int(m_modeling.CurrentTool());
	else if (m_sceneTool == SceneTool::GIZMO)
		key = KEY_GIZMO + int(Rt().GetGizmoMode());
	else
		key = m_sceneTool == SceneTool::SELECT ? KEY_SELECT : KEY_TAPE;
	if (key == m_shownToolKey)
		return;
	m_shownToolKey = key;
	const sdl3::FColor on = kit::PaletteOf(m_ctx).selection;
	auto paint = [&](ui::Widget button, bool active) { kit::SetBackground(m_ctx, button, active ? on : CLEAR); };
	using modeling::ToolKind;
	paint(m_orbitButton, key == KEY_ORBIT);
	paint(m_panButton, key == KEY_PAN);
	paint(m_selectButton, key == KEY_SELECT || key == int(ToolKind::SELECT));
	paint(m_tapeButton, key == KEY_TAPE || (modeling && key == int(ToolKind::TAPE)));
	const int transforms[3] = {int(ToolKind::MOVE), int(ToolKind::ROTATE), int(ToolKind::SCALE)};
	for (int i = 0; i < 3; ++i)
		paint(m_gizmoButtons[i], modeling ? key == transforms[i] : key == KEY_GIZMO + i);
	for (const auto &[kind, button] : m_modelToolButtons)
		paint(button, modeling && key == int(kind));
}

void EditorUi::RefreshEditorOverlays() {
	const sdl3::FRect rect = ViewportRect();
	const bool editing = !Rt().IsPlaying() && !m_view2d;
	// Sélection : une boîte bleue autour du nœud et de ses descendants
	// (l'objet entier pour une instance), comme un groupe de SketchUp.
	OverlayRibbons selection(Rt().ActiveCamera(), rect.h);
	if (editing && !m_modeling.IsActive() && Rt().SelectedId().Valid())
		if (Option<math::FAABB> box = Rt().NodeWorldBounds(Rt().SelectedId()); box.IsSome()) {
			math::FAABB b = box.Unwrap();
			const math::FVector3 pad = (b.max - b.min) * 0.01f + math::FVector3{0.01f, 0.01f, 0.01f};
			// Liseré sombre puis trait bleu : lisible sur fond clair comme
			// sombre (les deux sont dessinés par-dessus la scène).
			selection.AddBox({b.min - pad, b.max + pad}, SELECTION_OUTLINE, 4.5f);
			selection.AddBox({b.min - pad, b.max + pad}, SELECTION_COLOR, 2.f);
		}
	Rt().SetOverlay("__selection", std::move(selection.Vertices()), std::move(selection.Indices()), true);

	// Mètre de scène : premier nœud encadré, trait pointillé jusqu'au second
	// (ou jusqu'au nœud survolé).
	OverlayRibbons tape(Rt().ActiveCamera(), rect.h);
	if (editing && !m_modeling.IsActive() && m_sceneTool == SceneTool::TAPE && m_tapeA.IsSome()) {
		const math::FVector3 a = m_tapeA.Value();
		const float cross = math::Max((Rt().ActiveCamera().position - a).Length() * 0.012f, 0.02f);
		for (const math::FVector3 &axis : {math::FVector3{1, 0, 0}, math::FVector3{0, 1, 0}, math::FVector3{0, 0, 1}})
			tape.Add(a - axis * cross, a + axis * cross, TAPE_COLOR, 2.5f);
		const Option<math::FVector3> b = m_tapeB.IsSome() ? m_tapeB : m_tapeLive;
		if (b.IsSome()) {
			tape.AddDashed(a, b.Value(), TAPE_COLOR, 2.f);
			for (const math::FVector3 &axis : {math::FVector3{1, 0, 0}, math::FVector3{0, 1, 0}, math::FVector3{0, 0, 1}})
				tape.Add(b.Value() - axis * cross, b.Value() + axis * cross, TAPE_COLOR, 2.5f);
		}
		if (m_tapeFirst.Valid())
			if (Option<math::FAABB> box = Rt().NodeWorldBounds(m_tapeFirst); box.IsSome())
				tape.AddBox(box.Unwrap(), TAPE_COLOR, 1.5f);
	}
	Rt().SetOverlay("__tape", std::move(tape.Vertices()), std::move(tape.Indices()), true);
}

// ── Outils de scène ──────────────────────────────────────────────────────────

void EditorUi::SetSceneTool(SceneTool tool) {
	m_sceneTool = tool;
	m_navTool = NavTool::NONE;
	if (tool != SceneTool::TAPE)
		ResetTape();
	switch (tool) {
	case SceneTool::SELECT: SetStatus(String("Sélection : cliquez un nœud (double-clic : le modéliser)")); break;
	case SceneTool::TAPE: SetStatus(String("Mètre : cliquez deux nœuds pour mesurer leur distance")); break;
	case SceneTool::GIZMO: SetStatus(String::Format("Manipulateur : %s", GizmoModeName(Rt().GetGizmoMode()))); break;
	}
}

void EditorUi::SetNavTool(NavTool tool) {
	m_navTool = tool;
	m_navOrbiting = m_navPanning = false;
	if (tool == NavTool::ORBIT)
		SetStatus(String("Orbite : glissez avec le bouton gauche autour du point visé (Échap pour quitter)"));
	else if (tool == NavTool::PAN)
		SetStatus(String("Panoramique : glissez avec le bouton gauche (Échap pour quitter)"));
}

void EditorUi::ZoomExtents() {
	Option<math::FAABB> box = m_modeling.IsActive() ? m_modeling.WorldBounds() : Rt().SceneBounds();
	if (box.IsNone()) {
		SetStatus(String("Zoom étendu : rien à cadrer"));
		return;
	}
	Rt().FrameBounds(box.Unwrap());
	SetStatus(String("Zoom étendu"));
}

void EditorUi::ResetTape() {
	m_tapeFirst = scene::NodeId{};
	m_tapeA = m_tapeB = m_tapeLive = NONE;
	m_tapeText = String();
}

void EditorUi::TapeClick(const math::FRay &ray) {
	Option<Runtime::PickHit> hit = Rt().PickAt(ray);
	if (hit.IsNone()) {
		SetStatus(String("Mètre : cliquez un nœud"));
		return;
	}
	TapeNode(hit.Value().node);
}

void EditorUi::TapeNode(scene::NodeId id) {
	const scene::Node *node = Rt().FindNode(id);
	Option<math::FVector3> origin = NodeOrigin(Rt(), id);
	if (origin.IsNone())
		return;
	if (!m_tapeA.IsSome() || m_tapeB.IsSome()) { // premier nœud (ou nouvelle mesure)
		ResetTape();
		m_tapeFirst = id;
		m_tapeA = origin;
		SetStatus(String::Format("Mètre : depuis « %s », cliquez le second nœud", node ? node->name.CStr() : "?"));
		return;
	}
	m_tapeB = origin;
	const math::FVector3 d = m_tapeB.Value() - m_tapeA.Value();
	const scene::Node *first = Rt().FindNode(m_tapeFirst);
	m_tapeText = String::Format("« %s » ↔ « %s » : %s  (Δx %s, Δy %s, Δz %s)", first ? first->name.CStr() : "?",
								node ? node->name.CStr() : "?", modeling::FormatLength(d.Length()).CStr(),
								modeling::FormatLength(d.x).CStr(), modeling::FormatLength(d.y).CStr(),
								modeling::FormatLength(d.z).CStr());
	SetStatus(String::Format("Distance %s", m_tapeText.CStr()));
}

void EditorUi::TapeHover(const math::FRay &ray) {
	m_tapeLive = NONE;
	if (!m_tapeA.IsSome() || m_tapeB.IsSome())
		return;
	if (Option<Runtime::PickHit> hit = Rt().PickAt(ray); hit.IsSome()) {
		m_tapeLive = NodeOrigin(Rt(), hit.Value().node);
		if (m_tapeLive.IsSome())
			m_tapeText = String::Format("%s jusqu'à « %s »",
										modeling::FormatLength((m_tapeLive.Value() - m_tapeA.Value()).Length()).CStr(),
										hit.Value().name.CStr());
	}
}

// ── Esquisse ─────────────────────────────────────────────────────────────────

void EditorUi::OpenSketchMenu() {
	if (ui::Widget(m_workspaceButtons[0])) {
		const sdl3::FRect r = ui::Widget(m_workspaceButtons[0]).Bounds();
		m_sketchMenu.OpenAt(sdl3::FPoint{r.x, r.y + r.h});
	}
}

void EditorUi::BeginSketch(int plane) {
	if (!m_modeling.IsActive())
		return;
	if (plane == 3) {
		m_pickingSketchFace = true;
		SetNavTool(NavTool::NONE);
		SetStatus(String("Esquisse : cliquez la face (du modèle ou d'un autre nœud) sur laquelle dessiner"));
		return;
	}
	const modeling::Vec3 normals[3] = {{0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 0.f, 0.f}};
	m_modeling.SetSketchPlane(Some(modeling::InferenceSettings::WorkPlane{{0.f, 0.f, 0.f}, normals[plane]}));
	ViewSketchPlane();
	SetStatus(m_modeling.Status());
}

void EditorUi::EndSketch() {
	m_pickingSketchFace = false;
	if (!m_modeling.IsSketching())
		return;
	m_modeling.SetSketchPlane(NONE);
	if (!SetModelingView(String("iso")))
		ZoomExtents();
	SetStatus(m_modeling.Status());
}

void EditorUi::ViewSketchPlane() {
	Option<std::pair<math::FVector3, math::FVector3>> plane = m_modeling.SketchPlaneWorld();
	if (plane.IsNone())
		return;
	const auto [origin, normal] = plane.Unwrap();
	// Vue de face du plan, du côté où se trouve déjà la caméra.
	const math::FVector3 eye = Rt().ActiveCamera().position;
	const math::FVector3 forward = (eye - origin).Dot(normal) >= 0.f ? normal * -1.f : normal;
	math::FVector3 center = origin;
	float radius = 3.f;
	if (Option<math::FAABB> box = m_modeling.WorldBounds(); box.IsSome()) {
		const math::FVector3 c = (box.Value().min + box.Value().max) * 0.5f;
		center = c - normal * (c - origin).Dot(normal); // centre du modèle, ramené sur le plan
		radius = math::Max((box.Value().max - box.Value().min).Length() * 0.5f, 1.5f);
	}
	Rt().FrameView(center, forward, radius / math::Sin(Rt().ActiveCamera().fovYRadians * 0.5f) * 1.15f);
	m_modeling.RefreshLines();
}

math::FVector3 EditorUi::PointUnderCursor(float x, float y) {
	if (Option<math::FRay> ray = RayAt(x, y); ray.IsSome()) {
		if (Option<Runtime::PickHit> hit = Rt().PickAt(ray.Unwrap()); hit.IsSome())
			return hit.Value().point;
		const math::FRay r = ray.Unwrap();
		if (r.direction.y < -1e-3f) {
			const float t = -r.origin.y / r.direction.y;
			if (t < 500.f)
				return r.At(t);
		}
	}
	return Rt().CameraPivot();
}

} // namespace sandbox
