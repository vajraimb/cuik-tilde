
static int bbb;

// "Register Spilling and Live-Range Splitting for SSA-Form Programs" Sebastian Hack, Matthias Braun (2009)
typedef struct {
    size_t old_node_count;
    size_t num_spills;
    ArenaArray(TB_Node*) all_phis;
    ArenaArray(TB_Node*) all_defs;

    uint64_t single_def;
    uint64_t remat_all;

    TB_Node** leaders;

    uint64_t* W_entry;
    uint64_t* W_exit;

    uint64_t* live_in;
    uint64_t* live_out;

    RegMask** spill_mask;
    RegMask** reload_mask;

    int* spill_vreg_id;
    int* was_spilled;
    uint64_t* avoid_mask; // PATCH(odin-tilde): lineage avoid accumulation

    NL_Table spill_map;
    NL_Table uses_spill;
} RegSplitter;

// PATCH(odin-tilde): a stack pin may only be applied to a vreg whose
// entire def class is slot-capable (copies/phis/projections/temps). A
// real op sharing the vreg cannot live in a slot — the op cannot write
// one and the emitter has no reg-dst lowering for it (odin-tank
// runtime@_make_dynamic_array_len_cap: a cmovcc def in a spill class
// pinned to STACK31, inst2 assert). all_defs MUST be scanned: class
// members are mapped to the class vreg only at the end of this pass, so
// a vreg_map scan alone misses them (and gvn2node is stale/NULL for
// split-created nodes).
static bool spill_pin_is_pure(Ctx* ctx, Rogers* ra, RegSplitter* splitter, int vreg_id, TB_Node* n) {
    size_t cnt;
    TB_Node** arr = coalesce_set_array(ctx, ra, &n, &cnt);
    FOR_N(i, 0, cnt) {
        int t = arr[i]->type;
        if (t != TB_MACH_COPY && t != TB_PHI && t != TB_MACH_PROJ && t != TB_MACH_TEMP) return false;
    }
    aarray_for(i, splitter->all_defs) {
        TB_Node* m = splitter->all_defs[i];
        int t = m->type;
        if (t == TB_MACH_COPY || t == TB_PHI || t == TB_MACH_PROJ || t == TB_MACH_TEMP || t == TB_NULL) continue;
        int ml = uf_find(ra->uf, ra->uf_len, m->gvn);
        if (ctx->vreg_map[ml] == vreg_id) return false;
    }
    size_t vmap_len = aarray_length(ctx->vreg_map);
    size_t g2n_cap = ra->order_cap; // gvn2node is only order_cap long
    for (size_t mi = 0; mi < vmap_len && mi < g2n_cap; mi++) {
        if (ctx->vreg_map[mi] != vreg_id) continue;
        TB_Node* mn = ra->gvn2node[mi];
        if (mn == NULL) continue;
        int t = mn->type;
        if (t != TB_MACH_COPY && t != TB_PHI && t != TB_MACH_PROJ && t != TB_MACH_TEMP && t != TB_NULL) return false;
    }
    return true;
}

typedef struct {
    int head, tail;
} SplitterLoop;

static TB_Node* clone_node(TB_Function* f, TB_Node* n, size_t extra) {
    TB_Node* clone = tb_alloc_node(f, n->type, n->dt, n->input_count, extra);
    memcpy(clone->extra, n->extra, extra);
    FOR_N(j, 0, n->input_count) if (n->inputs[j]) {
        clone->inputs[j] = n->inputs[j];
        add_user(f, clone, n->inputs[j], j);
    }
    return clone;
}

static int spill_map_get2(NL_Table* spill_map, TB_Node* n) {
    intptr_t p = (intptr_t) nl_table_get(spill_map, n);
    return p - 1;
}

static bool should_skip_over(TB_Node* n) {
    return IS_PROJ(n) || n->type == TB_PHI || n->type == TB_MACH_FRAME_PTR;
}

static bool is_in_stack(Ctx* ctx, TB_Node* n, bool W) {
    if (n->type == TB_PHI) {
        return !W;
    }

    RegMask* def_mask = ctx->constraint(ctx, n, NULL);
    return reg_mask_is_stack(def_mask);
}

static TB_Node* alloc_spill_node(Ctx* ctx, Rogers* ra, RegMask* spill_mask, TB_Node* def) {
    TB_Function* f = ctx->f;
    RegMask* def_mask = ctx->constraint(ctx, def, NULL);
    TB_ASSERT(def_mask->class != REG_CLASS_STK);

    // remove may_spill if it was there
    if (def_mask->may_spill) {
        TB_ASSERT(def_mask->count == 1);
        def_mask = intern_regmask(ctx, def_mask->class, false, def_mask->mask[0]);
    }

    TB_Node* cpy = tb_alloc_node(f, TB_MACH_COPY, def->dt, 2, sizeof(TB_NodeMachCopy));
    set_input(f, cpy, def, 1);
    TB_NODE_SET_EXTRA(cpy, TB_NodeMachCopy, .def = spill_mask, .use = def_mask);
    return cpy;
}

static void insert_op_at_end(Ctx* ctx, Rogers* ra, TB_BasicBlock* bb, TB_Node* n) {
    int pos = aarray_length(bb->items);
    TB_Node* last = bb->items[pos - 1];
    if (IS_PROJ(last)) { last = last->inputs[0]; }
    if (tb_node_is_terminator(last)) {
        pos--;

        while (pos > 0 && bb->items[pos] != bb->start && IS_PROJ(bb->items[pos])) {
            pos--;
        }
    }

    size_t bb_id = bb - ctx->cfg.blocks;
    rogers_insert_op(ctx, bb_id, n, pos);

    if (ra != NULL) {
        // move up if necessary
        FOR_N(class, 1, ctx->num_classes) {
            ra->hrp[bb_id].start[class] += pos <= ra->hrp[bb_id].start[class];
            ra->hrp[bb_id].end[class] += pos <= ra->hrp[bb_id].end[class];
        }
    }
}

static TB_Node* find_existing_copy(Ctx* ctx, Rogers* ra, TB_BasicBlock* bb, TB_Node* src, RegMask* rm) {
    int i = aarray_length(bb->items) - 1;
    if (bb->items[i] == bb->end) {
        i--;
    }

    while (i > 0 && bb->items[i]->type == TB_MACH_COPY) {
        RegMask* cpy_rm = TB_NODE_GET_EXTRA_T(bb->items[i], TB_NodeMachCopy)->def;
        if (tb__reg_mask_meet(ctx, cpy_rm, rm) == rm && bb->items[i]->inputs[1] == src) {
            return bb->items[i];
        }
        i--;
    }
    return NULL;
}

static void assign_spill_vreg(Ctx* ctx, Rogers* ra, RegSplitter* splitter, TB_Node* n, int spill) {
    RegMask* spill_mask = splitter->spill_mask[spill];
    int vreg_id = splitter->spill_vreg_id[spill];

    // TODO(NeGate): not the fastest way to approach this, consider optimizing in the future
    rogers_uncoalesce(ctx, ra, n->gvn);

    // there's only one VReg for all the spill space
    if (vreg_id == 0) {
        VReg* new_vreg = tb__set_node_vreg(ctx, n);
        new_vreg->reg_width = tb__reg_width_from_dt(spill_mask->class, n->dt);
        new_vreg->mask = spill_mask;
        vreg_id = new_vreg - ctx->vregs;

        splitter->spill_vreg_id[spill] = vreg_id;
    } else {
        VReg* vreg = &ctx->vregs[vreg_id];
        int y = uf_find(ra->uf, ra->uf_len, n->gvn);
        rogers_coalesce(ctx, ra, vreg->n->gvn, y, vreg->n, n);
    }

    nl_table_put(&splitter->spill_map, n, (void*) ((uintptr_t) spill + 1));
    aarray_insert(ctx->vreg_map, n->gvn, vreg_id);
}

static TB_Node* insert_spill(Ctx* ctx, Rogers* ra, RegSplitter* splitter, TB_BasicBlock* bb, TB_Node* n, int spill, size_t* insert_pos) {
    RegMask* spill_mask = splitter->spill_mask[spill];
    TB_ASSERT_MSG(spill_mask, "cannot insert spill move for this kind of node");

    TB_Node* cpy = alloc_spill_node(ctx, ra, spill_mask, n);
    assign_spill_vreg(ctx, ra, splitter, cpy, spill);
    aarray_push(splitter->all_defs, cpy);

    if (insert_pos) {
        size_t t = *insert_pos, cnt = aarray_length(bb->items);
        while (t < cnt && should_skip_over(bb->items[t])) {
            t++;
        }
        size_t bb_id = bb - ctx->cfg.blocks;
        rogers_insert_op(ctx, bb_id, cpy, t);
        *insert_pos = t;

        // move up if necessary
        FOR_N(class, 1, ctx->num_classes) {
            ra->hrp[bb_id].start[class] += t <= ra->hrp[bb_id].start[class];
            ra->hrp[bb_id].end[class] += t <= ra->hrp[bb_id].end[class];
        }
    } else {
        insert_op_at_end(ctx, ra, bb, cpy);
    }

    TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%d: spill-store %%%u (of %%%u)\n", bb - ctx->cfg.blocks, spill, cpy->gvn, n->gvn));
    return cpy;
}

static void add_splitter_def(RegSplitter* splitter, TB_Node* n) {
    aarray_for(i, splitter->all_defs) {
        if (splitter->all_defs[i] == n) { return; }
    }
    aarray_push(splitter->all_defs, n);
}

static bool has_immediate_use(TB_Node* n, TB_Node* of) {
    FOR_N(i, 1, n->input_count) {
        if (n->inputs[i] == of) {
            return true;
        }
    }
    return false;
}

static void mark_stretched(Ctx* ctx, Rogers* ra, RegSplitter* splitter, TB_Node* n) {
    if (ctx->vreg_map[n->gvn] > 0) {
        VReg* vreg = &ctx->vregs[ctx->vreg_map[n->gvn]];
        vreg->class = 0;
        vreg->assigned = -1;
        dyn_array_put(ra->prio_alloc, n);
    }
}

