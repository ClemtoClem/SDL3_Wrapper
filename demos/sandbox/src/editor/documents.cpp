// Définitions de documents.hpp
#include <sandbox/editor/documents.hpp>

namespace sandbox {

// ── DocumentArea ─────────────────────────────────────────────────────────────

void DocumentArea::Build(ui::Widget parent, const std::function<void(ui::WidgetBuilder &)> &size) {
	m_documents.clear();
	m_dock.Build(parent, size);
	m_dock.onClose = [this](int page) { Close(page); };
}

void DocumentArea::OpenLibraryScript(const String &name) {
	const ScriptAsset *asset = m_ctx.runtime.GetProject().FindScript(name);
	if (!asset) {
		Status(String::Format("Script introuvable : %s", name.CStr()));
		return;
	}
	CodeDocument doc;
	doc.kind = DocumentKind::LIBRARY_SCRIPT;
	doc.target = name;
	doc.title = name + String(".script");
	Open(std::move(doc), asset->source);
}

void DocumentArea::OpenSceneScript(const String &sceneName) {
	const SceneDesc *scene = m_ctx.runtime.GetProject().Fs().FindScene(SceneRef{sceneName});
	if (!scene) {
		Status(String::Format("Scène introuvable : %s", sceneName.CStr()));
		return;
	}
	CodeDocument doc;
	doc.kind = DocumentKind::SCENE_SCRIPT;
	doc.target = sceneName;
	doc.title = sceneName + String(".main.script");
	Open(std::move(doc), scene->gameplayScript.Trim().IsEmpty() ? String(NEW_SCENE_SCRIPT_TEMPLATE) : scene->gameplayScript);
}

void DocumentArea::OpenComponentJson(scene::NodeId id, const String &type) {
	const scene::Node *node = m_ctx.runtime.FindNode(id);
	if (!node)
		return;
	scene::PropertyMap props;
	if (type.IsEmpty()) {
		props = node->properties;
	} else if (const scene::Component *component = node->FindComponent(type)) {
		props = component->props;
	} else {
		Status(String::Format("%s n'a pas de composant %s", node->name.CStr(), type.CStr()));
		return;
	}
	dataio::JsonDocument json;
	json.SetRoot(props.ToJson());
	CodeDocument doc;
	doc.kind = DocumentKind::COMPONENT_JSON;
	doc.node = id;
	doc.component = type;
	doc.target = String::Format("%u:%s", id.index, type.CStr());
	doc.title = String::Format("%s · %s.json", node->name.CStr(), type.IsEmpty() ? "propriétés" : type.CStr());
	Open(std::move(doc), json.EncodeStr());
}

void DocumentArea::OpenFile(const String &path) {
	auto text = script::LoadScriptFile(path);
	if (text.IsError()) {
		Status(String::Format("Lecture impossible : %s", path.CStr()));
		return;
	}
	CodeDocument doc;
	doc.kind = DocumentKind::FILE;
	doc.target = path;
	doc.readOnly = true;
	size_t slash = 0;
	for (size_t i = 0; i < path.size(); ++i)
		if (path[i] == '/')
			slash = i + 1;
	doc.title = path.Substr(slash);
	String content = text.Value();
	// Un .gltf « en une ligne » de plusieurs Mo figerait l'affichage : on
	// tronque (c'est une consultation, pas une édition).
	if (content.size() > MAX_FILE_BYTES)
		content = content.Substr(0, MAX_FILE_BYTES) + String("\n… (tronqué à 256 Ko pour l'affichage)\n");
	Open(std::move(doc), content);
}

void DocumentArea::OpenToolConsole() {
	CodeDocument doc;
	doc.kind = DocumentKind::TOOL_CONSOLE;
	doc.target = String("console");
	doc.title = String("Console de script");
	Open(std::move(doc), String(TOOL_CONSOLE_SNIPPET));
}

CodeDocument * DocumentArea::Active() {
	ui::Widget page = m_dock.Page(m_dock.Active());
	for (CodeDocument &doc : m_documents)
		if (doc.page == page)
			return &doc;
	return nullptr;
}

bool DocumentArea::SaveActive() {
	CodeDocument *doc = Active();
	if (!doc)
		return false;
	Save(*doc);
	return true;
}

void DocumentArea::RunActive() {
	CodeDocument *doc = Active();
	if (!doc)
		return;
	const String text = TextOf(*doc);
	if (doc->kind != DocumentKind::TOOL_CONSOLE) {
		Check(*doc, text);
		return;
	}
	auto result = m_ctx.runtime.RunToolScript(text);
	if (result.IsError()) {
		m_ctx.runtime.LogError(String::Format("Console : %s", result.Error().Format().CStr()));
		SetDocStatus(*doc, String::Format("Erreur ligne %d : %s", result.Error().line, result.Error().message.CStr()), true);
	} else {
		if (!result.Value().IsNil())
			m_ctx.runtime.LogSuccess(String::Format("→ %s", result.Value().ToDisplayString().CStr()));
		SetDocStatus(*doc, String("Exécuté"), false);
	}
}

void DocumentArea::CheckActive() {
	if (CodeDocument *doc = Active())
		Check(*doc, TextOf(*doc));
}

void DocumentArea::Close(int page) {
	ui::Widget entity = m_dock.Page(page);
	for (size_t i = 0; i < m_documents.size(); ++i) {
		if (m_documents[i].page != entity)
			continue;
		if (!m_documents[i].readOnly && TextOf(m_documents[i]) != m_documents[i].saved)
			m_ctx.runtime.LogWarning(String::Format("%s fermé sans enregistrer", m_documents[i].title.CStr()));
		m_documents.erase(m_documents.begin() + ptrdiff_t(i));
		m_dock.RemovePage(page);
		return;
	}
}

void DocumentArea::Tick() {
	for (int page = 1; page < m_dock.Count(); ++page) {
		ui::Widget entity = m_dock.Page(page);
		for (CodeDocument &doc : m_documents) {
			if (doc.page != entity)
				continue;
			const bool modified = !doc.readOnly && TextOf(doc) != doc.saved;
			m_dock.SetTitle(page, modified ? doc.title + String(" •") : doc.title);
		}
	}
	if (CodeDocument *doc = Active()) {
		if (auto editor = doc->area.Get<ui::TextEditor>(); editor.IsSome() && doc->area.IsFocused()) {
			const String text = doc->area.Text();
			const size_t cursor = editor.Unwrap()->cursor;
			int line = 1, column = 1;
			for (size_t i = 0; i < cursor && i < text.size(); ++i) {
				if (text[i] == '\n') {
					++line;
					column = 1;
				} else if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
					++column;
				}
			}
			if (line != m_cursorLine || column != m_cursorColumn) {
				m_cursorLine = line;
				m_cursorColumn = column;
				kit::SetLabelText(m_ctx, doc->cursor, String::Format("Ln %d, Col %d", line, column));
			}
		}
	}
}

