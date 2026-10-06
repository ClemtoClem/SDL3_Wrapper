// Définitions de inspector.hpp
#include "inspector.hpp"

namespace game_editor {

// ── InspectorPanel ───────────────────────────────────────────────────────────

void InspectorPanel::Build(ecs::Entity page) {
	m_page = page;
	MarkDirty();
}

void InspectorPanel::Teardown() {
	ClearPopups();
	m_page = ecs::Entity{};
}

void InspectorPanel::OnObjectChanged() {
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
	for (ecs::Entity popup : m_popups)
		if (popup.Valid())
			ui::DespawnTree(m_ctx.registry, popup);
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
	const SceneDesc *scene = m_ctx.runtime.ActiveScene();
	scene::Node *node = m_ctx.runtime.SelectedObject();
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
		ecs::Entity s = Section("Nœud", String());
		(void)rows.ReadOnly(s, "Type", String::Format("%s (%s)", kit::LookOf(*node).label, node->type.CStr()));
		(void)rows.ReadOnly(s, "Chemin", scene->tree.PathOf(id));
		(void)rows.Text(s, "Étiquette", TagOf(*node), [this, id](const String &tag) {
			(void)m_ctx.runtime.SetTagOf(id, tag.Trim());
		});
		(void)rows.Check(s, "Visible", node->visible, [this, id](bool v) { (void)m_ctx.runtime.SetVisible(id, v); });
		(void)rows.Check(s, "Verrouillé", node->locked, [this, id](bool v) { (void)m_ctx.runtime.SetLocked(id, v); });
	}

