// Définitions de voxelia/studio/texture_editor.hpp
#include <voxelia/studio/texture_editor.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

#include <SDL3/SDL.h>
#include <voxel/json.hpp>
#include <voxel/lighting.hpp>
#include <voxel/meshing.hpp>
#include <voxelia/session.hpp>
#include <voxelia/shell.hpp>

namespace voxelia::studio {

namespace fs = std::filesystem;
using voxel::Image;

namespace {

constexpr float PREVIEW_PERIOD = 0.2f; ///< aperçus refaits au plus 5 fois par seconde en peignant
constexpr float CATALOG_PERIOD = 3.f;
constexpr size_t CATALOG_MAX = 300;
constexpr int WITNESS = 6; ///< bloc témoin des variantes : 6 × 6 positions

Option<double> Number(const String &s) {
	char *end = nullptr;
	const double v = std::strtod(s.CStr(), &end);
	if (!end || end == s.CStr() || *end != '\0')
		return NONE;
	return Some(v);
}

/// Entiers de `args` à partir de `first` (NONE si l'un manque ou n'est pas un nombre).
Option<std::vector<int>> Ints(const std::vector<String> &args, size_t first, size_t count) {
	if (args.size() < first + count)
		return NONE;
	std::vector<int> out;
	for (size_t i = first; i < first + count; ++i) {
		const Option<double> v = Number(args[i]);
		if (v.IsNone())
			return NONE;
		out.push_back(int(std::floor(v.Value())));
	}
	return Some(out);
}

/// Une face du monde éclairée par le ciel (ambiante + soleil oblique), comme
/// le matériau BAKED : en linéaire, ACES, retour en sRGB.
Image SkyLit(const Image &source) {
	Image out = source;
	auto toSrgb = [](float c) {
		c = std::clamp(c, 0.f, 1.f);
		return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
	};
	auto aces = [](float x) { return std::clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.f, 1.f); };
	for (size_t i = 0; i + 3 < out.rgba.size(); i += 4)
		for (int k = 0; k < 3; ++k) {
			const float linear = voxel::lighting::SrgbToLinear(float(out.rgba[i + size_t(k)]) / 255.f);
			out.rgba[i + size_t(k)] = uint8_t(std::lround(toSrgb(aces(linear * 0.9f)) * 255.f));
		}
	return out;
}

ui::Color ColorOf(const Rgba &c) { return {float(c[0]) / 255.f, float(c[1]) / 255.f, float(c[2]) / 255.f, float(c[3]) / 255.f}; }

String Hex(const Rgba &c) { return String::Format("#%02x%02x%02x%02x", c[0], c[1], c[2], c[3]); }

/// « #rrggbb », « #rrggbbaa » ou « r g b [a] ».
Option<Rgba> ParseColor(const String &text) {
	const String t = text.Trim();
	if (t.StartsWith("#") && (t.GetSize() == 7 || t.GetSize() == 9)) {
		Rgba c{0, 0, 0, 255};
		for (size_t k = 0; k < (t.GetSize() - 1) / 2; ++k) {
			char *end = nullptr;
			const String pair = t.Substr(1 + k * 2, 2);
			const long v = std::strtol(pair.CStr(), &end, 16);
			if (!end || *end != '\0')
				return NONE;
			c[k] = uint8_t(v);
		}
		return Some(c);
	}
	std::vector<int> v;
	for (const String &w : t.Split(' '))
		if (Option<double> n = Number(w.Trim()); n.IsSome())
			v.push_back(std::clamp(int(n.Value()), 0, 255));
	if (v.size() < 3)
		return NONE;
	return Some(Rgba{uint8_t(v[0]), uint8_t(v[1]), uint8_t(v[2]), uint8_t(v.size() > 3 ? v[3] : 255)});
}

} // namespace

const char *PaintToolId(PaintTool tool) noexcept {
	switch (tool) {
	case PaintTool::ERASER:
		return "eraser";
	case PaintTool::PICKER:
		return "picker";
	case PaintTool::FILL:
		return "fill";
	case PaintTool::LINE:
		return "line";
	case PaintTool::RECT:
		return "rect";
	case PaintTool::ELLIPSE:
		return "ellipse";
	case PaintTool::SELECT:
		return "select";
	case PaintTool::MOVE:
		return "move";
	case PaintTool::PENCIL:
	default:
		return "pencil";
	}
}

Option<PaintTool> PaintToolOf(const String &id) noexcept {
	for (PaintTool t : {PaintTool::PENCIL, PaintTool::ERASER, PaintTool::PICKER, PaintTool::FILL, PaintTool::LINE, PaintTool::RECT,
						PaintTool::ELLIPSE, PaintTool::SELECT, PaintTool::MOVE})
		if (id == PaintToolId(t))
			return Some(t);
	return NONE;
}

TextureEditor::TextureEditor(Shell &shell, StudioReport *report) : m_shell(shell), m_report(report) {}

TextureEditor::~TextureEditor() = default;

voxel::Game *TextureEditor::Game() {
	Session *session = m_shell.Game();
	return session && session->Ready() ? &session->Game() : nullptr;
}

void TextureEditor::Visit(const char *feature, FeatureStatus status, String detail) {
	if (m_report)
		m_report->Visit(String(feature), status, std::move(detail));
}

// ── Panneau ──────────────────────────────────────────────────────────────────

