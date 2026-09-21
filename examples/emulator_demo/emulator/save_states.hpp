#ifndef SAVE_STATE_H
#define SAVE_STATE_H

#include <cstdint>

#include "core.hpp"

namespace emulator_demo {

enum StateResult {
    STATE_SUCCESS,
    STATE_FILE_FAIL,
    STATE_FORMAT_FAIL,
    STATE_VERSION_FAIL
};

/**
 * @brief Sauvegarde/chargement d'un instantané complet d'un Core (save state).
 *
 * Le fichier produit contient un petit en-tête (stateTag + stateVersion),
 * suivi du contenu binaire produit par Core::ioState() (via StateArchive).
 * L'en-tête permet à checkState() de valider rapidement un fichier sans
 * devoir désérialiser tout l'état de l'émulateur.
 */
class SaveStates {
public:
    SaveStates(Core *core): core(core) {}

    void setPath(const String& path, RomType romType);

    StateResult checkState();
    bool saveState();
    bool loadState();

private:
    Core *core;
    RomType romType = ROM_NDS;
    String path;

    static const char *stateTag;
    static const uint32_t stateVersion;
};


} // namespace emulator_demo

#endif /* SAVE_STATE_H */
