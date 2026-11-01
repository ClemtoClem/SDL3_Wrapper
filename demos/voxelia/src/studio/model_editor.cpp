// Définitions de voxelia/studio/model_editor.hpp
#include <voxelia/studio/model_editor.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

#include <render3d/texture_loader.hpp>
#include <voxel/json.hpp>
#include <voxelia/session.hpp>
#include <voxelia/shell.hpp>

namespace voxelia::studio {

namespace json = voxel::json;
using voxel::RigChannel;
using voxel::RigInterp;

namespace {

constexpr float GESTURE_SECONDS = 0.4f;
constexpr float UV_VIEW_WIDTH = 300.f;

Option<float> Number(const String &s) {
	char *end = nullptr;
	const float v = std::strtof(s.CStr(), &end);
	if (!end || end == s.CStr() || *end != '\0')
		return NONE;
	return Some(v);
}

/// Trois nombres consécutifs de `args` à partir de `first`.
Option<math::FVector3> Vec3Args(const std::vector<String> &args, size_t first) {
	if (args.size() < first + 3)
		return NONE;
	const Option<float> x = Number(args[first]), y = Number(args[first + 1]), z = Number(args[first + 2]);
	if (x.IsNone() || y.IsNone() || z.IsNone())
		return NONE;
	return Some(math::FVector3{x.Value(), y.Value(), z.Value()});
}

Option<RigChannel> ChannelOf(const String &s) {
	if (s == "rotation")
		return Some(RigChannel::ROTATION);
	if (s == "position")
		return Some(RigChannel::POSITION);
	return NONE;
}

Option<RigInterp> InterpOf(const String &s) {
	if (s == "linear")
		return Some(RigInterp::LINEAR);
	if (s == "step")
		return Some(RigInterp::STEP);
	if (s == "smooth")
		return Some(RigInterp::SMOOTH);
	if (s == "bezier")
		return Some(RigInterp::BEZIER);
	return NONE;
}

void ClearChildren(render3d::Object3D &root) {
	while (!root.Children().empty())
		(void)root.RemoveChild(root.Children().front().get());
}

/// Couleur d'une face dans la vue UV.
ui::Color FaceColor(const char *face) {
	const String f(face);
	if (f == "front")
		return {0.30f, 0.65f, 0.95f, 0.55f};
	if (f == "back")
		return {0.25f, 0.40f, 0.70f, 0.55f};
	if (f == "top")
		return {0.45f, 0.85f, 0.40f, 0.55f};
	if (f == "bottom")
		return {0.35f, 0.55f, 0.30f, 0.55f};
	if (f == "right")
		return {0.95f, 0.55f, 0.30f, 0.55f};
	return {0.90f, 0.35f, 0.45f, 0.55f};
}

} // namespace

ModelEditor::ModelEditor(Shell &shell, StudioReport *report) : m_shell(shell), m_report(report) {}

ModelEditor::~ModelEditor() {
	ClearChildren(m_previewRoot);
	m_previewNodes.clear();
}

voxel::Game *ModelEditor::Game() {
	Session *session = m_shell.Game();
	return session && session->Ready() ? &session->Game() : nullptr;
}

void ModelEditor::Visit(const char *feature, FeatureStatus status, String detail) {
	if (m_report)
		m_report->Visit(String(feature), status, std::move(detail));
}

void ModelEditor::Changed() {
	++m_version;
	if (m_doc.IsSome() && !m_part.IsEmpty() && !m_doc.Value().HasPart(m_part))
		m_part = m_doc.Value().Parts().empty() ? String() : m_doc.Value().Parts().front().name;
}

CommandResult ModelEditor::Done(Result<bool, String> result, const char *feature, const String &ok) {
	if (result.IsError()) {
		Visit(feature, FeatureStatus::Warning, result.Error());
		return CommandResult::Error(result.Error());
	}
	Changed();
	Visit(feature);
	// L'onglet suit l'opération : animation (clés, poignées, couches), parties sinon.
	const String f(feature);
	if (m_tabs.Valid())
		m_tabs.SetSelected(f.StartsWith("model.key") || f.StartsWith("model.clip") || f.StartsWith("model.curve") || f.StartsWith("model.layer") ? 2
						   : f.StartsWith("model.box")																				   ? 1
																																	   : 0,
						   true);
	return CommandResult::Ok(ok);
}

void ModelEditor::Gesture() {
	if (!m_doc.IsSome())
		return;
	if (!m_gesture) {
		m_doc.Value().BeginEdit();
		m_gesture = true;
	}
	m_gestureTimer = GESTURE_SECONDS;
}

void ModelEditor::CloseGesture() {
	if (m_gesture && m_doc.IsSome())
		m_doc.Value().EndEdit();
	m_gesture = false;
	m_gestureTimer = 0.f;
}

// ── Construction ─────────────────────────────────────────────────────────────

void ModelEditor::Build(ui::Widget parent) {
	// Éclairage d'atelier : soleil oblique (les côtés ne sont pas noirs), ambiante franche.
	ui::ViewportLighting lighting;
	lighting.sun.direction = math::FVector3{-0.45f, -1.f, -0.3f}.Normalize();
	lighting.sun.intensity = 1.1f;
	lighting.ambient.color = sdl3::Color{150, 150, 160};
	lighting.background = sdl3::Color{38, 42, 52, 255};
	parent.Append(ui::Column().Grow().Gap(4.f).Children({
		ui::Label("Aucun modèle ouvert — choisissez-en un dans l'onglet « Modèles ».").Class("studio-section").Ref(m_title),
		ui::Row().Gap(4.f).Children({
			ui::Button("Appliquer à chaud").Id("model-apply").OnClick([this] { (void)Apply(); }),
			ui::Button("Enregistrer").Id("model-save").OnClick([this] { (void)Save(); }),
			ui::Button("Exporter .obj").Id("model-export").OnClick([this] {
				if (m_doc.IsSome())
					(void)Export(String("data/voxelia/exports/") + m_doc.Value().Id().Replace(":", "_") + String(".obj"));
			}),
			ui::Button("↶").Tooltip("Annuler").OnClick([this] { (void)Undo(); }),
			ui::Button("↷").Tooltip("Rétablir").OnClick([this] { (void)Redo(); }),
		}),
		ui::Row().Gap(4.f).Children({
			ui::Input("chemin .gltf", "data/voxelia/exports/modele.gltf").Grow().Ref(m_gltfPath),
			ui::Button("Exporter glTF").OnClick([this] { (void)ExportGltf(m_gltfPath.Text().Trim()); }),
			ui::Button("Importer glTF").OnClick([this] { (void)ImportGltf(m_gltfPath.Text().Trim()); }),
		}),
		ui::Viewport3D(m_previewRoot, render3d::Camera{}, true, Some(lighting)).Id("model-preview").Height(230.f).Ref(m_viewport),
		ui::Column().Id("model-diagnostics").Ref(m_diagnostics),
		ui::TabView({String("Parties"), String("Boîtes / UV"), String("Animation"), String("JSON")}).Grow().Ref(m_tabs).Children({
			ui::ScrollView().Grow().Children({
				ui::Column().Id("model-parts").Ref(m_parts),
				ui::Column().Id("model-part-form").Gap(2.f).Ref(m_partForm),
			}),
			ui::ScrollView().Grow().Children({
				ui::Column().Id("model-boxes").Gap(2.f).Ref(m_boxes),
				ui::Box().Id("model-uv").Ref(m_uv),
			}),
			ui::ScrollView().Grow().Children({
				ui::Column().Id("model-animation").Gap(2.f).Ref(m_animation),
				ui::Column().Id("model-curves").Ref(m_curves),
			}),
			ui::Column().Grow().Gap(4.f).Children({
				ui::CodeEditor(String()).Id("model-json").Grow().Ref(m_json),
				ui::Row().Gap(4.f).Children({
					ui::Button("Appliquer le JSON").OnClick([this] {
						if (!m_doc.IsSome())
							return;
						CloseGesture();
						Result<bool, String> r = m_doc.Value().SetJson(m_json.Text());
						m_jsonStatus.SetText(r.IsOk() ? String("JSON appliqué au document") : r.Error());
						if (r.IsOk())
							Changed();
					}),
					ui::Label("").Class("studio-status").Ref(m_jsonStatus),
				}),
			}),
		}),
	}));
}

void ModelEditor::BuildCatalog(ui::Widget parent) {
	parent.Append(ui::Column().Grow().Gap(4.f).Children({
		ui::Row().Gap(4.f).Children({
			ui::Label("Modèles articulés").Class("studio-dock-title").Grow(),
			ui::Button("+").Tooltip("Nouveau modèle (mod « core »)").OnClick([this] {
				(void)New(String::Format("modele_%zu", size_t(m_version + 1)), String("core"));
			}),
		}),
		ui::ScrollView().Id("model-catalog").Grow().Ref(m_catalog),
	}));
	RebuildCatalog();
}

void ModelEditor::RebuildCatalog() {
	if (!m_catalog.Valid())
		return;
	m_catalog.Clear();
	voxel::Game *game = Game();
	if (!game) {
		m_catalog.Append(ui::Label("aucune partie en cours").Class("studio-status"));
		return;
	}
	for (const voxel::ModelDef *model : game->Content().Models()) {
		if (model->format != "rig")
			continue;
		const String id = model->id;
		m_catalog.Append(ui::Label(String("  ") + id).Class("studio-row").OnClick([this, id] { (void)Open(id); }));
	}
}

// ── Rafraîchissements ────────────────────────────────────────────────────────

void ModelEditor::Update(float dt) {
	if (m_gesture) {
		m_gestureTimer -= dt;
		if (m_gestureTimer <= 0.f)
			CloseGesture();
	}
	if (!m_doc.IsSome())
		return;
	if (m_playing && m_mixPreview) {
		m_time += dt;
	} else if (m_playing && !m_clip.IsEmpty()) {
		m_time += dt;
		if (Result<voxel::RigModel, String> rig = m_doc.Value().Build(); rig.IsOk())
			if (const voxel::RigClip *clip = rig.Value().FindClip(m_clip))
				m_time = voxel::RigClipTime(*clip, m_time);
	}
	if (m_version != m_shownVersion && !m_gesture)
		RebuildAll();
	if (m_version != m_previewVersion)
		RebuildPreview();
	PosePreview();
	// Caméra : orbite lente autour du modèle.
	m_orbit += dt * 0.35f;
	if (auto vp = m_viewport.Get<ui::Viewport3DState>(); vp.IsSome()) {
		const float h = std::max(0.5f, m_previewHeight);
		render3d::Camera camera;
		camera.target = {0.f, h * 0.5f, 0.f};
		camera.position = {std::sin(m_orbit) * h * 1.8f, h * 0.9f, std::cos(m_orbit) * h * 1.8f};
		camera.farPlane = 100.f;
		if (vp.Unwrap()->textureHeight > 0)
			camera.aspect = float(vp.Unwrap()->textureWidth) / float(vp.Unwrap()->textureHeight);
		vp.Unwrap()->camera = camera;
	}
}

void ModelEditor::RebuildAll() {
	m_shownVersion = m_version;
	if (!m_doc.IsSome())
		return;
	const RigDocument &doc = m_doc.Value();
	m_title.SetText(doc.Id() + (doc.Dirty() ? String(" ●") : String()) + String("   ") + doc.SourceFile());
	m_json.SetText(doc.Text());
	RebuildDiagnostics();
	RebuildParts();
	RebuildBoxes();
	RebuildAnimation();
}

void ModelEditor::RebuildDiagnostics() {
	m_diagnostics.Clear();
	const std::vector<RigDiagnostic> diags = m_doc.Value().Validate();
	if (diags.empty())
		m_diagnostics.Append(ui::Label("✓ aucun problème").Class("studio-log-ok"));
	for (const RigDiagnostic &d : diags)
		m_diagnostics.Append(
			ui::Label(d.message).Class(d.severity == RigDiagnostic::Severity::Error ? "studio-log-error" : "studio-log-warning"));
	Visit("model.validate", diags.empty() ? FeatureStatus::Ok : FeatureStatus::Warning, String::Format("%zu diagnostic(s)", diags.size()));
}

void ModelEditor::RebuildParts() {
	m_parts.Clear();
	m_partForm.Clear();
	const RigDocument &doc = m_doc.Value();
	const std::vector<RigPartInfo> parts = doc.Parts();
	for (const RigPartInfo &p : parts) {
		const String name = p.name;
		String indent;
		for (int i = 0; i < p.depth; ++i)
			indent += String("    ");
		ui::WidgetBuilder row = ui::Label(indent + (p.depth > 0 ? String("└ ") : String()) + name +
										  String::Format("  (%zu boîte%s)", p.boxes, p.boxes > 1 ? "s" : ""))
									.Class("studio-row")
									.OnClick([this, name] { (void)SelectPart(name); });
		if (name == m_part)
			row.Class("selected");
		m_parts.Append(row);
	}
	if (m_part.IsEmpty() || !doc.HasPart(m_part))
		return;
	const String part = m_part;
	// Parent : racine ou toute partie qui n'en descend pas (Reparent refuse les cycles).
	std::vector<String> parents{String("(racine)")};
	int selected = 0;
	String currentParent;
	for (const RigPartInfo &p : parts)
		if (p.name == part)
			currentParent = p.parent;
	for (const RigPartInfo &p : parts)
		if (p.name != part) {
			parents.push_back(p.name);
			if (p.name == currentParent)
				selected = int(parents.size() - 1);
		}
	ui::Widget renameInput;
	m_partForm.Append(ui::Label(String("Partie « ") + part + String(" »")).Class("studio-section"));
	m_partForm.Append(ui::Row().Gap(4.f).Children({
		ui::Input("nouveau nom", part).Grow().Ref(renameInput),
		ui::Button("Renommer").OnClick([this, part, renameInput]() mutable {
			const String to = renameInput.Text().Trim();
			if (Done(m_doc.Value().RenamePart(part, to), "model.part_rename", to).status == CommandStatus::Ok)
				m_part = to;
		}),
	}));
	m_partForm.Append(ui::Row().Gap(4.f).Children({
		ui::Label("Parent").Class("studio-key"),
		ui::Combo(parents, selected).Grow().OnSelect([this, part, parents](int i) {
			if (i >= 0 && size_t(i) < parents.size())
				(void)Done(m_doc.Value().Reparent(part, i == 0 ? String() : parents[size_t(i)]), "model.part_reparent", part);
		}),
	}));
	m_partForm.Append(ui::Row().Gap(4.f).Children({
		ui::Button("+ Enfant").OnClick([this, part] {
			const String name = String::Format("%s_%zu", part.CStr(), m_doc.Value().Parts().size());
			if (Done(m_doc.Value().AddPart(name, part), "model.part_add", name).status == CommandStatus::Ok)
				m_part = name;
		}),
		ui::Button("Dupliquer").OnClick([this, part] {
			(void)Done(m_doc.Value().DuplicatePart(part, part + String("_copie")), "model.part_duplicate", part);
		}),
		ui::Button("Supprimer").OnClick([this, part] { (void)Done(m_doc.Value().RemovePart(part), "model.part_remove", part); }),
	}));
	// Pivot et rotation de repos : chaque curseur glissé = une étape d'annulation.
	auto vecRow = [&](const char *label, math::FVector3 value, float min, float max, float step, bool rotation) {
		std::vector<ui::AnyWidget> children{ui::Label(String(label)).Class("studio-key")};
		for (int axis = 0; axis < 3; ++axis) {
			const float v = axis == 0 ? value.x : axis == 1 ? value.y : value.z;
			children.push_back(ui::NumberInput(min, max, v, step, 1).Grow().OnChange([this, part, axis, rotation](float x) {
				Gesture();
				math::FVector3 now = rotation ? m_doc.Value().Rotation(part) : m_doc.Value().Pivot(part);
				(axis == 0 ? now.x : axis == 1 ? now.y : now.z) = x;
				(void)(rotation ? m_doc.Value().SetRotation(part, now) : m_doc.Value().SetPivot(part, now));
				m_previewVersion = ~0ull; // l'aperçu suit le glissé
				Visit(rotation ? "model.part_rotate" : "model.part_pivot");
			}));
		}
		m_partForm.Append(ui::Row().Gap(2.f).Children(children));
	};
	vecRow("Pivot (px)", doc.Pivot(part), -256.f, 256.f, 0.5f, false);
	vecRow("Rotation (°)", doc.Rotation(part), -180.f, 180.f, 1.f, true);
	const math::FQuaternion q = voxel::RigRotation(doc.Rotation(part));
	m_partForm.Append(ui::Label(String::Format("quaternion (x, y, z, w) = %.3f, %.3f, %.3f, %.3f — ordre x, puis y, puis z", double(q.x),
											   double(q.y), double(q.z), double(q.w)))
						  .Class("studio-status"));
}

void ModelEditor::RebuildBoxes() {
	m_boxes.Clear();
	m_uv.Clear();
	if (m_part.IsEmpty() || !m_doc.Value().HasPart(m_part))
		return;
	const String part = m_part;
	const std::vector<voxel::RigBox> boxes = m_doc.Value().Boxes(part);
	m_boxes.Append(ui::Row().Gap(4.f).Children({
		ui::Label(String("Boîtes de « ") + part + String(" »")).Class("studio-section").Grow(),
		ui::Button("+ Boîte").OnClick([this, part] {
			const math::FVector3 p = m_doc.Value().Pivot(part);
			(void)Done(m_doc.Value().AddBox(part, {p.x - 2.f, p.y, p.z - 2.f}, {4.f, 4.f, 4.f}, 0, 0), "model.box_add", part);
		}),
	}));
	for (size_t i = 0; i < boxes.size(); ++i) {
		const voxel::RigBox box = boxes[i];
		auto field = [this, part, i](int which, float value, float min, float max) {
			return ui::NumberInput(min, max, value, 0.5f, 1).Grow().OnChange([this, part, i, which](float x) {
				std::vector<voxel::RigBox> now = m_doc.Value().Boxes(part);
				if (i >= now.size())
					return;
				voxel::RigBox b = now[i];
				float *slots[] = {&b.from.x, &b.from.y, &b.from.z, &b.size.x, &b.size.y, &b.size.z};
				if (which < 6)
					*slots[which] = x;
				else if (which == 6)
					b.u = int(x);
				else
					b.v = int(x);
				Gesture();
				(void)m_doc.Value().SetBox(part, i, b);
				m_previewVersion = ~0ull;
				Visit("model.box_edit");
			});
		};
		m_boxes.Append(ui::Label(String::Format("Boîte %zu%s", i, box.mirror ? " (miroir)" : "")).Class("studio-key"));
		m_boxes.Append(ui::Row().Gap(2.f).Children({ui::Label("de").Class("studio-status"), field(0, box.from.x, -256.f, 256.f),
													field(1, box.from.y, -256.f, 256.f), field(2, box.from.z, -256.f, 256.f)}));
		m_boxes.Append(ui::Row().Gap(2.f).Children({ui::Label("taille").Class("studio-status"), field(3, box.size.x, 0.f, 256.f),
													field(4, box.size.y, 0.f, 256.f), field(5, box.size.z, 0.f, 256.f)}));
		m_boxes.Append(ui::Row().Gap(2.f).Children({
			ui::Label("uv").Class("studio-status"),
			field(6, float(box.u), 0.f, 1024.f),
			field(7, float(box.v), 0.f, 1024.f),
			ui::Button("Miroir").Tooltip("Copie symétrique (x = 0)").OnClick([this, part, i] {
				(void)Done(m_doc.Value().MirrorBox(part, i), "model.box_mirror", part);
			}),
			ui::Button("×").OnClick([this, part, i] { (void)Done(m_doc.Value().RemoveBox(part, i), "model.box_remove", part); }),
		}));
	}
	// Dépliage UV : les faces de chaque boîte sur la texture (taille déclarée).
	Result<voxel::RigModel, String> rig = m_doc.Value().Build();
	if (rig.IsError())
		return;
	const float tw = float(rig.Value().textureWidth), th = float(rig.Value().textureHeight);
	const float scale = UV_VIEW_WIDTH / std::max(1.f, tw);
	m_uv.Append(ui::Label(String::Format("Dépliage UV (%d×%d) — avant, dos, dessus, dessous, côtés", int(tw), int(th))).Class("studio-section"));
	ui::Widget sheet = m_uv.Append(ui::Box().Width(UV_VIEW_WIDTH).Height(th * scale).Background({0.12f, 0.13f, 0.16f, 1.f}));
	for (size_t i = 0; i < boxes.size(); ++i)
		for (const voxel::RigUvFace &f : voxel::RigBoxUv(boxes[i]))
			sheet.Append(ui::Box()
							 .Absolute()
							 .Left(f.u * scale)
							 .Top(f.v * scale)
							 .Width(std::max(1.f, f.width * scale))
							 .Height(std::max(1.f, f.height * scale))
							 .Background(FaceColor(f.face))
							 .Tooltip(String::Format("boîte %zu, %s : (%g, %g) %g×%g", i, f.face, double(f.u), double(f.v), double(f.width),
													 double(f.height))));
	Visit("model.uv_view");
}

void ModelEditor::RebuildAnimation() {
	m_animation.Clear();
	m_curves.Clear();
	const RigDocument &doc = m_doc.Value();
	const std::vector<String> clips = doc.Clips();
	if (!m_clip.IsEmpty() && std::find(clips.begin(), clips.end(), m_clip) == clips.end())
		m_clip = String();
	if (m_clip.IsEmpty() && !clips.empty())
		m_clip = clips.front();
	ui::Widget clipName;
	m_animation.Append(ui::Row().Gap(4.f).Children({
		ui::Input("nouvelle animation", "idle").Grow().Ref(clipName),
		ui::Button("+ Animation").OnClick([this, clipName]() mutable {
			const String name = clipName.Text().Trim();
			if (Done(m_doc.Value().AddClip(name, 2.f, true), "model.clip_add", name).status == CommandStatus::Ok)
				m_clip = name;
		}),
	}));
	if (clips.empty()) {
		m_animation.Append(ui::Label("Aucune animation : « idle » joue en permanence, « walk » suit la marche.").Class("studio-status"));
		return;
	}
	int clipIndex = 0;
	for (size_t i = 0; i < clips.size(); ++i)
		if (clips[i] == m_clip)
			clipIndex = int(i);
	Result<voxel::RigModel, String> rig = doc.Build();
	const voxel::RigClip *clip = rig.IsOk() ? rig.Value().FindClip(m_clip) : nullptr;
	const float length = clip ? clip->length : 1.f;
	m_animation.Append(ui::Row().Gap(4.f).Children({
		ui::Combo(clips, clipIndex).Grow().OnSelect([this, clips](int i) {
			if (i >= 0 && size_t(i) < clips.size()) {
				m_clip = clips[size_t(i)];
				m_time = 0.f;
				Changed();
			}
		}),
		ui::Button(m_playing ? String("Pause") : String("Lecture")).OnClick([this] {
			m_playing = !m_playing;
			Changed();
		}),
		ui::Button("Supprimer").OnClick([this] { (void)Done(m_doc.Value().RemoveClip(m_clip), "model.clip_remove", m_clip); }),
	}));
	m_animation.Append(ui::Row().Gap(4.f).Children({
		ui::Label(String::Format("Temps (0 à %.2f s)%s", double(length), clip && clip->loop ? ", en boucle" : "")).Class("studio-key"),
		ui::NumberInput(0.f, length, m_time, 0.01f, 2).Grow().OnChange([this](float t) {
			m_time = t;
			m_playing = false;
			Visit("model.scrub");
		}),
	}));
	// Clé de la partie sélectionnée au temps courant.
	if (!m_part.IsEmpty()) {
		const std::vector<String> channels{String("rotation"), String("position")};
		const std::vector<String> interps{String("linear"), String("step"), String("smooth"), String("bezier")};
		m_animation.Append(ui::Row().Gap(4.f).Children({
			ui::Label(String("Clé de « ") + m_part + String(" »")).Class("studio-key"),
			ui::Combo(channels, m_channel == RigChannel::POSITION ? 1 : 0).OnSelect([this](int i) {
				m_channel = i == 1 ? RigChannel::POSITION : RigChannel::ROTATION;
				Changed();
			}),
			ui::Combo(interps, int(m_interp)).OnSelect([this](int i) { m_interp = RigInterp(std::clamp(i, 0, 3)); }),
		}));
		std::vector<ui::AnyWidget> value{ui::Label("valeur").Class("studio-status")};
		for (int axis = 0; axis < 3; ++axis)
			value.push_back(ui::NumberInput(-360.f, 360.f, axis == 0 ? m_keyValue.x : axis == 1 ? m_keyValue.y : m_keyValue.z, 1.f, 1)
								.Grow()
								.OnChange([this, axis](float x) { (axis == 0 ? m_keyValue.x : axis == 1 ? m_keyValue.y : m_keyValue.z) = x; }));
		value.push_back(ui::Button("Poser la clé").OnClick([this] {
			(void)Done(m_doc.Value().SetKey(m_clip, m_part, m_channel, m_time, m_keyValue, m_interp), "model.key_set",
					   String::Format("%.2f s", double(m_time)));
		}));
		m_animation.Append(ui::Row().Gap(2.f).Children(value));
		for (const voxel::RigKey &key : doc.Keys(m_clip, m_part, m_channel)) {
			const float t = key.time;
			m_animation.Append(ui::Row().Gap(4.f).Children({
				ui::Label(String::Format("  %.2f s : %.1f, %.1f, %.1f (%s)", double(key.time), double(key.value.x), double(key.value.y),
										 double(key.value.z), RigInterpId(key.interp)))
					.Class("studio-row")
					.Grow()
					.OnClick([this, t, key] {
						m_time = t;
						m_keyValue = key.value;
						m_selectedKey = Some(t);
						m_playing = false;
						Changed();
					}),
				ui::Button("×").OnClick([this, t] {
					(void)Done(m_doc.Value().RemoveKey(m_clip, m_part, m_channel, t), "model.key_remove", String::Format("%.2f s", double(t)));
				}),
			}));
		}
		// Poignées de Bézier de la clé sélectionnée (temps en s, valeur relative).
		const std::vector<voxel::RigKey> keys = doc.Keys(m_clip, m_part, m_channel);
		const voxel::RigKey *selected = nullptr;
		for (const voxel::RigKey &k : keys)
			if (m_selectedKey.IsSome() && std::fabs(k.time - m_selectedKey.Value()) <= 1e-3f)
				selected = &k;
		if (keys.size() >= 2)
			m_animation.Append(ui::Button("Lisser la piste").Tooltip("Tangentes automatiques (Catmull-Rom), clés en Bézier").OnClick([this] {
				(void)Done(m_doc.Value().AutoSmooth(m_clip, m_part, m_channel), "model.curve_smooth", m_part);
			}));
		if (selected) {
			const float t = selected->time;
			const voxel::RigHandle in = selected->in.value_or(voxel::RigHandle{-0.1f, {0.f, 0.f, 0.f}});
			const voxel::RigHandle out = selected->out.value_or(voxel::RigHandle{0.1f, {0.f, 0.f, 0.f}});
			m_handleIn = in;
			m_handleOut = out;
			m_animation.Append(ui::Label(String::Format("Poignées de la clé à %.2f s (%s)", double(t),
														 selected->in || selected->out ? "réglées" : "automatiques"))
								   .Class("studio-section"));
			auto handleRow = [&](const char *label, bool outgoing) {
				const voxel::RigHandle h = outgoing ? out : in;
				std::vector<ui::AnyWidget> row{ui::Label(String(label)).Class("studio-key")};
				for (int k = 0; k < 4; ++k) {
					const float v = k == 0 ? h.time : k == 1 ? h.value.x : k == 2 ? h.value.y : h.value.z;
					row.push_back(ui::NumberInput(k == 0 ? (outgoing ? 0.f : -10.f) : -360.f, k == 0 ? (outgoing ? 10.f : 0.f) : 360.f, v,
												  k == 0 ? 0.01f : 1.f, 2)
									  .Grow()
									  .OnChange([this, outgoing, k](float x) {
										  voxel::RigHandle &target = outgoing ? m_handleOut : m_handleIn;
										  (k == 0 ? target.time : k == 1 ? target.value.x : k == 2 ? target.value.y : target.value.z) = x;
									  }));
				}
				m_animation.Append(ui::Row().Gap(2.f).Children(row));
			};
			handleRow("entrée (t, x, y, z)", false);
			handleRow("sortie (t, x, y, z)", true);
			m_animation.Append(ui::Row().Gap(4.f).Children({
				ui::Button("Appliquer les poignées").OnClick([this, t] {
					(void)Done(m_doc.Value().SetKeyHandles(m_clip, m_part, m_channel, t, Some(m_handleIn), Some(m_handleOut)), "model.key_handles",
							   String::Format("%.2f s", double(t)));
				}),
				ui::Button("Poignées auto").OnClick([this, t] {
					(void)Done(m_doc.Value().SetKeyHandles(m_clip, m_part, m_channel, t, NONE, NONE), "model.key_handles",
							   String::Format("%.2f s", double(t)));
				}),
			}));
		}
		// Graphe des courbes x / y / z (échantillonnées par le moteur).
		const size_t samples = 64;
		const std::vector<math::FVector3> curve = doc.SampleCurve(m_clip, m_part, m_channel, samples);
		if (!curve.empty()) {
			std::vector<float> xs, cx, cy, cz;
			for (size_t i = 0; i < curve.size(); ++i) {
				xs.push_back(length * float(i) / float(samples - 1));
				cx.push_back(curve[i].x);
				cy.push_back(curve[i].y);
				cz.push_back(curve[i].z);
			}
			// Poignées de Bézier : les points de contrôle de chaque segment.
			std::vector<float> ht, hx, hy, hz;
			for (size_t i = 1; i < keys.size(); ++i)
				if (keys[i].interp == RigInterp::BEZIER) {
					const voxel::RigBezier bz = voxel::RigSegmentBezier(keys[i - 1], keys[i]);
					for (int p : {1, 2}) {
						ht.push_back(bz.t[p]);
						hx.push_back(bz.v[p].x);
						hy.push_back(bz.v[p].y);
						hz.push_back(bz.v[p].z);
					}
				}
			ui::PlotBuilder plot = ui::Plot(String("Courbes — ") + m_part + String(" / ") + String(RigChannelId(m_channel)));
			plot.Height(170.f)
				.AddLineSeriesXy(xs, cx, String("x"), Some(ui::Color{0.95f, 0.40f, 0.40f, 1.f}))
				.AddLineSeriesXy(xs, cy, String("y"), Some(ui::Color{0.45f, 0.85f, 0.40f, 1.f}))
				.AddLineSeriesXy(xs, cz, String("z"), Some(ui::Color{0.40f, 0.60f, 0.95f, 1.f}));
			if (!ht.empty())
				plot.AddSeries(ui::PlotSeriesKind::SCATTER, ht, hx, String("poignées x"), Some(ui::Color{0.95f, 0.70f, 0.70f, 1.f}))
					.AddSeries(ui::PlotSeriesKind::SCATTER, ht, hy, String("poignées y"), Some(ui::Color{0.70f, 0.95f, 0.70f, 1.f}))
					.AddSeries(ui::PlotSeriesKind::SCATTER, ht, hz, String("poignées z"), Some(ui::Color{0.70f, 0.80f, 0.95f, 1.f}));
			m_curves.Append(plot);
			Visit("model.curves");
		}
	}
	// Mélange de clips : couches jouées par-dessus idle / walk (« layers »).
	m_animation.Append(ui::Label("Mélange (couches)").Class("studio-section"));
	m_animation.Append(ui::Row().Gap(4.f).Children({
		ui::Checkbox(String("Aperçu du mélange"), m_mixPreview).OnToggle([this](bool on) {
			m_mixPreview = on;
			m_playing = on || m_playing;
			Visit("model.mix_preview");
		}),
		ui::Button("+ Couche").OnClick([this] {
			if (!m_clip.IsEmpty())
				(void)Done(m_doc.Value().AddLayer({m_clip, 1.f, 1.f, voxel::RigBlend::MIX}), "model.layer_add", m_clip);
		}),
	}));
	const std::vector<voxel::RigLayer> layers = doc.Layers();
	for (size_t i = 0; i < layers.size(); ++i) {
		const voxel::RigLayer layer = layers[i];
		int clipIndex2 = 0;
		for (size_t k = 0; k < clips.size(); ++k)
			if (clips[k] == layer.clip)
				clipIndex2 = int(k);
		auto update = [this, i](const std::function<void(voxel::RigLayer &)> &change) {
			std::vector<voxel::RigLayer> now = m_doc.Value().Layers();
			if (i >= now.size())
				return;
			change(now[i]);
			Gesture();
			(void)m_doc.Value().SetLayer(i, now[i]);
			Visit("model.layer_edit");
		};
		m_animation.Append(ui::Row().Gap(2.f).Children({
			ui::Combo(clips, clipIndex2).Grow().OnSelect([update, clips](int k) {
				if (k >= 0 && size_t(k) < clips.size())
					update([&](voxel::RigLayer &l) { l.clip = clips[size_t(k)]; });
			}),
			ui::Label("poids").Class("studio-status"),
			ui::NumberInput(0.f, 1.f, layer.weight, 0.05f, 2).OnChange([update](float w) { update([&](voxel::RigLayer &l) { l.weight = w; }); }),
			ui::Label("vitesse").Class("studio-status"),
			ui::NumberInput(0.f, 8.f, layer.speed, 0.1f, 2).OnChange([update](float v) { update([&](voxel::RigLayer &l) { l.speed = v; }); }),
			ui::Combo({String("mix"), String("add")}, layer.mode == voxel::RigBlend::ADD ? 1 : 0).OnSelect([update](int m) {
				update([&](voxel::RigLayer &l) { l.mode = m == 1 ? voxel::RigBlend::ADD : voxel::RigBlend::MIX; });
			}),
			ui::Button("×").OnClick([this, i] { (void)Done(m_doc.Value().RemoveLayer(i), "model.layer_remove", String::Format("%zu", i)); }),
		}));
	}
}

// ── Aperçu ───────────────────────────────────────────────────────────────────

void ModelEditor::RebuildPreview() {
	m_previewVersion = m_version;
	ClearChildren(m_previewRoot);
	m_previewNodes.clear();
	m_previewRig.reset();
	if (!m_doc.IsSome())
		return;
	Result<voxel::RigModel, String> built = m_doc.Value().Build();
	if (built.IsError())
		return;
	m_previewRig = std::make_shared<voxel::RigModel>(std::move(built).Unwrap());
	const voxel::RigModel &rig = *m_previewRig;
	voxel::Game *game = Game();
	// Skin : celui du modèle, sinon celui d'une entité qui l'utilise.
	String skin = rig.texture;
	if (skin.IsEmpty() && game)
		for (const voxel::EntityDef *e : game->Content().Entities())
			if (e->rig.model == m_doc.Value().Id() && !e->rig.skins.empty()) {
				skin = e->rig.skins.front();
				break;
			}
	int width = rig.textureWidth, height = rig.textureHeight;
	if (skin != m_skinId || !m_skin) {
		m_skinId = skin;
		m_skin.reset();
		voxel::Image pixels;
		if (game && !skin.IsEmpty())
			if (std::shared_ptr<voxel::TextureImage> tex = game->Assets().Texture(skin); tex && !tex->Pixels().IsEmpty())
				pixels = tex->Pixels();
		if (pixels.IsEmpty() && !skin.IsEmpty()) // fichier image (modèle importé), comme le fait le jeu
			if (auto file = voxel::LoadImageFile(skin); file.IsOk())
				pixels = std::move(file).Unwrap();
		if (pixels.IsEmpty()) { // damier : skin introuvable
			pixels.width = pixels.height = 2;
			pixels.rgba = {255, 0, 255, 255, 40, 40, 40, 255, 40, 40, 40, 255, 255, 0, 255, 255};
		}
		auto texture = render3d::CreateTextureFromPixels(m_shell.Canvas().Device(), uint32_t(pixels.width), uint32_t(pixels.height), pixels.rgba);
		if (texture.IsOk())
			m_skin = std::make_unique<sdl3::GpuTexture>(std::move(texture).Unwrap());
		// Taille RÉELLE de l'image (comme le jeu) : un skin 64×64 sur un modèle
		// déclaré 64×32 garde ses UV et active uv64 / only64 ; le damier, lui,
		// prend la taille déclarée.
		const bool fallback = pixels.width == 2 && pixels.height == 2;
		m_skinWidth = fallback ? rig.textureWidth : pixels.width;
		m_skinHeight = fallback ? rig.textureHeight : pixels.height;
	}
	width = m_skinWidth > 0 ? m_skinWidth : width;
	height = m_skinHeight > 0 ? m_skinHeight : height;
	render3d::Material material = render3d::Material::Default();
	material.nearestFilter = true;
	material.doubleSided = true;
	material.roughness = 1.f;
	if (m_skin)
		material.albedo = Some(MakeRef(*m_skin));
	float top = 0.5f;
	for (size_t i = 0; i < rig.parts.size(); ++i) {
		const voxel::RigPart &part = rig.parts[i];
		const voxel::ModelMesh mesh = voxel::BuildRigPart(rig, part, width, height);
		std::vector<render3d::Vertex3D> vertices;
		for (size_t k = 0; k < mesh.positions.size(); ++k) {
			vertices.push_back(render3d::Vertex3D{mesh.positions[k], mesh.normals[k], mesh.uvs[k], sdl3::Color::WHITE()});
			top = std::max(top, mesh.positions[k].y + part.pivot.y * rig.scale);
		}
		auto shape = std::make_unique<render3d::Shape>(std::make_shared<render3d::Mesh>(std::move(vertices), mesh.indices), material);
		render3d::Object3D *node = shape.get();
		const int parent = part.parent.IsEmpty() ? -1 : rig.Find(part.parent);
		(parent >= 0 ? *m_previewNodes[size_t(parent)] : m_previewRoot).Add(std::move(shape));
		m_previewNodes.push_back(node);
	}
	m_previewHeight = top;
	Visit("model.preview", FeatureStatus::Ok, String::Format("%zu partie(s)", m_previewNodes.size()));
}

void ModelEditor::PosePreview() {
	if (!m_previewRig || m_previewNodes.size() != m_previewRig->parts.size())
		return;
	const voxel::RigModel &rig = *m_previewRig;
	voxel::RigPoseInput input;
	input.time = m_time;
	input.clip = m_clip.IsEmpty() || m_mixPreview ? nullptr : rig.FindClip(m_clip);
	input.clipTime = m_time;
	// Aperçu du mélange : la pose complète du jeu (idle, couches) au temps courant.
	const bool mixing = m_mixPreview;
	for (size_t i = 0; i < m_previewNodes.size(); ++i) {
		const voxel::RigPart &part = rig.parts[i];
		voxel::RigPartPose pose;
		pose.rotation = part.rotation;
		if (input.clip || mixing)
			pose = voxel::PoseRig(rig, i, input);
		const int parent = part.parent.IsEmpty() ? -1 : rig.Find(part.parent);
		math::FVector3 pivot = part.pivot + pose.offset;
		if (parent >= 0)
			pivot = pivot - rig.parts[size_t(parent)].pivot;
		m_previewNodes[i]->SetPosition(pivot * rig.scale);
		m_previewNodes[i]->SetRotation(voxel::RigRotation(pose.rotation));
	}
}

// ── Actions ──────────────────────────────────────────────────────────────────

CommandResult ModelEditor::Open(const String &modelId) {
	voxel::Game *game = Game();
	if (!game)
		return CommandResult::Error(String("aucune partie en cours"));
	Option<RigDocument> doc = RigDocument::FromModel(game->Content(), modelId);
	if (doc.IsNone())
		return CommandResult::Error(String("modèle articulé introuvable (ou sans source) : ") + modelId);
	CloseGesture();
	m_doc = std::move(doc);
	m_part = m_doc.Value().Parts().empty() ? String() : m_doc.Value().Parts().front().name;
	m_clip = String();
	m_time = 0.f;
	m_playing = false;
	Changed();
	Visit("model.open");
	if (onDocumentOpened)
		onDocumentOpened();
	return CommandResult::Ok(String("modèle ouvert : ") + m_doc.Value().Id());
}

CommandResult ModelEditor::New(const String &id, const String &mod) {
	voxel::Game *game = Game();
	if (!game)
		return CommandResult::Error(String("aucune partie en cours"));
	if (id.IsEmpty() || id.Contains("/") || id.Contains(" "))
		return CommandResult::Error(String("id de modèle invalide : ") + id);
	if (game->Content().FindModel(voxel::ContentRegistry::Qualify(id, mod)))
		return CommandResult::Error(String("ce modèle existe déjà : ") + voxel::ContentRegistry::Qualify(id, mod));
	String baseDir;
	for (const voxel::ModelDef *m : game->Content().Models())
		if (m->mod == mod && !m->baseDir.IsEmpty()) {
			baseDir = m->baseDir;
			break;
		}
	for (const voxel::BlockDef &b : game->Content().Blocks())
		if (baseDir.IsEmpty() && b.mod == mod && !b.baseDir.IsEmpty())
			baseDir = b.baseDir;
	if (baseDir.IsEmpty())
		return CommandResult::Error(String("mod inconnu : ") + mod);
	CloseGesture();
	m_doc = Some(RigDocument::New(id, mod, baseDir, baseDir + String("/models/studio.json")));
	m_part = String("body");
	m_clip = String();
	m_time = 0.f;
	Changed();
	Visit("model.new");
	if (onDocumentOpened)
		onDocumentOpened();
	return CommandResult::Ok(String("nouveau modèle : ") + m_doc.Value().Id());
}

CommandResult ModelEditor::SelectPart(const String &part) {
	if (!m_doc.IsSome() || !m_doc.Value().HasPart(part))
		return CommandResult::Error(String("partie inconnue : ") + part);
	CloseGesture();
	m_part = part;
	Changed();
	Visit("model.part_select");
	return CommandResult::Ok(String("partie : ") + part);
}

CommandResult ModelEditor::Apply() {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucun modèle ouvert"));
	CloseGesture();
	for (const RigDiagnostic &d : m_doc.Value().Validate())
		if (d.severity == RigDiagnostic::Severity::Error)
			return CommandResult::Error(String("le modèle ne se charge pas : ") + d.message);
	Session *session = m_shell.Game();
	if (!session || !session->Ready())
		return CommandResult::Error(String("aucune partie en cours"));
	Result<Session::ContentReload, String> r = session->ReloadDefinition(m_doc.Value().Json(), m_doc.Value().Context());
	if (r.IsError()) {
		Visit("model.hot_reload", FeatureStatus::Error, r.Error());
		return CommandResult::Error(r.Error());
	}
	Visit("model.hot_reload", FeatureStatus::Ok, String::Format("%.0f ms", r.Value().milliseconds));
	RebuildCatalog();
	return CommandResult::Ok(m_doc.Value().Id() + String::Format(" rechargé à chaud (%.0f ms) : les entités qui l'utilisent le reprennent",
																	 r.Value().milliseconds));
}

CommandResult ModelEditor::Save() {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucun modèle ouvert"));
	CloseGesture();
	Result<bool, String> r = m_doc.Value().Save();
	if (r.IsError())
		return CommandResult::Error(r.Error());
	Changed();
	Visit("model.save", FeatureStatus::Ok, m_doc.Value().SourceFile());
	return CommandResult::Ok(String("enregistré : ") + m_doc.Value().SourceFile());
}

CommandResult ModelEditor::Export(const String &path) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucun modèle ouvert"));
	Result<size_t, String> r = m_doc.Value().ExportObj(path, m_clip.IsEmpty() ? String() : m_clip, m_time);
	if (r.IsError()) {
		Visit("model.export_obj", FeatureStatus::Error, r.Error());
		return CommandResult::Error(r.Error());
	}
	if (m_report)
		m_report->Capture(path);
	Visit("model.export_obj", FeatureStatus::Ok, String::Format("%zu triangles", r.Value()));
	return CommandResult::Ok(String::Format("%zu triangles → ", r.Value()) + path);
}

