// Définitions de voxelia/studio/block_editor.hpp
#include <voxelia/studio/block_editor.hpp>

#include <algorithm>
#include <cstring>
#include <set>

#include <voxel/json.hpp>
#include <voxelia/session.hpp>
#include <voxelia/shell.hpp>

namespace voxelia::studio {

namespace json = voxel::json;
using dataio::NodePtr;

namespace {

constexpr float GESTURE_SECONDS = 0.4f;  ///< sans changement : fin du geste
constexpr float VALIDATE_SECONDS = 0.3f; ///< validation après la dernière frappe

enum class FieldKind : uint8_t { Text, Json, Number, Bool, Choice };

struct FieldSpec {
	const char *section;
	const char *label;
	const char *path;
	FieldKind kind;
	float min = 0.f, max = 1.f, step = 1.f;
	std::vector<const char *> choices{};
	bool fallbackBool = false;
};

/// Champs du formulaire (chemins lus par ContentRegistry::LoadBlock).
const std::vector<FieldSpec> &Fields() {
	static const std::vector<FieldSpec> fields = {
		{"Identité", "Nom affiché", "displayName", FieldKind::Text},
		{"Graphismes", "Texture (toutes faces)", "graphics.texture", FieldKind::Text},
		{"Graphismes", "Textures par face", "graphics.textures", FieldKind::Json},
		{"Physique", "Solide", "physics.solid", FieldKind::Bool, 0.f, 1.f, 1.f, {}, true},
		{"Physique", "Remplaçable", "gameplay.replaceable", FieldKind::Bool},
		{"Physique", "Dureté", "gameplay.hardness", FieldKind::Number, 0.f, 50.f, 0.1f},
		{"Physique", "Glissance", "physics.surface.slipperiness", FieldKind::Number, 0.f, 1.f, 0.05f},
		{"Lumière", "Allumée", "light.enabled", FieldKind::Bool, 0.f, 1.f, 1.f, {}, true},
		{"Lumière", "Type", "light.type", FieldKind::Choice, 0.f, 1.f, 1.f, {"point", "spot", "directional"}},
		{"Lumière", "Couleur [r, g, b]", "light.color", FieldKind::Json},
		{"Lumière", "Intensité", "light.intensity", FieldKind::Number, 0.f, 20.f, 0.1f},
		{"Lumière", "Portée (blocs)", "light.range", FieldKind::Number, 0.f, 32.f, 0.5f},
		{"Lumière", "Angle intérieur (°)", "light.innerAngle", FieldKind::Number, 0.f, 180.f, 1.f},
		{"Lumière", "Angle extérieur (°)", "light.outerAngle", FieldKind::Number, 0.f, 180.f, 1.f},
		{"Interaction", "Bascule vers (état)", "interact.toggle", FieldKind::Text},
		{"Capteur", "Rayon", "sensor.radius", FieldKind::Number, 0.f, 32.f, 0.5f},
		{"Capteur", "Proche → état", "sensor.near", FieldKind::Text},
		{"Capteur", "Loin → état", "sensor.far", FieldKind::Text},
		{"Conteneur", "Emplacements", "container.slots", FieldKind::Number, 0.f, 108.f, 1.f},
	};
	return fields;
}

bool IsBlockDefinition(const NodePtr &def) {
	if (!def || !def->IsObject())
		return false;
	const String type = json::Str(def, "type", "block").ToLower();
	return type == "block" || type == "cube" || type == "nodebox" || type == "cross" || type == "plant" || type == "obj" ||
		   type == "b3d" || type == "mesh";
}

/// JSON sur une ligne (champ du formulaire) : retours et indentation ôtés.
String Compact(const String &text) {
	String out;
	bool quoted = false, newline = false;
	for (size_t i = 0; i < text.GetSize(); ++i) {
		const char c = text[i];
		if (c == '"' && (i == 0 || text[i - 1] != '\\'))
			quoted = !quoted;
		if (!quoted && (c == '\n' || c == '\r')) {
			newline = true;
			continue;
		}
		if (!quoted && newline && (c == ' ' || c == '\t'))
			continue;
		if (newline && !out.IsEmpty() && out[out.GetSize() - 1] == ',')
			out += ' ';
		newline = false;
		out += c;
	}
	return out;
}

/// Définitions d'un fichier de mod (objet ou tableau).
std::vector<NodePtr> Definitions(const NodePtr &root) {
	std::vector<NodePtr> out;
	if (root && root->IsArray())
		for (size_t i = 0; i < root->GetSize(); ++i)
			out.push_back(root->At(i));
	else if (root)
		out.push_back(root);
	return out;
}

} // namespace

BlockEditor::BlockEditor(Shell &shell, StudioReport *report, std::function<voxel::BlockPos()> placeAt)
	: m_shell(shell), m_report(report), m_placeAt(std::move(placeAt)) {}

voxel::Game *BlockEditor::Game() {
	Session *session = m_shell.Game();
	return session && session->Ready() ? &session->Game() : nullptr;
}

void BlockEditor::Visit(const char *feature, FeatureStatus status, String detail) {
	if (m_report)
		m_report->Visit(String(feature), status, std::move(detail));
}

String BlockEditor::Path(const char *field) const {
	return m_scope.IsEmpty() ? String(field) : String("states.") + m_scope + String(".") + String(field);
}

// ── Construction ─────────────────────────────────────────────────────────────

void BlockEditor::Build(ui::Widget parent) {
	parent.Append(ui::Column().Grow().Gap(4.f).Children({
		ui::Label("Aucun bloc ouvert — choisissez-en un dans l'onglet « Blocs ».").Class("studio-section").Ref(m_title),
		ui::Row().Gap(4.f).Children({
			ui::Button("Appliquer à chaud").Id("block-apply").Tooltip("Recharge le bloc dans la partie (Ctrl+Maj+R)")
				.OnClick([this] { (void)Apply(); }),
			ui::Button("Enregistrer").Id("block-save").OnClick([this] { (void)Save(); }),
			ui::Button("Poser").Id("block-place").Tooltip("Pose le bloc (ou l'état) devant la caméra").OnClick([this] { (void)Place(); }),
			ui::Button("↶").Id("block-undo").Tooltip("Annuler").OnClick([this] { (void)Undo(); }),
			ui::Button("↷").Id("block-redo").Tooltip("Rétablir").OnClick([this] { (void)Redo(); }),
		}),
		ui::Row().Gap(4.f).Ref(m_scopeRow),
		ui::Column().Id("block-diagnostics").Ref(m_diagnosticList),
		ui::TabView({String("Formulaire"), String("JSON"), String("États")}).Grow().Children({
			ui::ScrollView().Id("block-form").Grow().Ref(m_form),
			ui::Column().Grow().Gap(4.f).Children({
				ui::CodeEditor(String()).Id("block-json").Grow().Ref(m_jsonEditor),
				ui::Row().Gap(4.f).Children({
					ui::Button("Appliquer le JSON").Id("block-json-apply").OnClick([this] {
						if (!m_doc.IsSome())
							return;
						Result<bool, String> r = m_doc.Value().SetJson(m_jsonEditor.Text());
						m_jsonStatus.SetText(r.IsOk() ? String("JSON appliqué au document") : r.Error());
						if (r.IsOk()) {
							++m_version;
							Visit("block.json_edit");
						} else {
							Visit("block.json_edit", FeatureStatus::Warning, r.Error());
						}
					}),
					ui::Label("").Class("studio-status").Ref(m_jsonStatus),
				}),
			}),
			ui::ScrollView().Id("block-states").Grow().Ref(m_states),
		}),
	}));
}

void BlockEditor::BuildCatalog(ui::Widget parent) {
	parent.Append(ui::Column().Grow().Gap(4.f).Children({
		ui::Row().Gap(4.f).Children({
			ui::Input("Filtrer les blocs…").Id("block-filter").Grow().OnTextChange([this](const String &text) {
				m_catalogFilter = text;
				RebuildCatalog();
			}),
			ui::Button("+").Id("block-new").Tooltip("Nouveau bloc (mod « core »)").OnClick([this] {
				(void)New(String::Format("nouveau_%zu", size_t(m_version + 1)), String("core"));
			}),
		}),
		ui::ScrollView().Id("block-catalog").Grow().Ref(m_catalogList),
	}));
	RebuildCatalog();
}

void BlockEditor::RebuildCatalog() {
	if (!m_catalogList.Valid())
		return;
	m_catalogList.Clear();
	voxel::Game *game = Game();
	if (!game) {
		m_catalogList.Append(ui::Label("aucune partie en cours").Class("studio-status"));
		return;
	}
	const String filter = m_catalogFilter.ToLower();
	String mod;
	size_t shown = 0;
	for (const voxel::BlockDef &b : game->Content().Blocks()) {
		if (b.IsAir() || (!b.stateGroup.IsEmpty() && b.stateGroup != b.id))
			continue; // un état s'ouvre avec son bloc
		if (!filter.IsEmpty() && b.id.ToLower().Find(filter) == String::NPOS && b.displayName.ToLower().Find(filter) == String::NPOS)
			continue;
		if (b.mod != mod) {
			mod = b.mod;
			m_catalogList.Append(ui::Label(mod).Class("studio-group"));
		}
		const String id = b.id;
		m_catalogList.Append(ui::Label(String("  ") + (b.displayName.IsEmpty() ? b.id : b.displayName) +
									   (b.stateIds.empty() ? String() : String::Format(" (%zu états)", b.stateIds.size())))
								 .Class("studio-row")
								 .Tooltip(id)
								 .OnClick([this, id] { (void)Open(id); }));
		if (++shown >= 400) {
			m_catalogList.Append(ui::Label("… (affinez le filtre)").Class("studio-status"));
			break;
		}
	}
}

void BlockEditor::RebuildForm() {
	m_formVersion = m_version;
	if (!m_form.Valid())
		return;
	m_form.Clear();
	m_scopeRow.Clear();
	if (!m_doc.IsSome())
		return;
	BlockDocument &doc = m_doc.Value();

	// Portée : bloc commun, ou un de ses états.
	std::vector<String> scopes{String("Bloc (commun)")};
	int selected = 0;
	if (NodePtr states = doc.Field("states"); states && states->IsObject())
		for (const String &name : states->Keys()) {
			scopes.push_back(String("État « ") + name + String(" »"));
			if (name == m_scope)
				selected = int(scopes.size() - 1);
		}
	if (selected == 0)
		m_scope = String();
	std::vector<String> names{String()};
	if (NodePtr states = doc.Field("states"); states && states->IsObject())
		for (const String &name : states->Keys())
			names.push_back(name);
	m_scopeRow.Append(ui::Label("Portée").Class("studio-key"));
	m_scopeRow.Append(ui::Combo(scopes, selected).Id("block-scope").Grow().OnSelect([this, names](int index) {
		if (index >= 0 && size_t(index) < names.size())
			(void)SetScope(names[size_t(index)]);
	}));

	// Valeur EFFECTIVE d'un champ : l'état la surcharge, sinon le bloc commun.
	auto effective = [&](const char *field) -> NodePtr {
		if (NodePtr v = doc.Field(Path(field).CStr()))
			return v;
		return m_scope.IsEmpty() ? nullptr : doc.Field(field);
	};
	const char *section = "";
	for (const FieldSpec &spec : Fields()) {
		if (std::strcmp(section, spec.section) != 0) {
			section = spec.section;
			m_form.Append(ui::Label(String(section)).Class("studio-section"));
		}
		NodePtr value = effective(spec.path);
		const bool own = doc.Field(Path(spec.path).CStr()) != nullptr;
		const char *path = spec.path;
		ui::WidgetBuilder editor = ui::Label("");
		switch (spec.kind) {
		case FieldKind::Text:
			editor = ui::Input(String(), value ? json::Str(value, "") : String()).Grow().OnSubmit([this, path](const String &text) {
				Edit(path, text.IsEmpty() ? nullptr : json::MakeStr(text), false);
			});
			break;
		case FieldKind::Json:
			editor = ui::Input(String("JSON"), value ? Compact(json::Encode(value)) : String()).Grow().OnSubmit([this, path](const String &text) {
				if (text.Trim().IsEmpty()) {
					Edit(path, nullptr, false);
					return;
				}
				Result<NodePtr, String> parsed = json::Parse(text);
				if (parsed.IsOk())
					Edit(path, parsed.Value(), false);
				else
					m_jsonStatus.SetText(String(path) + String(" : ") + parsed.Error());
			});
			break;
		case FieldKind::Number:
			editor = ui::NumberInput(spec.min, spec.max, value ? float(json::Num(value, "", 0.0)) : spec.min, spec.step, 2)
						 .Grow()
						 .OnChange([this, path](float v) { Edit(path, json::MakeNum(double(v)), true); });
			break;
		case FieldKind::Bool:
			editor = ui::Checkbox(String(), value ? json::Bool(value, "", spec.fallbackBool) : spec.fallbackBool)
						 .OnToggle([this, path](bool on) { Edit(path, json::MakeBool(on), false); });
			break;
		case FieldKind::Choice: {
			std::vector<String> items;
			int index = 0;
			const String current = value ? json::Str(value, "") : String(spec.choices.front());
			for (size_t i = 0; i < spec.choices.size(); ++i) {
				items.push_back(String(spec.choices[i]));
				if (current == spec.choices[i])
					index = int(i);
			}
			std::vector<const char *> choices = spec.choices;
			editor = ui::Combo(items, index).Grow().OnSelect([this, path, choices](int i) {
				if (i >= 0 && size_t(i) < choices.size())
					Edit(path, json::MakeStr(String(choices[size_t(i)])), false);
			});
			break;
		}
		}
		m_form.Append(ui::Row().Gap(4.f).Children({
			ui::Label(String(spec.label) + (own || m_scope.IsEmpty() ? String() : String(" ·")))
				.Class("studio-key")
				.Tooltip(own || m_scope.IsEmpty() ? Path(spec.path) : String("hérité du bloc commun : ") + String(spec.path)),
			editor,
			ui::Button("×").Tooltip("Retirer le champ").OnClick([this, path] { Edit(path, nullptr, false); }),
		}));
	}
}

// ── Édition ──────────────────────────────────────────────────────────────────

void BlockEditor::Edit(const char *field, NodePtr value, bool continuous) {
	if (!m_doc.IsSome())
		return;
	BlockDocument &doc = m_doc.Value();
	if (continuous) {
		if (!m_gesture) {
			doc.BeginEdit();
			m_gesture = true;
		}
		m_gestureTimer = GESTURE_SECONDS;
	} else {
		CloseGesture();
	}
	doc.SetField(Path(field).CStr(), std::move(value));
	++m_version;
	m_formVersion = m_version; // le formulaire est à jour : c'est lui qui écrit
	m_validateDelay = VALIDATE_SECONDS;
	Visit("block.form_edit");
}

void BlockEditor::CloseGesture() {
	if (m_gesture && m_doc.IsSome())
		m_doc.Value().EndEdit();
	m_gesture = false;
	m_gestureTimer = 0.f;
}

void BlockEditor::Update(float dt) {
	if (m_gesture) {
		m_gestureTimer -= dt;
		if (m_gestureTimer <= 0.f)
			CloseGesture();
	}
	if (m_version != m_formVersion && !m_gesture)
		RebuildForm();
	if (m_version != m_validatedVersion) {
		m_validateDelay -= dt;
		if (m_validateDelay <= 0.f)
			RefreshDiagnostics();
	}
}

void BlockEditor::RefreshHeader() {
	if (!m_doc.IsSome() || !m_title.Valid())
		return;
	const BlockDocument &doc = m_doc.Value();
	m_title.SetText(doc.Id() + (doc.Dirty() ? String(" ●") : String()) + String("   ") + doc.SourceFile());
	// Texte JSON : rafraîchi tant que l'utilisateur ne l'a pas modifié lui-même.
	m_jsonEditor.SetText(doc.Text());
}

void BlockEditor::RefreshDiagnostics() {
	m_validatedVersion = m_version;
	RefreshHeader();
	if (!m_doc.IsSome() || !m_diagnosticList.Valid())
		return;
	voxel::Game *game = Game();
	m_diagnostics = game ? m_doc.Value().Validate(game->Content()) : std::vector<BlockDiagnostic>{};
	m_diagnosticList.Clear();
	if (m_diagnostics.empty())
		m_diagnosticList.Append(ui::Label("✓ aucun problème").Class("studio-log-ok"));
	for (const BlockDiagnostic &d : m_diagnostics)
		m_diagnosticList.Append(
			ui::Label(d.message)
				.Class(d.severity == BlockDiagnostic::Severity::Error ? "studio-log-error" : "studio-log-warning")
				.Tooltip(d.field.IsEmpty() ? String("bloc entier") : String("champ : ") + d.field));
	Visit("block.validate", m_diagnostics.empty() ? FeatureStatus::Ok : FeatureStatus::Warning,
		  String::Format("%zu diagnostic(s)", m_diagnostics.size()));
	RefreshStates();
}

void BlockEditor::RefreshStates() {
	if (!m_states.Valid() || !m_doc.IsSome())
		return;
	m_states.Clear();
	voxel::Game *game = Game();
	if (!game)
		return;
	const std::vector<MergedState> states = m_doc.Value().MergedStates(game->Content());
	if (states.empty()) {
		m_states.Append(ui::Label("Le document ne se charge pas : voir les diagnostics.").Class("studio-log-error"));
		return;
	}
	for (const MergedState &s : states) {
		m_states.Append(ui::Label((s.name.IsEmpty() ? String("(sans états)") : String("État « ") + s.name + String(" »")) +
								  String("  →  ") + s.id + (s.isDefault ? String("  (par défaut)") : String()))
							.Class("studio-section"));
		if (!s.isDefault)
			m_states.Append(ui::Label(s.differences.empty() ? String("identique à l'état par défaut")
															: String::Format("%zu champ(s) diffèrent de l'état par défaut :", s.differences.size()))
								.Class("studio-status"));
		for (const String &path : s.differences)
			m_states.Append(ui::Label(String("    ") + path).Class("studio-log-warning"));
	}
}

// ── Actions ──────────────────────────────────────────────────────────────────

CommandResult BlockEditor::Open(const String &blockId) {
	voxel::Game *game = Game();
	if (!game)
		return CommandResult::Error(String("aucune partie en cours"));
	const String id = voxel::ContentRegistry::Qualify(blockId, String("core"));
	Option<BlockDocument> doc = BlockDocument::FromBlock(game->Content(), blockId.Contains(":") ? blockId : id);
	if (doc.IsNone())
		return CommandResult::Error(String("bloc introuvable (ou sans source) : ") + blockId);
	CloseGesture();
	m_doc = std::move(doc);
	m_scope = String();
	++m_version;
	m_validateDelay = 0.f;
	RefreshDiagnostics();
	Visit("block.open");
	if (onDocumentOpened)
		onDocumentOpened();
	return CommandResult::Ok(String("bloc ouvert : ") + m_doc.Value().Id());
}

CommandResult BlockEditor::New(const String &id, const String &mod) {
	voxel::Game *game = Game();
	if (!game)
		return CommandResult::Error(String("aucune partie en cours"));
	if (id.IsEmpty() || id.Contains("/") || id.Contains(" "))
		return CommandResult::Error(String("id de bloc invalide : ") + id);
	if (game->Content().FindBlock(voxel::ContentRegistry::Qualify(id, mod)))
		return CommandResult::Error(String("ce bloc existe déjà : ") + voxel::ContentRegistry::Qualify(id, mod));
	// Fichier du mod : celui d'un bloc existant du même mod, sinon blocks/studio.json.
	String baseDir, file;
	for (const voxel::BlockDef &b : game->Content().Blocks())
		if (b.mod == mod && !b.baseDir.IsEmpty()) {
			baseDir = b.baseDir;
			break;
		}
	if (baseDir.IsEmpty())
		return CommandResult::Error(String("mod inconnu ou sans bloc : ") + mod);
	file = baseDir + String("/blocks/studio.json");
	CloseGesture();
	m_doc = Some(BlockDocument::New(id, mod, baseDir, file));
	m_scope = String();
	++m_version;
	m_validateDelay = 0.f;
	RefreshDiagnostics();
	Visit("block.new");
	if (onDocumentOpened)
		onDocumentOpened();
	return CommandResult::Ok(String("nouveau bloc : ") + m_doc.Value().Id() + String(" (") + file + String(")"));
}

CommandResult BlockEditor::SetField(const String &path, const String &jsonValue) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucun bloc ouvert"));
	NodePtr value;
	if (jsonValue != "null") {
		Result<NodePtr, String> parsed = json::Parse(jsonValue);
		if (parsed.IsError())
			return CommandResult::Error(String("valeur JSON invalide : ") + parsed.Error());
		value = parsed.Value();
	}
	Edit(path.CStr(), value, false);
	m_formVersion = ~0ull; // le formulaire relit la valeur écrite
	return CommandResult::Ok(Path(path.CStr()) + String(" = ") + jsonValue);
}

