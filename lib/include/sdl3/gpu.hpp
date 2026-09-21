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

    void SetName(const char *name) {
        if (m_handle && m_device)
            SDL_SetGPUBufferName(m_device, m_handle, name);
    }
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
    ~GpuMappedBuffer() {
        if (ptr && m_device && buf)
            SDL_UnmapGPUTransferBuffer(m_device, buf);
    }
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

    [[nodiscard]] GpuMappedBuffer Map(bool cycle = false) noexcept {
        return GpuMappedBuffer(m_device, m_handle, cycle);
    }

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

    void SetName(const char *name) {
        if (m_handle && m_device)
            SDL_SetGPUTextureName(m_device, m_handle, name);
    }
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
    ~GpuComputePipeline() {
        if (m_handle && m_device)
            SDL_ReleaseGPUComputePipeline(m_device, m_handle);
    }
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

    void End() noexcept {
        if (m_handle) {
            SDL_EndGPURenderPass(m_handle);
            m_handle = nullptr;
        }
    }

    // ── Pipeline ─────────────────────────────────────────────────────────────

    void BindPipeline(const GpuGraphicsPipeline &p) noexcept {
        if (m_handle)
            SDL_BindGPUGraphicsPipeline(m_handle, p.Get());
    }

    // ── Viewport / scissor ────────────────────────────────────────────────────

    void SetViewport(const GpuViewport &vp) noexcept {
        if (m_handle)
            SDL_SetGPUViewport(m_handle, &vp);
    }
    void SetScissor(const SDL_Rect &r) noexcept {
        if (m_handle)
            SDL_SetGPUScissor(m_handle, &r);
    }
    void SetBlendConstants(SDL_FColor color) noexcept {
        if (m_handle)
            SDL_SetGPUBlendConstants(m_handle, color);
    }
    void SetStencilReference(uint8_t ref) noexcept {
        if (m_handle)
            SDL_SetGPUStencilReference(m_handle, ref);
    }

    // ── Vertex / index buffers ────────────────────────────────────────────────

    void BindVertexBuffers(uint32_t firstSlot, std::span<const GpuBufferBinding> bindings) noexcept {
        if (m_handle)
            SDL_BindGPUVertexBuffers(m_handle, firstSlot, bindings.data(), uint32_t(bindings.size()));
    }
    void BindVertexBuffer(uint32_t slot, const GpuBufferBinding &binding) noexcept {
        BindVertexBuffers(slot, {&binding, 1});
    }
    void BindIndexBuffer(const GpuBufferBinding &binding, GpuIndexElementSize elemSize) noexcept {
        if (m_handle)
            SDL_BindGPUIndexBuffer(m_handle, &binding, elemSize);
    }

    // ── Vertex shader resources ───────────────────────────────────────────────

    void BindVertexSamplers(uint32_t firstSlot, std::span<const GpuTextureSamplerBinding> bindings) noexcept {
        if (m_handle)
            SDL_BindGPUVertexSamplers(m_handle, firstSlot, bindings.data(), uint32_t(bindings.size()));
    }
    void BindVertexStorageTextures(uint32_t firstSlot, std::span<const Ref<GpuTexture>> textures) {
        if (!m_handle)
            return;
        auto raw = ::detail::ToRawHandles(textures);
        SDL_BindGPUVertexStorageTextures(m_handle, firstSlot, raw.data(), uint32_t(raw.size()));
    }
    void BindVertexStorageBuffers(uint32_t firstSlot, std::span<const Ref<GpuBuffer>> buffers) {
        if (!m_handle)
            return;
        auto raw = ::detail::ToRawHandles(buffers);
        SDL_BindGPUVertexStorageBuffers(m_handle, firstSlot, raw.data(), uint32_t(raw.size()));
    }

    // ── Fragment shader resources ─────────────────────────────────────────────

    void BindFragmentSamplers(uint32_t firstSlot, std::span<const GpuTextureSamplerBinding> bindings) noexcept {
        if (m_handle)
            SDL_BindGPUFragmentSamplers(m_handle, firstSlot, bindings.data(), uint32_t(bindings.size()));
    }
    void BindFragmentStorageTextures(uint32_t firstSlot, std::span<const Ref<GpuTexture>> textures) {
        if (!m_handle)
            return;
        auto raw = ::detail::ToRawHandles(textures);
        SDL_BindGPUFragmentStorageTextures(m_handle, firstSlot, raw.data(), uint32_t(raw.size()));
    }
    void BindFragmentStorageBuffers(uint32_t firstSlot, std::span<const Ref<GpuBuffer>> buffers) {
        if (!m_handle)
            return;
        auto raw = ::detail::ToRawHandles(buffers);
        SDL_BindGPUFragmentStorageBuffers(m_handle, firstSlot, raw.data(), uint32_t(raw.size()));
    }

    // ── Draw calls ────────────────────────────────────────────────────────────

    void DrawPrimitives(uint32_t numVertices, uint32_t numInstances = 1, uint32_t firstVertex = 0,
                        uint32_t firstInstance = 0) noexcept {
        if (m_handle)
            SDL_DrawGPUPrimitives(m_handle, numVertices, numInstances, firstVertex, firstInstance);
    }
    void DrawIndexedPrimitives(uint32_t numIndices, uint32_t numInstances = 1, uint32_t firstIndex = 0,
                               int32_t vertexOffset = 0, uint32_t firstInstance = 0) noexcept {
        if (m_handle)
            SDL_DrawGPUIndexedPrimitives(m_handle, numIndices, numInstances, firstIndex, vertexOffset, firstInstance);
    }
    void DrawPrimitivesIndirect(const GpuBuffer &buf, uint32_t offset, uint32_t drawCount) noexcept {
        if (m_handle)
            SDL_DrawGPUPrimitivesIndirect(m_handle, buf.Get(), offset, drawCount);
    }
    void DrawIndexedPrimitivesIndirect(const GpuBuffer &buf, uint32_t offset, uint32_t drawCount) noexcept {
        if (m_handle)
            SDL_DrawGPUIndexedPrimitivesIndirect(m_handle, buf.Get(), offset, drawCount);
    }
};

