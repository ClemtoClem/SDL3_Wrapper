// Définitions de render3d/texture_loader.hpp
#include <render3d/texture_loader.hpp>

#include <vector>

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

	std::vector<uint8_t> pixels(size_t(owned.GetWidth()) * size_t(owned.GetHeight()) * 4);
	const uint8_t *src = static_cast<const uint8_t *>(owned.Get()->pixels);
	const size_t rowBytes = size_t(owned.GetWidth()) * 4;
	for (int row = 0; row < owned.GetHeight(); ++row)
		std::memcpy(&pixels[size_t(row) * rowBytes], src + size_t(row) * size_t(owned.Get()->pitch), rowBytes);
	return CreateTextureFromPixels(device, uint32_t(owned.GetWidth()), uint32_t(owned.GetHeight()), pixels);
}

Result<sdl3::GpuTexture, String> CreateTextureFromPixels(sdl3::GpuDevice &device, uint32_t width, uint32_t height,
															std::span<const uint8_t> rgba) {
	if (width == 0 || height == 0 || rgba.size() < size_t(width) * size_t(height) * 4)
		return Err(String("CreateTextureFromPixels : taille de pixels incohérente"));
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

	const uint32_t byteSize = width * height * 4;
	auto transferResult = device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::UPLOAD, byteSize);
	if (!transferResult)
		return Err(String(transferResult.Error()));
	sdl3::GpuTransferBuffer transfer = std::move(transferResult.Value());
	{
		auto mapped = transfer.Map(false);
		if (!mapped)
			return Err(String("CreateTextureFromPixels : tampon de transfert inaccessible"));
		std::memcpy(mapped.As<uint8_t>(), rgba.data(), byteSize);
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
