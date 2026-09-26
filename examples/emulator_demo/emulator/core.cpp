#include "settings.hpp"

#include <algorithm>
#include <cstring>
#include <thread>
#include "core.hpp"

namespace emulator_demo {

Core::Core(RomType type, String path, int id)
    : id(id), actionReplay(this), ndsBios9(this), ndsBios7(this), cartridgeNds(this), cartridgeGba(this), cp15(this),
      divSqrt(this), dldi(this), dma{Dma(this, 0), Dma(this, 1)}, gpu(this), gpu2D{Gpu2D(this, 0), Gpu2D(this, 1)}, gpu3D(this),
      gpu3DRenderer(this), hleArm7(this), input(this), interpreter{CPU(this, 0), CPU(this, 1)}, ipc(this), memory(this), rtc(this),
      spi(this), spu(this), timers{Timers(this, 0), Timers(this, 1)}, wifi(this) {
    if (!memory.loadNdsBios9() && (!Settings::getDirectBoot() || (path == "")))
        { bootError = ERROR_BIOS; return; }
    if (!memory.loadNdsBios7() && (!Settings::getDirectBoot() || (path == "")))
        { bootError = ERROR_BIOS; return; }
    if (!spi.loadFirmware() && (!Settings::getDirectBoot() || (path == "")))
        { bootError = ERROR_FIRM; return; }
    // Amont (NooDS) : une session GBA SEULE exige le BIOS GBA, boot direct ou
    // non — le cœur n'a pas de BIOS GBA en HLE (seulement ARM9/ARM7 NDS), et
    // le premier SWI ou la première interruption sauterait dans une zone
    // vide. La réécriture précédente de cette condition l'autorisait en boot
    // direct : la ROM « démarrait », puis l'émulation se figeait.
    const bool gbaBiosLoaded = memory.loadGbaBios();
    if (!gbaBiosLoaded && type == ROM_GBA && path != "")
        { bootError = ERROR_GBA_BIOS; return; }

    resetCyclesTask = std::bind(&Core::resetCycles, this);
    schedule(Task(&resetCyclesTask, 0x7FFFFFFF));
    gpu.scheduleInit();
    spu.scheduleInit();
    memory.updateMap9(0x00000000, 0xFFFFFFFF);
    memory.updateMap7(0x00000000, 0xFFFFFFFF);
    interpreter[0].init();
    interpreter[1].init();

    if (type == ROM_GBA && path != "") {
        if (!cartridgeGba.loadRom(path))
            { bootError = ERROR_ROM; return; }
        // Upstream NooDS also checks "no NDS ROM was given" here, since its
        // constructor can load an NDS ROM *and* a GBA slot-2 ROM together
        // (defaulting to NDS mode unless there's no NDS ROM at all). This
        // fork's constructor takes a single RomType + path, so type ==
        // ROM_GBA already means "GBA-only session" — that upstream check
        // has no equivalent here.
        if (Settings::getDirectBoot()) {
            memory.write<uint16_t>(0, 0x4000304, 0x8003);
            enterGbaMode();
        }
    }

    if (type == ROM_NDS && path != "") {
        if (!cartridgeNds.loadRom(path))
            { bootError = ERROR_ROM; return; }

        if (Settings::getDirectBoot()) {
            cp15.write(1, 0, 0, 0x0005707D);
            cp15.write(9, 1, 0, 0x0300000A);
            cp15.write(9, 1, 1, 0x00000020);
            memory.write<uint8_t>(0, 0x4000247, 0x03);
            memory.write<uint8_t>(0, 0x4000300, 0x01);
            memory.write<uint8_t>(1, 0x4000300, 0x01);
            memory.write<uint16_t>(0, 0x4000304, 0x0001);
            memory.write<uint16_t>(1, 0x4000504, 0x0200);
            memory.write<uint32_t>(0, 0x27FF800, 0x00001FC2);
            memory.write<uint32_t>(0, 0x27FF804, 0x00001FC2);
            memory.write<uint16_t>(0, 0x27FF850, 0x5835);
            memory.write<uint16_t>(0, 0x27FF880, 0x0007);
            memory.write<uint16_t>(0, 0x27FF884, 0x0006);
            memory.write<uint32_t>(0, 0x27FFC00, 0x00001FC2);
            memory.write<uint32_t>(0, 0x27FFC04, 0x00001FC2);
            memory.write<uint16_t>(0, 0x27FFC10, 0x5835);
            memory.write<uint16_t>(0, 0x27FFC40, 0x0001);
            cartridgeNds.directBoot();
            interpreter[0].directBoot();
            interpreter[1].directBoot();
            spi.directBoot();
        }
    }

    // Opt-in: fully HLE the ARM7 (halted, IPC protocol stubbed out) instead
    // of interpreting real ARM7 code — lets an NDS session boot without a
    // firmware/ARM7 BIOS dump. No effect in GBA mode.
    if (!gbaMode && Settings::getArm7Hle()) {
        arm7Hle = true;
        hleArm7.init();
    }

    running.store(true);
}


void Core::runFrame() {
    (this->*runFunc)();
}


void Core::resetCycles() {
    for (unsigned int i = 0; i < tasks.size(); i++)
        tasks[i].cycles -= globalCycles;

    arm9Cycles -= std::min(globalCycles, arm9Cycles);
    arm7Cycles -= std::min(globalCycles, arm7Cycles);
    timers[0].resetCycles();
    timers[1].resetCycles();
    globalCycles -= globalCycles;
    schedule(Task(&resetCyclesTask, 0x7FFFFFFF));
}

void Core::runGbaFrame() {
    while (running.exchange(true)) {
        if (arm7Cycles > globalCycles)
            globalCycles = arm7Cycles;

        while (interpreter[1].shouldRun() && tasks[0].cycles > arm7Cycles)
            arm7Cycles = (globalCycles += interpreter[1].runOpcode());

        globalCycles = tasks[0].cycles;
        while (tasks[0].cycles <= globalCycles) {
            (*tasks[0].task)();
            tasks.erase(tasks.begin());
        }
    }
}

void Core::runNdsFrame() {
    while (running.exchange(true)) {
        while (tasks[0].cycles > globalCycles) {
            if (interpreter[0].shouldRun() && globalCycles >= arm9Cycles)
                arm9Cycles = globalCycles + interpreter[0].runOpcode();

            if (interpreter[1].shouldRun() && globalCycles >= arm7Cycles)
                arm7Cycles = globalCycles + (interpreter[1].runOpcode() << 1);

            globalCycles = std::min<uint32_t>((interpreter[0].shouldRun() ? arm9Cycles : -1),
                                              (interpreter[1].shouldRun() ? arm7Cycles : -1));
        }

        globalCycles = tasks[0].cycles;
        while (tasks[0].cycles <= globalCycles) {
            (*tasks[0].task)();
            tasks.erase(tasks.begin());
        }
    }
}

void Core::schedule(Task task) {
    task.cycles += globalCycles;
    auto it = std::upper_bound(tasks.cbegin(), tasks.cend(), task);
    tasks.insert(it, task);
}

void Core::enterGbaMode() {
    gbaMode = true;
    runFunc = &Core::runGbaFrame;
    running.store(false);
    tasks.clear();

    schedule(Task(&resetCyclesTask, 1));
    gpu.gbaScheduleInit();
    spu.gbaScheduleInit();

    memory.updateMap7(0x00000000, 0xFFFFFFFF);
    interpreter[1].init();
    interpreter[1].setBios(nullptr);
    rtc.reset();
    memory.write<uint8_t>(0, 0x4000240, 0x80);
    memory.write<uint8_t>(0, 0x4000241, 0x80);
}

void Core::endFrame() {
    running.store(false);
    fpsCount++;

    if (arm7Hle)
        hleArm7.runFrame();
    std::chrono::duration<double> fpsTime = std::chrono::steady_clock::now() - lastFpsTime;
    if (fpsTime.count() >= 1.0f) {
        fps = fpsCount;
        fpsCount = 0;
        lastFpsTime = std::chrono::steady_clock::now();
    }

    if (wifi.shouldSchedule())
        wifi.scheduleInit();
}

void Core::ioState(StateArchive &archive) {
    // Marqueur interne à l'archive (en plus de l'en-tête de fichier géré par
    // SaveStates) : identifie le mode au moment de la sauvegarde et permet un
    // contrôle de cohérence supplémentaire au chargement.
    char tag[4];
    if (archive.saving)
        std::memcpy(tag, gbaMode ? "GBAS" : "NDSS", 4);
    archive.io(tag);
    uint32_t version = 1;
    archive.io(version);
    if (!archive.saving && (version != 1 || (std::memcmp(tag, "NDSS", 4) != 0 && std::memcmp(tag, "GBAS", 4) != 0)))
        LOG("Unexpected save state marker/version inside archive (0x%.4s, v%u)\n", tag, version);

    archive.io(globalCycles);
    archive.io(arm9Cycles);
    archive.io(arm7Cycles);
    archive.io(gbaMode);

    ndsBios9.ioState(archive);
    ndsBios7.ioState(archive);
    // Les deux cartouches sont toujours sérialisées, même si une seule est
    // pertinente selon le mode courant : cela garde une disposition de
    // l'archive fixe, indépendante de gbaMode.
    cartridgeNds.ioState(archive);
    cartridgeGba.ioState(archive);
    cp15.ioState(archive);
    divSqrt.ioState(archive);
    dma[0].ioState(archive);
    dma[1].ioState(archive);
    gpu.ioState(archive);
    gpu2D[0].ioState(archive);
    gpu2D[1].ioState(archive);
    gpu3D.ioState(archive);
    gpu3DRenderer.ioState(archive);
    hleArm7.ioState(archive);
    input.ioState(archive);
    interpreter[0].ioState(archive);
    interpreter[1].ioState(archive);
    ipc.ioState(archive);
    memory.ioState(archive);
    rtc.ioState(archive);
    spi.ioState(archive);
    spu.ioState(archive);
    timers[0].ioState(archive);
    timers[1].ioState(archive);
    wifi.ioState(archive);

    if (archive.saving)
        return;

    // Tout ce qui suit ne concerne qu'un chargement : reconstruction de ce
    // qui n'est pas (et ne peut pas être) sérialisé directement.
    runFunc = gbaMode ? &Core::runGbaFrame : &Core::runNdsFrame;

    // Le scheduler n'est jamais sérialisé (il contient des pointeurs vers des
    // std::function internes à chaque composant). On repart d'une file vide
    // et on ré-arme uniquement ce qui doit l'être, exactement comme le fait
    // le constructeur pour un démarrage à froid.
    tasks.clear();
    schedule(Task(&resetCyclesTask, 0x7FFFFFFF));

    // Reconstruit les caches de pointeurs mémoire/VRAM à partir des registres
    // bruts qui viennent d'être restaurés.
    memory.updateMap9(0x00000000, 0xFFFFFFFF);
    memory.updateMap7(0x00000000, 0xFFFFFFFF);
    memory.rebuildVram();

    if (gbaMode) {
        gpu.gbaScheduleInit();
        spu.gbaScheduleInit();
    } else {
        gpu.scheduleInit();
        spu.scheduleInit();
    }

    dma[0].rearm();
    dma[1].rearm();
    timers[0].rearm();
    timers[1].rearm();

    wifi.resetSchedule();
    if (wifi.shouldSchedule())
        wifi.scheduleInit();
}

FORCE_INLINE int CPU::runOpcode() {
    uint32_t opcode = pipeline[0];
    pipeline[0] = pipeline[1];
    if (cpsr & BIT(5)) {
        pipeline[1] = core->memory.read<uint16_t>(cpu, *registers[15] += 2);
        return (this->*thumbInstrs[(opcode >> 6) & 0x3FF])(opcode);
    }
    else {
        pipeline[1] = core->memory.read<uint32_t>(cpu, *registers[15] += 4);
        switch (condition[((opcode >> 24) & 0xF0) | (cpsr >> 28)]) {
        case 0:
            return 1;
        case 2:
            return handleReserved(opcode);
        default:
            return (this->*armInstrs[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)])(opcode);
        }
    }
}

} // namespace emulator_demo

