// Smoke test : conversion équirectangulaire -> cubemap (lib/render3d/cubemap.hpp,
// environment.hpp) — M11 du plan. Source synthétique (pas de vrai fichier
// .hdr, généré en mémoire) : un dégradé PÉRIODIQUE en longitude
// (color(u) = (0.5+0.5cos(2πu), 0, 0.5+0.5sin(2πu))) plutôt que des blocs de
// couleur discrets — un motif discret placerait une transition exactement
// sur l'axe -X (u=0/1, la couture de l'enroulement REPEAT), un motif continu
// et périodique élimine ce risque : n'importe quelle direction (y compris
// exactement sur la couture) donne une couleur bien définie, indépendante de
// l'alignement exact des texels avec le filtrage LINEAR réellement utilisé
// en production (voir LoadEnvironmentFromHdr).
#define USE_TEST
#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include "render3d/environment.hpp"
#include "render3d/shader_builder.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

using namespace sdl3;
using namespace render3d;

namespace {

void AppendBytes(std::vector<uint8_t> &buf, std::initializer_list<uint8_t> bytes) {
    buf.insert(buf.end(), bytes.begin(), bytes.end());
}

void AppendString(std::vector<uint8_t> &buf, const char *s) {
    while (*s)
        buf.push_back(uint8_t(*s++));
}

constexpr int EQUIRECT_WIDTH = 64;
constexpr int EQUIRECT_HEIGHT = 4;

[[nodiscard]] HdrImage MakeSyntheticEquirect() {
    HdrImage image;
    image.width = EQUIRECT_WIDTH;
    image.height = EQUIRECT_HEIGHT;
    image.pixels.resize(size_t(EQUIRECT_WIDTH) * size_t(EQUIRECT_HEIGHT) * 3);
    constexpr float PI = 3.14159265359f;
    for (int y = 0; y < EQUIRECT_HEIGHT; ++y) {
        for (int x = 0; x < EQUIRECT_WIDTH; ++x) {
            float u = (float(x) + 0.5f) / float(EQUIRECT_WIDTH);
            float r = 0.5f + 0.5f * std::cos(2.f * PI * u);
            float b = 0.5f + 0.5f * std::sin(2.f * PI * u);
            size_t idx = (size_t(y) * size_t(EQUIRECT_WIDTH) + size_t(x)) * 3;
            image.pixels[idx + 0] = r;
            image.pixels[idx + 1] = 0.f;
            image.pixels[idx + 2] = b;
        }
    }
    return image;
}

} // namespace

