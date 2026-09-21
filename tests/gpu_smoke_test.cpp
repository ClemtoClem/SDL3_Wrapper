// Smoke test : sdl3::Gpu* (lib/sdl3/gpu.hpp) — validation bout en bout du
// pipeline GPU minimal (device, buffer, shader, pipeline, une frame réelle)
// AVANT de faire confiance à la couche render3d:: construite dessus. Utilise
// directement shape2d.vert/shape2d_solid.frag (le pipeline 2D le plus
// simple) plutôt que render3d:: — objectif : isoler une éventuelle panne du
// driver GPU d'une panne de la couche plus haute.
#define USE_TEST
#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include <cstddef>
#include <cstring>

using namespace sdl3;

TEST(GpuSmokeTest, EndToEndTriangle) {
    auto ctx = SdlContext::Create(init_flags::VIDEO);
    ASSERT_TRUE(ctx.IsOk());

    auto windowResult = Window::Create(String("gpu_smoke_test"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());

    auto deviceResult =
        GpuDevice::Create(gpu_shader_format::SPIR_V | gpu_shader_format::MSL | gpu_shader_format::DXIL);
    ASSERT_TRUE(deviceResult.IsOk());
    GpuDevice device = std::move(deviceResult.Value());

    ASSERT_TRUE(device.ClaimWindow(MakeRef(window)));

    GpuShaderFormat available = device.ShaderFormats();
    const char *ext = (available & gpu_shader_format::SPIR_V) ? ".spv" : ".msl";
    const char *entrypoint = (available & gpu_shader_format::SPIR_V) ? "main" : "main0";
    GpuShaderFormat format = (available & gpu_shader_format::SPIR_V) ? gpu_shader_format::SPIR_V : gpu_shader_format::MSL;

    auto vertexBytes = ReadFile(String("assets/shaders/bin/shape2d.vert") + ext);
    ASSERT_TRUE(vertexBytes.IsOk());
    auto fragmentBytes = ReadFile(String("assets/shaders/bin/shape2d_solid.frag") + ext);
    ASSERT_TRUE(fragmentBytes.IsOk());

    GpuShaderCreateInfo vertexInfo{};
    vertexInfo.code_size = vertexBytes.Value().size();
    vertexInfo.code = vertexBytes.Value().data();
    vertexInfo.entrypoint = entrypoint;
    vertexInfo.format = format;
    vertexInfo.stage = gpu_shader_stage::VERTEX;
    vertexInfo.num_uniform_buffers = 1;
    auto vertexShader = device.CreateShader(vertexInfo);
    ASSERT_TRUE(vertexShader.IsOk());

    GpuShaderCreateInfo fragmentInfo{};
    fragmentInfo.code_size = fragmentBytes.Value().size();
    fragmentInfo.code = fragmentBytes.Value().data();
    fragmentInfo.entrypoint = entrypoint;
    fragmentInfo.format = format;
    fragmentInfo.stage = gpu_shader_stage::FRAGMENT;
    auto fragmentShader = device.CreateShader(fragmentInfo);
    ASSERT_TRUE(fragmentShader.IsOk());

    struct Vertex2D {
        float x, y;
        float r, g, b, a;
        float u, v;
    };

    GpuVertexBufferDescription vertexBufferDesc{0, uint32_t(sizeof(Vertex2D)), gpu_vertex_input_rate::VERTEX, 0};
    GpuVertexAttribute vertexAttributes[3] = {
        {0, 0, gpu_vertex_element_format::FLOAT2, 0},
        {1, 0, gpu_vertex_element_format::FLOAT4, uint32_t(offsetof(Vertex2D, r))},
        {2, 0, gpu_vertex_element_format::FLOAT2, uint32_t(offsetof(Vertex2D, u))},
    };

    GpuColorTargetDescription colorTargetDesc{};
    colorTargetDesc.format = device.SwapchainTextureFormat(MakeRef(window));

    GpuGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = vertexShader.Value().Get();
    pipelineInfo.fragment_shader = fragmentShader.Value().Get();
    pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vertexBufferDesc;
    pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
    pipelineInfo.vertex_input_state.vertex_attributes = vertexAttributes;
    pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
    pipelineInfo.primitive_type = gpu_primitive_type::TRIANGLE_LIST;
    pipelineInfo.rasterizer_state.fill_mode = gpu_fill_mode::FILL;
    pipelineInfo.rasterizer_state.cull_mode = gpu_cull_mode::NONE;
    pipelineInfo.rasterizer_state.front_face = gpu_front_face::COUNTER_CLOCKWISE;
    pipelineInfo.multisample_state.sample_count = gpu_sample_count::SAMPLE1;
    pipelineInfo.target_info.color_target_descriptions = &colorTargetDesc;
    pipelineInfo.target_info.num_color_targets = 1;

    auto pipelineResult = device.CreateGraphicsPipeline(pipelineInfo);
    ASSERT_TRUE(pipelineResult.IsOk());
    GpuGraphicsPipeline pipeline = std::move(pipelineResult.Value());

    Vertex2D triangle[3] = {
        {0.f, -0.5f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f},
        {0.5f, 0.5f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f},
        {-0.5f, 0.5f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f},
    };
    uint32_t vertexBytesSize = uint32_t(sizeof(triangle));

    auto vertexBufferResult = device.CreateBuffer(gpu_buffer_usage::VERTEX, vertexBytesSize);
    ASSERT_TRUE(vertexBufferResult.IsOk());
    GpuBuffer vertexBuffer = std::move(vertexBufferResult.Value());

    auto transferResult = device.CreateTransferBuffer(gpu_transfer_buffer_usage::UPLOAD, vertexBytesSize);
    ASSERT_TRUE(transferResult.IsOk());
    GpuTransferBuffer transfer = std::move(transferResult.Value());
    {
        auto mapped = transfer.Map(false);
        ASSERT_TRUE(bool(mapped));
        std::memcpy(mapped.GetData(), triangle, vertexBytesSize);
    }

    auto uploadCmd = device.AcquireCommandBuffer();
    {
        GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
        GpuTransferBufferLocation src{transfer.Get(), 0};
        GpuBufferRegion dst{vertexBuffer.Get(), 0, vertexBytesSize};
        copyPass.UploadToBuffer(src, dst, false);
    }
    ASSERT_TRUE(uploadCmd.Submit());
    ASSERT_TRUE(device.WaitIdle());

    float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    // AcquireSwapchainTexture (non bloquant) plutôt que WaitAndAcquire : une
    // fenêtre qui ne serait jamais présentée par le compositeur ne doit pas
    // faire bloquer indéfiniment un test automatisé.
    auto frameCmd = device.AcquireCommandBuffer();
    auto swapchain = frameCmd.AcquireSwapchainTexture(MakeRef(window));
    if (swapchain.IsSome()) {
        GpuColorTargetInfo colorTarget{};
        colorTarget.texture = swapchain.Value().Get();
        colorTarget.clear_color = SDL_FColor{0.f, 0.f, 0.f, 1.f};
        colorTarget.load_op = gpu_load_op::CLEAR;
        colorTarget.store_op = gpu_store_op::STORE;

        {
            GpuRenderPass pass = frameCmd.BeginRenderPass(colorTarget);
            pass.BindPipeline(pipeline);
            GpuBufferBinding binding{vertexBuffer.Get(), 0};
            pass.BindVertexBuffer(0, binding);
            frameCmd.PushVertexUniformData(0, identity);
            pass.DrawPrimitives(3);
        }
        EXPECT_TRUE(frameCmd.Submit());
    } else {
        EXPECT_TRUE(frameCmd.Cancel());
    }

    EXPECT_TRUE(device.WaitIdle());
    EXPECT_TRUE(GetError().IsEmpty());
}

int main() {
    return RUN_ALL_TESTS();
}
