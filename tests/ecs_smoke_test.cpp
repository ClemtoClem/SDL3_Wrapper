#define USE_TEST

#include "core/test.hpp"
#include "ecs/ecs.hpp"

// ===========================================================================
// Composants et Ressources de test
// ===========================================================================

struct Position {
    float x, y;
    bool operator==(const Position &o) const { return x == o.x && y == o.y; }
};

struct Velocity {
    float dx, dy;
    bool operator==(const Velocity &o) const { return dx == o.dx && dy == o.dy; }
};

struct Health {
    int hp;
    bool operator==(const Health &o) const { return hp == o.hp; }
};

struct TagIsPlayer {
    bool dummy = true; // composant tag vide (ou presque)
};

struct TimeResource {
    float deltaTime;
};

// ===========================================================================
// TESTS : Allocateur d'Entités (Basse couche)
// ===========================================================================

TEST(EcsAllocator, AllocateAndFree) {
    ecs::EntityAllocator alloc;

    ecs::Entity e1 = alloc.Allocate();
    ecs::Entity e2 = alloc.Allocate();

    EXPECT_TRUE(alloc.IsAlive(e1));
    EXPECT_TRUE(alloc.IsAlive(e2));
    EXPECT_EQ(alloc.AliveCount(), 2);

    EXPECT_TRUE(alloc.Free(e1));
    EXPECT_FALSE(alloc.IsAlive(e1));
    EXPECT_EQ(alloc.AliveCount(), 1);

    // Double free
    EXPECT_FALSE(alloc.Free(e1));
}

TEST(EcsAllocator, GenerationalIndex) {
    ecs::EntityAllocator alloc;

    ecs::Entity e1 = alloc.Allocate();
    alloc.Free(e1);

    ecs::Entity e2 = alloc.Allocate(); // Doit réutiliser l'ID de e1
    EXPECT_EQ(e1.id, e2.id);
    EXPECT_TRUE(e1.generation < e2.generation); // La génération a dû augmenter
    EXPECT_TRUE(e1 != e2);
}

// ===========================================================================
// TESTS : Cycle de vie et Composants (Registre)
// ===========================================================================

TEST(EcsRegistry, SpawnAndDespawn) {
    ecs::ArchetypeRegistry world;

    ecs::Entity e = world.Spawn();
    EXPECT_TRUE(world.IsAlive(e));
    EXPECT_EQ(world.AliveCount(), 1);

    EXPECT_TRUE(world.Despawn(e));
    EXPECT_FALSE(world.IsAlive(e));
    EXPECT_EQ(world.AliveCount(), 0);
}

TEST(EcsRegistry, AddAndGetComponent) {
    ecs::ArchetypeRegistry world;
    ecs::Entity e = world.Spawn();

    world.AddComponent(e, Position{10.0f, 20.0f});
    EXPECT_TRUE(world.HasComponent<Position>(e));
    EXPECT_FALSE(world.HasComponent<Velocity>(e));

    // Utilisation de GetComponent retournant un Option<RefMut<T>>
    auto optPos = world.GetComponent<Position>(e);
    EXPECT_TRUE(optPos.IsSome());
    EXPECT_EQ(optPos.Unwrap()->x, 10.0f);

    // Modification via RefMut
    optPos.Unwrap()->x = 42.0f;
    EXPECT_EQ(world.GetComponent<Position>(e).Unwrap()->x, 42.0f);
}

TEST(EcsRegistry, RemoveComponent) {
    ecs::ArchetypeRegistry world;
    ecs::Entity e = world.Spawn();

    world.AddComponent(e, Position{0, 0});
    world.AddComponent(e, Velocity{1, 1});

    EXPECT_TRUE(world.HasComponent<Position>(e));
    EXPECT_TRUE(world.HasComponent<Velocity>(e));

    // Supprime la vélocité
    EXPECT_TRUE(world.RemoveComponent<Velocity>(e));
    EXPECT_FALSE(world.HasComponent<Velocity>(e));

    // La position doit toujours être là (migration réussie)
    EXPECT_TRUE(world.HasComponent<Position>(e));
}

// ===========================================================================
// TESTS : Bundles
// ===========================================================================

TEST(EcsRegistry, SpawnBundle) {
    ecs::ArchetypeRegistry world;

    ecs::Entity e = world.SpawnBundle(Position{5.0f, 5.0f}, Velocity{1.0f, -1.0f}, Health{100});

    EXPECT_TRUE(world.IsAlive(e));
    EXPECT_TRUE(world.HasComponent<Position>(e));
    EXPECT_TRUE(world.HasComponent<Velocity>(e));
    EXPECT_TRUE(world.HasComponent<Health>(e));

    EXPECT_EQ(world.GetComponent<Health>(e).Unwrap()->hp, 100);
}

// ===========================================================================
// TESTS : Requêtes (Queries)
// ===========================================================================

