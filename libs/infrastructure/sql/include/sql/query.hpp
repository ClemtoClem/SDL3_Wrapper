#pragma once
/**
 * sql:: predicate combinators for Table<Row>::Query — deliberately tiny:
 * ordinary function-object composition, not a query language. A single
 * predicate lambda already reads perfectly well on its own (that's what
 * Table<Row>::Query takes), so these exist only for the genuinely useful
 * cases: matching everything, and combining two predicates without
 * repeating boilerplate at every call site.
 */
#include <functional>
#include <utility>

namespace sql {

/// Matches every row — useful as an explicit "select all" Query() argument.
template <typename Row> [[nodiscard]] auto All() {
	return [](const Row &) { return true; };
}

/// Composes two predicates: matches rows both `fn1` and `fn2` accept.
template <typename Fn1, typename Fn2> [[nodiscard]] auto And(Fn1 fn1, Fn2 fn2) {
	return [fn1 = std::move(fn1), fn2 = std::move(fn2)](const auto &row) {
		return std::invoke(fn1, row) && std::invoke(fn2, row);
	};
}

/// Composes two predicates: matches rows either `fn1` or `fn2` accepts.
template <typename Fn1, typename Fn2> [[nodiscard]] auto Or(Fn1 fn1, Fn2 fn2) {
	return [fn1 = std::move(fn1), fn2 = std::move(fn2)](const auto &row) {
		return std::invoke(fn1, row) || std::invoke(fn2, row);
	};
}

} // namespace sql