	// ── Transform ─────────────────────────────────────────────────────
	if (Is2DNode(*node)) {
		// Nœud 2D : position et échelle planes (pixels), rotation autour de Z.
		ecs::Entity s = Section("Transform", String());
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
		ecs::Entity s = Section("Transform", String());
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

	if (VisualDesc::Has(*node))
		BuildVisualSections(rows, id, VisualDesc::Read(*node));
	if (LightDesc::Has(*node))
		BuildLightSections(rows, id, LightDesc::Read(*node));
	if (CameraNodeDesc::Has(*node))
		BuildCameraSection(rows, id, CameraNodeDesc::Read(*node));
	if (TriggerDesc::Has(*node))
		BuildTriggerSection(rows, id, TriggerDesc::Read(*node));
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
	ui::WidgetBuilder head = m_ctx.factory.Row();
	head.Pad(0.f);
	head.Gap(6.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(m_page);
	ecs::Entity headEntity = head.Spawn();
	(void)kit::Glyph(m_ctx, headEntity, look.icon, look.color, 18.f);
	ui::WidgetBuilder name = m_ctx.factory.Input();
	name.GrowW().H(ui::Dimension::Px(26.f)).FontSize(14.f).Tooltip(String("Entrée pour renommer")).Parent(headEntity);
	name.OnSubmit([this, id](const String &text) {
		if (!text.Trim().IsEmpty() && m_ctx.runtime.RenameObject(id, text.Trim()))
			Status(String::Format("Renommé : %s", text.Trim().CStr()));
	});
	ecs::Entity nameEntity = name.Spawn();
	if (auto field = m_ctx.registry.GetComponent<ui::UiInput>(nameEntity); field.IsSome())
		field.Unwrap()->text = node.name;

	ui::WidgetBuilder bar = m_ctx.factory.Row();
	bar.Pad(0.f);
	bar.Gap(4.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(m_page);
	ecs::Entity barEntity = bar.Spawn();
	ui::WidgetBuilder title = m_ctx.factory.Label(String("Propriétés"));
	title.FontSize(13.f).TextColor(m_ctx.Theme().muted).WAuto().HAuto().Parent(barEntity);
	title.Spawn();
	(void)kit::Spacer(m_ctx, barEntity);

	ecs::Entity menu = m_ctx.factory.ContextMenu();
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

ecs::Entity InspectorPanel::Section(const char *title, const String &component) {
	const String key(title);
	const bool open = m_open.find(key) == m_open.end() || m_open[key];
	ui::WidgetBuilder section = m_ctx.factory.Expander(key, open);
	section.GrowW().Gap(4.f).FontSize(14.f).Parent(m_page);
	section.OnToggle([this, key](bool expanded) { m_open[key] = expanded; });
	ecs::Entity entity = section.Spawn();
	m_sections.emplace_back(key, entity);
	if (!component.IsEmpty() && open) {
		const scene::NodeId id = m_ctx.runtime.SelectedId();
		ecs::Entity menu = m_ctx.factory.ContextMenu();
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
		ui::WidgetBuilder button = m_ctx.factory.Button(String());
		button.Size(22.f, 22.f).Bg(sdl3::FColor{0.f, 0.f, 0.f, 0.f}).Radius(4.f).Tooltip(String("Actions du composant"));
		// L'ancre se résout dans la boîte de CONTENU (sous le padding qui
		// réserve l'en-tête) : on remonte donc de la hauteur d'en-tête.
		const float header = m_ctx.Theme().fontSize + 14.f + 6.f;
		button.Absolute().Anchor(ui::Anchor::TopRight).Offset(0.f, -header + 3.f).Parent(entity);
		ecs::Entity buttonEntity = button.Spawn();
		ui::WidgetBuilder glyph = m_ctx.factory.Icon(ui::MaterialIcons::MORE_VERT, 15.f);
		glyph.Absolute().Anchor(ui::Anchor::Center).Justify(ui::Justify::Center).PointerThrough().Parent(buttonEntity);
		(void)glyph.Spawn();
		AttachMenu(buttonEntity, menu);
	}
	return entity;
}

void InspectorPanel::AddMenuItem(ecs::Entity menu, const char *text, std::function<void()> action) {
	ui::WidgetBuilder item = m_ctx.factory.MenuItem(String(text));
	item.OnClick(std::move(action)).Parent(menu);
	(void)item.Spawn();
}

void InspectorPanel::AttachMenu(ecs::Entity button, ecs::Entity menu) {
	if (auto callbacks = m_ctx.registry.GetComponent<ui::UiCallbacks>(button); callbacks.IsSome())
		callbacks.Unwrap()->onClick = [this, button, menu] {
			if (auto computed = m_ctx.registry.GetComponent<ui::UiComputed>(button); computed.IsSome()) {
				const sdl3::FRect r = computed.Unwrap()->screen;
				m_ctx.gui.OpenPopupAt(menu, sdl3::FPoint{r.x + r.w - 200.f, r.y + r.h});
			}
		};
}

void InspectorPanel::BuildVisualSections(kit::PropertyRows &rows, scene::NodeId id, const VisualDesc &visual) {
	{
		ecs::Entity s = Section("Maillage", String(component::VISUAL));
		if (visual.shape == ShapeKind::MODEL) {
			(void)rows.ReadOnly(s, "Modèle", visual.source);
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
		}
	}
	{
		const MaterialDesc material = visual.material;
		ecs::Entity s = Section("Matériau", String());
		std::vector<String> kinds;
		for (MaterialKind kind : {MaterialKind::PLASTIC, MaterialKind::METAL, MaterialKind::WOOD, MaterialKind::PBR,
								  MaterialKind::UNLIT, MaterialKind::BASIC})
			kinds.push_back(String(MaterialKindName(kind)));
		(void)rows.Choice(s, "Famille", std::move(kinds), int(material.kind), [this, id](int index) {
			MutateMaterial(id, [index](MaterialDesc &m) { m.kind = MaterialKind(index); });
		});
		(void)rows.Color(s, "Couleur", material.baseColor, Live([this, id](sdl3::Color c) {
			(void)m_ctx.runtime.SetMaterialColor(id, c);
		}), m_popups);
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

void InspectorPanel::BuildLightSections(kit::PropertyRows &rows, scene::NodeId id, const LightDesc &light) {
	{
		ecs::Entity s = Section("Lumière", String(component::LIGHT));
		(void)rows.Choice(s, "Type", {String("Ponctuelle"), String("Projecteur")}, int(light.kind), [this, id](int index) {
			MutateLight(id, [index](LightDesc &l) { l.kind = LightKind(index); });
			MarkDirty();
		});
		(void)rows.Color(s, "Couleur", light.color, Live([this, id](sdl3::Color c) {
			MutateLight(id, [c](LightDesc &l) { l.color = c; });
		}), m_popups);
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
		ecs::Entity s = Section("Ombres", String());
		(void)rows.Check(s, "Activées", light.castShadow, [this, id](bool v) {
			MutateLight(id, [v](LightDesc &l) { l.castShadow = v; });
		});
		(void)kit::Caption(m_ctx, s, String("Une seule lumière ponctuelle ombrée à la fois (la première)."), 11.f);
	}
}

void InspectorPanel::BuildCameraSection(kit::PropertyRows &rows, scene::NodeId id, const CameraNodeDesc &camera) {
	ecs::Entity s = Section("Caméra", String(component::CAMERA));
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
	ecs::Entity s = Section("Déclencheur", String(component::TRIGGER));
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

void InspectorPanel::BuildPhysicsSection(kit::PropertyRows &rows, scene::NodeId id, const PhysicsDesc &physics) {
	ecs::Entity s = Section("Physique", String(component::BODY));
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
	ui::WidgetBuilder impulse = m_ctx.factory.Button(String("Impulsion vers le haut (mode Jeu)"));
	impulse.GrowW().HAuto().FontSize(12.f).Parent(s);
	impulse.OnClick([this, id] {
		if (const scene::Node *node = m_ctx.runtime.FindObject(id))
			(void)m_ctx.runtime.ApplyImpulse(node->name, math::FVector3{0.f, 240.f, 0.f});
	});
	(void)impulse.Spawn();
}

void InspectorPanel::BuildCanvasItemSection(kit::PropertyRows &rows, scene::NodeId id, const CanvasItemDesc &item) {
	ecs::Entity s = Section("Apparence 2D", String(component::CANVAS_ITEM));
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
	(void)rows.Color(s, item.kind == CanvasItemKind::SPRITE ? "Teinte" : "Couleur", item.color,
					 Live([this, id](sdl3::Color c) { MutateCanvasItem(id, [c](CanvasItemDesc &d) { d.color = c; }); }),
					 m_popups);
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
		}), m_popups);
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
	ecs::Entity s = Section("Caméra 2D", String(component::CAMERA_2D));
	(void)rows.Number(s, "Zoom", camera.zoom, 0.05f, 20.f, 0.01f, Live([this, id](float v) {
		if (const scene::Node *node = m_ctx.runtime.FindObject(id)) {
			Camera2DDesc updated = Camera2DDesc::Read(*node);
			updated.zoom = v;
			(void)m_ctx.runtime.SetCamera2D(id, updated);
		}
	}));
	(void)rows.Check(s, "Caméra du jeu", camera.current, [this, id](bool v) {
		SceneDesc *scene = m_ctx.runtime.ActiveScene();
		if (!scene)
			return;
		// Une seule caméra 2D du jeu : cocher celle-ci décoche les autres.
		for (scene::NodeId other : scene->Objects())
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
	const scene::Node *node = m_ctx.runtime.FindObject(id);
	if (!node)
		return;
	CanvasItemDesc item = CanvasItemDesc::Read(*node);
	change(item);
	(void)m_ctx.runtime.SetCanvasItem(id, item);
}

void InspectorPanel::BuildScriptSection(kit::PropertyRows &rows, scene::NodeId id, const ScriptRef &ref) {
	ecs::Entity s = Section("Script", String(component::SCRIPT));
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
		ecs::Entity status = rows.Row(s, "État");
		(void)kit::Glyph(m_ctx, status, badge.icon, badge.color, 15.f);
		ui::WidgetBuilder text = m_ctx.factory.Label(badge.text);
		text.GrowW().HAuto().FontSize(12.f).TextEllipsis().Tooltip(badge.text).Parent(status);
		(void)text.Spawn();
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
	ui::WidgetBuilder open = m_ctx.factory.Button(String("Ouvrir dans l'éditeur de code"));
	open.GrowW().HAuto().FontSize(12.f).Parent(s);
	open.OnClick([this, name = ref.script] {
		if (m_actions.openScript && !name.IsEmpty())
			m_actions.openScript(name);
	});
	(void)open.Spawn();
}

void InspectorPanel::BuildScriptObjects(kit::PropertyRows &rows, scene::NodeId id) {
	std::vector<ScriptObjectInfo> objects = m_ctx.runtime.ScriptObjectsOf(id);
	if (objects.empty())
		return;
	// Les objets de script qui PORTENT ce nœud (Behaviour attachée, Mesh3D
	// créé par un script…) : lus dans les registres C++, sans exécuter de
	// script. Les champs sont un instantané : « Actualiser » le reprend.
	ecs::Entity s = Section("Objets de script", String());
	constexpr size_t MAX_FIELDS = 16;
	for (const ScriptObjectInfo &info : objects) {
		ecs::Entity head = rows.Row(s, info.attached ? "Behaviour" : "Objet");
		(void)kit::Glyph(m_ctx, head, info.destroyed ? ui::MaterialIcons::ERROR : ui::MaterialIcons::CHECK_CIRCLE,
						 info.destroyed ? kit::PaletteOf(m_ctx).error : kit::syntax::Colors().type, 15.f);
		ui::WidgetBuilder title = m_ctx.factory.Label(info.Signature());
		title.GrowW().HAuto().FontSize(12.f).TextEllipsis().Tooltip(String::Format("créé par : %s", info.origin.CStr()));
		title.Parent(head);
		(void)title.Spawn();
		const std::vector<std::pair<String, String>> fields = info.Fields();
		for (size_t i = 0; i < fields.size() && i < MAX_FIELDS; ++i)
			(void)rows.ReadOnly(s, fields[i].first.CStr(), fields[i].second);
		if (fields.size() > MAX_FIELDS)
			(void)kit::Caption(m_ctx, s, String::Format("… %d autres champs", int(fields.size() - MAX_FIELDS)), 11.f);
		if (info.destroyed)
			continue;
		ui::WidgetBuilder destroy = m_ctx.factory.Button(String("Détruire (on_destroy → deinit → bases)"));
		destroy.GrowW().HAuto().FontSize(12.f).Parent(s);
		destroy.OnClick([this, info] {
			if (m_ctx.runtime.DestroyScriptObject(info))
				Status(String::Format("%s détruit", info.className.CStr()));
			Refresh();
		});
		(void)destroy.Spawn();
	}
	ui::WidgetBuilder refresh = m_ctx.factory.Button(String("Actualiser les champs"));
	refresh.GrowW().HAuto().FontSize(12.f).Parent(s);
	refresh.OnClick([this] { Refresh(); });
	(void)refresh.Spawn();
}

void InspectorPanel::BuildCustomProperties(const scene::Node &node) {
	if (node.properties.Size() == 0)
		return;
	ecs::Entity s = Section("Propriétés personnalisées", String());
	kit::PropertyRows rows(m_ctx);
	for (const auto &[key, value] : node.properties.Entries())
		(void)rows.ReadOnly(s, key.CStr(), value.ToDisplayString());
	ui::WidgetBuilder edit = m_ctx.factory.Button(String("Modifier en JSON…"));
	edit.GrowW().HAuto().FontSize(12.f).Parent(s);
	edit.OnClick([this, id = node.id] {
		if (m_actions.editJson)
			m_actions.editJson(id, String());
	});
	(void)edit.Spawn();
}

void InspectorPanel::BuildAddComponent(const scene::Node &node) {
	ecs::Entity menu = m_ctx.factory.ContextMenu();
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
	ui::WidgetBuilder add = m_ctx.factory.Button(String("Ajouter un composant"));
	add.GrowW().H(ui::Dimension::Px(28.f)).Parent(m_page);
	ecs::Entity button = add.Spawn();
	AttachMenu(button, menu);
}

void InspectorPanel::ScrollToPendingSection() {
	for (const auto &[title, entity] : m_sections) {
		if (title != m_pendingSection)
			continue;
		auto section = m_ctx.registry.GetComponent<ui::UiComputed>(entity);
		auto page = m_ctx.registry.GetComponent<ui::UiComputed>(m_page);
		auto rect = m_ctx.registry.GetComponent<ui::UiRect>(m_page);
		if (section.IsNone() || page.IsNone() || rect.IsNone() || section.Unwrap()->screen.h <= 0.f)
			return; // mise en page pas encore faite : réessai à l'image suivante
		rect.Unwrap()->scroll.y += section.Unwrap()->screen.y - page.Unwrap()->screen.y - 4.f;
		rect.Unwrap()->ClampScroll();
		m_ctx.gui.Layout().MarkDirty();
		break;
	}
	m_pendingSection = String();
}

void InspectorPanel::MutateVisual(scene::NodeId id, const std::function<void(VisualDesc &)> &change) {
	const scene::Node *node = m_ctx.runtime.FindObject(id);
	if (!node)
		return;
	VisualDesc visual = VisualDesc::Read(*node);
	change(visual);
	(void)m_ctx.runtime.SetVisual(id, visual);
}

void InspectorPanel::MutateMaterial(scene::NodeId id, const std::function<void(MaterialDesc &)> &change) {
	const scene::Node *node = m_ctx.runtime.FindObject(id);
	if (!node)
		return;
	MaterialDesc material = VisualDesc::Read(*node).material;
	change(material);
	(void)m_ctx.runtime.SetMaterial(id, material);
}

void InspectorPanel::MutateLight(scene::NodeId id, const std::function<void(LightDesc &)> &change) {
	const scene::Node *node = m_ctx.runtime.FindObject(id);
	if (!node)
		return;
	LightDesc light = LightDesc::Read(*node);
	change(light);
	(void)m_ctx.runtime.SetLight(id, light);
}

void InspectorPanel::MutatePhysics(scene::NodeId id, const std::function<void(PhysicsDesc &)> &change) {
	const scene::Node *node = m_ctx.runtime.FindObject(id);
	if (!node)
		return;
	PhysicsDesc physics = PhysicsDesc::Read(*node);
	change(physics);
	(void)m_ctx.runtime.SetPhysics(id, physics);
}

CameraNodeDesc InspectorPanel::CurrentCamera(scene::NodeId id) {
	const scene::Node *node = m_ctx.runtime.FindObject(id);
	return node ? CameraNodeDesc::Read(*node) : CameraNodeDesc{};
}

TriggerDesc InspectorPanel::CurrentTrigger(scene::NodeId id) {
	const scene::Node *node = m_ctx.runtime.FindObject(id);
	return node ? TriggerDesc::Read(*node) : TriggerDesc{};
}

const char * InspectorPanel::ComponentLabel(const String &type) {
	if (type == component::VISUAL)
		return "Maillage";
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

void LibraryPanel::BuildMaterials(ecs::Entity page) {
	m_materialsPage = page;
	(void)kit::Caption(m_ctx, page, String("Clic : appliquer au maillage sélectionné"), 12.f);
	const std::vector<MaterialPreset> presets = MaterialPresets();
	ecs::Entity row{};
	for (size_t i = 0; i < presets.size(); ++i) {
		if (i % 3 == 0) {
			ui::WidgetBuilder rowBuilder = m_ctx.factory.Row();
			rowBuilder.Pad(0.f);
			rowBuilder.Gap(6.f).GrowW().HAuto().Parent(page);
			row = rowBuilder.Spawn();
		}
		const MaterialPreset preset = presets[i];
		ui::WidgetBuilder tile = m_ctx.factory.Button(String());
		tile.Size(84.f, 74.f).Radius(4.f).Bg(sdl3::FColor{0.f, 0.f, 0.f, 0.f}).Tooltip(String(preset.name)).Parent(row);
		tile.OnClick([this, preset] { Apply(preset); });
		ecs::Entity tileEntity = tile.Spawn();
		ui::WidgetBuilder swatch = m_ctx.factory.Row();
		swatch.Pad(0.f);
		swatch.Size(46.f, 40.f).Radius(preset.material.kind == MaterialKind::METAL ? 20.f : 4.f);
		swatch.Bg(kit::FromColor(preset.material.baseColor)).Absolute().Anchor(ui::Anchor::Top).Offset(0.f, 6.f);
		swatch.PointerThrough().Parent(tileEntity);
		(void)swatch.Spawn();
		ui::WidgetBuilder name = m_ctx.factory.Label(String(preset.name));
		name.Absolute().Anchor(ui::Anchor::Bottom).Offset(0.f, -4.f).WAuto().HAuto().FontSize(12.f).PointerThrough();
		name.Parent(tileEntity);
		(void)name.Spawn();
	}
}

void LibraryPanel::BuildScripts(ecs::Entity page) {
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
		ui::WidgetBuilder row = m_ctx.factory.Row();
		row.Pad(0.f);
		row.Gap(6.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(m_scriptsPage);
		ecs::Entity rowEntity = row.Spawn();
		(void)kit::Glyph(m_ctx, rowEntity, badge.icon, badge.color, 16.f);
		// Ligne sélectionnable (texte à gauche) : un UiButton centre
		// toujours son texte.
		ui::WidgetBuilder name = m_ctx.factory.Selectable(String(), m_scriptRowIndex++);
		name.GrowW().HAuto().Pad(math::Sides{4.f, 3.f}).Parent(rowEntity);
		name.Tooltip(script.description.IsEmpty() ? badge.text
												  : String::Format("%s\n%s", script.description.CStr(), badge.text.CStr()));
		name.OnClick([this, scriptName = script.name] {
			if (m_actions.openScript)
				m_actions.openScript(scriptName);
		});
		ecs::Entity nameEntity = name.Spawn();
		ui::WidgetBuilder text = m_ctx.factory.Label(script.name + String(".script"));
		text.GrowW().HAuto().FontSize(13.f).TextEllipsis().PointerThrough().Parent(nameEntity);
		(void)text.Spawn();
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

void LibraryPanel::BuildWorld(ecs::Entity page) {
	m_worldPage = page;
	RefreshWorld();
}

void LibraryPanel::RefreshWorld() {
	if (!m_worldPage.Valid())
		return;
	kit::ClearChildren(m_ctx, m_worldPage);
	for (ecs::Entity popup : m_popups)
		ui::DespawnTree(m_ctx.registry, popup);
	m_popups.clear();
	SceneDesc *scene = m_ctx.runtime.ActiveScene();
	if (!scene)
		return;
	kit::PropertyRows rows(m_ctx, 90.f);
	ui::WidgetBuilder title = m_ctx.factory.Label(scene->name);
	title.FontSize(14.f).Bold().WAuto().HAuto().Parent(m_worldPage);
	(void)title.Spawn();
	(void)kit::Caption(m_ctx, m_worldPage, scene->description, 11.f);
	(void)rows.Vector3(m_worldPage, "Gravité", scene->environment.gravity, 0.05f, [this](math::FVector3 v) {
		if (SceneDesc *target = m_ctx.runtime.ActiveScene())
			target->environment.gravity = v;
		m_ctx.runtime.ApplyEnvironment();
	});
	(void)rows.Slider(m_worldPage, "Soleil", scene->environment.sunIntensity, 0.f, 3.f, [this](float v) {
		if (SceneDesc *target = m_ctx.runtime.ActiveScene())
			target->environment.sunIntensity = v;
		m_ctx.runtime.ApplyEnvironment();
	});
	(void)rows.Color(m_worldPage, "Ambiance", scene->environment.ambientColor, [this](sdl3::Color c) {
		if (SceneDesc *target = m_ctx.runtime.ActiveScene())
			target->environment.ambientColor = c;
		m_ctx.runtime.ApplyEnvironment();
	}, m_popups);
	(void)rows.Color(m_worldPage, "Fond", scene->environment.backgroundColor, [this](sdl3::Color c) {
		if (SceneDesc *target = m_ctx.runtime.ActiveScene())
			target->environment.backgroundColor = c;
		m_ctx.runtime.ApplyEnvironment();
	}, m_popups);

	// Calque 2D de la scène.
	(void)kit::Caption(m_ctx, m_worldPage, String("2D : écran de référence et rendu"), 12.f);
	auto canvas2d = [this](const std::function<void(Canvas2DDesc &)> &change) {
		if (SceneDesc *target = m_ctx.runtime.ActiveScene()) {
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
	}, m_popups);
}

void LibraryPanel::Teardown() {
	for (ecs::Entity popup : m_popups)
		if (popup.Valid())
			ui::DespawnTree(m_ctx.registry, popup);
	m_popups.clear();
	m_materialsPage = m_scriptsPage = m_worldPage = ecs::Entity{};
}

void LibraryPanel::Apply(const MaterialPreset &preset) {
	const scene::NodeId id = m_ctx.runtime.SelectedId();
	const scene::Node *node = m_ctx.runtime.FindObject(id);
	if (!node || !VisualDesc::Has(*node)) {
		if (m_actions.status)
			m_actions.status(String("Sélectionnez un maillage pour lui appliquer un matériau"));
		return;
	}
	(void)m_ctx.runtime.SetMaterial(id, preset.material);
	if (m_actions.status)
		m_actions.status(String::Format("Matériau « %s » appliqué à %s", preset.name, node->name.CStr()));
}

} // namespace game_editor
