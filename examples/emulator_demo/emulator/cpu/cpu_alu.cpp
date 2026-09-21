#include <bit>
#include "emulator/cpu/cpu.hpp"
#include "emulator/core.hpp"

namespace emulator_demo {


FORCE_INLINE uint32_t CPU::lli(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    return value << shift;
}

FORCE_INLINE uint32_t CPU::llr(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F] + (((opcode & 0x0000000F) == 0x0000000F) ? 4 : 0);
    uint8_t  shift = *registers[(opcode & 0x00000F00) >> 8];
    return (shift < 32) ? (value << shift) : 0;
}

FORCE_INLINE uint32_t CPU::lri(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    return shift ? (value >> shift) : 0;
}

FORCE_INLINE uint32_t CPU::lrr(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F] + (((opcode & 0x0000000F) == 0x0000000F) ? 4 : 0);
    uint8_t  shift = *registers[(opcode & 0x00000F00) >> 8];
    return (shift < 32) ? (value >> shift) : 0;
}

FORCE_INLINE uint32_t CPU::ari(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    return shift ? ((int32_t)value >> shift) : ((value & BIT(31)) ? 0xFFFFFFFF : 0);
}

FORCE_INLINE uint32_t CPU::arr(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F] + (((opcode & 0x0000000F) == 0x0000000F) ? 4 : 0);
    uint8_t  shift = *registers[(opcode & 0x00000F00) >> 8];
    return (shift < 32) ? ((int32_t)value >> shift) : ((value & BIT(31)) ? 0xFFFFFFFF : 0);
}

FORCE_INLINE uint32_t CPU::rri(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    return shift ? ((value << (32 - shift)) | (value >> shift)) : (((cpsr & BIT(29)) << 2) | (value >> 1));
}

// Rotations : `std::rotr` et non `(v << (32 - s)) | (v >> s)`, qui décale de
// 32 bits quand s == 0 (comportement indéfini, relevé par UBSan ; x86 donnait
// le bon résultat par hasard).
FORCE_INLINE uint32_t CPU::rrr(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F] + (((opcode & 0x0000000F) == 0x0000000F) ? 4 : 0);
    uint8_t  shift = *registers[(opcode & 0x00000F00) >> 8];
    return std::rotr(value, shift % 32);
}

FORCE_INLINE uint32_t CPU::imm(uint32_t opcode) {
    uint32_t value = opcode & 0x000000FF;
    uint8_t  shift = (opcode & 0x00000F00) >> 7;
    return std::rotr(value, shift);
}

FORCE_INLINE uint32_t CPU::lliS(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    if (shift > 0)
        cpsr = (cpsr & ~BIT(29)) | ((bool)(value & BIT(32 - shift)) << 29);
    return value << shift;
}

FORCE_INLINE uint32_t CPU::llrS(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F] + (((opcode & 0x0000000F) == 0x0000000F) ? 4 : 0);
    uint8_t  shift = *registers[(opcode & 0x00000F00) >> 8];
    if (shift > 0)
        cpsr = (cpsr & ~BIT(29)) | ((shift <= 32 && (value & BIT(32 - shift))) << 29);
    return (shift < 32) ? (value << shift) : 0;
}

FORCE_INLINE uint32_t CPU::lriS(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    cpsr           = (cpsr & ~BIT(29)) | ((bool)(value & BIT(shift ? (shift - 1) : 31)) << 29);
    return shift ? (value >> shift) : 0;
}

FORCE_INLINE uint32_t CPU::lrrS(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F] + (((opcode & 0x0000000F) == 0x0000000F) ? 4 : 0);
    uint8_t  shift = *registers[(opcode & 0x00000F00) >> 8];
    if (shift > 0)
        cpsr = (cpsr & ~BIT(29)) | ((shift <= 32 && (value & BIT(shift - 1))) << 29);
    return (shift < 32) ? (value >> shift) : 0;
}

FORCE_INLINE uint32_t CPU::ariS(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    cpsr           = (cpsr & ~BIT(29)) | ((bool)(value & BIT(shift ? (shift - 1) : 31)) << 29);
    return shift ? ((int32_t)value >> shift) : ((value & BIT(31)) ? 0xFFFFFFFF : 0);
}

FORCE_INLINE uint32_t CPU::arrS(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F] + (((opcode & 0x0000000F) == 0x0000000F) ? 4 : 0);
    uint8_t  shift = *registers[(opcode & 0x00000F00) >> 8];
    if (shift > 0)
        cpsr = (cpsr & ~BIT(29)) | ((bool)(value & BIT((shift <= 32) ? (shift - 1) : 31)) << 29);
    return (shift < 32) ? ((int32_t)value >> shift) : ((value & BIT(31)) ? 0xFFFFFFFF : 0);
}

FORCE_INLINE uint32_t CPU::rriS(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    uint32_t res   = shift ? ((value << (32 - shift)) | (value >> shift)) : (((cpsr & BIT(29)) << 2) | (value >> 1));
    cpsr           = (cpsr & ~BIT(29)) | ((bool)(value & BIT(shift ? (shift - 1) : 0)) << 29);
    return res;
}

FORCE_INLINE uint32_t CPU::rrrS(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0x0000000F] + (((opcode & 0x0000000F) == 0x0000000F) ? 4 : 0);
    uint8_t  shift = *registers[(opcode & 0x00000F00) >> 8];
    if (shift > 0)
        cpsr = (cpsr & ~BIT(29)) | ((bool)(value & BIT((shift - 1) % 32)) << 29);
    return std::rotr(value, shift % 32);
}

FORCE_INLINE uint32_t CPU::immS(uint32_t opcode) {
    uint32_t value = opcode & 0x000000FF;
    uint8_t  shift = (opcode & 0x00000F00) >> 7;
    if (shift > 0)
        cpsr = (cpsr & ~BIT(29)) | ((bool)(value & BIT(shift - 1)) << 29);
    return std::rotr(value, shift);
}

