#pragma once
#include "resource.hpp"
#include <concepts>
#include <memory>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "../core/core.hpp"

namespace resources {

// ============================================================================
// Pool<T> — strongly-typed thread-safe named resource map.
// ============================================================================
template <std::derived_from<Resource> T> class Pool {
public:
	bool Insert(String name, std::shared_ptr<T> res) {
		std::scoped_lock lock(mu);
		return map.emplace(std::move(name), std::move(res)).second;
	}
	void Upsert(String name, std::shared_ptr<T> res) {
		std::scoped_lock lock(mu);
		map.insert_or_assign(std::move(name), std::move(res));
	}
	template <std::invocable<const String &> Factory>
	std::shared_ptr<T> GetOrInsert(const String &name, Factory &&factory) {
		{
			std::scoped_lock lk(mu);
			if (auto it = map.find(name); it != map.end())
				return it->second;
		}
		auto res = factory(name);
		std::scoped_lock lk(mu);
		return map.emplace(name, std::move(res)).first->second;
	}
	bool Remove(const String &name) {
		std::scoped_lock lock(mu);
		return map.erase(name) > 0;
	}
	void Clear() {
		std::scoped_lock lock(mu);
		map.clear();
	}

	/// `NONE` si absent — pas de `nullptr` nu qui fuite hors de l'abstraction
	/// `Option<T>` déjà établie ailleurs (cf. `sdl3::filesystem::PrefPath`).
	[[nodiscard]] Option<std::shared_ptr<T>> Get(const String &name) const {
		std::scoped_lock lock(mu);
		auto it = map.find(name);
		if (it == map.end())
			return NONE;
		return Some(it->second);
	}
	[[nodiscard]] bool Contains(const String &name) const {
		std::scoped_lock lk(mu);
		return map.contains(name);
	}
	[[nodiscard]] size_t GetSize() const {
		std::scoped_lock lk(mu);
		return map.size();
	}

	template <std::invocable<const String &, const std::shared_ptr<T> &> Fn> void ForEach(Fn &&fn) const {
		std::vector<std::pair<String, std::shared_ptr<T>>> snap;
		{
			std::scoped_lock lk(mu);
			snap.reserve(map.size());
			for (auto &[k, v] : map)
				snap.emplace_back(k, v);
		}
		for (auto &[k, v] : snap)
			fn(k, v);
	}

private:
	mutable std::mutex mu;
	std::unordered_map<String, std::shared_ptr<T>> map;
};

// ============================================================================
// Registry — heterogeneous type-keyed collection of Pool<T>.
// ============================================================================
class Registry {
	struct IErased {
		virtual ~IErased() = default;
		virtual void Clear() = 0;
	};
	template <std::derived_from<Resource> T> struct Slot : IErased {
		Pool<T> pool;
		void Clear() override { pool.Clear(); }
	};

public:
	Registry() = default;
	Registry(const Registry &) = delete;
	Registry &operator=(const Registry &) = delete;
	Registry(Registry &&) noexcept = default;
	Registry &operator=(Registry &&) noexcept = default;

	template <std::derived_from<Resource> T> [[nodiscard]] Pool<T> &GetPool() {
		auto key = std::type_index(typeid(T));
		std::scoped_lock lk(mu);
		auto it = slots.find(key);
		if (it == slots.end())
			it = slots.emplace(key, std::make_unique<Slot<T>>()).first;
		return static_cast<Slot<T> *>(it->second.get())->pool;
	}

	void ClearAll() {
		std::scoped_lock lk(mu);
		for (auto &[_, s] : slots)
			s->Clear();
	}

	template <std::derived_from<Resource> T> bool Insert(String n, std::shared_ptr<T> r) {
		return GetPool<T>().Insert(std::move(n), std::move(r));
	}
	template <std::derived_from<Resource> T> void Upsert(String n, std::shared_ptr<T> r) {
		GetPool<T>().Upsert(std::move(n), std::move(r));
	}
	template <std::derived_from<Resource> T> auto Get(const String &n) { return GetPool<T>().Get(n); }
	template <std::derived_from<Resource> T> bool Contains(const String &n) { return GetPool<T>().Contains(n); }
	template <std::derived_from<Resource> T> bool Remove(const String &n) { return GetPool<T>().Remove(n); }
	template <std::derived_from<Resource> T> void Clear() { GetPool<T>().Clear(); }

private:
	mutable std::mutex mu;
	std::unordered_map<std::type_index, std::unique_ptr<IErased>> slots;
};

} // namespace resources
