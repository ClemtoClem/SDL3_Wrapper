// Smoke test : render3d::CompileGlslToSpirv/CrossCompileSpirvToMsl/CompileShader
// (lib/render3d/shader_compiler.hpp) — compilateur GLSL embarqué (shaderc +
// spirv-cross) utilisé par ShaderBuilder, validé bout en bout AVANT de lui
// faire confiance pour composer des shaders dynamiques.
#define USE_TEST
#include "core/test.hpp"
#include "sdl3/sdl3.hpp"
#include "render3d/shader_compiler.hpp"

using namespace sdl3;
using namespace render3d;

TEST(ShaderCompilerSmokeTest, GlslToSpirvToMsl) {
    static constexpr const char *VERTEX_SRC = R"(#version 450
layout(location = 0) in vec2 inPosition;
layout(set = 1, binding = 0) uniform UBO { mat4 uMvp; };
void main() { gl_Position = uMvp * vec4(inPosition, 0.0, 1.0); }
)";
    static constexpr const char *FRAGMENT_SRC = R"(#version 450
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(1.0, 0.0, 0.0, 1.0); }
)";

    auto vertexSpirv = CompileGlslToSpirv(StringView(VERTEX_SRC), gpu_shader_stage::VERTEX, StringView("test.vert"));
    ASSERT_TRUE(vertexSpirv.IsOk());
    EXPECT_TRUE(!vertexSpirv.Value().empty());

    auto fragmentSpirv =
        CompileGlslToSpirv(StringView(FRAGMENT_SRC), gpu_shader_stage::FRAGMENT, StringView("test.frag"));
    ASSERT_TRUE(fragmentSpirv.IsOk());
    EXPECT_TRUE(!fragmentSpirv.Value().empty());

    auto msl = CrossCompileSpirvToMsl(fragmentSpirv.Value());
    ASSERT_TRUE(msl.IsOk());
    EXPECT_TRUE(msl.Value().Contains("main0"));

    // Un GLSL invalide doit échouer proprement (Result::IsError), pas planter.
    auto bad = CompileGlslToSpirv(StringView("not valid glsl {{{"), gpu_shader_stage::FRAGMENT, StringView("bad"));
    EXPECT_TRUE(bad.IsError());
}

TEST(ShaderCompilerSmokeTest, CompileShaderEndToEndPipeline) {
    auto ctx = SdlContext::Create(init_flags::VIDEO);
    ASSERT_TRUE(ctx.IsOk());

    auto windowResult = Window::Create(String("shader_compiler_smoke_test"), 64, 64, 0);
    ASSERT_TRUE(windowResult.IsOk());
    Window window = std::move(windowResult.Value());

    auto deviceResult =
        GpuDevice::Create(gpu_shader_format::SPIR_V | gpu_shader_format::MSL | gpu_shader_format::DXIL);
    ASSERT_TRUE(deviceResult.IsOk());
    GpuDevice device = std::move(deviceResult.Value());
    ASSERT_TRUE(device.ClaimWindow(MakeRef(window)));

    static constexpr const char *VERTEX_SRC = R"(#version 450
layout(location = 0) in vec2 inPosition;
void main() { gl_Position = vec4(inPosition, 0.0, 1.0); }
)";
    static constexpr const char *FRAGMENT_SRC = R"(#version 450
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(0.0, 1.0, 0.0, 1.0); }
)";

    auto vertexShader = CompileShader(device, StringView(VERTEX_SRC), gpu_shader_stage::VERTEX,
                                       StringView("smoke.vert"), 0, 0);
    ASSERT_TRUE(vertexShader.IsOk());
    auto fragmentShader = CompileShader(device, StringView(FRAGMENT_SRC), gpu_shader_stage::FRAGMENT,
                                         StringView("smoke.frag"), 0, 0);
    ASSERT_TRUE(fragmentShader.IsOk());

    GpuVertexBufferDescription vertexBufferDesc{0, sizeof(float) * 2, gpu_vertex_input_rate::VERTEX, 0};
    GpuVertexAttribute vertexAttribute{0, 0, gpu_vertex_element_format::FLOAT2, 0};

    GpuColorTargetDescription colorTargetDesc{};
    colorTargetDesc.format = device.SwapchainTextureFormat(MakeRef(window));

    GpuGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = vertexShader.Value().Get();
    pipelineInfo.fragment_shader = fragmentShader.Value().Get();
    pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vertexBufferDesc;
    pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
    pipelineInfo.vertex_input_state.vertex_attributes = &vertexAttribute;
    pipelineInfo.vertex_input_state.num_vertex_attributes = 1;
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

    float triangle[6] = {0.f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f};
    auto vertexBufferResult = device.CreateBuffer(gpu_buffer_usage::VERTEX, uint32_t(sizeof(triangle)));
    ASSERT_TRUE(vertexBufferResult.IsOk());
    GpuBuffer vertexBuffer = std::move(vertexBufferResult.Value());

    auto transferResult = device.CreateTransferBuffer(gpu_transfer_buffer_usage::UPLOAD, uint32_t(sizeof(triangle)));
    ASSERT_TRUE(transferResult.IsOk());
    GpuTransferBuffer transfer = std::move(transferResult.Value());
    {
        auto mapped = transfer.Map(false);
        ASSERT_TRUE(bool(mapped));
        std::memcpy(mapped.GetData(), triangle, sizeof(triangle));
    }

    auto uploadCmd = device.AcquireCommandBuffer();
    {
        GpuCopyPass copyPass = uploadCmd.BeginCopyPass();
        GpuTransferBufferLocation src{transfer.Get(), 0};
        GpuBufferRegion dst{vertexBuffer.Get(), 0, uint32_t(sizeof(triangle))};
        copyPass.UploadToBuffer(src, dst, false);
    }
    ASSERT_TRUE(uploadCmd.Submit());
    ASSERT_TRUE(device.WaitIdle());

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
