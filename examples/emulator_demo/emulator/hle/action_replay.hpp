#ifndef ACTION_REPLAY_H
#define ACTION_REPLAY_H

#include <cstdint>
#include <mutex>
#include <vector>

#include "core/core.hpp"
#include "sdl3/iostream.hpp"

namespace emulator_demo {

class Core;

struct ARCheat {
    String           name;
    std::vector<uint32_t> code;
    bool                  enabled;
};

/**
 * @brief Action Replay DS cheat engine: loads/saves a `.cht` cheat file next
 * to the ROM and, once per frame (right before Core::endFrame(), see
 * Gpu::drawScanline case 262), interprets the enabled cheats' opcodes to
 * patch live memory. NDS-only (no GBA Action Replay format).
 */
class ActionReplay {
  public:
    std::vector<ARCheat> cheats;

    ActionReplay(Core *core) : core(core) {
    }

    void setPath(String path);

    /// Loads (replacing `cheats`) from `fromPath`, or from the path set via
    /// setPath() when `fromPath` is empty (the normal per-ROM `.cht` file).
    /// Used by the Cheats UI to import a cheat database file without losing
    /// track of where saveCheats() should persist afterwards.
    bool loadCheats(const String &fromPath = "");
    bool saveCheats();
    void applyCheats();

  private:
    Core       *core;
    std::mutex  mutex;
    String path;

    Result<sdl3::IOStream, StringView> openFile(const String &forPath, const char *mode);
};

} // namespace emulator_demo

#endif // ACTION_REPLAY_H