// ============================================================================
// GpuComputePass — RAII (auto-ends on destruction)
// ============================================================================

class GpuComputePass: public Wrapper<SDL_GPUComputePass, SDL_EndGPUComputePass> {
public:
    using Wrapper::Wrapper;

    void End() noexcept {
        if (m_handle) {
            SDL_EndGPUComputePass(m_handle);
            m_handle = nullptr;
        }
    }

    void BindPipeline(const GpuComputePipeline &p) noexcept {
        if (m_handle)
            SDL_BindGPUComputePipeline(m_handle, p.Get());
    }
    void BindSamplers(uint32_t firstSlot, std::span<const GpuTextureSamplerBinding> bindings) noexcept {
        if (m_handle)
            SDL_BindGPUComputeSamplers(m_handle, firstSlot, bindings.data(), uint32_t(bindings.size()));
    }
    void BindStorageTextures(uint32_t firstSlot, std::span<const Ref<GpuTexture>> textures) {
        if (!m_handle)
            return;
        auto raw = ::detail::ToRawHandles(textures);
        SDL_BindGPUComputeStorageTextures(m_handle, firstSlot, raw.data(), uint32_t(raw.size()));
    }
    void BindStorageBuffers(uint32_t firstSlot, std::span<const Ref<GpuBuffer>> buffers) {
        if (!m_handle)
            return;
        auto raw = ::detail::ToRawHandles(buffers);
        SDL_BindGPUComputeStorageBuffers(m_handle, firstSlot, raw.data(), uint32_t(raw.size()));
    }
    void Dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ) noexcept {
        if (m_handle)
            SDL_DispatchGPUCompute(m_handle, groupX, groupY, groupZ);
    }
    void DispatchIndirect(const GpuBuffer &buf, uint32_t offset) noexcept {
        if (m_handle)
            SDL_DispatchGPUComputeIndirect(m_handle, buf.Get(), offset);
    }
};

// ============================================================================
// GpuCopyPass — RAII (auto-ends on destruction)
// ============================================================================

class GpuCopyPass: public Wrapper<SDL_GPUCopyPass, SDL_EndGPUCopyPass> {
public:
    using Wrapper::Wrapper;
    
    void End() noexcept {
        if (m_handle) {
            SDL_EndGPUCopyPass(m_handle);
            m_handle = nullptr;
        }
    }