FORCE_INLINE int CPU::_and(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 & op2;
    if (op0 != registers[15])
        return 1;
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::eor(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 ^ op2;
    if (op0 != registers[15])
        return 1;
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::sub(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 - op2;
    if (op0 != registers[15])
        return 1;
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::rsb(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op2 - op1;
    if (op0 != registers[15])
        return 1;
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::add(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 + op2;
    if (op0 != registers[15])
        return 1;
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::adc(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 + op2 + ((cpsr & BIT(29)) >> 29);
    if (op0 != registers[15])
        return 1;
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::sbc(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 - op2 - 1 + ((cpsr & BIT(29)) >> 29);
    if (op0 != registers[15])
        return 1;
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::rsc(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op2 - op1 - 1 + ((cpsr & BIT(29)) >> 29);
    if (op0 != registers[15])
        return 1;
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::tst(uint32_t opcode, uint32_t op2) {
    uint32_t op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    uint32_t res = op1 & op2;
    cpsr         = (cpsr & ~0xC0000000) | (res & BIT(31)) | ((res == 0) << 30);
    return 1;
}

FORCE_INLINE int CPU::teq(uint32_t opcode, uint32_t op2) {
    uint32_t op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    uint32_t res = op1 ^ op2;
    cpsr         = (cpsr & ~0xC0000000) | (res & BIT(31)) | ((res == 0) << 30);
    return 1;
}

FORCE_INLINE int CPU::cmp(uint32_t opcode, uint32_t op2) {
    uint32_t op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    uint32_t res = op1 - op2;
    cpsr         = (cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) | ((op1 >= res) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (res & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

FORCE_INLINE int CPU::cmn(uint32_t opcode, uint32_t op2) {
    uint32_t op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    uint32_t res = op1 + op2;
    cpsr         = (cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) | ((op1 > res) << 29) |
           (((op2 & BIT(31)) == (op1 & BIT(31)) && (res & BIT(31)) != (op2 & BIT(31))) << 28);
    return 1;
}

FORCE_INLINE int CPU::orr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 | op2;
    if (op0 != registers[15])
        return 1;
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::mov(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    *op0          = op2;
    if (op0 != registers[15])
        return 1;
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::bic(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 & ~op2;
    if (op0 != registers[15])
        return 1;
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::mvn(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    *op0          = ~op2;
    if (op0 != registers[15])
        return 1;
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::ands(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 & op2;
    cpsr          = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op0 != registers[15])
        return 1;
    if (spsr)
        setCpsr(*spsr);
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::eors(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 ^ op2;
    cpsr          = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op0 != registers[15])
        return 1;
    if (spsr)
        setCpsr(*spsr);
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::subs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 - op2;
    cpsr          = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 >= *op0) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (*op0 & BIT(31)) == (op2 & BIT(31))) << 28);
    if (op0 != registers[15])
        return 1;
    if (spsr)
        setCpsr(*spsr);
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::rsbs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op2 - op1;
    cpsr          = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op2 >= *op0) << 29) |
           (((op1 & BIT(31)) != (op2 & BIT(31)) && (*op0 & BIT(31)) == (op1 & BIT(31))) << 28);
    if (op0 != registers[15])
        return 1;
    if (spsr)
        setCpsr(*spsr);
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::adds(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 + op2;
    cpsr          = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 > *op0) << 29) |
           (((op2 & BIT(31)) == (op1 & BIT(31)) && (*op0 & BIT(31)) != (op2 & BIT(31))) << 28);
    if (op0 != registers[15])
        return 1;
    if (spsr)
        setCpsr(*spsr);
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::adcs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 + op2 + ((cpsr & BIT(29)) >> 29);
    cpsr          = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) |
           ((op1 > *op0 || (op2 == 0xFFFFFFFF && (cpsr & BIT(29)))) << 29) |
           (((op2 & BIT(31)) == (op1 & BIT(31)) && (*op0 & BIT(31)) != (op2 & BIT(31))) << 28);
    if (op0 != registers[15])
        return 1;
    if (spsr)
        setCpsr(*spsr);
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::sbcs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 - op2 - 1 + ((cpsr & BIT(29)) >> 29);
    cpsr          = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) |
           ((op1 >= *op0 && (op2 != 0xFFFFFFFF || (cpsr & BIT(29)))) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (*op0 & BIT(31)) == (op2 & BIT(31))) << 28);
    if (op0 != registers[15])
        return 1;
    if (spsr)
        setCpsr(*spsr);
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::rscs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op2 - op1 - 1 + ((cpsr & BIT(29)) >> 29);
    cpsr          = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) |
           ((op2 >= *op0 && (op1 != 0xFFFFFFFF || (cpsr & BIT(29)))) << 29) |
           (((op1 & BIT(31)) != (op2 & BIT(31)) && (*op0 & BIT(31)) == (op1 & BIT(31))) << 28);
    if (op0 != registers[15])
        return 1;
    if (spsr)
        setCpsr(*spsr);
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::orrs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 | op2;
    cpsr          = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op0 != registers[15])
        return 1;
    if (spsr)
        setCpsr(*spsr);
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::movs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    *op0          = op2;
    cpsr          = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op0 != registers[15])
        return 1;
    if (spsr)
        setCpsr(*spsr);
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::bics(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 & ~op2;
    cpsr          = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op0 != registers[15])
        return 1;
    if (spsr)
        setCpsr(*spsr);
    flushPipeline();
    return 3;
}

FORCE_INLINE int CPU::mvns(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    *op0          = ~op2;
    cpsr          = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op0 != registers[15])
        return 1;
    if (spsr)
        setCpsr(*spsr);
    flushPipeline();
    return 3;
}

int CPU::andLli(uint32_t opcode) {
    return _and(opcode, lli(opcode));
}

int CPU::andLlr(uint32_t opcode) {
    return _and(opcode, llr(opcode)) + 1;
}

int CPU::andLri(uint32_t opcode) {
    return _and(opcode, lri(opcode));
}

int CPU::andLrr(uint32_t opcode) {
    return _and(opcode, lrr(opcode)) + 1;
}

int CPU::andAri(uint32_t opcode) {
    return _and(opcode, ari(opcode));
}

int CPU::andArr(uint32_t opcode) {
    return _and(opcode, arr(opcode)) + 1;
}

int CPU::andRri(uint32_t opcode) {
    return _and(opcode, rri(opcode));
}

int CPU::andRrr(uint32_t opcode) {
    return _and(opcode, rrr(opcode)) + 1;
}

int CPU::andImm(uint32_t opcode) {
    return _and(opcode, imm(opcode));
}

int CPU::andsLli(uint32_t opcode) {
    return ands(opcode, lliS(opcode));
}

int CPU::andsLlr(uint32_t opcode) {
    return ands(opcode, llrS(opcode)) + 1;
}

int CPU::andsLri(uint32_t opcode) {
    return ands(opcode, lriS(opcode));
}

int CPU::andsLrr(uint32_t opcode) {
    return ands(opcode, lrrS(opcode)) + 1;
}

int CPU::andsAri(uint32_t opcode) {
    return ands(opcode, ariS(opcode));
}

int CPU::andsArr(uint32_t opcode) {
    return ands(opcode, arrS(opcode)) + 1;
}

int CPU::andsRri(uint32_t opcode) {
    return ands(opcode, rriS(opcode));
}

int CPU::andsRrr(uint32_t opcode) {
    return ands(opcode, rrrS(opcode)) + 1;
}

int CPU::andsImm(uint32_t opcode) {
    return ands(opcode, immS(opcode));
}

int CPU::eorLli(uint32_t opcode) {
    return eor(opcode, lli(opcode));
}

int CPU::eorLlr(uint32_t opcode) {
    return eor(opcode, llr(opcode)) + 1;
}

int CPU::eorLri(uint32_t opcode) {
    return eor(opcode, lri(opcode));
}

int CPU::eorLrr(uint32_t opcode) {
    return eor(opcode, lrr(opcode)) + 1;
}

int CPU::eorAri(uint32_t opcode) {
    return eor(opcode, ari(opcode));
}

int CPU::eorArr(uint32_t opcode) {
    return eor(opcode, arr(opcode)) + 1;
}

int CPU::eorRri(uint32_t opcode) {
    return eor(opcode, rri(opcode));
}

int CPU::eorRrr(uint32_t opcode) {
    return eor(opcode, rrr(opcode)) + 1;
}

int CPU::eorImm(uint32_t opcode) {
    return eor(opcode, imm(opcode));
}

int CPU::eorsLli(uint32_t opcode) {
    return eors(opcode, lliS(opcode));
}

int CPU::eorsLlr(uint32_t opcode) {
    return eors(opcode, llrS(opcode)) + 1;
}

int CPU::eorsLri(uint32_t opcode) {
    return eors(opcode, lriS(opcode));
}

int CPU::eorsLrr(uint32_t opcode) {
    return eors(opcode, lrrS(opcode)) + 1;
}

int CPU::eorsAri(uint32_t opcode) {
    return eors(opcode, ariS(opcode));
}

int CPU::eorsArr(uint32_t opcode) {
    return eors(opcode, arrS(opcode)) + 1;
}

int CPU::eorsRri(uint32_t opcode) {
    return eors(opcode, rriS(opcode));
}

int CPU::eorsRrr(uint32_t opcode) {
    return eors(opcode, rrrS(opcode)) + 1;
}

int CPU::eorsImm(uint32_t opcode) {
    return eors(opcode, immS(opcode));
}

int CPU::subLli(uint32_t opcode) {
    return sub(opcode, lli(opcode));
}

int CPU::subLlr(uint32_t opcode) {
    return sub(opcode, llr(opcode)) + 1;
}

int CPU::subLri(uint32_t opcode) {
    return sub(opcode, lri(opcode));
}

int CPU::subLrr(uint32_t opcode) {
    return sub(opcode, lrr(opcode)) + 1;
}

int CPU::subAri(uint32_t opcode) {
    return sub(opcode, ari(opcode));
}

int CPU::subArr(uint32_t opcode) {
    return sub(opcode, arr(opcode)) + 1;
}

int CPU::subRri(uint32_t opcode) {
    return sub(opcode, rri(opcode));
}

int CPU::subRrr(uint32_t opcode) {
    return sub(opcode, rrr(opcode)) + 1;
}

int CPU::subImm(uint32_t opcode) {
    return sub(opcode, imm(opcode));
}