void TextureEditor::Build(ui::Widget parent) {
	auto toolButton = [this](const char *label, PaintTool tool, const char *tip) {
		return ui::Button(label).Tooltip(tip).OnClick([this, tool] { (void)SetTool(tool); });
	};
	parent.Append(ui::Column().Grow().Gap(4.f).Children({
		ui::Label("Aucune texture — choisissez-en une dans l'onglet « Textures » ou créez-en une.").Class("studio-section").Ref(m_title),
		ui::Row().Gap(2.f).Children({
			toolButton("✎", PaintTool::PENCIL, "Crayon"),
			toolButton("⌫", PaintTool::ERASER, "Gomme"),
			toolButton("⊙", PaintTool::PICKER, "Pipette"),
			toolButton("▧", PaintTool::FILL, "Remplissage"),
			toolButton("╱", PaintTool::LINE, "Ligne"),
			toolButton("▭", PaintTool::RECT, "Rectangle"),
			toolButton("◯", PaintTool::ELLIPSE, "Ellipse"),
			toolButton("⬚", PaintTool::SELECT, "Sélection rectangulaire"),
			toolButton("✥", PaintTool::MOVE, "Déplacer (sélection ou calque)"),
			ui::Button("↶").Tooltip("Annuler").OnClick([this] { (void)Undo(); }),
			ui::Button("↷").Tooltip("Rétablir").OnClick([this] { (void)Redo(); }),
		}),
		ui::Row().Gap(4.f).Children({
			ui::Input("couleur", String("#000000ff")).Width(96.f).Ref(m_color).OnSubmit([this](const String &text) {
				if (m_doc.IsSome())
					if (Option<Rgba> c = ParseColor(text); c.IsSome())
						m_doc.Value().brush.color = c.Value();
			}),
			ui::Label("taille").Class("studio-status"),
			ui::Input("1", String("1")).Width(40.f).Ref(m_brushSize).OnSubmit([this](const String &text) {
				if (Option<double> n = Number(text.Trim()); n.IsSome() && m_doc.IsSome())
					m_doc.Value().brush.size = std::clamp(int(n.Value()), 1, 64);
			}),
			ui::Combo({String("normal"), String("multiply"), String("screen"), String("overlay")}, 0).OnSelect([this](int i) {
				if (m_doc.IsSome())
					m_doc.Value().brush.blend = PaintBlend(std::clamp(i, 0, 3));
			}),
			ui::Combo({String("sans symétrie"), String("H"), String("V"), String("HV")}, 0).OnSelect([this](int i) {
				if (m_doc.IsSome())
					m_doc.Value().brush.symmetry = PaintSymmetry(std::clamp(i, 0, 3));
			}),
			ui::Checkbox("plein", false).OnToggle([this](bool on) { shapeFilled = on; }),
		}),
		ui::Row().Id("texture-palette").Gap(2.f).Ref(m_palette),
		ui::Canvas([this](ui::Painter &painter, const sdl3::FRect &rect) { DrawCanvas(painter, rect); }).Id("texture-canvas").Height(300.f),
		ui::Label("").Class("studio-status").Id("texture-status").Ref(m_status),
		ui::TabView({String("Calques"), String("Aperçu"), String("Variantes"), String("Atlas"), String("Fichier")}).Grow().Ref(m_tabs).Children({
			ui::ScrollView().Grow().Children({
				ui::Column().Gap(2.f).Children({
					ui::Row().Gap(2.f).Children({
						ui::Button("+ calque").OnClick([this] {
							if (m_doc.IsSome())
								m_doc.Value().AddLayer(String::Format("Calque %zu", m_doc.Value().Layers().size() + 1));
						}),
						ui::Button("Dupliquer").OnClick([this] {
							if (m_doc.IsSome())
								(void)m_doc.Value().DuplicateLayer(m_doc.Value().Active());
						}),
						ui::Button("Fusionner ↓").OnClick([this] {
							if (m_doc.IsSome())
								(void)m_doc.Value().MergeDown(m_doc.Value().Active());
						}),
						ui::Button("Supprimer").OnClick([this] {
							if (m_doc.IsSome())
								(void)m_doc.Value().RemoveLayer(m_doc.Value().Active());
						}),
					}),
					ui::Row().Gap(2.f).Children({
						ui::Button("Flou").OnClick([this] {
							if (m_doc.IsSome())
								m_doc.Value().Blur();
						}),
						ui::Button("Netteté").OnClick([this] {
							if (m_doc.IsSome())
								m_doc.Value().Sharpen();
						}),
						ui::Button("Vider").OnClick([this] {
							if (m_doc.IsSome())
								m_doc.Value().Clear();
						}),
						ui::Combo({String("bruit"), String("dégradé"), String("briques"), String("damier")}, 0).OnSelect([this](int i) {
							static const char *KINDS[] = {"noise", "gradient", "bricks", "checker"};
							if (m_doc.IsSome())
								(void)m_doc.Value().Generate(String(KINDS[std::clamp(i, 0, 3)]), uint32_t(m_doc.Value().Version()));
						}),
					}),
					ui::Column().Id("texture-layers").Ref(m_layers),
				}),
			}),
			ui::ScrollView().Grow().Children({
				ui::Column().Gap(4.f).Children({
					ui::Combo({String("rendu 8 px"), String("16 px"), String("32 px"), String("64 px"), String("128 px"), String("256 px")}, 1)
						.OnSelect([this](int i) {
							renderSize = 8 << std::clamp(i, 0, 5);
							m_previewVersion = ~0ull;
						}),
					ui::Canvas([this](ui::Painter &painter, const sdl3::FRect &rect) { DrawPreviews(painter, rect); })
						.Id("texture-previews")
						.Height(190.f),
					ui::Label("Mipmaps (jusqu'à 1 px, comme l'atlas) · éclairé par le ciel · lueur (pleine luminosité)").Class("studio-status"),
				}),
			}),
			ui::ScrollView().Grow().Children({
				ui::Column().Gap(4.f).Children({
					ui::Row().Gap(2.f).Children({
						ui::Button("+ copie").Tooltip("Nouvelle variante : l'image courante").OnClick([this] { (void)AddVariant(String("copy")); }),
						ui::Button("+ bruit").Tooltip("Nouvelle variante : bruit entre les couleurs du pinceau").OnClick([this] {
							(void)AddVariant(String("noise"));
						}),
					}),
					ui::Column().Id("texture-variants").Ref(m_variantList),
					ui::Canvas([this](ui::Painter &painter, const sdl3::FRect &rect) { DrawVariants(painter, rect); })
						.Id("texture-witness")
						.Height(180.f),
					ui::Label("Bloc témoin : la variante tirée à chaque position (comme le mailleur)").Class("studio-status"),
				}),
			}),
			ui::ScrollView().Grow().Children({
				ui::Column().Gap(4.f).Children({
					ui::Row().Gap(4.f).Children({
						ui::Label("tuile").Class("studio-status"),
						ui::Input("16", String("16")).Width(50.f).Ref(m_tileSize),
						ui::Label("max").Class("studio-status"),
						ui::Input("256", String("256")).Width(50.f).Ref(m_maxTile),
						ui::Button("Reconstruire l'atlas").OnClick([this] {
							const Option<double> t = Number(m_tileSize.Text().Trim()), m = Number(m_maxTile.Text().Trim());
							(void)RebuildAtlas(t.IsSome() ? int(t.Value()) : 16, m.IsSome() ? int(m.Value()) : 256);
						}),
					}),
					ui::Column().Id("texture-atlas").Ref(m_atlasInfo),
					ui::Column().Id("texture-atlas-tiles").Ref(m_atlasTiles),
				}),
			}),
			ui::Column().Gap(4.f).Children({
				ui::Button("Enregistrer dans le mod et appliquer").OnClick([this] { (void)Save(); }),
				ui::Input("chemin (.png .jpg, ou dossier de projet)", String("data/voxelia/exports/texture.png")).Ref(m_ioPath),
				ui::Row().Gap(4.f).Children({
					ui::Button("Importer").OnClick([this] { (void)Import(m_ioPath.Text().Trim()); }),
					ui::Button("Exporter").OnClick([this] { (void)Export(m_ioPath.Text().Trim(), 0); }),
					ui::Button("Exporter au rendu").OnClick([this] { (void)Export(m_ioPath.Text().Trim(), renderSize); }),
				}),
				ui::Row().Gap(4.f).Children({
					ui::Button("Enregistrer le projet").OnClick([this] { (void)SaveProject(m_ioPath.Text().Trim()); }),
					ui::Button("Ouvrir un projet").OnClick([this] { (void)LoadProject(m_ioPath.Text().Trim()); }),
				}),
			}),
		}),
	}));
	RebuildAtlasInfo();
}

void TextureEditor::BuildCatalog(ui::Widget parent) {
	parent.Append(ui::Column().Grow().Gap(4.f).Children({
		ui::Row().Gap(4.f).Children({
			ui::Label("Textures").Class("studio-dock-title").Grow(),
			ui::Button("+").Tooltip("Nouvelle texture 16 × 16 (mod « core »)").OnClick([this] {
				(void)New(16, 16, String::Format("core:texture_%zu", size_t(m_uploadSerial % 1000)));
			}),
		}),
		ui::Input("Filtrer…").Ref(m_catalogFilter).OnTextChange([this](const String &text) {
			m_filter = text.ToLower();
			RebuildCatalog();
		}),
		ui::ScrollView().Id("texture-catalog").Grow().Ref(m_catalog),
	}));
	RebuildCatalog();
}

void TextureEditor::RebuildCatalog() {
	if (!m_catalog.Valid())
		return;
	m_catalog.Clear();
	voxel::Game *game = Game();
	if (!game) {
		m_catalog.Append(ui::Label("aucune partie en cours").Class("studio-status"));
		return;
	}
	std::vector<String> ids;
	for (const voxel::TextureDef *t : game->Content().Textures())
		if (m_filter.IsEmpty() || t->id.ToLower().Contains(m_filter))
			ids.push_back(t->id);
	std::sort(ids.begin(), ids.end());
	for (size_t i = 0; i < ids.size() && i < CATALOG_MAX; ++i) {
		const String id = ids[i];
		m_catalog.Append(ui::Label(String("  ") + id).Class("studio-row").OnClick([this, id] { (void)Open(id); }));
	}
	if (ids.size() > CATALOG_MAX)
		m_catalog.Append(ui::Label(String::Format("… %zu de plus (filtrez)", ids.size() - CATALOG_MAX)).Class("studio-status"));
}

