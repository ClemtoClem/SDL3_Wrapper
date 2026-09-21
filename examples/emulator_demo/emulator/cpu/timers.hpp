#ifndef TIMERS_H
#define TIMERS_H

#include <cstdint>
#include <functional>
#include <memory>
#include "../state_archive.hpp"

namespace emulator_demo {

class Core;

/**
 * @brief Gestionnaire des minuteries (timers) matériels.
 *
 * Cette classe implémente les 4 minuteries disponibles pour chaque CPU.
 * Elles peuvent être utilisées pour générer des interruptions, synchroniser
 * des événements (comme l'audio), ou chaîner plusieurs timers.
 */
class Timers {
  public:
    /**
     * @brief Constructeur de la classe Timers.
     *
     * Initialise les structures internes et associe les tâches de débordement.
     *
     * @param core Pointeur vers le cœur principal de l’émulateur.
     * @param cpu Identifiant du CPU concerné (false = CPU0, true = CPU1).
     */
    Timers(Core *core, bool cpu);

    /**
     * @brief Réinitialise le décompte des cycles.
     *
     * Ajuste les compteurs internes en soustrayant le nombre
     * total de cycles globaux écoulés.
     */
    void resetCycles();

    /**
     * @brief Lit le registre TMCNT_H.
     *
     * Ce registre contient le contrôle du timer (activation, mode, IRQ...).
     *
     * @param timer Index du timer (0 à 3).
     * @return Valeur du registre TMCNT_H du timer.
     */
    uint16_t readTmCntH(int timer) {
        return tmCntH[timer];
    }

    /**
     * @brief Lit le registre TMCNT_L (valeur courante du timer).
     *
     * Si le timer est actif, la valeur courante est recalculée
     * en fonction des cycles globaux et du prescaler.
     *
     * @param timer Index du timer (0 à 3).
     * @return Valeur courante du compteur du timer.
     */
    uint16_t readTmCntL(int timer);

    /**
     * @brief Écrit dans le registre TMCNT_L (valeur de départ).
     *
     * @param timer Index du timer (0 à 3).
     * @param mask Masque des bits modifiables.
     * @param value Nouvelle valeur à écrire.
     */
    void writeTmCntL(int timer, uint16_t mask, uint16_t value);

    /**
     * @brief Écrit dans le registre TMCNT_H (contrôle du timer).
     *
     * Met à jour la configuration du timer (activation, mode cascade,
     * prescaler, génération d’interruption).
     *
     * @param timer Index du timer (0 à 3).
     * @param mask Masque des bits modifiables.
     * @param value Nouvelle valeur à écrire.
     */
    void writeTmCntH(int timer, uint16_t mask, uint16_t value);

    /**
     * @brief Sérialise/désérialise l'état des 4 minuteries pour les save states.
     * @param archive Archive de sauvegarde/chargement.
     */
    void ioState(StateArchive &archive) {
        archive.io(timers);
        archive.io(shifts);
        archive.io(endCycles);
        archive.io(tmCntL);
        archive.io(tmCntH);
    }

    /**
     * @brief Replanifie les débordements des minuteries actives après un chargement d'état.
     *
     * Le scheduler n'étant pas sérialisé, chaque timer actif (non cascadé, ou
     * timer 0) doit être reprogrammé pour déborder au cycle indiqué par
     * endCycles[], qui vient d'être restauré.
     */
    void rearm();

  private:
    Core *core;                 /**< Pointeur vers le cœur principal de l’émulateur. */
    bool                  cpu;                  /**< CPU concerné (false = CPU0, true = CPU1). */
    uint16_t              timers[4]    = {};    /**< Valeurs courantes des compteurs de timers. */
    uint8_t               shifts[4]    = {};    /**< Prescaler appliqué à chaque timer (en puissance de 2). */
    uint32_t              endCycles[4] = {};    /**< Cycle global prévu pour le prochain débordement. */
    uint16_t              tmCntL[4]    = {};    /**< Registres TMCNT_L (valeurs de départ). */
    uint16_t              tmCntH[4]    = {};    /**< Registres TMCNT_H (contrôle des timers). */
    std::function<void()> overflowTask[4];      /**< Tâches déclenchées lors d’un débordement. */

    /**
     * @brief Gère un débordement de timer.
     *
     * Réinitialise le compteur, planifie le prochain débordement,
     * déclenche une interruption si nécessaire, et gère le chaînage
     * avec le timer suivant.
     *
     * @param timer Index du timer (0 à 3).
     */
    void overflow(int timer);
};

} // namespace emulator_demo

#endif
