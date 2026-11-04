#include <stdint.h>
#include <sys/types.h>
#include "../emitter.h"
#include "../../meta/a64bitmasks.h"

#define ENUM(N) enum N typedef N; enum N
#define STRUCT(N) struct N typedef N; struct N

// provides mask of N bits
#define MASK(N) ((1 << N) - 1)
// get N bits from offset O in value V
#define GET_BITS(O, N, V) (((V) >> O) & MASK(N))
// move N bits of value V to offset O
#define PUT_BITS(O, N, V) (((V) & MASK(N)) << O)

// Xn refers to the 64bit variants of the registers,
// usually the 32bit aliases are Wn (we don't have enums
// for them because it's not that important, they're equal)
// NOTE(odin-tilde): GPR/FPR 寄存器枚举由 DSL reg_class 生成（见 a64_gen.h，
// 含 GPR_NONE/FPR_NONE 哨兵），这里只保留别名。
enum {
    FP = 29,   // frame pointer
    LR = 30,   // link register (RPC)
    SP = 31,   // context specific because ARM lmao (== ZR)
};

// NOTE(odin-tilde)：Condition 枚举由 DSL 生成（见 a64.machine 的 (enum Condition ...)，
// 落在 tb_gen_private.h，供生成代码与 emitter 共同使用）。

ENUM(ShiftType) {
    LSL, LSR, ASR, ROR,
};

ENUM(AddOp) {
    //      u
    ADD = 0b0,
    SUB = 0b1,
};

/* UDF
    0000 0000 0000 0000 iiii iiii iiii iiii
    i = imm16 immediate (ignored by hardware)
*/
static void udf(TB_CGEmitter* restrict e, uint16_t imm) {
    uint32_t inst = 0;
    inst |= PUT_BITS(0, 16, imm);
    inst &= ~MASK_reserved_perm_undef_UDF_only_perm_undef;
    inst |=  BITS_reserved_perm_undef_UDF_only_perm_undef;
    EMIT4(e, inst);
}

/* pc rel addressing
    pjj1 0000 iiii iiii iiii iiii iiid dddd
    p = adr/adrp
    j = imm lo
    i = imm hi
    d = destination register
*/
ENUM(PCRelOp) {
    ADR  = 0,
    ADRP = 1,
};
static void dpimm_pcreladdr(TB_CGEmitter* restrict e, PCRelOp op, GPR Rd, int32_t imm21) {
    uint32_t inst = 0;
    uint32_t immlo = GET_BITS(0,  2, imm21);
    uint32_t immhi = GET_BITS(2, 19, imm21);
    inst |= PUT_BITS(31,  1, op);
    inst |= PUT_BITS(29,  2, immlo);
    inst |= PUT_BITS( 5, 19, immhi);
    inst |= PUT_BITS( 0,  5, Rd);
    inst &= ~MASK_dpimm_pcreladdr;
    inst |=  BITS_dpimm_pcreladdr;
    EMIT4(e, inst);
}

/* add/sub (immediate)
    buf1 0001 0sii iiii iiii iinn nnnd dddd
    b = 32/64 bit
    u = add/sub
    f = ignore/set flags
    s = 0/12 shift
    i = 12 bit immediate
    n = source register
    d = destination register
*/
static void dpimm_addsub_imm(TB_CGEmitter* restrict e, bool wide, AddOp op, GPR Rd, GPR Rn, uint16_t imm12, bool shift, bool set_flags) {
    uint32_t inst = 0;
    inst |= PUT_BITS(31,  1, wide);
    inst |= PUT_BITS(30,  1, op);
    inst |= PUT_BITS(29,  1, set_flags);
    inst |= PUT_BITS(22,  1, shift);
    inst |= PUT_BITS(10, 12, imm12);
    inst |= PUT_BITS( 5,  5, Rn);
    inst |= PUT_BITS( 0,  5, Rd);
    inst &= ~MASK_dpimm_addsub_imm;
    inst |=  BITS_dpimm_addsub_imm;
    EMIT4(e, inst);
}

/* move wide (immediate)
    bpp1 0010 1ssi iiii iiii iiii iiid dddd
    b = 32/64 bit
    p = opcode
    s = shift amount
    i = imm16
    d = destination register
*/
ENUM(MoveOp) {
    MOVN = 0b00, // move inverse immediate
    MOVZ = 0b10, // move zero-extended immediate
    MOVK = 0b11, // move immediate (keep other bits)
};
static void dpimm_movewide(TB_CGEmitter* restrict e, bool wide, MoveOp opc, GPR Rd, uint16_t imm16, uint8_t shift) {
    uint32_t inst = 0;
    inst |= PUT_BITS(31,  1, wide);
    inst |= PUT_BITS(29,  2, opc);
    inst |= PUT_BITS(21,  2, shift);
    inst |= PUT_BITS( 5, 16, imm16);
    inst |= PUT_BITS( 0,  5, Rd);
    inst &= ~MASK_dpimm_movewide;
    inst |=  BITS_dpimm_movewide;
    EMIT4(e, inst);
}