TEST(EcsRegistry, QueryIteration) {
    ecs::ArchetypeRegistry world;

    ecs::Entity e1 = world.SpawnBundle(Position{0, 0}, Velocity{1, 0});
    ecs::Entity e2 = world.SpawnBundle(Position{0, 0}, Health{100});
    ecs::Entity e3 = world.SpawnBundle(Position{0, 0}, Velocity{0, 1}, Health{50});

    int count = 0;
    world.Query<Position, Velocity>([&](ecs::Entity e, Position &p, Velocity &v) {
        (void)e;
        count++;
        p.x += v.dx;
        p.y += v.dy;
    });

    EXPECT_EQ(count, 2); // e1 et e3 ont (Position, Velocity)
    EXPECT_EQ(world.GetComponent<Position>(e1).Unwrap()->x, 1.0f);
    EXPECT_EQ(world.GetComponent<Position>(e3).Unwrap()->y, 1.0f);
    EXPECT_EQ(world.GetComponent<Position>(e2).Unwrap()->x, 0.0f); // Intouché
}

TEST(EcsRegistry, QueryFilters) {
    ecs::ArchetypeRegistry world;

    world.SpawnBundle(Position{1, 1}, TagIsPlayer{}); // Le joueur
    world.SpawnBundle(Position{2, 2});                // PNJ
    world.SpawnBundle(Position{3, 3}, Health{10});    // Ennemi

    int countWithoutHealth = 0;
    world.Query<Position, ecs::Without<Health>>([&](ecs::Entity, Position &) { countWithoutHealth++; });
    EXPECT_EQ(countWithoutHealth, 2); // Joueur et PNJ

    int countWithTag = 0;
    world.Query<Position, ecs::With<TagIsPlayer>>([&](ecs::Entity, Position &) { countWithTag++; });
    EXPECT_EQ(countWithTag, 1); // Seulement le joueur
}

TEST(EcsRegistry, ComplexFilters_And_Or) {
    ecs::ArchetypeRegistry world;

    world.SpawnBundle(Position{1, 1}, Velocity{0, 0}, TagIsPlayer{});
    world.SpawnBundle(Position{2, 2}, Health{100});
    world.SpawnBundle(Position{3, 3}, Velocity{1, 1});
    world.SpawnBundle(Position{4, 4});

    // Filtre OR : possède Velocity OU TagIsPlayer
    using FilterOr = ecs::Or<ecs::With<Velocity>, ecs::With<TagIsPlayer>>;
    auto resultsOr = world.QueryVec<Position, FilterOr>();
    EXPECT_EQ(resultsOr.size(), 2); // Entités 1 et 3

    // Filtre AND : Sans Velocity ET Sans TagIsPlayer
    using FilterAnd = ecs::And<ecs::Without<Velocity>, ecs::Without<TagIsPlayer>>;
    auto results_and = world.QueryVec<Position, FilterAnd>();
    EXPECT_EQ(results_and.size(), 2); // Entités 2 et 4
}

TEST(EcsRegistry, QuerySingle) {
    ecs::ArchetypeRegistry world;

    // Cas vide
    EXPECT_TRUE(world.QuerySingle<Position>().IsNone());

    // Cas 1 entité (Succès)
    world.SpawnBundle(Position{10, 10}, TagIsPlayer{});
    auto opt1 = world.QuerySingle<Position, ecs::With<TagIsPlayer>>();
    EXPECT_TRUE(opt1.IsSome());
    auto [ent1, pos1] = opt1.Unwrap();
    EXPECT_EQ(pos1->x, 10.0f);

    // Cas > 1 entité (Echec car non unique)
    world.SpawnBundle(Position{20, 20}, TagIsPlayer{});
    auto opt2 = world.QuerySingle<Position, ecs::With<TagIsPlayer>>();
    EXPECT_TRUE(opt2.IsNone());
}

// ===========================================================================
// TESTS : Ressources
// ===========================================================================

TEST(EcsResources, InsertAndGet) {
    ecs::ArchetypeRegistry world;

    EXPECT_TRUE(world.resources.Get<TimeResource>().IsNone());

    world.resources.Insert(TimeResource{0.016f});
    EXPECT_TRUE(world.resources.Contains<TimeResource>());

    auto optTime = world.resources.Get<TimeResource>();
    EXPECT_TRUE(optTime.IsSome());
    EXPECT_EQ(optTime.Unwrap()->deltaTime, 0.016f);

    // Modification de la ressource
    optTime.Unwrap()->deltaTime = 0.5f;
    EXPECT_EQ(world.resources.Get<TimeResource>().Unwrap()->deltaTime, 0.5f);
}

TEST(EcsResources, GetResult) {
    ecs::ArchetypeRegistry world;

    // Ressource manquante -> Err
    auto resErr = world.resources.GetResult<TimeResource>();
    EXPECT_TRUE(resErr.IsError());
    EXPECT_EQ(resErr.unwrap_error(), "Resource not found");

    // Ressource présente -> Ok
    world.resources.Insert(TimeResource{1.0f});
    auto resOk = world.resources.GetResult<TimeResource>();
    EXPECT_TRUE(resOk.IsOk());
    EXPECT_EQ(resOk.Unwrap()->deltaTime, 1.0f);
}

TEST(EcsResources, Remove) {
    ecs::ArchetypeRegistry world;
    world.resources.Insert(TimeResource{1.0f});

    EXPECT_TRUE(world.resources.Remove<TimeResource>());
    EXPECT_FALSE(world.resources.Contains<TimeResource>());
    EXPECT_FALSE(world.resources.Remove<TimeResource>()); // Déjà supprimé
}
// Point d'entrée du test — absent jusqu'ici, ce qui faisait échouer
// l'édition de liens (« référence indéfinie vers main ») une fois la
// compilation réparée. Même forme que tous les autres tests du dépôt.
int main() { return RUN_ALL_TESTS(); }