void TextureEditor::RefreshHeader() {
	if (!m_doc.IsSome())
		return;
	const TextureDocument &doc = m_doc.Value();
	m_title.SetText(String::Format("%s%s — %d × %d (source) · rendu %d px", m_textureId.CStr(), doc.Dirty() ? " ●" : "", doc.Width(),
								   doc.Height(), renderSize));
	m_status.SetText(String::Format("%s · calque « %s » · annulations %zu", PaintToolId(m_tool), doc.Layers()[size_t(doc.Active())].name.CStr(),
									doc.UndoDepth()));
	if (m_color.Valid() && doc.brush.color != m_shownColor) { // pipette, palette, console
		m_shownColor = doc.brush.color;
		m_color.SetText(Hex(doc.brush.color));
	}
}

void TextureEditor::RebuildLayers() {
	m_layers.Clear();
	if (!m_doc.IsSome())
		return;
	const TextureDocument &doc = m_doc.Value();
	// Du haut (dernier) au fond, comme dans les logiciels de dessin.
	for (int i = int(doc.Layers().size()) - 1; i >= 0; --i) {
		const PaintLayer &l = doc.Layers()[size_t(i)];
		m_layers.Append(ui::Row().Gap(2.f).Children({
			ui::Checkbox(String(), l.visible).OnToggle([this, i](bool on) {
				if (m_doc.IsSome())
					(void)m_doc.Value().SetLayerVisible(i, on);
			}),
			ui::Label((i == doc.Active() ? String("▶ ") : String("   ")) + l.name + (l.group.IsEmpty() ? String() : String("  [") + l.group + String("]")))
				.Class("studio-row")
				.Grow()
				.OnClick([this, i] {
					if (m_doc.IsSome())
						(void)m_doc.Value().SetActive(i);
				}),
			ui::Combo({String("normal"), String("multiply"), String("screen"), String("overlay")}, int(l.blend)).OnSelect([this, i](int m) {
				if (m_doc.IsSome())
					(void)m_doc.Value().SetLayerBlend(i, PaintBlend(std::clamp(m, 0, 3)));
			}),
			ui::Slider(0.f, 1.f, l.opacity, 0.05f).Width(70.f).OnChange([this, i](float v) {
				if (m_doc.IsSome() && std::fabs(m_doc.Value().Layers()[size_t(i)].opacity - v) > 1e-3f)
					(void)m_doc.Value().SetLayerOpacity(i, v);
			}),
			ui::Button("▲").OnClick([this, i] {
				if (m_doc.IsSome() && i + 1 < int(m_doc.Value().Layers().size()))
					(void)m_doc.Value().MoveLayer(i, i + 1);
			}),
			ui::Button("▼").OnClick([this, i] {
				if (m_doc.IsSome() && i > 0)
					(void)m_doc.Value().MoveLayer(i, i - 1);
			}),
		}));
	}
}

void TextureEditor::RebuildPalette() {
	m_palette.Clear();
	if (!m_doc.IsSome())
		return;
	for (const Rgba &c : m_doc.Value().Palette())
		m_palette.Append(ui::ColorSwatch(ColorOf(c)).Width(16.f).Height(16.f).Tooltip(Hex(c)).OnClick([this, c] {
			if (m_doc.IsSome())
				m_doc.Value().brush.color = c;
		}));
}

void TextureEditor::RebuildAtlasInfo() {
	if (!m_atlasInfo.Valid())
		return;
	m_atlasInfo.Clear();
	m_atlasTiles.Clear();
	Session *session = m_shell.Game();
	if (!session || !session->Ready())
		return;
	const WorldRenderer::AtlasStats &a = session->Renderer().AtlasInfo();
	m_tileSize.SetText(String::Format("%d", a.tileSize));
	m_maxTile.SetText(String::Format("%d", a.maxTileSize));
	m_atlasInfo.Append(ui::Label(String::Format("atlas %d × %d · %zu tuiles · occupé à %.0f %% · tuile %d px, max %d px · %.0f ms", a.width,
												 a.height, a.tiles, double(a.occupancy) * 100.0, a.tileSize, a.maxTileSize,
												 double(a.milliseconds))));
	size_t shown = 0;
	for (const auto &[id, rect] : session->Renderer().Atlas().Tiles()) {
		if (++shown > 200) {
			m_atlasTiles.Append(ui::Label("…").Class("studio-status"));
			break;
		}
		const int px = int(std::lround((rect.u1 - rect.u0) * float(a.width))) + 1; // rentré d'un demi-texel de chaque côté
		m_atlasTiles.Append(ui::Label(String::Format("  %s — %d px", id.CStr(), px)).Class("studio-status"));
	}
}

std::vector<String> TextureEditor::Variants() const {
	Session *session = m_shell.Game();
	if (!session || !session->Ready() || m_textureId.IsEmpty())
		return {};
	const voxel::TextureDef *def = session->Game().Content().FindTexture(m_textureId);
	if (def && !def->variants.empty())
		return def->variants;
	// Une variante ouverte : celles de sa texture mère.
	for (const voxel::TextureDef *t : session->Game().Content().Textures())
		if (std::find(t->variants.begin(), t->variants.end(), m_textureId) != t->variants.end())
			return t->variants;
	return {};
}

void TextureEditor::RebuildVariants() {
	if (!m_variantList.Valid())
		return;
	m_variantList.Clear();
	m_variantImages.clear();
	const std::vector<String> variants = Variants();
	voxel::Game *game = Game();
	if (variants.empty() || !game) {
		m_variantList.Append(ui::Label("Pas de variante : « + » sépare la texture en nom.1.png, nom.2.png…").Class("studio-status"));
		return;
	}
	for (size_t i = 0; i < variants.size(); ++i) {
		const String id = variants[i];
		m_variantList.Append(ui::Row().Gap(2.f).Children({
			ui::Label((id == m_textureId ? String("▶ ") : String("   ")) + id).Class("studio-row").Grow().OnClick([this, id] { (void)Open(id); }),
			ui::Button("✕").Tooltip("Supprimer le fichier de cette variante").OnClick([this, i] { (void)RemoveVariant(i); }),
		}));
		// La variante en cours d'édition : l'image du document.
		if (id == m_textureId && m_doc.IsSome())
			m_variantImages.push_back(m_doc.Value().Rendered(renderSize));
		else if (Result<Image, String> img = game->Assets().RenderTexture(id); img.IsOk())
			m_variantImages.push_back(voxel::TextureAtlas::Resample(img.Value(), renderSize));
		else
			m_variantImages.push_back(Image{});
	}
}

// ── Mise à jour et dessin ────────────────────────────────────────────────────

void TextureEditor::Update(float dt) {
	m_catalogTimer += dt;
	if (m_catalogTimer >= CATALOG_PERIOD) {
		m_catalogTimer = 0.f;
		if (m_catalog.Valid() && m_catalog.Children().empty())
			RebuildCatalog(); // la partie est arrivée après le panneau
	}
	if (!m_doc.IsSome())
		return;
	const uint64_t version = m_doc.Value().Version();
	if (version != m_shownVersion && !m_dragging) {
		m_shownVersion = version;
		RebuildLayers();
		RebuildPalette();
		RefreshHeader();
	} else if (m_dragging) {
		RefreshHeader();
	}
	m_previewTimer -= dt;
	if (version != m_previewVersion && m_previewTimer <= 0.f) {
		m_previewTimer = PREVIEW_PERIOD;
		m_previewVersion = version;
		m_mips = m_doc.Value().MipChain(renderSize);
		m_lit = SkyLit(m_mips.front());
		m_glow = m_doc.Value().Glow(renderSize);
		RebuildVariants();
		++m_uploadSerial;
	}
}