    void UploadToTexture(const GpuTextureTransferInfo &src, const GpuTextureRegion &dst, bool cycle = false) noexcept {
        if (m_handle)
            SDL_UploadToGPUTexture(m_handle, &src, &dst, cycle);
    }
    void UploadToBuffer(const GpuTransferBufferLocation &src, const GpuBufferRegion &dst, bool cycle = false) noexcept {
        if (m_handle)
            SDL_UploadToGPUBuffer(m_handle, &src, &dst, cycle);
    }
    void CopyTextureToTexture(const GpuTextureLocation &src, const GpuTextureLocation &dst, uint32_t w, uint32_t h,
                              uint32_t d, bool cycle = false) noexcept {
        if (m_handle)
            SDL_CopyGPUTextureToTexture(m_handle, &src, &dst, w, h, d, cycle);
    }
    void CopyBufferToBuffer(const GpuBufferLocation &src, const GpuBufferLocation &dst, uint32_t size,
                            bool cycle = false) noexcept {
        if (m_handle)
            SDL_CopyGPUBufferToBuffer(m_handle, &src, &dst, size, cycle);
    }
    void DownloadFromTexture(const GpuTextureRegion &src, const GpuTextureTransferInfo &dst) noexcept {
        if (m_handle)
            SDL_DownloadFromGPUTexture(m_handle, &src, &dst);
    }
    void DownloadFromBuffer(const GpuBufferRegion &src, const GpuTransferBufferLocation &dst) noexcept {
        if (m_handle)
            SDL_DownloadFromGPUBuffer(m_handle, &src, &dst);
    }
};

// ============================================================================
// GpuCommandBuffer — acquired from GpuDevice, auto-cancelled on destruction
// ============================================================================

inline void CancelGPUCommandBuffer(SDL_GPUDevice *d, SDL_GPUCommandBuffer* b) {
    (void)d;
    SDL_CancelGPUCommandBuffer(b);
}

class GpuCommandBuffer: public DeviceWrapper<SDL_GPUDevice, SDL_GPUCommandBuffer, CancelGPUCommandBuffer> {
public:
    using DeviceWrapper::DeviceWrapper;
    
    // ── Debug ─────────────────────────────────────────────────────────────────

