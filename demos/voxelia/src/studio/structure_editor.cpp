// Définitions de voxelia/studio/structure_editor.hpp
#include <voxelia/studio/structure_editor.hpp>

#include <algorithm>
#include <cmath>

#include <render3d/shape.hpp>
#include <voxel/formula.hpp>
#include <voxelia/session.hpp>
#include <voxelia/shell.hpp>

namespace voxelia::studio {

using voxel::BlockPos;
using voxel::PlotSpec;

namespace {

constexpr const char *HIGHLIGHT_SELECTION = "studio:selection";
constexpr const char *HIGHLIGHT_PASTE = "studio:paste";
constexpr float CATALOG_PERIOD = 2.f;

Option<double> Number(const String &s) {
	char *end = nullptr;
	const double v = std::strtod(s.CStr(), &end);
	if (!end || end == s.CStr() || *end != '\0')
		return NONE;
	return Some(v);
}

/// Nombres séparés par des espaces (« 1 2 3 »).
std::vector<double> Numbers(const String &text) {
	std::vector<double> out;
	for (const String &word : text.Trim().Split(' '))
		if (Option<double> v = Number(word.Trim()); v.IsSome())
			out.push_back(v.Value());
	return out;
}

/// Trois entiers de `args` à partir de `first`.
Option<BlockPos> PosArgs(const std::vector<String> &args, size_t first) {
	if (args.size() < first + 3)
		return NONE;
	const Option<double> x = Number(args[first]), y = Number(args[first + 1]), z = Number(args[first + 2]);
	if (x.IsNone() || y.IsNone() || z.IsNone())
		return NONE;
	return Some(BlockPos{int(std::floor(x.Value())), int(std::floor(y.Value())), int(std::floor(z.Value()))});
}

int AxisOf(const String &s) { return s == "x" ? 0 : s == "y" ? 1 : s == "z" ? 2 : -1; }

const char *KindName(PlotSpec::Kind kind) {
	switch (kind) {
	case PlotSpec::Kind::CURVE:
		return "curve";
	case PlotSpec::Kind::SURFACE:
		return "surface";
	case PlotSpec::Kind::SOLID:
		return "solid";
	case PlotSpec::Kind::IMPLICIT:
	default:
		return "implicit";
	}
}

Option<PlotSpec::Kind> KindOf(const String &s) {
	if (s == "implicit" || s == "implicite")
		return Some(PlotSpec::Kind::IMPLICIT);
	if (s == "curve" || s == "courbe")
		return Some(PlotSpec::Kind::CURVE);
	if (s == "surface")
		return Some(PlotSpec::Kind::SURFACE);
	if (s == "solid" || s == "solide")
		return Some(PlotSpec::Kind::SOLID);
	return NONE;
}

/// Mots de `args` à partir de `first`, rejoints par des espaces (formules).
String Rest(const std::vector<String> &args, size_t first) {
	String out;
	for (size_t i = first; i < args.size(); ++i)
		out += (i > first ? String(" ") : String()) + args[i];
	return out;
}

ui::Color TokenColor(voxel::FormulaToken::Kind kind) {
	using K = voxel::FormulaToken::Kind;
	switch (kind) {
	case K::NUMBER:
		return {0.95f, 0.75f, 0.45f, 1.f};
	case K::VARIABLE:
		return {0.55f, 0.85f, 1.f, 1.f};
	case K::FUNCTION:
		return {0.75f, 0.6f, 1.f, 1.f};
	case K::CONSTANT:
		return {1.f, 0.6f, 0.75f, 1.f};
	case K::KEYWORD:
		return {0.5f, 0.95f, 0.6f, 1.f};
	case K::OPERATOR:
	case K::PAREN:
		return {0.8f, 0.82f, 0.86f, 1.f};
	case K::UNKNOWN:
	default:
		return {1.f, 0.35f, 0.35f, 1.f};
	}
}

const char *CHEAT_SHEET = "+ - * / % ^ (puissance)   < <= > >= == !=   and or not   produits implicites (« 2x », « u cos(v) »)\n"
						  "if(c, a, b)   pi e tau   implicite : condition → région pleine, expression → surface où elle s'annule";

} // namespace

StructureEditor::StructureEditor(Shell &shell, StudioReport *report, std::function<BlockPos()> placeAt)
	: m_shell(shell), m_report(report), m_placeAt(std::move(placeAt)) {
	m_spec.expression = "x^2 + y^2 + z^2 <= 6^2";
	m_spec.fx = "6 cos(t)";
	m_spec.fy = "6 sin(t)";
	m_spec.fz = "t";
	m_spec.block = "core:stone";
	m_spec.min = {-8, -8, -8};
	m_spec.max = {8, 8, 8};
}

StructureEditor::~StructureEditor() { ClearWorldMarks(); }

voxel::Game *StructureEditor::Game() {
	Session *session = m_shell.Game();
	return session && session->Ready() ? &session->Game() : nullptr;
}

void StructureEditor::Visit(const char *feature, FeatureStatus status, String detail) {
	if (m_report)
		m_report->Visit(String(feature), status, std::move(detail));
}

void StructureEditor::ClearWorldMarks() {
	if (voxel::Game *game = Game()) {
		game->ClearHighlight(String(HIGHLIGHT_SELECTION));
		game->ClearHighlight(String(HIGHLIGHT_PASTE));
	}
	if (Session *session = m_shell.Game(); session && session->Ready())
		session->Renderer().SetGhost(nullptr, BlockPos{0, 0, 0});
}

// ── Panneau ──────────────────────────────────────────────────────────────────

void StructureEditor::Build(ui::Widget parent) {
	ui::ViewportLighting lighting;
	lighting.sun.direction = math::FVector3{-0.45f, -1.f, -0.3f}.Normalize();
	lighting.sun.intensity = 1.1f;
	lighting.ambient.color = sdl3::Color{150, 150, 160};
	lighting.background = sdl3::Color{38, 42, 52, 255};
	auto highlighter = [this](const String &line, std::vector<ui::TextSpan> &out) {
		for (const voxel::FormulaToken &t : voxel::TokenizeFormula(line, StructureDocument::FormulaVariables(m_spec.kind)))
			out.push_back(ui::TextSpan{t.begin, t.end, TokenColor(t.kind)});
	};
	auto codeLine = [&](ui::Widget &ref, const String &value, const char *id) {
		return ui::CodeEditor(value, highlighter).Id(id).Height(26.f).Ref(ref);
	};
	parent.Append(ui::Column().Grow().Gap(4.f).Children({
		ui::Label("Aucune structure — capturez une région, ouvrez-en une (onglet « Structures ») ou tracez une formule.")
			.Class("studio-section")
			.Ref(m_title),
		ui::Row().Gap(4.f).Children({
			ui::Button("↻").Tooltip("Quart de tour (vertical)").OnClick([this] { (void)Rotate(1); }),
			ui::Button("Miroir X").OnClick([this] { (void)Mirror(0); }),
			ui::Button("Miroir Y").OnClick([this] { (void)Mirror(1); }),
			ui::Button("Miroir Z").OnClick([this] { (void)Mirror(2); }),
			ui::Button("Enveloppe").Tooltip("Ne garder que la coquille").OnClick([this] { (void)Hollow(-1); }),
			ui::Button("↶").Tooltip("Annuler").OnClick([this] { (void)Undo(); }),
			ui::Button("↷").Tooltip("Rétablir").OnClick([this] { (void)Redo(); }),
		}),
		ui::Viewport3D(m_previewRoot, render3d::Camera{}, true, Some(lighting)).Id("structure-preview").Height(210.f).Ref(m_viewport),
		ui::Label("").Class("studio-status").Id("structure-status").Ref(m_status),
		ui::TabView({String("Structure"), String("Formule"), String("Monde")}).Grow().Children({
			ui::ScrollView().Grow().Children({
				ui::Column().Gap(4.f).Children({
					ui::Column().Id("structure-info").Ref(m_info),
					ui::Row().Gap(4.f).Children({
						ui::Input("de (vide : tout)").Grow().Ref(m_replaceFrom),
						ui::Input("vers (vide : vider)").Grow().Ref(m_replaceTo),
						ui::Button("Remplacer").OnClick([this] { (void)Replace(m_replaceFrom.Text().Trim(), m_replaceTo.Text().Trim()); }),
					}),
					ui::Row().Gap(4.f).Children({
						ui::Input("nom, world:nom ou chemin (.vxs.json .json .we .mts)").Grow().Ref(m_saveName),
						ui::Button("Enregistrer").OnClick([this] { (void)Save(m_saveName.Text().Trim()); }),
						ui::Button("→ presse-papiers").OnClick([this] {
							(void)ToClipboard(m_doc.IsSome() ? m_doc.Value().Name() : String("studio"));
						}),
					}),
					ui::Label("Blocs").Class("studio-dock-title"),
					ui::Column().Id("structure-counts").Ref(m_counts),
				}),
			}),
			ui::ScrollView().Grow().Children({
				ui::Column().Gap(4.f).Children({
					ui::Combo({String("Implicite f(x, y, z)"), String("Courbe (t)"), String("Surface (u, v)"), String("Solide (u, v, w)")},
							  int(m_spec.kind))
						.OnSelect([this](int i) {
							m_spec.kind = PlotSpec::Kind(std::clamp(i, 0, 3));
							Visit("structure.formula_kind", FeatureStatus::Ok, String(KindName(m_spec.kind)));
						}),
					ui::Label("Implicite").Class("studio-status"),
					codeLine(m_formula, m_spec.expression, "structure-formula"),
					ui::Label("Paramétrique : x, y, z").Class("studio-status"),
					codeLine(m_fx, m_spec.fx, "structure-fx"),
					codeLine(m_fy, m_spec.fy, "structure-fy"),
					codeLine(m_fz, m_spec.fz, "structure-fz"),
					ui::Row().Gap(4.f).Children({
						ui::Input("bloc", m_spec.block).Grow().Ref(m_block),
						ui::Input("échelle", String("1")).Width(60.f).Ref(m_scale),
						ui::Checkbox("z vers le haut", m_spec.zUp).OnToggle([this](bool on) { m_spec.zUp = on; }),
						ui::Checkbox("enveloppe", m_spec.hollow).OnToggle([this](bool on) { m_spec.hollow = on; }),
					}),
					ui::Input("boîte (implicite) : xmin ymin zmin xmax ymax zmax", String("-8 -8 -8 8 8 8")).Ref(m_bounds),
					ui::Input("plages : t0 t1 [u…] (paramétrique)", String("0 6.2832 0 6.2832 0 1")).Ref(m_ranges),
					ui::Row().Gap(4.f).Children({
						ui::Button("Vérifier").OnClick([this] {
							ReadFormulaFields();
							(void)CheckFormula();
						}),
						ui::Button("Tracer").OnClick([this] {
							ReadFormulaFields();
							(void)Plot();
						}),
					}),
					ui::Label("").Id("structure-formula-status").Ref(m_formulaStatus),
					ui::Label(String(CHEAT_SHEET)).Class("studio-status"),
				}),
			}),
			ui::ScrollView().Grow().Children({
				ui::Column().Gap(4.f).Children({
					ui::Label("").Id("structure-selection").Ref(m_selection),
					ui::Row().Gap(4.f).Children({
						ui::Button("Coin A ici").OnClick([this] { (void)SetCorner(0, NONE); }),
						ui::Button("Coin B ici").OnClick([this] { (void)SetCorner(1, NONE); }),
						ui::Button("Capturer").OnClick([this] { (void)Capture(); }),
					}),
					ui::Row().Gap(4.f).Children({
						ui::Input("bloc", String("core:stone")).Grow().Ref(m_fillBlock),
						ui::Combo({String("all"), String("hollow"), String("walls"), String("outline"), String("keep")}, 0)
							.OnSelect([this](int i) {
								static const char *MODES[] = {"all", "hollow", "walls", "outline", "keep"};
								(void)Fill(m_fillBlock.Text().Trim(), String(MODES[std::clamp(i, 0, 4)]));
							}),
						ui::Button("Remplir").OnClick([this] { (void)Fill(m_fillBlock.Text().Trim(), String("all")); }),
					}),
					ui::Label("Collage au bloc visé").Class("studio-dock-title"),
					ui::Row().Gap(4.f).Children({
						ui::Combo({String("0°"), String("90°"), String("180°"), String("270°")}, 0).OnSelect([this](int i) {
							paste.rotation = i;
						}),
						ui::Combo({String("air posé"), String("air ignoré (fusion)"), String("seulement dans l'air")}, 0).OnSelect([this](int i) {
							paste.keepAir = i == 0;
							paste.onlyAir = i == 2;
						}),
						ui::Checkbox("fantôme", false).OnToggle([this](bool on) { (void)ShowGhost(on); }),
					}),
					ui::Row().Gap(4.f).Children({
						ui::Button("Coller").OnClick([this] { (void)Paste(NONE); }),
						ui::Button("Annuler le collage").OnClick([this] { (void)UndoPaste(); }),
					}),
				}),
			}),
		}),
	}));
	RefreshSelection();
}

void StructureEditor::BuildCatalog(ui::Widget parent) {
	parent.Append(ui::Column().Grow().Gap(4.f).Children({
		ui::Row().Gap(4.f).Children({
			ui::Label("Structures").Class("studio-dock-title").Grow(),
			ui::Button("+").Tooltip("Nouvelle structure 8 × 8 × 8").OnClick([this] { (void)New(BlockPos{8, 8, 8}, String("structure")); }),
			ui::Button("⟳").Tooltip("Relire les dossiers").OnClick([this] { RebuildCatalog(); }),
		}),
		ui::ScrollView().Id("structure-catalog").Grow().Ref(m_catalog),
	}));
	RebuildCatalog();
}

void StructureEditor::RebuildCatalog() {
	if (!m_catalog.Valid())
		return;
	m_catalog.Clear();
	voxel::Game *game = Game();
	if (!game) {
		m_catalog.Append(ui::Label("aucune partie en cours").Class("studio-status"));
		return;
	}
	auto section = [&](const char *title, const std::vector<String> &names, const char *prefix) {
		m_catalog.Append(ui::Label(String(title)).Class("studio-section"));
		if (names.empty())
			m_catalog.Append(ui::Label("  (vide)").Class("studio-status"));
		for (const String &name : names) {
			const String what = String(prefix) + name;
			m_catalog.Append(ui::Label(String("  ") + name).Class("studio-row").OnClick([this, what] { (void)Open(what); }));
		}
	};
	section("Bibliothèque (entre mondes)", game->StructureFiles(true), "");
	section("Ce monde", game->StructureFiles(false), "world:");
	section("Presse-papiers", game->ClipboardNames(), "clipboard:");
}

// ── Rafraîchissements ────────────────────────────────────────────────────────

void StructureEditor::Update(float dt) {
	m_catalogTimer += dt;
	if (m_catalogTimer >= CATALOG_PERIOD) { // fichiers et presse-papiers changent hors du studio
		m_catalogTimer = 0.f;
		RebuildCatalog();
	}
	if (m_doc.IsSome() && m_doc.Value().Version() != m_shownVersion)
		RebuildInfo();
	if (m_doc.IsSome() && m_doc.Value().Version() != m_previewVersion)
		RebuildPreview();
	RefreshGhost();
	m_orbit += dt * 0.3f;
	if (auto vp = m_viewport.Get<ui::Viewport3DState>(); vp.IsSome()) {
		const math::FVector3 s = m_previewSize;
		const float r = std::max({s.x, s.y, s.z, 2.f});
		render3d::Camera camera;
		camera.target = {0.f, s.y * 0.5f, 0.f};
		camera.position = {std::sin(m_orbit) * r * 1.5f, s.y * 0.5f + r * 0.8f, std::cos(m_orbit) * r * 1.5f};
		camera.farPlane = r * 8.f + 50.f;
		if (vp.Unwrap()->textureHeight > 0)
			camera.aspect = float(vp.Unwrap()->textureWidth) / float(vp.Unwrap()->textureHeight);
		vp.Unwrap()->camera = camera;
	}
}

void StructureEditor::RebuildInfo() {
	const StructureDocument &doc = m_doc.Value();
	m_shownVersion = doc.Version();
	const voxel::Schematic &s = doc.Schematic();
	m_title.SetText(doc.Name() + (doc.Dirty() ? String(" ●") : String()));
	m_info.Clear();
	m_info.Append(ui::Label(String::Format("%d × %d × %d — %lld cellule(s), %lld bloc(s), ancre %d %d %d", s.Size().x, s.Size().y, s.Size().z,
										   (long long)s.Filled(), (long long)s.Solid(), s.anchor.x, s.anchor.y, s.anchor.z)));
	m_info.Append(ui::Label(String::Format("annulations : %zu · rétablissements : %zu", doc.UndoDepth(), doc.RedoDepth())).Class("studio-status"));
	m_counts.Clear();
	for (const auto &[block, count] : doc.Counts())
		m_counts.Append(ui::Label(String::Format("  %s × %lld", block.CStr(), (long long)count)).Class("studio-row").OnClick([this, block] {
			m_replaceFrom.SetText(block);
		}));
}

void StructureEditor::RebuildPreview() {
	m_previewVersion = m_doc.Value().Version();
	while (!m_previewRoot.Children().empty())
		(void)m_previewRoot.RemoveChild(m_previewRoot.Children().front().get());
	m_previewTriangles = 0;
	Session *session = m_shell.Game();
	if (!session || !session->Ready())
		return;
	const voxel::Schematic &s = m_doc.Value().Schematic();
	m_previewSize = {float(s.Size().x), float(s.Size().y), float(s.Size().z)};
	for (WorldRenderer::MeshPart &part : session->Renderer().StructureMeshes(s)) {
		m_previewTriangles += part.mesh->IndexCount() / 3;
		auto shape = std::make_unique<render3d::Shape>(part.mesh, part.material);
		shape->SetPosition({-m_previewSize.x * 0.5f, 0.f, -m_previewSize.z * 0.5f}); // centrée sur l'origine
		m_previewRoot.Add(std::move(shape));
	}
	Visit("structure.preview", m_previewTriangles > 0 || s.Solid() == 0 ? FeatureStatus::Ok : FeatureStatus::Warning,
		  String::Format("%zu triangle(s)", m_previewTriangles));
}

void StructureEditor::RefreshSelection() {
	voxel::Game *game = Game();
	String text("Sélection : ");
	for (int i = 0; i < 2; ++i) {
		text += String(i == 0 ? "A " : "  B ");
		text += m_corners[size_t(i)].IsSome()
					? String::Format("%d %d %d", m_corners[size_t(i)].Value().x, m_corners[size_t(i)].Value().y, m_corners[size_t(i)].Value().z)
					: String("—");
	}
	if (m_selection.Valid())
		m_selection.SetText(text);
	if (!game)
		return;
	if (m_corners[0].IsSome() && m_corners[1].IsSome())
		game->SetHighlight(String(HIGHLIGHT_SELECTION), m_corners[0].Value(), m_corners[1].Value(), {90, 200, 255, 255});
	else if (m_corners[0].IsSome() || m_corners[1].IsSome()) {
		const BlockPos one = m_corners[0].IsSome() ? m_corners[0].Value() : m_corners[1].Value();
		game->SetHighlight(String(HIGHLIGHT_SELECTION), one, one, {90, 200, 255, 255});
	} else {
		game->ClearHighlight(String(HIGHLIGHT_SELECTION));
	}
}

void StructureEditor::RefreshGhost() {
	Session *session = m_shell.Game();
	voxel::Game *game = Game();
	if (!session || !game)
		return;
	if (!m_ghost || !m_doc.IsSome() || m_doc.Value().Schematic().Empty()) {
		if (session->Renderer().HasGhost())
			session->Renderer().SetGhost(nullptr, BlockPos{0, 0, 0});
		game->ClearHighlight(String(HIGHLIGHT_PASTE));
		return;
	}
	const voxel::Schematic &s = m_doc.Value().Schematic();
	if (!m_ghostShape || m_ghostVersion != m_doc.Value().Version() || m_ghostRotation != paste.rotation) {
		m_ghostShape = std::make_shared<const voxel::Schematic>(s.Rotated(paste.rotation));
		m_ghostVersion = m_doc.Value().Version();
		m_ghostRotation = paste.rotation;
	}
	const auto [lo, hi] = s.Bounds(m_placeAt(), paste.rotation);
	session->Renderer().SetGhost(m_ghostShape, lo);
	game->SetHighlight(String(HIGHLIGHT_PASTE), lo, hi, {255, 210, 90, 255});
}

// ── Document ─────────────────────────────────────────────────────────────────

CommandResult StructureEditor::Opened(StructureDocument doc, const String &feature, const String &detail) {
	m_doc = Some(std::move(doc));
	m_shownVersion = m_previewVersion = ~0ull;
	m_ghostShape.reset();
	if (m_saveName.Valid())
		m_saveName.SetText(m_doc.Value().Name());
	if (onDocumentOpened)
		onDocumentOpened();
	Visit(feature.CStr(), FeatureStatus::Ok, detail);
	const voxel::Schematic &s = m_doc.Value().Schematic();
	return CommandResult::Ok(String::Format("%s : %d × %d × %d, %lld bloc(s)", m_doc.Value().Name().CStr(), s.Size().x, s.Size().y,
											s.Size().z, (long long)s.Solid()));
}

CommandResult StructureEditor::Done(Result<bool, String> result, const char *feature, const String &ok) {
	if (result.IsError()) {
		Visit(feature, FeatureStatus::Warning, result.Error());
		if (m_status.Valid())
			m_status.SetText(result.Error());
		return CommandResult::Error(result.Error());
	}
	Visit(feature, FeatureStatus::Ok, ok);
	if (m_status.Valid())
		m_status.SetText(ok);
	return CommandResult::Ok(ok);
}

CommandResult StructureEditor::New(const BlockPos &size, const String &name) {
	if (size.x <= 0 || size.y <= 0 || size.z <= 0 || int64_t(size.x) * size.y * size.z > 256ll * 256 * 256)
		return CommandResult::Error(String("taille invalide (1 à 256 par côté)"));
	voxel::Schematic s(size);
	s.name = name;
	return Opened(StructureDocument(std::move(s), name), String("structure.new"), name);
}

String StructureEditor::LibraryPath(const String &name, bool exchange) const {
	Session *session = m_shell.Game();
	const String dir = session && session->Ready() ? session->Game().StructuresDirectory(exchange) : String();
	const String file = voxel::Schematic::FormatOf(name).IsEmpty() ? name + String(".vxs.json") : name;
	return dir.IsEmpty() ? file : dir + String("/") + file;
}

CommandResult StructureEditor::Open(const String &what) {
	voxel::Game *game = Game();
	if (!game)
		return CommandResult::Error(String("aucune partie en cours"));
	if (what.StartsWith("clipboard:")) {
		const String name = what.Substring(10);
		const voxel::Schematic *clip = game->Clipboard(name);
		if (!clip)
			return CommandResult::Error(String("presse-papiers inconnu : ") + name);
		return Opened(StructureDocument(*clip, name), String("structure.open_clipboard"), name);
	}
	const bool world = what.StartsWith("world:");
	const String name = world ? what.Substring(6) : what;
	const String path = name.Contains("/") ? name : LibraryPath(name, !world);
	std::vector<String> unknown;
	Result<StructureDocument, String> doc = StructureDocument::Load(path, game->Content(), &unknown);
	if (doc.IsError())
		return CommandResult::Error(doc.Error());
	CommandResult r = Opened(std::move(doc).Unwrap(), String("structure.open"), path);
	if (!unknown.empty()) {
		String list;
		for (const String &u : unknown)
			list += (list.IsEmpty() ? String() : String(", ")) + u;
		Visit("structure.unknown_blocks", FeatureStatus::Warning, list);
		return CommandResult::Warning(r.message + String(" — blocs inconnus vidés : ") + list);
	}
	return r;
}

CommandResult StructureEditor::Save(const String &target) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune structure ouverte"));
	const String name = target.IsEmpty() ? m_doc.Value().Name() : target;
	const bool world = name.StartsWith("world:");
	const String bare = world ? name.Substring(6) : name;
	const String path = bare.Contains("/") ? bare : LibraryPath(bare, !world);
	Result<String, String> saved = m_doc.Value().Save(path);
	if (saved.IsError()) {
		Visit("structure.save", FeatureStatus::Warning, saved.Error());
		return CommandResult::Error(saved.Error());
	}
	RebuildCatalog();
	m_shownVersion = ~0ull; // le titre perd son « ● »
	Visit("structure.save", FeatureStatus::Ok, saved.Value());
	return CommandResult::Ok(String("enregistrée : ") + saved.Value());
}

