#pragma once
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>

// Déclarations anticipées pour l'interopérabilité
template <typename T, typename E> class Result;
template <typename T> struct Ok;
template <typename E> struct Err;

// ---------------------------------------------------------------------------
// NONE / Some
// ---------------------------------------------------------------------------

/// Tag représentant l'absence de valeur.
struct NoneType {
	constexpr NoneType() noexcept = default;
};
inline constexpr NoneType NONE{};

/// Wrapper portant une valeur présente.
template <typename T> struct Some {
	T value;

	explicit Some(T val) : value(std::move(val)) {}

	template <typename... Args> explicit Some(std::in_place_t, Args &&...args) : value(std::forward<Args>(args)...) {}
};

template <typename T> Some(T) -> Some<T>;

// ---------------------------------------------------------------------------
// Exception
// ---------------------------------------------------------------------------

/// Exception levée lors de l'accès à une valeur absente.
class BadOptionAccess : public std::exception {
public:
	const char *what() const noexcept override { return "Option: attempt to access value of a NONE"; }
};

// ---------------------------------------------------------------------------
// Option<T>
// ---------------------------------------------------------------------------

template <typename T> class Option {
	static_assert(!std::is_reference_v<T>, "Option<T&> interdit — utilisez Option<std::reference_wrapper<T>> "
										   "ou Ref<T> pour les références.");

	// Stockage sous-jasant via union anonyme pour gérer le cycle de vie manuellement
	union {
		T value;
		char dummy; // Utilisé pour l'état NONE
	};
	bool m_isSome;

public:
	// --- Constructeurs / Destructeur -----------------------------------------

	constexpr Option() noexcept : dummy{}, m_isSome(false) {}

	constexpr Option(NoneType) noexcept : dummy{}, m_isSome(false) {}

	Option(Some<T> some) : value(std::move(some.value)), m_isSome(true) {}

	template <typename... Args>
	explicit Option(std::in_place_t, Args &&...args) : value(std::forward<Args>(args)...), m_isSome(true) {}

	// Destructeur : détruit T uniquement s'il est actif
	~Option() {
		if constexpr (!std::is_trivially_destructible_v<T>) {
			if (m_isSome)
				value.~T();
		}
	}

	// Copie
	Option(const Option &other) noexcept(std::is_nothrow_copy_constructible_v<T>) : m_isSome(other.m_isSome) {
		if (m_isSome) {
			new (&value) T(other.value);
		}
	}

	Option &operator=(const Option &other) noexcept(std::is_nothrow_copy_constructible_v<T> &&
													std::is_nothrow_copy_assignable_v<T>) {
		if (this != &other) {
			if (m_isSome) {
				if (other.m_isSome) {
					value = other.value; // Copy-assign
				} else {
					value.~T(); // Détruit l'ancien
					m_isSome = false;
				}
			} else {
				if (other.m_isSome) {
					new (&value) T(other.value); // Copy-construct
					m_isSome = true;
				}
			}
		}
		return *this;
	}

	// Déplacement
	Option(Option &&other) noexcept(std::is_nothrow_move_constructible_v<T>) : m_isSome(other.m_isSome) {
		if (m_isSome) {
			new (&value) T(std::move(other.value));
		}
	}

	Option &operator=(Option &&other) noexcept(std::is_nothrow_move_constructible_v<T> &&
											   std::is_nothrow_move_assignable_v<T>) {
		if (this != &other) {
			if (m_isSome) {
				if (other.m_isSome) {
					value = std::move(other.value); // Move-assign
				} else {
					value.~T();
					m_isSome = false;
				}
			} else {
				if (other.m_isSome) {
					new (&value) T(std::move(other.value)); // Move-construct
					m_isSome = true;
				}
			}
		}
		return *this;
	}

	// Assignation depuis NONE ou Some
	Option &operator=(NoneType) noexcept {
		if (m_isSome) {
			if constexpr (!std::is_trivially_destructible_v<T>) {
				value.~T();
			}
			m_isSome = false;
		}
		return *this;
	}

	Option &operator=(Some<T> some) {
		if (m_isSome) {
			value = std::move(some.value);
		} else {
			new (&value) T(std::move(some.value));
			m_isSome = true;
		}
		return *this;
	}

	// --- Inspection ----------------------------------------------------------

	[[nodiscard]] constexpr bool IsSome() const noexcept { return m_isSome; }
	[[nodiscard]] constexpr bool IsNone() const noexcept { return !m_isSome; }
	[[nodiscard]] explicit constexpr operator bool() const noexcept { return IsSome(); }

	// --- Accès --------------------------------------------------------------

	[[nodiscard]] const T &ValueUnchecked() const noexcept { return value; }
	[[nodiscard]] T &ValueUnchecked() noexcept { return value; }

	/// Accès vérifié — Lève BadOptionAccess si NONE.
	[[nodiscard]] const T &Value() const {
		if (!IsSome())
			throw BadOptionAccess{};
		return value;
	}
	[[nodiscard]] T &Value() {
		if (!IsSome())
			throw BadOptionAccess{};
		return value;
	}

	/// Déréférencement direct (abort si NONE, car noexcept).
	[[nodiscard]] const T &operator*() const noexcept {
		if (!IsSome())
			std::abort();
		return value;
	}
	[[nodiscard]] T &operator*() noexcept {
		if (!IsSome())
			std::abort();
		return value;
	}
	[[nodiscard]] const T *operator->() const noexcept {
		if (!IsSome())
			std::abort();
		return &value;
	}
	[[nodiscard]] T *operator->() noexcept {
		if (!IsSome())
			std::abort();
		return &value;
	}

	// --- Extraction ---------------------------------------------------------

	[[nodiscard]] T Unwrap() && {
		if (!IsSome())
			throw BadOptionAccess{};
		T val = std::move(value);
		// Optionnelle : remettre l'Option dans un état valide (NONE)
		if constexpr (!std::is_trivially_destructible_v<T>) {
			value.~T();
		}
		m_isSome = false;
		return val;
	}

	[[nodiscard]] T Unwrap() const & {
		if (!IsSome())
			throw BadOptionAccess{};
		return value;
	}

	[[nodiscard]] T UnwrapOr(T defaultValue) const & { return m_isSome ? value : std::move(defaultValue); }

	[[nodiscard]] T UnwrapOr(T defaultValue) && { return m_isSome ? std::move(value) : std::move(defaultValue); }

	template <typename Fn> [[nodiscard]] T UnwrapOrElse(Fn &&fn) const & {
		return m_isSome ? value : std::invoke(std::forward<Fn>(fn));
	}

	template <typename Fn> [[nodiscard]] T UnwrapOrElse(Fn &&fn) && {
		return m_isSome ? std::move(value) : std::invoke(std::forward<Fn>(fn));
	}

	// --- Pattern matching ---------------------------------------------------

	template <typename SomeFn, typename NoneFn> [[nodiscard]] auto Match(SomeFn &&onSome, NoneFn &&onNone) const & {
		if (m_isSome)
			return std::invoke(std::forward<SomeFn>(onSome), value);
		else
			return std::invoke(std::forward<NoneFn>(onNone));
	}

	template <typename SomeFn, typename NoneFn> [[nodiscard]] auto Match(SomeFn &&onSome, NoneFn &&onNone) && {
		if (m_isSome)
			return std::invoke(std::forward<SomeFn>(onSome), std::move(value));
		else
			return std::invoke(std::forward<NoneFn>(onNone));
	}

	// --- Transformations fonctionnelles -------------------------------------

	template <typename Fn> [[nodiscard]] auto Map(Fn &&fn) const & -> Option<std::invoke_result_t<Fn, const T &>> {
		using U = std::invoke_result_t<Fn, const T &>;
		if (m_isSome)
			return Option<U>(Some(std::invoke(std::forward<Fn>(fn), value)));
		return NONE;
	}

	template <typename Fn> [[nodiscard]] auto Map(Fn &&fn) && -> Option<std::invoke_result_t<Fn, T &&>> {
		using U = std::invoke_result_t<Fn, T &&>;
		if (m_isSome)
			return Option<U>(Some(std::invoke(std::forward<Fn>(fn), std::move(value))));
		return NONE;
	}

	template <typename Fn> [[nodiscard]] auto AndThen(Fn &&fn) const & -> std::invoke_result_t<Fn, const T &> {
		// using R = std::invoke_result_t<Fn, const T&>;
		if (m_isSome)
			return std::invoke(std::forward<Fn>(fn), value);
		return NONE;
	}

	template <typename Fn> [[nodiscard]] auto AndThen(Fn &&fn) && -> std::invoke_result_t<Fn, T &&> {
		// using R = std::invoke_result_t<Fn, T&&>;
		if (m_isSome)
			return std::invoke(std::forward<Fn>(fn), std::move(value));
		return NONE;
	}

	template <typename Fn> [[nodiscard]] Option OrElse(Fn &&fn) const & {
		if (m_isSome)
			return *this;
		return std::invoke(std::forward<Fn>(fn));
	}

	template <typename Fn> [[nodiscard]] Option OrElse(Fn &&fn) && {
		if (m_isSome)
			return std::move(*this);
		return std::invoke(std::forward<Fn>(fn));
	}

	template <typename Pred> [[nodiscard]] Option Filter(Pred &&pred) const & {
		if (m_isSome && std::invoke(std::forward<Pred>(pred), value))
			return *this;
		return NONE;
	}

	// --- Interopérabilité avec Result<T,E> ----------------------------------

	template <typename E> [[nodiscard]] Result<T, E> OkOr(E err) const &;

	template <typename E, typename Fn> [[nodiscard]] Result<T, E> OkOrElse(Fn &&fn) const &;

	// --- Comparaisons -------------------------------------------------------

	[[nodiscard]] bool operator==(const Option &other) const {
		return m_isSome == other.m_isSome && (!m_isSome || value == other.value);
	}
	[[nodiscard]] bool operator!=(const Option &other) const { return !(*this == other); }
	[[nodiscard]] bool operator==(NoneType) const noexcept { return IsNone(); }
	[[nodiscard]] bool operator!=(NoneType) const noexcept { return IsSome(); }

	// Comparaison symétrique pour NONE
	friend bool operator==(NoneType, const Option &opt) noexcept { return opt.IsNone(); }
	friend bool operator!=(NoneType, const Option &opt) noexcept { return opt.IsSome(); }
};

// Helpers de fabrication sans spécification du type
template <typename T> [[nodiscard]] Option<std::decay_t<T>> MakeSome(T &&val) {
	return Option<std::decay_t<T>>(Some<std::decay_t<T>>(std::forward<T>(val)));
}

template <typename T, typename... Args> [[nodiscard]] Option<T> MakeSomeInPlace(Args &&...args) {
	return Option<T>(std::in_place, std::forward<Args>(args)...);
}