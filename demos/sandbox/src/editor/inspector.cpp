// Définitions de inspector.hpp
#include <sandbox/editor/inspector.hpp>
#include <sandbox/editor/modeling_session.hpp>

#include <sandbox/document/objects.hpp>

namespace sandbox {

// ── InspectorPanel ───────────────────────────────────────────────────────────

void InspectorPanel::Build(ui::Widget page) {
	m_page = page;
	MarkDirty();
}

void InspectorPanel::Teardown() {
	ClearPopups();
	m_page = ui::Widget();
}

void InspectorPanel::OnNodeChanged() {
	if (!m_localEdit)
		MarkDirty();
}

void InspectorPanel::Tick() {
	if (m_dirty && m_page.Valid()) {
		m_dirty = false;
		Refresh();
	}
	if (!m_pendingSection.IsEmpty())
		ScrollToPendingSection();
}

void InspectorPanel::FocusSection(const String &title) {
	m_open[title] = true;
	m_pendingSection = title;
	MarkDirty();
}

const char * InspectorPanel::SectionForTab(int tab) noexcept {
	switch (tab) {
		case 1:
			return "Matériau";
		case 2:
			return "Physique";
		case 3:
			return "Lumière";
		default:
			break;
	}
	return "Transform";
}

void InspectorPanel::ClearPopups() {
	for (ui::Widget popup : m_popups)
		if (popup.Valid())
			ui::Widget(popup).Destroy();
	m_popups.clear();
}

void InspectorPanel::Status(const String &text) {
	if (m_actions.status)
		m_actions.status(text);
}

void InspectorPanel::Refresh() {
	kit::ClearChildren(m_ctx, m_page);
	ClearPopups();
	m_sections.clear();
	const SceneDesc *scene = m_ctx.runtime.ActiveDocument();
	scene::Node *node = m_ctx.runtime.SelectedNode();
	if (!scene || !node) {
		(void)kit::Caption(m_ctx, m_page, String("Aucun nœud sélectionné"), 13.f);
		(void)kit::Caption(m_ctx, m_page,
						   String("Cliquez un objet dans la vue ou une ligne de l'arbre de scène."), 12.f);
		return;
	}
	const scene::NodeId id = node->id;
	kit::PropertyRows rows(m_ctx);
	BuildHeader(*scene, *node);

	// ── Nœud ──────────────────────────────────────────────────────────
	{
		ui::Widget s = Section("Nœud", String());
		(void)rows.ReadOnly(s, "Type", String::Format("%s (%s)", kit::LookOf(*node).label, node->type.CStr()));
		// Le chemin TYPÉ du nœud dans le projet : son document (scène ou
		// objet) puis son chemin dans l'arbre — « Nœud (objet) /objects/Torche/Flamme ».
		const NodeRef ref = m_ctx.runtime.GetProject().Fs().NodeRefOf(m_ctx.runtime.ActivePath(), id);
		(void)rows.ReadOnly(s, "Chemin", ref.FullPath().ToString());
		(void)rows.ReadOnly(s, "Nature", String(ProjectPathKindName(ref.FullPath().kind)));
		(void)rows.Text(s, "Étiquette", TagOf(*node), [this, id](const String &tag) {
			(void)m_ctx.runtime.SetTagOf(id, tag.Trim());
		});
		(void)rows.Check(s, "Visible", node->visible, [this, id](bool v) { (void)m_ctx.runtime.SetVisible(id, v); });
		(void)rows.Check(s, "Verrouillé", node->locked, [this, id](bool v) { (void)m_ctx.runtime.SetLocked(id, v); });
	}

	// ── Transform ─────────────────────────────────────────────────────
	if (Is2DNode(*node)) {
		// Nœud 2D : position et échelle planes (pixels), rotation autour de Z.
		ui::Widget s = Section("Transform", String());
		const scene::Transform t = node->transform;
		(void)rows.Vector2(s, "Position", {t.position.x, t.position.y}, 1.f,
						   Live([this, id, z = t.position.z](math::FVector2 v) {
							   (void)m_ctx.runtime.SetPosition(id, {v.x, v.y, z});
						   }));
		(void)rows.Number(s, "Rotation", t.EulerDegrees().z, -360.f, 360.f, 0.5f,
						  Live([this, id](float v) { (void)m_ctx.runtime.SetEulerDegrees(id, {0.f, 0.f, v}); }), 1);
		(void)rows.Vector2(s, "Échelle", {t.scale.x, t.scale.y}, 0.02f,
						   Live([this, id](math::FVector2 v) { (void)m_ctx.runtime.SetScale(id, {v.x, v.y, 1.f}); }),
						   -1000.f, 1000.f);
	} else {
		ui::Widget s = Section("Transform", String());
		(void)rows.Vector3(s, "Position", node->transform.position, 0.05f,
						   Live([this, id](math::FVector3 v) { (void)m_ctx.runtime.SetPosition(id, v); }));
		(void)rows.Vector3(s, "Rotation", node->transform.EulerDegrees(), 0.5f,
						   Live([this, id](math::FVector3 v) { (void)m_ctx.runtime.SetEulerDegrees(id, v); }), -360.f, 360.f);
		(void)rows.Vector3(s, "Échelle", node->transform.scale, 0.02f,
						   Live([this, id](math::FVector3 v) { (void)m_ctx.runtime.SetScale(id, v); }), 0.001f, 1000.f);
		if (scene->tree.ParentOf(id) != scene->tree.Root()) {
			const math::FVector3 world = scene->tree.GlobalPosition(id);
			(void)rows.ReadOnly(s, "Position monde",
								String::Format("%.2f   %.2f   %.2f", double(world.x), double(world.y), double(world.z)));
		}
	}

	if (sandbox::objects::IsInstance(*node))
		BuildInstanceSection(rows, *scene, id);
	if (VisualDesc::Has(*node))
		BuildVisualSections(rows, id, VisualDesc::Read(*node));
	if (LightDesc::Has(*node))
		BuildLightSections(rows, id, LightDesc::Read(*node));
	if (CameraNodeDesc::Has(*node))
		BuildCameraSection(rows, id, CameraNodeDesc::Read(*node));
	if (TriggerDesc::Has(*node))
		BuildTriggerSection(rows, id, TriggerDesc::Read(*node));
	if (AudioSourceDesc::Has(*node))
		BuildAudioSourceSection(rows, id, AudioSourceDesc::Read(*node));
	if (PhysicsDesc::Has(*node))
		BuildPhysicsSection(rows, id, PhysicsDesc::Read(*node));
	if (CanvasItemDesc::Has(*node))
		BuildCanvasItemSection(rows, id, CanvasItemDesc::Read(*node));
	if (Camera2DDesc::Has(*node))
		BuildCamera2DSection(rows, id, Camera2DDesc::Read(*node));
	if (ScriptRef::Has(*node))
		BuildScriptSection(rows, id, ScriptRef::Read(*node));
	BuildScriptObjects(rows, id);
	BuildCustomProperties(*node);
	BuildAddComponent(*node);
}

void InspectorPanel::BuildHeader(const SceneDesc &scene, const scene::Node &node) {
	const scene::NodeId id = node.id;
	const kit::NodeLook look = kit::LookOf(node);
	ui::WidgetBuilder head = ui::Row();
	head.Padding(0.f);
	head.Gap(6.f).GrowWidth().AlignItems(ui::Align::Center);
	ui::Widget headEntity = m_page.Append(head);
	(void)kit::Glyph(m_ctx, headEntity, look.icon, look.color, 18.f);
	ui::WidgetBuilder name = ui::Input();
	name.GrowWidth().Height(26.f).FontSize(14.f).Tooltip(String("Entrée pour renommer"));
	name.OnSubmit([this, id](const String &text) {
		if (!text.Trim().IsEmpty() && m_ctx.runtime.RenameNode(id, text.Trim()))
			Status(String::Format("Renommé : %s", text.Trim().CStr()));
	});
	ui::Widget nameEntity = headEntity.Append(name);
	nameEntity.SetText(node.name);

	ui::WidgetBuilder bar = ui::Row();
	bar.Padding(0.f);
	bar.Gap(4.f).GrowWidth().AlignItems(ui::Align::Center);
	ui::Widget barEntity = m_page.Append(bar);
	ui::WidgetBuilder title = ui::Label(String("Propriétés"));
	title.FontSize(13.f).Color(m_ctx.Theme().muted);
	barEntity.Append(title);
	(void)kit::Spacer(m_ctx, barEntity);

	ui::Widget menu = m_ctx.gui.Mount(ui::ContextMenu());
	m_popups.push_back(menu);
	AddMenuItem(menu, "Modifier les propriétés en JSON…", [this, id] {
		if (m_actions.editJson)
			m_actions.editJson(id, String());
	});
	AddMenuItem(menu, "Copier le chemin", [this, id, path = scene.tree.PathOf(id)] {
		(void)id;
		(void)SDL_SetClipboardText(path.CStr());
		Status(String::Format("Chemin copié : %s", path.CStr()));
	});
	AddMenuItem(menu, "Cadrer dans la vue", [this, id] { (void)m_ctx.runtime.FocusOn(id); });
	AttachMenu(kit::IconButton(m_ctx, barEntity, ui::MaterialIcons::MORE_VERT, String("Actions du nœud"), [] {}, 22.f),
			   menu);
}

ui::Widget InspectorPanel::Section(const char *title, const String &component) {
	const String key(title);
	const bool open = m_open.find(key) == m_open.end() || m_open[key];
	ui::WidgetBuilder section = ui::Expander(key, open);
	section.GrowWidth().Gap(4.f).FontSize(14.f);
	section.OnToggle([this, key](bool expanded) { m_open[key] = expanded; });
	ui::Widget entity = m_page.Append(section);
	m_sections.emplace_back(key, entity);
	if (!component.IsEmpty() && open) {
		const scene::NodeId id = m_ctx.runtime.SelectedId();
		ui::Widget menu = m_ctx.gui.Mount(ui::ContextMenu());
		m_popups.push_back(menu);
		AddMenuItem(menu, "Réinitialiser", [this, id, component] {
			(void)m_ctx.runtime.RemoveComponentOfType(id, component);
			(void)m_ctx.runtime.AddComponentOfType(id, component);
		});
		AddMenuItem(menu, "Retirer le composant", [this, id, component] {
			if (m_ctx.runtime.RemoveComponentOfType(id, component))
				Status(String::Format("Composant retiré : %s", component.CStr()));
		});
		AddMenuItem(menu, "Modifier en JSON…", [this, id, component] {
			if (m_actions.editJson)
				m_actions.editJson(id, component);
		});
		// ⋮ dans l'en-tête de la section : positionné en ABSOLU dans la
		// bande d'en-tête que l'Expander réserve par son padding haut.
		ui::WidgetBuilder button = ui::Button(String());
		button.Size(22.f, 22.f).Background(sdl3::FColor{0.f, 0.f, 0.f, 0.f}).BorderRadius(4.f).Tooltip(String("Actions du composant"));
		// L'ancre se résout dans la boîte de CONTENU (sous le padding qui
		// réserve l'en-tête) : on remonte donc de la hauteur d'en-tête.
		const float header = m_ctx.Theme().fontSize + 14.f + 6.f;
		button.Absolute().Anchor(ui::Anchor::TopRight).Offset(0.f, -header + 3.f);
		ui::Widget buttonEntity = entity.Append(button);
		ui::WidgetBuilder glyph = ui::Icon(ui::MaterialIcons::MORE_VERT, 15.f);
		glyph.Absolute().Anchor(ui::Anchor::Center).JustifyContent(ui::Justify::Center).PointerThrough();
		buttonEntity.Append(glyph);
		AttachMenu(buttonEntity, menu);
	}
	return entity;
}

void InspectorPanel::AddMenuItem(ui::Widget menu, const char *text, std::function<void()> action) {
	ui::WidgetBuilder item = ui::MenuItem(String(text));
	item.OnClick(std::move(action));
	menu.Append(item);
}

void InspectorPanel::AttachMenu(ui::Widget button, ui::Widget menu) {
	button.OnClick([this, button, menu]() mutable {
			if (ui::Widget(button)) {
				const sdl3::FRect r = ui::Widget(button).Bounds();
				menu.OpenAt(sdl3::FPoint{r.x + r.w - 200.f, r.y + r.h});
			}
		});
}

void InspectorPanel::BuildVisualSections(kit::PropertyRows &rows, scene::NodeId id, const VisualDesc &visual) {
	{
		ui::Widget s = Section("Maillage", String(component::VISUAL));
		if (visual.shape == ShapeKind::MODEL) {
			(void)rows.ReadOnly(s, "Modèle", visual.source);
		} else if (visual.shape == ShapeKind::MESH) {
			BuildModeledMeshRows(rows, s, id, visual);
		} else {
			std::vector<String> shapes;
			for (ShapeKind shape : EDITABLE_SHAPES)
				shapes.push_back(String(ShapeLabel(shape)));
			int current = 0;
			for (size_t i = 0; i < std::size(EDITABLE_SHAPES); ++i)
				if (EDITABLE_SHAPES[i] == visual.shape)
					current = int(i);
			(void)rows.Choice(s, "Forme", std::move(shapes), current, [this, id](int index) {
				MutateVisual(id, [index](VisualDesc &v) { v.shape = EDITABLE_SHAPES[size_t(index)]; });
				MarkDirty();
			});
			(void)rows.Vector3(s, "Dimensions", visual.dimensions, 0.02f, Live([this, id](math::FVector3 d) {
				MutateVisual(id, [d](VisualDesc &v) { v.dimensions = d; });
			}), 0.01f, 500.f);
			(void)rows.Number(s, "Segments", float(visual.segments), 3.f, 128.f, 0.2f, Live([this, id](float n) {
				MutateVisual(id, [n](VisualDesc &v) { v.segments = int(n); });
			}), 0);
			ui::WidgetBuilder convert = ui::Button(String("Convertir et modéliser (faces éditables)"));
			convert.GrowWidth().FontSize(12.f);
			convert.Tooltip(String("Remplace la primitive par un maillage modélisé, éditable à la SketchUp"));
			convert.OnClick([this, id] {
				if (m_actions.beginModeling)
					m_actions.beginModeling(id);
			});
			s.Append(convert);
		}
	}
	{
		const MaterialDesc material = visual.material;
		ui::Widget s = Section("Matériau", String());
		std::vector<String> kinds;
		for (MaterialKind kind : {MaterialKind::PLASTIC, MaterialKind::METAL, MaterialKind::WOOD, MaterialKind::PBR,
								  MaterialKind::UNLIT, MaterialKind::BASIC})
			kinds.push_back(String(MaterialKindName(kind)));
		(void)rows.Choice(s, "Famille", std::move(kinds), int(material.kind), [this, id](int index) {
			MutateMaterial(id, [index](MaterialDesc &m) { m.kind = MaterialKind(index); });
		});
		(void)rows.Color(s, "Couleur", material.baseColor, Live([this, id](sdl3::Color c) {
			(void)m_ctx.runtime.SetMaterialColor(id, c);
		}));
		(void)rows.Slider(s, "Métallique", material.metallic, 0.f, 1.f, Live([this, id](float v) {
			MutateMaterial(id, [v](MaterialDesc &m) { m.metallic = v; });
		}));
		(void)rows.Slider(s, "Rugosité", material.roughness, 0.f, 1.f, Live([this, id](float v) {
			MutateMaterial(id, [v](MaterialDesc &m) { m.roughness = v; });
		}));
		(void)rows.Check(s, "Double face", material.doubleSided, [this, id](bool v) {
			MutateMaterial(id, [v](MaterialDesc &m) { m.doubleSided = v; });
		});
		(void)rows.Check(s, "Fil de fer", material.wireframe, [this, id](bool v) {
			MutateMaterial(id, [v](MaterialDesc &m) { m.wireframe = v; });
		});
	}
}

void InspectorPanel::BuildModeledMeshRows(kit::PropertyRows &rows, ui::Widget s, scene::NodeId id,
										  const VisualDesc &visual) {
	(void)rows.ReadOnly(s, "Fichier", visual.source);
	Result<modeling::Model *, String> model = m_ctx.runtime.Meshes().Get(visual.source);
	if (model.IsError()) {
		(void)kit::Caption(m_ctx, s, model.Error(), 11.f);
		return;
	}
	size_t faces = 0, edges = 0;
	for (const modeling::Face &f : model.Value()->Faces())
		faces += f.alive ? 1 : 0;
	for (const modeling::Edge &e : model.Value()->Edges())
		edges += e.alive ? 1 : 0;
	(void)rows.ReadOnly(s, "Géométrie", String::Format("%d faces, %d arêtes", int(faces), int(edges)));
	const bool editing = m_ctx.modeling && m_ctx.modeling->IsActive() && m_ctx.modeling->Node() == id;
	ui::WidgetBuilder edit = ui::Button(String(editing ? "Terminer la modélisation (Échap)" : "Modéliser"));
	edit.GrowWidth().FontSize(12.f);
	edit.OnClick([this, id, editing] {
		if (editing && m_actions.endModeling)
			m_actions.endModeling();
		else if (!editing && m_actions.beginModeling)
			m_actions.beginModeling(id);
	});
	s.Append(edit);
	ui::WidgetBuilder row = ui::Row();
	row.GrowWidth().Gap(4.f).Padding(0.f);
	ui::Widget rowEntity = s.Append(row);
	for (const char *extension : {"obj", "stl", "ply"}) {
		ui::WidgetBuilder button = ui::Button(String::Format("Exporter .%s", extension));
		button.GrowWidth().FontSize(12.f);
		button.OnClick([this, extension] {
			if (m_actions.exportMesh)
				m_actions.exportMesh(extension);
		});
		rowEntity.Append(button);
	}
}

void InspectorPanel::BuildLightSections(kit::PropertyRows &rows, scene::NodeId id, const LightDesc &light) {
	{
		ui::Widget s = Section("Lumière", String(component::LIGHT));
		(void)rows.Choice(s, "Type", {String("Ponctuelle"), String("Projecteur")}, int(light.kind), [this, id](int index) {
			MutateLight(id, [index](LightDesc &l) { l.kind = LightKind(index); });
			MarkDirty();
		});
		(void)rows.Color(s, "Couleur", light.color, Live([this, id](sdl3::Color c) {
			MutateLight(id, [c](LightDesc &l) { l.color = c; });
		}));
		(void)rows.Number(s, "Intensité", light.intensity, 0.f, 50.f, 0.02f, Live([this, id](float v) {
			MutateLight(id, [v](LightDesc &l) { l.intensity = v; });
		}));
		(void)rows.Number(s, "Portée", light.range, 0.f, 500.f, 0.05f, Live([this, id](float v) {
			MutateLight(id, [v](LightDesc &l) { l.range = v; });
		}));
		if (light.kind == LightKind::SPOT) {
			(void)rows.Number(s, "Angle", light.spotAngle, 1.f, 89.f, 0.2f, Live([this, id](float v) {
				MutateLight(id, [v](LightDesc &l) { l.spotAngle = v; });
			}), 1);
			(void)rows.Slider(s, "Pénombre", light.penumbra, 0.f, 1.f, Live([this, id](float v) {
				MutateLight(id, [v](LightDesc &l) { l.penumbra = v; });
			}));
		}
	}
	{
		ui::Widget s = Section("Ombres", String());
		(void)rows.Check(s, "Activées", light.castShadow, [this, id](bool v) {
			MutateLight(id, [v](LightDesc &l) { l.castShadow = v; });
		});
		(void)kit::Caption(m_ctx, s, String("Une seule lumière ponctuelle ombrée à la fois (la première)."), 11.f);
	}
}

void InspectorPanel::BuildCameraSection(kit::PropertyRows &rows, scene::NodeId id, const CameraNodeDesc &camera) {
	ui::Widget s = Section("Caméra", String(component::CAMERA));
	(void)rows.Number(s, "Champ de vision", camera.fovDegrees, 20.f, 120.f, 0.2f, Live([this, id](float v) {
		CameraNodeDesc updated = CurrentCamera(id);
		updated.fovDegrees = v;
		(void)m_ctx.runtime.SetCameraNode(id, updated);
	}), 1);
	(void)rows.Check(s, "Caméra du jeu", camera.current, [this, id](bool v) {
		CameraNodeDesc updated = CurrentCamera(id);
		updated.current = v;
		(void)m_ctx.runtime.SetCameraNode(id, updated);
	});
	(void)kit::Caption(m_ctx, s, String("En mode Jeu, la vue part de la caméra marquée « du jeu »."), 11.f);
}

void InspectorPanel::BuildTriggerSection(kit::PropertyRows &rows, scene::NodeId id, const TriggerDesc &trigger) {
	ui::Widget s = Section("Déclencheur", String(component::TRIGGER));
	(void)rows.Vector3(s, "Demi-taille", trigger.halfExtents, 0.02f, Live([this, id](math::FVector3 v) {
		TriggerDesc updated = CurrentTrigger(id);
		updated.halfExtents = v;
		(void)m_ctx.runtime.SetTrigger(id, updated);
	}), 0.01f, 500.f);
	(void)rows.Text(s, "Évènement", trigger.event, [this, id](const String &event) {
		TriggerDesc updated = CurrentTrigger(id);
		updated.event = event.Trim();
		(void)m_ctx.runtime.SetTrigger(id, updated);
	});
	(void)rows.Check(s, "Une seule fois", trigger.once, [this, id](bool v) {
		TriggerDesc updated = CurrentTrigger(id);
		updated.once = v;
		(void)m_ctx.runtime.SetTrigger(id, updated);
	});
	(void)kit::Caption(m_ctx, s, String("Appelle on_trigger(zone, objet, évènement) quand le joueur ou un corps dynamique y entre."), 11.f);
}

void InspectorPanel::BuildAudioSourceSection(kit::PropertyRows &rows, scene::NodeId id, const AudioSourceDesc &source) {
	ui::Widget s = Section("Source audio", String(component::AUDIO_SOURCE));
	(void)rows.Text(s, "Son", source.clip, [this, id](const String &clip) {
		MutateAudioSource(id, [clip](AudioSourceDesc &a) { a.clip = clip.Trim(); });
		MarkDirty();
	});
	(void)rows.Slider(s, "Volume", source.volume, 0.f, 2.f, Live([this, id](float v) {
		MutateAudioSource(id, [v](AudioSourceDesc &a) { a.volume = v; });
	}));
	(void)rows.Check(s, "En boucle", source.loop, [this, id](bool v) {
		MutateAudioSource(id, [v](AudioSourceDesc &a) { a.loop = v; });
	});
	(void)rows.Check(s, "Jouer au démarrage", source.playOnStart, [this, id](bool v) {
		MutateAudioSource(id, [v](AudioSourceDesc &a) { a.playOnStart = v; });
	});
	(void)rows.Check(s, "Spatialisé", source.spatial, [this, id](bool v) {
		MutateAudioSource(id, [v](AudioSourceDesc &a) { a.spatial = v; });
	});
	// Écoute : le son tel quel, sans spatialisation ni boucle.
	ui::WidgetBuilder listen = ui::Button(String("Écouter / arrêter"));
	listen.GrowWidth().Height(26.f);
	ui::Widget button = s.Append(listen);
	button.OnClick([this, id] {
			if (m_ctx.runtime.IsPreviewing()) {
				m_ctx.runtime.StopPreview();
				return;
			}
			const scene::Node *node = m_ctx.runtime.FindNode(id);
			if (node && AudioSourceDesc::Has(*node))
				(void)m_ctx.runtime.PreviewSound(AudioSourceDesc::Read(*node).clip);
		});
	(void)kit::Caption(m_ctx, s, String("Glisser un son de l'explorateur dans la vue crée une source. Scripts : audio.play_source(nom)."), 11.f);
}

void InspectorPanel::BuildPhysicsSection(kit::PropertyRows &rows, scene::NodeId id, const PhysicsDesc &physics) {
	ui::Widget s = Section("Physique", String(component::BODY));
	(void)rows.Choice(s, "Corps", {String("aucun"), String("statique"), String("dynamique")}, int(physics.body),
					  [this, id](int index) { MutatePhysics(id, [index](PhysicsDesc &p) { p.body = BodyKind(index); }); });
	(void)rows.Choice(s, "Collision", {String("boîte"), String("sphère"), String("capsule")}, int(physics.collider),
					  [this, id](int index) {
						  MutatePhysics(id, [index](PhysicsDesc &p) { p.collider = ColliderKind(index); });
					  });
	(void)rows.Vector3(s, "Demi-taille", physics.halfExtents, 0.02f, Live([this, id](math::FVector3 v) {
		MutatePhysics(id, [v](PhysicsDesc &p) { p.halfExtents = v; });
	}), 0.01f, 500.f);
	(void)rows.Number(s, "Masse", physics.mass, 0.01f, 10000.f, 0.5f, Live([this, id](float v) {
		MutatePhysics(id, [v](PhysicsDesc &p) { p.mass = v; });
	}));
	(void)rows.Slider(s, "Rebond", physics.restitution, 0.f, 1.f, Live([this, id](float v) {
		MutatePhysics(id, [v](PhysicsDesc &p) { p.restitution = v; });
	}));
	(void)rows.Slider(s, "Frottement", physics.friction, 0.f, 2.f, Live([this, id](float v) {
		MutatePhysics(id, [v](PhysicsDesc &p) { p.friction = v; });
	}));
	ui::WidgetBuilder impulse = ui::Button(String("Impulsion vers le haut (mode Jeu)"));
	impulse.GrowWidth().FontSize(12.f);
	impulse.OnClick([this, id] {
		if (const scene::Node *node = m_ctx.runtime.FindNode(id))
			(void)m_ctx.runtime.ApplyImpulse(node->name, math::FVector3{0.f, 240.f, 0.f});
	});
	s.Append(impulse);
}

void InspectorPanel::BuildCanvasItemSection(kit::PropertyRows &rows, scene::NodeId id, const CanvasItemDesc &item) {
	ui::Widget s = Section("Apparence 2D", String(component::CANVAS_ITEM));
	std::vector<String> kinds;
	int current = 0;
	for (size_t i = 0; i < std::size(CANVAS_ITEM_KINDS); ++i) {
		kinds.push_back(String(CanvasItemKindLabel(CANVAS_ITEM_KINDS[i])));
		if (CANVAS_ITEM_KINDS[i] == item.kind)
			current = int(i);
	}
	(void)rows.Choice(s, "Forme", kinds, current, [this, id](int index) {
		MutateCanvasItem(id, [index](CanvasItemDesc &c) {
			c.kind = CANVAS_ITEM_KINDS[size_t(index)];
			if ((c.kind == CanvasItemKind::POLYGON || c.kind == CanvasItemKind::LINE) && c.points.empty())
				c.points = {{-c.size.x * 0.5f, c.size.y * 0.5f}, {0.f, -c.size.y * 0.5f}, {c.size.x * 0.5f, c.size.y * 0.5f}};
			if (c.kind == CanvasItemKind::TEXT && c.text.IsEmpty())
				c.text = String("Texte");
		});
		MarkDirty();
	});
	(void)rows.Color(s, item.kind == CanvasItemKind::SPRITE ? "Teinte" : "Couleur", item.color, Live([this, id](sdl3::Color c) { MutateCanvasItem(id, [c](CanvasItemDesc &d) { d.color = c; }); }));
	switch (item.kind) {
		case CanvasItemKind::TEXT:
			(void)rows.Text(s, "Texte", item.text, [this, id](const String &text) {
				MutateCanvasItem(id, [text](CanvasItemDesc &d) { d.text = text; });
			});
			(void)rows.Number(s, "Police", item.fontSize, 4.f, 400.f, 0.2f, Live([this, id](float v) {
				MutateCanvasItem(id, [v](CanvasItemDesc &d) { d.fontSize = v; });
			}), 0);
			(void)rows.Choice(s, "Alignement", {String("à gauche"), String("centré"), String("à droite")}, int(item.align),
							  [this, id](int index) {
								  MutateCanvasItem(id, [index](CanvasItemDesc &d) { d.align = TextAlign2D(index); });
							  });
			break;
		case CanvasItemKind::POLYGON:
		case CanvasItemKind::LINE:
			(void)rows.ReadOnly(s, "Sommets", String::Format("%d", int(item.points.size())));
			(void)kit::Caption(m_ctx, s, String("Les sommets se modifient en JSON (menu ⋮ de la section)."), 11.f);
			break;
		default:
			(void)rows.Vector2(s, "Taille", item.size, 1.f, Live([this, id](math::FVector2 v) {
				MutateCanvasItem(id, [v](CanvasItemDesc &d) { d.size = {std::max(0.f, v.x), std::max(0.f, v.y)}; });
			}), 0.f, 100000.f);
			break;
	}
	if (item.kind == CanvasItemKind::SPRITE) {
		(void)rows.Text(s, "Image", item.texture, [this, id](const String &path) {
			MutateCanvasItem(id, [path](CanvasItemDesc &d) { d.texture = path.Trim(); });
		});
		(void)rows.Check(s, "Miroir H", item.flipH, [this, id](bool v) {
			MutateCanvasItem(id, [v](CanvasItemDesc &d) { d.flipH = v; });
		});
		(void)rows.Check(s, "Miroir V", item.flipV, [this, id](bool v) {
			MutateCanvasItem(id, [v](CanvasItemDesc &d) { d.flipV = v; });
		});
	}
	if (item.kind == CanvasItemKind::LINE) {
		(void)rows.Number(s, "Épaisseur", item.outline, 0.f, 256.f, 0.1f, Live([this, id](float v) {
			MutateCanvasItem(id, [v](CanvasItemDesc &d) { d.outline = v; });
		}), 1);
	} else if (item.kind != CanvasItemKind::SPRITE && item.kind != CanvasItemKind::TEXT) {
		(void)rows.Check(s, "Rempli", item.filled, [this, id](bool v) {
			MutateCanvasItem(id, [v](CanvasItemDesc &d) { d.filled = v; });
		});
		(void)rows.Number(s, "Contour", item.outline, 0.f, 256.f, 0.1f, Live([this, id](float v) {
			MutateCanvasItem(id, [v](CanvasItemDesc &d) { d.outline = v; });
		}), 1);
		(void)rows.Color(s, "Couleur du contour", item.outlineColor, Live([this, id](sdl3::Color c) {
			MutateCanvasItem(id, [c](CanvasItemDesc &d) { d.outlineColor = c; });
		}));
	}
	(void)rows.Check(s, "Pivot au centre", item.centered, [this, id](bool v) {
		MutateCanvasItem(id, [v](CanvasItemDesc &d) { d.centered = v; });
	});
	(void)rows.Number(s, "Profondeur (z)", float(item.z), -1000.f, 1000.f, 0.1f, Live([this, id](float v) {
		MutateCanvasItem(id, [v](CanvasItemDesc &d) { d.z = int(std::lround(v)); });
	}), 0);
	(void)kit::Caption(m_ctx, s, String("Les grands z passent devant ; à z égal, l'ordre de l'arbre."), 11.f);
}

void InspectorPanel::BuildCamera2DSection(kit::PropertyRows &rows, scene::NodeId id, const Camera2DDesc &camera) {
	ui::Widget s = Section("Caméra 2D", String(component::CAMERA_2D));
	(void)rows.Number(s, "Zoom", camera.zoom, 0.05f, 20.f, 0.01f, Live([this, id](float v) {
		if (const scene::Node *node = m_ctx.runtime.FindNode(id)) {
			Camera2DDesc updated = Camera2DDesc::Read(*node);
			updated.zoom = v;
			(void)m_ctx.runtime.SetCamera2D(id, updated);
		}
	}));
	(void)rows.Check(s, "Caméra du jeu", camera.current, [this, id](bool v) {
		SceneDesc *scene = m_ctx.runtime.ActiveDocument();
		if (!scene)
			return;
		// Une seule caméra 2D du jeu : cocher celle-ci décoche les autres.
		for (scene::NodeId other : scene->Nodes())
			if (const scene::Node *node = scene->tree.Get(other); node && Camera2DDesc::Has(*node)) {
				Camera2DDesc updated = Camera2DDesc::Read(*node);
				const bool wanted = other == id ? v : (v ? false : updated.current);
				if (wanted != updated.current) {
					updated.current = wanted;
					(void)m_ctx.runtime.SetCamera2D(other, updated);
				}
			}
		MarkDirty();
	});
	(void)kit::Caption(m_ctx, s, String("En mode Jeu, la caméra 2D du jeu est au centre de l'écran."), 11.f);
}

void InspectorPanel::MutateCanvasItem(scene::NodeId id, const std::function<void(CanvasItemDesc &)> &change) {
	const scene::Node *node = m_ctx.runtime.FindNode(id);
	if (!node)
		return;
	CanvasItemDesc item = CanvasItemDesc::Read(*node);
	change(item);
	(void)m_ctx.runtime.SetCanvasItem(id, item);
}

void InspectorPanel::BuildScriptSection(kit::PropertyRows &rows, scene::NodeId id, const ScriptRef &ref) {
	ui::Widget s = Section("Script", String(component::SCRIPT));
	const Project &project = m_ctx.runtime.GetProject();
	std::vector<String> names{String("(aucun)")};
	int current = 0;
	for (const ScriptAsset &script : project.scripts) {
		if (script.name == ref.script)
			current = int(names.size());
		names.push_back(script.name);
	}
	(void)rows.Choice(s, "Script", names, current, [this, id, names](int index) {
		(void)m_ctx.runtime.SetScriptRef(id, index <= 0 ? String() : names[size_t(index)]);
	});
	const ScriptAsset *asset = project.FindScript(ref.script);
	if (!asset) {
		(void)rows.ReadOnly(s, "État", String::Format("« %s » introuvable", ref.script.CStr()));
	} else {
		// Ce que le moteur fera de ce script au lancement, lu sans l'exécuter :
		// un module (aucune Behaviour) ne s'attache à rien.
		const ScriptOutline outline = m_ctx.runtime.OutlineScript(asset->source, ScriptUse::LIBRARY);
		kit::ScriptBadge badge = kit::BadgeOf(m_ctx, outline);
		if (outline.error.IsNone() && outline.role != ScriptRole::BEHAVIOUR) {
			badge.icon = ui::MaterialIcons::WARNING;
			badge.color = kit::PaletteOf(m_ctx).warning;
			badge.text = outline.attachProblem.IsEmpty()
							 ? String("aucune classe dérivée de Behaviour : rien ne s'attachera au nœud")
							 : outline.attachProblem;
		}
		ui::Widget status = rows.Row(s, "État");
		(void)kit::Glyph(m_ctx, status, badge.icon, badge.color, 15.f);
		ui::WidgetBuilder text = ui::Label(badge.text);
		text.GrowWidth().FontSize(12.f).Ellipsis().Tooltip(badge.text);
		status.Append(text);
		if (const ScriptClassInfo *main = outline.Find(outline.mainClass)) {
			(void)rows.ReadOnly(s, "Classe", main->Signature());
			String hooks;
			for (const String &hook : main->hooks) {
				hooks.Concat(hooks.IsEmpty() ? "" : ", ");
				hooks.Concat(hook);
			}
			(void)rows.ReadOnly(s, "Rappels", hooks.IsEmpty() ? String("(aucun)") : hooks);
		}
		if (!outline.imports.empty()) {
			String imports;
			for (const String &module : outline.imports) {
				imports.Concat(imports.IsEmpty() ? "" : ", ");
				imports.Concat(module);
			}
			(void)rows.ReadOnly(s, "Importe", imports);
		}
		if (!asset->description.IsEmpty())
			(void)kit::Caption(m_ctx, s, asset->description, 11.f);
	}
	ui::WidgetBuilder open = ui::Button(String("Ouvrir dans l'éditeur de code"));
	open.GrowWidth().FontSize(12.f);
	open.OnClick([this, name = ref.script] {
		if (m_actions.openScript && !name.IsEmpty())
			m_actions.openScript(name);
	});
	s.Append(open);
}

void InspectorPanel::BuildInstanceSection(kit::PropertyRows &rows, const SceneDesc &scene, scene::NodeId id) {
	namespace obj = sandbox::objects;
	const scene::Node *node = scene.tree.Get(id);
	const String source = obj::SourceOf(*node);
	const bool exists = m_ctx.runtime.GetProject().Fs().FindObject(ObjectRef{source}) != nullptr;
	int generated = 0;
	scene.tree.Traverse(id, [&](scene::NodeId, const scene::Node &child) { generated += obj::IsGenerated(child) ? 1 : 0; });

	ui::Widget s = Section("Instance d'objet", String());
	(void)rows.ReadOnly(s, "Objet", exists ? source : source + String(" (introuvable)"));
	(void)rows.ReadOnly(s, "Contenu", String::Format("%d nœud(s) générés depuis l'objet", generated));
	(void)kit::Caption(m_ctx, s,
					   String("Position, rotation et taille appartiennent à l'instance ; le contenu suit l'objet. "
							  "Ouvrez l'objet pour le modifier dans toutes les scènes."),
					   11.f);
	ui::WidgetBuilder buttons = ui::Row();
	buttons.Gap(6.f).GrowWidth();
	ui::Widget row = s.Append(buttons);
	ui::WidgetBuilder open = ui::Button(String("Ouvrir l'objet"));
	open.GrowWidth().FontSize(12.f);
	if (!exists)
		open.Disabled();
	open.OnClick([this, source] {
		if (m_ctx.runtime.OpenObject(ObjectRef{source}))
			Status(String::Format("Objet ouvert : %s", source.CStr()));
	});
	row.Append(open);
	ui::WidgetBuilder detach = ui::Button(String("Rendre indépendant"));
	detach.GrowWidth().FontSize(12.f).Tooltip(String("Le contenu devient des nœuds propres à ce document"));
	detach.OnClick([this, id] {
		auto done = m_ctx.runtime.DetachInstance(id);
		Status(done.IsOk() ? String("Instance rendue indépendante")
						   : String::Format("Impossible : %s", done.Error().CStr()));
	});
	row.Append(detach);
}

void InspectorPanel::BuildScriptObjects(kit::PropertyRows &rows, scene::NodeId id) {
	std::vector<ScriptObjectInfo> objects = m_ctx.runtime.ScriptObjectsOf(id);
	if (objects.empty())
		return;
	// Les objets de script qui PORTENT ce nœud (Behaviour attachée, Mesh3D
	// créé par un script…) : lus dans les registres C++, sans exécuter de
	// script. Les champs sont un instantané : « Actualiser » le reprend.
	ui::Widget s = Section("Objets de script", String());
	constexpr size_t MAX_FIELDS = 16;
	for (const ScriptObjectInfo &info : objects) {
		ui::Widget head = rows.Row(s, info.attached ? "Behaviour" : "Objet");
		(void)kit::Glyph(m_ctx, head, info.destroyed ? ui::MaterialIcons::ERROR : ui::MaterialIcons::CHECK_CIRCLE,
						 info.destroyed ? kit::PaletteOf(m_ctx).error : kit::syntax::Colors().type, 15.f);
		ui::WidgetBuilder title = ui::Label(info.Signature());
		title.GrowWidth().FontSize(12.f).Ellipsis().Tooltip(String::Format("créé par : %s", info.origin.CStr()));
		head.Append(title);
		const std::vector<std::pair<String, String>> fields = info.Fields();
		for (size_t i = 0; i < fields.size() && i < MAX_FIELDS; ++i)
			(void)rows.ReadOnly(s, fields[i].first.CStr(), fields[i].second);
		if (fields.size() > MAX_FIELDS)
			(void)kit::Caption(m_ctx, s, String::Format("… %d autres champs", int(fields.size() - MAX_FIELDS)), 11.f);
		if (info.destroyed)
			continue;
		ui::WidgetBuilder destroy = ui::Button(String("Détruire (on_destroy → deinit → bases)"));
		destroy.GrowWidth().FontSize(12.f);
		destroy.OnClick([this, info] {
			if (m_ctx.runtime.DestroyScriptObject(info))
				Status(String::Format("%s détruit", info.className.CStr()));
			Refresh();
		});
		s.Append(destroy);
	}
	ui::WidgetBuilder refresh = ui::Button(String("Actualiser les champs"));
	refresh.GrowWidth().FontSize(12.f);
	refresh.OnClick([this] { Refresh(); });
	s.Append(refresh);
}

void InspectorPanel::BuildCustomProperties(const scene::Node &node) {
	if (node.properties.Size() == 0)
		return;
	ui::Widget s = Section("Propriétés personnalisées", String());
	kit::PropertyRows rows(m_ctx);
	for (const auto &[key, value] : node.properties.Entries())
		(void)rows.ReadOnly(s, key.CStr(), value.ToDisplayString());
	ui::WidgetBuilder edit = ui::Button(String("Modifier en JSON…"));
	edit.GrowWidth().FontSize(12.f);
	edit.OnClick([this, id = node.id] {
		if (m_actions.editJson)
			m_actions.editJson(id, String());
	});
	s.Append(edit);
}

void InspectorPanel::BuildAddComponent(const scene::Node &node) {
	ui::Widget menu = m_ctx.gui.Mount(ui::ContextMenu());
	m_popups.push_back(menu);
	int available = 0;
	for (const String &type : Runtime::AddableComponents()) {
		if (node.HasComponent(type))
			continue;
		++available;
		const scene::NodeId id = node.id;
		AddMenuItem(menu, ComponentLabel(type), [this, id, type] {
			if (m_ctx.runtime.AddComponentOfType(id, type))
				Status(String::Format("Composant ajouté : %s", ComponentLabel(type)));
			MarkDirty();
		});
	}
	if (available == 0)
		return;
	ui::WidgetBuilder add = ui::Button(String("Ajouter un composant"));
	add.GrowWidth().Height(28.f);
	ui::Widget button = m_page.Append(add);
	AttachMenu(button, menu);
}

void InspectorPanel::ScrollToPendingSection() {
	for (const auto &[title, entity] : m_sections) {
		if (title != m_pendingSection)
			continue;
		ui::Widget section = entity, page = m_page;
		if (!section || !page || section.Bounds().h <= 0.f)
			return; // mise en page pas encore faite : réessai à l'image suivante
		const sdl3::FPoint scroll = page.ScrollOffset();
		page.ScrollTo({scroll.x, scroll.y + section.Bounds().y - page.Bounds().y - 4.f});
		break;
	}
	m_pendingSection = String();
}

void InspectorPanel::MutateVisual(scene::NodeId id, const std::function<void(VisualDesc &)> &change) {
	const scene::Node *node = m_ctx.runtime.FindNode(id);
	if (!node)
		return;
	VisualDesc visual = VisualDesc::Read(*node);
	change(visual);
	(void)m_ctx.runtime.SetVisual(id, visual);
}

void InspectorPanel::MutateMaterial(scene::NodeId id, const std::function<void(MaterialDesc &)> &change) {
	const scene::Node *node = m_ctx.runtime.FindNode(id);
	if (!node)
		return;
	MaterialDesc material = VisualDesc::Read(*node).material;
	change(material);
	(void)m_ctx.runtime.SetMaterial(id, material);
}

void InspectorPanel::MutateLight(scene::NodeId id, const std::function<void(LightDesc &)> &change) {
	const scene::Node *node = m_ctx.runtime.FindNode(id);
	if (!node)
		return;
	LightDesc light = LightDesc::Read(*node);
	change(light);
	(void)m_ctx.runtime.SetLight(id, light);
}

void InspectorPanel::MutatePhysics(scene::NodeId id, const std::function<void(PhysicsDesc &)> &change) {
	const scene::Node *node = m_ctx.runtime.FindNode(id);
	if (!node)
		return;
	PhysicsDesc physics = PhysicsDesc::Read(*node);
	change(physics);
	(void)m_ctx.runtime.SetPhysics(id, physics);
}

CameraNodeDesc InspectorPanel::CurrentCamera(scene::NodeId id) {
	const scene::Node *node = m_ctx.runtime.FindNode(id);
	return node ? CameraNodeDesc::Read(*node) : CameraNodeDesc{};
}

void InspectorPanel::MutateAudioSource(scene::NodeId id, const std::function<void(AudioSourceDesc &)> &change) {
	const scene::Node *node = m_ctx.runtime.FindNode(id);
	if (!node)
		return;
	AudioSourceDesc source = AudioSourceDesc::Read(*node);
	change(source);
	(void)m_ctx.runtime.SetAudioSource(id, source);
}

TriggerDesc InspectorPanel::CurrentTrigger(scene::NodeId id) {
	const scene::Node *node = m_ctx.runtime.FindNode(id);
	return node ? TriggerDesc::Read(*node) : TriggerDesc{};
}

const char * InspectorPanel::ComponentLabel(const String &type) {
	if (type == component::VISUAL)
		return "Maillage";
	if (type == component::AUDIO_SOURCE)
		return "Source audio";
	if (type == component::BODY)
		return "Physique";
	if (type == component::LIGHT)
		return "Lumière";
	if (type == component::CAMERA)
		return "Caméra";
	if (type == component::TRIGGER)
		return "Déclencheur";
	if (type == component::SCRIPT)
		return "Script";
	return "Composant";
}

const char * InspectorPanel::ShapeLabel(ShapeKind shape) noexcept {
	switch (shape) {
		case ShapeKind::BOX:
			return "Cube";
		case ShapeKind::SPHERE:
			return "Sphère";
		case ShapeKind::CYLINDER:
			return "Cylindre";
		case ShapeKind::CONE:
			return "Cône";
		case ShapeKind::TORUS:
			return "Tore";
		case ShapeKind::PLANE:
			return "Plan";
		case ShapeKind::ICOSAHEDRON:
			return "Icosaèdre";
		case ShapeKind::TORUS_KNOT:
			return "Nœud de tore";
		case ShapeKind::PORTAL_QUAD:
			return "Portail";
		case ShapeKind::MODEL:
			return "Modèle";
		case ShapeKind::MESH:
			return "Maillage modélisé";
	}
	return "?";
}

std::vector<MaterialPreset> MaterialPresets() {
	auto make = [](MaterialKind kind, sdl3::Color color, float metallic, float roughness) {
		MaterialDesc m;
		m.kind = kind;
		m.baseColor = color;
		m.metallic = metallic;
		m.roughness = roughness;
		return m;
	};
	return {
		{"Pierre", make(MaterialKind::PLASTIC, {118, 104, 92, 255}, 0.f, 0.9f)},
		{"Pavé", make(MaterialKind::PLASTIC, {92, 84, 78, 255}, 0.f, 0.95f)},
		{"Bois", make(MaterialKind::WOOD, {128, 82, 48, 255}, 0.f, 0.7f)},
		{"Métal", make(MaterialKind::METAL, {170, 174, 182, 255}, 1.f, 0.35f)},
		{"Or", make(MaterialKind::METAL, {255, 196, 72, 255}, 1.f, 0.25f)},
		{"Cuivre", make(MaterialKind::PBR, {200, 118, 76, 255}, 1.f, 0.4f)},
		{"Plastique", make(MaterialKind::PLASTIC, {196, 62, 58, 255}, 0.f, 0.45f)},
		{"Herbe", make(MaterialKind::PLASTIC, {86, 150, 64, 255}, 0.f, 0.9f)},
		{"Eau", make(MaterialKind::PBR, {52, 118, 196, 255}, 0.f, 0.08f)},
		{"Flamme", make(MaterialKind::UNLIT, {255, 170, 60, 255}, 0.f, 1.f)},
		{"Os", make(MaterialKind::PLASTIC, {226, 218, 196, 255}, 0.f, 0.7f)},
		{"Ardoise", make(MaterialKind::PBR, {58, 62, 70, 255}, 0.f, 0.8f)},
	};
}

// ── LibraryPanel ─────────────────────────────────────────────────────────────

void LibraryPanel::BuildMaterials(ui::Widget page) {
	m_materialsPage = page;
	(void)kit::Caption(m_ctx, page, String("Clic : appliquer au maillage sélectionné"), 12.f);
	const std::vector<MaterialPreset> presets = MaterialPresets();
	ui::Widget row{};
	for (size_t i = 0; i < presets.size(); ++i) {
		if (i % 3 == 0) {
			ui::WidgetBuilder rowBuilder = ui::Row();
			rowBuilder.Padding(0.f);
			rowBuilder.Gap(6.f).GrowWidth();
			row = page.Append(rowBuilder);
		}
		const MaterialPreset preset = presets[i];
		ui::WidgetBuilder tile = ui::Button(String());
		tile.Size(84.f, 74.f).BorderRadius(4.f).Background(sdl3::FColor{0.f, 0.f, 0.f, 0.f}).Tooltip(String(preset.name));
		tile.OnClick([this, preset] { Apply(preset); });
		ui::Widget tileEntity = row.Append(tile);
		ui::WidgetBuilder swatch = ui::Row();
		swatch.Padding(0.f);
		swatch.Size(46.f, 40.f).BorderRadius(preset.material.kind == MaterialKind::METAL ? 20.f : 4.f);
		swatch.Background(kit::FromColor(preset.material.baseColor)).Absolute().Anchor(ui::Anchor::Top).Offset(0.f, 6.f);
		swatch.PointerThrough();
		tileEntity.Append(swatch);
		ui::WidgetBuilder name = ui::Label(String(preset.name));
		name.Absolute().Anchor(ui::Anchor::Bottom).Offset(0.f, -4.f).FontSize(12.f).PointerThrough();
		tileEntity.Append(name);
	}
}

void LibraryPanel::BuildScripts(ui::Widget page) {
	m_scriptsPage = page;
	RefreshScripts();
}

void LibraryPanel::RefreshScripts() {
	if (!m_scriptsPage.Valid())
		return;
	kit::ClearChildren(m_ctx, m_scriptsPage);
	m_scriptRowIndex = 0;
	for (const ScriptAsset &script : m_ctx.runtime.GetProject().scripts) {
		const kit::ScriptBadge badge =
			kit::BadgeOf(m_ctx, m_ctx.runtime.OutlineScript(script.source, ScriptUse::LIBRARY));
		ui::WidgetBuilder row = ui::Row();
		row.Padding(0.f);
		row.Gap(6.f).GrowWidth().AlignItems(ui::Align::Center);
		ui::Widget rowEntity = m_scriptsPage.Append(row);
		(void)kit::Glyph(m_ctx, rowEntity, badge.icon, badge.color, 16.f);
		// Ligne sélectionnable (texte à gauche) : un UiButton centre
		// toujours son texte.
		ui::WidgetBuilder name = ui::Selectable(String(), m_scriptRowIndex++);
		name.GrowWidth().Padding(math::Sides{4.f, 3.f});
		name.Tooltip(script.description.IsEmpty() ? badge.text
												  : String::Format("%s\n%s", script.description.CStr(), badge.text.CStr()));
		name.OnClick([this, scriptName = script.name] {
			if (m_actions.openScript)
				m_actions.openScript(scriptName);
		});
		ui::Widget nameEntity = rowEntity.Append(name);
		ui::WidgetBuilder text = ui::Label(script.name + String(".script"));
		text.GrowWidth().FontSize(13.f).Ellipsis().PointerThrough();
		nameEntity.Append(text);
		(void)kit::IconButton(m_ctx, rowEntity, ui::MaterialIcons::LINK, String("Attacher au nœud sélectionné"),
							  [this, scriptName = script.name] {
								  const scene::NodeId id = m_ctx.runtime.SelectedId();
								  if (!id.Valid() || !m_ctx.runtime.SetScriptRef(id, scriptName) || !m_actions.status)
									  return;
								  // Attaché quand même (le script peut être en cours d'écriture),
								  // mais on dit tout de suite ce que le moteur en fera.
								  const ScriptAsset *asset = m_ctx.runtime.GetProject().FindScript(scriptName);
								  const ScriptOutline outline =
									  m_ctx.runtime.OutlineScript(asset ? asset->source : String(), ScriptUse::LIBRARY);
								  m_actions.status(outline.attachProblem.IsEmpty()
													   ? String::Format("Script « %s » attaché", scriptName.CStr())
													   : String::Format("Script « %s » attaché, mais %s", scriptName.CStr(),
																		outline.attachProblem.CStr()));
							  }, 22.f);
	}
}

void LibraryPanel::BuildWorld(ui::Widget page) {
	m_worldPage = page;
	RefreshWorld();
}

void LibraryPanel::RefreshWorld() {
	if (!m_worldPage.Valid())
		return;
	kit::ClearChildren(m_ctx, m_worldPage);
	for (ui::Widget popup : m_popups)
		ui::Widget(popup).Destroy();
	m_popups.clear();
	SceneDesc *scene = m_ctx.runtime.ActiveDocument();
	if (!scene)
		return;
	kit::PropertyRows rows(m_ctx, 90.f);
	ui::WidgetBuilder title = ui::Label(scene->name);
	title.FontSize(14.f).Bold();
	m_worldPage.Append(title);
	(void)kit::Caption(m_ctx, m_worldPage, scene->description, 11.f);
	if (scene->IsObject()) {
		// Un objet n'est qu'une arborescence de nœuds : le monde (gravité,
		// soleil, ciel, caméra, calque 2D) appartient aux SCÈNES qui
		// l'instancient.
		(void)kit::Caption(m_ctx, m_worldPage,
						   String("Objet réutilisable : pas de réglages du monde. Ils appartiennent aux scènes qui "
								  "l'instancient (la vue d'édition garde un éclairage neutre)."),
						   12.f);
		(void)rows.Text(m_worldPage, "Description", scene->description, [this](const String &text) {
			if (SceneDesc *target = m_ctx.runtime.ActiveDocument())
				target->description = text.Trim();
			RefreshWorld();
		});
		return;
	}
	(void)rows.Vector3(m_worldPage, "Gravité", scene->environment.gravity, 0.05f, [this](math::FVector3 v) {
		if (SceneDesc *target = m_ctx.runtime.ActiveDocument())
			target->environment.gravity = v;
		m_ctx.runtime.ApplyEnvironment();
	});
	(void)rows.Slider(m_worldPage, "Soleil", scene->environment.sunIntensity, 0.f, 3.f, [this](float v) {
		if (SceneDesc *target = m_ctx.runtime.ActiveDocument())
			target->environment.sunIntensity = v;
		m_ctx.runtime.ApplyEnvironment();
	});
	(void)rows.Color(m_worldPage, "Ambiance", scene->environment.ambientColor, [this](sdl3::Color c) {
		if (SceneDesc *target = m_ctx.runtime.ActiveDocument())
			target->environment.ambientColor = c;
		m_ctx.runtime.ApplyEnvironment();
	});
	(void)rows.Color(m_worldPage, "Fond", scene->environment.backgroundColor, [this](sdl3::Color c) {
		if (SceneDesc *target = m_ctx.runtime.ActiveDocument())
			target->environment.backgroundColor = c;
		m_ctx.runtime.ApplyEnvironment();
	});

	// Calque 2D de la scène.
	(void)kit::Caption(m_ctx, m_worldPage, String("2D : écran de référence et rendu"), 12.f);
	auto canvas2d = [this](const std::function<void(Canvas2DDesc &)> &change) {
		if (SceneDesc *target = m_ctx.runtime.ActiveDocument()) {
			Canvas2DDesc canvas = target->canvas;
			change(canvas);
			(void)m_ctx.runtime.SetCanvas2D(canvas);
		}
	};
	(void)rows.Check(m_worldPage, "Monde 3D", scene->canvas.render3d, [canvas2d](bool v) {
		canvas2d([v](Canvas2DDesc &c) { c.render3d = v; });
	});
	(void)rows.Number(m_worldPage, "Largeur 2D", float(scene->canvas.width), 16.f, 16384.f, 1.f, [canvas2d](float v) {
		canvas2d([v](Canvas2DDesc &c) { c.width = int(std::lround(v)); });
	}, 0);
	(void)rows.Number(m_worldPage, "Hauteur 2D", float(scene->canvas.height), 16.f, 16384.f, 1.f, [canvas2d](float v) {
		canvas2d([v](Canvas2DDesc &c) { c.height = int(std::lround(v)); });
	}, 0);
	(void)rows.Color(m_worldPage, "Fond 2D", scene->canvas.background, [canvas2d](sdl3::Color c) {
		canvas2d([c](Canvas2DDesc &d) { d.background = c; });
	});
}

void LibraryPanel::Teardown() {
	for (ui::Widget popup : m_popups)
		if (popup.Valid())
			ui::Widget(popup).Destroy();
	m_popups.clear();
	m_materialsPage = m_scriptsPage = m_worldPage = ui::Widget();
}

void LibraryPanel::Apply(const MaterialPreset &preset) {
	const scene::NodeId id = m_ctx.runtime.SelectedId();
	const scene::Node *node = m_ctx.runtime.FindNode(id);
	if (!node || !VisualDesc::Has(*node)) {
		if (m_actions.status)
			m_actions.status(String("Sélectionnez un maillage pour lui appliquer un matériau"));
		return;
	}
	(void)m_ctx.runtime.SetMaterial(id, preset.material);
	if (m_actions.status)
		m_actions.status(String::Format("Matériau « %s » appliqué à %s", preset.name, node->name.CStr()));
}

} // namespace sandbox