static TB_Node* insert_reload(Ctx* ctx, Rogers* ra, RegSplitter* splitter, TB_BasicBlock* bb, TB_Node** defs, TB_Node** bb_defs, int spill, uint64_t W, size_t* insert_pos) {
    TB_Function* f = ctx->f;
    size_t bb_id = bb - ctx->cfg.blocks;

    TB_Node* n = bb_defs[spill];
    if (n == NULL) {
        TB_ASSERT((splitter->remat_all >> spill) & 1);
        n = splitter->leaders[spill];
    }

    // PATCH(odin-tilde): a remat_all range is available everywhere by
    // rematerialization — the splitter builds no phis for it and does NOT
    // guarantee a spill store dominating this block. If bb_defs[spill] is a
    // spill-store copy that only some predecessor paths wrote, a slot reload
    // here reads an unwritten slot on the other paths (fmt@fmt_value crashed
    // on exactly this: a reload of STACK89 whose only store sat in a sibling
    // branch). Unwrap copy chains back to the remat-able producer so the
    // remat branch below clones it next to this use instead.
    if ((splitter->remat_all >> spill) & 1) {
        TB_Node* src = n;
        while (src->type == TB_MACH_COPY && src->inputs[1] != NULL) {
            src = src->inputs[1];
        }
        if (!is_spill_store(src) && can_remat(ctx, src)) {
            n = src;
        }
    }

    TB_Node* cpy;
    if (!is_spill_store(n) && can_remat(ctx, n)) {
        size_t extra = extra_bytes(n);
        cpy = clone_node(f, n, extra);
        TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%d: remat %%%u (of %%%u)\n", bb_id, spill, cpy->gvn, n->gvn));

        // if the inputs to the cloned node refer to spilled nodes, we update the edges
        FOR_N(k, 1, cpy->input_count) {
            TB_Node* in = cpy->inputs[k];
            if (in == NULL) { continue; }
            // If we're a known single-def, just don't bother cloning
            if (in->gvn < splitter->old_node_count) {
                size_t cnt;
                coalesce_set_array(ctx, ra, &in, &cnt);
                if (cnt <= 1) {
                    mark_stretched(ctx, ra, splitter, in);
                    continue;
                }
            }
            // We can stretch the initial un-copied form for better results
            if (is_spill_store(in)) {
                in = in->inputs[1];
            }

            if (in->gvn < splitter->old_node_count) {
                mark_stretched(ctx, ra, splitter, in);
            }

            if (f->scheduled[in->gvn] != bb) {
                RegMask* rm = ctx->constraint(ctx, in, NULL);
                TB_Node* reuse = find_existing_copy(ctx, ra, f->scheduled[in->gvn], in, rm);

                // printf("ASKING %%%u %p %p\n", in->gvn, rm, reuse);
                if (reuse != NULL) {
                    set_input(f, cpy, reuse, k);
                } else {
                    // Copy "in" value
                    TB_Node* in_cpy = tb_alloc_node(f, TB_MACH_COPY, in->dt, 2, sizeof(TB_NodeMachCopy));
                    set_input(f, in_cpy, in, 1);
                    TB_NODE_SET_EXTRA(in_cpy, TB_NodeMachCopy, .def = rm, .use = rm);

                    // Place within the in's block
                    insert_op_at_end(ctx, ra, f->scheduled[in->gvn], in_cpy);
                    aarray_push(splitter->all_defs, in_cpy);

                    // TODO(NeGate): we wanna avoid making too many copies so maybe collapse some?
                    set_input(f, cpy, in_cpy, k);
                }
            }
        }
    } else {
        cpy = tb_alloc_node(f, TB_MACH_COPY, n->dt, 2, sizeof(TB_NodeMachCopy));
        set_input(f, cpy, n, 1);
        TB_NODE_SET_EXTRA(cpy, TB_NodeMachCopy, .def = splitter->reload_mask[spill], .use = splitter->spill_mask[spill]);

        TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%d: reload %%%u (of %%%u)\n", bb_id, spill, cpy->gvn, n->gvn));
    }
    aarray_push(splitter->all_defs, cpy);

    if (insert_pos) {
        // should probably place it above any temps
        size_t t = *insert_pos;
        if (t < aarray_length(bb->items)) {
            while (t > 0 && bb->items[t]->type == TB_MACH_TEMP) {
                t--;
            }
        }
        rogers_insert_op(ctx, bb_id, cpy, t);
        *insert_pos = t;

        // move up if necessary
        FOR_N(class, 1, ctx->num_classes) {
            ra->hrp[bb_id].start[class] += t <= ra->hrp[bb_id].start[class];
            ra->hrp[bb_id].end[class] += t <= ra->hrp[bb_id].end[class];
        }
    } else {
        insert_op_at_end(ctx, ra, bb, cpy);
    }

    nl_table_put(&splitter->spill_map, cpy, (void*) ((uintptr_t) spill + 1));
    return cpy;
}

// PATCH(odin-tilde): mask of the register a split def currently resides in.
static RegMask* split_def_mask(Ctx* ctx, Rogers* ra, RegSplitter* splitter, TB_Node* def) {
    if (def->type == TB_MACH_COPY) {
        return TB_NODE_GET_EXTRA_T(def, TB_NodeMachCopy)->def;
    }
    if (ra->uf != NULL && def->gvn < splitter->old_node_count) {
        int leader = uf_find(ra->uf, ra->uf_len, def->gvn);
        int vid = leader < aarray_length(ctx->vreg_map) ? ctx->vreg_map[leader] : 0;
        if (vid > 0 && ctx->vregs[vid].mask != NULL) {
            return ctx->vregs[vid].mask;
        }
    }
    return ctx->constraint(ctx, def, NULL);
}

// PATCH(odin-tilde): implements the stock TODO at the use-reconnect site.
// After a clobber split, the resident def's mask can exclude a register a
// "tight" (fixed) use requires — e.g. a call argument pinned to RSI whose
// value was reloaded outside the clobbered set. Insert a bridging copy
// right before the use: reads the resident def, writes the pinned mask.
// Its live range is a single point so no clobber can span it.
static TB_Node* insert_bridge_copy(Ctx* ctx, Rogers* ra, RegSplitter* splitter, TB_BasicBlock* bb, size_t* j_ptr, TB_Node* def, RegMask* def_mask, TB_Node* n, int k, RegMask* in_mask) {
    TB_Function* f = ctx->f;
    size_t bb_id = bb - ctx->cfg.blocks;
    size_t j = *j_ptr;

    TB_Node* cpy = tb_alloc_node(f, TB_MACH_COPY, n->inputs[k]->dt, 2, sizeof(TB_NodeMachCopy));
    set_input(f, cpy, def, 1);
    TB_NODE_SET_EXTRA(cpy, TB_NodeMachCopy, .def = in_mask, .use = def_mask);
    aarray_push(splitter->all_defs, cpy);

    // place above any temps belonging to the use
    size_t t = j;
    while (t > 0 && bb->items[t]->type == TB_MACH_TEMP) {
        t--;
    }
    rogers_insert_op(ctx, bb_id, cpy, t);
    *j_ptr += t <= j;

    FOR_N(class, 1, ctx->num_classes) {
        ra->hrp[bb_id].start[class] += t <= ra->hrp[bb_id].start[class];
        ra->hrp[bb_id].end[class]   += t <= ra->hrp[bb_id].end[class];
    }

    TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: bridge %%%u (of %%%u) for pinned use %%%u:%d\n", bb_id, cpy->gvn, def->gvn, n->gvn, k));
    set_input(f, n, cpy, k);
    return cpy;
}

static void dump_split_state(TB_Node** defs, int bb_id, int num_spills, uint64_t W, uint64_t S) {
    printf("  BB%-3d: W=", bb_id);
    FOR_REV_N(j, 0, num_spills) {
        putchar((W >> j) & 1 ? '1' : '0');
    }
    /* printf(" S=");
    FOR_REV_N(j, 0, num_spills) {
        putchar((S >> j) & 1 ? '1' : '0');
    } */
    printf(" [ ");
    FOR_N(j, 0, num_spills) {
        if (defs[j]) {
            printf("%%%u ", defs[j]->gvn);
        } else {
            printf("___ ");
        }
    }
    printf("]\n");
}

// Index where we next start or end an HRP region
static int nearest_hrp_event(Ctx* ctx, Rogers* ra, int bb_id, int j) {
    int nearest = INT_MAX;
    FOR_N(class, 0, ctx->num_classes) {
        int start = ra->hrp[bb_id].start[class];
        int end   = ra->hrp[bb_id].end[class];

        if (start >= j && nearest > start) {
            nearest = start;
        }

        if (start >= 0 && end >= j && nearest > end) {
            nearest = end;
        }
    }

    return nearest;
}

static uint64_t update_phi_edges(Ctx* ctx, Rogers* ra, RegSplitter* splitter, uint64_t W, TB_Node** defs, TB_Node** phis, int bb_id, int pred_id, uint64_t all_spills, int pred_path, bool backedge) {
    TB_Function* f = ctx->f;
    int num_spills = splitter->num_spills;

    TB_Node* header = ctx->cfg.blocks[bb_id].start;
    int pred_count = header->type == TB_PROJ && header->inputs[0]->type == TB_ROOT ? 0 : header->input_count;

    TB_Node** bb_phis = &phis[bb_id*num_spills];
    TB_Node** bb_defs = &defs[bb_id*num_spills];
    TB_Node** pred_defs = &defs[pred_id*num_spills];

    // check if the forward edges differ, if they do we need phis
    FOR_N(k, 0, num_spills) {
        TB_Node* pred_def = pred_defs[k];

        if (!((splitter->live_out[pred_id] >> k) & 1)) {
            continue;
        }

        if (!((splitter->remat_all >> k) & 1) && pred_def) {
            bool is_stk     = is_in_stack(ctx, pred_def, (splitter->W_exit[pred_id] >> k) & 1);
            bool should_stk = (~W >> k) & 1;
            if (is_stk && !should_stk) {
                pred_def = insert_reload(ctx, ra, splitter, &ctx->cfg.blocks[pred_id], defs, pred_defs, k, splitter->W_exit[pred_id], NULL);
            } else if (!is_stk && should_stk && splitter->spill_mask[k]) {
                // spilling.
                // int spill = spill_map_get2(&splitter->spill_map, pred_def);
                pred_def = insert_spill(ctx, ra, splitter, &ctx->cfg.blocks[pred_id], pred_def, k, NULL);

                #if 0
                if (pred_def->type == TB_MACH_COPY && !reg_mask_is_stack(TB_NODE_GET_EXTRA_T(pred_def, TB_NodeMachCopy)->use)) {
                    if (pred_def->inputs[1]->type != TB_MACH_COPY && can_remat(ctx, pred_def->inputs[1])) {
                        pred_def = pred_def->inputs[1];
                        add_splitter_def(splitter, pred_def);
                        TB_OPTDEBUG(REGSPLIT)(printf("  BB%d: SPILL%zu: convert copy %%%u to initially rematerializable value %%%u\n", bb_id, k, pred_def->gvn, pred_def->inputs[1]->gvn));
                    } else {
                        TB_NODE_GET_EXTRA_T(pred_def, TB_NodeMachCopy)->def = splitter->spill_mask[k];
                        TB_OPTDEBUG(REGSPLIT)(printf("  BB%d: SPILL%zu: convert copy to spill-store %%%u\n", bb_id, k, pred_def->gvn));
                    }
                } else {
                    // If we're gonna spill a reload, let's just extend the existing spill
                    if (pred_def->gvn >= splitter->old_node_count && pred_def->type == TB_MACH_COPY && is_reload(pred_def)) {
                        TB_OPTDEBUG(REGSPLIT)(printf("  BB%d: SPILL%zu: don't spill %%%u (a reload), extend lifetime of existing spill %%%u\n", bb_id, k, pred_def->gvn, pred_def->inputs[1]->gvn));
                        pred_def = pred_def->inputs[1];
                        add_splitter_def(splitter, pred_def);
                    } else {
                    }
                }
                #endif
            }
        }

        if (backedge) {
            // we know there's a phi so there's no need for the rest of this goop
            if (bb_phis[k]) {
                // PATCH(odin-tilde): pred_def can be NULL when slot k has no
                // split def on this edge (the phi-input use itself sets the
                // live_out bit); keep the program-specified input instead of
                // writing NULL into the phi
                if (pred_def != NULL) {
                    set_input(f, bb_phis[k], pred_def, 1+pred_path);
                }
            }
        } else {
            if (bb_defs[k] == NULL) {
                bb_defs[k] = pred_def;
            } else if (bb_defs[k]->type == TB_PHI && bb_defs[k]->inputs[0] == header) {
                if (pred_def != NULL && bb_defs[k]->dt.type == TB_TAG_VOID) {
                    // just in case...
                    bb_defs[k]->dt = pred_def->dt;
                }
                // PATCH(odin-tilde): see above — never write NULL over the
                // phi's existing edge value
                if (pred_def != NULL) {
                    set_input(f, bb_defs[k], pred_def, 1+pred_path);
                }
            } else if (bb_defs[k] != pred_def) {
                // PATCH(odin-tilde): a remat_all merge may only take the
                // single reload path when the value genuinely
                // rematerializes from this def. After several rounds the
                // "single def" can be a reload/spill-store chain whose
                // producer is NOT remat-able (e.g. a param projection):
                // insert_reload then degrades to a slot reload reading the
                // CURRENT pred's store — a store that does not dominate
                // this block on the OTHER edges (ols request_initialize
                // SIGSEGV: BB450 read STACK677 written only in BB418).
                // Detect the dead end here and build a phi instead.
                bool can_remat_here = false;
                if (pred_def != NULL && ((splitter->remat_all >> k) & 1)) {
                    TB_Node* rsrc = pred_def;
                    while (rsrc->type == TB_MACH_COPY && rsrc->inputs[1] != NULL) {
                        rsrc = rsrc->inputs[1];
                    }
                    can_remat_here = !is_spill_store(rsrc) && can_remat(ctx, rsrc);
                }
                if (can_remat_here) {
                    // this value is single-def, there's no variants of it so no phis
                    size_t pos = 1;
                    bb_defs[k] = insert_reload(ctx, ra, splitter, &ctx->cfg.blocks[bb_id], defs, pred_defs, k, splitter->W_exit[bb_id], &pos);
                } else if (pred_def != NULL) {
                    // PATCH(odin-tilde): pred_def can be NULL here even though
                    // the slot is live_out of the pred (defs[] has no entry
                    // for it — same gap as the backedge/phi-edge cases
                    // patched above, reached with rotated-loop CFGs at
                    // stage>=1: __truncsfhf2). Converting a NULL def into a
                    // phi edge would write NULL inputs and crash codegen;
                    // keep the def gathered from the other preds instead.
                    // convert from single value to PHI
                    TB_Node* phi = phis[bb_id*num_spills + k];
                    if (phi == NULL) {
                        phi = tb_alloc_node(f, TB_PHI, pred_def->dt, 1 + pred_count, 0);
                        phis[bb_id*num_spills + k] = phi;
                        rogers_insert_op(ctx, bb_id, phi, 1);

                        // move up if necessary
                        FOR_N(class, 1, ctx->num_classes) {
                            ra->hrp[bb_id].start[class] += 1 <= ra->hrp[bb_id].start[class];
                            ra->hrp[bb_id].end[class] += 1 <= ra->hrp[bb_id].end[class];
                        }
                    }
                    set_input(f, phi, header, 0);
                    // all the edges up until this point were the same
                    TB_Node* same = bb_defs[k];
                    FOR_N(i, 0, pred_path) {
                        set_input(f, phi, same, 1+i);
                    }
                    set_input(f, phi, pred_def, 1+pred_path);
                    bb_defs[k] = phi;
                    nl_table_put(&splitter->spill_map, phi, (void*) (k + 1));

                    aarray_push(splitter->all_phis, phi);
                    TB_OPTDEBUG(REGSPLIT)(printf("  BB%d: SPILL%zu: phi %%%u\n", bb_id, k, phi->gvn));
                }
            }
        }
    }

    return W;
}