/* conditional branch (immediate)
    0101 0100 iiii iiii iiii iiii iiip cccc
    i = 19 bit address
    p = consistent?
    c = condition
*/
static void control_condbranch(TB_CGEmitter* restrict e, Condition cond, uint32_t address, bool consistent) {
    uint32_t inst = 0;
    inst |= PUT_BITS(5, 19, address);
    inst |= PUT_BITS(4, 1, consistent);
    inst |= PUT_BITS(0, 4, cond);
    inst &= ~MASK_control_condbranch;
    inst |=  BITS_control_condbranch;
    EMIT4(e, inst);
}

/* exception generation
    1101 0100 pppi iiii iiii iiii iiiq qqll
    p = opcode
    i = imm16
    q = opcode 2
    l = LL
*/
static void exception_break(TB_CGEmitter* restrict e, uint16_t imm16) {
    uint32_t inst = 0b11010100001000000000000000000000;
    inst |= PUT_BITS(5, 16, imm16);
    EMIT4(e, inst);
}
static void exception_halt(TB_CGEmitter* restrict e, uint16_t imm16) {
    uint32_t inst = 0b11010100010000000000000000000000;
    inst |= PUT_BITS(5, 16, imm16);
    EMIT4(e, inst);
}

/* hints
    1101 0101 0000 0011 0010 rrrr ppp1 1111
    r = CRm
    p = opcode 2
*/
static void hints_nop(TB_CGEmitter* restrict e) {
    uint32_t inst = 0b11010101000000110010000000011111;
    EMIT4(e, inst);
}

/* system register move
    1101 0101 00d1 ssss ssss ssss ssst tttt
    d = store/load
    s = system register
    t = general register
*/
ENUM(SystemMoveOp) {
    SYSMOVE_STORE,
    SYSMOVE_LOAD,
};
uint16_t typedef SystemReg;
static void control_systemmove(TB_CGEmitter* restrict e, SystemMoveOp op, SystemReg Rs, GPR Rt) {
    uint32_t inst = 0;
    inst |= PUT_BITS(21,  1, op);
    inst |= PUT_BITS( 5, 15, Rs);
    inst |= PUT_BITS( 0,  5, Rt);
    inst &= ~MASK_control_systemmove;
    inst |=  BITS_control_systemmove;
    EMIT4(e, inst);
}

/* unconditional branch (register)
    1101 011p pppq qqqq rrrr rrnn nnnm mmmm
    p = opcode
    q = second opcode always 11111
    r = third opcode (auth related)
    n = address register
    m = modifier register, auth related
*/
ENUM(BranchOp) {
    //       pppp
    BR   = 0b0000,
    BLR  = 0b0001,
    RET  = 0b0010,
    ERET = 0b0100,
    DRPS = 0b0101,
};
static void control_branch_reg(TB_CGEmitter* restrict e, BranchOp op, GPR Rn) {
    uint32_t inst = 0;
    if (op == ERET || op == DRPS) Rn = 0b11111;
    uint32_t q = 0b11111; // always 11111
    uint32_t r = 0b00000; // always 00000 for our use
    uint32_t m = 0b00000; // always 00000 for our use
    inst |= PUT_BITS(21, 4, op);
    inst |= PUT_BITS(16, 5, q);
    inst |= PUT_BITS(10, 6, r);
    inst |= PUT_BITS( 5, 5, Rn);
    inst |= PUT_BITS( 0, 5, m);
    inst &= ~MASK_control_branch_reg;
    inst |=  BITS_control_branch_reg;
    EMIT4(e, inst);
}

/* unconditional branch (immediate)
    p001 01ii iiii iiii iiii iiii iiii iiii
    p = link?
    i = 26 bit immediate
*/
static void control_branch_imm(TB_CGEmitter* restrict e, uint32_t imm26, bool with_link) {
    uint32_t inst = 0;
    inst |= PUT_BITS(31,  1, with_link);
    inst |= PUT_BITS( 0, 26, imm26);
    inst &= ~MASK_control_branch_imm;
    inst |=  BITS_control_branch_imm;
    EMIT4(e, inst);
}

/* compare and branch (immediate)
    b011 010p iiii iiii iiii iiii iiit tttt
    b = 32/64 bit
    p = zero/not-zero
    i = imm19
    t = register to compare
*/
static void control_compbranch(TB_CGEmitter* restrict e, bool wide, bool zero, GPR Rt, uint16_t imm19) {
    uint32_t inst = 0;
    inst |= PUT_BITS(31,  1, wide);
    inst |= PUT_BITS(24,  1, !zero);
    inst |= PUT_BITS( 5, 19, imm19);
    inst |= PUT_BITS( 0,  5, Rt);
    inst &= ~MASK_control_compbranch;
    inst |=  BITS_control_compbranch;
    EMIT4(e, inst);
}

