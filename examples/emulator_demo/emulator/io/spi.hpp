/**
 * @file spi.h
 * @brief Définition de la classe Spi, gérant l'interface SPI, le firmware,
 *        l'écran tactile et le microphone pour l’émulateur.
 */

#ifndef SPI_H
#define SPI_H

#include <cstdint>
#include <mutex>
#include <memory>
#include "../state_archive.hpp"

namespace emulator_demo {

class Core;

/**
 * @class Spi
 * @brief Gestion de la communication SPI, du firmware, du tactile et du microphone.
 *
 * Cette classe simule un contrôleur SPI utilisé pour :
 * - Charger et modifier le firmware
 * - Accéder aux registres SPI
 * - Simuler les entrées tactiles
 * - Fournir des données microphone
 * - Gérer les commandes d’accès mémoire via SPI
 */
class Spi {
  public:
    /**
     * @brief Constructeur.
     * @param core Pointeur vers le cœur principal de l'émulateur.
     */
    Spi(Core *core) : core(core) {}

    /**
     * @brief Destructeur : libère la mémoire du firmware et du buffer micro.
     */
    ~Spi();
    
    /**
     * @brief Charge le firmware depuis un fichier ou crée un firmware minimal si absent.
     * Met également à jour l’ID et recalcule plusieurs CRC internes.
     * @return true si le firmware chargé dépasse 0x20000 octets, false sinon.
     */
    bool loadFirmware();
    
    /**
     * @brief Effectue un "direct boot" en copiant une zone du firmware dans la mémoire de la console.
     */
    void directBoot();
    
    /**
     * @brief Calcule la position tactile ADC à partir de coordonnées écran.
     * @param x Position X (1–254)
     * @param y Position Y (1–190)
     */
    void setTouch(int x, int y);

    /**
     * @brief Dernière valeur ADC X/Y simulée par setTouch() — lue par HleArm7
     * pour répondre aux requêtes de polling tactile via IPC.
     */
    uint16_t readTouchX() const {
        return touchX;
    }
    uint16_t readTouchY() const {
        return touchY;
    }

    /**
     * @brief Réinitialise les valeurs tactiles (stylet non pressé).
     */
    void clearTouch();
    
    /**
     * @brief Envoie un buffer audio (microphone).
     * Calcule aussi le pas d'échantillonnage (micStep) en fonction du taux d'échantillonnage fourni.
     * @param samples Tableau d'échantillons 16 bits
     * @param count Nombre d'échantillons
     * @param rate Fréquence d’échantillonnage
     */
    void sendMicData(const int16_t *samples, size_t count, size_t rate);
    
    /**
     * @brief Lit le registre CNT du SPI.
     * @return Valeur du registre spiCnt.
     */
    uint16_t readSpiCnt() const {
      return spiCnt;
    }

    /**
     * @brief Lit le registre DATA du SPI.
     * @return Dernière donnée SPI lue ou générée.
     */
    uint8_t readSpiData() const {
      return spiData;
    }

    /**
     * @brief Écrit dans le registre SPI CNT en respectant un masque matériel.
     * @param mask Masque de bits à modifier
     * @param value Valeur à écrire
     */
    void writeSpiCnt(uint16_t mask, uint16_t value);

    /**
     * @brief Traite l’envoi d’un octet sur le bus SPI et exécute la commande en fonction
     * du mode SPI (lecture firmware, tactile, micro…)
     * @param value Octet envoyé
     */
    void writeSpiData(uint8_t value);

    /**
     * @brief Sérialise/désérialise l'état du SPI pour les save states.
     *
     * Le firmware (chargé depuis le disque via loadFirmware()) et le buffer
     * micro (entrée audio transitoire) ne font pas partie de l'état persistant.
     * @param archive Archive de sauvegarde/chargement.
     */
    void ioState(StateArchive &archive) {
        archive.io(writeCount);
        archive.io(address);
        archive.io(command);
        archive.io(touchX);
        archive.io(touchY);
        archive.io(spiCnt);
        archive.io(spiData);
    }

  private:
    Core *core;                             ///< Pointeur vers le cœur de l'émulateur
    uint8_t        *firmware   = nullptr;   ///< Buffer firmware
    size_t          firmSize   = 0;         ///< Taille du firmware
    int16_t        *micBuffer  = nullptr;   ///< Buffer d’échantillons micro
    size_t          micBufSize = 0;         ///< Taille du buffer micro
    uint32_t        micCycles  = 0;         ///< Cycle global lors de la réception micro
    uint32_t        micStep    = 0;         ///< Pas en cycles entre deux échantillons micro
    uint16_t        micSample  = 0;         ///< Dernier échantillon micro traité
    std::mutex      mutex;                  ///< Mutex protégeant micBuffer et les variables audio
    unsigned int    writeCount = 0;         ///< Nombre d'écritures SPI consécutives
    uint32_t        address    = 0;         ///< Adresse d’accès
    uint8_t         command    = 0;         ///< Commande SPI en cours d’exécution
    uint16_t        touchX     = 0x000;     ///< Dernière valeur ADC X simulée
    uint16_t        touchY     = 0xFFF;     ///< Dernière valeur ADC Y simulée
    uint16_t        spiCnt     = 0;         ///< Registre CNT du SPI
    uint8_t         spiData    = 0;         ///< Registre DATA du SPI

    /**
     * @brief Calcule un CRC16 spécifique utilisé par le firmware.
     * @param value Valeur initiale du CRC
     * @param data Données à traiter
     * @param size Taille des données
     * @return CRC final
     */
    static uint16_t crc16(uint32_t value, uint8_t *data, size_t size);
};

} // namespace emulator_demo

#endif
