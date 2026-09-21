#include "emulator/gbc/gbc_cpu.hpp"

#include "emulator/defines.hpp"
#include "emulator/gbc/gbc_core.hpp"
#include "emulator/gbc/gbc_defines.hpp"

namespace emulator_demo {

using namespace gbc;

namespace {
// Matches opcode `op` against `value` after masking with `mask` — e.g.
// M(op, 0x80, 0xf8) matches the 8 opcodes 0x80-0x87 (ADD A,reg8).
inline bool M(uint8_t op, uint8_t value, uint8_t mask) {
    return (op & mask) == value;
}
constexpr uint8_t kFlagMasks[4] = {kFlagZ, kFlagZ, kFlagC, kFlagC}; // for cond-code bits 0-1 of JR/JP/CALL/RET

// Cycle cost per opcode (4-cycle units already expanded), main table then
// CB-prefixed table — values from the widely-referenced GB opcode cycle
// tables (see e.g. gbdev.io/pandocs, or gb-instructions.txt as cited in the
// koenk/gbc reference this was cross-checked against).
constexpr int kCycles[256] = {
     4, 12,  8,  8,  4,  4,  8,  4, 20,  8,  8,  8,  4,  4,  8,  4,
     4, 12,  8,  8,  4,  4,  8,  4, 12,  8,  8,  8,  4,  4,  8,  4,
     8, 12,  8,  8,  4,  4,  8,  4,  8,  8,  8,  8,  4,  4,  8,  4,
     8, 12,  8,  8, 12, 12, 12,  4,  8,  8,  8,  8,  4,  4,  8,  4,
     4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
     4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
     4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
     8,  8,  8,  8,  8,  8,  4,  8,  4,  4,  4,  4,  4,  4,  8,  4,
     4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
     4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
     4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
     4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  8,  4,  8,  4,
     8, 12, 12, 16, 12, 16,  8, 16,  8, 16, 12,  0, 12, 24,  8, 16,
     8, 12, 12,  4, 12, 16,  8, 16,  8, 16, 12,  4, 12,  4,  8, 16,
    12, 12,  8,  4,  4, 16,  8, 16, 16,  4, 16,  4,  4,  4,  8, 16,
    12, 12,  8,  4,  4, 16,  8, 16, 12,  8, 16,  4,  0,  4,  8, 16,
};
constexpr int kCyclesCb[256] = {
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
     8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8,
     8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8,
     8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8,
     8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8,
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
};

inline void setFlag(uint8_t &f, uint8_t bit, bool v) {
    f = v ? uint8_t(f | bit) : uint8_t(f & ~bit);
}
} // namespace

void GbcCpu::reset(bool cgb) {
    if (!cgb) {
        a = 0x01; f = 0xb0; b = 0x00; c = 0x13; d = 0x00; e = 0xd8; h = 0x01; l = 0x4d;
    } else {
        a = 0x11; f = 0x80; b = 0x00; c = 0x00; d = 0xff; e = 0x56; h = 0x00; l = 0x0d;
    }
    sp = 0xfffe;
    pc = 0x0100;
    halted = false;
    ime = true;
    ie = 0;
    ifReg = 0;
    doubleSpeed_ = false;
}

uint8_t *GbcCpu::reg8(int idx) {
    switch (idx & 7) {
        case 0: return &b;
        case 1: return &c;
        case 2: return &d;
        case 3: return &e;
        case 4: return &h;
        case 5: return &l;
        case 6: return nullptr; // (HL) — caller must go through mem()/memWrite()
        default: return &a;
    }
}

uint16_t GbcCpu::reg16(int idx) {
    switch (idx & 3) {
        case 0: return uint16_t((b << 8) | c);
        case 1: return uint16_t((d << 8) | e);
        case 2: return uint16_t((h << 8) | l);
        default: return sp;
    }
}

void GbcCpu::setReg16(int idx, uint16_t value) {
    switch (idx & 3) {
        case 0: b = uint8_t(value >> 8); c = uint8_t(value); break;
        case 1: d = uint8_t(value >> 8); e = uint8_t(value); break;
        case 2: h = uint8_t(value >> 8); l = uint8_t(value); break;
        default: sp = value; break;
    }
}

uint16_t GbcCpu::reg16s(int idx) {
    switch (idx & 3) {
        case 0: return uint16_t((b << 8) | c);
        case 1: return uint16_t((d << 8) | e);
        case 2: return uint16_t((h << 8) | l);
        default: return uint16_t((a << 8) | f);
    }
}

void GbcCpu::setReg16s(int idx, uint16_t value) {
    switch (idx & 3) {
        case 0: b = uint8_t(value >> 8); c = uint8_t(value); break;
        case 1: d = uint8_t(value >> 8); e = uint8_t(value); break;
        case 2: h = uint8_t(value >> 8); l = uint8_t(value); break;
        default: a = uint8_t(value >> 8); f = uint8_t(value & 0xf0); break; // low nibble of F always reads 0
    }
}

uint8_t GbcCpu::mem(uint16_t addr) {
    return core->getMemory().read(addr);
}
void GbcCpu::memWrite(uint16_t addr, uint8_t value) {
    core->getMemory().write(addr, value);
}
uint8_t GbcCpu::imm8() {
    return mem(pc);
}
uint16_t GbcCpu::imm16() {
    return uint16_t(mem(pc) | (mem(uint16_t(pc + 1)) << 8));
}

void GbcCpu::handleInterrupts() {
    uint8_t pending = uint8_t(ie & ifReg);
    if (!pending) return;
    if (ime) {
        for (int i = 0; i < 5; i++) {
            if (pending & (1 << i)) {
                ime = false;
                ifReg = uint8_t(ifReg ^ (1 << i));
                core->getMemory().push16(sp, pc);
                pc = uint16_t(i * 8 + 0x40);
                halted = false;
                return;
            }
        }
    } else {
        halted = false; // HALT still wakes on a pending (but disabled) interrupt
    }
}

int GbcCpu::step() {
    handleInterrupts();

    uint8_t op = mem(pc);
    int cycles = kCycles[op];
    if (op == 0xcb) cycles = kCyclesCb[mem(uint16_t(pc + 1))];

    if (halted) return 4;

    pc++;
    if (op == 0xcb) return doCbInstruction();
    doInstruction();
    return cycles;
}

int GbcCpu::doCbInstruction() {
    uint8_t op = mem(pc++);
    uint8_t *r = reg8(op & 7);
    uint8_t val = r ? *r : mem(reg16(2));
    uint8_t res = val;

    if (M(op, 0x00, 0xf8)) { // RLC
        res = uint8_t((val << 1) | (val >> 7));
        setFlag(f, kFlagZ, res == 0); setFlag(f, kFlagN, false); setFlag(f, kFlagH, false); setFlag(f, kFlagC, val >> 7);
    } else if (M(op, 0x08, 0xf8)) { // RRC
        res = uint8_t((val >> 1) | ((val & 1) << 7));
        setFlag(f, kFlagZ, res == 0); setFlag(f, kFlagN, false); setFlag(f, kFlagH, false); setFlag(f, kFlagC, val & 1);
    } else if (M(op, 0x10, 0xf8)) { // RL
        res = uint8_t((val << 1) | ((f & kFlagC) ? 1 : 0));
        setFlag(f, kFlagZ, res == 0); setFlag(f, kFlagN, false); setFlag(f, kFlagH, false); setFlag(f, kFlagC, val >> 7);
    } else if (M(op, 0x18, 0xf8)) { // RR
        res = uint8_t((val >> 1) | ((f & kFlagC) ? 0x80 : 0));
        setFlag(f, kFlagZ, res == 0); setFlag(f, kFlagN, false); setFlag(f, kFlagH, false); setFlag(f, kFlagC, val & 1);
    } else if (M(op, 0x20, 0xf8)) { // SLA
        setFlag(f, kFlagC, val >> 7);
        res = uint8_t(val << 1);
        setFlag(f, kFlagZ, res == 0); setFlag(f, kFlagN, false); setFlag(f, kFlagH, false);
    } else if (M(op, 0x28, 0xf8)) { // SRA
        setFlag(f, kFlagC, val & 1);
        res = uint8_t((val >> 1) | (val & 0x80));
        setFlag(f, kFlagZ, res == 0); setFlag(f, kFlagN, false); setFlag(f, kFlagH, false);
    } else if (M(op, 0x30, 0xf8)) { // SWAP
        res = uint8_t(((val << 4) & 0xf0) | ((val >> 4) & 0xf));
        f = res == 0 ? kFlagZ : 0;
    } else if (M(op, 0x38, 0xf8)) { // SRL
        setFlag(f, kFlagC, val & 1);
        res = uint8_t(val >> 1);
        setFlag(f, kFlagZ, res == 0); setFlag(f, kFlagN, false); setFlag(f, kFlagH, false);
    } else if (M(op, 0x40, 0xc0)) { // BIT
        uint8_t bit = (op >> 3) & 7;
        setFlag(f, kFlagZ, ((val >> bit) & 1) == 0); setFlag(f, kFlagN, false); setFlag(f, kFlagH, true);
        return kCyclesCb[op];
    } else if (M(op, 0x80, 0xc0)) { // RES
        res = uint8_t(val & ~(1 << ((op >> 3) & 7)));
    } else if (M(op, 0xc0, 0xc0)) { // SET
        res = uint8_t(val | (1 << ((op >> 3) & 7)));
    }

    if (r) *r = res; else memWrite(reg16(2), res);
    return kCyclesCb[op];
}

void GbcCpu::doInstruction() {
    uint8_t op = mem(uint16_t(pc - 1));

    if (M(op, 0x00, 0xff)) { // NOP
    } else if (M(op, 0x01, 0xcf)) { // LD reg16, u16
        setReg16(op >> 4, imm16()); pc += 2;
    } else if (M(op, 0x02, 0xff)) { // LD (BC), A
        memWrite(reg16(0), a);
    } else if (M(op, 0x03, 0xcf)) { // INC reg16
        setReg16(op >> 4, uint16_t(reg16(op >> 4) + 1));
    } else if (M(op, 0x04, 0xc7)) { // INC reg8
        uint8_t *r = reg8(op >> 3);
        uint8_t val = r ? *r : mem(reg16(2));
        uint8_t res = uint8_t(val + 1);
        setFlag(f, kFlagZ, res == 0); setFlag(f, kFlagN, false); setFlag(f, kFlagH, (val & 0xf) == 0xf);
        if (r) *r = res; else memWrite(reg16(2), res);
    } else if (M(op, 0x05, 0xc7)) { // DEC reg8
        uint8_t *r = reg8(op >> 3);
        uint8_t val = uint8_t((r ? *r : mem(reg16(2))) - 1);
        setFlag(f, kFlagN, true); setFlag(f, kFlagZ, val == 0); setFlag(f, kFlagH, (val & 0xf) == 0xf);
        if (r) *r = val; else memWrite(reg16(2), val);
    } else if (M(op, 0x06, 0xc7)) { // LD reg8, imm8
        uint8_t *r = reg8(op >> 3);
        uint8_t v = imm8(); pc++;
        if (r) *r = v; else memWrite(reg16(2), v);
    } else if (M(op, 0x07, 0xff)) { // RLCA
        uint8_t res = uint8_t((a << 1) | (a >> 7));
        f = (a >> 7) ? kFlagC : 0; a = res;
    } else if (M(op, 0x08, 0xff)) { // LD (imm16), SP
        core->getMemory().write16(imm16(), sp); pc += 2;
    } else if (M(op, 0x09, 0xcf)) { // ADD HL, reg16
        uint16_t hl = reg16(2), src = reg16(op >> 4);
        uint32_t res = hl + src;
        setFlag(f, kFlagN, false);
        setFlag(f, kFlagH, ((hl & 0xfff) + (src & 0xfff)) & 0x1000);
        setFlag(f, kFlagC, res > 0xffff);
        setReg16(2, uint16_t(res));
    } else if (M(op, 0x0a, 0xff)) { // LD A, (BC)
        a = mem(reg16(0));
    } else if (M(op, 0x0b, 0xcf)) { // DEC reg16
        setReg16(op >> 4, uint16_t(reg16(op >> 4) - 1));
    } else if (M(op, 0x0f, 0xff)) { // RRCA
        f = (a & 1) ? kFlagC : 0; a = uint8_t((a >> 1) | ((a & 1) << 7));
    } else if (M(op, 0x10, 0xff)) { // STOP
        pc++; // followed by a padding byte; STOP itself has no persistent behavior modeled here
    } else if (M(op, 0x12, 0xff)) { // LD (DE), A
        memWrite(reg16(1), a);
    } else if (M(op, 0x17, 0xff)) { // RLA
        uint8_t res = uint8_t((a << 1) | ((f & kFlagC) ? 1 : 0));
        f = (a & 0x80) ? kFlagC : 0; a = res;
    } else if (M(op, 0x18, 0xff)) { // JR off8
        pc = uint16_t(pc + int8_t(imm8()) + 1);
    } else if (M(op, 0x1a, 0xff)) { // LD A, (DE)
        a = mem(reg16(1));
    } else if (M(op, 0x1f, 0xff)) { // RRA
        uint8_t res = uint8_t((a >> 1) | ((f & kFlagC) ? 0x80 : 0));
        setFlag(f, kFlagZ, false); setFlag(f, kFlagN, false); setFlag(f, kFlagH, false); setFlag(f, kFlagC, a & 1);
        a = res;
    } else if (M(op, 0x20, 0xe7)) { // JR cond, off8
        uint8_t flag = (op >> 3) & 3;
        if ((bool((f & kFlagMasks[flag]))) == (flag & 1)) pc = uint16_t(pc + int8_t(imm8()));
        pc++;
    } else if (M(op, 0x22, 0xff)) { // LDI (HL), A
        memWrite(reg16(2), a); setReg16(2, uint16_t(reg16(2) + 1));
    } else if (M(op, 0x27, 0xff)) { // DAA
        int8_t add = 0;
        bool nf = f & kFlagN, hf = f & kFlagH, cf = f & kFlagC;
        if ((!nf && (a & 0xf) > 0x9) || hf) add |= 0x6;
        bool newC = cf;
        if ((!nf && a > 0x99) || cf) { add |= 0x60; newC = true; }
        a = uint8_t(a + (nf ? -add : add));
        setFlag(f, kFlagZ, a == 0); setFlag(f, kFlagH, false); setFlag(f, kFlagC, newC);
    } else if (M(op, 0x2a, 0xff)) { // LDI A, (HL)
        a = mem(reg16(2)); setReg16(2, uint16_t(reg16(2) + 1));
    } else if (M(op, 0x2f, 0xff)) { // CPL
        a = uint8_t(~a); setFlag(f, kFlagN, true); setFlag(f, kFlagH, true);
    } else if (M(op, 0x32, 0xff)) { // LDD (HL), A
        memWrite(reg16(2), a); setReg16(2, uint16_t(reg16(2) - 1));
    } else if (M(op, 0x37, 0xff)) { // SCF
        setFlag(f, kFlagN, false); setFlag(f, kFlagH, false); setFlag(f, kFlagC, true);
    } else if (M(op, 0x3a, 0xff)) { // LDD A, (HL)
        a = mem(reg16(2)); setReg16(2, uint16_t(reg16(2) - 1));
    } else if (M(op, 0x3f, 0xff)) { // CCF
        setFlag(f, kFlagC, !(f & kFlagC)); setFlag(f, kFlagN, false); setFlag(f, kFlagH, false);
    } else if (M(op, 0x76, 0xff)) { // HALT
        halted = true;
    } else if (M(op, 0x40, 0xc0)) { // LD reg8, reg8
        uint8_t *src = reg8(op); uint8_t *dst = reg8(op >> 3);
        uint8_t v = src ? *src : mem(reg16(2));
        if (dst) *dst = v; else memWrite(reg16(2), v);
    } else if (M(op, 0x80, 0xf8)) { // ADD A, reg8
        uint8_t *src = reg8(op); uint8_t v = src ? *src : mem(reg16(2));
        uint16_t res = uint16_t(a + v);
        setFlag(f, kFlagZ, uint8_t(res) == 0); setFlag(f, kFlagN, false);
        setFlag(f, kFlagH, (a ^ v ^ res) & 0x10); setFlag(f, kFlagC, res & 0x100);
        a = uint8_t(res);
    } else if (M(op, 0x88, 0xf8)) { // ADC A, reg8
        uint8_t *src = reg8(op); uint8_t v = src ? *src : mem(reg16(2));
        uint16_t res = uint16_t(a + v + ((f & kFlagC) ? 1 : 0));
        setFlag(f, kFlagZ, uint8_t(res) == 0); setFlag(f, kFlagN, false);
        setFlag(f, kFlagH, (a ^ v ^ res) & 0x10); setFlag(f, kFlagC, res & 0x100);
        a = uint8_t(res);
    } else if (M(op, 0x90, 0xf8)) { // SUB reg8
        uint8_t *r = reg8(op); uint8_t v = r ? *r : mem(reg16(2));
        uint8_t res = uint8_t(a - v);
        setFlag(f, kFlagZ, res == 0); setFlag(f, kFlagN, true);
        setFlag(f, kFlagH, (a & 0xf) < (v & 0xf)); setFlag(f, kFlagC, a < v);
        a = res;
    } else if (M(op, 0x98, 0xf8)) { // SBC A, reg8
        uint8_t *r = reg8(op); uint8_t v = r ? *r : mem(reg16(2));
        uint8_t cin = (f & kFlagC) ? 1 : 0;
        int res = a - v - cin;
        setFlag(f, kFlagZ, uint8_t(res) == 0); setFlag(f, kFlagN, true);
        setFlag(f, kFlagH, (int(a & 0xf) - int(v & 0xf) - cin) < 0);
        setFlag(f, kFlagC, a < v + cin);
        a = uint8_t(res);
    } else if (M(op, 0xa0, 0xf8)) { // AND reg8
        uint8_t *r = reg8(op); uint8_t v = r ? *r : mem(reg16(2));
        a = uint8_t(a & v); setFlag(f, kFlagZ, a == 0); setFlag(f, kFlagN, false); setFlag(f, kFlagH, true); setFlag(f, kFlagC, false);
    } else if (M(op, 0xa8, 0xf8)) { // XOR reg8
        uint8_t *r = reg8(op); uint8_t v = r ? *r : mem(reg16(2));
        a = uint8_t(a ^ v); f = a ? 0 : kFlagZ;
    } else if (M(op, 0xb0, 0xf8)) { // OR reg8
        uint8_t *r = reg8(op); uint8_t v = r ? *r : mem(reg16(2));
        a = uint8_t(a | v); f = a ? 0 : kFlagZ;
    } else if (M(op, 0xb8, 0xf8)) { // CP reg8
        uint8_t *r = reg8(op); uint8_t v = r ? *r : mem(reg16(2));
        setFlag(f, kFlagZ, a == v); setFlag(f, kFlagN, true);
        setFlag(f, kFlagH, (a & 0xf) < (v & 0xf)); setFlag(f, kFlagC, a < v);
    } else if (M(op, 0xc0, 0xe7)) { // RET cond
        uint8_t flag = (op >> 3) & 3;
        if ((bool((f & kFlagMasks[flag]))) == (flag & 1)) pc = core->getMemory().pop16(sp);
    } else if (M(op, 0xc1, 0xcf)) { // POP reg16
        setReg16s(op >> 4, core->getMemory().pop16(sp));
    } else if (M(op, 0xc2, 0xe7)) { // JP cond, imm16
        uint8_t flag = (op >> 3) & 3;
        if ((bool((f & kFlagMasks[flag]))) == (flag & 1)) pc = imm16(); else pc += 2;
    } else if (M(op, 0xc3, 0xff)) { // JP imm16
        pc = imm16();
    } else if (M(op, 0xc4, 0xe7)) { // CALL cond, imm16
        uint16_t dst = imm16(); pc += 2;
        uint8_t flag = (op >> 3) & 3;
        if ((bool((f & kFlagMasks[flag]))) == (flag & 1)) { core->getMemory().push16(sp, pc); pc = dst; }
    } else if (M(op, 0xc5, 0xcf)) { // PUSH reg16
        core->getMemory().push16(sp, reg16s(op >> 4));
    } else if (M(op, 0xc6, 0xff)) { // ADD A, imm8
        uint8_t v = imm8(); uint16_t res = uint16_t(a + v);
        setFlag(f, kFlagZ, uint8_t(res) == 0); setFlag(f, kFlagN, false);
        setFlag(f, kFlagH, (a ^ v ^ res) & 0x10); setFlag(f, kFlagC, res & 0x100);
        a = uint8_t(res); pc++;
    } else if (M(op, 0xc7, 0xc7)) { // RST
        core->getMemory().push16(sp, pc); pc = uint16_t(((op >> 3) & 7) * 8);
    } else if (M(op, 0xc9, 0xff)) { // RET
        pc = core->getMemory().pop16(sp);
    } else if (M(op, 0xcd, 0xff)) { // CALL imm16
        uint16_t dst = imm16(); core->getMemory().push16(sp, uint16_t(pc + 2)); pc = dst;
    } else if (M(op, 0xce, 0xff)) { // ADC A, imm8
        uint8_t v = imm8(); uint16_t res = uint16_t(a + v + ((f & kFlagC) ? 1 : 0));
        setFlag(f, kFlagZ, uint8_t(res) == 0); setFlag(f, kFlagN, false);
        setFlag(f, kFlagH, (a ^ v ^ res) & 0x10); setFlag(f, kFlagC, res & 0x100);
        a = uint8_t(res); pc++;
    } else if (M(op, 0xd6, 0xff)) { // SUB imm8
        uint8_t v = imm8(); uint8_t res = uint8_t(a - v);
        setFlag(f, kFlagZ, res == 0); setFlag(f, kFlagN, true);
        setFlag(f, kFlagH, (a & 0xf) < (v & 0xf)); setFlag(f, kFlagC, a < v);
        a = res; pc++;
    } else if (M(op, 0xd9, 0xff)) { // RETI
        pc = core->getMemory().pop16(sp); ime = true;
    } else if (M(op, 0xde, 0xff)) { // SBC A, imm8
        uint8_t v = imm8(); uint8_t cin = (f & kFlagC) ? 1 : 0;
        int res = a - v - cin;
        setFlag(f, kFlagZ, uint8_t(res) == 0); setFlag(f, kFlagN, true);
        setFlag(f, kFlagH, (int(a & 0xf) - int(v & 0xf) - cin) < 0);
        setFlag(f, kFlagC, a < v + cin);
        a = uint8_t(res); pc++;
    } else if (M(op, 0xe0, 0xff)) { // LD (0xFF00+imm8), A
        memWrite(uint16_t(0xff00 + imm8()), a); pc++;
    } else if (M(op, 0xe2, 0xff)) { // LD (0xFF00+C), A
        memWrite(uint16_t(0xff00 + c), a);
    } else if (M(op, 0xe6, 0xff)) { // AND imm8
        a = uint8_t(a & imm8()); pc++;
        setFlag(f, kFlagZ, a == 0); setFlag(f, kFlagN, false); setFlag(f, kFlagH, true); setFlag(f, kFlagC, false);
    } else if (M(op, 0xe8, 0xff)) { // ADD SP, imm8s
        int8_t off = int8_t(imm8());
        uint16_t oldSp = sp;
        setFlag(f, kFlagZ, false); setFlag(f, kFlagN, false);
        setFlag(f, kFlagH, (oldSp & 0xf) + (uint8_t(off) & 0xf) > 0xf);
        setFlag(f, kFlagC, (oldSp & 0xff) + (uint8_t(off) & 0xff) > 0xff);
        sp = uint16_t(sp + off); pc++;
    } else if (M(op, 0xe9, 0xff)) { // JP (HL)
        pc = reg16(2);
    } else if (M(op, 0xea, 0xff)) { // LD (imm16), A
        memWrite(imm16(), a); pc += 2;
    } else if (M(op, 0xee, 0xff)) { // XOR imm8
        a = uint8_t(a ^ imm8()); pc++; f = a ? 0 : kFlagZ;
    } else if (M(op, 0xf0, 0xff)) { // LD A, (0xFF00+imm8)
        a = mem(uint16_t(0xff00 + imm8())); pc++;
    } else if (M(op, 0xf2, 0xff)) { // LD A, (0xFF00+C)
        a = mem(uint16_t(0xff00 + c));
    } else if (M(op, 0xf3, 0xff)) { // DI
        ime = false;
    } else if (M(op, 0xf6, 0xff)) { // OR imm8
        a = uint8_t(a | imm8()); f = a ? 0 : kFlagZ; pc++;
    } else if (M(op, 0xf8, 0xff)) { // LD HL, SP+imm8
        int8_t off = int8_t(imm8());
        setFlag(f, kFlagZ, false); setFlag(f, kFlagN, false);
        setFlag(f, kFlagH, (sp & 0xf) + (uint8_t(off) & 0xf) > 0xf);
        setFlag(f, kFlagC, (sp & 0xff) + (uint8_t(off) & 0xff) > 0xff);
        setReg16(2, uint16_t(sp + off)); pc++;
    } else if (M(op, 0xf9, 0xff)) { // LD SP, HL
        sp = reg16(2);
    } else if (M(op, 0xfa, 0xff)) { // LD A, (imm16)
        a = mem(imm16()); pc += 2;
    } else if (M(op, 0xfb, 0xff)) { // EI
        ime = true;
    } else if (M(op, 0xfe, 0xff)) { // CP imm8
        uint8_t v = imm8();
        setFlag(f, kFlagZ, a == v); setFlag(f, kFlagN, true);
        setFlag(f, kFlagH, (a & 0xf) < (v & 0xf)); setFlag(f, kFlagC, a < v);
        pc++;
    } else {
        LOG("GbcCpu: unknown opcode 0x%02X @ pc=0x%04X\n", op, uint16_t(pc - 1));
    }
}

void GbcCpu::ioState(StateArchive &archive) {
    archive.io(a); archive.io(f); archive.io(b); archive.io(c);
    archive.io(d); archive.io(e); archive.io(h); archive.io(l);
    archive.io(sp); archive.io(pc);
    archive.io(halted); archive.io(ime); archive.io(ie); archive.io(ifReg);
    archive.io(doubleSpeed_);
}

} // namespace emulator_demo