/* compare registers and branch
    b111 0100 cccm mmmm 00ii iiii iiit tttt
    b = 32/64 bit
    c = condition
    m = 2nd register
    i = imm9 relative address
    t = tested register
*/
ENUM(CompBranchCC) {
    CB_GT, CB_GE, CB_HI, CB_HS, CB_EQ, CB_NE,
};
static void control_compbranch_regs(TB_CGEmitter* restrict e, bool wide, CompBranchCC cond, GPR Rt, GPR Rm, uint16_t reladdr) {
    uint32_t inst = 0;
    inst |= PUT_BITS(21, 3, cond);
    inst |= PUT_BITS(16, 5, Rm);
    inst |= PUT_BITS(5, 9, reladdr);
    inst |= PUT_BITS(0, 5, Rt);
    inst &= ~MASK_control_compbranch_regs;
    inst |=  BITS_control_compbranch_regs;
    EMIT4(e, inst);
}

/* compare register with immediate and branch
    b111 0100 ccci iiii i0jj jjjj jjjt tttt
    b = 32/64 bit
    c = condition
    i = imm6
    j = imm9 relative address
    t = tested register
*/
static void control_compbranch_imm(TB_CGEmitter* restrict e, bool wide, CompBranchCC cond, GPR Rt, uint8_t imm, uint16_t reladdr) {
    uint32_t inst = 0;
    inst |= PUT_BITS(21, 3, cond);
    inst |= PUT_BITS(15, 6, imm);
    inst |= PUT_BITS(5, 9, reladdr);
    inst |= PUT_BITS(0, 5, Rt);
    inst &= ~MASK_control_compbranch_imm;
    inst |=  BITS_control_compbranch_imm;
    EMIT4(e, inst);
}

/* data processing (2 source)
    b0s1 1010 110m mmmm pppp ppnn nnnd dddd
    b = 32/64 bit
    s = always 0 apart from SUBPS (FEAT_MTE)
    m = 2nd register
    p = opcode
    n = 1st register
    d = destination register
*/
ENUM(DPR2op) {
    // op    pppppp
    UDIV = 0b000010, // d = n / m (unsigned)
    SDIV = 0b000011, // d = n / m (signed)
    LSLV = 0b001000, // d = n << m
    LSRV = 0b001001, // d = n >> m (zero)
    ASRV = 0b001010, // d = n >> m (sign)
    RORV = 0b001011, // rotate right
    SMAX = 0b011000, // d = n > m ? n : m (signed)
    UMAX = 0b011001, // d = n > m ? n : m (unsigned)
    SMIN = 0b011010, // d = n < m ? n : m (signed)
    UMIN = 0b011011, // d = n < m ? n : m (unsigned)
};
static void dpreg_dp_2src(TB_CGEmitter* restrict e, bool wide, DPR2op op, GPR Rd, GPR Rn, GPR Rm) {
    uint32_t inst = 0;
    inst |= PUT_BITS(31, 1, wide);
    inst |= PUT_BITS(16, 5, Rm);
    inst |= PUT_BITS(10, 6, op);
    inst |= PUT_BITS( 5, 5, Rn);
    inst |= PUT_BITS( 0, 5, Rd);
    inst &= ~MASK_dpreg_dp_2src;
    inst |=  BITS_dpreg_dp_2src;
    EMIT4(e, inst);
}

/* data processing (1 source)
    b1s1 1010 110p pppp qqqq qqnn nnnd dddd
    b = 32/64 bit
    s = i don't know but it's always 0 according to ISA_A64_xml_A_profile-2024-09.pdf
    p = opcode2
    q = opcode1
    n = 1st register
    d = destination register
*/
ENUM(DPR1op) {
    // op1    qqqqqq
    RBIT  = 0b000000, // reverse bits
    REV16 = 0b000001, // reverse each 2-byte halfwords
    REV   = 0b000010, // reverse bytes
    CLZ   = 0b000100, // count leading zeros
    CLS   = 0b000101, // count leading signs
    CTZ   = 0b000110, // count trailing zeros
    CNT   = 0b000111, // count set bits
    ABS   = 0b001000, // absolute of signed value
};
static void dpreg_dp_1src(TB_CGEmitter* restrict e, bool wide, DPR1op op, GPR Rd, GPR Rn) {
    uint32_t inst = 0;
    uint32_t p = 0; // always 0 for what we do currently
    inst |= PUT_BITS(31, 1, wide);
    inst |= PUT_BITS(16, 5, p);
    inst |= PUT_BITS(10, 6, op);
    inst |= PUT_BITS( 5, 5, Rn);
    inst |= PUT_BITS( 0, 5, Rd);
    inst &= ~MASK_dpreg_dp_1src;
    inst |=  BITS_dpreg_dp_1src;
    EMIT4(e, inst);
}

