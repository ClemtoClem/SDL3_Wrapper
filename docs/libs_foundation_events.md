# events — signaux et bus d'évènements

`libs/foundation/events` · couche **foundation** · `libevents.a` · espace de noms `events::`

## Présentation

Ce module fournit deux mécanismes de diffusion, sans aucune dépendance :

- `Signal<Args...>` : un patron observateur typé, à connexions multiples ;
- `EventBus` : une diffusion **par type d'évènement**, immédiate ou différée.
  L'émetteur et les récepteurs ne se connaissent pas. Le mode différé est sûr
  entre fils.

[`platform`](libs_infrastructure_platform.md) traduit les évènements SDL
sur un `EventBus`, et [`voxel`](libs_infrastructure_voxel.md) s'en sert pour
les évènements de jeu.

## Architecture interne

| Fichier | Contenu |
|---|---|
| `include/events/events.hpp` | En-tête parapluie |
| `include/events/signal.hpp`, `src/signal.cpp` | `Connection`, `ScopedConnection`, `Signal<Args...>` |
| `include/events/event_bus.hpp`, `src/event_bus.cpp` | `EventBus` |

## API publique

### `Signal<Args...>`

| Membre | Rôle |
|---|---|
| `Connection Connect(Slot)` | Connecte une `std::function<void(Args...)>` ; un slot vide est ignoré |
| `void Emit(Args...)` / `operator()` | Appelle chaque slot connecté, dans l'ordre de connexion |
| `size_t SlotCount()`, `bool IsEmpty()` | Slots encore connectés |
| `void DisconnectAll()` | Coupe toutes les connexions |

- **Réentrance** : pendant `Emit`, un slot peut se déconnecter, en déconnecter
  un autre ou en connecter un nouveau. `Emit` parcourt un instantané de la
  liste, et un slot ajouté pendant l'émission ne sera appelé qu'à la suivante.
- **Fils** : un `Signal` n'est pas synchronisé. Il s'utilise depuis un seul
  fil.

### `Connection` et `ScopedConnection`

- **`Connection`** : jeton copiable qui permet de se déconnecter
  (`Disconnect()`, `IsConnected()`). Il reste valide même après la destruction
  du signal, car il ne référence qu'un état partagé.
- **`ScopedConnection`** : forme RAII, déplaçable et non copiable. Elle se
  déconnecte à sa destruction. C'est la forme à préférer pour un objet qui
  écoute un signal plus durable que lui. `Release()` abandonne la connexion
  sans la couper.

### `EventBus`

Un type d'évènement est n'importe quel type copiable ou déplaçable. Il n'y a
ni hiérarchie à respecter ni identifiant à déclarer.

| Membre | Rôle |
|---|---|
| `template<class E> Connection Subscribe(std::function<void(const E&)>)` | Abonnement aux évènements de type `E` |
| `template<class E> void Publish(const E&)` | Livraison **immédiate**, sur le fil appelant |
| `template<class E> void Enqueue(E)` | Mise en file protégée par un mutex : **seul point d'entrée sûr depuis un autre fil** |
| `size_t Dispatch()` | Livre, dans l'ordre d'arrivée, les évènements en file au moment de l'appel ; rend leur nombre |
| `size_t PendingCount() const` | Taille de la file |
| `template<class E> size_t SubscriberCount() const` | Nombre d'abonnés à `E` |
| `void ClearQueue()` | Vide la file sans rien livrer |

Un évènement mis en file **pendant** un `Dispatch()` sera livré au suivant. Un
gestionnaire qui ré-enfile un évènement ne peut donc pas faire boucler
indéfiniment un même `Dispatch()`.

## Exemple

```cpp
#include <cstdio>
#include <thread>
#include <events/events.hpp>

struct ScoreChanged {
	int player;
	int score;
};

int main() {
	events::Signal<int, int> resized;
	{
		events::ScopedConnection c = resized.Connect([](int w, int h) { std::printf("taille %dx%d\n", w, h); });
		resized.Emit(800, 600); // appelé
	}
	resized.Emit(1024, 768); // la connexion est rompue : rien n'est appelé

	events::EventBus bus;
	events::ScopedConnection sub = bus.Subscribe<ScoreChanged>(
		[](const ScoreChanged &e) { std::printf("joueur %d : %d points\n", e.player, e.score); });
	bus.Publish(ScoreChanged{1, 10}); // immédiat

	std::thread worker([&] { bus.Enqueue(ScoreChanged{2, 25}); }); // autre fil
	worker.join();
	bus.Dispatch(); // livré sur le fil principal
}
```

## Intégration

`modules = ["events"]`. Dans une application, on appelle en général
`Dispatch()` une fois par image depuis le fil principal. C'est ce que fait
`platform::Application` (voir [platform](libs_infrastructure_platform.md)).

## Tests

```sh
cpp-maker test -m events
```