void DocumentArea::Status(const String &text) {
	if (onStatus)
		onStatus(text);
}

void DocumentArea::Open(CodeDocument doc, const String &text) {
	for (size_t i = 0; i < m_documents.size(); ++i) {
		if (m_documents[i].kind == doc.kind && m_documents[i].target == doc.target) {
			for (int page = 0; page < m_dock.Count(); ++page)
				if (m_dock.Page(page) == m_documents[i].page)
					m_dock.SetActive(page);
			return;
		}
	}
	const ui::Palette &theme = m_ctx.Theme();
	doc.page = m_dock.AddPage(doc.title, false, true, 0.f);
	doc.saved = text;

	// Barre du document : nom · état · position · actions.
	ui::WidgetBuilder bar = ui::Row();
	bar.Gap(6.f).Padding(math::Sides{8.f, 3.f}).GrowWidth().AlignItems(ui::Align::Center);
	ui::Widget barEntity = doc.page.Append(bar);
	(void)kit::Glyph(m_ctx, barEntity, doc.kind == DocumentKind::COMPONENT_JSON ? ui::MaterialIcons::DATA_OBJECT
									   : doc.kind == DocumentKind::TOOL_CONSOLE  ? ui::MaterialIcons::TERMINAL
																				 : ui::MaterialIcons::DESCRIPTION,
					 theme.muted, 15.f);
	ui::WidgetBuilder path = ui::Label(DescriptionOf(doc));
	path.FontSize(12.f).Color(theme.muted).Ellipsis();
	barEntity.Append(path);
	(void)kit::Spacer(m_ctx, barEntity);
	ui::WidgetBuilder status = ui::Label(doc.readOnly ? String("Lecture seule") : String());
	status.FontSize(12.f).Color(theme.muted);
	doc.status = barEntity.Append(status);
	ui::WidgetBuilder cursor = ui::Label(String("Ln 1, Col 1"));
	cursor.Size(96.f, 0.f).FontSize(12.f).Color(theme.muted);
	doc.cursor = barEntity.Append(cursor);
	if (doc.kind == DocumentKind::TOOL_CONSOLE) {
		ui::WidgetBuilder run = ui::Button(String("Exécuter"));
		run.FontSize(12.f).Tooltip(String("Ctrl+Entrée"));
		run.OnClick([this] { RunActive(); });
		barEntity.Append(run);
	} else if (!doc.readOnly) {
		ui::WidgetBuilder check = ui::Button(String("Vérifier"));
		check.FontSize(12.f);
		check.OnClick([this] { CheckActive(); });
		barEntity.Append(check);
		ui::WidgetBuilder save = ui::Button(String("Enregistrer"));
		save.FontSize(12.f).Tooltip(String("Ctrl+S"));
		save.OnClick([this] { (void)SaveActive(); });
		barEntity.Append(save);
		if (doc.kind == DocumentKind::LIBRARY_SCRIPT || doc.kind == DocumentKind::SCENE_SCRIPT) {
			ui::WidgetBuilder saveAs = ui::Button(String("Enregistrer sous…"));
			saveAs.FontSize(12.f).Tooltip(String("Écrit le script dans un fichier .script"));
			const String key = doc.kind == DocumentKind::SCENE_SCRIPT ? String("@") + doc.target : doc.target;
			saveAs.OnClick([this, key] {
				(void)SaveActive();
				if (onSaveScriptAs)
					onSaveScriptAs(key);
			});
			barEntity.Append(saveAs);
		}
	}

	ui::WidgetBuilder area = ui::CodeEditor(text, doc.kind == DocumentKind::COMPONENT_JSON
													  ? ui::SyntaxHighlighter(kit::syntax::HighlightJson)
													  : kit::syntax::ForName(doc.title));
	area.GrowWidth().GrowHeight().FontSize(13.f);
	if (doc.readOnly)
		area.EditMode(ui::IOMode::ReadAndCopy);
	doc.area = doc.page.Append(area);
	m_documents.push_back(std::move(doc));
	// AddPage a activé la page AVANT que le document soit enregistré :
	// on la réactive pour que l'appel d'activation le trouve.
	m_dock.SetActive(m_dock.Count() - 1);
}

