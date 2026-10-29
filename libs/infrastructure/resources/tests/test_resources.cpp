// Smoke test : resources:: (lib/resources/resources.hpp) — Resource/
// BytesResource, Pool<T>/Registry, AsyncLoader (sur sdl3::Thread), ResourceCache.
#include "test.hpp"
#include "resources/resources.hpp"
#include <sdl3/sdl3.hpp>
#include <cassert>
#include <iostream>

using resources::AsyncLoader;
using resources::BytesResource;
using resources::Pool;
using resources::Registry;
using resources::ResourceCache;
using resources::ResourceState;


// ── BytesResource : chargement sync réel via sdl3::readFile ──────────────
TEST(BytesResource_Sync) {
    auto res = std::make_shared<BytesResource>(String("assets/textures/star.png"));
    CHECK(res->IsUnloaded());
    CHECK(res->Path() == StringView("assets/textures/star.png"));

    auto loadResult = res->Load();
    CHECK(loadResult.IsOk());
    CHECK(res->IsReady());
    CHECK(!res->Bytes().empty()); // fichier reel, contenu non vide
    std::cout << "BytesResource (chargement sync via sdl3::readFile, " << res->Bytes().size()
                << " octets): ok\n";

    res->Unload();
    CHECK(res->IsUnloaded());
    CHECK(res->Bytes().empty());
}

// ── BytesResource : échec propre sur fichier inexistant ──────────────────
TEST(BytesResource_DoesNotExist) {
    auto res = std::make_shared<BytesResource>(String("assets/does_not_exist_12345.bin"));
    auto loadResult = res->Load();
    CHECK(!loadResult.IsOk());
    CHECK(!loadResult.Error().IsEmpty()); // message d'erreur SDL propage, pas vide
    CHECK(res->HasFailed());
}

// ── Resource::OnReady : callback appelé immédiatement si déjà prêt ───────
TEST(OnReady_ImmediateCallback) {
    auto res = std::make_shared<BytesResource>(String("assets/textures/star.png"));
    res->Load();
    CHECK(res->IsReady());
    bool fired = false;
    res->OnReady([&fired](resources::Resource &) { fired = true; });
    CHECK(fired); // deja pret -> appel immediat, pas de file d'attente
}

// ── Resource::OnReady : callback différé jusqu'à Load() ─────────────────
TEST(OnReady_DifferedCallback) {
    auto res = std::make_shared<BytesResource>(String("assets/textures/star.png"));
    bool fired = false;
    res->OnReady([&fired](resources::Resource &) { fired = true; });
    CHECK(!fired); // pas encore charge -> pas encore appele
    res->Load();
    CHECK(fired); // charge -> callback en attente declenche
}

    // ── Pool<T> : Insert/Get(Option)/Contains/Remove/Clear ───────────────────
TEST(Poll) {
    Pool<BytesResource> pool;
    auto res = std::make_shared<BytesResource>(String("assets/textures/star.png"));
    CHECK(pool.Insert("a", res));
    CHECK(!pool.Insert("a", res)); // deja present -> echec (pas d'ecrasement silencieux)
    CHECK(pool.Contains("a"));
    CHECK(pool.GetSize() == 1);

    auto got = pool.Get("a");
    CHECK(got.IsSome()); // Option<shared_ptr<T>>, pas de nullptr nu
    CHECK(got.Unwrap() == res);

    auto missing = pool.Get("nope");
    CHECK(missing.IsNone());

    // Hors de CHECK() : compilé en -DNDEBUG (release), l'appel disparaîtrait.
    bool removed = pool.Remove("a");
    CHECK(removed);
    CHECK(!pool.Contains("a"));
    CHECK(pool.GetSize() == 0);
}

// ── Registry : collection hétérogène par type ────────────────────────────
TEST(Registry) {
    Registry registry;
    auto res = std::make_shared<BytesResource>(String("assets/textures/star.png"));
    CHECK(registry.Insert<BytesResource>("x", res));
    CHECK(registry.Contains<BytesResource>("x"));
    auto got = registry.Get<BytesResource>("x");
    CHECK(got.IsSome());
    registry.ClearAll();
    CHECK(!registry.Contains<BytesResource>("x"));
}

// ── ResourceCache::Resolve : préfixe le chemin de base ───────────────────
TEST(ResourceCache_Resolve) {
    ResourceCache cache(String("assets/"));
    CHECK(cache.Resolve("textures/default_particle.png") == String("assets/textures/default_particle.png"));
    CHECK(cache.Resolve("/abs/path") == String("/abs/path")); // chemin absolu : pas de prefixe
    CHECK(cache.Resolve("./rel") == String("./rel"));         // deja relatif explicite : pas de prefixe
}

// ── ResourceCache::LoadSync ────────────────────────────────────────────────
TEST(ResourceCache_LoadSync) {
    ResourceCache cache;
    auto res = cache.EmplaceSync<BytesResource>(String("assets/textures/star.png"));
    CHECK(res->IsReady());
    auto cached = cache.Get<BytesResource>(String("assets/textures/star.png"));
    CHECK(cached.IsSome());
    CHECK(cached.Unwrap() == res); // meme instance, pas rechargee
}

// ── AsyncLoader : pool de threads réel (sdl3::Thread), plusieurs charges ──
TEST(AsyncLoader) {
    AsyncLoader loader(2);
    std::vector<std::shared_ptr<resources::Resource>> resList;
    std::vector<std::future<bool>> futures;
    constexpr int K_COUNT = 6;
    for (int i = 0; i < K_COUNT; ++i) {
        auto res = std::make_shared<BytesResource>(String("assets/textures/star.png"));
        resList.push_back(res);
        futures.push_back(loader.Enqueue(res));
    }
    bool allOk = true;
    for (auto &f : futures)
        allOk = f.get() && allOk;
    CHECK(allOk);
    CHECK(loader.WaitIdle());
    for (auto &r : resList)
        CHECK(r->IsReady());
}

// ── ResourceCache::LoadAsync : bout en bout avec le loader ────────────────
TEST(ResourceCache_LoadAsync) {
    ResourceCache cache(String(), 2);
    auto future = cache.EmplaceAsync<BytesResource>(String("assets/textures/star.png"));
    CHECK(future.get());
    CHECK(cache.WaitIdle());
    auto got = cache.Get<BytesResource>(String("assets/textures/star.png"));
    CHECK(got.IsSome());
    CHECK(got.Unwrap()->IsReady());
}


TEST(AssetId_PathAndKeyNamespacesAreDistinct) {
	using resources::AssetId;
	CHECK(AssetId::FromPath(StringView("models/k.gltf")) != AssetId::FromKey(StringView("models/k.gltf")));
}