int CPU::subsLli(uint32_t opcode) {
    return subs(opcode, lli(opcode));
}

int CPU::subsLlr(uint32_t opcode) {
    return subs(opcode, llr(opcode)) + 1;
}

int CPU::subsLri(uint32_t opcode) {
    return subs(opcode, lri(opcode));
}

int CPU::subsLrr(uint32_t opcode) {
    return subs(opcode, lrr(opcode)) + 1;
}

int CPU::subsAri(uint32_t opcode) {
    return subs(opcode, ari(opcode));
}

int CPU::subsArr(uint32_t opcode) {
    return subs(opcode, arr(opcode)) + 1;
}

int CPU::subsRri(uint32_t opcode) {
    return subs(opcode, rri(opcode));
}

int CPU::subsRrr(uint32_t opcode) {
    return subs(opcode, rrr(opcode)) + 1;
}

int CPU::subsImm(uint32_t opcode) {
    return subs(opcode, imm(opcode));
}

int CPU::rsbLli(uint32_t opcode) {
    return rsb(opcode, lli(opcode));
}

int CPU::rsbLlr(uint32_t opcode) {
    return rsb(opcode, llr(opcode)) + 1;
}

int CPU::rsbLri(uint32_t opcode) {
    return rsb(opcode, lri(opcode));
}

int CPU::rsbLrr(uint32_t opcode) {
    return rsb(opcode, lrr(opcode)) + 1;
}

int CPU::rsbAri(uint32_t opcode) {
    return rsb(opcode, ari(opcode));
}

int CPU::rsbArr(uint32_t opcode) {
    return rsb(opcode, arr(opcode)) + 1;
}

int CPU::rsbRri(uint32_t opcode) {
    return rsb(opcode, rri(opcode));
}

int CPU::rsbRrr(uint32_t opcode) {
    return rsb(opcode, rrr(opcode)) + 1;
}

int CPU::rsbImm(uint32_t opcode) {
    return rsb(opcode, imm(opcode));
}

int CPU::rsbsLli(uint32_t opcode) {
    return rsbs(opcode, lli(opcode));
}

int CPU::rsbsLlr(uint32_t opcode) {
    return rsbs(opcode, llr(opcode)) + 1;
}

int CPU::rsbsLri(uint32_t opcode) {
    return rsbs(opcode, lri(opcode));
}

int CPU::rsbsLrr(uint32_t opcode) {
    return rsbs(opcode, lrr(opcode)) + 1;
}

int CPU::rsbsAri(uint32_t opcode) {
    return rsbs(opcode, ari(opcode));
}

int CPU::rsbsArr(uint32_t opcode) {
    return rsbs(opcode, arr(opcode)) + 1;
}

int CPU::rsbsRri(uint32_t opcode) {
    return rsbs(opcode, rri(opcode));
}

int CPU::rsbsRrr(uint32_t opcode) {
    return rsbs(opcode, rrr(opcode)) + 1;
}

int CPU::rsbsImm(uint32_t opcode) {
    return rsbs(opcode, imm(opcode));
}

int CPU::addLli(uint32_t opcode) {
    return add(opcode, lli(opcode));
}

int CPU::addLlr(uint32_t opcode) {
    return add(opcode, llr(opcode)) + 1;
}

int CPU::addLri(uint32_t opcode) {
    return add(opcode, lri(opcode));
}

int CPU::addLrr(uint32_t opcode) {
    return add(opcode, lrr(opcode)) + 1;
}

int CPU::addAri(uint32_t opcode) {
    return add(opcode, ari(opcode));
}

int CPU::addArr(uint32_t opcode) {
    return add(opcode, arr(opcode)) + 1;
}

int CPU::addRri(uint32_t opcode) {
    return add(opcode, rri(opcode));
}

int CPU::addRrr(uint32_t opcode) {
    return add(opcode, rrr(opcode)) + 1;
}

int CPU::addImm(uint32_t opcode) {
    return add(opcode, imm(opcode));
}

int CPU::addsLli(uint32_t opcode) {
    return adds(opcode, lli(opcode));
}

int CPU::addsLlr(uint32_t opcode) {
    return adds(opcode, llr(opcode)) + 1;
}

int CPU::addsLri(uint32_t opcode) {
    return adds(opcode, lri(opcode));
}

int CPU::addsLrr(uint32_t opcode) {
    return adds(opcode, lrr(opcode)) + 1;
}

int CPU::addsAri(uint32_t opcode) {
    return adds(opcode, ari(opcode));
}

int CPU::addsArr(uint32_t opcode) {
    return adds(opcode, arr(opcode)) + 1;
}

int CPU::addsRri(uint32_t opcode) {
    return adds(opcode, rri(opcode));
}

int CPU::addsRrr(uint32_t opcode) {
    return adds(opcode, rrr(opcode)) + 1;
}

int CPU::addsImm(uint32_t opcode) {
    return adds(opcode, imm(opcode));
}

int CPU::adcLli(uint32_t opcode) {
    return adc(opcode, lli(opcode));
}

int CPU::adcLlr(uint32_t opcode) {
    return adc(opcode, llr(opcode)) + 1;
}

int CPU::adcLri(uint32_t opcode) {
    return adc(opcode, lri(opcode));
}

int CPU::adcLrr(uint32_t opcode) {
    return adc(opcode, lrr(opcode)) + 1;
}

int CPU::adcAri(uint32_t opcode) {
    return adc(opcode, ari(opcode));
}

int CPU::adcArr(uint32_t opcode) {
    return adc(opcode, arr(opcode)) + 1;
}

int CPU::adcRri(uint32_t opcode) {
    return adc(opcode, rri(opcode));
}

int CPU::adcRrr(uint32_t opcode) {
    return adc(opcode, rrr(opcode)) + 1;
}

int CPU::adcImm(uint32_t opcode) {
    return adc(opcode, imm(opcode));
}

int CPU::adcsLli(uint32_t opcode) {
    return adcs(opcode, lli(opcode));
}

int CPU::adcsLlr(uint32_t opcode) {
    return adcs(opcode, llr(opcode)) + 1;
}

int CPU::adcsLri(uint32_t opcode) {
    return adcs(opcode, lri(opcode));
}

int CPU::adcsLrr(uint32_t opcode) {
    return adcs(opcode, lrr(opcode)) + 1;
}

int CPU::adcsAri(uint32_t opcode) {
    return adcs(opcode, ari(opcode));
}

int CPU::adcsArr(uint32_t opcode) {
    return adcs(opcode, arr(opcode)) + 1;
}

int CPU::adcsRri(uint32_t opcode) {
    return adcs(opcode, rri(opcode));
}

int CPU::adcsRrr(uint32_t opcode) {
    return adcs(opcode, rrr(opcode)) + 1;
}

int CPU::adcsImm(uint32_t opcode) {
    return adcs(opcode, imm(opcode));
}

int CPU::sbcLli(uint32_t opcode) {
    return sbc(opcode, lli(opcode));
}

int CPU::sbcLlr(uint32_t opcode) {
    return sbc(opcode, llr(opcode)) + 1;
}

int CPU::sbcLri(uint32_t opcode) {
    return sbc(opcode, lri(opcode));
}

int CPU::sbcLrr(uint32_t opcode) {
    return sbc(opcode, lrr(opcode)) + 1;
}

int CPU::sbcAri(uint32_t opcode) {
    return sbc(opcode, ari(opcode));
}

int CPU::sbcArr(uint32_t opcode) {
    return sbc(opcode, arr(opcode)) + 1;
}

int CPU::sbcRri(uint32_t opcode) {
    return sbc(opcode, rri(opcode));
}

int CPU::sbcRrr(uint32_t opcode) {
    return sbc(opcode, rrr(opcode)) + 1;
}

int CPU::sbcImm(uint32_t opcode) {
    return sbc(opcode, imm(opcode));
}

int CPU::sbcsLli(uint32_t opcode) {
    return sbcs(opcode, lli(opcode));
}

int CPU::sbcsLlr(uint32_t opcode) {
    return sbcs(opcode, llr(opcode)) + 1;
}

int CPU::sbcsLri(uint32_t opcode) {
    return sbcs(opcode, lri(opcode));
}

