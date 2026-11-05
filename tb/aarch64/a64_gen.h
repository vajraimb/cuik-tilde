#include "../emitter.h"
#include "../tb_internal.h"
#include <string.h>
#include <ctype.h>

static void global_init() {}

typedef enum GPR {
    R0,
    R1,
    R2,
    R3,
    R4,
    R5,
    R6,
    R7,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
    R16,
    R17,
    R18,
    R19,
    R20,
    R21,
    R22,
    R23,
    R24,
    R25,
    R26,
    R27,
    R28,
    R29,
    R30,
    ZR,
    GPR_NONE = -1,
} GPR;

typedef enum FPR {
    V0,
    V1,
    V2,
    V3,
    V4,
    V5,
    V6,
    V7,
    V8,
    V9,
    V10,
    V11,
    V12,
    V13,
    V14,
    V15,
    V16,
    V17,
    V18,
    V19,
    V20,
    V21,
    V22,
    V23,
    V24,
    V25,
    V26,
    V27,
    V28,
    V29,
    V30,
    V31,
    FPR_NONE = -1,
} FPR;

enum {
    REG_CLASS_GPR = 1,
    REG_CLASS_FPR = 2,
    REG_CLASS_FLAGS = 3,
    REG_CLASS_COUNT,
};

enum {
    BUNDLE_INST_MAX = 1,
};

#include "../codegen_impl.h"

static void mach_dsl_init(Ctx* restrict ctx, TB_ABI abi) {
    ctx->num_regs[REG_CLASS_GPR] = 32;
    ctx->num_regs[REG_CLASS_FPR] = 32;
    ctx->num_regs[REG_CLASS_FLAGS] = 1;
}

