#include "emulator/cpu/cpu.hpp"
#include "emulator/core.hpp"

namespace emulator_demo {


int CPU::bx(uint32_t opcode) {
    uint32_t op0 = *registers[opcode & 0x0000000F];
    if (op0 & BIT(0))
        cpsr |= BIT(5);
    *registers[15] = op0;
    flushPipeline();
    return 3;
}

int CPU::blxReg(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t op0 = *registers[opcode & 0x0000000F];
    if (op0 & BIT(0))
        cpsr |= BIT(5);
    *registers[14] = *registers[15] - 4;
    *registers[15] = op0;
    flushPipeline();
    return 3;
}

int CPU::b(uint32_t opcode) {
    uint32_t op0 = ((opcode & BIT(23)) ? 0xFC000000 : 0) | ((opcode & 0x00FFFFFF) << 2);
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int CPU::bl(uint32_t opcode) {
    uint32_t op0   = ((opcode & BIT(23)) ? 0xFC000000 : 0) | ((opcode & 0x00FFFFFF) << 2);
    *registers[14] = *registers[15] - 4;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int CPU::blx(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t op0 = ((opcode & BIT(23)) ? 0xFC000000 : 0) | ((opcode & 0x00FFFFFF) << 2) | ((opcode & BIT(24)) >> 23);
    cpsr |= BIT(5);
    *registers[14] = *registers[15] - 4;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int CPU::swi(uint32_t opcode) {
    *registers[15] -= 4;
    return exception(0x08);
}

int CPU::bxRegT(uint16_t opcode) {
    uint32_t op0 = *registers[(opcode & 0x0078) >> 3];
    if (!(op0 & BIT(0)))
        cpsr &= ~BIT(5);
    *registers[15] = op0;
    flushPipeline();
    return 3;
}

int CPU::blxRegT(uint16_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t op0 = *registers[(opcode & 0x0078) >> 3];
    if (!(op0 & BIT(0)))
        cpsr &= ~BIT(5);
    *registers[14] = *registers[15] - 1;
    *registers[15] = op0;
    flushPipeline();
    return 3;
}

int CPU::beqT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (cpsr & BIT(30)) {
        *registers[15] += op0;
        flushPipeline();
        return 3;
    }
    return 1;
}

int CPU::bneT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (!(cpsr & BIT(30))) {
        *registers[15] += op0;
        flushPipeline();
        return 3;
    }
    return 1;
}

int CPU::bcsT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (cpsr & BIT(29)) {
        *registers[15] += op0;
        flushPipeline();
        return 3;
    }
    return 1;
}

int CPU::bccT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (!(cpsr & BIT(29))) {
        *registers[15] += op0;
        flushPipeline();
        return 3;
    }
    return 1;
}

int CPU::bmiT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (cpsr & BIT(31)) {
        *registers[15] += op0;
        flushPipeline();
        return 3;
    }
    return 1;
}

int CPU::bplT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (!(cpsr & BIT(31))) {
        *registers[15] += op0;
        flushPipeline();
        return 3;
    }
    return 1;
}

int CPU::bvsT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (cpsr & BIT(28)) {
        *registers[15] += op0;
        flushPipeline();
        return 3;
    }
    return 1;
}

int CPU::bvcT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (!(cpsr & BIT(28))) {
        *registers[15] += op0;
        flushPipeline();
        return 3;
    }
    return 1;
}

int CPU::bhiT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if ((cpsr & BIT(29)) && !(cpsr & BIT(30))) {
        *registers[15] += op0;
        flushPipeline();
        return 3;
    }
    return 1;
}

int CPU::blsT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (!(cpsr & BIT(29)) || (cpsr & BIT(30))) {
        *registers[15] += op0;
        flushPipeline();
        return 3;
    }
    return 1;
}

int CPU::bgeT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if ((cpsr & BIT(31)) == (cpsr & BIT(28)) << 3) {
        *registers[15] += op0;
        flushPipeline();
        return 3;
    }
    return 1;
}

int CPU::bltT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if ((cpsr & BIT(31)) != (cpsr & BIT(28)) << 3) {
        *registers[15] += op0;
        flushPipeline();
        return 3;
    }
    return 1;
}

int CPU::bgtT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (!(cpsr & BIT(30)) && (cpsr & BIT(31)) == (cpsr & BIT(28)) << 3) {
        *registers[15] += op0;
        flushPipeline();
        return 3;
    }
    return 1;
}

int CPU::bleT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if ((cpsr & BIT(30)) || (cpsr & BIT(31)) != (cpsr & BIT(28)) << 3) {
        *registers[15] += op0;
        flushPipeline();
        return 3;
    }
    return 1;
}

int CPU::bT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(10)) ? 0xFFFFF000 : 0) | ((opcode & 0x07FF) << 1);
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int CPU::blSetupT(uint16_t opcode) {
    uint32_t op0   = ((opcode & BIT(10)) ? 0xFFFFF000 : 0) | ((opcode & 0x07FF) << 1);
    *registers[14] = *registers[15] + (op0 << 11);
    return 1;
}

int CPU::blOffT(uint16_t opcode) {
    uint32_t op0   = (opcode & 0x07FF) << 1;
    uint32_t ret   = *registers[15] - 1;
    *registers[15] = *registers[14] + op0;
    *registers[14] = ret;
    flushPipeline();
    return 3;
}

int CPU::blxOffT(uint16_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t op0 = (opcode & 0x07FF) << 1;
    cpsr &= ~BIT(5);
    uint32_t ret   = *registers[15] - 1;
    *registers[15] = *registers[14] + op0;
    *registers[14] = ret;
    flushPipeline();
    return 3;
}

int CPU::swiT(uint16_t opcode) {
    *registers[15] -= 4;
    return exception(0x08);
}

} // namespace emulator_demo