CommandResult StructureEditor::ToClipboard(const String &name) {
	voxel::Game *game = Game();
	if (!game || !m_doc.IsSome())
		return CommandResult::Error(String("aucune structure ouverte"));
	game->SetClipboard(name, m_doc.Value().Schematic());
	RebuildCatalog();
	Visit("structure.clipboard", FeatureStatus::Ok, name);
	return CommandResult::Ok(String("presse-papiers : ") + name);
}

CommandResult StructureEditor::Rotate(int quarters) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune structure ouverte"));
	m_doc.Value().Rotate(quarters);
	return Done(Ok(true), "structure.rotate", String::Format("%d quart(s) de tour", quarters));
}

CommandResult StructureEditor::Mirror(int axis) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune structure ouverte"));
	return Done(m_doc.Value().Mirror(axis), "structure.mirror", String("miroir ") + String(axis == 0 ? "x" : axis == 1 ? "y" : "z"));
}

CommandResult StructureEditor::Hollow(int flatAxis) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune structure ouverte"));
	return Done(m_doc.Value().Hollow(flatAxis), "structure.hollow", String("enveloppe"));
}

CommandResult StructureEditor::Replace(const String &from, const String &to) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune structure ouverte"));
	if (voxel::Game *game = Game(); game && !to.IsEmpty() && !game->Content().FindBlock(to))
		return CommandResult::Error(String("bloc inconnu : ") + to);
	Result<int64_t, String> n = m_doc.Value().ReplaceBlock(from, to);
	if (n.IsError())
		return CommandResult::Error(n.Error());
	return Done(Ok(true), "structure.replace", String::Format("%lld cellule(s) remplacée(s)", (long long)n.Value()));
}

