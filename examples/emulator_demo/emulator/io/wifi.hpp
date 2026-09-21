/**
 * @file wifi.h
 * @brief Émulation du module WiFi NDS/GBA (MAC + BB) utilisé par le core.
 *
 * Cette classe gère :
 *  - la simulation des registres WiFi (MAC/BB)
 *  - les compteurs internes (timings microsecondes, beacons…)
 *  - les buffers RX/TX circulaires
 *  - la gestion des interruptions WiFi
 *  - l'émulation des transferts de paquets entre instances Wifi
 *
 * Elle représente une interface WiFi pour une instance de Core.
 */

#ifndef WIFI_H
#define WIFI_H

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>
#include <memory>
#include "../state_archive.hpp"

namespace emulator_demo {

class Core;

/**
 * @class Wifi
 * @brief Module d'émulation WiFi pour une instance Core.
 *
 * Chaque Core possède son module Wifi, capable de communiquer avec
 * les autres Wifi via addConnection() / remConnection().
 */
class Wifi {
  public:
  /**
     * @brief Constructeur.
     * @param core Pointeur vers le Core possédant ce module WiFi.
     */
    Wifi(Core *core);

    /**
     * @brief Indique si le module doit reprogrammer son scheduler.
     */
    bool shouldSchedule() {
        return (!connections.empty() || wUsCountcnt) && !scheduled;
    }

     /**
     * @brief Programme la tâche WiFi dans le scheduler du Core.
     */
    void scheduleInit();

    /**
     * @brief Ajoute une connexion WiFi bidirectionnelle avec un autre Core.
     * @param core Core distant à relier.
     */
    void addConnection(Core *core);

    /**
     * @brief Supprime une connexion WiFi bidirectionnelle.
     * @param core Core distant.
     */
    void remConnection(Core *core);
    
    /** @name Lectures des registres MAC/BB */
    ///@{
    uint16_t readWModeWep() const {
        return wModeWep;
    }
    uint16_t readWIrf() const {
        return wIrf;
    }
    uint16_t readWIe() const {
        return wIe;
    }
    uint16_t readWMacaddr(int index) const {
        return wMacaddr[index];
    }
    uint16_t readWBssid(int index) const {
        return wBssid[index];
    }
    uint16_t readWAidFull() const {
        return wAidFull;
    }
    uint16_t readWRxcnt() const {
        return wRxcnt;
    }
    uint16_t readWPowerstate() const {
        return wPowerstate;
    }
    uint16_t readWPowerforce() const {
        return wPowerforce;
    }
    uint16_t readWRxbufBegin() const {
        return wRxbufBegin;
    }
    uint16_t readWRxbufEnd() const {
        return wRxbufEnd;
    }
    uint16_t readWRxbufWrcsr() const {
        return wRxbufWrcsr >> 1;
    }
    uint16_t readWRxbufWrAddr() const {
        return wRxbufWrAddr;
    }
    uint16_t readWRxbufRdAddr() const {
        return wRxbufRdAddr;
    }
    uint16_t readWRxbufReadcsr() const {
        return wRxbufReadcsr;
    }
    uint16_t readWRxbufGap() const {
        return wRxbufGap;
    }
    uint16_t readWRxbufGapdisp() const {
        return wRxbufGapdisp;
    }
    uint16_t readWRxbufCount() const {
        return wRxbufCount;
    }
    uint16_t readWTxbufWrAddr() const {
        return wTxbufWrAddr;
    }
    uint16_t readWTxbufCount() const {
        return wTxbufCount;
    }
    uint16_t readWTxbufGap() const {
        return wTxbufGap;
    }
    uint16_t readWTxbufGapdisp() const {
        return wTxbufGapdisp;
    }
    uint16_t readWTxbufLoc(int index) const {
        return wTxbufLoc[index];
    }
    uint16_t readWBeaconInt() const {
        return wBeaconInt;
    }
    uint16_t readWTxreqRead() const {
        return wTxreqRead;
    }
    uint16_t readWUsCountcnt() const {
        return wUsCountcnt;
    }
    uint16_t readWUsComparecnt() const {
        return wUsComparecnt;
    }
    uint16_t readWPreBeacon() const {
        return wPreBeacon;
    }
    uint16_t readWBeaconCount() const {
        return wBeaconCount;
    }
    uint16_t readWConfig(int index) const {
        return wConfig[index];
    }
    uint16_t readWPostBeacon() const {
        return wPostBeacon;
    }
    uint16_t readWBbRead() const {
        return wBbRead;
    }
    uint16_t readWRxbufRdData();
    ///@}

