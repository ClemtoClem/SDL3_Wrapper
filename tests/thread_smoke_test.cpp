// Smoke test : sdl3:: threading (lib/sdl3/thread.hpp) — Thread, Mutex/
// MutexGuard, RWLock+guards, Semaphore, Condition, AtomicInt/AtomicU32/
// AtomicPointer<T>, SpinLock+guard, InitState, TLS<T>.
#define USE_TEST

#include "core/core.hpp"
#include "sdl3/sdl3.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace sdl3;

// ── Thread::Create/wait : jointure + code de sortie ─────────────────────
TEST(Thread, Create) {
    auto tRes = Thread::Create([]() -> int { return 42; }, "worker");
    EXPECT_TRUE(tRes.IsOk());
    Thread t = std::move(tRes.Value());
    auto status = t.Wait();
    EXPECT_TRUE(status.IsSome());
    EXPECT_EQ(status.Unwrap(), 42);
    EXPECT_FALSE(bool(t)); // vidé après wait()
    std::cout << "Thread::Create/wait (code de sortie): ok\n";
}

// ── Thread::Detach : ne bloque pas, objet vidé après l'appel ─────────────
TEST(Thread, Detach) {
    AtomicInt done(0);
    auto tRes = Thread::Create([&done]() -> int {
        done.Store(1);
        return 0;
    });
    EXPECT_TRUE(tRes.IsOk());
    Thread t = std::move(tRes.Value());
    t.Detach();
    EXPECT_FALSE(bool(t));
    // Attente courte pour laisser le thread détaché se terminer avant de
    // vérifier son effet (best-effort, pas de garantie temporelle stricte
    // — c'est le point même de detach() : on n'a plus la main dessus).
    for (int i = 0; i < 1000 && done.Load() == 0; ++i)
        DelayMS(1);
    EXPECT_EQ(done.Load(), 1);
    std::cout << "Thread::Detach (fire-and-forget): ok\n";
}

// ── AtomicInt/AtomicU32 : opérations de base ─────────────────────────────
TEST(Thread, AtomicIntAndAtomicU32) {
    AtomicInt a(10);
    EXPECT_TRUE(a.Load() == 10);
    EXPECT_TRUE(a.Store(20) == 10); // renvoie l'ANCIENNE valeur
    EXPECT_TRUE(a.Load() == 20);
    EXPECT_TRUE(a.FetchAdd(5) == 20); // renvoie l'ANCIENNE valeur
    EXPECT_TRUE(a.Load() == 25);
    EXPECT_TRUE(a.CompareExchange(25, 100));  // réussit : valeur == 25
    EXPECT_TRUE(a.Load() == 100);
    EXPECT_TRUE(!a.CompareExchange(25, 200)); // échoue : valeur != 25
    EXPECT_TRUE(a.Load() == 100);

    AtomicU32 u(7);
    EXPECT_TRUE(u.Load() == 7);
    EXPECT_TRUE(u.CompareExchange(7, 9));
    EXPECT_TRUE(u.Load() == 9);
    std::cout << "AtomicInt/AtomicU32 (operations de base): ok\n";
}

// ── AtomicPointer<T> ──────────────────────────────────────────────────────
TEST(Thread, AtomicPointer) {
    int x = 1, y = 2;
    AtomicPointer<int> p(&x);
    EXPECT_TRUE(p.Load() == &x);
    EXPECT_TRUE(p.Store(&y) == &x);
    EXPECT_TRUE(p.Load() == &y);
    EXPECT_TRUE(p.CompareExchange(&y, &x));
    EXPECT_TRUE(p.Load() == &x);
    EXPECT_TRUE(!p.CompareExchange(&y, nullptr));
    EXPECT_TRUE(p.Load() == &x);
    std::cout << "AtomicPointer<T> (CAS): ok\n";
}

// ── Multi-thread : N threads incrémentent un AtomicInt partagé M fois ────
// Vérifie Thread::Create/wait ET AtomicInt::FetchAdd sous contention réelle.
TEST(Thread, Multithread) {
    constexpr int K_THREADS = 8, K_INCREMENTS = 5000;
    AtomicInt counter(0);
    std::vector<Thread> threads;
    threads.reserve(K_THREADS);
    for (int i = 0; i < K_THREADS; ++i) {
        auto tRes = Thread::Create([&counter]() -> int {
            for (int j = 0; j < K_INCREMENTS; ++j)
                counter.FetchAdd(1);
            return 0;
        });
        EXPECT_TRUE(tRes.IsOk());
        threads.push_back(std::move(tRes.Value()));
    }
    for (auto &t : threads)
        t.Wait();
    EXPECT_TRUE(counter.Load() == K_THREADS * K_INCREMENTS);
    std::cout << "Thread + AtomicInt (contention reelle, " << K_THREADS << "x" << K_INCREMENTS
                << " incrementations): ok\n";
}

