#ifndef GBC_CPU_H
#define GBC_CPU_H

#include <cstdint>

#include "../state_archive.hpp"

namespace emulator_demo {

class GbcCore;

/**
 * @brief Sharp LR35902 interpreter (the GB/GBC CPU: an 8080/Z80-derived
 * 8-bit core). Unlike the ARM interpreter's per-addressing-mode dispatch
 * table, this uses a compact mask-matched if-chain — the LR35902's opcode
 * encoding is small and regular enough (256 main + 256 CB-prefixed opcodes)
 * that this stays both fast and easy to audit against hardware references.
 */
class GbcCpu {
  public:
    explicit GbcCpu(GbcCore *core) : core(core) {
    }

    void reset(bool cgb);

    /// Executes exactly one instruction (or, if halted, does nothing but
    /// still costs 4 cycles) and returns its cost in CPU cycles.
    int step();

    void requestInterrupt(uint8_t bit) {
        ifReg = uint8_t(ifReg | bit);
    }
    uint8_t readIe() const {
        return ie;
    }
    void writeIe(uint8_t value) {
        ie = value;
    }
    uint8_t readIf() const {
        return ifReg;
    }
    void writeIf(uint8_t value) {
        ifReg = value;
    }

    [[nodiscard]] bool doubleSpeed() const {
        return doubleSpeed_;
    }

    void ioState(StateArchive &archive);

  private:
    GbcCore *core;

    uint8_t a = 0, f = 0, b = 0, c = 0, d = 0, e = 0, h = 0, l = 0;
    uint16_t sp = 0, pc = 0;

    bool halted   = false;
    bool ime      = false;
    uint8_t ie    = 0;
    uint8_t ifReg = 0;
    bool doubleSpeed_ = false; // CGB double-speed mode (KEY1) — accepted but not modeled (no perf reason to)

    // Register-index decode tables, matching the LR35902 opcode encoding's
    // 3-bit register fields: 0=B,1=C,2=D,3=E,4=H,5=L,6=(HL) [handled
    // separately since it's a memory access, not a register], 7=A.
    uint8_t  *reg8(int idx);
    uint16_t  reg16(int idx);   // BC/DE/HL/SP
    void      setReg16(int idx, uint16_t value);
    uint16_t  reg16s(int idx);  // BC/DE/HL/AF (for PUSH/POP)
    void      setReg16s(int idx, uint16_t value);

    uint8_t  imm8();
    uint16_t imm16();
    uint8_t  mem(uint16_t addr);
    void     memWrite(uint16_t addr, uint8_t value);

    void handleInterrupts();
    void doInstruction();
    int  doCbInstruction();
};

} // namespace emulator_demo

#endif // GBC_CPU_H
