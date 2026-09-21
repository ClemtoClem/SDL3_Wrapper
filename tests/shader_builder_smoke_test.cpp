// Smoke test : render3d::ShaderBuilder (lib/render3d/shader_builder.hpp) —
// 3 configurations (Unlit+couleur, Phong+texture, PBR+2 lumières
// ponctuelles), chacune compilée et dessinée réellement sur un device GPU
// réel, plus une vérification exacte de pixel sur la config Unlit (la plus
// simple à recalculer à la main), même méthodologie que la vérification du
// shader Phong/du winding faite plus tôt sur canvas.hpp.
#define USE_TEST
#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include "render3d/canvas.hpp"
#include "render3d/shader_builder.hpp"
#include "render3d/texture_loader.hpp"
#include <cstring>

using namespace sdl3;
using namespace render3d;

namespace {

// Pipeline minimal partagé par les 3 sous-tests : device + fenêtre + une
// texture couleur/depth offscreen (évite toute dépendance à un compositeur
// pour présenter une swapchain — cf. leçon de la vérification Phong/winding).
struct OffscreenRig {
    Window window;
    GpuDevice device;
    GpuTexture colorTexture;
    GpuTexture depthTexture;

    [[nodiscard]] static Result<OffscreenRig, String> Create(uint32_t w, uint32_t h) {
        auto windowResult = Window::Create(String("shader_builder_smoke_test"), int(w), int(h), 0);
        if (!windowResult)
            return Err(String(windowResult.Error()));

        auto deviceResult =
            GpuDevice::Create(gpu_shader_format::SPIR_V | gpu_shader_format::MSL | gpu_shader_format::DXIL);
        if (!deviceResult)
            return Err(String(deviceResult.Error()));
        GpuDevice device = std::move(deviceResult.Value());
        if (!device.ClaimWindow(MakeRef(windowResult.Value())))
            return Err(String(GetError()));

        GpuTextureCreateInfo colorInfo{};
        colorInfo.type = gpu_texture_type::TEXTURE2_D;
        colorInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        colorInfo.usage = gpu_texture_usage::COLOR_TARGET | gpu_texture_usage::SAMPLER;
        colorInfo.width = w;
        colorInfo.height = h;
        colorInfo.layer_count_or_depth = 1;
        colorInfo.num_levels = 1;
        colorInfo.sample_count = gpu_sample_count::SAMPLE1;
        auto colorResult = device.CreateTexture(colorInfo);
        if (!colorResult)
            return Err(String(colorResult.Error()));

        GpuTextureCreateInfo depthInfo = colorInfo;
        depthInfo.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        depthInfo.usage = gpu_texture_usage::DEPTH_STENCIL_TARGET;
        auto depthResult = device.CreateTexture(depthInfo);
        if (!depthResult)
            return Err(String(depthResult.Error()));

        return Ok(OffscreenRig{std::move(windowResult.Value()), std::move(device), std::move(colorResult.Value()),
                               std::move(depthResult.Value())});
    }
};

// Construit un pipeline graphique minimal (triangle plein écran, pas
// d'index) à partir d'un ShaderProgram déjà compilé par ShaderBuilder.
[[nodiscard]] Result<GpuGraphicsPipeline, String> BuildTrianglePipeline(GpuDevice &device, ShaderProgram &program,
                                                                        GpuTextureFormat colorFormat,
                                                                        GpuTextureFormat depthFormat) {
    static GpuVertexBufferDescription vertexBufferDesc{0, sizeof(Vertex3D), gpu_vertex_input_rate::VERTEX, 0};
    static GpuVertexAttribute vertexAttributes[4] = {
        {0, 0, gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(Vertex3D, position))},
        {1, 0, gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(Vertex3D, normal))},
        {2, 0, gpu_vertex_element_format::FLOAT2, uint32_t(offsetof(Vertex3D, uv))},
        {3, 0, gpu_vertex_element_format::UBYTE4_NORM, uint32_t(offsetof(Vertex3D, color))},
    };
    GpuColorTargetDescription colorTargetDesc{};
    colorTargetDesc.format = colorFormat;

    GpuGraphicsPipelineCreateInfo info{};
    info.vertex_shader = program.Vertex().Get();
    info.fragment_shader = program.Fragment().Get();
    info.vertex_input_state.vertex_buffer_descriptions = &vertexBufferDesc;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = vertexAttributes;
    info.vertex_input_state.num_vertex_attributes = 4;
    info.primitive_type = gpu_primitive_type::TRIANGLE_LIST;
    info.rasterizer_state.fill_mode = gpu_fill_mode::FILL;
    info.rasterizer_state.cull_mode = gpu_cull_mode::NONE;
    info.rasterizer_state.front_face = gpu_front_face::CLOCKWISE;
    info.multisample_state.sample_count = gpu_sample_count::SAMPLE1;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = true;
    info.depth_stencil_state.compare_op = gpu_compare_op::LESS_OR_EQUAL;
    info.target_info.color_target_descriptions = &colorTargetDesc;
    info.target_info.num_color_targets = 1;
    info.target_info.has_depth_stencil_target = true;
    info.target_info.depth_stencil_format = depthFormat;

    auto result = device.CreateGraphicsPipeline(info);
    if (!result)
        return Err(String(result.Error()));
    return Ok(std::move(result.Value()));
}

} // namespace

