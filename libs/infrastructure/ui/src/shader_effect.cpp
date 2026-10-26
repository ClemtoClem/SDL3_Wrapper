// Définitions de ui/shader_effect.hpp
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

#include <render3d/canvas.hpp>
#include <render3d/offscreen.hpp>
#include <render3d/shader_compiler.hpp>
#include <sdl3/render.hpp>

#include <ui/shader_effect.hpp>
#include <ui/systems.hpp>

namespace ui {

namespace detail {

// Triangle plein écran sans vertex buffer (uv dans [0,1] sur la partie
// visible). Conventions du dépôt : sampler fragment = set 2, uniforms = set 3.
const char *const FULLSCREEN_TRIANGLE_VERT = R"(#version 450

layout(location = 0) out vec2 outUV;

void main() {
	vec2 pos = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
	// NDC : y vers le haut ; texture : v vers le bas.
	outUV = vec2(pos.x, 1.0 - pos.y);
	gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char *const TINT_SHIFT_FRAG = R"(#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D uSource;

layout(set = 3, binding = 0) uniform EffectUBO {
	vec4 uColor;
	float uParam;
	float uPad0;
	float uPad1;
	float uPad2;
};

void main() {
	vec4 src = texture(uSource, inUV);
	vec3 tinted = mix(src.rgb, src.rgb * uColor.rgb, uParam);
	outColor = vec4(tinted, src.a);
}
)";

const char *const GLOW_FRAG = R"(#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D uSource;

layout(set = 3, binding = 0) uniform EffectUBO {
	vec4 uColor;
	float uParam;
	float uPad0;
	float uPad1;
	float uPad2;
};

void main() {
	vec4 src = texture(uSource, inUV);
	float dist = distance(inUV, vec2(0.5, 0.5));
	float t = smoothstep(uParam, 0.5, dist);
	vec3 glowed = mix(src.rgb, uColor.rgb, t);
	outColor = vec4(glowed, src.a);
}
)";

} // namespace detail

void SetShaderEffect(Widget widget, Option<ShaderEffect> effect) {
	if (!widget.Valid())
		return;
	if (effect.IsSome())
		widget.Set(effect.Value());
	else
		widget.Remove<ShaderEffect>();
}

namespace {

constexpr sdl3::GpuTextureFormat EFFECT_COLOR_FORMAT = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

/// std140 : vec4 puis 4 flottants (32 octets).
struct EffectUBO {
	float color[4];
	float param;
	float pad0 = 0.f, pad1 = 0.f, pad2 = 0.f;
};

/// État GPU d'un widget à effet.
struct EffectTargets {
	Option<sdl3::Texture> capture = NONE; ///< cible du renderer (sous-arbre)
	uint32_t captureW = 0, captureH = 0;
	std::vector<uint8_t> readback;
	Option<sdl3::GpuTexture> output = NONE; ///< sortie du pipeline
	uint32_t outputW = 0, outputH = 0;
	std::vector<uint8_t> download;
	Option<sdl3::Texture> display = NONE; ///< dernière image valide
	uint32_t displayW = 0, displayH = 0;
	uint64_t frame = 0; ///< image de la dernière réussite
};

/// État partagé de l'extension (pipelines, sampler, cibles par entité).
struct EffectGpu {
	Option<sdl3::GpuGraphicsPipeline> tint = NONE;
	Option<sdl3::GpuGraphicsPipeline> glow = NONE;
	Option<sdl3::GpuSampler> sampler = NONE;
	bool failedPipelines = false;
	std::unordered_map<ecs::Entity, EffectTargets> targets;
	uint64_t frame = 0;

