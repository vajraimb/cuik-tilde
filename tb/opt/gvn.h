#include "../tb_internal.h"

static size_t extra_bytes(TB_Node* n) {
    if (n->type == TB_VSHUFFLE) {
        TB_NodeVShuffle* v = TB_NODE_GET_EXTRA(n);
        return sizeof(TB_NodeVShuffle) + (v->width * sizeof(int));
    }

    return tb_node_extra_bytes(n->type);
}

static uint32_t gvn_hash_bytes(const void* data, size_t len) {
    uint32_t h = 0x811C9DC5;
    for (size_t i = 0; i < len; i++) {
        h = (((const uint8_t*) data)[i] ^ h) * 0x01000193;
    }
    return h;
}

uint32_t gvn_hash(void* a) {
    TB_Node* n = a;
    size_t extra = extra_bytes(n);
    uint32_t h = n->type + n->dt.raw + n->input_count + extra;

    // locals can't be put into the GVN table
    TB_ASSERT(n->type != TB_LOCAL);
    FOR_N(i, 0, n->input_count) {
        h += n->inputs[i] ? n->inputs[i]->gvn : 0;
    }

    // The pool's slot layout follows these hashes, so they may only depend
    // on the IR. A TB_SYMBOL's extra is the address of its TB_Symbol, which
    // differs on every run and reshuffles the whole table, and with it which
    // entries the probe for a node whose inputs were changed in place (and
    // that therefore sits in the pool under a stale hash) happens to reach.
    // Symbol names are unique per module, so hashing the name is equivalent
    // to the pointer comparison gvn_compare does.
    if (n->type == TB_SYMBOL || n->type == TB_MACH_SYMBOL) {
        TB_Symbol* sym = TB_NODE_GET_EXTRA_T(n, TB_NodeSymbol)->sym;
        h += sym ? gvn_hash_bytes(sym->name, sym->name_length) : 0;
        return ((uint64_t) h * 11400714819323198485llu) >> 32llu;
    }

    // fnv1a the extra space
    uint32_t* extra_arr = (uint32_t*) n->extra;
    FOR_N(i, 0, extra / 4) {
        h += extra_arr[i];
    }

    FOR_N(i, extra & ~0x7, extra) {
        h += n->extra[i];
    }

    // fib hashing amirite
    return ((uint64_t) h * 11400714819323198485llu) >> 32llu;
}

bool gvn_compare(void* a, void* b) {
    TB_Node *x = a, *y = b;

    // early outs
    if (x->type != y->type || x->input_count != y->input_count || x->dt.raw != y->dt.raw) {
        return false;
    }

    // match up inputs
    FOR_N(i, 0, x->input_count) {
        if (x->inputs[i] != y->inputs[i]) {
            return false;
        }
    }

    size_t extra = extra_bytes(x);
    return extra == 0 || memcmp(x->extra, y->extra, extra) == 0;

    #if 0
    bool skip_ctrl = x->type == TB_LOAD;
    if (skip_ctrl) {
        if (extra && memcmp(x->extra, y->extra, extra) != 0) {
            return false;
        }

        // if either ctrl doms the other, these two loads match
        if (fast_dommy(y->inputs[0], x->inputs[0])) {
            x->inputs[0] = y->inputs[0];
            return true;
        }

        if (fast_dommy(x->inputs[0], y->inputs[0])) {
            return true;
        }

        return false;
    } else {
        return extra == 0 || memcmp(x->extra, y->extra, extra) == 0;
    }
    #endif
}