TEST(ShaderBuilderSmokeTest, UnlitColorExactPixel) {
    auto rigResult = OffscreenRig::Create(32, 32);
    ASSERT_TRUE(rigResult.IsOk());
    OffscreenRig rig = std::move(rigResult.Value());

    auto programResult = ShaderBuilder().Color(FColor{0.2f, 0.6f, 0.8f, 1.f}).Lit(LightingModel::UNLIT).Build(rig.device);
    ASSERT_TRUE(programResult.IsOk());
    ShaderProgram program = std::move(programResult.Value());

    auto pipelineResult =
        BuildTrianglePipeline(rig.device, program, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_D32_FLOAT);
    ASSERT_TRUE(pipelineResult.IsOk());
    GpuGraphicsPipeline pipeline = std::move(pipelineResult.Value());

    Vertex3D triangle[3] = {
        {{-2.f, -2.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
        {{2.f, -2.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
        {{0.f, 2.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
    };
    auto vbResult = rig.device.CreateBuffer(gpu_buffer_usage::VERTEX, uint32_t(sizeof(triangle)));
    ASSERT_TRUE(vbResult.IsOk());
    GpuBuffer vertexBuffer = std::move(vbResult.Value());
    auto tbResult = rig.device.CreateTransferBuffer(gpu_transfer_buffer_usage::UPLOAD, uint32_t(sizeof(triangle)));
    ASSERT_TRUE(tbResult.IsOk());
    GpuTransferBuffer transfer = std::move(tbResult.Value());
    {
        auto mapped = transfer.Map(false);
        ASSERT_TRUE(bool(mapped));
        std::memcpy(mapped.GetData(), triangle, sizeof(triangle));
    }
    auto uploadCmd = rig.device.AcquireCommandBuffer();
    {
        GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
        GpuTransferBufferLocation src{transfer.Get(), 0};
        GpuBufferRegion dst{vertexBuffer.Get(), 0, uint32_t(sizeof(triangle))};
        copyPass.UploadToBuffer(src, dst, false);
    }
    ASSERT_TRUE(uploadCmd.Submit());
    ASSERT_TRUE(rig.device.WaitIdle());

    float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    struct PerObjectUBO {
        float model[16];
        float normalMatrix[16];
    };
    PerObjectUBO perObject{};
    std::memcpy(perObject.model, identity, sizeof(identity));
    std::memcpy(perObject.normalMatrix, identity, sizeof(identity));
    struct MaterialUBO {
        float baseColor[4];
        float emissive[4];
        float roughness, metallic, useTexture, padding0;
    };
    MaterialUBO material{{0.2f, 0.6f, 0.8f, 1.f}, {0, 0, 0, 0}, 0, 0, 0, 0};

    auto frameCmd = rig.device.AcquireCommandBuffer();
    GpuColorTargetInfo colorTarget{};
    colorTarget.texture = rig.colorTexture.Get();
    colorTarget.clear_color = SDL_FColor{0.f, 0.f, 0.f, 1.f};
    colorTarget.load_op = gpu_load_op::CLEAR;
    colorTarget.store_op = gpu_store_op::STORE;
    GpuDepthStencilTargetInfo depthTarget{};
    depthTarget.texture = rig.depthTexture.Get();
    depthTarget.clear_depth = 1.f;
    depthTarget.load_op = gpu_load_op::CLEAR;
    depthTarget.store_op = gpu_store_op::DONT_CARE;
    depthTarget.stencil_load_op = gpu_load_op::DONT_CARE;
    depthTarget.stencil_store_op = gpu_store_op::DONT_CARE;
    {
        GpuRenderPass pass = frameCmd.BeginRenderPass(colorTarget, &depthTarget);
        pass.BindPipeline(pipeline);
        GpuBufferBinding binding{vertexBuffer.Get(), 0};
        pass.BindVertexBuffer(0, binding);
        frameCmd.PushVertexUniformData(0, identity);
        frameCmd.PushVertexUniformData(1, perObject);
        frameCmd.PushFragmentUniformData(0, material);
        pass.DrawPrimitives(3);
    }
    ASSERT_TRUE(frameCmd.Submit());
    ASSERT_TRUE(rig.device.WaitIdle());

    auto downloadResult = rig.device.CreateTransferBuffer(gpu_transfer_buffer_usage::DOWNLOAD, 32 * 32 * 4);
    ASSERT_TRUE(downloadResult.IsOk());
    GpuTransferBuffer downloadBuffer = std::move(downloadResult.Value());
    auto downloadCmd = rig.device.AcquireCommandBuffer();
    {
        GpuCopyPass copyPass = downloadCmd.BeginCopyPass();
        GpuTextureRegion src{rig.colorTexture.Get(), 0, 0, 0, 0, 0, 32, 32, 1};
        GpuTextureTransferInfo dst{downloadBuffer.Get(), 0, 32, 32};
        copyPass.DownloadFromTexture(src, dst);
    }
    ASSERT_TRUE(downloadCmd.Submit());
    ASSERT_TRUE(rig.device.WaitIdle());

    auto mapped = downloadBuffer.Map(false);
    ASSERT_TRUE(bool(mapped));
    uint8_t *pixels = mapped.As<uint8_t>();
    int idx = (16 * 32 + 16) * 4; // centre du triangle
    // Unlit : sortie = uBaseColor * inColor (blanc) directement, sans éclairage.
    EXPECT_TRUE(std::abs(int(pixels[idx + 0]) - int(0.2f * 255)) <= 2);
    EXPECT_TRUE(std::abs(int(pixels[idx + 1]) - int(0.6f * 255)) <= 2);
    EXPECT_TRUE(std::abs(int(pixels[idx + 2]) - int(0.8f * 255)) <= 2);

    EXPECT_TRUE(GetError().IsEmpty());
}

TEST(ShaderBuilderSmokeTest, PhongWithTextureCompilesAndDraws) {
    auto rigResult = OffscreenRig::Create(32, 32);
    ASSERT_TRUE(rigResult.IsOk());
    OffscreenRig rig = std::move(rigResult.Value());

    auto textureResult = LoadTextureFromFile(rig.device, String("assets/textures/default_particle.png"));
    ASSERT_TRUE(textureResult.IsOk());
    GpuTexture texture = std::move(textureResult.Value());

    auto programResult = ShaderBuilder()
                             .Color(FColor{1.f, 1.f, 1.f, 1.f})
                             .Texture(MakeRef(texture))
                             .Lit(LightingModel::PHONG)
                             .Build(rig.device);
    ASSERT_TRUE(programResult.IsOk());
    ShaderProgram program = std::move(programResult.Value());

    auto pipelineResult =
        BuildTrianglePipeline(rig.device, program, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_D32_FLOAT);
    ASSERT_TRUE(pipelineResult.IsOk());
    GpuGraphicsPipeline pipeline = std::move(pipelineResult.Value());

    GpuSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = gpu_filter::LINEAR;
    samplerInfo.mag_filter = gpu_filter::LINEAR;
    samplerInfo.mipmap_mode = gpu_sampler_mipmap_mode::LINEAR;
    samplerInfo.address_mode_u = gpu_sampler_address_mode::REPEAT;
    samplerInfo.address_mode_v = gpu_sampler_address_mode::REPEAT;
    samplerInfo.address_mode_w = gpu_sampler_address_mode::REPEAT;
    auto samplerResult = rig.device.CreateSampler(samplerInfo);
    ASSERT_TRUE(samplerResult.IsOk());
    GpuSampler sampler = std::move(samplerResult.Value());

    Vertex3D triangle[3] = {
        {{-2.f, -2.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 1.f}, Color::WHITE()},
        {{2.f, -2.f, 0.f}, {0.f, 0.f, -1.f}, {1.f, 1.f}, Color::WHITE()},
        {{0.f, 2.f, 0.f}, {0.f, 0.f, -1.f}, {0.5f, 0.f}, Color::WHITE()},
    };
    auto vbResult = rig.device.CreateBuffer(gpu_buffer_usage::VERTEX, uint32_t(sizeof(triangle)));
    ASSERT_TRUE(vbResult.IsOk());
    GpuBuffer vertexBuffer = std::move(vbResult.Value());
    auto tbResult = rig.device.CreateTransferBuffer(gpu_transfer_buffer_usage::UPLOAD, uint32_t(sizeof(triangle)));
    ASSERT_TRUE(tbResult.IsOk());
    GpuTransferBuffer transfer = std::move(tbResult.Value());
    {
        auto mapped = transfer.Map(false);
        ASSERT_TRUE(bool(mapped));
        std::memcpy(mapped.GetData(), triangle, sizeof(triangle));
    }
    auto uploadCmd = rig.device.AcquireCommandBuffer();
    {
        GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
        GpuTransferBufferLocation src{transfer.Get(), 0};
        GpuBufferRegion dst{vertexBuffer.Get(), 0, uint32_t(sizeof(triangle))};
        copyPass.UploadToBuffer(src, dst, false);
    }
    ASSERT_TRUE(uploadCmd.Submit());
    ASSERT_TRUE(rig.device.WaitIdle());

    float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    struct PerObjectUBO {
        float model[16];
        float normalMatrix[16];
    };
    PerObjectUBO perObject{};
    std::memcpy(perObject.model, identity, sizeof(identity));
    std::memcpy(perObject.normalMatrix, identity, sizeof(identity));
    struct MaterialUBO {
        float baseColor[4];
        float emissive[4];
        float roughness, metallic, useTexture, padding0;
    };
    MaterialUBO material{{1.f, 1.f, 1.f, 1.f}, {0, 0, 0, 0}, 0.5f, 0.f, 1.f, 0.f};
    struct LightUBO {
        float lightDir[3];
        float pad1;
        float lightColor[4];
        float ambientColor[4];
        float cameraPos[3];
        float pad2;
    };
    LightUBO light{};
    light.lightDir[1] = -1.f;
    light.lightColor[0] = light.lightColor[1] = light.lightColor[2] = light.lightColor[3] = 1.f;
    light.ambientColor[0] = light.ambientColor[1] = light.ambientColor[2] = 0.3f;
    light.ambientColor[3] = 1.f;
    light.cameraPos[2] = -1.f;

    auto frameCmd = rig.device.AcquireCommandBuffer();
    GpuColorTargetInfo colorTarget{};
    colorTarget.texture = rig.colorTexture.Get();
    colorTarget.clear_color = SDL_FColor{0.f, 0.f, 0.f, 1.f};
    colorTarget.load_op = gpu_load_op::CLEAR;
    colorTarget.store_op = gpu_store_op::STORE;
    GpuDepthStencilTargetInfo depthTarget{};
    depthTarget.texture = rig.depthTexture.Get();
    depthTarget.clear_depth = 1.f;
    depthTarget.load_op = gpu_load_op::CLEAR;
    depthTarget.store_op = gpu_store_op::DONT_CARE;
    depthTarget.stencil_load_op = gpu_load_op::DONT_CARE;
    depthTarget.stencil_store_op = gpu_store_op::DONT_CARE;
    {
        GpuRenderPass pass = frameCmd.BeginRenderPass(colorTarget, &depthTarget);
        pass.BindPipeline(pipeline);
        GpuBufferBinding binding{vertexBuffer.Get(), 0};
        pass.BindVertexBuffer(0, binding);
        frameCmd.PushVertexUniformData(0, identity);
        frameCmd.PushVertexUniformData(1, perObject);
        frameCmd.PushFragmentUniformData(0, material);
        frameCmd.PushFragmentUniformData(1, light);
        GpuTextureSamplerBinding samplerBinding{texture.Get(), sampler.Get()};
        pass.BindFragmentSamplers(0, {&samplerBinding, 1});
        pass.DrawPrimitives(3);
    }
    EXPECT_TRUE(frameCmd.Submit());
    EXPECT_TRUE(rig.device.WaitIdle());
    EXPECT_TRUE(GetError().IsEmpty());
}

TEST(ShaderBuilderSmokeTest, PbrWithPointLightsCompilesAndDraws) {
    auto rigResult = OffscreenRig::Create(32, 32);
    ASSERT_TRUE(rigResult.IsOk());
    OffscreenRig rig = std::move(rigResult.Value());

    auto programResult = ShaderBuilder()
                             .Color(FColor{0.7f, 0.3f, 0.2f, 1.f})
                             .Lit(LightingModel::PBR)
                             .Metallic(0.8f)
                             .Roughness(0.3f)
                             .PointLights(2)
                             .Build(rig.device);
    ASSERT_TRUE(programResult.IsOk());
    ShaderProgram program = std::move(programResult.Value());
    EXPECT_EQ(ShaderBuilder().PointLights(2).FragmentUniformBufferCount(), uint32_t(3));

    auto pipelineResult =
        BuildTrianglePipeline(rig.device, program, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_D32_FLOAT);
    ASSERT_TRUE(pipelineResult.IsOk());
    GpuGraphicsPipeline pipeline = std::move(pipelineResult.Value());

    Vertex3D triangle[3] = {
        {{-2.f, -2.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
        {{2.f, -2.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
        {{0.f, 2.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
    };
    auto vbResult = rig.device.CreateBuffer(gpu_buffer_usage::VERTEX, uint32_t(sizeof(triangle)));
    ASSERT_TRUE(vbResult.IsOk());
    GpuBuffer vertexBuffer = std::move(vbResult.Value());
    auto tbResult = rig.device.CreateTransferBuffer(gpu_transfer_buffer_usage::UPLOAD, uint32_t(sizeof(triangle)));
    ASSERT_TRUE(tbResult.IsOk());
    GpuTransferBuffer transfer = std::move(tbResult.Value());
    {
        auto mapped = transfer.Map(false);
        ASSERT_TRUE(bool(mapped));
        std::memcpy(mapped.GetData(), triangle, sizeof(triangle));
    }
    auto uploadCmd = rig.device.AcquireCommandBuffer();
    {
        GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
        GpuTransferBufferLocation src{transfer.Get(), 0};
        GpuBufferRegion dst{vertexBuffer.Get(), 0, uint32_t(sizeof(triangle))};
        copyPass.UploadToBuffer(src, dst, false);
    }
    ASSERT_TRUE(uploadCmd.Submit());
    ASSERT_TRUE(rig.device.WaitIdle());

    float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    struct PerObjectUBO {
        float model[16];
        float normalMatrix[16];
    };
    PerObjectUBO perObject{};
    std::memcpy(perObject.model, identity, sizeof(identity));
    std::memcpy(perObject.normalMatrix, identity, sizeof(identity));
    struct MaterialUBO {
        float baseColor[4];
        float emissive[4];
        float roughness, metallic, useTexture, padding0;
    };
    MaterialUBO material{{0.7f, 0.3f, 0.2f, 1.f}, {0, 0, 0, 0}, 0.3f, 0.8f, 0.f, 0.f};
    struct LightUBO {
        float lightDir[3];
        float pad1;
        float lightColor[4];
        float ambientColor[4];
        float cameraPos[3];
        float pad2;
    };
    LightUBO light{};
    light.lightDir[1] = -1.f;
    light.cameraPos[2] = -1.f;
    struct PointLightGpu {
        float position[3], distanceVal;
        float color[3], decay;
    };
    PointLightGpu pointLights[2] = {
        {{-1.f, 0.f, -0.5f}, 0.f, {1.f, 0.f, 0.f}, 2.f},
        {{1.f, 0.f, -0.5f}, 0.f, {0.f, 0.f, 1.f}, 2.f},
    };

    auto frameCmd = rig.device.AcquireCommandBuffer();
    GpuColorTargetInfo colorTarget{};
    colorTarget.texture = rig.colorTexture.Get();
    colorTarget.clear_color = SDL_FColor{0.f, 0.f, 0.f, 1.f};
    colorTarget.load_op = gpu_load_op::CLEAR;
    colorTarget.store_op = gpu_store_op::STORE;
    GpuDepthStencilTargetInfo depthTarget{};
    depthTarget.texture = rig.depthTexture.Get();
    depthTarget.clear_depth = 1.f;
    depthTarget.load_op = gpu_load_op::CLEAR;
    depthTarget.store_op = gpu_store_op::DONT_CARE;
    depthTarget.stencil_load_op = gpu_load_op::DONT_CARE;
    depthTarget.stencil_store_op = gpu_store_op::DONT_CARE;
    {
        GpuRenderPass pass = frameCmd.BeginRenderPass(colorTarget, &depthTarget);
        pass.BindPipeline(pipeline);
        GpuBufferBinding binding{vertexBuffer.Get(), 0};
        pass.BindVertexBuffer(0, binding);
        frameCmd.PushVertexUniformData(0, identity);
        frameCmd.PushVertexUniformData(1, perObject);
        frameCmd.PushFragmentUniformData(0, material);
        frameCmd.PushFragmentUniformData(1, light);
        frameCmd.PushFragmentUniformData(2, pointLights);
        pass.DrawPrimitives(3);
    }
    EXPECT_TRUE(frameCmd.Submit());
    EXPECT_TRUE(rig.device.WaitIdle());
    EXPECT_TRUE(GetError().IsEmpty());
}

TEST(ShaderBuilderSmokeTest, PbrExactPixelAgainstHandComputedBrdf) {
    // Configuration choisie pour que N, L, V, H soient tous colinéaires
    // (0,0,-1) — dot(N,L)=dot(N,V)=dot(N,H)=1 — afin de pouvoir calculer la
    // BRDF Cook-Torrance à la main sans intégrer d'angle. albedo blanc,
    // metallic=0, roughness=0.5, ambiante nulle (isole la contribution
    // directe) :
    //   F0 = 0.04 ; D = a²/(π·a²²) = 5.09296 (a = roughness² = 0.25)
    //   G = 1 (NdotV=NdotL=1) ; F = F0 (cosTheta=1 -> terme de Fresnel nul)
    //   specular = D·G·F/4 = 0.05093 ; kD = (1-F0) = 0.96
    //   diffuse = kD·albedo/π = 0.30558
    //   direct = (diffuse + specular)·NdotL = 0.35651 -> octet ≈ round(0.35651*255) = 91
    auto rigResult = OffscreenRig::Create(32, 32);
    ASSERT_TRUE(rigResult.IsOk());
    OffscreenRig rig = std::move(rigResult.Value());

    auto programResult = ShaderBuilder()
                             .Color(FColor{1.f, 1.f, 1.f, 1.f})
                             .Lit(LightingModel::PBR)
                             .Metallic(0.f)
                             .Roughness(0.5f)
                             .Build(rig.device);
    ASSERT_TRUE(programResult.IsOk());
    ShaderProgram program = std::move(programResult.Value());

    auto pipelineResult =
        BuildTrianglePipeline(rig.device, program, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_D32_FLOAT);
    ASSERT_TRUE(pipelineResult.IsOk());
    GpuGraphicsPipeline pipeline = std::move(pipelineResult.Value());

    // Triangle plein écran, face à la caméra : normale (0,0,-1).
    Vertex3D triangle[3] = {
        {{-2.f, -2.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
        {{2.f, -2.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
        {{0.f, 2.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
    };
    auto vbResult = rig.device.CreateBuffer(gpu_buffer_usage::VERTEX, uint32_t(sizeof(triangle)));
    ASSERT_TRUE(vbResult.IsOk());
    GpuBuffer vertexBuffer = std::move(vbResult.Value());
    auto tbResult = rig.device.CreateTransferBuffer(gpu_transfer_buffer_usage::UPLOAD, uint32_t(sizeof(triangle)));
    ASSERT_TRUE(tbResult.IsOk());
    GpuTransferBuffer transfer = std::move(tbResult.Value());
    {
        auto mapped = transfer.Map(false);
        ASSERT_TRUE(bool(mapped));
        std::memcpy(mapped.GetData(), triangle, sizeof(triangle));
    }
    auto uploadCmd = rig.device.AcquireCommandBuffer();
    {
        GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
        GpuTransferBufferLocation src{transfer.Get(), 0};
        GpuBufferRegion dst{vertexBuffer.Get(), 0, uint32_t(sizeof(triangle))};
        copyPass.UploadToBuffer(src, dst, false);
    }
    ASSERT_TRUE(uploadCmd.Submit());
    ASSERT_TRUE(rig.device.WaitIdle());

    float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    struct PerObjectUBO {
        float model[16];
        float normalMatrix[16];
    };
    PerObjectUBO perObject{};
    std::memcpy(perObject.model, identity, sizeof(identity));
    std::memcpy(perObject.normalMatrix, identity, sizeof(identity));
    struct MaterialUBO {
        float baseColor[4];
        float emissive[4];
        float roughness, metallic, useTexture, padding0;
    };
    MaterialUBO material{{1.f, 1.f, 1.f, 1.f}, {0.f, 0.f, 0.f, 0.f}, 0.5f, 0.f, 0.f, 0.f};
    struct LightUBO {
        float lightDir[3];
        float pad1;
        float lightColor[4];
        float ambientColor[4];
        float cameraPos[3];
        float pad2;
    };
    LightUBO light{};
    light.lightDir[2] = 1.f; // uLightDir = (0,0,1) -> L = normalize(-uLightDir) = (0,0,-1) = N
    light.lightColor[0] = light.lightColor[1] = light.lightColor[2] = light.lightColor[3] = 1.f;
    // ambientColor reste à 0 : isole la contribution directe pour le calcul à la main.
    light.cameraPos[2] = -1.f; // V = normalize(cameraPos - worldPos) = normalize(0,0,-1) = (0,0,-1) = N

    auto frameCmd = rig.device.AcquireCommandBuffer();
    GpuColorTargetInfo colorTarget{};
    colorTarget.texture = rig.colorTexture.Get();
    colorTarget.clear_color = SDL_FColor{0.f, 0.f, 0.f, 1.f};
    colorTarget.load_op = gpu_load_op::CLEAR;
    colorTarget.store_op = gpu_store_op::STORE;
    GpuDepthStencilTargetInfo depthTarget{};
    depthTarget.texture = rig.depthTexture.Get();
    depthTarget.clear_depth = 1.f;
    depthTarget.load_op = gpu_load_op::CLEAR;
    depthTarget.store_op = gpu_store_op::DONT_CARE;
    depthTarget.stencil_load_op = gpu_load_op::DONT_CARE;
    depthTarget.stencil_store_op = gpu_store_op::DONT_CARE;
    {
        GpuRenderPass pass = frameCmd.BeginRenderPass(colorTarget, &depthTarget);
        pass.BindPipeline(pipeline);
        GpuBufferBinding binding{vertexBuffer.Get(), 0};
        pass.BindVertexBuffer(0, binding);
        frameCmd.PushVertexUniformData(0, identity);
        frameCmd.PushVertexUniformData(1, perObject);
        frameCmd.PushFragmentUniformData(0, material);
        frameCmd.PushFragmentUniformData(1, light);
        pass.DrawPrimitives(3);
    }
    ASSERT_TRUE(frameCmd.Submit());
    ASSERT_TRUE(rig.device.WaitIdle());

    auto downloadResult = rig.device.CreateTransferBuffer(gpu_transfer_buffer_usage::DOWNLOAD, 32 * 32 * 4);
    ASSERT_TRUE(downloadResult.IsOk());
    GpuTransferBuffer downloadBuffer = std::move(downloadResult.Value());
    auto downloadCmd = rig.device.AcquireCommandBuffer();
    {
        GpuCopyPass copyPass = downloadCmd.BeginCopyPass();
        GpuTextureRegion src{rig.colorTexture.Get(), 0, 0, 0, 0, 0, 32, 32, 1};
        GpuTextureTransferInfo dst{downloadBuffer.Get(), 0, 32, 32};
        copyPass.DownloadFromTexture(src, dst);
    }
    ASSERT_TRUE(downloadCmd.Submit());
    ASSERT_TRUE(rig.device.WaitIdle());

    auto mapped = downloadBuffer.Map(false);
    ASSERT_TRUE(bool(mapped));
    uint8_t *pixels = mapped.As<uint8_t>();
    int idx = (16 * 32 + 16) * 4; // centre du triangle
    EXPECT_TRUE(std::abs(int(pixels[idx + 0]) - 91) <= 2);
    EXPECT_TRUE(std::abs(int(pixels[idx + 1]) - 91) <= 2);
    EXPECT_TRUE(std::abs(int(pixels[idx + 2]) - 91) <= 2);

    EXPECT_TRUE(GetError().IsEmpty());
}

int main() {
    return RUN_ALL_TESTS();
}
