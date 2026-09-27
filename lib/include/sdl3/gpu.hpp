#pragma once
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <cstring>
#include <span>
#include <vector>

#include "../core/core.hpp"
#include "render.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// Type aliases — use SDL types directly in the sdl3 namespace
// ============================================================================

using GpuTextureFormat = SDL_GPUTextureFormat;
using GpuTextureType = SDL_GPUTextureType;
using GpuTextureUsageFlags = SDL_GPUTextureUsageFlags;
using GpuSampleCount = SDL_GPUSampleCount;
using GpuCubeMapFace = SDL_GPUCubeMapFace;
using GpuBufferUsageFlags = SDL_GPUBufferUsageFlags;
using GpuTransferBufferUsage = SDL_GPUTransferBufferUsage;
using GpuShaderFormat = SDL_GPUShaderFormat;
using GpuShaderStage = SDL_GPUShaderStage;
using GpuVertexElementFormat = SDL_GPUVertexElementFormat;
using GpuVertexInputRate = SDL_GPUVertexInputRate;
using GpuPrimitiveType = SDL_GPUPrimitiveType;
using GpuFillMode = SDL_GPUFillMode;
using GpuCullMode = SDL_GPUCullMode;
using GpuFrontFace = SDL_GPUFrontFace;
using GpuCompareOp = SDL_GPUCompareOp;
using GpuStencilOp = SDL_GPUStencilOp;
using GpuBlendOp = SDL_GPUBlendOp;
using GpuBlendFactor = SDL_GPUBlendFactor;
using GpuColorComponentFlags = SDL_GPUColorComponentFlags;
using GpuFilter = SDL_GPUFilter;
using GpuSamplerMipmapMode = SDL_GPUSamplerMipmapMode;
using GpuSamplerAddressMode = SDL_GPUSamplerAddressMode;
using GpuIndexElementSize = SDL_GPUIndexElementSize;
using GpuLoadOp = SDL_GPULoadOp;
using GpuStoreOp = SDL_GPUStoreOp;
using GpuSwapchainComposition = SDL_GPUSwapchainComposition;
using GpuPresentMode = SDL_GPUPresentMode;

// ============================================================================
// Enum / flag constants, namespaced per value (mirrors render.hpp's PixelFormat::)
// ============================================================================

namespace gpu_shader_format {
constexpr GpuShaderFormat INVALID = SDL_GPU_SHADERFORMAT_INVALID;
constexpr GpuShaderFormat PRIVATE = SDL_GPU_SHADERFORMAT_PRIVATE;
constexpr GpuShaderFormat SPIR_V = SDL_GPU_SHADERFORMAT_SPIRV;
constexpr GpuShaderFormat DXBC = SDL_GPU_SHADERFORMAT_DXBC;
constexpr GpuShaderFormat DXIL = SDL_GPU_SHADERFORMAT_DXIL;
constexpr GpuShaderFormat MSL = SDL_GPU_SHADERFORMAT_MSL;
constexpr GpuShaderFormat METAL_LIB = SDL_GPU_SHADERFORMAT_METALLIB;
} // namespace gpu_shader_format

namespace gpu_texture_usage {
constexpr GpuTextureUsageFlags SAMPLER = SDL_GPU_TEXTUREUSAGE_SAMPLER;
constexpr GpuTextureUsageFlags COLOR_TARGET = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
constexpr GpuTextureUsageFlags DEPTH_STENCIL_TARGET = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
constexpr GpuTextureUsageFlags GRAPHICS_STORAGE_READ = SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ;
constexpr GpuTextureUsageFlags COMPUTE_STORAGE_READ = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ;
constexpr GpuTextureUsageFlags COMPUTE_STORAGE_WRITE = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
constexpr GpuTextureUsageFlags COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE =
    SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE;
} // namespace gpu_texture_usage

namespace gpu_buffer_usage {
constexpr GpuBufferUsageFlags VERTEX = SDL_GPU_BUFFERUSAGE_VERTEX;
constexpr GpuBufferUsageFlags INDEX = SDL_GPU_BUFFERUSAGE_INDEX;
constexpr GpuBufferUsageFlags INDIRECT = SDL_GPU_BUFFERUSAGE_INDIRECT;
constexpr GpuBufferUsageFlags GRAPHICS_STORAGE_READ = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
constexpr GpuBufferUsageFlags COMPUTE_STORAGE_READ = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
constexpr GpuBufferUsageFlags COMPUTE_STORAGE_WRITE = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
} // namespace gpu_buffer_usage

namespace gpu_color_component {
constexpr GpuColorComponentFlags R = SDL_GPU_COLORCOMPONENT_R;
constexpr GpuColorComponentFlags G = SDL_GPU_COLORCOMPONENT_G;
constexpr GpuColorComponentFlags B = SDL_GPU_COLORCOMPONENT_B;
constexpr GpuColorComponentFlags A = SDL_GPU_COLORCOMPONENT_A;
} // namespace gpu_color_component