    void InsertDebugLabel(const char *label) noexcept {
        if (m_handle)
            SDL_InsertGPUDebugLabel(m_handle, label);
    }
    void PushDebugGroup(const char *name) noexcept {
        if (m_handle)
            SDL_PushGPUDebugGroup(m_handle, name);
    }
    void PopDebugGroup() noexcept {
        if (m_handle)
            SDL_PopGPUDebugGroup(m_handle);
    }

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
                                                const GpuDepthStencilTargetInfo *depthStencil = nullptr) noexcept {
        if (!m_handle)
            return {};
        return GpuRenderPass(
            SDL_BeginGPURenderPass(m_handle, colorTargets.data(), uint32_t(colorTargets.size()), depthStencil));
    }
    [[nodiscard]] GpuRenderPass BeginRenderPass(const GpuColorTargetInfo &colorTarget,
                                                const GpuDepthStencilTargetInfo *depthStencil = nullptr) noexcept {
        return BeginRenderPass({&colorTarget, 1}, depthStencil);
    }

    [[nodiscard]] GpuComputePass
    BeginComputePass(std::span<const GpuStorageTextureReadWriteBinding> storageTextures = {},
                     std::span<const GpuStorageBufferReadWriteBinding> storageBuffers = {}) noexcept {
        if (!m_handle)
            return {};
        return GpuComputePass(SDL_BeginGPUComputePass(m_handle, storageTextures.data(),
                                                      uint32_t(storageTextures.size()), storageBuffers.data(),
                                                      uint32_t(storageBuffers.size())));
    }

    [[nodiscard]] GpuCopyPass BeginCopyPass() noexcept {
        if (!m_handle)
            return {};
        return GpuCopyPass(SDL_BeginGPUCopyPass(m_handle));
    }

    // ── Texture operations ────────────────────────────────────────────────────

    void GenerateMipmaps(const GpuTexture &tex) noexcept {
        if (m_handle)
            SDL_GenerateMipmapsForGPUTexture(m_handle, tex.Get());
    }
    void BlitTexture(const GpuBlitInfo &info) noexcept {
        if (m_handle)
            SDL_BlitGPUTexture(m_handle, &info);
    }

    // ── Swapchain acquisition ─────────────────────────────────────────────────

    /// Returns the swapchain texture for this frame — NONE if the window is
    /// minimized or on error (check GetError()). The returned m_handle is owned
    /// by the swapchain, not by the caller: it must never be released, so it
    /// comes back as a non-owning Borrowed<> rather than a raw pointer.
    [[nodiscard]] Option<Borrowed<SDL_GPUTexture>> AcquireSwapchainTexture(Ref<Window> win, uint32_t *outW = nullptr,
                                                                           uint32_t *outH = nullptr) noexcept {
        if (!m_handle)
            return NONE;
        SDL_GPUTexture *tex = nullptr;
        SDL_AcquireGPUSwapchainTexture(m_handle, win->Get(), &tex, outW, outH);
        if (!tex)
            return NONE;
        return Some(Borrowed<SDL_GPUTexture>(tex));
    }
    [[nodiscard]] Option<Borrowed<SDL_GPUTexture>>
    WaitAndAcquireSwapchainTexture(Ref<Window> win, uint32_t *outW = nullptr, uint32_t *outH = nullptr) noexcept {
        if (!m_handle)
            return NONE;
        SDL_GPUTexture *tex = nullptr;
        SDL_WaitAndAcquireGPUSwapchainTexture(m_handle, win->Get(), &tex, outW, outH);
        if (!tex)
            return NONE;
        return Some(Borrowed<SDL_GPUTexture>(tex));
    }

    // ── Submit ────────────────────────────────────────────────────────────────

    [[nodiscard]] bool Submit() noexcept {
        if (!m_handle)
            return false;
        bool ok = SDL_SubmitGPUCommandBuffer(m_handle);
        m_handle = nullptr;
        return ok;
    }

    [[nodiscard]] Option<GpuFence> SubmitAndAcquireFence() noexcept {
        if (!m_handle)
            return NONE;
        SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(m_handle);
        m_handle = nullptr;
        if (!fence)
            return NONE;
        return Some(GpuFence(m_device, fence));
    }

    [[nodiscard]] bool Cancel() noexcept {
        if (!m_handle)
            return false;
        bool ok = SDL_CancelGPUCommandBuffer(m_handle);
        m_handle = nullptr;
        return ok;
    }
};

// ============================================================================
// GpuDevice — main context, factory for all GPU resources
// ============================================================================

class GpuDevice: public Wrapper<SDL_GPUDevice, SDL_DestroyGPUDevice> {
public:
    using Wrapper::Wrapper;

    // ── Factories ─────────────────────────────────────────────────────────────

    [[nodiscard]] static Result<GpuDevice, Error> Create(SDL_GPUShaderFormat formats, bool debugMode = false,
                                                              const char *driverName = nullptr) {
        auto *d = SDL_CreateGPUDevice(formats, debugMode, driverName);
        if (!d)
            return Err(GetError());
        return Ok(GpuDevice(d));
    }

    [[nodiscard]] static Result<GpuDevice, StringView> CreateWithProperties(SDL_PropertiesID props) {
        auto *d = SDL_CreateGPUDeviceWithProperties(props);
        if (!d)
            return Err(GetError());
        return Ok(GpuDevice(d));
    }

    // ── Device info ───────────────────────────────────────────────────────────

    [[nodiscard]] const char *Driver() const noexcept { return m_handle ? SDL_GetGPUDeviceDriver(m_handle) : nullptr; }
    [[nodiscard]] SDL_GPUShaderFormat ShaderFormats() const noexcept {
        return m_handle ? SDL_GetGPUShaderFormats(m_handle) : SDL_GPUShaderFormat(SDL_GPU_SHADERFORMAT_INVALID);
    }
    [[nodiscard]] SDL_PropertiesID Properties() const noexcept {
        return m_handle ? SDL_GetGPUDeviceProperties(m_handle) : 0;
    }

    [[nodiscard]] bool WaitIdle() noexcept { return m_handle && SDL_WaitForGPUIdle(m_handle); }

    // ── Texture format queries ────────────────────────────────────────────────