CommandResult ModelEditor::ExportGltf(const String &path) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucun modèle ouvert"));
	// Le skin (image) voyage avec le modèle quand c'est un fichier connu.
	String skin;
	if (voxel::Game *game = Game(); game && !m_skinId.IsEmpty()) {
		if (const voxel::TextureDef *tex = game->Content().FindTexture(m_skinId); tex && !tex->path.IsEmpty())
			skin = tex->path;
		else if (std::filesystem::exists(m_skinId.CStr()))
			skin = m_skinId;
	}
	Result<size_t, String> r = m_doc.Value().ExportGltf(path, skin, 30.f, skin.IsEmpty() ? 0 : m_skinWidth, skin.IsEmpty() ? 0 : m_skinHeight);
	if (r.IsError()) {
		Visit("model.export_gltf", FeatureStatus::Error, r.Error());
		return CommandResult::Error(r.Error());
	}
	if (m_report)
		m_report->Capture(path);
	Visit("model.export_gltf", FeatureStatus::Ok, String::Format("%zu triangles, %zu animation(s)", r.Value(), m_doc.Value().Clips().size()));
	return CommandResult::Ok(String::Format("glTF : %zu triangles, %zu animation(s) → ", r.Value(), m_doc.Value().Clips().size()) + path);
}