/* logical (shifted register)
    bcc0 1010 ssum mmmm iiii iinn nnnd dddd
    b = 32/64 bit
    c = opcode and/or/eor/ands
    s = shift type lsl/lsr/asr/ror
    u = negate 2nd reg
    m = 2nd reg (shifted)
    i = imm6 shift amount
    n = 1st reg
    d = destination reg
*/
ENUM(LogicOp) {
    //       ccu
    AND  = 0b000, // d = n & m
    BIC  = 0b001, // d = n & ~m
    ORR  = 0b010, // d = n | m
    ORN  = 0b011, // d = n | ~m
    EOR  = 0b100, // d = n ^ m
    EON  = 0b101, // d = n ^ ~m
    ANDS = 0b110, // d = n & m (set flags)
    BICS = 0b111, // d = n & ~m (set flags)
};
static void dpreg_log_shift(TB_CGEmitter* restrict e, bool wide, LogicOp op, GPR Rd, GPR Rn, GPR Rm, uint8_t imm6, ShiftType shift) {
    uint32_t inst = 0;
    uint32_t code = GET_BITS(1, 2, op);
    uint32_t neg  = GET_BITS(0, 1, op);
    inst |= PUT_BITS(31, 1, wide);
    inst |= PUT_BITS(29, 2, code);
    inst |= PUT_BITS(22, 2, shift);
    inst |= PUT_BITS(21, 1, neg);
    inst |= PUT_BITS(16, 5, Rm);
    inst |= PUT_BITS(10, 6, imm6);
    inst |= PUT_BITS( 5, 5, Rn);
    inst |= PUT_BITS( 0, 5, Rd);
    inst &= ~MASK_dpreg_log_shift;
    inst |=  BITS_dpreg_log_shift;
    EMIT4(e, inst);
}

/* add/sub (shifted register)
    buf0 1011 ss0m mmmm iiii iinn nnnd dddd
    b = 32/64 bit
    u = add/sub
    f = ignore/set flags
    s = shift type
    m = 2nd register
    i = shift amount
    n = 1st register
    d = destination register
*/
static void dpreg_addsub_shift(TB_CGEmitter* restrict e, bool wide, AddOp op, GPR Rd, GPR Rn, GPR Rm, uint8_t imm6, ShiftType shift, bool set_flags) {
    uint32_t inst = 0;
    inst |= PUT_BITS(31, 1, wide);
    inst |= PUT_BITS(30, 1, op);
    inst |= PUT_BITS(29, 1, set_flags);
    inst |= PUT_BITS(22, 2, shift);
    inst |= PUT_BITS(16, 5, Rm);
    inst |= PUT_BITS(10, 6, imm6);
    inst |= PUT_BITS( 5, 5, Rn);
    inst |= PUT_BITS( 0, 5, Rd);
    inst &= ~MASK_dpreg_addsub_shift;
    inst |=  BITS_dpreg_addsub_shift;
    EMIT4(e, inst);
}

/* add/sub (extended register, 64-bit) —— Rn 允许 SP（shifted register 的
    31 是 ZR，extended 的 31 是 SP，这是两者唯一差别）。Rm 按 UXTX 恒等
    扩展。模板 0x0B206000 取自 clang `add x0,x0,w1,sxtw`=0x8B21C000 实测：
    S=0、01011@28-24、00@23-22、option=UXTX(3)@15-13、imm3=0@12-10。
*/
static void dpreg_addsub_ext64(TB_CGEmitter* restrict e, AddOp op, GPR Rd, GPR Rn, GPR Rm) {
    uint32_t inst = 0x0B206000u;
    inst |= PUT_BITS(31, 1, 1);   // 64-bit only
    inst |= PUT_BITS(30, 1, op);
    inst |= PUT_BITS(16, 5, Rm);
    inst |= PUT_BITS( 5, 5, Rn);
    inst |= PUT_BITS( 0, 5, Rd);
    EMIT4(e, inst);
}

/* conditional select
    bps1 1010 110m mmmm cccc qqnn nnnd dddd
    b = 32/64 bit
    p = op
    s = always 0, 1 is unallocated :)
    m = 2nd reg
    c = condition
    q = op2
    n = 1st reg
    d = destination reg
*/
ENUM(CondSelOp) {
    // opcode pqq
    CSEL  = 0b000, // d = c ? n : m
    CSINC = 0b001, // d = c ? n : ++m
    CSINV = 0b100, // d = c ? n : ~m
    CSNEG = 0b101, // d = c ? n : -m
};
static void dpreg_condsel(TB_CGEmitter* restrict e, bool wide, CondSelOp op, Condition cond, GPR Rd, GPR Rn, GPR Rm) {
    uint32_t inst = 0;
    uint32_t p = GET_BITS(2, 1, op);
    uint32_t q = GET_BITS(0, 2, op);
    uint32_t s = 0; // always 0
    inst |= PUT_BITS(31, 1, wide);
    inst |= PUT_BITS(30, 1, p);
    inst |= PUT_BITS(29, 1, s);
    inst |= PUT_BITS(16, 5, Rm);
    inst |= PUT_BITS(12, 4, cond);
    inst |= PUT_BITS(10, 2, q);
    inst |= PUT_BITS( 5, 5, Rn);
    inst |= PUT_BITS( 0, 5, Rd);
    inst &= ~MASK_dpreg_condsel;
    inst |=  BITS_dpreg_condsel;
    EMIT4(e, inst);
}