static TB_Node* mach_dfa_accept_1(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_ICONST, $dt, 1, sizeof(TB_NodeInt));
    set_input(f, k0, f->root_node, k0_i++);
    TB_NodeInt* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->value = tb_data_type_bit_size(f->super.module, $dt.type);
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_SUB, $dt, 3, sizeof(TB_NodeBinopInt));
    k1_i++;
    set_input(f, k1, k0, k1_i++);
    set_input(f, k1, $rhs, k1_i++);
    TB_NodeBinopInt* k1_extra = TB_NODE_GET_EXTRA(k1);
    switch (hash) {
    }
    k1 = tb_opt_gvn_node(f, k1);
    
    size_t k2_i = 0;
    TB_Node* k2 = tb_alloc_node(f, TB_ROR, $dt, 3, sizeof(TB_NodeBinopInt));
    k2_i++;
    set_input(f, k2, $lhs, k2_i++);
    set_input(f, k2, k1, k2_i++);
    TB_NodeBinopInt* k2_extra = TB_NODE_GET_EXTRA(k2);
    switch (hash) {
    }
    k2 = tb_opt_gvn_node(f, k2);
    
    return k2;
}
static TB_Node* mach_dfa_accept_2(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = n->dt;

    int k0_type = 0;
    TB_DataType k0_dt = TB_TYPE_VOID;
    switch (hash) {
        case 80: k0_type = TB_UDIV, k0_dt = $dt; break;
        case 81: k0_type = TB_SDIV, k0_dt = $dt; break;
        default: abort(); return NULL;
    }
    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, k0_type, k0_dt, 3, sizeof(TB_NodeBinopInt));
    k0_i++;
    set_input(f, k0, $lhs, k0_i++);
    set_input(f, k0, $rhs, k0_i++);
    TB_NodeBinopInt* k0_extra = TB_NODE_GET_EXTRA(k0);
    switch (hash) {
    }
    k0 = tb_opt_gvn_node(f, k0);
    
    size_t k1_i = 0;
    TB_Node* k1 = tb_alloc_node(f, TB_MUL, $dt, 3, sizeof(TB_NodeBinopInt));
    k1_i++;
    set_input(f, k1, $rhs, k1_i++);
    set_input(f, k1, k0, k1_i++);
    TB_NodeBinopInt* k1_extra = TB_NODE_GET_EXTRA(k1);
    switch (hash) {
    }
    k1 = tb_opt_gvn_node(f, k1);
    
    size_t k2_i = 0;
    TB_Node* k2 = tb_alloc_node(f, TB_SUB, $dt, 3, sizeof(TB_NodeBinopInt));
    k2_i++;
    set_input(f, k2, $lhs, k2_i++);
    set_input(f, k2, k1, k2_i++);
    TB_NodeBinopInt* k2_extra = TB_NODE_GET_EXTRA(k2);
    switch (hash) {
    }
    k2 = tb_opt_gvn_node(f, k2);
    
    return k2;
}
static TB_Node* mach_dfa_accept_3(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_a64_COND, TB_TYPE_I8, 3, sizeof(A64Cond));
    k0_i++;
    set_input(f, k0, $lhs, k0_i++);
    set_input(f, k0, $rhs, k0_i++);
    A64Cond* k0_extra = TB_NODE_GET_EXTRA(k0);
    switch (hash) {
        case 88: k0_extra->cond = A64_EQ, k0_extra->extra_dt = $dt; break;
        case 89: k0_extra->cond = A64_NE, k0_extra->extra_dt = $dt; break;
        case 92: k0_extra->cond = A64_LT, k0_extra->extra_dt = $dt; break;
        case 93: k0_extra->cond = A64_LE, k0_extra->extra_dt = $dt; break;
        case 90: k0_extra->cond = A64_CC, k0_extra->extra_dt = $dt; break;
        case 91: k0_extra->cond = A64_LS, k0_extra->extra_dt = $dt; break;
    }
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_4(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_a64_COND, TB_TYPE_I8, 3, sizeof(A64Cond));
    k0_i++;
    set_input(f, k0, $lhs, k0_i++);
    set_input(f, k0, $rhs, k0_i++);
    A64Cond* k0_extra = TB_NODE_GET_EXTRA(k0);
    switch (hash) {
        case 88: k0_extra->cond = A64_EQ, k0_extra->extra_dt = $dt; break;
        case 89: k0_extra->cond = A64_NE, k0_extra->extra_dt = $dt; break;
        case 94: k0_extra->cond = A64_MI, k0_extra->extra_dt = $dt; break;
        case 95: k0_extra->cond = A64_LS, k0_extra->extra_dt = $dt; break;
    }
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_5(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $base = n->inputs[1];
    TB_Node* $off = n->inputs[2];

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_a64_add_imm12, TB_TYPE_PTR, 2, sizeof(A64Imm12));
    k0_i++;
    set_input(f, k0, $base, k0_i++);
    A64Imm12* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->imm = (uint32_t)a64_as_int64($off);
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_6(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = n->dt;

    int k0_type = 0;
    TB_DataType k0_dt = TB_TYPE_VOID;
    switch (hash) {
        case 73: k0_type = TB_a64_lsl_imm, k0_dt = $dt; break;
        case 74: k0_type = TB_a64_lsr_imm, k0_dt = $dt; break;
        case 75: k0_type = TB_a64_asr_imm, k0_dt = $dt; break;
        default: abort(); return NULL;
    }
    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, k0_type, k0_dt, 2, sizeof(A64Imm12));
    k0_i++;
    set_input(f, k0, $lhs, k0_i++);
    A64Imm12* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->imm = (uint32_t)a64_as_int64($rhs);
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static TB_Node* mach_dfa_accept_7(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_a64_COND_IMM, TB_TYPE_I8, 2, sizeof(A64Cond));
    k0_i++;
    set_input(f, k0, $lhs, k0_i++);
    A64Cond* k0_extra = TB_NODE_GET_EXTRA(k0);
    switch (hash) {
        case 88: k0_extra->cond = A64_EQ, k0_extra->extra_dt = $dt, k0_extra->imm = a64_as_int64($rhs); break;
        case 89: k0_extra->cond = A64_NE, k0_extra->extra_dt = $dt, k0_extra->imm = a64_as_int64($rhs); break;
        case 92: k0_extra->cond = A64_LT, k0_extra->extra_dt = $dt, k0_extra->imm = a64_as_int64($rhs); break;
        case 93: k0_extra->cond = A64_LE, k0_extra->extra_dt = $dt, k0_extra->imm = a64_as_int64($rhs); break;
        case 90: k0_extra->cond = A64_CC, k0_extra->extra_dt = $dt, k0_extra->imm = a64_as_int64($rhs); break;
        case 91: k0_extra->cond = A64_LS, k0_extra->extra_dt = $dt, k0_extra->imm = a64_as_int64($rhs); break;
    }
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_8(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $x = n->inputs[1];
    TB_DataType $dt = n->dt;

    return a64_multi_way_branch(ctx, f, n);
}
static TB_Node* mach_dfa_accept_9(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $x = n->inputs[1];
    float $prob = TB_NODE_GET_EXTRA_T(n, TB_NodeIf)->prob;
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_a64_jcc_imm, TB_TYPE_TUPLE, 2, sizeof(A64Cond));
    set_input(f, k0, $ctrl, k0_i++);
    set_input(f, k0, $x, k0_i++);
    A64Cond* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->cond = A64_NE, k0_extra->extra_dt = n->inputs[1]->dt, k0_extra->imm = 0, k0_extra->prob = $prob;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_10(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;


    return a64_isel_if_branch(ctx, f, n);
}