CommandResult BlockEditor::SetScope(const String &state) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucun bloc ouvert"));
	if (!state.IsEmpty() && !m_doc.Value().Field((String("states.") + state).CStr()))
		return CommandResult::Error(String("état inconnu : ") + state);
	CloseGesture();
	m_scope = state;
	++m_version;
	return CommandResult::Ok(state.IsEmpty() ? String("portée : bloc commun") : String("portée : état ") + state);
}

CommandResult BlockEditor::Reload(const NodePtr &definition, const voxel::ContentRegistry::LoadContext &context,
								  const String &what) {
	Session *session = m_shell.Game();
	if (!session || !session->Ready())
		return CommandResult::Error(String("aucune partie en cours"));
	Result<Session::ContentReload, String> r = session->ReloadDefinition(definition, context);
	if (r.IsError()) {
		Visit("block.hot_reload", FeatureStatus::Error, r.Error());
		return CommandResult::Error(what + String(" : ") + r.Error());
	}
	++m_reloads;
	if (m_report)
		m_report->Count(String("blocks.reloaded"), static_cast<long long>(r.Value().blocks.size()));
	const String detail = String::Format("%zu bloc(s)%s, %.0f ms", r.Value().blocks.size(),
										 r.Value().atlasRebuilt ? ", atlas reconstruit" : "", r.Value().milliseconds);
	Visit("block.hot_reload", FeatureStatus::Ok, detail);
	RebuildCatalog();
	return CommandResult::Ok(what + String(" rechargé à chaud : ") + detail);
}

