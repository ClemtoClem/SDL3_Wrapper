/**
 * @file rtc.h
 * @brief Déclaration de la classe Rtc, simulant l’horloge temps réel (RTC) et le port GPIO RTC.
 */

#ifndef RTC_H
#define RTC_H

#include <cstdint>
#include <memory>
#include "../defines.hpp"
#include "../state_archive.hpp"

namespace emulator_demo {

class Core;

/**
 * @class Rtc
 * @brief Simulation RTC pour GBA/NDS : communication série, registres, date/heure et GPIO associés.
 *
 * Cette classe gère :
 * - Le protocole série RTC (CS/SCK/SIO)
 * - Les registres internes du RTC
 * - La lecture/écriture de l'heure en BCD
 * - Le fonctionnement différent entre mode NDS et GBA
 * - Les lignes GPIO (gpDirection, gpControl, gpData)
 */
class Rtc {
  public:
    /**
     * @brief Constructeur.
     * @param core Pointeur vers l'objet Core.
     */
    Rtc(Core *core) : core(core) { }

    /**
     * @brief Active la gestion du RTC via le GPIO (mode NDS).
     */
    void enableGpRtc() {
        gpRtc = true;
    }

    /**
     * @brief Réinitialise les registres RTC internes.
     */
    void reset();

    /**
     * @brief Lit la valeur du registre RTC principal.
     * @return Octet contenant CS/SCK/SIO ainsi que les flags RTC.
     */
    uint8_t readRtc();

    /**
     * @brief Lit l'état du port GPIO (lignes CS/SIO/SCK).
     * @return Valeur bit à bit.
     */
    uint16_t readGpData();

    /**
     * @brief Lit la direction GPIO.
     * @return Masque de direction : 1 = sortie, 0 = entrée.
     */
    uint16_t readGpDirection() {
        return gpDirection;
    }

    /**
     * @brief Lit le registre de contrôle GPIO.
     * @return Valeur du registre.
     */
    uint16_t readGpControl() {
        return gpControl;
    }

    /**
     * @brief Écrit dans le registre RTC principal (affecte CS/SCK/SIO).
     * @param value Valeur écrite.
     */
    void writeRtc(uint8_t value);

    /**
     * @brief Écrit dans le port GPIO RTC (bits CS/SIO/SCK).
     * @param value Nouveaux bits.
     * @param mask Masque des bits à modifier.
     */
    void writeGpData(uint16_t value, uint16_t mask);

    /**
     * @brief Configure la direction des lignes GPIO.
     * @param value Direction : 1 = sortie.
     * @param mask Masque des bits concernés.
     */
    void writeGpDirection(uint16_t value, uint16_t mask);

    /**
     * @brief Écrit dans le registre de contrôle GPIO.
     * @param value Nouvelle valeur.
     * @param mask Masque des bits modifiables.
     */
    void writeGpControl(uint16_t value, uint16_t mask);

    /**
     * @brief Sérialise/désérialise l'état du RTC pour les save states.
     * @param archive Archive de sauvegarde/chargement.
     */
    void ioState(StateArchive &archive) {
        archive.io(gpRtc);
        archive.io(csCur);
        archive.io(sckCur);
        archive.io(sioCur);
        archive.io(writeCount);
        archive.io(command);
        archive.io(control);
        archive.io(dateTime);
        archive.io(rtc);
        archive.io(gpDirection);
        archive.io(gpControl);
    }

  private:
    Core *core;                   ///< Référence au cœur principal.
    bool gpRtc           = false; ///< Le RTC est-il piloté par le GPIO ?
    bool csCur           = false; ///< Ligne CS actuelle.
    bool sckCur          = false; ///< Ligne SCK actuelle.
    bool sioCur          = false; ///< Ligne SIO actuelle.
    uint8_t writeCount   = 0;     ///< Nombre de bits écrits dans la commande.
    uint8_t command      = 0;     ///< Dernière commande reçue.
    uint8_t control      = 0;     ///< Registre de contrôle RTC.
    uint8_t dateTime[7]  = {};    ///< Données date/heure encodées en BCD.
    uint8_t rtc          = 0;     ///< Registre RTC principal.
    uint16_t gpDirection = 0;     ///< Direction des GPIO.
    uint16_t gpControl   = 0;     ///< Contrôle GPIO.

    /**
     * @brief Met à jour le protocole série RTC en fonction de CS/SCK/SIO.
     * @details Gère l’écriture de commandes 8 bits, puis :
     * - Lecture de registres si le bit 0 de la commande vaut 1
     * - Écriture sinon
     * @param cs État ligne CS
     * @param sck État ligne SCK
     * @param sio État ligne SIO
     */
    void updateRtc(bool cs, bool sck, bool sio);

    /**
     * @brief Met à jour la date/heure à partir de l'heure système.
     * @details Format utilisé : BCD (année/mois/jour/heure/min/sec)
     * Comportement 12h/24h dépend du mode GBA/NDS.
     */
    void updateDateTime();

    /**
     * @brief Lit un registre interne du RTC.
     * @param index Index du registre (différent en mode GBA).
     * @return Bit lu.
     */
    bool readRegister(uint8_t index);

    /**
     * @brief Écrit un bit dans un registre interne RTC.
     * @param index Index du registre.
     * @param value Valeur du bit.
     */
    void writeRegister(uint8_t index, bool value);
};

} // namespace emulator_demo

#endif