int CPU::sbcsLrr(uint32_t opcode) {
    return sbcs(opcode, lrr(opcode)) + 1;
}

int CPU::sbcsAri(uint32_t opcode) {
    return sbcs(opcode, ari(opcode));
}

int CPU::sbcsArr(uint32_t opcode) {
    return sbcs(opcode, arr(opcode)) + 1;
}

int CPU::sbcsRri(uint32_t opcode) {
    return sbcs(opcode, rri(opcode));
}

int CPU::sbcsRrr(uint32_t opcode) {
    return sbcs(opcode, rrr(opcode)) + 1;
}

int CPU::sbcsImm(uint32_t opcode) {
    return sbcs(opcode, imm(opcode));
}

int CPU::rscLli(uint32_t opcode) {
    return rsc(opcode, lli(opcode));
}

int CPU::rscLlr(uint32_t opcode) {
    return rsc(opcode, llr(opcode)) + 1;
}

int CPU::rscLri(uint32_t opcode) {
    return rsc(opcode, lri(opcode));
}

int CPU::rscLrr(uint32_t opcode) {
    return rsc(opcode, lrr(opcode)) + 1;
}

int CPU::rscAri(uint32_t opcode) {
    return rsc(opcode, ari(opcode));
}

int CPU::rscArr(uint32_t opcode) {
    return rsc(opcode, arr(opcode)) + 1;
}

int CPU::rscRri(uint32_t opcode) {
    return rsc(opcode, rri(opcode));
}

int CPU::rscRrr(uint32_t opcode) {
    return rsc(opcode, rrr(opcode)) + 1;
}

int CPU::rscImm(uint32_t opcode) {
    return rsc(opcode, imm(opcode));
}

int CPU::rscsLli(uint32_t opcode) {
    return rscs(opcode, lli(opcode));
}

int CPU::rscsLlr(uint32_t opcode) {
    return rscs(opcode, llr(opcode)) + 1;
}

int CPU::rscsLri(uint32_t opcode) {
    return rscs(opcode, lri(opcode));
}

int CPU::rscsLrr(uint32_t opcode) {
    return rscs(opcode, lrr(opcode)) + 1;
}

int CPU::rscsAri(uint32_t opcode) {
    return rscs(opcode, ari(opcode));
}

int CPU::rscsArr(uint32_t opcode) {
    return rscs(opcode, arr(opcode)) + 1;
}

int CPU::rscsRri(uint32_t opcode) {
    return rscs(opcode, rri(opcode));
}

int CPU::rscsRrr(uint32_t opcode) {
    return rscs(opcode, rrr(opcode)) + 1;
}

int CPU::rscsImm(uint32_t opcode) {
    return rscs(opcode, imm(opcode));
}

int CPU::tstLli(uint32_t opcode) {
    return tst(opcode, lliS(opcode));
}

int CPU::tstLlr(uint32_t opcode) {
    return tst(opcode, llrS(opcode)) + 1;
}

int CPU::tstLri(uint32_t opcode) {
    return tst(opcode, lriS(opcode));
}

int CPU::tstLrr(uint32_t opcode) {
    return tst(opcode, lrrS(opcode)) + 1;
}

int CPU::tstAri(uint32_t opcode) {
    return tst(opcode, ariS(opcode));
}

int CPU::tstArr(uint32_t opcode) {
    return tst(opcode, arrS(opcode)) + 1;
}

int CPU::tstRri(uint32_t opcode) {
    return tst(opcode, rriS(opcode));
}

int CPU::tstRrr(uint32_t opcode) {
    return tst(opcode, rrrS(opcode)) + 1;
}

int CPU::tstImm(uint32_t opcode) {
    return tst(opcode, immS(opcode));
}

int CPU::teqLli(uint32_t opcode) {
    return teq(opcode, lliS(opcode));
}

int CPU::teqLlr(uint32_t opcode) {
    return teq(opcode, llrS(opcode)) + 1;
}

int CPU::teqLri(uint32_t opcode) {
    return teq(opcode, lriS(opcode));
}

int CPU::teqLrr(uint32_t opcode) {
    return teq(opcode, lrrS(opcode)) + 1;
}

int CPU::teqAri(uint32_t opcode) {
    return teq(opcode, ariS(opcode));
}

int CPU::teqArr(uint32_t opcode) {
    return teq(opcode, arrS(opcode)) + 1;
}

int CPU::teqRri(uint32_t opcode) {
    return teq(opcode, rriS(opcode));
}

int CPU::teqRrr(uint32_t opcode) {
    return teq(opcode, rrrS(opcode)) + 1;
}

int CPU::teqImm(uint32_t opcode) {
    return teq(opcode, immS(opcode));
}

int CPU::cmpLli(uint32_t opcode) {
    return cmp(opcode, lliS(opcode));
}

int CPU::cmpLlr(uint32_t opcode) {
    return cmp(opcode, llrS(opcode)) + 1;
}

int CPU::cmpLri(uint32_t opcode) {
    return cmp(opcode, lriS(opcode));
}

int CPU::cmpLrr(uint32_t opcode) {
    return cmp(opcode, lrrS(opcode)) + 1;
}

int CPU::cmpAri(uint32_t opcode) {
    return cmp(opcode, ariS(opcode));
}

int CPU::cmpArr(uint32_t opcode) {
    return cmp(opcode, arrS(opcode)) + 1;
}

int CPU::cmpRri(uint32_t opcode) {
    return cmp(opcode, rriS(opcode));
}

int CPU::cmpRrr(uint32_t opcode) {
    return cmp(opcode, rrrS(opcode)) + 1;
}

int CPU::cmpImm(uint32_t opcode) {
    return cmp(opcode, immS(opcode));
}

int CPU::cmnLli(uint32_t opcode) {
    return cmn(opcode, lliS(opcode));
}

int CPU::cmnLlr(uint32_t opcode) {
    return cmn(opcode, llrS(opcode)) + 1;
}

int CPU::cmnLri(uint32_t opcode) {
    return cmn(opcode, lriS(opcode));
}

int CPU::cmnLrr(uint32_t opcode) {
    return cmn(opcode, lrrS(opcode)) + 1;
}

int CPU::cmnAri(uint32_t opcode) {
    return cmn(opcode, ariS(opcode));
}

int CPU::cmnArr(uint32_t opcode) {
    return cmn(opcode, arrS(opcode)) + 1;
}

int CPU::cmnRri(uint32_t opcode) {
    return cmn(opcode, rriS(opcode));
}

int CPU::cmnRrr(uint32_t opcode) {
    return cmn(opcode, rrrS(opcode)) + 1;
}

int CPU::cmnImm(uint32_t opcode) {
    return cmn(opcode, immS(opcode));
}

int CPU::orrLli(uint32_t opcode) {
    return orr(opcode, lli(opcode));
}

int CPU::orrLlr(uint32_t opcode) {
    return orr(opcode, llr(opcode)) + 1;
}

int CPU::orrLri(uint32_t opcode) {
    return orr(opcode, lri(opcode));
}

int CPU::orrLrr(uint32_t opcode) {
    return orr(opcode, lrr(opcode)) + 1;
}

int CPU::orrAri(uint32_t opcode) {
    return orr(opcode, ari(opcode));
}

int CPU::orrArr(uint32_t opcode) {
    return orr(opcode, arr(opcode)) + 1;
}

int CPU::orrRri(uint32_t opcode) {
    return orr(opcode, rri(opcode));
}

int CPU::orrRrr(uint32_t opcode) {
    return orr(opcode, rrr(opcode)) + 1;
}

int CPU::orrImm(uint32_t opcode) {
    return orr(opcode, imm(opcode));
}

int CPU::orrsLli(uint32_t opcode) {
    return orrs(opcode, lliS(opcode));
}

int CPU::orrsLlr(uint32_t opcode) {
    return orrs(opcode, llrS(opcode)) + 1;
}

int CPU::orrsLri(uint32_t opcode) {
    return orrs(opcode, lriS(opcode));
}

int CPU::orrsLrr(uint32_t opcode) {
    return orrs(opcode, lrrS(opcode)) + 1;
}

int CPU::orrsAri(uint32_t opcode) {
    return orrs(opcode, ariS(opcode));
}