sdl3::Texture *TextureEditor::Upload(sdl3::Renderer &renderer, const String &key, const Image &image) {
	if (image.IsEmpty())
		return nullptr;
	auto it = std::find_if(m_uploads.begin(), m_uploads.end(), [&](const auto &e) { return e.first == key; });
	Uploaded *u = it == m_uploads.end() ? nullptr : it->second.get();
	const uint64_t version = key == "canvas" && m_doc.IsSome() ? m_doc.Value().Version() : m_uploadSerial;
	if (!u || u->width != image.width || u->height != image.height) {
		auto created = sdl3::Texture::Create(renderer, image.width, image.height, sdl3::PixelFormat::RGBA32);
		if (created.IsError())
			return nullptr;
		auto entry = std::make_unique<Uploaded>(Uploaded{std::move(created).Unwrap(), image.width, image.height, ~0ull});
		(void)entry->texture.SetScaleMode(SDL_SCALEMODE_NEAREST);
		(void)entry->texture.SetBlendMode(sdl3::BlendMode::BLEND);
		u = entry.get();
		if (it == m_uploads.end())
			m_uploads.push_back({key, std::move(entry)});
		else
			it->second = std::move(entry);
	}
	if (u->version != version) {
		(void)u->texture.Update(image.rgba.data(), image.width * 4);
		u->version = version;
	}
	return &u->texture;
}

void TextureEditor::DrawCanvas(ui::Painter &painter, const sdl3::FRect &rect) {
	m_canvasRect = rect;
	painter.FillRect(rect, ui::Color{0.12f, 0.13f, 0.16f, 1.f});
	if (!m_doc.IsSome() || !painter.Renderer())
		return;
	const TextureDocument &doc = m_doc.Value();
	const float fit = std::min(rect.w / float(doc.Width()), rect.h / float(doc.Height()));
	const float zoom = m_zoom > 0.f ? m_zoom : fit;
	const float w = float(doc.Width()) * zoom, h = float(doc.Height()) * zoom;
	m_imageRect = sdl3::FRect{rect.x + (rect.w - w) * 0.5f, rect.y + (rect.h - h) * 0.5f, w, h};
	painter.PushClip(rect);
	painter.Checkerboard(m_imageRect, std::max(4.f, zoom));
	if (sdl3::Texture *tex = Upload(*painter.Renderer(), String("canvas"), doc.Composite()))
		painter.Image(*tex, m_imageRect);
	// Grille des pixels quand ils sont assez gros.
	if (zoom >= 6.f) {
		const ui::Color grid{1.f, 1.f, 1.f, 0.12f};
		for (int x = 0; x <= doc.Width(); ++x)
			painter.Line({m_imageRect.x + float(x) * zoom, m_imageRect.y}, {m_imageRect.x + float(x) * zoom, m_imageRect.y + h}, 1.f, grid);
		for (int y = 0; y <= doc.Height(); ++y)
			painter.Line({m_imageRect.x, m_imageRect.y + float(y) * zoom}, {m_imageRect.x + w, m_imageRect.y + float(y) * zoom}, 1.f, grid);
	}
	auto box = [&](int x0, int y0, int x1, int y1, ui::Color c) {
		const float ax = m_imageRect.x + float(std::min(x0, x1)) * zoom, ay = m_imageRect.y + float(std::min(y0, y1)) * zoom;
		const float bx = m_imageRect.x + float(std::max(x0, x1) + 1) * zoom, by = m_imageRect.y + float(std::max(y0, y1) + 1) * zoom;
		const sdl3::FPoint pts[4] = {{ax, ay}, {bx, ay}, {bx, by}, {ax, by}};
		painter.Polyline(pts, 1.5f, c, true);
	};
	if (const PaintRect &s = doc.Selection(); !s.Empty())
		box(s.x, s.y, s.x + s.w - 1, s.y + s.h - 1, ui::Color{0.4f, 0.8f, 1.f, 1.f});
	// Forme en cours de tracé.
	if (m_dragging && (m_tool == PaintTool::LINE || m_tool == PaintTool::RECT || m_tool == PaintTool::ELLIPSE || m_tool == PaintTool::SELECT)) {
		if (m_tool == PaintTool::LINE)
			painter.Line({m_imageRect.x + (float(m_dragStart[0]) + 0.5f) * zoom, m_imageRect.y + (float(m_dragStart[1]) + 0.5f) * zoom},
						 {m_imageRect.x + (float(m_dragNow[0]) + 0.5f) * zoom, m_imageRect.y + (float(m_dragNow[1]) + 0.5f) * zoom}, 1.5f,
						 ui::Color{1.f, 0.85f, 0.3f, 1.f});
		else
			box(m_dragStart[0], m_dragStart[1], m_dragNow[0], m_dragNow[1], ui::Color{1.f, 0.85f, 0.3f, 1.f});
	}
	painter.PopClip();
}

void TextureEditor::DrawPreviews(ui::Painter &painter, const sdl3::FRect &rect) {
	painter.FillRect(rect, ui::Color{0.12f, 0.13f, 0.16f, 1.f});
	if (!painter.Renderer() || m_mips.empty())
		return;
	// Ligne du haut : chaque niveau agrandi à la même taille (le pixel perdu se voit).
	const float cell = std::min(64.f, (rect.w - 4.f) / float(m_mips.size()) - 4.f);
	for (size_t i = 0; i < m_mips.size(); ++i) {
		const sdl3::FRect dst{rect.x + 4.f + float(i) * (cell + 4.f), rect.y + 4.f, cell, cell};
		painter.Checkerboard(dst, 4.f);
		if (sdl3::Texture *t = Upload(*painter.Renderer(), String::Format("mip%zu", i), m_mips[i]))
			painter.Image(*t, dst);
	}
	// Ligne du bas : éclairé par le ciel / lueur, côte à côte.
	const float big = std::min(110.f, rect.h - cell - 16.f);
	const sdl3::FRect lit{rect.x + 4.f, rect.y + cell + 12.f, big, big}, glow{rect.x + big + 16.f, rect.y + cell + 12.f, big, big};
	painter.Checkerboard(lit, 6.f);
	painter.Checkerboard(glow, 6.f);
	if (sdl3::Texture *t = Upload(*painter.Renderer(), String("lit"), m_lit))
		painter.Image(*t, lit);
	if (sdl3::Texture *t = Upload(*painter.Renderer(), String("glow"), m_glow))
		painter.Image(*t, glow);
}

void TextureEditor::DrawVariants(ui::Painter &painter, const sdl3::FRect &rect) {
	painter.FillRect(rect, ui::Color{0.12f, 0.13f, 0.16f, 1.f});
	if (!painter.Renderer() || m_variantImages.empty())
		return;
	const float cell = std::min((rect.w - 8.f) / float(WITNESS), (rect.h - 8.f) / float(WITNESS));
	for (int z = 0; z < WITNESS; ++z)
		for (int x = 0; x < WITNESS; ++x) {
			const size_t pick = voxel::VariantIndex(voxel::BlockPos{x, 0, z}, m_variantImages.size());
			const sdl3::FRect dst{rect.x + 4.f + float(x) * cell, rect.y + 4.f + float(z) * cell, cell, cell};
			if (sdl3::Texture *t = Upload(*painter.Renderer(), String::Format("variant%zu", pick), m_variantImages[pick]))
				painter.Image(*t, dst);
		}
}

Option<std::array<int, 2>> TextureEditor::PixelAt(float x, float y) const {
	if (!m_doc.IsSome() || m_imageRect.w <= 0.f || m_imageRect.h <= 0.f)
		return NONE;
	const int px = int(std::floor((x - m_imageRect.x) / m_imageRect.w * float(m_doc.Value().Width())));
	const int py = int(std::floor((y - m_imageRect.y) / m_imageRect.h * float(m_doc.Value().Height())));
	return Some(std::array<int, 2>{px, py});
}

