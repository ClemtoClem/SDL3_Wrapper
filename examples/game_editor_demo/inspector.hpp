#pragma once
/**
 * game_editor — l'inspecteur et la bibliothèque (colonne de droite).
 *
 * `InspectorPanel` montre le nœud sélectionné comme dans les maquettes :
 * son nom (éditable), puis une SECTION repliable par composant — Transform,
 * Maillage, Matériau, Lumière, Ombres, Caméra, Déclencheur, Physique,
 * Script, propriétés libres — chacune avec son menu ⋮ (réinitialiser,
 * retirer, modifier en JSON), et « Ajouter un composant » en bas.
 *
 * ── Ne pas se reconstruire sous la souris ────────────────────────────────
 * Toute modification du nœud notifie l'interface (Runtime::onObjectChanged),
 * et l'inspecteur se reconstruit pour montrer la nouvelle valeur. Mais une
 * valeur tirée à la souris change à CHAQUE mouvement : reconstruire à ce
 * moment détruirait le champ qu'on est en train de tirer. Les éditions
 * CONTINUES (glisser, curseur, couleur) passent donc par `Live()`, qui
 * signale « c'est moi » et fait taire la reconstruction ; les éditions
 * DISCRÈTES (liste, case à cocher) reconstruisent, parce qu'elles peuvent
 * faire apparaître d'autres champs (un projecteur a un angle, une lumière
 * ponctuelle non).
 *
 * `LibraryPanel` (sous l'inspecteur) : matériaux prêts à appliquer, scripts
 * de la bibliothèque, réglages du monde.
 */
#include <functional>
#include <unordered_map>
#include <vector>

#include "core/core.hpp"
#include "ui/ui.hpp"

#include "kit.hpp"
#include "project.hpp"
#include "runtime.hpp"

namespace game_editor {

/// Ce que l'inspecteur demande à l'éditeur (ouvrir un document).
struct InspectorActions {
	std::function<void(const String &script)> openScript;
	/// Édition JSON d'un composant (`type` vide : propriétés libres du nœud).
	std::function<void(scene::NodeId, const String &type)> editJson;
	std::function<void(const String &)> status;
};

class InspectorPanel {
public:
	InspectorPanel(UiContext &ctx, InspectorActions actions) : m_ctx(ctx), m_actions(std::move(actions)) {}
	InspectorPanel(const InspectorPanel &) = delete;
	InspectorPanel &operator=(const InspectorPanel &) = delete;

	void Build(ecs::Entity page) {
		m_page = page;
		MarkDirty();
	}

	void Teardown() {
		ClearPopups();
		m_page = ecs::Entity{};
	}

	/// À brancher sur Runtime::onObjectChanged / onSelectionChanged.
	void OnObjectChanged() {
		if (!m_localEdit)
			MarkDirty();
	}
	void MarkDirty() noexcept { m_dirty = true; }

	void Tick() {
		if (m_dirty && m_page.Valid()) {
			m_dirty = false;
			Refresh();
		}
		if (!m_pendingSection.IsEmpty())
			ScrollToPendingSection();
	}

	/// Déplie une section et la fait apparaître (pilotage par script :
	/// `editor.open_panel("inspector", 1)` → Matériau).
	void FocusSection(const String &title) {
		m_open[title] = true;
		m_pendingSection = title;
		MarkDirty();
	}