// ── Mutex+MutexGuard : sérialise un compteur NON atomique sous contention ──
// Si le verrou ne serialisait pas réellement, des incréments torn feraient
// que le total final < kThreads*kIncrements — signal de correction net.
TEST(Thread, MutexAndMutexGuard) {
    constexpr int K_THREADS = 8, K_INCREMENTS = 5000;
    auto mRes = Mutex::Create();
    EXPECT_TRUE(mRes.IsOk());
    Mutex mutex = std::move(mRes.Value());
    int counter = 0;
    std::vector<Thread> threads;
    threads.reserve(K_THREADS);
    for (int i = 0; i < K_THREADS; ++i) {
        auto tRes = Thread::Create([&mutex, &counter]() -> int {
            for (int j = 0; j < K_INCREMENTS; ++j) {
                MutexGuard guard(mutex);
                ++counter;
            }
            return 0;
        });
        EXPECT_TRUE(tRes.IsOk());
        threads.push_back(std::move(tRes.Value()));
    }
    for (auto &t : threads)
        t.Wait();
    EXPECT_TRUE(counter == K_THREADS * K_INCREMENTS);
    std::cout << "Mutex+MutexGuard (serialisation d'un compteur non-atomique): ok\n";
}

// ── SpinLock+SpinLockGuard : même test, verrou d'attente active ──────────
TEST(Thread, SpinLockAndSpinLockGuard) {
    constexpr int K_THREADS = 4, K_INCREMENTS = 2000;
    SpinLock lock;
    int counter = 0;
    std::vector<Thread> threads;
    threads.reserve(K_THREADS);
    for (int i = 0; i < K_THREADS; ++i) {
        auto tRes = Thread::Create([&lock, &counter]() -> int {
            for (int j = 0; j < K_INCREMENTS; ++j) {
                SpinLockGuard guard(lock);
                ++counter;
            }
            return 0;
        });
        EXPECT_TRUE(tRes.IsOk());
        threads.push_back(std::move(tRes.Value()));
    }
    for (auto &t : threads)
        t.Wait();
    EXPECT_TRUE(counter == K_THREADS * K_INCREMENTS);
    std::cout << "SpinLock+SpinLockGuard (serialisation): ok\n";
}

// ── RWLock : un writer en arrière-plan bloque tryLockWrite/tryLockRead ───
TEST(Thread, RWLock) {
    auto lRes = RWLock::Create();
    EXPECT_TRUE(lRes.IsOk());
    RWLock lock = std::move(lRes.Value());

    AtomicInt writerHasLock(0), mainMayCheck(0), checkedWhileLocked(0);
    auto tRes = Thread::Create([&]() -> int {
        RWLockWriteGuard guard(lock);
        writerHasLock.Store(1);
        while (mainMayCheck.Load() == 0)
            SDL_Delay(1); // maintient le verrou jusqu'à ce que le thread principal ait vérifié
        return 0;
    });
    EXPECT_TRUE(tRes.IsOk());
    Thread t = std::move(tRes.Value());

    while (writerHasLock.Load() == 0)
        SDL_Delay(1);
    // Le writer tient le verrou : lecture ET écriture doivent échouer.
    checkedWhileLocked.Store((!lock.TryLockRead() && !lock.TryLockWrite()) ? 1 : 0);
    mainMayCheck.Store(1);
    t.Wait();

    EXPECT_TRUE(checkedWhileLocked.Load() == 1);
    // Le writer a relâché : la lecture doit maintenant réussir.
    EXPECT_TRUE(lock.TryLockRead());
    lock.Unlock();
    std::cout << "RWLock (writer bloque lecteurs/rediger concurrents): ok\n";
}