/* conditional compare (register)
    bps1 1010 001m mmmm cccc 0qnn nnnr ffff
    b = 32/64 bit
    p = opcode 1, when 0 means compare A and ¬B, when 1 means compare A and B
    s = idk but always 1
    m = 2nd register
    c = condition
    q = opcode 2 always 0, unallocated :)
    n = 1st register
    r = opcode 3 always 0, unallocated :)
    f = flag literal

    ---1 1010 001m mmmm cccc 0-nn nnn- vvvv
    size         Rm     cond   Rn      nzcv
    abc1 1010 010- ---- ---- 0d-- ---e ----
    --0                       -      - unallocated :)
    -01                       0      0 ccmn (conditional compare negative)
    -11                       0      0 ccmp ()
*/
ENUM(CCop) {
    CCMN, // flags = c ? n <=> ~m : f
    CCMP, // flags = c ? n <=>  m : f
};
static void dpreg_condcmp_reg(TB_CGEmitter* restrict e, bool wide, CCop op, Condition cond, GPR Rn, GPR Rm, uint8_t else_flags) {
    uint32_t inst = 0;
    uint32_t s = 1; // always 1
    uint32_t q = 0; // always 0
    uint32_t r = 0; // always 0
    inst |= PUT_BITS(31, 1, wide);
    inst |= PUT_BITS(30, 1, op);
    inst |= PUT_BITS(29, 1, s);
    inst |= PUT_BITS(16, 5, Rm);
    inst |= PUT_BITS(12, 4, cond);
    inst |= PUT_BITS(10, 1, q);
    inst |= PUT_BITS( 5, 5, Rn);
    inst |= PUT_BITS( 4, 1, r);
    inst |= PUT_BITS( 0, 4, else_flags);
    inst &= ~MASK_dpreg_condcmp_reg;
    inst |=  BITS_dpreg_condcmp_reg;
    EMIT4(e, inst);
}

/*  data processing (3 source)
    bpp1 1011 qqqm mmmm raaa aann nnnd dddd
    b = 32/64 bit
    p = opcode bits 5-4
    q = opcode bits 3-1
    m = 2nd register
    r = opcode bits 0
    a = 3rd register
    n = 1st register
    d = destination register
*/
ENUM(DPR3op) {
    // opcode  ppqqqr
    MADD   = 0b000000, // d = a + n * m
    MSUB   = 0b000001, // d = a - n * m
    SMADDL = 0b000010, // d = a + n * m (64bit + 32bit * 32bit) (signed)
    SMSUBL = 0b000011, // d = a - n * m (64bit - 32bit * 32bit) (signed)
    SMULH  = 0b000100, // d = (n * m) >> 64 (signed)
    UMADDL = 0b001010, // d = a + n * m (64bit + 32bit * 32bit) (unsigned)
    UMSUBL = 0b001011, // d = a - n * m (64bit - 32bit * 32bit) (unsigned)
    UMULH  = 0b001100, // d = (n * m) >> 64 (unsigned)
};
static void dpreg_dp_3src(TB_CGEmitter* restrict e, bool wide, DPR3op op, GPR Rd, GPR Rn, GPR Rm, GPR Ra) {
    uint32_t inst = 0;
    if (op != MADD && op != MSUB) wide = true;
    if (op == SMULH || op == UMULH) Ra = 0b11111;
    uint32_t p = GET_BITS(4, 2, op);
    uint32_t q = GET_BITS(1, 3, op);
    uint32_t r = GET_BITS(0, 1, op);
    inst |= PUT_BITS(31, 1, wide);
    inst |= PUT_BITS(29, 2, p);
    inst |= PUT_BITS(21, 3, q);
    inst |= PUT_BITS(16, 5, Rm);
    inst |= PUT_BITS(15, 1, r);
    inst |= PUT_BITS(10, 5, Ra);
    inst |= PUT_BITS( 5, 5, Rn);
    inst |= PUT_BITS( 0, 5, Rd);
    inst &= ~MASK_dpreg_dp_3src;
    inst |=  BITS_dpreg_dp_3src;
    EMIT4(e, inst);
}



ENUM(FPtype) {
    Single = 0b00,
    Double = 0b01,
    Half   = 0b11,
};

/* conversion between floating-point and integer
    b0s1 1110 tt1m mppp 0000 00nn nnnd dddd
    b = 32/64 bit
    s = always 0
    t = floating point type
    m = rmode (different meanings)
    p = opcode
    n = source register
    d = destination register
*/
ENUM(FPconvINTop) {
    FCVTZS   = 0b000,
    FCVTZU   = 0b001,
    SCVTF    = 0b010,
    UCVTF    = 0b011,
    FMOV_F2I = 0b110,
    FMOV_I2F = 0b111,
};
static void simd_dp_float2int(TB_CGEmitter* restrict e, bool wide, FPtype type, FPconvINTop op, GPR Rd, GPR Rn, bool top_half) {
    uint32_t inst = 0;
    // rmode 是 2 位字段，不能存进 bool（0b11 会塌成 1 = FCVTP*，向 +inf 舍入）
    uint32_t rmode = top_half;
    if (op == FCVTZS || op == FCVTZU) rmode = 0b11;
    if (op == SCVTF  || op == UCVTF)  rmode = 0b00;
    uint32_t s = 0; // always 0
    inst |= PUT_BITS(31, 1, wide);
    inst |= PUT_BITS(29, 1, s);
    inst |= PUT_BITS(22, 2, type);
    inst |= PUT_BITS(19, 2, rmode);
    inst |= PUT_BITS(16, 3, op);
    inst |= PUT_BITS( 5, 5, Rn);
    inst |= PUT_BITS( 0, 5, Rd);
    inst &= ~MASK_simd_dp_float2int;
    inst |=  BITS_simd_dp_float2int;
    EMIT4(e, inst);
}

