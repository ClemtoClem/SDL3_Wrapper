// Définitions de voxelia/studio/entity_editor.hpp
#include <voxelia/studio/entity_editor.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <ecs/relations.hpp>
#include <voxel/json.hpp>
#include <voxel/scripting.hpp>
#include <voxelia/session.hpp>
#include <voxelia/shell.hpp>

namespace voxelia::studio {

namespace json = voxel::json;
using dataio::NodePtr;
using voxel::BlockPos;

namespace {

constexpr float VALIDATE_DELAY = 0.3f;

enum class FieldKind : uint8_t { Text, Number, Bool, Json };

struct FieldSpec {
	const char *section, *label, *path;
	FieldKind kind;
	float min = 0.f, max = 1.f, step = 1.f;
	bool fallbackBool = false;
	float fallback = 0.f; ///< valeur du moteur quand le champ est absent
};

/// Champs du formulaire (chemins lus par ContentRegistry::LoadEntity et les scripts).
const std::vector<FieldSpec> &Fields() {
	static const std::vector<FieldSpec> fields = {
		{"Identité", "Nom affiché", "displayName", FieldKind::Text},
		{"Identité", "Étiquettes", "tags", FieldKind::Json},
		{"Physique", "Boîte [x, y, z]", "physics.collider", FieldKind::Json},
		{"Physique", "Masse", "physics.mass", FieldKind::Number, 0.f, 1000.f, 1.f, false, 1.f},
		{"Physique", "Vitesse max", "physics.maxSpeed", FieldKind::Number, 0.f, 20.f, 0.1f, false, 4.f},
		{"Physique", "Saut", "physics.jumpSpeed", FieldKind::Number, 0.f, 30.f, 0.1f, false, 8.f},
		{"Physique", "Marche (hauteur)", "physics.stepHeight", FieldKind::Number, 0.f, 2.f, 0.05f, false, 0.6f},
		{"Physique", "Gravité (facteur)", "physics.gravityScale", FieldKind::Number, -4.f, 4.f, 0.05f, false, 1.f},
		{"Physique", "Frottement", "physics.friction", FieldKind::Number, 0.f, 1.f, 0.05f, false, 0.6f},
		{"Physique", "Rebond", "physics.bounciness", FieldKind::Number, 0.f, 1.f, 0.05f},
		{"Physique", "Nage", "physics.swims", FieldKind::Bool, 0.f, 1.f, 1.f, true},
		{"Physique", "Vole", "physics.flies", FieldKind::Bool},
		{"Physique", "Poussable", "physics.pushable", FieldKind::Bool, 0.f, 1.f, 1.f, true},
		{"Gameplay", "Points de vie", "health", FieldKind::Number, 0.f, 1000.f, 1.f, false, 10.f},
		{"Gameplay", "Expérience", "xp", FieldKind::Number, 0.f, 1000.f, 1.f},
		{"Gameplay", "Enregistrée avec le monde", "persistent", FieldKind::Bool, 0.f, 1.f, 1.f, true},
		{"Gameplay", "Clic droit (fonction de script)", "onInteract", FieldKind::Text},
		{"Apparence", "Forme (sans modèle)", "shape", FieldKind::Text},
		{"Apparence", "Modèle articulé", "rig.model", FieldKind::Text},
		{"Apparence", "Échelle du modèle", "rig.scale", FieldKind::Number, 0.05f, 10.f, 0.05f, false, 1.f},
		{"Lumière", "Allumée", "light.enabled", FieldKind::Bool, 0.f, 1.f, 1.f, true},
		{"Lumière", "Couleur [r, g, b]", "light.color", FieldKind::Json},
		{"Lumière", "Intensité", "light.intensity", FieldKind::Number, 0.f, 20.f, 0.1f},
		{"Lumière", "Portée (blocs)", "light.range", FieldKind::Number, 0.f, 32.f, 0.5f},
		{"Sons", "Ambiance (idle)", "sounds.idle", FieldKind::Text},
		{"Sons", "Blessure (hurt)", "sounds.hurt", FieldKind::Text},
		{"Sons", "Mort (death)", "sounds.death", FieldKind::Text},
		{"Sons", "Pas (step)", "sounds.step", FieldKind::Text},
		{"Apparition", "Biomes", "spawn.biomes", FieldKind::Json},
		{"Apparition", "Sur les blocs", "spawn.onBlocks", FieldKind::Json},
		{"Apparition", "Chance", "spawn.chance", FieldKind::Number, 0.f, 1.f, 0.001f},
		{"Apparition", "Nombre max", "spawn.max", FieldKind::Number, 0.f, 64.f, 1.f},
		{"Apparition", "La nuit seulement", "spawn.night", FieldKind::Bool},
	};
	return fields;
}

/// Valeur saisie : JSON valide, sinon le mot nu pris comme texte ; vide : nullptr.
NodePtr ParseValue(const String &text) {
	const String t = text.Trim();
	if (t.IsEmpty())
		return nullptr;
	if (Result<NodePtr, String> parsed = json::Parse(t); parsed.IsOk())
		return parsed.Value();
	return json::MakeStr(t);
}

Option<double> Number(const String &s) {
	char *end = nullptr;
	const double v = std::strtod(s.CStr(), &end);
	if (!end || end == s.CStr() || *end != '\0')
		return NONE;
	return Some(v);
}

} // namespace

EntityEditor::EntityEditor(Shell &shell, StudioReport *report, std::function<BlockPos()> arenaAt)
	: m_shell(shell), m_report(report), m_arenaAt(std::move(arenaAt)) {}

EntityEditor::~EntityEditor() { (void)StopPreview(); }

voxel::Game *EntityEditor::Game() {
	Session *session = m_shell.Game();
	return session && session->Ready() ? &session->Game() : nullptr;
}

void EntityEditor::Visit(const char *feature, FeatureStatus status, String detail) {
	if (m_report)
		m_report->Visit(String(feature), status, std::move(detail));
}

void EntityEditor::Changed() {
	++m_version;
	m_validateDelay = VALIDATE_DELAY;
}

CommandResult EntityEditor::Done(Result<bool, String> result, const char *feature, const String &ok) {
	if (result.IsError()) {
		Visit(feature, FeatureStatus::Warning, result.Error());
		return CommandResult::Error(result.Error());
	}
	Changed();
	Visit(feature, FeatureStatus::Ok, ok);
	return CommandResult::Ok(ok);
}

// ── Panneau ──────────────────────────────────────────────────────────────────

void EntityEditor::Build(ui::Widget parent) {
	parent.Append(ui::Column().Grow().Gap(4.f).Children({
		ui::Label("Aucune entité — choisissez-en une dans l'onglet « Entités ».").Class("studio-section").Ref(m_title),
		ui::Row().Gap(4.f).Children({
			ui::Button("Appliquer à chaud").OnClick([this] { (void)Apply(); }),
			ui::Button("Enregistrer").OnClick([this] { (void)Save(); }),
			ui::Button("↶").Tooltip("Annuler").OnClick([this] { (void)Undo(); }),
			ui::Button("↷").Tooltip("Rétablir").OnClick([this] { (void)Redo(); }),
		}),
		ui::Row().Gap(4.f).Children({
			ui::Button("▶ Aperçu du comportement").Tooltip("Seule dans une arène devant la caméra, IA active").OnClick([this] {
				(void)StartPreview();
			}),
			ui::Button("■ Arrêter").OnClick([this] { (void)StopPreview(); }),
		}),
		ui::Label("").Class("studio-status").Id("entity-status").Ref(m_status),
		ui::Column().Id("entity-diagnostics").Ref(m_diagnosticList),
		ui::TabView({String("Propriétés"), String("IA"), String("Butin / skins"), String("Composants"), String("JSON")}).Grow().Children({
			ui::ScrollView().Grow().Children({ui::Column().Id("entity-form").Gap(2.f).Ref(m_form)}),
			ui::ScrollView().Grow().Children({
				ui::Column().Gap(4.f).Children({
					ui::Row().Gap(4.f).Children({
						ui::Input("comportement (wander, core:chase…)").Grow().Ref(m_newBehavior),
						ui::Button("Ajouter").OnClick([this] { (void)AddBehavior(m_newBehavior.Text().Trim()); }),
					}),
					ui::Column().Id("entity-behaviors").Gap(2.f).Ref(m_behaviors),
				}),
			}),
			ui::ScrollView().Grow().Children({
				ui::Column().Gap(4.f).Children({
					ui::Label("Butin").Class("studio-dock-title"),
					ui::Row().Gap(4.f).Children({
						ui::Input("objet nombre (core:bone 2)").Grow().Ref(m_newDrop),
						ui::Button("Ajouter").OnClick([this] {
							const std::vector<String> words = m_newDrop.Text().Trim().Split(' ');
							if (!words.empty()) {
								const Option<double> n = words.size() > 1 ? Number(words[1]) : Some(1.0);
								(void)AddDrop(words[0], n.IsSome() ? int(n.Value()) : 1);
							}
						}),
					}),
					ui::Column().Id("entity-drops").Ref(m_drops),
					ui::Label("Skins (un tiré au hasard à l'apparition)").Class("studio-dock-title"),
					ui::Column().Id("entity-skins").Ref(m_skins),
				}),
			}),
			ui::ScrollView().Grow().Children({ui::Column().Id("entity-components").Ref(m_components)}),
			ui::Column().Grow().Gap(4.f).Children({
				ui::CodeEditor(String()).Id("entity-json").Grow().Ref(m_json),
				ui::Row().Gap(4.f).Children({
					ui::Button("Appliquer le JSON").OnClick([this] {
						if (!m_doc.IsSome())
							return;
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

void EntityEditor::BuildCatalog(ui::Widget parent) {
	parent.Append(ui::Column().Grow().Gap(4.f).Children({
		ui::Row().Gap(4.f).Children({
			ui::Label("Entités").Class("studio-dock-title").Grow(),
			ui::Button("+").Tooltip("Nouvelle créature (mod « core »)").OnClick([this] {
				(void)New(String::Format("creature_%zu", size_t(m_version + 1)), String("core"));
			}),
		}),
		ui::Input("Filtrer…").OnTextChange([this](const String &text) {
			m_filter = text.ToLower();
			RebuildCatalog();
		}),
		ui::ScrollView().Id("entity-catalog").Grow().Ref(m_catalog),
	}));
	RebuildCatalog();
}

void EntityEditor::RebuildCatalog() {
	if (!m_catalog.Valid())
		return;
	m_catalog.Clear();
	voxel::Game *game = Game();
	if (!game) {
		m_catalog.Append(ui::Label("aucune partie en cours").Class("studio-status"));
		return;
	}
	// Par genre (creature, entity, projectile…), triés.
	std::map<std::string, std::vector<String>> byKind;
	for (const voxel::EntityDef *e : game->Content().Entities())
		if (m_filter.IsEmpty() || e->id.ToLower().Contains(m_filter) || e->displayName.ToLower().Contains(m_filter))
			byKind[e->kind.CStr()].push_back(e->id);
	for (auto &[kind, ids] : byKind) {
		std::sort(ids.begin(), ids.end());
		m_catalog.Append(ui::Label(String(kind.c_str())).Class("studio-section"));
		for (const String &id : ids)
			m_catalog.Append(ui::Label(String("  ") + id).Class("studio-row").OnClick([this, id] { (void)Open(id); }));
	}
}

void EntityEditor::RebuildAll() {
	m_shownVersion = m_version;
	if (!m_doc.IsSome())
		return;
	const EntityDocument &doc = m_doc.Value();
	m_title.SetText(doc.Id() + (doc.Dirty() ? String(" ●") : String()) + String("   ") + doc.SourceFile());
	m_json.SetText(doc.Text());
	RebuildForm();
	RebuildLists();
	m_components.Clear();
	if (voxel::Game *game = Game())
		for (const auto &[name, summary] : doc.Components(game->Content()))
			m_components.Append(ui::Row().Gap(6.f).Children({
				ui::Label(name).Class("studio-key").Width(110.f),
				ui::Label(summary).Grow(),
			}));
}

void EntityEditor::RebuildForm() {
	m_form.Clear();
	EntityDocument &doc = m_doc.Value();
	const char *section = "";
	for (const FieldSpec &spec : Fields()) {
		if (std::strcmp(section, spec.section) != 0) {
			section = spec.section;
			m_form.Append(ui::Label(String(section)).Class("studio-section"));
		}
		const NodePtr value = doc.Field(spec.path);
		const String path(spec.path);
		ui::WidgetBuilder editor = ui::Label("");
		switch (spec.kind) {
		case FieldKind::Text:
			editor = ui::Input(String(), value ? json::Str(value, "") : String()).Grow().OnSubmit([this, path](const String &text) {
				(void)SetField(path, text.Trim().IsEmpty() ? String() : String("\"") + text.Trim() + String("\""));
			});
			break;
		case FieldKind::Json:
			editor = ui::Input(String("JSON"), value ? CompactJson(json::Encode(value)) : String()).Grow().OnSubmit([this, path](const String &text) {
				(void)SetField(path, text);
			});
			break;
		case FieldKind::Number:
			editor = ui::NumberInput(spec.min, spec.max, value ? float(json::Num(value, "", 0.0)) : spec.fallback, spec.step, 3)
						 .Grow()
						 .OnChange([this, path](float v) { (void)SetField(path, String::Format("%g", double(v))); });
			break;
		case FieldKind::Bool:
			editor = ui::Checkbox(String(), value ? json::Bool(value, "", spec.fallbackBool) : spec.fallbackBool).OnToggle([this, path](bool on) {
				(void)SetField(path, String(on ? "true" : "false"));
			});
			break;
		}
		m_form.Append(ui::Row().Gap(6.f).Children({ui::Label(String(spec.label)).Class("studio-key").Width(150.f), editor}));
	}
}

void EntityEditor::RebuildLists() {
	const EntityDocument &doc = m_doc.Value();
	m_behaviors.Clear();
	const std::vector<EntityBehavior> behaviors = doc.Behaviors();
	if (behaviors.empty())
		m_behaviors.Append(ui::Label("aucun comportement : l'entité reste immobile").Class("studio-status"));
	for (size_t i = 0; i < behaviors.size(); ++i) {
		const EntityBehavior &b = behaviors[i];
		String params;
		if (b.params)
			for (const String &key : b.params->Keys())
				if (key != "id" && key != "behavior")
					params += (params.IsEmpty() ? String() : String(", ")) + key + String(" = ") + CompactJson(json::Encode(b.params->Get(key)));
		m_behaviors.Append(ui::Row().Gap(2.f).Children({
			ui::Label(String::Format("%zu. ", i + 1) + b.id).Class("studio-row").Width(170.f),
			ui::Label(params.IsEmpty() ? String("(sans paramètre)") : params).Class("studio-status").Grow(),
			ui::Button("▲").OnClick([this, i] {
				if (i > 0)
					(void)MoveBehavior(i, i - 1);
			}),
			ui::Button("▼").OnClick([this, i] { (void)MoveBehavior(i, i + 1); }),
			ui::Button("✕").OnClick([this, i] { (void)RemoveBehavior(i); }),
		}));
		// Un paramètre : « clé valeur » (JSON ou mot nu ; valeur vide : retiré).
		m_behaviors.Append(ui::Input("paramètre : clé valeur (ex. range 12)").OnSubmit([this, i](const String &text) {
			const String t = text.Trim();
			const size_t space = t.Find(" ");
			(void)SetBehaviorParam(i, space == String::NPOS ? t : t.Substr(0, space), space == String::NPOS ? String() : t.Substring(space + 1));
		}));
	}
	m_drops.Clear();
	const auto drops = doc.Drops();
	for (size_t i = 0; i < drops.size(); ++i)
		m_drops.Append(ui::Row().Gap(2.f).Children({
			ui::Label(String::Format("%s × %d", drops[i].first.CStr(), drops[i].second)).Class("studio-row").Grow(),
			ui::Button("✕").OnClick([this, i] { (void)RemoveDrop(i); }),
		}));
	m_skins.Clear();
	const std::vector<String> skins = doc.Skins();
	for (size_t i = 0; i < skins.size(); ++i)
		m_skins.Append(ui::Row().Gap(2.f).Children({
			ui::Label(skins[i]).Class("studio-row").Grow(),
			ui::Button("✕").OnClick([this, i] {
				if (m_doc.IsSome())
					(void)Done(m_doc.Value().RemoveSkin(i), "entity.skin_remove", String("skin retiré"));
			}),
		}));
	m_skins.Append(ui::Input("+ skin (texture)").OnSubmit([this](const String &text) {
		if (m_doc.IsSome())
			(void)Done(m_doc.Value().AddSkin(text), "entity.skin_add", text.Trim());
	}));
}

void EntityEditor::Revalidate() {
	m_validatedVersion = m_version;
	voxel::Game *game = Game();
	if (!game || !m_doc.IsSome())
		return;
	m_diagnostics = m_doc.Value().Validate(game->Content(), game->Scripts().BehaviorIds());
	m_diagnosticList.Clear();
	if (m_diagnostics.empty())
		m_diagnosticList.Append(ui::Label("✓ aucun problème").Class("studio-log-ok"));
	for (const EntityDiagnostic &d : m_diagnostics)
		m_diagnosticList.Append(
			ui::Label(d.message).Class(d.severity == EntityDiagnostic::Severity::Error ? "studio-log-error" : "studio-log-warning"));
	Visit("entity.validate", m_diagnostics.empty() ? FeatureStatus::Ok : FeatureStatus::Warning,
		  String::Format("%zu diagnostic(s)", m_diagnostics.size()));
}

void EntityEditor::Update(float dt) {
	if (m_doc.IsSome() && m_version != m_shownVersion)
		RebuildAll();
	m_validateDelay -= dt;
	if (m_doc.IsSome() && m_version != m_validatedVersion && m_validateDelay <= 0.f)
		Revalidate();
	if (m_preview.Valid()) {
		voxel::Game *game = Game();
		if (!game || !game->Registry().IsAlive(m_preview)) {
			m_status.SetText("l'entité d'aperçu n'existe plus (morte, ou partie changée)");
			m_preview = ecs::Entity{};
		} else if (auto h = game->Registry().GetComponent<voxel::Health>(m_preview); h.IsSome()) {
			auto t = game->Registry().GetComponent<voxel::Transform>(m_preview);
			m_status.SetText(String::Format("aperçu : %.0f / %.0f PV%s", double(h.Unwrap()->current), double(h.Unwrap()->max),
											t.IsSome() ? String::Format(", en %.1f %.1f %.1f", double(t.Unwrap()->position.x),
																		double(t.Unwrap()->position.y), double(t.Unwrap()->position.z))
															 .CStr()
													   : ""));
		}
	}
}

// ── Actions ──────────────────────────────────────────────────────────────────

CommandResult EntityEditor::Open(const String &entityId) {
	voxel::Game *game = Game();
	if (!game)
		return CommandResult::Error(String("aucune partie en cours"));
	Option<EntityDocument> doc = EntityDocument::FromEntity(game->Content(), entityId);
	if (doc.IsNone())
		return CommandResult::Error(String("entité inconnue ou sans source : ") + entityId);
	m_doc = std::move(doc);
	Changed();
	m_validateDelay = 0.f;
	if (onDocumentOpened)
		onDocumentOpened();
	Visit("entity.open", FeatureStatus::Ok, entityId);
	return CommandResult::Ok(String("ouverte : ") + entityId);
}

CommandResult EntityEditor::New(const String &id, const String &mod) {
	voxel::Game *game = Game();
	if (!game)
		return CommandResult::Error(String("aucune partie en cours"));
	const String qualified = voxel::ContentRegistry::Qualify(id, mod);
	if (game->Content().FindEntity(qualified))
		return CommandResult::Error(String("cette entité existe déjà : ") + qualified);
	String baseDir;
	for (const voxel::ModInfo &m : game->Mods().mods)
		if (m.id == mod)
			baseDir = m.path;
	if (baseDir.IsEmpty())
		return CommandResult::Error(String("mod inconnu : ") + mod);
	m_doc = Some(EntityDocument::New(id, mod, baseDir, baseDir + String("/creatures/studio.json")));
	Changed();
	m_validateDelay = 0.f;
	if (onDocumentOpened)
		onDocumentOpened();
	Visit("entity.new", FeatureStatus::Ok, qualified);
	return CommandResult::Ok(String("nouvelle entité : ") + qualified + String(" (creatures/studio.json)"));
}

CommandResult EntityEditor::SetField(const String &path, const String &value) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune entité ouverte"));
	m_doc.Value().SetField(path.CStr(), ParseValue(value));
	Changed();
	Visit("entity.field", FeatureStatus::Ok, path);
	return CommandResult::Ok(path + String(" = ") + (value.Trim().IsEmpty() ? String("(retiré)") : value.Trim()));
}

CommandResult EntityEditor::Apply() {
	Session *session = m_shell.Game();
	if (!session || !session->Ready() || !m_doc.IsSome())
		return CommandResult::Error(String("aucune entité ouverte"));
	Revalidate();
	for (const EntityDiagnostic &d : m_diagnostics)
		if (d.severity == EntityDiagnostic::Severity::Error)
			return CommandResult::Error(String("refusé : ") + d.message);
	Result<Session::ContentReload, String> r = session->ReloadDefinition(m_doc.Value().Json(), m_doc.Value().Context());
	if (r.IsError()) {
		Visit("entity.hot_reload", FeatureStatus::Error, r.Error());
		return CommandResult::Error(r.Error());
	}
	++m_reloads;
	RebuildCatalog();
	String detail = String::Format("%.0f ms", r.Value().milliseconds);
	if (m_preview.Valid()) { // les composants sont copiés à l'apparition : l'aperçu est refait
		CommandResult respawn = SpawnPreview();
		detail += String(", aperçu refait");
		if (respawn.status != CommandStatus::Ok)
			return respawn;
	}
	Visit("entity.hot_reload", FeatureStatus::Ok, detail);
	return CommandResult::Ok(m_doc.Value().Id() + String(" rechargée à chaud : ") + detail);
}

CommandResult EntityEditor::Save() {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune entité ouverte"));
	Result<bool, String> r = m_doc.Value().Save();
	if (r.IsError())
		return CommandResult::Error(r.Error());
	Changed();
	Visit("entity.save", FeatureStatus::Ok, m_doc.Value().SourceFile());
	return CommandResult::Ok(String("enregistrée : ") + m_doc.Value().SourceFile());
}

CommandResult EntityEditor::Undo() {
	if (!m_doc.IsSome() || !m_doc.Value().Undo())
		return CommandResult::Warning(String("rien à annuler"));
	Changed();
	return CommandResult::Ok(String("annulé"));
}

CommandResult EntityEditor::Redo() {
	if (!m_doc.IsSome() || !m_doc.Value().Redo())
		return CommandResult::Warning(String("rien à rétablir"));
	Changed();
	return CommandResult::Ok(String("rétabli"));
}

CommandResult EntityEditor::AddBehavior(const String &id) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune entité ouverte"));
	return Done(m_doc.Value().AddBehavior(id), "entity.behavior_add", id);
}

CommandResult EntityEditor::RemoveBehavior(size_t index) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune entité ouverte"));
	return Done(m_doc.Value().RemoveBehavior(index), "entity.behavior_remove", String::Format("%zu", index));
}

CommandResult EntityEditor::MoveBehavior(size_t from, size_t to) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune entité ouverte"));
	return Done(m_doc.Value().MoveBehavior(from, to), "entity.behavior_move", String::Format("%zu → %zu", from, to));
}

CommandResult EntityEditor::SetBehaviorParam(size_t index, const String &key, const String &value) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune entité ouverte"));
	return Done(m_doc.Value().SetBehaviorParam(index, key, ParseValue(value)), "entity.behavior_param", key);
}

CommandResult EntityEditor::AddDrop(const String &item, int count) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune entité ouverte"));
	return Done(m_doc.Value().AddDrop(item, count), "entity.drop_add", item);
}

CommandResult EntityEditor::RemoveDrop(size_t index) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune entité ouverte"));
	return Done(m_doc.Value().RemoveDrop(index), "entity.drop_remove", String::Format("%zu", index));
}

// ── Aperçu de comportement ───────────────────────────────────────────────────

CommandResult EntityEditor::SpawnPreview() {
	voxel::Game *game = Game();
	if (!game || !m_doc.IsSome())
		return CommandResult::Error(String("aucune entité ouverte"));
	if (m_preview.Valid() && game->Registry().IsAlive(m_preview))
		(void)ecs::DespawnRecursive(game->Registry(), m_preview);
	m_preview = ecs::Entity{};
	if (!game->Content().FindEntity(m_doc.Value().Id()))
		return CommandResult::Error(String("entité pas encore dans la partie : « Appliquer à chaud » d'abord"));
	const math::FVector3 feet{float(m_arenaCenter.x) + 0.5f, float(m_arenaCenter.y) + 1.f, float(m_arenaCenter.z) + 0.5f};
	m_preview = game->SpawnEntity(m_doc.Value().Id(), feet);
	if (!m_preview.Valid())
		return CommandResult::Error(String("apparition impossible"));
	if (onPreviewSpawned)
		onPreviewSpawned(m_preview);
	return CommandResult::Ok(String("aperçu"));
}

CommandResult EntityEditor::StartPreview() {
	voxel::Game *game = Game();
	if (!game || !m_doc.IsSome())
		return CommandResult::Error(String("aucune entité ouverte"));
	if (!m_arenaBackup.IsSome()) {
		// L'arène : sol de pierre, murs et toit de verre, intérieur d'air ; ce
		// qu'elle remplace est gardé pour être rendu à l'arrêt.
		m_arenaCenter = m_arenaAt();
		const BlockPos lo{m_arenaCenter.x - ARENA_HALF, m_arenaCenter.y, m_arenaCenter.z - ARENA_HALF};
		const BlockPos hi{m_arenaCenter.x + ARENA_HALF, m_arenaCenter.y + ARENA_HEIGHT, m_arenaCenter.z + ARENA_HALF};
		m_arenaBackup = Some(std::make_pair(game->CaptureRegion(lo, hi), lo));
		for (auto r : {game->FillRegion(lo, hi, String("core:air")), game->FillRegion(lo, {hi.x, lo.y, hi.z}, String("core:stone")),
					   game->FillRegion({lo.x, lo.y + 1, lo.z}, hi, String("core:glass"), String("walls")),
					   game->FillRegion({lo.x, hi.y, lo.z}, hi, String("core:glass"))})
			if (r.IsError())
				return CommandResult::Error(String("arène : ") + r.Error());
	}
	CommandResult spawned = SpawnPreview();
	if (spawned.status != CommandStatus::Ok)
		return spawned;
	Visit("entity.preview", FeatureStatus::Ok, m_doc.Value().Id());
	return CommandResult::Ok(String::Format("aperçu de %s dans l'arène en %d %d %d", m_doc.Value().Id().CStr(), m_arenaCenter.x,
											m_arenaCenter.y, m_arenaCenter.z));
}

CommandResult EntityEditor::StopPreview() {
	voxel::Game *game = Game();
	if (!game) {
		m_preview = ecs::Entity{};
		m_arenaBackup = NONE;
		return CommandResult::Warning(String("aucune partie en cours"));
	}
	const bool any = m_preview.Valid() || m_arenaBackup.IsSome();
	if (m_preview.Valid() && game->Registry().IsAlive(m_preview))
		(void)ecs::DespawnRecursive(game->Registry(), m_preview); // sans butin ni « mort »
	m_preview = ecs::Entity{};
	if (m_arenaBackup.IsSome()) {
		voxel::Schematic::PasteOptions restore;
		restore.keepAir = true;
		(void)game->PasteSchematic(m_arenaBackup.Value().first, m_arenaBackup.Value().second, restore);
		m_arenaBackup = NONE;
	}
	if (m_status.Valid())
		m_status.SetText(String());
	if (!any)
		return CommandResult::Warning(String("aucun aperçu en cours"));
	Visit("entity.preview_stop");
	return CommandResult::Ok(String("aperçu arrêté, région rendue"));
}

// ── Console ──────────────────────────────────────────────────────────────────

void EntityEditor::RegisterCommands(ConsoleModel &console) {
	auto need = [](const std::vector<String> &args, size_t n, const char *usage) -> Option<CommandResult> {
		if (args.size() < n)
			return Some(CommandResult::Error(String("usage : ") + String(usage)));
		return NONE;
	};
	auto rest = [](const std::vector<String> &args, size_t first) {
		String out;
		for (size_t i = first; i < args.size(); ++i)
			out += (i > first ? String(" ") : String()) + args[i];
		return out;
	};
	console.Register("studio.entity.open", "studio.entity.open <id>", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.entity.open <id>"); e.IsSome())
			return e.Value();
		return Open(a[1]);
	});
	console.Register("studio.entity.new", "studio.entity.new <id> [mod] — nouvelle créature", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.entity.new <id> [mod]"); e.IsSome())
			return e.Value();
		return New(a[1], a.size() > 2 ? a[2] : String("core"));
	});
	console.Register("studio.entity.set", "studio.entity.set <champ> <json|mot> — écrit un champ (vide : le retire)", [this, need, rest](const auto &a) {
		if (auto e = need(a, 2, "studio.entity.set <champ> <valeur>"); e.IsSome())
			return e.Value();
		return SetField(a[1], rest(a, 2));
	});
	console.Register("studio.entity.apply", "studio.entity.apply — rechargement à chaud", [this](const auto &) { return Apply(); });
	console.Register("studio.entity.save", "studio.entity.save — dans le mod", [this](const auto &) { return Save(); });
	console.Register("studio.entity.undo", "studio.entity.undo", [this](const auto &) { return Undo(); });
	console.Register("studio.entity.redo", "studio.entity.redo", [this](const auto &) { return Redo(); });
	console.Register("studio.entity.behavior.add", "studio.entity.behavior.add <id>", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.entity.behavior.add <id>"); e.IsSome())
			return e.Value();
		return AddBehavior(a[1]);
	});
	console.Register("studio.entity.behavior.remove", "studio.entity.behavior.remove <index>", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.entity.behavior.remove <index>"); e.IsSome())
			return e.Value();
		const Option<double> n = Number(a[1]);
		return n.IsSome() ? RemoveBehavior(size_t(n.Value())) : CommandResult::Error(String("index attendu"));
	});
	console.Register("studio.entity.behavior.move", "studio.entity.behavior.move <de> <vers>", [this, need](const auto &a) {
		if (auto e = need(a, 3, "studio.entity.behavior.move <de> <vers>"); e.IsSome())
			return e.Value();
		const Option<double> from = Number(a[1]), to = Number(a[2]);
		return from.IsSome() && to.IsSome() ? MoveBehavior(size_t(from.Value()), size_t(to.Value())) : CommandResult::Error(String("index attendus"));
	});
	console.Register("studio.entity.behavior.param", "studio.entity.behavior.param <index> <clé> [valeur] — sans valeur : retiré",
					 [this, need, rest](const auto &a) {
						 if (auto e = need(a, 3, "studio.entity.behavior.param <index> <clé> [valeur]"); e.IsSome())
							 return e.Value();
						 const Option<double> n = Number(a[1]);
						 return n.IsSome() ? SetBehaviorParam(size_t(n.Value()), a[2], rest(a, 3)) : CommandResult::Error(String("index attendu"));
					 });
	console.Register("studio.entity.drop.add", "studio.entity.drop.add <objet> [nombre]", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.entity.drop.add <objet> [nombre]"); e.IsSome())
			return e.Value();
		const Option<double> n = a.size() > 2 ? Number(a[2]) : Some(1.0);
		return AddDrop(a[1], n.IsSome() ? int(n.Value()) : 1);
	});
	console.Register("studio.entity.drop.remove", "studio.entity.drop.remove <index>", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.entity.drop.remove <index>"); e.IsSome())
			return e.Value();
		const Option<double> n = Number(a[1]);
		return n.IsSome() ? RemoveDrop(size_t(n.Value())) : CommandResult::Error(String("index attendu"));
	});
	console.Register("studio.entity.preview", "studio.entity.preview — aperçu du comportement dans une arène", [this](const auto &) { return StartPreview(); });
	console.Register("studio.entity.preview.stop", "studio.entity.preview.stop — retire l'aperçu, rend la région", [this](const auto &) {
		return StopPreview();
	});
	console.Register("studio.entity.check", "studio.entity.check — diagnostics", [this](const auto &) {
		if (!m_doc.IsSome())
			return CommandResult::Error(String("aucune entité ouverte"));
		Revalidate();
		if (m_diagnostics.empty())
			return CommandResult::Ok(String("aucun problème"));
		String text = String::Format("%zu diagnostic(s)", m_diagnostics.size());
		for (const EntityDiagnostic &d : m_diagnostics)
			text += String("\n  ") + d.message;
		return CommandResult::Warning(text);
	});
}

} // namespace voxelia::studio