namespace gpu_primitive_type {
constexpr GpuPrimitiveType TRIANGLE_LIST = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
constexpr GpuPrimitiveType TRIANGLE_STRIP = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
constexpr GpuPrimitiveType LINE_LIST = SDL_GPU_PRIMITIVETYPE_LINELIST;
constexpr GpuPrimitiveType LINE_STRIP = SDL_GPU_PRIMITIVETYPE_LINESTRIP;
constexpr GpuPrimitiveType POINT_LIST = SDL_GPU_PRIMITIVETYPE_POINTLIST;
} // namespace gpu_primitive_type

namespace gpu_texture_type {
constexpr GpuTextureType TEXTURE2_D = SDL_GPU_TEXTURETYPE_2D;
constexpr GpuTextureType TEXTURE2_D_ARRAY = SDL_GPU_TEXTURETYPE_2D_ARRAY;
constexpr GpuTextureType TEXTURE3_D = SDL_GPU_TEXTURETYPE_3D;
constexpr GpuTextureType CUBE = SDL_GPU_TEXTURETYPE_CUBE;
constexpr GpuTextureType CUBE_ARRAY = SDL_GPU_TEXTURETYPE_CUBE_ARRAY;
} // namespace gpu_texture_type

namespace gpu_sample_count {
constexpr GpuSampleCount SAMPLE1 = SDL_GPU_SAMPLECOUNT_1;
constexpr GpuSampleCount SAMPLE2 = SDL_GPU_SAMPLECOUNT_2;
constexpr GpuSampleCount SAMPLE4 = SDL_GPU_SAMPLECOUNT_4;
constexpr GpuSampleCount SAMPLE8 = SDL_GPU_SAMPLECOUNT_8;
} // namespace gpu_sample_count

namespace gpu_cubemap_face {
constexpr GpuCubeMapFace POSITIVE_X = SDL_GPU_CUBEMAPFACE_POSITIVEX;
constexpr GpuCubeMapFace NEGATIVE_X = SDL_GPU_CUBEMAPFACE_NEGATIVEX;
constexpr GpuCubeMapFace POSITIVE_Y = SDL_GPU_CUBEMAPFACE_POSITIVEY;
constexpr GpuCubeMapFace NEGATIVE_Y = SDL_GPU_CUBEMAPFACE_NEGATIVEY;
constexpr GpuCubeMapFace POSITIVE_Z = SDL_GPU_CUBEMAPFACE_POSITIVEZ;
constexpr GpuCubeMapFace NEGATIVE_Z = SDL_GPU_CUBEMAPFACE_NEGATIVEZ;
} // namespace gpu_cubemap_face

namespace gpu_transfer_buffer_usage {
constexpr GpuTransferBufferUsage UPLOAD = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
constexpr GpuTransferBufferUsage DOWNLOAD = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
} // namespace gpu_transfer_buffer_usage

namespace gpu_shader_stage {
constexpr GpuShaderStage VERTEX = SDL_GPU_SHADERSTAGE_VERTEX;
constexpr GpuShaderStage FRAGMENT = SDL_GPU_SHADERSTAGE_FRAGMENT;
} // namespace gpu_shader_stage

namespace gpu_vertex_element_format {
constexpr GpuVertexElementFormat INVALID = SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
constexpr GpuVertexElementFormat INT = SDL_GPU_VERTEXELEMENTFORMAT_INT;
constexpr GpuVertexElementFormat INT2 = SDL_GPU_VERTEXELEMENTFORMAT_INT2;
constexpr GpuVertexElementFormat INT3 = SDL_GPU_VERTEXELEMENTFORMAT_INT3;
constexpr GpuVertexElementFormat INT4 = SDL_GPU_VERTEXELEMENTFORMAT_INT4;
constexpr GpuVertexElementFormat UINT = SDL_GPU_VERTEXELEMENTFORMAT_UINT;
constexpr GpuVertexElementFormat UINT2 = SDL_GPU_VERTEXELEMENTFORMAT_UINT2;
constexpr GpuVertexElementFormat UINT3 = SDL_GPU_VERTEXELEMENTFORMAT_UINT3;
constexpr GpuVertexElementFormat UINT4 = SDL_GPU_VERTEXELEMENTFORMAT_UINT4;
constexpr GpuVertexElementFormat FLOAT = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
constexpr GpuVertexElementFormat FLOAT2 = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
constexpr GpuVertexElementFormat FLOAT3 = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
constexpr GpuVertexElementFormat FLOAT4 = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
constexpr GpuVertexElementFormat BYTE2 = SDL_GPU_VERTEXELEMENTFORMAT_BYTE2;
constexpr GpuVertexElementFormat BYTE4 = SDL_GPU_VERTEXELEMENTFORMAT_BYTE4;
constexpr GpuVertexElementFormat UBYTE2 = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2;
constexpr GpuVertexElementFormat UBYTE4 = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4;
constexpr GpuVertexElementFormat BYTE2_NORM = SDL_GPU_VERTEXELEMENTFORMAT_BYTE2_NORM;
constexpr GpuVertexElementFormat BYTE4_NORM = SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM;
constexpr GpuVertexElementFormat UBYTE2_NORM = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2_NORM;
constexpr GpuVertexElementFormat UBYTE4_NORM = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
constexpr GpuVertexElementFormat SHORT2 = SDL_GPU_VERTEXELEMENTFORMAT_SHORT2;
constexpr GpuVertexElementFormat SHORT4 = SDL_GPU_VERTEXELEMENTFORMAT_SHORT4;
constexpr GpuVertexElementFormat USHORT2 = SDL_GPU_VERTEXELEMENTFORMAT_USHORT2;
constexpr GpuVertexElementFormat USHORT4 = SDL_GPU_VERTEXELEMENTFORMAT_USHORT4;
constexpr GpuVertexElementFormat SHORT2_NORM = SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM;
constexpr GpuVertexElementFormat SHORT4_NORM = SDL_GPU_VERTEXELEMENTFORMAT_SHORT4_NORM;
constexpr GpuVertexElementFormat USHORT2_NORM = SDL_GPU_VERTEXELEMENTFORMAT_USHORT2_NORM;
constexpr GpuVertexElementFormat USHORT4_NORM = SDL_GPU_VERTEXELEMENTFORMAT_USHORT4_NORM;
constexpr GpuVertexElementFormat HALF2 = SDL_GPU_VERTEXELEMENTFORMAT_HALF2;
constexpr GpuVertexElementFormat HALF4 = SDL_GPU_VERTEXELEMENTFORMAT_HALF4;
} // namespace gpu_vertex_element_format