static TB_Node* mach_dfa_accept_11(Ctx* ctx, TB_Function* f, TB_Node* n) {
    uint64_t hash = 0;

    TB_Node* $pred = n->inputs[1];
    TB_Node* $a = n->inputs[2];
    TB_Node* $b = n->inputs[3];
    TB_DataType $dt = n->dt;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_a64_csel, $dt, 4, sizeof(A64Cond));
    k0_i++;
    set_input(f, k0, $pred, k0_i++);
    set_input(f, k0, $a, k0_i++);
    set_input(f, k0, $b, k0_i++);
    A64Cond* k0_extra = TB_NODE_GET_EXTRA(k0);
    k0_extra->cond = A64_NE;
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}
static MatchRuleID mach_dfa_rule_1(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    // { 76, 80, 81, 88, 89, 92, 93, 90, 91, 88, 89, 94, 95 }
    switch (hash) {
        default:
        return 0;
        // (ANY_OP ___ $lhs (ANY_OP ___ (ANY_OP f->root_node) $rhs))
        case 76: {
            {
                TB_DataType $dt = n->dt;
                return 1;
            }
        }
        // (ANY_OP ___ $lhs (ANY_OP ___ $rhs (ANY_OP ___ $lhs $rhs)))
        case 80: case 81: {
            {
                TB_DataType $dt = n->dt;
                return 2;
            }
        }
        // (ANY_OP ___ $lhs $rhs)
        case 88: case 89: case 92: case 93: case 90: case 91: case 94: case 95: {
            {
                TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;
                if (TB_IS_BOOL_INT_PTR($dt)) { return 3; }
            }
            {
                TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;
                if (TB_IS_FLOAT_TYPE($dt)) { return 4; }
            }
            return 0;
        }
    }
}

static MatchRuleID mach_dfa_rule_2(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $base = n->inputs[1];
    TB_Node* $off = n->inputs[2];
    uint64_t hash = 0;

    // { 0 }
    {
        if (a64_fits_addsub_imm(a64_as_int64($off))) { return 5; }
    }
    return 0;
}

static MatchRuleID mach_dfa_rule_3(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    // { 73, 74, 75, 88, 89, 92, 93, 90, 91 }
    switch (hash) {
        default:
        return 0;
        // (ANY_OP ___ $lhs)
        case 73: case 74: case 75: case 88: case 89: case 92: case 93: case 90: case 91: {
            {
                TB_DataType $dt = n->dt;
                if (a64_shift_imm_ok(f, $dt, $rhs)) { return 6; }
            }
            {
                TB_DataType $dt = TB_NODE_GET_EXTRA_T(n, TB_NodeCompare)->cmp_dt;
                if (TB_IS_BOOL_INT_PTR($dt) && a64_fits_addsub_imm(a64_as_int64($rhs))) { return 7; }
            }
            return 0;
        }
    }
}

static MatchRuleID mach_dfa_rule_4(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $ctrl = n->inputs[0];
    TB_Node* $x = n->inputs[1];
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    // { 17, 18 }
    switch (hash) {
        default:
        return 0;
        // a64_multi_way_branch(ctx, f, n)
        case 17: {
            {
                TB_DataType $dt = n->dt;
                return 8;
            }
        }
        // (ANY_OP $ctrl $x)
        case 18: {
            {
                float $prob = TB_NODE_GET_EXTRA_T(n, TB_NodeIf)->prob;
                TB_DataType $dt = n->dt;
                return 9;
            }
        }
    }
}

