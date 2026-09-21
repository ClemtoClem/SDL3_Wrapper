#pragma once
#include <type_traits>

// ---------------------------------------------------------------------------
// Ref<T> — emprunt en lecture seule (immutable borrow)
// ---------------------------------------------------------------------------

/**
 * @brief Référence non-propriétaire en lecture seule
 *
 * Garanties :
 * - Ne libère jamais la ressource (pas d'ownership).
 * - Interdit la construction depuis une rvalue (protection contre les dangling refs).
 * - Sémantiquement plus explicite que `const T*` ou `const T&` comme paramètre.
 *
 * @code{.cpp}
 * std::string s = "hello";
 * Ref<std::string> r(s);
 *
 * std::cout << r->size();  // accès direct
 * std::cout << (*r);       // déréférencement
 *
 * // Interdit — protège contre les temporaires :
 * // Ref<std::string> bad(std::string("tmp")); // erreur de compilation
 * @endcode
 */
template <typename T> class Ref {
	static_assert(!std::is_pointer_v<T>, "Ref<T*> est ambigu — utilisez Ref<T> sur la valeur pointée.");

	const T *m_ptr;

public:
	/// Construit depuis une lvalue const ou non-const.
	constexpr Ref(const T &resource) noexcept : m_ptr(&resource) {}

	/// Interdit la construction depuis une rvalue (temporaire).
	Ref(const T &&) = delete;

	// Copie autorisée (sémantique de référence).
	constexpr Ref(const Ref &) noexcept = default;
	constexpr Ref &operator=(const Ref &) noexcept = default;

	// --- Accès --------------------------------------------------------------

	[[nodiscard]] constexpr const T *Get() const noexcept { return m_ptr; }
	[[nodiscard]] constexpr const T &operator*() const noexcept { return *m_ptr; }
	[[nodiscard]] constexpr const T *operator->() const noexcept { return m_ptr; }

	/// Conversion implicite vers `const T&` pour compatibilité avec les APIs existantes.
	[[nodiscard]] constexpr operator const T &() const noexcept { return *m_ptr; }

	// --- Comparaisons (sur la valeur, pas le pointeur) ----------------------

	[[nodiscard]] constexpr bool operator==(const Ref &other) const noexcept { return *m_ptr == *other.m_ptr; }
	[[nodiscard]] constexpr bool operator!=(const Ref &other) const noexcept { return !(*this == other); }
	[[nodiscard]] constexpr bool operator==(const T &other) const noexcept { return *m_ptr == other; }
};

// ---------------------------------------------------------------------------
// RefMut<T> — emprunt en lecture/écriture (mutable borrow)
// ---------------------------------------------------------------------------

/**
 * @brief Référence non-propriétaire en lecture/écriture, inspirée de `&mut T` Rust.
 *
 * @code{.cpp}
 * int x = 10;
 * RefMut<int> r(x);
 * *r = 42;
 * r.replace(99);
 * @endcode
 */
template <typename T> class RefMut {
	static_assert(!std::is_const_v<T>,
				  "RefMut<const T> n'a pas de sens — utilisez Ref<T> pour une référence constante.");
	static_assert(!std::is_pointer_v<T>, "RefMut<T*> est ambigu — utilisez RefMut<T> sur la valeur pointée.");

	T *m_ptr;

public:
	constexpr explicit RefMut(T &resource) noexcept : m_ptr(&resource) {}

	/// Interdit la construction depuis une rvalue.
	RefMut(T &&) = delete;

	// Copie autorisée (sémantique de référence).
	constexpr RefMut(const RefMut &) noexcept = default;
	constexpr RefMut &operator=(const RefMut &) noexcept = default;

	// --- Accès --------------------------------------------------------------

	[[nodiscard]] constexpr T *Get() const noexcept { return m_ptr; }
	[[nodiscard]] constexpr T &operator*() const noexcept { return *m_ptr; }
	[[nodiscard]] constexpr T *operator->() const noexcept { return m_ptr; }

	/// Conversion implicite vers `T&`.
	[[nodiscard]] constexpr operator T &() const noexcept { return *m_ptr; }

	/// Conversion vers `Ref<T>` (downgrade vers lecture seule).
	[[nodiscard]] constexpr operator Ref<T>() const noexcept { return Ref<T>(*m_ptr); }

	// --- Mutation -----------------------------------------------------------

	/// Remplace la valeur pointée et retourne l'ancienne.
	[[nodiscard]] T Replace(T newValue) const {
		T old = std::move(*m_ptr);
		*m_ptr = std::move(newValue);
		return old;
	}

	/// Échange la valeur pointée avec `other`.
	void Swap(T &other) const noexcept {
		using std::swap;
		swap(*m_ptr, other);
	}

	// --- Comparaisons -------------------------------------------------------

	[[nodiscard]] constexpr bool operator==(const RefMut &other) const noexcept { return *m_ptr == *other.m_ptr; }
	[[nodiscard]] constexpr bool operator!=(const RefMut &other) const noexcept { return !(*this == other); }
};

// ---------------------------------------------------------------------------
// Helpers de fabrication (déduction de type automatique)
// ---------------------------------------------------------------------------

/// Crée un `Ref<T>` avec déduction de type.
template <typename T> [[nodiscard]] constexpr Ref<T> MakeRef(const T &val) noexcept { return Ref<T>(val); }

/// Crée un `RefMut<T>` avec déduction de type.
template <typename T> [[nodiscard]] constexpr RefMut<T> MakeRefMut(T &val) noexcept { return RefMut<T>(val); }