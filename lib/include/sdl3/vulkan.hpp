#pragma once
#include <SDL3/SDL_vulkan.h>
#include <span>

#include "../core/core.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// VkLibrary — RAII Vulkan loader library (SDL_Vulkan_LoadLibrary / UnloadLibrary)
// ============================================================================

class VkLibrary {
	bool loaded = false;

public:
	constexpr VkLibrary() noexcept = default;
	explicit VkLibrary(bool loaded) noexcept : loaded(loaded) {}
	~VkLibrary() {
		if (loaded)
			SDL_Vulkan_UnloadLibrary();
	}

	VkLibrary(const VkLibrary &) = delete;
	VkLibrary &operator=(const VkLibrary &) = delete;
	VkLibrary(VkLibrary &&o) noexcept : loaded(o.loaded) { o.loaded = false; }
	VkLibrary &operator=(VkLibrary &&o) noexcept {
		if (this != &o) {
			if (loaded)
				SDL_Vulkan_UnloadLibrary();
			loaded = o.loaded;
			o.loaded = false;
		}
		return *this;
	}

	[[nodiscard]] explicit operator bool() const noexcept { return loaded; }

	// Load the Vulkan loader library. Pass nullptr to use the system default.
	[[nodiscard]] static Result<VkLibrary, StringView> Load(const char *path = nullptr) {
		if (!SDL_Vulkan_LoadLibrary(path))
			return Err(GetError());
		return Ok(VkLibrary(true));
	}
};

// ============================================================================
// Vulkan free functions — namespace vulkan
// ============================================================================

namespace vulkan {

// ── Instance proc address ─────────────────────────────────────────────────────

// Returns vkGetInstanceProcAddr as an opaque function pointer.
[[nodiscard]] inline SDL_FunctionPointer GetInstanceProcAddr() noexcept {
	return SDL_Vulkan_GetVkGetInstanceProcAddr();
}

// ── Instance extensions ───────────────────────────────────────────────────────

// Returns the set of Vulkan instance extensions SDL requires on this platform.
// The returned span is valid until the next call to SDL_Vulkan_GetInstanceExtensions
// or until SDL is shut down.
[[nodiscard]] inline std::span<const char *const> InstanceExtensions() noexcept {
	Uint32 count = 0;
	const char *const *exts = SDL_Vulkan_GetInstanceExtensions(&count);
	if (!exts)
		return {};
	return {exts, count};
}

// ── Surface creation / destruction ────────────────────────────────────────────

// Create a Vulkan surface for the given window.
// `allocator` may be nullptr to use the default allocator.
[[nodiscard]] inline Result<VkSurfaceKHR, StringView> CreateSurface(SDL_Window *win, VkInstance instance,
																	const VkAllocationCallbacks *allocator = nullptr) {
	VkSurfaceKHR surface{};
	if (!SDL_Vulkan_CreateSurface(win, instance, allocator, &surface))
		return Err(GetError());
	return Ok(surface);
}

// Destroy a Vulkan surface that was created with createSurface().
// Must be called before SDL_DestroyWindow for the associated window.
inline void DestroySurface(VkInstance instance, VkSurfaceKHR surface,
						   const VkAllocationCallbacks *allocator = nullptr) noexcept {
	SDL_Vulkan_DestroySurface(instance, surface, allocator);
}

// ── Presentation support ──────────────────────────────────────────────────────

// Returns true if the given physical device / queue family supports presentation.
[[nodiscard]] inline bool PresentationSupported(VkInstance instance, VkPhysicalDevice physicalDevice,
												uint32_t queueFamilyIndex) noexcept {
	return SDL_Vulkan_GetPresentationSupport(instance, physicalDevice, queueFamilyIndex);
}

} // namespace vulkan
} // namespace sdl3
