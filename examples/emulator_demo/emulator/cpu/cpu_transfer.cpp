#include "emulator/cpu/cpu.hpp"
#include "emulator/core.hpp"

namespace emulator_demo {



FORCE_INLINE uint32_t CPU::ip(uint32_t opcode) {
    return opcode & 0x00000FFF;
}

FORCE_INLINE uint32_t CPU::ipH(uint32_t opcode) {
    return ((opcode & 0x00000F00) >> 4) | (opcode & 0x0000000F);
}

FORCE_INLINE uint32_t CPU::rp(uint32_t opcode) {
    return *registers[opcode & 0x0000000F];
}

FORCE_INLINE uint32_t CPU::rpll(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    return value << shift;
}

FORCE_INLINE uint32_t CPU::rplr(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    return shift ? (value >> shift) : 0;
}

FORCE_INLINE uint32_t CPU::rpar(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    return shift ? ((int32_t)value >> shift) : ((value & BIT(31)) ? 0xFFFFFFFF : 0);
}

FORCE_INLINE uint32_t CPU::rprr(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    return shift ? ((value << (32 - shift)) | (value >> shift)) : (((cpsr & BIT(29)) << 2) | (value >> 1));
}

FORCE_INLINE int CPU::ldrsbOf(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16];
    *op0          = (int8_t)core->memory.read<uint8_t>(cpu, op1 + op2);
    if (op0 != registers[15])
        return ((cpu == 0) ? 1 : 3);
    flushPipeline();
    return 5;
}

FORCE_INLINE int CPU::ldrshOf(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16];
    *op0          = (int16_t)core->memory.read<uint16_t>(cpu, op1 += op2);
    if (cpu == 1 && (op1 & 1))
        *op0 = (int16_t)*op0 >> 8;
    if (op0 != registers[15])
        return ((cpu == 0) ? 1 : 3);
    flushPipeline();
    return 5;
}