	sdl3::GpuSampler *Sampler(sdl3::GpuDevice &device) {
		if (sampler.IsSome())
			return &sampler.Value();
		// NEAREST : source et destination ont la même taille.
		sdl3::GpuSamplerCreateInfo info{};
		info.min_filter = sdl3::gpu_filter::NEAREST;
		info.mag_filter = sdl3::gpu_filter::NEAREST;
		info.mipmap_mode = sdl3::gpu_sampler_mipmap_mode::NEAREST;
		info.address_mode_u = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
		info.address_mode_v = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
		info.address_mode_w = sdl3::gpu_sampler_address_mode::CLAMP_TO_EDGE;
		auto result = device.CreateSampler(info);
		if (!result)
			return nullptr;
		sampler = Some(std::move(result.Value()));
		return &sampler.Value();
	}

	sdl3::GpuGraphicsPipeline *Pipeline(sdl3::GpuDevice &device, ShaderEffectKind kind) {
		Option<sdl3::GpuGraphicsPipeline> &slot = kind == ShaderEffectKind::GLOW ? glow : tint;
		if (slot.IsSome())
			return &slot.Value();
		if (failedPipelines)
			return nullptr;
		const char *frag = kind == ShaderEffectKind::GLOW ? detail::GLOW_FRAG : detail::TINT_SHIFT_FRAG;
		const char *name = kind == ShaderEffectKind::GLOW ? "ui_effect_glow" : "ui_effect_tint";
		auto vs = render3d::CompileShader(device, StringView(detail::FULLSCREEN_TRIANGLE_VERT),
										  sdl3::gpu_shader_stage::VERTEX, StringView("ui_effect.vert"), 0, 0);
		auto fs = render3d::CompileShader(device, StringView(frag), sdl3::gpu_shader_stage::FRAGMENT, StringView(name), 1,
										  1);
		if (!vs || !fs) {
			failedPipelines = true; // compilateur absent : ne pas réessayer à chaque image
			return nullptr;
		}
		sdl3::GpuGraphicsPipelineCreateInfo info{};
		info.vertex_shader = vs.Value().Get();
		info.fragment_shader = fs.Value().Get();
		info.primitive_type = sdl3::gpu_primitive_type::TRIANGLE_LIST;
		info.rasterizer_state.fill_mode = sdl3::gpu_fill_mode::FILL;
		info.rasterizer_state.cull_mode = sdl3::gpu_cull_mode::NONE;
		info.rasterizer_state.front_face = sdl3::gpu_front_face::CLOCKWISE;
		info.multisample_state.sample_count = sdl3::gpu_sample_count::SAMPLE1;
		sdl3::GpuColorTargetDescription colorDesc{};
		colorDesc.format = EFFECT_COLOR_FORMAT;
		info.target_info.color_target_descriptions = &colorDesc;
		info.target_info.num_color_targets = 1;
		auto pipeline = device.CreateGraphicsPipeline(info);
		if (!pipeline) {
			failedPipelines = true;
			return nullptr;
		}
		slot = Some(std::move(pipeline.Value()));
		return &slot.Value();
	}

	static bool EnsureOutput(sdl3::GpuDevice &device, EffectTargets &t, uint32_t w, uint32_t h) {
		if (t.output.IsSome() && t.outputW == w && t.outputH == h)
			return true;
		sdl3::GpuTextureCreateInfo info{};
		info.type = sdl3::gpu_texture_type::TEXTURE2_D;
		info.format = EFFECT_COLOR_FORMAT;
		info.usage = sdl3::gpu_texture_usage::COLOR_TARGET | sdl3::gpu_texture_usage::SAMPLER;
		info.width = w;
		info.height = h;
		info.layer_count_or_depth = 1;
		info.num_levels = 1;
		info.sample_count = sdl3::gpu_sample_count::SAMPLE1;
		auto result = device.CreateTexture(info);
		if (!result)
			return false;
		t.output = Some(std::move(result.Value()));
		t.outputW = w;
		t.outputH = h;
		return true;
	}

