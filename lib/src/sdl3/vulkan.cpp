// Définitions de sdl3/vulkan.hpp
// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include "sdl3/sdl3.hpp"
#include "sdl3/vulkan.hpp"

namespace sdl3 {

// ── VkLibrary ────────────────────────────────────────────────────────────────

VkLibrary::~VkLibrary() {
	if (loaded)
		SDL_Vulkan_UnloadLibrary();
}

Result<VkLibrary, StringView> VkLibrary::Load(const char *path) {
	if (!SDL_Vulkan_LoadLibrary(path))
		return Err(GetError());
	return Ok(VkLibrary(true));
}

namespace vulkan {

SDL_FunctionPointer GetInstanceProcAddr() noexcept {
	return SDL_Vulkan_GetVkGetInstanceProcAddr();
}

std::span<const char *const> InstanceExtensions() noexcept {
	Uint32 count = 0;
	const char *const *exts = SDL_Vulkan_GetInstanceExtensions(&count);
	if (!exts)
		return {};
	return {exts, count};
}

Result<VkSurfaceKHR, StringView> CreateSurface(SDL_Window *win, VkInstance instance,
		const VkAllocationCallbacks *allocator) {
	VkSurfaceKHR surface{};
	if (!SDL_Vulkan_CreateSurface(win, instance, allocator, &surface))
		return Err(GetError());
	return Ok(surface);
}

void DestroySurface(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *allocator) noexcept {
	SDL_Vulkan_DestroySurface(instance, surface, allocator);
}

bool PresentationSupported(VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex) noexcept {
	return SDL_Vulkan_GetPresentationSupport(instance, physicalDevice, queueFamilyIndex);
}

} // namespace vulkan

} // namespace sdl3