// ── Semaphore : producteur/consommateur (tampon de taille 1) ────────────
TEST(Thread, Semaphore) {
    auto emptyRes = Semaphore::Create(1); // 1 slot vide au depart
    auto fullRes = Semaphore::Create(0);  // 0 slot plein au depart
    EXPECT_TRUE(emptyRes.IsOk() && fullRes.IsOk());
    Semaphore emptySlots = std::move(emptyRes.Value());
    Semaphore fullSlots = std::move(fullRes.Value());
    int buffer = 0;
    constexpr int K_ITEMS = 20;

    auto producer = Thread::Create([&]() -> int {
        for (int i = 0; i < K_ITEMS; ++i) {
            emptySlots.Wait();
            buffer = i;
            fullSlots.Signal();
        }
        return 0;
    });
    EXPECT_TRUE(producer.IsOk());
    Thread prod = std::move(producer.Value());

    int received = -1;
    for (int i = 0; i < K_ITEMS; ++i) {
        fullSlots.Wait();
        received = buffer;
        EXPECT_TRUE(received == i); // strictement en ordre : le tampon taille-1 le garantit
        emptySlots.Signal();
    }
    prod.Wait();
    std::cout << "Semaphore (producteur/consommateur, tampon taille 1): ok\n";
}

// ── Condition+Mutex : attente/notification classique ─────────────────────
TEST(Thread, ConditionAndMutex) {
    auto mRes = Mutex::Create();
    auto cRes = Condition::Create();
    EXPECT_TRUE(mRes.IsOk() && cRes.IsOk());
    Mutex mutex = std::move(mRes.Value());
    Condition cond = std::move(cRes.Value());
    bool ready = false;
    int payload = 0;

    auto worker = Thread::Create([&]() -> int {
        mutex.Lock();
        payload = 123;
        ready = true;
        mutex.Unlock();
        cond.Signal();
        return 0;
    });
    EXPECT_TRUE(worker.IsOk());
    Thread w = std::move(worker.Value());

    mutex.Lock();
    while (!ready)
        cond.Wait(mutex);
    int seen = payload;
    mutex.Unlock();
    w.Wait();

    EXPECT_TRUE(seen == 123);
    std::cout << "Condition+Mutex (attente/notification): ok\n";
}

// ── InitState : cycle complet init -> quit -> réinit ────────────────────
// SDL_ShouldQuit() fait transitionner l'état INITIALIZED -> UNINITIALIZING
// et renvoie vrai pour l'appelant qui doit alors faire le nettoyage (donc
// vrai juste après setInitialized(true), pas faux).
TEST(Thread, InitState) {
    InitState state;
    EXPECT_TRUE(state.ShouldInit()); // premier appel : true, l'appelant doit initialiser
    state.SetInitialized(true); // -> INITIALIZED
    EXPECT_TRUE(!state.ShouldInit()); // déjà initialisé : pas de nouvelle demande d'init
    EXPECT_TRUE(state.ShouldQuit()); // état INITIALIZED : ce thread doit faire le nettoyage
    state.SetInitialized(false); // -> UNINITIALIZED, cycle complet
    EXPECT_TRUE(state.ShouldInit()); // prêt pour un nouveau cycle
    state.SetInitialized(true);
    std::cout << "InitState (cycle init -> quit -> reinit): ok\n";
}

// ── TLS<T> : valeurs indépendantes par thread ────────────────────────────
TEST(Thread, TLS) {
    TLS<int> tls;
    static int valA = 111, valB = 222;
    AtomicInt seenA(0), seenB(0);

    auto ta = Thread::Create([&]() -> int {
        tls.Set(&valA);
        SDL_Delay(20); // laisse l'autre thread poser SA propre valeur entre-temps
        seenA.Store(*tls.Get() == valA ? 1 : 0);
        return 0;
    });
    auto tb = Thread::Create([&]() -> int {
        tls.Set(&valB);
        SDL_Delay(20);
        seenB.Store(*tls.Get() == valB ? 1 : 0);
        return 0;
    });
    EXPECT_TRUE(ta.IsOk() && tb.IsOk());
    Thread threadA = std::move(ta.Value());
    Thread threadB = std::move(tb.Value());
    threadA.Wait();
    threadB.Wait();
    EXPECT_TRUE(seenA.Load() == 1);
    EXPECT_TRUE(seenB.Load() == 1);
    std::cout << "TLS<T> (valeurs independantes par thread): ok\n";
}

int main() {
    return RUN_ALL_TESTS();
}