static void* arena_zalloc(TB_Arena* arena, size_t size) {
    void* ptr = tb_arena_alloc(arena, size);
    memset(ptr, 0, size);
    return ptr;
}

static void tb__insert_splits(Ctx* ctx, Rogers* restrict ra, SplitDecision* splits, size_t num_spills) {
    TB_Arena* arena = ra->arena;
    TB_Function* f = ctx->f;

    // bbb++;
    // tb_opt__REGSPLIT = bbb == 6 || bbb == 7;

    TB_OPTDEBUG(REGSPLIT)(printf("== INSERT NODES ==\n"));

    size_t old_node_count = f->node_count;
    cuikperf_region_start("alloc", NULL);

    // we can only spill 64 vregs at once due to some bitsets i don't feel like changing
    TB_ASSERT(num_spills <= 64);

    RegSplitter splitter = { 0 };
    splitter.old_node_count = old_node_count;
    splitter.num_spills = num_spills;
    splitter.spill_map = nl_table_alloc(num_spills);
    splitter.uses_spill = nl_table_alloc(num_spills);
    splitter.spill_mask = tb_arena_alloc(ra->arena, num_spills * sizeof(RegMask*));
    splitter.reload_mask = tb_arena_alloc(ra->arena, num_spills * sizeof(RegMask*));
    splitter.leaders = tb_arena_alloc(ra->arena, num_spills * sizeof(TB_Node*));

    splitter.spill_vreg_id = tb_arena_alloc(ra->arena, num_spills * sizeof(int));
    splitter.was_spilled   = tb_arena_alloc(ra->arena, num_spills * sizeof(int));
    splitter.avoid_mask    = tb_arena_alloc(ra->arena, num_spills * sizeof(uint64_t));

    uint64_t was_spilled_before = 0;
    // PATCH(odin-tilde): spill ids whose spill space is pinned to a pure
    // stack slot (non-clobber splits); see the recompute loop below.
    uint64_t stk_only = 0;
    uint64_t class2vreg[MAX_REG_CLASSES] = { 0 };
    FOR_N(i, 0, num_spills) {
        uint32_t vreg_id = splits[i].target;

        VReg* to_spill = &ctx->vregs[vreg_id];
        int class = to_spill->mask->class;
        TB_ASSERT(class > 0);
        class2vreg[class] |= 1ull << i;

        // the splitter cannot operate on stack nodes, they also
        // don't ever need "splitting"
        TB_ASSERT(!reg_mask_is_stack(to_spill->mask));
        // PATCH(odin-tilde): do NOT inherit the lineage avoid set: each
        // split only avoids the registers of its OWN clobber. Accumulating
        // the avoid set across split generations shrinks the usable mask
        // every round until the split loop cannot converge.
        splitter.avoid_mask[i]    = 0;
        splitter.reload_mask[i] = ctx->normie_mask[class];
        if (splits[i].clobber) {
            RegMask* clobber = splits[i].clobber;
            TB_ASSERT(ctx->num_regs[clobber->class] < 64);

            uint64_t total_mask = UINT64_MAX >> (64 - ctx->num_regs[clobber->class]);
            // avoid only THIS split's clobber registers (see above).
            splitter.avoid_mask[i] |= clobber->mask[0];
            uint64_t avoid = splitter.avoid_mask[i] & total_mask;
            splitter.spill_mask[i] = intern_regmask(ctx, clobber->class, true, ~avoid & total_mask);
            // PATCH(odin-tilde): reloads of a clobber-split must avoid the
            // clobbered registers too. Reloads are placed at block
            // granularity, so a reload can still span the clobbering call;
            // if it may take a clobbered register, the same conflict just
            // reappears on the next round and RA never converges.
            splitter.reload_mask[i] = splitter.spill_mask[i];
        } else {
            splitter.spill_mask[i] = ctx->mayspill_mask[class];
        }
        // PATCH(odin-tilde): pin every spill space with a usable spill
        // mask to a pure stack slot (clobber splits included — their
        // spill-stores were observed being colored into registers, which
        // breaks the splitter's slot-residency assumption exactly like
        // the non-clobber case). The reload mask keeps the clobber-avoid
        // semantics, only the store side is stack-pinned.
        if (splitter.spill_mask[i] != NULL) {
            stk_only |= 1ull << i;
        }
        splitter.spill_vreg_id[i] = 0;
        splitter.was_spilled[i]   = to_spill->was_spilled;

        if (to_spill->was_spilled) {
            was_spilled_before |= 1ull << i;
        }

        to_spill->mask = NULL;
        to_spill->spill_cost = NAN;

        TB_ASSERT(to_spill->n);
        splitter.leaders[i] = to_spill->n;

        size_t cnt;
        TB_Node** arr = coalesce_set_array(ctx, ra, &splitter.leaders[i], &cnt);
        FOR_N(j, 0, cnt) {
            nl_table_put(&splitter.spill_map, arr[j], (void*) (i + 1));

            FOR_USERS(u, arr[j]) {
                nl_table_put(&splitter.uses_spill, USERN(u), (void*) 1);
            }
        }
    }

    // [def_i*num_spills + spill_i]
    TB_Node** defs = arena_zalloc(ra->arena, num_spills * ctx->bb_count * sizeof(TB_Node*));
    TB_Node** phis = arena_zalloc(ra->arena, num_spills * ctx->bb_count * sizeof(TB_Node*));
    FOR_N(i, 0, num_spills * ctx->bb_count) {
        defs[i] = NULL;
        phis[i] = NULL;
    }

    // these are nodes which aggressively try to spill their entire range, generally because it's
    // not used throughout most of it until the end
    uint64_t spill_aggro = 0;
    splitter.single_def  = 0;

    uint64_t* def_map = arena_zalloc(ra->arena, ctx->bb_count * sizeof(uint64_t));
    uint64_t* use_map = arena_zalloc(ra->arena, ctx->bb_count * sizeof(uint64_t));

    // W means it's in a register right now
    splitter.W_entry  = arena_zalloc(ra->arena, ctx->bb_count * sizeof(uint64_t));
    splitter.W_exit   = arena_zalloc(ra->arena, ctx->bb_count * sizeof(uint64_t));
    splitter.live_in  = arena_zalloc(ra->arena, ctx->bb_count * sizeof(uint64_t));
    splitter.live_out = arena_zalloc(ra->arena, ctx->bb_count * sizeof(uint64_t));
    cuikperf_region_end();

    cuikperf_region_start("init", NULL);
    ////////////////////////////////
    // 1. Classify spilled regions
    ////////////////////////////////
    FOR_N(spill_i, 0, num_spills) {
        size_t cnt;
        TB_Node** arr = coalesce_set_array(ctx, ra, &splitter.leaders[spill_i], &cnt);
        FOR_N(j, 0, cnt) {
            TB_Node* n = arr[j];
            // PATCH(odin-tilde): same unscheduled-fresh-node guard as the
            // user scan below — f->scheduled is only valid below scheduled_n.
            if (n->gvn >= f->scheduled_n || f->scheduled[n->gvn] == NULL) { continue; }
            int bb_id = f->scheduled[n->gvn] - ctx->cfg.blocks;

            def_map[bb_id] |= 1ull << spill_i;
            FOR_USERS(u, n) {
                // PATCH(odin-tilde): batched insert_splits (64/round) can
                // leave reload/spill users from an earlier batch that were
                // never scheduled — their scheduled[] slot is out of bounds
                // (strconv@format_digits at stage>=1). They carry no BB info
                // yet; the split traversal below wires them up directly.
                uint32_t ug = USERN(u)->gvn;
                if (ug >= f->scheduled_n || f->scheduled[ug] == NULL) { continue; }
                TB_BasicBlock* use_bb = f->scheduled[ug];
                use_map[use_bb - ctx->cfg.blocks] |= 1ull << spill_i;
            }
        }

        // append the liveness info
        FOR_N(k, 0, ctx->bb_count) {
            TB_BasicBlock* bb = &ctx->cfg.blocks[k];
            bool live_in = false, live_out = false;

            FOR_N(j, 0, cnt) {
                TB_Node* n = arr[j];
                live_in  |= set_get(&bb->live_in, n->gvn);
                live_out |= set_get(&bb->live_out, n->gvn);
            }

            splitter.live_in[k]  |= ((uint64_t)live_in) << spill_i;
            splitter.live_out[k] |= ((uint64_t)live_out) << spill_i;
        }

        if (cnt == 1) {
            splitter.single_def |= 1ull << spill_i;
            // PATCH(odin-tilde): only remat on the FIRST split of a vreg.
            // Remat-ing a node that's pinned to a fixed register (e.g. an
            // ICONST/MACH_COPY forced into RCX for rep-movs/call args)
            // recreates an identical node on the exact same conflict point
            // every round and the RA never converges (HRP regions that would
            // split the occupant are written but never read in this version).
            // On repeat splits, fall through to the normal spill path so the
            // value actually leaves the pinned register for a stack slot.
            //
            // PATCH(odin-tilde): classes with NO spill mask at all (e.g.
            // FLAGS on x64, mayspill_mask[class] == NULL) can never take the
            // spill path — it would manufacture a NULL-mask spill copy and
            // crash in mach_copy (reg_mask_is_stack(NULL)). They must remat
            // on every split: remat clones the producer (cmp) next to its
            // uses, shrinking the live range, so this converges unlike the
            // pinned-GPR remat loop described above.
            int spill_class = splitter.reload_mask[spill_i]->class;
            if (can_remat(ctx, arr[0]) &&
                (splitter.was_spilled[spill_i] == 0 || ctx->mayspill_mask[spill_class] == NULL)) {
                splitter.remat_all |= 1ull << spill_i;
            }

            if (arr[0]->user_count == 1) {
                spill_aggro |= 1ull << spill_i;
            }
        }
    }

    IF_OPT(REGSPLIT) {
        printf("== TO BE SPILLED (%zu) ==\n", num_spills);
        FOR_N(i, 0, num_spills) {
            uint32_t vreg_id = splits[i].target;
            double cost = rogers_get_spill_cost(ctx, ra, &ctx->vregs[vreg_id]);

            printf("  V%-5u %f %"PRIu64, vreg_id, cost, ctx->vregs[vreg_id].area);
            if ((spill_aggro >> i) & 1) {
                printf(" (SPILL AGGRO)");
            }
            if (splitter.was_spilled[i]) {
                printf(" (SPILLED %d TIMES)", splitter.was_spilled[i]);
            }
            if ((splitter.remat_all >> i) & 1) {
                printf(" (REMAT HARD)");
            }
            printf("\n");

            size_t cnt;
            TB_Node** arr = coalesce_set_array(ctx, ra, &splitter.leaders[i], &cnt);
            FOR_N(j, 0, cnt) {
                TB_Node* n = arr[j];

                printf("  * ");
                ctx->print_pretty(ctx, n);
                printf(" (%d uses)\n", n->user_count);
                TB_ASSERT(ctx->vreg_map[n->gvn] == vreg_id);

                /* FOR_USERS(u, n) {
                    printf("||  ");
                    ctx->print_pretty(ctx, USERN(u));
                    printf("\n");
                }*/
            }
        }
    }

    IF_OPT(REGSPLIT) {
        printf("== DEF-USE INFO ==\n");
        FOR_N(i, 0, ctx->bb_count) {
            TB_BasicBlock* bb = &ctx->cfg.blocks[i];
            printf("  BB%-3zu: D=", i);
            FOR_REV_N(j, 0, num_spills) {
                putchar((def_map[i] >> j) & 1 ? '1' : '0');
            }
            printf(" U=");
            FOR_REV_N(j, 0, num_spills) {
                putchar((use_map[i] >> j) & 1 ? '1' : '0');
            }
            printf(" ");
            FOR_N(class, 1, ctx->num_classes) {
                HRPRegion* hrp = &ra->hrp[i];
                if (hrp->start[class] >= 0 &&
                    hrp->start[class] < (int) aarray_length(bb->items) &&
                    hrp->end[class] < (int) aarray_length(bb->items)) {
                    printf("[%s, %d (%%%u), %d (%%%u)] ", reg_class_name(class), hrp->start[class], bb->items[hrp->start[class]]->gvn, hrp->end[class], bb->items[hrp->end[class]]->gvn);
                }
            }
            FOR_SUCC(it, bb->end) {
                TB_BasicBlock* succ_bb = nl_map_get_checked(ctx->cfg.node_to_block, it.succ);
                printf("  BB%-3zu", succ_bb - ctx->cfg.blocks);
            }
            printf("\n");
        }
    }
    cuikperf_region_end();

    int ttt = 0, uuu = 0, vvv = 0;

    ////////////////////////////////
    // 2. Insert spills and reloads
    ////////////////////////////////
    TB_OPTDEBUG(REGSPLIT)(printf("== INSERT NODES ==\n"));
    uint64_t all_spills = (UINT64_MAX >> (64 - num_spills));
    splitter.all_phis = aarray_create(arena, TB_Node*, 30);
    splitter.all_defs = aarray_create(arena, TB_Node*, 30);

    int freq[64];
    ArenaArray(SplitterLoop) loops = aarray_create(arena, SplitterLoop, 10);
    FOR_N(bb_id, 0, ctx->bb_count) {
        TB_BasicBlock* bb = &ctx->cfg.blocks[bb_id];
        HRPRegion* hrp = &ra->hrp[bb_id];

        TB_Node* header = bb->start;
        int pred_count = header->type == TB_PROJ && header->inputs[0]->type == TB_ROOT ? 0 : header->input_count;

        TB_Node** bb_defs = &defs[bb_id*num_spills];
        uint64_t W = 0;
        // uint64_t S = 0;

        ////////////////////////////////
        // Compute W_entry
        ////////////////////////////////
        int loop_dom = -1;
        int loop_tail = -1;
        if (pred_count) {
            FOR_N(j, 0, num_spills) {
                freq[j] = 0;
            }

            int fwd_pred_count = 0;
            FOR_N(j, 0, pred_count) {
                TB_Node* pred = cfg_get_pred(&ctx->cfg, header, j);
                TB_ASSERT(pred->input_count != 0 && pred->type != TB_DEAD);

                TB_BasicBlock* pred_bb = f->scheduled[pred->gvn];
                int pred_id = pred_bb - ctx->cfg.blocks;
                if (pred_id >= bb_id) {
                    // pick the furthest backedge
                    loop_tail = TB_MAX(loop_tail, pred_id);
                } else {
                    FOR_N(k, 0, num_spills) {
                        freq[k] += (splitter.W_exit[pred_id] >> k) & 1;
                    }
                    fwd_pred_count++;
                }
            }

            // if all forward preds go into this block in a register
            // we'll keep it that way
            uint64_t mismatch = 0;
            FOR_N(k, 0, num_spills) {
                if (freq[k] == fwd_pred_count) {
                    W |= 1ull << k;
                } else if (freq[k] != 0) {
                    mismatch |= 1ull << k;
                }
            }

            if (1) { // loop_tail >= 0) {
                // if the Join block is HRP and the value isn't used in the first block, we'll spill
                HRPRegion* hrp = &ra->hrp[bb_id];
                uint64_t is_hrp = 0;
                FOR_N(class, 1, ctx->num_classes) {
                    if (hrp->start[class] == 0) {
                        is_hrp |= class2vreg[class];
                        W &= ~is_hrp;
                    } else {
                        // if the preds mismatch but we're not an HRP block, let's force regs
                        W |= class2vreg[class] & mismatch;
                    }
                }

                for (size_t j = 0; j < aarray_length(bb->items); j++) {
                    TB_Node* n = bb->items[j];
                    if (n->type == TB_PHI) {
                        continue;
                    }

                    // any uses of the "about" to be spilled values?
                    FOR_N(k, 1, n->input_count) {
                        TB_Node* in = n->inputs[k];
                        int spill = spill_map_get2(&splitter.spill_map, in);
                        if (spill >= 0 && !((splitter.remat_all >> spill) & 1)) {
                            W |= (1ull << spill) & is_hrp;
                            TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%d: immediate use of %%%u, don't spill it\n", bb_id, spill, in->gvn));
                        }
                    }

                    // def was assigned
                    if (n->gvn >= aarray_length(ctx->vreg_map) || ctx->vreg_map[n->gvn]) {
                        break;
                    }
                }
            }
        }

        // process phi defs
        if (NODE_ISA(header, REGION)) {
            FOR_USERS(u, header) {
                if (USERN(u)->type != TB_PHI) {
                    continue;
                }

                TB_Node* phi = USERN(u);
                int spill = spill_map_get2(&splitter.spill_map, phi);
                if (spill >= 0) {
                    bb_defs[spill] = phi;
                    phis[bb_id*num_spills + spill] = phi;

                    if (!((W >> spill) & 1)) {
                        TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%d: def %%%u (spill-phi)\n", bb_id, spill, phi->gvn));
                        assign_spill_vreg(ctx, ra, &splitter, phi, spill);
                    } else {
                        TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%d: def %%%u\n", bb_id, spill, phi->gvn));
                    }
                    aarray_push(splitter.all_phis, phi);
                }
            }
        }

        splitter.W_entry[bb_id] = W;

        FOR_N(j, 0, pred_count) {
            TB_Node* pred = cfg_get_pred(&ctx->cfg, header, j);
            TB_ASSERT(pred->input_count != 0 && pred->type != TB_DEAD);

            TB_BasicBlock* pred_bb = f->scheduled[pred->gvn];
            int pred_id = pred_bb - ctx->cfg.blocks;
            if (pred_id >= bb_id) {
                // backedge, always insert phis
                FOR_N(k, 0, num_spills) {
                    TB_Node* phi = phis[bb_id*num_spills + k];
                    if (phi == NULL) {
                        TB_Node* same = bb_defs[k];
                        if (same == NULL || !((splitter.live_in[bb_id] >> k) & 1)) {
                            // TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%zu: the spill doesn't actually live through this block\n", bb_id, k));
                            continue;
                        }

                        // single-def remat nodes don't need to split
                        if ((splitter.remat_all >> k) & 1) {
                            bb_defs[k] = same;
                            continue;
                        }

                        // TB_ASSERT(same != NULL && "how is there no dominating edge yet?");
                        phi = tb_alloc_node(f, TB_PHI, same ? same->dt : TB_TYPE_VOID, 1 + pred_count, 0);
                        set_input(f, phi, header, 0);
                        FOR_N(i, 0, j) {
                            set_input(f, phi, same, 1+i);
                        }
                        phis[bb_id*num_spills + k] = phi;
                        rogers_insert_op(ctx, bb_id, phi, 1);

                        nl_table_put(&splitter.spill_map, phi, (void*) (k + 1));
                        aarray_push(splitter.all_phis, phi);

                        bb_defs[k] = phi;
                        TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%zu: phi %%%u (incomplete)\n", bb_id, k, phi->gvn));
                    }
                }
            } else {
                W = update_phi_edges(ctx, ra, &splitter, W, defs, phis, bb_id, pred_id, all_spills, j, false);
            }
        }

        TB_OPTDEBUG(REGSPLIT)(dump_split_state(bb_defs, bb_id, num_spills, W, 0));

        // queue up a loop completion task once we've walked to the furthest loop tail
        if (loop_tail >= 0) {
            aarray_push(loops, (SplitterLoop){ bb_id, loop_tail });
        }

        // int nearest_hrp_t = nearest_hrp_event(ctx, ra, bb_id, 0);

        cuikperf_region_start("BB", NULL);
        uint64_t defer_spill = 0;
        uint64_t delay_spill = 0;
        uint64_t has_spilled = 0;
        uint64_t is_hrp_rn = 0;
        for (size_t j = 0; j < aarray_length(bb->items); j++) {
            TB_Node* n = bb->items[j];

            // ignore the newly inserted nodes, they don't need any fun treatment
            if (NODE_ISA(n, REGION) || n->type == TB_PHI || n->gvn >= old_node_count) {
                continue;
            }

            // spill if we're in an HRP region
            if (1) { // j >= nearest_hrp_t) {
                FOR_N(class, 0, ctx->num_classes) {
                    // never even got high pressure
                    if (ra->hrp[bb_id].start[class] < 0) {
                        continue;
                    }

                    if (j == ra->hrp[bb_id].start[class]) {
                        TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: %%%u: entered HRP region\n", bb_id, n->gvn));
                    }

                    if (j >= ra->hrp[bb_id].start[class] && j <= ra->hrp[bb_id].end[class]) {
                        is_hrp_rn |= class2vreg[class];
                    } else {
                        is_hrp_rn &= ~class2vreg[class];
                    }
                }

                // We gotta spam this function until all the delayed spills are cleaned up (they clear up quickly)
                /* if (delay_spill == 0) {
                    nearest_hrp_t = nearest_hrp_event(ctx, ra, bb_id, nearest_hrp_t + 1);
                }
                printf("  AREST %d %#lx\n", nearest_hrp_t, is_hrp_rn); */
            }

            uint64_t should_spill = is_hrp_rn & W & ~has_spilled & ~delay_spill;
            if (should_spill) {
                FOR_N(spill, 0, num_spills) {
                    TB_Node* def = bb_defs[spill];
                    if ((should_spill >> spill) & 1) {
                        // don't spill right now if we're about to use it in a coalesced op (it wouldn't
                        // save on reg pressure)
                        if (spill_map_get2(&splitter.spill_map, bb->items[j]) != spill) {
                            if (def == NULL) {
                                TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%zu: wasn't live in HRP region\n", bb_id, spill));
                            } else if (!can_remat(ctx, def) || is_reload(def)) {
                                size_t t = j;
                                bb_defs[spill] = insert_spill(ctx, ra, &splitter, bb, def, spill, &t);
                                j += t <= j;
                            } else if (def->type == TB_MACH_COPY && !reg_mask_is_stack(TB_NODE_GET_EXTRA_T(def, TB_NodeMachCopy)->use)) {
                                if (def->inputs[1]->type != TB_MACH_COPY && can_remat(ctx, def->inputs[1])) {
                                    bb_defs[spill] = def->inputs[1];
                                    TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%zu: convert copy %%%u to initially rematerializable value %%%u\n", bb_id, spill, def->gvn, def->inputs[1]->gvn));
                                } else {
                                    TB_NODE_GET_EXTRA_T(def, TB_NodeMachCopy)->def = splitter.spill_mask[spill];
                                    TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%zu: convert copy to spill-store %%%u\n", bb_id, spill, def->gvn));
                                }
                            } else {
                                TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%zu: split remat %%%u (%s)\n", bb_id, spill, def->gvn, tb_node_get_name(def->type)));
                            }

                            has_spilled |= (1ull << spill);
                            W &= ~(1ull << spill);
                        }
                    }
                }
            }
            delay_spill = 0;
            ttt++;

            // reload on demand if a use comes up and we're spilled
            bool vaporized = false;
            if (nl_table_get(&splitter.uses_spill, n)) {
                uuu++;

                FOR_N(k, 1, n->input_cap) {
                    TB_Node* in = n->inputs[k];
                    if (in == NULL) {
                        continue;
                    }

                    int spill = spill_map_get2(&splitter.spill_map, in);
                    if (spill < 0) {
                        continue;
                    }

                    vvv++;
                    TB_Node* def = bb_defs[spill];
                    if (def == NULL) { // PATCH(odin-tilde): no split def reaches this use; keep the original input
                        continue;
                    }
                    if ((W >> spill) & 1) {
                        // the reload mask might not overlap with the use mask if we clobbered
                        // a reg and realized we couldn't maintain the old mask, this might force a copy right before the
                        // "tight" uses.
                        // PATCH(odin-tilde): implemented below (was a stock TODO).
                        // A pinned use must never re-pin the resident def's
                        // vreg through the mask meet — route it through a
                        // single-point bridge copy unless the def already
                        // lives in exactly the pinned register.
                        RegMask* in_mask = constraint_in(ctx, n, k);
                        if (fixed_reg_mask(in_mask) >= 0) {
                            RegMask* def_mask = split_def_mask(ctx, ra, &splitter, def);
                            if (def_mask != NULL && fixed_reg_mask(def_mask) != fixed_reg_mask(in_mask)) {
                                insert_bridge_copy(ctx, ra, &splitter, bb, &j, def, def_mask, n, k, in_mask);
                            }
                        }
                    } else {
                        if (n->type == TB_MACH_COPY) {
                            TB_NodeMachCopy* dst = TB_NODE_GET_EXTRA(n);
                            if (reg_mask_is_stack(dst->def) && spill_map_get2(&splitter.spill_map, n) == spill) {
                                // we're asking to spill something that's already spilled, just extend the range.
                                TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%d: connecting spill range at %%%u with %%%u (cutting around %%%u)\n", bb_id, spill, n->gvn, def->gvn, in->gvn));
                                RegMask* def_mask = splitter.spill_mask[spill];

                                // update any copy masks, the users of a spill can only really be copies
                                // or folded reload uses
                                FOR_USERS(u, n) {
                                    if (USERN(u)->type == TB_MACH_COPY) {
                                        TB_NodeMachCopy* cpy = TB_NODE_GET_EXTRA(USERN(u));
                                        cpy->use = def_mask;
                                    }
                                }

                                subsume_node2(f, n, def);
                                vaporized = true;
                                TB_ASSERT(n->user_count == 0);
                            } else {
                                // convert copy into reload
                                if (!is_spill_store(n) && is_reload(def)) {
                                    mark_stretched(ctx, ra, &splitter, def->inputs[1]);
                                    dst->use = TB_NODE_GET_EXTRA_T(def, TB_NodeMachCopy)->use;
                                    set_input(f, n, def->inputs[1], k);
                                    continue;
                                } else if (!reg_mask_is_stack(dst->def) && is_spill_store(def)) {
                                    // PATCH(odin-tilde): added the
                                    // is_spill_store(def) requirement —
                                    // only convert this copy into a stack
                                    // reload when the value is genuinely
                                    // spill-slot resident. Under the remat
                                    // strategy (HRP "split remat") NO spill
                                    // store is ever written, so converting
                                    // here read an unwritten slot and the
                                    // original range never shrank (the RA
                                    // re-split the same vreg forever). The
                                    // remat case now falls through to
                                    // insert_reload, which rematerializes
                                    // the def right next to this use.
                                    dst->use = splitter.spill_mask[spill];
                                    set_input(f, n, def, k);

                                    // reset this copies assignment if possible
                                    if (fixed_reg_mask(dst->def) < 0) {
                                        int vreg_id = ctx->vreg_map[n->gvn];
                                        ctx->vregs[vreg_id].class = 0;
                                        ctx->vregs[vreg_id].assigned = -1;
                                    }
                                    continue;
                                }
                            }
                        }

                        RegMask* in_mask = constraint_in(ctx, n, k);
                        bool can_fold = in_mask->may_spill;

                        // if we could do a folded reload but the destination can't, we
                        // fail to do perform it (since we couldn't coalesce).
                        if (can_fold) {
                            // PATCH(odin-tilde): also refuse to fold when the
                            // range is remat_all — def may be a spill-store
                            // copy (can_remat false) whose store does not
                            // dominate this use; folding would read an
                            // unwritten slot. insert_reload remats instead.
                            if (can_remat(ctx, def) || ((splitter.remat_all >> spill) & 1)) {
                                // this would just neither spill or reload the original range and thus
                                // not shrink it, so we force a "reload" here.
                                can_fold = false;
                            } else if (ctx->node_2addr(n) == k) {
                                RegMask* rm = ctx->constraint(ctx, n, NULL);
                                if (!rm->may_spill) {
                                    can_fold = false;
                                }
                            }
                        }

                        if (!can_fold) {
                            // should probably place it above any temps
                            size_t t = j;
                            def = insert_reload(ctx, ra, &splitter, bb, defs, bb_defs, spill, W, &t);
                            j += t <= j;

                            // if we haven't spilled before, we can reuse the reloaded value even if we're in an HRP.
                            if (((~is_hrp_rn & ~was_spilled_before) >> spill) & 1) {
                                bb_defs[spill] = def;
                                W |= 1ull << spill;

                                if (splitter.spill_mask[spill]) {
                                    defer_spill |= 1ull << spill;
                                }
                            }
                        }
                    }

                    if (in != def) {
                        set_input(f, n, def, k);

                        // if there's other edges on the node that refer to the old node, we
                        // should update them now. we only need to check for a direct node match since indirect
                        // matches to other nodes in the same vreg couldn't be possible (they'd be interfering).
                        FOR_N(l, k+1, n->input_count) {
                            if (n->inputs[l] == in) {
                                set_input(f, n, def, l);
                            }
                        }

                        // PATCH(odin-tilde): a reload placed for a clobber
                        // split avoids the clobbered registers, so it cannot
                        // directly satisfy a use pinned to one of them; bridge
                        // it (see the W-resident case above). Even when the
                        // masks do meet, routing a pinned use straight onto a
                        // normie/narrow def re-pins that def's vreg through
                        // the mask meet and the split never converges.
                        RegMask* in_mask = constraint_in(ctx, n, k);
                        if (fixed_reg_mask(in_mask) >= 0) {
                            RegMask* def_mask = split_def_mask(ctx, ra, &splitter, def);
                            if (def_mask != NULL && fixed_reg_mask(def_mask) != fixed_reg_mask(in_mask)) {
                                insert_bridge_copy(ctx, ra, &splitter, bb, &j, def, def_mask, n, k, in_mask);
                            }
                        }
                    }

                    TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%d: use at %%%u:%zu (of %%%u)\n", bb_id, spill, n->gvn, k, def->gvn));
                }
            }

            if (vaporized) {
                aarray_push(splitter.all_defs, n);
            } else {
                // mark new definitions
                int spill = spill_map_get2(&splitter.spill_map, n);
                if (spill >= 0) {
                    bb_defs[spill] = n;
                    aarray_push(splitter.all_defs, n);

                    W |= 1ull << spill;

                    if (n->type == TB_MACH_COPY) {
                        TB_NodeMachCopy* cpy = TB_NODE_GET_EXTRA(n);
                        cpy->def = splitter.reload_mask[spill];
                    }

                    TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%d: def %%%u\n", bb_id, spill, n->gvn));

                    // PATCH(odin-tilde): include was_spilled_before — a
                    // vreg that was already split in a previous round and
                    // STILL failed allocation must not keep a long
                    // register-resident range from its def to the first
                    // HRP spill point (e.g. a pinned param projection
                    // blocking an argument register across a whole block
                    // of memcpys). Escalate to spilling immediately after
                    // the def: the register range becomes [def, def+1],
                    // the minimum possible, so this always converges.
                    bool should_spill_immediately = ((is_hrp_rn | spill_aggro | splitter.remat_all | was_spilled_before) >> spill) & 1;
                    int class = splitter.reload_mask[spill]->class;
                    if (j >= ra->hrp[bb_id].start[class] && j <= ra->hrp[bb_id].end[class]) {
                        should_spill_immediately = true;
                    }

                    int dst_vreg = n->gvn < aarray_length(ctx->vreg_map) ? ctx->vreg_map[n->gvn] : 0;

                    // if the value is coalesced with the node right below, we'd rather
                    // spill after such a chain.
                    size_t t = j+1, cnt = aarray_length(bb->items);
                    while (t < cnt && should_skip_over(bb->items[t])) {
                        t++;
                    }

                    if (t < cnt) {
                        if (spill_map_get2(&splitter.spill_map, bb->items[t]) == spill) {
                            should_spill_immediately = false;
                            delay_spill |= 1ull << spill;
                        } else if (has_immediate_use(bb->items[t], n)) {
                            // don't delay the spill past a use which clobbers the
                            // registers this value might be sitting in (e.g. a call
                            // whose arg copy is pinned to a caller-saved register).
                            // The value is dead right after such a use, so a
                            // spill-store placed after it reads garbage and the RA
                            // would keep re-splitting the same range forever.
                            RegMask* def_rm = NULL;
                            if (n->type == TB_MACH_COPY) {
                                def_rm = TB_NODE_GET_EXTRA_T(n, TB_NodeMachCopy)->def;
                            } else if (ctx->constraint) {
                                def_rm = ctx->constraint(ctx, n, NULL);
                            }

                            bool use_clobbers = false;
                            if (def_rm != NULL) {
                                RegMask** kills = ctx->ins;
                                int kill_count = ctx->constraint_kill(ctx, bb->items[t], kills);
                                FOR_N(kk, 0, kill_count) {
                                    if (kills[kk]->class != def_rm->class) {
                                        continue;
                                    }
                                    FOR_N(w, 0, def_rm->count) {
                                        if (w < kills[kk]->count && (kills[kk]->mask[w] & def_rm->mask[w])) {
                                            use_clobbers = true;
                                            break;
                                        }
                                    }
                                    if (use_clobbers) {
                                        break;
                                    }
                                }
                            }

                            if (!use_clobbers) {
                                should_spill_immediately = false;
                                delay_spill |= 1ull << spill;
                            }
                        }
                    }

                    // spill immediately
                    if (should_spill_immediately) {
                        if (!can_remat(ctx, n)) {
                            bb_defs[spill] = insert_spill(ctx, ra, &splitter, bb, n, spill, &t);
                            j += t <= j;
                        } else if (n->type == TB_MACH_COPY && !reg_mask_is_stack(TB_NODE_GET_EXTRA_T(n, TB_NodeMachCopy)->use)) {
                            if (n->inputs[1]->type != TB_MACH_COPY && can_remat(ctx, n->inputs[1])) {
                                bb_defs[spill] = n->inputs[1];
                                TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%d: convert copy %%%u to initially rematerializable value %%%u\n", bb_id, spill, n->gvn, n->inputs[1]->gvn));
                            } else {
                                TB_NODE_GET_EXTRA_T(n, TB_NodeMachCopy)->def = splitter.spill_mask[spill];
                                TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%d: convert copy to spill-store %%%u\n", bb_id, spill, n->gvn));
                            }
                        } else {
                            TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%d: split remat %%%u (%s)\n", bb_id, spill, n->gvn, tb_node_get_name(n->type)));
                        }

                        // S &= ~(1ull << spill);
                        W &= ~(1ull << spill);
                    }
                }
            }
        }
        cuikperf_region_end();

        if (defer_spill) {
            FOR_N(j, 0, num_spills) {
                if ((((W & defer_spill & splitter.live_out[bb_id]) >> j) & 1) && bb_defs[j]) {
                    bb_defs[j] = insert_spill(ctx, ra, &splitter, bb, bb_defs[j], j, NULL);
                }
            }
        }

        TB_OPTDEBUG(REGSPLIT)(dump_split_state(bb_defs, bb_id, num_spills, W, 0));

        splitter.W_exit[bb_id] = W;

        // complete loop phis
        size_t j = aarray_length(loops);
        while (j--) {
            if (loops[j].tail != bb_id) {
                continue;
            }

            uint32_t head = loops[j].head;
            aarray_remove(loops, j);

            // this is always a region so the pred count is simpler
            TB_Node* header = ctx->cfg.blocks[head].start;
            size_t pred_count = header->input_count;

            uint64_t W_head = splitter.W_entry[head];
            FOR_N(j, 0, pred_count) {
                TB_Node* pred = cfg_get_pred(&ctx->cfg, header, j);
                TB_ASSERT(pred->input_count != 0 && pred->type != TB_DEAD);

                TB_BasicBlock* pred_bb = f->scheduled[pred->gvn];
                int pred_id = pred_bb - ctx->cfg.blocks;
                if (pred_id < head) {
                    continue;
                }

                update_phi_edges(ctx, ra, &splitter, W_head, defs, phis, head, pred_id, all_spills, j, true);
            }

            FOR_N(k, 0, num_spills) {
                TB_Node* phi = phis[head*num_spills + k];
                if (phi == NULL) {
                    continue;
                }

                TB_ASSERT(phi->type == TB_PHI && phi->input_count == 1+pred_count);

                // remove the unnecessary phis
                TB_Node* leader = NULL;
                FOR_N(i, 1, phi->input_count) {
                    TB_Node* in = phi->inputs[i];
                    if (in == phi) { continue; }
                    else if (leader == NULL) { leader = in; }
                    else if (leader != in) { leader = phi; break; }
                }

                TB_ASSERT(leader);
                if (leader != phi) {
                    TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%td: phi %%%u (pruned to %%%u)\n", bb_id, k, phi->gvn, leader->gvn));

                    // replace all def sites that use it, these could
                    // only exist within the loop itself so we can avoid
                    // checking unnecessary blocks.
                    FOR_N(i, head, bb_id+1) {
                        if (defs[i*num_spills + k] == phi) {
                            defs[i*num_spills + k] = leader;
                        }
                    }

                    subsume_node(f, phi, leader);
                    tb__remove_node(ctx, f, phi);

                    phis[head*num_spills + k] = NULL;
                } else {
                    TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: SPILL%td: phi %%%u (complete)\n", k, bb_id, phi->gvn));
                }
            }
        }

        TB_OPTDEBUG(REGSPLIT)(printf("\n"));
    }
    // TB_OPTDEBUG(REGSPLIT)(rogers_dump_sched(ctx, old_node_count));
    // printf("A %d %d %d\n", ttt, uuu, vvv);

    ////////////////////////////////
    // 3. Re-coalesce nodes
    ////////////////////////////////
    TB_OPTDEBUG(REGSPLIT)(printf("== PHIS ==\n"));

    // insert copies on any phis which have pre-split and post-split
    // paths coming in (inserting copies to make these phis into post-split)
    aarray_for(i, splitter.all_phis) {
        TB_Node* n = splitter.all_phis[i];
        if (n->type == TB_NULL) { // it was pruned, remove it from the sets
            rogers_uncoalesce(ctx, ra, n->gvn);
            continue;
        }

        // it might be time to include these guys
        aarray_push(splitter.all_defs, n);

        int spill = spill_map_get2(&splitter.spill_map, n);
        int vreg_id = n->gvn < aarray_length(ctx->vreg_map) ? ctx->vreg_map[n->gvn] : 0;
        if (spill < 0 || splitter.spill_vreg_id[spill] == vreg_id) {
            continue;
        }

        int post_split = 0;
        FOR_N(j, 1, n->input_count) {
            if (n->inputs[j] == NULL) { // PATCH(odin-tilde): diagnose NULL phi input
                fprintf(stderr, "[ra_split] NULL phi input: func=%s phi=%%%u slot=%zu input_count=%u region=%%%u region_type=%d region_ic=%u spill=%d\n",
                        ctx->f->super.name ? ctx->f->super.name : "?",
                        n->gvn, j, n->input_count,
                        n->inputs[0] ? n->inputs[0]->gvn : 0,
                        n->inputs[0] ? n->inputs[0]->type : -1,
                        n->inputs[0] ? n->inputs[0]->input_count : 9999u,
                        spill);
                FOR_N(k2, 1, n->input_count) {
                    if (n->inputs[k2]) {
                        fprintf(stderr, "  in[%zu] = gvn=%u type=%d\n", k2, n->inputs[k2]->gvn, n->inputs[k2]->type);
                    } else {
                        fprintf(stderr, "  in[%zu] = NULL\n", k2);
                    }
                }
                fflush(stderr);
                abort();
            }
            post_split += (n->inputs[j]->gvn >= old_node_count);
        }

        if (post_split != n->input_count - 1) {
            TB_Node* header = n->inputs[0];
            FOR_N(j, 1, n->input_count) {
                TB_Node* pred_def = n->inputs[j];
                if (pred_def->gvn < old_node_count) {
                    TB_Node* pred = cfg_get_pred(&ctx->cfg, header, j - 1);
                    TB_ASSERT(pred->input_count != 0 && pred->type != TB_DEAD);
                    TB_BasicBlock* pred_bb = f->scheduled[pred->gvn];
                    int pred_id = pred_bb - ctx->cfg.blocks;

                    RegMask* def_mask = ctx->constraint(ctx, pred_def, NULL);

                    TB_Node* cpy = tb_alloc_node(f, TB_MACH_COPY, pred_def->dt, 2, sizeof(TB_NodeMachCopy));
                    set_input(f, cpy, pred_def, 1);
                    TB_NODE_SET_EXTRA(cpy, TB_NodeMachCopy, .def = def_mask, .use = def_mask);

                    insert_op_at_end(ctx, ra, pred_bb, cpy);
                    aarray_push(splitter.all_defs, cpy);

                    set_input(f, n, cpy, j);
                }
            }
        }
    }

    // split all the defs and then re-coalesce them
    TB_OPTDEBUG(REGSPLIT)(printf("== RE-COALESCE ==\n"));

    if (f->node_count >= ra->uf_len) {
        size_t new_len = tb_next_pow2(f->node_count + 16);
        rogers_resize_uf(ctx, ra, new_len);
    }

    // make sure there's enough room for the vreg_map
    ctx->vreg_map = aarray__reserve2(ctx->vreg_map, sizeof(*ctx->vreg_map), f->node_count);

    /* printf("ALL_DEFS:\n");
    aarray_for(i, splitter.all_defs) {
        printf(" [%zu] = %%%u\n", i, splitter.all_defs[i]->gvn);
    }
    printf("ALL_PHIS:\n");
    aarray_for(i, splitter.all_phis) {
        printf(" [%zu] = %%%u\n", i, splitter.all_phis[i]->gvn);
    } */

    for (size_t i = 0; i < aarray_length(splitter.all_defs);) {
        TB_Node* n = splitter.all_defs[i];
        if (n->user_count == 0) {
            rogers_uncoalesce(ctx, ra, n->gvn);

            #if TB_OPTDEBUG_REGSPLIT
            printf("  KILL ");
            ctx->print_pretty(ctx, n);
            printf("\n");
            #endif

            // delete the original def
            if (f->scheduled[n->gvn]) {
                ctx->vreg_map[n->gvn] = 0;
                tb__remove_node(ctx, f, n);
                tb_kill_node(f, n);
            }

            aarray_remove(splitter.all_defs, i);
            continue;
        }

        // make them all their own roots
        TB_ASSERT(n->gvn < ra->uf_len);
        // PATCH(odin-tilde): scrub n from its old leader's coalesce array
        // before re-rooting. Previously only the leader-keyed table entry was
        // removed (below), leaving n listed in the old set's array; later
        // put_to_sleep_vreg/evict_vreg walks of that array then saw n mapped
        // to a different vreg and tripped their invariant asserts.
        rogers_uncoalesce(ctx, ra, n->gvn);
        ra->uf[n->gvn] = n->gvn;
        ra->uf_size[n->gvn] = 1;

        TB_ASSERT(n->gvn < aarray_length(ctx->vreg_map));
        ctx->vreg_map[n->gvn] = 0;

        IF_OPT(REGSPLIT) {
            printf("  ");
            ctx->print_pretty(ctx, n);
            printf("\n");
        }

        // most of these nodes don't really have a bound set but whatever
        nl_table_remove(&ra->coalesce_set, (void*) (uintptr_t) (n->gvn + 1));
        i += 1;
    }

    TB_OPTDEBUG(REGSPLIT)(printf("\n"));

    // we can now aggressively coalesce nodes without
    // worry... unless it's a copy node, we assume those
    // are always relevant splits.
    aarray_for(i, splitter.all_defs) {
        TB_Node* n = splitter.all_defs[i];
        int x = uf_find(ra->uf, ra->uf_len, n->gvn);
        RegMask* rm = ctx->constraint(ctx, n, NULL);

        // phi-coalesce
        if (n->type == TB_PHI) {
            FOR_N(k, 1, n->input_count) {
                // interfere against everything in the set
                TB_Node* in = n->inputs[k];
                int y = uf_find(ra->uf, ra->uf_len, in->gvn);
                rogers_coalesce(ctx, ra, x, y, n, in);
            }
        }

        // CISC-coalesce
        int shared_edge = ctx->node_2addr(n);
        if (n->type != TB_MACH_COPY && shared_edge >= 0 && n->inputs[shared_edge]) {
            int y = uf_find(ra->uf, ra->uf_len, n->inputs[shared_edge]->gvn);
            rogers_coalesce(ctx, ra, y, x, n->inputs[shared_edge], n);
        }
    }

    // create or reset assignments
    aarray_for(i, splitter.all_defs) {
        TB_Node* n = splitter.all_defs[i];
        int leader = uf_find(ra->uf, ra->uf_len, n->gvn);
        if (n->gvn != leader) {
            continue;
        }

        int vreg_id = ctx->vreg_map[leader];
        TB_ASSERT(vreg_id == 0);

        VReg* new_vreg = tb__set_node_vreg(ctx, n);
        ctx->vreg_map[leader] = new_vreg - ctx->vregs;

        int spill = spill_map_get2(&splitter.spill_map, n);
        if (spill >= 0) {
            // PATCH(odin-tilde): saturate so the counter never wraps
            int ws = splitter.was_spilled[spill] + 1;
            new_vreg->was_spilled = ws > 3 ? 3 : ws;
            new_vreg->avoid_mask  = splitter.avoid_mask[spill];
        }
    }

    aarray_for(i, splitter.all_defs) {
        TB_Node* n = splitter.all_defs[i];
        int leader = uf_find(ra->uf, ra->uf_len, n->gvn);

        // PATCH(odin-tilde): relax zombie pinned copies. A pinned wrapper
        // copy whose pinned use was rewired away keeps its fixed def mask
        // forever (its own constraint re-pins the vreg every round) and can
        // deadlock the allocator against the register's occupant. If no
        // remaining use requires the pin, drop the def mask back to the
        // class normie mask.
        if (n->type == TB_MACH_COPY) {
            TB_NodeMachCopy* mc = TB_NODE_GET_EXTRA(n);
            int fx = fixed_reg_mask(mc->def);
            if (fx >= 0) {
                bool needed = false;
                FOR_USERS(u, n) {
                    if (USERI(u) > 0 && USERI(u) < USERN(u)->input_count) {
                        RegMask* um = constraint_in(ctx, USERN(u), USERI(u));
                        if (um && um->class == mc->def->class && fixed_reg_mask(um) == fx) {
                            needed = true;
                            break;
                        }
                    }
                }
                if (!needed) {
                    mc->def = ctx->normie_mask[mc->def->class];
                }
            }
        }

        // PATCH(odin-tilde): spill-stores of a split must stay
        // slot-resident. The splitter's W-tracking treats "not
        // register-resident" as "in the stack slot", but the spill copies'
        // may_spill GPR masks still let the RA color the shared spill vreg
        // into a register; the slot is then never written on paths that
        // skip the store and later reloads read garbage (ols resolve_poly
        // SIGSEGV: a context-param spill-store became "mov r9 -> r13"
        // while the merge-block reload read the never-written slot). Pin
        // the vreg to the pure-stack spill mask so stores always write the
        // slot and reloads always read it. Reload copies are excluded via
        // !is_reload: for clobber splits their def mask is the (may_spill)
        // avoid mask, so is_spill_store alone would misclassify them and
        // pin the register the reload is supposed to land in. Spill phis
        // coalesce with their input stores, so stack-resident (W=0) phis
        // inherit the pin through the shared vreg, while register-resident
        // (W=1) phis — whose inputs are reloads, never spill-stores — are
        // untouched.
        {
            int spill_i = spill_map_get2(&splitter.spill_map, n);
            if (spill_i >= 0 && ((stk_only >> spill_i) & 1) &&
                is_spill_store(n) && !is_reload(n)) {
                // PATCH(odin-tilde): only pin PURE spill spaces — see
                // spill_pin_is_pure. A vreg shared with a real op cannot
                // be stack-resident (odin-tank runtime@_make_dynamic_
                // array_len_cap: a cmovcc def sharing a spill class vreg
                // pinned to STACK31, inst2 assert).
                int stk_vreg_id = ctx->vreg_map[leader];
                bool pure = stk_vreg_id != 0 &&
                    spill_pin_is_pure(ctx, ra, &splitter, stk_vreg_id, n);
                if (pure) {
                    {
                        VReg* stk_vreg = &ctx->vregs[stk_vreg_id];
                        stk_vreg->mask = intern_regmask(ctx, REG_CLASS_STK, true, 0);
                        stk_vreg->reg_width = tb__reg_width_from_dt(REG_CLASS_STK, n->dt);
                        stk_vreg->spill_bias = -100.0f;
                        stk_vreg->spill_cost = INFINITY;
                        ctx->vreg_map[n->gvn] = stk_vreg_id;
                        continue;
                    }
                }
            }
        }

        RegMask* mask = ctx->constraint(ctx, n, NULL);

        int vreg_id = ctx->vreg_map[leader];
        if (n->gvn == leader) {
            VReg* vreg = &ctx->vregs[vreg_id];
            vreg->reg_width  = tb__reg_width_from_dt(mask->class, n->dt);
            vreg->spill_bias = mask->may_spill ? -100.0f : 0.0f;
        }
        // PATCH(odin-tilde): do NOT meet in the old vreg mask when it is
        // stack-ish (STK class or any may_spill mask) or already empty.
        // The old mask records the PREVIOUS segment's fate — a "proper
        // spill" STK pin or a mayspill narrowing — and meeting it into the
        // new segment either crosses classes (GPR def-constraint meet STK
        // pin => TB_REG_EMPTY) or intersects disjoint mayspill bit sets
        // (=> interned empty mayspill mask). Both poison the new vreg into
        // an uncolorable empty mask, the split loop re-creates the same
        // poison every generation, and the store chain grows one link per
        // round until the round assert (server@index_file). Legitimate
        // constraints flow forward through the def constraint above and
        // the live use masks below; this split's own clobber narrowing is
        // re-applied by the reload-narrowing block further down.
        RegMask* old_vm = ctx->vregs[vreg_id].mask;
        // PATCH(odin-tilde): preserve the stk_only spill pin. The pin is
        // applied to spill-store vregs earlier in THIS pass; overwriting
        // it with a recomputed GPR mask lets the store color into a
        // register, the slot is never written, and reloads read garbage
        // (ols request_initialize SIGSEGV: allocator/context fields came
        // back as path-string bytes "/Users/vajra/Claw/Tildebackend").
        // Only preserve for PURE spill spaces (no real-op defs): a vreg
        // shared with a real op cannot be stack-resident (the op cannot
        // write a slot) — keeping the pin there re-poisons the split
        // loop (server@index_file).
        if (old_vm != NULL && reg_mask_is_spill(old_vm)) {
            // PATCH(odin-tilde): only preserve the pin for PURE spill
            // spaces — see spill_pin_is_pure.
            bool pure = spill_pin_is_pure(ctx, ra, &splitter, vreg_id, n);
            if (pure) {
                ctx->vregs[vreg_id].mask = old_vm;
                ctx->vreg_map[n->gvn] = vreg_id;
                continue;
            }
        }
        // PATCH(odin-tilde): for a proper-spill (immediate-store) def
        // segment, skip the old-mask meet. The segment ends at the spill
        // store inserted right after the def — no clobber can cross it —
        // so the def may live in ANY register, including caller-saved
        // ones. The old mask records the pre-split vreg's monotone
        // clobber narrowing (e.g. live-across-call shrunk to the 6
        // callee-saved regs, all of which can be permanently occupied by
        // pinned argument values); meeting it in poisons every new def
        // generation into an uncolorable mask and the split loop spins
        // forever (fmt@fmt_arg: gvn 141 x86_mov, rounds to 8192+).
        int spill_i0 = spill_map_get2(&splitter.spill_map, n);
        bool immediate_store = spill_i0 >= 0 && ((was_spilled_before >> spill_i0) & 1);
        if (!immediate_store &&
            old_vm != NULL && old_vm != &TB_REG_EMPTY &&
            !reg_mask_is_stack(old_vm) && reg_mask_is_not_empty(old_vm)) {
            mask = tb__reg_mask_meet(ctx, mask, old_vm);
        }

        FOR_USERS(u, n) {
            if (USERI(u) > 0 && USERI(u) < USERN(u)->input_count) {
                RegMask* in_mask = constraint_in(ctx, USERN(u), USERI(u));
                mask = tb__reg_mask_meet(ctx, mask, in_mask);
            }
        }

        // PATCH(odin-tilde): restore hardware pins of every node that shares
        // this vreg. The mask above is rebuilt from the segment leader's def
        // constraint plus use meets only; a coalesced/hard-split member with
        // an intrinsically fixed def (e.g. umulpair's RDX hi projection)
        // never contributes its pin, and the relaxation guard below only
        // fires when the mask is ALREADY fixed — a normie mask here detaches
        // the projection from the register the parent instruction actually
        // writes (div-by-10 magic wrong, strconv_decimal@assign loops
        // forever).
        {
            size_t gcnt2;
            TB_Node** garr2 = coalesce_set_array(ctx, ra, &n, &gcnt2);
            FOR_N(gi, 0, gcnt2) {
                TB_Node* gn = garr2[gi];
                if (gn->type == TB_MACH_COPY || gn->type == TB_NULL) { continue; }
                RegMask* gm = ctx->constraint(ctx, gn, NULL);
                if (gm && gm != &TB_REG_EMPTY && fixed_reg_mask(gm) >= 0 &&
                    mask != &TB_REG_EMPTY && mask->class == gm->class) {
                    mask = tb__reg_mask_meet(ctx, mask, gm);
                }
            }
            // coalesce sets are keyed by uf-leader and miss nodes that were
            // hard-split onto this vreg (vreg_map says they belong, uf says
            // otherwise) — e.g. umulpair's RDX-pinned hi projection.
            size_t vmap_len = aarray_length(ctx->vreg_map);
            size_t g2n_cap = ra->order_cap; // gvn2node is only order_cap long
            for (size_t mi = 0; mi < vmap_len && mi < g2n_cap; mi++) {
                if (ctx->vreg_map[mi] != vreg_id) { continue; }
                TB_Node* mn = ra->gvn2node[mi];
                if (mn == NULL || mn->type == TB_MACH_COPY || mn->type == TB_NULL) { continue; }
                RegMask* mm = ctx->constraint(ctx, mn, NULL);
                if (mm && mm != &TB_REG_EMPTY && fixed_reg_mask(mm) >= 0 &&
                    mask != &TB_REG_EMPTY && mask->class == mm->class) {
                    mask = tb__reg_mask_meet(ctx, mask, mm);
                }
            }
        }

        // PATCH(odin-tilde): relax zombie fixed masks. Meets are monotone,
        // so once a vreg's mask was pinned by a use that has since been
        // rewired away, it stays pinned forever even though no remaining
        // use (across the whole coalesce group) requires it — and a fixed
        // vreg that keeps losing its register spins the split loop.
        // Intrinsically pinned defs (projections, temps, pinned copies)
        // are exempt: their own constraint legitimately fixes the mask.
        if (mask != &TB_REG_EMPTY && fixed_reg_mask(mask) >= 0 &&
            fixed_reg_mask(ctx->constraint(ctx, n, NULL)) < 0) {
            int fx = fixed_reg_mask(mask);
            // PATCH(odin-tilde): intrinsically pinned DEFS anywhere in the
            // coalesce group (e.g. a call-result projection 2addr-merged
            // into this add) legitimately fix the mask — the hardware
            // writes that register no matter what the uses look like.
            // Checking only n's own def constraint misses them and drops
            // the pin, detaching the projection from the register the call
            // actually returns in (observed: sha256 round function — F1's
            // RAX result "assigned" to EBP with no copy ever moving it out
            // of RAX).
            bool intrinsic = false;
            size_t gcnt;
            TB_Node** garr = coalesce_set_array(ctx, ra, &n, &gcnt);
            FOR_N(gi, 0, gcnt) {
                if (garr[gi]->type != TB_MACH_COPY &&
                    fixed_reg_mask(ctx->constraint(ctx, garr[gi], NULL)) >= 0) {
                    intrinsic = true;
                    break;
                }
            }
            // PATCH(odin-tilde): coalesce groups are keyed by uf-leader and
            // miss nodes that were hard-split onto this vreg (vreg_map says
            // they belong, uf says otherwise) — e.g. umulpair's RDX-pinned hi
            // projection. Such a node still has its hardware register written
            // by the parent instruction; relaxing the mask detaches it
            // (observed: div-by-10 magic wrong, assign loops forever).
            if (!intrinsic) {
                size_t vmap_len = aarray_length(ctx->vreg_map);
                size_t g2n_cap = ra->order_cap; // gvn2node is only order_cap long
                for (size_t mi = 0; mi < vmap_len && mi < g2n_cap; mi++) {
                    if (ctx->vreg_map[mi] != vreg_id) { continue; }
                    TB_Node* mn = ra->gvn2node[mi];
                    if (mn && mn->type != TB_MACH_COPY && mn->type != TB_NULL &&
                        fixed_reg_mask(ctx->constraint(ctx, mn, NULL)) >= 0) {
                        intrinsic = true;
                        break;
                    }
                }
            }
            bool needed = false;
            if (!intrinsic) {
                FOR_N(gi, 0, gcnt) {
                    FOR_USERS(u, garr[gi]) {
                        if (USERI(u) > 0 && USERI(u) < USERN(u)->input_count) {
                            RegMask* um = constraint_in(ctx, USERN(u), USERI(u));
                            if (um && um->class == mask->class && fixed_reg_mask(um) == fx) {
                                needed = true;
                                break;
                            }
                        }
                    }
                    if (needed) { break; }
                }
            }
            if (!intrinsic && !needed) {
                mask = ctx->normie_mask[mask->class];
            }
        }
        // PATCH(odin-tilde): for clobber/avoid splits the split def's own
        // mask must also exclude the avoided registers — narrowing only the
        // reload/spill copies still lets the def itself retake the avoided
        // register (first-free or hint) and re-conflict on the next round.
        {
            int spill_i = spill_map_get2(&splitter.spill_map, n);
            if (spill_i >= 0 && splits[spill_i].clobber != NULL &&
                splitter.reload_mask[spill_i] != NULL &&
                mask != NULL && mask != &TB_REG_EMPTY &&
                mask->class == splitter.reload_mask[spill_i]->class) {
                RegMask* narrowed = tb__reg_mask_meet(ctx, mask, splitter.reload_mask[spill_i]);
                if (narrowed != NULL && narrowed != &TB_REG_EMPTY) {
                    mask = narrowed;
                }
            }
        }
        // PATCH(odin-tilde): a MACH_COPY's rebuilt mask can meet itself into
        // TB_REG_EMPTY when one endpoint is a non-spillable register mask
        // and the other is a stack mask (reg_mask_meet returns EMPTY for
        // cross-class meets unless both may_spill — but a reg<->stack copy
        // is perfectly emit-able and its live range is a single point).
        // Without the fallback the vreg is poisoned empty, never colors,
        // and the split loop re-queues it forever (gc_batch_loss ABI fix:
        // io@query 34 vregs, 13 mach_copy mask=EMPTY residuals).
        if (mask == &TB_REG_EMPTY && n->type == TB_MACH_COPY) {
            RegMask* own = ctx->constraint(ctx, n, NULL);
            if (own != NULL && own != &TB_REG_EMPTY) { mask = own; }
        }
        ctx->vregs[vreg_id].mask = mask;
        ctx->vreg_map[n->gvn] = vreg_id;
    }

    // hint every copy across vregs
    aarray_for(i, splitter.all_defs) {
        TB_Node* n = splitter.all_defs[i];
        int leader = uf_find(ra->uf, ra->uf_len, n->gvn);
        if (n->gvn == leader) {
            int spill = spill_map_get2(&splitter.spill_map, n);
            if (spill >= 0 && ((was_spilled_before >> spill) & 1)) {
                int vreg_id = ctx->vreg_map[leader];
                VReg* vreg  = &ctx->vregs[vreg_id];
                // PATCH(odin-tilde): same purity rule as the other stk_only
                // pins — never pin a vreg whose class contains a real op
                // (odin-tank runtime@_make_dynamic_array_len_cap: cmovcc def
                // mapped into the class at the setmap above, re-pinned here,
                // inst2 assert).
                if (vreg->mask->may_spill && spill_pin_is_pure(ctx, ra, &splitter, vreg_id, n)) {
                    TB_OPTDEBUG(REGALLOC3)(printf("  * V%u is now a proper spill\n", vreg_id));

                    vreg->spill_cost = INFINITY;
                    vreg->mask = intern_regmask(ctx, REG_CLASS_STK, true, 0);
                    vreg->reg_width = tb__reg_width_from_dt(REG_CLASS_STK, n->dt);
                }
            }
        }

        // hint the source to be like the destination (we're allocating
        // in reverse).
        if (n->type == TB_MACH_COPY) {
            int x = ctx->vreg_map[n->gvn];
            int y = ctx->vreg_map[n->inputs[1]->gvn];
            if (x != y) {
                ctx->vregs[y].hint_vreg = x;
            }
        }
    }

    IF_OPT(REGSPLIT) {
        aarray_for(i, splitter.all_defs) {
            TB_Node* n = splitter.all_defs[i];

            #ifndef NDEBUG
            // PHIs better coalesce with their direct inputs
            if (n->type == TB_PHI) {
                FOR_N(j, 1, n->input_count) {
                    if (ctx->vreg_map[n->gvn] != ctx->vreg_map[n->inputs[j]->gvn]) {
                        TB_OPTDEBUG(REGSPLIT)(rogers_dump_sched(ctx, old_node_count));
                        TB_ASSERT(ctx->vreg_map[n->gvn] == ctx->vreg_map[n->inputs[j]->gvn]);
                    }
                }
            }
            #endif

            if (ra->uf[n->gvn] == n->gvn) {
                int vreg_id = ctx->vreg_map[n->gvn];
                VReg* vreg = &ctx->vregs[vreg_id];

                printf("# V%-4"PRIdPTR" bias=%.2f ", vreg - ctx->vregs, vreg->spill_bias);
                tb__print_regmask(&OUT_STREAM_DEFAULT, vreg->mask);
                printf("\n  ");
                ctx->print_pretty(ctx, n);
                printf(" # UF size = %d, mask = ", ra->uf_size[n->gvn]);
                tb__print_regmask(&OUT_STREAM_DEFAULT, ctx->vregs[vreg_id].mask);
                printf("\n");

                size_t cnt;
                TB_Node** arr = coalesce_set_array(ctx, ra, &n, &cnt);
                FOR_N(j, 0, cnt) {
                    TB_Node* k = arr[j];
                    if (k != n) {
                        printf("    * ");
                        ctx->print_pretty(ctx, k);
                        printf("\n");
                    }
                }
            }
        }
    }

    nl_table_free(splitter.spill_map);
    nl_table_free(splitter.uses_spill);

    TB_OPTDEBUG(SERVER)(dbg_submit_event_sched(&ctx->cfg, f, "Post-split"));
    TB_OPTDEBUG(REGSPLIT)(rogers_dump_sched(ctx, old_node_count));
}

