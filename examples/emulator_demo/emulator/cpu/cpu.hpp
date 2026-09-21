#ifndef CPU_INTERPRETER_H
#define CPU_INTERPRETER_H

#include <cstdint>
#include <functional>
#include <memory>
#include "../defines.hpp"
#include "../state_archive.hpp"

namespace emulator_demo {

class Core;
class Bios;
class CPU {
  private:
    Core *core;
    bool  cpu;
    Bios *bios = nullptr;

    uint32_t  pipeline[2]      = {};
    uint32_t *registers[16]    = {};
    uint32_t  registersUsr[16] = {};
    uint32_t  registersFiq[7]  = {};
    uint32_t  registersSvc[2]  = {};
    uint32_t  registersAbt[2]  = {};
    uint32_t  registersIrq[2]  = {};
    uint32_t  registersUnd[2]  = {};

    uint32_t cpsr = 0, *spsr = nullptr;
    uint32_t spsrFiq = 0, spsrSvc = 0, spsrAbt = 0, spsrIrq = 0, spsrUnd = 0;
    uint8_t  halted = 0;
    uint8_t  ime    = 0;
    uint32_t ie = 0, irf = 0;
    uint8_t  postFlg = 0;

  public:
    CPU(Core *core, bool cpu);

    void init();
    void directBoot();
    int  runOpcode();

    void halt(int bit) {
        halted |= BIT(bit);
    }
    void unhalt(int bit) {
        halted &= ~BIT(bit);
    }
    void sendInterrupt(int bit);
    bool shouldRun() {
        return !halted;
    }
    bool isThumb() {
        return cpsr & BIT(5);
    }
    uint32_t getPC() {
        return *registers[15];
    }
    void setBios(Bios *bios) {
        this->bios = bios;
    }
    int     handleHleIrq();
    uint8_t readIme() {
        return ime;
    }
    uint32_t readIe() {
        return ie;
    }
    uint32_t readIrf() {
        return irf;
    }
    uint8_t readPostFlg() {
        return postFlg;
    }

    void writeIme(uint8_t value);
    void writeIe(uint32_t mask, uint32_t value);
    void writeIrf(uint32_t mask, uint32_t value);
    void writePostFlg(uint8_t value);

    /**
     * @brief Sérialise/désérialise l'état du CPU (registres, CPSR/SPSR, IRQ) pour les save states.
     * @param archive Archive de sauvegarde/chargement.
     */
    void ioState(StateArchive &archive);

  private:
    std::function<void()> interruptTask;

    void interrupt();
    int  exception(uint8_t vector);
    void flushPipeline();
    void setCpsr(uint32_t value, bool save = false);
    int  handleReserved(uint32_t opcode);
    int  finishHleIrq();
    int  unkArm(uint32_t opcode);
    int  unkThumb(uint16_t opcode);

    /** @name */
    ///@{
    uint32_t lli(uint32_t opcode);
    uint32_t llr(uint32_t opcode);
    uint32_t lri(uint32_t opcode);
    uint32_t lrr(uint32_t opcode);
    uint32_t ari(uint32_t opcode);
    uint32_t arr(uint32_t opcode);
    uint32_t rri(uint32_t opcode);
    uint32_t rrr(uint32_t opcode);
    uint32_t imm(uint32_t opcode);
    uint32_t lliS(uint32_t opcode);
    uint32_t llrS(uint32_t opcode);
    uint32_t lriS(uint32_t opcode);
    uint32_t lrrS(uint32_t opcode);
    uint32_t ariS(uint32_t opcode);
    uint32_t arrS(uint32_t opcode);
    uint32_t rriS(uint32_t opcode);
    uint32_t rrrS(uint32_t opcode);
    uint32_t immS(uint32_t opcode);
    ///@}

    /** @name ALU Operations */
    ///@{
    int _and(uint32_t opcode, uint32_t op2);    ///< AND{cond}{S}   Rd,Rn,Op2   Nzc-    Rd = Rn AND Op2
    int eor(uint32_t opcode, uint32_t op2);     ///< EOR{cond}{S}   Rd,Rn,Op2   NZc-    Rd = Rn XOR Op2
    int sub(uint32_t opcode, uint32_t op2);     ///< SUB{cond}{S}   Rd,Rn,Op2   NZcV    Rd = Rn-Op2
    int rsb(uint32_t opcode, uint32_t op2);     ///< RSB{cond}{S}   Rd,Rn,Op2   NZcV    Rd = Op2-Rn
    int add(uint32_t opcode, uint32_t op2);     ///< ADD{cond}{S}   Rd,Rn,Op2   NZcV    Rd = Rn+Op2
    int adc(uint32_t opcode, uint32_t op2);     ///< ADC{cond}{S}   Rd,Rn,Op2   NZcV    Rd = Rn+Op2+Cy
    int sbc(uint32_t opcode, uint32_t op2);     ///< SBC{cond}{S}   Rd,Rn,Op2   NZcV    Rd = Rn-Op2+Cy-1
    int rsc(uint32_t opcode, uint32_t op2);     ///< RSC{cond}{S}   Rd,Rn,Op2   NZcV    Rd = Op2-Rn+Cy-1
    int tst(uint32_t opcode, uint32_t op2);     ///< TST{cond}{P}      Rn,Op2   NZc-    Void = Rn AND Op2
    int teq(uint32_t opcode, uint32_t op2);     ///< TEQ{cond}{P}      Rn,Op2   NZc-    Void = Rn XOR Op2
    int cmp(uint32_t opcode, uint32_t op2);     ///< CMP{cond}{P}      Rn,Op2   NZcV    Void = Rn-Op2
    int cmn(uint32_t opcode, uint32_t op2);     ///< CMN{cond}{P}      Rn,Op2   NZcV    Void = Rn+Op2
    int orr(uint32_t opcode, uint32_t op2);
    int mov(uint32_t opcode, uint32_t op2);     ///< MOV{cond}{S}   Rd,Op2      NZc-    Rd = Op2
    int bic(uint32_t opcode, uint32_t op2);     ///< BIC{cond}{S}   Rd,Rn,Op2   NZc-    Rd = Rn AND NOT Op2
    int mvn(uint32_t opcode, uint32_t op2);     ///< MVN{cond}{S}   Rd,Op2      NZc-    Rd = NOT Op2
    int ands(uint32_t opcode, uint32_t op2);
    int eors(uint32_t opcode, uint32_t op2);
    int subs(uint32_t opcode, uint32_t op2);
    int rsbs(uint32_t opcode, uint32_t op2);
    int adds(uint32_t opcode, uint32_t op2);
    int adcs(uint32_t opcode, uint32_t op2);
    int sbcs(uint32_t opcode, uint32_t op2);
    int rscs(uint32_t opcode, uint32_t op2);
    int orrs(uint32_t opcode, uint32_t op2);    ///< ORR{cond}{S}   Rd,Rn,Op2     NZc-  Rd = Rn OR Op2
    int movs(uint32_t opcode, uint32_t op2);
    int bics(uint32_t opcode, uint32_t op2);
    int mvns(uint32_t opcode, uint32_t op2);
    ///@}

