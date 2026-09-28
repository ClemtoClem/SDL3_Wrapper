
#ifndef DMA_H
#define DMA_H

#include <memory>
#include <cstdint>
#include <functional>
#include "../state_archive.hpp"

namespace emulator_demo {

class Core;

/**
 * @brief Gestionnaire de transfert DMA (Direct Memory Access).
 *
 * Cette classe implémente la logique des canaux DMA (jusqu'à 4),
 * permettant le transfert direct de données entre la mémoire et les périphériques
 * sans passer par le CPU.
 */
class Dma {
  private:
    Core *core;              /**< Pointeur vers le cœur principal de l'émulateur. */
    bool                  cpu;               /**< Indique le CPU concerné (0 ou 1). */
    uint32_t              srcAddrs[4]   = {};/**< Adresses sources courantes des transferts DMA. */
    uint32_t              dstAddrs[4]   = {};/**< Adresses destinations courantes des transferts DMA. */
    uint32_t              wordCounts[4] = {};/**< Compteurs de mots restants à transférer. */
    uint32_t              dmaSad[4]     = {};/**< Registres DMA Source Address. */
    uint32_t              dmaDad[4]     = {};/**< Registres DMA Destination Address. */
    uint32_t              dmaCnt[4]     = {};/**< Registres DMA Control. */
    std::function<void()> transferTask[4];   /**< Tâches planifiées pour gérer les transferts DMA. */
    
    /**
     * @brief Exécute un transfert DMA pour un canal donné.
     *
     * Cette fonction effectue le transfert mémoire-mémoire ou mémoire-périphérique
     * selon les modes configurés dans les registres de contrôle du DMA.
     *
     * @param channel Index du canal DMA (0 à 3).
     */ 
    void transfer(int channel);

  public:
    /**
     * @brief Constructeur du gestionnaire DMA.
     *
     * Initialise les canaux DMA et associe les tâches de transfert.
     *
     * @param core Pointeur vers le cœur principal de l’émulateur.
     * @param cpu Identifiant du CPU (0 = ARM9, 1 = ARM7).
     */
    Dma(Core *core, bool cpu);

    /**
     * @brief Déclenche les transferts DMA pour les canaux sélectionnés.
     *
     * Vérifie les canaux actifs et planifie les tâches de transfert si les conditions sont remplies.
     *
     * @param mode Mode de déclenchement (ex: VBlank, HBlank, FIFO...).
     * @param channels Masque des canaux concernés (par défaut 0x0F = tous).
     */
    void trigger(int mode, uint8_t channels = 0x0F);

    /**
     * @brief Lit le registre DMA Source Address.
     *
     * @param channel Index du canal DMA (0 à 3).
     * @return Valeur du registre dmaSad[channel].
     */
    uint32_t readDmaSad(int channel) {
      return dmaSad[channel];
    }

    /**
     * @brief Lit le registre DMA Destination Address.
     *
     * @param channel Index du canal DMA (0 à 3).
     * @return Valeur du registre dmaDad[channel].
     */
    uint32_t readDmaDad(int channel) {
      return dmaDad[channel];
    }

    /**
     * @brief Lit le registre DMA Control.
     *
     * @param channel Index du canal DMA (0 à 3).
     * @return Valeur du registre dmaCnt[channel], avec certains bits masqués selon le mode.
     */
    uint32_t readDmaCnt(int channel);

    /**
     * @brief Écrit dans le registre DMA Source Address.
     *
     * @param channel Index du canal DMA (0 à 3).
     * @param mask Masque des bits modifiables.
     * @param value Nouvelle valeur à appliquer.
     */
    void writeDmaSad(int channel, uint32_t mask, uint32_t value);
   
    /**
     * @brief Écrit dans le registre DMA Destination Address.
     *
     * @param channel Index du canal DMA (0 à 3).
     * @param mask Masque des bits modifiables.
     * @param value Nouvelle valeur à appliquer.
     */
    void writeDmaDad(int channel, uint32_t mask, uint32_t value);
    
    /**
     * @brief Écrit dans le registre DMA Control.
     *
     * Met à jour les paramètres du canal DMA et déclenche éventuellement un transfert immédiat.
     *
     * @param channel Index du canal DMA (0 à 3).
     * @param mask Masque des bits modifiables.
     * @param value Nouvelle valeur à appliquer.
     */
    void writeDmaCnt(int channel, uint32_t mask, uint32_t value);

    /**
     * @brief Sérialise/désérialise l'état des 4 canaux DMA pour les save states.
     * @param archive Archive de sauvegarde/chargement.
     */
    void ioState(StateArchive &archive) {
        archive.io(srcAddrs);
        archive.io(dstAddrs);
        archive.io(wordCounts);
        archive.io(dmaSad);
        archive.io(dmaDad);
        archive.io(dmaCnt);
    }

    /**
     * @brief Replanifie les transferts en cours après un chargement d'état.
     *
     * Le scheduler (Core::tasks) n'est pas sérialisé : un transfert GXFIFO
     * (mode 7) en attente de place dans la FIFO doit donc être reprogrammé
     * manuellement, à l'identique de ce que ferait une écriture normale du
     * registre. Les autres modes (immédiat déjà terminé au moment du save,
     * ou déclenchés par un événement externe comme VBlank/HBlank) n'ont pas
     * besoin d'être relancés : ils repartiront naturellement au prochain
     * événement qui les déclenche.
     */
    void rearm();
};

} // namespace emulator_demo

#endif
