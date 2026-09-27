// Définitions de render3d/texture_loader.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "render3d/texture_loader.hpp"

namespace render3d {

Result<sdl3::GpuTexture, String> LoadTextureFromFile(sdl3::GpuDevice &device, const String &path) {
	auto surfaceResult = sdl3::ImgLoad(path);
	if (!surfaceResult)
		return Err(String(surfaceResult.Error()));
	sdl3::Surface surface = std::move(surfaceResult.Value());

	// `sdl3::PixelFormat::RGBA32` et non la constante SDL brute : le wrapper
	// n'expose jamais les types C de SDL dans ses signatures (cf.
	// memory/feedback_wrap_c_pointers.md), et ce fichier ne compilait plus
	// depuis que `Surface::Convert` a été typée — il n'était inclus par aucune
	// unité de traduction jusqu'ici, d'où la rupture passée inaperçue.
	auto convertedResult = surface.Convert(sdl3::PixelFormat::RGBA32);
	if (!convertedResult)
		return Err(String(convertedResult.Error()));
	sdl3::Surface owned = std::move(convertedResult.Value());

	uint32_t width = uint32_t(owned.GetWidth());
	uint32_t height = uint32_t(owned.GetHeight());

	sdl3::GpuTextureCreateInfo textureInfo{};
	textureInfo.type = sdl3::gpu_texture_type::TEXTURE2_D;
	textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	textureInfo.usage = sdl3::gpu_texture_usage::SAMPLER;
	textureInfo.width = width;
	textureInfo.height = height;
	textureInfo.layer_count_or_depth = 1;
	textureInfo.num_levels = 1;
	textureInfo.sample_count = sdl3::gpu_sample_count::SAMPLE1;
	auto textureResult = device.CreateTexture(textureInfo);
	if (!textureResult)
		return Err(String(textureResult.Error()));

	uint32_t byteSize = width * height * 4;
	auto transferResult = device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::UPLOAD, byteSize);
	if (!transferResult)
		return Err(String(transferResult.Error()));
	sdl3::GpuTransferBuffer transfer = std::move(transferResult.Value());
	{
		auto mapped = transfer.Map(false);
		if (!mapped)
			return Err(String("LoadTextureFromFile: failed to map transfer buffer"));
		const uint8_t *src = static_cast<const uint8_t *>(owned.Get()->pixels);
		uint8_t *dst = mapped.As<uint8_t>();
		uint32_t rowBytes = width * 4;
		for (uint32_t row = 0; row < height; ++row)
			std::memcpy(dst + row * rowBytes, src + row * size_t(owned.Get()->pitch), rowBytes);
	}

	auto cmd = device.AcquireCommandBuffer();
	{
		sdl3::GpuCopyPass copyPass = cmd.BeginCopyPass();
		sdl3::GpuTextureTransferInfo src{transfer.Get(), 0, width, height};
		sdl3::GpuTextureRegion dst{textureResult.Value().Get(), 0, 0, 0, 0, 0, width, height, 1};
		copyPass.UploadToTexture(src, dst, false);
	}
	if (!cmd.Submit())
		return Err(String(sdl3::GetError()));

	return Ok(std::move(textureResult.Value()));
}

} // namespace render3d
