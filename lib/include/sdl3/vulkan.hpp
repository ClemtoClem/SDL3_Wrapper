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
	~VkLibrary();

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
	[[nodiscard]] static Result<VkLibrary, StringView> Load(const char *path = nullptr);
};

// ============================================================================
// Vulkan free functions — namespace vulkan
// ============================================================================

namespace vulkan {

// ── Instance proc address ─────────────────────────────────────────────────────

// Returns vkGetInstanceProcAddr as an opaque function pointer.
[[nodiscard]] SDL_FunctionPointer GetInstanceProcAddr() noexcept;

// ── Instance extensions ───────────────────────────────────────────────────────

// Returns the set of Vulkan instance extensions SDL requires on this platform.
// The returned span is valid until the next call to SDL_Vulkan_GetInstanceExtensions
// or until SDL is shut down.
[[nodiscard]] std::span<const char *const> InstanceExtensions() noexcept;

// ── Surface creation / destruction ────────────────────────────────────────────

// Create a Vulkan surface for the given window.
// `allocator` may be nullptr to use the default allocator.
[[nodiscard]] Result<VkSurfaceKHR, StringView> CreateSurface(SDL_Window *win, VkInstance instance,
																	const VkAllocationCallbacks *allocator = nullptr);

// Destroy a Vulkan surface that was created with createSurface().
// Must be called before SDL_DestroyWindow for the associated window.
void DestroySurface(VkInstance instance, VkSurfaceKHR surface,
						   const VkAllocationCallbacks *allocator = nullptr) noexcept;

// ── Presentation support ──────────────────────────────────────────────────────

// Returns true if the given physical device / queue family supports presentation.
[[nodiscard]] bool PresentationSupported(VkInstance instance, VkPhysicalDevice physicalDevice,
												uint32_t queueFamilyIndex) noexcept;

} // namespace vulkan
} // namespace sdl3