bool TextureEditor::HandleEvent(const sdl3::Event &ev) {
	if (!m_doc.IsSome())
		return false;
	const uint32_t type = ev.raw.type;
	auto inside = [&](float x, float y) {
		return x >= m_canvasRect.x && y >= m_canvasRect.y && x < m_canvasRect.x + m_canvasRect.w && y < m_canvasRect.y + m_canvasRect.h;
	};
	TextureDocument &doc = m_doc.Value();
	if (type == SDL_EVENT_MOUSE_WHEEL && inside(ev.raw.wheel.mouse_x, ev.raw.wheel.mouse_y)) {
		const float fit = std::min(m_canvasRect.w / float(doc.Width()), m_canvasRect.h / float(doc.Height()));
		const float zoom = m_zoom > 0.f ? m_zoom : fit;
		m_zoom = std::clamp(zoom * (ev.raw.wheel.y > 0.f ? 1.25f : 0.8f), 0.25f, 64.f);
		return true;
	}
	if (type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.raw.button.button == SDL_BUTTON_LEFT && inside(ev.raw.button.x, ev.raw.button.y)) {
		const Option<std::array<int, 2>> p = PixelAt(ev.raw.button.x, ev.raw.button.y);
		if (p.IsNone())
			return false;
		m_dragging = true;
		m_dragStart = m_dragNow = p.Value();
		if (m_tool == PaintTool::PENCIL || m_tool == PaintTool::ERASER) {
			doc.BeginStroke();
			doc.StrokeTo(p.Value()[0], p.Value()[1], m_tool == PaintTool::ERASER);
		} else if (m_tool == PaintTool::PICKER || m_tool == PaintTool::FILL) {
			(void)Apply(p.Value()[0], p.Value()[1], p.Value()[0], p.Value()[1]);
			m_dragging = false;
		}
		return true;
	}
	if (type == SDL_EVENT_MOUSE_MOTION && m_dragging) {
		if (const Option<std::array<int, 2>> p = PixelAt(ev.raw.motion.x, ev.raw.motion.y); p.IsSome()) {
			m_dragNow = p.Value();
			if (m_tool == PaintTool::PENCIL || m_tool == PaintTool::ERASER)
				doc.StrokeTo(p.Value()[0], p.Value()[1], m_tool == PaintTool::ERASER);
		}
		return true;
	}
	if (type == SDL_EVENT_MOUSE_BUTTON_UP && ev.raw.button.button == SDL_BUTTON_LEFT && m_dragging) {
		m_dragging = false;
		if (m_tool == PaintTool::PENCIL || m_tool == PaintTool::ERASER) {
			doc.EndStroke();
			Visit(m_tool == PaintTool::PENCIL ? "texture.paint" : "texture.erase");
		} else {
			(void)Apply(m_dragStart[0], m_dragStart[1], m_dragNow[0], m_dragNow[1]);
		}
		return true;
	}
	return false;
}

// ── Actions ──────────────────────────────────────────────────────────────────

CommandResult TextureEditor::Opened(TextureDocument doc, const String &id, const String &path, const char *feature) {
	m_doc = Some(std::move(doc));
	m_textureId = id;
	m_path = path;
	m_shownVersion = m_previewVersion = ~0ull;
	m_previewTimer = 0.f;
	m_zoom = 0.f;
	m_uploads.clear();
	if (onDocumentOpened)
		onDocumentOpened();
	Visit(feature, FeatureStatus::Ok, id);
	return CommandResult::Ok(String::Format("%s : %d × %d", id.CStr(), m_doc.Value().Width(), m_doc.Value().Height()));
}

CommandResult TextureEditor::Open(const String &textureId) {
	voxel::Game *game = Game();
	if (!game)
		return CommandResult::Error(String("aucune partie en cours"));
	const voxel::TextureDef *def = game->Content().FindTexture(textureId);
	if (!def)
		return CommandResult::Error(String("texture inconnue : ") + textureId);
	Result<Image, String> pixels = game->Assets().RenderTexture(textureId);
	if (pixels.IsError())
		return CommandResult::Error(pixels.Error());
	Result<TextureDocument, String> doc = TextureDocument::FromImage(std::move(pixels).Unwrap(), textureId);
	if (doc.IsError())
		return CommandResult::Error(doc.Error());
	return Opened(std::move(doc).Unwrap(), textureId, def->path, "texture.open");
}

CommandResult TextureEditor::New(int width, int height, const String &id) {
	Result<TextureDocument, String> doc = TextureDocument::Create(width, height, id);
	if (doc.IsError())
		return CommandResult::Error(doc.Error());
	const String qualified = id.Contains(":") ? id : String("core:") + id;
	return Opened(std::move(doc).Unwrap(), qualified, String(), "texture.new");
}

CommandResult TextureEditor::Import(const String &path) {
	Result<TextureDocument, String> doc = TextureDocument::Import(path);
	if (doc.IsError())
		return CommandResult::Error(doc.Error());
	const String id = String("core:") + doc.Value().Name();
	return Opened(std::move(doc).Unwrap(), id, String(), "texture.import");
}

CommandResult TextureEditor::Export(const String &path, int size) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune texture ouverte"));
	if (Result<bool, String> r = m_doc.Value().Export(path, size); r.IsError())
		return CommandResult::Error(r.Error());
	Visit("texture.export", FeatureStatus::Ok, path);
	return CommandResult::Ok(String("exportée : ") + path);
}

CommandResult TextureEditor::SaveProject(const String &dir) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune texture ouverte"));
	if (Result<bool, String> r = m_doc.Value().SaveProject(dir); r.IsError())
		return CommandResult::Error(r.Error());
	Visit("texture.project_save", FeatureStatus::Ok, dir);
	return CommandResult::Ok(String("projet enregistré : ") + dir);
}

CommandResult TextureEditor::LoadProject(const String &dir) {
	Result<TextureDocument, String> doc = TextureDocument::LoadProject(dir);
	if (doc.IsError())
		return CommandResult::Error(doc.Error());
	const String id = m_textureId.IsEmpty() ? String("core:") + doc.Value().Name() : m_textureId;
	return Opened(std::move(doc).Unwrap(), id, m_path, "texture.project_load");
}

String TextureEditor::TargetPath(const String &id) const {
	Session *session = m_shell.Game();
	if (!session || !session->Ready())
		return String();
	voxel::Game &game = session->Game();
	if (const voxel::TextureDef *def = game.Content().FindTexture(id); def && !def->path.IsEmpty()) {
		const String lower = def->path.ToLower();
		if (lower.EndsWith(".png") || lower.EndsWith(".jpg") || lower.EndsWith(".jpeg"))
			return def->path;
	}
	const size_t colon = id.Find(":");
	const String mod = colon == String::NPOS ? String("core") : id.Substr(0, colon);
	const String name = colon == String::NPOS ? id : id.Substring(colon + 1);
	for (const voxel::ModInfo &m : game.Mods().mods)
		if (m.id == mod)
			return m.path + String("/textures/") + name + String(".png");
	return String();
}

CommandResult TextureEditor::Reload(const String &id, const String &path, bool newDefinition) {
	Session *session = m_shell.Game();
	if (!session || !session->Ready())
		return CommandResult::Error(String("aucune partie en cours"));
	voxel::Game &game = session->Game();
	if (newDefinition) {
		// Texture qui n'était pas un fichier (ou nouvelle) : définie par son image.
		const size_t colon = id.Find(":");
		dataio::NodePtr def = dataio::Node::MakeObject();
		def->Set("type", voxel::json::MakeStr("texture"));
		def->Set("id", voxel::json::MakeStr(id));
		def->Set("path", voxel::json::MakeStr(path));
		voxel::ContentRegistry::LoadContext ctx;
		ctx.mod = colon == String::NPOS ? String("core") : id.Substr(0, colon);
		if (auto r = game.ReloadDefinition(def, ctx); r.IsError())
			return CommandResult::Error(r.Error());
	} else {
		(void)game.Assets().ReloadTexture(id);
	}
	const WorldRenderer::AtlasStats &a = session->Renderer().AtlasInfo();
	Result<WorldRenderer::AtlasStats, String> rebuilt = session->Renderer().RebuildAtlas(a.tileSize, a.maxTileSize);
	if (rebuilt.IsError())
		return CommandResult::Error(rebuilt.Error());
	RebuildAtlasInfo();
	return CommandResult::Ok(String::Format("atlas reconstruit en %.0f ms", double(rebuilt.Value().milliseconds)));
}