int CPU::orrsArr(uint32_t opcode) {
    return orrs(opcode, arrS(opcode)) + 1;
}

int CPU::orrsRri(uint32_t opcode) {
    return orrs(opcode, rriS(opcode));
}

int CPU::orrsRrr(uint32_t opcode) {
    return orrs(opcode, rrrS(opcode)) + 1;
}

int CPU::orrsImm(uint32_t opcode) {
    return orrs(opcode, immS(opcode));
}

int CPU::movLli(uint32_t opcode) {
    return mov(opcode, lli(opcode));
}

int CPU::movLlr(uint32_t opcode) {
    return mov(opcode, llr(opcode)) + 1;
}

int CPU::movLri(uint32_t opcode) {
    return mov(opcode, lri(opcode));
}

int CPU::movLrr(uint32_t opcode) {
    return mov(opcode, lrr(opcode)) + 1;
}

int CPU::movAri(uint32_t opcode) {
    return mov(opcode, ari(opcode));
}

int CPU::movArr(uint32_t opcode) {
    return mov(opcode, arr(opcode)) + 1;
}

int CPU::movRri(uint32_t opcode) {
    return mov(opcode, rri(opcode));
}

int CPU::movRrr(uint32_t opcode) {
    return mov(opcode, rrr(opcode)) + 1;
}

int CPU::movImm(uint32_t opcode) {
    return mov(opcode, imm(opcode));
}

int CPU::movsLli(uint32_t opcode) {
    return movs(opcode, lliS(opcode));
}

int CPU::movsLlr(uint32_t opcode) {
    return movs(opcode, llrS(opcode)) + 1;
}

int CPU::movsLri(uint32_t opcode) {
    return movs(opcode, lriS(opcode));
}

int CPU::movsLrr(uint32_t opcode) {
    return movs(opcode, lrrS(opcode)) + 1;
}

int CPU::movsAri(uint32_t opcode) {
    return movs(opcode, ariS(opcode));
}

int CPU::movsArr(uint32_t opcode) {
    return movs(opcode, arrS(opcode)) + 1;
}

int CPU::movsRri(uint32_t opcode) {
    return movs(opcode, rriS(opcode));
}

int CPU::movsRrr(uint32_t opcode) {
    return movs(opcode, rrrS(opcode)) + 1;
}

int CPU::movsImm(uint32_t opcode) {
    return movs(opcode, immS(opcode));
}

int CPU::bicLli(uint32_t opcode) {
    return bic(opcode, lli(opcode));
}

int CPU::bicLlr(uint32_t opcode) {
    return bic(opcode, llr(opcode)) + 1;
}

int CPU::bicLri(uint32_t opcode) {
    return bic(opcode, lri(opcode));
}

int CPU::bicLrr(uint32_t opcode) {
    return bic(opcode, lrr(opcode)) + 1;
}

int CPU::bicAri(uint32_t opcode) {
    return bic(opcode, ari(opcode));
}

int CPU::bicArr(uint32_t opcode) {
    return bic(opcode, arr(opcode)) + 1;
}

int CPU::bicRri(uint32_t opcode) {
    return bic(opcode, rri(opcode));
}

int CPU::bicRrr(uint32_t opcode) {
    return bic(opcode, rrr(opcode)) + 1;
}

int CPU::bicImm(uint32_t opcode) {
    return bic(opcode, imm(opcode));
}

int CPU::bicsLli(uint32_t opcode) {
    return bics(opcode, lliS(opcode));
}

int CPU::bicsLlr(uint32_t opcode) {
    return bics(opcode, llrS(opcode)) + 1;
}

int CPU::bicsLri(uint32_t opcode) {
    return bics(opcode, lriS(opcode));
}

int CPU::bicsLrr(uint32_t opcode) {
    return bics(opcode, lrrS(opcode)) + 1;
}

int CPU::bicsAri(uint32_t opcode) {
    return bics(opcode, ariS(opcode));
}

int CPU::bicsArr(uint32_t opcode) {
    return bics(opcode, arrS(opcode)) + 1;
}

int CPU::bicsRri(uint32_t opcode) {
    return bics(opcode, rriS(opcode));
}

int CPU::bicsRrr(uint32_t opcode) {
    return bics(opcode, rrrS(opcode)) + 1;
}

int CPU::bicsImm(uint32_t opcode) {
    return bics(opcode, immS(opcode));
}

int CPU::mvnLli(uint32_t opcode) {
    return mvn(opcode, lli(opcode));
}

int CPU::mvnLlr(uint32_t opcode) {
    return mvn(opcode, llr(opcode)) + 1;
}

int CPU::mvnLri(uint32_t opcode) {
    return mvn(opcode, lri(opcode));
}

int CPU::mvnLrr(uint32_t opcode) {
    return mvn(opcode, lrr(opcode)) + 1;
}

int CPU::mvnAri(uint32_t opcode) {
    return mvn(opcode, ari(opcode));
}

int CPU::mvnArr(uint32_t opcode) {
    return mvn(opcode, arr(opcode)) + 1;
}

int CPU::mvnRri(uint32_t opcode) {
    return mvn(opcode, rri(opcode));
}

int CPU::mvnRrr(uint32_t opcode) {
    return mvn(opcode, rrr(opcode)) + 1;
}

int CPU::mvnImm(uint32_t opcode) {
    return mvn(opcode, imm(opcode));
}

int CPU::mvnsLli(uint32_t opcode) {
    return mvns(opcode, lliS(opcode));
}

int CPU::mvnsLlr(uint32_t opcode) {
    return mvns(opcode, llrS(opcode)) + 1;
}

int CPU::mvnsLri(uint32_t opcode) {
    return mvns(opcode, lriS(opcode));
}

int CPU::mvnsLrr(uint32_t opcode) {
    return mvns(opcode, lrrS(opcode)) + 1;
}

int CPU::mvnsAri(uint32_t opcode) {
    return mvns(opcode, ariS(opcode));
}

int CPU::mvnsArr(uint32_t opcode) {
    return mvns(opcode, arrS(opcode)) + 1;
}

int CPU::mvnsRri(uint32_t opcode) {
    return mvns(opcode, rriS(opcode));
}

int CPU::mvnsRrr(uint32_t opcode) {
    return mvns(opcode, rrrS(opcode)) + 1;
}

int CPU::mvnsImm(uint32_t opcode) {
    return mvns(opcode, immS(opcode));
}

int CPU::mul(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x000F0000) >> 16];
    uint32_t  op1 = *registers[opcode & 0x0000000F];
    int32_t   op2 = *registers[(opcode & 0x00000F00) >> 8];
    *op0          = op1 * op2;
    if (cpu == 0)
        return 2;
    int m;
    // `m < 4` testé EN PREMIER : sinon la condition évaluait `1 << 32` (comportement
    // indéfini, relevé par UBSan) avant de constater que la boucle devait s'arrêter.
    for (m = 1; m < 4 && (op2 < (-1 << (m * 8)) || op2 >= (1 << (m * 8))); m++)
        ;
    return m + 1;
}

int CPU::mla(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x000F0000) >> 16];
    uint32_t  op1 = *registers[opcode & 0x0000000F];
    int32_t   op2 = *registers[(opcode & 0x00000F00) >> 8];
    uint32_t  op3 = *registers[(opcode & 0x0000F000) >> 12];
    *op0          = op1 * op2 + op3;
    if (cpu == 0)
        return 2;
    int m;
    for (m = 1; m < 4 && (op2 < (-1 << (m * 8)) || op2 >= (1 << (m * 8))); m++)
        ;
    return m + 2;
}

int CPU::umull(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    uint32_t  op2 = *registers[opcode & 0x0000000F];
    int32_t   op3 = *registers[(opcode & 0x00000F00) >> 8];
    uint64_t  res = (uint64_t)op2 * (uint32_t)op3;
    *op1          = res >> 32;
    *op0          = res;
    if (cpu == 0)
        return 3;
    int m;
    for (m = 1; m < 4 && (op3 < (-1 << (m * 8)) || op3 >= (1 << (m * 8))); m++)
        ;
    return m + 2;
}

