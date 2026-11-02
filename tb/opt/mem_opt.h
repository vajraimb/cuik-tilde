// Certain aliasing optimizations technically count as peepholes lmao, these can get fancy
// so the sliding window notion starts to break down but there's no global analysis and
// i can make them incremental technically so we'll go wit it.
static TB_Node* identity_load(TB_Function* f, TB_Node* n) {
    // god i need a pattern matcher
    //   (load (store X A Y) A) => Y
    TB_Node *mem = n->inputs[1], *addr = n->inputs[2];
    if (mem->type == TB_STORE && mem->inputs[2] == addr &&
        n->dt.raw == mem->inputs[3]->dt.raw) {
        // PATCH(odin-tilde): forwarding is only sound if the store dominates
        // the load. After tb_opt_locals' SPLITMEM/PROJ rewiring (and peeps
        // iterating on the result) a load's mem input can be a same-address
        // store from a sibling branch that never executes on the load's path
        // (bufio@writer_write's b.err load picking up the .Negative_Write=11
        // store from the m<0 error arm at stage>=1, silently turning the
        // normal-return path into an unconditional error return — no output).
        TB_Node* store_ctrl = mem->inputs[0];
        TB_Node* load_ctrl  = n->inputs[0];
        if (store_ctrl == NULL || (load_ctrl != NULL && fast_dommy(store_ctrl, load_ctrl))) {
            return mem->inputs[3];
        }
    }

    return n;
}

static TB_Node* ideal_return(TB_Function* f, TB_Node* n) {
    return NULL;
}