	[[nodiscard]] static const char *SectionForTab(int tab) noexcept {
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

private:
	// ── Garde des éditions continues (cf. en-tête) ──────────────────────────

	template <typename F> auto Live(F edit) {
		return [this, edit](auto value) {
			m_localEdit = true;
			edit(value);
			m_localEdit = false;
		};
	}

	void ClearPopups() {
		for (ecs::Entity popup : m_popups)
			if (popup.Valid())
				ui::DespawnTree(m_ctx.registry, popup);
		m_popups.clear();
	}

	void Status(const String &text) {
		if (m_actions.status)
			m_actions.status(text);
	}

	// ── Construction ─────────────────────────────────────────────────────────

	void Refresh() {
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
		{
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
		if (ScriptRef::Has(*node))
			BuildScriptSection(rows, id, ScriptRef::Read(*node));
		BuildCustomProperties(*node);
		BuildAddComponent(*node);
	}

	/// En-tête : icône du type + nom éditable, puis « Propriétés ⋮ ».
	void BuildHeader(const SceneDesc &scene, const scene::Node &node) {
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

	/// Section repliable. `component` non vide : section d'un composant, qui
	/// reçoit son menu ⋮ (réinitialiser / retirer / JSON).
	ecs::Entity Section(const char *title, const String &component) {
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

	void AddMenuItem(ecs::Entity menu, const char *text, std::function<void()> action) {
		ui::WidgetBuilder item = m_ctx.factory.MenuItem(String(text));
		item.OnClick(std::move(action)).Parent(menu);
		(void)item.Spawn();
	}

	/// Le clic sur `button` ouvre `menu` sous lui.
	void AttachMenu(ecs::Entity button, ecs::Entity menu) {
		if (auto callbacks = m_ctx.registry.GetComponent<ui::UiCallbacks>(button); callbacks.IsSome())
			callbacks.Unwrap()->onClick = [this, button, menu] {
				if (auto computed = m_ctx.registry.GetComponent<ui::UiComputed>(button); computed.IsSome()) {
					const sdl3::FRect r = computed.Unwrap()->screen;
					m_ctx.gui.OpenPopupAt(menu, sdl3::FPoint{r.x + r.w - 200.f, r.y + r.h});
				}
			};
	}

	// ── Sections par composant ───────────────────────────────────────────────

	void BuildVisualSections(kit::PropertyRows &rows, scene::NodeId id, const VisualDesc &visual) {
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

	void BuildLightSections(kit::PropertyRows &rows, scene::NodeId id, const LightDesc &light) {
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

	void BuildCameraSection(kit::PropertyRows &rows, scene::NodeId id, const CameraNodeDesc &camera) {
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

	void BuildTriggerSection(kit::PropertyRows &rows, scene::NodeId id, const TriggerDesc &trigger) {
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

	void BuildPhysicsSection(kit::PropertyRows &rows, scene::NodeId id, const PhysicsDesc &physics) {
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

	void BuildScriptSection(kit::PropertyRows &rows, scene::NodeId id, const ScriptRef &ref) {
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
			Option<data::script::ScriptError> error = Runtime::CheckScript(asset->source);
			ecs::Entity status = rows.Row(s, "État");
			(void)kit::Glyph(m_ctx, status, error.IsSome() ? ui::MaterialIcons::ERROR : ui::MaterialIcons::CHECK_CIRCLE,
							 error.IsSome() ? kit::PaletteOf(m_ctx).error : kit::PaletteOf(m_ctx).ok, 15.f);
			ui::WidgetBuilder text = m_ctx.factory.Label(error.IsSome() ? error.Unwrap().Format() : String("compile"));
			text.GrowW().HAuto().FontSize(12.f).TextEllipsis().Parent(status);
			(void)text.Spawn();
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

	/// Propriétés libres du nœud (lues par les scripts via node.prop).
	void BuildCustomProperties(const scene::Node &node) {
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

	/// « Ajouter un composant » : les composants que le nœud n'a pas encore.
	void BuildAddComponent(const scene::Node &node) {
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

	void ScrollToPendingSection() {
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

	// ── Mutations (lecture de l'état COURANT, pas d'une copie périmée) ──────

	void MutateVisual(scene::NodeId id, const std::function<void(VisualDesc &)> &change) {
		const scene::Node *node = m_ctx.runtime.FindObject(id);
		if (!node)
			return;
		VisualDesc visual = VisualDesc::Read(*node);
		change(visual);
		(void)m_ctx.runtime.SetVisual(id, visual);
	}

	void MutateMaterial(scene::NodeId id, const std::function<void(MaterialDesc &)> &change) {
		const scene::Node *node = m_ctx.runtime.FindObject(id);
		if (!node)
			return;
		MaterialDesc material = VisualDesc::Read(*node).material;
		change(material);
		(void)m_ctx.runtime.SetMaterial(id, material);
	}

	void MutateLight(scene::NodeId id, const std::function<void(LightDesc &)> &change) {
		const scene::Node *node = m_ctx.runtime.FindObject(id);
		if (!node)
			return;
		LightDesc light = LightDesc::Read(*node);
		change(light);
		(void)m_ctx.runtime.SetLight(id, light);
	}

	void MutatePhysics(scene::NodeId id, const std::function<void(PhysicsDesc &)> &change) {
		const scene::Node *node = m_ctx.runtime.FindObject(id);
		if (!node)
			return;
		PhysicsDesc physics = PhysicsDesc::Read(*node);
		change(physics);
		(void)m_ctx.runtime.SetPhysics(id, physics);
	}

	[[nodiscard]] CameraNodeDesc CurrentCamera(scene::NodeId id) {
		const scene::Node *node = m_ctx.runtime.FindObject(id);
		return node ? CameraNodeDesc::Read(*node) : CameraNodeDesc{};
	}

	[[nodiscard]] TriggerDesc CurrentTrigger(scene::NodeId id) {
		const scene::Node *node = m_ctx.runtime.FindObject(id);
		return node ? TriggerDesc::Read(*node) : TriggerDesc{};
	}

public:
	[[nodiscard]] static const char *ComponentLabel(const String &type) {
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

	[[nodiscard]] static const char *ShapeLabel(ShapeKind shape) noexcept {
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

private:
	static constexpr ShapeKind EDITABLE_SHAPES[] = {ShapeKind::BOX,   ShapeKind::SPHERE,      ShapeKind::CYLINDER,
													ShapeKind::CONE,  ShapeKind::TORUS,       ShapeKind::PLANE,
													ShapeKind::ICOSAHEDRON, ShapeKind::TORUS_KNOT};

	UiContext &m_ctx;
	InspectorActions m_actions;
	ecs::Entity m_page{};
	std::vector<ecs::Entity> m_popups;
	std::vector<std::pair<String, ecs::Entity>> m_sections;
	std::unordered_map<String, bool> m_open;
	String m_pendingSection;
	bool m_dirty = true;
	bool m_localEdit = false;
};

// ============================================================================
// LibraryPanel — matériaux, scripts, monde
// ============================================================================

/// Préréglage de matériau (onglet « Matériaux »).
struct MaterialPreset {
	const char *name;
	MaterialDesc material;
};

[[nodiscard]] inline std::vector<MaterialPreset> MaterialPresets() {
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

class LibraryPanel {
public:
	LibraryPanel(UiContext &ctx, InspectorActions actions) : m_ctx(ctx), m_actions(std::move(actions)) {}

	void BuildMaterials(ecs::Entity page) {
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

	void BuildScripts(ecs::Entity page) {
		m_scriptsPage = page;
		RefreshScripts();
	}

	void RefreshScripts() {
		if (!m_scriptsPage.Valid())
			return;
		kit::ClearChildren(m_ctx, m_scriptsPage);
		m_scriptRowIndex = 0;
		for (const ScriptAsset &script : m_ctx.runtime.GetProject().scripts) {
			const bool broken = Runtime::CheckScript(script.source).IsSome();
			ui::WidgetBuilder row = m_ctx.factory.Row();
			row.Pad(0.f);
			row.Gap(6.f).GrowW().HAuto().Align(ui::CrossAlign::Center).Parent(m_scriptsPage);
			ecs::Entity rowEntity = row.Spawn();
			(void)kit::Glyph(m_ctx, rowEntity, broken ? ui::MaterialIcons::ERROR : ui::MaterialIcons::CHECK_CIRCLE,
							 broken ? kit::PaletteOf(m_ctx).error : kit::PaletteOf(m_ctx).ok, 16.f);
			// Ligne sélectionnable (texte à gauche) : un UiButton centre
			// toujours son texte.
			ui::WidgetBuilder name = m_ctx.factory.Selectable(String(), m_scriptRowIndex++);
			name.GrowW().HAuto().Pad(math::Sides{4.f, 3.f}).Tooltip(script.description).Parent(rowEntity);
			name.OnClick([this, scriptName = script.name] {
				if (m_actions.openScript)
					m_actions.openScript(scriptName);
			});
			ecs::Entity nameEntity = name.Spawn();
			ui::WidgetBuilder text = m_ctx.factory.Label(script.name + String(".sled"));
			text.GrowW().HAuto().FontSize(13.f).TextEllipsis().PointerThrough().Parent(nameEntity);
			(void)text.Spawn();
			(void)kit::IconButton(m_ctx, rowEntity, ui::MaterialIcons::LINK, String("Attacher au nœud sélectionné"),
								  [this, scriptName = script.name] {
									  const scene::NodeId id = m_ctx.runtime.SelectedId();
									  if (id.Valid() && m_ctx.runtime.SetScriptRef(id, scriptName) && m_actions.status)
										  m_actions.status(String::Format("Script « %s » attaché", scriptName.CStr()));
								  }, 22.f);
		}
	}

	void BuildWorld(ecs::Entity page) {
		m_worldPage = page;
		RefreshWorld();
	}

	void RefreshWorld() {
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
	}

	void Teardown() {
		for (ecs::Entity popup : m_popups)
			if (popup.Valid())
				ui::DespawnTree(m_ctx.registry, popup);
		m_popups.clear();
		m_materialsPage = m_scriptsPage = m_worldPage = ecs::Entity{};
	}

private:
	void Apply(const MaterialPreset &preset) {
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

	UiContext &m_ctx;
	InspectorActions m_actions;
	ecs::Entity m_materialsPage{}, m_scriptsPage{}, m_worldPage{};
	std::vector<ecs::Entity> m_popups;
	int m_scriptRowIndex = 0;
};

} // namespace game_editor