	/// (a) capture du sous-arbre dans `t.readback` ([R,G,B,A] compacts).
	static bool Capture(ecs::ArchetypeRegistry &world, Context &ctx, ecs::Entity e, const sdl3::FRect &box,
						EffectTargets &t, uint32_t w, uint32_t h) {
		sdl3::Renderer &ren = *ctx.renderer;
		if (t.capture.IsNone() || t.captureW != w || t.captureH != h) {
			auto created = ren.CreateTexture(sdl3::PixelFormat::RGBA8888, sdl3::TextureAccess::TARGET, int(w), int(h));
			if (!created)
				return false;
			t.capture = Some(std::move(created.Value()));
			t.captureW = w;
			t.captureH = h;
		}
		if (!ren.SetTarget(t.capture.Value()))
			return false;
		ren.SetDrawColor(sdl3::FColor{0.f, 0.f, 0.f, 0.f});
		ren.Clear();
		// Le dessin est en coordonnées de fenêtre : un viewport décalé ramène
		// le coin du widget en (0,0) de la cible.
		const int ox = int(box.x), oy = int(box.y);
		(void)ren.SetViewport(sdl3::Rect{-ox, -oy, ox + int(w) + 1, oy + int(h) + 1});
		ctx.painter.ResetClip(box);
		ctx.painter.SetOpacity(1.f);
		RenderSystem::DrawSubtree(world, ctx, e);
		(void)ren.ClearViewport();
		auto read = ren.ReadPixels();
		(void)ren.ResetTarget();
		if (!read)
			return false;
		// RGBA32 : ordre mémoire littéral [R,G,B,A] (RGBA8888 est empaqueté).
		auto converted = read.Value().Convert(sdl3::PixelFormat::RGBA32);
		if (!converted)
			return false;
		SDL_Surface *surf = converted.Value().Get();
		if (!surf || uint32_t(surf->w) != w || uint32_t(surf->h) != h)
			return false;
		t.readback.resize(size_t(w) * h * 4);
		const auto *src = static_cast<const uint8_t *>(surf->pixels);
		for (uint32_t row = 0; row < h; ++row)
			std::memcpy(t.readback.data() + size_t(row) * w * 4, src + size_t(row) * size_t(surf->pitch), size_t(w) * 4);
		return true;
	}

	/// (b)–(d) : upload, pipeline, download, texture affichée.
	bool Process(Context &ctx, const ShaderEffect &fx, EffectTargets &t, uint32_t w, uint32_t h) {
		sdl3::GpuDevice &device = ctx.canvas3d->Device();
		auto uploaded = render3d::UploadSurfaceToGpuTexture(device, t.readback.data(), w, h, EFFECT_COLOR_FORMAT);
		if (!uploaded)
			return false;
		sdl3::GpuTexture source = std::move(uploaded.Value());
		if (!EnsureOutput(device, t, w, h))
			return false;
		sdl3::GpuGraphicsPipeline *pipeline = Pipeline(device, fx.kind);
		sdl3::GpuSampler *samp = Sampler(device);
		if (!pipeline || !samp)
			return false;
		const EffectUBO ubo{{fx.color.r, fx.color.g, fx.color.b, fx.color.a}, fx.param};

		auto cmd = device.AcquireCommandBuffer();
		{
			sdl3::GpuColorTargetInfo colorTarget{};
			colorTarget.texture = t.output.Value().Get();
			colorTarget.clear_color = sdl3::FColor{0.f, 0.f, 0.f, 0.f};
			colorTarget.load_op = sdl3::gpu_load_op::CLEAR;
			colorTarget.store_op = sdl3::gpu_store_op::STORE;
			sdl3::GpuRenderPass pass = cmd.BeginRenderPass(colorTarget);
			pass.SetViewport(sdl3::GpuViewport{0.f, 0.f, float(w), float(h), 0.f, 1.f});
			pass.BindPipeline(*pipeline);
			sdl3::GpuTextureSamplerBinding binding{};
			binding.texture = source.Get();
			binding.sampler = samp->Get();
			pass.BindFragmentSamplers(0, {&binding, 1});
			cmd.PushFragmentUniformData(0, ubo);
			pass.DrawPrimitives(3, 1, 0, 0);
		}
		if (!cmd.Submit() || !device.WaitIdle())
			return false;

		const uint32_t byteSize = w * h * sdl3::GpuTexelBlockSize(EFFECT_COLOR_FORMAT);
		auto buffer = device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::DOWNLOAD, byteSize);
		if (!buffer)
			return false;
		sdl3::GpuTransferBuffer download = std::move(buffer.Value());
		auto copyCmd = device.AcquireCommandBuffer();
		{
			sdl3::GpuCopyPass copy = copyCmd.BeginCopyPass();
			sdl3::GpuTextureRegion src{t.output.Value().Get(), 0, 0, 0, 0, 0, w, h, 1};
			sdl3::GpuTextureTransferInfo dst{download.Get(), 0, w, h};
			copy.DownloadFromTexture(src, dst);
		}
		if (!copyCmd.Submit() || !device.WaitIdle())
			return false;
		auto mapped = download.Map(false);
		if (!mapped)
			return false;
		t.download.resize(byteSize);
		std::memcpy(t.download.data(), mapped.GetData(), byteSize);