    [[nodiscard]] bool TextureSupportsFormat(SDL_GPUTextureFormat format, SDL_GPUTextureType type,
                                             SDL_GPUTextureUsageFlags usage) const noexcept {
        return m_handle && SDL_GPUTextureSupportsFormat(m_handle, format, type, usage);
    }
    [[nodiscard]] bool TextureSupportsSampleCount(SDL_GPUTextureFormat format,
                                                  SDL_GPUSampleCount count) const noexcept {
        return m_handle && SDL_GPUTextureSupportsSampleCount(m_handle, format, count);
    }

    // ── Swapchain / window ────────────────────────────────────────────────────

    [[nodiscard]] bool ClaimWindow(Ref<Window> win) noexcept {
        return m_handle && SDL_ClaimWindowForGPUDevice(m_handle, win->Get());
    }
    void ReleaseWindow(Ref<Window> win) noexcept {
        if (m_handle)
            SDL_ReleaseWindowFromGPUDevice(m_handle, win->Get());
    }
    [[nodiscard]] bool SetSwapchainParameters(Ref<Window> win, SDL_GPUSwapchainComposition composition,
                                              SDL_GPUPresentMode presentMode) noexcept {
        return m_handle && SDL_SetGPUSwapchainParameters(m_handle, win->Get(), composition, presentMode);
    }
    [[nodiscard]] bool SetAllowedFramesInFlight(uint32_t n) noexcept {
        return m_handle && SDL_SetGPUAllowedFramesInFlight(m_handle, n);
    }
    [[nodiscard]] SDL_GPUTextureFormat SwapchainTextureFormat(Ref<Window> win) const noexcept {
        return m_handle ? SDL_GetGPUSwapchainTextureFormat(m_handle, win->Get()) : SDL_GPU_TEXTUREFORMAT_INVALID;
    }
    [[nodiscard]] bool WindowSupportsComposition(Ref<Window> win, SDL_GPUSwapchainComposition c) const noexcept {
        return m_handle && SDL_WindowSupportsGPUSwapchainComposition(m_handle, win->Get(), c);
    }
    [[nodiscard]] bool WindowSupportsPresentMode(Ref<Window> win, SDL_GPUPresentMode mode) const noexcept {
        return m_handle && SDL_WindowSupportsGPUPresentMode(m_handle, win->Get(), mode);
    }
    [[nodiscard]] bool WaitForSwapchain(Ref<Window> win) noexcept {
        return m_handle && SDL_WaitForGPUSwapchain(m_handle, win->Get());
    }

    // ── Command buffer ────────────────────────────────────────────────────────

    [[nodiscard]] GpuCommandBuffer AcquireCommandBuffer() noexcept {
        if (!m_handle)
            return {};
        return GpuCommandBuffer(m_handle, SDL_AcquireGPUCommandBuffer(m_handle));
    }

    // ── Fences ───────────────────────────────────────────────────────────────

    [[nodiscard]] bool WaitForFences(bool waitAll, std::span<const Ref<GpuFence>> fences) {
        if (!m_handle)
            return false;
        auto raw = ::detail::ToRawHandles(fences);
        return SDL_WaitForGPUFences(m_handle, waitAll, raw.data(), uint32_t(raw.size()));
    }

    // ── Resource creation ─────────────────────────────────────────────────────

    [[nodiscard]] Result<GpuBuffer, StringView> CreateBuffer(const GpuBufferCreateInfo &info) {
        auto *b = SDL_CreateGPUBuffer(m_handle, &info);
        if (!b)
            return Err(GetError());
        return Ok(GpuBuffer(m_handle, b));
    }
    [[nodiscard]] Result<GpuBuffer, StringView> CreateBuffer(SDL_GPUBufferUsageFlags usage, uint32_t size) {
        GpuBufferCreateInfo info{};
        info.usage = usage;
        info.size = size;
        return CreateBuffer(info);
    }

    [[nodiscard]] Result<GpuTransferBuffer, StringView> CreateTransferBuffer(const GpuTransferBufferCreateInfo &info) {
        auto *b = SDL_CreateGPUTransferBuffer(m_handle, &info);
        if (!b)
            return Err(GetError());
        return Ok(GpuTransferBuffer(m_handle, b));
    }
    [[nodiscard]] Result<GpuTransferBuffer, StringView> CreateTransferBuffer(SDL_GPUTransferBufferUsage usage,
                                                                             uint32_t size) {
        GpuTransferBufferCreateInfo info{};
        info.usage = usage;
        info.size = size;
        return CreateTransferBuffer(info);
    }