TEST(EnvironmentSmokeTest, EquirectToCubemapMatchesExpectedDirectionalColor) {
    constexpr uint32_t FACE_SIZE = 32;

    auto windowResult = Window::Create(String("environment_smoke_test"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());
    auto deviceResult = GpuDevice::Create(gpu_shader_format::SPIR_V | gpu_shader_format::MSL | gpu_shader_format::DXIL);
    ASSERT_TRUE(deviceResult.IsOk());
    GpuDevice device = std::move(deviceResult.Value());
    ASSERT_TRUE(device.ClaimWindow(MakeRef(window)));

    HdrImage synthetic = MakeSyntheticEquirect();
    auto equirectResult = UploadEquirectTexture(device, synthetic);
    ASSERT_TRUE(equirectResult.IsOk());
    GpuTexture equirectTexture = std::move(equirectResult.Value());

    GpuSamplerCreateInfo equirectSamplerInfo{};
    equirectSamplerInfo.min_filter = gpu_filter::LINEAR;
    equirectSamplerInfo.mag_filter = gpu_filter::LINEAR;
    equirectSamplerInfo.mipmap_mode = gpu_sampler_mipmap_mode::LINEAR;
    equirectSamplerInfo.address_mode_u = gpu_sampler_address_mode::REPEAT;
    equirectSamplerInfo.address_mode_v = gpu_sampler_address_mode::CLAMP_TO_EDGE;
    equirectSamplerInfo.address_mode_w = gpu_sampler_address_mode::CLAMP_TO_EDGE;
    auto equirectSamplerResult = device.CreateSampler(equirectSamplerInfo);
    ASSERT_TRUE(equirectSamplerResult.IsOk());
    GpuSampler equirectSampler = std::move(equirectSamplerResult.Value());

    GpuTextureFormat cubemapFormat = ResolveHdrColorFormat(device);
    ASSERT_TRUE(cubemapFormat == GpuTextureFormat(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT) ||
               cubemapFormat == GpuTextureFormat(SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT));
    auto cubemapResult = CreateEnvironmentCubemap(device, FACE_SIZE, cubemapFormat);
    ASSERT_TRUE(cubemapResult.IsOk());
    GpuTexture cubemap = std::move(cubemapResult.Value());

    auto programResult = BuildEquirectToCubemapShaderProgram(device);
    ASSERT_TRUE(programResult.IsOk());
    ShaderProgram program = std::move(programResult.Value());
    auto pipelineResult = CreateCubeCapturePipeline(device, program, cubemapFormat);
    ASSERT_TRUE(pipelineResult.IsOk());
    GpuGraphicsPipeline pipeline = std::move(pipelineResult.Value());

    std::vector<Vertex3D> cubeVerts = CubeVertices();
    uint32_t cubeBytes = uint32_t(cubeVerts.size() * sizeof(Vertex3D));
    auto vbResult = device.CreateBuffer(gpu_buffer_usage::VERTEX, cubeBytes);
    ASSERT_TRUE(vbResult.IsOk());
    GpuBuffer vertexBuffer = std::move(vbResult.Value());
    {
        auto transferResult = device.CreateTransferBuffer(gpu_transfer_buffer_usage::UPLOAD, cubeBytes);
        ASSERT_TRUE(transferResult.IsOk());
        GpuTransferBuffer transfer = std::move(transferResult.Value());
        {
            auto mapped = transfer.Map(false);
            ASSERT_TRUE(bool(mapped));
            std::memcpy(mapped.GetData(), cubeVerts.data(), cubeBytes);
        }
        auto uploadCmd = device.AcquireCommandBuffer();
        GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
        GpuTransferBufferLocation src{transfer.Get(), 0};
        GpuBufferRegion dst{vertexBuffer.Get(), 0, cubeBytes};
        copyPass.UploadToBuffer(src, dst, false);
        ASSERT_TRUE(uploadCmd.Submit());
        ASSERT_TRUE(device.WaitIdle());
    }

    for (int face = 0; face < 6; ++face) {
        auto cmd = device.AcquireCommandBuffer();
        GpuColorTargetInfo colorTarget{};
        colorTarget.texture = cubemap.Get();
        colorTarget.layer_or_depth_plane = uint32_t(face);
        colorTarget.load_op = gpu_load_op::DONT_CARE;
        colorTarget.store_op = gpu_store_op::STORE;
        GpuRenderPass pass = cmd.BeginRenderPass(colorTarget, nullptr);
        pass.SetViewport(GpuViewport{0.f, 0.f, float(FACE_SIZE), float(FACE_SIZE), 0.f, 1.f});
        pass.BindPipeline(pipeline);
        GpuBufferBinding binding{vertexBuffer.Get(), 0};
        pass.BindVertexBuffer(0, binding);
        cmd.PushVertexUniformData(0, CubeFaceViewProjection(face));
        GpuTextureSamplerBinding samplerBinding{equirectTexture.Get(), equirectSampler.Get()};
        pass.BindFragmentSamplers(0, {&samplerBinding, 1});
        pass.DrawPrimitives(uint32_t(cubeVerts.size()));
        ASSERT_TRUE(cmd.Submit());
        ASSERT_TRUE(device.WaitIdle());
    }

    // Téléchargement : R16G16B16A16_FLOAT ou R32G32B32A32_FLOAT selon le
    // format résolu — on lit toujours en float, la taille de texel diffère.
    bool is16 = cubemapFormat == GpuTextureFormat(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    uint32_t bytesPerTexel = is16 ? 8u : 16u;
    auto downloadResult =
        device.CreateTransferBuffer(gpu_transfer_buffer_usage::DOWNLOAD, FACE_SIZE * FACE_SIZE * bytesPerTexel);
    ASSERT_TRUE(downloadResult.IsOk());
    GpuTransferBuffer downloadBuffer = std::move(downloadResult.Value());

    // +X (u=0.5 -> R~0,B~0.5), -X (u=0/1, la couture -> R~1,B~0.5),
    // +Z (u=0.75 -> R~0.5,B~0), -Z (u=0.25 -> R~0.5,B~1) — voir le calcul
    // en tête de fichier. Tolérance généreuse (discrétisation 64 texels +
    // approximation de la face du cube unité par le shader, pas une valeur
    // BRDF exacte comme les tests shadow_smoke_test.cpp).
    struct Expectation {
        int face;
        float r, b;
    };
    Expectation expectations[4] = {
        {0, 0.0f, 0.5f}, // +X
        {1, 1.0f, 0.5f}, // -X
        {4, 0.5f, 0.0f}, // +Z
        {5, 0.5f, 1.0f}, // -Z
    };

    for (const auto &expectation : expectations) {
        auto cmd = device.AcquireCommandBuffer();
        GpuCopyPass copyPass = cmd.BeginCopyPass();
        GpuTextureRegion src{cubemap.Get(), 0, uint32_t(expectation.face), 0, 0, 0, FACE_SIZE, FACE_SIZE, 1};
        GpuTextureTransferInfo dst{downloadBuffer.Get(), 0, FACE_SIZE, FACE_SIZE};
        copyPass.DownloadFromTexture(src, dst);
        ASSERT_TRUE(cmd.Submit());
        ASSERT_TRUE(device.WaitIdle());

        auto mapped = downloadBuffer.Map(false);
        ASSERT_TRUE(bool(mapped));
        size_t centerTexel = (size_t(FACE_SIZE / 2) * FACE_SIZE + size_t(FACE_SIZE / 2));
        float r, g, b;
        if (is16) {
            const uint16_t *half = mapped.As<uint16_t>() + centerTexel * 4;
            auto toFloat = [](uint16_t h) {
                // Décodage half-float minimal (assez pour nos valeurs [0,1], pas de
                // gestion des cas spéciaux inf/nan/dénormalisés - inutiles ici).
                uint32_t sign = (uint32_t(h) & 0x8000u) << 16;
                uint32_t exponent = (uint32_t(h) >> 10) & 0x1Fu;
                uint32_t mantissa = uint32_t(h) & 0x3FFu;
                uint32_t bits;
                if (exponent == 0) {
                    bits = sign; // dénormalisé/zero -> proche de 0, suffisant ici
                } else {
                    bits = sign | ((exponent - 15u + 127u) << 23) | (mantissa << 13);
                }
                float f;
                std::memcpy(&f, &bits, sizeof(f));
                return f;
            };
            r = toFloat(half[0]);
            g = toFloat(half[1]);
            b = toFloat(half[2]);
        } else {
            const float *f = mapped.As<float>() + centerTexel * 4;
            r = f[0];
            g = f[1];
            b = f[2];
        }
        EXPECT_TRUE(std::abs(r - expectation.r) < 0.15f);
        EXPECT_TRUE(std::abs(g - 0.f) < 0.05f);
        EXPECT_TRUE(std::abs(b - expectation.b) < 0.15f);
    }

    EXPECT_TRUE(GetError().IsEmpty());
}

TEST(EnvironmentSmokeTest, IrradianceOfUniformEnvironmentMatchesSourceRadiance) {
    // Vérification à forme close (voir le plan, M12) : un environnement de
    // radiance CONSTANTE L a pour irradiance diffuse exactement L (le facteur
    // de normalisation π de l'intégrale hémisphère cosinus-pondérée annule
    // exactement le π introduit par la moyenne de Lambert — voir le
    // commentaire de BuildIrradianceConvolutionShaderProgram, cubemap.hpp).
    // Fichier .hdr synthétique minimal, gris uniforme (128,128,128,130) ->
    // scale=2^(130-136)=0.015625 -> L=(2.0,2.0,2.0) sur les 3 canaux.
    std::vector<uint8_t> file;
    AppendString(file, "#?RADIANCE\n");
    AppendString(file, "FORMAT=32-bit_rle_rgbe\n");
    AppendString(file, "\n");
    AppendString(file, "-Y 2 +X 4\n");
    for (int i = 0; i < 4 * 2; ++i)
        AppendBytes(file, {128, 128, 128, 130});
    String path("build/tests/synthetic_uniform.hdr");
    ASSERT_TRUE(WriteFile(path, file.data(), file.size()));

    auto windowResult = Window::Create(String("environment_smoke_test_irradiance"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());
    auto deviceResult = GpuDevice::Create(gpu_shader_format::SPIR_V | gpu_shader_format::MSL | gpu_shader_format::DXIL);
    ASSERT_TRUE(deviceResult.IsOk());
    GpuDevice device = std::move(deviceResult.Value());
    ASSERT_TRUE(device.ClaimWindow(MakeRef(window)));

    auto envResult = LoadEnvironmentFromHdr(device, path, /*cubemapSize=*/8, /*irradianceSize=*/4);
    ASSERT_TRUE(envResult.IsOk());
    Environment env = std::move(envResult.Value());

    bool is16 = ResolveHdrColorFormat(device) == GpuTextureFormat(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    uint32_t bytesPerTexel = is16 ? 8u : 16u;
    constexpr uint32_t IRRADIANCE_SIZE = 4;
    auto downloadResult =
        device.CreateTransferBuffer(gpu_transfer_buffer_usage::DOWNLOAD, IRRADIANCE_SIZE * IRRADIANCE_SIZE * bytesPerTexel);
    ASSERT_TRUE(downloadResult.IsOk());
    GpuTransferBuffer downloadBuffer = std::move(downloadResult.Value());

    // +X et +Z suffisent (même logique que le test de conversion ci-dessus) :
    // un environnement uniforme donne la même irradiance dans toutes les
    // directions, pas besoin de sonder les 6 faces pour le vérifier.
    for (int face : {0, 4}) {
        auto cmd = device.AcquireCommandBuffer();
        GpuCopyPass copyPass = cmd.BeginCopyPass();
        GpuTextureRegion src{env.irradianceCubemap.Get(), 0,   uint32_t(face),  0,
                             0,                            0,   IRRADIANCE_SIZE, IRRADIANCE_SIZE,
                             1};
        GpuTextureTransferInfo dst{downloadBuffer.Get(), 0, IRRADIANCE_SIZE, IRRADIANCE_SIZE};
        copyPass.DownloadFromTexture(src, dst);
        ASSERT_TRUE(cmd.Submit());
        ASSERT_TRUE(device.WaitIdle());

        auto mapped = downloadBuffer.Map(false);
        ASSERT_TRUE(bool(mapped));
        size_t centerTexel = size_t(IRRADIANCE_SIZE / 2) * IRRADIANCE_SIZE + size_t(IRRADIANCE_SIZE / 2);
        float r, g, b;
        if (is16) {
            const uint16_t *half = mapped.As<uint16_t>() + centerTexel * 4;
            auto toFloat = [](uint16_t h) {
                uint32_t sign = (uint32_t(h) & 0x8000u) << 16;
                uint32_t exponent = (uint32_t(h) >> 10) & 0x1Fu;
                uint32_t mantissa = uint32_t(h) & 0x3FFu;
                uint32_t bits = exponent == 0 ? sign : (sign | ((exponent - 15u + 127u) << 23) | (mantissa << 13));
                float f;
                std::memcpy(&f, &bits, sizeof(f));
                return f;
            };
            r = toFloat(half[0]);
            g = toFloat(half[1]);
            b = toFloat(half[2]);
        } else {
            const float *f = mapped.As<float>() + centerTexel * 4;
            r = f[0];
            g = f[1];
            b = f[2];
        }
        EXPECT_TRUE(std::abs(r - 2.0f) < 0.3f);
        EXPECT_TRUE(std::abs(g - 2.0f) < 0.3f);
        EXPECT_TRUE(std::abs(b - 2.0f) < 0.3f);
    }

    // BRDF LUT (M13) — vérification qualitative (pas de valeur exacte, comme
    // le plan le prévoit pour ce test) : les 4 coins de la LUT couvrent les 4
    // combinaisons {NdotV≈0 ou 1} x {roughness≈0 ou 1}, sans savoir a priori
    // quel coin de la texture téléchargée correspond à quelle combinaison
    // (dépend de conventions de rasterisation non vérifiées ici) — au moins
    // un coin doit ressembler à un miroir lisse en incidence normale
    // (scale≈1, biais≈0, voir le calcul en tête de commentaire du shader) et
    // au moins un coin doit montrer un terme de Fresnel fort (biais élevé,
    // signature classique des angles rasants, quel que soit le coin exact).
    {
        bool lutIs16 = ResolveBrdfLutFormat(device) == GpuTextureFormat(SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT);
        uint32_t lutBytesPerTexel = lutIs16 ? 4u : 8u;
        constexpr uint32_t LUT_SIZE = 256;
        auto lutDownloadResult =
            device.CreateTransferBuffer(gpu_transfer_buffer_usage::DOWNLOAD, LUT_SIZE * LUT_SIZE * lutBytesPerTexel);
        ASSERT_TRUE(lutDownloadResult.IsOk());
        GpuTransferBuffer lutDownloadBuffer = std::move(lutDownloadResult.Value());
        {
            auto cmd = device.AcquireCommandBuffer();
            GpuCopyPass copyPass = cmd.BeginCopyPass();
            GpuTextureRegion src{env.brdfLut.Get(), 0, 0, 0, 0, 0, LUT_SIZE, LUT_SIZE, 1};
            GpuTextureTransferInfo dst{lutDownloadBuffer.Get(), 0, LUT_SIZE, LUT_SIZE};
            copyPass.DownloadFromTexture(src, dst);
            ASSERT_TRUE(cmd.Submit());
            ASSERT_TRUE(device.WaitIdle());
        }
        auto lutMapped = lutDownloadBuffer.Map(false);
        ASSERT_TRUE(bool(lutMapped));

        auto readCorner = [&](uint32_t px, uint32_t py) -> std::pair<float, float> {
            size_t idx = size_t(py) * LUT_SIZE + size_t(px);
            if (lutIs16) {
                const uint16_t *half = lutMapped.As<uint16_t>() + idx * 2;
                auto toFloat = [](uint16_t h) {
                    uint32_t sign = (uint32_t(h) & 0x8000u) << 16;
                    uint32_t exponent = (uint32_t(h) >> 10) & 0x1Fu;
                    uint32_t mantissa = uint32_t(h) & 0x3FFu;
                    uint32_t bits =
                        exponent == 0 ? sign : (sign | ((exponent - 15u + 127u) << 23) | (mantissa << 13));
                    float f;
                    std::memcpy(&f, &bits, sizeof(f));
                    return f;
                };
                return {toFloat(half[0]), toFloat(half[1])};
            }
            const float *f = lutMapped.As<float>() + idx * 2;
            return {f[0], f[1]};
        };

        float maxA = -1.f, maxB = -1.f;
        for (uint32_t py : {0u, LUT_SIZE - 1}) {
            for (uint32_t px : {0u, LUT_SIZE - 1}) {
                auto [a, b] = readCorner(px, py);
                EXPECT_TRUE(a > -0.05f && a < 1.2f);
                EXPECT_TRUE(b > -0.05f && b < 1.2f);
                maxA = std::max(maxA, a);
                maxB = std::max(maxB, b);
            }
        }
        EXPECT_TRUE(maxA > 0.85f);
        EXPECT_TRUE(maxB > 0.15f);
    }

    EXPECT_TRUE(GetError().IsEmpty());
}

namespace {
// Pipeline triangle plein cadre pour un ShaderProgram généré par
// ShaderBuilder — même structure que BuildShadedQuadPipeline (shadow_smoke_test.cpp)
// / BuildTrianglePipeline (shader_builder_smoke_test.cpp), dupliquée ici
// (chaque fichier de test reste autonome, pattern déjà établi dans ce dépôt).
[[nodiscard]] Result<GpuGraphicsPipeline, String> BuildIblTrianglePipeline(GpuDevice &device, ShaderProgram &program,
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

TEST(EnvironmentSmokeTest, IblShadingFormulaMatchesHandComputedCombination) {
    // Isole la formule de combinaison IBL de ShadeSurface (shader_chunks.hpp,
    // #ifdef IBL_ENABLED) plutôt que de re-vérifier l'exactitude de
    // l'irradiance/du préfiltrage/de la LUT (déjà fait ci-dessus séparément) :
    // environnement de couleur UNIFORME L=(1.5,1.0,0.5) -> irradiance ET
    // préfiltré valent ~L à n'importe quel mip/direction (M12/M13), donc le
    // pixel final attendu = L * (kD + F0*scale + bias) avec kD=1-F0=0.96 (F=F0
    // exactement à NdotV=1, voir le calcul en commentaire ci-dessous) et
    // (scale,bias) lus directement dans la LUT réellement générée — vérifie
    // que ShadeSurface applique correctement le split-sum, pas que la LUT
    // elle-même est exacte (déjà couvert par IblShadingFormula... non, par
    // le test des coins de la LUT ci-dessus).
    constexpr uint32_t SIZE = 32;
    std::vector<uint8_t> file;
    AppendString(file, "#?RADIANCE\n");
    AppendString(file, "FORMAT=32-bit_rle_rgbe\n");
    AppendString(file, "\n");
    AppendString(file, "-Y 2 +X 4\n");
    // (192,128,64,129) -> scale=2^(129-136)=2^-7=1/128 -> (1.5, 1.0, 0.5)
    for (int i = 0; i < 4 * 2; ++i)
        AppendBytes(file, {192, 128, 64, 129});
    String path("build/tests/synthetic_ibl_uniform.hdr");
    ASSERT_TRUE(WriteFile(path, file.data(), file.size()));

    auto windowResult = Window::Create(String("environment_smoke_test_ibl"), int(SIZE), int(SIZE), 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());
    auto deviceResult = GpuDevice::Create(gpu_shader_format::SPIR_V | gpu_shader_format::MSL | gpu_shader_format::DXIL);
    ASSERT_TRUE(deviceResult.IsOk());
    GpuDevice device = std::move(deviceResult.Value());
    ASSERT_TRUE(device.ClaimWindow(MakeRef(window)));

    constexpr uint32_t LUT_SIZE = 256;
    auto envResult = LoadEnvironmentFromHdr(device, path, /*cubemapSize=*/8, /*irradianceSize=*/4,
                                            /*prefilterBaseSize=*/16, /*brdfLutSize=*/LUT_SIZE);
    ASSERT_TRUE(envResult.IsOk());
    Environment env = std::move(envResult.Value());

    // Lit le (scale,bias) réel à (NdotV≈1, roughness=0.5) — texel (255,128)
    // pour une LUT 256x256 (u=NdotV, v=roughness, voir BuildBrdfLutShaderProgram).
    bool lutIs16 = ResolveBrdfLutFormat(device) == GpuTextureFormat(SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT);
    uint32_t lutBytesPerTexel = lutIs16 ? 4u : 8u;
    float lutScale, lutBias;
    {
        auto downloadResult =
            device.CreateTransferBuffer(gpu_transfer_buffer_usage::DOWNLOAD, LUT_SIZE * LUT_SIZE * lutBytesPerTexel);
        ASSERT_TRUE(downloadResult.IsOk());
        GpuTransferBuffer downloadBuffer = std::move(downloadResult.Value());
        auto cmd = device.AcquireCommandBuffer();
        GpuCopyPass copyPass = cmd.BeginCopyPass();
        GpuTextureRegion src{env.brdfLut.Get(), 0, 0, 0, 0, 0, LUT_SIZE, LUT_SIZE, 1};
        GpuTextureTransferInfo dst{downloadBuffer.Get(), 0, LUT_SIZE, LUT_SIZE};
        copyPass.DownloadFromTexture(src, dst);
        ASSERT_TRUE(cmd.Submit());
        ASSERT_TRUE(device.WaitIdle());
        auto mapped = downloadBuffer.Map(false);
        ASSERT_TRUE(bool(mapped));
        size_t idx = size_t(128) * LUT_SIZE + size_t(255);
        if (lutIs16) {
            const uint16_t *half = mapped.As<uint16_t>() + idx * 2;
            auto toFloat = [](uint16_t h) {
                uint32_t sign = (uint32_t(h) & 0x8000u) << 16;
                uint32_t exponent = (uint32_t(h) >> 10) & 0x1Fu;
                uint32_t mantissa = uint32_t(h) & 0x3FFu;
                uint32_t bits = exponent == 0 ? sign : (sign | ((exponent - 15u + 127u) << 23) | (mantissa << 13));
                float f;
                std::memcpy(&f, &bits, sizeof(f));
                return f;
            };
            lutScale = toFloat(half[0]);
            lutBias = toFloat(half[1]);
        } else {
            const float *f = mapped.As<float>() + idx * 2;
            lutScale = f[0];
            lutBias = f[1];
        }
    }

    // F0=0.04 (dielectrique), F=FresnelSchlickRoughness(NdotV=1,F0,r) = F0
    // exactement (pow(1-NdotV,5)=pow(0,5)=0 à NdotV=1, quel que soit r) ->
    // kD=(1-F0)*(1-metallic)=0.96.
    constexpr float F0 = 0.04f;
    constexpr float KD = 1.f - F0;
    float expectedR = 1.5f * (KD + F0 * lutScale + lutBias);
    float expectedG = 1.0f * (KD + F0 * lutScale + lutBias);
    float expectedB = 0.5f * (KD + F0 * lutScale + lutBias);

    auto programResult = ShaderBuilder()
                             .Color(FColor{1.f, 1.f, 1.f, 1.f})
                             .Lit(LightingModel::PBR)
                             .Metallic(0.f)
                             .Roughness(0.5f)
                             .Environment(true)
                             .Build(device);
    ASSERT_TRUE(programResult.IsOk());
    ShaderProgram program = std::move(programResult.Value());

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
    GpuTextureCreateInfo depthInfo = colorInfo;
    depthInfo.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    depthInfo.usage = gpu_texture_usage::DEPTH_STENCIL_TARGET;
    auto depthResult = device.CreateTexture(depthInfo);
    ASSERT_TRUE(depthResult.IsOk());
    GpuTexture depthTexture = std::move(depthResult.Value());

    auto pipelineResult = BuildIblTrianglePipeline(device, program, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                                   SDL_GPU_TEXTUREFORMAT_D32_FLOAT);
    ASSERT_TRUE(pipelineResult.IsOk());
    GpuGraphicsPipeline pipeline = std::move(pipelineResult.Value());

    // Triangle plein cadre face à la caméra, normale (0,0,-1) -> N=V=(0,0,-1)
    // avec cameraPos=(0,0,-1) et worldPos=(x,y,0) : NdotV=1 exactement.
    Vertex3D triangle[3] = {
        {{-2.f, -2.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
        {{2.f, -2.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
        {{0.f, 2.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, Color::WHITE()},
    };
    auto vbResult = device.CreateBuffer(gpu_buffer_usage::VERTEX, uint32_t(sizeof(triangle)));
    ASSERT_TRUE(vbResult.IsOk());
    GpuBuffer vertexBuffer = std::move(vbResult.Value());
    {
        auto tbResult = device.CreateTransferBuffer(gpu_transfer_buffer_usage::UPLOAD, uint32_t(sizeof(triangle)));
        ASSERT_TRUE(tbResult.IsOk());
        GpuTransferBuffer transfer = std::move(tbResult.Value());
        {
            auto mapped = transfer.Map(false);
            ASSERT_TRUE(bool(mapped));
            std::memcpy(mapped.GetData(), triangle, sizeof(triangle));
        }
        auto uploadCmd = device.AcquireCommandBuffer();
        GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
        GpuTransferBufferLocation src{transfer.Get(), 0};
        GpuBufferRegion dst{vertexBuffer.Get(), 0, uint32_t(sizeof(triangle))};
        copyPass.UploadToBuffer(src, dst, false);
        ASSERT_TRUE(uploadCmd.Submit());
        ASSERT_TRUE(device.WaitIdle());
    }

    // Sampler albedo (useTexture=0, jamais échantillonné mais requis).
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

    GpuSamplerCreateInfo iblSamplerInfo{};
    iblSamplerInfo.min_filter = gpu_filter::LINEAR;
    iblSamplerInfo.mag_filter = gpu_filter::LINEAR;
    iblSamplerInfo.mipmap_mode = gpu_sampler_mipmap_mode::LINEAR;
    iblSamplerInfo.address_mode_u = gpu_sampler_address_mode::CLAMP_TO_EDGE;
    iblSamplerInfo.address_mode_v = gpu_sampler_address_mode::CLAMP_TO_EDGE;
    iblSamplerInfo.address_mode_w = gpu_sampler_address_mode::CLAMP_TO_EDGE;
    auto iblSamplerResult = device.CreateSampler(iblSamplerInfo);
    ASSERT_TRUE(iblSamplerResult.IsOk());
    GpuSampler iblSampler = std::move(iblSamplerResult.Value());

    struct MaterialUBO {
        float baseColor[4];
        float emissive[4];
        float roughness, metallic, useTexture, useIBL;
    };
    MaterialUBO material{{1.f, 1.f, 1.f, 1.f}, {0.f, 0.f, 0.f, 0.f}, 0.5f, 0.f, 0.f, 1.f};

    struct LightUBO {
        float lightDir[3];
        float pad1;
        float lightColor[4];
        float ambientColor[4];
        float cameraPos[3];
        float pad2;
    };
    LightUBO lightUbo{}; // uLightColor=0, uAmbientColor=0 : isole la contribution IBL
    lightUbo.cameraPos[2] = -1.f;

    auto frameCmd = device.AcquireCommandBuffer();
    GpuColorTargetInfo colorTarget{};
    colorTarget.texture = colorTexture.Get();
    colorTarget.clear_color = SDL_FColor{0.f, 0.f, 0.f, 1.f};
    colorTarget.load_op = gpu_load_op::CLEAR;
    colorTarget.store_op = gpu_store_op::STORE;
    GpuDepthStencilTargetInfo depthTarget{};
    depthTarget.texture = depthTexture.Get();
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
        float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        struct PerObjectUBO {
            float model[16];
            float normalMatrix[16];
        };
        PerObjectUBO perObject{};
        std::memcpy(perObject.model, identity, sizeof(identity));
        std::memcpy(perObject.normalMatrix, identity, sizeof(identity));
        frameCmd.PushVertexUniformData(0, identity);
        frameCmd.PushVertexUniformData(1, perObject);
        frameCmd.PushFragmentUniformData(0, material);
        frameCmd.PushFragmentUniformData(1, lightUbo);

        std::array<GpuTextureSamplerBinding, 4> samplerBindings{};
        samplerBindings[0] = {whiteTexture.Get(), albedoSampler.Get()};
        samplerBindings[1] = {env.irradianceCubemap.Get(), iblSampler.Get()};
        samplerBindings[2] = {env.prefilteredCubemap.Get(), iblSampler.Get()};
        samplerBindings[3] = {env.brdfLut.Get(), iblSampler.Get()};
        pass.BindFragmentSamplers(0, samplerBindings);

        pass.DrawPrimitives(3);
    }
    ASSERT_TRUE(frameCmd.Submit());
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
    int idx = (int(SIZE / 2) * int(SIZE) + int(SIZE / 2)) * 4;
    int expectedByteR = int(std::clamp(expectedR, 0.f, 1.f) * 255.f);
    int expectedByteG = int(std::clamp(expectedG, 0.f, 1.f) * 255.f);
    int expectedByteB = int(std::clamp(expectedB, 0.f, 1.f) * 255.f);
    // Tolérance généreuse : bruit Monte-Carlo de l'irradiance/du préfiltrage
    // (échantillonnage fini, voir cubemap.hpp) + résolutions réduites ici
    // (cubemapSize=8 etc., pour la vitesse du test).
    EXPECT_TRUE(std::abs(int(pixels[idx + 0]) - expectedByteR) <= 15);
    EXPECT_TRUE(std::abs(int(pixels[idx + 1]) - expectedByteG) <= 15);
    EXPECT_TRUE(std::abs(int(pixels[idx + 2]) - expectedByteB) <= 15);

    EXPECT_TRUE(GetError().IsEmpty());
}

int main() { return RUN_ALL_TESTS(); }