CommandResult BlockEditor::Apply() {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucun bloc ouvert"));
	CloseGesture();
	voxel::Game *game = Game();
	if (game)
		for (const BlockDiagnostic &d : m_doc.Value().Validate(game->Content()))
			if (d.severity == BlockDiagnostic::Severity::Error)
				return CommandResult::Error(String("le bloc ne se charge pas : ") + d.message);
	return Reload(m_doc.Value().Json(), m_doc.Value().Context(), m_doc.Value().Id());
}

CommandResult BlockEditor::Save() {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucun bloc ouvert"));
	CloseGesture();
	Result<bool, String> r = m_doc.Value().Save();
	if (r.IsError()) {
		Visit("block.save", FeatureStatus::Error, r.Error());
		return CommandResult::Error(r.Error());
	}
	RefreshHeader();
	Visit("block.save", FeatureStatus::Ok, m_doc.Value().SourceFile());
	return CommandResult::Ok(String("enregistré : ") + m_doc.Value().SourceFile());
}

CommandResult BlockEditor::Place() {
	voxel::Game *game = Game();
	if (!game || !m_doc.IsSome())
		return CommandResult::Error(String("aucun bloc ouvert ou aucune partie"));
	String id = m_doc.Value().Id();
	if (!m_scope.IsEmpty())
		if (const voxel::BlockDef *group = game->Content().FindBlock(id))
			for (const auto &[name, stateId] : group->stateIds)
				if (name == m_scope)
					id = stateId;
	if (!game->Content().FindBlock(id))
		return CommandResult::Error(String("bloc pas encore dans la partie : « Appliquer à chaud » d'abord (") + id + String(")"));
	const voxel::BlockPos at = m_placeAt();
	if (!game->GetWorld().SetBlock(at, id, 0, voxel::ChangeCause::COMMAND))
		return CommandResult::Error(String("pose impossible en ") + at.ToString() + String(" (chunk non chargé ?)"));
	Visit("block.place", FeatureStatus::Ok, id);
	return CommandResult::Ok(id + String(" posé en ") + at.ToString());
}