CommandResult ModelEditor::ImportGltf(const String &path) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucun modèle ouvert (créez-en un : studio.model.new)"));
	CloseGesture();
	Result<size_t, String> r = m_doc.Value().ImportGltf(path);
	if (r.IsError()) {
		Visit("model.import_gltf", FeatureStatus::Error, r.Error());
		return CommandResult::Error(r.Error());
	}
	Changed();
	Visit("model.import_gltf", FeatureStatus::Ok, String::Format("%zu partie(s)", r.Value()));
	return CommandResult::Ok(String::Format("%zu partie(s) importée(s) de ", r.Value()) + path);
}

CommandResult ModelEditor::Undo() {
	CloseGesture();
	if (!m_doc.IsSome() || !m_doc.Value().Undo())
		return CommandResult::Warning(String("rien à annuler"));
	Changed();
	Visit("model.undo");
	return CommandResult::Ok(String("annulé"));
}

CommandResult ModelEditor::Redo() {
	CloseGesture();
	if (!m_doc.IsSome() || !m_doc.Value().Redo())
		return CommandResult::Warning(String("rien à rétablir"));
	Changed();
	Visit("model.redo");
	return CommandResult::Ok(String("rétabli"));
}

CommandResult ModelEditor::Play(const String &clip) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucun modèle ouvert"));
	const std::vector<String> clips = m_doc.Value().Clips();
	if (std::find(clips.begin(), clips.end(), clip) == clips.end())
		return CommandResult::Error(String("animation inconnue : ") + clip);
	m_clip = clip;
	m_time = 0.f;
	m_playing = true;
	Changed();
	Visit("model.play", FeatureStatus::Ok, clip);
	return CommandResult::Ok(String("lecture : ") + clip);
}

