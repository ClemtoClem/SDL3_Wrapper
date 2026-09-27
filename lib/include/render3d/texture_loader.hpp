#pragma once
#include <cstring>

#include "../sdl3/gpu.hpp"
#include "../sdl3/image.hpp"

namespace render3d {

/// Charge un fichier image (PNG/JPG/...) directement en sdl3::GpuTexture —
/// aucun helper de ce type n'existait avant (sdl3/image.hpp ne charge que
/// vers Surface/l'ancien Texture de sdl3::Renderer). Convertit vers RGBA32
/// pour un upload GPU direct, en réutilisant le même enchaînement que
/// Canvas::Create() pour sa texture blanche 1x1 par défaut (CreateTexture +
/// CreateTransferBuffer + GpuCopyPass::UploadToTexture).
[[nodiscard]] Result<sdl3::GpuTexture, String> LoadTextureFromFile(sdl3::GpuDevice &device,
																		  const String &path);

} // namespace render3d
