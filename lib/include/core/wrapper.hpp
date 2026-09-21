#pragma once
#include <span>
#include <utility>
#include <vector>

#include "ref.hpp"

/**
 * @brief Classe de base RAII pour envelopper les pointeurs de bibliothèques C.
 * * @tparam T Le type de la structure C (ex: SDL_Window)
 * @tparam Deleter La fonction C à appeler pour libérer la ressource (ex: SDL_DestroyWindow)
 */
template <typename T, auto Deleter> class Wrapper {
protected:
	T *m_handle = nullptr;

public:
	// --- Constructeurs ---
	constexpr Wrapper() noexcept = default;
	constexpr explicit Wrapper(T *m_handle) noexcept : m_handle(m_handle) {}

	// Interdire la copie : on ne peut pas dupliquer magiquement une ressource C (comme une fenêtre)
	Wrapper(const Wrapper &) = delete;
	Wrapper &operator=(const Wrapper &) = delete;

	// Autoriser le déplacement (Move semantics) : transfère la propriété de la ressource
	constexpr Wrapper(Wrapper &&other) noexcept : m_handle(other.Release()) {}

	/**
	 * @brief Construit un Wrapper depuis un emprunt mutable (Transfert de propriété !)
	 * @warning Le Wrapper devient propriétaire et détruira la ressource à sa mort.
	 */
	constexpr explicit Wrapper(RefMut<T> ref) noexcept : m_handle(ref.Get()) {}

	constexpr Wrapper &operator=(Wrapper &&other) noexcept {
		if (this != &other) {
			Reset(other.Release());
		}
		return *this;
	}

	// --- Destructeur RAII ---
	~Wrapper() { Reset(); }

	// --- Génération d'emprunts ---

	/**
	 * @brief Prête la ressource en lecture seule.
	 * @return Option<Ref<T>> Retourne `NONE` si le wrapper est vide, garantissant une sécurité totale.
	 */
	[[nodiscard]] Option<Ref<T>> AsRef() const noexcept {
		if (m_handle) {
			return Some(MakeRef(*m_handle));
		}
		return NONE;
	}

	// --- Gestion de la ressource ---

	/// Détruit la ressource actuelle (si elle existe) et en prend éventuellement une nouvelle
	void Reset(T *newHandle = nullptr) noexcept {
		if (m_handle) {
			Deleter(m_handle);
		}
		m_handle = newHandle;
	}

	/// Relâche la propriété de la ressource sans la détruire
	[[nodiscard]] T *Release() noexcept {
		T *temp = m_handle;
		m_handle = nullptr;
		return temp;
	}

	// --- Accès ---

	[[nodiscard]] T *Get() const noexcept { return m_handle; }

	/// Permet de vérifier facilement si la ressource est valide : `if (window) { ... }`
	[[nodiscard]] explicit operator bool() const noexcept { return m_handle != nullptr; }

	/// Conversion implicite vers le pointeur C pour faciliter l'appel aux fonctions de la SDL
	[[nodiscard]] operator T *() const noexcept { return m_handle; }
};

/**
 * @brief Classe de base RAII pour envelopper les pointeurs de bibliothèques C
 * dont le deleter a besoin de DEUX arguments (ex: `SDL_ReleaseGPUBuffer(device,
 * buffer)`) — variante à deux arguments de `Wrapper<T, Deleter>` ci-dessus.
 * @tparam D Le type du "device" propriétaire (ex: SDL_GPUDevice), non possédé ici.
 * @tparam T Le type de la ressource C (ex: SDL_GPUBuffer)
 * @tparam Deleter La fonction C à appeler pour libérer la ressource (ex: SDL_ReleaseGPUBuffer)
 */