    /** @name Écritures des registres MAC/BB */
    ///@{
    void writeWModeWep(uint16_t mask, uint16_t value);
    void writeWIrf(uint16_t mask, uint16_t value);
    void writeWIe(uint16_t mask, uint16_t value);
    void writeWMacaddr(int index, uint16_t mask, uint16_t value);
    void writeWBssid(int index, uint16_t mask, uint16_t value);
    void writeWAidFull(uint16_t mask, uint16_t value);
    void writeWRxcnt(uint16_t mask, uint16_t value);
    void writeWPowerstate(uint16_t mask, uint16_t value);
    void writeWPowerforce(uint16_t mask, uint16_t value);
    void writeWRxbufBegin(uint16_t mask, uint16_t value);
    void writeWRxbufEnd(uint16_t mask, uint16_t value);
    void writeWRxbufWrAddr(uint16_t mask, uint16_t value);
    void writeWRxbufRdAddr(uint16_t mask, uint16_t value);
    void writeWRxbufReadcsr(uint16_t mask, uint16_t value);
    void writeWRxbufGap(uint16_t mask, uint16_t value);
    void writeWRxbufGapdisp(uint16_t mask, uint16_t value);
    void writeWTxbufLoc(int index, uint16_t mask, uint16_t value);
    void writeWBeaconInt(uint16_t mask, uint16_t value);
    void writeWTxreqReset(uint16_t mask, uint16_t value);
    void writeWTxreqSet(uint16_t mask, uint16_t value);
    void writeWUsCountcnt(uint16_t mask, uint16_t value);
    void writeWUsComparecnt(uint16_t mask, uint16_t value);
    void writeWPreBeacon(uint16_t mask, uint16_t value);
    void writeWBeaconCount(uint16_t mask, uint16_t value);
    void writeWRxbufCount(uint16_t mask, uint16_t value);
    void writeWTxbufWrAddr(uint16_t mask, uint16_t value);
    void writeWTxbufCount(uint16_t mask, uint16_t value);
    void writeWTxbufWrData(uint16_t mask, uint16_t value);
    void writeWTxbufGap(uint16_t mask, uint16_t value);
    void writeWTxbufGapdisp(uint16_t mask, uint16_t value);
    void writeWConfig(int index, uint16_t mask, uint16_t value);
    void writeWPostBeacon(uint16_t mask, uint16_t value);
    void writeWBbCnt(uint16_t mask, uint16_t value);
    void writeWBbWrite(uint16_t mask, uint16_t value);
    void writeWIrfSet(uint16_t mask, uint16_t value);
    ///@}

    /**
     * @brief Sérialise/désérialise les registres WiFi pour les save states.
     *
     * Les connexions multijoueur (connections) et les paquets en attente
     * (packets) sont des pointeurs vers d'autres instances vivantes et des
     * données de session : ils ne font pas partie d'un instantané et sont
     * reconstruits par la couche applicative si besoin.
     * @param archive Archive de sauvegarde/chargement.
     */
    void ioState(StateArchive &archive) {
        archive.io(bbRegisters);
        archive.io(wModeWep);
        archive.io(wIrf);
        archive.io(wIe);
        archive.io(wMacaddr);
        archive.io(wBssid);
        archive.io(wAidFull);
        archive.io(wRxcnt);
        archive.io(wPowerstate);
        archive.io(wPowerforce);
        archive.io(wRxbufBegin);
        archive.io(wRxbufEnd);
        archive.io(wRxbufWrcsr);
        archive.io(wRxbufWrAddr);
        archive.io(wRxbufRdAddr);
        archive.io(wRxbufReadcsr);
        archive.io(wRxbufGap);
        archive.io(wRxbufGapdisp);
        archive.io(wTxbufLoc);
        archive.io(wBeaconInt);
        archive.io(wTxreqRead);
        archive.io(wUsCountcnt);
        archive.io(wUsComparecnt);
        archive.io(wPreBeacon);
        archive.io(wBeaconCount);
        archive.io(wRxbufCount);
        archive.io(wTxbufWrAddr);
        archive.io(wTxbufCount);
        archive.io(wTxbufGap);
        archive.io(wTxbufGapdisp);
        archive.io(wPostBeacon);
        archive.io(wBbWrite);
        archive.io(wBbRead);
        archive.io(wConfig);
    }

