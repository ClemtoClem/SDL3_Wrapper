// Smoke test : resources:: (lib/resources/resources.hpp) — Resource/
// BytesResource, Pool<T>/Registry, AsyncLoader (sur sdl3::Thread), ResourceCache.
#include "resources/resources.hpp"
#include "sdl3/sdl3.hpp"
#include <cassert>
#include <iostream>

using resources::AsyncLoader;
using resources::BytesResource;
using resources::Pool;
using resources::Registry;
using resources::ResourceCache;
using resources::ResourceState;

int main() {
    // ── BytesResource : chargement sync réel via sdl3::readFile ──────────────
    {
        auto res = std::make_shared<BytesResource>(String("assets/textures/default_particle.png"));
        assert(res->IsUnloaded());
        assert(res->Path() == StringView("assets/textures/default_particle.png"));

        auto loadResult = res->Load();
        assert(loadResult.IsOk());
        assert(res->IsReady());
        assert(!res->Bytes().empty()); // fichier reel, contenu non vide
        std::cout << "BytesResource (chargement sync via sdl3::readFile, " << res->Bytes().size()
                  << " octets): ok\n";

        res->Unload();
        assert(res->IsUnloaded());
        assert(res->Bytes().empty());
        std::cout << "Resource::Unload (retour a Unloaded, tampon vide): ok\n";
    }

    // ── BytesResource : échec propre sur fichier inexistant ──────────────────
    {
        auto res = std::make_shared<BytesResource>(String("assets/does_not_exist_12345.bin"));
        auto loadResult = res->Load();
        assert(!loadResult.IsOk());
        assert(!loadResult.Error().IsEmpty()); // message d'erreur SDL propage, pas vide
        assert(res->HasFailed());
        std::cout << "BytesResource (echec propre sur fichier absent, etat Failed): ok\n";
    }

    // ── Resource::OnReady : callback appelé immédiatement si déjà prêt ───────
    {
        auto res = std::make_shared<BytesResource>(String("assets/textures/default_particle.png"));
        res->Load();
        assert(res->IsReady());
        bool fired = false;
        res->OnReady([&fired](resources::Resource &) { fired = true; });
        assert(fired); // deja pret -> appel immediat, pas de file d'attente
        std::cout << "Resource::OnReady (appel immediat si deja pret): ok\n";
    }

    // ── Resource::OnReady : callback différé jusqu'à Load() ─────────────────
    {
        auto res = std::make_shared<BytesResource>(String("assets/textures/default_particle.png"));
        bool fired = false;
        res->OnReady([&fired](resources::Resource &) { fired = true; });
        assert(!fired); // pas encore charge -> pas encore appele
        res->Load();
        assert(fired); // charge -> callback en attente declenche
        std::cout << "Resource::OnReady (callback differe jusqu'au chargement): ok\n";
    }

    // ── Pool<T> : Insert/Get(Option)/Contains/Remove/Clear ───────────────────
    {
        Pool<BytesResource> pool;
        auto res = std::make_shared<BytesResource>(String("assets/textures/default_particle.png"));
        assert(pool.Insert("a", res));
        assert(!pool.Insert("a", res)); // deja present -> echec (pas d'ecrasement silencieux)
        assert(pool.Contains("a"));
        assert(pool.GetSize() == 1);

        auto got = pool.Get("a");
        assert(got.IsSome()); // Option<shared_ptr<T>>, pas de nullptr nu
        assert(got.Unwrap() == res);

        auto missing = pool.Get("nope");
        assert(missing.IsNone());

        // Hors de assert() : compilé en -DNDEBUG (release), l'appel disparaîtrait.
        bool removed = pool.Remove("a");
        assert(removed);
        assert(!pool.Contains("a"));
        assert(pool.GetSize() == 0);
        std::cout << "Pool<T> (Insert/Get retourne Option/Contains/Remove): ok\n";
    }

    // ── Registry : collection hétérogène par type ────────────────────────────
    {
        Registry registry;
        auto res = std::make_shared<BytesResource>(String("assets/textures/default_particle.png"));
        assert(registry.Insert<BytesResource>("x", res));
        assert(registry.Contains<BytesResource>("x"));
        auto got = registry.Get<BytesResource>("x");
        assert(got.IsSome());
        registry.ClearAll();
        assert(!registry.Contains<BytesResource>("x"));
        std::cout << "Registry (collection heterogene par type, ClearAll): ok\n";
    }

    // ── ResourceCache::Resolve : préfixe le chemin de base ───────────────────
    {
        ResourceCache cache(String("assets/"));
        assert(cache.Resolve("textures/default_particle.png") == String("assets/textures/default_particle.png"));
        assert(cache.Resolve("/abs/path") == String("/abs/path")); // chemin absolu : pas de prefixe
        assert(cache.Resolve("./rel") == String("./rel"));         // deja relatif explicite : pas de prefixe
        std::cout << "ResourceCache::Resolve (prefixe le chemin de base): ok\n";
    }

    // ── ResourceCache::LoadSync ────────────────────────────────────────────────
    {
        ResourceCache cache;
        auto res = cache.EmplaceSync<BytesResource>(String("assets/textures/default_particle.png"));
        assert(res->IsReady());
        auto cached = cache.Get<BytesResource>(String("assets/textures/default_particle.png"));
        assert(cached.IsSome());
        assert(cached.Unwrap() == res); // meme instance, pas rechargee
        std::cout << "ResourceCache::LoadSync/EmplaceSync (charge + enregistre): ok\n";
    }

    // ── AsyncLoader : pool de threads réel (sdl3::Thread), plusieurs charges ──
    {
        AsyncLoader loader(2);
        std::vector<std::shared_ptr<resources::Resource>> resList;
        std::vector<std::future<bool>> futures;
        constexpr int K_COUNT = 6;
        for (int i = 0; i < K_COUNT; ++i) {
            auto res = std::make_shared<BytesResource>(String("assets/textures/default_particle.png"));
            resList.push_back(res);
            futures.push_back(loader.Enqueue(res));
        }
        bool allOk = true;
        for (auto &f : futures)
            allOk = f.get() && allOk;
        assert(allOk);
        assert(loader.WaitIdle());
        for (auto &r : resList)
            assert(r->IsReady());
        std::cout << "AsyncLoader (pool de " << 2 << " sdl3::Thread, " << K_COUNT << " charges async): ok\n";
    }

    // ── ResourceCache::LoadAsync : bout en bout avec le loader ────────────────
    {
        ResourceCache cache(String(), 2);
        auto future = cache.EmplaceAsync<BytesResource>(String("assets/textures/default_particle.png"));
        assert(future.get());
        assert(cache.WaitIdle());
        auto got = cache.Get<BytesResource>(String("assets/textures/default_particle.png"));
        assert(got.IsSome());
        assert(got.Unwrap()->IsReady());
        std::cout << "ResourceCache::LoadAsync/EmplaceAsync (bout en bout, pool de threads reel): ok\n";
    }

    std::cout << "resources smoke test done\n";
    return 0;
}