int CPU::umlal(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    uint32_t  op2 = *registers[opcode & 0x0000000F];
    int32_t   op3 = *registers[(opcode & 0x00000F00) >> 8];
    uint64_t  res = (uint64_t)op2 * (uint32_t)op3;
    res += ((uint64_t)*op1 << 32) | *op0;
    *op1 = res >> 32;
    *op0 = res;
    if (cpu == 0)
        return 3;
    int m;
    for (m = 1; m < 4 && (op3 < (-1 << (m * 8)) || op3 >= (1 << (m * 8))); m++)
        ;
    return m + 3;
}

int CPU::smull(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    int32_t   op2 = *registers[opcode & 0x0000000F];
    int32_t   op3 = *registers[(opcode & 0x00000F00) >> 8];
    int64_t   res = (int64_t)op2 * op3;
    *op1          = res >> 32;
    *op0          = res;
    if (cpu == 0)
        return 3;
    int m;
    for (m = 1; m < 4 && (op3 < (-1 << (m * 8)) || op3 >= (1 << (m * 8))); m++)
        ;
    return m + 2;
}

int CPU::smlal(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    int32_t   op2 = *registers[opcode & 0x0000000F];
    int32_t   op3 = *registers[(opcode & 0x00000F00) >> 8];
    int64_t   res = (int64_t)op2 * op3;
    res += ((int64_t)*op1 << 32) | *op0;
    *op1 = res >> 32;
    *op0 = res;
    if (cpu == 0)
        return 3;
    int m;
    for (m = 1; m < 4 && (op3 < (-1 << (m * 8)) || op3 >= (1 << (m * 8))); m++)
        ;
    return m + 3;
}

int CPU::muls(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x000F0000) >> 16];
    uint32_t  op1 = *registers[opcode & 0x0000000F];
    int32_t   op2 = *registers[(opcode & 0x00000F00) >> 8];
    *op0          = op1 * op2;
    cpsr          = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (cpu == 0)
        return 4;
    int m;
    for (m = 1; m < 4 && (op2 < (-1 << (m * 8)) || op2 >= (1 << (m * 8))); m++)
        ;
    return m + 1;
}

int CPU::mlas(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x000F0000) >> 16];
    uint32_t  op1 = *registers[opcode & 0x0000000F];
    int32_t   op2 = *registers[(opcode & 0x00000F00) >> 8];
    uint32_t  op3 = *registers[(opcode & 0x0000F000) >> 12];
    *op0          = op1 * op2 + op3;
    cpsr          = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (cpu == 0)
        return 4;
    int m;
    for (m = 1; m < 4 && (op2 < (-1 << (m * 8)) || op2 >= (1 << (m * 8))); m++)
        ;
    return m + 2;
}

int CPU::umulls(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    uint32_t  op2 = *registers[opcode & 0x0000000F];
    int32_t   op3 = *registers[(opcode & 0x00000F00) >> 8];
    uint64_t  res = (uint64_t)op2 * (uint32_t)op3;
    *op1          = res >> 32;
    *op0          = res;
    cpsr          = (cpsr & ~0xC0000000) | (*op1 & BIT(31)) | ((*op1 == 0) << 30);
    if (cpu == 0)
        return 5;
    int m;
    for (m = 1; m < 4 && (op3 < (-1 << (m * 8)) || op3 >= (1 << (m * 8))); m++)
        ;
    return m + 2;
}

int CPU::umlals(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    uint32_t  op2 = *registers[opcode & 0x0000000F];
    int32_t   op3 = *registers[(opcode & 0x00000F00) >> 8];
    uint64_t  res = (uint64_t)op2 * (uint32_t)op3;
    res += ((uint64_t)*op1 << 32) | *op0;
    *op1 = res >> 32;
    *op0 = res;
    cpsr = (cpsr & ~0xC0000000) | (*op1 & BIT(31)) | ((*op1 == 0) << 30);
    if (cpu == 0)
        return 5;
    int m;
    for (m = 1; m < 4 && (op3 < (-1 << (m * 8)) || op3 >= (1 << (m * 8))); m++)
        ;
    return m + 3;
}

int CPU::smulls(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    int32_t   op2 = *registers[opcode & 0x0000000F];
    int32_t   op3 = *registers[(opcode & 0x00000F00) >> 8];
    int64_t   res = (int64_t)op2 * op3;
    *op1          = res >> 32;
    *op0          = res;
    cpsr          = (cpsr & ~0xC0000000) | (*op1 & BIT(31)) | ((*op1 == 0) << 30);
    if (cpu == 0)
        return 5;
    int m;
    for (m = 1; m < 4 && (op3 < (-1 << (m * 8)) || op3 >= (1 << (m * 8))); m++)
        ;
    return m + 2;
}

int CPU::smlals(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    int32_t   op2 = *registers[opcode & 0x0000000F];
    int32_t   op3 = *registers[(opcode & 0x00000F00) >> 8];
    int64_t   res = (int64_t)op2 * op3;
    res += ((int64_t)*op1 << 32) | *op0;
    *op1 = res >> 32;
    *op0 = res;
    cpsr = (cpsr & ~0xC0000000) | (*op1 & BIT(31)) | ((*op1 == 0) << 30);
    if (cpu == 0)
        return 5;
    int m;
    for (m = 1; m < 4 && (op3 < (-1 << (m * 8)) || op3 >= (1 << (m * 8))); m++)
        ;
    return m + 3;
}

int CPU::smulbb(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x000F0000) >> 16];
    int16_t   op1 = *registers[opcode & 0x0000000F];
    int16_t   op2 = *registers[(opcode & 0x00000F00) >> 8];
    *op0          = op1 * op2;
    return 1;
}

int CPU::smulbt(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x000F0000) >> 16];
    int16_t   op1 = *registers[opcode & 0x0000000F];
    int16_t   op2 = *registers[(opcode & 0x00000F00) >> 8] >> 16;
    *op0          = op1 * op2;
    return 1;
}

int CPU::smultb(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x000F0000) >> 16];
    int16_t   op1 = *registers[opcode & 0x0000000F] >> 16;
    int16_t   op2 = *registers[(opcode & 0x00000F00) >> 8];
    *op0          = op1 * op2;
    return 1;
}

int CPU::smultt(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x000F0000) >> 16];
    int16_t   op1 = *registers[opcode & 0x0000000F] >> 16;
    int16_t   op2 = *registers[(opcode & 0x00000F00) >> 8] >> 16;
    *op0          = op1 * op2;
    return 1;
}

int CPU::smulwb(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x000F0000) >> 16];
    int32_t   op1 = *registers[opcode & 0x0000000F];
    int16_t   op2 = *registers[(opcode & 0x00000F00) >> 8];
    *op0          = ((int64_t)op1 * op2) >> 16;
    return 1;
}

int CPU::smulwt(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x000F0000) >> 16];
    int32_t   op1 = *registers[opcode & 0x0000000F];
    int16_t   op2 = *registers[(opcode & 0x00000F00) >> 8] >> 16;
    *op0          = ((int64_t)op1 * op2) >> 16;
    return 1;
}

int CPU::smlabb(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x000F0000) >> 16];
    int16_t   op1 = *registers[opcode & 0x0000000F];
    int16_t   op2 = *registers[(opcode & 0x00000F00) >> 8];
    uint32_t  op3 = *registers[(opcode & 0x0000F000) >> 12];
    uint32_t  res = op1 * op2;
    *op0          = res + op3;
    if ((*op0 & BIT(31)) != (res & BIT(31)))
        cpsr |= BIT(27);
    return 1;
}

int CPU::smlabt(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x000F0000) >> 16];
    int16_t   op1 = *registers[opcode & 0x0000000F];
    int16_t   op2 = *registers[(opcode & 0x00000F00) >> 8] >> 16;
    uint32_t  op3 = *registers[(opcode & 0x0000F000) >> 12];
    uint32_t  res = op1 * op2;
    *op0          = res + op3;
    if ((*op0 & BIT(31)) != (res & BIT(31)))
        cpsr |= BIT(27);
    return 1;
}