CommandResult StructureEditor::SetCell(const BlockPos &p, const String &block) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune structure ouverte"));
	if (voxel::Game *game = Game(); game && !block.IsEmpty() && !game->Content().FindBlock(block))
		return CommandResult::Error(String("bloc inconnu : ") + block);
	return Done(m_doc.Value().SetCell(p, block), "structure.set_cell", String::Format("%d %d %d", p.x, p.y, p.z));
}

CommandResult StructureEditor::SetAnchor(const BlockPos &p) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune structure ouverte"));
	return Done(m_doc.Value().SetAnchor(p), "structure.anchor", String::Format("ancre %d %d %d", p.x, p.y, p.z));
}

CommandResult StructureEditor::Undo() {
	if (!m_doc.IsSome() || !m_doc.Value().Undo())
		return CommandResult::Warning(String("rien à annuler"));
	Visit("structure.undo");
	return CommandResult::Ok(String("annulé"));
}

CommandResult StructureEditor::Redo() {
	if (!m_doc.IsSome() || !m_doc.Value().Redo())
		return CommandResult::Warning(String("rien à rétablir"));
	Visit("structure.redo");
	return CommandResult::Ok(String("rétabli"));
}

// ── Formule ──────────────────────────────────────────────────────────────────

void StructureEditor::ReadFormulaFields() {
	if (!m_formula.Valid())
		return;
	m_spec.expression = m_formula.Text().Trim();
	m_spec.fx = m_fx.Text().Trim();
	m_spec.fy = m_fy.Text().Trim();
	m_spec.fz = m_fz.Text().Trim();
	m_spec.block = m_block.Text().Trim();
	if (Option<double> scale = Number(m_scale.Text().Trim()); scale.IsSome() && scale.Value() > 0.0)
		m_spec.scale = scale.Value();
	if (const std::vector<double> b = Numbers(m_bounds.Text()); b.size() >= 6) {
		m_spec.min = {int(b[0]), int(b[1]), int(b[2])};
		m_spec.max = {int(b[3]), int(b[4]), int(b[5])};
	}
	const std::vector<double> r = Numbers(m_ranges.Text());
	for (size_t i = 0; i + 1 < r.size() && i / 2 < 3; i += 2) {
		m_spec.range[i / 2][0] = r[i];
		m_spec.range[i / 2][1] = r[i + 1];
	}
}

