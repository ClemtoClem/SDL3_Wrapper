#ifndef IPC_H
#define IPC_H

#include <cstdint>
#include <queue>
#include <memory>
#include "../state_archive.hpp"

namespace emulator_demo {

class Core;

/**
 * @brief Gestion de l'IPC (Inter-Processor Communication).
 *
 * Cette classe simule le mécanisme d'échange de données entre les deux CPU
 * d'une console via les registres IPC (synchronisation et FIFOs).
 */
class Ipc {
  public:
    /**
     * @brief Constructeur de la classe Ipc.
     *
     * Initialise les registres IPC et associe le cœur principal.
     *
     * @param core Pointeur vers le cœur principal de l’émulateur.
     */
    Ipc(Core *core) : core(core) {
    }

    /**
     * @brief Lit le registre IPCSYNC.
     *
     * Ce registre contient des bits de synchronisation entre les deux CPU.
     *
     * @param cpu Identifiant du CPU (false = CPU0, true = CPU1).
     * @return Valeur du registre ipcSync[cpu].
     */
    uint16_t readIpcSync(bool cpu) {
      return ipcSync[cpu];
    }

    /**
     * @brief Lit le registre IPCFIFOCNT.
     *
     * Ce registre contient les flags de gestion de la FIFO IPC
     * (état vide, plein, erreurs, interruptions).
     *
     * @param cpu Identifiant du CPU (false = CPU0, true = CPU1).
     * @return Valeur du registre ipcFifoCnt[cpu].
     */
    uint16_t readIpcFifoCnt(bool cpu) {
      return ipcFifoCnt[cpu];
    }

    /**
     * @brief Lit le registre IPCFIFORECV.
     *
     * Récupère une valeur depuis la FIFO de l'autre CPU si disponible.
     *
     * @param cpu Identifiant du CPU (false = CPU0, true = CPU1).
     * @return Dernière valeur reçue via la FIFO.
     */
    uint32_t readIpcFifoRecv(bool cpu);

    /**
     * @brief Écrit dans le registre IPCSYNC.
     *
     * Met à jour les bits de synchronisation et déclenche éventuellement
     * une interruption sur l'autre CPU.
     *
     * @param cpu Identifiant du CPU (false = CPU0, true = CPU1).
     * @param mask Masque des bits modifiables.
     * @param value Valeur à appliquer.
     */
    void writeIpcSync(bool cpu, uint16_t mask, uint16_t value);

    /**
     * @brief Écrit dans le registre IPCFIFOCNT.
     *
     * Met à jour les flags de la FIFO et déclenche les interruptions associées
     * (vidage, erreurs, réception...).
     *
     * @param cpu Identifiant du CPU (false = CPU0, true = CPU1).
     * @param mask Masque des bits modifiables.
     * @param value Valeur à appliquer.
     */
    void writeIpcFifoCnt(bool cpu, uint16_t mask, uint16_t value);

    /**
     * @brief Écrit dans le registre IPCFIFOSEND.
     *
     * Envoie une donnée dans la FIFO de l'autre CPU si elle n'est pas pleine,
     * ou déclenche un flag d'erreur si la FIFO est pleine.
     *
     * @param cpu Identifiant du CPU (false = CPU0, true = CPU1).
     * @param mask Masque des bits applicables.
     * @param value Valeur à envoyer.
     */
    void writeIpcFifoSend(bool cpu, uint32_t mask, uint32_t value);

    /**
     * @brief Sérialise/désérialise l'état de l'IPC (registres et FIFOs) pour les save states.
     * @param archive Archive de sauvegarde/chargement.
     */
    void ioState(StateArchive &archive) {
        archive.ioQueue(fifos[0]);
        archive.ioQueue(fifos[1]);
        archive.io(ipcSync);
        archive.io(ipcFifoCnt);
        archive.io(ipcFifoRecv);
    }

  private:
    Core *core;                /**< Pointeur vers le cœur principal. */
    std::queue<uint32_t> fifos[2];            /**< Files FIFO (une par CPU). */
    uint16_t             ipcSync[2]     = {}; /**< Registres IPCSYNC (synchronisation). */
    uint16_t             ipcFifoCnt[2]  = {0x0101, 0x0101}; /**< Registres IPCFIFOCNT (gestion FIFO). */
    uint32_t             ipcFifoRecv[2] = {}; /**< Dernières valeurs reçues depuis la FIFO. */
};

} // namespace emulator_demo

#endif
