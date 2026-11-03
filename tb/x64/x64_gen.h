#include "../emitter.h"
#include "../tb_internal.h"
#include <string.h>
#include <ctype.h>

static void global_init() {}

void TB_X86_FeatureSet__set(TB_X86_FeatureSet* out, TB_X86_Feature ent) {
    out->set[ent / 64] |= 1ull << (ent % 64);
    switch (ent) {
    case TB_X86_FEATURE_SSE3:
        out->set[0] |= (1ull << 1ull);
        break;
    case TB_X86_FEATURE_SSE41:
        out->set[0] |= (1ull << 2ull) | (1ull << 1ull);
        break;
    case TB_X86_FEATURE_SSE42:
        out->set[0] |= (1ull << 2ull) | (1ull << 1ull);
        break;
    case TB_X86_FEATURE_AVX2:
        out->set[0] |= (1ull << 11ull);
        break;
    case TB_X86_FEATURE_X86_64_V1:
        out->set[0] |= (1ull << 1ull);
        break;
    case TB_X86_FEATURE_X86_64_V2:
        out->set[0] |= (1ull << 13ull) | (1ull << 1ull) | (1ull << 2ull) | (1ull << 3ull) | (1ull << 4ull) | (1ull << 5ull);
        break;
    case TB_X86_FEATURE_X86_64_V3:
        out->set[0] |= (1ull << 14ull) | (1ull << 13ull) | (1ull << 1ull) | (1ull << 2ull) | (1ull << 3ull) | (1ull << 4ull) | (1ull << 5ull) | (1ull << 11ull) | (1ull << 12ull) | (1ull << 9ull) | (1ull << 10ull) | (1ull << 6ull);
        break;
    case TB_X86_FEATURE_X86_64_V4:
        out->set[0] |= (1ull << 15ull) | (1ull << 14ull) | (1ull << 13ull) | (1ull << 1ull) | (1ull << 2ull) | (1ull << 3ull) | (1ull << 4ull) | (1ull << 5ull) | (1ull << 11ull) | (1ull << 12ull) | (1ull << 9ull) | (1ull << 10ull) | (1ull << 6ull);
        break;
    default:
        tb_unreachable();
        break;
    }
}

bool TB_X86_FeatureSet__get(TB_X86_FeatureSet const* out, TB_X86_Feature ent) {
    TB_ASSERT(ent < TB_X86_FEATURE__MAX);
    return (out->set[ent / 64] >> (ent % 64)) & 1;
}

void TB_X86_FeatureSet__each_toggled(TB_X86_FeatureSet const* set, void (*consumer)(TB_X86_Feature, void*), void* userptr) {
    FOR_N(i, 0, TB_X86_FEATURE__MAX) {
        if ((set->set[i / 64] >> (i % 64)) & 1) { consumer(i, userptr); }
    }
}

char const* TB_X86_Feature__as_str(TB_X86_Feature ent) {
    switch (ent) {
    case TB_X86_FEATURE_SSE2:
        return "sse2";
    case TB_X86_FEATURE_SSE3:
        return "sse3";
    case TB_X86_FEATURE_SSE41:
        return "sse41";
    case TB_X86_FEATURE_SSE42:
        return "sse42";
    case TB_X86_FEATURE_POPCNT:
        return "popcnt";
    case TB_X86_FEATURE_LZCNT:
        return "lzcnt";
    case TB_X86_FEATURE_CLMUL:
        return "clmul";
    case TB_X86_FEATURE_F16C:
        return "f16c";
    case TB_X86_FEATURE_BMI1:
        return "bmi1";
    case TB_X86_FEATURE_BMI2:
        return "bmi2";
    case TB_X86_FEATURE_AVX:
        return "avx";
    case TB_X86_FEATURE_AVX2:
        return "avx2";
    case TB_X86_FEATURE_X86_64_V1:
        return "x86_64_v1";
    case TB_X86_FEATURE_X86_64_V2:
        return "x86_64_v2";
    case TB_X86_FEATURE_X86_64_V3:
        return "x86_64_v3";
    case TB_X86_FEATURE_X86_64_V4:
        return "x86_64_v4";
    default:
        tb_unreachable();
        return 0;
    }
}

int TB_X86_Feature__from_str(TB_X86_Feature* out, char const* str, unsigned int str_len) {
    if (!str || str_len == 0) return 1;
    unsigned char hash = (tolower(str[0]) ^ tolower(str[str_len - 1])) % 8;
    switch (hash) {
        case 0:
        if (str_len == 4 && !tb_string_case_cmp(str, "sse3", 4)) {
            *out = TB_X86_FEATURE_SSE3;
            return 0;
        }
        if (str_len == 5 && !tb_string_case_cmp(str, "lzcnt", 5)) {
            *out = TB_X86_FEATURE_LZCNT;
            return 0;
        }
        if (str_len == 4 && !tb_string_case_cmp(str, "bmi2", 4)) {
            *out = TB_X86_FEATURE_BMI2;
            return 0;
        }
        break;
        case 1:
        if (str_len == 4 && !tb_string_case_cmp(str, "sse2", 4)) {
            *out = TB_X86_FEATURE_SSE2;
            return 0;
        }
        if (str_len == 5 && !tb_string_case_cmp(str, "sse42", 5)) {
            *out = TB_X86_FEATURE_SSE42;
            return 0;
        }
        if (str_len == 3 && !tb_string_case_cmp(str, "avx", 3)) {
            *out = TB_X86_FEATURE_AVX;
            return 0;
        }
        if (str_len == 9 && !tb_string_case_cmp(str, "x86_64-v1", 9)) {
            *out = TB_X86_FEATURE_X86_64_V1;
            return 0;
        }
        break;
        case 2:
        if (str_len == 5 && !tb_string_case_cmp(str, "sse41", 5)) {
            *out = TB_X86_FEATURE_SSE41;
            return 0;
        }
        if (str_len == 9 && !tb_string_case_cmp(str, "x86_64-v2", 9)) {
            *out = TB_X86_FEATURE_X86_64_V2;
            return 0;
        }
        break;
        case 3:
        if (str_len == 4 && !tb_string_case_cmp(str, "bmi1", 4)) {
            *out = TB_X86_FEATURE_BMI1;
            return 0;
        }
        if (str_len == 4 && !tb_string_case_cmp(str, "avx2", 4)) {
            *out = TB_X86_FEATURE_AVX2;
            return 0;
        }
        if (str_len == 9 && !tb_string_case_cmp(str, "x86_64-v3", 9)) {
            *out = TB_X86_FEATURE_X86_64_V3;
            return 0;
        }
        break;
        case 4:
        if (str_len == 6 && !tb_string_case_cmp(str, "popcnt", 6)) {
            *out = TB_X86_FEATURE_POPCNT;
            return 0;
        }
        if (str_len == 9 && !tb_string_case_cmp(str, "x86_64-v4", 9)) {
            *out = TB_X86_FEATURE_X86_64_V4;
            return 0;
        }
        break;
        case 5:
        if (str_len == 4 && !tb_string_case_cmp(str, "f16c", 4)) {
            *out = TB_X86_FEATURE_F16C;
            return 0;
        }
        break;
        case 6:
        break;
        case 7:
        if (str_len == 5 && !tb_string_case_cmp(str, "clmul", 5)) {
            *out = TB_X86_FEATURE_CLMUL;
            return 0;
        }
        break;
        default:
            tb_unreachable();
            return 1;
    }
    return 1;
}

int TB_X86_FeatureSet__parse(TB_X86_FeatureSet* out, char const* str) {
    memset(out, 0, sizeof(TB_X86_FeatureSet));
    if (!str) return 0;
    for (; str && *str; ) {
        char const* next = strchr(str, ',');
        unsigned int len = next ? next - str : strlen(str);
        TB_X86_Feature x;
        if (TB_X86_Feature__from_str(&x, str, len)) return 1;
        TB_X86_FeatureSet__set(out, x);
        if (next) {str = next + 1;} else {break;}
    }
    return 0;
}

typedef enum GPR {
    RAX,
    RCX,
    RDX,
    RBX,
    RSP,
    RBP,
    RSI,
    RDI,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
    GPR_NONE = -1,
} GPR;

typedef enum XMM {
    XMM0,
    XMM1,
    XMM2,
    XMM3,
    XMM4,
    XMM5,
    XMM6,
    XMM7,
    XMM8,
    XMM9,
    XMM10,
    XMM11,
    XMM12,
    XMM13,
    XMM14,
    XMM15,
    XMM_NONE = -1,
} XMM;

enum {
    REG_CLASS_GPR = 1,
    REG_CLASS_XMM = 2,
    REG_CLASS_FLAGS = 3,
    REG_CLASS_COUNT,
};

enum {
    BUNDLE_INST_MAX = 1,
};

#include "../codegen_impl.h"

static void mach_dsl_init(Ctx* restrict ctx, TB_ABI abi) {
    ctx->num_regs[REG_CLASS_GPR] = 16;
    ctx->num_regs[REG_CLASS_XMM] = 16;
    ctx->num_regs[REG_CLASS_FLAGS] = 1;
}

static TB_Node* mach_dfa_accept_1(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $base = n->inputs[1];
    TB_Node* $index = n->inputs[2];

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_MEMORY, TB_TYPE_VOID, 2, sizeof(X86MemOp));
    set_input(f, k0, $base, k0_i++);
    set_input(f, k0, $index, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->flags = OP_INDEXED, k0_extra->scale = 0;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_2(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $base = n->inputs[1];
    TB_Node* $imm = n->inputs[2];

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_MEMORY, TB_TYPE_VOID, 1, sizeof(X86MemOp));
    set_input(f, k0, $base, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->disp = as_int32($imm);
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_3(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $base = n->inputs[1];
    TB_Node* $index = n->inputs[2]->inputs[1];
    TB_Node* $imm = n->inputs[2]->inputs[2];

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_MEMORY, TB_TYPE_VOID, 2, sizeof(X86MemOp));
    set_input(f, k0, $base, k0_i++);
    set_input(f, k0, $index, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->disp = as_int32($imm), k0_extra->flags = OP_INDEXED;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_4(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $base = n->inputs[1];
    TB_Node* $index = n->inputs[2]->inputs[1]->inputs[1];
    TB_Node* $scale = n->inputs[2]->inputs[1]->inputs[2];
    TB_Node* $disp = n->inputs[2]->inputs[2];

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_MEMORY, TB_TYPE_VOID, 2, sizeof(X86MemOp));
    set_input(f, k0, $base, k0_i++);
    set_input(f, k0, $index, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->disp = as_int32($disp), k0_extra->flags = OP_INDEXED, k0_extra->scale = as_int32($scale);
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_5(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $base = n->inputs[1];
    TB_Node* $index = n->inputs[2]->inputs[1];
    TB_Node* $scale = n->inputs[2]->inputs[2];

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_MEMORY, TB_TYPE_VOID, 2, sizeof(X86MemOp));
    set_input(f, k0, $base, k0_i++);
    set_input(f, k0, $index, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->flags = OP_INDEXED, k0_extra->scale = as_int32($scale);
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_6(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $root = n->inputs[0];
    int $disp = TB_NODE_GET_EXTRA_T(n, TB_NodeLocal)->stack_pos;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_MEMORY, TB_TYPE_VOID, 1, sizeof(X86MemOp));
    set_input(f, k0, ctx->frame_ptr, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->disp = $disp;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_7(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $root = n->inputs[0];
    TB_Symbol* $sym = TB_NODE_GET_EXTRA_T(n, TB_NodeSymbol)->sym;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_MACH_SYMBOL, TB_TYPE_VOID, 1, sizeof(TB_NodeMachSymbol));
    set_input(f, k0, $root, k0_i++);
    TB_NodeMachSymbol* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->sym = $sym;
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_MEMORY, TB_TYPE_VOID, 1, sizeof(X86MemOp));
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    switch (hash) {
    }
    k1 = tb_opt_gvn_node(f, k1);
    
    return k1;
}
static TB_Node* mach_dfa_accept_8(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $root = n->inputs[0];

    int k0_type = 0;
    TB_DataType k0_dt = TB_TYPE_VOID;
    switch (hash) {
        case 2: k0_type = TB_x86_vzero, k0_dt = TB_TYPE_F32; break;
        case 3: k0_type = TB_x86_vzero, k0_dt = TB_TYPE_F64; break;
        default: abort(); return NULL;
    }
    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, k0_type, k0_dt, 1, sizeof(X86MemOp));
    set_input(f, k0, $root, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    switch (hash) {
    }
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_9(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $root = n->inputs[0];

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_MACH_SYMBOL, TB_TYPE_VOID, 1, sizeof(TB_NodeMachSymbol));
    set_input(f, k0, $root, k0_i++);
    TB_NodeMachSymbol* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->sym = gimme_float_sym(ctx->module, n);
    k0 = tb_opt_gvn_node(f, k0);
    
    int k1_type = 0;
    TB_DataType k1_dt = TB_TYPE_VOID;
    switch (hash) {
        case 2: k1_type = TB_x86_vmov, k1_dt = TB_TYPE_F32; break;
        case 3: k1_type = TB_x86_vmov, k1_dt = TB_TYPE_F64; break;
        default: abort(); return NULL;
    }
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, k1_type, k1_dt, 3, sizeof(X86MemOp));
    k1_i++;
    k1_i++;
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    k1_extra->mode = MODE_LD;
    k1 = tb_opt_gvn_node(f, k1);
    
    return k1;
}

static TB_Node* mach_dfa_accept_10(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $root = n->inputs[1]->inputs[0];
    TB_Node* $imm = n->inputs[2];
    int $disp = TB_NODE_GET_EXTRA_T(n->inputs[1], TB_NodeLocal)->stack_pos;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_MEMORY, TB_TYPE_VOID, 1, sizeof(X86MemOp));
    set_input(f, k0, ctx->frame_ptr, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->disp = $disp + as_int32($imm);
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_11(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $root = n->inputs[1]->inputs[0];
    TB_Node* $offset = n->inputs[2];
    TB_Symbol* $sym = TB_NODE_GET_EXTRA_T(n->inputs[1], TB_NodeSymbol)->sym;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_MACH_SYMBOL, TB_TYPE_VOID, 1, sizeof(TB_NodeMachSymbol));
    set_input(f, k0, $root, k0_i++);
    TB_NodeMachSymbol* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->sym = $sym;
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_lea, TB_TYPE_PTR, 3, sizeof(X86MemOp));
    k1_i++;
    k1_i++;
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    k1_extra->mode = MODE_LD;
    k1 = tb_opt_gvn_node(f, k1);
    
    size_t k2_i = 0;
    TB_Node* k2 = tb_alloc_node(f, TB_x86_MEMORY, TB_TYPE_VOID, 2, sizeof(X86MemOp));
    set_input(f, k2, k1, k2_i++);
    set_input(f, k2, $offset, k2_i++);
    X86MemOp* k2_extra = TB_NODE_GET_EXTRA(k2);
    k2_extra->flags = OP_INDEXED;
    k2 = tb_opt_gvn_node(f, k2);
    
    return k2;
}

static TB_Node* mach_dfa_accept_12(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $root = n->inputs[1]->inputs[0];
    TB_Node* $disp = n->inputs[2];
    TB_Symbol* $sym = TB_NODE_GET_EXTRA_T(n->inputs[1], TB_NodeSymbol)->sym;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_MACH_SYMBOL, TB_TYPE_VOID, 1, sizeof(TB_NodeMachSymbol));
    set_input(f, k0, $root, k0_i++);
    TB_NodeMachSymbol* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->sym = $sym;
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_MEMORY, TB_TYPE_VOID, 1, sizeof(X86MemOp));
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    k1_extra->disp = as_int32($disp);
    k1 = tb_opt_gvn_node(f, k1);
    
    return k1;
}