/* floating-point data-processing (1 source)
    m0s1 1110 tt1p pppp p100 00nn nnnd dddd
    m = always 0
    s = always 0
    t = floating-point type
    p = opcode
    n = source register
    d = destination register
*/
ENUM(FPDP1op) {
    //       oooooo
    FMOV = 0b000000,
    FNEG = 0b000010,
    FSQRT = 0b000011,
};
static void simd_dp_floatdp1(TB_CGEmitter* restrict e, FPtype type, FPDP1op op, GPR Rd, GPR Rn) {
    uint32_t inst = 0;
    uint32_t m = 0; // always 0
    uint32_t s = 0; // always 0
    inst |= PUT_BITS(31, 1, m);
    inst |= PUT_BITS(29, 1, s);
    inst |= PUT_BITS(22, 2, type);
    inst |= PUT_BITS(15, 6, op);
    inst |= PUT_BITS( 5, 5, Rn);
    inst |= PUT_BITS( 0, 5, Rd);
    inst &= ~MASK_simd_dp_floatdp1;
    inst |=  BITS_simd_dp_floatdp1;
    EMIT4(e, inst);
}

//!! floating-point compare

//!! floating-point immediate

//!! floating-point conditional compare

/* floating-point data-processing (2 source)
    a0s1 1110 tt1m mmmm pppp 10nn nnnd dddd
    a = always 0
    s = always 0
    t = floating point type
    m = 2nd source register
    p = opcode
    n = 1st source register
    d = destination register
*/
ENUM(FPDP2op) {
    FMUL   = 0b0000, // d = n * m
    FDIV   = 0b0001, // d = n / m
    FADD   = 0b0010, // d = n + m
    FSUB   = 0b0011, // d = n - m
    FMAX   = 0b0100, // d = n > m ? n : m (prefers NaN)
    FMIN   = 0b0101, // d = n < m ? n : m (prefers NaN)
    FMAXNM = 0b0110, // d = n > m ? n : m (prefers Number)
    FMINNM = 0b0111, // d = n < m ? n : m (prefers Number)
    FNMUL  = 0b1000, // d = -(n * m)
};
static void simd_dp_floatdp2(TB_CGEmitter* restrict e, FPtype type, FPDP2op op, GPR Rd, GPR Rn, GPR Rm) {
    uint32_t inst = 0;
    uint32_t a = 0; // always 0
    uint32_t s = 0; // always 0
    inst |= PUT_BITS(31, 1, a);
    inst |= PUT_BITS(29, 1, s);
    inst |= PUT_BITS(22, 2, type);
    inst |= PUT_BITS(16, 5, Rm);
    inst |= PUT_BITS(12, 4, op);
    inst |= PUT_BITS( 5, 5, Rn);
    inst |= PUT_BITS( 0, 5, Rd);
    inst &= ~MASK_simd_dp_floatdp2;
    inst |=  BITS_simd_dp_floatdp2;
    EMIT4(e, inst);
}

//!! floating-point conditional select

//!! floating-point data-processing (3 source)

/* load register (literal)
    pp01 1r00 iiii iiii iiii iiii iiit tttt
    p = opcode
    r = int/float
    i = 19 bit immediate
    t = target resgister
*/

static void ldst_loadlit(TB_CGEmitter* restrict e, bool wide, bool fp, GPR Rt, int32_t imm19) {
    uint32_t inst = 0;
    inst |= PUT_BITS(30,  1, wide);
    inst |= PUT_BITS(26,  1, fp);
    inst |= PUT_BITS( 5, 19, imm19);
    inst |= PUT_BITS( 0,  5, Rt);
    inst &= ~MASK_ldst_loadlit;
    inst |=  BITS_ldst_loadlit;
    EMIT4(e, inst);
}

////////////////
// NOTE(odin-tilde): 以下为阶段2补充的 load/store 与浮点编码函数。
////////////////

/* load/store register (unsigned immediate, scaled)
    1x 111 0 01 iiiiiiiiiiii nnnnn ttttt
    [31:30] size (log2 bytes), [26] V (0=GPR,1=SIMD&FP),
    [23:22] opc: 00=store, 01=load zero-extend,
                  10=load sign-extend to 32 (或 LDRSW), 11=load sign-extend to 64,
    imm12 = offset >> size（caller 保证对齐）, Rn=base（允许 SP）, Rt
    覆盖 STRB..STRX / LDRB..LDRX / LDRSB/SH/SW（符号扩展）。
*/
static void ldst_mem_imm(TB_CGEmitter* restrict e, bool fp, int size_log2, int opc, GPR Rt, GPR Rn, uint32_t imm12) {
    uint32_t inst = 0;
    inst |= PUT_BITS(30,  2, size_log2);
    inst |= PUT_BITS(26,  1, fp);
    inst |= PUT_BITS(22,  2, opc);
    inst |= PUT_BITS(10, 12, imm12);
    inst |= PUT_BITS( 5,  5, Rn);
    inst |= PUT_BITS( 0,  5, Rt);
    // LDR X0,[X1] = 0xF9400020, STR X0,[X1] = 0xF9000020
    inst |= 0x39000000;
    EMIT4(e, inst);
}