namespace gpu_vertex_input_rate {
constexpr GpuVertexInputRate VERTEX = SDL_GPU_VERTEXINPUTRATE_VERTEX;
constexpr GpuVertexInputRate INSTANCE = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
} // namespace gpu_vertex_input_rate

namespace gpu_fill_mode {
constexpr GpuFillMode FILL = SDL_GPU_FILLMODE_FILL;
constexpr GpuFillMode LINE = SDL_GPU_FILLMODE_LINE;
} // namespace gpu_fill_mode

namespace gpu_cull_mode {
constexpr GpuCullMode NONE = SDL_GPU_CULLMODE_NONE;
constexpr GpuCullMode FRONT = SDL_GPU_CULLMODE_FRONT;
constexpr GpuCullMode BACK = SDL_GPU_CULLMODE_BACK;
} // namespace gpu_cull_mode

namespace gpu_front_face {
constexpr GpuFrontFace COUNTER_CLOCKWISE = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
constexpr GpuFrontFace CLOCKWISE = SDL_GPU_FRONTFACE_CLOCKWISE;
} // namespace gpu_front_face

namespace gpu_compare_op {
constexpr GpuCompareOp INVALID = SDL_GPU_COMPAREOP_INVALID;
constexpr GpuCompareOp NEVER = SDL_GPU_COMPAREOP_NEVER;
constexpr GpuCompareOp LESS = SDL_GPU_COMPAREOP_LESS;
constexpr GpuCompareOp EQUAL = SDL_GPU_COMPAREOP_EQUAL;
constexpr GpuCompareOp LESS_OR_EQUAL = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
constexpr GpuCompareOp GREATER = SDL_GPU_COMPAREOP_GREATER;
constexpr GpuCompareOp NOT_EQUAL = SDL_GPU_COMPAREOP_NOT_EQUAL;
constexpr GpuCompareOp GREATER_OR_EQUAL = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
constexpr GpuCompareOp ALWAYS = SDL_GPU_COMPAREOP_ALWAYS;
} // namespace gpu_compare_op

namespace gpu_stencil_op {
constexpr GpuStencilOp INVALID = SDL_GPU_STENCILOP_INVALID;
constexpr GpuStencilOp KEEP = SDL_GPU_STENCILOP_KEEP;
constexpr GpuStencilOp ZERO = SDL_GPU_STENCILOP_ZERO;
constexpr GpuStencilOp REPLACE = SDL_GPU_STENCILOP_REPLACE;
constexpr GpuStencilOp INCREMENT_AND_CLAMP = SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP;
constexpr GpuStencilOp DECREMENT_AND_CLAMP = SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP;
constexpr GpuStencilOp INVERT = SDL_GPU_STENCILOP_INVERT;
constexpr GpuStencilOp INCREMENT_AND_WRAP = SDL_GPU_STENCILOP_INCREMENT_AND_WRAP;
constexpr GpuStencilOp DECREMENT_AND_WRAP = SDL_GPU_STENCILOP_DECREMENT_AND_WRAP;
} // namespace gpu_stencil_op

namespace gpu_blend_op {
constexpr GpuBlendOp INVALID = SDL_GPU_BLENDOP_INVALID;
constexpr GpuBlendOp ADD = SDL_GPU_BLENDOP_ADD;
constexpr GpuBlendOp SUBTRACT = SDL_GPU_BLENDOP_SUBTRACT;
constexpr GpuBlendOp REVERSE_SUBTRACT = SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
constexpr GpuBlendOp MIN = SDL_GPU_BLENDOP_MIN;
constexpr GpuBlendOp MAX = SDL_GPU_BLENDOP_MAX;
} // namespace gpu_blend_op

