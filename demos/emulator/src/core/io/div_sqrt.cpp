#include <emulator/core/io/div_sqrt.hpp>

#include <emulator/core/core.hpp>

#include <cmath>

namespace emulator {


void DivSqrt::divide() {
    if (divDenom == 0)
        divCnt |= BIT(14);
    else
        divCnt = static_cast<uint16_t>(divCnt & ~BIT(14));

    switch (divCnt & 0x0003) {
        case 0: {
            if ((int32_t)divNumer == INT32_MIN && (int32_t)divDenom == -1) {
                divResult    = static_cast<int64_t>(static_cast<unsigned long long>(int64_t((int32_t)divNumer)) ^ (0xFFFFFFFFULL << 32));
                divRemResult = 0;
            } else if ((int32_t)divDenom != 0) {
                divResult    = (int32_t)divNumer / (int32_t)divDenom;
                divRemResult = (int32_t)divNumer % (int32_t)divDenom;
            } else {
                divResult    = static_cast<int64_t>(static_cast<unsigned long long>((((int32_t)divNumer < 0) ? 1 : -1)) ^ (0xFFFFFFFFULL << 32));
                divRemResult = (int32_t)divNumer;
            }
            break;
        }
        case 1:
        case 3: {
            if (divNumer == INT64_MIN && (int32_t)divDenom == -1) {
                divResult    = divNumer;
                divRemResult = 0;
            } else if ((int32_t)divDenom != 0) {
                divResult    = divNumer / (int32_t)divDenom;
                divRemResult = divNumer % (int32_t)divDenom;
            } else {
                divResult    = (divNumer < 0) ? 1 : -1;
                divRemResult = divNumer;
            }
            break;
        }
        case 2: {
            if (divNumer == INT64_MIN && divDenom == -1) {
                divResult    = divNumer;
                divRemResult = 0;
            } else if (divDenom != 0) {
                divResult    = divNumer / divDenom;
                divRemResult = divNumer % divDenom;
            } else {
                divResult    = (divNumer < 0) ? 1 : -1;
                divRemResult = divNumer;
            }
            break;
        }
    }
}

void DivSqrt::squareRoot() {
    switch (sqrtCnt & 0x0001) {
        case 0:
            sqrtResult = static_cast<uint32_t>(sqrt((uint32_t)sqrtParam));
            break;
        case 1:
            sqrtResult = static_cast<uint32_t>(sqrtl(sqrtParam));
            break;
    }
}

void DivSqrt::writeDivCnt(uint16_t mask, uint16_t value) {
    mask &= 0x0003;
    divCnt = (divCnt & ~mask) | (value & mask);
    divide();
}

void DivSqrt::writeDivNumerL(uint32_t mask, uint32_t value) {
    divNumer = static_cast<int64_t>((static_cast<size_t>(divNumer) & ~((uint64_t)mask)) | (value & mask));
    divide();
}

void DivSqrt::writeDivNumerH(uint32_t mask, uint32_t value) {
    divNumer = static_cast<int64_t>((static_cast<size_t>(divNumer) & ~((uint64_t)mask << 32)) | ((uint64_t)(value & mask) << 32));
    divide();
}

void DivSqrt::writeDivDenomL(uint32_t mask, uint32_t value) {
    divDenom = static_cast<int64_t>((static_cast<size_t>(divDenom) & ~((uint64_t)mask)) | (value & mask));
    divide();
}

void DivSqrt::writeDivDenomH(uint32_t mask, uint32_t value) {
    divDenom = static_cast<int64_t>((static_cast<size_t>(divDenom) & ~((uint64_t)mask << 32)) | ((uint64_t)(value & mask) << 32));
    divide();
}

void DivSqrt::writeSqrtCnt(uint16_t mask, uint16_t value) {
    mask &= 0x0001;
    sqrtCnt = (sqrtCnt & ~mask) | (value & mask);
    squareRoot();
}

void DivSqrt::writeSqrtParamL(uint32_t mask, uint32_t value) {
    sqrtParam = (sqrtParam & ~((uint64_t)mask)) | (value & mask);
    squareRoot();
}

void DivSqrt::writeSqrtParamH(uint32_t mask, uint32_t value) {
    sqrtParam = (sqrtParam & ~((uint64_t)mask << 32)) | ((uint64_t)(value & mask) << 32);
    squareRoot();
}

} // namespace emulator