    [[nodiscard]] Result<GpuTexture, StringView> CreateTexture(const GpuTextureCreateInfo &info) {
        auto *t = SDL_CreateGPUTexture(m_handle, &info);
        if (!t)
            return Err(GetError());
        return Ok(GpuTexture(m_handle, t));
    }

    [[nodiscard]] Result<GpuSampler, StringView> CreateSampler(const GpuSamplerCreateInfo &info) {
        auto *s = SDL_CreateGPUSampler(m_handle, &info);
        if (!s)
            return Err(GetError());
        return Ok(GpuSampler(m_handle, s));
    }

    [[nodiscard]] Result<GpuShader, StringView> CreateShader(const GpuShaderCreateInfo &info) {
        auto *s = SDL_CreateGPUShader(m_handle, &info);
        if (!s)
            return Err(GetError());
        return Ok(GpuShader(m_handle, s));
    }

    [[nodiscard]] Result<GpuGraphicsPipeline, StringView>
    CreateGraphicsPipeline(const GpuGraphicsPipelineCreateInfo &info) {
        auto *p = SDL_CreateGPUGraphicsPipeline(m_handle, &info);
        if (!p)
            return Err(GetError());
        return Ok(GpuGraphicsPipeline(m_handle, p));
    }

    [[nodiscard]] Result<GpuComputePipeline, StringView>
    CreateComputePipeline(const GpuComputePipelineCreateInfo &info) {
        auto *p = SDL_CreateGPUComputePipeline(m_handle, &info);
        if (!p)
            return Err(GetError());
        return Ok(GpuComputePipeline(m_handle, p));
    }
};

// Renderer::GetGPUDevice() — corps (déclaré dans render.hpp, voir la note sur
// l'inclusion circulaire là-bas : a besoin du type complet de GpuDevice
// ci-dessus).
inline Result<GpuDevice, StringView> Renderer::GetGPUDevice() const {
    SDL_GPUDevice *gpu = SDL_GetGPURendererDevice(m_handle);
    if (!gpu)
        return Err(GetError());
    return Ok(GpuDevice(gpu));
}

// ============================================================================
// Free functions
// ============================================================================

[[nodiscard]] inline bool GpuSupportsShaderFormats(SDL_GPUShaderFormat formats, const char *name = nullptr) noexcept {
    return SDL_GPUSupportsShaderFormats(formats, name);
}
// Pré-vérifie qu'un backend GPU est disponible pour les propriétés données,
// sans créer de m_device — utile avant GpuDevice::createWithProperties().
[[nodiscard]] inline bool GpuSupportsProperties(SDL_PropertiesID props) noexcept {
    return SDL_GPUSupportsProperties(props);
}
[[nodiscard]] inline int GpuDriverCount() noexcept { return SDL_GetNumGPUDrivers(); }
[[nodiscard]] inline const char *GpuDriver(int i) noexcept { return SDL_GetGPUDriver(i); }

[[nodiscard]] inline uint32_t GpuTexelBlockSize(SDL_GPUTextureFormat fmt) noexcept {
    return SDL_GPUTextureFormatTexelBlockSize(fmt);
}
[[nodiscard]] inline uint32_t GpuCalculateTextureSize(SDL_GPUTextureFormat fmt, uint32_t w, uint32_t h,
                                                      uint32_t depthOrLayers) noexcept {
    return SDL_CalculateGPUTextureFormatSize(fmt, w, h, depthOrLayers);
}
[[nodiscard]] inline SDL_PixelFormat GpuTextureFormatToPixel(SDL_GPUTextureFormat fmt) noexcept {
    return SDL_GetPixelFormatFromGPUTextureFormat(fmt);
}
[[nodiscard]] inline SDL_GPUTextureFormat GpuTextureFormatFromPixel(SDL_PixelFormat fmt) noexcept {
    return SDL_GetGPUTextureFormatFromPixelFormat(fmt);
}

} // namespace sdl3