static TB_Node* mach_dfa_accept_13(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $addend = n->inputs[2];
    uint64_t $disp = TB_NODE_GET_EXTRA_T(n->inputs[1], X86MemOp)->disp;
    uint64_t $flags = TB_NODE_GET_EXTRA_T(n->inputs[1], X86MemOp)->flags;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_MEMORY, TB_TYPE_VOID, 1, sizeof(X86MemOp));
    set_input(f, k0, ctx->frame_ptr, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->disp = $disp + as_int32($addend), k0_extra->flags = $flags;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_14(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_cmp, TB_TYPE_I64, 4, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $rhs, k0_i++);
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->extra_dt = $dt;
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_COND, TB_TYPE_I8, 1, sizeof(X86MemOp));
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    switch (hash) {
        case 93: k1_extra->cond = LE; break;
        case 92: k1_extra->cond = L; break;
        case 91: k1_extra->cond = BE; break;
        case 90: k1_extra->cond = B; break;
    }
    k1 = tb_opt_gvn_node(f, k1);
    
    return k1;
}
static TB_Node* mach_dfa_accept_15(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_cmp, TB_TYPE_I64, 4, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $rhs, k0_i++);
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->extra_dt = $dt;
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_COND, TB_TYPE_I8, 1, sizeof(X86MemOp));
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    switch (hash) {
        case 88: k1_extra->cond = E; break;
        case 89: k1_extra->cond = NE; break;
    }
    k1 = tb_opt_gvn_node(f, k1);
    
    return k1;
}
static TB_Node* mach_dfa_accept_16(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_ucomi, TB_TYPE_I64, 4, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $rhs, k0_i++);
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->extra_dt = $dt;
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_setcc, TB_TYPE_I8, 3, sizeof(X86MemOp));
    k1_i++;
    k1_i++;
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    switch (hash) {
        case 88: k1_extra->cond = E; break;
        case 89: k1_extra->cond = NE; break;
    }
    k1 = tb_opt_gvn_node(f, k1);
    
    size_t k2_i = 0;
    TB_Node* k2 = tb_alloc_node(f, TB_x86_ucomi, TB_TYPE_I64, 4, sizeof(X86MemOp));
    k2_i++;
    k2_i++;
    set_input(f, k2, $rhs, k2_i++);
    set_input(f, k2, $lhs, k2_i++);
    X86MemOp* k2_extra = TB_NODE_GET_EXTRA(k2);
    k2_extra->extra_dt = $dt;
    k2 = tb_opt_gvn_node(f, k2);
    
    size_t k3_i = 0;
    TB_Node* k3 = tb_alloc_node(f, TB_x86_setcc, TB_TYPE_I8, 3, sizeof(X86MemOp));
    k3_i++;
    k3_i++;
    set_input(f, k3, k2, k3_i++);
    X86MemOp* k3_extra = TB_NODE_GET_EXTRA(k3);
    switch (hash) {
        case 88: k3_extra->cond = NP; break;
        case 89: k3_extra->cond = P; break;
    }
    k3 = tb_opt_gvn_node(f, k3);
    
    int k4_type = 0;
    TB_DataType k4_dt = TB_TYPE_VOID;
    switch (hash) {
        case 88: k4_type = TB_x86_and, k4_dt = TB_TYPE_I8; break;
        case 89: k4_type = TB_x86_or, k4_dt = TB_TYPE_I8; break;
        default: abort(); return NULL;
    }
    size_t k4_i = 0;
    TB_Node* k4 = tb_alloc_node(f, k4_type, k4_dt, 4, sizeof(X86MemOp));
    k4_i++;
    k4_i++;
    set_input(f, k4, k1, k4_i++);
    set_input(f, k4, k3, k4_i++);
    X86MemOp* k4_extra = TB_NODE_GET_EXTRA(k4);
    switch (hash) {
    }
    k4 = tb_opt_gvn_node(f, k4);
    
    return k4;
}
static TB_Node* mach_dfa_accept_17(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_ucomi, TB_TYPE_I64, 4, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $rhs, k0_i++);
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->extra_dt = $dt;
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_setcc, TB_TYPE_I8, 3, sizeof(X86MemOp));
    k1_i++;
    k1_i++;
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    switch (hash) {
        case 94: k1_extra->cond = B; break;
        case 95: k1_extra->cond = BE; break;
    }
    k1 = tb_opt_gvn_node(f, k1);
    
    size_t k2_i = 0;
    TB_Node* k2 = tb_alloc_node(f, TB_x86_ucomi, TB_TYPE_I64, 4, sizeof(X86MemOp));
    k2_i++;
    k2_i++;
    set_input(f, k2, $rhs, k2_i++);
    set_input(f, k2, $lhs, k2_i++);
    X86MemOp* k2_extra = TB_NODE_GET_EXTRA(k2);
    k2_extra->extra_dt = $dt;
    k2 = tb_opt_gvn_node(f, k2);
    
    size_t k3_i = 0;
    TB_Node* k3 = tb_alloc_node(f, TB_x86_setcc, TB_TYPE_I8, 3, sizeof(X86MemOp));
    k3_i++;
    k3_i++;
    set_input(f, k3, k2, k3_i++);
    X86MemOp* k3_extra = TB_NODE_GET_EXTRA(k3);
    k3_extra->cond = NP;
    k3 = tb_opt_gvn_node(f, k3);
    
    size_t k4_i = 0;
    TB_Node* k4 = tb_alloc_node(f, TB_x86_and, TB_TYPE_I8, 4, sizeof(X86MemOp));
    k4_i++;
    k4_i++;
    set_input(f, k4, k1, k4_i++);
    set_input(f, k4, k3, k4_i++);
    X86MemOp* k4_extra = TB_NODE_GET_EXTRA(k4);
    switch (hash) {
    }
    k4 = tb_opt_gvn_node(f, k4);
    
    return k4;
}
static TB_Node* mach_dfa_accept_18(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = n->dt;

    int k0_type = 0;
    TB_DataType k0_dt = TB_TYPE_VOID;
    switch (hash) {
        case 73: k0_type = TB_x86_shlx, k0_dt = $dt; break;
        case 74: k0_type = TB_x86_shrx, k0_dt = $dt; break;
        case 75: k0_type = TB_x86_sarx, k0_dt = $dt; break;
        default: abort(); return NULL;
    }
    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, k0_type, k0_dt, 4, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $lhs, k0_i++);
    set_input(f, k0, $rhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    switch (hash) {
    }
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_19(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = n->dt;

    int k0_type = 0;
    TB_DataType k0_dt = TB_TYPE_VOID;
    switch (hash) {
        case 73: k0_type = TB_x86_shl, k0_dt = $dt; break;
        case 74: k0_type = TB_x86_shr, k0_dt = $dt; break;
        case 75: k0_type = TB_x86_sar, k0_dt = $dt; break;
        default: abort(); return NULL;
    }
    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, k0_type, k0_dt, 4, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $rhs, k0_i++);
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    switch (hash) {
    }
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_20(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = n->dt;

    int k0_type = 0;
    TB_DataType k0_dt = TB_TYPE_VOID;
    switch (hash) {
        case 70: k0_type = TB_x86_add, k0_dt = $dt; break;
        case 68: k0_type = TB_x86_or, k0_dt = $dt; break;
        case 67: k0_type = TB_x86_and, k0_dt = $dt; break;
        case 71: k0_type = TB_x86_sub, k0_dt = $dt; break;
        case 69: k0_type = TB_x86_xor, k0_dt = $dt; break;
        default: abort(); return NULL;
    }
    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, k0_type, k0_dt, 4, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $rhs, k0_i++);
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    switch (hash) {
    }
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_21(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = n->dt;

    int k0_type = 0;
    TB_DataType k0_dt = TB_TYPE_VOID;
    switch (hash) {
        case 72: k0_type = TB_x86_imul, k0_dt = $dt; break;
        case 76: k0_type = TB_x86_rol, k0_dt = $dt; break;
        case 77: k0_type = TB_x86_ror, k0_dt = $dt; break;
        case 82: k0_type = TB_x86_vadd, k0_dt = $dt; break;
        case 83: k0_type = TB_x86_vsub, k0_dt = $dt; break;
        case 84: k0_type = TB_x86_vmul, k0_dt = $dt; break;
        case 85: k0_type = TB_x86_vdiv, k0_dt = $dt; break;
        case 87: k0_type = TB_x86_vmax, k0_dt = $dt; break;
        case 86: k0_type = TB_x86_vmin, k0_dt = $dt; break;
        default: abort(); return NULL;
    }
    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, k0_type, k0_dt, 4, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $rhs, k0_i++);
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    switch (hash) {
    }
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_22(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = n->dt;

    int k0_type = 0;
    TB_DataType k0_dt = TB_TYPE_VOID;
    switch (hash) {
        case 78: k0_type = TB_x86_div, k0_dt = TB_TYPE_TUPLE; break;
        case 79: k0_type = TB_x86_idiv, k0_dt = TB_TYPE_TUPLE; break;
        case 80: k0_type = TB_x86_div, k0_dt = TB_TYPE_TUPLE; break;
        case 81: k0_type = TB_x86_idiv, k0_dt = TB_TYPE_TUPLE; break;
        default: abort(); return NULL;
    }
    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, k0_type, k0_dt, 4, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $rhs, k0_i++);
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->extra_dt = $dt;
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_PROJ, $dt, 1, sizeof(TB_NodeProj));
    set_input(f, k1, k0, k1_i++);
    TB_NodeProj* k1_extra = TB_NODE_GET_EXTRA(k1);
    switch (hash) {
        case 78: case 79: k1_extra->index = 0; break;
        case 80: case 81: k1_extra->index = 1; break;
    }
    k1 = tb_opt_gvn_node(f, k1);
    
    return k1;
}