CommandResult TextureEditor::Save() {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune texture ouverte"));
	const String path = TargetPath(m_textureId);
	if (path.IsEmpty())
		return CommandResult::Error(String("mod introuvable pour ") + m_textureId);
	if (Result<bool, String> r = m_doc.Value().Export(path); r.IsError())
		return CommandResult::Error(r.Error());
	voxel::Game *game = Game();
	const voxel::TextureDef *def = game ? game->Content().FindTexture(m_textureId) : nullptr;
	const bool newDefinition = !def || def->path != path;
	CommandResult reloaded = Reload(m_textureId, path, newDefinition);
	if (reloaded.status != CommandStatus::Ok) {
		Visit("texture.save", FeatureStatus::Warning, reloaded.message);
		return reloaded;
	}
	m_doc.Value().MarkSaved();
	m_path = path;
	m_shownVersion = ~0ull;
	RebuildCatalog();
	String note;
	if (def && def->path.IsEmpty())
		note = String(" — la définition du mod (procédure ou composition) reste celle du JSON : ce fichier ne servira au prochain chargement que si elle est retirée");
	Visit("texture.save", note.IsEmpty() ? FeatureStatus::Ok : FeatureStatus::Warning, path);
	const String message = String("enregistrée : ") + path + String(" — ") + reloaded.message + note;
	return note.IsEmpty() ? CommandResult::Ok(message) : CommandResult::Warning(message);
}

CommandResult TextureEditor::Undo() {
	if (!m_doc.IsSome() || !m_doc.Value().Undo())
		return CommandResult::Warning(String("rien à annuler"));
	Visit("texture.undo");
	return CommandResult::Ok(String("annulé"));
}

CommandResult TextureEditor::Redo() {
	if (!m_doc.IsSome() || !m_doc.Value().Redo())
		return CommandResult::Warning(String("rien à rétablir"));
	Visit("texture.redo");
	return CommandResult::Ok(String("rétabli"));
}

CommandResult TextureEditor::SetTool(PaintTool tool) {
	m_tool = tool;
	if (m_doc.IsSome())
		RefreshHeader();
	return CommandResult::Ok(String("outil : ") + String(PaintToolId(tool)));
}

CommandResult TextureEditor::Apply(int ax, int ay, int bx, int by) {
	if (!m_doc.IsSome())
		return CommandResult::Error(String("aucune texture ouverte"));
	TextureDocument &doc = m_doc.Value();
	String detail;
	switch (m_tool) {
	case PaintTool::PENCIL:
	case PaintTool::ERASER:
		doc.Line(ax, ay, bx, by, m_tool == PaintTool::ERASER);
		break;
	case PaintTool::PICKER:
		if (Option<Rgba> c = doc.Pick(ax, ay); c.IsSome())
			detail = Hex(c.Value());
		else
			return CommandResult::Error(String("hors de la toile"));
		break;
	case PaintTool::FILL:
		detail = String::Format("%d pixel(s)", doc.Fill(ax, ay, fillTolerance, fillAlpha));
		break;
	case PaintTool::LINE:
		doc.Line(ax, ay, bx, by);
		break;
	case PaintTool::RECT:
		doc.Rectangle(ax, ay, bx, by, shapeFilled);
		break;
	case PaintTool::ELLIPSE:
		doc.Ellipse(ax, ay, bx, by, shapeFilled);
		break;
	case PaintTool::SELECT:
		if (ax == bx && ay == by)
			doc.Select(PaintRect{}); // un clic : plus de sélection
		else
			doc.Select(PaintRect{std::min(ax, bx), std::min(ay, by), std::abs(bx - ax) + 1, std::abs(by - ay) + 1});
		break;
	case PaintTool::MOVE:
		doc.Move(bx - ax, by - ay);
		break;
	}
	const String feature = String("texture.") + String(PaintToolId(m_tool));
	Visit(feature.CStr(), FeatureStatus::Ok, detail);
	return CommandResult::Ok(feature + (detail.IsEmpty() ? String() : String(" — ") + detail));
}

CommandResult TextureEditor::RebuildAtlas(int tileSize, int maxTileSize) {
	Session *session = m_shell.Game();
	if (!session || !session->Ready())
		return CommandResult::Error(String("aucune partie en cours"));
	Result<WorldRenderer::AtlasStats, String> r = session->Renderer().RebuildAtlas(tileSize, maxTileSize);
	if (r.IsError()) {
		Visit("texture.atlas_rebuild", FeatureStatus::Warning, r.Error());
		return CommandResult::Error(r.Error());
	}
	RebuildAtlasInfo();
	const WorldRenderer::AtlasStats &a = r.Value();
	const String text = String::Format("atlas %d × %d, %zu tuiles, %.0f %% occupé, %.0f ms", a.width, a.height, a.tiles,
									   double(a.occupancy) * 100.0, double(a.milliseconds));
	Visit("texture.atlas_rebuild", FeatureStatus::Ok, text);
	return CommandResult::Ok(text);
}

CommandResult TextureEditor::AddVariant(const String &kind) {
	voxel::Game *game = Game();
	if (!game || !m_doc.IsSome())
		return CommandResult::Error(String("aucune texture ouverte"));
	if (kind != "copy" && kind != "noise")
		return CommandResult::Error(String("copy ou noise"));
	std::vector<String> variants = Variants();
	// La texture mère (« mod:nom ») et ses fichiers « nom.N.png » dans son dossier.
	String base = m_textureId;
	if (!variants.empty())
		for (const voxel::TextureDef *t : game->Content().Textures())
			if (std::find(t->variants.begin(), t->variants.end(), variants.front()) != t->variants.end())
				base = t->id;
	String dir;
	if (!variants.empty()) {
		const voxel::TextureDef *first = game->Content().FindTexture(variants.front());
		dir = first ? String(fs::path(first->path.CStr()).parent_path().string().c_str()) : String();
	} else {
		const String target = TargetPath(m_textureId);
		dir = String(fs::path(target.CStr()).parent_path().string().c_str());
		// Une texture seule devient « nom.1.png » (le jeu ne voit les variantes
		// que sans « nom.png ») : son fichier est déplacé, sinon écrit.
		const String first = dir + String("/") + base.Substring(base.Find(":") + 1) + String(".1.png");
		std::error_code ec;
		if (const voxel::TextureDef *def = game->Content().FindTexture(m_textureId); def && fs::exists(def->path.CStr()))
			fs::rename(def->path.CStr(), first.CStr(), ec);
		else if (Result<bool, String> w = m_doc.Value().Export(first); w.IsError())
			return CommandResult::Error(w.Error());
		if (ec)
			return CommandResult::Error(String::Format("déplacement impossible : %s", ec.message().c_str()));
		variants.push_back(base + String(".1"));
		if (CommandResult r = Reload(variants.back(), first, true); r.status != CommandStatus::Ok)
			return r;
	}
	if (dir.IsEmpty())
		return CommandResult::Error(String("dossier de la texture introuvable"));
	const String name = base.Substring(base.Find(":") + 1);
	const size_t next = variants.size() + 1;
	const String file = dir + String::Format("/%s.%zu.png", name.CStr(), next);
	TextureDocument copy = m_doc.Value();
	if (kind == "noise")
		(void)copy.Generate(String("noise"), uint32_t(next * 7919u));
	if (Result<bool, String> w = copy.Export(file); w.IsError())
		return CommandResult::Error(w.Error());
	const String variantId = base + String::Format(".%zu", next);
	variants.push_back(variantId);
	if (CommandResult r = Reload(variantId, file, true); r.status != CommandStatus::Ok)
		return r;
	// La texture mère liste ses variantes.
	dataio::NodePtr def = dataio::Node::MakeObject();
	def->Set("type", voxel::json::MakeStr("texture"));
	def->Set("id", voxel::json::MakeStr(base));
	dataio::NodePtr list = dataio::Node::MakeArray();
	for (const String &v : variants)
		list->Push(voxel::json::MakeStr(v));
	def->Set("variants", list);
	voxel::ContentRegistry::LoadContext ctx;
	ctx.mod = base.Substr(0, base.Find(":"));
	if (auto r = game->ReloadDefinition(def, ctx); r.IsError())
		return CommandResult::Error(r.Error());
	CommandResult rebuilt = Reload(base, String(), false);
	m_previewVersion = ~0ull;
	Visit("texture.variant_add", FeatureStatus::Ok, variantId);
	return CommandResult::Ok(String::Format("variante %s (%zu en tout) — %s", variantId.CStr(), variants.size(), rebuilt.message.CStr()));
}