		if (t.display.IsNone() || t.displayW != w || t.displayH != h) {
			auto created = sdl3::Texture::Create(*ctx.renderer, int(w), int(h), sdl3::PixelFormat::RGBA32);
			if (!created)
				return false;
			t.display = Some(std::move(created.Value()));
			t.displayW = w;
			t.displayH = h;
		}
		(void)t.display.Value().Update(t.download.data(), int(w) * 4);
		return true;
	}

	void Prepare(ecs::ArchetypeRegistry &world, Context &ctx) {
		++frame;
		for (auto it = targets.begin(); it != targets.end();)
			it = world.IsAlive(it->first) && world.HasComponent<ShaderEffect>(it->first) ? std::next(it)
																						: targets.erase(it);
		if (!ctx.canvas3d || !ctx.renderer || !ctx.painter.CanDraw())
			return;
		for (ecs::Entity e : world.EntitiesWith<ShaderEffect>()) {
			auto fx = world.GetComponent<ShaderEffect>(e);
			auto rect = world.GetComponent<Rect>(e);
			if (fx.IsNone() || rect.IsNone() || !fx.Unwrap()->enabled || !rect.Unwrap()->visible)
				continue;
			const ShaderEffect effect = *fx.Unwrap();
			const sdl3::FRect box = rect.Unwrap()->box;
			const uint32_t w = uint32_t(math::Max(0.f, box.w)), h = uint32_t(math::Max(0.f, box.h));
			if (w == 0 || h == 0)
				continue;
			EffectTargets &t = targets[e];
			if (Capture(world, ctx, e, box, t, w, h) && Process(ctx, effect, t, w, h))
				t.frame = frame;
		}
	}

	bool Draw(ecs::ArchetypeRegistry &world, Context &ctx, ecs::Entity e, const Rect &rect) {
		auto fx = world.GetComponent<ShaderEffect>(e);
		if (fx.IsNone() || !fx.Unwrap()->enabled)
			return false;
		auto it = targets.find(e);
		if (it == targets.end() || it->second.display.IsNone())
			return false; // pas encore d'image valide : dessin normal
		const EffectTargets &t = it->second;
		// Taille changée sans nouvelle image : dessin normal plutôt qu'étiré.
		if (t.displayW != uint32_t(rect.box.w) || t.displayH != uint32_t(rect.box.h))
			return false;
		ctx.painter.Image(t.display.Value(),
						  sdl3::FRect{rect.box.x, rect.box.y, float(t.displayW), float(t.displayH)}, ObjectFit::Fill);
		return true;
	}
};

} // namespace

void RegisterShaderEffectExtension(Context &ctx) {
	auto gpu = std::make_shared<EffectGpu>();
	Extension ext;
	ext.name = "shader-effect";
	ext.prepare = [gpu](ecs::ArchetypeRegistry &world, Context &c) { gpu->Prepare(world, c); };
	ext.drawSubtree = [gpu](ecs::ArchetypeRegistry &world, Context &c, ecs::Entity e, const Rect &r) {
		return gpu->Draw(world, c, e, r);
	};
	RegisterExtension(ctx, std::move(ext));
}

} // namespace ui