int CPU::smlatb(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x000F0000) >> 16];
    int16_t   op1 = *registers[opcode & 0x0000000F] >> 16;
    int16_t   op2 = *registers[(opcode & 0x00000F00) >> 8];
    uint32_t  op3 = *registers[(opcode & 0x0000F000) >> 12];
    uint32_t  res = op1 * op2;
    *op0          = res + op3;
    if ((*op0 & BIT(31)) != (res & BIT(31)))
        cpsr |= BIT(27);
    return 1;
}

int CPU::smlatt(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x000F0000) >> 16];
    int16_t   op1 = *registers[opcode & 0x0000000F] >> 16;
    int16_t   op2 = *registers[(opcode & 0x00000F00) >> 8] >> 16;
    uint32_t  op3 = *registers[(opcode & 0x0000F000) >> 12];
    uint32_t  res = op1 * op2;
    *op0          = res + op3;
    if ((*op0 & BIT(31)) != (res & BIT(31)))
        cpsr |= BIT(27);
    return 1;
}

int CPU::smlawb(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x000F0000) >> 16];
    int32_t   op1 = *registers[opcode & 0x0000000F];
    int16_t   op2 = *registers[(opcode & 0x00000F00) >> 8];
    uint32_t  op3 = *registers[(opcode & 0x0000F000) >> 12];
    uint32_t  res = ((int64_t)op1 * op2) >> 16;
    *op0          = res + op3;
    if ((*op0 & BIT(31)) != (res & BIT(31)))
        cpsr |= BIT(27);
    return 1;
}

int CPU::smlawt(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x000F0000) >> 16];
    int32_t   op1 = *registers[opcode & 0x0000000F];
    int16_t   op2 = *registers[(opcode & 0x00000F00) >> 8] >> 16;
    uint32_t  op3 = *registers[(opcode & 0x0000F000) >> 12];
    uint32_t  res = ((int64_t)op1 * op2) >> 16;
    *op0          = res + op3;
    if ((*op0 & BIT(31)) != (res & BIT(31)))
        cpsr |= BIT(27);
    return 1;
}

int CPU::smlalbb(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    int16_t   op2 = *registers[opcode & 0x0000000F];
    int16_t   op3 = *registers[(opcode & 0x00000F00) >> 8];
    int64_t   res = ((int64_t)*op1 << 32) | *op0;
    res += op2 * op3;
    *op1 = res >> 32;
    *op0 = res;
    return 2;
}

int CPU::smlalbt(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    int16_t   op2 = *registers[opcode & 0x0000000F];
    int16_t   op3 = *registers[(opcode & 0x00000F00) >> 8] >> 16;
    int64_t   res = ((int64_t)*op1 << 32) | *op0;
    res += op2 * op3;
    *op1 = res >> 32;
    *op0 = res;
    return 2;
}

int CPU::smlaltb(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    int16_t   op2 = *registers[opcode & 0x0000000F] >> 16;
    int16_t   op3 = *registers[(opcode & 0x00000F00) >> 8];
    int64_t   res = ((int64_t)*op1 << 32) | *op0;
    res += op2 * op3;
    *op1 = res >> 32;
    *op0 = res;
    return 2;
}

int CPU::smlaltt(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = registers[(opcode & 0x000F0000) >> 16];
    int16_t   op2 = *registers[opcode & 0x0000000F] >> 16;
    int16_t   op3 = *registers[(opcode & 0x00000F00) >> 8] >> 16;
    int64_t   res = ((int64_t)*op1 << 32) | *op0;
    res += op2 * op3;
    *op1 = res >> 32;
    *op0 = res;
    return 2;
}

int CPU::qadd(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    int32_t   op1 = *registers[opcode & 0x0000000F];
    int32_t   op2 = *registers[(opcode & 0x000F0000) >> 16];
    int64_t   res = (int64_t)op1 + op2;
    if (res > 0x7FFFFFFF) {
        res = 0x7FFFFFFF;
        cpsr |= BIT(27);
    } else if (res < -0x80000000) {
        res = -0x80000000;
        cpsr |= BIT(27);
    }
    *op0 = res;
    return 1;
}

int CPU::qsub(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    int32_t   op1 = *registers[opcode & 0x0000000F];
    int32_t   op2 = *registers[(opcode & 0x000F0000) >> 16];
    int64_t   res = (int64_t)op1 - op2;
    if (res > 0x7FFFFFFF) {
        res = 0x7FFFFFFF;
        cpsr |= BIT(27);
    } else if (res < -0x80000000) {
        res = -0x80000000;
        cpsr |= BIT(27);
    }
    *op0 = res;
    return 1;
}

int CPU::qdadd(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    int32_t   op1 = *registers[opcode & 0x0000000F];
    int32_t   op2 = *registers[(opcode & 0x000F0000) >> 16];
    int64_t   res = (int64_t)op2 * 2;
    if (res > 0x7FFFFFFF) {
        res = 0x7FFFFFFF;
        cpsr |= BIT(27);
    } else if (res < -0x80000000) {
        res = -0x80000000;
        cpsr |= BIT(27);
    }
    res += op1;
    if (res > 0x7FFFFFFF) {
        res = 0x7FFFFFFF;
        cpsr |= BIT(27);
    } else if (res < -0x80000000) {
        res = -0x80000000;
        cpsr |= BIT(27);
    }
    *op0 = res;
    return 1;
}

int CPU::qdsub(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0 = registers[(opcode & 0x0000F000) >> 12];
    int32_t   op1 = *registers[opcode & 0x0000000F];
    int32_t   op2 = *registers[(opcode & 0x000F0000) >> 16];
    int64_t   res = (int64_t)op2 * 2;
    if (res > 0x7FFFFFFF) {
        res = 0x7FFFFFFF;
        cpsr |= BIT(27);
    } else if (res < -0x80000000) {
        res = -0x80000000;
        cpsr |= BIT(27);
    }
    res -= op1;
    if (res > 0x7FFFFFFF) {
        res = 0x7FFFFFFF;
        cpsr |= BIT(27);
    } else if (res < -0x80000000) {
        res = -0x80000000;
        cpsr |= BIT(27);
    }
    *op0 = res;
    return 1;
}

int CPU::clz(uint32_t opcode) {
    if (cpu == 1)
        return 1;
    uint32_t *op0   = registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1   = *registers[opcode & 0x0000000F];
    int       count = 0;
    while (op1 != 0) {
        op1 >>= 1;
        count++;
    }
    *op0 = 32 - count;
    return 1;
}

int CPU::addRegT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = *registers[(opcode & 0x01C0) >> 6];
    *op0          = op1 + op2;
    cpsr          = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 > *op0) << 29) |
           (((op2 & BIT(31)) == (op1 & BIT(31)) && (*op0 & BIT(31)) != (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::subRegT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = *registers[(opcode & 0x01C0) >> 6];
    *op0          = op1 - op2;
    cpsr          = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 >= *op0) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (*op0 & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::addHT(uint16_t opcode) {
    uint32_t *op0 = registers[((opcode & 0x0080) >> 4) | (opcode & 0x0007)];
    uint32_t  op2 = *registers[(opcode & 0x0078) >> 3];
    *op0 += op2;
    if (op0 != registers[15])
        return 1;
    flushPipeline();
    return 3;
}

int CPU::cmpHT(uint16_t opcode) {
    uint32_t op1 = *registers[((opcode & 0x0080) >> 4) | (opcode & 0x0007)];
    uint32_t op2 = *registers[(opcode & 0x0078) >> 3];
    uint32_t res = op1 - op2;
    cpsr         = (cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) | ((op1 >= res) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (res & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::movHT(uint16_t opcode) {
    uint32_t *op0 = registers[((opcode & 0x0080) >> 4) | (opcode & 0x0007)];
    uint32_t  op2 = *registers[(opcode & 0x0078) >> 3];
    *op0          = op2;
    if (op0 != registers[15])
        return 1;
    flushPipeline();
    return 3;
}

int CPU::addPcT(uint16_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0700) >> 8];
    uint32_t  op1 = *registers[15] & ~3;
    uint32_t  op2 = (opcode & 0x00FF) << 2;
    *op0          = op1 + op2;
    return 1;
}

int CPU::addSpT(uint16_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0700) >> 8];
    uint32_t  op1 = *registers[13];
    uint32_t  op2 = (opcode & 0x00FF) << 2;
    *op0          = op1 + op2;
    return 1;
}

int CPU::addSpImmT(uint16_t opcode) {
    uint32_t *op0 = registers[13];
    uint32_t  op2 = ((opcode & BIT(7)) ? (0 - (opcode & 0x007F)) : (opcode & 0x007F)) << 2;
    *op0 += op2;
    return 1;
}

int CPU::lslImmT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[(opcode & 0x0038) >> 3];
    uint8_t   op2 = (opcode & 0x07C0) >> 6;
    *op0          = op1 << op2;
    cpsr          = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op2 > 0)
        cpsr = (cpsr & ~BIT(29)) | ((bool)(op1 & BIT(32 - op2)) << 29);
    return 1;
}

