
#ifndef SPU_H
#define SPU_H
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <queue>
#include <mutex>
#include <memory>
#include "../state_archive.hpp"

namespace emulator_demo {

class Core;

/**
 * @brief Sound Processing Unit (SPU) emulator class.
 * 
 * Cette classe gère l'émulation audio des systèmes supportés (GBA, etc.).
 * Elle traite les registres sonores, les buffers audio, les mixages, 
 * et la génération des échantillons.
 */
class Spu {
  public:
    /**
     * @brief Construit une instance du SPU.
     * @param core Pointeur vers l'instance principale du noyau (Core).
     */
    Spu(Core *core);

    /**
     * @brief Destructeur, libère les buffers audio.
     */
    ~Spu();

    /**
     * @brief Planifie l'initialisation de la tâche audio principale.
     */
    void scheduleInit();

    /**
     * @brief Planifie l'initialisation de la tâche audio spécifique GBA.
     */
    void gbaScheduleInit();

    /**
     * @brief Récupère un buffer d'échantillons audio.
     * @param count Nombre d'échantillons souhaités.
     * @return Pointeur vers un tableau d'échantillons audio (à libérer par l'appelant).
     */
    uint32_t *getSamples(int count);

    /**
     * @brief Déclenche le traitement FIFO du GBA pour un timer donné.
     * @param timer Numéro du timer GBA utilisé.
     */
    void gbaFifoTimer(int timer);

    /**
     * @brief Lecture du registre GBA SOUNDxCNT_L.
     * @param channel Canal sonore (0–3).
     * @return Valeur du registre.
     */
    uint8_t readGbaSoundCntL(int channel);

    /**
     * @brief Lecture du registre GBA SOUNDxCNT_H.
     * @param channel Canal sonore (0–3).
     * @return Valeur du registre.
     */
    uint16_t readGbaSoundCntH(int channel);

    /**
     * @brief Lecture du registre GBA SOUNDxCNT_X.
     * @param channel Canal sonore (0–3).
     * @return Valeur du registre.
     */
    uint16_t readGbaSoundCntX(int channel);

    /**
     * @brief Lecture du registre GBA SOUNDCNT_L (global).
     * @return Valeur du registre.
     */
    uint16_t readGbaMainSoundCntL();

    /**
     * @brief Lecture du registre GBA SOUNDCNT_H (global).
     * @return Valeur du registre.
     */
    uint16_t readGbaMainSoundCntH();

    /**
     * @brief Lecture du registre GBA SOUNDCNT_X (global).
     * @return Valeur du registre.
     */
    uint8_t readGbaMainSoundCntX();

    /**
     * @brief Lecture du registre GBA SOUNDBIAS.
     * @return Valeur du registre.
     */
    uint16_t readGbaSoundBias();

    /**
     * @brief Lecture de la Wave RAM GBA.
     * @param index Index (0–31).
     * @return Octet stocké.
     */
    uint8_t readGbaWaveRam(int index);

    /**
     * @brief Lecture du registre SOUNDxCNT.
     * @param channel Canal sonore (0–15).
     * @return Valeur du registre.
     */
    uint32_t readSoundCnt(int channel);

    /**
     * @brief Lecture du registre MAIN_SOUNDCNT.
     * @return Valeur du registre.
     */
    uint16_t readMainSoundCnt();

    /**
     * @brief Lecture du registre SOUNDBIAS.
     * @return Valeur du registre.
     */
    uint16_t readSoundBias();

    /**
     * @brief Lecture du registre SNDCAPCNT (Sound Capture).
     * @param channel Canal de capture (0 ou 1).
     * @return Valeur du registre.
     */
    uint8_t readSndCapCnt(int channel);

    /**
     * @brief Lecture du registre SNDCAPDAD (destination address).
     * @param channel Canal de capture (0 ou 1).
     * @return Valeur du registre.
     */
    uint32_t readSndCapDad(int channel);

    /**
     * @brief Écriture dans le registre GBA SOUNDxCNT_L.
     * @param channel Canal sonore (0–3).
     * @param value Valeur à écrire.
     */
    void writeGbaSoundCntL(int channel, uint8_t value);

    /**
     * @brief Écriture dans le registre GBA SOUNDxCNT_H.
     * @param channel Canal sonore (0–3).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void writeGbaSoundCntH(int channel, uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans le registre GBA SOUNDxCNT_X.
     * @param channel Canal sonore (0–3).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void writeGbaSoundCntX(int channel, uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans le registre global GBA SOUNDCNT_L.
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void writeGbaMainSoundCntL(uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans le registre global GBA SOUNDCNT_H.
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void writeGbaMainSoundCntH(uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans le registre global GBA SOUNDCNT_X.
     * @param value Valeur à écrire.
     */
    void writeGbaMainSoundCntX(uint8_t value);

    /**
     * @brief Écriture dans le registre global GBA SOUNDBIAS.
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void writeGbaSoundBias(uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans la Wave RAM GBA.
     * @param index Index (0–31).
     * @param value Octet à écrire.
     */
    void writeGbaWaveRam(int index, uint8_t value);