    /** @name */
    ///@{
    int andLli(uint32_t opcode);
    int andLlr(uint32_t opcode);
    int andLri(uint32_t opcode);
    int andLrr(uint32_t opcode);
    int andAri(uint32_t opcode);
    int andArr(uint32_t opcode);
    int andRri(uint32_t opcode);
    int andRrr(uint32_t opcode);
    int andImm(uint32_t opcode);
    int andsLli(uint32_t opcode);
    int andsLlr(uint32_t opcode);
    int andsLri(uint32_t opcode);
    int andsLrr(uint32_t opcode);
    int andsAri(uint32_t opcode);
    int andsArr(uint32_t opcode);
    int andsRri(uint32_t opcode);
    int andsRrr(uint32_t opcode);
    int andsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int eorLli(uint32_t opcode);
    int eorLlr(uint32_t opcode);
    int eorLri(uint32_t opcode);
    int eorLrr(uint32_t opcode);
    int eorAri(uint32_t opcode);
    int eorArr(uint32_t opcode);
    int eorRri(uint32_t opcode);
    int eorRrr(uint32_t opcode);
    int eorImm(uint32_t opcode);
    int eorsLli(uint32_t opcode);
    int eorsLlr(uint32_t opcode);
    int eorsLri(uint32_t opcode);
    int eorsLrr(uint32_t opcode);
    int eorsAri(uint32_t opcode);
    int eorsArr(uint32_t opcode);
    int eorsRri(uint32_t opcode);
    int eorsRrr(uint32_t opcode);
    int eorsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int subLli(uint32_t opcode);
    int subLlr(uint32_t opcode);
    int subLri(uint32_t opcode);
    int subLrr(uint32_t opcode);
    int subAri(uint32_t opcode);
    int subArr(uint32_t opcode);
    int subRri(uint32_t opcode);
    int subRrr(uint32_t opcode);
    int subImm(uint32_t opcode);
    int subsLli(uint32_t opcode);
    int subsLlr(uint32_t opcode);
    int subsLri(uint32_t opcode);
    int subsLrr(uint32_t opcode);
    int subsAri(uint32_t opcode);
    int subsArr(uint32_t opcode);
    int subsRri(uint32_t opcode);
    int subsRrr(uint32_t opcode);
    int subsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int rsbLli(uint32_t opcode);
    int rsbLlr(uint32_t opcode);
    int rsbLri(uint32_t opcode);
    int rsbLrr(uint32_t opcode);
    int rsbAri(uint32_t opcode);
    int rsbArr(uint32_t opcode);
    int rsbRri(uint32_t opcode);
    int rsbRrr(uint32_t opcode);
    int rsbImm(uint32_t opcode);
    int rsbsLli(uint32_t opcode);
    int rsbsLlr(uint32_t opcode);
    int rsbsLri(uint32_t opcode);
    int rsbsLrr(uint32_t opcode);
    int rsbsAri(uint32_t opcode);
    int rsbsArr(uint32_t opcode);
    int rsbsRri(uint32_t opcode);
    int rsbsRrr(uint32_t opcode);
    int rsbsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int addLli(uint32_t opcode);
    int addLlr(uint32_t opcode);
    int addLri(uint32_t opcode);
    int addLrr(uint32_t opcode);
    int addAri(uint32_t opcode);
    int addArr(uint32_t opcode);
    int addRri(uint32_t opcode);
    int addRrr(uint32_t opcode);
    int addImm(uint32_t opcode);
    int addsLli(uint32_t opcode);
    int addsLlr(uint32_t opcode);
    int addsLri(uint32_t opcode);
    int addsLrr(uint32_t opcode);
    int addsAri(uint32_t opcode);
    int addsArr(uint32_t opcode);
    int addsRri(uint32_t opcode);
    int addsRrr(uint32_t opcode);
    int addsImm(uint32_t opcode);
    int adcLli(uint32_t opcode);
    int adcLlr(uint32_t opcode);
    int adcLri(uint32_t opcode);
    int adcLrr(uint32_t opcode);
    int adcAri(uint32_t opcode);
    int adcArr(uint32_t opcode);
    int adcRri(uint32_t opcode);
    int adcRrr(uint32_t opcode);
    int adcImm(uint32_t opcode);
    int adcsLli(uint32_t opcode);
    int adcsLlr(uint32_t opcode);
    int adcsLri(uint32_t opcode);
    int adcsLrr(uint32_t opcode);
    int adcsAri(uint32_t opcode);
    int adcsArr(uint32_t opcode);
    int adcsRri(uint32_t opcode);
    int adcsRrr(uint32_t opcode);
    int adcsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int sbcLli(uint32_t opcode);
    int sbcLlr(uint32_t opcode);
    int sbcLri(uint32_t opcode);
    int sbcLrr(uint32_t opcode);
    int sbcAri(uint32_t opcode);
    int sbcArr(uint32_t opcode);
    int sbcRri(uint32_t opcode);
    int sbcRrr(uint32_t opcode);
    int sbcImm(uint32_t opcode);
    int sbcsLli(uint32_t opcode);
    int sbcsLlr(uint32_t opcode);
    int sbcsLri(uint32_t opcode);
    int sbcsLrr(uint32_t opcode);
    int sbcsAri(uint32_t opcode);
    int sbcsArr(uint32_t opcode);
    int sbcsRri(uint32_t opcode);
    int sbcsRrr(uint32_t opcode);
    int sbcsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int rscLli(uint32_t opcode);
    int rscLlr(uint32_t opcode);
    int rscLri(uint32_t opcode);
    int rscLrr(uint32_t opcode);
    int rscAri(uint32_t opcode);
    int rscArr(uint32_t opcode);
    int rscRri(uint32_t opcode);
    int rscRrr(uint32_t opcode);
    int rscImm(uint32_t opcode);
    int rscsLli(uint32_t opcode);
    int rscsLlr(uint32_t opcode);
    int rscsLri(uint32_t opcode);
    int rscsLrr(uint32_t opcode);
    int rscsAri(uint32_t opcode);
    int rscsArr(uint32_t opcode);
    int rscsRri(uint32_t opcode);
    int rscsRrr(uint32_t opcode);
    int rscsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int tstLli(uint32_t opcode);
    int tstLlr(uint32_t opcode);
    int tstLri(uint32_t opcode);
    int tstLrr(uint32_t opcode);
    int tstAri(uint32_t opcode);
    int tstArr(uint32_t opcode);
    int tstRri(uint32_t opcode);
    int tstRrr(uint32_t opcode);
    int tstImm(uint32_t opcode);
    int teqLli(uint32_t opcode);
    int teqLlr(uint32_t opcode);
    int teqLri(uint32_t opcode);
    int teqLrr(uint32_t opcode);
    int teqAri(uint32_t opcode);
    int teqArr(uint32_t opcode);
    int teqRri(uint32_t opcode);
    int teqRrr(uint32_t opcode);
    int teqImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int cmpLli(uint32_t opcode);
    int cmpLlr(uint32_t opcode);
    int cmpLri(uint32_t opcode);
    int cmpLrr(uint32_t opcode);
    int cmpAri(uint32_t opcode);
    int cmpArr(uint32_t opcode);
    int cmpRri(uint32_t opcode);
    int cmpRrr(uint32_t opcode);
    int cmpImm(uint32_t opcode);
    int cmnLli(uint32_t opcode);
    int cmnLlr(uint32_t opcode);
    int cmnLri(uint32_t opcode);
    int cmnLrr(uint32_t opcode);
    int cmnAri(uint32_t opcode);
    int cmnArr(uint32_t opcode);
    int cmnRri(uint32_t opcode);
    int cmnRrr(uint32_t opcode);
    int cmnImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int orrLli(uint32_t opcode);
    int orrLlr(uint32_t opcode);
    int orrLri(uint32_t opcode);
    int orrLrr(uint32_t opcode);
    int orrAri(uint32_t opcode);
    int orrArr(uint32_t opcode);
    int orrRri(uint32_t opcode);
    int orrRrr(uint32_t opcode);
    int orrImm(uint32_t opcode);
    int orrsLli(uint32_t opcode);
    int orrsLlr(uint32_t opcode);
    int orrsLri(uint32_t opcode);
    int orrsLrr(uint32_t opcode);
    int orrsAri(uint32_t opcode);
    int orrsArr(uint32_t opcode);
    int orrsRri(uint32_t opcode);
    int orrsRrr(uint32_t opcode);
    int orrsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int movLli(uint32_t opcode);
    int movLlr(uint32_t opcode);
    int movLri(uint32_t opcode);
    int movLrr(uint32_t opcode);
    int movAri(uint32_t opcode);
    int movArr(uint32_t opcode);
    int movRri(uint32_t opcode);
    int movRrr(uint32_t opcode);
    int movImm(uint32_t opcode);
    int movsLli(uint32_t opcode);
    int movsLlr(uint32_t opcode);
    int movsLri(uint32_t opcode);
    int movsLrr(uint32_t opcode);
    int movsAri(uint32_t opcode);
    int movsArr(uint32_t opcode);
    int movsRri(uint32_t opcode);
    int movsRrr(uint32_t opcode);
    int movsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int bicLli(uint32_t opcode);
    int bicLlr(uint32_t opcode);
    int bicLri(uint32_t opcode);
    int bicLrr(uint32_t opcode);
    int bicAri(uint32_t opcode);
    int bicArr(uint32_t opcode);
    int bicRri(uint32_t opcode);
    int bicRrr(uint32_t opcode);
    int bicImm(uint32_t opcode);
    int bicsLli(uint32_t opcode);
    int bicsLlr(uint32_t opcode);
    int bicsLri(uint32_t opcode);
    int bicsLrr(uint32_t opcode);
    int bicsAri(uint32_t opcode);
    int bicsArr(uint32_t opcode);
    int bicsRri(uint32_t opcode);
    int bicsRrr(uint32_t opcode);
    int bicsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int mvnLli(uint32_t opcode);
    int mvnLlr(uint32_t opcode);
    int mvnLri(uint32_t opcode);
    int mvnLrr(uint32_t opcode);
    int mvnAri(uint32_t opcode);
    int mvnArr(uint32_t opcode);
    int mvnRri(uint32_t opcode);
    int mvnRrr(uint32_t opcode);
    int mvnImm(uint32_t opcode);
    int mvnsLli(uint32_t opcode);
    int mvnsLlr(uint32_t opcode);
    int mvnsLri(uint32_t opcode);
    int mvnsLrr(uint32_t opcode);
    int mvnsAri(uint32_t opcode);
    int mvnsArr(uint32_t opcode);
    int mvnsRri(uint32_t opcode);
    int mvnsRrr(uint32_t opcode);
    int mvnsImm(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int mul(uint32_t opcode);       ///< MUL{cond}{S}   Rd,Rm,Rs        NZx-    Rd = Rm*Rs
    int mla(uint32_t opcode);       ///< MLA{cond}{S}   Rd,Rm,Rs,Rn     NZx-    Rd = Rm*Rs+Rn
    int umull(uint32_t opcode);     ///< UMULL{cond}{S} RdLo,RdHi,Rm,R  NZx-    RdHiLo = Rm*Rs
    int umlal(uint32_t opcode);     ///< UMLAL{cond}{S} RdLo,RdHi,Rm,Rs NZx-    RdHiLo = Rm*Rs+RdHiLo
    int smull(uint32_t opcode);     ///< SMULL{cond}{S} RdLo,RdHi,Rm,Rs NZx-    RdHiLo = Rm*Rs
    int smlal(uint32_t opcode);     ///< SMLAL{cond}{S} RdLo,RdHi,Rm,Rs NZx-    RdHiLo = Rm*Rs+RdHiLo
    int muls(uint32_t opcode);
    int mlas(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int umulls(uint32_t opcode);
    int umlals(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int smulls(uint32_t opcode);
    int smlals(uint32_t opcode);
    int smulbb(uint32_t opcode);
    int smulbt(uint32_t opcode);
    int smultb(uint32_t opcode);
    int smultt(uint32_t opcode);
    int smulwb(uint32_t opcode);
    int smulwt(uint32_t opcode);
    int smlabb(uint32_t opcode);
    int smlabt(uint32_t opcode);
    int smlatb(uint32_t opcode);
    int smlatt(uint32_t opcode);
    int smlawb(uint32_t opcode);
    int smlawt(uint32_t opcode);
    int smlalbb(uint32_t opcode);
    int smlalbt(uint32_t opcode);
    int smlaltb(uint32_t opcode);
    int smlaltt(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int clz(uint32_t opcode);       ///< CLZ{cond}      Rd,Rm       ----    Count Leading Zeros
    int qadd(uint32_t opcode);      ///< QADD{cond}     Rd,Rm,Rn    ----q   Rd=Rm+Rn
    int qsub(uint32_t opcode);      ///< QSUB{cond}     Rd,Rm,Rn    ----q   Rd=Rm-Rn
    int qdadd(uint32_t opcode);     ///< QDADD{cond}    Rd,Rm,Rn    ----q   Rd=Rm+Rn*2
    int qdsub(uint32_t opcode);     ///< QDSUB{cond}    Rd,Rm,Rn    ----q   Rd=Rm-Rn*2
    ///@}

    /** @name */
    ///@{
    int addRegT(uint16_t opcode);
    int subRegT(uint16_t opcode);
    int addHT(uint16_t opcode);
    int cmpHT(uint16_t opcode);
    int movHT(uint16_t opcode);
    int addPcT(uint16_t opcode);
    int addSpT(uint16_t opcode);
    int addSpImmT(uint16_t opcode);
    ///@}

    /** @name */
    ///@{
    int lslImmT(uint16_t opcode);
    int lsrImmT(uint16_t opcode);
    int asrImmT(uint16_t opcode);
    int addImm3T(uint16_t opcode);
    int subImm3T(uint16_t opcode);
    int addImm8T(uint16_t opcode);
    int subImm8T(uint16_t opcode);
    int cmpImm8T(uint16_t opcode);
    int movImm8T(uint16_t opcode);
    ///@}

    /** @name */
    ///@{
    int lslDpT(uint16_t opcode);
    int lsrDpT(uint16_t opcode);
    int asrDpT(uint16_t opcode);
    int rorDpT(uint16_t opcode);
    int andDpT(uint16_t opcode);
    int eorDpT(uint16_t opcode);
    int adcDpT(uint16_t opcode);
    int sbcDpT(uint16_t opcode);
    int tstDpT(uint16_t opcode);
    int cmpDpT(uint16_t opcode);
    int cmnDpT(uint16_t opcode);
    int orrDpT(uint16_t opcode);
    int bicDpT(uint16_t opcode);
    int mvnDpT(uint16_t opcode);
    int negDpT(uint16_t opcode);
    int mulDpT(uint16_t opcode);
    ///@}

    /** @name */
    ///@{
    uint32_t ip(uint32_t opcode);
    uint32_t ipH(uint32_t opcode);
    uint32_t rp(uint32_t opcode);
    uint32_t rpll(uint32_t opcode);
    uint32_t rplr(uint32_t opcode);
    uint32_t rpar(uint32_t opcode);
    uint32_t rprr(uint32_t opcode);
    ///@}

    /** @name Memory Load/Store */
    ///@{
    int ldrsbOf(uint32_t opcode, uint32_t op2);
    int ldrshOf(uint32_t opcode, uint32_t op2);
    int ldrbOf(uint32_t opcode, uint32_t op2);
    int strbOf(uint32_t opcode, uint32_t op2);
    int ldrhOf(uint32_t opcode, uint32_t op2);
    int strhOf(uint32_t opcode, uint32_t op2);
    int ldrOf(uint32_t opcode, uint32_t op2);
    int strOf(uint32_t opcode, uint32_t op2);
    int ldrdOf(uint32_t opcode, uint32_t op2);
    int strdOf(uint32_t opcode, uint32_t op2);
    int ldrsbPr(uint32_t opcode, uint32_t op2);
    int ldrshPr(uint32_t opcode, uint32_t op2);
    int ldrbPr(uint32_t opcode, uint32_t op2);
    int strbPr(uint32_t opcode, uint32_t op2);
    int ldrhPr(uint32_t opcode, uint32_t op2);
    int strhPr(uint32_t opcode, uint32_t op2);
    int ldrPr(uint32_t opcode, uint32_t op2);
    int strPr(uint32_t opcode, uint32_t op2);
    int ldrdPr(uint32_t opcode, uint32_t op2);
    int strdPr(uint32_t opcode, uint32_t op2);
    int ldrsbPt(uint32_t opcode, uint32_t op2);
    int ldrshPt(uint32_t opcode, uint32_t op2);
    int ldrbPt(uint32_t opcode, uint32_t op2);
    int strbPt(uint32_t opcode, uint32_t op2);
    int ldrhPt(uint32_t opcode, uint32_t op2);
    int strhPt(uint32_t opcode, uint32_t op2);
    int ldrPt(uint32_t opcode, uint32_t op2);
    int strPt(uint32_t opcode, uint32_t op2);
    int ldrdPt(uint32_t opcode, uint32_t op2);
    int strdPt(uint32_t opcode, uint32_t op2);
    ///@}

    /** @name */
    ///@{
    int ldrsbOfrm(uint32_t opcode);
    int ldrsbOfim(uint32_t opcode);
    int ldrsbOfrp(uint32_t opcode);
    int ldrsbOfip(uint32_t opcode);
    int ldrsbPrrm(uint32_t opcode);
    int ldrsbPrim(uint32_t opcode);
    int ldrsbPrrp(uint32_t opcode);
    int ldrsbPrip(uint32_t opcode);
    int ldrsbPtrm(uint32_t opcode);
    int ldrsbPtim(uint32_t opcode);
    int ldrsbPtrp(uint32_t opcode);
    int ldrsbPtip(uint32_t opcode);
    int ldrshOfrm(uint32_t opcode);
    int ldrshOfim(uint32_t opcode);
    int ldrshOfrp(uint32_t opcode);
    int ldrshOfip(uint32_t opcode);
    int ldrshPrrm(uint32_t opcode);
    int ldrshPrim(uint32_t opcode);
    int ldrshPrrp(uint32_t opcode);
    int ldrshPrip(uint32_t opcode);
    int ldrshPtrm(uint32_t opcode);
    int ldrshPtim(uint32_t opcode);
    int ldrshPtrp(uint32_t opcode);
    int ldrshPtip(uint32_t opcode);
    int ldrbOfim(uint32_t opcode);
    int ldrbOfip(uint32_t opcode);
    int ldrbOfrmll(uint32_t opcode);
    int ldrbOfrmlr(uint32_t opcode);
    int ldrbOfrmar(uint32_t opcode);
    int ldrbOfrmrr(uint32_t opcode);
    int ldrbOfrpll(uint32_t opcode);
    int ldrbOfrplr(uint32_t opcode);
    int ldrbOfrpar(uint32_t opcode);
    int ldrbOfrprr(uint32_t opcode);
    int ldrbPrim(uint32_t opcode);
    int ldrbPrip(uint32_t opcode);
    int ldrbPrrmll(uint32_t opcode);
    int ldrbPrrmlr(uint32_t opcode);
    int ldrbPrrmar(uint32_t opcode);
    int ldrbPrrmrr(uint32_t opcode);
    int ldrbPrrpll(uint32_t opcode);
    int ldrbPrrplr(uint32_t opcode);
    int ldrbPrrpar(uint32_t opcode);
    int ldrbPrrprr(uint32_t opcode);
    int ldrbPtim(uint32_t opcode);
    int ldrbPtip(uint32_t opcode);
    int ldrbPtrmll(uint32_t opcode);
    int ldrbPtrmlr(uint32_t opcode);
    int ldrbPtrmar(uint32_t opcode);
    int ldrbPtrmrr(uint32_t opcode);
    int ldrbPtrpll(uint32_t opcode);
    int ldrbPtrplr(uint32_t opcode);
    int ldrbPtrpar(uint32_t opcode);
    int ldrbPtrprr(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int strbOfim(uint32_t opcode);
    int strbOfip(uint32_t opcode);
    int strbOfrmll(uint32_t opcode);
    int strbOfrmlr(uint32_t opcode);
    int strbOfrmar(uint32_t opcode);
    int strbOfrmrr(uint32_t opcode);
    int strbOfrpll(uint32_t opcode);
    int strbOfrplr(uint32_t opcode);
    int strbOfrpar(uint32_t opcode);
    int strbOfrprr(uint32_t opcode);
    int strbPrim(uint32_t opcode);
    int strbPrip(uint32_t opcode);
    int strbPrrmll(uint32_t opcode);
    int strbPrrmlr(uint32_t opcode);
    int strbPrrmar(uint32_t opcode);
    int strbPrrmrr(uint32_t opcode);
    int strbPrrpll(uint32_t opcode);
    int strbPrrplr(uint32_t opcode);
    int strbPrrpar(uint32_t opcode);
    int strbPrrprr(uint32_t opcode);
    int strbPtim(uint32_t opcode);
    int strbPtip(uint32_t opcode);
    int strbPtrmll(uint32_t opcode);
    int strbPtrmlr(uint32_t opcode);
    int strbPtrmar(uint32_t opcode);
    int strbPtrmrr(uint32_t opcode);
    int strbPtrpll(uint32_t opcode);
    int strbPtrplr(uint32_t opcode);
    int strbPtrpar(uint32_t opcode);
    int strbPtrprr(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int ldrhOfrm(uint32_t opcode);
    int ldrhOfim(uint32_t opcode);
    int ldrhOfrp(uint32_t opcode);
    int ldrhOfip(uint32_t opcode);
    int ldrhPrrm(uint32_t opcode);
    int ldrhPrim(uint32_t opcode);
    int ldrhPrrp(uint32_t opcode);
    int ldrhPrip(uint32_t opcode);
    int ldrhPtrm(uint32_t opcode);
    int ldrhPtim(uint32_t opcode);
    int ldrhPtrp(uint32_t opcode);
    int ldrhPtip(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int strhOfrm(uint32_t opcode);
    int strhOfim(uint32_t opcode);
    int strhOfrp(uint32_t opcode);
    int strhOfip(uint32_t opcode);
    int strhPrrm(uint32_t opcode);
    int strhPrim(uint32_t opcode);
    int strhPrrp(uint32_t opcode);
    int strhPrip(uint32_t opcode);
    int strhPtrm(uint32_t opcode);
    int strhPtim(uint32_t opcode);
    int strhPtrp(uint32_t opcode);
    int strhPtip(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int ldrOfim(uint32_t opcode);
    int ldrOfip(uint32_t opcode);
    int ldrOfrmll(uint32_t opcode);
    int ldrOfrmlr(uint32_t opcode);
    int ldrOfrmar(uint32_t opcode);
    int ldrOfrmrr(uint32_t opcode);
    int ldrOfrpll(uint32_t opcode);
    int ldrOfrplr(uint32_t opcode);
    int ldrOfrpar(uint32_t opcode);
    int ldrOfrprr(uint32_t opcode);
    int ldrPrim(uint32_t opcode);
    int ldrPrip(uint32_t opcode);
    int ldrPrrmll(uint32_t opcode);
    int ldrPrrmlr(uint32_t opcode);
    int ldrPrrmar(uint32_t opcode);
    int ldrPrrmrr(uint32_t opcode);
    int ldrPrrpll(uint32_t opcode);
    int ldrPrrplr(uint32_t opcode);
    int ldrPrrpar(uint32_t opcode);
    int ldrPrrprr(uint32_t opcode);
    int ldrPtim(uint32_t opcode);
    int ldrPtip(uint32_t opcode);
    int ldrPtrmll(uint32_t opcode);
    int ldrPtrmlr(uint32_t opcode);
    int ldrPtrmar(uint32_t opcode);
    int ldrPtrmrr(uint32_t opcode);
    int ldrPtrpll(uint32_t opcode);
    int ldrPtrplr(uint32_t opcode);
    int ldrPtrpar(uint32_t opcode);
    int ldrPtrprr(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int strOfim(uint32_t opcode);
    int strOfip(uint32_t opcode);
    int strOfrmll(uint32_t opcode);
    int strOfrmlr(uint32_t opcode);
    int strOfrmar(uint32_t opcode);
    int strOfrmrr(uint32_t opcode);
    int strOfrpll(uint32_t opcode);
    int strOfrplr(uint32_t opcode);
    int strOfrpar(uint32_t opcode);
    int strOfrprr(uint32_t opcode);
    int strPrim(uint32_t opcode);
    int strPrip(uint32_t opcode);
    int strPrrmll(uint32_t opcode);
    int strPrrmlr(uint32_t opcode);
    int strPrrmar(uint32_t opcode);
    int strPrrmrr(uint32_t opcode);
    int strPrrpll(uint32_t opcode);
    int strPrrplr(uint32_t opcode);
    int strPrrpar(uint32_t opcode);
    int strPrrprr(uint32_t opcode);
    int strPtim(uint32_t opcode);
    int strPtip(uint32_t opcode);
    int strPtrmll(uint32_t opcode);
    int strPtrmlr(uint32_t opcode);
    int strPtrmar(uint32_t opcode);
    int strPtrmrr(uint32_t opcode);
    int strPtrpll(uint32_t opcode);
    int strPtrplr(uint32_t opcode);
    int strPtrpar(uint32_t opcode);
    int strPtrprr(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int ldrdOfrm(uint32_t opcode);
    int ldrdOfim(uint32_t opcode);
    int ldrdOfrp(uint32_t opcode);
    int ldrdOfip(uint32_t opcode);
    int ldrdPrrm(uint32_t opcode);
    int ldrdPrim(uint32_t opcode);
    int ldrdPrrp(uint32_t opcode);
    int ldrdPrip(uint32_t opcode);
    int ldrdPtrm(uint32_t opcode);
    int ldrdPtim(uint32_t opcode);
    int ldrdPtrp(uint32_t opcode);
    int ldrdPtip(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int strdOfrm(uint32_t opcode);
    int strdOfim(uint32_t opcode);
    int strdOfrp(uint32_t opcode);
    int strdOfip(uint32_t opcode);
    int strdPrrm(uint32_t opcode);
    int strdPrim(uint32_t opcode);
    int strdPrrp(uint32_t opcode);
    int strdPrip(uint32_t opcode);
    int strdPtrm(uint32_t opcode);
    int strdPtim(uint32_t opcode);
    int strdPtrp(uint32_t opcode);
    int strdPtip(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int swpb(uint32_t opcode);
    int swp(uint32_t opcode);
    int ldmda(uint32_t opcode);
    int stmda(uint32_t opcode);
    int ldmia(uint32_t opcode);
    int stmia(uint32_t opcode);
    int ldmdb(uint32_t opcode);
    int stmdb(uint32_t opcode);
    int ldmib(uint32_t opcode);
    int stmib(uint32_t opcode);
    int ldmdaW(uint32_t opcode);
    int stmdaW(uint32_t opcode);
    int ldmiaW(uint32_t opcode);
    int stmiaW(uint32_t opcode);
    int ldmdbW(uint32_t opcode);
    int stmdbW(uint32_t opcode);
    int ldmibW(uint32_t opcode);
    int stmibW(uint32_t opcode);
    int ldmdaU(uint32_t opcode);
    int stmdaU(uint32_t opcode);
    int ldmiaU(uint32_t opcode);
    int stmiaU(uint32_t opcode);
    int ldmdbU(uint32_t opcode);
    int stmdbU(uint32_t opcode);
    int ldmibU(uint32_t opcode);
    int stmibU(uint32_t opcode);
    int ldmdaUW(uint32_t opcode);
    int stmdaUW(uint32_t opcode);
    int ldmiaUW(uint32_t opcode);
    int stmiaUW(uint32_t opcode);
    int ldmdbUW(uint32_t opcode);
    int stmdbUW(uint32_t opcode);
    int ldmibUW(uint32_t opcode);
    int stmibUW(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int msrRc(uint32_t opcode);
    int msrRs(uint32_t opcode);
    int msrIc(uint32_t opcode);
    int msrIs(uint32_t opcode);
    int mrsRc(uint32_t opcode);
    int mrsRs(uint32_t opcode);
    int mrc(uint32_t opcode);
    int mcr(uint32_t opcode);
    ///@}

    /** @name */
    ///@{
    int ldrsbRegT(uint16_t opcode);
    int ldrshRegT(uint16_t opcode);
    int ldrbRegT(uint16_t opcode);
    int strbRegT(uint16_t opcode);
    int ldrhRegT(uint16_t opcode);
    int strhRegT(uint16_t opcode);
    int ldrRegT(uint16_t opcode);
    int strRegT(uint16_t opcode);
    ///@}

    /** @name */
    ///@{
    int ldrbImm5T(uint16_t opcode);
    int strbImm5T(uint16_t opcode);
    int ldrhImm5T(uint16_t opcode);
    int strhImm5T(uint16_t opcode);
    int ldrImm5T(uint16_t opcode);
    int strImm5T(uint16_t opcode);
    int ldrPcT(uint16_t opcode);
    int ldrSpT(uint16_t opcode);
    int strSpT(uint16_t opcode);
    int ldmiaT(uint16_t opcode);
    int stmiaT(uint16_t opcode);
    int popT(uint16_t opcode);
    int pushT(uint16_t opcode);
    int popPcT(uint16_t opcode);
    int pushLrT(uint16_t opcode);
    int bx(uint32_t opcode);
    int blxReg(uint32_t opcode);
    int b(uint32_t opcode);
    int bl(uint32_t opcode);
    int blx(uint32_t opcode);
    int swi(uint32_t opcode);
    int bxRegT(uint16_t opcode);
    int blxRegT(uint16_t opcode);
    int bT(uint16_t opcode);
    int beqT(uint16_t opcode);
    int bneT(uint16_t opcode);
    int bcsT(uint16_t opcode);
    int bccT(uint16_t opcode);
    int bmiT(uint16_t opcode);
    int bplT(uint16_t opcode);
    int bvsT(uint16_t opcode);
    int bvcT(uint16_t opcode);
    int bhiT(uint16_t opcode);
    int blsT(uint16_t opcode);
    int bgeT(uint16_t opcode);
    int bltT(uint16_t opcode);
    int bgtT(uint16_t opcode);
    int bleT(uint16_t opcode);
    int blSetupT(uint16_t opcode);
    int blOffT(uint16_t opcode);
    int blxOffT(uint16_t opcode);
    int swiT(uint16_t opcode);
    ///@}

  private:
    int (CPU::*armInstrs[0x1000])(uint32_t) = {
        &CPU::andLli,     &CPU::andLlr,    &CPU::andLri,     &CPU::andLrr,    &CPU::andAri,     &CPU::andArr,
        &CPU::andRri,     &CPU::andRrr,    &CPU::andLli,     &CPU::mul,       &CPU::andLri,     &CPU::strhPtrm,
        &CPU::andAri,     &CPU::ldrdPtrm,  &CPU::andRri,     &CPU::strdPtrm,  &CPU::andsLli,    &CPU::andsLlr,
        &CPU::andsLri,    &CPU::andsLrr,   &CPU::andsAri,    &CPU::andsArr,   &CPU::andsRri,    &CPU::andsRrr,
        &CPU::andsLli,    &CPU::muls,      &CPU::andsLri,    &CPU::ldrhPtrm,  &CPU::andsAri,    &CPU::ldrsbPtrm,
        &CPU::andsRri,    &CPU::ldrshPtrm, &CPU::eorLli,     &CPU::eorLlr,    &CPU::eorLri,     &CPU::eorLrr,
        &CPU::eorAri,     &CPU::eorArr,    &CPU::eorRri,     &CPU::eorRrr,    &CPU::eorLli,     &CPU::mla,
        &CPU::eorLri,     &CPU::strhPtrm,  &CPU::eorAri,     &CPU::ldrdPtrm,  &CPU::eorRri,     &CPU::strdPtrm,
        &CPU::eorsLli,    &CPU::eorsLlr,   &CPU::eorsLri,    &CPU::eorsLrr,   &CPU::eorsAri,    &CPU::eorsArr,
        &CPU::eorsRri,    &CPU::eorsRrr,   &CPU::eorsLli,    &CPU::mlas,      &CPU::eorsLri,    &CPU::ldrhPtrm,
        &CPU::eorsAri,    &CPU::ldrsbPtrm, &CPU::eorsRri,    &CPU::ldrshPtrm, &CPU::subLli,     &CPU::subLlr,
        &CPU::subLri,     &CPU::subLrr,    &CPU::subAri,     &CPU::subArr,    &CPU::subRri,     &CPU::subRrr,
        &CPU::subLli,     &CPU::unkArm,    &CPU::subLri,     &CPU::strhPtim,  &CPU::subAri,     &CPU::ldrdPtim,
        &CPU::subRri,     &CPU::strdPtim,  &CPU::subsLli,    &CPU::subsLlr,   &CPU::subsLri,    &CPU::subsLrr,
        &CPU::subsAri,    &CPU::subsArr,   &CPU::subsRri,    &CPU::subsRrr,   &CPU::subsLli,    &CPU::unkArm,
        &CPU::subsLri,    &CPU::ldrhPtim,  &CPU::subsAri,    &CPU::ldrsbPtim, &CPU::subsRri,    &CPU::ldrshPtim,
        &CPU::rsbLli,     &CPU::rsbLlr,    &CPU::rsbLri,     &CPU::rsbLrr,    &CPU::rsbAri,     &CPU::rsbArr,
        &CPU::rsbRri,     &CPU::rsbRrr,    &CPU::rsbLli,     &CPU::unkArm,    &CPU::rsbLri,     &CPU::strhPtim,
        &CPU::rsbAri,     &CPU::ldrdPtim,  &CPU::rsbRri,     &CPU::strdPtim,  &CPU::rsbsLli,    &CPU::rsbsLlr,
        &CPU::rsbsLri,    &CPU::rsbsLrr,   &CPU::rsbsAri,    &CPU::rsbsArr,   &CPU::rsbsRri,    &CPU::rsbsRrr,
        &CPU::rsbsLli,    &CPU::unkArm,    &CPU::rsbsLri,    &CPU::ldrhPtim,  &CPU::rsbsAri,    &CPU::ldrsbPtim,
        &CPU::rsbsRri,    &CPU::ldrshPtim, &CPU::addLli,     &CPU::addLlr,    &CPU::addLri,     &CPU::addLrr,
        &CPU::addAri,     &CPU::addArr,    &CPU::addRri,     &CPU::addRrr,    &CPU::addLli,     &CPU::umull,
        &CPU::addLri,     &CPU::strhPtrp,  &CPU::addAri,     &CPU::ldrdPtrp,  &CPU::addRri,     &CPU::strdPtrp,
        &CPU::addsLli,    &CPU::addsLlr,   &CPU::addsLri,    &CPU::addsLrr,   &CPU::addsAri,    &CPU::addsArr,
        &CPU::addsRri,    &CPU::addsRrr,   &CPU::addsLli,    &CPU::umulls,    &CPU::addsLri,    &CPU::ldrhPtrp,
        &CPU::addsAri,    &CPU::ldrsbPtrp, &CPU::addsRri,    &CPU::ldrshPtrp, &CPU::adcLli,     &CPU::adcLlr,
        &CPU::adcLri,     &CPU::adcLrr,    &CPU::adcAri,     &CPU::adcArr,    &CPU::adcRri,     &CPU::adcRrr,
        &CPU::adcLli,     &CPU::umlal,     &CPU::adcLri,     &CPU::strhPtrp,  &CPU::adcAri,     &CPU::ldrdPtrp,
        &CPU::adcRri,     &CPU::strdPtrp,  &CPU::adcsLli,    &CPU::adcsLlr,   &CPU::adcsLri,    &CPU::adcsLrr,
        &CPU::adcsAri,    &CPU::adcsArr,   &CPU::adcsRri,    &CPU::adcsRrr,   &CPU::adcsLli,    &CPU::umlals,
        &CPU::adcsLri,    &CPU::ldrhPtrp,  &CPU::adcsAri,    &CPU::ldrsbPtrp, &CPU::adcsRri,    &CPU::ldrshPtrp,
        &CPU::sbcLli,     &CPU::sbcLlr,    &CPU::sbcLri,     &CPU::sbcLrr,    &CPU::sbcAri,     &CPU::sbcArr,
        &CPU::sbcRri,     &CPU::sbcRrr,    &CPU::sbcLli,     &CPU::smull,     &CPU::sbcLri,     &CPU::strhPtip,
        &CPU::sbcAri,     &CPU::ldrdPtip,  &CPU::sbcRri,     &CPU::strdPtip,  &CPU::sbcsLli,    &CPU::sbcsLlr,
        &CPU::sbcsLri,    &CPU::sbcsLrr,   &CPU::sbcsAri,    &CPU::sbcsArr,   &CPU::sbcsRri,    &CPU::sbcsRrr,
        &CPU::sbcsLli,    &CPU::smulls,    &CPU::sbcsLri,    &CPU::ldrhPtip,  &CPU::sbcsAri,    &CPU::ldrsbPtip,
        &CPU::sbcsRri,    &CPU::ldrshPtip, &CPU::rscLli,     &CPU::rscLlr,    &CPU::rscLri,     &CPU::rscLrr,
        &CPU::rscAri,     &CPU::rscArr,    &CPU::rscRri,     &CPU::rscRrr,    &CPU::rscLli,     &CPU::smlal,
        &CPU::rscLri,     &CPU::strhPtip,  &CPU::rscAri,     &CPU::ldrdPtip,  &CPU::rscRri,     &CPU::strdPtip,
        &CPU::rscsLli,    &CPU::rscsLlr,   &CPU::rscsLri,    &CPU::rscsLrr,   &CPU::rscsAri,    &CPU::rscsArr,
        &CPU::rscsRri,    &CPU::rscsRrr,   &CPU::rscsLli,    &CPU::smlals,    &CPU::rscsLri,    &CPU::ldrhPtip,
        &CPU::rscsAri,    &CPU::ldrsbPtip, &CPU::rscsRri,    &CPU::ldrshPtip, &CPU::mrsRc,      &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::qadd,      &CPU::unkArm,     &CPU::unkArm,
        &CPU::smlabb,     &CPU::swp,       &CPU::smlatb,     &CPU::strhOfrm,  &CPU::smlabt,     &CPU::ldrdOfrm,
        &CPU::smlatt,     &CPU::strdOfrm,  &CPU::tstLli,     &CPU::tstLlr,    &CPU::tstLri,     &CPU::tstLrr,
        &CPU::tstAri,     &CPU::tstArr,    &CPU::tstRri,     &CPU::tstRrr,    &CPU::tstLli,     &CPU::unkArm,
        &CPU::tstLri,     &CPU::ldrhOfrm,  &CPU::tstAri,     &CPU::ldrsbOfrm, &CPU::tstRri,     &CPU::ldrshOfrm,
        &CPU::msrRc,      &CPU::bx,        &CPU::unkArm,     &CPU::blxReg,    &CPU::unkArm,     &CPU::qsub,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::smlawb,     &CPU::unkArm,    &CPU::smulwb,     &CPU::strhPrrm,
        &CPU::smlawt,     &CPU::ldrdPrrm,  &CPU::smulwt,     &CPU::strdPrrm,  &CPU::teqLli,     &CPU::teqLlr,
        &CPU::teqLri,     &CPU::teqLrr,    &CPU::teqAri,     &CPU::teqArr,    &CPU::teqRri,     &CPU::teqRrr,
        &CPU::teqLli,     &CPU::unkArm,    &CPU::teqLri,     &CPU::ldrhPrrm,  &CPU::teqAri,     &CPU::ldrsbPrrm,
        &CPU::teqRri,     &CPU::ldrshPrrm, &CPU::mrsRs,      &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::qdadd,     &CPU::unkArm,     &CPU::unkArm,    &CPU::smlalbb,    &CPU::swpb,
        &CPU::smlaltb,    &CPU::strhOfim,  &CPU::smlalbt,    &CPU::ldrdOfim,  &CPU::smlaltt,    &CPU::strdOfim,
        &CPU::cmpLli,     &CPU::cmpLlr,    &CPU::cmpLri,     &CPU::cmpLrr,    &CPU::cmpAri,     &CPU::cmpArr,
        &CPU::cmpRri,     &CPU::cmpRrr,    &CPU::cmpLli,     &CPU::unkArm,    &CPU::cmpLri,     &CPU::ldrhOfim,
        &CPU::cmpAri,     &CPU::ldrsbOfim, &CPU::cmpRri,     &CPU::ldrshOfim, &CPU::msrRs,      &CPU::clz,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::qdsub,     &CPU::unkArm,     &CPU::unkArm,
        &CPU::smulbb,     &CPU::unkArm,    &CPU::smultb,     &CPU::strhPrim,  &CPU::smulbt,     &CPU::ldrdPrim,
        &CPU::smultt,     &CPU::strdPrim,  &CPU::cmnLli,     &CPU::cmnLlr,    &CPU::cmnLri,     &CPU::cmnLrr,
        &CPU::cmnAri,     &CPU::cmnArr,    &CPU::cmnRri,     &CPU::cmnRrr,    &CPU::cmnLli,     &CPU::unkArm,
        &CPU::cmnLri,     &CPU::ldrhPrim,  &CPU::cmnAri,     &CPU::ldrsbPrim, &CPU::cmnRri,     &CPU::ldrshPrim,
        &CPU::orrLli,     &CPU::orrLlr,    &CPU::orrLri,     &CPU::orrLrr,    &CPU::orrAri,     &CPU::orrArr,
        &CPU::orrRri,     &CPU::orrRrr,    &CPU::orrLli,     &CPU::unkArm,    &CPU::orrLri,     &CPU::strhOfrp,
        &CPU::orrAri,     &CPU::ldrdOfrp,  &CPU::orrRri,     &CPU::strdOfrp,  &CPU::orrsLli,    &CPU::orrsLlr,
        &CPU::orrsLri,    &CPU::orrsLrr,   &CPU::orrsAri,    &CPU::orrsArr,   &CPU::orrsRri,    &CPU::orrsRrr,
        &CPU::orrsLli,    &CPU::unkArm,    &CPU::orrsLri,    &CPU::ldrhOfrp,  &CPU::orrsAri,    &CPU::ldrsbOfrp,
        &CPU::orrsRri,    &CPU::ldrshOfrp, &CPU::movLli,     &CPU::movLlr,    &CPU::movLri,     &CPU::movLrr,
        &CPU::movAri,     &CPU::movArr,    &CPU::movRri,     &CPU::movRrr,    &CPU::movLli,     &CPU::unkArm,
        &CPU::movLri,     &CPU::strhPrrp,  &CPU::movAri,     &CPU::ldrdPrrp,  &CPU::movRri,     &CPU::strdPrrp,
        &CPU::movsLli,    &CPU::movsLlr,   &CPU::movsLri,    &CPU::movsLrr,   &CPU::movsAri,    &CPU::movsArr,
        &CPU::movsRri,    &CPU::movsRrr,   &CPU::movsLli,    &CPU::unkArm,    &CPU::movsLri,    &CPU::ldrhPrrp,
        &CPU::movsAri,    &CPU::ldrsbPrrp, &CPU::movsRri,    &CPU::ldrshPrrp, &CPU::bicLli,     &CPU::bicLlr,
        &CPU::bicLri,     &CPU::bicLrr,    &CPU::bicAri,     &CPU::bicArr,    &CPU::bicRri,     &CPU::bicRrr,
        &CPU::bicLli,     &CPU::unkArm,    &CPU::bicLri,     &CPU::strhOfip,  &CPU::bicAri,     &CPU::ldrdOfip,
        &CPU::bicRri,     &CPU::strdOfip,  &CPU::bicsLli,    &CPU::bicsLlr,   &CPU::bicsLri,    &CPU::bicsLrr,
        &CPU::bicsAri,    &CPU::bicsArr,   &CPU::bicsRri,    &CPU::bicsRrr,   &CPU::bicsLli,    &CPU::unkArm,
        &CPU::bicsLri,    &CPU::ldrhOfip,  &CPU::bicsAri,    &CPU::ldrsbOfip, &CPU::bicsRri,    &CPU::ldrshOfip,
        &CPU::mvnLli,     &CPU::mvnLlr,    &CPU::mvnLri,     &CPU::mvnLrr,    &CPU::mvnAri,     &CPU::mvnArr,
        &CPU::mvnRri,     &CPU::mvnRrr,    &CPU::mvnLli,     &CPU::unkArm,    &CPU::mvnLri,     &CPU::strhPrip,
        &CPU::mvnAri,     &CPU::ldrdPrip,  &CPU::mvnRri,     &CPU::strdPrip,  &CPU::mvnsLli,    &CPU::mvnsLlr,
        &CPU::mvnsLri,    &CPU::mvnsLrr,   &CPU::mvnsAri,    &CPU::mvnsArr,   &CPU::mvnsRri,    &CPU::mvnsRrr,
        &CPU::mvnsLli,    &CPU::unkArm,    &CPU::mvnsLri,    &CPU::ldrhPrip,  &CPU::mvnsAri,    &CPU::ldrsbPrip,
        &CPU::mvnsRri,    &CPU::ldrshPrip, &CPU::andImm,     &CPU::andImm,    &CPU::andImm,     &CPU::andImm,
        &CPU::andImm,     &CPU::andImm,    &CPU::andImm,     &CPU::andImm,    &CPU::andImm,     &CPU::andImm,
        &CPU::andImm,     &CPU::andImm,    &CPU::andImm,     &CPU::andImm,    &CPU::andImm,     &CPU::andImm,
        &CPU::andsImm,    &CPU::andsImm,   &CPU::andsImm,    &CPU::andsImm,   &CPU::andsImm,    &CPU::andsImm,
        &CPU::andsImm,    &CPU::andsImm,   &CPU::andsImm,    &CPU::andsImm,   &CPU::andsImm,    &CPU::andsImm,
        &CPU::andsImm,    &CPU::andsImm,   &CPU::andsImm,    &CPU::andsImm,   &CPU::eorImm,     &CPU::eorImm,
        &CPU::eorImm,     &CPU::eorImm,    &CPU::eorImm,     &CPU::eorImm,    &CPU::eorImm,     &CPU::eorImm,
        &CPU::eorImm,     &CPU::eorImm,    &CPU::eorImm,     &CPU::eorImm,    &CPU::eorImm,     &CPU::eorImm,
        &CPU::eorImm,     &CPU::eorImm,    &CPU::eorsImm,    &CPU::eorsImm,   &CPU::eorsImm,    &CPU::eorsImm,
        &CPU::eorsImm,    &CPU::eorsImm,   &CPU::eorsImm,    &CPU::eorsImm,   &CPU::eorsImm,    &CPU::eorsImm,
        &CPU::eorsImm,    &CPU::eorsImm,   &CPU::eorsImm,    &CPU::eorsImm,   &CPU::eorsImm,    &CPU::eorsImm,
        &CPU::subImm,     &CPU::subImm,    &CPU::subImm,     &CPU::subImm,    &CPU::subImm,     &CPU::subImm,
        &CPU::subImm,     &CPU::subImm,    &CPU::subImm,     &CPU::subImm,    &CPU::subImm,     &CPU::subImm,
        &CPU::subImm,     &CPU::subImm,    &CPU::subImm,     &CPU::subImm,    &CPU::subsImm,    &CPU::subsImm,
        &CPU::subsImm,    &CPU::subsImm,   &CPU::subsImm,    &CPU::subsImm,   &CPU::subsImm,    &CPU::subsImm,
        &CPU::subsImm,    &CPU::subsImm,   &CPU::subsImm,    &CPU::subsImm,   &CPU::subsImm,    &CPU::subsImm,
        &CPU::subsImm,    &CPU::subsImm,   &CPU::rsbImm,     &CPU::rsbImm,    &CPU::rsbImm,     &CPU::rsbImm,
        &CPU::rsbImm,     &CPU::rsbImm,    &CPU::rsbImm,     &CPU::rsbImm,    &CPU::rsbImm,     &CPU::rsbImm,
        &CPU::rsbImm,     &CPU::rsbImm,    &CPU::rsbImm,     &CPU::rsbImm,    &CPU::rsbImm,     &CPU::rsbImm,
        &CPU::rsbsImm,    &CPU::rsbsImm,   &CPU::rsbsImm,    &CPU::rsbsImm,   &CPU::rsbsImm,    &CPU::rsbsImm,
        &CPU::rsbsImm,    &CPU::rsbsImm,   &CPU::rsbsImm,    &CPU::rsbsImm,   &CPU::rsbsImm,    &CPU::rsbsImm,
        &CPU::rsbsImm,    &CPU::rsbsImm,   &CPU::rsbsImm,    &CPU::rsbsImm,   &CPU::addImm,     &CPU::addImm,
        &CPU::addImm,     &CPU::addImm,    &CPU::addImm,     &CPU::addImm,    &CPU::addImm,     &CPU::addImm,
        &CPU::addImm,     &CPU::addImm,    &CPU::addImm,     &CPU::addImm,    &CPU::addImm,     &CPU::addImm,
        &CPU::addImm,     &CPU::addImm,    &CPU::addsImm,    &CPU::addsImm,   &CPU::addsImm,    &CPU::addsImm,
        &CPU::addsImm,    &CPU::addsImm,   &CPU::addsImm,    &CPU::addsImm,   &CPU::addsImm,    &CPU::addsImm,
        &CPU::addsImm,    &CPU::addsImm,   &CPU::addsImm,    &CPU::addsImm,   &CPU::addsImm,    &CPU::addsImm,
        &CPU::adcImm,     &CPU::adcImm,    &CPU::adcImm,     &CPU::adcImm,    &CPU::adcImm,     &CPU::adcImm,
        &CPU::adcImm,     &CPU::adcImm,    &CPU::adcImm,     &CPU::adcImm,    &CPU::adcImm,     &CPU::adcImm,
        &CPU::adcImm,     &CPU::adcImm,    &CPU::adcImm,     &CPU::adcImm,    &CPU::adcsImm,    &CPU::adcsImm,
        &CPU::adcsImm,    &CPU::adcsImm,   &CPU::adcsImm,    &CPU::adcsImm,   &CPU::adcsImm,    &CPU::adcsImm,
        &CPU::adcsImm,    &CPU::adcsImm,   &CPU::adcsImm,    &CPU::adcsImm,   &CPU::adcsImm,    &CPU::adcsImm,
        &CPU::adcsImm,    &CPU::adcsImm,   &CPU::sbcImm,     &CPU::sbcImm,    &CPU::sbcImm,     &CPU::sbcImm,
        &CPU::sbcImm,     &CPU::sbcImm,    &CPU::sbcImm,     &CPU::sbcImm,    &CPU::sbcImm,     &CPU::sbcImm,
        &CPU::sbcImm,     &CPU::sbcImm,    &CPU::sbcImm,     &CPU::sbcImm,    &CPU::sbcImm,     &CPU::sbcImm,
        &CPU::sbcsImm,    &CPU::sbcsImm,   &CPU::sbcsImm,    &CPU::sbcsImm,   &CPU::sbcsImm,    &CPU::sbcsImm,
        &CPU::sbcsImm,    &CPU::sbcsImm,   &CPU::sbcsImm,    &CPU::sbcsImm,   &CPU::sbcsImm,    &CPU::sbcsImm,
        &CPU::sbcsImm,    &CPU::sbcsImm,   &CPU::sbcsImm,    &CPU::sbcsImm,   &CPU::rscImm,     &CPU::rscImm,
        &CPU::rscImm,     &CPU::rscImm,    &CPU::rscImm,     &CPU::rscImm,    &CPU::rscImm,     &CPU::rscImm,
        &CPU::rscImm,     &CPU::rscImm,    &CPU::rscImm,     &CPU::rscImm,    &CPU::rscImm,     &CPU::rscImm,
        &CPU::rscImm,     &CPU::rscImm,    &CPU::rscsImm,    &CPU::rscsImm,   &CPU::rscsImm,    &CPU::rscsImm,
        &CPU::rscsImm,    &CPU::rscsImm,   &CPU::rscsImm,    &CPU::rscsImm,   &CPU::rscsImm,    &CPU::rscsImm,
        &CPU::rscsImm,    &CPU::rscsImm,   &CPU::rscsImm,    &CPU::rscsImm,   &CPU::rscsImm,    &CPU::rscsImm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::tstImm,     &CPU::tstImm,
        &CPU::tstImm,     &CPU::tstImm,    &CPU::tstImm,     &CPU::tstImm,    &CPU::tstImm,     &CPU::tstImm,
        &CPU::tstImm,     &CPU::tstImm,    &CPU::tstImm,     &CPU::tstImm,    &CPU::tstImm,     &CPU::tstImm,
        &CPU::tstImm,     &CPU::tstImm,    &CPU::msrIc,      &CPU::msrIc,     &CPU::msrIc,      &CPU::msrIc,
        &CPU::msrIc,      &CPU::msrIc,     &CPU::msrIc,      &CPU::msrIc,     &CPU::msrIc,      &CPU::msrIc,
        &CPU::msrIc,      &CPU::msrIc,     &CPU::msrIc,      &CPU::msrIc,     &CPU::msrIc,      &CPU::msrIc,
        &CPU::teqImm,     &CPU::teqImm,    &CPU::teqImm,     &CPU::teqImm,    &CPU::teqImm,     &CPU::teqImm,
        &CPU::teqImm,     &CPU::teqImm,    &CPU::teqImm,     &CPU::teqImm,    &CPU::teqImm,     &CPU::teqImm,
        &CPU::teqImm,     &CPU::teqImm,    &CPU::teqImm,     &CPU::teqImm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::cmpImm,     &CPU::cmpImm,    &CPU::cmpImm,     &CPU::cmpImm,
        &CPU::cmpImm,     &CPU::cmpImm,    &CPU::cmpImm,     &CPU::cmpImm,    &CPU::cmpImm,     &CPU::cmpImm,
        &CPU::cmpImm,     &CPU::cmpImm,    &CPU::cmpImm,     &CPU::cmpImm,    &CPU::cmpImm,     &CPU::cmpImm,
        &CPU::msrIs,      &CPU::msrIs,     &CPU::msrIs,      &CPU::msrIs,     &CPU::msrIs,      &CPU::msrIs,
        &CPU::msrIs,      &CPU::msrIs,     &CPU::msrIs,      &CPU::msrIs,     &CPU::msrIs,      &CPU::msrIs,
        &CPU::msrIs,      &CPU::msrIs,     &CPU::msrIs,      &CPU::msrIs,     &CPU::cmnImm,     &CPU::cmnImm,
        &CPU::cmnImm,     &CPU::cmnImm,    &CPU::cmnImm,     &CPU::cmnImm,    &CPU::cmnImm,     &CPU::cmnImm,
        &CPU::cmnImm,     &CPU::cmnImm,    &CPU::cmnImm,     &CPU::cmnImm,    &CPU::cmnImm,     &CPU::cmnImm,
        &CPU::cmnImm,     &CPU::cmnImm,    &CPU::orrImm,     &CPU::orrImm,    &CPU::orrImm,     &CPU::orrImm,
        &CPU::orrImm,     &CPU::orrImm,    &CPU::orrImm,     &CPU::orrImm,    &CPU::orrImm,     &CPU::orrImm,
        &CPU::orrImm,     &CPU::orrImm,    &CPU::orrImm,     &CPU::orrImm,    &CPU::orrImm,     &CPU::orrImm,
        &CPU::orrsImm,    &CPU::orrsImm,   &CPU::orrsImm,    &CPU::orrsImm,   &CPU::orrsImm,    &CPU::orrsImm,
        &CPU::orrsImm,    &CPU::orrsImm,   &CPU::orrsImm,    &CPU::orrsImm,   &CPU::orrsImm,    &CPU::orrsImm,
        &CPU::orrsImm,    &CPU::orrsImm,   &CPU::orrsImm,    &CPU::orrsImm,   &CPU::movImm,     &CPU::movImm,
        &CPU::movImm,     &CPU::movImm,    &CPU::movImm,     &CPU::movImm,    &CPU::movImm,     &CPU::movImm,
        &CPU::movImm,     &CPU::movImm,    &CPU::movImm,     &CPU::movImm,    &CPU::movImm,     &CPU::movImm,
        &CPU::movImm,     &CPU::movImm,    &CPU::movsImm,    &CPU::movsImm,   &CPU::movsImm,    &CPU::movsImm,
        &CPU::movsImm,    &CPU::movsImm,   &CPU::movsImm,    &CPU::movsImm,   &CPU::movsImm,    &CPU::movsImm,
        &CPU::movsImm,    &CPU::movsImm,   &CPU::movsImm,    &CPU::movsImm,   &CPU::movsImm,    &CPU::movsImm,
        &CPU::bicImm,     &CPU::bicImm,    &CPU::bicImm,     &CPU::bicImm,    &CPU::bicImm,     &CPU::bicImm,
        &CPU::bicImm,     &CPU::bicImm,    &CPU::bicImm,     &CPU::bicImm,    &CPU::bicImm,     &CPU::bicImm,
        &CPU::bicImm,     &CPU::bicImm,    &CPU::bicImm,     &CPU::bicImm,    &CPU::bicsImm,    &CPU::bicsImm,
        &CPU::bicsImm,    &CPU::bicsImm,   &CPU::bicsImm,    &CPU::bicsImm,   &CPU::bicsImm,    &CPU::bicsImm,
        &CPU::bicsImm,    &CPU::bicsImm,   &CPU::bicsImm,    &CPU::bicsImm,   &CPU::bicsImm,    &CPU::bicsImm,
        &CPU::bicsImm,    &CPU::bicsImm,   &CPU::mvnImm,     &CPU::mvnImm,    &CPU::mvnImm,     &CPU::mvnImm,
        &CPU::mvnImm,     &CPU::mvnImm,    &CPU::mvnImm,     &CPU::mvnImm,    &CPU::mvnImm,     &CPU::mvnImm,
        &CPU::mvnImm,     &CPU::mvnImm,    &CPU::mvnImm,     &CPU::mvnImm,    &CPU::mvnImm,     &CPU::mvnImm,
        &CPU::mvnsImm,    &CPU::mvnsImm,   &CPU::mvnsImm,    &CPU::mvnsImm,   &CPU::mvnsImm,    &CPU::mvnsImm,
        &CPU::mvnsImm,    &CPU::mvnsImm,   &CPU::mvnsImm,    &CPU::mvnsImm,   &CPU::mvnsImm,    &CPU::mvnsImm,
        &CPU::mvnsImm,    &CPU::mvnsImm,   &CPU::mvnsImm,    &CPU::mvnsImm,   &CPU::strPtim,    &CPU::strPtim,
        &CPU::strPtim,    &CPU::strPtim,   &CPU::strPtim,    &CPU::strPtim,   &CPU::strPtim,    &CPU::strPtim,
        &CPU::strPtim,    &CPU::strPtim,   &CPU::strPtim,    &CPU::strPtim,   &CPU::strPtim,    &CPU::strPtim,
        &CPU::strPtim,    &CPU::strPtim,   &CPU::ldrPtim,    &CPU::ldrPtim,   &CPU::ldrPtim,    &CPU::ldrPtim,
        &CPU::ldrPtim,    &CPU::ldrPtim,   &CPU::ldrPtim,    &CPU::ldrPtim,   &CPU::ldrPtim,    &CPU::ldrPtim,
        &CPU::ldrPtim,    &CPU::ldrPtim,   &CPU::ldrPtim,    &CPU::ldrPtim,   &CPU::ldrPtim,    &CPU::ldrPtim,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::strbPtim,   &CPU::strbPtim,  &CPU::strbPtim,   &CPU::strbPtim,
        &CPU::strbPtim,   &CPU::strbPtim,  &CPU::strbPtim,   &CPU::strbPtim,  &CPU::strbPtim,   &CPU::strbPtim,
        &CPU::strbPtim,   &CPU::strbPtim,  &CPU::strbPtim,   &CPU::strbPtim,  &CPU::strbPtim,   &CPU::strbPtim,
        &CPU::ldrbPtim,   &CPU::ldrbPtim,  &CPU::ldrbPtim,   &CPU::ldrbPtim,  &CPU::ldrbPtim,   &CPU::ldrbPtim,
        &CPU::ldrbPtim,   &CPU::ldrbPtim,  &CPU::ldrbPtim,   &CPU::ldrbPtim,  &CPU::ldrbPtim,   &CPU::ldrbPtim,
        &CPU::ldrbPtim,   &CPU::ldrbPtim,  &CPU::ldrbPtim,   &CPU::ldrbPtim,  &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::strPtip,    &CPU::strPtip,   &CPU::strPtip,    &CPU::strPtip,   &CPU::strPtip,    &CPU::strPtip,
        &CPU::strPtip,    &CPU::strPtip,   &CPU::strPtip,    &CPU::strPtip,   &CPU::strPtip,    &CPU::strPtip,
        &CPU::strPtip,    &CPU::strPtip,   &CPU::strPtip,    &CPU::strPtip,   &CPU::ldrPtip,    &CPU::ldrPtip,
        &CPU::ldrPtip,    &CPU::ldrPtip,   &CPU::ldrPtip,    &CPU::ldrPtip,   &CPU::ldrPtip,    &CPU::ldrPtip,
        &CPU::ldrPtip,    &CPU::ldrPtip,   &CPU::ldrPtip,    &CPU::ldrPtip,   &CPU::ldrPtip,    &CPU::ldrPtip,
        &CPU::ldrPtip,    &CPU::ldrPtip,   &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::strbPtip,   &CPU::strbPtip,
        &CPU::strbPtip,   &CPU::strbPtip,  &CPU::strbPtip,   &CPU::strbPtip,  &CPU::strbPtip,   &CPU::strbPtip,
        &CPU::strbPtip,   &CPU::strbPtip,  &CPU::strbPtip,   &CPU::strbPtip,  &CPU::strbPtip,   &CPU::strbPtip,
        &CPU::strbPtip,   &CPU::strbPtip,  &CPU::ldrbPtip,   &CPU::ldrbPtip,  &CPU::ldrbPtip,   &CPU::ldrbPtip,
        &CPU::ldrbPtip,   &CPU::ldrbPtip,  &CPU::ldrbPtip,   &CPU::ldrbPtip,  &CPU::ldrbPtip,   &CPU::ldrbPtip,
        &CPU::ldrbPtip,   &CPU::ldrbPtip,  &CPU::ldrbPtip,   &CPU::ldrbPtip,  &CPU::ldrbPtip,   &CPU::ldrbPtip,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::strOfim,    &CPU::strOfim,   &CPU::strOfim,    &CPU::strOfim,
        &CPU::strOfim,    &CPU::strOfim,   &CPU::strOfim,    &CPU::strOfim,   &CPU::strOfim,    &CPU::strOfim,
        &CPU::strOfim,    &CPU::strOfim,   &CPU::strOfim,    &CPU::strOfim,   &CPU::strOfim,    &CPU::strOfim,
        &CPU::ldrOfim,    &CPU::ldrOfim,   &CPU::ldrOfim,    &CPU::ldrOfim,   &CPU::ldrOfim,    &CPU::ldrOfim,
        &CPU::ldrOfim,    &CPU::ldrOfim,   &CPU::ldrOfim,    &CPU::ldrOfim,   &CPU::ldrOfim,    &CPU::ldrOfim,
        &CPU::ldrOfim,    &CPU::ldrOfim,   &CPU::ldrOfim,    &CPU::ldrOfim,   &CPU::strPrim,    &CPU::strPrim,
        &CPU::strPrim,    &CPU::strPrim,   &CPU::strPrim,    &CPU::strPrim,   &CPU::strPrim,    &CPU::strPrim,
        &CPU::strPrim,    &CPU::strPrim,   &CPU::strPrim,    &CPU::strPrim,   &CPU::strPrim,    &CPU::strPrim,
        &CPU::strPrim,    &CPU::strPrim,   &CPU::ldrPrim,    &CPU::ldrPrim,   &CPU::ldrPrim,    &CPU::ldrPrim,
        &CPU::ldrPrim,    &CPU::ldrPrim,   &CPU::ldrPrim,    &CPU::ldrPrim,   &CPU::ldrPrim,    &CPU::ldrPrim,
        &CPU::ldrPrim,    &CPU::ldrPrim,   &CPU::ldrPrim,    &CPU::ldrPrim,   &CPU::ldrPrim,    &CPU::ldrPrim,
        &CPU::strbOfim,   &CPU::strbOfim,  &CPU::strbOfim,   &CPU::strbOfim,  &CPU::strbOfim,   &CPU::strbOfim,
        &CPU::strbOfim,   &CPU::strbOfim,  &CPU::strbOfim,   &CPU::strbOfim,  &CPU::strbOfim,   &CPU::strbOfim,
        &CPU::strbOfim,   &CPU::strbOfim,  &CPU::strbOfim,   &CPU::strbOfim,  &CPU::ldrbOfim,   &CPU::ldrbOfim,
        &CPU::ldrbOfim,   &CPU::ldrbOfim,  &CPU::ldrbOfim,   &CPU::ldrbOfim,  &CPU::ldrbOfim,   &CPU::ldrbOfim,
        &CPU::ldrbOfim,   &CPU::ldrbOfim,  &CPU::ldrbOfim,   &CPU::ldrbOfim,  &CPU::ldrbOfim,   &CPU::ldrbOfim,
        &CPU::ldrbOfim,   &CPU::ldrbOfim,  &CPU::strbPrim,   &CPU::strbPrim,  &CPU::strbPrim,   &CPU::strbPrim,
        &CPU::strbPrim,   &CPU::strbPrim,  &CPU::strbPrim,   &CPU::strbPrim,  &CPU::strbPrim,   &CPU::strbPrim,
        &CPU::strbPrim,   &CPU::strbPrim,  &CPU::strbPrim,   &CPU::strbPrim,  &CPU::strbPrim,   &CPU::strbPrim,
        &CPU::ldrbPrim,   &CPU::ldrbPrim,  &CPU::ldrbPrim,   &CPU::ldrbPrim,  &CPU::ldrbPrim,   &CPU::ldrbPrim,
        &CPU::ldrbPrim,   &CPU::ldrbPrim,  &CPU::ldrbPrim,   &CPU::ldrbPrim,  &CPU::ldrbPrim,   &CPU::ldrbPrim,
        &CPU::ldrbPrim,   &CPU::ldrbPrim,  &CPU::ldrbPrim,   &CPU::ldrbPrim,  &CPU::strOfip,    &CPU::strOfip,
        &CPU::strOfip,    &CPU::strOfip,   &CPU::strOfip,    &CPU::strOfip,   &CPU::strOfip,    &CPU::strOfip,
        &CPU::strOfip,    &CPU::strOfip,   &CPU::strOfip,    &CPU::strOfip,   &CPU::strOfip,    &CPU::strOfip,
        &CPU::strOfip,    &CPU::strOfip,   &CPU::ldrOfip,    &CPU::ldrOfip,   &CPU::ldrOfip,    &CPU::ldrOfip,
        &CPU::ldrOfip,    &CPU::ldrOfip,   &CPU::ldrOfip,    &CPU::ldrOfip,   &CPU::ldrOfip,    &CPU::ldrOfip,
        &CPU::ldrOfip,    &CPU::ldrOfip,   &CPU::ldrOfip,    &CPU::ldrOfip,   &CPU::ldrOfip,    &CPU::ldrOfip,
        &CPU::strPrip,    &CPU::strPrip,   &CPU::strPrip,    &CPU::strPrip,   &CPU::strPrip,    &CPU::strPrip,
        &CPU::strPrip,    &CPU::strPrip,   &CPU::strPrip,    &CPU::strPrip,   &CPU::strPrip,    &CPU::strPrip,
        &CPU::strPrip,    &CPU::strPrip,   &CPU::strPrip,    &CPU::strPrip,   &CPU::ldrPrip,    &CPU::ldrPrip,
        &CPU::ldrPrip,    &CPU::ldrPrip,   &CPU::ldrPrip,    &CPU::ldrPrip,   &CPU::ldrPrip,    &CPU::ldrPrip,
        &CPU::ldrPrip,    &CPU::ldrPrip,   &CPU::ldrPrip,    &CPU::ldrPrip,   &CPU::ldrPrip,    &CPU::ldrPrip,
        &CPU::ldrPrip,    &CPU::ldrPrip,   &CPU::strbOfip,   &CPU::strbOfip,  &CPU::strbOfip,   &CPU::strbOfip,
        &CPU::strbOfip,   &CPU::strbOfip,  &CPU::strbOfip,   &CPU::strbOfip,  &CPU::strbOfip,   &CPU::strbOfip,
        &CPU::strbOfip,   &CPU::strbOfip,  &CPU::strbOfip,   &CPU::strbOfip,  &CPU::strbOfip,   &CPU::strbOfip,
        &CPU::ldrbOfip,   &CPU::ldrbOfip,  &CPU::ldrbOfip,   &CPU::ldrbOfip,  &CPU::ldrbOfip,   &CPU::ldrbOfip,
        &CPU::ldrbOfip,   &CPU::ldrbOfip,  &CPU::ldrbOfip,   &CPU::ldrbOfip,  &CPU::ldrbOfip,   &CPU::ldrbOfip,
        &CPU::ldrbOfip,   &CPU::ldrbOfip,  &CPU::ldrbOfip,   &CPU::ldrbOfip,  &CPU::strbPrip,   &CPU::strbPrip,
        &CPU::strbPrip,   &CPU::strbPrip,  &CPU::strbPrip,   &CPU::strbPrip,  &CPU::strbPrip,   &CPU::strbPrip,
        &CPU::strbPrip,   &CPU::strbPrip,  &CPU::strbPrip,   &CPU::strbPrip,  &CPU::strbPrip,   &CPU::strbPrip,
        &CPU::strbPrip,   &CPU::strbPrip,  &CPU::ldrbPrip,   &CPU::ldrbPrip,  &CPU::ldrbPrip,   &CPU::ldrbPrip,
        &CPU::ldrbPrip,   &CPU::ldrbPrip,  &CPU::ldrbPrip,   &CPU::ldrbPrip,  &CPU::ldrbPrip,   &CPU::ldrbPrip,
        &CPU::ldrbPrip,   &CPU::ldrbPrip,  &CPU::ldrbPrip,   &CPU::ldrbPrip,  &CPU::ldrbPrip,   &CPU::ldrbPrip,
        &CPU::strPtrmll,  &CPU::unkArm,    &CPU::strPtrmlr,  &CPU::unkArm,    &CPU::strPtrmar,  &CPU::unkArm,
        &CPU::strPtrmrr,  &CPU::unkArm,    &CPU::strPtrmll,  &CPU::unkArm,    &CPU::strPtrmlr,  &CPU::unkArm,
        &CPU::strPtrmar,  &CPU::unkArm,    &CPU::strPtrmrr,  &CPU::unkArm,    &CPU::ldrPtrmll,  &CPU::unkArm,
        &CPU::ldrPtrmlr,  &CPU::unkArm,    &CPU::ldrPtrmar,  &CPU::unkArm,    &CPU::ldrPtrmrr,  &CPU::unkArm,
        &CPU::ldrPtrmll,  &CPU::unkArm,    &CPU::ldrPtrmlr,  &CPU::unkArm,    &CPU::ldrPtrmar,  &CPU::unkArm,
        &CPU::ldrPtrmrr,  &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::strbPtrmll, &CPU::unkArm,
        &CPU::strbPtrmlr, &CPU::unkArm,    &CPU::strbPtrmar, &CPU::unkArm,    &CPU::strbPtrmrr, &CPU::unkArm,
        &CPU::strbPtrmll, &CPU::unkArm,    &CPU::strbPtrmlr, &CPU::unkArm,    &CPU::strbPtrmar, &CPU::unkArm,
        &CPU::strbPtrmrr, &CPU::unkArm,    &CPU::ldrbPtrmll, &CPU::unkArm,    &CPU::ldrbPtrmlr, &CPU::unkArm,
        &CPU::ldrbPtrmar, &CPU::unkArm,    &CPU::ldrbPtrmrr, &CPU::unkArm,    &CPU::ldrbPtrmll, &CPU::unkArm,
        &CPU::ldrbPtrmlr, &CPU::unkArm,    &CPU::ldrbPtrmar, &CPU::unkArm,    &CPU::ldrbPtrmrr, &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::strPtrpll,  &CPU::unkArm,    &CPU::strPtrplr,  &CPU::unkArm,
        &CPU::strPtrpar,  &CPU::unkArm,    &CPU::strPtrprr,  &CPU::unkArm,    &CPU::strPtrpll,  &CPU::unkArm,
        &CPU::strPtrplr,  &CPU::unkArm,    &CPU::strPtrpar,  &CPU::unkArm,    &CPU::strPtrprr,  &CPU::unkArm,
        &CPU::ldrPtrpll,  &CPU::unkArm,    &CPU::ldrPtrplr,  &CPU::unkArm,    &CPU::ldrPtrpar,  &CPU::unkArm,
        &CPU::ldrPtrprr,  &CPU::unkArm,    &CPU::ldrPtrpll,  &CPU::unkArm,    &CPU::ldrPtrplr,  &CPU::unkArm,
        &CPU::ldrPtrpar,  &CPU::unkArm,    &CPU::ldrPtrprr,  &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::strbPtrpll, &CPU::unkArm,    &CPU::strbPtrplr, &CPU::unkArm,    &CPU::strbPtrpar, &CPU::unkArm,
        &CPU::strbPtrprr, &CPU::unkArm,    &CPU::strbPtrpll, &CPU::unkArm,    &CPU::strbPtrplr, &CPU::unkArm,
        &CPU::strbPtrpar, &CPU::unkArm,    &CPU::strbPtrprr, &CPU::unkArm,    &CPU::ldrbPtrpll, &CPU::unkArm,
        &CPU::ldrbPtrplr, &CPU::unkArm,    &CPU::ldrbPtrpar, &CPU::unkArm,    &CPU::ldrbPtrprr, &CPU::unkArm,
        &CPU::ldrbPtrpll, &CPU::unkArm,    &CPU::ldrbPtrplr, &CPU::unkArm,    &CPU::ldrbPtrpar, &CPU::unkArm,
        &CPU::ldrbPtrprr, &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::strOfrmll,  &CPU::unkArm,
        &CPU::strOfrmlr,  &CPU::unkArm,    &CPU::strOfrmar,  &CPU::unkArm,    &CPU::strOfrmrr,  &CPU::unkArm,
        &CPU::strOfrmll,  &CPU::unkArm,    &CPU::strOfrmlr,  &CPU::unkArm,    &CPU::strOfrmar,  &CPU::unkArm,
        &CPU::strOfrmrr,  &CPU::unkArm,    &CPU::ldrOfrmll,  &CPU::unkArm,    &CPU::ldrOfrmlr,  &CPU::unkArm,
        &CPU::ldrOfrmar,  &CPU::unkArm,    &CPU::ldrOfrmrr,  &CPU::unkArm,    &CPU::ldrOfrmll,  &CPU::unkArm,
        &CPU::ldrOfrmlr,  &CPU::unkArm,    &CPU::ldrOfrmar,  &CPU::unkArm,    &CPU::ldrOfrmrr,  &CPU::unkArm,
        &CPU::strPrrmll,  &CPU::unkArm,    &CPU::strPrrmlr,  &CPU::unkArm,    &CPU::strPrrmar,  &CPU::unkArm,
        &CPU::strPrrmrr,  &CPU::unkArm,    &CPU::strPrrmll,  &CPU::unkArm,    &CPU::strPrrmlr,  &CPU::unkArm,
        &CPU::strPrrmar,  &CPU::unkArm,    &CPU::strPrrmrr,  &CPU::unkArm,    &CPU::ldrPrrmll,  &CPU::unkArm,
        &CPU::ldrPrrmlr,  &CPU::unkArm,    &CPU::ldrPrrmar,  &CPU::unkArm,    &CPU::ldrPrrmrr,  &CPU::unkArm,
        &CPU::ldrPrrmll,  &CPU::unkArm,    &CPU::ldrPrrmlr,  &CPU::unkArm,    &CPU::ldrPrrmar,  &CPU::unkArm,
        &CPU::ldrPrrmrr,  &CPU::unkArm,    &CPU::strbOfrmll, &CPU::unkArm,    &CPU::strbOfrmlr, &CPU::unkArm,
        &CPU::strbOfrmar, &CPU::unkArm,    &CPU::strbOfrmrr, &CPU::unkArm,    &CPU::strbOfrmll, &CPU::unkArm,
        &CPU::strbOfrmlr, &CPU::unkArm,    &CPU::strbOfrmar, &CPU::unkArm,    &CPU::strbOfrmrr, &CPU::unkArm,
        &CPU::ldrbOfrmll, &CPU::unkArm,    &CPU::ldrbOfrmlr, &CPU::unkArm,    &CPU::ldrbOfrmar, &CPU::unkArm,
        &CPU::ldrbOfrmrr, &CPU::unkArm,    &CPU::ldrbOfrmll, &CPU::unkArm,    &CPU::ldrbOfrmlr, &CPU::unkArm,
        &CPU::ldrbOfrmar, &CPU::unkArm,    &CPU::ldrbOfrmrr, &CPU::unkArm,    &CPU::strbPrrmll, &CPU::unkArm,
        &CPU::strbPrrmlr, &CPU::unkArm,    &CPU::strbPrrmar, &CPU::unkArm,    &CPU::strbPrrmrr, &CPU::unkArm,
        &CPU::strbPrrmll, &CPU::unkArm,    &CPU::strbPrrmlr, &CPU::unkArm,    &CPU::strbPrrmar, &CPU::unkArm,
        &CPU::strbPrrmrr, &CPU::unkArm,    &CPU::ldrbPrrmll, &CPU::unkArm,    &CPU::ldrbPrrmlr, &CPU::unkArm,
        &CPU::ldrbPrrmar, &CPU::unkArm,    &CPU::ldrbPrrmrr, &CPU::unkArm,    &CPU::ldrbPrrmll, &CPU::unkArm,
        &CPU::ldrbPrrmlr, &CPU::unkArm,    &CPU::ldrbPrrmar, &CPU::unkArm,    &CPU::ldrbPrrmrr, &CPU::unkArm,
        &CPU::strOfrpll,  &CPU::unkArm,    &CPU::strOfrplr,  &CPU::unkArm,    &CPU::strOfrpar,  &CPU::unkArm,
        &CPU::strOfrprr,  &CPU::unkArm,    &CPU::strOfrpll,  &CPU::unkArm,    &CPU::strOfrplr,  &CPU::unkArm,
        &CPU::strOfrpar,  &CPU::unkArm,    &CPU::strOfrprr,  &CPU::unkArm,    &CPU::ldrOfrpll,  &CPU::unkArm,
        &CPU::ldrOfrplr,  &CPU::unkArm,    &CPU::ldrOfrpar,  &CPU::unkArm,    &CPU::ldrOfrprr,  &CPU::unkArm,
        &CPU::ldrOfrpll,  &CPU::unkArm,    &CPU::ldrOfrplr,  &CPU::unkArm,    &CPU::ldrOfrpar,  &CPU::unkArm,
        &CPU::ldrOfrprr,  &CPU::unkArm,    &CPU::strPrrpll,  &CPU::unkArm,    &CPU::strPrrplr,  &CPU::unkArm,
        &CPU::strPrrpar,  &CPU::unkArm,    &CPU::strPrrprr,  &CPU::unkArm,    &CPU::strPrrpll,  &CPU::unkArm,
        &CPU::strPrrplr,  &CPU::unkArm,    &CPU::strPrrpar,  &CPU::unkArm,    &CPU::strPrrprr,  &CPU::unkArm,
        &CPU::ldrPrrpll,  &CPU::unkArm,    &CPU::ldrPrrplr,  &CPU::unkArm,    &CPU::ldrPrrpar,  &CPU::unkArm,
        &CPU::ldrPrrprr,  &CPU::unkArm,    &CPU::ldrPrrpll,  &CPU::unkArm,    &CPU::ldrPrrplr,  &CPU::unkArm,
        &CPU::ldrPrrpar,  &CPU::unkArm,    &CPU::ldrPrrprr,  &CPU::unkArm,    &CPU::strbOfrpll, &CPU::unkArm,
        &CPU::strbOfrplr, &CPU::unkArm,    &CPU::strbOfrpar, &CPU::unkArm,    &CPU::strbOfrprr, &CPU::unkArm,
        &CPU::strbOfrpll, &CPU::unkArm,    &CPU::strbOfrplr, &CPU::unkArm,    &CPU::strbOfrpar, &CPU::unkArm,
        &CPU::strbOfrprr, &CPU::unkArm,    &CPU::ldrbOfrpll, &CPU::unkArm,    &CPU::ldrbOfrplr, &CPU::unkArm,
        &CPU::ldrbOfrpar, &CPU::unkArm,    &CPU::ldrbOfrprr, &CPU::unkArm,    &CPU::ldrbOfrpll, &CPU::unkArm,
        &CPU::ldrbOfrplr, &CPU::unkArm,    &CPU::ldrbOfrpar, &CPU::unkArm,    &CPU::ldrbOfrprr, &CPU::unkArm,
        &CPU::strbPrrpll, &CPU::unkArm,    &CPU::strbPrrplr, &CPU::unkArm,    &CPU::strbPrrpar, &CPU::unkArm,
        &CPU::strbPrrprr, &CPU::unkArm,    &CPU::strbPrrpll, &CPU::unkArm,    &CPU::strbPrrplr, &CPU::unkArm,
        &CPU::strbPrrpar, &CPU::unkArm,    &CPU::strbPrrprr, &CPU::unkArm,    &CPU::ldrbPrrpll, &CPU::unkArm,
        &CPU::ldrbPrrplr, &CPU::unkArm,    &CPU::ldrbPrrpar, &CPU::unkArm,    &CPU::ldrbPrrprr, &CPU::unkArm,
        &CPU::ldrbPrrpll, &CPU::unkArm,    &CPU::ldrbPrrplr, &CPU::unkArm,    &CPU::ldrbPrrpar, &CPU::unkArm,
        &CPU::ldrbPrrprr, &CPU::unkArm,    &CPU::stmda,      &CPU::stmda,     &CPU::stmda,      &CPU::stmda,
        &CPU::stmda,      &CPU::stmda,     &CPU::stmda,      &CPU::stmda,     &CPU::stmda,      &CPU::stmda,
        &CPU::stmda,      &CPU::stmda,     &CPU::stmda,      &CPU::stmda,     &CPU::stmda,      &CPU::stmda,
        &CPU::ldmda,      &CPU::ldmda,     &CPU::ldmda,      &CPU::ldmda,     &CPU::ldmda,      &CPU::ldmda,
        &CPU::ldmda,      &CPU::ldmda,     &CPU::ldmda,      &CPU::ldmda,     &CPU::ldmda,      &CPU::ldmda,
        &CPU::ldmda,      &CPU::ldmda,     &CPU::ldmda,      &CPU::ldmda,     &CPU::stmdaW,     &CPU::stmdaW,
        &CPU::stmdaW,     &CPU::stmdaW,    &CPU::stmdaW,     &CPU::stmdaW,    &CPU::stmdaW,     &CPU::stmdaW,
        &CPU::stmdaW,     &CPU::stmdaW,    &CPU::stmdaW,     &CPU::stmdaW,    &CPU::stmdaW,     &CPU::stmdaW,
        &CPU::stmdaW,     &CPU::stmdaW,    &CPU::ldmdaW,     &CPU::ldmdaW,    &CPU::ldmdaW,     &CPU::ldmdaW,
        &CPU::ldmdaW,     &CPU::ldmdaW,    &CPU::ldmdaW,     &CPU::ldmdaW,    &CPU::ldmdaW,     &CPU::ldmdaW,
        &CPU::ldmdaW,     &CPU::ldmdaW,    &CPU::ldmdaW,     &CPU::ldmdaW,    &CPU::ldmdaW,     &CPU::ldmdaW,
        &CPU::stmdaU,     &CPU::stmdaU,    &CPU::stmdaU,     &CPU::stmdaU,    &CPU::stmdaU,     &CPU::stmdaU,
        &CPU::stmdaU,     &CPU::stmdaU,    &CPU::stmdaU,     &CPU::stmdaU,    &CPU::stmdaU,     &CPU::stmdaU,
        &CPU::stmdaU,     &CPU::stmdaU,    &CPU::stmdaU,     &CPU::stmdaU,    &CPU::ldmdaU,     &CPU::ldmdaU,
        &CPU::ldmdaU,     &CPU::ldmdaU,    &CPU::ldmdaU,     &CPU::ldmdaU,    &CPU::ldmdaU,     &CPU::ldmdaU,
        &CPU::ldmdaU,     &CPU::ldmdaU,    &CPU::ldmdaU,     &CPU::ldmdaU,    &CPU::ldmdaU,     &CPU::ldmdaU,
        &CPU::ldmdaU,     &CPU::ldmdaU,    &CPU::stmdaUW,    &CPU::stmdaUW,   &CPU::stmdaUW,    &CPU::stmdaUW,
        &CPU::stmdaUW,    &CPU::stmdaUW,   &CPU::stmdaUW,    &CPU::stmdaUW,   &CPU::stmdaUW,    &CPU::stmdaUW,
        &CPU::stmdaUW,    &CPU::stmdaUW,   &CPU::stmdaUW,    &CPU::stmdaUW,   &CPU::stmdaUW,    &CPU::stmdaUW,
        &CPU::ldmdaUW,    &CPU::ldmdaUW,   &CPU::ldmdaUW,    &CPU::ldmdaUW,   &CPU::ldmdaUW,    &CPU::ldmdaUW,
        &CPU::ldmdaUW,    &CPU::ldmdaUW,   &CPU::ldmdaUW,    &CPU::ldmdaUW,   &CPU::ldmdaUW,    &CPU::ldmdaUW,
        &CPU::ldmdaUW,    &CPU::ldmdaUW,   &CPU::ldmdaUW,    &CPU::ldmdaUW,   &CPU::stmia,      &CPU::stmia,
        &CPU::stmia,      &CPU::stmia,     &CPU::stmia,      &CPU::stmia,     &CPU::stmia,      &CPU::stmia,
        &CPU::stmia,      &CPU::stmia,     &CPU::stmia,      &CPU::stmia,     &CPU::stmia,      &CPU::stmia,
        &CPU::stmia,      &CPU::stmia,     &CPU::ldmia,      &CPU::ldmia,     &CPU::ldmia,      &CPU::ldmia,
        &CPU::ldmia,      &CPU::ldmia,     &CPU::ldmia,      &CPU::ldmia,     &CPU::ldmia,      &CPU::ldmia,
        &CPU::ldmia,      &CPU::ldmia,     &CPU::ldmia,      &CPU::ldmia,     &CPU::ldmia,      &CPU::ldmia,
        &CPU::stmiaW,     &CPU::stmiaW,    &CPU::stmiaW,     &CPU::stmiaW,    &CPU::stmiaW,     &CPU::stmiaW,
        &CPU::stmiaW,     &CPU::stmiaW,    &CPU::stmiaW,     &CPU::stmiaW,    &CPU::stmiaW,     &CPU::stmiaW,
        &CPU::stmiaW,     &CPU::stmiaW,    &CPU::stmiaW,     &CPU::stmiaW,    &CPU::ldmiaW,     &CPU::ldmiaW,
        &CPU::ldmiaW,     &CPU::ldmiaW,    &CPU::ldmiaW,     &CPU::ldmiaW,    &CPU::ldmiaW,     &CPU::ldmiaW,
        &CPU::ldmiaW,     &CPU::ldmiaW,    &CPU::ldmiaW,     &CPU::ldmiaW,    &CPU::ldmiaW,     &CPU::ldmiaW,
        &CPU::ldmiaW,     &CPU::ldmiaW,    &CPU::stmiaU,     &CPU::stmiaU,    &CPU::stmiaU,     &CPU::stmiaU,
        &CPU::stmiaU,     &CPU::stmiaU,    &CPU::stmiaU,     &CPU::stmiaU,    &CPU::stmiaU,     &CPU::stmiaU,
        &CPU::stmiaU,     &CPU::stmiaU,    &CPU::stmiaU,     &CPU::stmiaU,    &CPU::stmiaU,     &CPU::stmiaU,
        &CPU::ldmiaU,     &CPU::ldmiaU,    &CPU::ldmiaU,     &CPU::ldmiaU,    &CPU::ldmiaU,     &CPU::ldmiaU,
        &CPU::ldmiaU,     &CPU::ldmiaU,    &CPU::ldmiaU,     &CPU::ldmiaU,    &CPU::ldmiaU,     &CPU::ldmiaU,
        &CPU::ldmiaU,     &CPU::ldmiaU,    &CPU::ldmiaU,     &CPU::ldmiaU,    &CPU::stmiaUW,    &CPU::stmiaUW,
        &CPU::stmiaUW,    &CPU::stmiaUW,   &CPU::stmiaUW,    &CPU::stmiaUW,   &CPU::stmiaUW,    &CPU::stmiaUW,
        &CPU::stmiaUW,    &CPU::stmiaUW,   &CPU::stmiaUW,    &CPU::stmiaUW,   &CPU::stmiaUW,    &CPU::stmiaUW,
        &CPU::stmiaUW,    &CPU::stmiaUW,   &CPU::ldmiaUW,    &CPU::ldmiaUW,   &CPU::ldmiaUW,    &CPU::ldmiaUW,
        &CPU::ldmiaUW,    &CPU::ldmiaUW,   &CPU::ldmiaUW,    &CPU::ldmiaUW,   &CPU::ldmiaUW,    &CPU::ldmiaUW,
        &CPU::ldmiaUW,    &CPU::ldmiaUW,   &CPU::ldmiaUW,    &CPU::ldmiaUW,   &CPU::ldmiaUW,    &CPU::ldmiaUW,
        &CPU::stmdb,      &CPU::stmdb,     &CPU::stmdb,      &CPU::stmdb,     &CPU::stmdb,      &CPU::stmdb,
        &CPU::stmdb,      &CPU::stmdb,     &CPU::stmdb,      &CPU::stmdb,     &CPU::stmdb,      &CPU::stmdb,
        &CPU::stmdb,      &CPU::stmdb,     &CPU::stmdb,      &CPU::stmdb,     &CPU::ldmdb,      &CPU::ldmdb,
        &CPU::ldmdb,      &CPU::ldmdb,     &CPU::ldmdb,      &CPU::ldmdb,     &CPU::ldmdb,      &CPU::ldmdb,
        &CPU::ldmdb,      &CPU::ldmdb,     &CPU::ldmdb,      &CPU::ldmdb,     &CPU::ldmdb,      &CPU::ldmdb,
        &CPU::ldmdb,      &CPU::ldmdb,     &CPU::stmdbW,     &CPU::stmdbW,    &CPU::stmdbW,     &CPU::stmdbW,
        &CPU::stmdbW,     &CPU::stmdbW,    &CPU::stmdbW,     &CPU::stmdbW,    &CPU::stmdbW,     &CPU::stmdbW,
        &CPU::stmdbW,     &CPU::stmdbW,    &CPU::stmdbW,     &CPU::stmdbW,    &CPU::stmdbW,     &CPU::stmdbW,
        &CPU::ldmdbW,     &CPU::ldmdbW,    &CPU::ldmdbW,     &CPU::ldmdbW,    &CPU::ldmdbW,     &CPU::ldmdbW,
        &CPU::ldmdbW,     &CPU::ldmdbW,    &CPU::ldmdbW,     &CPU::ldmdbW,    &CPU::ldmdbW,     &CPU::ldmdbW,
        &CPU::ldmdbW,     &CPU::ldmdbW,    &CPU::ldmdbW,     &CPU::ldmdbW,    &CPU::stmdbU,     &CPU::stmdbU,
        &CPU::stmdbU,     &CPU::stmdbU,    &CPU::stmdbU,     &CPU::stmdbU,    &CPU::stmdbU,     &CPU::stmdbU,
        &CPU::stmdbU,     &CPU::stmdbU,    &CPU::stmdbU,     &CPU::stmdbU,    &CPU::stmdbU,     &CPU::stmdbU,
        &CPU::stmdbU,     &CPU::stmdbU,    &CPU::ldmdbU,     &CPU::ldmdbU,    &CPU::ldmdbU,     &CPU::ldmdbU,
        &CPU::ldmdbU,     &CPU::ldmdbU,    &CPU::ldmdbU,     &CPU::ldmdbU,    &CPU::ldmdbU,     &CPU::ldmdbU,
        &CPU::ldmdbU,     &CPU::ldmdbU,    &CPU::ldmdbU,     &CPU::ldmdbU,    &CPU::ldmdbU,     &CPU::ldmdbU,
        &CPU::stmdbUW,    &CPU::stmdbUW,   &CPU::stmdbUW,    &CPU::stmdbUW,   &CPU::stmdbUW,    &CPU::stmdbUW,
        &CPU::stmdbUW,    &CPU::stmdbUW,   &CPU::stmdbUW,    &CPU::stmdbUW,   &CPU::stmdbUW,    &CPU::stmdbUW,
        &CPU::stmdbUW,    &CPU::stmdbUW,   &CPU::stmdbUW,    &CPU::stmdbUW,   &CPU::ldmdbUW,    &CPU::ldmdbUW,
        &CPU::ldmdbUW,    &CPU::ldmdbUW,   &CPU::ldmdbUW,    &CPU::ldmdbUW,   &CPU::ldmdbUW,    &CPU::ldmdbUW,
        &CPU::ldmdbUW,    &CPU::ldmdbUW,   &CPU::ldmdbUW,    &CPU::ldmdbUW,   &CPU::ldmdbUW,    &CPU::ldmdbUW,
        &CPU::ldmdbUW,    &CPU::ldmdbUW,   &CPU::stmib,      &CPU::stmib,     &CPU::stmib,      &CPU::stmib,
        &CPU::stmib,      &CPU::stmib,     &CPU::stmib,      &CPU::stmib,     &CPU::stmib,      &CPU::stmib,
        &CPU::stmib,      &CPU::stmib,     &CPU::stmib,      &CPU::stmib,     &CPU::stmib,      &CPU::stmib,
        &CPU::ldmib,      &CPU::ldmib,     &CPU::ldmib,      &CPU::ldmib,     &CPU::ldmib,      &CPU::ldmib,
        &CPU::ldmib,      &CPU::ldmib,     &CPU::ldmib,      &CPU::ldmib,     &CPU::ldmib,      &CPU::ldmib,
        &CPU::ldmib,      &CPU::ldmib,     &CPU::ldmib,      &CPU::ldmib,     &CPU::stmibW,     &CPU::stmibW,
        &CPU::stmibW,     &CPU::stmibW,    &CPU::stmibW,     &CPU::stmibW,    &CPU::stmibW,     &CPU::stmibW,
        &CPU::stmibW,     &CPU::stmibW,    &CPU::stmibW,     &CPU::stmibW,    &CPU::stmibW,     &CPU::stmibW,
        &CPU::stmibW,     &CPU::stmibW,    &CPU::ldmibW,     &CPU::ldmibW,    &CPU::ldmibW,     &CPU::ldmibW,
        &CPU::ldmibW,     &CPU::ldmibW,    &CPU::ldmibW,     &CPU::ldmibW,    &CPU::ldmibW,     &CPU::ldmibW,
        &CPU::ldmibW,     &CPU::ldmibW,    &CPU::ldmibW,     &CPU::ldmibW,    &CPU::ldmibW,     &CPU::ldmibW,
        &CPU::stmibU,     &CPU::stmibU,    &CPU::stmibU,     &CPU::stmibU,    &CPU::stmibU,     &CPU::stmibU,
        &CPU::stmibU,     &CPU::stmibU,    &CPU::stmibU,     &CPU::stmibU,    &CPU::stmibU,     &CPU::stmibU,
        &CPU::stmibU,     &CPU::stmibU,    &CPU::stmibU,     &CPU::stmibU,    &CPU::ldmibU,     &CPU::ldmibU,
        &CPU::ldmibU,     &CPU::ldmibU,    &CPU::ldmibU,     &CPU::ldmibU,    &CPU::ldmibU,     &CPU::ldmibU,
        &CPU::ldmibU,     &CPU::ldmibU,    &CPU::ldmibU,     &CPU::ldmibU,    &CPU::ldmibU,     &CPU::ldmibU,
        &CPU::ldmibU,     &CPU::ldmibU,    &CPU::stmibUW,    &CPU::stmibUW,   &CPU::stmibUW,    &CPU::stmibUW,
        &CPU::stmibUW,    &CPU::stmibUW,   &CPU::stmibUW,    &CPU::stmibUW,   &CPU::stmibUW,    &CPU::stmibUW,
        &CPU::stmibUW,    &CPU::stmibUW,   &CPU::stmibUW,    &CPU::stmibUW,   &CPU::stmibUW,    &CPU::stmibUW,
        &CPU::ldmibUW,    &CPU::ldmibUW,   &CPU::ldmibUW,    &CPU::ldmibUW,   &CPU::ldmibUW,    &CPU::ldmibUW,
        &CPU::ldmibUW,    &CPU::ldmibUW,   &CPU::ldmibUW,    &CPU::ldmibUW,   &CPU::ldmibUW,    &CPU::ldmibUW,
        &CPU::ldmibUW,    &CPU::ldmibUW,   &CPU::ldmibUW,    &CPU::ldmibUW,   &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,         &CPU::b,          &CPU::b,
        &CPU::b,          &CPU::b,         &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,        &CPU::bl,         &CPU::bl,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::unkArm,
        &CPU::unkArm,     &CPU::unkArm,    &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mcr,
        &CPU::unkArm,     &CPU::mcr,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,       &CPU::unkArm,     &CPU::mrc,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi,
        &CPU::swi,        &CPU::swi,       &CPU::swi,        &CPU::swi};
    int (CPU::*thumbInstrs[0x400])(uint16_t) = {
        &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,
        &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,
        &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,
        &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,
        &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lslImmT,
        &CPU::lslImmT,   &CPU::lslImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,
        &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,
        &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,
        &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,
        &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,
        &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::lsrImmT,   &CPU::asrImmT,   &CPU::asrImmT,
        &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,
        &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,
        &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,
        &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,
        &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,   &CPU::asrImmT,
        &CPU::addRegT,   &CPU::addRegT,   &CPU::addRegT,   &CPU::addRegT,   &CPU::addRegT,   &CPU::addRegT,
        &CPU::addRegT,   &CPU::addRegT,   &CPU::subRegT,   &CPU::subRegT,   &CPU::subRegT,   &CPU::subRegT,
        &CPU::subRegT,   &CPU::subRegT,   &CPU::subRegT,   &CPU::subRegT,   &CPU::addImm3T,  &CPU::addImm3T,
        &CPU::addImm3T,  &CPU::addImm3T,  &CPU::addImm3T,  &CPU::addImm3T,  &CPU::addImm3T,  &CPU::addImm3T,
        &CPU::subImm3T,  &CPU::subImm3T,  &CPU::subImm3T,  &CPU::subImm3T,  &CPU::subImm3T,  &CPU::subImm3T,
        &CPU::subImm3T,  &CPU::subImm3T,  &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,
        &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,
        &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,
        &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,
        &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,
        &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,  &CPU::movImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,
        &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,
        &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,
        &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,
        &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,
        &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,  &CPU::cmpImm8T,
        &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,
        &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,
        &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,
        &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,
        &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,  &CPU::addImm8T,
        &CPU::addImm8T,  &CPU::addImm8T,  &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,
        &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,
        &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,
        &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,
        &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,
        &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,  &CPU::subImm8T,  &CPU::andDpT,    &CPU::eorDpT,
        &CPU::lslDpT,    &CPU::lsrDpT,    &CPU::asrDpT,    &CPU::adcDpT,    &CPU::sbcDpT,    &CPU::rorDpT,
        &CPU::tstDpT,    &CPU::negDpT,    &CPU::cmpDpT,    &CPU::cmnDpT,    &CPU::orrDpT,    &CPU::mulDpT,
        &CPU::bicDpT,    &CPU::mvnDpT,    &CPU::addHT,     &CPU::addHT,     &CPU::addHT,     &CPU::addHT,
        &CPU::cmpHT,     &CPU::cmpHT,     &CPU::cmpHT,     &CPU::cmpHT,     &CPU::movHT,     &CPU::movHT,
        &CPU::movHT,     &CPU::movHT,     &CPU::bxRegT,    &CPU::bxRegT,    &CPU::blxRegT,   &CPU::blxRegT,
        &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,
        &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,
        &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,
        &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,
        &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::ldrPcT,
        &CPU::ldrPcT,    &CPU::ldrPcT,    &CPU::strRegT,   &CPU::strRegT,   &CPU::strRegT,   &CPU::strRegT,
        &CPU::strRegT,   &CPU::strRegT,   &CPU::strRegT,   &CPU::strRegT,   &CPU::strhRegT,  &CPU::strhRegT,
        &CPU::strhRegT,  &CPU::strhRegT,  &CPU::strhRegT,  &CPU::strhRegT,  &CPU::strhRegT,  &CPU::strhRegT,
        &CPU::strbRegT,  &CPU::strbRegT,  &CPU::strbRegT,  &CPU::strbRegT,  &CPU::strbRegT,  &CPU::strbRegT,
        &CPU::strbRegT,  &CPU::strbRegT,  &CPU::ldrsbRegT, &CPU::ldrsbRegT, &CPU::ldrsbRegT, &CPU::ldrsbRegT,
        &CPU::ldrsbRegT, &CPU::ldrsbRegT, &CPU::ldrsbRegT, &CPU::ldrsbRegT, &CPU::ldrRegT,   &CPU::ldrRegT,
        &CPU::ldrRegT,   &CPU::ldrRegT,   &CPU::ldrRegT,   &CPU::ldrRegT,   &CPU::ldrRegT,   &CPU::ldrRegT,
        &CPU::ldrhRegT,  &CPU::ldrhRegT,  &CPU::ldrhRegT,  &CPU::ldrhRegT,  &CPU::ldrhRegT,  &CPU::ldrhRegT,
        &CPU::ldrhRegT,  &CPU::ldrhRegT,  &CPU::ldrbRegT,  &CPU::ldrbRegT,  &CPU::ldrbRegT,  &CPU::ldrbRegT,
        &CPU::ldrbRegT,  &CPU::ldrbRegT,  &CPU::ldrbRegT,  &CPU::ldrbRegT,  &CPU::ldrshRegT, &CPU::ldrshRegT,
        &CPU::ldrshRegT, &CPU::ldrshRegT, &CPU::ldrshRegT, &CPU::ldrshRegT, &CPU::ldrshRegT, &CPU::ldrshRegT,
        &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,
        &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,
        &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,
        &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,
        &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,  &CPU::strImm5T,
        &CPU::strImm5T,  &CPU::strImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,
        &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,
        &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,
        &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,
        &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,
        &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::ldrImm5T,  &CPU::strbImm5T, &CPU::strbImm5T,
        &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T,
        &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T,
        &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T,
        &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T,
        &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T, &CPU::strbImm5T,
        &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T,
        &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T,
        &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T,
        &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T,
        &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::ldrbImm5T,
        &CPU::ldrbImm5T, &CPU::ldrbImm5T, &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T,
        &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T,
        &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T,
        &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T,
        &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T,
        &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T, &CPU::strhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T,
        &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T,
        &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T,
        &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T,
        &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T,
        &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T, &CPU::ldrhImm5T,
        &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,
        &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,
        &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,
        &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,
        &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,    &CPU::strSpT,
        &CPU::strSpT,    &CPU::strSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,
        &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,
        &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,
        &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,
        &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,
        &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::ldrSpT,    &CPU::addPcT,    &CPU::addPcT,
        &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,
        &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,
        &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,
        &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,
        &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,    &CPU::addPcT,
        &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,
        &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,
        &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,
        &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,
        &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpT,
        &CPU::addSpT,    &CPU::addSpT,    &CPU::addSpImmT, &CPU::addSpImmT, &CPU::addSpImmT, &CPU::addSpImmT,
        &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,
        &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,
        &CPU::pushT,     &CPU::pushT,     &CPU::pushT,     &CPU::pushT,     &CPU::pushLrT,   &CPU::pushLrT,
        &CPU::pushLrT,   &CPU::pushLrT,   &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,
        &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,
        &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,
        &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,
        &CPU::unkThumb,  &CPU::unkThumb,  &CPU::popT,      &CPU::popT,      &CPU::popT,      &CPU::popT,
        &CPU::popPcT,    &CPU::popPcT,    &CPU::popPcT,    &CPU::popPcT,    &CPU::unkThumb,  &CPU::unkThumb,
        &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,
        &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,
        &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,
        &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,
        &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,
        &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,    &CPU::stmiaT,
        &CPU::stmiaT,    &CPU::stmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,
        &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,
        &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,
        &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,
        &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,
        &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::ldmiaT,    &CPU::beqT,      &CPU::beqT,
        &CPU::beqT,      &CPU::beqT,      &CPU::bneT,      &CPU::bneT,      &CPU::bneT,      &CPU::bneT,
        &CPU::bcsT,      &CPU::bcsT,      &CPU::bcsT,      &CPU::bcsT,      &CPU::bccT,      &CPU::bccT,
        &CPU::bccT,      &CPU::bccT,      &CPU::bmiT,      &CPU::bmiT,      &CPU::bmiT,      &CPU::bmiT,
        &CPU::bplT,      &CPU::bplT,      &CPU::bplT,      &CPU::bplT,      &CPU::bvsT,      &CPU::bvsT,
        &CPU::bvsT,      &CPU::bvsT,      &CPU::bvcT,      &CPU::bvcT,      &CPU::bvcT,      &CPU::bvcT,
        &CPU::bhiT,      &CPU::bhiT,      &CPU::bhiT,      &CPU::bhiT,      &CPU::blsT,      &CPU::blsT,
        &CPU::blsT,      &CPU::blsT,      &CPU::bgeT,      &CPU::bgeT,      &CPU::bgeT,      &CPU::bgeT,
        &CPU::bltT,      &CPU::bltT,      &CPU::bltT,      &CPU::bltT,      &CPU::bgtT,      &CPU::bgtT,
        &CPU::bgtT,      &CPU::bgtT,      &CPU::bleT,      &CPU::bleT,      &CPU::bleT,      &CPU::bleT,
        &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::unkThumb,  &CPU::swiT,      &CPU::swiT,
        &CPU::swiT,      &CPU::swiT,      &CPU::bT,        &CPU::bT,        &CPU::bT,        &CPU::bT,
        &CPU::bT,        &CPU::bT,        &CPU::bT,        &CPU::bT,        &CPU::bT,        &CPU::bT,
        &CPU::bT,        &CPU::bT,        &CPU::bT,        &CPU::bT,        &CPU::bT,        &CPU::bT,
        &CPU::bT,        &CPU::bT,        &CPU::bT,        &CPU::bT,        &CPU::bT,        &CPU::bT,
        &CPU::bT,        &CPU::bT,        &CPU::bT,        &CPU::bT,        &CPU::bT,        &CPU::bT,
        &CPU::bT,        &CPU::bT,        &CPU::bT,        &CPU::bT,        &CPU::blxOffT,   &CPU::blxOffT,
        &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,
        &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,
        &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,
        &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,
        &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,   &CPU::blxOffT,
        &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,
        &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,
        &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,
        &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,
        &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blSetupT,
        &CPU::blSetupT,  &CPU::blSetupT,  &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,
        &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,
        &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,
        &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,
        &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,
        &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT,    &CPU::blOffT};

    const uint8_t condition[0x100] = {
        0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0,
        0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0,
        1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0,
        1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1,
        0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};


    const uint8_t bitCount[0x100] = {
        0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 1, 2, 2, 3, 2,
        3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3,
        3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5,
        6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4,
        3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4,
        5, 5, 6, 5, 6, 6, 7, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6,
        6, 7, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8};
};

} // namespace emulator_demo

#endif