    /**
     * @brief Réinitialise le drapeau de planification interne après un chargement d'état.
     *
     * `scheduled` n'est pas sérialisé : le scheduler (Core::tasks) est
     * entièrement reconstruit lors d'un chargement, donc la tâche countMs
     * n'est plus programmée. Ce drapeau doit être remis à false pour que
     * shouldSchedule()/scheduleInit() puissent la reprogrammer normalement.
     */
    void resetSchedule() {
        scheduled = false;
    }

    /**
     * @brief Pont réseau optionnel (app::NetBridge) : reçoit une copie de
     * chaque paquet émis par transfer(), en plus de la diffusion locale vers
     * `connections`. Laissé vide si aucune session réseau n'est active.
     * @param data Paquet brut (mêmes octets que ceux placés dans le TX buffer).
     * @param len Taille en octets.
     */
    std::function<void(const uint8_t *data, size_t len)> onPacketSent;

    /**
     * @brief Injecte un paquet reçu depuis le pont réseau (app::NetBridge),
     * comme s'il avait été livré par une connexion locale (addConnection()).
     * @param data Paquet brut.
     * @param len Taille en octets (doit être paire).
     */
    void injectPacket(const uint8_t *data, size_t len);

private:
    Core *core;                             ///< Instance `Core` parent.
    std::vector<Wifi *>     connections;    ///< Connexions `WiFi` actives
    std::vector<uint16_t *> packets;        ///< File de paquets en attente.
    std::mutex              mutex;          ///< Mutex de protection des buffers.

    bool     scheduled          = false;
    uint8_t  bbRegisters[0x100] = {};
    uint16_t wModeWep           = 0;
    uint16_t wIrf               = 0;
    uint16_t wIe                = 0;
    uint16_t wMacaddr[3]        = {};
    uint16_t wBssid[3]          = {};
    uint16_t wAidFull           = 0;
    uint16_t wRxcnt             = 0;
    uint16_t wPowerstate        = 0x0200;
    uint16_t wPowerforce        = 0;
    uint16_t wRxbufBegin        = 0;
    uint16_t wRxbufEnd          = 0;
    uint16_t wRxbufWrcsr        = 0;
    uint16_t wRxbufWrAddr       = 0;
    uint16_t wRxbufRdAddr       = 0;
    uint16_t wRxbufReadcsr      = 0;
    uint16_t wRxbufGap          = 0;
    uint16_t wRxbufGapdisp      = 0;
    uint16_t wTxbufLoc[5]       = {};
    uint16_t wBeaconInt         = 0;
    uint16_t wTxreqRead         = 0x0010;
    uint16_t wUsCountcnt        = 0;
    uint16_t wUsComparecnt      = 0;
    uint16_t wPreBeacon         = 0;
    uint16_t wBeaconCount       = 0;
    uint16_t wRxbufCount        = 0;
    uint16_t wTxbufWrAddr       = 0;
    uint16_t wTxbufCount        = 0;
    uint16_t wTxbufGap          = 0;
    uint16_t wTxbufGapdisp      = 0;
    uint16_t wPostBeacon        = 0;
    uint16_t wBbWrite           = 0;
    uint16_t wBbRead            = 0;
    uint16_t wConfig[15]        = {0x0048, 0x4840, 0x0000, 0x0000, 0x0142, 0x8064, 0x0000, 0x2443,
                                   0x0042, 0x0016, 0x0016, 0x0016, 0x162C, 0x0204, 0x0058};

    std::function<void()> countMsTask; ///< Tâche pour le scheduler (tick WiFi).

    /**
     * @brief Déclenche un bit d'interruption WiFi.
     * @param bit Numéro de bit dans wIrf.
     */
    void sendInterrupt(int bit);

    /**
     * @brief Tick périodique microseconde / beacon / timers.
     */
    void countMs();

    /**
     * @brief Défile et traite les paquets reçus.
     */
    void processPackets();

    /**
     * @brief Envoie un paquet depuis un TX buffer.
     * @param index Indice du TX buffer (0–4).
     */
    void transfer(int index);
};

} // namespace emulator_demo

#endif
