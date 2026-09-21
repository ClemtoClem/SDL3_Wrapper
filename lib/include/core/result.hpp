#pragma once
#include <functional>
#include <type_traits>
#include <variant>

// Déclaration anticipée pour l'interopérabilité
template <typename T> class Option;
struct NoneType;
template <typename T> struct Some;

// ---------------------------------------------------------------------------
// Idiome overloaded — requis pour std::visit avec lambdas multiples
// ---------------------------------------------------------------------------

template <typename... Ts> struct Overloaded : Ts... {
	using Ts::operator()...;
};

// Guide de déduction C++17
template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

// ---------------------------------------------------------------------------
// Ok<T> / Err<E>
// ---------------------------------------------------------------------------

/// Wrapper de succès.
template <typename T> struct Ok {
	T value;

	explicit Ok(T val) : value(std::move(val)) {}

	template <typename... Args> explicit Ok(std::in_place_t, Args &&...args) : value(std::forward<Args>(args)...) {}
};

/// Wrapper d'erreur.
template <typename E> struct Err {
	E error;

	explicit Err(E err) : error(std::move(err)) {}

	template <typename... Args> explicit Err(std::in_place_t, Args &&...args) : error(std::forward<Args>(args)...) {}
};

// Guides de déduction C++17
template <typename T> Ok(T) -> Ok<T>;
template <typename E> Err(E) -> Err<E>;

// ---------------------------------------------------------------------------
// Result<T, E>
// ---------------------------------------------------------------------------

/**
 * @brief Type somme représentant un succès `Ok<T>` ou une erreur `Err<E>`,
 * inspiré de `Result<T,E>` Rust.
 *
 * Garanties :
 * - Pas de valeur indéfinie — toujours Ok ou Err.
 * - `overloaded` défini dans ce fichier, aucune dépendance externe.
 * - Toutes les transformations propagent l'erreur sans branchement manuel.
 * - Interopérable avec `Option<T>` via `Ok()` et `Error()`.
 *
 * @code{.cpp}
 * Result<int, String> divide(int a, int b) {
 *     if (b == 0) return Err(String("division par zéro"));
 *     return Ok(a / b);
 * }
 *
 * // Chaînage
 * auto result = divide(10, 2)
 *     .map([](int v) { return v * 3; })
 *     .map_err([](const String& e) { return "Erreur : " + e; });
 *
 * // Pattern matching exhaustif
 * String msg = result.Match(
 *     [](int v)              { return String::From(v); },
 *     [](const String& e) { return e; }
 * );
 *
 * // Conversion vers Option
 * Option<int> opt = divide(10, 2).Ok();  // Some(5) ou NONE
 * @endcode
 */
