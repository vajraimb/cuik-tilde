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

    TB_Node* $root = n->inputs[0];
    float $value = TB_NODE_GET_EXTRA_T(n, TB_NodeFloat32)->value;

    size_t k0_i = 0;
    TB_Node* k0 = tb_alloc_node(f, TB_ICONST, TB_TYPE_I32, 1, sizeof(TB_NodeInt));
    set_input(f, k0, $root, k0_i++);
    TB_NodeInt* k0_extra = TB_NODE_GET_EXTRA(k0);
    switch (hash) {
        case 2: k0_extra->value = *(uint32_t*) &$value; break;
        case 3: k0_extra->value = *(uint64_t*) &$value; break;
    }
    k0 = tb_opt_gvn_node(f, k0);
    
    return k0;
}

static TB_Node* mach_dfa_accept_2(Ctx* ctx, TB_Function* f, TB_Node* n) {
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
static TB_Node* mach_dfa_accept_3(Ctx* ctx, TB_Function* f, TB_Node* n) {
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
static MatchRuleID mach_dfa_rule_1(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $root = n->inputs[0];
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    // { 2, 3 }
    switch (hash) {
        default:
        return 0;
        // (ANY_OP $root)
        case 2: case 3: {
            {
                float $value = TB_NODE_GET_EXTRA_T(n, TB_NodeFloat32)->value;
                return 1;
            }
        }
    }
}

static MatchRuleID mach_dfa_rule_2(Ctx* ctx, TB_Function* f, TB_Node* n, int depth) {
    TB_Node* $lhs = n->inputs[1];
    TB_Node* $rhs = n->inputs[2];
    uint64_t hash = 0;
    hash = hash*31 + n->type;

    // { 76, 80, 81 }
    switch (hash) {
        default:
        return 0;
        // (ANY_OP ___ $lhs (ANY_OP ___ (ANY_OP f->root_node) $rhs))
        case 76: {
            {
                TB_DataType $dt = n->dt;
                return 2;
            }
        }
        // (ANY_OP ___ $lhs (ANY_OP ___ $rhs (ANY_OP ___ $lhs $rhs)))
        case 80: case 81: {
            {
                TB_DataType $dt = n->dt;
                return 3;
            }
        }
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
        case TB_F32CONST:
        case TB_F64CONST:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            // MATCH ANY n->inputs[0]
            return (n->input_count == 1 ? RULE(1) : 0);
        }
        case TB_ROL:
        case TB_UMOD:
        case TB_SMOD:
        {
            DFA_LOG(depth+1, n->inputs[0], "Step");
            if (n->inputs[0] != NULL) { break; }
            DFA_LOG(depth+1, n->inputs[1], "Step");
            // MATCH ANY n->inputs[1]
            DFA_LOG(depth+1, n->inputs[2], "Step");
            // MATCH ANY n->inputs[2]
            return (n->input_count == 3 ? RULE(2) : 0);
        }
    }
    return 0;
}

static MatchRule match_rules[] = {
    NULL,
    mach_dfa_accept_1,
    mach_dfa_accept_2,
    mach_dfa_accept_3,
};
#undef ACCEPT