template <typename D, typename T, auto Deleter> class DeviceWrapper {
protected:
	D *m_device = nullptr;
	T *m_handle = nullptr;

public:
	// --- Constructeurs ---
	constexpr DeviceWrapper() noexcept = default;
	constexpr explicit DeviceWrapper(D *device, T *handle) noexcept : m_device(device), m_handle(handle) {}

	// Interdire la copie : on ne peut pas dupliquer magiquement une ressource C (comme une fenêtre)
	DeviceWrapper(const DeviceWrapper &) = delete;
	DeviceWrapper &operator=(const DeviceWrapper &) = delete;

	// Autoriser le déplacement (Move semantics) : transfère la propriété de la ressource
	constexpr DeviceWrapper(DeviceWrapper &&other) noexcept : m_device(other.m_device), m_handle(other.Release()) {}

	/**
	 * @brief Construit un DeviceWrapper depuis un device et un emprunt mutable
	 * (Transfert de propriété !)
	 * @warning Le DeviceWrapper devient propriétaire et détruira la ressource à sa mort.
	 */
	constexpr explicit DeviceWrapper(D *device, RefMut<T> ref) noexcept : m_device(device), m_handle(ref.Get()) {}

	constexpr DeviceWrapper &operator=(DeviceWrapper &&other) noexcept {
		if (this != &other) {
			m_device = other.m_device;
			Reset(other.Release());
		}
		return *this;
	}

	// --- Destructeur RAII ---
	~DeviceWrapper() { Reset(); }

	// --- Génération d'emprunts ---

	/**
	 * @brief Prête la ressource en lecture seule.
	 * @return Option<Ref<T>> Retourne `NONE` si le wrapper est vide, garantissant une sécurité totale.
	 */
	[[nodiscard]] Option<Ref<T>> AsRef() const noexcept {
		if (m_device && m_handle) {
			return Some(MakeRef(*m_handle));
		}
		return NONE;
	}

	// --- Gestion de la ressource ---

	/// Détruit la ressource actuelle (si elle existe) et en prend éventuellement une nouvelle
	void Reset(T *newHandle = nullptr) noexcept {
		if (m_device && m_handle) {
			Deleter(m_device, m_handle);
		}
		m_handle = newHandle;
	}

	/// Relâche la propriété de la ressource sans la détruire
	[[nodiscard]] T *Release() noexcept {
		T *temp = m_handle;
		m_handle = nullptr;
		return temp;
	}

	// --- Accès ---

	[[nodiscard]] D *GetDevice() const noexcept { return m_device; }

	[[nodiscard]] T *Get() const noexcept { return m_handle; }

	/// Permet de vérifier facilement si la ressource est valide : `if (buffer) { ... }`
	[[nodiscard]] explicit operator bool() const noexcept { return (m_device != nullptr) && (m_handle != nullptr); }

	/// Conversion implicite vers le pointeur C pour faciliter l'appel aux fonctions de la SDL
	[[nodiscard]] operator T *() const noexcept { return m_handle; }
};

/**
 * @brief Vue non-possédante sur une ressource C (ex: un SDL_IOStream prêté par
 * un processus, un SDL_Sensor déjà ouvert ailleurs).
 *
 * Contrairement à `Wrapper<T, Deleter>`, ne libère jamais la ressource à la
 * destruction. Sert de classe de base pour exposer une API typée sans jamais
 * faire apparaître le pointeur C brut dans les signatures publiques.
 */
template <typename T> class Borrowed {
protected:
	T *m_handle = nullptr;

public:
	constexpr Borrowed() noexcept = default;
	constexpr explicit Borrowed(T *m_handle) noexcept : m_handle(m_handle) {}

	[[nodiscard]] T *Get() const noexcept { return m_handle; }
	[[nodiscard]] explicit operator bool() const noexcept { return m_handle != nullptr; }
	[[nodiscard]] operator T *() const noexcept { return m_handle; }
};


namespace detail {
/// Unwraps a span of borrowed wrapper refs (Ref<GpuTexture>, Ref<GpuBuffer>,
/// Ref<GpuFence>...) into the raw m_handle array the C API expects — keeps raw
/// SDL_GPU* pointers out of every public bind*/wait* signature below.
template <typename Wrapped> [[nodiscard]] inline auto ToRawHandles(std::span<const Ref<Wrapped>> refs) {
	std::vector<decltype(std::declval<const Wrapped &>().Get())> out;
	out.reserve(refs.size());
	for (const Ref<Wrapped> &r : refs)
		out.push_back(r->Get());
	return out;
}

} // namespace detail