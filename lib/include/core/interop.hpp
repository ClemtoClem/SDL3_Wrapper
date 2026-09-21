#pragma once
#include "option.hpp"
#include "result.hpp"
#include "string.hpp"

using Error = StringView;

// ---------------------------------------------------------------------------
// Définitions croisées Option ↔ Result
// ---------------------------------------------------------------------------

// --- Option<T>::OkOr -------------------------------------------------------

// `Ok<T>(value)` et non `Ok<T>(*value)` : `Option<T>::value` EST le T stocké
// (membre d'une union anonyme, cf. option.hpp), pas un pointeur vers lui. Le
// déréférencement ne compilait pour aucun T — sauf à ce que T soit lui-même
// déréférençable, auquel cas il donnait le MAUVAIS type (`Ok<RefMut<X>>`
// construit depuis un `X&`). Ces deux fonctions n'avaient donc jamais été
// instanciées ailleurs que dans tests/ecs_smoke_test.cpp, qui ne compilait
// plus depuis (rupture connue et signalée jusque dans le Makefile).
template <typename T> template <typename E> Result<T, E> Option<T>::OkOr(E err) const & {
	if (IsSome())
		return Result<T, E>(Ok<T>(value));
	return Result<T, E>(Err<E>(std::move(err)));
}

template <typename T> template <typename E, typename Fn> Result<T, E> Option<T>::OkOrElse(Fn &&fn) const & {
	if (IsSome())
		return Result<T, E>(Ok<T>(value));
	return Result<T, E>(Err<E>(std::invoke(std::forward<Fn>(fn))));
}

// --- Result<T,E>::Ok() / Error() ---------------------------------------------

template <typename T, typename E> Option<T> Result<T, E>::Ok() const & {
	if (IsOk())
		return Option<T>(Some<T>(std::get<::Ok<T>>(m_data).value));
	return Option<T>(NONE);
}

template <typename T, typename E> Option<E> Result<T, E>::ErrorOption() const & {
	if (IsError())
		return Option<E>(Some<E>(std::get<Err<E>>(m_data).Error));
	return Option<E>(NONE);
}