template <typename T, typename E> class Result {

	static_assert(!std::is_reference_v<T> && !std::is_reference_v<E>,
				  "Result ne supporte pas les types référence directement.");

	std::variant<::Ok<T>, Err<E>> m_data;

public:
	// --- Static -----------------------------------------------------------------
	[[nodiscard]] static Result<std::decay_t<T>, void *> MakeOk(T &&val) = delete; // utiliser Ok{} directement

	template <typename... Args>
	[[nodiscard]] static Result<T, E> MakeOk(Args &&...args) {
		return Result<T, E>(Ok<T>(std::in_place, std::forward<Args>(args)...));
	}

	template <typename... Args>
	[[nodiscard]] static Result<T, E> MakeErr(Args &&...args) {
		return Result<T, E>(Err<E>(std::in_place, std::forward<Args>(args)...));
	}


	// --- Constructeurs -------------------------------------------------------

	Result(::Ok<T> ok) : m_data(std::move(ok)) {}
	Result(Err<E> err) : m_data(std::move(err)) {}

	// Copie / déplacement par défaut
	Result(const Result &) = default;
	Result &operator=(const Result &) = default;
	Result(Result &&) = default;
	Result &operator=(Result &&) = default;

	// --- Inspection ----------------------------------------------------------

	[[nodiscard]] bool IsOk() const noexcept { return std::holds_alternative<::Ok<T>>(m_data); }
	[[nodiscard]] bool IsError() const noexcept { return std::holds_alternative<Err<E>>(m_data); }

	[[nodiscard]] explicit operator bool() const noexcept { return IsOk(); }

	// --- Accès à la valeur ---------------------------------------------------

	/// Retourne une référence const à T — abort si Err.
	[[nodiscard]] const T &Value() const noexcept {
		if (!IsOk())
			std::abort();
		return std::get<::Ok<T>>(m_data).value;
	}
	[[nodiscard]] T &Value() noexcept {
		if (!IsOk())
			std::abort();
		return std::get<::Ok<T>>(m_data).value;
	}

	/// Retourne une référence const à E — abort si Ok.
	[[nodiscard]] const E &Error() const noexcept {
		if (!IsError())
			std::abort();
		return std::get<Err<E>>(m_data).error;
	}
	[[nodiscard]] E &Error() noexcept {
		if (!IsError())
			std::abort();
		return std::get<Err<E>>(m_data).error;
	}

	// --- Extraction ---------------------------------------------------------

	/// Extrait T ou abort.
	[[nodiscard]] T Unwrap() const & {
		if (!IsOk())
			std::abort();
		return std::get<::Ok<T>>(m_data).value;
	}
	[[nodiscard]] T Unwrap() && {
		if (!IsOk())
			std::abort();
		return std::move(std::get<::Ok<T>>(m_data).value);
	}

	/// Extrait T ou retourne `default_value`.
	[[nodiscard]] T UnwrapOr(T defaultValue) const & {
		return IsOk() ? std::get<::Ok<T>>(m_data).value : std::move(defaultValue);
	}
	[[nodiscard]] T UnwrapOr(T defaultValue) && {
		return IsOk() ? std::move(std::get<::Ok<T>>(m_data).value) : std::move(defaultValue);
	}

	/// Extrait T ou appelle `fn(E)`.
	template <typename Fn> [[nodiscard]] T UnwrapOrElse(Fn &&fn) const & {
		if (IsOk())
			return std::get<::Ok<T>>(m_data).value;
		return std::invoke(std::forward<Fn>(fn), std::get<Err<E>>(m_data).error);
	}
	template <typename Fn> [[nodiscard]] T UnwrapOrElse(Fn &&fn) && {
		if (IsOk())
			return std::move(std::get<::Ok<T>>(m_data).value);
		return std::invoke(std::forward<Fn>(fn), std::move(std::get<Err<E>>(m_data).error));
	}

	/// Extrait E ou abort.
	[[nodiscard]] E unwrap_error() const & {
		if (!IsError())
			std::abort();
		return std::get<Err<E>>(m_data).error;
	}
	[[nodiscard]] E UnwrapError() && {
		if (!IsError())
			std::abort();
		return std::move(std::get<Err<E>>(m_data).error);
	}

	// --- Pattern matching ---------------------------------------------------

	/**
	 * @brief Branchement exhaustif Ok / Err — les deux branches doivent retourner le même type.
	 */
	template <typename OkFn, typename ErrFn> [[nodiscard]] auto Match(OkFn &&onOk, ErrFn &&onErr) const & {
		return std::visit(
			Overloaded{[&](const ::Ok<T> &ok) { return std::invoke(std::forward<OkFn>(onOk), ok.value); },
					   [&](const Err<E> &err) { return std::invoke(std::forward<ErrFn>(onErr), err.error); }},
			m_data);
	}

	template <typename OkFn, typename ErrFn> [[nodiscard]] auto Match(OkFn &&onOk, ErrFn &&onErr) && {
		return std::visit(
			Overloaded{[&](::Ok<T> &&ok) { return std::invoke(std::forward<OkFn>(onOk), std::move(ok.value)); },
					   [&](Err<E> &&err) { return std::invoke(std::forward<ErrFn>(onErr), std::move(err.error)); }},
			std::move(m_data));
	}

	// --- Transformations fonctionnelles -------------------------------------

	/**
	 * @brief Applique `fn` à la valeur si Ok, propage Err sinon.
	 * @code
	 * Ok(3).map([](int v){ return v * 2; })   // → Ok(6)
	 * Err("oops").map([](int v){ return v; })  // → Err("oops")
	 * @endcode
	 */
	template <typename Fn> [[nodiscard]] auto Map(Fn &&fn) const & -> Result<std::invoke_result_t<Fn, const T &>, E> {
		using R = Result<std::invoke_result_t<Fn, const T &>, E>;
		if (IsOk())
			return R(::Ok(std::invoke(std::forward<Fn>(fn), std::get<::Ok<T>>(m_data).value)));
		return R(std::get<Err<E>>(m_data));
	}

	template <typename Fn> [[nodiscard]] auto Map(Fn &&fn) && -> Result<std::invoke_result_t<Fn, T &&>, E> {
		using R = Result<std::invoke_result_t<Fn, T &&>, E>;
		if (IsOk())
			return R(::Ok(std::invoke(std::forward<Fn>(fn), std::move(std::get<::Ok<T>>(m_data).value))));
		return R(std::move(std::get<Err<E>>(m_data)));
	}

	/**
	 * @brief Applique `fn` à l'erreur si Err, propage Ok sinon.
	 */
	template <typename Fn>
	[[nodiscard]] auto MapErr(Fn &&fn) const & -> Result<T, std::invoke_result_t<Fn, const E &>> {
		using R = Result<T, std::invoke_result_t<Fn, const E &>>;
		if (IsError())
			return R(Err(std::invoke(std::forward<Fn>(fn), std::get<Err<E>>(m_data).error)));
		return R(std::get<::Ok<T>>(m_data));
	}

	template <typename Fn> [[nodiscard]] auto MapErr(Fn &&fn) && -> Result<T, std::invoke_result_t<Fn, E &&>> {
		using R = Result<T, std::invoke_result_t<Fn, E &&>>;
		if (IsError())
			return R(Err(std::invoke(std::forward<Fn>(fn), std::move(std::get<Err<E>>(m_data).error))));
		return R(std::move(std::get<::Ok<T>>(m_data)));
	}

	/**
	 * @brief Chaîne un appel dont le résultat est déjà un `Result` (flatMap sur Ok).
	 * @code
	 * Ok(String("42"))
	 *     .AndThen([](const String& s) -> Result<int,String> {
	 *         try { return Ok(std::stoi(s)); }
	 *         catch(...) { return Err(String("parse error")); }
	 *     });
	 * @endcode
	 */
	template <typename Fn> [[nodiscard]] auto AndThen(Fn &&fn) const & -> std::invoke_result_t<Fn, const T &> {
		if (IsOk())
			return std::invoke(std::forward<Fn>(fn), std::get<::Ok<T>>(m_data).value);
		using R = std::invoke_result_t<Fn, const T &>;
		return R(std::get<Err<E>>(m_data));
	}

	template <typename Fn> [[nodiscard]] auto AndThen(Fn &&fn) && -> std::invoke_result_t<Fn, T &&> {
		if (IsOk())
			return std::invoke(std::forward<Fn>(fn), std::move(std::get<::Ok<T>>(m_data).value));
		using R = std::invoke_result_t<Fn, T &&>;
		return R(std::move(std::get<Err<E>>(m_data)));
	}

	/**
	 * @brief Chaîne un appel sur l'erreur si Err, propage Ok sinon (flatMap sur Err).
	 */
	template <typename Fn> [[nodiscard]] auto OrElse(Fn &&fn) const & -> std::invoke_result_t<Fn, const E &> {
		if (IsError())
			return std::invoke(std::forward<Fn>(fn), std::get<Err<E>>(m_data).error);
		using R = std::invoke_result_t<Fn, const E &>;
		return R(std::get<::Ok<T>>(m_data));
	}

	/// Prédicat combiné : IsOk() && predicate(value).
	template <typename Pred> [[nodiscard]] bool IsOkAnd(Pred &&pred) const {
		return IsOk() && std::invoke(std::forward<Pred>(pred), std::get<::Ok<T>>(m_data).value);
	}

	/// Prédicat combiné : IsError() && predicate(error).
	template <typename Pred> [[nodiscard]] bool IsErrorAnd(Pred &&pred) const {
		return IsError() && std::invoke(std::forward<Pred>(pred), std::get<Err<E>>(m_data).error);
	}

	// --- Interopérabilité avec Option<T> ------------------------------------

	/// Convertit Ok(v) → Some(v), Err(_) → NONE.
	[[nodiscard]] Option<T> Ok() const &;

	/// Convertit Err(e) → Some(e), Ok(_) → NONE.
	[[nodiscard]] Option<E> ErrorOption() const &;

	// --- Comparaisons -------------------------------------------------------

	[[nodiscard]] bool operator==(const Result &other) const { return m_data == other.m_data; }
	[[nodiscard]] bool operator!=(const Result &other) const { return !(*this == other); }
};