String DocumentArea::DescriptionOf(const CodeDocument &doc) {
	switch (doc.kind) {
		case DocumentKind::LIBRARY_SCRIPT:
			return String::Format("Bibliothèque › %s.script", doc.target.CStr());
		case DocumentKind::SCENE_SCRIPT:
			return String::Format("Scène « %s » › script de jeu", doc.target.CStr());
		case DocumentKind::COMPONENT_JSON:
			return doc.component.IsEmpty() ? String("Propriétés libres du nœud (JSON)")
										   : String::Format("Composant %s (JSON)", doc.component.CStr());
		case DocumentKind::FILE:
			return doc.target;
		case DocumentKind::TOOL_CONSOLE:
			return String("Exécute dans l'éditeur (API complète)");
	}
	return String();
}

String DocumentArea::TextOf(const CodeDocument &doc) const {
	return doc.area.Text();
}

void DocumentArea::SetDocStatus(CodeDocument &doc, const String &text, bool error) {
	kit::SetLabelText(m_ctx, doc.status, text);
	ui::Widget status = doc.status;
	status.Color(error ? kit::PaletteOf(m_ctx).error : kit::PaletteOf(m_ctx).ok);
}

void DocumentArea::Check(CodeDocument &doc, const String &text) {
	if (doc.kind == DocumentKind::COMPONENT_JSON) {
		dataio::JsonDocument json;
		if (auto error = json.DecodeStr(text); error.IsSome()) {
			SetDocStatus(doc, String::Format("JSON invalide : %s", error.Unwrap().Format().CStr()), true);
			return;
		}
		SetDocStatus(doc, String("JSON valide"), false);
		return;
	}
	// Au-delà de la syntaxe : ce que le moteur fera du script (sa classe
	// Scene ou Behaviour, ou son refus au lancement), lu sans l'exécuter.
	const ScriptOutline outline =
		m_ctx.runtime.OutlineScript(text, doc.kind == DocumentKind::SCENE_SCRIPT ? ScriptUse::SCENE : ScriptUse::LIBRARY);
	if (outline.error.IsSome()) {
		const script::ScriptError &error = outline.error.Value();
		SetDocStatus(doc, String::Format("Ligne %d : %s", error.line, error.message.CStr()), true);
	} else if (outline.role == ScriptRole::INVALID) {
		SetDocStatus(doc, String::Format("Compile, mais %s", outline.problem.CStr()), true);
	} else {
		SetDocStatus(doc, String::Format("Compile ✓ — %s", outline.Summary().CStr()), false);
	}
}

