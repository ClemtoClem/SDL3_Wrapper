#pragma once
/**
 * sql::Table<Row> — small in-process, row-shaped store with stable ids that
 * survive deletion. Insert/Get/Delete/Query only — this is NOT a SQL engine
 * (see sql.hpp's module doc comment for the full rationale).
 *
 * Mirrors this repo's own established pattern for stable-id-under-deletion
 * storage (cf. ecs::EntityAllocator, ecs/ecs.hpp) but simpler: no
 * generation/reuse — a deleted slot is tombstoned forever and its id is
 * never reassigned to a different Row, so any id ever returned by Insert()
 * remains a safe, stable handle for the lifetime of the Table.
 *
 * Table<Row> is deliberately persistence-agnostic: no data::/CSV dependency
 * here. CSV persistence is layered on top in storage.hpp via explicit
 * caller-supplied conversion functions, so a Table<Row> of an arbitrary
 * in-memory Row type works standalone.
 */
#include "../core/core.hpp"

#include <functional>
#include <vector>

namespace sql {

template <typename Row> class Table {
public:
	/// Appends a new row and returns its stable id — monotonically
	/// increasing, never reused even after Delete().
	size_t Insert(Row row) {
		size_t id = m_rows.size();
		m_rows.push_back(Some<Row>(std::move(row)));
		return id;
	}

	/// Read-only access to a live row, or NONE if `id` is out of range or
	/// was deleted.
	[[nodiscard]] Option<Ref<Row>> Get(size_t id) const {
		if (id >= m_rows.size() || m_rows[id].IsNone())
			return NONE;
		return Some(Ref<Row>(*m_rows[id]));
	}

	/// Read/write access to a live row, or NONE if `id` is out of range or
	/// was deleted.
	[[nodiscard]] Option<RefMut<Row>> Get(size_t id) {
		if (id >= m_rows.size() || m_rows[id].IsNone())
			return NONE;
		return Some(RefMut<Row>(*m_rows[id]));
	}

	/// Tombstones the slot at `id` — the row's storage is released but the
	/// id itself is never reassigned. Returns false if `id` was already
	/// deleted or is out of range.
	bool Delete(size_t id) {
		if (id >= m_rows.size() || m_rows[id].IsNone())
			return false;
		m_rows[id] = NONE;
		return true;
	}

	/// Predicate-based filter over live (non-tombstoned) rows only — a
	/// tombstoned row is never considered, regardless of what its
	/// leftover in-memory data would otherwise match. Returns matching ids
	/// in ascending order; callers then Get() each one.
	template <typename Predicate> [[nodiscard]] std::vector<size_t> Query(Predicate pred) const {
		std::vector<size_t> ids;
		for (size_t id = 0; id < m_rows.size(); ++id) {
			if (m_rows[id].IsSome() && std::invoke(pred, *m_rows[id]))
				ids.push_back(id);
		}
		return ids;
	}

	/// Read-only iteration over live rows in id order — `fn(id, const
	/// Row&)`. Used by storage.hpp to walk the table for CSV export
	/// without needing to know Row's field layout.
	template <typename Fn> void ForEachLive(Fn fn) const {
		for (size_t id = 0; id < m_rows.size(); ++id) {
			if (m_rows[id].IsSome())
				std::invoke(fn, id, *m_rows[id]);
		}
	}

	/// Number of live (non-tombstoned) rows.
	[[nodiscard]] size_t Size() const noexcept {
		size_t n = 0;
		for (auto &slot : m_rows)
			if (slot.IsSome())
				++n;
		return n;
	}

	[[nodiscard]] bool IsEmpty() const noexcept { return Size() == 0; }

private:
	std::vector<Option<Row>> m_rows;
};

} // namespace sql