CommandResult StructureEditor::CheckFormula() {
	Result<bool, String> checked = StructureDocument::CheckFormula(m_spec);
	if (m_formulaStatus.Valid())
		m_formulaStatus.SetText(checked.IsOk() ? String("✓ formule correcte") : String("✗ ") + checked.Error());
	Visit("structure.formula_check", checked.IsOk() ? FeatureStatus::Ok : FeatureStatus::Warning,
		  checked.IsOk() ? String(KindName(m_spec.kind)) : checked.Error());
	return checked.IsOk() ? CommandResult::Ok(String("formule correcte")) : CommandResult::Error(checked.Error());
}

CommandResult StructureEditor::Plot() {
	voxel::Game *game = Game();
	if (game && !game->Content().FindBlock(m_spec.block))
		return CommandResult::Error(String("bloc inconnu : ") + m_spec.block);
	if (!m_doc.IsSome())
		m_doc = Some(StructureDocument(voxel::Schematic(), String("formule")));
	Result<int64_t, String> plotted = m_doc.Value().Plot(m_spec);
	if (plotted.IsError()) {
		if (m_formulaStatus.Valid())
			m_formulaStatus.SetText(String("✗ ") + plotted.Error());
		Visit("structure.plot", FeatureStatus::Warning, plotted.Error());
		return CommandResult::Error(plotted.Error());
	}
	const voxel::Schematic &s = m_doc.Value().Schematic();
	const String ok = String::Format("%s : %lld bloc(s), %d × %d × %d", KindName(m_spec.kind), (long long)plotted.Value(), s.Size().x,
									 s.Size().y, s.Size().z);
	if (m_formulaStatus.Valid())
		m_formulaStatus.SetText(String("✓ ") + ok);
	if (onDocumentOpened)
		onDocumentOpened();
	Visit("structure.plot", FeatureStatus::Ok, ok);
	return CommandResult::Ok(ok);
}