int CPU::lsrImmT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[(opcode & 0x0038) >> 3];
    uint8_t   op2 = (opcode & 0x07C0) >> 6;
    *op0          = op2 ? (op1 >> op2) : 0;
    cpsr =
        (cpsr & ~0xE0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((bool)(op1 & BIT(op2 ? (op2 - 1) : 31)) << 29);
    return 1;
}

int CPU::asrImmT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[(opcode & 0x0038) >> 3];
    uint8_t   op2 = (opcode & 0x07C0) >> 6;
    *op0          = op2 ? ((int32_t)op1 >> op2) : ((op1 & BIT(31)) ? 0xFFFFFFFF : 0);
    cpsr =
        (cpsr & ~0xE0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((bool)(op1 & BIT(op2 ? (op2 - 1) : 31)) << 29);
    return 1;
}

int CPU::addImm3T(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = (opcode & 0x01C0) >> 6;
    *op0          = op1 + op2;
    cpsr          = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 > *op0) << 29) |
           (((op2 & BIT(31)) == (op1 & BIT(31)) && (*op0 & BIT(31)) != (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::subImm3T(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = (opcode & 0x01C0) >> 6;
    *op0          = op1 - op2;
    cpsr          = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 >= *op0) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (*op0 & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::addImm8T(uint16_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0700) >> 8];
    uint32_t  op1 = *registers[(opcode & 0x0700) >> 8];
    uint32_t  op2 = opcode & 0x00FF;
    *op0 += op2;
    cpsr = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 > *op0) << 29) |
           (((op2 & BIT(31)) == (op1 & BIT(31)) && (*op0 & BIT(31)) != (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::subImm8T(uint16_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0700) >> 8];
    uint32_t  op1 = *registers[(opcode & 0x0700) >> 8];
    uint32_t  op2 = opcode & 0x00FF;
    *op0 -= op2;
    cpsr = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 >= *op0) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (*op0 & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::cmpImm8T(uint16_t opcode) {
    uint32_t op1 = *registers[(opcode & 0x0700) >> 8];
    uint32_t op2 = opcode & 0x00FF;
    uint32_t res = op1 - op2;
    cpsr         = (cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) | ((op1 >= res) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (res & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::movImm8T(uint16_t opcode) {
    uint32_t *op0 = registers[(opcode & 0x0700) >> 8];
    uint32_t  op2 = opcode & 0x00FF;
    *op0          = op2;
    cpsr          = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    return 1;
}

int CPU::lslDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[opcode & 0x0007];
    uint8_t   op2 = *registers[(opcode & 0x0038) >> 3];
    *op0          = (op2 < 32) ? (*op0 << op2) : 0;
    cpsr          = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op2 > 0)
        cpsr = (cpsr & ~BIT(29)) | ((op2 <= 32 && (op1 & BIT(32 - op2))) << 29);
    return 1;
}

int CPU::lsrDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[opcode & 0x0007];
    uint8_t   op2 = *registers[(opcode & 0x0038) >> 3];
    *op0          = (op2 < 32) ? (*op0 >> op2) : 0;
    cpsr          = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op2 > 0)
        cpsr = (cpsr & ~BIT(29)) | ((op2 <= 32 && (op1 & BIT(op2 - 1))) << 29);
    return 1;
}

int CPU::asrDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[opcode & 0x0007];
    uint8_t   op2 = *registers[(opcode & 0x0038) >> 3];
    *op0          = (op2 < 32) ? ((int32_t)(*op0) >> op2) : ((*op0 & BIT(31)) ? 0xFFFFFFFF : 0);
    cpsr          = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op2 > 0)
        cpsr = (cpsr & ~BIT(29)) | ((bool)(op1 & BIT((op2 <= 32) ? (op2 - 1) : 31)) << 29);
    return 1;
}

int CPU::rorDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[opcode & 0x0007];
    uint8_t   op2 = *registers[(opcode & 0x0038) >> 3];
    *op0          = (*op0 << (32 - op2 % 32)) | (*op0 >> (op2 % 32));
    cpsr          = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op2 > 0)
        cpsr = (cpsr & ~BIT(29)) | ((bool)(op1 & BIT((op2 - 1) % 32)) << 29);
    return 1;
}

int CPU::andDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op2 = *registers[(opcode & 0x0038) >> 3];
    *op0 &= op2;
    cpsr = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    return 1;
}

int CPU::eorDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op2 = *registers[(opcode & 0x0038) >> 3];
    *op0 ^= op2;
    cpsr = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    return 1;
}

int CPU::adcDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[opcode & 0x0007];
    uint32_t  op2 = *registers[(opcode & 0x0038) >> 3];
    *op0 += op2 + ((cpsr & BIT(29)) >> 29);
    cpsr = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) |
           ((op1 > *op0 || (op2 == 0xFFFFFFFF && (cpsr & BIT(29)))) << 29) |
           (((op2 & BIT(31)) == (op1 & BIT(31)) && (*op0 & BIT(31)) != (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::sbcDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[opcode & 0x0007];
    uint32_t  op2 = *registers[(opcode & 0x0038) >> 3];
    *op0          = op1 - op2 - 1 + ((cpsr & BIT(29)) >> 29);
    cpsr          = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) |
           ((op1 >= *op0 && (op2 != 0xFFFFFFFF || (cpsr & BIT(29)))) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (*op0 & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::tstDpT(uint16_t opcode) {
    uint32_t op1 = *registers[opcode & 0x0007];
    uint32_t op2 = *registers[(opcode & 0x0038) >> 3];
    uint32_t res = op1 & op2;
    cpsr         = (cpsr & ~0xC0000000) | (res & BIT(31)) | ((res == 0) << 30);
    return 1;
}

int CPU::cmpDpT(uint16_t opcode) {
    uint32_t op1 = *registers[opcode & 0x0007];
    uint32_t op2 = *registers[(opcode & 0x0038) >> 3];
    uint32_t res = op1 - op2;
    cpsr         = (cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) | ((op1 >= res) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (res & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::cmnDpT(uint16_t opcode) {
    uint32_t op1 = *registers[opcode & 0x0007];
    uint32_t op2 = *registers[(opcode & 0x0038) >> 3];
    uint32_t res = op1 + op2;
    cpsr         = (cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) | ((op1 > res) << 29) |
           (((op2 & BIT(31)) == (op1 & BIT(31)) && (res & BIT(31)) != (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::orrDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op2 = *registers[(opcode & 0x0038) >> 3];
    *op0 |= op2;
    cpsr = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    return 1;
}

int CPU::bicDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op2 = *registers[(opcode & 0x0038) >> 3];
    *op0 &= ~op2;
    cpsr = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    return 1;
}

int CPU::mvnDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op2 = *registers[(opcode & 0x0038) >> 3];
    *op0          = ~op2;
    cpsr          = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    return 1;
}

int CPU::negDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = 0;
    uint32_t  op2 = *registers[(opcode & 0x0038) >> 3];
    *op0          = op1 - op2;
    cpsr          = (cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 >= *op0) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (*op0 & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::mulDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x0007];
    uint32_t  op1 = *registers[(opcode & 0x0038) >> 3];
    int32_t   op2 = *registers[opcode & 0x0007];
    *op0          = op1 * op2;
    cpsr          = (cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (cpu == 0)
        return 4;
    int m;
    for (m = 1; m < 4 && (op2 < (-1 << (m * 8)) || op2 >= (1 << (m * 8))); m++)
        ;
    return m + 1;
}

} // namespace emulator_demo

