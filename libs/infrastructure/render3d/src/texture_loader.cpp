// Définitions de render3d/texture_loader.hpp
#include <render3d/texture_loader.hpp>

#include <algorithm>
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
															std::span<const uint8_t> rgba, std::span<const std::span<const uint8_t>> mipmaps) {
	if (width == 0 || height == 0 || rgba.size() < size_t(width) * size_t(height) * 4)
		return Err(String("CreateTextureFromPixels : taille de pixels incohérente"));
	// Niveaux : l'image, puis chaque mipmap fourni (dimensions vérifiées).
	struct Level {
		std::span<const uint8_t> pixels;
		uint32_t width, height;
	};
	std::vector<Level> levels{{rgba, width, height}};
	for (const std::span<const uint8_t> &mip : mipmaps) {
		const uint32_t w = std::max(1u, width >> levels.size()), h = std::max(1u, height >> levels.size());
		if (mip.size() < size_t(w) * size_t(h) * 4)
			return Err(String("CreateTextureFromPixels : taille de mipmap incohérente"));
		levels.push_back({mip, w, h});
	}
	sdl3::GpuTextureCreateInfo textureInfo{};
	textureInfo.type = sdl3::gpu_texture_type::TEXTURE2_D;
	textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	textureInfo.usage = sdl3::gpu_texture_usage::SAMPLER;
	textureInfo.width = width;
	textureInfo.height = height;
	textureInfo.layer_count_or_depth = 1;
	textureInfo.num_levels = uint32_t(levels.size());
	textureInfo.sample_count = sdl3::gpu_sample_count::SAMPLE1;
	auto textureResult = device.CreateTexture(textureInfo);
	if (!textureResult)
		return Err(String(textureResult.Error()));

	uint32_t byteSize = 0;
	for (const Level &level : levels)
		byteSize += level.width * level.height * 4;
	auto transferResult = device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::UPLOAD, byteSize);
	if (!transferResult)
		return Err(String(transferResult.Error()));
	sdl3::GpuTransferBuffer transfer = std::move(transferResult.Value());
	{
		auto mapped = transfer.Map(false);
		if (!mapped)
			return Err(String("CreateTextureFromPixels : tampon de transfert inaccessible"));
		uint8_t *out = mapped.As<uint8_t>();
		for (const Level &level : levels) {
			const size_t bytes = size_t(level.width) * size_t(level.height) * 4;
			std::memcpy(out, level.pixels.data(), bytes);
			out += bytes;
		}
	}

	auto cmd = device.AcquireCommandBuffer();
	{
		sdl3::GpuCopyPass copyPass = cmd.BeginCopyPass();
		uint32_t offset = 0;
		for (uint32_t i = 0; i < uint32_t(levels.size()); ++i) {
			const Level &level = levels[i];
			sdl3::GpuTextureTransferInfo src{transfer.Get(), offset, level.width, level.height};
			sdl3::GpuTextureRegion dst{textureResult.Value().Get(), i, 0, 0, 0, 0, level.width, level.height, 1};
			copyPass.UploadToTexture(src, dst, false);
			offset += level.width * level.height * 4;
		}
	}
	if (!cmd.Submit())
		return Err(String(sdl3::GetError()));

	return Ok(std::move(textureResult.Value()));
}

Result<sdl3::GpuTexture, String> CreateDataTexture(sdl3::GpuDevice &device, SDL_GPUTextureFormat format, uint32_t width,
												 uint32_t height, uint32_t depth) {
	if (width == 0 || height == 0 || depth == 0)
		return Err(String("CreateDataTexture : taille nulle"));
	sdl3::GpuTextureCreateInfo info{};
	info.type = depth > 1 ? sdl3::gpu_texture_type::TEXTURE3_D : sdl3::gpu_texture_type::TEXTURE2_D;
	info.format = format;
	info.usage = sdl3::gpu_texture_usage::SAMPLER;
	info.width = width;
	info.height = height;
	info.layer_count_or_depth = depth;
	info.num_levels = 1;
	info.sample_count = sdl3::gpu_sample_count::SAMPLE1;
	auto texture = device.CreateTexture(info);
	if (!texture)
		return Err(String(texture.Error()));
	return Ok(std::move(texture.Value()));
}

Result<bool, String> UploadTextureRegions(sdl3::GpuDevice &device, sdl3::GpuTexture &texture,
										  std::span<const TextureRegionData> regions) {
	size_t total = 0;
	for (const TextureRegionData &r : regions)
		total += r.bytes.size();
	if (total == 0)
		return Ok(true);
	if (total > size_t(UINT32_MAX))
		return Err(String("UploadTextureRegions : envoi trop gros"));
	auto transferResult = device.CreateTransferBuffer(sdl3::gpu_transfer_buffer_usage::UPLOAD, uint32_t(total));
	if (!transferResult)
		return Err(String(transferResult.Error()));
	sdl3::GpuTransferBuffer transfer = std::move(transferResult.Value());
	{
		auto mapped = transfer.Map(false);
		if (!mapped)
			return Err(String("UploadTextureRegions : tampon de transfert inaccessible"));
		uint8_t *out = mapped.As<uint8_t>();
		for (const TextureRegionData &r : regions) {
			std::memcpy(out, r.bytes.data(), r.bytes.size());
			out += r.bytes.size();
		}
	}
	auto cmd = device.AcquireCommandBuffer();
	{
		sdl3::GpuCopyPass copyPass = cmd.BeginCopyPass();
		uint32_t offset = 0;
		for (const TextureRegionData &r : regions) {
			sdl3::GpuTextureTransferInfo src{transfer.Get(), offset, r.w, r.h};
			sdl3::GpuTextureRegion dst{texture.Get(), 0, 0, r.x, r.y, r.z, r.w, r.h, r.d};
			copyPass.UploadToTexture(src, dst, false);
			offset += uint32_t(r.bytes.size());
		}
	}
	if (!cmd.Submit())
		return Err(String(sdl3::GetError()));
	return Ok(true);
}

} // namespace render3d