namespace gpu_blend_factor {
constexpr GpuBlendFactor INVALID = SDL_GPU_BLENDFACTOR_INVALID;
constexpr GpuBlendFactor ZERO = SDL_GPU_BLENDFACTOR_ZERO;
constexpr GpuBlendFactor ONE = SDL_GPU_BLENDFACTOR_ONE;
constexpr GpuBlendFactor SRC_COLOR = SDL_GPU_BLENDFACTOR_SRC_COLOR;
constexpr GpuBlendFactor ONE_MINUS_SRC_COLOR = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
constexpr GpuBlendFactor DST_COLOR = SDL_GPU_BLENDFACTOR_DST_COLOR;
constexpr GpuBlendFactor ONE_MINUS_DST_COLOR = SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
constexpr GpuBlendFactor SRC_ALPHA = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
constexpr GpuBlendFactor ONE_MINUS_SRC_ALPHA = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
constexpr GpuBlendFactor DST_ALPHA = SDL_GPU_BLENDFACTOR_DST_ALPHA;
constexpr GpuBlendFactor ONE_MINUS_DST_ALPHA = SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
constexpr GpuBlendFactor CONSTANT_COLOR = SDL_GPU_BLENDFACTOR_CONSTANT_COLOR;
constexpr GpuBlendFactor ONE_MINUS_CONSTANT_COLOR = SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR;
constexpr GpuBlendFactor SRC_ALPHA_SATURATE = SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE;
} // namespace gpu_blend_factor

namespace gpu_filter {
constexpr GpuFilter NEAREST = SDL_GPU_FILTER_NEAREST;
constexpr GpuFilter LINEAR = SDL_GPU_FILTER_LINEAR;
} // namespace gpu_filter

namespace gpu_sampler_mipmap_mode {
constexpr GpuSamplerMipmapMode NEAREST = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
constexpr GpuSamplerMipmapMode LINEAR = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
} // namespace gpu_sampler_mipmap_mode

namespace gpu_sampler_address_mode {
constexpr GpuSamplerAddressMode REPEAT = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
constexpr GpuSamplerAddressMode MIRRORED_REPEAT = SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
constexpr GpuSamplerAddressMode CLAMP_TO_EDGE = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
} // namespace gpu_sampler_address_mode

namespace gpu_load_op {
constexpr GpuLoadOp LOAD = SDL_GPU_LOADOP_LOAD;
constexpr GpuLoadOp CLEAR = SDL_GPU_LOADOP_CLEAR;
constexpr GpuLoadOp DONT_CARE = SDL_GPU_LOADOP_DONT_CARE;
} // namespace gpu_load_op

namespace gpu_store_op {
constexpr GpuStoreOp STORE = SDL_GPU_STOREOP_STORE;
constexpr GpuStoreOp DONT_CARE = SDL_GPU_STOREOP_DONT_CARE;
constexpr GpuStoreOp RESOLVE = SDL_GPU_STOREOP_RESOLVE;
constexpr GpuStoreOp RESOLVE_AND_STORE = SDL_GPU_STOREOP_RESOLVE_AND_STORE;
} // namespace gpu_store_op

namespace gpu_index_element_size {
constexpr GpuIndexElementSize BITS16 = SDL_GPU_INDEXELEMENTSIZE_16BIT;
constexpr GpuIndexElementSize BITS32 = SDL_GPU_INDEXELEMENTSIZE_32BIT;
} // namespace gpu_index_element_size

namespace gpu_swapchain_composition {
constexpr GpuSwapchainComposition SDR = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
constexpr GpuSwapchainComposition SDR_LINEAR = SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR;
constexpr GpuSwapchainComposition HDR_EXTENDED_LINEAR = SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR;
constexpr GpuSwapchainComposition HDR10_ST2084 = SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084;
} // namespace gpu_swapchain_composition

namespace gpu_present_mode {
constexpr GpuPresentMode VSYNC = SDL_GPU_PRESENTMODE_VSYNC;
constexpr GpuPresentMode IMMEDIATE = SDL_GPU_PRESENTMODE_IMMEDIATE;
constexpr GpuPresentMode MAILBOX = SDL_GPU_PRESENTMODE_MAILBOX;
} // namespace gpu_present_mode

// CreateInfo / descriptor structs
using GpuBufferCreateInfo = SDL_GPUBufferCreateInfo;
using GpuTransferBufferCreateInfo = SDL_GPUTransferBufferCreateInfo;
using GpuTextureCreateInfo = SDL_GPUTextureCreateInfo;
using GpuSamplerCreateInfo = SDL_GPUSamplerCreateInfo;
using GpuShaderCreateInfo = SDL_GPUShaderCreateInfo;
using GpuGraphicsPipelineCreateInfo = SDL_GPUGraphicsPipelineCreateInfo;
using GpuComputePipelineCreateInfo = SDL_GPUComputePipelineCreateInfo;
using GpuVertexBufferDescription = SDL_GPUVertexBufferDescription;
using GpuVertexAttribute = SDL_GPUVertexAttribute;
using GpuVertexInputState = SDL_GPUVertexInputState;
using GpuStencilOpState = SDL_GPUStencilOpState;
using GpuColorTargetBlendState = SDL_GPUColorTargetBlendState;
using GpuRasterizerState = SDL_GPURasterizerState;
using GpuMultisampleState = SDL_GPUMultisampleState;
using GpuDepthStencilState = SDL_GPUDepthStencilState;
using GpuColorTargetDescription = SDL_GPUColorTargetDescription;
using GpuGraphicsPipelineTargetInfo = SDL_GPUGraphicsPipelineTargetInfo;
using GPUVulkanOptions = SDL_GPUVulkanOptions;

