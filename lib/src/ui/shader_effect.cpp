// Définitions de ui/shader_effect.hpp
#include "ui/shader_effect.hpp"

namespace ui {

// ── ShaderEffectSystem ───────────────────────────────────────────────────────

void ShaderEffectSystem::Update(ecs::ArchetypeRegistry &world, render3d::Canvas &canvas, IUiRenderBackend &ren,
		RenderSystem &renderSystem) {
	sdl3::Renderer *nativeRenderer = ren.NativeRenderer();
	if (!nativeRenderer)
		return; // backend sans sdl3::Renderer réel : rien à faire ce frame (cf. Viewport3DSystem)

	std::vector<ecs::Entity> entities = world.EntitiesWith<UiShaderEffect>();
	for (ecs::Entity e : entities)
		UpdateOne(world, canvas, ren, renderSystem, *nativeRenderer, e);
}

// ── ShaderEffectSystem::RestoreGuard ─────────────────────────────────────────

ShaderEffectSystem::RestoreGuard::~RestoreGuard() {
	if (!committed && saved.IsSome())
		rs.effectTextures.insert_or_assign(e, std::move(saved.Value()));
}

// ── ShaderEffectSystem ───────────────────────────────────────────────────────

Option<Ref<sdl3::GpuSampler>> ShaderEffectSystem::GetOrBuildSampler(sdl3::GpuDevice &device) {
	if (m_sampler.IsSome())
		return Some(MakeRef(m_sampler.Value()));
	// NEAREST (pas LINEAR) : le sous-arbre capturé est déjà à la
	// résolution exacte du widget (source == destination en taille), un
	// filtrage LINEAR ne ferait qu'introduire du flou d'interpolation
	// entre texels voisins — indésirable ici puisque les tests vérifient
	// des valeurs EXACTES par pixel (cf. tests/ui_shader_effects_smoke_
	// test.cpp).
	sdl3::GpuSamplerCreateInfo info{};
	info.min_filter = sdl3::gpu_filter::NEAREST;
	info.mag_filter = sdl3::gpu_filter::NEAREST;
	info.mipmap_mode = sdl3::gpu_sampler_mipmap_mode::NEAREST;
	info.address_mode_u = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
	info.address_mode_v = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
	info.address_mode_w = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
	auto result = device.CreateSampler(info);
	if (!result)
		return NONE;
	m_sampler = Some(std::move(result.Value()));
	return Some(MakeRef(m_sampler.Value()));
}

Option<Ref<sdl3::GpuGraphicsPipeline>> ShaderEffectSystem::GetOrBuildPipeline(sdl3::GpuDevice &device,
		UiShaderEffectKind kind) {
	Option<sdl3::GpuGraphicsPipeline> &slot = (kind == UiShaderEffectKind::GLOW) ? m_glowPipeline : m_tintPipeline;
	if (slot.IsSome())
		return Some(MakeRef(slot.Value()));

	const char *fragSource = (kind == UiShaderEffectKind::GLOW) ? GLOW_FRAG : TINT_SHIFT_FRAG;
	const char *debugName = (kind == UiShaderEffectKind::GLOW) ? "shader_effect_glow" : "shader_effect_tint";

	// Vertex : aucun sampler/uniform (le triangle plein-écran est généré
	// depuis gl_VertexIndex seul, cf. FULLSCREEN_TRIANGLE_VERT). Fragment :
	// 1 sampler (source) + 1 uniform buffer (EffectUBO).
	auto vs = render3d::CompileShader(device, StringView(FULLSCREEN_TRIANGLE_VERT), sdl3::gpu_shader_stage::VERTEX,
									  StringView("shader_effect.vert"), 0, 0);
	if (!vs)
		return NONE;
	auto fs = render3d::CompileShader(device, StringView(fragSource), sdl3::gpu_shader_stage::FRAGMENT,
									  StringView(debugName), 1, 1);
	if (!fs)
		return NONE;

	sdl3::GpuGraphicsPipelineCreateInfo info{};
	info.vertex_shader = vs.Value().Get();
	info.fragment_shader = fs.Value().Get();
	// vertex_input_state reste zero-initialisé (aucun vertex buffer, cf.
	// ci-dessus) — pas de depth (post-traitement 2D, pas de rendu 3D).
	info.primitive_type = sdl3::gpu_primitive_type::TRIANGLE_LIST;
	info.rasterizer_state.fill_mode = sdl3::gpu_fill_mode::FILL;
	info.rasterizer_state.cull_mode = sdl3::gpu_cull_mode::NONE; // triangle unique ad hoc, pas un maillage authored
	info.rasterizer_state.front_face = sdl3::gpu_front_face::CLOCKWISE;
	info.multisample_state.sample_count = sdl3::gpu_sample_count::SAMPLE1;

	sdl3::GpuColorTargetDescription colorDesc{};
	colorDesc.format = SHADER_EFFECT_COLOR_FORMAT;
	info.target_info.color_target_descriptions = &colorDesc;
	info.target_info.num_color_targets = 1;

	auto pipelineResult = device.CreateGraphicsPipeline(info);
	if (!pipelineResult)
		return NONE;
	slot = Some(std::move(pipelineResult.Value()));
	return Some(MakeRef(slot.Value()));
}

bool ShaderEffectSystem::EnsureEffectColorTexture(sdl3::GpuDevice &device, WidgetState &state, uint32_t w, uint32_t h) {
	if (state.effectColorTexture.IsSome() && state.effectWidth == w && state.effectHeight == h)
		return true;
	sdl3::GpuTextureCreateInfo info{};
	info.type = sdl3::gpu_texture_type::TEXTURE2_D;
	info.format = SHADER_EFFECT_COLOR_FORMAT;
	info.usage = sdl3::gpu_texture_usage::COLOR_TARGET | sdl3::gpu_texture_usage::SAMPLER;
	info.width = w;
	info.height = h;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = sdl3::gpu_sample_count::SAMPLE1;
	auto result = device.CreateTexture(info);
	if (!result)
		return false;
	state.effectColorTexture = Some(std::move(result.Value()));
	state.effectWidth = w;
	state.effectHeight = h;
	return true;
}

void ShaderEffectSystem::UpdateOne(ecs::ArchetypeRegistry &world, render3d::Canvas &canvas, IUiRenderBackend &ren,
		RenderSystem &renderSystem, sdl3::Renderer &nativeRenderer, ecs::Entity e) {
	auto fx = world.GetComponent<UiShaderEffect>(e);
	if (fx.IsNone())
		return; // parti entre la collecte et ici (despawn applicatif) : ignore-le simplement
	auto computed = world.GetComponent<UiComputed>(e);
	if (computed.IsNone())
		return; // pas encore passé par LayoutSystem une seule fois

	uint32_t w = uint32_t(sdl3::Max(0.f, computed.Unwrap()->screen.w));
	uint32_t h = uint32_t(sdl3::Max(0.f, computed.Unwrap()->screen.h));
	if (w == 0 || h == 0)
		return; // widget replié/masqué ce frame : pas de taille à rendre

	// Cf. en-tête du fichier, "piège anti-boucle-de-rétroaction" : retire
	// l'entrée existante AVANT de capturer, la restaure au pire (RAII) si
	// ce widget échoue plus loin ce frame.
	RestoreGuard guard{renderSystem, e, NONE, false};
	if (auto it = renderSystem.effectTextures.find(e); it != renderSystem.effectTextures.end()) {
		guard.saved = Some(std::move(it->second));
		renderSystem.effectTextures.erase(it);
	}

	WidgetState &state = m_state[e];

	// ── (a) Capture du sous-arbre dans une texture cible dédiée ────────
	bool needsSourceCreate = state.sourceTarget.IsNone() || state.sourceWidth != w || state.sourceHeight != h;
	if (needsSourceCreate) {
		// Format de CRÉATION de cette texture cible : sans importance ici
		// (simple canevas de dessin 2D via SetDrawColor/FillRect/DrawTree,
		// jamais nourri d'octets bruts directement) — RGBA8888 par défaut
		// suffit ; ReadPixels ci-dessous lit le format NATIF réel puis
		// Convert() explicitement vers RGBA32 (cf. sa propre note).
		auto created =
			nativeRenderer.CreateTexture(sdl3::PixelFormat::RGBA8888, sdl3::TextureAccess::TARGET, int(w), int(h));
		if (!created)
			return; // best-effort : réessaiera au prochain frame
		state.sourceTarget = Some(std::move(created.Value()));
		state.sourceWidth = w;
		state.sourceHeight = h;
	}
	if (!nativeRenderer.SetTarget(*state.sourceTarget))
		return;
	nativeRenderer.SetDrawColor(sdl3::FColor{0.f, 0.f, 0.f, 0.f});
	nativeRenderer.Clear();

	// Décalage de repère (cf. en-tête du fichier) : DrawTree dessine en
	// coordonnées ABSOLUES de fenêtre (UiComputed.screen/clip), pas
	// relatives à `e` — sans ce viewport décalé, le contenu se
	// dessinerait hors des bornes de notre petite texture w×h dès que le
	// widget n'est pas au coin (0,0) de la fenêtre.
	int ox = int(computed.Unwrap()->screen.x);
	int oy = int(computed.Unwrap()->screen.y);
	nativeRenderer.SetViewport(sdl3::Rect{-ox, -oy, ox + int(w) + 1, oy + int(h) + 1});
	renderSystem.DrawTree(world, ren, e, /*overlayEntry=*/false);
	ren.ClearClipRect(); // hygiène (même geste que RenderSystem::Run après son propre parcours d'arbre)
	nativeRenderer.ClearViewport(); // remis à plat AVANT ReadPixels (cf. doc Renderer::ReadPixels, render.hpp)

	auto readResult = nativeRenderer.ReadPixels();
	nativeRenderer.ResetTarget(); // dans tous les cas, avant tout retour anticipé qui suit
	if (!readResult)
		return;
	// RGBA32, PAS RGBA8888 (piège réel, cf. la note détaillée sur
	// Renderer::ReadPixels dans render.hpp) : RGBA8888 est un format
	// PACKED dont l'ordre en mémoire sur little-endian est l'INVERSE de
	// son nom — un octet-par-octet naïf en RGBA8888 lirait donc des
	// couleurs corrompues (canaux permutés) une fois uploadées côté GPU
	// ci-dessous. RGBA32 seul garantit l'ordre littéral [R,G,B,A].
	auto converted = readResult.Value().Convert(sdl3::PixelFormat::RGBA32);
	if (!converted)
		return;
	SDL_Surface *surf = converted.Value().Get();
	if (!surf || uint32_t(surf->w) != w || uint32_t(surf->h) != h)
		return; // ReadPixels peut renvoyer moins que demandé (clip au viewport courant) : abandonne plutôt que lire hors-buffer

	// Compaction tightly-packed (pitch de Convert() non garanti ==
	// largeur*4 par l'API SDL, cf. doc Renderer::ReadPixels) — même
	// discipline "ne jamais deviner le layout" que le reste de ce dépôt.
	state.readbackPixels.resize(size_t(w) * h * 4);
	const auto *srcPixels = static_cast<const uint8_t *>(surf->pixels);
	for (uint32_t row = 0; row < h; ++row)
		std::memcpy(state.readbackPixels.data() + size_t(row) * w * 4, srcPixels + size_t(row) * size_t(surf->pitch),
					size_t(w) * 4);

	// ── (b) upload CPU -> GPU (M21, offscreen.hpp) ──────────────────────
	auto uploaded = render3d::UploadSurfaceToGpuTexture(canvas.Device(), state.readbackPixels.data(), w, h,
														 SHADER_EFFECT_COLOR_FORMAT);
	if (!uploaded)
		return;
	sdl3::GpuTexture sourceGpuTexture = std::move(uploaded.Value());

	// ── (c) pipeline d'effet dédié ───────────────────────────────────
	if (!EnsureEffectColorTexture(canvas.Device(), state, w, h))
		return;
	auto pipeline = GetOrBuildPipeline(canvas.Device(), fx.Unwrap()->kind);
	if (!pipeline)
		return;
	auto sampler = GetOrBuildSampler(canvas.Device());
	if (!sampler)
		return;

	sdl3::FColor c = fx.Unwrap()->color;
	ShaderEffectUBO ubo{{c.r, c.g, c.b, c.a}, fx.Unwrap()->param};

	auto cmd = canvas.Device().AcquireCommandBuffer();
	{
		sdl3::GpuColorTargetInfo colorTarget{};
		colorTarget.texture = state.effectColorTexture.Value().Get();
		colorTarget.clear_color = sdl3::FColor{0.f, 0.f, 0.f, 0.f};
		colorTarget.load_op = sdl3::gpu_load_op::CLEAR;
		colorTarget.store_op = sdl3::gpu_store_op::STORE;

		sdl3::GpuRenderPass pass = cmd.BeginRenderPass(colorTarget);
		sdl3::GpuViewport gpuViewport{0.f, 0.f, float(w), float(h), 0.f, 1.f};
		pass.SetViewport(gpuViewport);
		pass.BindPipeline(pipeline.Value());
		sdl3::GpuTextureSamplerBinding samplerBinding{};
		samplerBinding.texture = sourceGpuTexture.Get();
		samplerBinding.sampler = sampler.Value()->Get();
		pass.BindFragmentSamplers(0, {&samplerBinding, 1});
		cmd.PushFragmentUniformData(0, ubo);
		pass.DrawPrimitives(3, 1, 0, 0);
	}
	if (!cmd.Submit())
		return;
	if (!canvas.Device().WaitIdle())
		return;

	// ── (d) download GPU -> CPU, ré-upload en sdl3::Texture affichée ───
	uint32_t bytesPerTexel = sdl3::GpuTexelBlockSize(SHADER_EFFECT_COLOR_FORMAT);
	uint32_t byteSize = w * h * bytesPerTexel;
	auto downloadResult = canvas.Device().CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::DOWNLOAD, byteSize);
	if (!downloadResult)
		return;
	sdl3::GpuTransferBuffer downloadBuffer = std::move(downloadResult.Value());
	auto downloadCmd = canvas.Device().AcquireCommandBuffer();
	{
		sdl3::GpuCopyPass copyPass = downloadCmd.BeginCopyPass();
		sdl3::GpuTextureRegion src{state.effectColorTexture.Value().Get(), 0, 0, 0, 0, 0, w, h, 1};
		sdl3::GpuTextureTransferInfo dst{downloadBuffer.Get(), 0, w, h};
		copyPass.DownloadFromTexture(src, dst);
	}
	if (!downloadCmd.Submit())
		return;
	if (!canvas.Device().WaitIdle())
		return;
	auto mapped = downloadBuffer.Map(false);
	if (!mapped)
		return;
	state.downloadPixels.resize(byteSize);
	std::memcpy(state.downloadPixels.data(), mapped.GetData(), byteSize);