CommandResult TextureEditor::RemoveVariant(size_t index) {
	voxel::Game *game = Game();
	const std::vector<String> variants = Variants();
	if (!game || index >= variants.size())
		return CommandResult::Error(String("variante inconnue"));
	const voxel::TextureDef *def = game->Content().FindTexture(variants[index]);
	if (!def || def->path.IsEmpty())
		return CommandResult::Error(String("variante sans fichier"));
	const String path = def->path, id = variants[index];
	return CommandResult::Confirm(String::Format("Supprimer le fichier %s ? Il disparaîtra au prochain chargement des mods.", path.CStr()),
								  [this, path, id]() {
									  std::error_code ec;
									  if (!fs::remove(path.CStr(), ec) || ec)
										  return CommandResult::Error(String("suppression impossible : ") + path);
									  Visit("texture.variant_remove", FeatureStatus::Ok, id);
									  m_previewVersion = ~0ull;
									  return CommandResult::Warning(String("supprimé : ") + path +
																	String(" (la partie garde la variante jusqu'au prochain chargement)"));
								  });
}

// ── Console ──────────────────────────────────────────────────────────────────

void TextureEditor::RegisterCommands(ConsoleModel &console) {
	auto need = [](const std::vector<String> &args, size_t n, const char *usage) -> Option<CommandResult> {
		if (args.size() < n)
			return Some(CommandResult::Error(String("usage : ") + String(usage)));
		return NONE;
	};
	auto doc = [this]() -> TextureDocument * { return m_doc.IsSome() ? &m_doc.Value() : nullptr; };
	console.Register("studio.texture.open", "studio.texture.open <id> — ouvre une texture du contenu", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.texture.open <id>"); e.IsSome())
			return e.Value();
		return Open(a[1]);
	});
	console.Register("studio.texture.new", "studio.texture.new <largeur> <hauteur> <mod:nom>", [this, need](const auto &a) {
		if (auto e = need(a, 4, "studio.texture.new <largeur> <hauteur> <mod:nom>"); e.IsSome())
			return e.Value();
		const Option<std::vector<int>> size = Ints(a, 1, 2);
		return size.IsSome() ? New(size.Value()[0], size.Value()[1], a[3]) : CommandResult::Error(String("taille invalide"));
	});
	console.Register("studio.texture.import", "studio.texture.import <fichier.png|jpg>", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.texture.import <fichier>"); e.IsSome())
			return e.Value();
		return Import(a[1]);
	});
	console.Register("studio.texture.export", "studio.texture.export <fichier.png|jpg> [taille de rendu]", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.texture.export <fichier> [taille]"); e.IsSome())
			return e.Value();
		const Option<double> size = a.size() > 2 ? Number(a[2]) : Some(0.0);
		return Export(a[1], size.IsSome() ? int(size.Value()) : 0);
	});
	console.Register("studio.texture.save", "studio.texture.save — dans le mod, puis atlas reconstruit", [this](const auto &) { return Save(); });
	console.Register("studio.texture.project.save", "studio.texture.project.save <dossier>", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.texture.project.save <dossier>"); e.IsSome())
			return e.Value();
		return SaveProject(a[1]);
	});
	console.Register("studio.texture.project.open", "studio.texture.project.open <dossier>", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.texture.project.open <dossier>"); e.IsSome())
			return e.Value();
		return LoadProject(a[1]);
	});
	console.Register("studio.texture.undo", "studio.texture.undo", [this](const auto &) { return Undo(); });
	console.Register("studio.texture.redo", "studio.texture.redo", [this](const auto &) { return Redo(); });
	console.Register("studio.texture.tool", "studio.texture.tool pencil|eraser|picker|fill|line|rect|ellipse|select|move", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.texture.tool <outil>"); e.IsSome())
			return e.Value();
		const Option<PaintTool> tool = PaintToolOf(a[1]);
		return tool.IsSome() ? SetTool(tool.Value()) : CommandResult::Error(String("outil inconnu : ") + a[1]);
	});
	console.Register("studio.texture.apply", "studio.texture.apply <x> <y> [x2 y2] — geste de l'outil courant", [this, need](const auto &a) {
		if (auto e = need(a, 3, "studio.texture.apply <x> <y> [x2 y2]"); e.IsSome())
			return e.Value();
		const Option<std::vector<int>> p = Ints(a, 1, a.size() >= 5 ? 4 : 2);
		if (p.IsNone())
			return CommandResult::Error(String("coordonnées invalides"));
		const std::vector<int> &v = p.Value();
		return v.size() == 4 ? Apply(v[0], v[1], v[2], v[3]) : Apply(v[0], v[1], v[0], v[1]);
	});
	console.Register("studio.texture.brush", "studio.texture.brush color|secondary <#rrggbb[aa]|r g b [a]> | size <n> | opacity <0..1> | blend <mode> | symmetry none|h|v|hv | filled on|off",
					 [this, need, doc](const auto &a) {
						 if (auto e = need(a, 3, "studio.texture.brush <réglage> <valeur>"); e.IsSome())
							 return e.Value();
						 TextureDocument *d = doc();
						 if (!d)
							 return CommandResult::Error(String("aucune texture ouverte"));
						 String value;
						 for (size_t i = 2; i < a.size(); ++i)
							 value += (i > 2 ? String(" ") : String()) + a[i];
						 const Option<double> n = Number(a[2]);
						 if (a[1] == "color" || a[1] == "secondary") {
							 const Option<Rgba> c = ParseColor(value);
							 if (c.IsNone())
								 return CommandResult::Error(String("couleur : #rrggbb[aa] ou r g b [a]"));
							 (a[1] == "color" ? d->brush.color : d->brush.secondary) = c.Value();
						 } else if (a[1] == "size" && n.IsSome()) {
							 d->brush.size = std::clamp(int(n.Value()), 1, 64);
						 } else if (a[1] == "opacity" && n.IsSome()) {
							 d->brush.opacity = std::clamp(float(n.Value()), 0.f, 1.f);
						 } else if (a[1] == "blend" && PaintBlendOf(a[2]).IsSome()) {
							 d->brush.blend = PaintBlendOf(a[2]).Value();
						 } else if (a[1] == "symmetry") {
							 d->brush.symmetry = a[2] == "h" ? PaintSymmetry::H : a[2] == "v" ? PaintSymmetry::V : a[2] == "hv" ? PaintSymmetry::HV : PaintSymmetry::NONE;
						 } else if (a[1] == "filled") {
							 shapeFilled = a[2] == "on" || a[2] == "true";
						 } else {
							 return CommandResult::Error(String("réglage ou valeur invalide : ") + a[1]);
						 }
						 return CommandResult::Ok(a[1] + String(" = ") + value);
					 });
	console.Register("studio.texture.fill.options", "studio.texture.fill.options <tolérance 0..255> <seuil alpha 0..255>", [this, need](const auto &a) {
		if (auto e = need(a, 3, "studio.texture.fill.options <tolérance> <seuil alpha>"); e.IsSome())
			return e.Value();
		const Option<std::vector<int>> v = Ints(a, 1, 2);
		if (v.IsNone())
			return CommandResult::Error(String("deux nombres attendus"));
		fillTolerance = std::clamp(v.Value()[0], 0, 255);
		fillAlpha = std::clamp(v.Value()[1], 0, 255);
		return CommandResult::Ok(String::Format("tolérance %d, seuil alpha %d", fillTolerance, fillAlpha));
	});
	console.Register("studio.texture.filter", "studio.texture.filter blur|sharpen|clear", [this, need, doc](const auto &a) {
		if (auto e = need(a, 2, "studio.texture.filter blur|sharpen|clear"); e.IsSome())
			return e.Value();
		TextureDocument *d = doc();
		if (!d)
			return CommandResult::Error(String("aucune texture ouverte"));
		if (a[1] == "blur")
			d->Blur();
		else if (a[1] == "sharpen")
			d->Sharpen();
		else if (a[1] == "clear")
			d->Clear();
		else
			return CommandResult::Error(String("blur, sharpen ou clear"));
		Visit((String("texture.") + a[1]).CStr());
		return CommandResult::Ok(a[1]);
	});
	console.Register("studio.texture.generate", "studio.texture.generate noise|gradient|bricks|checker [graine]", [this, need, doc](const auto &a) {
		if (auto e = need(a, 2, "studio.texture.generate <motif> [graine]"); e.IsSome())
			return e.Value();
		TextureDocument *d = doc();
		if (!d)
			return CommandResult::Error(String("aucune texture ouverte"));
		const Option<double> seed = a.size() > 2 ? Number(a[2]) : Some(1.0);
		Result<bool, String> r = d->Generate(a[1], uint32_t(seed.IsSome() ? seed.Value() : 1.0));
		Visit("texture.generate", r.IsOk() ? FeatureStatus::Ok : FeatureStatus::Warning, a[1]);
		return r.IsOk() ? CommandResult::Ok(a[1]) : CommandResult::Error(r.Error());
	});
	console.Register("studio.texture.layer", "studio.texture.layer add <nom> | select|remove|duplicate|merge <i> | move <de> <vers> | opacity <i> <v> | blend <i> <mode> | visible <i> on|off | group <i> <nom>",
					 [this, need, doc](const auto &a) {
						 if (auto e = need(a, 3, "studio.texture.layer <action> …"); e.IsSome())
							 return e.Value();
						 TextureDocument *d = doc();
						 if (!d)
							 return CommandResult::Error(String("aucune texture ouverte"));
						 if (a[1] == "add") {
							 d->AddLayer(a[2]);
							 Visit("texture.layer_add");
							 return CommandResult::Ok(String("calque ") + a[2]);
						 }
						 const Option<double> i = Number(a[2]);
						 if (i.IsNone())
							 return CommandResult::Error(String("index de calque attendu"));
						 const int index = int(i.Value());
						 Result<bool, String> r = Err(String("action inconnue : ") + a[1]);
						 if (a[1] == "select")
							 r = d->SetActive(index);
						 else if (a[1] == "remove")
							 r = d->RemoveLayer(index);
						 else if (a[1] == "duplicate")
							 r = d->DuplicateLayer(index);
						 else if (a[1] == "merge")
							 r = d->MergeDown(index);
						 else if (a.size() > 3) {
							 const Option<double> v = Number(a[3]);
							 if (a[1] == "move" && v.IsSome())
								 r = d->MoveLayer(index, int(v.Value()));
							 else if (a[1] == "opacity" && v.IsSome())
								 r = d->SetLayerOpacity(index, float(v.Value()));
							 else if (a[1] == "blend" && PaintBlendOf(a[3]).IsSome())
								 r = d->SetLayerBlend(index, PaintBlendOf(a[3]).Value());
							 else if (a[1] == "visible")
								 r = d->SetLayerVisible(index, a[3] == "on" || a[3] == "true");
							 else if (a[1] == "group")
								 r = d->SetLayerGroup(index, a[3]);
						 }
						 if (r.IsError())
							 return CommandResult::Error(r.Error());
						 Visit((String("texture.layer_") + a[1]).CStr());
						 return CommandResult::Ok(String("calque ") + a[1]);
					 });
	console.Register("studio.texture.select", "studio.texture.select <x> <y> <l> <h> | none", [this, need, doc](const auto &a) {
		if (auto e = need(a, 2, "studio.texture.select <x> <y> <l> <h> | none"); e.IsSome())
			return e.Value();
		TextureDocument *d = doc();
		if (!d)
			return CommandResult::Error(String("aucune texture ouverte"));
		if (a[1] == "none") {
			d->Select(PaintRect{});
			return CommandResult::Ok(String("sélection vidée"));
		}
		const Option<std::vector<int>> v = Ints(a, 1, 4);
		if (v.IsNone())
			return CommandResult::Error(String("x y largeur hauteur attendus"));
		d->Select(PaintRect{v.Value()[0], v.Value()[1], v.Value()[2], v.Value()[3]});
		Visit("texture.select");
		return CommandResult::Ok(String("sélection"));
	});
	console.Register("studio.texture.render", "studio.texture.render <8..256> — résolution de rendu des aperçus", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.texture.render <taille>"); e.IsSome())
			return e.Value();
		const Option<double> n = Number(a[1]);
		if (n.IsNone() || n.Value() < 1.0 || n.Value() > TextureDocument::MAX_RENDER)
			return CommandResult::Error(String("de 1 à 256"));
		renderSize = int(n.Value());
		m_previewVersion = ~0ull;
		Visit("texture.render_size", FeatureStatus::Ok, a[1]);
		return CommandResult::Ok(String::Format("rendu %d px", renderSize));
	});
	console.Register("studio.texture.atlas", "studio.texture.atlas [tuile max] — reconstruit l'atlas", [this](const auto &a) {
		const Option<std::vector<int>> v = Ints(a, 1, 2);
		Session *session = m_shell.Game();
		if (v.IsSome())
			return RebuildAtlas(v.Value()[0], v.Value()[1]);
		if (!session || !session->Ready())
			return CommandResult::Error(String("aucune partie en cours"));
		const WorldRenderer::AtlasStats &s = session->Renderer().AtlasInfo();
		return RebuildAtlas(s.tileSize, s.maxTileSize);
	});
	console.Register("studio.texture.variant.add", "studio.texture.variant.add copy|noise", [this](const auto &a) {
		return AddVariant(a.size() > 1 ? a[1] : String("copy"));
	});
	console.Register("studio.texture.variant.remove", "studio.texture.variant.remove <index> — supprime son fichier (confirmation)",
					 [this, need](const auto &a) {
						 if (auto e = need(a, 2, "studio.texture.variant.remove <index>"); e.IsSome())
							 return e.Value();
						 const Option<double> n = Number(a[1]);
						 return n.IsSome() && n.Value() >= 0.0 ? RemoveVariant(size_t(n.Value())) : CommandResult::Error(String("index invalide"));
					 });
	console.Register("studio.texture.view", "studio.texture.view layers|preview|variants|atlas|files — onglet du panneau", [this, need](const auto &a) {
		if (auto e = need(a, 2, "studio.texture.view <onglet>"); e.IsSome())
			return e.Value();
		static const char *TABS[] = {"layers", "preview", "variants", "atlas", "files"};
		for (int i = 0; i < 5; ++i)
			if (a[1] == TABS[i]) {
				m_tabs.SetSelected(i, true);
				return CommandResult::Ok(a[1]);
			}
		return CommandResult::Error(String("layers, preview, variants, atlas ou files"));
	});
	console.Register("studio.texture.info", "studio.texture.info — taille, calques, palette, variantes", [this](const auto &) {
		if (!m_doc.IsSome())
			return CommandResult::Error(String("aucune texture ouverte"));
		const TextureDocument &d = m_doc.Value();
		String text = String::Format("%s : %d × %d, %zu calque(s), %zu couleur(s) récente(s), %zu variante(s)", m_textureId.CStr(), d.Width(),
									 d.Height(), d.Layers().size(), d.Palette().size(), Variants().size());
		for (const PaintLayer &l : d.Layers())
			text += String::Format("\n  %s %s %.2f%s", l.name.CStr(), PaintBlendId(l.blend), double(l.opacity), l.visible ? "" : " (masqué)");
		return CommandResult::Ok(text);
	});
}

} // namespace voxelia::studio