// ── Monde ────────────────────────────────────────────────────────────────────

CommandResult StructureEditor::SetCorner(int corner, Option<BlockPos> at) {
	if (!Game())
		return CommandResult::Error(String("aucune partie en cours"));
	const BlockPos p = at.IsSome() ? at.Value() : m_placeAt();
	m_corners[size_t(corner & 1)] = Some(p);
	RefreshSelection();
	Visit("structure.corner");
	return CommandResult::Ok(String::Format("coin %c : %d %d %d", corner == 0 ? 'A' : 'B', p.x, p.y, p.z));
}

CommandResult StructureEditor::Capture() {
	voxel::Game *game = Game();
	if (!game)
		return CommandResult::Error(String("aucune partie en cours"));
	if (m_corners[0].IsNone() || m_corners[1].IsNone())
		return CommandResult::Error(String("coins A et B à placer d'abord"));
	voxel::Schematic s = game->CaptureRegion(m_corners[0].Value(), m_corners[1].Value());
	s.name = "capture";
	if (m_doc.IsSome()) { // la capture devient une étape du document ouvert
		m_doc.Value().Replace(std::move(s));
		Visit("structure.capture");
		const voxel::Schematic &c = m_doc.Value().Schematic();
		return CommandResult::Ok(String::Format("capturée : %d × %d × %d", c.Size().x, c.Size().y, c.Size().z));
	}
	return Opened(StructureDocument(std::move(s), String("capture")), String("structure.capture"), String());
}