FORCE_INLINE int CPU::ldrbOf(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16];
    *op0          = core->memory.read<uint8_t>(cpu, op1 + op2);
    if (op0 != registers[15])
        return ((cpu == 0) ? 1 : 3);
    if (cpu == 0 && (*op0 & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return 5;
}

FORCE_INLINE int CPU::strbOf(uint32_t opcode, uint32_t op2) {
    uint32_t op0 = *registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t op1 = *registers[(opcode & 0x000F0000) >> 16];
    core->memory.write<uint8_t>(cpu, op1 + op2, op0);
    return ((cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::ldrhOf(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16];
    *op0          = core->memory.read<uint16_t>(cpu, op1 += op2);
    if (cpu == 1 && (op1 & 1))
        *op0 = (*op0 << 24) | (*op0 >> 8);
    if (op0 != registers[15])
        return ((cpu == 0) ? 1 : 3);
    flushPipeline();
    return 5;
}

FORCE_INLINE int CPU::strhOf(uint32_t opcode, uint32_t op2) {
    uint32_t op0 = *registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t op1 = *registers[(opcode & 0x000F0000) >> 16];
    core->memory.write<uint16_t>(cpu, op1 + op2, op0);
    return ((cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::ldrOf(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16];
    *op0          = core->memory.read<uint32_t>(cpu, op1 += op2);
    if (op1 & 3) {
        int shift = (op1 & 3) * 8;
        *op0      = (*op0 << (32 - shift)) | (*op0 >> shift);
    }
    if (op0 != registers[15])
        return ((cpu == 0) ? 1 : 3);
    if (cpu == 0 && (*op0 & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return 5;
}

FORCE_INLINE int CPU::strOf(uint32_t opcode, uint32_t op2) {
    uint32_t op0 = *registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t op1 = *registers[(opcode & 0x000F0000) >> 16];
    core->memory.write<uint32_t>(cpu, op1 + op2, op0);
    return ((cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::ldrdOf(uint32_t opcode, uint32_t op2) {
    if (cpu == 1)
        return 1;
    uint8_t op0 = (opcode & 0x0000F000) >> 12;
    if (op0 == 15)
        return 1;
    uint32_t op1        = *registers[(opcode & 0x000F0000) >> 16];
    *registers[op0]     = core->memory.read<uint32_t>(cpu, op1 + op2);
    *registers[op0 + 1] = core->memory.read<uint32_t>(cpu, op1 + op2 + 4);
    return 2;
}

FORCE_INLINE int CPU::strdOf(uint32_t opcode, uint32_t op2) {
    if (cpu == 1)
        return 1;
    uint8_t op0 = (opcode & 0x0000F000) >> 12;
    if (op0 == 15)
        return 1;
    uint32_t op1 = *registers[(opcode & 0x000F0000) >> 16];
    core->memory.write<uint32_t>(cpu, op1 + op2, *registers[op0]);
    core->memory.write<uint32_t>(cpu, op1 + op2 + 4, *registers[op0 + 1]);
    return 2;
}

FORCE_INLINE int CPU::ldrsbPr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    *op1 += op2;
    *op0 = (int8_t)core->memory.read<uint8_t>(cpu, *op1);
    if (op0 != registers[15])
        return ((cpu == 0) ? 1 : 3);
    flushPipeline();
    return 5;
}

FORCE_INLINE int CPU::ldrshPr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    uint32_t  address;
    *op1 += op2;
    *op0 = (int16_t)core->memory.read<uint16_t>(cpu, address = *op1);
    if (cpu == 1 && (address & 1))
        *op0 = (int16_t)*op0 >> 8;
    if (op0 != registers[15])
        return ((cpu == 0) ? 1 : 3);
    flushPipeline();
    return 5;
}

FORCE_INLINE int CPU::ldrbPr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    *op1 += op2;
    *op0 = core->memory.read<uint8_t>(cpu, *op1);
    if (op0 != registers[15])
        return ((cpu == 0) ? 1 : 3);
    if (cpu == 0 && (*op0 & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return 5;
}

FORCE_INLINE int CPU::strbPr(uint32_t opcode, uint32_t op2) {
    uint32_t  op0 = *registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    *op1 += op2;
    core->memory.write<uint8_t>(cpu, *op1, op0);
    return ((cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::ldrhPr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    uint32_t  address;
    *op1 += op2;
    *op0 = core->memory.read<uint16_t>(cpu, address = *op1);
    if (cpu == 1 && (address & 1))
        *op0 = (*op0 << 24) | (*op0 >> 8);
    if (op0 != registers[15])
        return ((cpu == 0) ? 1 : 3);
    flushPipeline();
    return 5;
}

FORCE_INLINE int CPU::strhPr(uint32_t opcode, uint32_t op2) {
    uint32_t  op0 = *registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    *op1 += op2;
    core->memory.write<uint16_t>(cpu, *op1, op0);
    return ((cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::ldrPr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    uint32_t  address;
    *op1 += op2;
    *op0 = core->memory.read<uint32_t>(cpu, address = *op1);
    if (address & 3) {
        int shift = (address & 3) * 8;
        *op0      = (*op0 << (32 - shift)) | (*op0 >> shift);
    }
    if (op0 != registers[15])
        return ((cpu == 0) ? 1 : 3);
    if (cpu == 0 && (*op0 & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return 5;
}

FORCE_INLINE int CPU::strPr(uint32_t opcode, uint32_t op2) {
    uint32_t  op0 = *registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    *op1 += op2;
    core->memory.write<uint32_t>(cpu, *op1, op0);
    return ((cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::ldrdPr(uint32_t opcode, uint32_t op2) {
    if (cpu == 1)
        return 1;
    uint8_t op0 = (opcode & 0x0000F000) >> 12;
    if (op0 == 15)
        return 1;
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    *op1 += op2;
    *registers[op0]     = core->memory.read<uint32_t>(cpu, *op1);
    *registers[op0 + 1] = core->memory.read<uint32_t>(cpu, *op1 + 4);
    return 2;
}

FORCE_INLINE int CPU::strdPr(uint32_t opcode, uint32_t op2) {
    if (cpu == 1)
        return 1;
    uint8_t op0 = (opcode & 0x0000F000) >> 12;
    if (op0 == 15)
        return 1;
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    *op1 += op2;
    core->memory.write<uint32_t>(cpu, *op1, *registers[op0]);
    core->memory.write<uint32_t>(cpu, *op1 + 4, *registers[op0 + 1]);
    return 2;
}

FORCE_INLINE int CPU::ldrsbPt(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    *op0          = (int8_t)core->memory.read<uint8_t>(cpu, *op1);
    if (op0 != op1)
        *op1 += op2;
    if (op0 != registers[15])
        return ((cpu == 0) ? 1 : 3);
    flushPipeline();
    return 5;
}

FORCE_INLINE int CPU::ldrshPt(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    uint32_t  address;
    *op0 = (int16_t)core->memory.read<uint16_t>(cpu, address = *op1);
    if (op0 != op1)
        *op1 += op2;
    if (cpu == 1 && (address & 1))
        *op0 = (int16_t)*op0 >> 8;
    if (op0 != registers[15])
        return ((cpu == 0) ? 1 : 3);
    flushPipeline();
    return 5;
}

FORCE_INLINE int CPU::ldrbPt(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    *op0          = core->memory.read<uint8_t>(cpu, *op1);
    if (op0 != op1)
        *op1 += op2;
    if (op0 != registers[15])
        return ((cpu == 0) ? 1 : 3);
    if (cpu == 0 && (*op0 & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return 5;
}

FORCE_INLINE int CPU::strbPt(uint32_t opcode, uint32_t op2) {
    uint32_t  op0 = *registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    core->memory.write<uint8_t>(cpu, *op1, op0);
    *op1 += op2;
    return ((cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::ldrhPt(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    uint32_t  address;
    *op0 = core->memory.read<uint16_t>(cpu, address = *op1);
    *op1 += op2;
    if (cpu == 1 && (address & 1))
        *op0 = (*op0 << 24) | (*op0 >> 8);
    if (op0 != registers[15])
        return ((cpu == 0) ? 1 : 3);
    flushPipeline();
    return 5;
}

FORCE_INLINE int CPU::strhPt(uint32_t opcode, uint32_t op2) {
    uint32_t  op0 = *registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    core->memory.write<uint16_t>(cpu, *op1, op0);
    *op1 += op2;
    return ((cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::ldrPt(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    uint32_t  address;
    *op0 = core->memory.read<uint32_t>(cpu, address = *op1);
    if (address & 3) {
        int shift = (address & 3) * 8;
        *op0      = (*op0 << (32 - shift)) | (*op0 >> shift);
    }
    if (op0 != op1)
        *op1 += op2;
    if (op0 != registers[15])
        return ((cpu == 0) ? 1 : 3);
    if (cpu == 0 && (*op0 & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return 5;
}

FORCE_INLINE int CPU::strPt(uint32_t opcode, uint32_t op2) {
    uint32_t  op0 = *registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    core->memory.write<uint32_t>(cpu, *op1, op0);
    *op1 += op2;
    return ((cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::ldrdPt(uint32_t opcode, uint32_t op2) {
    if (cpu == 1)
        return 1;
    uint8_t op0 = (opcode & 0x0000F000) >> 12;
    if (op0 == 15)
        return 1;
    uint32_t *op1       = registers[(opcode & 0x000F0000) >> 16];
    *registers[op0]     = core->memory.read<uint32_t>(cpu, *op1);
    *registers[op0 + 1] = core->memory.read<uint32_t>(cpu, *op1 + 4);
    *op1 += op2;
    return 2;
}

FORCE_INLINE int CPU::strdPt(uint32_t opcode, uint32_t op2) {
    if (cpu == 1)
        return 1;
    uint8_t op0 = (opcode & 0x0000F000) >> 12;
    if (op0 == 15)
        return 1;
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    core->memory.write<uint32_t>(cpu, *op1, *registers[op0]);
    core->memory.write<uint32_t>(cpu, *op1 + 4, *registers[op0 + 1]);
    *op1 += op2;
    return 2;
}

int CPU::ldrsbOfrm(uint32_t opcode) {
    return ldrsbOf(opcode, -rp(opcode));
}

int CPU::ldrsbOfim(uint32_t opcode) {
    return ldrsbOf(opcode, -ipH(opcode));
}

int CPU::ldrsbOfrp(uint32_t opcode) {
    return ldrsbOf(opcode, rp(opcode));
}

int CPU::ldrsbOfip(uint32_t opcode) {
    return ldrsbOf(opcode, ipH(opcode));
}

int CPU::ldrsbPrrm(uint32_t opcode) {
    return ldrsbPr(opcode, -rp(opcode));
}

int CPU::ldrsbPrim(uint32_t opcode) {
    return ldrsbPr(opcode, -ipH(opcode));
}

int CPU::ldrsbPrrp(uint32_t opcode) {
    return ldrsbPr(opcode, rp(opcode));
}

int CPU::ldrsbPrip(uint32_t opcode) {
    return ldrsbPr(opcode, ipH(opcode));
}

int CPU::ldrsbPtrm(uint32_t opcode) {
    return ldrsbPt(opcode, -rp(opcode));
}

int CPU::ldrsbPtim(uint32_t opcode) {
    return ldrsbPt(opcode, -ipH(opcode));
}

int CPU::ldrsbPtrp(uint32_t opcode) {
    return ldrsbPt(opcode, rp(opcode));
}

int CPU::ldrsbPtip(uint32_t opcode) {
    return ldrsbPt(opcode, ipH(opcode));
}

int CPU::ldrshOfrm(uint32_t opcode) {
    return ldrshOf(opcode, -rp(opcode));
}

int CPU::ldrshOfim(uint32_t opcode) {
    return ldrshOf(opcode, -ipH(opcode));
}

int CPU::ldrshOfrp(uint32_t opcode) {
    return ldrshOf(opcode, rp(opcode));
}

int CPU::ldrshOfip(uint32_t opcode) {
    return ldrshOf(opcode, ipH(opcode));
}

int CPU::ldrshPrrm(uint32_t opcode) {
    return ldrshPr(opcode, -rp(opcode));
}

int CPU::ldrshPrim(uint32_t opcode) {
    return ldrshPr(opcode, -ipH(opcode));
}

int CPU::ldrshPrrp(uint32_t opcode) {
    return ldrshPr(opcode, rp(opcode));
}

int CPU::ldrshPrip(uint32_t opcode) {
    return ldrshPr(opcode, ipH(opcode));
}

int CPU::ldrshPtrm(uint32_t opcode) {
    return ldrshPt(opcode, -rp(opcode));
}

int CPU::ldrshPtim(uint32_t opcode) {
    return ldrshPt(opcode, -ipH(opcode));
}

int CPU::ldrshPtrp(uint32_t opcode) {
    return ldrshPt(opcode, rp(opcode));
}

int CPU::ldrshPtip(uint32_t opcode) {
    return ldrshPt(opcode, ipH(opcode));
}

int CPU::ldrbOfim(uint32_t opcode) {
    return ldrbOf(opcode, -ip(opcode));
}

int CPU::ldrbOfip(uint32_t opcode) {
    return ldrbOf(opcode, ip(opcode));
}

int CPU::ldrbOfrmll(uint32_t opcode) {
    return ldrbOf(opcode, -rpll(opcode));
}

int CPU::ldrbOfrmlr(uint32_t opcode) {
    return ldrbOf(opcode, -rplr(opcode));
}

int CPU::ldrbOfrmar(uint32_t opcode) {
    return ldrbOf(opcode, -rpar(opcode));
}

int CPU::ldrbOfrmrr(uint32_t opcode) {
    return ldrbOf(opcode, -rprr(opcode));
}

int CPU::ldrbOfrpll(uint32_t opcode) {
    return ldrbOf(opcode, rpll(opcode));
}

int CPU::ldrbOfrplr(uint32_t opcode) {
    return ldrbOf(opcode, rplr(opcode));
}

int CPU::ldrbOfrpar(uint32_t opcode) {
    return ldrbOf(opcode, rpar(opcode));
}

int CPU::ldrbOfrprr(uint32_t opcode) {
    return ldrbOf(opcode, rprr(opcode));
}

int CPU::ldrbPrim(uint32_t opcode) {
    return ldrbPr(opcode, -ip(opcode));
}

int CPU::ldrbPrip(uint32_t opcode) {
    return ldrbPr(opcode, ip(opcode));
}

int CPU::ldrbPrrmll(uint32_t opcode) {
    return ldrbPr(opcode, -rpll(opcode));
}

int CPU::ldrbPrrmlr(uint32_t opcode) {
    return ldrbPr(opcode, -rplr(opcode));
}

int CPU::ldrbPrrmar(uint32_t opcode) {
    return ldrbPr(opcode, -rpar(opcode));
}

int CPU::ldrbPrrmrr(uint32_t opcode) {
    return ldrbPr(opcode, -rprr(opcode));
}

int CPU::ldrbPrrpll(uint32_t opcode) {
    return ldrbPr(opcode, rpll(opcode));
}

int CPU::ldrbPrrplr(uint32_t opcode) {
    return ldrbPr(opcode, rplr(opcode));
}

int CPU::ldrbPrrpar(uint32_t opcode) {
    return ldrbPr(opcode, rpar(opcode));
}

int CPU::ldrbPrrprr(uint32_t opcode) {
    return ldrbPr(opcode, rprr(opcode));
}

int CPU::ldrbPtim(uint32_t opcode) {
    return ldrbPt(opcode, -ip(opcode));
}

int CPU::ldrbPtip(uint32_t opcode) {
    return ldrbPt(opcode, ip(opcode));
}

int CPU::ldrbPtrmll(uint32_t opcode) {
    return ldrbPt(opcode, -rpll(opcode));
}

int CPU::ldrbPtrmlr(uint32_t opcode) {
    return ldrbPt(opcode, -rplr(opcode));
}

int CPU::ldrbPtrmar(uint32_t opcode) {
    return ldrbPt(opcode, -rpar(opcode));
}

int CPU::ldrbPtrmrr(uint32_t opcode) {
    return ldrbPt(opcode, -rprr(opcode));
}

int CPU::ldrbPtrpll(uint32_t opcode) {
    return ldrbPt(opcode, rpll(opcode));
}

int CPU::ldrbPtrplr(uint32_t opcode) {
    return ldrbPt(opcode, rplr(opcode));
}

int CPU::ldrbPtrpar(uint32_t opcode) {
    return ldrbPt(opcode, rpar(opcode));
}

int CPU::ldrbPtrprr(uint32_t opcode) {
    return ldrbPt(opcode, rprr(opcode));
}

int CPU::strbOfim(uint32_t opcode) {
    return strbOf(opcode, -ip(opcode));
}

int CPU::strbOfip(uint32_t opcode) {
    return strbOf(opcode, ip(opcode));
}

int CPU::strbOfrmll(uint32_t opcode) {
    return strbOf(opcode, -rpll(opcode));
}

int CPU::strbOfrmlr(uint32_t opcode) {
    return strbOf(opcode, -rplr(opcode));
}

int CPU::strbOfrmar(uint32_t opcode) {
    return strbOf(opcode, -rpar(opcode));
}

int CPU::strbOfrmrr(uint32_t opcode) {
    return strbOf(opcode, -rprr(opcode));
}

int CPU::strbOfrpll(uint32_t opcode) {
    return strbOf(opcode, rpll(opcode));
}

int CPU::strbOfrplr(uint32_t opcode) {
    return strbOf(opcode, rplr(opcode));
}

int CPU::strbOfrpar(uint32_t opcode) {
    return strbOf(opcode, rpar(opcode));
}

int CPU::strbOfrprr(uint32_t opcode) {
    return strbOf(opcode, rprr(opcode));
}

int CPU::strbPrim(uint32_t opcode) {
    return strbPr(opcode, -ip(opcode));
}

int CPU::strbPrip(uint32_t opcode) {
    return strbPr(opcode, ip(opcode));
}

int CPU::strbPrrmll(uint32_t opcode) {
    return strbPr(opcode, -rpll(opcode));
}

int CPU::strbPrrmlr(uint32_t opcode) {
    return strbPr(opcode, -rplr(opcode));
}

int CPU::strbPrrmar(uint32_t opcode) {
    return strbPr(opcode, -rpar(opcode));
}

int CPU::strbPrrmrr(uint32_t opcode) {
    return strbPr(opcode, -rprr(opcode));
}

int CPU::strbPrrpll(uint32_t opcode) {
    return strbPr(opcode, rpll(opcode));
}

int CPU::strbPrrplr(uint32_t opcode) {
    return strbPr(opcode, rplr(opcode));
}

int CPU::strbPrrpar(uint32_t opcode) {
    return strbPr(opcode, rpar(opcode));
}

int CPU::strbPrrprr(uint32_t opcode) {
    return strbPr(opcode, rprr(opcode));
}

int CPU::strbPtim(uint32_t opcode) {
    return strbPt(opcode, -ip(opcode));
}

int CPU::strbPtip(uint32_t opcode) {
    return strbPt(opcode, ip(opcode));
}

int CPU::strbPtrmll(uint32_t opcode) {
    return strbPt(opcode, -rpll(opcode));
}

int CPU::strbPtrmlr(uint32_t opcode) {
    return strbPt(opcode, -rplr(opcode));
}

int CPU::strbPtrmar(uint32_t opcode) {
    return strbPt(opcode, -rpar(opcode));
}

int CPU::strbPtrmrr(uint32_t opcode) {
    return strbPt(opcode, -rprr(opcode));
}

int CPU::strbPtrpll(uint32_t opcode) {
    return strbPt(opcode, rpll(opcode));
}

int CPU::strbPtrplr(uint32_t opcode) {
    return strbPt(opcode, rplr(opcode));
}

int CPU::strbPtrpar(uint32_t opcode) {
    return strbPt(opcode, rpar(opcode));
}

int CPU::strbPtrprr(uint32_t opcode) {
    return strbPt(opcode, rprr(opcode));
}

int CPU::ldrhOfrm(uint32_t opcode) {
    return ldrhOf(opcode, -rp(opcode));
}

int CPU::ldrhOfim(uint32_t opcode) {
    return ldrhOf(opcode, -ipH(opcode));
}

int CPU::ldrhOfrp(uint32_t opcode) {
    return ldrhOf(opcode, rp(opcode));
}

int CPU::ldrhOfip(uint32_t opcode) {
    return ldrhOf(opcode, ipH(opcode));
}

int CPU::ldrhPrrm(uint32_t opcode) {
    return ldrhPr(opcode, -rp(opcode));
}

int CPU::ldrhPrim(uint32_t opcode) {
    return ldrhPr(opcode, -ipH(opcode));
}

int CPU::ldrhPrrp(uint32_t opcode) {
    return ldrhPr(opcode, rp(opcode));
}

int CPU::ldrhPrip(uint32_t opcode) {
    return ldrhPr(opcode, ipH(opcode));
}

int CPU::ldrhPtrm(uint32_t opcode) {
    return ldrhPt(opcode, -rp(opcode));
}

int CPU::ldrhPtim(uint32_t opcode) {
    return ldrhPt(opcode, -ipH(opcode));
}

int CPU::ldrhPtrp(uint32_t opcode) {
    return ldrhPt(opcode, rp(opcode));
}

int CPU::ldrhPtip(uint32_t opcode) {
    return ldrhPt(opcode, ipH(opcode));
}

int CPU::strhOfrm(uint32_t opcode) {
    return strhOf(opcode, -rp(opcode));
}

int CPU::strhOfim(uint32_t opcode) {
    return strhOf(opcode, -ipH(opcode));
}

int CPU::strhOfrp(uint32_t opcode) {
    return strhOf(opcode, rp(opcode));
}

int CPU::strhOfip(uint32_t opcode) {
    return strhOf(opcode, ipH(opcode));
}

int CPU::strhPrrm(uint32_t opcode) {
    return strhPr(opcode, -rp(opcode));
}

int CPU::strhPrim(uint32_t opcode) {
    return strhPr(opcode, -ipH(opcode));
}

int CPU::strhPrrp(uint32_t opcode) {
    return strhPr(opcode, rp(opcode));
}

int CPU::strhPrip(uint32_t opcode) {
    return strhPr(opcode, ipH(opcode));
}

int CPU::strhPtrm(uint32_t opcode) {
    return strhPt(opcode, -rp(opcode));
}

int CPU::strhPtim(uint32_t opcode) {
    return strhPt(opcode, -ipH(opcode));
}

int CPU::strhPtrp(uint32_t opcode) {
    return strhPt(opcode, rp(opcode));
}

int CPU::strhPtip(uint32_t opcode) {
    return strhPt(opcode, ipH(opcode));
}

int CPU::ldrOfim(uint32_t opcode) {
    return ldrOf(opcode, -ip(opcode));
}

int CPU::ldrOfip(uint32_t opcode) {
    return ldrOf(opcode, ip(opcode));
}

int CPU::ldrOfrmll(uint32_t opcode) {
    return ldrOf(opcode, -rpll(opcode));
}

int CPU::ldrOfrmlr(uint32_t opcode) {
    return ldrOf(opcode, -rplr(opcode));
}

int CPU::ldrOfrmar(uint32_t opcode) {
    return ldrOf(opcode, -rpar(opcode));
}

int CPU::ldrOfrmrr(uint32_t opcode) {
    return ldrOf(opcode, -rprr(opcode));
}

int CPU::ldrOfrpll(uint32_t opcode) {
    return ldrOf(opcode, rpll(opcode));
}

int CPU::ldrOfrplr(uint32_t opcode) {
    return ldrOf(opcode, rplr(opcode));
}

int CPU::ldrOfrpar(uint32_t opcode) {
    return ldrOf(opcode, rpar(opcode));
}

int CPU::ldrOfrprr(uint32_t opcode) {
    return ldrOf(opcode, rprr(opcode));
}

int CPU::ldrPrim(uint32_t opcode) {
    return ldrPr(opcode, -ip(opcode));
}

int CPU::ldrPrip(uint32_t opcode) {
    return ldrPr(opcode, ip(opcode));
}

int CPU::ldrPrrmll(uint32_t opcode) {
    return ldrPr(opcode, -rpll(opcode));
}

int CPU::ldrPrrmlr(uint32_t opcode) {
    return ldrPr(opcode, -rplr(opcode));
}

int CPU::ldrPrrmar(uint32_t opcode) {
    return ldrPr(opcode, -rpar(opcode));
}

int CPU::ldrPrrmrr(uint32_t opcode) {
    return ldrPr(opcode, -rprr(opcode));
}

int CPU::ldrPrrpll(uint32_t opcode) {
    return ldrPr(opcode, rpll(opcode));
}

int CPU::ldrPrrplr(uint32_t opcode) {
    return ldrPr(opcode, rplr(opcode));
}

int CPU::ldrPrrpar(uint32_t opcode) {
    return ldrPr(opcode, rpar(opcode));
}

int CPU::ldrPrrprr(uint32_t opcode) {
    return ldrPr(opcode, rprr(opcode));
}

int CPU::ldrPtim(uint32_t opcode) {
    return ldrPt(opcode, -ip(opcode));
}

int CPU::ldrPtip(uint32_t opcode) {
    return ldrPt(opcode, ip(opcode));
}

int CPU::ldrPtrmll(uint32_t opcode) {
    return ldrPt(opcode, -rpll(opcode));
}

int CPU::ldrPtrmlr(uint32_t opcode) {
    return ldrPt(opcode, -rplr(opcode));
}

int CPU::ldrPtrmar(uint32_t opcode) {
    return ldrPt(opcode, -rpar(opcode));
}

int CPU::ldrPtrmrr(uint32_t opcode) {
    return ldrPt(opcode, -rprr(opcode));
}

int CPU::ldrPtrpll(uint32_t opcode) {
    return ldrPt(opcode, rpll(opcode));
}

int CPU::ldrPtrplr(uint32_t opcode) {
    return ldrPt(opcode, rplr(opcode));
}

int CPU::ldrPtrpar(uint32_t opcode) {
    return ldrPt(opcode, rpar(opcode));
}

int CPU::ldrPtrprr(uint32_t opcode) {
    return ldrPt(opcode, rprr(opcode));
}

int CPU::strOfim(uint32_t opcode) {
    return strOf(opcode, -ip(opcode));
}

int CPU::strOfip(uint32_t opcode) {
    return strOf(opcode, ip(opcode));
}

int CPU::strOfrmll(uint32_t opcode) {
    return strOf(opcode, -rpll(opcode));
}

int CPU::strOfrmlr(uint32_t opcode) {
    return strOf(opcode, -rplr(opcode));
}

int CPU::strOfrmar(uint32_t opcode) {
    return strOf(opcode, -rpar(opcode));
}

int CPU::strOfrmrr(uint32_t opcode) {
    return strOf(opcode, -rprr(opcode));
}

int CPU::strOfrpll(uint32_t opcode) {
    return strOf(opcode, rpll(opcode));
}

int CPU::strOfrplr(uint32_t opcode) {
    return strOf(opcode, rplr(opcode));
}

int CPU::strOfrpar(uint32_t opcode) {
    return strOf(opcode, rpar(opcode));
}

int CPU::strOfrprr(uint32_t opcode) {
    return strOf(opcode, rprr(opcode));
}

int CPU::strPrim(uint32_t opcode) {
    return strPr(opcode, -ip(opcode));
}

int CPU::strPrip(uint32_t opcode) {
    return strPr(opcode, ip(opcode));
}

int CPU::strPrrmll(uint32_t opcode) {
    return strPr(opcode, -rpll(opcode));
}

int CPU::strPrrmlr(uint32_t opcode) {
    return strPr(opcode, -rplr(opcode));
}

int CPU::strPrrmar(uint32_t opcode) {
    return strPr(opcode, -rpar(opcode));
}

int CPU::strPrrmrr(uint32_t opcode) {
    return strPr(opcode, -rprr(opcode));
}

int CPU::strPrrpll(uint32_t opcode) {
    return strPr(opcode, rpll(opcode));
}

int CPU::strPrrplr(uint32_t opcode) {
    return strPr(opcode, rplr(opcode));
}

int CPU::strPrrpar(uint32_t opcode) {
    return strPr(opcode, rpar(opcode));
}

int CPU::strPrrprr(uint32_t opcode) {
    return strPr(opcode, rprr(opcode));
}

int CPU::strPtim(uint32_t opcode) {
    return strPt(opcode, -ip(opcode));
}

int CPU::strPtip(uint32_t opcode) {
    return strPt(opcode, ip(opcode));
}

int CPU::strPtrmll(uint32_t opcode) {
    return strPt(opcode, -rpll(opcode));
}

int CPU::strPtrmlr(uint32_t opcode) {
    return strPt(opcode, -rplr(opcode));
}

int CPU::strPtrmar(uint32_t opcode) {
    return strPt(opcode, -rpar(opcode));
}

int CPU::strPtrmrr(uint32_t opcode) {
    return strPt(opcode, -rprr(opcode));
}

int CPU::strPtrpll(uint32_t opcode) {
    return strPt(opcode, rpll(opcode));
}

int CPU::strPtrplr(uint32_t opcode) {
    return strPt(opcode, rplr(opcode));
}

int CPU::strPtrpar(uint32_t opcode) {
    return strPt(opcode, rpar(opcode));
}

int CPU::strPtrprr(uint32_t opcode) {
    return strPt(opcode, rprr(opcode));
}

int CPU::ldrdOfrm(uint32_t opcode) {
    return ldrdOf(opcode, -rp(opcode));
}

int CPU::ldrdOfim(uint32_t opcode) {
    return ldrdOf(opcode, -ipH(opcode));
}

int CPU::ldrdOfrp(uint32_t opcode) {
    return ldrdOf(opcode, rp(opcode));
}

int CPU::ldrdOfip(uint32_t opcode) {
    return ldrdOf(opcode, ipH(opcode));
}

int CPU::ldrdPrrm(uint32_t opcode) {
    return ldrdPr(opcode, -rp(opcode));
}

int CPU::ldrdPrim(uint32_t opcode) {
    return ldrdPr(opcode, -ipH(opcode));
}

int CPU::ldrdPrrp(uint32_t opcode) {
    return ldrdPr(opcode, rp(opcode));
}

int CPU::ldrdPrip(uint32_t opcode) {
    return ldrdPr(opcode, ipH(opcode));
}

int CPU::ldrdPtrm(uint32_t opcode) {
    return ldrdPt(opcode, -rp(opcode));
}

int CPU::ldrdPtim(uint32_t opcode) {
    return ldrdPt(opcode, -ipH(opcode));
}

int CPU::ldrdPtrp(uint32_t opcode) {
    return ldrdPt(opcode, rp(opcode));
}

int CPU::ldrdPtip(uint32_t opcode) {
    return ldrdPt(opcode, ipH(opcode));
}

int CPU::strdOfrm(uint32_t opcode) {
    return strdOf(opcode, -rp(opcode));
}

int CPU::strdOfim(uint32_t opcode) {
    return strdOf(opcode, -ipH(opcode));
}

int CPU::strdOfrp(uint32_t opcode) {
    return strdOf(opcode, rp(opcode));
}

int CPU::strdOfip(uint32_t opcode) {
    return strdOf(opcode, ipH(opcode));
}

int CPU::strdPrrm(uint32_t opcode) {
    return strdPr(opcode, -rp(opcode));
}

int CPU::strdPrim(uint32_t opcode) {
    return strdPr(opcode, -ipH(opcode));
}

int CPU::strdPrrp(uint32_t opcode) {
    return strdPr(opcode, rp(opcode));
}

int CPU::strdPrip(uint32_t opcode) {
    return strdPr(opcode, ipH(opcode));
}

int CPU::strdPtrm(uint32_t opcode) {
    return strdPt(opcode, -rp(opcode));
}

int CPU::strdPtim(uint32_t opcode) {
    return strdPt(opcode, -ipH(opcode));
}

int CPU::strdPtrp(uint32_t opcode) {
    return strdPt(opcode, rp(opcode));
}

int CPU::strdPtip(uint32_t opcode) {
    return strdPt(opcode, ipH(opcode));
}

int CPU::swpb(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[opcode & 0x0000000F];
    uint32_t  op2 = *registers[(opcode & 0x000F0000) >> 16];
    *op0          = core->memory.read<uint8_t>(cpu, op2);
    core->memory.write<uint8_t>(cpu, op2, op1);
    return (cpu == 0) ? 2 : 4;
}

int CPU::swp(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[opcode & 0x0000000F];
    uint32_t  op2 = *registers[(opcode & 0x000F0000) >> 16];
    *op0          = core->memory.read<uint32_t>(cpu, op2);
    core->memory.write<uint32_t>(cpu, op2, op1);
    if (op2 & 3) {
        int shift = (op2 & 3) * 8;
        *op0      = (*op0 << (32 - shift)) | (*op0 >> shift);
    }
    return (cpu == 0) ? 2 : 4;
}

int CPU::ldmda(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[(opcode & 0x000F0000) >> 16] - n * 4;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
        }
    }
    if (!(opcode & BIT(15)))
        return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    if (cpu == 0 && (*registers[15] & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return n + 4;
}

int CPU::stmda(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[(opcode & 0x000F0000) >> 16] - n * 4;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            core->memory.write<uint32_t>(cpu, op0, *registers[i]);
        }
    }
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::ldmia(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[(opcode & 0x000F0000) >> 16];
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
            op0 += 4;
        }
    }
    if (!(opcode & BIT(15)))
        return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    if (cpu == 0 && (*registers[15] & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return n + 4;
}

int CPU::stmia(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[(opcode & 0x000F0000) >> 16];
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            core->memory.write<uint32_t>(cpu, op0, *registers[i]);
            op0 += 4;
        }
    }
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::ldmdb(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[(opcode & 0x000F0000) >> 16] - n * 4;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
            op0 += 4;
        }
    }
    if (!(opcode & BIT(15)))
        return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    if (cpu == 0 && (*registers[15] & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return n + 4;
}

int CPU::stmdb(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[(opcode & 0x000F0000) >> 16] - n * 4;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            core->memory.write<uint32_t>(cpu, op0, *registers[i]);
            op0 += 4;
        }
    }
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::ldmib(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[(opcode & 0x000F0000) >> 16];
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
        }
    }
    if (!(opcode & BIT(15)))
        return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    if (cpu == 0 && (*registers[15] & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return n + 4;
}

int CPU::stmib(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[(opcode & 0x000F0000) >> 16];
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            core->memory.write<uint32_t>(cpu, op0, *registers[i]);
        }
    }
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::ldmdaW(uint32_t opcode) {
    int      m         = (opcode & 0x000F0000) >> 16;
    int      n         = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0       = *registers[m] - n * 4;
    uint32_t writeback = op0;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
        }
    }
    if (!(opcode & BIT(m)) ||
        (cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
        *registers[m] = writeback;
    if (!(opcode & BIT(15)))
        return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    if (cpu == 0 && (*registers[15] & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return n + 4;
}

int CPU::stmdaW(uint32_t opcode) {
    int      m         = (opcode & 0x000F0000) >> 16;
    int      n         = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0       = *registers[m] - n * 4;
    uint32_t writeback = op0;
    if (cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *registers[m] = writeback;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            core->memory.write<uint32_t>(cpu, op0, *registers[i]);
        }
    }
    *registers[m] = writeback;
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::ldmiaW(uint32_t opcode) {
    int      m   = (opcode & 0x000F0000) >> 16;
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[m];
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
            op0 += 4;
        }
    }
    if (!(opcode & BIT(m)) ||
        (cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
        *registers[m] = op0;
    if (!(opcode & BIT(15)))
        return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    if (cpu == 0 && (*registers[15] & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return n + 4;
}

int CPU::stmiaW(uint32_t opcode) {
    int      m   = (opcode & 0x000F0000) >> 16;
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[m];
    if (cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *registers[m] = op0 + n * 4;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            core->memory.write<uint32_t>(cpu, op0, *registers[i]);
            op0 += 4;
        }
    }
    *registers[m] = op0;
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::ldmdbW(uint32_t opcode) {
    int      m         = (opcode & 0x000F0000) >> 16;
    int      n         = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0       = *registers[m] - n * 4;
    uint32_t writeback = op0;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
            op0 += 4;
        }
    }
    if (!(opcode & BIT(m)) ||
        (cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
        *registers[m] = writeback;
    if (!(opcode & BIT(15)))
        return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    if (cpu == 0 && (*registers[15] & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return n + 4;
}

int CPU::stmdbW(uint32_t opcode) {
    int      m         = (opcode & 0x000F0000) >> 16;
    int      n         = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0       = *registers[m] - n * 4;
    uint32_t writeback = op0;
    if (cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *registers[m] = writeback;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            core->memory.write<uint32_t>(cpu, op0, *registers[i]);
            op0 += 4;
        }
    }
    *registers[m] = writeback;
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::ldmibW(uint32_t opcode) {
    int      m   = (opcode & 0x000F0000) >> 16;
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[m];
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
        }
    }
    if (!(opcode & BIT(m)) ||
        (cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
        *registers[m] = op0;
    if (!(opcode & BIT(15)))
        return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    if (cpu == 0 && (*registers[15] & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return n + 4;
}

int CPU::stmibW(uint32_t opcode) {
    int      m   = (opcode & 0x000F0000) >> 16;
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[m];
    if (cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *registers[m] = op0 + n * 4;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            core->memory.write<uint32_t>(cpu, op0, *registers[i]);
        }
    }
    *registers[m] = op0;
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::ldmdaU(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[(opcode & 0x000F0000) >> 16] - n * 4;
    if (!(opcode & BIT(15))) {
        for (int i = 0; i < 16; i++) {
            if (opcode & BIT(i)) {
                op0 += 4;
                registersUsr[i] = core->memory.read<uint32_t>(cpu, op0);
            }
        }
        return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    }
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
        }
    }
    if (spsr)
        setCpsr(*spsr);
    if (cpu == 0 && (*registers[15] & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return n + 4;
}

int CPU::stmdaU(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[(opcode & 0x000F0000) >> 16] - n * 4;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            core->memory.write<uint32_t>(cpu, op0, registersUsr[i]);
        }
    }
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::ldmiaU(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[(opcode & 0x000F0000) >> 16];
    if (!(opcode & BIT(15))) {
        for (int i = 0; i < 16; i++) {
            if (opcode & BIT(i)) {
                registersUsr[i] = core->memory.read<uint32_t>(cpu, op0);
                op0 += 4;
            }
        }
        return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    }
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
            op0 += 4;
        }
    }
    if (spsr)
        setCpsr(*spsr);
    if (cpu == 0 && (*registers[15] & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return n + 4;
}

int CPU::stmiaU(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[(opcode & 0x000F0000) >> 16];
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            core->memory.write<uint32_t>(cpu, op0, registersUsr[i]);
            op0 += 4;
        }
    }
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::ldmdbU(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[(opcode & 0x000F0000) >> 16] - n * 4;
    if (!(opcode & BIT(15))) {
        for (int i = 0; i < 16; i++) {
            if (opcode & BIT(i)) {
                registersUsr[i] = core->memory.read<uint32_t>(cpu, op0);
                op0 += 4;
            }
        }
        return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    }
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
            op0 += 4;
        }
    }
    if (spsr)
        setCpsr(*spsr);
    if (cpu == 0 && (*registers[15] & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return n + 4;
}

int CPU::stmdbU(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[(opcode & 0x000F0000) >> 16] - n * 4;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            core->memory.write<uint32_t>(cpu, op0, registersUsr[i]);
            op0 += 4;
        }
    }
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::ldmibU(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[(opcode & 0x000F0000) >> 16];
    if (!(opcode & BIT(15))) {
        for (int i = 0; i < 16; i++) {
            if (opcode & BIT(i)) {
                op0 += 4;
                registersUsr[i] = core->memory.read<uint32_t>(cpu, op0);
            }
        }
        return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    }
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
        }
    }
    if (spsr)
        setCpsr(*spsr);
    if (cpu == 0 && (*registers[15] & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return n + 4;
}

int CPU::stmibU(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[(opcode & 0x000F0000) >> 16];
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            core->memory.write<uint32_t>(cpu, op0, registersUsr[i]);
        }
    }
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::ldmdaUW(uint32_t opcode) {
    int      m         = (opcode & 0x000F0000) >> 16;
    int      n         = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0       = *registers[m] - n * 4;
    uint32_t writeback = op0;
    if (!(opcode & BIT(15))) {
        for (int i = 0; i < 16; i++) {
            if (opcode & BIT(i)) {
                op0 += 4;
                registersUsr[i] = core->memory.read<uint32_t>(cpu, op0);
            }
        }
        if (!(opcode & BIT(m)) ||
            (cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
            *registers[m] = writeback;
        return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    }
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
        }
    }
    if (!(opcode & BIT(m)) ||
        (cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
        *registers[m] = writeback;
    if (spsr)
        setCpsr(*spsr);
    if (cpu == 0 && (*registers[15] & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return n + 4;
}

int CPU::stmdaUW(uint32_t opcode) {
    int      m         = (opcode & 0x000F0000) >> 16;
    int      n         = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0       = *registers[m] - n * 4;
    uint32_t writeback = op0;
    if (cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *registers[m] = writeback;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            core->memory.write<uint32_t>(cpu, op0, registersUsr[i]);
        }
    }
    *registers[m] = writeback;
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::ldmiaUW(uint32_t opcode) {
    int      m   = (opcode & 0x000F0000) >> 16;
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[m];
    if (!(opcode & BIT(15))) {
        for (int i = 0; i < 16; i++) {
            if (opcode & BIT(i)) {
                registersUsr[i] = core->memory.read<uint32_t>(cpu, op0);
                op0 += 4;
            }
        }
        if (!(opcode & BIT(m)) ||
            (cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
            *registers[m] = op0;
        return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    }
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
            op0 += 4;
        }
    }
    if (!(opcode & BIT(m)) ||
        (cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
        *registers[m] = op0;
    if (spsr)
        setCpsr(*spsr);
    if (cpu == 0 && (*registers[15] & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return n + 4;
}

int CPU::stmiaUW(uint32_t opcode) {
    int      m   = (opcode & 0x000F0000) >> 16;
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[m];
    if (cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *registers[m] = op0 + n * 4;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            core->memory.write<uint32_t>(cpu, op0, registersUsr[i]);
            op0 += 4;
        }
    }
    *registers[m] = op0;
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::ldmdbUW(uint32_t opcode) {
    int      m         = (opcode & 0x000F0000) >> 16;
    int      n         = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0       = *registers[m] - n * 4;
    uint32_t writeback = op0;
    if (!(opcode & BIT(15))) {
        for (int i = 0; i < 16; i++) {
            if (opcode & BIT(i)) {
                registersUsr[i] = core->memory.read<uint32_t>(cpu, op0);
                op0 += 4;
            }
        }
        if (!(opcode & BIT(m)) ||
            (cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
            *registers[m] = writeback;
        return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    }
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
            op0 += 4;
        }
    }
    if (!(opcode & BIT(m)) ||
        (cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
        *registers[m] = writeback;
    if (spsr)
        setCpsr(*spsr);
    if (cpu == 0 && (*registers[15] & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return n + 4;
}

int CPU::stmdbUW(uint32_t opcode) {
    int      m         = (opcode & 0x000F0000) >> 16;
    int      n         = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0       = *registers[m] - n * 4;
    uint32_t writeback = op0;
    if (cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *registers[m] = writeback;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            core->memory.write<uint32_t>(cpu, op0, registersUsr[i]);
            op0 += 4;
        }
    }
    *registers[m] = writeback;
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::ldmibUW(uint32_t opcode) {
    int      m   = (opcode & 0x000F0000) >> 16;
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[m];
    if (!(opcode & BIT(15))) {
        for (int i = 0; i < 16; i++) {
            if (opcode & BIT(i)) {
                op0 += 4;
                registersUsr[i] = core->memory.read<uint32_t>(cpu, op0);
            }
        }
        if (!(opcode & BIT(m)) ||
            (cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
            *registers[m] = op0;
        return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    }
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
        }
    }
    if (!(opcode & BIT(m)) ||
        (cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
        *registers[m] = op0;
    if (spsr)
        setCpsr(*spsr);
    if (cpu == 0 && (*registers[15] & BIT(0)))
        cpsr |= BIT(5);
    flushPipeline();
    return n + 4;
}

int CPU::stmibUW(uint32_t opcode) {
    int      m   = (opcode & 0x000F0000) >> 16;
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *registers[m];
    if (cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *registers[m] = op0 + n * 4;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            core->memory.write<uint32_t>(cpu, op0, registersUsr[i]);
        }
    }
    *registers[m] = op0;
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::msrRc(uint32_t opcode) {
    uint32_t op1 = *registers[opcode & 0x0000000F];
    if (opcode & BIT(16)) {
        uint8_t mask = ((cpsr & 0x1F) == 0x10) ? 0xE0 : 0xFF;
        setCpsr((cpsr & ~mask) | (op1 & mask));
    }
    for (int i = 0; i < 3; i++) {
        if (opcode & BIT(17 + i))
            cpsr = (cpsr & ~(0x0000FF00 << (i * 8))) | (op1 & (0x0000FF00 << (i * 8)));
    }
    return 1;
}

int CPU::msrRs(uint32_t opcode) {
    uint32_t op1 = *registers[opcode & 0x0000000F];
    if (spsr) {
        for (int i = 0; i < 4; i++) {
            if (opcode & BIT(16 + i))
                *spsr = (*spsr & ~(0x000000FF << (i * 8))) | (op1 & (0x000000FF << (i * 8)));
        }
    }
    return 1;
}

int CPU::msrIc(uint32_t opcode) {
    uint32_t value = opcode & 0x000000FF;
    uint8_t  shift = (opcode & 0x00000F00) >> 7;
    uint32_t op1   = (value << (32 - shift)) | (value >> shift);
    if (opcode & BIT(16)) {
        uint8_t mask = ((cpsr & 0x1F) == 0x10) ? 0xE0 : 0xFF;
        setCpsr((cpsr & ~mask) | (op1 & mask));
    }
    for (int i = 0; i < 3; i++) {
        if (opcode & BIT(17 + i))
            cpsr = (cpsr & ~(0x0000FF00 << (i * 8))) | (op1 & (0x0000FF00 << (i * 8)));
    }
    return 1;
}

int CPU::msrIs(uint32_t opcode) {
    uint32_t value = opcode & 0x000000FF;
    uint8_t  shift = (opcode & 0x00000F00) >> 7;
    uint32_t op1   = (value << (32 - shift)) | (value >> shift);
    if (spsr) {
        for (int i = 0; i < 4; i++) {
            if (opcode & BIT(16 + i))
                *spsr = (*spsr & ~(0x000000FF << (i * 8))) | (op1 & (0x000000FF << (i * 8)));
        }
    }
    return 1;
}

int CPU::mrsRc(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    *op0          = cpsr;
    return (cpu == 0 ? 2 : 1);
}

int CPU::mrsRs(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    if (spsr)
        *op0 = *spsr;
    return (cpu == 0 ? 2 : 1);
}

int CPU::mrc(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op2 = registers[(opcode & 0x0000F000) >> 12];
    int       op3 = (opcode & 0x000F0000) >> 16;
    int       op4 = opcode & 0x0000000F;
    int       op5 = (opcode & 0x000000E0) >> 5;
    *op2          = core->cp15.read(op3, op4, op5);
    return 1;
}

int CPU::mcr(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t op2 = *registers[(opcode & 0x0000F000) >> 12];
    int      op3 = (opcode & 0x000F0000) >> 16;
    int      op4 = opcode & 0x0000000F;
    int      op5 = (opcode & 0x000000E0) >> 5;
    core->cp15.write(op3, op4, op5, op2);
    return 1;
}

int CPU::ldrsbRegT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = *registers[(opcode & 0x01C0) >> 6];
    *op0          = (int8_t)core->memory.read<uint8_t>(cpu, op1 + op2);
    return ((cpu == 0) ? 1 : 3);
}

int CPU::ldrshRegT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = *registers[(opcode & 0x01C0) >> 6];
    *op0          = (int16_t)core->memory.read<uint16_t>(cpu, op1 += op2);
    if (cpu == 1 && (op1 & 1))
        *op0 = (int16_t)*op0 >> 8;
    return ((cpu == 0) ? 1 : 3);
}

int CPU::ldrbRegT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = *registers[(opcode & 0x01C0) >> 6];
    *op0          = core->memory.read<uint8_t>(cpu, op1 + op2);
    return ((cpu == 0) ? 1 : 3);
}

int CPU::strbRegT(uint16_t opcode) {
    uint32_t op0 = *registers[opcode & 0x0007];
    uint32_t op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t op2 = *registers[(opcode & 0x01C0) >> 6];
    core->memory.write<uint8_t>(cpu, op1 + op2, op0);
    return ((cpu == 0) ? 1 : 2);
}

int CPU::ldrhRegT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = *registers[(opcode & 0x01C0) >> 6];
    *op0          = core->memory.read<uint16_t>(cpu, op1 += op2);
    if (cpu == 1 && (op1 & 1))
        *op0 = (*op0 << 24) | (*op0 >> 8);
    return ((cpu == 0) ? 1 : 3);
}

int CPU::strhRegT(uint16_t opcode) {
    uint32_t op0 = *registers[opcode & 0x0007];
    uint32_t op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t op2 = *registers[(opcode & 0x01C0) >> 6];
    core->memory.write<uint16_t>(cpu, op1 + op2, op0);
    return ((cpu == 0) ? 1 : 2);
}

int CPU::ldrRegT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = *registers[(opcode & 0x01C0) >> 6];
    *op0          = core->memory.read<uint32_t>(cpu, op1 += op2);
    if (op1 & 3) {
        int shift = (op1 & 3) * 8;
        *op0      = (*op0 << (32 - shift)) | (*op0 >> shift);
    }
    return ((cpu == 0) ? 1 : 3);
}

int CPU::strRegT(uint16_t opcode) {
    uint32_t op0 = *registers[opcode & 0x0007];
    uint32_t op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t op2 = *registers[(opcode & 0x01C0) >> 6];
    core->memory.write<uint32_t>(cpu, op1 + op2, op0);
    return ((cpu == 0) ? 1 : 2);
}

int CPU::ldrbImm5T(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = (opcode & 0x07C0) >> 6;
    *op0          = core->memory.read<uint8_t>(cpu, op1 + op2);
    return ((cpu == 0) ? 1 : 3);
}

int CPU::strbImm5T(uint16_t opcode) {
    uint32_t op0 = *registers[opcode & 0x0007];
    uint32_t op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t op2 = (opcode & 0x07C0) >> 6;
    core->memory.write<uint8_t>(cpu, op1 + op2, op0);
    return ((cpu == 0) ? 1 : 2);
}

int CPU::ldrhImm5T(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = (opcode & 0x07C0) >> 5;
    *op0          = core->memory.read<uint16_t>(cpu, op1 += op2);
    if (cpu == 1 && (op1 & 1))
        *op0 = (*op0 << 24) | (*op0 >> 8);
    return ((cpu == 0) ? 1 : 3);
}

int CPU::strhImm5T(uint16_t opcode) {
    uint32_t op0 = *registers[opcode & 0x0007];
    uint32_t op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t op2 = (opcode & 0x07C0) >> 5;
    core->memory.write<uint16_t>(cpu, op1 + op2, op0);
    return ((cpu == 0) ? 1 : 2);
}

int CPU::ldrImm5T(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = (opcode & 0x07C0) >> 4;
    *op0          = core->memory.read<uint32_t>(cpu, op1 += op2);
    if (op1 & 3) {
        int shift = (op1 & 3) * 8;
        *op0      = (*op0 << (32 - shift)) | (*op0 >> shift);
    }
    return ((cpu == 0) ? 1 : 3);
}

int CPU::strImm5T(uint16_t opcode) {
    uint32_t op0 = *registers[opcode & 0x0007];
    uint32_t op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t op2 = (opcode & 0x07C0) >> 4;
    core->memory.write<uint32_t>(cpu, op1 + op2, op0);
    return ((cpu == 0) ? 1 : 2);
}

int CPU::ldrPcT(uint16_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0700) >> 8];
    uint32_t  op1 = *registers[15] & ~3;
    uint32_t  op2 = (opcode & 0x00FF) << 2;
    *op0          = core->memory.read<uint32_t>(cpu, op1 += op2);
    if (op1 & 3) {
        int shift = (op1 & 3) * 8;
        *op0      = (*op0 << (32 - shift)) | (*op0 >> shift);
    }
    return ((cpu == 0) ? 1 : 3);
}

int CPU::ldrSpT(uint16_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0700) >> 8];
    uint32_t  op1 = *registers[13];
    uint32_t  op2 = (opcode & 0x00FF) << 2;
    *op0          = core->memory.read<uint32_t>(cpu, op1 += op2);
    if (op1 & 3) {
        int shift = (op1 & 3) * 8;
        *op0      = (*op0 << (32 - shift)) | (*op0 >> shift);
    }
    return ((cpu == 0) ? 1 : 3);
}

int CPU::strSpT(uint16_t opcode) {
    uint32_t op0 = *registers[(opcode & 0x0700) >> 8];
    uint32_t op1 = *registers[13];
    uint32_t op2 = (opcode & 0x00FF) << 2;
    core->memory.write<uint32_t>(cpu, op1 + op2, op0);
    return ((cpu == 0) ? 1 : 2);
}

int CPU::ldmiaT(uint16_t opcode) {
    int      m   = (opcode & 0x0700) >> 8;
    int      n   = bitCount[opcode & 0xFF];
    uint32_t op0 = *registers[m];
    for (int i = 0; i < 8; i++) {
        if (opcode & BIT(i)) {
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
            op0 += 4;
        }
    }
    if (!(opcode & BIT(m)))
        *registers[m] = op0;
    return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
}

int CPU::stmiaT(uint16_t opcode) {
    int      m   = (opcode & 0x0700) >> 8;
    int      n   = bitCount[opcode & 0xFF];
    uint32_t op0 = *registers[m];
    if (cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *registers[m] = op0 + n * 4;
    for (int i = 0; i < 8; i++) {
        if (opcode & BIT(i)) {
            core->memory.write<uint32_t>(cpu, op0, *registers[i]);
            op0 += 4;
        }
    }
    *registers[m] = op0;
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::popT(uint16_t opcode) {
    int      n   = bitCount[opcode & 0xFF];
    uint32_t op0 = *registers[13];
    for (int i = 0; i < 8; i++) {
        if (opcode & BIT(i)) {
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
            op0 += 4;
        }
    }
    *registers[13] = op0;
    return n + ((cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
}

int CPU::pushT(uint16_t opcode) {
    int      n     = bitCount[opcode & 0xFF];
    uint32_t op0   = *registers[13] - n * 4;
    *registers[13] = op0;
    for (int i = 0; i < 8; i++) {
        if (opcode & BIT(i)) {
            core->memory.write<uint32_t>(cpu, op0, *registers[i]);
            op0 += 4;
        }
    }
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::popPcT(uint16_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + 1;
    uint32_t op0 = *registers[13];
    for (int i = 0; i < 8; i++) {
        if (opcode & BIT(i)) {
            *registers[i] = core->memory.read<uint32_t>(cpu, op0);
            op0 += 4;
        }
    }
    *registers[15] = core->memory.read<uint32_t>(cpu, op0);
    *registers[13] = op0 + 4;
    if (cpu == 0 && !(*registers[15] & BIT(0)))
        cpsr &= ~BIT(5);
    flushPipeline();
    return n + 4;
}

int CPU::pushLrT(uint16_t opcode) {
    int      n     = bitCount[opcode & 0xFF] + 1;
    uint32_t op0   = *registers[13] - n * 4;
    *registers[13] = op0;
    for (int i = 0; i < 8; i++) {
        if (opcode & BIT(i)) {
            core->memory.write<uint32_t>(cpu, op0, *registers[i]);
            op0 += 4;
        }
    }
    core->memory.write<uint32_t>(cpu, op0, *registers[14]);
    return n + ((cpu == 0 && n > 1) ? 0 : 1);
}

} // namespace emulator_demo

