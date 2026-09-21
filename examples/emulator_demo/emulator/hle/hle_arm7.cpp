#include "emulator/hle/hle_arm7.hpp"

#include "emulator/core.hpp"
#include "emulator/defines.hpp"

namespace emulator_demo {

void HleArm7::init() {
    // Permanently halt the ARM7 and set initial IPC state
    core->interpreter[1].halt(2);
    core->ipc.writeIpcSync(1, 0xFFFF, 0x700);
    core->ipc.writeIpcFifoCnt(1, 0xFFFF, 0x8000);
}

void HleArm7::ipcSync(uint8_t value) {
    // Catch unhandled HLE IPC sync requests
    if (inited) {
        LOG("Unhandled HLE IPC sync sent after initialization\n");
        return;
    }

    // During init, decrement the sync value and send it back
    if (value > 0) {
        core->ipc.writeIpcSync(1, 0xFFFF, uint16_t((value - 1) << 8));
        return;
    }

    // Set subsystem init flags and finish the init process
    core->memory.write<uint32_t>(1, 0x2FFFF8C, 0x3FFF0);
    inited = true;
}

void HleArm7::ipcFifo(uint32_t value) {
    // Handle FIFO commands based on the subsystem tag
    if (!inited)
        return;
    switch (value & 0x1F) { // Subsystem
        case 0x6: // Touch screen
            // Poll touch input manually or enable auto-polling
            if ((value & 0xC0000000) == 0xC0000000) {
                pollTouch(value | BIT(21));
            } else if ((value & 0xC0000000) == 0x40000000) {
                autoTouch = (value & BIT(22)) != 0;
                pollTouch(0xC0204006);
            }
            return;

        default:
            // Stub unknown FIFO commands by replying with the same value
            LOG_VERBOSE("Unknown HLE IPC FIFO command: 0x%X\n", value);
            core->ipc.writeIpcFifoSend(1, 0xFFFFFFFF, value);
            return;
    }
}

void HleArm7::runFrame() {
    // Automatically poll extra keys and touch if enabled
    if (!inited)
        return;
    core->memory.write<uint16_t>(1, 0x2FFFFA8, uint16_t((core->input.readExtKeyIn() & 0xB) << 10));
    if (autoTouch)
        pollTouch(0xC0240006);
}

void HleArm7::pollTouch(uint32_t value) {
    // Update touch values in shared memory and send a FIFO reply
    if (core->input.readExtKeyIn() & BIT(6)) { // Released
        core->memory.write<uint16_t>(1, 0x2FFFFAA, 0x000);
        core->memory.write<uint16_t>(1, 0x2FFFFAC, 0x600);
    } else { // Pressed
        core->memory.write<uint16_t>(1, 0x2FFFFAA, core->spi.readTouchX());
        core->memory.write<uint16_t>(1, 0x2FFFFAC, uint16_t((core->spi.readTouchY() >> 4) | 0x100));
    }
    core->ipc.writeIpcFifoSend(1, 0xFFFFFFFF, value);
}

} // namespace emulator_demo