CommandResult StructureEditor::Fill(const String &block, const String &mode) {
	voxel::Game *game = Game();
	if (!game)
		return CommandResult::Error(String("aucune partie en cours"));
	if (m_corners[0].IsNone() || m_corners[1].IsNone())
		return CommandResult::Error(String("coins A et B à placer d'abord"));
	Result<int64_t, String> n = game->FillRegion(m_corners[0].Value(), m_corners[1].Value(), block, mode);
	if (n.IsError())
		return CommandResult::Error(n.Error());
	Visit("structure.fill", FeatureStatus::Ok, mode);
	return CommandResult::Ok(String::Format("%lld bloc(s) posé(s) (%s)", (long long)n.Value(), mode.CStr()));
}

CommandResult StructureEditor::ReplaceRegion(const String &from, const String &to) {
	voxel::Game *game = Game();
	if (!game)
		return CommandResult::Error(String("aucune partie en cours"));
	if (m_corners[0].IsNone() || m_corners[1].IsNone())
		return CommandResult::Error(String("coins A et B à placer d'abord"));
	Result<int64_t, String> n = game->ReplaceRegion(m_corners[0].Value(), m_corners[1].Value(), from, to);
	if (n.IsError())
		return CommandResult::Error(n.Error());
	Visit("structure.replace_region");
	return CommandResult::Ok(String::Format("%lld bloc(s) remplacé(s)", (long long)n.Value()));
}

CommandResult StructureEditor::Paste(Option<BlockPos> at) {
	voxel::Game *game = Game();
	if (!game)
		return CommandResult::Error(String("aucune partie en cours"));
	if (!m_doc.IsSome() || m_doc.Value().Schematic().Empty())
		return CommandResult::Error(String("aucune structure à coller"));
	const voxel::Schematic &s = m_doc.Value().Schematic();
	const BlockPos where = at.IsSome() ? at.Value() : m_placeAt();
	// Ce qui sera écrasé, pour « annuler le collage ».
	const auto [lo, hi] = s.Bounds(where, paste.rotation);
	m_pasteUndo = Some(std::make_pair(game->CaptureRegion(lo, hi), lo));
	const int64_t changed = game->PasteSchematic(s, where, paste);
	Visit("structure.paste", FeatureStatus::Ok, String::Format("%lld cellule(s)", (long long)changed));
	return CommandResult::Ok(String::Format("collée en %d %d %d : %lld cellule(s) modifiée(s)", where.x, where.y, where.z, (long long)changed));
}