static TB_Node* mach_dfa_accept_23(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_cmp, TB_TYPE_I64, 3, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->extra_dt = $dt, k0_extra->flags = OP_IMMEDIATE, k0_extra->imm = as_int32($rhs);
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_COND, TB_TYPE_I8, 1, sizeof(X86MemOp));
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    switch (hash) {
        case 93: k1_extra->cond = LE; break;
        case 92: k1_extra->cond = L; break;
        case 91: k1_extra->cond = BE; break;
        case 90: k1_extra->cond = B; break;
    }
    k1 = tb_opt_gvn_node(f, k1);
    
    return k1;
}
static TB_Node* mach_dfa_accept_24(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_cmp, TB_TYPE_I64, 3, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->extra_dt = $dt, k0_extra->flags = OP_IMMEDIATE, k0_extra->imm = as_int32($rhs);
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_COND, TB_TYPE_I8, 1, sizeof(X86MemOp));
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    switch (hash) {
        case 88: k1_extra->cond = E; break;
        case 89: k1_extra->cond = NE; break;
    }
    k1 = tb_opt_gvn_node(f, k1);
    
    return k1;
}
static TB_Node* mach_dfa_accept_25(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_test, TB_TYPE_I64, 4, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $lhs, k0_i++);
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->extra_dt = $dt;
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_COND, TB_TYPE_I8, 1, sizeof(X86MemOp));
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    switch (hash) {
        case 88: k1_extra->cond = E; break;
        case 89: k1_extra->cond = NE; break;
    }
    k1 = tb_opt_gvn_node(f, k1);
    
    return k1;
}
static TB_Node* mach_dfa_accept_26(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = n->dt;

    int k0_type = 0;
    TB_DataType k0_dt = TB_TYPE_VOID;
    switch (hash) {
        case 70: k0_type = TB_x86_add, k0_dt = $dt; break;
        case 68: k0_type = TB_x86_or, k0_dt = $dt; break;
        case 67: k0_type = TB_x86_and, k0_dt = $dt; break;
        case 71: k0_type = TB_x86_sub, k0_dt = $dt; break;
        case 69: k0_type = TB_x86_xor, k0_dt = $dt; break;
        default: abort(); return NULL;
    }
    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, k0_type, k0_dt, 3, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->flags = OP_IMMEDIATE, k0_extra->imm = as_int32($rhs);
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_27(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = n->dt;

    int k0_type = 0;
    TB_DataType k0_dt = TB_TYPE_VOID;
    switch (hash) {
        case 70: k0_type = TB_x86_add, k0_dt = $dt; break;
        case 68: k0_type = TB_x86_or, k0_dt = $dt; break;
        case 67: k0_type = TB_x86_and, k0_dt = $dt; break;
        case 71: k0_type = TB_x86_sub, k0_dt = $dt; break;
        case 69: k0_type = TB_x86_xor, k0_dt = $dt; break;
        default: abort(); return NULL;
    }
    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, k0_type, k0_dt, 4, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $rhs, k0_i++);
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    switch (hash) {
    }
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_28(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_imul, $dt, 3, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->flags = OP_IMMEDIATE, k0_extra->imm = as_int32($rhs);
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_29(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_imul, $dt, 4, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $rhs, k0_i++);
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    switch (hash) {
    }
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_30(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = n->dt;

    int k0_type = 0;
    TB_DataType k0_dt = TB_TYPE_VOID;
    switch (hash) {
        case 73: k0_type = TB_x86_shl, k0_dt = $dt; break;
        case 74: k0_type = TB_x86_shr, k0_dt = $dt; break;
        case 75: k0_type = TB_x86_sar, k0_dt = $dt; break;
        case 76: k0_type = TB_x86_rol, k0_dt = $dt; break;
        case 77: k0_type = TB_x86_ror, k0_dt = $dt; break;
        default: abort(); return NULL;
    }
    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, k0_type, k0_dt, 3, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->flags = OP_IMMEDIATE, k0_extra->imm = as_int32($rhs);
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_31(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;


    return isel_if_branch(ctx, f, n);
}
static TB_Node* mach_dfa_accept_32(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;


    return isel_va_start(ctx, f, n);
}

static TB_Node* mach_dfa_accept_33(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $cmp = n->inputs[1]->inputs[0];
    uint64_t $cond = TB_NODE_GET_EXTRA_T(n->inputs[1], X86MemOp)->cond;
    TB_DataType $dt = n->dt;
    float $prob = TB_NODE_GET_EXTRA_T(n, TB_NodeIf)->prob;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_jcc, TB_TYPE_TUPLE, 2, sizeof(X86MemOp));
    set_input(f, k0, $ctrl, k0_i++);
    set_input(f, k0, $cmp, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->cond = $cond, k0_extra->prob = $prob;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_34(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $x = n->inputs[1];
    TB_DataType $dt = n->dt;
    float $prob = TB_NODE_GET_EXTRA_T(n, TB_NodeIf)->prob;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_test, TB_TYPE_I64, 4, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $x, k0_i++);
    set_input(f, k0, $x, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->extra_dt = n->inputs[1]->dt;
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_jcc, TB_TYPE_TUPLE, 2, sizeof(X86MemOp));
    set_input(f, k1, $ctrl, k1_i++);
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    k1_extra->cond = NE, k1_extra->prob = $prob;
    k1 = tb_opt_gvn_node(f, k1);
    
    return k1;
}
static TB_Node* mach_dfa_accept_35(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $x = n->inputs[1];
    TB_DataType $dt = n->dt;

    return isel_multi_way_branch(ctx, f, n);
}

static TB_Node* mach_dfa_accept_36(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $ctrl2 = n->inputs[1]->inputs[0];
    TB_Node* $mem = n->inputs[1]->inputs[1];
    TB_Node* $x = n->inputs[1]->inputs[2];
    uint64_t $flags = TB_NODE_GET_EXTRA_T(n->inputs[1]->inputs[2], X86MemOp)->flags;
    TB_DataType $dt = n->dt;
    float $prob = TB_NODE_GET_EXTRA_T(n, TB_NodeIf)->prob;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_cmp, TB_TYPE_I64, 4, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    if ($x->type != TB_x86_MEMORY) {
        set_input(f, k0, $x, k0_i++);
    } else {
        FOR_N(i, 0, $x->input_count) { set_input(f, k0, $x->inputs[i], k0_i++); }
    }
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    if ($x->type == TB_x86_MEMORY) {
        memcpy(k0_extra, $x->extra, sizeof(X86MemOp));
    }
    k0_extra->extra_dt = n->inputs[1]->dt, k0_extra->flags = $flags | OP_IMMEDIATE, k0_extra->mode = MODE_LD;
    k0->input_count = k0_i;
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_jcc, TB_TYPE_TUPLE, 2, sizeof(X86MemOp));
    set_input(f, k1, $ctrl, k1_i++);
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    k1_extra->cond = NE, k1_extra->prob = $prob;
    k1 = tb_opt_gvn_node(f, k1);
    
    return k1;
}

static TB_Node* mach_dfa_accept_37(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $x = n->inputs[1]->inputs[1];
    TB_Node* $y = n->inputs[1]->inputs[2];
    TB_DataType $dt = n->dt;
    float $prob = TB_NODE_GET_EXTRA_T(n, TB_NodeIf)->prob;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_test, TB_TYPE_I64, 5, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    if ($x->type != TB_x86_MEMORY) {
        set_input(f, k0, $x, k0_i++);
    } else {
        FOR_N(i, 0, $x->input_count) { set_input(f, k0, $x->inputs[i], k0_i++); }
    }
    set_input(f, k0, $y, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    if ($x->type == TB_x86_MEMORY) {
        memcpy(k0_extra, $x->extra, sizeof(X86MemOp));
    }
    k0_extra->extra_dt = n->inputs[1]->dt;
    k0->input_count = k0_i;
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_jcc, TB_TYPE_TUPLE, 2, sizeof(X86MemOp));
    set_input(f, k1, $ctrl, k1_i++);
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    k1_extra->cond = NE, k1_extra->prob = $prob;
    k1 = tb_opt_gvn_node(f, k1);
    
    return k1;
}

static TB_Node* mach_dfa_accept_38(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $cmp = n->inputs[0];
    uint64_t $cond = TB_NODE_GET_EXTRA_T(n, X86MemOp)->cond;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_setcc, TB_TYPE_I8, 3, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $cmp, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->cond = $cond;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_39(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $pred = n->inputs[1];
    TB_Node* $a = n->inputs[2];
    TB_Node* $b = n->inputs[3];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_test, TB_TYPE_I64, 4, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $pred, k0_i++);
    set_input(f, k0, $pred, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->extra_dt = n->inputs[1]->dt;
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_cmovcc, $dt, 5, sizeof(X86MemOp));
    k1_i++;
    k1_i++;
    set_input(f, k1, $b, k1_i++);
    set_input(f, k1, $a, k1_i++);
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    k1_extra->cond = NE;
    k1 = tb_opt_gvn_node(f, k1);
    
    return k1;
}

static TB_Node* mach_dfa_accept_40(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $cmp = n->inputs[1]->inputs[0];
    TB_Node* $a = n->inputs[2];
    TB_Node* $b = n->inputs[3];
    uint64_t $cond = TB_NODE_GET_EXTRA_T(n->inputs[1], X86MemOp)->cond;
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_cmovcc, $dt, 5, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $b, k0_i++);
    set_input(f, k0, $a, k0_i++);
    set_input(f, k0, $cmp, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->cond = $cond;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_41(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $con = n->inputs[1]->inputs[1];
    TB_Node* $y = n->inputs[1]->inputs[2];
    TB_Node* $x = n->inputs[2];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_bt, TB_TYPE_I64, 5, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $y, k0_i++);
    if ($x->type != TB_x86_MEMORY) {
        set_input(f, k0, $x, k0_i++);
    } else {
        FOR_N(i, 0, $x->input_count) { set_input(f, k0, $x->inputs[i], k0_i++); }
    }
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    if ($x->type == TB_x86_MEMORY) {
        memcpy(k0_extra, $x->extra, sizeof(X86MemOp));
    }
    k0_extra->extra_dt = $dt;
    k0->input_count = k0_i;
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_COND, TB_TYPE_I8, 1, sizeof(X86MemOp));
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    k1_extra->cond = B;
    k1 = tb_opt_gvn_node(f, k1);
    
    return k1;
}

static TB_Node* mach_dfa_accept_42(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $x = n->inputs[2];
    TB_Node* $y = n->inputs[3]->inputs[1];
    TB_Node* $con = n->inputs[3]->inputs[2];
    TB_Node* $pred = n->inputs[4];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_adc, $dt, 5, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    if ($x->type != TB_x86_MEMORY) {
        set_input(f, k0, $x, k0_i++);
    } else {
        FOR_N(i, 0, $x->input_count) { set_input(f, k0, $x->inputs[i], k0_i++); }
    }
    set_input(f, k0, $pred, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    if ($x->type == TB_x86_MEMORY) {
        memcpy(k0_extra, $x->extra, sizeof(X86MemOp));
    }
    k0_extra->flags = OP_IMMEDIATE, k0_extra->imm = 0;
    k0->input_count = k0_i;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_43(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $root = n->inputs[2]->inputs[0];
    size_t $REST_LEN = n->input_count - 3;
    TB_Node** $REST  = &n->inputs[3];
    TB_Symbol* $sym = TB_NODE_GET_EXTRA_T(n->inputs[2], TB_NodeSymbol)->sym;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_MACH_SYMBOL, TB_TYPE_VOID, 1, sizeof(TB_NodeMachSymbol));
    set_input(f, k0, $root, k0_i++);
    TB_NodeMachSymbol* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->sym = $sym;
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_call, TB_TYPE_TUPLE, 3 + $REST_LEN, sizeof(X86Call));
    set_input(f, k1, $ctrl, k1_i++);
    set_input(f, k1, $mem, k1_i++);
    set_input(f, k1, k0, k1_i++);
    FOR_N(i, 0, $REST_LEN) { set_input(f, k1, $REST[i], k1_i++); }
    X86Call* k1_extra = TB_NODE_GET_EXTRA(k1);
    k1_extra->proto = TB_NODE_GET_EXTRA_T(n, TB_NodeCall)->proto;
    k1 = tb_opt_gvn_node(f, k1);
    
    return k1;
}

static TB_Node* mach_dfa_accept_44(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $target = n->inputs[2];
    size_t $REST_LEN = n->input_count - 3;
    TB_Node** $REST  = &n->inputs[3];

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_call, TB_TYPE_TUPLE, 3 + $REST_LEN, sizeof(X86Call));
    set_input(f, k0, $ctrl, k0_i++);
    set_input(f, k0, $mem, k0_i++);
    set_input(f, k0, $target, k0_i++);
    FOR_N(i, 0, $REST_LEN) { set_input(f, k0, $REST[i], k0_i++); }
    X86Call* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->proto = TB_NODE_GET_EXTRA_T(n, TB_NodeCall)->proto;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_45(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $ctrl = n->inputs[1]->inputs[0];
    TB_Node* $mem = n->inputs[1]->inputs[1];
    TB_Node* $lhs = n->inputs[1]->inputs[2];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = n->dt;

    int k0_type = 0;
    TB_DataType k0_dt = TB_TYPE_VOID;
    switch (hash) {
        case 73: k0_type = TB_x86_shlx, k0_dt = $dt; break;
        case 74: k0_type = TB_x86_shrx, k0_dt = $dt; break;
        case 75: k0_type = TB_x86_sarx, k0_dt = $dt; break;
        default: abort(); return NULL;
    }
    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, k0_type, k0_dt, 5, sizeof(X86MemOp));
    set_input(f, k0, $ctrl, k0_i++);
    set_input(f, k0, $mem, k0_i++);
    if ($lhs->type != TB_x86_MEMORY) {
        set_input(f, k0, $lhs, k0_i++);
    } else {
        FOR_N(i, 0, $lhs->input_count) { set_input(f, k0, $lhs->inputs[i], k0_i++); }
    }
    set_input(f, k0, $rhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    if ($lhs->type == TB_x86_MEMORY) {
        memcpy(k0_extra, $lhs->extra, sizeof(X86MemOp));
    }
    k0_extra->mode = MODE_LD;
    k0->input_count = k0_i;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_46(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $lhs = n;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_lea, TB_TYPE_PTR, 4, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    if ($lhs->type != TB_x86_MEMORY) {
        set_input(f, k0, $lhs, k0_i++);
    } else {
        FOR_N(i, 0, $lhs->input_count) { set_input(f, k0, $lhs->inputs[i], k0_i++); }
    }
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    if ($lhs->type == TB_x86_MEMORY) {
        memcpy(k0_extra, $lhs->extra, sizeof(X86MemOp));
    }
    k0_extra->mode = MODE_LD;
    k0->input_count = k0_i;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_47(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $addr = n->inputs[2];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_mov, $dt, 3, sizeof(X86MemOp));
    set_input(f, k0, $ctrl, k0_i++);
    set_input(f, k0, $mem, k0_i++);
    set_input(f, k0, $addr, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->mode = MODE_LD;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_48(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $addr = n->inputs[2];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_MACH_SYMBOL, TB_TYPE_VOID, 1, sizeof(TB_NodeMachSymbol));
    set_input(f, k0, f->root_node, k0_i++);
    TB_NodeMachSymbol* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->sym = ctx->module->ccgc.phase_control;
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_mov, TB_TYPE_PTR, 3, sizeof(X86MemOp));
    k1_i++;
    k1_i++;
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    k1_extra->mode = MODE_LD;
    k1 = tb_opt_gvn_node(f, k1);
    
    size_t k2_i = 0;
    TB_Node* k2 = tb_alloc_node(f, TB_x86_mov, $dt, 4, sizeof(X86MemOp));
    set_input(f, k2, $ctrl, k2_i++);
    set_input(f, k2, $mem, k2_i++);
    set_input(f, k2, $addr, k2_i++);
    set_input(f, k2, k1, k2_i++);
    X86MemOp* k2_extra = TB_NODE_GET_EXTRA(k2);
    k2_extra->mode = MODE_LD;
    k2 = tb_opt_gvn_node(f, k2);
    
    return k2;
}
static TB_Node* mach_dfa_accept_49(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $addr = n->inputs[2];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_vmov, $dt, 3, sizeof(X86MemOp));
    set_input(f, k0, $ctrl, k0_i++);
    set_input(f, k0, $mem, k0_i++);
    set_input(f, k0, $addr, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->mode = MODE_LD;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_50(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $addr = n->inputs[2];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_mov, $dt, 4, sizeof(X86MemOp));
    set_input(f, k0, $ctrl, k0_i++);
    set_input(f, k0, $mem, k0_i++);
    if ($addr->type != TB_x86_MEMORY) {
        set_input(f, k0, $addr, k0_i++);
    } else {
        FOR_N(i, 0, $addr->input_count) { set_input(f, k0, $addr->inputs[i], k0_i++); }
    }
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    if ($addr->type == TB_x86_MEMORY) {
        memcpy(k0_extra, $addr->extra, sizeof(X86MemOp));
    }
    k0_extra->mode = MODE_LD;
    k0->input_count = k0_i;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_51(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $addr = n->inputs[2];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_MACH_SYMBOL, TB_TYPE_VOID, 1, sizeof(TB_NodeMachSymbol));
    set_input(f, k0, f->root_node, k0_i++);
    TB_NodeMachSymbol* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->sym = ctx->module->ccgc.phase_control;
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_mov, TB_TYPE_PTR, 3, sizeof(X86MemOp));
    k1_i++;
    k1_i++;
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    k1_extra->mode = MODE_LD;
    k1 = tb_opt_gvn_node(f, k1);
    
    size_t k2_i = 0;
    TB_Node* k2 = tb_alloc_node(f, TB_x86_mov, $dt, 5, sizeof(X86MemOp));
    set_input(f, k2, $ctrl, k2_i++);
    set_input(f, k2, $mem, k2_i++);
    if ($addr->type != TB_x86_MEMORY) {
        set_input(f, k2, $addr, k2_i++);
    } else {
        FOR_N(i, 0, $addr->input_count) { set_input(f, k2, $addr->inputs[i], k2_i++); }
    }
    set_input(f, k2, k1, k2_i++);
    X86MemOp* k2_extra = TB_NODE_GET_EXTRA(k2);
    if ($addr->type == TB_x86_MEMORY) {
        memcpy(k2_extra, $addr->extra, sizeof(X86MemOp));
    }
    k2_extra->mode = MODE_LD;
    k2->input_count = k2_i;
    k2 = tb_opt_gvn_node(f, k2);
    
    return k2;
}
static TB_Node* mach_dfa_accept_52(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $addr = n->inputs[2];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_vmov, $dt, 4, sizeof(X86MemOp));
    set_input(f, k0, $ctrl, k0_i++);
    set_input(f, k0, $mem, k0_i++);
    if ($addr->type != TB_x86_MEMORY) {
        set_input(f, k0, $addr, k0_i++);
    } else {
        FOR_N(i, 0, $addr->input_count) { set_input(f, k0, $addr->inputs[i], k0_i++); }
    }
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    if ($addr->type == TB_x86_MEMORY) {
        memcpy(k0_extra, $addr->extra, sizeof(X86MemOp));
    }
    k0_extra->mode = MODE_LD;
    k0->input_count = k0_i;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_53(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $addr = n->inputs[2];
    TB_Node* $val = n->inputs[3];

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_mov, TB_TYPE_MEMORY, 5, sizeof(X86MemOp));
    set_input(f, k0, $ctrl, k0_i++);
    set_input(f, k0, $mem, k0_i++);
    if ($addr->type != TB_x86_MEMORY) {
        set_input(f, k0, $addr, k0_i++);
    } else {
        FOR_N(i, 0, $addr->input_count) { set_input(f, k0, $addr->inputs[i], k0_i++); }
    }
    set_input(f, k0, $val, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    if ($addr->type == TB_x86_MEMORY) {
        memcpy(k0_extra, $addr->extra, sizeof(X86MemOp));
    }
    k0_extra->extra_dt = n->inputs[3]->dt, k0_extra->mode = MODE_ST;
    k0->input_count = k0_i;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_54(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $addr = n->inputs[2];
    TB_Node* $val = n->inputs[3];

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_vmov, TB_TYPE_MEMORY, 5, sizeof(X86MemOp));
    set_input(f, k0, $ctrl, k0_i++);
    set_input(f, k0, $mem, k0_i++);
    if ($addr->type != TB_x86_MEMORY) {
        set_input(f, k0, $addr, k0_i++);
    } else {
        FOR_N(i, 0, $addr->input_count) { set_input(f, k0, $addr->inputs[i], k0_i++); }
    }
    set_input(f, k0, $val, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    if ($addr->type == TB_x86_MEMORY) {
        memcpy(k0_extra, $addr->extra, sizeof(X86MemOp));
    }
    k0_extra->extra_dt = n->inputs[3]->dt, k0_extra->mode = MODE_ST;
    k0->input_count = k0_i;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_55(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $lhs = n->inputs[2];
    TB_Node* $val = n->inputs[3];

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_mov, TB_TYPE_MEMORY, 5, sizeof(X86MemOp));
    set_input(f, k0, $ctrl, k0_i++);
    set_input(f, k0, $mem, k0_i++);
    if ($lhs->type != TB_x86_MEMORY) {
        set_input(f, k0, $lhs, k0_i++);
    } else {
        FOR_N(i, 0, $lhs->input_count) { set_input(f, k0, $lhs->inputs[i], k0_i++); }
    }
    set_input(f, k0, $val, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    if ($lhs->type == TB_x86_MEMORY) {
        memcpy(k0_extra, $lhs->extra, sizeof(X86MemOp));
    }
    k0_extra->extra_dt = n->inputs[3]->dt, k0_extra->mode = MODE_ST;
    k0->input_count = k0_i;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_56(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $lhs = n->inputs[2];
    TB_Node* $val = n->inputs[3];

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_vmov, TB_TYPE_MEMORY, 5, sizeof(X86MemOp));
    set_input(f, k0, $ctrl, k0_i++);
    set_input(f, k0, $mem, k0_i++);
    if ($lhs->type != TB_x86_MEMORY) {
        set_input(f, k0, $lhs, k0_i++);
    } else {
        FOR_N(i, 0, $lhs->input_count) { set_input(f, k0, $lhs->inputs[i], k0_i++); }
    }
    set_input(f, k0, $val, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    if ($lhs->type == TB_x86_MEMORY) {
        memcpy(k0_extra, $lhs->extra, sizeof(X86MemOp));
    }
    k0_extra->extra_dt = n->inputs[3]->dt, k0_extra->mode = MODE_ST;
    k0->input_count = k0_i;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_57(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $lhs = n->inputs[2];
    TB_Node* $val = n->inputs[3];
    uint64_t $flags = TB_NODE_GET_EXTRA_T(n->inputs[2], X86MemOp)->flags;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_mov, TB_TYPE_MEMORY, 4, sizeof(X86MemOp));
    set_input(f, k0, $ctrl, k0_i++);
    set_input(f, k0, $mem, k0_i++);
    if ($lhs->type != TB_x86_MEMORY) {
        set_input(f, k0, $lhs, k0_i++);
    } else {
        FOR_N(i, 0, $lhs->input_count) { set_input(f, k0, $lhs->inputs[i], k0_i++); }
    }
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    if ($lhs->type == TB_x86_MEMORY) {
        memcpy(k0_extra, $lhs->extra, sizeof(X86MemOp));
    }
    k0_extra->extra_dt = n->inputs[3]->dt, k0_extra->flags = $flags | OP_IMMEDIATE, k0_extra->imm = as_int32($val), k0_extra->mode = MODE_ST;
    k0->input_count = k0_i;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_58(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $ctrl = n->inputs[2]->inputs[0];
    TB_Node* $mem = n->inputs[2]->inputs[1];
    TB_Node* $rhs = n->inputs[2]->inputs[2];
    TB_DataType $dt = n->dt;

    int k0_type = 0;
    TB_DataType k0_dt = TB_TYPE_VOID;
    switch (hash) {
        case 70: k0_type = TB_x86_add, k0_dt = $dt; break;
        case 68: k0_type = TB_x86_or, k0_dt = $dt; break;
        case 67: k0_type = TB_x86_and, k0_dt = $dt; break;
        case 71: k0_type = TB_x86_sub, k0_dt = $dt; break;
        case 69: k0_type = TB_x86_xor, k0_dt = $dt; break;
        default: abort(); return NULL;
    }
    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, k0_type, k0_dt, 5, sizeof(X86MemOp));
    set_input(f, k0, $ctrl, k0_i++);
    set_input(f, k0, $mem, k0_i++);
    if ($rhs->type != TB_x86_MEMORY) {
        set_input(f, k0, $rhs, k0_i++);
    } else {
        FOR_N(i, 0, $rhs->input_count) { set_input(f, k0, $rhs->inputs[i], k0_i++); }
    }
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    if ($rhs->type == TB_x86_MEMORY) {
        memcpy(k0_extra, $rhs->extra, sizeof(X86MemOp));
    }
    k0_extra->mode = MODE_LD;
    k0->input_count = k0_i;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_59(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $ctrl = n->inputs[2]->inputs[0];
    TB_Node* $mem = n->inputs[2]->inputs[1];
    TB_Node* $rhs = n->inputs[2]->inputs[2];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_imul, $dt, 5, sizeof(X86MemOp));
    set_input(f, k0, $ctrl, k0_i++);
    set_input(f, k0, $mem, k0_i++);
    if ($rhs->type != TB_x86_MEMORY) {
        set_input(f, k0, $rhs, k0_i++);
    } else {
        FOR_N(i, 0, $rhs->input_count) { set_input(f, k0, $rhs->inputs[i], k0_i++); }
    }
    set_input(f, k0, $lhs, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    if ($rhs->type == TB_x86_MEMORY) {
        memcpy(k0_extra, $rhs->extra, sizeof(X86MemOp));
    }
    k0_extra->mode = MODE_LD;
    k0->input_count = k0_i;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_60(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $x = n->inputs[1];
    TB_Node* $root = n->inputs[1]->inputs[0];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_vzero, $dt, 1, sizeof(X86MemOp));
    set_input(f, k0, $root, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    switch (hash) {
    }
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_61(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $x = n->inputs[1];
    TB_Node* $root = n->inputs[1]->inputs[0];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_MACH_SYMBOL, TB_TYPE_VOID, 1, sizeof(TB_NodeMachSymbol));
    set_input(f, k0, $root, k0_i++);
    TB_NodeMachSymbol* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->sym = gimme_float_sym(ctx->module, n);
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_vmov, $dt, 3, sizeof(X86MemOp));
    k1_i++;
    k1_i++;
    set_input(f, k1, k0, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    k1_extra->mode = MODE_LD;
    k1 = tb_opt_gvn_node(f, k1);
    
    return k1;
}

static TB_Node* mach_dfa_accept_62(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $src = n->inputs[1];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_MACH_SYMBOL, TB_TYPE_VOID, 1, sizeof(TB_NodeMachSymbol));
    set_input(f, k0, f->root_node, k0_i++);
    TB_NodeMachSymbol* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->sym = gimme_float_neg_zero(ctx->module, $dt);
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_x86_vxor, $dt, 4, sizeof(X86MemOp));
    k1_i++;
    k1_i++;
    set_input(f, k1, k0, k1_i++);
    set_input(f, k1, $src, k1_i++);
    X86MemOp* k1_extra = TB_NODE_GET_EXTRA(k1);
    k1_extra->mode = MODE_LD;
    k1 = tb_opt_gvn_node(f, k1);
    
    return k1;
}
static TB_Node* mach_dfa_accept_63(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $src = n->inputs[1];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_MACH_COPY, $dt, 2, sizeof(TB_NodeMachCopy));
    k0_i++;
    set_input(f, k0, $src, k0_i++);
    TB_NodeMachCopy* k0_extra = TB_NODE_GET_EXTRA(k0);
    switch (hash) {
        case 60: k0_extra->def = ctx->normie_mask[TB_IS_FLOAT_TYPE(n->dt) ? REG_CLASS_XMM : REG_CLASS_GPR], k0_extra->use = ctx->normie_mask[TB_IS_FLOAT_TYPE($src->dt) ? REG_CLASS_XMM : REG_CLASS_GPR]; break;
        case 51: k0_extra->def = ctx->normie_mask[REG_CLASS_GPR], k0_extra->use = ctx->normie_mask[REG_CLASS_GPR]; break;
    }
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_64(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $src = n->inputs[1];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_movsx32, TB_TYPE_I64, 3, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $src, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->extra_dt = TB_TYPE_I64;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_65(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $src = n->inputs[1];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_movsx8, $dt, 3, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $src, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    // PATCH(odin-tilde): extra_dt must be the SOURCE width (matches x64.machine):
    // legalize_int(extra_dt) drives the emitter's byte-op REX rule — with the
    // result dt (i32) here a movsx8 from sil/dil/bpl+ encoded 0F BE without REX,
    // silently reading bh/ch/dh instead (build-dependent garbage, stage2 only
    // since stage0 keeps the ext on a memory operand).
    k0_extra->extra_dt = $src->dt;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_66(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $src = n->inputs[1];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_movsx16, $dt, 3, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $src, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->extra_dt = $dt;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_67(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $src = n->inputs[1];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_mov, TB_TYPE_I64, 3, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $src, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->extra_dt = TB_TYPE_I32;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_68(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $src = n->inputs[1];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_movzx8, $dt, 3, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $src, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->extra_dt = TB_TYPE_I32;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_69(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $src = n->inputs[1];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_MACH_COPY, $dt, 2, sizeof(TB_NodeMachCopy));
    k0_i++;
    set_input(f, k0, $src, k0_i++);
    TB_NodeMachCopy* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->def = ctx->normie_mask[REG_CLASS_GPR], k0_extra->use = ctx->normie_mask[REG_CLASS_GPR];
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_70(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $src = n->inputs[1];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_movzx16, $dt, 3, sizeof(X86MemOp));
    k0_i++;
    k0_i++;
    set_input(f, k0, $src, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->extra_dt = TB_TYPE_I16;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_71(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $src = n->inputs[1];
    TB_Node* $ctrl = n->inputs[1]->inputs[0];
    TB_Node* $mem = n->inputs[1]->inputs[1];
    TB_Node* $addr = n->inputs[1]->inputs[2];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_movzx8, $dt, 3, sizeof(X86MemOp));
    set_input(f, k0, $ctrl, k0_i++);
    set_input(f, k0, $mem, k0_i++);
    set_input(f, k0, $addr, k0_i++);
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->extra_dt = TB_TYPE_I32, k0_extra->mode = MODE_LD;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_72(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $src = n->inputs[1];
    TB_Node* $ctrl = n->inputs[1]->inputs[0];
    TB_Node* $mem = n->inputs[1]->inputs[1];
    TB_Node* $addr = n->inputs[1]->inputs[2];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_x86_movzx8, $dt, 4, sizeof(X86MemOp));
    set_input(f, k0, $ctrl, k0_i++);
    set_input(f, k0, $mem, k0_i++);
    if ($addr->type != TB_x86_MEMORY) {
        set_input(f, k0, $addr, k0_i++);
    } else {
        FOR_N(i, 0, $addr->input_count) { set_input(f, k0, $addr->inputs[i], k0_i++); }
    }
    X86MemOp* k0_extra = TB_NODE_GET_EXTRA(k0);
    if ($addr->type == TB_x86_MEMORY) {
        memcpy(k0_extra, $addr->extra, sizeof(X86MemOp));
    }
    k0_extra->extra_dt = TB_TYPE_I32, k0_extra->mode = MODE_LD;
    k0->input_count = k0_i;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static MatchRuleID mach_dfa_rule_1(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $base = n->inputs[1];
    TB_Node* $index = n->inputs[2];
    uint64_t hash = 0;

    // { 0 }
    {
        return 1;
    }
}

static MatchRuleID mach_dfa_rule_2(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $base = n->inputs[1];
    TB_Node* $imm = n->inputs[2];
    uint64_t hash = 0;

    // { 0 }
    {
        if (fits_into_int32(TB_TYPE_I64, $imm)) { return 2; }
    }
    return 0;
}

static MatchRuleID mach_dfa_rule_3(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $base = n->inputs[1];
    TB_Node* $index = n->inputs[2]->inputs[1];
    TB_Node* $imm = n->inputs[2]->inputs[2];
    uint64_t hash = 0;

    // { 0 }
    {
        if (fits_into_int32(TB_TYPE_I64, $imm)) { return 3; }
    }
    return 0;
}

static MatchRuleID mach_dfa_rule_4(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $base = n->inputs[1];
    TB_Node* $index = n->inputs[2]->inputs[1]->inputs[1];
    TB_Node* $scale = n->inputs[2]->inputs[1]->inputs[2];
    TB_Node* $disp = n->inputs[2]->inputs[2];
    uint64_t hash = 0;

    // { 0, 0 }
    switch (hash) {
        default:
        return 0;
        // (MEMORY $base $index)
        case 0: {
            {
                if (fits_into_scale($scale) && fits_into_int32(TB_TYPE_I64, $disp)) { return 4; }
            }
            return 0;
        }
    }
}

static MatchRuleID mach_dfa_rule_5(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $base = n->inputs[1];
    TB_Node* $index = n->inputs[2]->inputs[1];
    TB_Node* $scale = n->inputs[2]->inputs[2];
    uint64_t hash = 0;

    // { 0 }
    {
        if (fits_into_scale($scale)) { return 5; }
    }
    return 0;
}

static MatchRuleID mach_dfa_rule_6(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $root = n->inputs[0];
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    // { 48, 49, 2, 2, 3, 3 }
    switch (hash) {
        default:
        return 0;
        // (MEMORY ctx->frame_ptr)
        case 48: {
            {
                int $disp = TB_NODE_GET_EXTRA_T(n, TB_NodeLocal)->stack_pos;
                return 6;
            }
        }
        // (MEMORY (ANY_OP $root))
        case 49: {
            {
                TB_Symbol* $sym = TB_NODE_GET_EXTRA_T(n, TB_NodeSymbol)->sym;
                return 7;
            }
        }
        // (ANY_OP $root),(ANY_OP ___ ___ (ANY_OP $root))
        case 2: case 3: {
            {
                if (is_float_zero(n)) { return 8; }
            }
            {
                return 9;
            }
        }
    }
}

static MatchRuleID mach_dfa_rule_7(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $root = n->inputs[1]->inputs[0];
    TB_Node* $imm = n->inputs[2];
    uint64_t hash = 0;

    // { 0 }
    {
        int $disp = TB_NODE_GET_EXTRA_T(n->inputs[1], TB_NodeLocal)->stack_pos;
        if (fits_into_int32(TB_TYPE_I64, $imm)) { return 10; }
    }
    return 0;
}

static MatchRuleID mach_dfa_rule_8(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $root = n->inputs[1]->inputs[0];
    TB_Node* $offset = n->inputs[2];
    uint64_t hash = 0;

    // { 0 }
    {
        TB_Symbol* $sym = TB_NODE_GET_EXTRA_T(n->inputs[1], TB_NodeSymbol)->sym;
        return 11;
    }
}

static MatchRuleID mach_dfa_rule_9(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $root = n->inputs[1]->inputs[0];
    TB_Node* $disp = n->inputs[2];
    uint64_t hash = 0;

    // { 0 }
    {
        TB_Symbol* $sym = TB_NODE_GET_EXTRA_T(n->inputs[1], TB_NodeSymbol)->sym;
        if (fits_into_int32(TB_TYPE_I64, $disp)) { return 12; }
    }
    return 0;
}

static MatchRuleID mach_dfa_rule_10(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $addend = n->inputs[2];
    uint64_t hash = 0;

    // { 0 }
    {
        uint64_t $disp = TB_NODE_GET_EXTRA_T(n->inputs[1], X86MemOp)->disp;
        uint64_t $flags = TB_NODE_GET_EXTRA_T(n->inputs[1], X86MemOp)->flags;
        return 13;
    }
}

static MatchRuleID mach_dfa_rule_11(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    // { 93, 92, 91, 90, 88, 89, 94, 95, 88, 89, 73, 74, 75, 70, 68, 67, 71, 69, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 87, 86 }
    switch (hash) {
        default:
        return 0;
        // (COND (ANY_OP ___ ___ $rhs $lhs))
        case 93: case 92: case 91: case 90: {
            {
                TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;
                if (TB_IS_BOOL_INT_PTR($dt)) { return 14; }
            }
            return 0;
        }
        // (COND (ANY_OP ___ ___ $rhs $lhs)),(IARITH ___ ___ (ANY_OP ___ ___ (ANY_OP ___ ___ $rhs $lhs)) (ANY_OP ___ ___ (ANY_OP ___ ___ $rhs $lhs)))
        case 88: case 89: {
            {
                TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;
                if (TB_IS_BOOL_INT_PTR($dt)) { return 15; }
            }
            {
                TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;
                if (TB_IS_FLOAT_TYPE($dt)) { return 16; }
            }
            return 0;
        }
        // (IARITH ___ ___ (ANY_OP ___ ___ (ANY_OP ___ ___ $rhs $lhs)) (ANY_OP ___ ___ (ANY_OP ___ ___ $rhs $lhs)))
        case 94: case 95: {
            {
                TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;
                if (TB_IS_FLOAT_TYPE($dt)) { return 17; }
            }
            return 0;
        }
        // (ANY_OP ___ ___ $lhs $rhs),(ANY_OP ___ ___ $rhs $lhs)
        case 73: case 74: case 75: {
            {
                TB_DataType $dt = n->dt;
                if (TB_X86_FeatureSet__get(&f->features.x86, TB_X86_FEATURE_BMI2)) { return 18; }
            }
            {
                TB_DataType $dt = n->dt;
                return 19;
            }
        }
        // (IARITH ___ ___ $rhs $lhs)
        case 70: case 68: case 67: case 71: case 69: {
            {
                TB_DataType $dt = n->dt;
                return 20;
            }
        }
        // (ANY_OP ___ ___ $rhs $lhs)
        case 72: case 76: case 77: case 82: case 83: case 84: case 85: case 87: case 86: {
            {
                TB_DataType $dt = n->dt;
                return 21;
            }
        }
        // (ANY_OP (ANY_OP ___ ___ $rhs $lhs))
        case 78: case 79: case 80: case 81: {
            {
                TB_DataType $dt = n->dt;
                return 22;
            }
        }
    }
}

static MatchRuleID mach_dfa_rule_12(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    // { 93, 92, 91, 90, 88, 89, 88, 89, 70, 70, 68, 68, 67, 67, 71, 71, 69, 69, 72, 72, 73, 74, 75, 76, 77 }
    switch (hash) {
        default:
        return 0;
        // (COND (ANY_OP ___ ___ $lhs))
        case 93: case 92: case 91: case 90: {
            {
                TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;
                if (TB_IS_BOOL_INT_PTR($dt) && fits_into_int32($dt, $rhs)) { return 23; }
            }
            return 0;
        }
        // (COND (ANY_OP ___ ___ $lhs)),(COND (ANY_OP ___ ___ $lhs $lhs))
        case 88: case 89: {
            {
                TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;
                if (TB_IS_BOOL_INT_PTR($dt) && fits_into_int32($dt, $rhs)) { return 24; }
            }
            {
                TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;
                if (TB_IS_INT_OR_PTR($dt) && TB_NODE_GET_EXTRA_T($rhs, TB_NodeInt)->value == 0) { return 25; }
            }
            return 0;
        }
        // (IARITH ___ ___ $lhs),(IARITH ___ ___ $rhs $lhs)
        case 70: case 68: case 67: case 71: case 69: {
            {
                TB_DataType $dt = n->dt;
                if (fits_into_int32($dt, $rhs)) { return 26; }
            }
            {
                TB_DataType $dt = n->dt;
                return 27;
            }
        }
        // (ANY_OP ___ ___ $lhs),(ANY_OP ___ ___ $rhs $lhs)
        case 72: {
            {
                TB_DataType $dt = n->dt;
                if (fits_into_int32($dt, $rhs)) { return 28; }
            }
            {
                TB_DataType $dt = n->dt;
                return 29;
            }
        }
        // (ANY_OP ___ ___ $lhs)
        case 73: case 74: case 75: case 76: case 77: {
            {
                TB_DataType $dt = n->dt;
                if (fits_into_uint8($dt, $rhs)) { return 30; }
            }
            return 0;
        }
    }
}

static MatchRuleID mach_dfa_rule_13(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    // { 18, 19, 103, 103 }
    switch (hash) {
        default:
        return 0;
        // isel_if_branch(ctx, f, n)
        case 18: case 19: {
            {
                return 31;
            }
        }
        // isel_va_start(ctx, f, n)
        case 103: {
            {
                return 32;
            }
        }
    }
}

static MatchRuleID mach_dfa_rule_14(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $cmp = n->inputs[1]->inputs[0];
    uint64_t hash = 0;

    // { 0 }
    {
        uint64_t $cond = TB_NODE_GET_EXTRA_T(n->inputs[1], X86MemOp)->cond;
        TB_DataType $dt = n->dt;
        float $prob = TB_NODE_GET_EXTRA_T(n, TB_NodeIf)->prob;
        return 33;
    }
}

static MatchRuleID mach_dfa_rule_15(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $x = n->inputs[1];
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    // { 18, 17 }
    switch (hash) {
        default:
        return 0;
        // (ANY_OP $ctrl (ANY_OP ___ ___ $x $x))
        case 18: {
            {
                TB_DataType $dt = n->dt;
                float $prob = TB_NODE_GET_EXTRA_T(n, TB_NodeIf)->prob;
                return 34;
            }
        }
        // isel_multi_way_branch(ctx, f, n)
        case 17: {
            {
                TB_DataType $dt = n->dt;
                return 35;
            }
        }
    }
}

static MatchRuleID mach_dfa_rule_16(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $ctrl2 = n->inputs[1]->inputs[0];
    TB_Node* $mem = n->inputs[1]->inputs[1];
    TB_Node* $x = n->inputs[1]->inputs[2];
    uint64_t hash = 0;

    // { 0 }
    {
        uint64_t $flags = TB_NODE_GET_EXTRA_T(n->inputs[1]->inputs[2], X86MemOp)->flags;
        TB_DataType $dt = n->dt;
        float $prob = TB_NODE_GET_EXTRA_T(n, TB_NodeIf)->prob;
        return 36;
    }
}

static MatchRuleID mach_dfa_rule_17(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $x = n->inputs[1]->inputs[1];
    TB_Node* $y = n->inputs[1]->inputs[2];
    uint64_t hash = 0;

    // { 0 }
    {
        TB_DataType $dt = n->dt;
        float $prob = TB_NODE_GET_EXTRA_T(n, TB_NodeIf)->prob;
        return 37;
    }
}

static MatchRuleID mach_dfa_rule_18(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $cmp = n->inputs[0];
    uint64_t hash = 0;

    // { 0 }
    {
        uint64_t $cond = TB_NODE_GET_EXTRA_T(n, X86MemOp)->cond;
        return 38;
    }
}

static MatchRuleID mach_dfa_rule_19(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $pred = n->inputs[1];
    TB_Node* $a = n->inputs[2];
    TB_Node* $b = n->inputs[3];
    uint64_t hash = 0;

    // { 0 }
    {
        TB_DataType $dt = n->dt;
        return 39;
    }
}

static MatchRuleID mach_dfa_rule_20(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $cmp = n->inputs[1]->inputs[0];
    TB_Node* $a = n->inputs[2];
    TB_Node* $b = n->inputs[3];
    uint64_t hash = 0;

    // { 0 }
    {
        uint64_t $cond = TB_NODE_GET_EXTRA_T(n->inputs[1], X86MemOp)->cond;
        TB_DataType $dt = n->dt;
        return 40;
    }
}

static MatchRuleID mach_dfa_rule_21(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $con = n->inputs[1]->inputs[1];
    TB_Node* $y = n->inputs[1]->inputs[2];
    TB_Node* $x = n->inputs[2];
    uint64_t hash = 0;

    // { 0 }
    {
        TB_DataType $dt = n->dt;
        if (as_int32($con) == 1) { return 41; }
    }
    return 0;
}

static MatchRuleID mach_dfa_rule_22(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $x = n->inputs[2];
    TB_Node* $y = n->inputs[3]->inputs[1];
    TB_Node* $con = n->inputs[3]->inputs[2];
    TB_Node* $pred = n->inputs[4];
    uint64_t hash = 0;

    // { 0 }
    {
        TB_DataType $dt = n->dt;
        if (as_int32($con) == 1 && $x == $y && TB_NODE_GET_EXTRA_T(n, X86MemOp)->cond == B) { return 42; }
    }
    return 0;
}

static MatchRuleID mach_dfa_rule_23(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $root = n->inputs[2]->inputs[0];
    size_t $REST_LEN = n->input_count - 3;
    TB_Node** $REST  = &n->inputs[3];
    uint64_t hash = 0;

    // { 0 }
    {
        TB_Symbol* $sym = TB_NODE_GET_EXTRA_T(n->inputs[2], TB_NodeSymbol)->sym;
        return 43;
    }
}

static MatchRuleID mach_dfa_rule_24(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $target = n->inputs[2];
    size_t $REST_LEN = n->input_count - 3;
    TB_Node** $REST  = &n->inputs[3];
    uint64_t hash = 0;

    // { 0 }
    {
        return 44;
    }
}

static MatchRuleID mach_dfa_rule_25(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $ctrl = n->inputs[1]->inputs[0];
    TB_Node* $mem = n->inputs[1]->inputs[1];
    TB_Node* $lhs = n->inputs[1]->inputs[2];
    TB_Node* $rhs = n->inputs[2];
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    // { 73, 74, 75 }
    switch (hash) {
        default:
        return 0;
        // (ANY_OP $ctrl $mem $lhs $rhs)
        case 73: case 74: case 75: {
            {
                TB_DataType $dt = n->dt;
                if (TB_X86_FeatureSet__get(&f->features.x86, TB_X86_FEATURE_BMI2)) { return 45; }
            }
            return 0;
        }
    }
}

static MatchRuleID mach_dfa_rule_26(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $lhs = n;
    uint64_t hash = 0;

    // { 0 }
    {
        return 46;
    }
}

static MatchRuleID mach_dfa_rule_27(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $addr = n->inputs[2];
    uint64_t hash = 0;

    // { 0, 0, 0 }
    switch (hash) {
        default:
        return 0;
        // (ANY_OP $ctrl $mem $addr),(ANY_OP $ctrl $mem $addr (ANY_OP ___ ___ (ANY_OP f->root_node)))
        case 0: {
            {
                TB_DataType $dt = n->dt;
                if (TB_IS_INT_OR_PTR0($dt)) { return 47; }
            }
            {
                TB_DataType $dt = n->dt;
                if ($dt.type == TB_TAG_PTR && $dt.elem_or_addrspace != 0) { return 48; }
            }
            {
                TB_DataType $dt = n->dt;
                if (TB_IS_FLOAT_TYPE($dt) || TB_IS_VECTOR_TYPE($dt)) { return 49; }
            }
            return 0;
        }
    }
}

static MatchRuleID mach_dfa_rule_28(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $addr = n->inputs[2];
    uint64_t hash = 0;

    // { 0, 0, 0 }
    switch (hash) {
        default:
        return 0;
        // (ANY_OP $ctrl $mem $addr),(ANY_OP $ctrl $mem $addr (ANY_OP ___ ___ (ANY_OP f->root_node)))
        case 0: {
            {
                TB_DataType $dt = n->dt;
                if (TB_IS_INT_OR_PTR0($dt)) { return 50; }
            }
            {
                TB_DataType $dt = n->dt;
                if ($dt.type == TB_TAG_PTR && $dt.elem_or_addrspace != 0) { return 51; }
            }
            {
                TB_DataType $dt = n->dt;
                if (TB_IS_FLOAT_TYPE($dt) || TB_IS_VECTOR_TYPE($dt)) { return 52; }
            }
            return 0;
        }
    }
}

static MatchRuleID mach_dfa_rule_29(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $addr = n->inputs[2];
    TB_Node* $val = n->inputs[3];
    uint64_t hash = 0;

    // { 0, 0 }
    switch (hash) {
        default:
        return 0;
        // (ANY_OP $ctrl $mem $addr $val)
        case 0: {
            {
                if (TB_IS_INT_OR_PTR($val->dt)) { return 53; }
            }
            {
                if (TB_IS_FLOAT_TYPE(n->inputs[3]->dt) || TB_IS_VECTOR_TYPE(n->inputs[3]->dt)) { return 54; }
            }
            return 0;
        }
    }
}

static MatchRuleID mach_dfa_rule_30(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $lhs = n->inputs[2];
    TB_Node* $val = n->inputs[3];
    uint64_t hash = 0;

    // { 0, 0 }
    switch (hash) {
        default:
        return 0;
        // (ANY_OP $ctrl $mem $lhs $val)
        case 0: {
            {
                if (TB_IS_INT_OR_PTR($val->dt)) { return 55; }
            }
            {
                if (TB_IS_FLOAT_TYPE(n->inputs[3]->dt) || TB_IS_VECTOR_TYPE(n->inputs[3]->dt)) { return 56; }
            }
            return 0;
        }
    }
}

static MatchRuleID mach_dfa_rule_31(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $mem = n->inputs[1];
    TB_Node* $lhs = n->inputs[2];
    TB_Node* $val = n->inputs[3];
    uint64_t hash = 0;

    // { 0 }
    {
        uint64_t $flags = TB_NODE_GET_EXTRA_T(n->inputs[2], X86MemOp)->flags;
        if (TB_IS_INT_OR_PTR($val->dt) && fits_into_int32($val->dt, $val)) { return 57; }
    }
    return 0;
}

static MatchRuleID mach_dfa_rule_32(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $lhs = n->inputs[1];
    TB_Node* $ctrl = n->inputs[2]->inputs[0];
    TB_Node* $mem = n->inputs[2]->inputs[1];
    TB_Node* $rhs = n->inputs[2]->inputs[2];
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    // { 70, 68, 67, 71, 69, 72 }
    switch (hash) {
        default:
        return 0;
        // (IARITH $ctrl $mem $rhs $lhs)
        case 70: case 68: case 67: case 71: case 69: {
            {
                TB_DataType $dt = n->dt;
                return 58;
            }
        }
        // (ANY_OP $ctrl $mem $rhs $lhs)
        case 72: {
            {
                TB_DataType $dt = n->dt;
                return 59;
            }
        }
    }
}

static MatchRuleID mach_dfa_rule_33(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $x = n->inputs[1];
    TB_Node* $root = n->inputs[1]->inputs[0];
    uint64_t hash = 0;

    // { 0, 0 }
    switch (hash) {
        default:
        return 0;
        // (ANY_OP $root),(ANY_OP ___ ___ (ANY_OP $root))
        case 0: {
            {
                TB_DataType $dt = n->dt;
                if (is_float_zero($x)) { return 60; }
            }
            {
                TB_DataType $dt = n->dt;
                return 61;
            }
        }
    }
}

static MatchRuleID mach_dfa_rule_34(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $src = n->inputs[1];
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    // { 66, 60, 51, 54, 55, 54, 55, 55, 54, 55 }
    switch (hash) {
        default:
        return 0;
        // (ANY_OP ___ ___ (ANY_OP f->root_node) $src)
        case 66: {
            {
                TB_DataType $dt = n->dt;
                return 62;
            }
        }
        // (ANY_OP ___ $src)
        case 60: case 51: {
            {
                TB_DataType $dt = n->dt;
                return 63;
            }
        }
        // (ANY_OP ___ ___ $src)
        case 54: {
            {
                TB_DataType $dt = n->dt;
                if ($dt.type == TB_TAG_I64 && $src->dt.type == TB_TAG_I32) { return 64; }
            }
            {
                TB_DataType $dt = n->dt;
                if (($dt.type == TB_TAG_I16 || $dt.type == TB_TAG_I32 || $dt.type == TB_TAG_I64) && ($src->dt.type == TB_TAG_I8 || $src->dt.type == TB_TAG_BOOL)) { return 65; }
            }
            {
                TB_DataType $dt = n->dt;
                if ($src->dt.type == TB_TAG_I16) { return 66; }
            }
            return 0;
        }
        // (ANY_OP ___ ___ $src),(ANY_OP ___ $src)
        case 55: {
            {
                TB_DataType $dt = n->dt;
                if ($dt.type == TB_TAG_I64 && $src->dt.type == TB_TAG_I32) { return 67; }
            }
            {
                TB_DataType $dt = n->dt;
                if (($dt.type == TB_TAG_I16 || $dt.type == TB_TAG_I32 || $dt.type == TB_TAG_I64) && ($src->dt.type == TB_TAG_I8 || $src->dt.type == TB_TAG_BOOL)) { return 68; }
            }
            {
                TB_DataType $dt = n->dt;
                if ($dt.type == TB_TAG_I8 && $src->dt.type == TB_TAG_BOOL) { return 69; }
            }
            {
                TB_DataType $dt = n->dt;
                if ($src->dt.type == TB_TAG_I16) { return 70; }
            }
            return 0;
        }
    }
}

static MatchRuleID mach_dfa_rule_35(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $src = n->inputs[1];
    TB_Node* $ctrl = n->inputs[1]->inputs[0];
    TB_Node* $mem = n->inputs[1]->inputs[1];
    TB_Node* $addr = n->inputs[1]->inputs[2];
    uint64_t hash = 0;

    // { 0 }
    {
        TB_DataType $dt = n->dt;
        if (($dt.type == TB_TAG_I16 || $dt.type == TB_TAG_I32 || $dt.type == TB_TAG_I64) && ($src->dt.type == TB_TAG_I8 || $src->dt.type == TB_TAG_BOOL)) { return 71; }
    }
    return 0;
}

static MatchRuleID mach_dfa_rule_36(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $src = n->inputs[1];
    TB_Node* $ctrl = n->inputs[1]->inputs[0];
    TB_Node* $mem = n->inputs[1]->inputs[1];
    TB_Node* $addr = n->inputs[1]->inputs[2];
    uint64_t hash = 0;

    // { 0 }
    {
        TB_DataType $dt = n->dt;
        if (($dt.type == TB_TAG_I16 || $dt.type == TB_TAG_I32 || $dt.type == TB_TAG_I64) && ($src->dt.type == TB_TAG_I8 || $src->dt.type == TB_TAG_BOOL)) { return 72; }
    }
    return 0;
}

static bool mach_is_subpat(int type) {
    switch (type) {
        default: return false;
    }
}

static int mach_is_operand(int type) {
    switch (type) {
        default: return 0;
    }
}

enum {
    RULE_NONE,
    RULE_MEMORY,
    RULE_COND,
    RULE_IARITH,
};

static void mach_indent(int depth) {
    while (depth--) { printf("  "); }
}

#if 0
#define DFA_LOG(depth, n, fmt, ...) (mach_indent(depth), printf(fmt, __VA_ARGS__), printf(": "), tb_print_dumb_node(NULL, n), printf("\n"))
#define DFA_LOG2(depth, fmt, ...) (mach_indent(depth), printf(fmt, __VA_ARGS__), printf("\n"))
#else
#define DFA_LOG(depth, n, fmt, ...)
#define DFA_LOG2(depth, fmt, ...)
#endif

static int node_isel_op_group(MatchRuleID id) {
    switch (id) {
        case 1: return RULE_MEMORY;
        case 2: return RULE_MEMORY;
        case 3: return RULE_MEMORY;
        case 4: return RULE_MEMORY;
        case 5: return RULE_MEMORY;
        case 6: return RULE_MEMORY;
        case 7: return RULE_MEMORY;
        case 10: return RULE_MEMORY;
        case 11: return RULE_MEMORY;
        case 12: return RULE_MEMORY;
        case 13: return RULE_MEMORY;
        case 14: return RULE_COND;
        case 15: return RULE_COND;
        case 16: return RULE_IARITH;
        case 17: return RULE_IARITH;
        case 20: return RULE_IARITH;
        case 23: return RULE_COND;
        case 24: return RULE_COND;
        case 25: return RULE_COND;
        case 26: return RULE_IARITH;
        case 27: return RULE_IARITH;
        case 41: return RULE_COND;
        case 58: return RULE_IARITH;
        default: return RULE_NONE;
    }
}

#define RULE(x) mach_dfa_rule_ ## x(ctx, f, n, depth)
static MatchRuleID node_isel_raw(Ctx* ctx, TB_Function* f, Set* shared, TB_Node* n, int depth) {
    MatchRuleID k;
    switch (n->type) {
        case TB_PTR_OFFSET:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            if (n->inputs[0] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[1], "Step");
            TB_Node* n__0 = n->inputs[1];
            DFA_LOG2(depth+2, "TRY 'LOCAL'");
            do { if (n__0 && n__0->type == TB_LOCAL && !set_get(shared, n__0->gvn)) {
                DFA_LOG(depth+2, n__0->inputs[0], "Step");
                // MATCH ANY n__0->inputs[0]
                DFA_LOG(depth+1, n->inputs[2], "Step");
                TB_Node* n__1 = n->inputs[2];
                DFA_LOG2(depth+2, "TRY 'ICONST'");
                do { if (n__1 && n__1->type == TB_ICONST && !set_get(shared, n__1->gvn)) {
                    k = (n->input_count == 3 ? RULE(7) : 0);
                    if (k) { return k; }
                } } while (0);

                break;
            } } while (0);

            DFA_LOG2(depth+2, "TRY 'SYMBOL'");
            do { if (n__0 && n__0->type == TB_SYMBOL && !set_get(shared, n__0->gvn)) {
                DFA_LOG(depth+2, n__0->inputs[0], "Step");
                // MATCH ANY n__0->inputs[0]
                DFA_LOG(depth+1, n->inputs[2], "Step");
                TB_Node* n__2 = n->inputs[2];
                DFA_LOG2(depth+2, "TRY 'ICONST'");
                do { if (n__2 && n__2->type == TB_ICONST && !set_get(shared, n__2->gvn)) {
                    k = (n->input_count == 3 ? RULE(9) : 0);
                    if (k) { return k; }
                } } while (0);

                k = (n->input_count == 3 ? RULE(8) : 0);
                if (k) { return k; }
            } } while (0);

            DFA_LOG2(depth+2, "TRY 'MEMORY'");
            int n__0_mark = ctx->dsl.top;
            int n__0_final = !set_get(shared, n__0->gvn) ? node_isel_raw(ctx, f, shared, n__0, depth+1) : 0;
            do { if (node_isel_op_group(n__0_final) == RULE_MEMORY) {
                DFA_LOG(depth+2, n__0, "Accept as MEMORY");
                ctx->dsl.accept[ctx->dsl.top++] = (TB_SubMatch){ n__0_final, n, 1 };
                DFA_LOG(depth+2, n__0->inputs[0], "Step");
                TB_Node* n__4 = n__0->inputs[0];
                DFA_LOG2(depth+3, "TRY 'MACH_FRAME_PTR'");
                do { if (n__4 && n__4->type == TB_MACH_FRAME_PTR && !set_get(shared, n__4->gvn)) {
                    DFA_LOG(depth+1, n->inputs[2], "Step");
                    TB_Node* n__5 = n->inputs[2];
                    DFA_LOG2(depth+2, "TRY 'ICONST'");
                    do { if (n__5 && n__5->type == TB_ICONST && !set_get(shared, n__5->gvn)) {
                        k = (n->input_count == 3 ? RULE(10) : 0);
                        if (k) { return k; }
                    } } while (0);

                    break;
                } } while (0);

                break;
            } else { DFA_LOG(depth+2, n__0, "Reject"); } } while (0);
            ctx->dsl.top = n__0_mark;

            DFA_LOG(depth+1, n->inputs[2], "Step");
            TB_Node* n__6 = n->inputs[2];
            DFA_LOG2(depth+2, "TRY 'SHL'");
            do { if (n__6 && n__6->type == TB_SHL && !set_get(shared, n__6->gvn)) {
                DFA_LOG(depth+2, n__6->inputs[0], "Step");
                if (n__6->inputs[0] != NULL) { break; }
                DFA_LOG(depth+2, n__6->inputs[1], "Step");
                // MATCH ANY n__6->inputs[1]
                DFA_LOG(depth+2, n__6->inputs[2], "Step");
                TB_Node* n__7 = n__6->inputs[2];
                DFA_LOG2(depth+3, "TRY 'ICONST'");
                do { if (n__7 && n__7->type == TB_ICONST && !set_get(shared, n__7->gvn)) {
                    k = (n->input_count == 3 ? RULE(5) : 0);
                    if (k) { return k; }
                } } while (0);

                break;
            } } while (0);

            DFA_LOG2(depth+2, "TRY 'ADD'");
            do { if (n__6 && n__6->type == TB_ADD && !set_get(shared, n__6->gvn)) {
                DFA_LOG(depth+2, n__6->inputs[0], "Step");
                if (n__6->inputs[0] != NULL) { break; }
                DFA_LOG(depth+2, n__6->inputs[1], "Step");
                TB_Node* n__8 = n__6->inputs[1];
                DFA_LOG2(depth+3, "TRY 'SHL'");
                do { if (n__8 && n__8->type == TB_SHL && !set_get(shared, n__8->gvn)) {
                    DFA_LOG(depth+3, n__8->inputs[0], "Step");
                    if (n__8->inputs[0] != NULL) { break; }
                    DFA_LOG(depth+3, n__8->inputs[1], "Step");
                    // MATCH ANY n__8->inputs[1]
                    DFA_LOG(depth+3, n__8->inputs[2], "Step");
                    TB_Node* n__9 = n__8->inputs[2];
                    DFA_LOG2(depth+4, "TRY 'ICONST'");
                    do { if (n__9 && n__9->type == TB_ICONST && !set_get(shared, n__9->gvn)) {
                        DFA_LOG(depth+2, n__6->inputs[2], "Step");
                        TB_Node* n__10 = n__6->inputs[2];
                        DFA_LOG2(depth+3, "TRY 'ICONST'");
                        do { if (n__10 && n__10->type == TB_ICONST && !set_get(shared, n__10->gvn)) {
                            k = (n->input_count == 3 ? RULE(4) : 0);
                            if (k) { return k; }
                        } } while (0);

                        break;
                    } } while (0);

                    break;
                } } while (0);

                DFA_LOG(depth+2, n__6->inputs[2], "Step");
                TB_Node* n__11 = n__6->inputs[2];
                DFA_LOG2(depth+3, "TRY 'ICONST'");
                do { if (n__11 && n__11->type == TB_ICONST && !set_get(shared, n__11->gvn)) {
                    k = (n->input_count == 3 ? RULE(3) : 0);
                    if (k) { return k; }
                } } while (0);

                break;
            } } while (0);

            DFA_LOG2(depth+2, "TRY 'ICONST'");
            do { if (n__6 && n__6->type == TB_ICONST && !set_get(shared, n__6->gvn)) {
                k = (n->input_count == 3 ? RULE(2) : 0);
                if (k) { return k; }
            } } while (0);

            return (n->input_count == 3 ? RULE(1) : 0);
        }
        case TB_LOCAL:
        case TB_SYMBOL:
        case TB_F32CONST:
        case TB_F64CONST:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            // MATCH ANY n->inputs[0]
            return (n->input_count == 1 ? RULE(6) : 0);
        }
        case TB_CMP_SLE:
        case TB_CMP_SLT:
        case TB_CMP_ULE:
        case TB_CMP_ULT:
        case TB_CMP_EQ:
        case TB_CMP_NE:
        case TB_ROL:
        case TB_ROR:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            if (n->inputs[0] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[1], "Step");
            // MATCH ANY n->inputs[1]
            DFA_LOG(depth+1, n->inputs[2], "Step");
            TB_Node* n__12 = n->inputs[2];
            DFA_LOG2(depth+2, "TRY 'ICONST'");
            do { if (n__12 && n__12->type == TB_ICONST && !set_get(shared, n__12->gvn)) {
                k = (n->input_count == 3 ? RULE(12) : 0);
                if (k) { return k; }
            } } while (0);

            return (n->input_count == 3 ? RULE(11) : 0);
        }
        case TB_CMP_FLT:
        case TB_CMP_FLE:
        case TB_UDIV:
        case TB_SDIV:
        case TB_UMOD:
        case TB_SMOD:
        case TB_FADD:
        case TB_FSUB:
        case TB_FMUL:
        case TB_FDIV:
        case TB_FMAX:
        case TB_FMIN:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            if (n->inputs[0] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[1], "Step");
            // MATCH ANY n->inputs[1]
            DFA_LOG(depth+1, n->inputs[2], "Step");
            // MATCH ANY n->inputs[2]
            return (n->input_count == 3 ? RULE(11) : 0);
        }
        case TB_IF:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            // MATCH ANY n->inputs[0]
            DFA_LOG(depth+1, n->inputs[1], "Step");
            TB_Node* n__13 = n->inputs[1];
            DFA_LOG2(depth+2, "TRY 'AND'");
            do { if (n__13 && n__13->type == TB_AND && !set_get(shared, n__13->gvn)) {
                DFA_LOG(depth+2, n__13->inputs[0], "Step");
                if (n__13->inputs[0] != NULL) { break; }
                DFA_LOG(depth+2, n__13->inputs[1], "Step");
                // MATCH ANY n__13->inputs[1]
                DFA_LOG(depth+2, n__13->inputs[2], "Step");
                // MATCH ANY n__13->inputs[2]
                k = (n->input_count == 2 ? RULE(17) : 0);
                if (k) { return k; }
            } } while (0);

            DFA_LOG2(depth+2, "TRY 'LOAD'");
            do { if (n__13 && n__13->type == TB_LOAD && !set_get(shared, n__13->gvn)) {
                DFA_LOG(depth+2, n__13->inputs[0], "Step");
                // MATCH ANY n__13->inputs[0]
                DFA_LOG(depth+2, n__13->inputs[1], "Step");
                // MATCH ANY n__13->inputs[1]
                DFA_LOG(depth+2, n__13->inputs[2], "Step");
                TB_Node* n__14 = n__13->inputs[2];
                DFA_LOG2(depth+3, "TRY 'MEMORY'");
                int n__14_mark = ctx->dsl.top;
                int n__14_final = !set_get(shared, n__14->gvn) ? node_isel_raw(ctx, f, shared, n__14, depth+2) : 0;
                do { if (node_isel_op_group(n__14_final) == RULE_MEMORY) {
                    DFA_LOG(depth+3, n__14, "Accept as MEMORY");
                    ctx->dsl.accept[ctx->dsl.top++] = (TB_SubMatch){ n__14_final, n__13, 2 };
                    k = (n->input_count == 2 ? RULE(16) : 0);
                    if (k) { return k; }
                } else { DFA_LOG(depth+3, n__14, "Reject"); } } while (0);
                ctx->dsl.top = n__14_mark;

                break;
            } } while (0);

            DFA_LOG2(depth+2, "TRY 'COND'");
            int n__13_mark = ctx->dsl.top;
            int n__13_final = !set_get(shared, n__13->gvn) ? node_isel_raw(ctx, f, shared, n__13, depth+1) : 0;
            do { if (node_isel_op_group(n__13_final) == RULE_COND) {
                DFA_LOG(depth+2, n__13, "Accept as COND");
                ctx->dsl.accept[ctx->dsl.top++] = (TB_SubMatch){ n__13_final, n, 1 };
                DFA_LOG(depth+2, n__13->inputs[0], "Step");
                // MATCH ANY n__13->inputs[0]
                k = (n->input_count == 2 ? RULE(14) : 0);
                if (k) { return k; }
            } else { DFA_LOG(depth+2, n__13, "Reject"); } } while (0);
            ctx->dsl.top = n__13_mark;

            return (n->input_count == 2 ? RULE(15) : RULE(13));
        }
        case TB_AFFINE_LATCH:
        case TB_VA_START:
        {
            return RULE(13);
        }
        case TB_BRANCH:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            // MATCH ANY n->inputs[0]
            DFA_LOG(depth+1, n->inputs[1], "Step");
            // MATCH ANY n->inputs[1]
            return (n->input_count == 2 ? RULE(15) : 0);
        }
        case TB_x86_COND:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            // MATCH ANY n->inputs[0]
            return (n->input_count == 1 ? RULE(18) : 0);
        }
        case TB_SELECT:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            if (n->inputs[0] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[1], "Step");
            TB_Node* n__17 = n->inputs[1];
            DFA_LOG2(depth+2, "TRY 'COND'");
            int n__17_mark = ctx->dsl.top;
            int n__17_final = !set_get(shared, n__17->gvn) ? node_isel_raw(ctx, f, shared, n__17, depth+1) : 0;
            do { if (node_isel_op_group(n__17_final) == RULE_COND) {
                DFA_LOG(depth+2, n__17, "Accept as COND");
                ctx->dsl.accept[ctx->dsl.top++] = (TB_SubMatch){ n__17_final, n, 1 };
                DFA_LOG(depth+2, n__17->inputs[0], "Step");
                // MATCH ANY n__17->inputs[0]
                DFA_LOG(depth+1, n->inputs[2], "Step");
                // MATCH ANY n->inputs[2]
                DFA_LOG(depth+1, n->inputs[3], "Step");
                // MATCH ANY n->inputs[3]
                k = (n->input_count == 4 ? RULE(20) : 0);
                if (k) { return k; }
            } else { DFA_LOG(depth+2, n__17, "Reject"); } } while (0);
            ctx->dsl.top = n__17_mark;

            DFA_LOG(depth+1, n->inputs[2], "Step");
            // MATCH ANY n->inputs[2]
            DFA_LOG(depth+1, n->inputs[3], "Step");
            // MATCH ANY n->inputs[3]
            return (n->input_count == 4 ? RULE(19) : 0);
        }
        case TB_AND:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            if (n->inputs[0] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[1], "Step");
            TB_Node* n__19 = n->inputs[1];
            DFA_LOG2(depth+2, "TRY 'SHL'");
            do { if (n__19 && n__19->type == TB_SHL && !set_get(shared, n__19->gvn)) {
                DFA_LOG(depth+2, n__19->inputs[0], "Step");
                if (n__19->inputs[0] != NULL) { break; }
                DFA_LOG(depth+2, n__19->inputs[1], "Step");
                TB_Node* n__20 = n__19->inputs[1];
                DFA_LOG2(depth+3, "TRY 'ICONST'");
                do { if (n__20 && n__20->type == TB_ICONST && !set_get(shared, n__20->gvn)) {
                    DFA_LOG(depth+2, n__19->inputs[2], "Step");
                    // MATCH ANY n__19->inputs[2]
                    DFA_LOG(depth+1, n->inputs[2], "Step");
                    // MATCH ANY n->inputs[2]
                    k = (n->input_count == 3 ? RULE(21) : 0);
                    if (k) { return k; }
                } } while (0);

                break;
            } } while (0);

            DFA_LOG(depth+1, n->inputs[2], "Step");
            TB_Node* n__21 = n->inputs[2];
            DFA_LOG2(depth+2, "TRY 'ICONST'");
            do { if (n__21 && n__21->type == TB_ICONST && !set_get(shared, n__21->gvn)) {
                k = (n->input_count == 3 ? RULE(12) : 0);
                if (k) { return k; }
            } } while (0);

            DFA_LOG2(depth+2, "TRY 'LOAD'");
            do { if (n__21 && n__21->type == TB_LOAD && !set_get(shared, n__21->gvn)) {
                DFA_LOG(depth+2, n__21->inputs[0], "Step");
                // MATCH ANY n__21->inputs[0]
                DFA_LOG(depth+2, n__21->inputs[1], "Step");
                // MATCH ANY n__21->inputs[1]
                DFA_LOG(depth+2, n__21->inputs[2], "Step");
                TB_Node* n__22 = n__21->inputs[2];
                DFA_LOG2(depth+3, "TRY 'MEMORY'");
                int n__22_mark = ctx->dsl.top;
                int n__22_final = !set_get(shared, n__22->gvn) ? node_isel_raw(ctx, f, shared, n__22, depth+2) : 0;
                do { if (node_isel_op_group(n__22_final) == RULE_MEMORY) {
                    DFA_LOG(depth+3, n__22, "Accept as MEMORY");
                    ctx->dsl.accept[ctx->dsl.top++] = (TB_SubMatch){ n__22_final, n__21, 2 };
                    k = (n->input_count == 3 ? RULE(32) : 0);
                    if (k) { return k; }
                } else { DFA_LOG(depth+3, n__22, "Reject"); } } while (0);
                ctx->dsl.top = n__22_mark;

                break;
            } } while (0);

            return (n->input_count == 3 ? RULE(11) : 0);
        }
        case TB_x86_cmovcc:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            if (n->inputs[0] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[1], "Step");
            if (n->inputs[1] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[2], "Step");
            // MATCH ANY n->inputs[2]
            DFA_LOG(depth+1, n->inputs[3], "Step");
            TB_Node* n__24 = n->inputs[3];
            DFA_LOG2(depth+2, "TRY 'ADD'");
            do { if (n__24 && n__24->type == TB_ADD && !set_get(shared, n__24->gvn)) {
                DFA_LOG(depth+2, n__24->inputs[0], "Step");
                if (n__24->inputs[0] != NULL) { break; }
                DFA_LOG(depth+2, n__24->inputs[1], "Step");
                // MATCH ANY n__24->inputs[1]
                DFA_LOG(depth+2, n__24->inputs[2], "Step");
                TB_Node* n__25 = n__24->inputs[2];
                DFA_LOG2(depth+3, "TRY 'ICONST'");
                do { if (n__25 && n__25->type == TB_ICONST && !set_get(shared, n__25->gvn)) {
                    DFA_LOG(depth+1, n->inputs[4], "Step");
                    // MATCH ANY n->inputs[4]
                    k = (n->input_count == 5 ? RULE(22) : 0);
                    if (k) { return k; }
                } } while (0);

                break;
            } } while (0);

            break;
        }
        case TB_CALL:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            // MATCH ANY n->inputs[0]
            DFA_LOG(depth+1, n->inputs[1], "Step");
            // MATCH ANY n->inputs[1]
            DFA_LOG(depth+1, n->inputs[2], "Step");
            TB_Node* n__26 = n->inputs[2];
            DFA_LOG2(depth+2, "TRY 'SYMBOL'");
            do { if (n__26 && n__26->type == TB_SYMBOL && !set_get(shared, n__26->gvn)) {
                DFA_LOG(depth+2, n__26->inputs[0], "Step");
                // MATCH ANY n__26->inputs[0]
                k = RULE(23);
                if (k) { return k; }
            } } while (0);

            return RULE(24);
        }
        case TB_SHL:
        case TB_SHR:
        case TB_SAR:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            if (n->inputs[0] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[1], "Step");
            TB_Node* n__27 = n->inputs[1];
            DFA_LOG2(depth+2, "TRY 'LOAD'");
            do { if (n__27 && n__27->type == TB_LOAD && !set_get(shared, n__27->gvn)) {
                DFA_LOG(depth+2, n__27->inputs[0], "Step");
                // MATCH ANY n__27->inputs[0]
                DFA_LOG(depth+2, n__27->inputs[1], "Step");
                // MATCH ANY n__27->inputs[1]
                DFA_LOG(depth+2, n__27->inputs[2], "Step");
                TB_Node* n__28 = n__27->inputs[2];
                DFA_LOG2(depth+3, "TRY 'MEMORY'");
                int n__28_mark = ctx->dsl.top;
                int n__28_final = !set_get(shared, n__28->gvn) ? node_isel_raw(ctx, f, shared, n__28, depth+2) : 0;
                do { if (node_isel_op_group(n__28_final) == RULE_MEMORY) {
                    DFA_LOG(depth+3, n__28, "Accept as MEMORY");
                    ctx->dsl.accept[ctx->dsl.top++] = (TB_SubMatch){ n__28_final, n__27, 2 };
                    DFA_LOG(depth+1, n->inputs[2], "Step");
                    // MATCH ANY n->inputs[2]
                    k = (n->input_count == 3 ? RULE(25) : 0);
                    if (k) { return k; }
                } else { DFA_LOG(depth+3, n__28, "Reject"); } } while (0);
                ctx->dsl.top = n__28_mark;

                break;
            } } while (0);

            DFA_LOG(depth+1, n->inputs[2], "Step");
            TB_Node* n__30 = n->inputs[2];
            DFA_LOG2(depth+2, "TRY 'ICONST'");
            do { if (n__30 && n__30->type == TB_ICONST && !set_get(shared, n__30->gvn)) {
                k = (n->input_count == 3 ? RULE(12) : 0);
                if (k) { return k; }
            } } while (0);

            return (n->input_count == 3 ? RULE(11) : 0);
        }
        case TB_x86_MEMORY:
        {
            return RULE(26);
        }
        case TB_LOAD:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            // MATCH ANY n->inputs[0]
            DFA_LOG(depth+1, n->inputs[1], "Step");
            // MATCH ANY n->inputs[1]
            DFA_LOG(depth+1, n->inputs[2], "Step");
            TB_Node* n__31 = n->inputs[2];
            DFA_LOG2(depth+2, "TRY 'MEMORY'");
            int n__31_mark = ctx->dsl.top;
            int n__31_final = !set_get(shared, n__31->gvn) ? node_isel_raw(ctx, f, shared, n__31, depth+1) : 0;
            do { if (node_isel_op_group(n__31_final) == RULE_MEMORY) {
                DFA_LOG(depth+2, n__31, "Accept as MEMORY");
                ctx->dsl.accept[ctx->dsl.top++] = (TB_SubMatch){ n__31_final, n, 2 };
                k = (n->input_count == 3 ? RULE(28) : 0);
                if (k) { return k; }
            } else { DFA_LOG(depth+2, n__31, "Reject"); } } while (0);
            ctx->dsl.top = n__31_mark;

            return (n->input_count == 3 ? RULE(27) : 0);
        }
        case TB_STORE:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            // MATCH ANY n->inputs[0]
            DFA_LOG(depth+1, n->inputs[1], "Step");
            // MATCH ANY n->inputs[1]
            DFA_LOG(depth+1, n->inputs[2], "Step");
            TB_Node* n__33 = n->inputs[2];
            DFA_LOG2(depth+2, "TRY 'MEMORY'");
            int n__33_mark = ctx->dsl.top;
            int n__33_final = !set_get(shared, n__33->gvn) ? node_isel_raw(ctx, f, shared, n__33, depth+1) : 0;
            do { if (node_isel_op_group(n__33_final) == RULE_MEMORY) {
                DFA_LOG(depth+2, n__33, "Accept as MEMORY");
                ctx->dsl.accept[ctx->dsl.top++] = (TB_SubMatch){ n__33_final, n, 2 };
                DFA_LOG(depth+1, n->inputs[3], "Step");
                TB_Node* n__35 = n->inputs[3];
                DFA_LOG2(depth+2, "TRY 'ICONST'");
                do { if (n__35 && n__35->type == TB_ICONST && !set_get(shared, n__35->gvn)) {
                    k = (n->input_count == 4 ? RULE(31) : 0);
                    if (k) { return k; }
                } } while (0);

                k = (n->input_count == 4 ? RULE(30) : 0);
                if (k) { return k; }
            } else { DFA_LOG(depth+2, n__33, "Reject"); } } while (0);
            ctx->dsl.top = n__33_mark;

            DFA_LOG(depth+1, n->inputs[3], "Step");
            // MATCH ANY n->inputs[3]
            return (n->input_count == 4 ? RULE(29) : 0);
        }
        case TB_ADD:
        case TB_OR:
        case TB_SUB:
        case TB_XOR:
        case TB_MUL:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            if (n->inputs[0] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[1], "Step");
            // MATCH ANY n->inputs[1]
            DFA_LOG(depth+1, n->inputs[2], "Step");
            TB_Node* n__36 = n->inputs[2];
            DFA_LOG2(depth+2, "TRY 'ICONST'");
            do { if (n__36 && n__36->type == TB_ICONST && !set_get(shared, n__36->gvn)) {
                k = (n->input_count == 3 ? RULE(12) : 0);
                if (k) { return k; }
            } } while (0);

            DFA_LOG2(depth+2, "TRY 'LOAD'");
            do { if (n__36 && n__36->type == TB_LOAD && !set_get(shared, n__36->gvn)) {
                DFA_LOG(depth+2, n__36->inputs[0], "Step");
                // MATCH ANY n__36->inputs[0]
                DFA_LOG(depth+2, n__36->inputs[1], "Step");
                // MATCH ANY n__36->inputs[1]
                DFA_LOG(depth+2, n__36->inputs[2], "Step");
                TB_Node* n__37 = n__36->inputs[2];
                DFA_LOG2(depth+3, "TRY 'MEMORY'");
                int n__37_mark = ctx->dsl.top;
                int n__37_final = !set_get(shared, n__37->gvn) ? node_isel_raw(ctx, f, shared, n__37, depth+2) : 0;
                do { if (node_isel_op_group(n__37_final) == RULE_MEMORY) {
                    DFA_LOG(depth+3, n__37, "Accept as MEMORY");
                    ctx->dsl.accept[ctx->dsl.top++] = (TB_SubMatch){ n__37_final, n__36, 2 };
                    k = (n->input_count == 3 ? RULE(32) : 0);
                    if (k) { return k; }
                } else { DFA_LOG(depth+3, n__37, "Reject"); } } while (0);
                ctx->dsl.top = n__37_mark;

                break;
            } } while (0);

            return (n->input_count == 3 ? RULE(11) : 0);
        }
        case TB_VBROADCAST:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            if (n->inputs[0] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[1], "Step");
            TB_Node* n__39 = n->inputs[1];
            DFA_LOG2(depth+2, "TRY 'F32CONST'");
            do { if (n__39 && n__39->type == TB_F32CONST && !set_get(shared, n__39->gvn)) {
                DFA_LOG(depth+2, n__39->inputs[0], "Step");
                // MATCH ANY n__39->inputs[0]
                k = (n->input_count == 2 ? RULE(33) : 0);
                if (k) { return k; }
            } } while (0);

            break;
        }
        case TB_FNEG:
        case TB_BITCAST:
        case TB_TRUNCATE:
        case TB_SIGN_EXT:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            if (n->inputs[0] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[1], "Step");
            // MATCH ANY n->inputs[1]
            return (n->input_count == 2 ? RULE(34) : 0);
        }
        case TB_ZERO_EXT:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            if (n->inputs[0] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[1], "Step");
            TB_Node* n__40 = n->inputs[1];
            DFA_LOG2(depth+2, "TRY 'LOAD'");
            do { if (n__40 && n__40->type == TB_LOAD && !set_get(shared, n__40->gvn)) {
                DFA_LOG(depth+2, n__40->inputs[0], "Step");
                // MATCH ANY n__40->inputs[0]
                DFA_LOG(depth+2, n__40->inputs[1], "Step");
                // MATCH ANY n__40->inputs[1]
                DFA_LOG(depth+2, n__40->inputs[2], "Step");
                TB_Node* n__41 = n__40->inputs[2];
                DFA_LOG2(depth+3, "TRY 'MEMORY'");
                int n__41_mark = ctx->dsl.top;
                int n__41_final = !set_get(shared, n__41->gvn) ? node_isel_raw(ctx, f, shared, n__41, depth+2) : 0;
                do { if (node_isel_op_group(n__41_final) == RULE_MEMORY) {
                    DFA_LOG(depth+3, n__41, "Accept as MEMORY");
                    ctx->dsl.accept[ctx->dsl.top++] = (TB_SubMatch){ n__41_final, n__40, 2 };
                    k = (n->input_count == 2 ? RULE(36) : 0);
                    if (k) { return k; }
                } else { DFA_LOG(depth+3, n__41, "Reject"); } } while (0);
                ctx->dsl.top = n__41_mark;

                k = (n->input_count == 2 ? RULE(35) : 0);
                if (k) { return k; }
            } } while (0);

            return (n->input_count == 2 ? RULE(34) : 0);
        }
    }
    return 0;
}