static MatchRuleID mach_dfa_rule_5(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    // { 18, 19 }
    switch (hash) {
        default:
        return 0;
        // a64_isel_if_branch(ctx, f, n)
        case 18: case 19: {
            {
                return 10;
            }
        }
    }
}

static MatchRuleID mach_dfa_rule_6(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $pred = n->inputs[1];
    TB_Node* $a = n->inputs[2];
    TB_Node* $b = n->inputs[3];
    uint64_t hash = 0;

    // { 0 }
    {
        TB_DataType $dt = n->dt;
        return 11;
    }
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
        default: return RULE_NONE;
    }
}

#define RULE(x) mach_dfa_rule_ ## x(ctx, f, n, depth)
static MatchRuleID node_isel_raw(Ctx* ctx, TB_Function* f, Set* shared, TB_Node* n, int depth) {
    MatchRuleID k;
    switch (n->type) {
        case TB_ROL:
        case TB_UMOD:
        case TB_SMOD:
        case TB_CMP_FLT:
        case TB_CMP_FLE:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            if (n->inputs[0] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[1], "Step");
            // MATCH ANY n->inputs[1]
            DFA_LOG(depth+1, n->inputs[2], "Step");
            // MATCH ANY n->inputs[2]
            return (n->input_count == 3 ? RULE(1) : 0);
        }
        case TB_PTR_OFFSET:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            if (n->inputs[0] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[1], "Step");
            // MATCH ANY n->inputs[1]
            DFA_LOG(depth+1, n->inputs[2], "Step");
            TB_Node* n__0 = n->inputs[2];
            DFA_LOG2(depth+2, "TRY 'ICONST'");
            do { if (n__0 && n__0->type == TB_ICONST && !set_get(shared, n__0->gvn)) {
                k = (n->input_count == 3 ? RULE(2) : 0);
                if (k) { return k; }
            } } while (0);

            break;
        }
        case TB_SHL:
        case TB_SHR:
        case TB_SAR:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            if (n->inputs[0] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[1], "Step");
            // MATCH ANY n->inputs[1]
            DFA_LOG(depth+1, n->inputs[2], "Step");
            TB_Node* n__1 = n->inputs[2];
            DFA_LOG2(depth+2, "TRY 'ICONST'");
            do { if (n__1 && n__1->type == TB_ICONST && !set_get(shared, n__1->gvn)) {
                k = (n->input_count == 3 ? RULE(3) : 0);
                if (k) { return k; }
            } } while (0);

            break;
        }
        case TB_BRANCH:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            // MATCH ANY n->inputs[0]
            DFA_LOG(depth+1, n->inputs[1], "Step");
            // MATCH ANY n->inputs[1]
            return (n->input_count == 2 ? RULE(4) : 0);
        }
        case TB_CMP_EQ:
        case TB_CMP_NE:
        case TB_CMP_SLT:
        case TB_CMP_SLE:
        case TB_CMP_ULT:
        case TB_CMP_ULE:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            if (n->inputs[0] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[1], "Step");
            // MATCH ANY n->inputs[1]
            DFA_LOG(depth+1, n->inputs[2], "Step");
            TB_Node* n__2 = n->inputs[2];
            DFA_LOG2(depth+2, "TRY 'ICONST'");
            do { if (n__2 && n__2->type == TB_ICONST && !set_get(shared, n__2->gvn)) {
                k = (n->input_count == 3 ? RULE(3) : 0);
                if (k) { return k; }
            } } while (0);

            return (n->input_count == 3 ? RULE(1) : 0);
        }
        case TB_IF:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            // MATCH ANY n->inputs[0]
            DFA_LOG(depth+1, n->inputs[1], "Step");
            // MATCH ANY n->inputs[1]
            return (n->input_count == 2 ? RULE(4) : RULE(5));
        }
        case TB_AFFINE_LATCH:
        {
            return RULE(5);
        }
        case TB_SELECT:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            if (n->inputs[0] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[1], "Step");
            // MATCH ANY n->inputs[1]
            DFA_LOG(depth+1, n->inputs[2], "Step");
            // MATCH ANY n->inputs[2]
            DFA_LOG(depth+1, n->inputs[3], "Step");
            // MATCH ANY n->inputs[3]
            return (n->input_count == 4 ? RULE(6) : 0);
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
};
#undef ACCEPT
