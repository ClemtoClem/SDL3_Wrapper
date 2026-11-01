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
/// générées. `mipmaps` : niveaux suivants déjà calculés (le i-ième fait
/// max(1, width >> (i + 1)) × max(1, height >> (i + 1)) pixels).
[[nodiscard]] Result<sdl3::GpuTexture, String> CreateTextureFromPixels(sdl3::GpuDevice &device, uint32_t width,
																		uint32_t height, std::span<const uint8_t> rgba,
																		std::span<const std::span<const uint8_t>> mipmaps = {});

/// Texture de données (sans mipmaps, échantillonnable), 2D (`depth` = 1) ou
/// 3D (`depth` > 1), au format donné — grilles et tables lues par un shader
/// (texelFetch). Son contenu est indéfini avant UploadTextureRegions.
[[nodiscard]] Result<sdl3::GpuTexture, String> CreateDataTexture(sdl3::GpuDevice &device, SDL_GPUTextureFormat format,
																  uint32_t width, uint32_t height, uint32_t depth = 1);

/// Région d'une texture de données et ses octets (lignes puis tranches,
/// serrées : `w × h × d × taille d'un texel`).
struct TextureRegionData {
	uint32_t x = 0, y = 0, z = 0, w = 0, h = 0, d = 1;
	std::span<const uint8_t> bytes;
};

/// Envoie plusieurs régions d'une même texture en une seule passe de copie.
Result<bool, String> UploadTextureRegions(sdl3::GpuDevice &device, sdl3::GpuTexture &texture,
										  std::span<const TextureRegionData> regions);

} // namespace render3d
