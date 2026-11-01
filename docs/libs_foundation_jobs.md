# jobs — vivier de fils et ParallelFor

`libs/foundation/jobs` · couche **foundation** · `libjobs.a` · espace de noms `jobs::`

## Présentation

`jobs::JobSystem` est un petit ordonnanceur. Il crée une seule fois un vivier
de fils persistants, réveillés par une variable de condition, et répartit sur
eux des boucles parallélisables. Il ne s'appuie que sur la bibliothèque
standard (`std::thread`, `std::mutex`, `std::condition_variable`,
`std::atomic`) et n'utilise pas SDL.

Exemple d'utilisation dans le dépôt : [`physics`](libs_infrastructure_physics.md)
répartit ainsi les paires de la phase large et la génération des contacts. Il
garde l'ordre des résultats, donc la simulation reste reproductible.

Ce que le système **ne fait pas** :

- il ne vole pas de tâches ;
- il ne gère ni dépendances ni priorités entre tâches ;
- il ne propage pas d'exceptions : une tâche ne doit pas en lever.

## API publique (`include/jobs/job_system.hpp`)

| Membre | Rôle |
|---|---|
| `static size_t RecommendedWorkerCount()` | Nombre de cœurs logiques moins un (le fil appelant participe au travail) ; au minimum 0 |
| `explicit JobSystem(size_t workerCount = RecommendedWorkerCount())` | Crée le vivier. **0 fil** : tout s'exécute en ligne sur l'appelant |
| `~JobSystem()` | Termine les tâches en file, arrête et joint les fils |
| `size_t WorkerCount() const` | Nombre de fils secondaires |
| `bool IsInline() const` | Vrai sans fil secondaire |
| `int PeakConcurrentWorkers() const` | Nombre maximal de fils actifs en même temps depuis la création |
| `void ParallelFor(size_t count, size_t minimumPerBatch, const std::function<void(size_t)> &body)` | Appelle `body(i)` pour chaque `i` de `[0, count)`, sur le vivier **et** sur le fil appelant |

Règles d'utilisation de `ParallelFor` :

- L'appel est **bloquant** : au retour, tous les appels de `body` sont
  terminés. Aucune tâche ne survit donc à ce qu'elle capture.
- En dessous de `minimumPerBatch` éléments, tout s'exécute en ligne. La
  synchronisation d'un lot coûte quelques microsecondes.
- `body` est appelé depuis plusieurs fils. Il ne doit écrire qu'à des
  emplacements distincts pour chaque indice, sinon il doit se synchroniser
  lui-même.
- La classe n'est ni copiable ni déplaçable.

### Vérifier un calcul parallèle

`JobSystem(0)` exécute exactement le même code sans parallélisme. Les tests
comparent ainsi les deux résultats pour vérifier qu'ils sont identiques.

## Exemple

```cpp
#include <cmath>
#include <cstdio>
#include <vector>
#include <jobs/job_system.hpp>

int main() {
	jobs::JobSystem pool; // RecommendedWorkerCount() fils
	std::vector<double> out(100000);
	pool.ParallelFor(out.size(), 1024, [&](size_t i) { out[i] = std::sqrt(double(i)); });

	jobs::JobSystem inlinePool(0); // même calcul, sur le fil appelant
	std::vector<double> ref(out.size());
	inlinePool.ParallelFor(ref.size(), 1024, [&](size_t i) { ref[i] = std::sqrt(double(i)); });

	std::printf("%zu fils, identiques : %d, pic : %d\n", pool.WorkerCount(), out == ref, pool.PeakConcurrentWorkers());
}
```

## Intégration

`modules = ["jobs"]`. Créez **un seul** `JobSystem` par application (ou par
sous-système) et gardez-le pendant toute sa durée de vie : créer des fils à
chaque image coûterait plus cher que le travail lui-même.

## Tests

```sh
cpp-maker test -m jobs
```