/* load/store register (unscaled 9-bit signed offset, LDUR/STUR 家族)
    1x 111 0 00 0 iiiiiiiiii 00 nnnnn ttttt
    与 ldst_mem_imm 相同的 size/opc 语义，但 offset 为 -256..255 的原始字节数。
*/
static void ldst_mem_unscaled(TB_CGEmitter* restrict e, bool fp, int size_log2, int opc, GPR Rt, GPR Rn, int16_t simm9) {
    uint32_t inst = 0;
    inst |= PUT_BITS(30,  2, size_log2);
    inst |= PUT_BITS(26,  1, fp);
    inst |= PUT_BITS(22,  2, opc);
    inst |= PUT_BITS(12,  9, simm9 & 0x1FF);
    inst |= PUT_BITS( 5,  5, Rn);
    inst |= PUT_BITS( 0,  5, Rt);
    // LDUR X0,[X1] = 0xF8400020, STUR X0,[X1] = 0xF8000020
    inst |= 0x38000000;
    EMIT4(e, inst);
}

/* floating-point compare
    000 11110 tt 1 mmmmm 0010 00 nnnnn 000000
    type: 00=single 01=double
    结果写 NZCV：a>b→N0 Z0 C1 V0；a==b→N0 Z1 C1 V0；
              a<b→N1 Z0 C0 V0；unordered→N0 Z0 C1 V1
    因此 GT=GE? 映射：FLT→MI、FLE→LS、FEQ→EQ、FNE→NE、FGT→GT、FGE→GE。
*/
static void simd_dp_fcmp(TB_CGEmitter* restrict e, FPtype type, GPR Rn, GPR Rm) {
    uint32_t inst = 0;
    inst |= PUT_BITS(22,  2, type);
    inst |= PUT_BITS(21,  1, 1);
    inst |= PUT_BITS(16,  5, Rm);
    inst |= PUT_BITS( 5,  5, Rn);
    // FCMP S0,S0 = 0x1E202000 / D0,D0 = 0x1E602000（基值含 bit21+bit13）
    inst |= 0x1E202000;
    EMIT4(e, inst);
}

/* floating-point conditional select
    000 11110 tt 0 mmmmm cccc 11 nnnnn ddddd
*/
static void simd_dp_fcsel(TB_CGEmitter* restrict e, FPtype type, GPR Rd, GPR Rn, GPR Rm, Condition cond) {
    uint32_t inst = 0;
    inst |= PUT_BITS(22,  2, type);
    inst |= PUT_BITS(21,  1, 1);
    inst |= PUT_BITS(16,  5, Rm);
    inst |= PUT_BITS(12,  4, cond);
    inst |= PUT_BITS(10,  2, 0b11);
    inst |= PUT_BITS( 5,  5, Rn);
    inst |= PUT_BITS( 0,  5, Rd);
    // FCSEL S0,S0,S0,EQ = 0x1E200C00
    inst |= 0x1E200400;
    EMIT4(e, inst);
}

/* floating-point convert (scalar, S<->D)
    000 11110 0x10001 000000 nnnnn ddddd
    fcvt_sd: double → single；fcvt_ds: single → double
*/
static void simd_dp_fcvt_sd(TB_CGEmitter* restrict e, GPR Rd, GPR Rn) { // d = (float) n(double)
    uint32_t inst = 0x1E624000;
    inst |= PUT_BITS( 5, 5, Rn);
    inst |= PUT_BITS( 0, 5, Rd);
    EMIT4(e, inst);
}
static void simd_dp_fcvt_ds(TB_CGEmitter* restrict e, GPR Rd, GPR Rn) { // d = (double) n(single)
    uint32_t inst = 0x1E22C000;
    inst |= PUT_BITS( 5, 5, Rn);
    inst |= PUT_BITS( 0, 5, Rd);
    EMIT4(e, inst);
}

/* bitfield move (SBFM/UBFM)：符号/零扩展、位提取
    x 10 100110 N immr imms nnnnn ddddd
    N = wide；UBFM: opc=10, SBFM: opc=00
    常用别名：UXTB=UBFM #0,#7  SXTB=SBFM #0,#7  UXTH #0,#15  SXTH #0,#15
              UXTW/SXTW #0,#31（64 位形式）
*/
static void dpimm_bitfield(TB_CGEmitter* restrict e, bool wide, bool sign, GPR Rd, GPR Rn, uint8_t immr, uint8_t imms) {
    uint32_t inst = 0;
    inst |= PUT_BITS(31,  1, wide);
    inst |= PUT_BITS(22,  1, wide); // N
    inst |= PUT_BITS(16,  6, immr);
    inst |= PUT_BITS(10,  6, imms);
    inst |= PUT_BITS( 5,  5, Rn);
    inst |= PUT_BITS( 0,  5, Rd);
    // UBFM W0,W0,#0,#7 = 0x53001C00；SBFM X0,X0,#0,#31 = 0x93407C00
    inst |= sign ? 0x13000000 : 0x53000000;
    EMIT4(e, inst);
}