// Binding / region structs
using GpuViewport = SDL_GPUViewport;
using GpuBufferBinding = SDL_GPUBufferBinding;
using GpuBufferRegion = SDL_GPUBufferRegion;
using GpuBufferLocation = SDL_GPUBufferLocation;
using GpuTransferBufferLocation = SDL_GPUTransferBufferLocation;
using GpuTextureRegion = SDL_GPUTextureRegion;
using GpuTextureLocation = SDL_GPUTextureLocation;
using GpuTextureTransferInfo = SDL_GPUTextureTransferInfo;
using GpuTextureSamplerBinding = SDL_GPUTextureSamplerBinding;
using GpuColorTargetInfo = SDL_GPUColorTargetInfo;
using GpuDepthStencilTargetInfo = SDL_GPUDepthStencilTargetInfo;
using GpuStorageTextureReadWriteBinding = SDL_GPUStorageTextureReadWriteBinding;
using GpuStorageBufferReadWriteBinding = SDL_GPUStorageBufferReadWriteBinding;
using GpuBlitInfo = SDL_GPUBlitInfo;
using GpuBlitRegion = SDL_GPUBlitRegion;

// ============================================================================
// GpuBuffer
// ============================================================================

class GpuBuffer: public DeviceWrapper<SDL_GPUDevice,SDL_GPUBuffer,SDL_ReleaseGPUBuffer> {
public:
    using DeviceWrapper::DeviceWrapper;

    void SetName(const char *name);
};

// ============================================================================
// GpuMappedBuffer — RAII map/unmap around a TransferBuffer
// ============================================================================

class GpuMappedBuffer {
    SDL_GPUDevice *m_device = nullptr;
    SDL_GPUTransferBuffer *buf = nullptr;
    void *ptr = nullptr;

public:
    constexpr GpuMappedBuffer() noexcept = default;
    GpuMappedBuffer(SDL_GPUDevice *dev, SDL_GPUTransferBuffer *buf, bool cycle)
        : m_device(dev), buf(buf), ptr(SDL_MapGPUTransferBuffer(dev, buf, cycle)) {}
    ~GpuMappedBuffer();
    GpuMappedBuffer(const GpuMappedBuffer &) = delete;
    GpuMappedBuffer &operator=(const GpuMappedBuffer &) = delete;
    GpuMappedBuffer(GpuMappedBuffer &&o) noexcept : m_device(o.m_device), buf(o.buf), ptr(o.ptr) {
        o.ptr = nullptr;
    }

    [[nodiscard]] void *GetData() noexcept { return ptr; }
    [[nodiscard]] explicit operator bool() const noexcept { return ptr != nullptr; }

    template <typename T> [[nodiscard]] T *As() noexcept { return static_cast<T *>(ptr); }

    template <typename T> [[nodiscard]] std::span<T> Span(size_t count) noexcept {
        return ptr ? std::span<T>(static_cast<T *>(ptr), count) : std::span<T>{};
    }
};

// ============================================================================
// GpuTransferBuffer
// ============================================================================

class GpuTransferBuffer: public DeviceWrapper<SDL_GPUDevice, SDL_GPUTransferBuffer, SDL_ReleaseGPUTransferBuffer> {
public:
    using DeviceWrapper::DeviceWrapper;

    [[nodiscard]] GpuMappedBuffer Map(bool cycle = false) noexcept;

    template <typename T> [[nodiscard]] bool WriteData(std::span<const T> data, bool cycle = false) noexcept {
        auto mapped = Map(cycle);
        if (!mapped)
            return false;
        std::memcpy(mapped.GetData(), data.data(), data.size_bytes());
        return true;
    }
};

// ============================================================================
// GpuTexture
// ============================================================================

class GpuTexture: public DeviceWrapper<SDL_GPUDevice, SDL_GPUTexture, SDL_ReleaseGPUTexture> {
public:
    using DeviceWrapper::DeviceWrapper;

    void SetName(const char *name);
};

// ============================================================================
// GpuRasterizerState
// ============================================================================

/*class GpuRasterizerState: public Wrapper<SDL_GPURasterizerState> {
public:
    using Wrapper::Wrapper;
};*/

// ============================================================================
// GpuSampler
// ============================================================================

class GpuSampler: public DeviceWrapper<SDL_GPUDevice, SDL_GPUSampler, SDL_ReleaseGPUSampler> {
public:
    using DeviceWrapper::DeviceWrapper;
};

// ============================================================================
// GpuShader
// ============================================================================

class GpuShader: public DeviceWrapper<SDL_GPUDevice, SDL_GPUShader, SDL_ReleaseGPUShader> {
public:
    using DeviceWrapper::DeviceWrapper;
    GpuShader(SDL_GPUDevice *dev, const GpuShaderCreateInfo &info) : DeviceWrapper(dev, SDL_CreateGPUShader(dev, &info)) {}
};

// ============================================================================
// GpuGraphicsPipeline
// ============================================================================

class GpuGraphicsPipeline: public DeviceWrapper<SDL_GPUDevice, SDL_GPUGraphicsPipeline, SDL_ReleaseGPUGraphicsPipeline> {
public:
    using DeviceWrapper::DeviceWrapper;
};

// ============================================================================
// GpuComputePipeline
// ============================================================================