CommandResult ModelEditor::SetTime(float time) {
	m_time = std::max(0.f, time);
	m_playing = false;
	Changed();
	Visit("model.scrub");
	return CommandResult::Ok(String::Format("t = %.2f s", double(m_time)));
}

void ModelEditor::RegisterCommands(ConsoleModel &console) {
	auto need = [](const std::vector<String> &args, size_t n, const char *usage) -> Option<CommandResult> {
		if (args.size() < n)
			return Some(CommandResult::Error(String("usage : ") + String(usage)));
		return NONE;
	};
	console.Register("studio.model.open", "studio.model.open <id> — ouvre un modèle articulé", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.model.open <id>"); e.IsSome())
			return e.Value();
		return Open(a[1]);
	});
	console.Register("studio.model.new", "studio.model.new <id> [mod] — nouveau modèle", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.model.new <id> [mod]"); e.IsSome())
			return e.Value();
		return New(a[1], a.size() > 2 ? a[2] : String("core"));
	});
	console.Register("studio.model.part", "studio.model.part <nom> — sélectionne une partie", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.model.part <nom>"); e.IsSome())
			return e.Value();
		return SelectPart(a[1]);
	});
	console.Register("studio.model.part.add", "studio.model.part.add <nom> [parent]", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.model.part.add <nom> [parent]"); e.IsSome())
			return e.Value();
		if (!m_doc.IsSome())
			return CommandResult::Error(String("aucun modèle ouvert"));
		CommandResult r = Done(m_doc.Value().AddPart(a[1], a.size() > 2 ? a[2] : String()), "model.part_add", a[1]);
		if (r.status == CommandStatus::Ok)
			m_part = a[1];
		return r;
	});
	console.Register("studio.model.part.remove", "studio.model.part.remove <nom>", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.model.part.remove <nom>"); e.IsSome())
			return e.Value();
		return m_doc.IsSome() ? Done(m_doc.Value().RemovePart(a[1]), "model.part_remove", a[1])
							  : CommandResult::Error(String("aucun modèle ouvert"));
	});
	console.Register("studio.model.part.rename", "studio.model.part.rename <de> <vers>", [this, need](const auto &a) {
		if (auto e = need(a, 3, "studio.model.part.rename <de> <vers>"); e.IsSome())
			return e.Value();
		if (!m_doc.IsSome())
			return CommandResult::Error(String("aucun modèle ouvert"));
		CommandResult r = Done(m_doc.Value().RenamePart(a[1], a[2]), "model.part_rename", a[2]);
		if (r.status == CommandStatus::Ok && m_part == a[1])
			m_part = a[2];
		return r;
	});
	console.Register("studio.model.part.parent", "studio.model.part.parent <nom> [parent] — sans parent : racine", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.model.part.parent <nom> [parent]"); e.IsSome())
			return e.Value();
		return m_doc.IsSome() ? Done(m_doc.Value().Reparent(a[1], a.size() > 2 ? a[2] : String()), "model.part_reparent", a[1])
							  : CommandResult::Error(String("aucun modèle ouvert"));
	});
	console.Register("studio.model.part.pivot", "studio.model.part.pivot <nom> x y z (px)", [this](const auto &a) {
		const Option<math::FVector3> v = Vec3Args(a, 2);
		if (a.size() < 5 || v.IsNone() || !m_doc.IsSome())
			return CommandResult::Error(String("usage : studio.model.part.pivot <nom> x y z"));
		return Done(m_doc.Value().SetPivot(a[1], v.Value()), "model.part_pivot", a[1]);
	});
	console.Register("studio.model.part.rotation", "studio.model.part.rotation <nom> x y z (°)", [this](const auto &a) {
		const Option<math::FVector3> v = Vec3Args(a, 2);
		if (a.size() < 5 || v.IsNone() || !m_doc.IsSome())
			return CommandResult::Error(String("usage : studio.model.part.rotation <nom> x y z"));
		return Done(m_doc.Value().SetRotation(a[1], v.Value()), "model.part_rotate", a[1]);
	});
	console.Register("studio.model.box.add", "studio.model.box.add <partie> fx fy fz sx sy sz u v", [this](const auto &a) {
		const Option<math::FVector3> from = Vec3Args(a, 2), size = Vec3Args(a, 5);
		const Option<float> u = a.size() > 8 ? Number(a[8]) : Option<float>(NONE), v = a.size() > 9 ? Number(a[9]) : Option<float>(NONE);
		if (from.IsNone() || size.IsNone() || u.IsNone() || v.IsNone() || !m_doc.IsSome())
			return CommandResult::Error(String("usage : studio.model.box.add <partie> fx fy fz sx sy sz u v"));
		return Done(m_doc.Value().AddBox(a[1], from.Value(), size.Value(), int(u.Value()), int(v.Value())), "model.box_add", a[1]);
	});
	console.Register("studio.model.box.mirror", "studio.model.box.mirror <partie> <index>", [this](const auto &a) {
		const Option<float> i = a.size() > 2 ? Number(a[2]) : Option<float>(NONE);
		if (i.IsNone() || !m_doc.IsSome())
			return CommandResult::Error(String("usage : studio.model.box.mirror <partie> <index>"));
		return Done(m_doc.Value().MirrorBox(a[1], size_t(i.Value())), "model.box_mirror", a[1]);
	});
	console.Register("studio.model.clip.add", "studio.model.clip.add <nom> <durée> [once]", [this](const auto &a) {
		const Option<float> length = a.size() > 2 ? Number(a[2]) : Option<float>(NONE);
		if (length.IsNone() || !m_doc.IsSome())
			return CommandResult::Error(String("usage : studio.model.clip.add <nom> <durée> [once]"));
		CommandResult r = Done(m_doc.Value().AddClip(a[1], length.Value(), !(a.size() > 3 && a[3] == "once")), "model.clip_add", a[1]);
		if (r.status == CommandStatus::Ok)
			m_clip = a[1];
		return r;
	});
	console.Register("studio.model.key", "studio.model.key <clip> <partie> rotation|position <t> x y z [linear|step|smooth]",
					 [this](const auto &a) {
						 const Option<RigChannel> channel = a.size() > 3 ? ChannelOf(a[3]) : Option<RigChannel>(NONE);
						 const Option<float> t = a.size() > 4 ? Number(a[4]) : Option<float>(NONE);
						 const Option<math::FVector3> v = Vec3Args(a, 5);
						 const Option<RigInterp> interp = a.size() > 8 ? InterpOf(a[8]) : Some(RigInterp::LINEAR);
						 if (channel.IsNone() || t.IsNone() || v.IsNone() || interp.IsNone() || !m_doc.IsSome())
							 return CommandResult::Error(String("usage : studio.model.key <clip> <partie> rotation|position <t> x y z [interp]"));
						 return Done(m_doc.Value().SetKey(a[1], a[2], channel.Value(), t.Value(), v.Value(), interp.Value()), "model.key_set",
									 String::Format("%s/%s à %.2f s", a[1].CStr(), a[2].CStr(), double(t.Value())));
					 });
	console.Register("studio.model.play", "studio.model.play <clip> — lecture dans l'aperçu", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.model.play <clip>"); e.IsSome())
			return e.Value();
		return Play(a[1]);
	});
	console.Register("studio.model.time", "studio.model.time <s> — position de la tête de lecture", [this](const auto &a) {
		const Option<float> t = a.size() > 1 ? Number(a[1]) : Option<float>(NONE);
		return t.IsSome() ? SetTime(t.Value()) : CommandResult::Error(String("usage : studio.model.time <s>"));
	});
	console.Register("studio.model.export", "studio.model.export <fichier.obj> — maillage posé", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.model.export <fichier.obj>"); e.IsSome())
			return e.Value();
		return Export(a[1]);
	});
	console.Register("studio.model.key.handles",
					 "studio.model.key.handles <clip> <partie> rotation|position <t> in_t in_x in_y in_z out_t out_x out_y out_z | auto",
					 [this](const auto &a) {
						 const Option<RigChannel> channel = a.size() > 3 ? ChannelOf(a[3]) : Option<RigChannel>(NONE);
						 const Option<float> t = a.size() > 4 ? Number(a[4]) : Option<float>(NONE);
						 if (channel.IsNone() || t.IsNone() || !m_doc.IsSome())
							 return CommandResult::Error(String("usage : studio.model.key.handles <clip> <partie> <canal> <t> … | auto"));
						 if (a.size() == 6 && a[5] == "auto")
							 return Done(m_doc.Value().SetKeyHandles(a[1], a[2], channel.Value(), t.Value(), NONE, NONE), "model.key_handles", a[2]);
						 const Option<float> inT = a.size() > 5 ? Number(a[5]) : Option<float>(NONE);
						 const Option<math::FVector3> inV = Vec3Args(a, 6);
						 const Option<float> outT = a.size() > 9 ? Number(a[9]) : Option<float>(NONE);
						 const Option<math::FVector3> outV = Vec3Args(a, 10);
						 if (inT.IsNone() || inV.IsNone() || outT.IsNone() || outV.IsNone())
							 return CommandResult::Error(String("poignées : in_t in_x in_y in_z out_t out_x out_y out_z attendus"));
						 return Done(m_doc.Value().SetKeyHandles(a[1], a[2], channel.Value(), t.Value(), Some(voxel::RigHandle{inT.Value(), inV.Value()}),
																 Some(voxel::RigHandle{outT.Value(), outV.Value()})),
									 "model.key_handles", a[2]);
					 });
	console.Register("studio.model.smooth", "studio.model.smooth <clip> <partie> rotation|position — tangentes automatiques", [this](const auto &a) {
		const Option<RigChannel> channel = a.size() > 3 ? ChannelOf(a[3]) : Option<RigChannel>(NONE);
		if (channel.IsNone() || !m_doc.IsSome())
			return CommandResult::Error(String("usage : studio.model.smooth <clip> <partie> rotation|position"));
		return Done(m_doc.Value().AutoSmooth(a[1], a[2], channel.Value()), "model.curve_smooth", a[2]);
	});
	console.Register("studio.model.layer.add", "studio.model.layer.add <clip> <poids> [mix|add] [vitesse]", [this](const auto &a) {
		const Option<float> weight = a.size() > 2 ? Number(a[2]) : Option<float>(NONE);
		const Option<float> speed = a.size() > 4 ? Number(a[4]) : Some(1.f);
		if (weight.IsNone() || speed.IsNone() || !m_doc.IsSome() || (a.size() > 3 && a[3] != "mix" && a[3] != "add"))
			return CommandResult::Error(String("usage : studio.model.layer.add <clip> <poids> [mix|add] [vitesse]"));
		return Done(m_doc.Value().AddLayer({a[1], weight.Value(), speed.Value(), a.size() > 3 && a[3] == "add" ? voxel::RigBlend::ADD : voxel::RigBlend::MIX}),
					"model.layer_add", a[1]);
	});
	console.Register("studio.model.layer.remove", "studio.model.layer.remove <index>", [this](const auto &a) {
		const Option<float> i = a.size() > 1 ? Number(a[1]) : Option<float>(NONE);
		if (i.IsNone() || !m_doc.IsSome())
			return CommandResult::Error(String("usage : studio.model.layer.remove <index>"));
		return Done(m_doc.Value().RemoveLayer(size_t(i.Value())), "model.layer_remove", a[1]);
	});
	console.Register("studio.model.mix", "studio.model.mix on|off — aperçu du mélange (pose du jeu)", [this](const auto &a) {
		if (a.size() < 2 || (a[1] != "on" && a[1] != "off"))
			return CommandResult::Error(String("usage : studio.model.mix on|off"));
		m_mixPreview = a[1] == "on";
		m_playing = m_mixPreview || m_playing;
		Changed();
		if (m_tabs.Valid())
			m_tabs.SetSelected(2, true);
		Visit("model.mix_preview");
		return CommandResult::Ok(String(m_mixPreview ? "aperçu : mélange" : "aperçu : clip seul"));
	});
	console.Register("studio.model.export.gltf", "studio.model.export.gltf <fichier.gltf> — nœuds, maillages, animations", [this](const auto &a) {
		return a.size() > 1 ? ExportGltf(a[1]) : CommandResult::Error(String("usage : studio.model.export.gltf <fichier.gltf>"));
	});
	console.Register("studio.model.import.gltf", "studio.model.import.gltf <fichier.gltf> — un nœud = une partie", [this](const auto &a) {
		return a.size() > 1 ? ImportGltf(a[1]) : CommandResult::Error(String("usage : studio.model.import.gltf <fichier.gltf>"));
	});
	console.Register("studio.model.apply", "studio.model.apply — rechargement à chaud", [this](const auto &) { return Apply(); });
	console.Register("studio.model.save", "studio.model.save — enregistre dans le mod", [this](const auto &) { return Save(); });
	console.Register("studio.model.undo", "studio.model.undo", [this](const auto &) { return Undo(); });
	console.Register("studio.model.redo", "studio.model.redo", [this](const auto &) { return Redo(); });
}

} // namespace voxelia::studio