	bool needsDisplayCreate = state.displayWidth != w || state.displayHeight != h ||
							  renderSystem.effectTextures.find(e) == renderSystem.effectTextures.end();
	if (needsDisplayCreate) {
		// RGBA32 EXPLICITE (pas le défaut RGBA8888 de Texture::Create,
		// cf. piège documenté sur Renderer::ReadPixels dans render.hpp) :
		// state.downloadPixels ci-dessous est en ordre littéral
		// [R,G,B,A] (sortie GPU R8G8B8A8_UNORM) — une texture taguée
		// RGBA8888 nourrie de ces octets s'afficherait totalement
		// transparente (premier octet, R, relu comme alpha), confirmé
		// empiriquement pendant ce jalon.
		auto created = sdl3::Texture::Create(nativeRenderer, int(w), int(h), sdl3::PixelFormat::RGBA32);
		if (!created)
			return;
		renderSystem.effectTextures.insert_or_assign(e, std::move(created.Value()));
		state.displayWidth = w;
		state.displayHeight = h;
	}
	renderSystem.effectTextures.at(e).Update(state.downloadPixels.data(), int(w) * 4);
	guard.committed = true; // succès : la texture fraîche remplace définitivement l'ancienne (RestoreGuard n'agit plus)
}

} // namespace ui