void DocumentArea::Save(CodeDocument &doc) {
	if (doc.readOnly) {
		SetDocStatus(doc, String("Lecture seule : les ressources du disque ne sont pas réécrites"), true);
		return;
	}
	const String text = TextOf(doc);
	Option<script::ScriptError> error = NONE;
	switch (doc.kind) {
		case DocumentKind::LIBRARY_SCRIPT:
			error = m_ctx.runtime.SetScriptSource(doc.target, text);
			break;
		case DocumentKind::SCENE_SCRIPT:
			error = m_ctx.runtime.SetGameplayScript(SceneRef{doc.target}, text);
			break;
		case DocumentKind::COMPONENT_JSON: {
			dataio::JsonDocument json;
			if (auto parseError = json.DecodeStr(text); parseError.IsSome()) {
				SetDocStatus(doc, String::Format("JSON invalide : %s", parseError.Unwrap().Format().CStr()), true);
				return;
			}
			if (!m_ctx.runtime.SetComponentProps(doc.node, doc.component, scene::PropertyMap::FromJson(json.GetRoot()))) {
				SetDocStatus(doc, String("Le nœud n'existe plus"), true);
				return;
			}
			break;
		}
		case DocumentKind::FILE:
		case DocumentKind::TOOL_CONSOLE:
			return;
	}
	doc.saved = text;
	if (error.IsSome()) {
		SetDocStatus(doc, String::Format("Enregistré — ligne %d : %s", error.Unwrap().line, error.Unwrap().message.CStr()), true);
		m_ctx.runtime.LogWarning(String::Format("%s enregistré avec une erreur (ligne %d)", doc.title.CStr(), error.Unwrap().line));
	} else {
		SetDocStatus(doc, String("Enregistré ✓"), false);
		m_ctx.runtime.LogSuccess(String::Format("%s enregistré", doc.title.CStr()));
	}
}

} // namespace sandbox