class GpuComputePipeline {
    SDL_GPUDevice *m_device = nullptr;
    SDL_GPUComputePipeline *m_handle = nullptr;

public:
    constexpr GpuComputePipeline() noexcept = default;
    GpuComputePipeline(SDL_GPUDevice *dev, SDL_GPUComputePipeline *h) : m_device(dev), m_handle(h) {}
    ~GpuComputePipeline();
    GpuComputePipeline(const GpuComputePipeline &) = delete;
    GpuComputePipeline &operator=(const GpuComputePipeline &) = delete;
    GpuComputePipeline(GpuComputePipeline &&o) noexcept : m_device(o.m_device), m_handle(o.m_handle) {
        o.m_handle = nullptr;
    }
    GpuComputePipeline &operator=(GpuComputePipeline &&o) noexcept {
        if (this != &o) {
            if (m_handle && m_device)
                SDL_ReleaseGPUComputePipeline(m_device, m_handle);
            m_device = o.m_device;
            m_handle = o.m_handle;
            o.m_handle = nullptr;
        }
        return *this;
    }

    [[nodiscard]] SDL_GPUComputePipeline *Get() const noexcept { return m_handle; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_handle != nullptr; }
};

// ============================================================================
// GpuFence
// ============================================================================

class GpuFence: public DeviceWrapper<SDL_GPUDevice, SDL_GPUFence, SDL_ReleaseGPUFence> {
public:
    using DeviceWrapper::DeviceWrapper;
    
    [[nodiscard]] bool Query() const noexcept { return m_handle && m_device && SDL_QueryGPUFence(m_device, m_handle); }
};

// ============================================================================
// GpuRenderPass — RAII (auto-ends on destruction)
// ============================================================================

class GpuRenderPass: public Wrapper<SDL_GPURenderPass, SDL_EndGPURenderPass> {
public:
    using Wrapper::Wrapper;

    void End() noexcept;

    // ── Pipeline ─────────────────────────────────────────────────────────────

    void BindPipeline(const GpuGraphicsPipeline &p) noexcept;

    // ── Viewport / scissor ────────────────────────────────────────────────────

    void SetViewport(const GpuViewport &vp) noexcept;
    void SetScissor(const SDL_Rect &r) noexcept;
    void SetBlendConstants(SDL_FColor color) noexcept;
    void SetStencilReference(uint8_t ref) noexcept;

    // ── Vertex / index buffers ────────────────────────────────────────────────

    void BindVertexBuffers(uint32_t firstSlot, std::span<const GpuBufferBinding> bindings) noexcept;
    void BindVertexBuffer(uint32_t slot, const GpuBufferBinding &binding) noexcept;
    void BindIndexBuffer(const GpuBufferBinding &binding, GpuIndexElementSize elemSize) noexcept;

    // ── Vertex shader resources ───────────────────────────────────────────────

    void BindVertexSamplers(uint32_t firstSlot, std::span<const GpuTextureSamplerBinding> bindings) noexcept;
    void BindVertexStorageTextures(uint32_t firstSlot, std::span<const Ref<GpuTexture>> textures);
    void BindVertexStorageBuffers(uint32_t firstSlot, std::span<const Ref<GpuBuffer>> buffers);

    // ── Fragment shader resources ─────────────────────────────────────────────

    void BindFragmentSamplers(uint32_t firstSlot, std::span<const GpuTextureSamplerBinding> bindings) noexcept;
    void BindFragmentStorageTextures(uint32_t firstSlot, std::span<const Ref<GpuTexture>> textures);
    void BindFragmentStorageBuffers(uint32_t firstSlot, std::span<const Ref<GpuBuffer>> buffers);

    // ── Draw calls ────────────────────────────────────────────────────────────

    void DrawPrimitives(uint32_t numVertices, uint32_t numInstances = 1, uint32_t firstVertex = 0,
                        uint32_t firstInstance = 0) noexcept;
    void DrawIndexedPrimitives(uint32_t numIndices, uint32_t numInstances = 1, uint32_t firstIndex = 0,
                               int32_t vertexOffset = 0, uint32_t firstInstance = 0) noexcept;
    void DrawPrimitivesIndirect(const GpuBuffer &buf, uint32_t offset, uint32_t drawCount) noexcept;
    void DrawIndexedPrimitivesIndirect(const GpuBuffer &buf, uint32_t offset, uint32_t drawCount) noexcept;
};

// ============================================================================
// GpuComputePass — RAII (auto-ends on destruction)
// ============================================================================

class GpuComputePass: public Wrapper<SDL_GPUComputePass, SDL_EndGPUComputePass> {
public:
    using Wrapper::Wrapper;

    void End() noexcept;

    void BindPipeline(const GpuComputePipeline &p) noexcept;
    void BindSamplers(uint32_t firstSlot, std::span<const GpuTextureSamplerBinding> bindings) noexcept;
    void BindStorageTextures(uint32_t firstSlot, std::span<const Ref<GpuTexture>> textures);
    void BindStorageBuffers(uint32_t firstSlot, std::span<const Ref<GpuBuffer>> buffers);
    void Dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ) noexcept;
    void DispatchIndirect(const GpuBuffer &buf, uint32_t offset) noexcept;
};