    /**
     * @brief Écriture dans le FIFO A du GBA.
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void writeGbaFifoA(uint32_t mask, uint32_t value);

    /**
     * @brief Écriture dans le FIFO B du GBA.
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void writeGbaFifoB(uint32_t mask, uint32_t value);

    /**
     * @brief Écriture dans le registre SOUNDxCNT.
     * @param channel Canal sonore (0–15).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void writeSoundCnt(int channel, uint32_t mask, uint32_t value);

    /**
     * @brief Écriture dans le registre SOUNDxSAD.
     * @param channel Canal sonore (0–15).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void writeSoundSad(int channel, uint32_t mask, uint32_t value);

    /**
     * @brief Écriture dans le registre SOUNDxTMR.
     * @param channel Canal sonore (0–15).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void writeSoundTmr(int channel, uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans le registre SOUNDxPNT.
     * @param channel Canal sonore (0–15).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void writeSoundPnt(int channel, uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans le registre SOUNDxLEN.
     * @param channel Canal sonore (0–15).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void writeSoundLen(int channel, uint32_t mask, uint32_t value);

    /**
     * @brief Écriture dans le registre MAIN_SOUNDCNT.
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void writeMainSoundCnt(uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans le registre SOUNDBIAS.
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void writeSoundBias(uint16_t mask, uint16_t value);

    /**
     * @brief Écriture dans le registre SNDCAPCNT (Sound Capture).
     * @param channel Canal de capture (0 ou 1).
     * @param value Valeur à écrire.
     */
    void writeSndCapCnt(int channel, uint8_t value);

    /**
     * @brief Écriture dans le registre SNDCAPDAD (Sound Capture).
     * @param channel Canal de capture (0 ou 1).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void writeSndCapDad(int channel, uint32_t mask, uint32_t value);

    /**
     * @brief Écriture dans le registre SNDCAPLEN (Sound Capture).
     * @param channel Canal de capture (0 ou 1).
     * @param mask Masque de bits.
     * @param value Valeur à écrire.
     */
    void writeSndCapLen(int channel, uint16_t mask, uint16_t value);

    /**
     * @brief Sérialise/désérialise l'état du SPU pour les save states.
     *
     * Le buffer en cours de remplissage (bufferIn) n'est pas conservé, seul
     * le buffer déjà prêt (bufferOut) l'est ; la synchronisation avec le
     * thread audio de la couche applicative (pause pendant l'opération) est
     * de la responsabilité de l'appelant.
     * @param archive Archive de sauvegarde/chargement.
     */
    void ioState(StateArchive &archive);

  private:
    Core*     core; //!< Pointeur vers le noyau principal.

    // Buffers audio internes
    uint32_t *bufferIn = nullptr, *bufferOut = nullptr;
    int       bufferSize = 0, bufferPointer = 0;

    // Synchronisation multi-thread
    std::condition_variable cond1, cond2;
    std::mutex              mutex1, mutex2;
    std::atomic<bool>       ready;

    // État interne du GBA (timers, enveloppes, RAM audio, etc.)
    int                     gbaFrameSequencer = 0;
    int                     gbaSoundTimers[4] = {};
    int                     gbaEnvelopes[3]   = {};
    int                     gbaEnvTimers[3]   = {};
    int                     gbaSweepTimer     = 0;
    int                     gbaWaveDigit      = 0;
    uint16_t                gbaNoiseValue     = 0;
    uint8_t                 gbaWaveRam[2][16] = {};
    std::queue<int8_t>      gbaFifoA, gbaFifoB;
    int8_t                  gbaSampleA = 0, gbaSampleB = 0;
    uint16_t                enabled = 0;
    static const int        indexTable[8];
    static const int16_t    adpcmTable[89];
    int32_t                 adpcmValue[16] = {}, adpcmLoopValue[16] = {};
    int                     adpcmIndex[16] = {}, adpcmLoopIndex[16] = {};
    bool                    adpcmToggle[16]  = {};
    int                     dutyCycles[6]    = {};
    uint16_t                noiseValues[2]   = {};
    uint32_t                soundCurrent[16] = {};
    uint16_t                soundTimers[16]  = {};
    uint32_t                sndCapCurrent[2] = {};
    uint16_t                sndCapTimers[2]  = {};
    uint8_t                 gbaSoundCntL[2]  = {};
    uint16_t                gbaSoundCntH[4]  = {};
    uint16_t                gbaSoundCntX[4]  = {};
    uint16_t                gbaMainSoundCntL = 0;
    uint16_t                gbaMainSoundCntH = 0;
    uint8_t                 gbaMainSoundCntX = 0;
    uint16_t                gbaSoundBias     = 0;
    uint32_t                soundCnt[16]     = {};
    uint32_t                soundSad[16]     = {};
    uint16_t                soundTmr[16]     = {};
    uint16_t                soundPnt[16]     = {};
    uint32_t                soundLen[16]     = {};
    uint16_t                mainSoundCnt     = 0;
    uint16_t                soundBias        = 0;
    uint8_t                 sndCapCnt[2]     = {};
    uint32_t                sndCapDad[2]     = {};
    uint16_t                sndCapLen[2]     = {};
    std::function<void()>   runGbaSampleTask;
    std::function<void()>   runSampleTask;
    
    /**
     * @brief Tâche périodique : génère les échantillons GBA.
     */
    void runGbaSample();

    /**
     * @brief Tâche périodique : génère les échantillons audio généraux.
     */
    void runSample();

    /**
     * @brief Échange les buffers audio (double buffering).
     */
    void swapBuffers();

    /**
     * @brief Active un canal sonore donné.
     * @param channel Index du canal (0–15).
     */
    void startChannel(int channel);
};

} // namespace emulator_demo

#endif
