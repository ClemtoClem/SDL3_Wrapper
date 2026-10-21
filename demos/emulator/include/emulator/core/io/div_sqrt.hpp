
#ifndef DIV_SQRT_H
#define DIV_SQRT_H

#include <cstdint>
#include <memory>
#include <emulator/core/state_archive.hpp>

namespace emulator {


class Core;
class DivSqrt {
  public:
    DivSqrt(Core *core_) : core(core_) {
    }
    uint16_t readDivCnt() {
        return divCnt;
    }
    uint32_t readDivNumerL() {
        return static_cast<uint32_t>(divNumer);
    }
    uint32_t readDivNumerH() {
        return static_cast<uint32_t>(divNumer >> 32);
    }
    uint32_t readDivDenomL() {
        return static_cast<uint32_t>(divDenom);
    }
    uint32_t readDivDenomH() {
        return static_cast<uint32_t>(divDenom >> 32);
    }
    uint32_t readDivResultL() {
        return static_cast<uint32_t>(divResult);
    }
    uint32_t readDivResultH() {
        return static_cast<uint32_t>(divResult >> 32);
    }
    uint32_t readDivRemResultL() {
        return static_cast<uint32_t>(divRemResult);
    }
    uint32_t readDivRemResultH() {
        return static_cast<uint32_t>(divRemResult >> 32);
    }
    uint16_t readSqrtCnt() {
        return sqrtCnt;
    }
    uint32_t readSqrtResult() {
        return sqrtResult;
    }
    uint32_t readSqrtParamL() {
        return static_cast<uint32_t>(sqrtParam);
    }
    uint32_t readSqrtParamH() {
        return static_cast<uint32_t>(sqrtParam >> 32);
    }
    void writeDivCnt(uint16_t mask, uint16_t value);
    void writeDivNumerL(uint32_t mask, uint32_t value);
    void writeDivNumerH(uint32_t mask, uint32_t value);
    void writeDivDenomL(uint32_t mask, uint32_t value);
    void writeDivDenomH(uint32_t mask, uint32_t value);
    void writeSqrtCnt(uint16_t mask, uint16_t value);
    void writeSqrtParamL(uint32_t mask, uint32_t value);
    void writeSqrtParamH(uint32_t mask, uint32_t value);

    void ioState(StateArchive &archive) {
        archive.io(divCnt);
        archive.io(divNumer);
        archive.io(divDenom);
        archive.io(divResult);
        archive.io(divRemResult);
        archive.io(sqrtCnt);
        archive.io(sqrtResult);
        archive.io(sqrtParam);
    }

  private:
    Core *core;
    uint16_t divCnt       = 0;
    int64_t  divNumer     = 0;
    int64_t  divDenom     = 0;
    int64_t  divResult    = 0;
    int64_t  divRemResult = 0;
    uint16_t sqrtCnt      = 0;
    uint32_t sqrtResult   = 0;
    uint64_t sqrtParam    = 0;
    void     divide();
    void     squareRoot();
};

} // namespace emulator

#endif