// ============================================================================
// GpuCopyPass — RAII (auto-ends on destruction)
// ============================================================================

class GpuCopyPass: public Wrapper<SDL_GPUCopyPass, SDL_EndGPUCopyPass> {
public:
    using Wrapper::Wrapper;
    
    void End() noexcept;

    void UploadToTexture(const GpuTextureTransferInfo &src, const GpuTextureRegion &dst, bool cycle = false) noexcept;
    void UploadToBuffer(const GpuTransferBufferLocation &src, const GpuBufferRegion &dst, bool cycle = false) noexcept;
    void CopyTextureToTexture(const GpuTextureLocation &src, const GpuTextureLocation &dst, uint32_t w, uint32_t h,
                              uint32_t d, bool cycle = false) noexcept;
    void CopyBufferToBuffer(const GpuBufferLocation &src, const GpuBufferLocation &dst, uint32_t size,
                            bool cycle = false) noexcept;
    void DownloadFromTexture(const GpuTextureRegion &src, const GpuTextureTransferInfo &dst) noexcept;
    void DownloadFromBuffer(const GpuBufferRegion &src, const GpuTransferBufferLocation &dst) noexcept;
};

// ============================================================================
// GpuCommandBuffer — acquired from GpuDevice, auto-cancelled on destruction
// ============================================================================

void CancelGPUCommandBuffer(SDL_GPUDevice *d, SDL_GPUCommandBuffer* b);

class GpuCommandBuffer: public DeviceWrapper<SDL_GPUDevice, SDL_GPUCommandBuffer, CancelGPUCommandBuffer> {
public:
    using DeviceWrapper::DeviceWrapper;
    
    // ── Debug ─────────────────────────────────────────────────────────────────

    void InsertDebugLabel(const char *label) noexcept;
    void PushDebugGroup(const char *name) noexcept;
    void PopDebugGroup() noexcept;

    // ── Uniform data ──────────────────────────────────────────────────────────

    template <typename T> void PushVertexUniformData(uint32_t slot, const T &data) noexcept {
        if (m_handle)
            SDL_PushGPUVertexUniformData(m_handle, slot, &data, uint32_t(sizeof(T)));
    }
    template <typename T> void PushFragmentUniformData(uint32_t slot, const T &data) noexcept {
        if (m_handle)
            SDL_PushGPUFragmentUniformData(m_handle, slot, &data, uint32_t(sizeof(T)));
    }
    template <typename T> void PushComputeUniformData(uint32_t slot, const T &data) noexcept {
        if (m_handle)
            SDL_PushGPUComputeUniformData(m_handle, slot, &data, uint32_t(sizeof(T)));
    }

    // ── Begin passes ──────────────────────────────────────────────────────────

    [[nodiscard]] GpuRenderPass BeginRenderPass(std::span<const GpuColorTargetInfo> colorTargets,
                                                const GpuDepthStencilTargetInfo *depthStencil = nullptr) noexcept;
    [[nodiscard]] GpuRenderPass BeginRenderPass(const GpuColorTargetInfo &colorTarget,
                                                const GpuDepthStencilTargetInfo *depthStencil = nullptr) noexcept;

    [[nodiscard]] GpuComputePass
    BeginComputePass(std::span<const GpuStorageTextureReadWriteBinding> storageTextures = {},
                     std::span<const GpuStorageBufferReadWriteBinding> storageBuffers = {}) noexcept;

    [[nodiscard]] GpuCopyPass BeginCopyPass() noexcept;

    // ── Texture operations ────────────────────────────────────────────────────

    void GenerateMipmaps(const GpuTexture &tex) noexcept;
    void BlitTexture(const GpuBlitInfo &info) noexcept;

    // ── Swapchain acquisition ─────────────────────────────────────────────────

    /// Returns the swapchain texture for this frame — NONE if the window is
    /// minimized or on error (check GetError()). The returned m_handle is owned
    /// by the swapchain, not by the caller: it must never be released, so it
    /// comes back as a non-owning Borrowed<> rather than a raw pointer.
    [[nodiscard]] Option<Borrowed<SDL_GPUTexture>> AcquireSwapchainTexture(Ref<Window> win, uint32_t *outW = nullptr,
                                                                           uint32_t *outH = nullptr) noexcept;
    [[nodiscard]] Option<Borrowed<SDL_GPUTexture>>
    WaitAndAcquireSwapchainTexture(Ref<Window> win, uint32_t *outW = nullptr, uint32_t *outH = nullptr) noexcept;

    // ── Submit ────────────────────────────────────────────────────────────────

    [[nodiscard]] bool Submit() noexcept;

    [[nodiscard]] Option<GpuFence> SubmitAndAcquireFence() noexcept;

    [[nodiscard]] bool Cancel() noexcept;
};

// ============================================================================
// GpuDevice — main context, factory for all GPU resources
// ============================================================================

class GpuDevice: public Wrapper<SDL_GPUDevice, SDL_DestroyGPUDevice> {
public:
    using Wrapper::Wrapper;

    // ── Factories ─────────────────────────────────────────────────────────────

    [[nodiscard]] static Result<GpuDevice, Error> Create(SDL_GPUShaderFormat formats, bool debugMode = false,
                                                              const char *driverName = nullptr);

