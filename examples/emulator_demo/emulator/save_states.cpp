#include "emulator/save_states.hpp"

#include "emulator/state_archive.hpp"
#include "sdl3/iostream.hpp"

#include <cstring>
#include <vector>

namespace emulator_demo {

const char     *SaveStates::stateTag     = "NDSCSTAT";
const uint32_t  SaveStates::stateVersion = 1;

void SaveStates::setPath(const String& path, RomType romType) {
    this->path    = path;
    this->romType = romType;
}

StateResult SaveStates::checkState() {
    auto res = sdl3::IOStream::FromFile(path, "rb");
    if (!res)
        return STATE_FILE_FAIL;
    sdl3::IOStream file = std::move(res).Unwrap();

    size_t tagLen = strlen(stateTag);
    std::vector<char> tagBuf(tagLen);
    if (file.Read(tagBuf.data(), tagLen) != tagLen)
        return STATE_FILE_FAIL;
    if (memcmp(tagBuf.data(), stateTag, tagLen) != 0)
        return STATE_FORMAT_FAIL;

    uint32_t version = 0;
    if (file.Read(&version, sizeof(uint32_t)) != sizeof(uint32_t))
        return STATE_FILE_FAIL;

    if (version != stateVersion)
        return STATE_VERSION_FAIL;
    return STATE_SUCCESS;
}

bool SaveStates::saveState() {
    // Sérialise tout l'état de l'émulateur dans un buffer mémoire d'abord :
    // si quelque chose échoue ensuite à l'écriture, on n'a pas laissé de
    // fichier partiellement écrit.
    StateArchive archive(true);
    core->ioState(archive);

    auto res = sdl3::IOStream::FromFile(path, "wb");
    if (!res)
        return false;
    sdl3::IOStream file = std::move(res).Unwrap();

    size_t tagLen = strlen(stateTag);
    bool   ok     = (file.Write(stateTag, tagLen) == tagLen) &&
                  (file.Write(&stateVersion, sizeof(uint32_t)) == sizeof(uint32_t)) &&
                  (archive.buffer.empty() ||
                   file.Write(archive.buffer.data(), archive.buffer.size()) == archive.buffer.size());
    return ok;
}

bool SaveStates::loadState() {
    if (checkState() != STATE_SUCCESS)
        return false;

    auto res = sdl3::IOStream::FromFile(path, "rb");
    if (!res)
        return false;
    sdl3::IOStream file = std::move(res).Unwrap();

    size_t headerSize = strlen(stateTag) + sizeof(uint32_t);
    Sint64 total = file.GetSize();
    if (total < 0 || size_t(total) < headerSize)
        return false;
    file.Seek(Sint64(headerSize), SDL_IO_SEEK_SET);

    std::vector<uint8_t> data(size_t(total) - headerSize);
    if (!data.empty() && file.Read(data.data(), data.size()) != data.size())
        return false;

    // StateArchive(saving=false) consomme le buffer dans le même ordre exact
    // que celui utilisé par Core::ioState() lors de la sauvegarde. Un fichier
    // corrompu ou d'un format incompatible fait passer l'archive en échec
    // (lecture hors bornes) après avoir DÉJÀ écrasé une partie de l'état :
    // on prend donc un instantané de l'état courant avant, et on le
    // restaure si le chargement échoue, plutôt que de laisser tourner un
    // émulateur à moitié chargé.
    StateArchive backup(true);
    core->ioState(backup);

    StateArchive archive(std::move(data));
    core->ioState(archive);
    if (archive.failed) {
        LOG("SaveStates::loadState: état corrompu ou incompatible (%s), état précédent restauré\n", path.CStr());
        StateArchive restore(std::move(backup.buffer));
        core->ioState(restore);
        return false;
    }
    return true;
}

} // namespace emulator_demo
