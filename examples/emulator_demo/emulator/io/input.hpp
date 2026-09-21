#ifndef INPUT_H
#define INPUT_H

#include <cstdint>
#include <memory>
#include "../state_archive.hpp"

namespace emulator_demo {

class Core;

/**
 * @brief Gestionnaire des entrées (clavier et écran tactile).
 *
 * Cette classe simule les registres d’entrée d’une console (touches et écran tactile),
 * et fournit des méthodes pour appuyer ou relâcher des touches/écran, ainsi que pour
 * lire les registres correspondants.
 */
class Input {
  public:
    /**
     * @brief Constructeur de la classe Input.
     *
     * Initialise les registres d'entrée avec leurs valeurs par défaut.
     *
     * @param core Pointeur vers le cœur principal de l’émulateur.
     */
    Input(Core *core) : core(core) {}

    /**
     * @brief Simule l’appui sur une touche.
     *
     * Modifie le registre interne afin d’indiquer que la touche est pressée.
     *
     * @param key Index de la touche (0–9 pour les touches classiques,
     *            10–11 pour les touches externes).
     */
    void pressKey(int key);

    /**
     * @brief Simule le relâchement d’une touche.
     *
     * Modifie le registre interne afin d’indiquer que la touche n’est plus pressée.
     *
     * @param key Index de la touche (0–9 pour les touches classiques,
     *            10–11 pour les touches externes).
     */
    void releaseKey(int key);

    /**
     * @brief Simule un appui sur l’écran tactile.
     *
     * Modifie le registre interne afin d’indiquer que l’écran est pressé.
     */
    void pressScreen();

    /**
     * @brief Simule le relâchement de l’écran tactile.
     *
     * Modifie le registre interne afin d’indiquer que l’écran n’est plus pressé.
     */
    void releaseScreen();

    /**
     * @brief Lit le registre des touches principales.
     *
     * @return Valeur du registre `keyInput`, indiquant l’état des touches (bits actifs = non pressé).
     */
    uint16_t readKeyInput() {
      return keyInput;
    }

    /**
     * @brief Lit le registre des touches étendues.
     *
     * Inclut les touches supplémentaires et l’état de l’écran tactile.
     *
     * @return Valeur du registre `extKeyIn`.
     */
    uint16_t readExtKeyIn() {
      return extKeyIn;
    }

    /**
     * @brief Sérialise/désérialise les registres d'entrée pour les save states.
     * @param archive Archive de sauvegarde/chargement.
     */
    void ioState(StateArchive &archive) {
        archive.io(keyInput);
        archive.io(extKeyIn);
    }

  private:
    Core *core;                  /**< Pointeur vers le cœur principal de l’émulateur. */
    uint16_t keyInput = 0x03FF;     /**< Registre des touches principales (10 bits). */
    uint16_t extKeyIn = 0x007F;     /**< Registre des touches étendues + écran tactile. */
};

} // namespace emulator_demo

#endif
