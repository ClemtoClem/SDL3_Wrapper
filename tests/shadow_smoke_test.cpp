// Smoke test : infrastructure de shadow mapping (lib/render3d/shadow.hpp) —
// M7 du plan. Construit le pipeline "depth-only" réel (celui que Canvas
// utilisera en M8), rend un triangle plein cadre dans une depth texture
// offscreen depuis le point de vue d'un DirectionalLight (via
// DirectionalLight::ShadowViewProjection(), light.hpp), télécharge la depth
// texture et vérifie que la valeur de profondeur au centre correspond à la
// valeur calculée à la main pour la matrice ortho utilisée — même
// méthodologie de vérification par pixel que shader_builder_smoke_test.cpp.
//
// Deux triangles de winding opposés sont dessinés (2 DrawIndexedPrimitives
// distincts) pour ce test : CreateShadowCasterPipeline() utilise le même
// cull_mode::BACK que le pipeline principal de Canvas, et ce test vérifie
// l'exactitude géométrique de la matrice lumière + le pipeline depth-only
// lui-même — pas la convention de winding d'un maillage donné (déjà validée
// par ExpectConsistentWinding dans render3d_smoke_test.cpp) — dessiner les
// deux windings garantit qu'un des deux passe le cull test quel que soit le
// sens exact, sans affaiblir la vérification de la valeur de profondeur.
#define USE_TEST
#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include "render3d/light.hpp"
#include "render3d/shader_builder.hpp"
#include "render3d/shadow.hpp"
#include <cmath>
#include <cstring>