CommandResult BlockEditor::Undo() {
	CloseGesture();
	if (!m_doc.IsSome() || !m_doc.Value().Undo())
		return CommandResult::Warning(String("rien à annuler"));
	++m_version;
	Visit("block.undo");
	return CommandResult::Ok(String("annulé"));
}

CommandResult BlockEditor::Redo() {
	CloseGesture();
	if (!m_doc.IsSome() || !m_doc.Value().Redo())
		return CommandResult::Warning(String("rien à rétablir"));
	++m_version;
	Visit("block.redo");
	return CommandResult::Ok(String("rétabli"));
}

CommandResult BlockEditor::ReloadFromDisk(const String &blockId) {
	voxel::Game *game = Game();
	if (!game)
		return CommandResult::Error(String("aucune partie en cours"));
	const voxel::BlockDef *block = game->Content().FindBlock(blockId.Contains(":") ? blockId : voxel::ContentRegistry::Qualify(blockId, String("core")));
	if (!block)
		return CommandResult::Error(String("bloc inconnu : ") + blockId);
	if (!block->stateGroup.IsEmpty())
		if (const voxel::BlockDef *group = game->Content().FindBlock(block->stateGroup))
			block = group;
	if (block->sourceFile.IsEmpty())
		return CommandResult::Error(String("bloc sans fichier source : ") + block->id);
	Result<NodePtr, String> file = json::ReadFile(block->sourceFile);
	if (file.IsError())
		return CommandResult::Error(file.Error());
	voxel::ContentRegistry::LoadContext ctx;
	ctx.mod = block->mod;
	ctx.baseDir = block->baseDir;
	ctx.source = block->sourceFile;
	const String id = block->id;
	for (const NodePtr &def : Definitions(file.Value()))
		if (IsBlockDefinition(def) && voxel::ContentRegistry::Qualify(json::Str(def, "id"), ctx.mod) == id)
			return Reload(def, ctx, id);
	return CommandResult::Error(String("définition introuvable dans ") + ctx.source + String(" : ") + id);
}