/* A64 tree -> TB_NodeTypeEnum
    > reserved
        - UDF
    > data processing (immediate)
        > pc rel addressing
            adr - TB_PTR_OFFSET
        > move wide
            movz - TB_ICONST
            movk - TB_ICONST
    > branches, exceptions, system
        > conditional branch (immediate)
            b.cond - TB_BRANCH
        > exception generation
            brk - TB_DEBUGBREAK/TB_TRAP
        > hints
            nop
        > system register move
        > unconditional branch (register)
            br  - TB_BRANCH
            blr - TB_CALL
            ret - TB_RETURN
        > unconditional branch (immediate)
            b - TB_BRANCH
            bl - TB_CALL
        > compare and branch (immediate)
        > compare registers and branch
        > compare register with immediate and branch
    > data processing (register)
        > data processing (2 source)
            udiv - TB_UDIV
            sdiv - TB_SDIV
            lslv - TB_SHL
            lsrv - TB_SHR
            asrv - TB_SAR
            rorv - TB_ROR (+TB_ROL)
        > data processing (1 source)
            rev - TB_BSWAP
            clz - TB_CLZ
            ctz - TB_CTZ
            cnt - TB_POPCNT
        > logical (shifted register)
            and/bic/ands/bics - TB_AND
            orr/orn           - TB_OR
            eor/eon           - TB_XOR
        > add/sub (shifted register)
            add  - TB_ADD
            sub  - TB_SUB
        > conditional select
            csel - TB_SELECT
        > conditional compare (register)
        > data processing (3 source)
            madd - TB_MUL
    > data processing -- scalar floating-point and advanced simd
        > conversion between floating-point and fixed-point
        > conversion between floating-point and integer
            fmov - TB_MACH_MOVE
        > floating-point data-processing (1 source)
            fneg - TB_FNEG
        > floating-point compare
        > floating-point immediate
        > floating-point conditional compare
        > floating-point data-processing (2 source)
            fmul - TB_FMUL
            fdiv - TB_FDIV
            fadd - TB_FADD
            fsub - TB_FSUB
            fmax - TB_FMAX
            fmin - TB_FMIN
        > floating-point conditional select
        > floating-point data-processing (3 source)
    > loads and stores
        > load register (literal)
            ldr

nodes to implement
// Conversions (?)
TB_TRUNCATE,
TB_FLOAT_TRUNC,
TB_FLOAT_EXT,
TB_SIGN_EXT,
TB_ZERO_EXT,
TB_UINT2FLOAT,
TB_FLOAT2UINT,
TB_INT2FLOAT,
TB_FLOAT2INT,
TB_BITCAST,

// Select
TB_SELECT, csel, fcsel

// Bitmagic
TB_BSWAP, rev
TB_CLZ, clz
TB_CTZ, ctz
TB_POPCNT, cnt

// Integer arithmatic
TB_AND, and
TB_OR, orr
TB_XOR, eor
TB_ADD, add
TB_SUB, sub
TB_MUL, (mul a b c) = (madd a b c zr)

TB_SHL, lslv
TB_SHR, lsrv
TB_SAR, asrv
TB_ROL, (? gonna have to be rorv but negative)
TB_ROR, rorv
TB_UDIV, udiv
TB_SDIV, sdiv
TB_UMOD, (? gonna have to do some funky math)
TB_SMOD, (https://devblogs.microsoft.com/oldnewthing/20220801-00/?p=106922)
    There is no instruction for calculating the remainder.
    You can do that manually by calculating
        r = n − (n ÷ d) × d
    This can be done by following up the division with an msub:
    ; unsigned remainder after division
    udiv    Rq, Rn, Rm          ; Rq = Rn ÷ Rm
    msub    Rr, Rq, Rm, Rn      ; Rr = Rn - Rq × Rm
                                ;    = Rn - (Rn ÷ Rm) × Rm
    ; signed remainder after division
    sdiv    Rq, Rn, Rm          ; Rq = Rn ÷ Rm
    msub    Rr, Rq, Rm, Rn      ; Rr = Rn - Rq × Rm
                                ;    = Rn - (Rn ÷ Rm) × Rm


// Float arithmatic
TB_FADD, fadd
TB_FSUB, fsub
TB_FMUL, fmul
TB_FDIV, fdiv
TB_FMIN, fmin
TB_FMAX, fmax

// Comparisons (?cmp)
TB_CMP_EQ,
TB_CMP_NE,
TB_CMP_ULT,
TB_CMP_ULE,
TB_CMP_SLT,
TB_CMP_SLE,
TB_CMP_FLT,
TB_CMP_FLE,

TB_FRAME_PTR,
*/