namespace {

// Pipeline "plein cadre, éclairé" partagé par le 2e test — même structure que
// BuildTrianglePipeline() dans shader_builder_smoke_test.cpp (vertex layout
// Vertex3D à 4 attributs), le nombre d'échantillonneurs/UBO fragment étant
// entièrement déterminé par ce que le ShaderProgram a déclaré à la
// compilation (voir ShaderBuilder::Build → CompileShader).
[[nodiscard]] Result<sdl3::GpuGraphicsPipeline, String> BuildShadedQuadPipeline(sdl3::GpuDevice &device,
                                                                                render3d::ShaderProgram &program,
                                                                                sdl3::GpuTextureFormat colorFormat,
                                                                                sdl3::GpuTextureFormat depthFormat) {
    using namespace sdl3;
    static GpuVertexBufferDescription vertexBufferDesc{0, sizeof(render3d::Vertex3D), gpu_vertex_input_rate::VERTEX, 0};
    static GpuVertexAttribute vertexAttributes[4] = {
        {0, 0, gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(render3d::Vertex3D, position))},
        {1, 0, gpu_vertex_element_format::FLOAT3, uint32_t(offsetof(render3d::Vertex3D, normal))},
        {2, 0, gpu_vertex_element_format::FLOAT2, uint32_t(offsetof(render3d::Vertex3D, uv))},
        {3, 0, gpu_vertex_element_format::UBYTE4_NORM, uint32_t(offsetof(render3d::Vertex3D, color))},
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
    info.rasterizer_state.cull_mode = gpu_cull_mode::NONE; // seule la vérification winding du pipeline principal (Canvas) importe ici
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

using namespace sdl3;
using namespace render3d;

TEST(ShadowSmokeTest, DirectionalDepthPassMatchesHandComputedValue) {
    constexpr uint32_t SIZE = 32;

    auto windowResult = Window::Create(String("shadow_smoke_test"), int(SIZE), int(SIZE), 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());

    auto deviceResult = GpuDevice::Create(gpu_shader_format::SPIR_V | gpu_shader_format::MSL | gpu_shader_format::DXIL);
    ASSERT_TRUE(deviceResult.IsOk());
    GpuDevice device = std::move(deviceResult.Value());
    ASSERT_TRUE(device.ClaimWindow(MakeRef(window)));

    GpuTextureFormat depthFormat = ResolveShadowDepthFormat(device);
    auto depthResult = CreateShadowDepthTexture(device, SIZE, depthFormat);
    ASSERT_TRUE(depthResult.IsOk());
    GpuTexture depthTexture = std::move(depthResult.Value());

    auto programResult = BuildShadowCasterShaderProgram(device);
    ASSERT_TRUE(programResult.IsOk());
    ShaderProgram program = std::move(programResult.Value());

    auto pipelineResult = CreateShadowCasterPipeline(device, program, depthFormat);
    ASSERT_TRUE(pipelineResult.IsOk());
    GpuGraphicsPipeline pipeline = std::move(pipelineResult.Value());

    // Lumière directionnelle regardant vers +Z (dir={0,0,1}), volume ortho de
    // demi-étendue 5 centré sur l'origine, near=1/far=11 -> eye = target -
    // dir*(far*0.5) = (0,0,-5.5). Un triangle plein cadre à z=0 est donc à
    // une distance d=5.5 de l'oeil le long de la direction de vue : depth
    // attendue = (d - near) / (far - near) = (5.5 - 1) / 10 = 0.45.
    DirectionalLight light;
    light.direction = {0.f, 0.f, 1.f};
    light.shadowTarget = {0.f, 0.f, 0.f};
    light.shadowOrthoSize = 5.f;
    light.shadowNear = 1.f;
    light.shadowFar = 11.f;
    math::FMatrix4 viewProjection = light.ShadowViewProjection();

    // Deux petits triangles centrés (bien à l'intérieur de l'ortho box +-5,
    // laissant les coins du viewport hors triangle pour la vérification
    // "reste à la valeur de clear" ci-dessous), windings opposés.
    Vertex3D triangleA[3] = {
        {{-2.f, -2.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
        {{2.f, -2.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
        {{0.f, 2.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
    };
    Vertex3D triangleB[3] = {triangleA[0], triangleA[2], triangleA[1]};

    auto vbResult = device.CreateBuffer(gpu_buffer_usage::VERTEX, uint32_t(sizeof(triangleA) + sizeof(triangleB)));
    ASSERT_TRUE(vbResult.IsOk());
    GpuBuffer vertexBuffer = std::move(vbResult.Value());
    auto tbResult =
        device.CreateTransferBuffer(gpu_transfer_buffer_usage::UPLOAD, uint32_t(sizeof(triangleA) + sizeof(triangleB)));
    ASSERT_TRUE(tbResult.IsOk());
    GpuTransferBuffer transfer = std::move(tbResult.Value());
    {
        auto mapped = transfer.Map(false);
        ASSERT_TRUE(bool(mapped));
        std::memcpy(mapped.GetData(), triangleA, sizeof(triangleA));
        std::memcpy(static_cast<uint8_t *>(mapped.GetData()) + sizeof(triangleA), triangleB, sizeof(triangleB));
    }
    auto uploadCmd = device.AcquireCommandBuffer();
    {
        GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
        GpuTransferBufferLocation src{transfer.Get(), 0};
        GpuBufferRegion dst{vertexBuffer.Get(), 0, uint32_t(sizeof(triangleA) + sizeof(triangleB))};
        copyPass.UploadToBuffer(src, dst, false);
    }
    ASSERT_TRUE(uploadCmd.Submit());
    ASSERT_TRUE(device.WaitIdle());

    struct PerObjectUBO {
        math::FMatrix4 model;
        math::FMatrix4 normalMatrix;
    };
    PerObjectUBO perObject{math::FMatrix4::Identity(), math::FMatrix4::Identity()};

    auto frameCmd = device.AcquireCommandBuffer();
    GpuDepthStencilTargetInfo depthTarget{};
    depthTarget.texture = depthTexture.Get();
    depthTarget.clear_depth = 1.f;
    depthTarget.load_op = gpu_load_op::CLEAR;
    depthTarget.store_op = gpu_store_op::STORE;
    depthTarget.stencil_load_op = gpu_load_op::DONT_CARE;
    depthTarget.stencil_store_op = gpu_store_op::DONT_CARE;
    {
        GpuRenderPass pass = frameCmd.BeginRenderPass(std::span<const GpuColorTargetInfo>{}, &depthTarget);
        pass.BindPipeline(pipeline);
        GpuBufferBinding binding{vertexBuffer.Get(), 0};
        pass.BindVertexBuffer(0, binding);
        frameCmd.PushVertexUniformData(0, viewProjection);
        frameCmd.PushVertexUniformData(1, perObject);
        pass.DrawPrimitives(3, 1, 0);
        pass.DrawPrimitives(3, 1, 3);
    }
    ASSERT_TRUE(frameCmd.Submit());
    ASSERT_TRUE(device.WaitIdle());

    ASSERT_TRUE(depthFormat == GpuTextureFormat(SDL_GPU_TEXTUREFORMAT_D32_FLOAT)); // sinon le download 4o/texel ci-dessous est faux
    auto downloadResult = device.CreateTransferBuffer(gpu_transfer_buffer_usage::DOWNLOAD, SIZE * SIZE * 4);
    ASSERT_TRUE(downloadResult.IsOk());
    GpuTransferBuffer downloadBuffer = std::move(downloadResult.Value());
    auto downloadCmd = device.AcquireCommandBuffer();
    {
        GpuCopyPass copyPass = downloadCmd.BeginCopyPass();
        GpuTextureRegion src{depthTexture.Get(), 0, 0, 0, 0, 0, SIZE, SIZE, 1};
        GpuTextureTransferInfo dst{downloadBuffer.Get(), 0, SIZE, SIZE};
        copyPass.DownloadFromTexture(src, dst);
    }
    ASSERT_TRUE(downloadCmd.Submit());
    ASSERT_TRUE(device.WaitIdle());

    auto mapped = downloadBuffer.Map(false);
    ASSERT_TRUE(bool(mapped));
    float *depths = mapped.As<float>();
    float centerDepth = depths[(SIZE / 2) * SIZE + (SIZE / 2)];
    EXPECT_TRUE(std::abs(centerDepth - 0.45f) <= 0.02f);

    // Un coin hors du triangle (au-delà de l'ortho box de toute façon) doit
    // rester à la valeur de clear (1.0, jamais écrite).
    float cornerDepth = depths[0];
    EXPECT_TRUE(std::abs(cornerDepth - 1.f) <= 1e-5f);

    EXPECT_TRUE(GetError().IsEmpty());
}

TEST(ShadowSmokeTest, DirectionalShadowDarkensOccludedReceiverPixel) {
    // Scène à deux passes, entièrement bas niveau (pas de Canvas — le
    // swapchain n'est pas fiable en environnement sandboxé sans compositeur,
    // voir les autres tests bas niveau de ce dépôt) :
    //   1. Un petit "caster" carré à y=2 est rendu dans la shadow map d'une
    //      lumière directionnelle regardant vers le bas (direction={0,-1,0},
    //      exerçant la branche |dir.y|>0.99 de ShadowViewProjection — non
    //      couverte par le 1er test de ce fichier).
    //   2. Un grand "receiver" plan à y=0 est dessiné avec un shader Phong
    //      généré par ShaderBuilder().Shadow(1) (le vrai pipeline shadow-aware,
    //      pas un stub), échantillonnant cette shadow map.
    // Un pixel du receiver directement sous le caster doit être nettement
    // plus sombre qu'un pixel hors de son empreinte — seule affirmation
    // requise par le plan (pas de valeur BRDF exacte, cf. shader_builder_
    // smoke_test.cpp pour cette rigueur-là sur Phong/PBR sans ombre).
    constexpr uint32_t SIZE = 64;

    auto windowResult = Window::Create(String("shadow_smoke_test_shading"), int(SIZE), int(SIZE), 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());
    auto deviceResult = GpuDevice::Create(gpu_shader_format::SPIR_V | gpu_shader_format::MSL | gpu_shader_format::DXIL);
    ASSERT_TRUE(deviceResult.IsOk());
    GpuDevice device = std::move(deviceResult.Value());
    ASSERT_TRUE(device.ClaimWindow(MakeRef(window)));

    DirectionalLight light;
    light.direction = {0.f, -1.f, 0.f}; // éclaire vers le bas -> branche up=(0,0,1) de ShadowViewProjection
    light.shadowTarget = {0.f, 0.f, 0.f};
    light.shadowOrthoSize = 5.f;
    light.shadowNear = 1.f;
    light.shadowFar = 10.f;
    light.castShadow = true;
    math::FMatrix4 lightViewProjection = light.ShadowViewProjection();

    // ── Passe 1 : shadow map (caster carré [-1,1]x[-1,1] à y=2) ────────────
    GpuTextureFormat shadowDepthFormat = ResolveShadowDepthFormat(device);
    auto shadowDepthResult = CreateShadowDepthTexture(device, SHADOW_MAP_RESOLUTION, shadowDepthFormat);
    ASSERT_TRUE(shadowDepthResult.IsOk());
    GpuTexture shadowDepthTexture = std::move(shadowDepthResult.Value());

    auto casterProgramResult = BuildShadowCasterShaderProgram(device);
    ASSERT_TRUE(casterProgramResult.IsOk());
    ShaderProgram casterProgram = std::move(casterProgramResult.Value());
    auto casterPipelineResult = CreateShadowCasterPipeline(device, casterProgram, shadowDepthFormat);
    ASSERT_TRUE(casterPipelineResult.IsOk());
    GpuGraphicsPipeline casterPipeline = std::move(casterPipelineResult.Value());

    Vertex3D casterA[3] = {
        {{-1.f, 2.f, -1.f}, {0.f, 1.f, 0.f}, {0.f, 0.f}, Color::WHITE()},
        {{1.f, 2.f, -1.f}, {0.f, 1.f, 0.f}, {0.f, 0.f}, Color::WHITE()},
        {{1.f, 2.f, 1.f}, {0.f, 1.f, 0.f}, {0.f, 0.f}, Color::WHITE()},
    };
    Vertex3D casterB[3] = {casterA[0], casterA[2], {{-1.f, 2.f, 1.f}, {0.f, 1.f, 0.f}, {0.f, 0.f}, Color::WHITE()}};
    Vertex3D casterARev[3] = {casterA[0], casterA[2], casterA[1]};
    Vertex3D casterBRev[3] = {casterB[0], casterB[2], casterB[1]};

    Vertex3D casterVerts[12];
    std::memcpy(&casterVerts[0], casterA, sizeof(casterA));
    std::memcpy(&casterVerts[3], casterB, sizeof(casterB));
    std::memcpy(&casterVerts[6], casterARev, sizeof(casterARev));
    std::memcpy(&casterVerts[9], casterBRev, sizeof(casterBRev));

    auto casterVbResult = device.CreateBuffer(gpu_buffer_usage::VERTEX, uint32_t(sizeof(casterVerts)));
    ASSERT_TRUE(casterVbResult.IsOk());
    GpuBuffer casterVertexBuffer = std::move(casterVbResult.Value());
    {
        auto tbResult = device.CreateTransferBuffer(gpu_transfer_buffer_usage::UPLOAD, uint32_t(sizeof(casterVerts)));
        ASSERT_TRUE(tbResult.IsOk());
        GpuTransferBuffer transfer = std::move(tbResult.Value());
        {
            auto mapped = transfer.Map(false);
            ASSERT_TRUE(bool(mapped));
            std::memcpy(mapped.GetData(), casterVerts, sizeof(casterVerts));
        }
        auto uploadCmd = device.AcquireCommandBuffer();
        GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
        GpuTransferBufferLocation src{transfer.Get(), 0};
        GpuBufferRegion dst{casterVertexBuffer.Get(), 0, uint32_t(sizeof(casterVerts))};
        copyPass.UploadToBuffer(src, dst, false);
        ASSERT_TRUE(uploadCmd.Submit());
        ASSERT_TRUE(device.WaitIdle());
    }

    struct PerObjectUBO {
        math::FMatrix4 model;
        math::FMatrix4 normalMatrix;
    };
    PerObjectUBO identityObject{math::FMatrix4::Identity(), math::FMatrix4::Identity()};

    {
        auto shadowCmd = device.AcquireCommandBuffer();
        GpuDepthStencilTargetInfo shadowTarget{};
        shadowTarget.texture = shadowDepthTexture.Get();
        shadowTarget.clear_depth = 1.f;
        shadowTarget.load_op = gpu_load_op::CLEAR;
        shadowTarget.store_op = gpu_store_op::STORE;
        shadowTarget.stencil_load_op = gpu_load_op::DONT_CARE;
        shadowTarget.stencil_store_op = gpu_store_op::DONT_CARE;
        GpuRenderPass pass = shadowCmd.BeginRenderPass(std::span<const GpuColorTargetInfo>{}, &shadowTarget);
        pass.SetViewport(GpuViewport{0.f, 0.f, float(SHADOW_MAP_RESOLUTION), float(SHADOW_MAP_RESOLUTION), 0.f, 1.f});
        pass.BindPipeline(casterPipeline);
        GpuBufferBinding binding{casterVertexBuffer.Get(), 0};
        pass.BindVertexBuffer(0, binding);
        shadowCmd.PushVertexUniformData(0, lightViewProjection);
        shadowCmd.PushVertexUniformData(1, identityObject);
        pass.DrawPrimitives(12, 1, 0);
        ASSERT_TRUE(shadowCmd.Submit());
        ASSERT_TRUE(device.WaitIdle());
    }

    // ── Passe 2 : receiver plan [-4,4]x[-4,4] à y=0, shader shadow-aware ────
    auto shadedProgramResult = ShaderBuilder().Color(FColor{1.f, 1.f, 1.f, 1.f}).Lit(LightingModel::PHONG).Shadow(1).Build(device);
    ASSERT_TRUE(shadedProgramResult.IsOk());
    ShaderProgram shadedProgram = std::move(shadedProgramResult.Value());

    GpuTextureCreateInfo colorInfo{};
    colorInfo.type = gpu_texture_type::TEXTURE2_D;
    colorInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    colorInfo.usage = gpu_texture_usage::COLOR_TARGET | gpu_texture_usage::SAMPLER;
    colorInfo.width = SIZE;
    colorInfo.height = SIZE;
    colorInfo.layer_count_or_depth = 1;
    colorInfo.num_levels = 1;
    colorInfo.sample_count = gpu_sample_count::SAMPLE1;
    auto colorResult = device.CreateTexture(colorInfo);
    ASSERT_TRUE(colorResult.IsOk());
    GpuTexture colorTexture = std::move(colorResult.Value());

    GpuTextureCreateInfo mainDepthInfo = colorInfo;
    mainDepthInfo.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    mainDepthInfo.usage = gpu_texture_usage::DEPTH_STENCIL_TARGET;
    auto mainDepthResult = device.CreateTexture(mainDepthInfo);
    ASSERT_TRUE(mainDepthResult.IsOk());
    GpuTexture mainDepthTexture = std::move(mainDepthResult.Value());

    auto shadedPipelineResult = BuildShadedQuadPipeline(device, shadedProgram, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                                         SDL_GPU_TEXTUREFORMAT_D32_FLOAT);
    ASSERT_TRUE(shadedPipelineResult.IsOk());
    GpuGraphicsPipeline shadedPipeline = std::move(shadedPipelineResult.Value());

    Vertex3D receiver[6] = {
        {{-4.f, 0.f, -4.f}, {0.f, 1.f, 0.f}, {0.f, 0.f}, Color::WHITE()},
        {{4.f, 0.f, -4.f}, {0.f, 1.f, 0.f}, {1.f, 0.f}, Color::WHITE()},
        {{4.f, 0.f, 4.f}, {0.f, 1.f, 0.f}, {1.f, 1.f}, Color::WHITE()},
        {{-4.f, 0.f, -4.f}, {0.f, 1.f, 0.f}, {0.f, 0.f}, Color::WHITE()},
        {{4.f, 0.f, 4.f}, {0.f, 1.f, 0.f}, {1.f, 1.f}, Color::WHITE()},
        {{-4.f, 0.f, 4.f}, {0.f, 1.f, 0.f}, {0.f, 1.f}, Color::WHITE()},
    };
    auto receiverVbResult = device.CreateBuffer(gpu_buffer_usage::VERTEX, uint32_t(sizeof(receiver)));
    ASSERT_TRUE(receiverVbResult.IsOk());
    GpuBuffer receiverVertexBuffer = std::move(receiverVbResult.Value());
    {
        auto tbResult = device.CreateTransferBuffer(gpu_transfer_buffer_usage::UPLOAD, uint32_t(sizeof(receiver)));
        ASSERT_TRUE(tbResult.IsOk());
        GpuTransferBuffer transfer = std::move(tbResult.Value());
        {
            auto mapped = transfer.Map(false);
            ASSERT_TRUE(bool(mapped));
            std::memcpy(mapped.GetData(), receiver, sizeof(receiver));
        }
        auto uploadCmd = device.AcquireCommandBuffer();
        GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
        GpuTransferBufferLocation src{transfer.Get(), 0};
        GpuBufferRegion dst{receiverVertexBuffer.Get(), 0, uint32_t(sizeof(receiver))};
        copyPass.UploadToBuffer(src, dst, false);
        ASSERT_TRUE(uploadCmd.Submit());
        ASSERT_TRUE(device.WaitIdle());
    }

    // Texture blanche 1x1 pour le sampler albedo (useTexture=0 : jamais
    // échantillonnée, mais SDL_GPU exige un binding valide malgré tout).
    GpuTextureCreateInfo whiteInfo{};
    whiteInfo.type = gpu_texture_type::TEXTURE2_D;
    whiteInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    whiteInfo.usage = gpu_texture_usage::SAMPLER;
    whiteInfo.width = 1;
    whiteInfo.height = 1;
    whiteInfo.layer_count_or_depth = 1;
    whiteInfo.num_levels = 1;
    whiteInfo.sample_count = gpu_sample_count::SAMPLE1;
    auto whiteTextureResult = device.CreateTexture(whiteInfo);
    ASSERT_TRUE(whiteTextureResult.IsOk());
    GpuTexture whiteTexture = std::move(whiteTextureResult.Value());
    {
        uint8_t whitePixel[4] = {255, 255, 255, 255};
        auto tbResult = device.CreateTransferBuffer(gpu_transfer_buffer_usage::UPLOAD, sizeof(whitePixel));
        ASSERT_TRUE(tbResult.IsOk());
        GpuTransferBuffer transfer = std::move(tbResult.Value());
        {
            auto mapped = transfer.Map(false);
            ASSERT_TRUE(bool(mapped));
            std::memcpy(mapped.GetData(), whitePixel, sizeof(whitePixel));
        }
        auto uploadCmd = device.AcquireCommandBuffer();
        GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
        GpuTextureTransferInfo src{transfer.Get(), 0, 1, 1};
        GpuTextureRegion dst{whiteTexture.Get(), 0, 0, 0, 0, 0, 1, 1, 1};
        copyPass.UploadToTexture(src, dst, false);
        ASSERT_TRUE(uploadCmd.Submit());
        ASSERT_TRUE(device.WaitIdle());
    }

    GpuSamplerCreateInfo albedoSamplerInfo{};
    albedoSamplerInfo.min_filter = gpu_filter::LINEAR;
    albedoSamplerInfo.mag_filter = gpu_filter::LINEAR;
    albedoSamplerInfo.mipmap_mode = gpu_sampler_mipmap_mode::LINEAR;
    albedoSamplerInfo.address_mode_u = gpu_sampler_address_mode::REPEAT;
    albedoSamplerInfo.address_mode_v = gpu_sampler_address_mode::REPEAT;
    albedoSamplerInfo.address_mode_w = gpu_sampler_address_mode::REPEAT;
    auto albedoSamplerResult = device.CreateSampler(albedoSamplerInfo);
    ASSERT_TRUE(albedoSamplerResult.IsOk());
    GpuSampler albedoSampler = std::move(albedoSamplerResult.Value());

    auto shadowSamplerResult = CreateShadowComparisonSampler(device);
    ASSERT_TRUE(shadowSamplerResult.IsOk());
    GpuSampler shadowSampler = std::move(shadowSamplerResult.Value());

    struct MaterialUBO {
        float baseColor[4];
        float emissive[4];
        float roughness, metallic, useTexture, padding0;
    };
    MaterialUBO material{{1.f, 1.f, 1.f, 1.f}, {0.f, 0.f, 0.f, 0.f}, 0.5f, 0.f, 0.f, 0.f};

    // LightUBO étendu (shader_chunks::LIGHT_UBO_SHADOW) : les matrices d'ombre
    // vivent dans le MÊME binding=1 que Light, pas un binding séparé — SDL_GPU
    // limite à 4 uniform buffers fragment par étage (voir Canvas::End(),
    // LightUBOWithShadow, pour la même contrainte côté Canvas).
    struct LightUBOWithShadow {
        float lightDir[3];
        float pad1;
        float lightColor[4];
        float ambientColor[4];
        float cameraPos[3];
        float pad2;
        math::FMatrix4 dirShadowMatrix;
        math::FMatrix4 spotShadowMatrix0;
        math::FMatrix4 spotShadowMatrix1;
        float dirShadowEnabled;
        float numSpotShadows;
        float shadowBias;
        float padding3;
    };
    LightUBOWithShadow lightUbo{};
    lightUbo.lightDir[1] = -1.f;
    lightUbo.lightColor[0] = lightUbo.lightColor[1] = lightUbo.lightColor[2] = lightUbo.lightColor[3] = 1.f;
    // ambientColor reste à 0 : isole la contribution directe (pixel non
    // ombré doit clairement contraster avec le pixel ombré, cf. plan).
    lightUbo.cameraPos[1] = 8.f;
    lightUbo.dirShadowMatrix = lightViewProjection;
    lightUbo.dirShadowEnabled = 1.f;
    lightUbo.numSpotShadows = 0.f;
    lightUbo.shadowBias = 0.0025f;

    auto colorCmd = device.AcquireCommandBuffer();
    GpuColorTargetInfo colorTarget{};
    colorTarget.texture = colorTexture.Get();
    colorTarget.clear_color = SDL_FColor{0.f, 0.f, 0.f, 1.f};
    colorTarget.load_op = gpu_load_op::CLEAR;
    colorTarget.store_op = gpu_store_op::STORE;
    GpuDepthStencilTargetInfo mainDepthTarget{};
    mainDepthTarget.texture = mainDepthTexture.Get();
    mainDepthTarget.clear_depth = 1.f;
    mainDepthTarget.load_op = gpu_load_op::CLEAR;
    mainDepthTarget.store_op = gpu_store_op::DONT_CARE;
    mainDepthTarget.stencil_load_op = gpu_load_op::DONT_CARE;
    mainDepthTarget.stencil_store_op = gpu_store_op::DONT_CARE;
    {
        GpuRenderPass pass = colorCmd.BeginRenderPass(colorTarget, &mainDepthTarget);
        pass.BindPipeline(shadedPipeline);
        GpuBufferBinding binding{receiverVertexBuffer.Get(), 0};
        pass.BindVertexBuffer(0, binding);
        // Caméra orthographique vue de dessus (même technique que
        // DirectionalLight::ShadowViewProjection, sans lien avec elle) :
        // couvre exactement l'étendue du receiver ([-4,4]x[-4,4]) pour que
        // le centre du viewport corresponde à l'origine monde (sous le
        // caster) et n'importe quel coin à un point manifestement hors de
        // son empreinte, quel que soit le flip d'axe exact introduit par
        // LookAt/Ortho (symétrique par construction).
        math::FMatrix4 cameraView = math::FMatrix4::LookAt({0.f, 8.f, 0.f}, {0.f, 0.f, 0.f}, {0.f, 0.f, 1.f});
        math::FMatrix4 cameraProj = math::FMatrix4::Ortho(-4.f, 4.f, -4.f, 4.f, 1.f, 15.f);
        colorCmd.PushVertexUniformData(0, cameraProj * cameraView);
        colorCmd.PushVertexUniformData(1, identityObject);
        colorCmd.PushFragmentUniformData(0, material);
        colorCmd.PushFragmentUniformData(1, lightUbo);

        std::array<GpuTextureSamplerBinding, 4> samplerBindings{};
        samplerBindings[0] = {whiteTexture.Get(), albedoSampler.Get()};
        samplerBindings[1] = {shadowDepthTexture.Get(), shadowSampler.Get()};
        samplerBindings[2] = {shadowDepthTexture.Get(), shadowSampler.Get()}; // spot0, non utilisé (numSpotShadows=0)
        samplerBindings[3] = {shadowDepthTexture.Get(), shadowSampler.Get()}; // spot1, idem
        pass.BindFragmentSamplers(0, samplerBindings);

        pass.DrawPrimitives(6);
    }
    ASSERT_TRUE(colorCmd.Submit());
    ASSERT_TRUE(device.WaitIdle());

    auto downloadResult = device.CreateTransferBuffer(gpu_transfer_buffer_usage::DOWNLOAD, SIZE * SIZE * 4);
    ASSERT_TRUE(downloadResult.IsOk());
    GpuTransferBuffer downloadBuffer = std::move(downloadResult.Value());
    auto downloadCmd = device.AcquireCommandBuffer();
    {
        GpuCopyPass copyPass = downloadCmd.BeginCopyPass();
        GpuTextureRegion src{colorTexture.Get(), 0, 0, 0, 0, 0, SIZE, SIZE, 1};
        GpuTextureTransferInfo dst{downloadBuffer.Get(), 0, SIZE, SIZE};
        copyPass.DownloadFromTexture(src, dst);
    }
    ASSERT_TRUE(downloadCmd.Submit());
    ASSERT_TRUE(device.WaitIdle());

    auto mapped = downloadBuffer.Map(false);
    ASSERT_TRUE(bool(mapped));
    uint8_t *pixels = mapped.As<uint8_t>();
    // Caméra ortho symétrique centrée sur l'origine (voir ci-dessus) : le
    // centre du viewport correspond à l'origine monde (sous le caster,
    // ombré) et n'importe quel coin à un point loin de son empreinte ±1
    // (éclairé), quel que soit le flip d'axe exact de LookAt/Ortho.
    int shadowedIdx = ((SIZE / 2) * SIZE + (SIZE / 2)) * 4;
    int litIdx = ((SIZE - 2) * SIZE + (SIZE - 2)) * 4;
    int shadowedBrightness = pixels[shadowedIdx + 0];
    int litBrightness = pixels[litIdx + 0];
    EXPECT_TRUE(shadowedBrightness < 60);
    EXPECT_TRUE(litBrightness > 150);
    EXPECT_TRUE(litBrightness - shadowedBrightness > 100);

    EXPECT_TRUE(GetError().IsEmpty());
}

TEST(ShadowSmokeTest, PointLightCubeShadowDarkensOccludedReceiverPixel) {
    // Même schéma à deux passes que le test directionnel ci-dessus, avec un
    // PointLight à la place : caster carré à y=2 au-dessus d'un receiver
    // plan à y=0, lumière au-dessus (0,4,0). Le rayon lumière->(0,0,0)
    // (directement sous la lumière ET le caster) est bloqué ; le rayon
    // lumière->(3,0,3) (hors de l'empreinte ±1 du caster) ne l'est pas —
    // même affirmation que le plan pour M8 (contraste, pas de valeur BRDF
    // exacte), mais exerce en plus le pipeline cubemap réel (les 6 faces,
    // voir shadow.hpp::CreatePointShadowCasterPipeline/CubeTexture) et
    // vérifie implicitement que l'ordre des faces SDL_GPU (layer_or_depth_plane)
    // correspond à l'échantillonnage samplerCube standard (+X,-X,+Y,-Y,+Z,-Z,
    // voir PointLight::ShadowViewProjection, light.hpp) — jamais exercé avant
    // ce test dans ce dépôt.
    constexpr uint32_t SIZE = 64;

    auto windowResult = Window::Create(String("shadow_smoke_test_point"), int(SIZE), int(SIZE), 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());
    auto deviceResult = GpuDevice::Create(gpu_shader_format::SPIR_V | gpu_shader_format::MSL | gpu_shader_format::DXIL);
    ASSERT_TRUE(deviceResult.IsOk());
    GpuDevice device = std::move(deviceResult.Value());
    ASSERT_TRUE(device.ClaimWindow(MakeRef(window)));

    PointLight light;
    light.position = {0.f, 4.f, 0.f};
    light.castShadow = true;
    light.shadowNear = 0.1f;
    light.shadowFar = 10.f;

    // ── Passe 1 : cubemap de distance (caster carré [-1,1]x[-1,1] à y=2) ───
    auto pointCubeResult = CreatePointShadowCubeTexture(device, POINT_SHADOW_MAP_RESOLUTION);
    ASSERT_TRUE(pointCubeResult.IsOk());
    GpuTexture pointShadowMap = std::move(pointCubeResult.Value());

    GpuTextureFormat faceDepthFormat = ResolveShadowDepthFormat(device);
    auto faceDepthResult = CreateShadowDepthTexture(device, POINT_SHADOW_MAP_RESOLUTION, faceDepthFormat);
    ASSERT_TRUE(faceDepthResult.IsOk());
    GpuTexture faceDepthTexture = std::move(faceDepthResult.Value());

    auto pointCasterProgramResult = BuildPointShadowCasterShaderProgram(device);
    ASSERT_TRUE(pointCasterProgramResult.IsOk());
    ShaderProgram pointCasterProgram = std::move(pointCasterProgramResult.Value());
    auto pointCasterPipelineResult = CreatePointShadowCasterPipeline(device, pointCasterProgram, faceDepthFormat);
    ASSERT_TRUE(pointCasterPipelineResult.IsOk());
    GpuGraphicsPipeline pointCasterPipeline = std::move(pointCasterPipelineResult.Value());

    Vertex3D casterA[3] = {
        {{-1.f, 2.f, -1.f}, {0.f, 1.f, 0.f}, {0.f, 0.f}, Color::WHITE()},
        {{1.f, 2.f, -1.f}, {0.f, 1.f, 0.f}, {0.f, 0.f}, Color::WHITE()},
        {{1.f, 2.f, 1.f}, {0.f, 1.f, 0.f}, {0.f, 0.f}, Color::WHITE()},
    };
    Vertex3D casterB[3] = {casterA[0], casterA[2], {{-1.f, 2.f, 1.f}, {0.f, 1.f, 0.f}, {0.f, 0.f}, Color::WHITE()}};
    Vertex3D casterARev[3] = {casterA[0], casterA[2], casterA[1]};
    Vertex3D casterBRev[3] = {casterB[0], casterB[2], casterB[1]};
    Vertex3D casterVerts[12];
    std::memcpy(&casterVerts[0], casterA, sizeof(casterA));
    std::memcpy(&casterVerts[3], casterB, sizeof(casterB));
    std::memcpy(&casterVerts[6], casterARev, sizeof(casterARev));
    std::memcpy(&casterVerts[9], casterBRev, sizeof(casterBRev));

    auto casterVbResult = device.CreateBuffer(gpu_buffer_usage::VERTEX, uint32_t(sizeof(casterVerts)));
    ASSERT_TRUE(casterVbResult.IsOk());
    GpuBuffer casterVertexBuffer = std::move(casterVbResult.Value());
    {
        auto tbResult = device.CreateTransferBuffer(gpu_transfer_buffer_usage::UPLOAD, uint32_t(sizeof(casterVerts)));
        ASSERT_TRUE(tbResult.IsOk());
        GpuTransferBuffer transfer = std::move(tbResult.Value());
        {
            auto mapped = transfer.Map(false);
            ASSERT_TRUE(bool(mapped));
            std::memcpy(mapped.GetData(), casterVerts, sizeof(casterVerts));
        }
        auto uploadCmd = device.AcquireCommandBuffer();
        GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
        GpuTransferBufferLocation src{transfer.Get(), 0};
        GpuBufferRegion dst{casterVertexBuffer.Get(), 0, uint32_t(sizeof(casterVerts))};
        copyPass.UploadToBuffer(src, dst, false);
        ASSERT_TRUE(uploadCmd.Submit());
        ASSERT_TRUE(device.WaitIdle());
    }

    struct PerObjectUBO {
        math::FMatrix4 model;
        math::FMatrix4 normalMatrix;
    };
    PerObjectUBO identityObject{math::FMatrix4::Identity(), math::FMatrix4::Identity()};
    struct PointShadowCasterUBO {
        float lightPos[3];
        float padding0;
    };
    PointShadowCasterUBO pointUbo{{light.position.x, light.position.y, light.position.z}, 0.f};

    for (int face = 0; face < 6; ++face) {
        auto shadowCmd = device.AcquireCommandBuffer();
        GpuColorTargetInfo faceColorTarget{};
        faceColorTarget.texture = pointShadowMap.Get();
        faceColorTarget.layer_or_depth_plane = uint32_t(face);
        faceColorTarget.clear_color = SDL_FColor{1e6f, 1e6f, 1e6f, 1.f};
        faceColorTarget.load_op = gpu_load_op::CLEAR;
        faceColorTarget.store_op = gpu_store_op::STORE;
        GpuDepthStencilTargetInfo faceDepthTarget{};
        faceDepthTarget.texture = faceDepthTexture.Get();
        faceDepthTarget.clear_depth = 1.f;
        faceDepthTarget.load_op = gpu_load_op::CLEAR;
        faceDepthTarget.store_op = gpu_store_op::DONT_CARE;
        faceDepthTarget.stencil_load_op = gpu_load_op::DONT_CARE;
        faceDepthTarget.stencil_store_op = gpu_store_op::DONT_CARE;
        GpuRenderPass pass = shadowCmd.BeginRenderPass(faceColorTarget, &faceDepthTarget);
        pass.SetViewport(GpuViewport{0.f, 0.f, float(POINT_SHADOW_MAP_RESOLUTION), float(POINT_SHADOW_MAP_RESOLUTION),
                                     0.f, 1.f});
        pass.BindPipeline(pointCasterPipeline);
        GpuBufferBinding binding{casterVertexBuffer.Get(), 0};
        pass.BindVertexBuffer(0, binding);
        shadowCmd.PushVertexUniformData(0, light.ShadowViewProjection(face));
        shadowCmd.PushVertexUniformData(1, identityObject);
        shadowCmd.PushFragmentUniformData(0, pointUbo);
        pass.DrawPrimitives(12, 1, 0);
        ASSERT_TRUE(shadowCmd.Submit());
        ASSERT_TRUE(device.WaitIdle());
    }

    // ── Passe 2 : receiver plan [-4,4]x[-4,4] à y=0, PointLight+ombre ───────
    auto shadedProgramResult = ShaderBuilder()
                                   .Color(FColor{1.f, 1.f, 1.f, 1.f})
                                   .Lit(LightingModel::PHONG)
                                   .PointLights(1)
                                   .Shadow(1)
                                   .Build(device);
    ASSERT_TRUE(shadedProgramResult.IsOk());
    ShaderProgram shadedProgram = std::move(shadedProgramResult.Value());

    GpuTextureCreateInfo colorInfo{};
    colorInfo.type = gpu_texture_type::TEXTURE2_D;
    colorInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    colorInfo.usage = gpu_texture_usage::COLOR_TARGET | gpu_texture_usage::SAMPLER;
    colorInfo.width = SIZE;
    colorInfo.height = SIZE;
    colorInfo.layer_count_or_depth = 1;
    colorInfo.num_levels = 1;
    colorInfo.sample_count = gpu_sample_count::SAMPLE1;
    auto colorResult = device.CreateTexture(colorInfo);
    ASSERT_TRUE(colorResult.IsOk());
    GpuTexture colorTexture = std::move(colorResult.Value());

    GpuTextureCreateInfo mainDepthInfo = colorInfo;
    mainDepthInfo.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    mainDepthInfo.usage = gpu_texture_usage::DEPTH_STENCIL_TARGET;
    auto mainDepthResult = device.CreateTexture(mainDepthInfo);
    ASSERT_TRUE(mainDepthResult.IsOk());
    GpuTexture mainDepthTexture = std::move(mainDepthResult.Value());

    auto shadedPipelineResult = BuildShadedQuadPipeline(device, shadedProgram, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                                         SDL_GPU_TEXTUREFORMAT_D32_FLOAT);
    ASSERT_TRUE(shadedPipelineResult.IsOk());
    GpuGraphicsPipeline shadedPipeline = std::move(shadedPipelineResult.Value());

    Vertex3D receiver[6] = {
        {{-4.f, 0.f, -4.f}, {0.f, 1.f, 0.f}, {0.f, 0.f}, Color::WHITE()},
        {{4.f, 0.f, -4.f}, {0.f, 1.f, 0.f}, {1.f, 0.f}, Color::WHITE()},
        {{4.f, 0.f, 4.f}, {0.f, 1.f, 0.f}, {1.f, 1.f}, Color::WHITE()},
        {{-4.f, 0.f, -4.f}, {0.f, 1.f, 0.f}, {0.f, 0.f}, Color::WHITE()},
        {{4.f, 0.f, 4.f}, {0.f, 1.f, 0.f}, {1.f, 1.f}, Color::WHITE()},
        {{-4.f, 0.f, 4.f}, {0.f, 1.f, 0.f}, {0.f, 1.f}, Color::WHITE()},
    };
    auto receiverVbResult = device.CreateBuffer(gpu_buffer_usage::VERTEX, uint32_t(sizeof(receiver)));
    ASSERT_TRUE(receiverVbResult.IsOk());
    GpuBuffer receiverVertexBuffer = std::move(receiverVbResult.Value());
    {
        auto tbResult = device.CreateTransferBuffer(gpu_transfer_buffer_usage::UPLOAD, uint32_t(sizeof(receiver)));
        ASSERT_TRUE(tbResult.IsOk());
        GpuTransferBuffer transfer = std::move(tbResult.Value());
        {
            auto mapped = transfer.Map(false);
            ASSERT_TRUE(bool(mapped));
            std::memcpy(mapped.GetData(), receiver, sizeof(receiver));
        }
        auto uploadCmd = device.AcquireCommandBuffer();
        GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
        GpuTransferBufferLocation src{transfer.Get(), 0};
        GpuBufferRegion dst{receiverVertexBuffer.Get(), 0, uint32_t(sizeof(receiver))};
        copyPass.UploadToBuffer(src, dst, false);
        ASSERT_TRUE(uploadCmd.Submit());
        ASSERT_TRUE(device.WaitIdle());
    }

    GpuTextureCreateInfo whiteInfo{};
    whiteInfo.type = gpu_texture_type::TEXTURE2_D;
    whiteInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    whiteInfo.usage = gpu_texture_usage::SAMPLER;
    whiteInfo.width = 1;
    whiteInfo.height = 1;
    whiteInfo.layer_count_or_depth = 1;
    whiteInfo.num_levels = 1;
    whiteInfo.sample_count = gpu_sample_count::SAMPLE1;
    auto whiteTextureResult = device.CreateTexture(whiteInfo);
    ASSERT_TRUE(whiteTextureResult.IsOk());
    GpuTexture whiteTexture = std::move(whiteTextureResult.Value());
    {
        uint8_t whitePixel[4] = {255, 255, 255, 255};
        auto tbResult = device.CreateTransferBuffer(gpu_transfer_buffer_usage::UPLOAD, sizeof(whitePixel));
        ASSERT_TRUE(tbResult.IsOk());
        GpuTransferBuffer transfer = std::move(tbResult.Value());
        {
            auto mapped = transfer.Map(false);
            ASSERT_TRUE(bool(mapped));
            std::memcpy(mapped.GetData(), whitePixel, sizeof(whitePixel));
        }
        auto uploadCmd = device.AcquireCommandBuffer();
        GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
        GpuTextureTransferInfo src{transfer.Get(), 0, 1, 1};
        GpuTextureRegion dst{whiteTexture.Get(), 0, 0, 0, 0, 0, 1, 1, 1};
        copyPass.UploadToTexture(src, dst, false);
        ASSERT_TRUE(uploadCmd.Submit());
        ASSERT_TRUE(device.WaitIdle());
    }

    GpuSamplerCreateInfo albedoSamplerInfo{};
    albedoSamplerInfo.min_filter = gpu_filter::LINEAR;
    albedoSamplerInfo.mag_filter = gpu_filter::LINEAR;
    albedoSamplerInfo.mipmap_mode = gpu_sampler_mipmap_mode::LINEAR;
    albedoSamplerInfo.address_mode_u = gpu_sampler_address_mode::REPEAT;
    albedoSamplerInfo.address_mode_v = gpu_sampler_address_mode::REPEAT;
    albedoSamplerInfo.address_mode_w = gpu_sampler_address_mode::REPEAT;
    auto albedoSamplerResult = device.CreateSampler(albedoSamplerInfo);
    ASSERT_TRUE(albedoSamplerResult.IsOk());
    GpuSampler albedoSampler = std::move(albedoSamplerResult.Value());

    auto dirShadowSamplerResult = CreateShadowComparisonSampler(device);
    ASSERT_TRUE(dirShadowSamplerResult.IsOk());
    GpuSampler dirShadowSampler = std::move(dirShadowSamplerResult.Value());
    // Sert de bouchon pour les samplers 2D directionnel/spot (déclarés mais
    // jamais échantillonnés : uDirShadowEnabled=0, uNumSpotShadows=0
    // ci-dessous), et une depth texture 2D quelconque suffit comme bouchon.
    auto dummyDepthResult = CreateShadowDepthTexture(device, 4, faceDepthFormat);
    ASSERT_TRUE(dummyDepthResult.IsOk());
    GpuTexture dummyDepthTexture = std::move(dummyDepthResult.Value());

    auto pointShadowSamplerResult = CreatePointShadowSampler(device);
    ASSERT_TRUE(pointShadowSamplerResult.IsOk());
    GpuSampler pointShadowSampler = std::move(pointShadowSamplerResult.Value());

    struct MaterialUBO {
        float baseColor[4];
        float emissive[4];
        float roughness, metallic, useTexture, padding0;
    };
    MaterialUBO material{{1.f, 1.f, 1.f, 1.f}, {0.f, 0.f, 0.f, 0.f}, 0.5f, 0.f, 0.f, 0.f};

    struct LightUBOWithShadow {
        float lightDir[3];
        float pad1;
        float lightColor[4];
        float ambientColor[4];
        float cameraPos[3];
        float pad2;
        math::FMatrix4 dirShadowMatrix;
        math::FMatrix4 spotShadowMatrix0;
        math::FMatrix4 spotShadowMatrix1;
        float dirShadowEnabled;
        float numSpotShadows;
        float shadowBias;
        float padding3;
        float pointShadowEnabled;
        float pointShadowLightIndex;
        float pointShadowBias;
        float pointShadowPadding;
    };
    LightUBOWithShadow lightUbo{};
    // uLightColor/uAmbientColor restent à 0 : isole la contribution du
    // PointLight (seule sous test ici), même logique que les tests ci-dessus.
    lightUbo.cameraPos[1] = 8.f;
    lightUbo.pointShadowEnabled = 1.f;
    lightUbo.pointShadowLightIndex = 0.f;
    lightUbo.pointShadowBias = 0.05f;

    struct PointLightGpu {
        float position[3], distanceVal;
        float color[3], decay;
    };
    // decay=0 -> PunctualAttenuation retourne 1.0 constant (voir shader_chunks::
    // LIGHT_ATTENUATION_FUNC) : isole le test de l'atténuation par distance,
    // seul le terme géométrique dot(N,L) (et l'ombre) varie entre les 2 points
    // sondés. Couleur forte pour saturer le point éclairé après le terme géométrique.
    PointLightGpu pointLightGpu{{light.position.x, light.position.y, light.position.z}, 0.f, {2.f, 2.f, 2.f}, 0.f};

    auto colorCmd = device.AcquireCommandBuffer();
    GpuColorTargetInfo colorTarget{};
    colorTarget.texture = colorTexture.Get();
    colorTarget.clear_color = SDL_FColor{0.f, 0.f, 0.f, 1.f};
    colorTarget.load_op = gpu_load_op::CLEAR;
    colorTarget.store_op = gpu_store_op::STORE;
    GpuDepthStencilTargetInfo mainDepthTarget{};
    mainDepthTarget.texture = mainDepthTexture.Get();
    mainDepthTarget.clear_depth = 1.f;
    mainDepthTarget.load_op = gpu_load_op::CLEAR;
    mainDepthTarget.store_op = gpu_store_op::DONT_CARE;
    mainDepthTarget.stencil_load_op = gpu_load_op::DONT_CARE;
    mainDepthTarget.stencil_store_op = gpu_store_op::DONT_CARE;
    {
        GpuRenderPass pass = colorCmd.BeginRenderPass(colorTarget, &mainDepthTarget);
        pass.BindPipeline(shadedPipeline);
        GpuBufferBinding binding{receiverVertexBuffer.Get(), 0};
        pass.BindVertexBuffer(0, binding);
        math::FMatrix4 cameraView = math::FMatrix4::LookAt({0.f, 8.f, 0.f}, {0.f, 0.f, 0.f}, {0.f, 0.f, 1.f});
        math::FMatrix4 cameraProj = math::FMatrix4::Ortho(-4.f, 4.f, -4.f, 4.f, 1.f, 15.f);
        colorCmd.PushVertexUniformData(0, cameraProj * cameraView);
        colorCmd.PushVertexUniformData(1, identityObject);
        colorCmd.PushFragmentUniformData(0, material);
        colorCmd.PushFragmentUniformData(1, lightUbo);
        SDL_PushGPUFragmentUniformData(colorCmd.Get(), 2, &pointLightGpu, sizeof(pointLightGpu));

        std::array<GpuTextureSamplerBinding, 5> samplerBindings{};
        samplerBindings[0] = {whiteTexture.Get(), albedoSampler.Get()};
        samplerBindings[1] = {dummyDepthTexture.Get(), dirShadowSampler.Get()};
        samplerBindings[2] = {dummyDepthTexture.Get(), dirShadowSampler.Get()};
        samplerBindings[3] = {dummyDepthTexture.Get(), dirShadowSampler.Get()};
        samplerBindings[4] = {pointShadowMap.Get(), pointShadowSampler.Get()};
        pass.BindFragmentSamplers(0, samplerBindings);

        pass.DrawPrimitives(6);
    }
    ASSERT_TRUE(colorCmd.Submit());
    ASSERT_TRUE(device.WaitIdle());

    auto downloadResult = device.CreateTransferBuffer(gpu_transfer_buffer_usage::DOWNLOAD, SIZE * SIZE * 4);
    ASSERT_TRUE(downloadResult.IsOk());
    GpuTransferBuffer downloadBuffer = std::move(downloadResult.Value());
    auto downloadCmd = device.AcquireCommandBuffer();
    {
        GpuCopyPass copyPass = downloadCmd.BeginCopyPass();
        GpuTextureRegion src{colorTexture.Get(), 0, 0, 0, 0, 0, SIZE, SIZE, 1};
        GpuTextureTransferInfo dst{downloadBuffer.Get(), 0, SIZE, SIZE};
        copyPass.DownloadFromTexture(src, dst);
    }
    ASSERT_TRUE(downloadCmd.Submit());
    ASSERT_TRUE(device.WaitIdle());

    auto mapped = downloadBuffer.Map(false);
    ASSERT_TRUE(bool(mapped));
    uint8_t *pixels = mapped.As<uint8_t>();
    int shadowedIdx = ((SIZE / 2) * SIZE + (SIZE / 2)) * 4;
    int litIdx = ((SIZE - 2) * SIZE + (SIZE - 2)) * 4;
    int shadowedBrightness = pixels[shadowedIdx + 0];
    int litBrightness = pixels[litIdx + 0];
    EXPECT_TRUE(shadowedBrightness < 60);
    EXPECT_TRUE(litBrightness > 150);
    EXPECT_TRUE(litBrightness - shadowedBrightness > 100);

    EXPECT_TRUE(GetError().IsEmpty());
}

int main() { return RUN_ALL_TESTS(); }