CommandResult BlockEditor::ReloadMod(const String &mod) {
	Session *session = m_shell.Game();
	voxel::Game *game = Game();
	if (!game || !session)
		return CommandResult::Error(String("aucune partie en cours"));
	// Fichiers des blocs du mod (lus une fois chacun), rechargés en UN lot :
	// fils attendus, atlas et mailleurs refaits une seule fois.
	std::set<std::string> files;
	String baseDir;
	for (const voxel::BlockDef &b : game->Content().Blocks())
		if (b.mod == mod && !b.sourceFile.IsEmpty()) {
			files.insert(b.sourceFile.CStr());
			baseDir = b.baseDir;
		}
	if (files.empty())
		return CommandResult::Error(String("mod sans bloc chargé : ") + mod);
	std::vector<Session::Definition> batch;
	std::vector<String> failures;
	for (const std::string &path : files) {
		Result<NodePtr, String> file = json::ReadFile(String(path.c_str()));
		if (file.IsError()) {
			failures.push_back(file.Error());
			continue;
		}
		voxel::ContentRegistry::LoadContext ctx;
		ctx.mod = mod;
		ctx.baseDir = baseDir;
		ctx.source = String(path.c_str());
		for (const NodePtr &def : Definitions(file.Value()))
			if (IsBlockDefinition(def))
				batch.push_back({def, ctx});
	}
	const Session::ContentReload r = session->ReloadDefinitions(batch);
	for (const String &e : r.errors)
		failures.push_back(e);
	m_reloads += batch.size() - std::min(batch.size(), r.errors.size());
	if (m_report)
		m_report->Count(String("blocks.reloaded"), static_cast<long long>(r.blocks.size()));
	RebuildCatalog();
	Visit("mod.hot_reload", failures.empty() ? FeatureStatus::Ok : FeatureStatus::Warning,
		  String::Format("%s : %zu définition(s), %zu bloc(s)%s, %.0f ms", mod.CStr(), batch.size(), r.blocks.size(),
						 r.atlasRebuilt ? ", atlas reconstruit" : "", r.milliseconds));
	if (!failures.empty())
		return CommandResult::Warning(String::Format("%zu définition(s) rechargée(s), %zu échec(s) : ",
													 batch.size() - std::min(batch.size(), failures.size()), failures.size()) +
									  failures.front());
	// Avertissement voulu : biomes et dimensions ne régénèrent pas le terrain déjà généré.
	return CommandResult::Warning(String::Format("mod %s : %zu bloc(s) rechargé(s)%s — biomes et dimensions : le terrain déjà "
												 "généré ne change pas",
												 mod.CStr(), r.blocks.size(), r.atlasRebuilt ? ", atlas reconstruit" : ""));
}

} // namespace voxelia::studio
