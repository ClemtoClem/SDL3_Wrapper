// EditorUi — modélisation à la SketchUp dans la vue 3D (cf. panels.hpp,
// « Modélisation à la SketchUp »). La logique vit dans ModelingSession ; ce
// fichier ne fait que la brancher sur la souris, le clavier et les widgets.
#include <sandbox/editor/panels.hpp>

#include <sandbox/document/objects.hpp>
#include <sandbox/engine/overlay_ribbons.hpp>

#include <cstdlib>
#include <filesystem>

#include <modeling/io.hpp>
#include <modeling/operations.hpp>
#include <modeling/primitives.hpp>

namespace sandbox {

namespace {

/// Formes insérables : celles de render3d (converties), puis celles
/// d'ezGeometry portées dans modeling/primitives.hpp.
struct ShapeEntry {
	const char *name;
	const char *base; ///< nom de nœud / de fichier
};
constexpr ShapeEntry SHAPES[] = {
	{"Boîte", "Boite"},         {"Sphère", "Sphere"},       {"Cylindre", "Cylindre"},
	{"Cône", "Cone"},           {"Tore", "Tore"},           {"Pyramide", "Pyramide"},
	{"Demi-sphère", "Dome"},    {"Capsule", "Capsule"},     {"Escalier", "Escalier"},
	{"Escalier hélicoïdal", "Colimacon"}, {"Arche", "Arche"}, {"Rampe", "Rampe"},
};

Result<modeling::Model, String> ShapeModel(int shape, int segments) {
	using Spec = render3d::PrimitiveSpec;
	switch (shape) {
	case 0: return Ok(MeshLibrary::FromPrimitive(Spec::Box(1.f, 1.f, 1.f)));
	case 1: return Ok(MeshLibrary::FromPrimitive(Spec{Spec::Shape::SPHERE, {1.f, 1.f, 1.f}, segments}));
	case 2: return Ok(MeshLibrary::FromPrimitive(Spec{Spec::Shape::CYLINDER, {1.f, 1.f, 1.f}, segments}));
	case 3: return Ok(MeshLibrary::FromPrimitive(Spec{Spec::Shape::CONE, {1.f, 1.f, 1.f}, segments}));
	case 4: return Ok(MeshLibrary::FromPrimitive(Spec{Spec::Shape::TORUS, {1.f, 0.3f, 1.f}, segments}));
	default: break;
	}
	modeling::Model model;
	Result<std::vector<modeling::FaceId>, String> built = Ok(std::vector<modeling::FaceId>{});
	switch (shape) {
	case 5: built = modeling::AddPyramid(model, 1.f, 1.f); break;
	case 6: built = modeling::AddHalfSphere(model, 0.5f, segments); break;
	case 7: built = modeling::AddCapsule(model, 0.25f, 0.5f, segments); break;
	case 8: built = modeling::AddStairs(model, {1.f, 1.f, 1.5f}, 6); break;
	case 9: built = modeling::AddSpiralStairs(model, 0.2f, 1.f, 2.f, 12); break;
	case 10: built = modeling::AddArch(model, 2.f, 0.3f, 0.5f, segments / 2); break;
	case 11: built = modeling::AddRamp(model, {1.f, 0.5f, 1.5f}); break;
	default: return Err(String("forme inconnue"));
	}
	if (built.IsError())
		return Err(built.Error());
	return Ok(std::move(model));
}

void DrawDashed(sdl3::Renderer &ren, math::FVector2 a, math::FVector2 b) {
	const math::FVector2 d = b - a;
	const float length = d.Length();
	if (length < 1e-3f)
		return;
	constexpr float DASH = 6.f;
	for (float t = 0.f; t < length; t += 2.f * DASH) {
		const float t1 = math::Min(t + DASH, length);
		ren.DrawLine(a.x + d.x * t / length, a.y + d.y * t / length, a.x + d.x * t1 / length, a.y + d.y * t1 / length);
	}
}

} // namespace

// ── Cycle de vie ─────────────────────────────────────────────────────────────

void EditorUi::BeginModeling(scene::NodeId id) {
	if (Rt().IsPlaying()) {
		SetStatus(String("Arrêtez la partie (F5) avant de modéliser"));
		return;
	}
	// Chaque nœud d'un OBJET se modélise : un nœud vu dans une instance
	// ouvre l'objet et y désigne le nœud d'origine ; l'instance elle-même
	// ouvre l'objet et, s'il n'a qu'une forme, la prend.
	if (const scene::Node *node = Rt().FindNode(id); node && objects::IsInstance(*node) && !objects::IsGenerated(*node)) {
		const String object = objects::SourceOf(*node);
		if (!Rt().OpenObject(ObjectRef{object})) {
			SetStatus(String::Format("Objet « %s » introuvable", object.CStr()));
			return;
		}
		std::vector<scene::NodeId> shapes;
		if (const SceneDesc *document = Rt().ActiveDocument())
			for (scene::NodeId child : document->tree.AllNodes())
				if (const scene::Node *n = document->tree.Get(child); n && VisualDesc::Has(*n) && !objects::IsGenerated(*n))
					shapes.push_back(child);
		if (shapes.size() != 1) {
			SetStatus(String::Format("Objet « %s » ouvert : double-cliquez le nœud à modéliser", object.CStr()));
			return;
		}
		id = shapes.front();
	} else if (Rt().ObjectNodeOf(id).IsSome()) {
		Result<scene::NodeId, String> opened = Rt().OpenObjectNode(id);
		if (opened.IsError()) {
			SetStatus(String::Format("Modélisation impossible : %s", opened.Error().CStr()));
			return;
		}
		id = opened.Unwrap();
	}
	ResetTape();
	SetNavTool(NavTool::NONE);
	Result<bool, String> begun = m_modeling.Begin(id);
	if (begun.IsError()) {
		SetStatus(String::Format("Modélisation impossible : %s", begun.Error().CStr()));
		return;
	}
	Set2DView(false);
	(void)Rt().Select(id);
	Rt().SetGizmoSuppressed(true);
	m_inspector.MarkDirty();
	SetStatus(m_modeling.Status());
}

void EditorUi::EndModeling() {
	const bool was = m_modeling.Node().Valid();
	m_modeling.End();
	m_pickingSketchFace = false;
	m_inspector.MarkDirty();
	if (was)
		SetStatus(String("Modélisation terminée — le maillage sera écrit avec le projet (Ctrl+S)"));
}

void EditorUi::CreateModeledMesh(const String &name, modeling::Model model) {
	Option<scene::NodeId> id = Rt().CreateMeshNode(name, std::move(model));
	if (id.IsNone()) {
		SetStatus(String("Aucun document ouvert"));
		return;
	}
	(void)Rt().SetPosition(id.Unwrap(), SpawnPointInFrontOfCamera(0.f));
	BeginModeling(id.Unwrap());
}

void EditorUi::InsertModelingShape(int shape) {
	Result<modeling::Model, String> model = ShapeModel(shape, m_modeling.circleSegments);
	if (model.IsError()) {
		SetStatus(String::Format("Forme impossible : %s", model.Error().CStr()));
		return;
	}
	if (!m_modeling.IsActive()) {
		CreateModeledMesh(String(SHAPES[shape].base), std::move(model).Unwrap());
		return;
	}
	// Dans le modèle édité : posée à côté de ce qui existe déjà.
	const modeling::Model piece = std::move(model).Unwrap();
	(void)m_modeling.Apply(String::Format("%s insérée", SHAPES[shape].name), [&](modeling::Model &target) {
		float x = 0.f;
		bool empty = true;
		for (const modeling::Face &f : target.Faces())
			empty = empty && !f.alive;
		if (!empty)
			x = target.Bounds().max.x - piece.Bounds().min.x + 0.5f;
		(void)target.Append(piece, math::FMatrix4::Translate(x, 0.f, 0.f));
		return true;
	});
	SetStatus(m_modeling.Status());
}

void EditorUi::ExportModeledMesh(const char *extension) {
	String source = m_modeling.IsActive() ? m_modeling.Source() : String();
	if (source.IsEmpty())
		if (const scene::Node *node = Rt().SelectedNode(); node && VisualDesc::Has(*node)) {
			const VisualDesc visual = VisualDesc::Read(*node);
			if (visual.shape == ShapeKind::MESH)
				source = visual.source;
		}
	if (source.IsEmpty()) {
		SetStatus(String("Exporter : sélectionnez un maillage modélisé"));
		return;
	}
	Result<modeling::Model *, String> model = Rt().Meshes().Get(source);
	if (model.IsError()) {
		SetStatus(model.Error());
		return;
	}
	const String directory = Rt().ProjectDirectory().IsEmpty() ? m_savesDir : Rt().ProjectDirectory();
	const String path = files::Join(directory, String::Format("exports/%s.%s", files::Stem(source).CStr(), extension));
	std::error_code ignored;
	std::filesystem::create_directories(std::filesystem::path(path.CStr()).parent_path(), ignored);
	if (Option<String> err = modeling::ExportModel(path, *model.Value()); err.IsSome())
		SetStatus(String::Format("Export impossible : %s", err.Unwrap().CStr()));
	else
		SetStatus(String::Format("Maillage exporté : %s", path.CStr()));
}

void EditorUi::SetModelingLight(bool enabled) {
	m_modelingLight = enabled;
	Rt().SetNeutralLighting(enabled && !Rt().IsPlaying());
	SetStatus(String(enabled ? "Éclairage d'atelier : lumière du monde désactivée pendant l'édition"
							 : "Éclairage du monde rétabli"));
}

bool EditorUi::SetModelingView(const String &view) {
	struct View {
		const char *name;
		math::FVector3 forward; ///< direction du regard
	};
	// L'iso regarde la façade avant (z min) et le côté gauche (x min), en
	// plongée, comme la vue « Iso » de SketchUp.
	static const View VIEWS[] = {
		{"iso", {1.f, -0.85f, 1.f}},  {"top", {0.f, -1.f, 0.f}},   {"front", {0.f, -0.12f, 1.f}},
		{"right", {-1.f, -0.12f, 0.f}}, {"back", {0.f, -0.12f, -1.f}}, {"left", {1.f, -0.12f, 0.f}},
	};
	Option<math::FAABB> bounds = m_modeling.WorldBounds();
	if (bounds.IsNone())
		return false;
	for (const View &v : VIEWS)
		if (view == v.name) {
			const math::FAABB box = bounds.Unwrap();
			const float radius = math::Max((box.max - box.min).Length() * 0.5f, 0.5f);
			const float fov = Rt().ActiveCamera().fovYRadians;
			Rt().FrameView((box.min + box.max) * 0.5f, v.forward, radius / math::Sin(fov * 0.5f) * 1.15f);
			m_modeling.RefreshLines();
			return true;
		}
	return false;
}

// ── Évènements ───────────────────────────────────────────────────────────────

bool EditorUi::HandleModelingEvent(const sdl3::Event &event) {
	if (!m_modeling.IsActive())
		return false;
	const SDL_Keymod mods = SDL_GetModState();
	const bool ctrl = (mods & SDL_KMOD_CTRL) != 0, shift = (mods & SDL_KMOD_SHIFT) != 0;
	const sdl3::FRect rect = ViewportRect();
	m_modeling.SetViewportSize(rect.w, rect.h);

	if (event.IsMouseMotion()) {
		if (!m_rightMouseDown && !m_navOrbiting && !m_navPanning)
			m_modeling.PointerMove({event.MouseMotion().x - rect.x, event.MouseMotion().y - rect.y});
		return false; // la caméra suit toujours la souris
	}
	if (event.IsMouseDown(SDL_BUTTON_LEFT) && (mods & SDL_KMOD_ALT) == 0 && m_navTool == NavTool::NONE) {
		const float x = event.MouseButton().x, y = event.MouseButton().y;
		if (!PointerOverViewport(x, y))
			return false;
		if (m_pickingSketchFace) {
			// Esquisse « sur une face » : celle du modèle sous le curseur,
			// sinon celle d'un autre nœud (plan estimé par trois rayons).
			m_pickingSketchFace = false;
			bool picked = m_modeling.PickSketchFace({x - rect.x, y - rect.y});
			if (!picked) {
				Option<Runtime::PickHit> hits[3];
				const float offsets[3][2] = {{0.f, 0.f}, {3.f, 0.f}, {0.f, 3.f}};
				for (int i = 0; i < 3; ++i)
					if (Option<math::FRay> ray = RayAt(x + offsets[i][0], y + offsets[i][1]); ray.IsSome())
						hits[i] = Rt().PickAt(ray.Unwrap());
				if (hits[0].IsSome() && hits[1].IsSome() && hits[2].IsSome() && hits[0].Value().node == hits[1].Value().node &&
					hits[0].Value().node == hits[2].Value().node) {
					const math::FVector3 p = hits[0].Value().point;
					math::FVector3 n = (hits[1].Value().point - p).Cross(hits[2].Value().point - p);
					if (n.Length() > 1e-9f) {
						n = n.Normalize();
						if (n.Dot(Rt().ActiveCamera().position - p) < 0.f)
							n = n * -1.f;
						picked = m_modeling.SetSketchPlaneWorld(p, n);
					}
				}
			}
			if (picked)
				ViewSketchPlane();
			SetStatus(picked ? m_modeling.Status() : String("Esquisse : aucune face sous le curseur"));
			return true;
		}
		// Double-clic à côté du modèle, sur un autre nœud : on passe à lui
		// (pratique pour modéliser un à un les nœuds d'un objet).
		if (event.MouseButton().clicks >= 2 && m_modeling.CurrentTool() == modeling::ToolKind::SELECT &&
			!m_modeling.Hovered().IsValid())
			if (Option<math::FRay> ray = RayAt(x, y); ray.IsSome())
				if (Option<Runtime::PickHit> hit = Rt().PickAt(ray.Unwrap());
					hit.IsSome() && hit.Value().node != m_modeling.Node()) {
					BeginModeling(hit.Value().node);
					return true;
				}
		(void)m_modeling.Click({x - rect.x, y - rect.y}, shift, ctrl);
		SetStatus(m_modeling.Status());
		return true;
	}
	if (!event.IsKeyDown())
		return false;
	const SDL_Keycode key = event.Key().key;
	if (ctrl) {
		if (key == SDLK_Z)
			(void)(shift ? m_modeling.Redo() : m_modeling.Undo());
		else if (key == SDLK_Y)
			(void)m_modeling.Redo();
		else
			return false; // Ctrl+S & co : à l'éditeur
		SetStatus(m_modeling.Status());
		return true;
	}
	if (m_rightMouseDown)
		return false; // vol de la caméra (ZQSD)
	// Navigation (hors saisie d'une cote) : O orbite, H panoramique, Maj+Z.
	if (m_modeling.Typed().IsEmpty()) {
		if (key == SDLK_Z && shift) {
			ZoomExtents();
			return true;
		}
		if (key == SDLK_O || key == SDLK_H) {
			SetNavTool(key == SDLK_O ? NavTool::ORBIT : NavTool::PAN);
			return true;
		}
	}
	if (key == SDLK_ESCAPE && (m_navTool != NavTool::NONE || m_pickingSketchFace)) {
		SetNavTool(NavTool::NONE);
		m_pickingSketchFace = false;
		return true;
	}
	switch (key) {
	case SDLK_ESCAPE:
		if (!m_modeling.Escape())
			EndModeling();
		return true;
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		(void)m_modeling.Commit();
		SetStatus(m_modeling.Status());
		return true;
	case SDLK_BACKSPACE: return m_modeling.Backspace();
	case SDLK_DELETE:
		(void)m_modeling.DeleteSelection(); // jamais le nœud lui-même
		return true;
	case SDLK_KP_PERIOD: return m_modeling.TypeChar('.');
	case SDLK_KP_MINUS: return m_modeling.TypeChar('-');
	default: break;
	}
	if (key >= SDLK_KP_1 && key <= SDLK_KP_9)
		return m_modeling.TypeChar(char('1' + (key - SDLK_KP_1)));
	if (key == SDLK_KP_0)
		return m_modeling.TypeChar('0');
	if (key >= 32 && key < 127) {
		const modeling::ToolKind before = m_modeling.CurrentTool();
		const bool used = m_modeling.TypeChar(char(key));
		if (used && (m_modeling.CurrentTool() != before || key == SDLK_SPACE))
			m_navTool = NavTool::NONE;
		return used;
	}
	return false;
}

// ── Pilotage par script (editor.ui("model…", …)) ────────────────────────────

bool EditorUi::ModelingCommand(const String &command, const String &argument) {
	if (command == "model") {
		const scene::NodeId id = argument.IsEmpty() ? Rt().SelectedId() : Rt().ResolveId(argument);
		BeginModeling(id);
		return m_modeling.IsActive();
	}
	if (command == "model_insert") {
		for (int i = 0; i < int(std::size(SHAPES)); ++i)
			if (argument == SHAPES[i].name || argument == SHAPES[i].base) {
				InsertModelingShape(i);
				return true;
			}
		return false;
	}
	if (command == "model_export") {
		ExportModeledMesh(argument.IsEmpty() ? "obj" : argument.CStr());
		return true;
	}
	if (!m_modeling.IsActive())
		return false;
	const sdl3::FRect rect = ViewportRect();
	m_modeling.SetViewportSize(rect.w, rect.h);
	if (command == "model_end") {
		EndModeling();
		return true;
	}
	if (command == "model_tool") {
		for (modeling::ToolKind kind : modeling::ALL_TOOLS)
			if (argument == modeling::ToolName(kind) ||
				(argument.GetSize() == 1 && argument.CharAt(0) == modeling::ToolShortcut(kind))) {
				m_modeling.SetTool(kind);
				return true;
			}
		return false;
	}
	if (command == "model_at" || command == "model_hover") {
		float v[3] = {0.f, 0.f, 0.f};
		const char *text = argument.CStr();
		for (float &value : v) {
			char *end = nullptr;
			value = std::strtof(text, &end);
			if (end == text)
				return false;
			text = end;
			while (*text == ';' || *text == ' ')
				++text;
		}
		Option<math::FVector2> px = m_modeling.ScreenOf({v[0], v[1], v[2]});
		if (px.IsNone())
			return false;
		m_modeling.PointerMove(px.Unwrap());
		if (command == "model_at")
			(void)m_modeling.Click(px.Unwrap());
		SetStatus(m_modeling.Status());
		return true;
	}
	if (command == "model_type") {
		for (size_t i = 0; i < argument.GetSize(); ++i)
			(void)m_modeling.TypeChar(argument.CharAt(i));
		const bool done = m_modeling.Commit();
		SetStatus(m_modeling.Status());
		return done;
	}
	if (command == "model_light") {
		SetModelingLight(argument != "off");
		return true;
	}
	if (command == "model_view")
		return SetModelingView(argument.IsEmpty() ? String("iso") : argument);
	if (command == "model_undo")
		return m_modeling.Undo();
	if (command == "model_redo")
		return m_modeling.Redo();
	return false;
}

// ── Widgets ──────────────────────────────────────────────────────────────────

void EditorUi::BuildModelingMenu(ui::Widget bar) {
	ui::Widget menu = bar.Append(ui::Menu(String("Modélisation")).Children({MenuAction("Modéliser la sélection", "", [this] { BeginModeling(Rt().SelectedId()); }),
MenuAction("Nouveau maillage modélisé", "", [this] { CreateModeledMesh(String("Maillage"), modeling::Model{}); }),
MenuAction("Convertir en maillage modélisé", "", [this] {
			Result<String, String> done = Rt().ConvertToMesh(Rt().SelectedId());
			SetStatus(done.IsOk() ? String::Format("Converti : %s", done.Value().CStr())
								  : String::Format("Conversion impossible : %s", done.Error().CStr()));
		}),
MenuAction("Adoucir les arêtes (30°)", "", [this] {
			if (!m_modeling.IsActive()) {
				SetStatus(String("Adoucir : modélisez d'abord un maillage"));
				return;
			}
			size_t softened = 0;
			(void)m_modeling.Apply(String("Arêtes adoucies"), [&](modeling::Model &model) {
				softened = modeling::SoftenEdges(model, 30.f * math::PI / 180.f, m_modeling.Selection().faces);
				return softened > 0;
			});
			SetStatus(String::Format("%d arête(s) adoucie(s)", int(softened)));
		}),
MenuAction("Éclairage d'atelier (bascule)", "", [this] { SetModelingLight(!m_modelingLight); }),
MenuAction("Terminer la modélisation", "Échap", [this] { EndModeling(); })}));
	m_menuPopups.push_back(menu);
	ui::Widget insert = menu.Append(ui::SubMenu(String("Insérer une forme")));
	m_menuPopups.push_back(insert);
	for (int i = 0; i < int(std::size(SHAPES)); ++i)
		AddItem(insert, SHAPES[i].name, "", [this, i] { InsertModelingShape(i); });
	ui::Widget views = menu.Append(ui::SubMenu(String("Vues standard")));
	m_menuPopups.push_back(views);
	for (const auto &[label, key] : {std::pair{"Iso", "iso"}, std::pair{"Dessus", "top"}, std::pair{"Face", "front"},
									 std::pair{"Droite", "right"}, std::pair{"Arrière", "back"},
									 std::pair{"Gauche", "left"}})
		AddItem(views, label, "", [this, view = String(key)] {
			if (!SetModelingView(view))
				SetStatus(String("Vues standard : modélisez d'abord un maillage non vide"));
		});
	ui::Widget exportMenu = menu.Append(ui::SubMenu(String("Exporter le maillage")));
	m_menuPopups.push_back(exportMenu);
	AddItem(exportMenu, "Wavefront OBJ (+ MTL)", "", [this] { ExportModeledMesh("obj"); });
	AddItem(exportMenu, "STL binaire", "", [this] { ExportModeledMesh("stl"); });
	AddItem(exportMenu, "PLY", "", [this] { ExportModeledMesh("ply"); });
}

void EditorUi::BuildModelingOverlay(ui::Widget viewport) {
	ui::WidgetBuilder layer =
		ui::Canvas([this](ui::Painter &painter, const sdl3::FRect &r) {
		if (painter.Renderer())
			DrawModelingLayer(*painter.Renderer(), r);
	});
	layer.Absolute().Anchor(ui::Anchor::TopLeft).Width(ui::Length::Pct(100.f)).Height(ui::Length::Pct(100.f));
	layer.PointerThrough();
	viewport.Append(layer);

	// Consigne de l'outil et mesure (la « boîte de mesures »), en bas à gauche.
	ui::WidgetBuilder info = ui::Column();
	info.Absolute().Anchor(ui::Anchor::BottomLeft).Offset(10.f, -10.f).Gap(2.f);
	info.Padding(math::Sides{10.f, 6.f}).BorderRadius(6.f).Background(sdl3::FColor{0.08f, 0.08f, 0.1f, 0.82f});
	info.PointerThrough();
	m_modelInfo = viewport.Append(info);
	ui::WidgetBuilder hint = ui::Label(String(""));
	hint.FontSize(12.f).Color(sdl3::FColor{1.f, 1.f, 1.f, 0.8f}).PointerThrough();
	m_modelHint = m_modelInfo.Append(hint);
	ui::WidgetBuilder measure = ui::Label(String(""));
	measure.FontSize(14.f).Bold().Color(sdl3::FColor::WHITE()).PointerThrough();
	m_modelMeasure = m_modelInfo.Append(measure);

	m_shownModeling = true; // force la mise à jour (masquage) à l'image suivante
	m_shownModelTool = -1;
	m_shownModelingLight = -1;
	m_shownModelHint = m_shownModelMeasure = String();
}

void EditorUi::RefreshModelingUi() {
	if (!m_editorBuilt)
		return;
	// Le nœud a disparu (suppression, autre document) ou la partie démarre.
	if (m_modeling.Node().Valid() && (!m_modeling.IsActive() || Rt().IsPlaying()))
		EndModeling();
	RefreshViewportTools();
	RefreshEditorOverlays();
	const bool active = m_modeling.IsActive();
	const bool taping = !active && m_sceneTool == SceneTool::TAPE && !Rt().IsPlaying();
	const bool infoShown = active || taping;
	if (infoShown != m_shownModeling) {
		m_shownModeling = infoShown;
		kit::SetHidden(m_ctx, m_modelInfo, !infoShown);
	}
	String hintText, measureText;
	if (taping) {
		hintText = String(!m_tapeA.IsSome() || m_tapeB.IsSome() ? "Mètre : cliquez un premier nœud"
																  : "Mètre : cliquez le second nœud (Échap pour recommencer)");
		measureText = String::Format("Mètre  ›  %s", m_tapeText.IsEmpty() ? "distance entre deux nœuds" : m_tapeText.CStr());
	} else if (active) {
		const sdl3::FRect rect = ViewportRect();
		m_modeling.SetViewportSize(rect.w, rect.h);
		const ModelingSession::Overlay overlay = m_modeling.BuildOverlay();
		hintText = m_pickingSketchFace ? String("Esquisse : cliquez la face sur laquelle dessiner")
									   : overlay.hint.IsEmpty() ? m_modeling.Status() : overlay.hint;
		if (!overlay.label.IsEmpty() && !m_pickingSketchFace)
			hintText = String::Format("%s  ·  %s", overlay.label.CStr(), hintText.CStr());
		measureText = String::Format("%s%s  ›  %s", m_modeling.IsSketching() ? "Esquisse · " : "",
									 modeling::ToolName(m_modeling.CurrentTool()),
									 overlay.measure.IsEmpty() ? "tapez une cote puis Entrée" : overlay.measure.CStr());
		// Les arêtes sont avancées vers la caméra : à republier quand elle bouge.
		m_modeling.RefreshLines();
	} else {
		return;
	}
	if (hintText != m_shownModelHint) {
		m_shownModelHint = hintText;
		kit::SetLabelText(m_ctx, m_modelHint, hintText);
	}
	if (measureText != m_shownModelMeasure) {
		m_shownModelMeasure = measureText;
		kit::SetLabelText(m_ctx, m_modelMeasure, measureText);
	}
}

void EditorUi::DrawModelingLayer(sdl3::Renderer &ren, sdl3::FRect r) {
	if (!m_modeling.IsActive() || Rt().IsPlaying() || r.w < 2.f || r.h < 2.f)
		return;
	m_modeling.SetViewportSize(r.w, r.h);
	const ModelingSession::Overlay overlay = m_modeling.BuildOverlay();
	(void)ren.SetBlendMode(sdl3::BlendMode::BLEND);
	auto at = [&](math::FVector2 p) { return sdl3::FPoint{r.x + p.x, r.y + p.y}; };
	if (overlay.polygon.size() >= 3) {
		std::vector<sdl3::FPoint> points;
		for (math::FVector2 p : overlay.polygon)
			points.push_back(at(p));
		ren.SetDrawColor(sdl3::FColor{0.25f, 0.5f, 1.f, 0.18f});
		(void)ren.FillPolygon(points);
		points.push_back(points.front());
		ren.SetDrawColor(sdl3::FColor{0.25f, 0.5f, 1.f, 0.9f});
		(void)ren.DrawLines(points);
	}
	for (const ModelingSession::Overlay::Segment &s : overlay.segments) {
		ren.SetDrawColor(kit::FromColor(s.color));
		const sdl3::FPoint a = at(s.a), b = at(s.b);
		if (s.dashed)
			DrawDashed(ren, {a.x, a.y}, {b.x, b.y});
		else {
			(void)ren.DrawLine(a, b);
			(void)ren.DrawLine(a.x + 1.f, a.y, b.x + 1.f, b.y); // trait de 2 px
		}
	}
	if (overlay.marker.IsSome()) {
		const sdl3::FPoint c = at(overlay.marker.Unwrap());
		ren.SetDrawColor(sdl3::FColor{0.f, 0.f, 0.f, 0.85f});
		(void)ren.FillCircle(c, 6.5f);
		ren.SetDrawColor(kit::FromColor(overlay.markerColor));
		(void)ren.FillCircle(c, 5.f);
	}
}

} // namespace sandbox
