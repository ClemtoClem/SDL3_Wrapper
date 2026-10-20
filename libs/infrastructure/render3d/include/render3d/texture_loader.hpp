#pragma once
#include <cstring>
#include <span>

#include <sdl3/gpu.hpp>
#include <sdl3/image.hpp>

namespace render3d {

/// Charge un fichier image (PNG/JPG/...) directement en sdl3::GpuTexture —
/// aucun helper de ce type n'existait avant (sdl3/image.hpp ne charge que
/// vers Surface/l'ancien Texture de sdl3::Renderer). Convertit vers RGBA32
/// pour un upload GPU direct, en réutilisant le même enchaînement que
/// Canvas::Create() pour sa texture blanche 1x1 par défaut (CreateTexture +
/// CreateTransferBuffer + GpuCopyPass::UploadToTexture).
[[nodiscard]] Result<sdl3::GpuTexture, String> LoadTextureFromFile(sdl3::GpuDevice &device,
																		  const String &path);

/// Texture RGBA8 créée depuis des pixels en mémoire (`width × height × 4`
/// octets, lignes de haut en bas) : atlas assemblés à l'exécution, textures
/// générées.
[[nodiscard]] Result<sdl3::GpuTexture, String> CreateTextureFromPixels(sdl3::GpuDevice &device, uint32_t width,
																		uint32_t height, std::span<const uint8_t> rgba);

} // namespace render3d
