
#ifndef CORE_H
#define CORE_H

#include <chrono>
#include <map>
#include <cstdint>
#include <functional>
#include <vector>
#include <memory>
#include "core/core.hpp"
#include "cpu/cp15.hpp"
#include "cpu/cpu.hpp"
#include "cpu/timers.hpp"
#include "defines.hpp"
#include "gpu/gpu.hpp"
#include "gpu/gpu_2d.hpp"
#include "gpu/gpu_3d.hpp"
#include "gpu/gpu_3d_renderer.hpp"
#include "hle/action_replay.hpp"
#include "hle/bios.hpp"
#include "hle/dldi.hpp"
#include "hle/hle_arm7.hpp"
#include "io/div_sqrt.hpp"
#include "io/input.hpp"
#include "io/ipc.hpp"
#include "io/rtc.hpp"
#include "io/spi.hpp"
#include "io/spu.hpp"
#include "io/wifi.hpp"
#include "memory/cartridge.hpp"
#include "memory/dma.hpp"
#include "memory/memory.hpp"
#include "state_archive.hpp"

namespace emulator_demo {

enum RomType {
    ROM_NDS,
    ROM_GBA
};

enum CoreError {
    ERROR_NONE = 0,
    ERROR_BIOS,
    ERROR_GBA_BIOS,
    ERROR_FIRM,
    ERROR_ROM
};

struct Task {
    Task(std::function<void()> *task, uint32_t cycles) : task(task), cycles(cycles) {
    }
    std::function<void()> *task;
    uint32_t cycles;
    bool operator<(const Task &task) const {
        return cycles < task.cycles;
    }
};


class Core {
public:
    /// Construit et démarre un cœur. Préférer create() : le constructeur ne
    /// peut pas échouer autrement qu'en renseignant getBootError() (ce dépôt
    /// n'utilise pas d'exception — l'original levait un CoreError).
    Core(RomType type = ROM_NDS, String path = "", int id = 0);

    /// Démarre un cœur sur le tas (ses composants gardent un pointeur vers
    /// lui : son adresse doit rester stable). Err si BIOS/firmware/ROM
    /// illisibles.
    [[nodiscard]] static Result<std::unique_ptr<Core>, CoreError> create(RomType type, const String &path, int id = 0) {
        auto core = std::make_unique<Core>(type, path, id);
        if (core->bootError != ERROR_NONE)
            return Err(core->bootError);
        return Ok(std::move(core));
    }

    [[nodiscard]] static const char *errorText(CoreError error) {
        switch (error) {
        case ERROR_NONE: return "aucune erreur";
        case ERROR_BIOS: return "BIOS NDS introuvable ou illisible (boot direct ou --arm7-hle pour s'en passer)";
        case ERROR_GBA_BIOS: return "BIOS GBA (gbaBios.bin) requis : le cœur n'émule pas le BIOS GBA (voir --bios-dir)";
        case ERROR_FIRM: return "firmware introuvable ou illisible";
        case ERROR_ROM: return "ROM introuvable ou illisible";
        }
        return "erreur inconnue";
    }

    [[nodiscard]] CoreError getBootError() const {
        return bootError;
    }

    void runFrame();

    bool isGbaMode() {
        return gbaMode;
    }

    int getId() {
        return id;
    }

    int getFps() {
        return fps;
    }

    uint32_t getGlobalCycles() {
        return globalCycles;
    }

    void schedule(Task task);
    void enterGbaMode();
    void endFrame();

    /**
     * @brief Sérialise/désérialise l'état complet de l'émulateur (save state).
     *
     * Orchestre l'appel à ioState() de chaque composant, dans l'ordre de
     * déclaration des membres, puis (lors d'un chargement uniquement)
     * reconstruit tout ce qui n'est pas sérialisable directement : caches de
     * pointeurs mémoire/VRAM, threads de rendu/audio, et tâches du
     * scheduler (Core::tasks n'est jamais sérialisé : il contient des
     * pointeurs vers des std::function internes, qui n'ont de sens que dans
     * le processus courant).
     * @param archive Archive de sauvegarde/chargement.
     */
    void ioState(StateArchive &archive);

    /// True once init() below has actually taken effect (NDS mode + opted in
    /// via Settings::getArm7Hle() at construction time) — checked by Ipc to
    /// redirect ARM9-side IPCSYNC/IPCFIFO writes to hleArm7 instead of the
    /// (halted, never-running) real ARM7.
    bool arm7Hle = false;

    /// Always false: this fork does not implement DSi mode. Kept as a real
    /// member (rather than removed) so DSi-only peripherals ported from
    /// upstream (e.g. Aes) can gate their behavior the same way upstream
    /// does, and compile/serialize correctly, without pretending DSi
    /// hardware is actually emulated.
    bool dsiMode = false;

    ActionReplay actionReplay;
    NdsBios9 ndsBios9;
    NdsBios7 ndsBios7;
    CartridgeNds cartridgeNds;
    CartridgeGba cartridgeGba;
    Cp15 cp15;
    DivSqrt divSqrt;
    Dldi dldi;
    Dma dma[2];
    Gpu gpu;
    Gpu2D gpu2D[2];
    Gpu3D gpu3D;
    Gpu3DRenderer gpu3DRenderer;
    HleArm7 hleArm7;
    Input input;
    CPU interpreter[2];
    Ipc ipc;
    Memory memory;
    Rtc rtc;
    Spi spi;
    Spu spu;
    Timers timers[2];
    Wifi wifi;

private:
    void (Core::*runFunc)(void) = &Core::runNdsFrame;
    bool gbaMode = false;
    CoreError bootError = ERROR_NONE;
    int id = 0;
    std::vector<Task> tasks;
    uint32_t globalCycles = 0;
    uint32_t arm9Cycles = 0, arm7Cycles = 0;
    std::atomic<bool> running;
    int fps = 0, fpsCount = 0;

    std::chrono::steady_clock::time_point lastFpsTime;
    std::function<void()> resetCyclesTask;
    void resetCycles();
    void runNdsFrame();
    void runGbaFrame();
};

} // namespace emulator_demo

#endif