static MatchRule match_rules[] = {
    NULL,
    mach_dfa_accept_1,
    mach_dfa_accept_2,
    mach_dfa_accept_3,
    mach_dfa_accept_4,
    mach_dfa_accept_5,
    mach_dfa_accept_6,
    mach_dfa_accept_7,
    mach_dfa_accept_8,
    mach_dfa_accept_9,
    mach_dfa_accept_10,
    mach_dfa_accept_11,
    mach_dfa_accept_12,
    mach_dfa_accept_13,
    mach_dfa_accept_14,
    mach_dfa_accept_15,
    mach_dfa_accept_16,
    mach_dfa_accept_17,
    mach_dfa_accept_18,
    mach_dfa_accept_19,
    mach_dfa_accept_20,
    mach_dfa_accept_21,
    mach_dfa_accept_22,
    mach_dfa_accept_23,
    mach_dfa_accept_24,
    mach_dfa_accept_25,
    mach_dfa_accept_26,
    mach_dfa_accept_27,
    mach_dfa_accept_28,
    mach_dfa_accept_29,
    mach_dfa_accept_30,
    mach_dfa_accept_31,
    mach_dfa_accept_32,
    mach_dfa_accept_33,
    mach_dfa_accept_34,
    mach_dfa_accept_35,
    mach_dfa_accept_36,
    mach_dfa_accept_37,
    mach_dfa_accept_38,
    mach_dfa_accept_39,
    mach_dfa_accept_40,
    mach_dfa_accept_41,
    mach_dfa_accept_42,
    mach_dfa_accept_43,
    mach_dfa_accept_44,
    mach_dfa_accept_45,
    mach_dfa_accept_46,
    mach_dfa_accept_47,
    mach_dfa_accept_48,
    mach_dfa_accept_49,
    mach_dfa_accept_50,
    mach_dfa_accept_51,
    mach_dfa_accept_52,
    mach_dfa_accept_53,
    mach_dfa_accept_54,
    mach_dfa_accept_55,
    mach_dfa_accept_56,
    mach_dfa_accept_57,
    mach_dfa_accept_58,
    mach_dfa_accept_59,
    mach_dfa_accept_60,
    mach_dfa_accept_61,
    mach_dfa_accept_62,
    mach_dfa_accept_63,
    mach_dfa_accept_64,
    mach_dfa_accept_65,
    mach_dfa_accept_66,
    mach_dfa_accept_67,
    mach_dfa_accept_68,
    mach_dfa_accept_69,
    mach_dfa_accept_70,
    mach_dfa_accept_71,
    mach_dfa_accept_72,
};
#undef ACCEPT
