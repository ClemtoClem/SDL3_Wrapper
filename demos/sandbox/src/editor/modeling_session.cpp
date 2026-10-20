// Définitions de modeling_session.hpp
#include <sandbox/editor/modeling_session.hpp>

#include <algorithm>

#include <modeling/io.hpp>
#include <modeling/operations.hpp>
#include <modeling/triangulate.hpp>
#include <render3d/shape.hpp>
#include <sandbox/engine/overlay_ribbons.hpp>

#include <cmath>

namespace sandbox {

namespace {

constexpr size_t UNDO_LIMIT = 64;

sdl3::Color ToSdl(const modeling::Color &c) {
	auto byte = [](float v) { return uint8_t(math::Clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
	return sdl3::Color{byte(c.r), byte(c.g), byte(c.b), byte(c.a)};
}

const sdl3::Color EDGE_COLOR{25, 25, 30, 255};
const sdl3::Color SELECTED_COLOR{40, 110, 255, 255};
const sdl3::Color PREVIEW_COLOR{20, 20, 20, 255};
const sdl3::Color GUIDE_COLOR{90, 90, 90, 255};
const sdl3::Color SKETCH_GRID_COLOR{150, 160, 185, 255};
const sdl3::Color SKETCH_AXIS_COLOR{70, 90, 150, 255};
constexpr float GRID_STEP = 0.5f;

bool IsMeasureChar(char c) {
	return (c >= '0' && c <= '9') || c == ',' || c == '.' || c == ';' || c == '-';
}

} // namespace

ModelingSession::~ModelingSession() { End(); }

// ── Cycle de vie ─────────────────────────────────────────────────────────────

Result<bool, String> ModelingSession::Begin(scene::NodeId id) {
	End();
	Result<String, String> source = m_rt.ConvertToMesh(id);
	if (source.IsError())
		return Err(source.Error());
	if (Result<modeling::Model *, String> model = m_rt.Meshes().Get(source.Value()); model.IsError())
		return Err(model.Error());
	m_node = id;
	m_source = source.Unwrap();
	m_undo.clear();
	m_redo.clear();
	m_typed = String();
	m_status = String("Modélisation : L ligne, R rectangle, C cercle, P pousser/tirer, Échap pour quitter");
	SetTool(modeling::ToolKind::LINE);
	RefreshLines();
	return Ok(true);
}

void ModelingSession::End() {
	if (!m_node.Valid())
		return;
	m_node = scene::NodeId{};
	m_source = String();
	m_tool.reset();
	m_selection.Clear();
	m_sketch = NONE;
	m_hover = {};
	m_typed = String();
	m_rt.SetModelingLines({});
}

bool ModelingSession::IsActive() const {
	return m_node.Valid() && const_cast<Runtime &>(m_rt).FindNode(m_node) != nullptr;
}

modeling::Model *ModelingSession::Model() const {
	if (!IsActive())
		return nullptr;
	Result<modeling::Model *, String> model = m_rt.Meshes().Get(m_source);
	return model.IsOk() ? model.Value() : nullptr;
}

// ── Outils ───────────────────────────────────────────────────────────────────

void ModelingSession::SetTool(modeling::ToolKind kind) {
	m_toolKind = kind;
	m_tool = modeling::MakeTool(kind);
	m_typed = String();
	m_status = String::Format("Outil %s", modeling::ToolName(kind));
	if (modeling::Model *model = Model()) {
		modeling::ToolContext ctx = Context(*model);
		m_tool->Hover(ctx, EventAt(m_cursor, false, false));
	}
}

modeling::ToolContext ModelingSession::Context(modeling::Model &model) {
	return modeling::ToolContext{model, m_selection, material, circleSegments, polygonSides};
}

// ── Repères ──────────────────────────────────────────────────────────────────

Option<math::FMatrix4> ModelingSession::WorldMatrix() const {
	if (!IsActive())
		return NONE;
	const render3d::Object3D *object = const_cast<Runtime &>(m_rt).FindObject3D(m_node);
	if (!object)
		return NONE;
	return Some(object->WorldMatrix());
}

Option<math::FVector2> ModelingSession::Project(const math::FMatrix4 &world, const modeling::Vec3 &p) const {
	return m_rt.ActiveCamera().WorldToScreen(world.TransformPoint(p), m_width, m_height);
}

Option<math::FAABB> ModelingSession::WorldBounds() const {
	modeling::Model *model = Model();
	Option<math::FMatrix4> world = WorldMatrix();
	if (!model || world.IsNone())
		return NONE;
	const math::FAABB local = model->Bounds();
	if (!local.IsValid())
		return NONE;
	return Some(local.Transformed(world.Unwrap()));
}

Option<math::FVector2> ModelingSession::ScreenOf(const modeling::Vec3 &local) const {
	Option<math::FMatrix4> world = WorldMatrix();
	return world.IsSome() ? Project(world.Unwrap(), local) : Option<math::FVector2>(NONE);
}

void ModelingSession::SetViewportSize(float width, float height) noexcept {
	m_width = math::Max(width, 1.f);
	m_height = math::Max(height, 1.f);
}

modeling::ToolEvent ModelingSession::EventAt(math::FVector2 cursor, bool shift, bool ctrl) {
	modeling::ToolEvent event;
	event.shift = shift;
	event.ctrl = ctrl;
	modeling::Model *model = Model();
	Option<math::FMatrix4> world = WorldMatrix();
	if (!model || world.IsNone())
		return event;
	const math::FMatrix4 m = world.Unwrap();
	const math::FMatrix4 inverse = m.Inverse();
	const math::FRay worldRay = m_rt.ActiveCamera().ScreenPointToRay(cursor.x, cursor.y, m_width, m_height);
	const math::FVector3 origin = inverse.TransformPoint(worldRay.origin);
	const math::FVector3 ahead = inverse.TransformPoint(worldRay.origin + worldRay.direction);
	event.ray = math::FRay(origin, ahead - origin);

	modeling::InferenceQuery query;
	query.ray = event.ray;
	query.cursor = cursor;
	query.project = [this, m](const modeling::Vec3 &p) { return Project(m, p); };
	if (m_tool)
		query.anchor = m_tool->Anchor();
	modeling::InferenceSettings settings;
	settings.gridStep = gridStep;
	settings.workPlane = m_sketch;
	event.at = modeling::Infer(*model, query, settings);
	return event;
}

// ── Souris et clavier ────────────────────────────────────────────────────────

void ModelingSession::PointerMove(math::FVector2 cursor) {
	m_cursor = cursor;
	modeling::Model *model = Model();
	if (!model || !m_tool)
		return;
	const modeling::ToolEvent event = EventAt(cursor, false, false);
	m_hover = event.at;
	modeling::ToolContext ctx = Context(*model);
	m_tool->Hover(ctx, event);
}

bool ModelingSession::Click(math::FVector2 cursor, bool shift, bool ctrl) {
	m_cursor = cursor;
	modeling::Model *model = Model();
	if (!model || !m_tool)
		return false;
	const modeling::ToolEvent event = EventAt(cursor, shift, ctrl);
	m_hover = event.at;
	String snapshot = modeling::Serialize(*model);
	modeling::ToolContext ctx = Context(*model);
	m_tool->Hover(ctx, event);
	const modeling::ToolResult result = m_tool->Click(ctx, event);
	Report(result, std::move(snapshot));
	if (!result.changed)
		RefreshLines(); // la sélection a pu changer
	return true;
}

bool ModelingSession::TypeChar(char c) {
	if (!IsActive())
		return false;
	if (c >= 'A' && c <= 'Z')
		c = char(c - 'A' + 'a');
	const bool typing = !m_typed.IsEmpty();
	if (IsMeasureChar(c) || (typing && (c == ' ' || c == 'm' || c == 'c' || c == 'x'))) {
		m_typed += c;
		return true;
	}
	if (!typing)
		if (Option<modeling::ToolKind> kind = modeling::ToolFromShortcut(c); kind.IsSome()) {
			SetTool(kind.Unwrap());
			return true;
		}
	return false;
}

bool ModelingSession::Backspace() {
	if (m_typed.IsEmpty())
		return false;
	m_typed = m_typed.Substring(0, m_typed.GetSize() - 1);
	return true;
}

bool ModelingSession::Commit() {
	modeling::Model *model = Model();
	if (!model || !m_tool || m_typed.IsEmpty())
		return false;
	const std::vector<float> values = modeling::ParseMeasurement(m_typed);
	if (values.empty()) {
		m_status = String::Format("Cote illisible : « %s »", m_typed.CStr());
		return true;
	}
	String snapshot = modeling::Serialize(*model);
	modeling::ToolContext ctx = Context(*model);
	const modeling::ToolResult result = m_tool->Type(ctx, values);
	m_typed = String();
	Report(result, std::move(snapshot));
	return true;
}

bool ModelingSession::Escape() {
	if (m_tool && m_tool->IsActive()) {
		m_tool->Reset();
		m_status = String("Opération abandonnée");
		return true;
	}
	if (!m_typed.IsEmpty()) {
		m_typed = String();
		return true;
	}
	if (!m_selection.IsEmpty()) {
		m_selection.Clear();
		RefreshLines();
		return true;
	}
	return false;
}

bool ModelingSession::DeleteSelection() {
	if (m_selection.IsEmpty())
		return false;
	const modeling::Selection selection = m_selection;
	return Apply(String("Effacer"), [&](modeling::Model &model) {
		modeling::Erase(model, selection.edges, selection.faces);
		return true;
	});
}

bool ModelingSession::Apply(const String &label, const std::function<bool(modeling::Model &)> &op) {
	modeling::Model *model = Model();
	if (!model)
		return false;
	String snapshot = modeling::Serialize(*model);
	modeling::ToolResult result;
	result.changed = op(*model);
	result.message = label;
	Report(result, std::move(snapshot));
	return result.changed;
}

bool ModelingSession::Undo() {
	modeling::Model *model = Model();
	if (!model || m_undo.empty())
		return false;
	Result<modeling::Model, String> previous = modeling::Deserialize(m_undo.back());
	if (previous.IsError())
		return false;
	m_redo.push_back(modeling::Serialize(*model));
	m_undo.pop_back();
	m_rt.Meshes().Replace(m_source, std::move(previous).Unwrap());
	if (m_tool)
		m_tool->Reset();
	m_status = String("Annuler");
	ModelChanged();
	return true;
}

bool ModelingSession::Redo() {
	modeling::Model *model = Model();
	if (!model || m_redo.empty())
		return false;
	Result<modeling::Model, String> next = modeling::Deserialize(m_redo.back());
	if (next.IsError())
		return false;
	m_undo.push_back(modeling::Serialize(*model));
	m_redo.pop_back();
	m_rt.Meshes().Replace(m_source, std::move(next).Unwrap());
	if (m_tool)
		m_tool->Reset();
	m_status = String("Rétablir");
	ModelChanged();
	return true;
}

void ModelingSession::Report(const modeling::ToolResult &result, String undoSnapshot) {
	if (result.error.IsSome())
		m_status = result.error.Unwrap();
	else if (!result.message.IsEmpty())
		m_status = result.message;
	if (!result.changed)
		return;
	m_undo.push_back(std::move(undoSnapshot));
	if (m_undo.size() > UNDO_LIMIT)
		m_undo.erase(m_undo.begin());
	m_redo.clear();
	ModelChanged();
}

void ModelingSession::ModelChanged() {
	if (modeling::Model *model = Model())
		m_selection.Prune(*model);
	m_rt.MeshChanged(m_source);
	RefreshLines();
}

// ── Dessin ───────────────────────────────────────────────────────────────────

void ModelingSession::RefreshLines() {
	modeling::Model *model = Model();
	Option<math::FMatrix4> world = WorldMatrix();
	if (!model || world.IsNone()) {
		m_rt.SetModelingLines();
		return;
	}
	const math::FMatrix4 m = world.Unwrap();
	// Arêtes d'une face sélectionnée : surlignées comme la face.
	std::vector<modeling::EdgeId> highlighted = m_selection.edges;
	for (modeling::FaceId f : m_selection.faces)
		for (modeling::EdgeId e : model->EdgesOfFace(f))
			highlighted.push_back(e);
	std::sort(highlighted.begin(), highlighted.end());

	OverlayRibbons ribbons(m_rt.ActiveCamera(), m_height);
	auto add = [&](const modeling::Vec3 &a, const modeling::Vec3 &b, sdl3::Color color, float width) {
		ribbons.Add(m.TransformPoint(a), m.TransformPoint(b), color, width);
	};
	for (modeling::EdgeId id : modeling::VisibleEdges(*model)) {
		const modeling::Edge &edge = model->Edges()[id];
		const bool selected = std::binary_search(highlighted.begin(), highlighted.end(), id);
		add(model->Position(edge.a), model->Position(edge.b), selected ? SELECTED_COLOR : EDGE_COLOR,
			selected ? 3.f : 1.6f);
	}
	// Axes du modèle (rouge, vert, bleu), comme l'origine de SketchUp.
	for (modeling::InferenceKind kind :
		 {modeling::InferenceKind::ON_AXIS_X, modeling::InferenceKind::ON_AXIS_Y, modeling::InferenceKind::ON_AXIS_Z})
		add({0, 0, 0}, modeling::AxisDirection(kind), ToSdl(modeling::InferenceColor(kind)), 2.f);
	// Plan d'esquisse : une grille d'un demi-mètre autour de son origine.
	if (m_sketch.IsSome()) {
		const modeling::Vec3 n = m_sketch.Value().normal.Normalize();
		const modeling::Vec3 helper = std::fabs(n.y) < 0.9f ? modeling::Vec3{0.f, 1.f, 0.f} : modeling::Vec3{1.f, 0.f, 0.f};
		const modeling::Vec3 u = helper.Cross(n).Normalize(), v = n.Cross(u);
		const math::FAABB box = model->Bounds();
		const float extent = box.IsValid() ? math::Max((box.max - box.min).Length(), 4.f) : 4.f;
		const int half = int(std::ceil(extent / GRID_STEP));
		const modeling::Vec3 o = m_sketch.Value().origin;
		const float size = float(half) * GRID_STEP;
		for (int i = -half; i <= half; ++i) {
			const float t = float(i) * GRID_STEP;
			const sdl3::Color color = i == 0 ? SKETCH_AXIS_COLOR : SKETCH_GRID_COLOR;
			add(o + u * t - v * size, o + u * t + v * size, color, i == 0 ? 1.4f : 1.f);
			add(o + v * t - u * size, o + v * t + u * size, color, i == 0 ? 1.4f : 1.f);
		}
	}
	m_rt.SetModelingLines(std::move(ribbons.Vertices()), std::move(ribbons.Indices()));
}

void ModelingSession::SetSketchPlane(Option<modeling::InferenceSettings::WorkPlane> plane) {
	m_sketch = plane;
	if (m_tool)
		m_tool->Reset();
	m_status = plane.IsSome() ? String("Esquisse : les points sont tracés sur le plan choisi (3D pour en sortir)")
							  : String("Esquisse terminée");
	RefreshLines();
}

bool ModelingSession::SetSketchPlaneWorld(const math::FVector3 &point, const math::FVector3 &normal) {
	Option<math::FMatrix4> world = WorldMatrix();
	if (world.IsNone() || normal.Length() < 1e-6f)
		return false;
	const math::FMatrix4 inverse = world.Unwrap().Inverse();
	const modeling::Vec3 origin = inverse.TransformPoint(point);
	// Une normale se transforme par l'inverse transposée : via deux points
	// du plan, on reste exact même avec une échelle non uniforme.
	const math::FVector3 helper = std::fabs(normal.y) < 0.9f ? math::FVector3{0.f, 1.f, 0.f} : math::FVector3{1.f, 0.f, 0.f};
	const math::FVector3 u = helper.Cross(normal).Normalize(), v = normal.Cross(u);
	const modeling::Vec3 lu = inverse.TransformPoint(point + u) - origin, lv = inverse.TransformPoint(point + v) - origin;
	SetSketchPlane(Some(modeling::InferenceSettings::WorkPlane{origin, lu.Cross(lv).Normalize()}));
	return true;
}

Option<std::pair<math::FVector3, math::FVector3>> ModelingSession::SketchPlaneWorld() const {
	Option<math::FMatrix4> world = WorldMatrix();
	if (m_sketch.IsNone() || world.IsNone())
		return NONE;
	const math::FMatrix4 m = world.Unwrap();
	const modeling::Vec3 o = m_sketch.Value().origin, n = m_sketch.Value().normal.Normalize();
	const modeling::Vec3 helper = std::fabs(n.y) < 0.9f ? modeling::Vec3{0.f, 1.f, 0.f} : modeling::Vec3{1.f, 0.f, 0.f};
	const modeling::Vec3 u = helper.Cross(n).Normalize(), v = n.Cross(u);
	const math::FVector3 wo = m.TransformPoint(o);
	const math::FVector3 wn = (m.TransformPoint(o + u) - wo).Cross(m.TransformPoint(o + v) - wo);
	if (wn.Length() < 1e-9f)
		return NONE;
	return Some(std::make_pair(wo, wn.Normalize()));
}

bool ModelingSession::PickSketchFace(math::FVector2 cursor) {
	modeling::Model *model = Model();
	if (!model)
		return false;
	const modeling::ToolEvent event = EventAt(cursor, false, false);
	Option<modeling::RayHit> hit = modeling::Raycast(*model, event.ray);
	if (hit.IsNone())
		return false;
	SetSketchPlane(Some(modeling::InferenceSettings::WorkPlane{hit.Value().point, hit.Value().normal}));
	return true;
}

ModelingSession::Overlay ModelingSession::BuildOverlay() const {
	Overlay overlay;
	modeling::Model *model = Model();
	Option<math::FMatrix4> world = WorldMatrix();
	if (!model || world.IsNone() || !m_tool)
		return overlay;
	const math::FMatrix4 m = world.Unwrap();
	auto *self = const_cast<ModelingSession *>(this);
	const modeling::ToolContext ctx = self->Context(*model);
	const modeling::ToolPreview preview = m_tool->Preview(ctx);

	auto segment = [&](const modeling::Vec3 &a, const modeling::Vec3 &b, sdl3::Color color, bool dashed) {
		Option<math::FVector2> pa = Project(m, a), pb = Project(m, b);
		if (pa.IsSome() && pb.IsSome())
			overlay.segments.push_back({pa.Unwrap(), pb.Unwrap(), color, dashed});
	};
	for (const auto &[a, b] : preview.lines)
		segment(a, b, PREVIEW_COLOR, false);
	for (const auto &[a, b] : preview.guides)
		segment(a, b, GUIDE_COLOR, true);
	// Ligne d'inférence d'axe : de l'ancrage au point, dans la couleur de l'axe.
	if (Option<modeling::Vec3> anchor = m_tool->Anchor(); anchor.IsSome() && m_hover.IsAxis())
		segment(anchor.Unwrap(), m_hover.point, ToSdl(modeling::InferenceColor(m_hover.kind)), true);
	for (const modeling::Vec3 &p : preview.polygon)
		if (Option<math::FVector2> px = Project(m, p); px.IsSome())
			overlay.polygon.push_back(px.Unwrap());
	if (m_hover.IsValid())
		if (Option<math::FVector2> px = Project(m, m_hover.point); px.IsSome()) {
			overlay.marker = px;
			overlay.markerColor = ToSdl(modeling::InferenceColor(m_hover.kind));
			overlay.label = String(modeling::InferenceLabel(m_hover.kind));
		}
	overlay.hint = preview.hint;
	overlay.measure = m_typed.IsEmpty() ? preview.measure : String::Format("Saisie : %s", m_typed.CStr());
	return overlay;
}

} // namespace sandbox