CommandResult StructureEditor::UndoPaste() {
	voxel::Game *game = Game();
	if (!game || m_pasteUndo.IsNone())
		return CommandResult::Warning(String("aucun collage à annuler"));
	voxel::Schematic::PasteOptions restore;
	restore.keepAir = true;
	const int64_t changed = game->PasteSchematic(m_pasteUndo.Value().first, m_pasteUndo.Value().second, restore);
	m_pasteUndo = NONE;
	Visit("structure.paste_undo");
	return CommandResult::Ok(String::Format("collage annulé (%lld cellule(s))", (long long)changed));
}

CommandResult StructureEditor::ShowGhost(bool on) {
	m_ghost = on;
	RefreshGhost();
	Visit("structure.ghost", FeatureStatus::Ok, String(on ? "affiché" : "masqué"));
	return CommandResult::Ok(String(on ? "fantôme affiché" : "fantôme masqué"));
}

// ── Console ──────────────────────────────────────────────────────────────────

void StructureEditor::RegisterCommands(ConsoleModel &console) {
	auto need = [](const std::vector<String> &args, size_t n, const char *usage) -> Option<CommandResult> {
		if (args.size() < n)
			return Some(CommandResult::Error(String("usage : ") + String(usage)));
		return NONE;
	};
	console.Register("studio.structure.new", "studio.structure.new <x> <y> <z> [nom] — structure vide", [this, need](const auto &a) {
		if (auto e = need(a, 4, "studio.structure.new <x> <y> <z> [nom]"); e.IsSome())
			return e.Value();
		const Option<BlockPos> size = PosArgs(a, 1);
		if (size.IsNone())
			return CommandResult::Error(String("taille invalide"));
		return New(size.Value(), a.size() > 4 ? a[4] : String("structure"));
	});
	console.Register("studio.structure.open", "studio.structure.open <nom | world:nom | clipboard:nom | chemin>", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.structure.open <nom>"); e.IsSome())
			return e.Value();
		return Open(a[1]);
	});
	console.Register("studio.structure.save", "studio.structure.save [nom | world:nom | chemin.(vxs.json|json|we|mts)]",
					 [this](const auto &a) { return Save(a.size() > 1 ? a[1] : String()); });
	console.Register("studio.structure.clipboard", "studio.structure.clipboard <nom> — copie dans un presse-papiers", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.structure.clipboard <nom>"); e.IsSome())
			return e.Value();
		return ToClipboard(a[1]);
	});
	console.Register("studio.structure.rotate", "studio.structure.rotate [quarts]", [this](const auto &a) {
		const Option<double> n = a.size() > 1 ? Number(a[1]) : Some(1.0);
		return n.IsSome() ? Rotate(int(n.Value())) : CommandResult::Error(String("nombre de quarts attendu"));
	});
	console.Register("studio.structure.mirror", "studio.structure.mirror x|y|z", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.structure.mirror x|y|z"); e.IsSome())
			return e.Value();
		return Mirror(AxisOf(a[1]));
	});
	console.Register("studio.structure.hollow", "studio.structure.hollow [x|y|z] — enveloppe (plate selon l'axe)", [this](const auto &a) {
		return Hollow(a.size() > 1 ? AxisOf(a[1]) : -1);
	});
	console.Register("studio.structure.replace", "studio.structure.replace <de|*> <vers|-> — dans la structure", [this, need](const auto &a) {
		if (auto e = need(a, 3, "studio.structure.replace <de|*> <vers|->"); e.IsSome())
			return e.Value();
		return Replace(a[1] == "*" ? String() : a[1], a[2] == "-" ? String() : a[2]);
	});
	console.Register("studio.structure.set", "studio.structure.set <x> <y> <z> <bloc|-> — une cellule", [this, need](const auto &a) {
		if (auto e = need(a, 5, "studio.structure.set <x> <y> <z> <bloc|->"); e.IsSome())
			return e.Value();
		const Option<BlockPos> p = PosArgs(a, 1);
		return p.IsSome() ? SetCell(p.Value(), a[4] == "-" ? String() : a[4]) : CommandResult::Error(String("cellule invalide"));
	});
	console.Register("studio.structure.anchor", "studio.structure.anchor <x> <y> <z>", [this, need](const auto &a) {
		if (auto e = need(a, 4, "studio.structure.anchor <x> <y> <z>"); e.IsSome())
			return e.Value();
		const Option<BlockPos> p = PosArgs(a, 1);
		return p.IsSome() ? SetAnchor(p.Value()) : CommandResult::Error(String("cellule invalide"));
	});
	console.Register("studio.structure.undo", "studio.structure.undo", [this](const auto &) { return Undo(); });
	console.Register("studio.structure.redo", "studio.structure.redo", [this](const auto &) { return Redo(); });
	console.Register("studio.structure.formula", "studio.structure.formula <kind|block|scale|zup|hollow|bounds|range|x|y|z|expr> <valeur…>",
					 [this, need](const auto &a) {
						 if (auto e = need(a, 3, "studio.structure.formula <clé> <valeur…>"); e.IsSome())
							 return e.Value();
						 const String key = a[1], value = Rest(a, 2);
						 const std::vector<double> numbers = Numbers(value);
						 if (key == "kind") {
							 const Option<PlotSpec::Kind> kind = KindOf(value);
							 if (kind.IsNone())
								 return CommandResult::Error(String("implicit, curve, surface ou solid"));
							 m_spec.kind = kind.Value();
						 } else if (key == "block") {
							 m_spec.block = value;
						 } else if (key == "scale" && !numbers.empty() && numbers[0] > 0.0) {
							 m_spec.scale = numbers[0];
						 } else if (key == "zup") {
							 m_spec.zUp = value == "true" || value == "1" || value == "oui";
						 } else if (key == "hollow") {
							 m_spec.hollow = value == "true" || value == "1" || value == "oui";
						 } else if (key == "bounds" && numbers.size() >= 6) {
							 m_spec.min = {int(numbers[0]), int(numbers[1]), int(numbers[2])};
							 m_spec.max = {int(numbers[3]), int(numbers[4]), int(numbers[5])};
						 } else if (key == "range" && numbers.size() >= 3 && numbers[0] >= 0.0 && numbers[0] < 3.0) {
							 m_spec.range[int(numbers[0])][0] = numbers[1];
							 m_spec.range[int(numbers[0])][1] = numbers[2];
						 } else if (key == "samples" && numbers.size() >= 2 && numbers[0] >= 0.0 && numbers[0] < 3.0) {
							 m_spec.samples[int(numbers[0])] = std::max(2, int(numbers[1]));
						 } else if (key == "x" || key == "y" || key == "z" || key == "expr") {
							 (key == "x" ? m_spec.fx : key == "y" ? m_spec.fy : key == "z" ? m_spec.fz : m_spec.expression) = value;
						 } else {
							 return CommandResult::Error(String("clé ou valeur invalide : ") + key);
						 }
						 // Le formulaire montre ce que la console a réglé.
						 if (m_formula.Valid()) {
							 m_formula.SetText(m_spec.expression);
							 m_fx.SetText(m_spec.fx);
							 m_fy.SetText(m_spec.fy);
							 m_fz.SetText(m_spec.fz);
							 m_block.SetText(m_spec.block);
						 }
						 Visit("structure.formula_set", FeatureStatus::Ok, key);
						 return CommandResult::Ok(key + String(" = ") + value);
					 });
	console.Register("studio.structure.check", "studio.structure.check — vérifie la formule", [this](const auto &) { return CheckFormula(); });
	console.Register("studio.structure.plot", "studio.structure.plot [formule implicite…] — trace", [this](const auto &a) {
		if (a.size() > 1) {
			m_spec.kind = PlotSpec::Kind::IMPLICIT;
			m_spec.expression = Rest(a, 1);
			if (m_formula.Valid())
				m_formula.SetText(m_spec.expression);
		}
		return Plot();
	});
	console.Register("studio.structure.corner", "studio.structure.corner a|b [x y z] — coin de la sélection (sinon : bloc visé)",
					 [this, need](const auto &a) {
						 if (auto e = need(a, 2, "studio.structure.corner a|b [x y z]"); e.IsSome())
							 return e.Value();
						 const int corner = a[1] == "b" || a[1] == "B" ? 1 : 0;
						 return SetCorner(corner, PosArgs(a, 2));
					 });
	console.Register("studio.structure.capture", "studio.structure.capture [x1 y1 z1 x2 y2 z2] — copie la sélection", [this](const auto &a) {
		if (a.size() >= 7) {
			const Option<BlockPos> p = PosArgs(a, 1), q = PosArgs(a, 4);
			if (p.IsNone() || q.IsNone())
				return CommandResult::Error(String("coins invalides"));
			m_corners = {p, q};
			RefreshSelection();
		}
		return Capture();
	});
	console.Register("studio.structure.fill", "studio.structure.fill <bloc> [all|hollow|walls|outline|keep] — remplit la sélection",
					 [this, need](const auto &a) {
						 if (auto e = need(a, 2, "studio.structure.fill <bloc> [mode]"); e.IsSome())
							 return e.Value();
						 return Fill(a[1], a.size() > 2 ? a[2] : String("all"));
					 });
	console.Register("studio.structure.replace_region", "studio.structure.replace_region <de|*> <vers> — dans la sélection",
					 [this, need](const auto &a) {
						 if (auto e = need(a, 3, "studio.structure.replace_region <de|*> <vers>"); e.IsSome())
							 return e.Value();
						 return ReplaceRegion(a[1] == "*" ? String() : a[1], a[2]);
					 });
	console.Register("studio.structure.paste", "studio.structure.paste [x y z] — colle (sinon : au bloc visé)",
					 [this](const auto &a) { return Paste(PosArgs(a, 1)); });
	console.Register("studio.structure.paste.options", "studio.structure.paste.options <rotation 0..3> <keep|merge|only>",
					 [this, need](const auto &a) {
						 if (auto e = need(a, 3, "studio.structure.paste.options <rotation> <keep|merge|only>"); e.IsSome())
							 return e.Value();
						 const Option<double> r = Number(a[1]);
						 if (r.IsNone() || (a[2] != "keep" && a[2] != "merge" && a[2] != "only"))
							 return CommandResult::Error(String("rotation 0..3 et keep|merge|only attendus"));
						 paste.rotation = ((int(r.Value()) % 4) + 4) % 4;
						 paste.keepAir = a[2] == "keep";
						 paste.onlyAir = a[2] == "only";
						 return CommandResult::Ok(String::Format("rotation %d, %s", paste.rotation, a[2].CStr()));
					 });
	console.Register("studio.structure.paste.undo", "studio.structure.paste.undo — annule le dernier collage", [this](const auto &) { return UndoPaste(); });
	console.Register("studio.structure.ghost", "studio.structure.ghost on|off — fantôme du collage au bloc visé", [this](const auto &a) {
		return ShowGhost(a.size() < 2 || a[1] == "on" || a[1] == "oui");
	});
	console.Register("studio.structure.info", "studio.structure.info — taille, blocs", [this](const auto &) {
		if (!m_doc.IsSome())
			return CommandResult::Error(String("aucune structure ouverte"));
		const voxel::Schematic &s = m_doc.Value().Schematic();
		String text = String::Format("%s : %d × %d × %d, %lld bloc(s)", m_doc.Value().Name().CStr(), s.Size().x, s.Size().y, s.Size().z,
									 (long long)s.Solid());
		for (const auto &[block, count] : m_doc.Value().Counts())
			text += String::Format("\n  %s × %lld", block.CStr(), (long long)count);
		return CommandResult::Ok(text);
	});
}

} // namespace voxelia::studio