    [[nodiscard]] static Result<GpuDevice, StringView> CreateWithProperties(SDL_PropertiesID props);

    // ── Device info ───────────────────────────────────────────────────────────

    [[nodiscard]] const char *Driver() const noexcept { return m_handle ? SDL_GetGPUDeviceDriver(m_handle) : nullptr; }
    [[nodiscard]] SDL_GPUShaderFormat ShaderFormats() const noexcept;
    [[nodiscard]] SDL_PropertiesID Properties() const noexcept;

    [[nodiscard]] bool WaitIdle() noexcept { return m_handle && SDL_WaitForGPUIdle(m_handle); }

    // ── Texture format queries ────────────────────────────────────────────────

    [[nodiscard]] bool TextureSupportsFormat(SDL_GPUTextureFormat format, SDL_GPUTextureType type,
                                             SDL_GPUTextureUsageFlags usage) const noexcept;
    [[nodiscard]] bool TextureSupportsSampleCount(SDL_GPUTextureFormat format,
                                                  SDL_GPUSampleCount count) const noexcept;

    // ── Swapchain / window ────────────────────────────────────────────────────

    [[nodiscard]] bool ClaimWindow(Ref<Window> win) noexcept;
    void ReleaseWindow(Ref<Window> win) noexcept;
    [[nodiscard]] bool SetSwapchainParameters(Ref<Window> win, SDL_GPUSwapchainComposition composition,
                                              SDL_GPUPresentMode presentMode) noexcept;
    [[nodiscard]] bool SetAllowedFramesInFlight(uint32_t n) noexcept;
    [[nodiscard]] SDL_GPUTextureFormat SwapchainTextureFormat(Ref<Window> win) const noexcept;
    [[nodiscard]] bool WindowSupportsComposition(Ref<Window> win, SDL_GPUSwapchainComposition c) const noexcept;
    [[nodiscard]] bool WindowSupportsPresentMode(Ref<Window> win, SDL_GPUPresentMode mode) const noexcept;
    [[nodiscard]] bool WaitForSwapchain(Ref<Window> win) noexcept;

    // ── Command buffer ────────────────────────────────────────────────────────

    [[nodiscard]] GpuCommandBuffer AcquireCommandBuffer() noexcept;

    // ── Fences ───────────────────────────────────────────────────────────────

    [[nodiscard]] bool WaitForFences(bool waitAll, std::span<const Ref<GpuFence>> fences);

    // ── Resource creation ─────────────────────────────────────────────────────

    [[nodiscard]] Result<GpuBuffer, StringView> CreateBuffer(const GpuBufferCreateInfo &info);
    [[nodiscard]] Result<GpuBuffer, StringView> CreateBuffer(SDL_GPUBufferUsageFlags usage, uint32_t size);

    [[nodiscard]] Result<GpuTransferBuffer, StringView> CreateTransferBuffer(const GpuTransferBufferCreateInfo &info);
    [[nodiscard]] Result<GpuTransferBuffer, StringView> CreateTransferBuffer(SDL_GPUTransferBufferUsage usage,
                                                                             uint32_t size);

    [[nodiscard]] Result<GpuTexture, StringView> CreateTexture(const GpuTextureCreateInfo &info);

    [[nodiscard]] Result<GpuSampler, StringView> CreateSampler(const GpuSamplerCreateInfo &info);

    [[nodiscard]] Result<GpuShader, StringView> CreateShader(const GpuShaderCreateInfo &info);

    [[nodiscard]] Result<GpuGraphicsPipeline, StringView>
    CreateGraphicsPipeline(const GpuGraphicsPipelineCreateInfo &info);

    [[nodiscard]] Result<GpuComputePipeline, StringView>
    CreateComputePipeline(const GpuComputePipelineCreateInfo &info);
};

// Renderer::GetGPUDevice() — corps (déclaré dans render.hpp, voir la note sur
// l'inclusion circulaire là-bas : a besoin du type complet de GpuDevice
// ci-dessus).


// ============================================================================
// Free functions
// ============================================================================

[[nodiscard]] bool GpuSupportsShaderFormats(SDL_GPUShaderFormat formats, const char *name = nullptr) noexcept;
// Pré-vérifie qu'un backend GPU est disponible pour les propriétés données,
// sans créer de m_device — utile avant GpuDevice::createWithProperties().
[[nodiscard]] bool GpuSupportsProperties(SDL_PropertiesID props) noexcept;
[[nodiscard]] inline int GpuDriverCount() noexcept { return SDL_GetNumGPUDrivers(); }
[[nodiscard]] inline const char *GpuDriver(int i) noexcept { return SDL_GetGPUDriver(i); }

[[nodiscard]] uint32_t GpuTexelBlockSize(SDL_GPUTextureFormat fmt) noexcept;
[[nodiscard]] uint32_t GpuCalculateTextureSize(SDL_GPUTextureFormat fmt, uint32_t w, uint32_t h,
                                                      uint32_t depthOrLayers) noexcept;
[[nodiscard]] SDL_PixelFormat GpuTextureFormatToPixel(SDL_GPUTextureFormat fmt) noexcept;
[[nodiscard]] SDL_GPUTextureFormat GpuTextureFormatFromPixel(SDL_PixelFormat fmt) noexcept;

} // namespace sdl3