// PATCH(odin-tilde): materialize uses of stack-resident vregs.
// Several nets (the stk_only spill-space pin, the "proper spill" drop,
// clobber-split reload masks) can leave a vreg with a STK-class mask.
// Only mach_copy consumers tolerate that (MOV folds a memory source);
// every other unfused emitter path hardcodes register operands —
// op_gpr_at address bases (runtime@print_type: mov loading through a
// pinned store) and inst2 secondaries (server@collect_value_decl: an
// x86_sub reading a stack-pinned vreg) both asserted. Every store of a
// spill space writes the SAME slot, so the slot is path-insensitively
// current at any dominated use: insert an explicit reload copy before
// such uses and rewire them to it. Uses whose own constraint expects a
// stack slot (um->class == STK, e.g. call argument slots) are left
// alone — the slot IS their required location. Called once per RA round
// after the split phase (also covers rounds whose only stack-pinnings
// came from the no-split "proper spill" net).
static void materialize_stk_uses(Ctx* ctx, Rogers* ra) {
    TB_Function* f = ctx->f;

    // Collapse reload->spill-store pairs: R is a register-class mach_copy
    // reading a stack slot, whose sole user S is a spill-store writing
    // another slot. Rewire S to read R's source slot directly and kill R.
    // Splitting such a pair only re-creates it one slot deeper (the reload
    // can never need a register — its only consumer is a store), growing
    // an alternating store/reload chain by two links EVERY round until the
    // round assert fires (server@index_file reached thousands of links).
    // Same-block requirement: the def-use edge guarantees the source slot
    // is still current at S (no intervening store of the source space can
    // exist between a splitter-placed reload and its use). Pinned copies
    // (fixed def masks, e.g. call-argument copies) are never collapsed —
    // killing them would drop the pinned register write.
    FOR_N(bb_id, 0, ctx->bb_count) {
        TB_BasicBlock* bb = &ctx->cfg.blocks[bb_id];
        for (size_t ni = 0; ni < aarray_length(bb->items); ni++) {
            TB_Node* r = bb->items[ni];
            if (r->type != TB_MACH_COPY || r->user_count != 1) { continue; }
            int rv = (r->gvn < aarray_length(ctx->vreg_map)) ? ctx->vreg_map[r->gvn] : 0;
            if (rv <= 0) { continue; }
            VReg* rvv = &ctx->vregs[rv];
            if (rvv->mask == NULL || rvv->mask->class == REG_CLASS_STK ||
                fixed_reg_mask(rvv->mask) >= 0) { continue; }
            TB_Node* rsrc = r->inputs[1];
            if (rsrc == NULL) { continue; }
            int rsv = (rsrc->gvn < aarray_length(ctx->vreg_map)) ? ctx->vreg_map[rsrc->gvn] : 0;
            if (rsv <= 0) { continue; }
            VReg* rsrcv = &ctx->vregs[rsv];
            if (rsrcv->mask == NULL || rsrcv->mask->class != REG_CLASS_STK) { continue; }
            TB_Node* s = USERN(&r->users[0]);
            if (s->type != TB_MACH_COPY || USERI(&r->users[0]) != 1) { continue; }
            if (f->scheduled[s->gvn] != bb) { continue; }
            int sv = (s->gvn < aarray_length(ctx->vreg_map)) ? ctx->vreg_map[s->gvn] : 0;
            if (sv <= 0) { continue; }
            VReg* svv = &ctx->vregs[sv];
            if (svv->mask == NULL || svv->mask->class != REG_CLASS_STK) { continue; }

            set_input(f, s, rsrc, 1);
            ctx->vreg_map[r->gvn] = 0;
            tb__remove_node(ctx, f, r);
            tb_kill_node(f, r);
            ni--; // re-examine the slot the removed copy occupied
        }
    }

    FOR_N(bb_id, 0, ctx->bb_count) {
        TB_BasicBlock* bb = &ctx->cfg.blocks[bb_id];
        for (size_t ni = 0; ni < aarray_length(bb->items); ni++) {
            TB_Node* n = bb->items[ni];
            if (n->type == TB_PHI || n->type == TB_MACH_PROJ ||
                n->type == TB_MACH_COPY) { continue; }

            size_t ins_pos = ni;
            int inserted = 0;
            FOR_N(k, 1, n->input_count) {
                TB_Node* in = n->inputs[k];
                if (in == NULL) { continue; }
                int sv = (in->gvn < aarray_length(ctx->vreg_map)) ? ctx->vreg_map[in->gvn] : 0;
                if (sv <= 0) { continue; }
                VReg* svr = &ctx->vregs[sv];
                if (svr->mask == NULL || svr->mask->class != REG_CLASS_STK) { continue; }
                RegMask* um = constraint_in(ctx, n, k);
                if (um != NULL && um->class == REG_CLASS_STK) { continue; }
                int vclass = um != NULL ? um->class :
                    (in->dt.type == TB_TAG_F32 || in->dt.type == TB_TAG_F64 ||
                     in->dt.type == TB_TAG_V128 ? 2 : 1);
                RegMask* rmask = ctx->normie_mask[vclass];
                RegMask* smask = ctx->mayspill_mask[vclass];
                if (rmask == NULL || smask == NULL) { continue; }

                TB_Node* cpy = tb_alloc_node(f, TB_MACH_COPY, in->dt, 2, sizeof(TB_NodeMachCopy));
                set_input(f, cpy, in, 1);
                TB_NODE_SET_EXTRA(cpy, TB_NodeMachCopy, .def = rmask, .use = smask);
                VReg* rv = tb__set_node_vreg(ctx, cpy);
                rv->mask = rmask;
                rv->reg_width = tb__reg_width_from_dt(rmask->class, cpy->dt);

                rogers_insert_op(ctx, bb_id, cpy, ins_pos);
                FOR_N(class, 1, ctx->num_classes) {
                    ra->hrp[bb_id].start[class] += ins_pos <= ra->hrp[bb_id].start[class];
                    ra->hrp[bb_id].end[class]   += ins_pos <= ra->hrp[bb_id].end[class];
                }
                ins_pos++;
                inserted++;
                set_input(f, n, cpy, k);
                TB_OPTDEBUG(REGSPLIT)(printf("  BB%zu: stk-use reload %%%u (of %%%u) for %%%u:%zu\n",
                    bb_id, cpy->gvn, in->gvn, n->gvn, k));
            }
            ni += inserted;
        }
    }
}

