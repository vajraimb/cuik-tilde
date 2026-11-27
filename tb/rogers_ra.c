// Efficient global register allocation (2020):
//   https://arxiv.org/pdf/2011.05608.pdf
#include "codegen.h"
#include <limits.h>
#include <float.h>
#include <stdlib.h>

#if USE_INTRIN && CUIK__IS_X64
#include <x86intrin.h>
#endif

#define FOREACH_SET(it, set) \
FOR_N(_i, 0, ((set).capacity + 63) / 64) FOR_BIT(it, _i*64, (set).data[_i])

typedef struct {
    uint64_t key; // key
    int last_use; // val
} InactiveCacheEntry;

// High reg pressure block data, the splitter
// will avoid keeping split values alive in this
// region when possible.
typedef struct {
    int start[MAX_REG_CLASSES];
    int end[MAX_REG_CLASSES];
} HRPRegion;

typedef struct {
    // VReg
    uint32_t target;

    // if this allocation was clobbered, this means
    // we only need to avoid the "failed" node for
    // the definition (since it wanted to use the
    // register we did):
    //
    // # insert %d to put %c into a reg which isn't clobbered.
    // %c = ...            # RAX
    // %d: GPR ~ RAX = %c
    // call foo            # kill: RAX
    // use(%d)
    RegMask* clobber;
} SplitDecision;

// per-round index width for precolor_conflicts (see the PATCH there)
enum { PC_MAX_REGS = 64 };

typedef struct {
    Ctx* ctx;
    TB_Arena* arena;

    int num_classes;
    int* num_regs;

    // dominance frontiers
    ArenaArray(int)* df; // [ctx->bb_count]
    DynArray(SplitDecision) splits;
    DynArray(int) potential_spills; // [gvn]

    DynArray(TB_Node*) prio_alloc;

    int order_cap;
    int* order;
    TB_Node** gvn2node;

    // [class][reg]
    int* active[MAX_REG_CLASSES];
    int stack_reg_count;

    Set been_spilled;
    SparseSet future_active[MAX_REG_CLASSES];
    Set live;

    // PATCH(odin-tilde): precolor_conflicts' register indexes, rebuilt every
    // round in allocate_loop (the arrays live in the round arena).
    size_t pc_upto[MAX_REG_CLASSES];
    bool pc_scan[MAX_REG_CLASSES];
    uint32_t* pc_bkt[MAX_REG_CLASSES][PC_MAX_REGS];
    uint32_t pc_bkt_len[MAX_REG_CLASSES][PC_MAX_REGS];
    uint32_t pc_bkt_cap[MAX_REG_CLASSES][PC_MAX_REGS];
    uint32_t* pc_fixed[MAX_REG_CLASSES][PC_MAX_REGS];
    uint32_t pc_fixed_len[MAX_REG_CLASSES][PC_MAX_REGS];
    uint32_t pc_fixed_cap[MAX_REG_CLASSES][PC_MAX_REGS];
    bool pc_fixed_built[MAX_REG_CLASSES];

    // PATCH(odin-tilde): the per-round structures both sleeper scans answer
    // from (see the comments before pc_sleeper_hits and at precolor_conflicts).
    //   pc_slept / pc_fxbits   per item position: "has been put to sleep" /
    //                           "is a member of a fixed-mask vreg";
    //                           pc_pos_off[b] is block b's first word.
    //   pc_pre[b]              gvns of block b asleep behind the walk's back
    //                           (put to sleep while the walk was already
    //                           below them, used but not woken, or put to
    //                           sleep outside their block's walk).
    //   pc_walk_bb/pc_item_pos where the reverse walk is.
    //   pc_fx_act/pc_fx_slot   the fixed-mask members of the pre-allocate
    //                           walk's block whose range [def, last use]
    //                           contains the current position (pc_fx_sort
    //                           lists them by last use, pc_fx_cnt sorts).
    //   pc_min_lo[gvn]         lowest block index whose live_out holds the
    //                           gvn (expire_interval).
    uint64_t* pc_slept;
    uint64_t* pc_fxbits;
    size_t* pc_pos_off;
    size_t pc_blocks;
    uint32_t** pc_pre;
    uint32_t* pc_pre_len;
    uint32_t* pc_pre_cap;
    size_t pc_walk_bb;
    int pc_item_pos;
    bool pc_last_walk;
    uint32_t* pc_fx_act;
    uint32_t* pc_fx_act_last;
    uint32_t pc_fx_act_len, pc_fx_act_cap;
    int* pc_fx_slot;
    size_t pc_fx_block;
    bool pc_prio;
    uint32_t* pc_fx_sort;
    int* pc_fx_cnt;
    size_t pc_max_items;
    uint32_t pc_fx_sorted, pc_fx_cursor;
    uint32_t* pc_min_lo;
    // scratch lists
    TB_BasicBlock* pc_live_bb;
    uint32_t* pc_live;
    uint32_t pc_live_len, pc_live_cap;
    uint32_t* pc_cand;
    uint32_t pc_cand_len, pc_cand_cap;
    uint32_t* pc_fcand;
    uint32_t pc_fcand_len, pc_fcand_cap;
    uint32_t* pc_ref, *pc_got;
    uint32_t pc_ref_len, pc_ref_cap, pc_got_len, pc_got_cap;
    // TB_PC_AUDIT=1 runs the original scans as well and reports any disagreement
    unsigned long pc_audit_checked, pc_audit_bad;
    unsigned long pc_reg_checked, pc_reg_bad, pc_audit_stale, pc_exp_checked;
    // TB_PC_STATS=1 prints them per function
    unsigned long long pc_stat[16];
    // ra_split.c: per value, the blocks it is live-in / live-out of, read off
    // the block bitsets once per allocate_loop call (liveness does not change
    // while the round runs); pc_lv_count is the node count they were built
    // for — later batches of the round see newer nodes, which have no bits
    bool pc_lv_built;
    size_t pc_lv_count;
    int32_t* pc_lv_in_start;
    uint32_t* pc_lv_in;
    int32_t* pc_lv_out_start;
    uint32_t* pc_lv_out;

    HRPRegion* hrp;

    // bitset of which nodes are vregs
    uint64_t* is_vreg;

    // coalesce disjoint set
    int* uf;
    int* uf_size;
    int uf_len;

    // leader -> list of members
    NL_Table coalesce_set;

    // last use in a BB
    InactiveCacheEntry* inactive_cache;

    // where is the linear scan at
    int where_bb;
    int where_order;

    // how many did the RA introduce
    int num_spills;
    int max_regs_in_class;

    // interference mask
    int mask_cap;
    uint64_t* mask;

    // PATCH(odin-tilde): incremental occupied-bit mirror of active[][],
    // so allocate_reg can OR whole words instead of scanning num_regs
    // entries (the STK class scan was quadratic in the spill count).
    uint64_t* active_mask[MAX_REG_CLASSES];
    int active_mask_words;

    // PATCH(odin-tilde): set while the post-wave final_check pass runs. The
    // spill-everything wave can leave active[] entries whose nodes were
    // re-split onto different vregs, so the strict handoff invariants in
    // mark_active/unmark_active may transiently break; in this mode we evict
    // instead of asserting (the pass either converges or reports !done and
    // the driver's bail handler degrades the function to peeps-only).
    bool soft_final;
} Rogers;

static void tb__insert_splits(Ctx* ctx, Rogers* restrict ra, SplitDecision* splits, size_t num_spills);
static void materialize_stk_uses(Ctx* ctx, Rogers* ra); // PATCH(odin-tilde)
static void insert_op_at_end(Ctx* ctx, Rogers* ra, TB_BasicBlock* bb, TB_Node* n);

static bool allocate_loop(Ctx* restrict ctx, Rogers* restrict ra, TB_Arena* arena);
static void compute_ordinals(Ctx* restrict ctx, Rogers* restrict ra, TB_Arena* arena);
static bool interfere(Ctx* restrict ctx, Rogers* restrict ra, TB_Node* lhs, TB_Node* rhs);
static void rogers_renumber_block(TB_BasicBlock* bb, Rogers* restrict ra);

static void rogers_remat(Ctx* ctx, Rogers* ra, TB_Node* n, bool kill_node);
static void better_spill_range(Ctx* ctx, Rogers* restrict ra, TB_Node* to_spill, size_t old_node_count);
static int last_use_in_bb(TB_BasicBlock* blocks, TB_BasicBlock** scheduled, Rogers* restrict ra, TB_BasicBlock* bb, TB_Node* n, uint32_t n_gvn);

static bool foobar;
static int stats_aaa;
static int histo[10000];

static TB_Node** coalesce_set_array(Ctx* ctx, Rogers* ra, TB_Node** n_ptr, size_t* out_count) {
    int leader = uf_find(ra->uf, ra->uf_len, (*n_ptr)->gvn);
    ArenaArray(TB_Node*) set = nl_table_get(&ra->coalesce_set, (void*) (uintptr_t) (leader + 1));
    if (set) {
        // PATCH(odin-tilde): coalesce sets retain pointers to nodes that were
        // later killed — the CISC/phi hard-split deletion paths null the node
        // and zero its vreg_map without removing it from the set. Consumers
        // then crash on the stale member (put_to_sleep_vreg assert, NULL
        // scheduled[] block deref in interference — seen compiling odinfmt,
        // dead copy %126 in a phi's set). Filter dead members centrally.
        size_t len = aarray_length(set);
        bool has_dead = false;
        FOR_N(i, 0, len) {
            if (set[i]->type == TB_NULL) { has_dead = true; break; }
        }
        if (has_dead) {
            TB_Node** filtered = tb_arena_alloc(&ctx->f->tmp_arena, len * sizeof(TB_Node*));
            size_t cnt = 0;
            FOR_N(i, 0, len) {
                if (set[i]->type != TB_NULL) {
                    filtered[cnt++] = set[i];
                }
            }
            *out_count = cnt;
            return filtered;
        }
        *out_count = len;
        return set;
    } else {
        *out_count = 1;
        return n_ptr;
    }
}

static int rogers_insert_op(Ctx* ctx, int bb_id, TB_Node* n, int pos) {
    TB_BasicBlock* bb = &ctx->cfg.blocks[bb_id];

    // skip phis and projections so that they stay nice and snug
    size_t cnt = aarray_length(bb->items);
    aarray_push(bb->items, 0);
    if (cnt > pos) {
        memmove(&bb->items[pos + 1], &bb->items[pos], (cnt - pos) * sizeof(TB_Node*));
    }
    bb->items[pos] = n;

    tb__insert(ctx, ctx->f, bb, n);
    return pos;
}

static double rogers_get_spill_cost(Ctx* restrict ctx, Rogers* restrict ra, VReg* vreg) {
    if (isnan(vreg->spill_cost)) {
        int leader = uf_find(ra->uf, ra->uf_len, vreg->n->gvn);
        ArenaArray(TB_Node*) set = nl_table_get(&ra->coalesce_set, (void*) (uintptr_t) (leader + 1));

        double c = 0.0;
        if (set) {
            aarray_for(i, set) {
                // PATCH(odin-tilde): skip killed members (see coalesce_set_array)
                if (set[i]->type == TB_NULL) { continue; }
                c += get_node_spill_cost(ctx, set[i]);
            }
        } else {
            c = get_node_spill_cost(ctx, vreg->n);
        }

        vreg->spill_cost = c + vreg->spill_bias;
    }

    // no area? this means it's used right after def
    if (vreg->area == 0) {
        // printf("spill_cost(%zu) = %f\n", vreg - ctx->vregs, INFINITY);
        return INFINITY;
    }

    // printf("spill_cost(%zu) = %f\n", vreg - ctx->vregs, vreg->spill_cost / vreg->area);
    double score = vreg->spill_cost - vreg->area*0.2;
    // PATCH(odin-tilde): penalize repeatedly-split vregs PROPORTIONALLY so
    // choose_best_spill rotates victims instead of sacrificing the same
    // vreg every round (a flat penalty ties once every candidate has been
    // split twice — observed: ~10 x86_cmp/x86_test vregs all at 1e6 trading
    // the identical eviction forever on FLAGS, a single-register class).
    // The least-split candidate is always preferred, so a victim whose
    // split makes no progress (e.g. a test whose only use is the adjacent
    // jcc — remat lands on the same point) is never chosen twice in a row.
    if (vreg->was_spilled > 0) {
        score += (double)vreg->was_spilled * 1e6;
    }
    return score;
}

static void rogers_print_vreg(Ctx* restrict ctx, Rogers* restrict ra, VReg* vreg) {
    double cost = rogers_get_spill_cost(ctx, ra, vreg);
    printf("# V%-4"PRIdPTR" cost=%.2f area=%"PRIu64" bias=%.2f ", vreg - ctx->vregs, cost, vreg->area, vreg->spill_bias);
    tb__print_regmask(&OUT_STREAM_DEFAULT, vreg->mask);
    printf("\n");
}

static bool rogers_can_coalesce(Ctx* restrict ctx, Rogers* restrict ra, TB_Node* xn, TB_Node* yn) {
    if (ra->uf[yn->gvn] != yn->gvn || ra->uf_size[yn->gvn] != 1) {
        return false;
    }

    int x = uf_find(ra->uf, ra->uf_len, xn->gvn);
    ArenaArray(TB_Node*) set = nl_table_get(&ra->coalesce_set, (void*) (uintptr_t) (x + 1));
    if (set == NULL) {
        return !interfere(ctx, ra, xn, yn);
    } else {
        aarray_for(i, set) {
            // PATCH(odin-tilde): skip killed members (see coalesce_set_array)
            if (set[i]->type == TB_NULL) { continue; }
            if (interfere(ctx, ra, set[i], yn)) {
                return false;
            }
        }

        return true;
    }
}

static void rogers_resize_uf(Ctx* restrict ctx, Rogers* restrict ra, size_t new_len) {
    ra->uf = cuik_realloc(ra->uf, new_len * sizeof(int));
    ra->uf_size = cuik_realloc(ra->uf_size, new_len * sizeof(int));
    FOR_N(i, ra->uf_len, new_len) {
        ra->uf[i] = i;
        ra->uf_size[i] = 1;
    }
    ra->uf_len = new_len;
}

static void rogers_coalesce(Ctx* restrict ctx, Rogers* restrict ra, int x, int y, TB_Node* xn, TB_Node* yn) {
    if (x == y) {
        return;
    }

    int max = TB_MAX(x, y) + 1;
    if (max >= ra->uf_len) {
        rogers_resize_uf(ctx, ra, max);
    }

    // hard coalesce with direct input
    TB_ASSERT(x < ra->uf_len && y < ra->uf_len);
    ra->uf[y] = x;
    ra->uf_size[x] += ra->uf_size[y];

    TB_ASSERT(ra->uf[x] == x);
    ArenaArray(TB_Node*)* new_set = (ArenaArray(TB_Node*)*) nl_table_getp(&ra->coalesce_set, (void*) (uintptr_t) (x + 1));
    if (new_set == NULL) {
        ArenaArray(TB_Node*) set = aarray_create(&ctx->f->arena, TB_Node*, 4);
        nl_table_put(&ra->coalesce_set, (void*) (uintptr_t) (x + 1), set);

        // lazy? yes
        new_set = (ArenaArray(TB_Node*)*) nl_table_getp(&ra->coalesce_set, (void*) (uintptr_t) (x + 1));
        aarray_push(*new_set, xn);
    }

    ArenaArray(TB_Node*) old_set = nl_table_get(&ra->coalesce_set, (void*) (uintptr_t) (y + 1));
    if (old_set == NULL) {
        aarray_push(*new_set, yn);
    } else {
        aarray_for(i, old_set) {
            aarray_push(*new_set, old_set[i]);
        }
    }
}

static void rogers_update_mask(Ctx* restrict ctx, Rogers* restrict ra, TB_Node* n, int vreg_id) {
    int leader = uf_find(ra->uf, ra->uf_len, n->gvn);
    ArenaArray(TB_Node*) set = nl_table_get(&ra->coalesce_set, (void*) (uintptr_t) (leader + 1));

    RegMask* mask = NULL;
    if (set && aarray_length(set)) {
        // split the copy
        aarray_for(i, set) {
            int y = set[i]->gvn;
            TB_Node* yy = ra->gvn2node[y];

            // PATCH(odin-tilde): skip killed set members (see
            // put_to_sleep_vreg) — remapping or constraining a dead node
            // would resurrect it into this vreg.
            if (yy->type == TB_NULL) {
                continue;
            }

            ctx->vreg_map[y] = vreg_id;
            mask = tb__reg_mask_meet(ctx, mask, ctx->constraint(ctx, yy, NULL));
            FOR_USERS(u, yy) {
                if (USERI(u) > 0 && USERI(u) < USERN(u)->input_count) {
                    RegMask* in_mask = constraint_in(ctx, USERN(u), USERI(u));
                    mask = tb__reg_mask_meet(ctx, mask, in_mask);
                }
            }
        }
    } else {
        ctx->vreg_map[n->gvn] = vreg_id;
        mask = ctx->constraint(ctx, n, NULL);
        FOR_USERS(u, n) {
            if (USERI(u) > 0 && USERI(u) < USERN(u)->input_count) {
                RegMask* in_mask = constraint_in(ctx, USERN(u), USERI(u));
                mask = tb__reg_mask_meet(ctx, mask, in_mask);
            }
        }
    }
    TB_ASSERT(mask != &TB_REG_EMPTY);

    VReg* vreg = &ctx->vregs[vreg_id];
    vreg->mask = mask;
    // vreg->reg_width = tb__reg_width_from_dt(mask->class, n->dt);
}

static TB_Node* rogers_pick_leader(Ctx* restrict ctx, Rogers* restrict ra, TB_Node* n) {
    int leader = uf_find(ra->uf, ra->uf_len, n->gvn);
    ArenaArray(TB_Node*) set = nl_table_get(&ra->coalesce_set, (void*) (uintptr_t) (leader + 1));
    if (set == NULL || aarray_length(set) == 0) {
        return n;
    }

    double best_cost = FLT_MAX;
    TB_Node* best = NULL;

    // split the copy
    RegMask* mask = NULL;
    aarray_for(i, set) {
        int y = set[i]->gvn;
        TB_Node* yy = ra->gvn2node[y];
        // we don't want to split... splits
        if (yy->type == TB_MACH_COPY) {
            continue;
        }

        double cost = get_node_spill_cost(ctx, yy);
        if (best == NULL || cost < best_cost) {
            best = yy;
        }
    }

    return best ? best : n;
}

static void rogers_uncoalesce(Ctx* restrict ctx, Rogers* restrict ra, int x) {
    int leader = uf_find(ra->uf, ra->uf_len, x);
    ArenaArray(TB_Node*) set = nl_table_get(&ra->coalesce_set, (void*) (uintptr_t) (leader + 1));
    if (set == NULL) {
        return;
    }

    // if the leader is the node being removed from the set, pick a new leader
    int new_leader = -1;
    for (int i = 0; i < aarray_length(set); i++) {
        int y = set[i]->gvn;
        if (y == x) {
            ra->uf[y] = y;
            aarray_remove(set, i);
            i--;
        } else if (leader == x) {
            if (new_leader < 0) {
                new_leader = y;
            }

            ra->uf[y] = new_leader;
        }
    }

    if (new_leader >= 0) {
        nl_table_remove(&ra->coalesce_set, (void*) (uintptr_t) (leader + 1));
        nl_table_put(&ra->coalesce_set, (void*) (uintptr_t) (new_leader + 1), set);
    }
}

static TB_Node* rogers_hard_split(Ctx* restrict ctx, Rogers* restrict ra, TB_Node* n, TB_Node* in, RegMask* rm, int vreg_id) {
    TB_Function* f = ctx->f;

    // PATCH(odin-tilde): rematerializing `in` next to a *different* use (e.g.
    // a phi-edge clone of an lea) copies `in`'s inputs verbatim — but an input
    // that is a member of n's coalesce group may have been clobbered by
    // another group member between in's position and the clone's: the group's
    // location then holds the *latest member def*, not the input value `in`
    // originally read. sort_probe's smoothsort sift hit exactly this: the
    // group {head phi, rt=lea(head,-width)} gained a phi-edge lea clone whose
    // base input was the head phi, but the group location already held rt at
    // the clone point, so the loop-carried "head" became rt+off. Fall back
    // to a plain copy of `in` — in's own location still holds the value.
    bool remat_ok = can_remat(ctx, in);
    if (remat_ok && ra->uf != NULL && n->gvn < ra->uf_len) {
        int group = uf_find(ra->uf, ra->uf_len, n->gvn);
        FOR_N(j, 0, in->input_count) {
            TB_Node* inp = in->inputs[j];
            if (inp != NULL && inp->gvn < ra->uf_len && uf_find(ra->uf, ra->uf_len, inp->gvn) == group) {
                remat_ok = false;
                break;
            }
        }
    }

    TB_Node* move;
    if (remat_ok) {
        size_t extra = extra_bytes(in);
        move = tb_alloc_node(f, in->type, in->dt, in->input_count, extra);
        memcpy(move->extra, in->extra, extra);
        FOR_N(j, 0, in->input_count) if (in->inputs[j]) {
            move->inputs[j] = in->inputs[j];
            add_user(f, move, in->inputs[j], j);
        }
    } else {
        move = tb_alloc_node(f, TB_MACH_COPY, in->dt, 2, sizeof(TB_NodeMachCopy));
        set_input(f, move, in, 1);
        TB_NODE_SET_EXTRA(move, TB_NodeMachCopy, .def = rm, .use = ctx->normie_mask[rm->class]);
    }
    aarray_insert(ctx->vreg_map, move->gvn, vreg_id);
    return move;
}

static void rogers_dump_sched(Ctx* restrict ctx, int old_node_count) {
    FOR_N(i, 0, ctx->bb_count) {
        TB_BasicBlock* bb = &ctx->cfg.blocks[i];
        printf("BB%zu (freq=%f, %%%u):\n", i, bb->freq, bb->start->gvn);

        TB_Node* end = bb->end;
        aarray_for(i, bb->items) {
            // if the first node is a region or
            if (bb->start == bb->items[i]) {
                continue;
            }

            printf("  ");
            // tb_print_dumb_node(NULL, bb->items[i]);
            ctx->print_pretty(ctx, bb->items[i]);
            if (bb->items[i]->gvn >= old_node_count) {
                printf("  #  NEW!!!");
            }
            printf("\n");
        }

        if (!tb_node_is_terminator(bb->end)) {
            TB_Node* succ_n = cfg_next_control(bb->end);
            TB_BasicBlock* succ_bb = nl_map_get_checked(ctx->cfg.node_to_block, succ_n);
            int b = succ_bb - ctx->cfg.blocks;
            if (ctx->cfg.blocks[b].fwd > 0) {
                while (b != ctx->cfg.blocks[b].fwd) {
                    b = ctx->cfg.blocks[b].fwd;
                }
            }

            printf("    jmp BB%d\n", b);
        }
    }
}

static void rogers_dump_split(Ctx* restrict ctx, Rogers* restrict ra, TB_BasicBlock* block, TB_Node* aa, TB_Node* bb) {
    int a[2], b[2];

    bool entry_block = block == &ctx->cfg.blocks[0];
    a[0] = set_get(&block->live_in, aa->gvn) ? 0 : ra->order[aa->gvn] - 1;
    b[0] = set_get(&block->live_in, bb->gvn) ? 0 : ra->order[bb->gvn] - 1;
    a[1] = last_use_in_bb(ctx->cfg.blocks, ctx->f->scheduled, ra, block, aa, aa->gvn) - 1;
    b[1] = last_use_in_bb(ctx->cfg.blocks, ctx->f->scheduled, ra, block, bb, bb->gvn) - 1;

    // if (IS_PROJ(aa)) { aa = aa->inputs[0]; }
    // if (IS_PROJ(bb)) { bb = bb->inputs[0]; }

    int start = TB_MIN(a[0], b[0]);
    int end   = TB_MAX(a[1], b[1]);
    if (end < aarray_length(block->items)) {
        end += 1;
    }

    printf("  A B:\n");
    FOR_N(i, start, end) {
        printf(
            "  %c %c  ",
            i >= a[0] && i <= a[1] ? '*' : ' ',
            i >= b[0] && i <= b[1] ? '*' : ' '
        );
        tb_print_dumb_node(NULL, block->items[i]);
        printf("\n");
    }
}

// mark program point as HRP
static void mark_point_as_hrp(Ctx* ctx, Rogers* ra, TB_Node* n, int reg_class) {
    // TB_ASSERT(reg_class > 0);
    // TB_OPTDEBUG(REGALLOC6)(printf("#       %%%u is considered HRP\n", n->gvn));

    uint32_t gvn = n->gvn;
    TB_BasicBlock* bb = ctx->f->scheduled[gvn];
    int bb_id = bb - ctx->cfg.blocks;
    int t = ra->order[gvn] - 1;

    int end_t = t;
    while (end_t+1 < aarray_length(bb->items) && IS_PROJ(bb->items[end_t+1]) && bb->items[end_t+1]->inputs[0] == n) {
        end_t++;
    }

    HRPRegion* hrp = &ra->hrp[bb_id];
    if (hrp->start[reg_class] < 0) {
        hrp->start[reg_class] = t;
        hrp->end[reg_class]   = end_t;
    } else {
        hrp->start[reg_class] = TB_MIN(hrp->start[reg_class], t);
        hrp->end[reg_class]   = TB_MAX(hrp->end[reg_class], end_t);
    }
}

static void gimme_lifetime(Ctx* ctx, Rogers* ra, TB_BasicBlock** scheduled, TB_BasicBlock* bb, TB_Node* n, int* range) {
    uint32_t gvn = n->gvn;
    int start_t = 0;
    if (scheduled[gvn] == bb) {
        if (n->type == TB_PHI) {
            start_t = 0;
        } else if (IS_PROJ(n)) {
            uint32_t tuple_gvn = n->inputs[0]->gvn;
            start_t = ra->order[tuple_gvn] - 1;
        } else {
            start_t = ra->order[gvn] - 1;
        }
    } else if (!set_get(&bb->live_in, gvn)) {
        range[0] = INT_MAX;
        range[1] = -1;
        return;
    }

    int end_t = last_use_in_bb(ctx->cfg.blocks, scheduled, ra, bb, n, gvn) - 1;
    TB_ASSERT(end_t >= start_t);

    range[0] = start_t;
    range[1] = end_t;
}

static void mark_node_as_hrp(Ctx* ctx, Rogers* ra, uint32_t gvn, uint32_t failed_gvn, int reg_class) {
    TB_ASSERT(reg_class > 0);
    TB_ASSERT(ctx->vreg_map[gvn]);

    // we're gonna spill it, so that's not really increasing HRP
    if (ctx->vregs[ctx->vreg_map[gvn]].mask->may_spill) {
        return;
    }

    cuikperf_region_start("mark", NULL);
    // TB_OPTDEBUG(REGALLOC6)(printf("#       %%%u /\\ %%%u (%s) is considered HRP range\n", gvn, failed_gvn, reg_class_name(reg_class)));

    TB_BasicBlock** scheduled = ctx->f->scheduled;
    TB_Node* n = ra->gvn2node[gvn];
    TB_Node* failed = ra->gvn2node[failed_gvn];

    // only one def BB

    // each active range outside of the def BB is live in

    FOR_N(bb_id, 0, ctx->bb_count) {
        TB_BasicBlock* bb = &ctx->cfg.blocks[bb_id];
        HRPRegion* hrp = &ra->hrp[bb_id];

        // Fully HRP already
        if (hrp->start[reg_class] == 0 && hrp->end[reg_class] == aarray_length(bb->items)-1) {
            continue;
        }

        int A[2], B[2];
        gimme_lifetime(ctx, ra, scheduled, bb, n,      A);
        gimme_lifetime(ctx, ra, scheduled, bb, failed, B);

        int start_t = TB_MAX(A[0], B[0]);
        int end_t   = TB_MIN(A[1], B[1]);
        if (end_t < 0) {
            continue;
        }

        // TB_OPTDEBUG(REGALLOC6)(printf("#         BB%zu [%d (%%%u), %d (%%%u)] (%u items)\n", bb_id, start_t, bb->items[start_t]->gvn, end_t, bb->items[end_t]->gvn, aarray_length(bb->items)));

        if (hrp->start[reg_class] < 0) {
            hrp->start[reg_class] = start_t;
            hrp->end[reg_class]   = end_t;
        } else {
            hrp->start[reg_class] = TB_MIN(hrp->start[reg_class], start_t);
            hrp->end[reg_class]   = TB_MAX(hrp->end[reg_class], end_t);
        }
        // stats_aaa++;
    }
    cuikperf_region_end();
}

static thread_local VReg* compare_split__vregs;
static int compare_split(const void* a, const void* b) {
    VReg* vregs = compare_split__vregs;
    VReg* aa = &vregs[((const SplitDecision*) a)->target];
    VReg* bb = &vregs[((const SplitDecision*) b)->target];

    float as = aa->spill_cost - aa->area*0.2;
    float bs = bb->spill_cost - bb->area*0.2;
    if (as != bs) {
        return as > bs ? -1 : 1;
    }
    return 0;
}

void tb__rogers(Ctx* restrict ctx, TB_Arena* arena) {
    Rogers ra = { .ctx = ctx, .arena = arena };
    TB_Function* f = ctx->f;
    size_t node_count = f->node_count;

    TB_Worklist* ws = f->worklist;
    worklist_clear(ws);
    ra.uf_len = f->node_count;

    int max_regs_in_class = 0;
    FOR_N(i, 0, ctx->num_classes) {
        size_t count = ctx->num_regs[i];
        if (max_regs_in_class < count) {
            max_regs_in_class = count;
        }
    }
    ra.num_regs = ctx->num_regs;
    ra.max_regs_in_class = max_regs_in_class;

    // used for hard-list list at the very start
    ra.potential_spills = dyn_array_create(int, 32);
    ra.splits = dyn_array_create(SplitDecision, 32);
    bool cisc_stuff = false;

    // PATCH(odin-tilde): wrap every pinned (fixed-mask, non-stack) input
    // edge in a dedicated single-use copy BEFORE any mask meets happen.
    // Stock TB meets pinned use masks (e.g. a SysV call argument forced
    // into RSI) into the value's vreg, so the whole live range becomes
    // fixed to one register; when that range spans a clobbering call or
    // a register-pressure region, the splitter cannot satisfy it (several
    // stock TODOs) and miscompiles or spins forever. The wrapper copy's
    // vreg is fixed but lives for a single point — no clobber or pressure
    // region can span it — while the value vreg stays in a normal mask
    // and is spilled/split by the usual machinery. MACH_COPY nodes are
    // exempt from 2addr coalescing below, so the wrapper cannot merge
    // back into its source.
    CUIK_TIMED_BLOCK("pinned input copies") {
        bool pinned_changes = false;
        FOR_N(i, 0, ctx->bb_count) {
            TB_BasicBlock* bb = &ctx->cfg.blocks[i];
            // snapshot item pointers: insertions shift the array, so
            // indexing into bb->items would skip or reprocess nodes.
            size_t item_count = aarray_length(bb->items);
            TB_Node** items_snapshot = tb_arena_alloc(&f->tmp_arena, item_count * sizeof(TB_Node*));
            memcpy(items_snapshot, bb->items, item_count * sizeof(TB_Node*));
            FOR_N(j, 0, item_count) {
                TB_Node* n = items_snapshot[j];
                if (n->type == TB_PHI || n->type == TB_MACH_COPY ||
                    n->type == TB_MACH_TEMP || n->type == TB_MACH_PROJ ||
                    n->type == TB_MACH_SYMBOL) {
                    continue;
                }

                RegMask** ins = ctx->ins;
                ctx->constraint(ctx, n, ins);

                size_t in_count = n->input_count;
                FOR_N(k, 1, in_count) {
                    TB_Node* in = n->inputs[k];
                    if (in == NULL) { continue; }
                    RegMask* in_mask = ins[k];
                    if (in_mask == NULL || in_mask == &TB_REG_EMPTY) { continue; }
                    if (in_mask->class == REG_CLASS_STK) { continue; }
                    // Classes with no spill mask at all (e.g. FLAGS on
                    // x64) are inherently single-register, their producer
                    // is pinned by its own def constraint, and the splitter
                    // has no spill path for them — wrapping such an edge
                    // only manufactures a pinned, unspillable copy that
                    // crashes mach_copy (NULL spill mask).
                    if (ctx->mayspill_mask[in_mask->class] == NULL) { continue; }
                    if (fixed_reg_mask(in_mask) < 0) { continue; }
                    if (in->type == TB_MACH_TEMP) { continue; }
                    if (in->gvn >= aarray_length(ctx->vreg_map) || ctx->vreg_map[in->gvn] <= 0) { continue; }

                    TB_Node* cpy = tb_alloc_node(f, TB_MACH_COPY, in->dt, 2, sizeof(TB_NodeMachCopy));
                    set_input(f, cpy, in, 1);
                    TB_NODE_SET_EXTRA(cpy, TB_NodeMachCopy, .def = in_mask, .use = ctx->normie_mask[in_mask->class]);

                    VReg* v = tb__set_node_vreg(ctx, cpy);
                    v->mask = in_mask;
                    v->reg_width = tb__reg_width_from_dt(in_mask->class, in->dt);

                    set_input(f, n, cpy, k);
                    tb__insert_before(ctx, f, cpy, n);
                    pinned_changes = true;
                }
            }
        }
        if (pinned_changes) {
            redo_dataflow(ctx, arena);
        }
    }

    // create timeline & insert moves
    CUIK_TIMED_BLOCK("insert legalizing moves") {
        FOR_N(i, 0, ctx->bb_count) {
            TB_BasicBlock* bb = &ctx->cfg.blocks[i];
            FOR_N(j, 0, aarray_length(bb->items)) {
                TB_Node* n = bb->items[j];

                RegMask** ins = ctx->ins;
                ctx->constraint(ctx, n, ins);

                // insert input copies (temporaries & clobbers never introduce
                // these so we're safe don't check those)
                size_t in_count = n->input_count;
                FOR_N(k, 1, in_count) if (n->inputs[k]) {
                    TB_Node* in = n->inputs[k];
                    RegMask* in_mask = ins[k];
                    if (in_mask == &TB_REG_EMPTY) { continue; }

                    VReg* in_vreg = node_vreg(ctx, in);

                    #ifndef NDEBUG
                    // common enough error that i figure i should make a proper error
                    if (in_vreg == NULL) {
                        printf("RA ERROR in %s (%s:%d):\n  ", f->super.name, __FILE__, __LINE__);
                        tb_print_dumb_node(NULL, in);
                        printf("\n  ^^^^^  this node has no vreg even though it's used by %%%u[%zu] in BB%zu:\n  ", n->gvn, k, i);
                        tb_print_dumb_node(NULL, n);
                        printf("\n");
                        fflush(stdout);
                        abort(); // PATCH(odin-tilde): was tb_integrated_dbg(f, n); which hangs on stdin
                    }
                    #endif

                    /* int hint = fixed_reg_mask(in_mask);
                    if (hint >= 0 && in_vreg->mask->class == in_mask->class) {
                        in_vreg->hint_vreg = ra.fixed[in_mask->class] + hint;
                    } */

                    // intersect use masks with the vreg's mask, if it becomes empty we've
                    // got a hard-split (not necessarily spilling to the stack)
                    RegMask* new_mask = tb__reg_mask_meet(ctx, in_vreg->mask, ins[k]);
                    if (in_vreg->mask != &TB_REG_EMPTY && new_mask == &TB_REG_EMPTY) {
                        TB_OPTDEBUG(REGALLOC)(printf("HARD-SPLIT on V%td\n", in_vreg - ctx->vregs));
                        dyn_array_put(ra.potential_spills, in_vreg - ctx->vregs);
                    }

                    in_vreg->mask = new_mask;
                }

                int vreg_id = ctx->vreg_map[n->gvn];
                if (vreg_id > 0) {
                    VReg* vreg = &ctx->vregs[vreg_id];
                    RegMask* def_mask = vreg->mask;

                    vreg->spill_cost = NAN;
                    if (n->type == TB_PHI) {
                        ra.uf_len += n->input_count;
                        worklist_push(ws, n);
                    } else if (ctx->node_2addr(n) >= 0) {
                        ra.uf_len += 1;
                        cisc_stuff = true;
                    }
                }
            }
        }
    }

    // this avoids the subsume_node calls adding nodes to the list, they'd
    // do this if you remove nodes such that they get DCE'd
    f->worklist = NULL;

    // resolving hard-splits
    if (dyn_array_length(ra.potential_spills) > 0) {
        cuikperf_region_start("hard splits", NULL);
        // insert hard split code
        FOR_N(i, 0, dyn_array_length(ra.potential_spills)) {
            VReg* vreg = &ctx->vregs[ra.potential_spills[i]];
            RegMask* mask = ctx->constraint(ctx, vreg->n, NULL);
            vreg->mask = mask;

            if (can_remat(ctx, vreg->n)) {
                rogers_remat(ctx, &ra, vreg->n, true);
            } else {
                spill_entire_lifetime(ctx, vreg, mask, vreg->n, true);
            }

            ra.uf_len += 1;
        }
        dyn_array_clear(ra.potential_spills);
        cuikperf_region_end();

        f->worklist = ws;
        redo_dataflow(ctx, arena);
        f->worklist = NULL;
    }

    ra.coalesce_set = nl_table_alloc(100);
    ra.uf = cuik_malloc(ra.uf_len * sizeof(int));
    ra.uf_size = cuik_malloc(ra.uf_len * sizeof(int));
    FOR_N(i, 0, ra.uf_len) {
        ra.uf[i] = i;
        ra.uf_size[i] = 1;
    }

    bool changes = false;
    if (cisc_stuff || dyn_array_length(ws->items) > 0) {
        cuikperf_region_start("aggro coalesce", NULL);

        TB_ArenaSavepoint sp = tb_arena_save(arena);
        compute_ordinals(ctx, &ra, arena);

        // CISC ops will coalesce with their shared edge, if not they'll insert a copy node
        FOR_REV_N(i, 0, ctx->bb_count) {
            TB_BasicBlock* bb = &ctx->cfg.blocks[i];
            FOR_REV_N(j, 0, aarray_length(bb->items)) {
                TB_Node* n = bb->items[j];
                if (ctx->vreg_map[n->gvn] == 0) { continue; }

                int shared_edge = ctx->node_2addr(n);
                if (shared_edge >= 0 && n->inputs[shared_edge]) {
                    int x = uf_find(ra.uf, ra.uf_len, n->gvn);
                    int vreg_id = ctx->vreg_map[x];
                    TB_Node* in = n->inputs[shared_edge];

                    // this is a necessary copy
                    if (n->type == TB_MACH_COPY) {
                        continue;
                    }

                    int y = uf_find(ra.uf, ra.uf_len, in->gvn);

                    RegMask* in_mask = constraint_in(ctx, n, shared_edge);
                    RegMask* new_mask = tb__reg_mask_meet(ctx, in_mask, ctx->vregs[ctx->vreg_map[y]].mask);
                    new_mask = tb__reg_mask_meet(ctx, new_mask, ctx->vregs[vreg_id].mask);

                    if (!rogers_can_coalesce(ctx, &ra, n, in) || new_mask == &TB_REG_EMPTY) {
                        // insert a copy
                        TB_OPTDEBUG(REGALLOC)(printf("CISC OP %%%u (-> %%%u) has conflict\n", n->gvn, in->gvn));

                        TB_Node* copy = rogers_hard_split(ctx, &ra, n, in, in_mask, vreg_id);
                        // PATCH(odin-tilde): only rewire the shared 2addr edge.
                        // Upstream rewired every input equal to `in` "for
                        // simplicity", but when the same node also feeds
                        // another operand with a tighter constraint (e.g.
                        // `shl x, x` where the amount input is pinned to
                        // RCX), that use was moved onto the fresh copy whose
                        // vreg never had the RCX pin met into it (the
                        // legalizing-moves pass already ran), so the group
                        // could be assigned a register that violates the
                        // pin and the emitter would assert on shl-by-non-CL.
                        set_input(f, n, copy, shared_edge);
                        tb__insert_before(ctx, ctx->f, copy, n);
                        // PATCH(odin-tilde): the copy now sits in n's block —
                        // give it (and its shifted neighbours) true ordinals
                        // (see rogers_renumber_block).
                        rogers_renumber_block(f->scheduled[n->gvn], &ra);

                        if (in->user_count == 0) {
                            // delete the original def
                            ctx->vregs[ctx->vreg_map[in->gvn]].uses -= 1;
                            ctx->vreg_map[in->gvn] = 0;
                            tb__remove_node(ctx, ctx->f, in);
                            tb_kill_node(ctx->f, in);
                        }

                        in = n->inputs[shared_edge];
                        changes = true;
                    } else {
                        ctx->vregs[vreg_id].mask = new_mask;
                    }

                    // hard coalesce with direct input
                    y = uf_find(ra.uf, ra.uf_len, in->gvn);
                    rogers_coalesce(ctx, &ra, x, y, n, in);
                }
            }
        }

        FOR_N(i, 0, dyn_array_length(ws->items)) {
            TB_Node* n = ws->items[i];
            TB_ASSERT(n->type == TB_PHI);

            // join all these into one lifetime, make n the leader
            int x = uf_find(ra.uf, ra.uf_len, n->gvn);
            int vreg_id = ctx->vreg_map[x];

            RegMask* rm = ctx->vregs[vreg_id].mask;
            FOR_N(k, 1, n->input_count) {
                // interfere against everything in the set
                TB_Node* in = n->inputs[k];
                int y = uf_find(ra.uf, ra.uf_len, in->gvn);

                RegMask* new_mask = tb__reg_mask_meet(ctx, rm, ctx->vregs[ctx->vreg_map[y]].mask);
                if (!rogers_can_coalesce(ctx, &ra, n, in) || new_mask == &TB_REG_EMPTY) {
                    TB_OPTDEBUG(REGALLOC)(printf("PHI %%%u (-> %%%u) has self-conflict\n", n->gvn, in->gvn));

                    TB_Node* move = rogers_hard_split(ctx, &ra, n, in, rm, vreg_id);
                    set_input(f, n, move, k);

                    // PATCH(odin-tilde): a cross-class meet can come out empty
                    // here — the isel routes float SELECTs to GPR cmovcc, so an
                    // F32 phi's mask can be stack-only while the remat clone of
                    // an XMM input still constrains to XMM. Keep the previous
                    // mask (a stack-only phi is always legal); the vreg is on
                    // potential_spills and the split pass resolves the copy.
                    // Stock assumed the meet can never be empty.
                    {
                        RegMask* cm = ctx->constraint(ctx, move, NULL);
                        RegMask* nm = tb__reg_mask_meet(ctx, rm, cm);
                        if (nm != &TB_REG_EMPTY) {
                            rm = nm;
                        }
                    }
                    TB_ASSERT(rm != &TB_REG_EMPTY);

                    TB_Node* pred = cfg_get_pred(&ctx->cfg, n->inputs[0], k - 1);
                    TB_BasicBlock* pred_bb = f->scheduled[pred->gvn];
                    TB_ASSERT(pred->input_count != 0 && pred->type != TB_DEAD);

                    if (in->user_count == 0) {
                        // delete the original def
                        ctx->vregs[ctx->vreg_map[in->gvn]].uses -= 1;
                        ctx->vreg_map[in->gvn] = 0;
                        tb__remove_node(ctx, ctx->f, in);
                        tb_kill_node(ctx->f, in);
                    }

                    // place at the end of the pred BB to the phi, basically the latest point
                    insert_op_at_end(ctx, NULL, pred_bb, move);
                    // PATCH(odin-tilde): edge copy landed in pred_bb — true
                    // ordinals for it and its neighbours (see rogers_renumber_block).
                    rogers_renumber_block(pred_bb, &ra);
                    changes = true;
                } else {
                    rm = new_mask;
                }

                // hard coalesce with direct input
                y = uf_find(ra.uf, ra.uf_len, n->inputs[k]->gvn);
                rogers_coalesce(ctx, &ra, x, y, n, n->inputs[k]);
            }

            TB_ASSERT(x == ra.uf[x]); // must've stayed the head
            ctx->vregs[ctx->vreg_map[x]].mask = rm;
        }

        // compute lists of coalesced nodes
        FOR_N(i, 0, ctx->bb_count) {
            TB_BasicBlock* bb = &ctx->cfg.blocks[i];
            aarray_for(j, bb->items) {
                TB_Node* n = bb->items[j];
                int vreg_id = ctx->vreg_map[n->gvn];
                if (vreg_id == 0) { continue; }

                // remap the vregs
                int leader = uf_find(ra.uf, ra.uf_len, n->gvn);
                if (leader != n->gvn) {
                    // PATCH(odin-tilde): a register-required def can never
                    // share a vreg with a stack-pinned class. The stk_only
                    // spill pin is applied during the split phase, but uf
                    // merges are rebuilt per round — a real op coalesced
                    // into the spill class afterwards (vreg_map remap below)
                    // would land in a slot it cannot write (odin-tank
                    // runtime@_make_dynamic_array_len_cap: cmovcc def ->
                    // STACK31, inst2 assert). Keep n on its own vreg; the
                    // copy edge that triggered the coalesce stays
                    // materialized. PHIs are exempt — they legitimately
                    // live in slots (parallel-move emit handles slots).
                    int leader_vid = ctx->vreg_map[leader];
                    RegMask* dm = ctx->constraint(ctx, n, NULL);
                    bool reg_required = dm && dm != &TB_REG_EMPTY &&
                        dm->class != REG_CLASS_STK && !dm->may_spill &&
                        n->type != TB_PHI;
                    bool leader_is_stk = leader_vid > 0 &&
                        (size_t) leader_vid < aarray_length(ctx->vregs) &&
                        ctx->vregs[leader_vid].mask &&
                        ctx->vregs[leader_vid].mask->class == REG_CLASS_STK;
                    if (reg_required && leader_is_stk) {
                        int old_vid = ctx->vreg_map[n->gvn];
                        bool need_new = old_vid == 0 ||
                            (size_t) old_vid >= aarray_length(ctx->vregs) ||
                            ctx->vregs[old_vid].mask == NULL ||
                            ctx->vregs[old_vid].mask->class == REG_CLASS_STK;
                        if (need_new) {
                            VReg* nv = tb__set_node_vreg(ctx, n);
                            nv->mask = dm;
                            nv->reg_width = tb__reg_width_from_dt(dm->class, n->dt);
                        }
                    } else {
                        ctx->vreg_map[n->gvn] = ctx->vreg_map[leader];

                        // PATCH(odin-tilde): rogers_coalesce merges only the uf
                        // sets, never the def constraints. A coalesced node with
                        // an intrinsically fixed def (e.g. umulpair's RDX hi
                        // projection) thus lands in a vreg whose mask never
                        // contains the pin; round 1 then "successfully" colors
                        // the vreg into an arbitrary register and the emitter
                        // reads the wrong half of the hardware product (div-by-10
                        // magic wrong, strconv_decimal@assign loops forever).
                        // Meet the fixed def constraint into the leader vreg.
                        if (dm && dm != &TB_REG_EMPTY && fixed_reg_mask(dm) >= 0 &&
                            leader_vid > 0 && (size_t)leader_vid < aarray_length(ctx->vregs)) {
                            VReg* lv = &ctx->vregs[leader_vid];
                            if (lv->mask && lv->mask != &TB_REG_EMPTY &&
                                lv->mask->class == dm->class) {
                                RegMask* nm = tb__reg_mask_meet(ctx, lv->mask, dm);
                                if (nm != &TB_REG_EMPTY) {
                                    lv->mask = nm;
                                }
                            }
                        }
                    }
                }
            }
        }

        ra.order_cap = 0;
        ra.order = NULL;

        tb_arena_restore(&f->tmp_arena, sp);
        cuikperf_region_end();
    }
    // RA calls might add dead nodes but we don't care
    f->worklist = ws;

    if (changes) {
        // recompute liveness
        redo_dataflow(ctx, arena);
    }

    int starting_spills = ctx->num_regs[REG_CLASS_STK];
    ra.num_spills = starting_spills;

    TB_OPTDEBUG(REGALLOC3)(rogers_dump_sched(ctx, f->node_count), printf("\n"));

    ra.hrp = tb_arena_alloc(arena, ctx->bb_count * sizeof(HRPRegion));
    FOR_N(i, 0, ctx->bb_count) {
        ra.hrp[i].start[0] = -1;
        ra.hrp[i].end[0]   = -1;
    }

    TB_OPTDEBUG(REGSPLIT)(rogers_dump_sched(ctx, f->node_count));

    int rounds = 0;
    size_t last_spills = 0;
    // PATCH(odin-tilde): set by the final pessimistic spill wave; the next
    // iteration re-runs the coloring pass and MUST accept its verdict.
    bool final_check = false;
    cuikperf_region_start("main loop", NULL);
    for (;;) {
        TB_ArenaSavepoint sp = tb_arena_save(arena);

        rounds++;
        TB_OPTDEBUG(REGALLOC)(printf("# ========= Round %d =========\n", rounds));
        // PATCH(odin-tilde): the pinned-wrapper IR this port generates can
        // legitimately need a few hundred rounds on large functions (the
        // loop resolves about one conflict wave per round; e.g. Odin's
        // reflect@write_type_writer needs 283). Round 150 is the
        // pessimistic-bail ceiling: the wave at exactly 512 spills
        // everything spillable, iteration 513 re-colors and must either
        // succeed or bail/ICE loudly below (never spin forever). 512 is
        // ~1.8x the highest observed converging round count (283), so
        // legitimately large functions still converge normally.
        TB_ASSERT(rounds <= 513);

        // PATCH(odin-tilde): global zombie fixed-mask sweep. Splitting can
        // leave a vreg pinned to a single register after the use that
        // required the pin was rewired away; since mask meets are monotone
        // the pin never fades on its own, and a fixed vreg that keeps
        // losing its register spins the split loop forever. Intrinsically
        // pinned defs (projections, temps, pinned copies, stack slots)
        // keep their masks: their own constraint legitimately fixes them.
        if (rounds > 1) {
            // Build per-vreg member lists from vreg_map. The coalesce_set
            // table is NOT authoritative here: split re-rooting scrubs
            // nodes out of it (ra_split.c) while vreg_map still maps those
            // nodes to the same vreg. A call-result projection scrubbed
            // this way is invisible to a coalesce_set scan, so the pin it
            // imposes looks like a relaxable zombie — relaxing it detaches
            // the projection from the register the hardware actually
            // writes (observed: F1's call result "assigned" to RBX with no
            // copy ever moving it out of RAX — sha256 silently wrong).
            size_t sweep_nv = aarray_length(ctx->vregs);
            ArenaArray(TB_Node*)* sweep_members = tb_arena_alloc(&ctx->f->tmp_arena, sweep_nv * sizeof(ArenaArray(TB_Node*)));
            memset(sweep_members, 0, sweep_nv * sizeof(ArenaArray(TB_Node*)));
            {
                size_t vmap_len = aarray_length(ctx->vreg_map);
                FOR_N(bi, 0, ctx->bb_count) {
                    TB_BasicBlock* sbb = &ctx->cfg.blocks[bi];
                    aarray_for(sj, sbb->items) {
                        TB_Node* gn = sbb->items[sj];
                        if (gn->gvn >= vmap_len) { continue; }
                        int vid = ctx->vreg_map[gn->gvn];
                        if (vid <= 0 || vid >= (int)sweep_nv) { continue; }
                        if (sweep_members[vid] == NULL) {
                            sweep_members[vid] = aarray_create(&ctx->f->tmp_arena, TB_Node*, 4);
                        }
                        aarray_push(sweep_members[vid], gn);
                    }
                }
            }

            // NOTE: start at V1 — V0 is the "no vreg" dummy slot. Its data
            // is never initialized by the constraint phase (nothing maps to
            // vreg 0), so it holds uninitialized arena garbage; reading its
            // ->mask dereferences a wild pointer (nondeterministic crashes).
            for (ptrdiff_t vi = 1, vi_end__ = aarray_length(ctx->vregs); vi < vi_end__; ++vi) {
                VReg* v = &ctx->vregs[vi];
                if (v->n == NULL || v->mask == NULL || v->mask == &TB_REG_EMPTY) { continue; }
                int fx = fixed_reg_mask(v->mask);
                if (fx < 0) { continue; }

                // intrinsically pinned defs (projections, temps, stack
                // slots) keep their masks; pinned wrapper copies are only
                // kept while a use actually requires the pin.
                // PATCH(odin-tilde): check EVERY member of the coalesce set,
                // not just the representative v->n — a call-result
                // projection pinned to RAX can be coalesced into a vreg
                // whose leader is an unpinned node (e.g. a 2addr add), and
                // relaxing that vreg's mask detaches the projection from the
                // register the hardware actually writes (observed: F1's
                // call result "assigned" to RBX with no copy ever moving it
                // out of RAX — sha256 round computation silently wrong).
                bool intrinsic = false;
                if (sweep_members[vi] != NULL) {
                    aarray_for(gi, sweep_members[vi]) {
                        TB_Node* gn = sweep_members[vi][gi];
                        if (gn->type != TB_MACH_COPY &&
                            fixed_reg_mask(ctx->constraint(ctx, gn, NULL)) >= 0) {
                            intrinsic = true;
                            break;
                        }
                    }
                }
                // PATCH(odin-tilde): nodes that never appear in any bb's items
                // still carry their hardware pin — implicit projections of
                // mach ops (umulpair hi/lo in RDX/RAX, call results, ...) are
                // materialized by the parent instruction, not scheduled as
                // standalone items, so the sweep above can't see them and
                // their pin would be relaxed just because no single use
                // demands that exact register (the uses take any GPR).
                // Observed: umulpair hi proj unpinned from RDX, assigned RAX,
                // div-by-10 magic silently wrong (strconv_decimal@assign
                // loops forever).
                if (!intrinsic && v->n != NULL && v->n->type != TB_MACH_COPY &&
                    fixed_reg_mask(ctx->constraint(ctx, v->n, NULL)) >= 0) {
                    intrinsic = true;
                }
                if (intrinsic) {
                    continue;
                }

                bool needed = false;
                if (sweep_members[vi] != NULL) {
                    aarray_for(gi, sweep_members[vi]) {
                        FOR_USERS(u, sweep_members[vi][gi]) {
                            if (USERI(u) > 0 && USERI(u) < USERN(u)->input_count) {
                                RegMask* um = constraint_in(ctx, USERN(u), USERI(u));
                                if (um && um->class == v->mask->class && fixed_reg_mask(um) == fx) {
                                    needed = true;
                                    break;
                                }
                            }
                        }
                        if (needed) { break; }
                    }
                }
                if (!needed) {
                    bool is_copy = v->n->type == TB_MACH_COPY;
                    if (is_copy) {
                        TB_NodeMachCopy* mc = TB_NODE_GET_EXTRA(v->n);
                        if (fixed_reg_mask(mc->def) >= 0) {
                            mc->def = ctx->normie_mask[mc->def->class];
                        }
                    }
                    v->mask = ctx->normie_mask[v->mask->class];
                }
            }
        }

        // tb_opt__REGALLOC  = rounds == 1;
        // tb_opt__REGALLOC5 = rounds == 2;
        // foobar = rounds == 1;

        // reset HRP regions
        FOR_N(i, 0, ctx->bb_count) {
            FOR_N(j, 1, ctx->num_classes) {
                ra.hrp[i].start[j] = -1;
                ra.hrp[i].end[j]   = -1;
            }
        }

        cuikperf_region_start("round", NULL);
        bool done;
        if (final_check) {
            // PATCH(odin-tilde): last-chance coloring after the
            // spill-everything wave. If even this pass reports conflicts
            // the function is uncolorable for rogers (e.g. a coalesce web
            // spanning a call while every callee-saved register is pinned
            // by ABI argument values — fmt@fmt_arg). With a bail handler
            // installed (the Odin driver) we longjmp out so the driver
            // can re-emit the function with the peeps-only pipeline;
            // otherwise ICE loudly with the residual decisions.
            ra.soft_final = true;
            done = allocate_loop(ctx, &ra, arena);
            ra.soft_final = false;
            if (!done && tb__ra_bail_active() && getenv("TB_RA_NOBAIL") == NULL) {
                cuik_free(ra.uf);
                cuik_free(ra.uf_size);
                nl_table_free(ra.coalesce_set);
                tb__ra_bail();
            }
            if (!done) {
                fflush(stdout); // flush the final-pass trace before the ICE
                FOR_N(i, 0, dyn_array_length(ra.splits)) {
                    VReg* v = &ctx->vregs[ra.splits[i].target];
                    TB_Node* n = v->n;
                    fprintf(stderr, "[ra] residual conflict V%u", ra.splits[i].target);
                    if (n) { fprintf(stderr, " gvn=%u %s", n->gvn, tb_node_get_name(n->type)); }
                    if (v->mask == NULL) { fprintf(stderr, " mask=NULL"); }
                    else if (v->mask == &TB_REG_EMPTY) { fprintf(stderr, " mask=EMPTY"); }
                    else { fprintf(stderr, " cls=%d fixed=%d bits=%016llx", v->mask->class, fixed_reg_mask(v->mask), (unsigned long long)v->mask->mask[0]); }
                    fprintf(stderr, " assigned=%d was_spilled=%d", v->assigned, v->was_spilled);
                    if (ra.splits[i].clobber) { fprintf(stderr, " clobber"); }
                    if (n && f->scheduled[n->gvn]) {
                        fprintf(stderr, " bb=%td", f->scheduled[n->gvn] - ctx->cfg.blocks);
                    }
                    // failure-path diagnostic: who keeps this value alive
                    if (n) {
                        fprintf(stderr, "\n[ra]   users:");
                        FOR_USERS(u, n) {
                            TB_Node* un = USERN(u);
                            fprintf(stderr, " g%u(%s)", un->gvn, tb_node_get_name(un->type));
                            if (f->scheduled[un->gvn]) {
                                fprintf(stderr, "@[bb%td]", f->scheduled[un->gvn] - ctx->cfg.blocks);
                            }
                        }
                        // odin-tilde: dump sibling FLAGS-class vregs (single-reg class —
                        // any overlap with another live flags range is the uncolorable cause)
                        if (v->mask && v->mask->class == 3) {
                            fprintf(stderr, "\n[ra]   flags-class vregs:");
                            FOR_N(vi, 0, aarray_length(ctx->vregs)) {
                                VReg* sv = &ctx->vregs[vi];
                                if (sv->mask == NULL || sv->mask->class != 3 || sv->n == NULL) { continue; }
                                fprintf(stderr, " V%zu=g%u(%s)", vi, sv->n->gvn, tb_node_get_name(sv->n->type));
                                if (f->scheduled[sv->n->gvn]) {
                                    fprintf(stderr, "@[bb%td]", f->scheduled[sv->n->gvn] - ctx->cfg.blocks);
                                }
                                fprintf(stderr, " assigned=%d", sv->assigned);
                            }
                        }
                    }
                    // failure-path diagnostic: dump mach_copy endpoints and input chain
                    if (n && n->type == TB_MACH_COPY) {
                        TB_NodeMachCopy* mc = TB_NODE_GET_EXTRA(n);
                        fprintf(stderr, "\n[ra]   copy def: ");
                        if (mc->def == NULL) { fprintf(stderr, "NULL"); }
                        else if (mc->def == &TB_REG_EMPTY) { fprintf(stderr, "EMPTY"); }
                        else { fprintf(stderr, "cls=%d mayspill=%d fixed=%d bits=%016llx", mc->def->class, mc->def->may_spill, fixed_reg_mask(mc->def), (unsigned long long)mc->def->mask[0]); }
                        fprintf(stderr, " use: ");
                        if (mc->use == NULL) { fprintf(stderr, "NULL"); }
                        else if (mc->use == &TB_REG_EMPTY) { fprintf(stderr, "EMPTY"); }
                        else { fprintf(stderr, "cls=%d mayspill=%d fixed=%d bits=%016llx", mc->use->class, mc->use->may_spill, fixed_reg_mask(mc->use), (unsigned long long)mc->use->mask[0]); }
                        fprintf(stderr, " dt=%d", n->dt.type);
                        TB_Node* src = n->inputs[1];
                        if (src) {
                            fprintf(stderr, " src(gvn=%u %s)", src->gvn, tb_node_get_name(src->type));
                            if (src->type == TB_MACH_COPY) {
                                TB_NodeMachCopy* sc = TB_NODE_GET_EXTRA(src);
                                fprintf(stderr, " src.def: ");
                                if (sc->def == NULL) { fprintf(stderr, "NULL"); }
                                else if (sc->def == &TB_REG_EMPTY) { fprintf(stderr, "EMPTY"); }
                                else { fprintf(stderr, "cls=%d fixed=%d bits=%016llx", sc->def->class, fixed_reg_mask(sc->def), (unsigned long long)sc->def->mask[0]); }
                            }
                        }
                        TB_Node* real_def = n;
                        while (real_def->type == TB_MACH_COPY && real_def->inputs[1]) { real_def = real_def->inputs[1]; }
                        fprintf(stderr, " root_def(gvn=%u %s)", real_def->gvn, tb_node_get_name(real_def->type));
                    }
                    fprintf(stderr, "\n");
                }
            }
            TB_ASSERT_MSG(done, "rogers: spill-everything wave still has conflicts (uncolorable def point in %s)", f->super.name);
        } else {
            done = allocate_loop(ctx, &ra, arena);
        }
        // rogers_dump_sched(ctx, f->node_count);
        cuikperf_region_end();

        if (0) {
            printf("=== ROUND %d === (%zu)\n", rounds, f->node_count);
            for (int i = 0; i < 10000; i++) {
                if (histo[i]) {
                    printf("%d;%d\n", i, histo[i]);
                    histo[i] = 0;
                }
            }
        }

        if (done) {
            // printf("STATS: %d\n", stats_aaa);
            TB_OPTDEBUG(REGALLOC3)(rogers_dump_sched(ctx, f->node_count));

            // TEMP DIAG: TB_OPT_RA_DUMP=<substr> dumps the post-RA schedule
            { static const char* ra_dump = (const char*)-1;
              if (ra_dump == (const char*)-1) { const char* s = getenv("TB_OPT_RA_DUMP"); ra_dump = s; }
              if (ra_dump && f->super.name && strstr(f->super.name, ra_dump)) {
                  rogers_dump_sched(ctx, f->node_count);
              }
            }

            tb_arena_restore(arena, sp);
            cuikperf_region_end();

            cuik_free(ra.uf);
            cuik_free(ra.uf_size);
            nl_table_free(ra.coalesce_set);

            #if 0
            // dump_sched(ctx);
            FOR_N(i, 0, f->node_count) {
                int x = ctx->vreg_map[i];
                if (x > 0) {
                    TB_Node* xn = ra.gvn2node[i];
                    FOR_N(j, i+1, f->node_count) {
                        int y = ctx->vreg_map[j];
                        TB_Node* yn = ra.gvn2node[j];

                        if (y > 0 && interfere(ctx, &ra, xn, yn)) {
                            if (ctx->vregs[x].class == ctx->vregs[y].class && ctx->vregs[x].assigned == ctx->vregs[y].assigned) {
                                printf("V%u (%%%u) interfering with V%u (%%%u): ", x, xn->gvn, y, yn->gvn);
                                print_reg_name(ctx->vregs[x].class, ctx->vregs[x].assigned);
                                printf("\n");
                            }
                        }
                    }
                }
            }
            #endif

            if (ctx->features.gen & TB_FEATURE_STACK_MAPS) {
                CUIK_TIMED_BLOCK("build stack maps") {
                    // Sparse set repr
                    TB_ArenaSavepoint sp = tb_arena_save(&f->tmp_arena);
                    ArenaArray(int) stack = aarray_create(&f->tmp_arena, int, 30);
                    int* array = tb_arena_alloc(&f->tmp_arena, f->node_count * sizeof(int));
                    FOR_REV_N(i, 0, ctx->bb_count) {
                        TB_BasicBlock* bb = &ctx->cfg.blocks[i];

                        aarray_clear(stack);
                        FOR_N(i, 0, f->node_count) {
                            array[i] = -1;
                        }

                        // start intervals
                        BITS64_FOR(j, bb->live_out.data, bb->live_out.capacity) {
                            if (array[j] < 0 && is_gcref_dt(ra.gvn2node[j]->dt)) {
                                array[j] = aarray_length(stack);
                                aarray_push(stack, j);
                            }
                        }

                        FOR_REV_N(j, 0, aarray_length(bb->items)) {
                            TB_Node* n = bb->items[j];

                            // expire
                            if (array[n->gvn] >= 0) {
                                int last_gvn = stack[aarray_length(stack) - 1];
                                aarray_remove(stack, array[n->gvn]);
                                array[last_gvn] = array[n->gvn];
                                array[n->gvn] = -1;
                            }

                            if (tb_node_is_safepoint(n)) {
                                TB_Safepoint* sfpt = tb_arena_alloc(ctx->emit.arena, sizeof(TB_Safepoint) + aarray_length(stack)*sizeof(int32_t));
                                sfpt->func = ctx->f;
                                sfpt->node = n;
                                sfpt->count = aarray_length(stack);

                                aarray_for(k, stack) {
                                    TB_ASSERT(ctx->vreg_map[stack[k]] > 0);
                                    VReg* v = &ctx->vregs[ctx->vreg_map[stack[k]]];
                                    int ref_type = ra.gvn2node[stack[k]]->dt.elem_or_addrspace;
                                    sfpt->refs[k] = (v->class << 24u) | (v->assigned << 8u) | ref_type;
                                }

                                TB_NodeSafepoint* n_sfpt = TB_NODE_GET_EXTRA(n);
                                sfpt->userdata = n_sfpt->userdata;
                                n_sfpt->sfpt = sfpt;
                            }

                            // start intervals
                            if (n->type != TB_PHI) {
                                FOR_N(k, 1, n->input_count) {
                                    TB_Node* in = n->inputs[k];
                                    if (in && is_gcref_dt(in->dt)) {
                                        // alive
                                        if (array[in->gvn] < 0) {
                                            array[in->gvn] = aarray_length(stack);
                                            aarray_push(stack, in->gvn);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    tb_arena_restore(&f->tmp_arena, sp);
                }
            }

            ctx->num_spills += ra.num_spills - starting_spills;
            // printf("ROUNDS ON %s: %d\n", f->super.name, rounds);
            if (ra.pc_audit_checked || ra.pc_reg_checked) {
                fprintf(stderr, "[pc-audit] %s: rounds=%d vregs=%zu precolor checked=%lu bad=%lu | allocate_reg checked=%lu bad=%lu stale=%lu | expire checked=%lu\n", f->super.name, rounds, aarray_length(ctx->vregs),
                    ra.pc_audit_checked, ra.pc_audit_bad, ra.pc_reg_checked, ra.pc_reg_bad, ra.pc_audit_stale, ra.pc_exp_checked);
            }
            if (getenv("TB_PC_STATS") != NULL) {
                // precolor: narrow queries / other queries / narrow sleeper candidates (of which sure) / narrow fixed candidates /
                //           register-bucket sleeper candidates / fixed-index candidates;
                // allocate_reg: narrow queries / their candidates / full scans; interfere() calls / last_use_in_bb misses
                fprintf(stderr, "[pc-stats] %s: rounds=%d nodes=%zu vregs=%zu | precolor narrow=%llu other=%llu cand=%llu sure=%llu fcand=%llu bucket=%llu fidx=%llu | allocate_reg narrow=%llu cand=%llu full=%llu | interfere=%llu lastuse_miss=%llu\n",
                    f->super.name, rounds, f->node_count, aarray_length(ctx->vregs),
                    ra.pc_stat[0], ra.pc_stat[1], ra.pc_stat[2], ra.pc_stat[8], ra.pc_stat[3], ra.pc_stat[4], ra.pc_stat[5],
                    ra.pc_stat[6], ra.pc_stat[7], ra.pc_stat[9], ra.pc_stat[10], ra.pc_stat[11]);
            }
            return;
        }

        // gonna be modding shit too much for it to matter.
        ra.inactive_cache = NULL;
        cuikperf_region_start("insert spills", NULL);

        TB_OPTDEBUG(REGALLOC)(printf("\n\n=== SPILLING ===\n"));

        if (0) {
            size_t num_spills = dyn_array_length(ra.splits);
            int dt = (int)num_spills - (int)last_spills;
            if (rounds == 1) {
                dt = 0;
            }

            printf("AAA %-3d %zu (\x1b[%dm%+d\x1b[0m), %u\n", rounds, num_spills, dt < 0 ? 32 : 31, dt, aarray_length(ctx->vregs));
            last_spills = num_spills;
        }

        // PATCH(odin-tilde): pessimistic convergence bail. Some optimized
        // functions hit a family of split-loop deadlocks (pinned argument
        // registers vs clobber-narrowed defs, pinned phis vs pinned
        // sleepers, ...) where conflict waves are re-created as fast as
        // they're resolved and the round count climbs forever (verified:
        // repeating this wave every 128 rounds past 8064 never converges).
        // At the ceiling, discard this round's decisions and split EVERY
        // non-pinned register vreg in one wave, with was_spilled pre-set
        // so the splitter takes the immediate-store proper-spill path
        // everywhere. Register pressure then collapses to pinned values
        // plus single-point defs; the next round re-colors once and must
        // succeed (see final_check above).
        if (rounds == 512) {
            dyn_array_clear(ra.splits);
            size_t n_picked = 0;
            FOR_N(i, 1, aarray_length(ctx->vregs)) {
                VReg* v = &ctx->vregs[i];
                if (v->mask == NULL) { continue; }
                if (v->mask == &TB_REG_EMPTY) {
                    // "must spill" signal: must go THROUGH the splitter
                    // (see the filter loop below), so queue it even though
                    // it has no usable mask.
                    dyn_array_put(ra.splits, ((SplitDecision){ .target = (uint32_t)i }));
                    n_picked++;
                    continue;
                }
                if (reg_mask_is_stack(v->mask)) { continue; }
                if (ctx->mayspill_mask[v->mask->class] == NULL) { continue; }
                if (fixed_reg_mask(v->mask) >= 0) { continue; }
                TB_Node* vn = v->n;
                if (vn == NULL || vn->type == TB_NULL) { continue; }
                if ((size_t)vn->gvn >= aarray_length(ctx->vreg_map)) { continue; }
                if ((size_t)vn->gvn >= ra.uf_len) { continue; }
                if (uf_find(ra.uf, ra.uf_len, vn->gvn) != vn->gvn) { continue; }
                if (f->scheduled[vn->gvn] == NULL) { continue; }
                if (v->was_spilled < 1) { v->was_spilled = 1; }
                dyn_array_put(ra.splits, ((SplitDecision){ .target = (uint32_t)i }));
                n_picked++;
            }
            fprintf(stderr, "[ra] rogers: %s hit round ceiling, spill-everything wave picked %zu vregs\n",
                f->super.name, n_picked);
            final_check = true;
        }

        // alloc failures on may_spill vregs will just drop to stack
        for (size_t i = 0; i < dyn_array_length(ra.splits);) {
            SplitDecision split = ra.splits[i];
            VReg* v = &ctx->vregs[split.target];
            // PATCH(odin-tilde): a NULL mask can appear on a vreg whose
            // constraint phase was skipped (defensive; stock would crash
            // dereferencing it below).
            if (v->mask == NULL) {
                dyn_array_remove(ra.splits, i);
                continue;
            }
            // PATCH(odin-tilde): TB_REG_EMPTY (the mask meet's "must spill"
            // signal) must go THROUGH the splitter, not be dropped and not
            // be pinned straight to STK: the splitter rewires every use to
            // a fresh reload vreg with a normie GPR mask, which is the only
            // way to escape the poisoned empty meet. Dropping re-queues the
            // identical conflict forever (fmt@fmt_bit_set, 2048 rounds);
            // direct STK pinning leaves register-only uses (e.g. an x86_mov
            // base operand) pointing at a stack slot (runtime@print_type).
            // PATCH(odin-tilde): a split decision on a vreg whose class has
            // NO spill mask (e.g. FLAGS) and which cannot be rematerialized
            // can never be executed by the splitter — it would manufacture
            // a NULL-mask spill copy and crash (reg_mask_is_stack(NULL)).
            // Drop it; the conflicting party will move instead.
            if (ctx->mayspill_mask[v->mask->class] == NULL && !can_remat(ctx, v->n)) {
                dyn_array_remove(ra.splits, i);
                continue;
            }
            if (v->mask->may_spill) {
                TB_OPTDEBUG(REGALLOC)(printf("  * V%u is now a proper spill\n", split.target));

                v->spill_cost = INFINITY;
                v->mask = intern_regmask(ctx, REG_CLASS_STK, true, 0);
                v->reg_width = tb__reg_width_from_dt(REG_CLASS_STK, v->n->dt);

                // reset assignment, it's going on the stack next time
                v->class = 0;
                v->assigned = -1;

                dyn_array_remove(ra.splits, i);
            } else {
                i += 1;
            }
        }

        // printf("  %zu\n", dyn_array_length(ra.splits));


        size_t num_spills = dyn_array_length(ra.splits);
        if (num_spills) {
            #if 0
            rogers_dump_sched(ctx, f->node_count);
            printf("SPILLS %zu\n", num_spills);
            FOR_N(i, 0, num_spills) {
                int vreg_id = ra.splits[i].target;

                size_t cnt;
                TB_Node** arr = coalesce_set_array(ctx, &ra, &ctx->vregs[vreg_id].n, &cnt);
                FOR_N(j, 0, cnt) {
                    TB_Node* n = arr[j];

                    TB_BasicBlock* bb = f->scheduled[n->gvn];
                    int t = ra.order[n->gvn];

                    ctx->print_pretty(ctx, n);
                    printf(" BB%zu:%d (%d uses)\n", bb - ctx->cfg.blocks, t, n->user_count);

                    FOR_USERS(u, n) {
                        TB_Node* un = USERN(u);

                        TB_BasicBlock* bb = f->scheduled[un->gvn];
                        int t = ra.order[un->gvn];

                        printf("  ");
                        ctx->print_pretty(ctx, un);
                        printf(" BB%zu:%d\n", bb - ctx->cfg.blocks, t);
                    }
                }
                printf("\n");
            }
            exit(0);
            #endif

            // reset assignment, but don't try to split them this round
            // PATCH(odin-tilde): upstream throttled split insertion to
            // 64/round, deferring the rest to later full-recolor rounds;
            // on huge functions (core:reflect write_type_writer, 1540 BBs)
            // that forced ~283 rounds. ra_split.c still has a hard 64-per-
            // call limit (uint64 bitsets indexed by spill slot), so insert
            // in batches of 64 within the same round instead: same eventual
            // splits, far fewer recolor rounds. Order by spill cost so the
            // most expensive conflicts are split first.
            enum { MAX_SPLITS_PER_CALL = 64 };
            if (num_spills > MAX_SPLITS_PER_CALL) {
                compare_split__vregs = ctx->vregs;
                qsort(ra.splits, num_spills, sizeof(SplitDecision), compare_split);
            }

            for (size_t base = 0; base < num_spills; base += MAX_SPLITS_PER_CALL) {
                size_t batch = num_spills - base;
                if (batch > MAX_SPLITS_PER_CALL) {
                    batch = MAX_SPLITS_PER_CALL;
                }
                tb__insert_splits(ctx, &ra, ra.splits + base, batch);
            }
            dyn_array_clear(ra.splits);
        }

        // PATCH(odin-tilde): materialize non-copy uses of stack-resident
        // vregs every round (see ra_split.c) — also covers rounds whose
        // only stack-pinnings came from the no-split "proper spill" net.
        materialize_stk_uses(ctx, &ra);

        cuikperf_region_end();
        tb_arena_restore(arena, sp);
        redo_dataflow(ctx, arena);
    }
}

enum { INACTIVE_CACHE_LOG2 = 9 };

#if TB_OPTDEBUG_STATS
static int stats_c = 0;
#endif

static uint32_t inactive_hash_index(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9U;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebU;
    x ^= x >> 31;

    // grab top bits
    return x >> (64ull - INACTIVE_CACHE_LOG2);
}

static int last_use_in_bb(TB_BasicBlock* blocks, TB_BasicBlock** scheduled, Rogers* restrict ra, TB_BasicBlock* bb, TB_Node* n, uint32_t n_gvn) {
    // printf("Last use in BB%zu for %%%u\n", bb - blocks, n->gvn);

    #if TB_OPTDEBUG_STATS
    stats_c++;
    #endif

    uint64_t key = n_gvn | ((bb - blocks) << 32ull);
    int hash_index = inactive_hash_index(key);
    TB_ASSERT(hash_index < (1ull << INACTIVE_CACHE_LOG2));
    if (ra->inactive_cache && ra->inactive_cache[hash_index].key == key) {
        #if TB_OPTDEBUG_STATS
        stats_hit += 1;
        #endif

        return ra->inactive_cache[hash_index].last_use;
    }

    ra->pc_stat[11]++;
    #if TB_OPTDEBUG_STATS
    stats_miss += 1;
    #endif

    int l = 1;
    if (set_get(&bb->live_out, n->gvn)) {
        // if there's no uses, we'll assume it's live out so the
        // "last use" is the BB end
        l = aarray_length(bb->items);
    } else {
        if (scheduled[n->gvn] == bb) {
            l = ra->order[n->gvn];
        }

        FOR_USERS(u, n) {
            TB_Node* un = USERN(u);
            if (USERI(u) < un->input_count &&
                scheduled[un->gvn] == bb &&
                l < ra->order[un->gvn]) {
                l = ra->order[un->gvn];
            }
        }
    }

    if (ra->inactive_cache) {
        ra->inactive_cache[hash_index].key = key;
        ra->inactive_cache[hash_index].last_use = l;
    }
    return l;
}

static bool order_after(int* order, TB_BasicBlock** scheduled, uint32_t a_gvn, uint32_t b_gvn) {
    // the basic blocks are sequentially allocated in reverse post-order
    return scheduled[a_gvn] > scheduled[b_gvn] || (scheduled[a_gvn] == scheduled[b_gvn] && order[a_gvn] > order[b_gvn]);
}

static bool interfere_in_block(TB_BasicBlock* blocks, int* order, TB_BasicBlock** scheduled, Rogers* restrict ra, TB_Node* lhs, TB_Node* rhs, uint32_t lhs_gvn, uint32_t rhs_gvn, TB_BasicBlock* block) {
    // PATCH(odin-tilde): soft-final evictions requeue nodes and can end up
    // querying a node against itself (parse_i64_maybe_prefixed after the
    // spill-everything wave at stage>=1). A live range never interferes with
    // itself — treat as no conflict instead of asserting.
    if (lhs == rhs || lhs_gvn == rhs_gvn) { return false; }
    TB_ASSERT(lhs != rhs && "i... why?");

    // PATCH(odin-tilde): two phis in the same block are both defined at
    // the entry, so their live ranges overlap there. Coalescing them put
    // big._itoa_raw_full's `q, temp = temp, q` in one stack slot: after the
    // first iteration both names were the quotient, destroy freed it twice
    // and leaked the copy. Phi inputs are uses at the predecessor edge and
    // were not always in live_out, so the check below missed the overlap.
    if (lhs->type == TB_PHI && rhs->type == TB_PHI &&
        scheduled[lhs_gvn] != NULL && scheduled[lhs_gvn] == scheduled[rhs_gvn]) {
        return true;
    }

    // PATCH(odin-tilde): the two data projections of a widening multiply are
    // one instruction. a64 isel writes the low half, then moves the high half
    // out of a scratch; if both projections share a register that move
    // clobbers the low half and big.itoa remainders index the wrong digit.
    // x64 pins them to RAX/RDX, so they already cannot alias.
    if (lhs->type == TB_PROJ && rhs->type == TB_PROJ &&
        lhs->inputs[0] == rhs->inputs[0] && lhs->inputs[0] &&
        (lhs->inputs[0]->type == TB_UMULPAIR || lhs->inputs[0]->type == TB_SMULPAIR)) {
        return true;
    }

    // phis might have a liveness hole in the middle
    bool lhs_live_out = set_get(&block->live_out, lhs_gvn);
    bool rhs_live_out = set_get(&block->live_out, rhs_gvn);
    if (lhs_live_out && rhs_live_out) {
        return true;
    } else if (!lhs_live_out && !rhs_live_out) {
        TB_Node *first = lhs, *last = rhs;
        uint32_t first_gvn = lhs_gvn, last_gvn = rhs_gvn;
        if (order_after(order, scheduled, lhs_gvn, rhs_gvn)) {
            first = rhs, last = lhs;
            first_gvn = rhs_gvn, last_gvn = lhs_gvn;
        }

        int last_t = order[last_gvn];
        int last_use = last_use_in_bb(blocks, scheduled, ra, block, first, first_gvn);
        return scheduled[last_gvn] == block && last_use > last_t;
    } else {
        if (lhs_live_out) {
            SWAP(TB_Node*, lhs, rhs);
            SWAP(uint32_t, lhs_gvn, rhs_gvn);
        }

        int rhs_t = scheduled[rhs_gvn] == block ? ra->order[rhs_gvn] : 0;
        int last_use = last_use_in_bb(blocks, scheduled, ra, block, lhs, lhs_gvn);
        return last_use > rhs_t;
    }
}

static bool interfere(Ctx* restrict ctx, Rogers* restrict ra, TB_Node* lhs, TB_Node* rhs) {
    ra->pc_stat[10]++;
    TB_BasicBlock** scheduled = ctx->f->scheduled;
    int* order = ra->order;

    uint32_t lhs_gvn = lhs->gvn, rhs_gvn = rhs->gvn;
    TB_BasicBlock* lhs_block = scheduled[lhs_gvn];
    TB_BasicBlock* rhs_block = scheduled[rhs_gvn];

    if (interfere_in_block(ctx->cfg.blocks, order, scheduled, ra, lhs, rhs, lhs_gvn, rhs_gvn, lhs_block)) {
        return true;
    }

    return lhs_block != rhs_block && interfere_in_block(ctx->cfg.blocks, order, scheduled, ra, rhs, lhs, rhs_gvn, lhs_gvn, rhs_block);
}

// arena-backed growable array (the per-round lists of the sleeper scans)
static void pc_push(TB_Arena* arena, void** arr, uint32_t* len, uint32_t* cap, size_t elem_size, const void* v) {
    if (*len == *cap) {
        uint32_t new_cap = *cap ? *cap * 2 : 16;
        void* new_arr = tb_arena_alloc(arena, new_cap * elem_size);
        if (*len) { memcpy(new_arr, *arr, *len * elem_size); }
        *arr = new_arr;
        *cap = new_cap;
    }
    memcpy((char*)*arr + (*len) * elem_size, v, elem_size);
    (*len)++;
}

static bool pc_listed(const uint32_t* list, uint32_t len, uint32_t v) {
    FOR_N(i, 0, len) {
        if (list[i] == v) { return true; }
    }
    return false;
}

// `gvn`, a node of block `b`, went to sleep while the pre-allocate loop was
// already below it, or outside its block's walk: a query there can only learn
// about it from the block's list. (The coloring walk reads its block's
// ranges instead, see pc_narrow_candidates.)
static void pc_note_behind(Rogers* ra, size_t b, uint32_t gvn, int ordinal) {
    if (ra->pc_last_walk) { return; }
    bool behind = b != ra->pc_walk_bb || ordinal < ra->pc_item_pos;
    if (behind && !pc_listed(ra->pc_pre[b], ra->pc_pre_len[b], gvn)) {
        pc_push(ra->arena, (void**)&ra->pc_pre[b], &ra->pc_pre_len[b], &ra->pc_pre_cap[b], sizeof(uint32_t), &gvn);
    }
}

// PATCH(odin-tilde): the pre-allocate loop and the coloring walk both go
// backwards over the items; the sleeper scans below answer from where the
// walk is (see precolor_conflicts and pc_sleeper_hits).

// a block's walk begins: nothing of it has been passed yet, so no fixed-mask
// member is live across the current point
static void pc_walk_block(Rogers* ra, size_t bb_id) {
    ra->pc_walk_bb = bb_id;
    ra->pc_item_pos = INT_MAX;
    FOR_N(t, 0, ra->pc_fx_act_len) {
        ra->pc_fx_slot[ra->pc_fx_act[t]] = -1;
    }
    ra->pc_fx_act_len = 0;
}

// what last_use_in_bb answers for `m` in `bb`: the block's length when m is
// live-out, else the highest position of its uses in the block (its own
// definition when it has none)
static int pc_last_use(Ctx* ctx, Rogers* ra, TB_BasicBlock* bb, TB_Node* m) {
    if (set_get(&bb->live_out, m->gvn)) { return (int)aarray_length(bb->items); }
    int last = ra->order[m->gvn];
    FOR_USERS(u, m) {
        TB_Node* un = USERN(u);
        if (USERI(u) < un->input_count && ctx->f->scheduled[un->gvn] == bb && ra->order[un->gvn] > last) {
            last = ra->order[un->gvn];
        }
    }
    return last;
}

// the coloring walk met a use of `in` and could not wake it: the value stays
// asleep with a use above any lower position, so it is behind the walk's back
// a value of the current block went into the live-across set: `last` is
// where it stops being live
static void pc_fx_insert(Rogers* ra, uint32_t gvn, uint32_t last) {
    if (ra->pc_fx_act_len == ra->pc_fx_act_cap) {
        uint32_t new_cap = ra->pc_fx_act_cap ? ra->pc_fx_act_cap * 2 : 16;
        uint32_t* gvns = tb_arena_alloc(ra->arena, new_cap * sizeof(uint32_t));
        uint32_t* lasts = tb_arena_alloc(ra->arena, new_cap * sizeof(uint32_t));
        if (ra->pc_fx_act_len) {
            memcpy(gvns, ra->pc_fx_act, ra->pc_fx_act_len * sizeof(uint32_t));
            memcpy(lasts, ra->pc_fx_act_last, ra->pc_fx_act_len * sizeof(uint32_t));
        }
        ra->pc_fx_act = gvns;
        ra->pc_fx_act_last = lasts;
        ra->pc_fx_act_cap = new_cap;
    }
    ra->pc_fx_act[ra->pc_fx_act_len] = gvn;
    ra->pc_fx_act_last[ra->pc_fx_act_len] = last;
    ra->pc_fx_act_len++;
    ra->pc_fx_slot[gvn] = (int)ra->pc_fx_act_len - 1;
}

// the value defined by the item at `pos` of `bb`, when it is live across more
// than one position: live across (definition, last use)
static void pc_fx_range(Ctx* ctx, Rogers* ra, TB_BasicBlock* bb, int pos, uint32_t* ranges) {
    TB_Node* m = bb->items[pos];
    if (ctx->vreg_map[m->gvn] <= 0) { return; }
    int last = pc_last_use(ctx, ra, bb, m);
    if (last > pos + 1) {
        uint32_t n = ra->pc_fx_sorted++;
        ranges[n * 2] = m->gvn;
        ranges[n * 2 + 1] = (uint32_t)last;
        ra->pc_fx_cnt[last]++;
    }
}

// The values of `bb` that are live across more than one position of it —
// every node with a vreg, or only the members of fixed-mask vregs — ordered
// by where they stop being live (the walk meets the high positions first).
// The walk sees a node's uses before its definition, so "live across the
// current point" cannot be read off the uses passed so far: it is the range
// between the definition and the last use, known up front.
static void pc_fixed_block(Ctx* ctx, Rogers* ra, TB_BasicBlock* bb, bool all_vregs) {
    size_t items = aarray_length(bb->items);
    if (items + 2 > ra->pc_max_items) {
        ra->pc_max_items = items + 2;
        ra->pc_fx_cnt = tb_arena_alloc(ra->arena, ra->pc_max_items * sizeof(int));
    }
    memset(ra->pc_fx_cnt, 0, (items + 2) * sizeof(int));
    ra->pc_fx_sorted = 0;
    ra->pc_fx_cursor = 0;
    ra->pc_fx_sort = NULL;
    ra->pc_fx_block = bb - ctx->cfg.blocks;

    // at most one item per position
    uint32_t* ranges = tb_arena_alloc(ra->arena, (items + 1) * 2 * sizeof(uint32_t));
    size_t base = ra->pc_pos_off[bb - ctx->cfg.blocks];
    FOR_N(pos_idx, 0, (all_vregs ? items : (items + 63) / 64)) {
        int pos = (int)pos_idx;
        if (!all_vregs) {
            uint64_t bits = ra->pc_fxbits[base + pos_idx];
            while (bits) {
                int bit = tb_ffs64(bits) - 1;
                bits &= bits - 1;
                pc_fx_range(ctx, ra, bb, pos_idx * 64 + bit, ranges);
            }
            continue;
        }
        pc_fx_range(ctx, ra, bb, pos, ranges);
    }
    if (ra->pc_fx_sorted == 0) { return; }

    // counting sort, highest last use first
    uint32_t* sorted = tb_arena_alloc(ra->arena, ra->pc_fx_sorted * 2 * sizeof(uint32_t));
    int running = 0;
    FOR_REV_N(k, 0, (ptrdiff_t)(items + 1)) {
        int old = ra->pc_fx_cnt[k];
        ra->pc_fx_cnt[k] = running;
        running += old;
    }
    FOR_N(t, 0, ra->pc_fx_sorted) {
        int last = (int)ranges[t * 2 + 1];
        int at = ra->pc_fx_cnt[last]++;
        sorted[at * 2] = ranges[t * 2];
        sorted[at * 2 + 1] = (uint32_t)last;
    }
    ra->pc_fx_sort = sorted;
}

// the walk is about to query the item at `pos`
static void pc_fixed_arrive(Rogers* ra, int pos) {
    // a member is live across `pos` when its last use is below: last use > pos
    while (ra->pc_fx_cursor < ra->pc_fx_sorted && ra->pc_fx_sort[ra->pc_fx_cursor * 2 + 1] > (uint32_t)pos) {
        uint32_t t = ra->pc_fx_cursor++;
        pc_fx_insert(ra, ra->pc_fx_sort[t * 2], ra->pc_fx_sort[t * 2 + 1]);
    }
}

// the walk has passed the item `n`: its definition ends a value's range there
static void pc_fixed_sweep(Ctx* ctx, Rogers* ra, TB_Node* n) {
    (void)ctx;
    int slot = ra->pc_fx_slot[n->gvn];
    if (slot >= 0) {
        uint32_t last = ra->pc_fx_act[--ra->pc_fx_act_len];
        uint32_t last_of_last = ra->pc_fx_act_last[ra->pc_fx_act_len];
        ra->pc_fx_act[slot] = last;
        ra->pc_fx_act_last[slot] = last_of_last;
        ra->pc_fx_slot[last] = slot;
        ra->pc_fx_slot[n->gvn] = -1;
    }
}

static void put_to_sleep(Rogers* restrict ra, int class, int gvn) {
    TB_OPTDEBUG(REGALLOC)(printf("#   \x1b[33msleep\x1b[0m  %%%u\n", gvn));
    sparse_set_put(&ra->future_active[class], gvn);

    // PATCH(odin-tilde): remember where the sleeper lives (never cleared;
    // the scans re-check that it is still asleep) — see the comment before
    // pc_sleeper_hits.
    if (ra->pc_slept != NULL) {
        TB_Node* m = ra->gvn2node[gvn];
        TB_BasicBlock* mb = m != NULL ? ra->ctx->f->scheduled[gvn] : NULL;
        if (mb != NULL) {
            size_t b = (size_t)(mb - ra->ctx->cfg.blocks);
            int pos = ra->order[gvn] - 1;
            if (b < ra->pc_blocks && pos >= 0 && (size_t)pos < aarray_length(mb->items)) {
                ra->pc_slept[ra->pc_pos_off[b] + pos / 64] |= 1ull << (pos % 64);
                pc_note_behind(ra, b, gvn, pos + 1);
            }
        }
    }
}

static void put_to_sleep_vreg(Ctx* restrict ctx, Rogers* restrict ra, int class, size_t cnt, TB_Node** arr, TB_Node* n, int vreg_id) {
    FOR_N(j, 0, cnt) {
        if (arr[j] != n) {
            TB_ASSERT(arr[j]->gvn < ctx->f->node_count);
            // PATCH(odin-tilde): coalesce sets retain pointers to nodes that
            // were later killed — the CISC/phi hard-split paths null the node
            // and zero its vreg_map without removing it from the set (seen on
            // odinfmt: a deleted copy node coalesced into a phi's set tripped
            // the assert below). Dead members need no liveness tracking.
            if (arr[j]->type == TB_NULL) {
                continue;
            }
            TB_ASSERT(ctx->vreg_map[arr[j]->gvn] == vreg_id);
            put_to_sleep(ra, class, arr[j]->gvn);
        }
    }
}

// TODO(NeGate):
// Inactive set is sorted into buckets based on their assignment, this
// way we only query for assignments which we haven't yet forbidden (either
// due to the active set or the reg mask).

static void active_mask_set(Rogers* ra, int class, int reg, int width) {
    // replicate the exact single-word semantics of the original scan
    uint64_t allot = UINT64_MAX >> (64ull - width);
    ra->active_mask[class][reg / 64ull] |= allot << (reg % 64ull);
}
static void active_mask_clear(Rogers* ra, int class, int reg, int width) {
    uint64_t allot = UINT64_MAX >> (64ull - width);
    ra->active_mask[class][reg / 64ull] &= ~(allot << (reg % 64ull));
}

// PATCH(odin-tilde): the incrementally maintained active_mask mirror can go
// stale when a split pass kills a node that is currently active — the corpse
// cleanup in choose_best_spill frees the active[] slot but used to leave the
// mirror bit set. A stale bit blocks its register in every later ra_mask with
// NO eviction candidate ever offered for it (the active[] slot is empty), so
// once enough bits rot the class fails every allocation at the same program
// point and the split loop spins forever (fmt@fmt_arg: all 16 GPR bits
// blocked, only 7 real occupants, identical self-split every round).
static void sweep_dead_actives(Ctx* restrict ctx, Rogers* restrict ra, int class) {
    int num_regs = ctx->num_regs[class];
    FOR_N(i, 0, num_regs) {
        int g = ra->active[class][i];
        if (g <= 0) { continue; }
        if (g >= ra->order_cap || ra->gvn2node[g] == NULL) {
            ra->active[class][i] = 0;
            int pv = g < aarray_length(ctx->vreg_map) ? ctx->vreg_map[g] : 0;
            int width = 1;
            if (pv > 0 && ctx->vregs[pv].assigned >= 0) {
                width = ctx->vregs[pv].reg_width;
            }
            active_mask_clear(ra, class, i, width);
        }
    }
}

static void mark_active(Ctx* restrict ctx, Rogers* restrict ra, int gvn) {
    VReg* vreg = &ctx->vregs[ctx->vreg_map[gvn]];
    if (vreg->class == REG_CLASS_STK) {
        TB_ASSERT_MSG(vreg->assigned >= 0 && vreg->assigned < ra->stack_reg_count, "mark_active: stack slot %d out of range (count %d) in %s", vreg->assigned, (int)ra->stack_reg_count, ctx->f->super.name);
    } else {
        TB_ASSERT_MSG(vreg->class >= 0 && (size_t)vreg->class < ctx->num_classes && vreg->assigned >= 0 && (size_t)vreg->assigned < ctx->num_regs[vreg->class],
            "mark_active: reg %d out of range for class %d in %s", vreg->assigned, (int)vreg->class, ctx->f->super.name);
    }

    // if there's a value already in the active slot and it's
    // the same vreg, we're just handing off our assignment and
    // we need to move that node to future active
    int old = ra->active[vreg->class][vreg->assigned];
    if (old != 0 && old != gvn) {
        // PATCH(odin-tilde): a different vreg here is a stale occupant from
        // before the wave (its node was re-split onto another vreg, see the
        // remap/pin fixes); stock asserts, but evicting it to the sleepers
        // lets it be re-colored. A same-vreg occupant must go to sleep too:
        // dropping it on the floor forgets a still-live member, so its slot
        // looks free to the next interfering vreg (minesweeper3d main@main:
        // a phi got STACK33 while the coalesced const copy %281 was live in it).
        if (ctx->vreg_map[old] != ctx->vreg_map[gvn] || ra->gvn2node[old] != NULL) {
            put_to_sleep(ra, vreg->class, old);
        }
    }

    ra->active[vreg->class][vreg->assigned] = gvn;
    active_mask_set(ra, vreg->class, vreg->assigned, vreg->reg_width);
}

static void wake_up_node(Ctx* restrict ctx, Rogers* restrict ra, int class, int gvn) {
    TB_OPTDEBUG(REGALLOC)(printf("#   wake   %%%u\n", gvn));
    sparse_set_remove(&ra->future_active[class], gvn);
    mark_active(ctx, ra, gvn);
}

static void unmark_active(Ctx* restrict ctx, Rogers* restrict ra, int gvn) {
    TB_ASSERT(ctx->vreg_map[gvn]);
    VReg* other = &ctx->vregs[ctx->vreg_map[gvn]];
    TB_ASSERT(other->assigned >= 0);
    if (ra->soft_final && ra->active[other->class][other->assigned] != gvn) {
        // PATCH(odin-tilde): slot already evicted/reused during the soft
        // final pass — nothing to unmark, avoid the stale assert.
        return;
    }
    TB_ASSERT(ra->active[other->class][other->assigned] == gvn);
    ra->active[other->class][other->assigned] = 0;
    active_mask_clear(ra, other->class, other->assigned, other->reg_width);
}

static void unmark_active2(Ctx* restrict ctx, Rogers* restrict ra, int gvn) {
    TB_ASSERT(ctx->vreg_map[gvn]);
    VReg* other = &ctx->vregs[ctx->vreg_map[gvn]];
    TB_ASSERT(other->assigned >= 0);
    if (ra->active[other->class][other->assigned] == gvn) {
        ra->active[other->class][other->assigned] = 0;
        active_mask_clear(ra, other->class, other->assigned, other->reg_width);
    }
}

static SplitDecision choose_best_spill(Ctx* restrict ctx, Rogers* restrict ra, TB_Node* attempted_n) {
    cuikperf_region_start("choose spill", NULL);

    VReg* attempted_vreg = node_vreg(ctx, attempted_n);
    RegMask* useful_mask = attempted_vreg->mask;
    int useful_class = attempted_vreg->mask->class;
    int us = ctx->vreg_map[attempted_n->gvn];
    int fx_attempted = fixed_reg_mask(useful_mask);
    // PATCH(odin-tilde): drop stale active slots up front so the scan below
    // and the ra_mask mirror agree (see sweep_dead_actives).
    sweep_dead_actives(ctx, ra, useful_class);
    FOR_N(i, 0, ctx->num_regs[useful_class]) {
        // TODO(NeGate): this is bit math & tests, we can speed this process up
        if (ra->active[useful_class][i] > 0 && bits64_member(ra->mask, i)) {
            // PATCH(odin-tilde): the active table can hold gvns of nodes a
            // later split pass killed (the kill path removes them from the
            // schedule without deactivating). gvn2node is rebuilt from the
            // current schedule every pass, so a NULL entry proves the node
            // is dead: its reservation is meaningless — free the slot
            // instead of offering the corpse as an eviction candidate
            // (crashed in gvn2node[best_gvn] compiling odinfmt/fmt).
            int active_gvn = ra->active[useful_class][i];
            if (ra->gvn2node[active_gvn] == NULL) {
                int pv = active_gvn < aarray_length(ctx->vreg_map) ? ctx->vreg_map[active_gvn] : 0;
                int width = (pv > 0) ? ctx->vregs[pv].reg_width : 1;
                active_mask_clear(ra, useful_class, i, width);
                ra->active[useful_class][i] = 0;
                continue;
            }
            // PATCH(odin-tilde): never offer the attempted vreg itself as an
            // eviction candidate. It can be active already when two defs of
            // the same vreg (e.g. two coalesced projections pinned to the
            // same register) overlap: evicting "ourselves" just marks the
            // vreg been_spilled without separating the overlapping defs, so
            // the identical self-conflict recurs every round. With self
            // excluded the allocator falls through to a real self-split,
            // which does separate the defs.
            if (ctx->vreg_map[active_gvn] == us) {
                continue;
            }
            dyn_array_put(ra->potential_spills, active_gvn);
        }
    }

    int best_spill = 0;
    int best_gvn = 0;
    float best_score = INFINITY;

    // if multiple nodes interfere then that's multiple vregs interferring
    // and if that's the case it's best to self-spill.
    int same_assignment = 0;
    FOR_REV_N(i, 0, dyn_array_length(ra->potential_spills)) {
        int gvn = ra->potential_spills[i];

        TB_ASSERT(ctx->vreg_map[gvn] > 0);
        VReg* vreg = &ctx->vregs[ctx->vreg_map[gvn]];
        if (!within_reg_mask(useful_mask, vreg->assigned)) {
            continue;
        }
        // PATCH(odin-tilde): when the attempted vreg is NOT itself pinned,
        // a pinned occupant (fixed single-register mask) can never be freed
        // by eviction — its split clones re-inherit the pin at the same
        // program point (call argument, rep-movs operand) and the identical
        // conflict recurs next round, spinning the split loop forever while
        // the pinned vreg's tiny spill cost always wins as the eviction
        // target. For a non-pinned attempter such occupants are simply
        // never candidates: it must evict a non-pinned neighbor or split
        // itself. (A PINNED attempter keeps them as candidates: evicting a
        // range-pinned occupant is the only way to free its register, since
        // the occupant's tail can bridge out — see the occ_pinned_same
        // logic below.)
        // EXCEPTION: pinned PROJECTIONS (parameter/call-result projs). Their
        // pin is exercised once at their def (entry or a call), not at the
        // conflict point, so evicting them genuinely frees the register
        // here: the split keeps the def pinned and bridges the tail out.
        // In the Odin calling convention arguments arrive in callee-saved
        // registers too, so ALL of R3/R5/R12-R15 can be held by live
        // pinned parameter projections — skipping them leaves zero
        // candidates and the self-split remats a stack reload at the same
        // point every round (image@alpha_drop_if_present spun forever).
        if (fx_attempted < 0 &&
            vreg->mask != NULL && vreg->mask != &TB_REG_EMPTY &&
            fixed_reg_mask(vreg->mask) >= 0 &&
            (vreg->n == NULL || vreg->n->type != TB_MACH_PROJ)) {
            continue;
        }

        float score = rogers_get_spill_cost(ctx, ra, vreg);
        if (score <= best_score) {
            if (best_spill >= 0) {
                TB_OPTDEBUG(REGALLOC)(printf("#     %%%u is a better spill! V%d (%f is better than %f)\n", gvn, ctx->vreg_map[gvn], score, best_score));
            } else {
                TB_OPTDEBUG(REGALLOC)(printf("#     %%%u is... one of the spills of all time! %f\n", gvn, score));
            }
            best_score = score;
            best_spill = ctx->vreg_map[gvn];
            best_gvn = gvn;
        } else {
            if (score == INFINITY) {
                TB_OPTDEBUG(REGALLOC)(printf("#     %%%u is a bad pick! %f %"PRIu64"\n", gvn, score, vreg->area));
            } else {
                TB_OPTDEBUG(REGALLOC)(printf("#     %%%u is a bad pick! %f\n", gvn, score));
            }
        }
    }

    float us_score = rogers_get_spill_cost(ctx, ra, &ctx->vregs[us]);
    int fx_us = fixed_reg_mask(ctx->vregs[us].mask);
    bool occ_pinned_same = false;
    if (fx_us >= 0 && best_spill > 0) {
        RegMask* occ_mask = ctx->vregs[best_spill].mask;
        occ_pinned_same = occ_mask != NULL && occ_mask != &TB_REG_EMPTY &&
                          fixed_reg_mask(occ_mask) == fx_us;
    }
    // PATCH(odin-tilde): self-splitting a fixed-mask vreg usually cannot
    // resolve its allocation failure — the split def inherits the same
    // single-register mask and fails identically next round (infinite
    // remat/split loop on e.g. an iconst pinned to RSI). Evict the
    // register's occupant instead whenever one is available.
    // EXCEPTION: when the occupant is pinned to the SAME register, its pin
    // is just as real — evicting it can never free the register (the split
    // clones re-inherit the pin and the identical conflict recurs every
    // round, e.g. a call-result projection pinned to RSI vs a rep-movs
    // wrapper copy pinned to RSI). The attempted vreg must then split its
    // own range: the def point stays pinned but the tail bridges out of
    // the register through spill copies. A pinned attempted COPY is the
    // mirror case — its range is a single point so self-splitting is
    // futile, and evicting the range-pinned occupant is the only option.
    //
    // Single-register classes with no spill path (e.g. FLAGS) force
    // eviction too — but only while the victim is FRESH (was_spilled < 2).
    // Evicting an already-split FLAGS vreg is futile when its range is
    // minimal (test adjacent to its jcc: the remat lands on the same
    // point), and once every candidate is stale the musical-chairs loop
    // never ends (image@alpha_drop_if_present: a cmp live across two
    // flags-clobbering shrs must self-split — its remat clones land next
    // to the cmovs, AFTER the clobbers, which is the one split that
    // actually shrinks the conflict).
    bool victim_is_fresh = best_spill > 0 && ctx->vregs[best_spill].was_spilled < 2;
    if (best_spill > 0 && best_score != INFINITY && fx_us >= 0 &&
        (!occ_pinned_same || attempted_n->type == TB_MACH_COPY ||
         (ctx->mayspill_mask[useful_class] == NULL && victim_is_fresh))) {
        // Evict the occupant. The mayspill_mask == NULL case (e.g. FLAGS,
        // a single-register class): self-splitting can never resolve the
        // conflict — the tail cannot bridge out of the register (no spill
        // path, remat recreates the def at the same conflicting use
        // point) — so the occupant must move instead: its forced remat
        // shrinks ITS live range toward its own uses.
        us_score = INFINITY;
    } else if (occ_pinned_same && us_score != INFINITY) {
        // force the self-split branch below
        best_score = INFINITY;
    }
    // PATCH(odin-tilde): pinned single-point COPY vs INFINITY-score
    // occupant (phi) — force eviction
    if (best_spill > 0 && best_score == INFINITY && fx_us >= 0 &&
        attempted_n->type == TB_MACH_COPY && !occ_pinned_same &&
        ctx->mayspill_mask[useful_class] != NULL) {
        us_score = INFINITY;
    }
    // PATCH(odin-tilde): if the victim lineage's accumulated avoid mask
    // plus the register it's being evicted from covers EVERY register in
    // the class, its split clones would get an empty mask and instantly
    // re-conflict — force the self-split instead.
    if (best_spill > 0 && us_score != INFINITY && fx_us >= 0 &&
        ctx->num_regs[useful_class] < 64) {
        uint64_t total = (1ull << ctx->num_regs[useful_class]) - 1;
        uint64_t avoid = ctx->vregs[best_spill].avoid_mask | (1ull << fx_us);
        if ((avoid & total) == total) {
            best_score = INFINITY;
        }
    }
    if (us_score < best_score) {
        // mark the second best as the HRP region to split around it
        if (best_spill > 0 && best_score != INFINITY) {
            size_t cnt;
            TB_Node** arr = coalesce_set_array(ctx, ra, &attempted_n, &cnt);
            FOR_N(j, 0, cnt) {
                mark_node_as_hrp(ctx, ra, best_gvn, arr[j]->gvn, useful_class);
            }
        } else {
            // mark all use sites as HRP
            FOR_USERS(u, attempted_n) {
                mark_point_as_hrp(ctx, ra, USERN(u), useful_class);
            }
        }

        best_spill = us;
        best_score = us_score;
    } else if (best_spill == 0) {
        // PATCH(odin-tilde): no eviction candidate at all — every occupant
        // is a pinned holder (or a stale corpse cleaned up above). The
        // attempted vreg must split its own range: a may_spill mask then
        // drops straight to a stack slot in the split-decision filter
        // ("now a proper spill"), anything else gets the normal split
        // treatment. Stock code fell into the else branch below and
        // dereferenced gvn2node[0] (NULL) — e.g. a dead spill phi whose
        // spill-store siblings were vaporized, leaving it with an
        // uncolorable empty mask (fmt@_cq_should_print_intermediate_plus).
        FOR_USERS(u, attempted_n) {
            mark_point_as_hrp(ctx, ra, USERN(u), useful_class);
        }
        best_spill = us;
        best_score = us_score;
    } else {
        size_t cnt;
        TB_Node** arr = coalesce_set_array(ctx, ra, &ra->gvn2node[best_gvn], &cnt);
        FOR_N(j, 0, cnt) {
            mark_node_as_hrp(ctx, ra, attempted_n->gvn, arr[j]->gvn, useful_class);
        }
    }

    // PATCH(odin-tilde): an INFINITY score is now reachable (self-split of
    // an unevictable vreg via the no-candidate fallback above).
    TB_ASSERT(best_spill > 0);

    // PATCH(odin-tilde): when a pinned vreg wins its register by evicting
    // an occupant, the occupant's split clones must avoid that register —
    // otherwise they get re-colored straight back into it (first-free or
    // copy hint), the same conflict recurs next round, and the split loop
    // never converges. The splitter treats `clobber` as the register set
    // the split clones must avoid.
    SplitDecision s = { best_spill };
    if (best_spill != us) {
        int fx = fixed_reg_mask(ctx->vregs[us].mask);
        // single-register classes (e.g. FLAGS) have nothing to avoid into:
        // the complement mask would be empty, so leave those to the normal
        // split machinery.
        if (fx >= 0 && ctx->num_regs[useful_class] > 1) {
            s.clobber = intern_regmask(ctx, useful_class, false, (1ull << fx));
        }
    }
    cuikperf_region_end();
    return s;
}

static int evict_vreg(Ctx* ctx, Rogers* ra, int evicted_class, int target) {
    TB_ASSERT(!set_get(&ra->been_spilled, target));
    VReg* evicted = &ctx->vregs[target];
    int evicted_reg = evicted->assigned;
    set_put(&ra->been_spilled, target);

    if (evicted_reg >= 0) {
        // unmark all evicted nodes
        size_t evicted_cnt;
        TB_Node** evicted_arr = coalesce_set_array(ctx, ra, &evicted->n, &evicted_cnt);
        FOR_N(i, 0, evicted_cnt) {
            TB_ASSERT(ctx->vreg_map[evicted_arr[i]->gvn] == target);

            unmark_active2(ctx, ra, evicted_arr[i]->gvn);
            set_remove(&ra->live, evicted_arr[i]->gvn);
            sparse_set_remove(&ra->future_active[evicted_class], evicted_arr[i]->gvn);
        }

        // reset assignment
        evicted->class = 0;
        evicted->assigned = -1;
    }
    TB_OPTDEBUG(REGALLOC)(printf("#       \x1b[33mGONNA SPILL V%u\x1b[0m\n", target));
    return evicted_reg;
}

// hands out a fresh stack slot, growing the stack class' active set (it's the
// only resizable class) and the scratch mask alongside
static int stk_new_slot(Rogers* ra, int width) {
    if (ra->num_spills + width > ra->stack_reg_count) {
        size_t new_size = ra->stack_reg_count * 2;
        while (ra->num_spills + width > new_size) { new_size *= 2; }

        ra->active[0] = tb_arena_realloc(ra->arena, ra->active[0], ra->stack_reg_count * sizeof(int), new_size * sizeof(int));
        FOR_N(i, ra->stack_reg_count, new_size) {
            ra->active[0][i] = 0;
        }
        ra->stack_reg_count = new_size;

        // PATCH(odin-tilde): grow the occupied-bit mirror alongside
        size_t new_words = (new_size + 63) / 64;
        if (new_words > (size_t)ra->active_mask_words) {
            ra->active_mask[0] = tb_arena_realloc(ra->arena, ra->active_mask[0],
                ra->active_mask_words * sizeof(uint64_t), new_words * sizeof(uint64_t));
            FOR_N(i, ra->active_mask_words, new_words) {
                ra->active_mask[0][i] = 0;
            }
            ra->active_mask_words = new_words;
        }
    }

    int slot = ra->num_spills;
    ra->num_spills += width;

    // resize the mask array if necessary
    size_t new_cap = ra->max_regs_in_class > ra->num_spills ? ra->max_regs_in_class : ra->num_spills;
    if (((ra->mask_cap+63)/64) < (new_cap+63)/64) {
        ra->mask_cap = new_cap + 64;
        ra->mask = tb_arena_alloc(ra->arena, ((ra->mask_cap+63)/64) * sizeof(uint64_t));
    }
    return slot;
}

static bool sets_interfere(Ctx* ctx, Rogers* ra, size_t cnt, TB_Node** set, TB_Node* other) {
    FOR_N(k, 0, cnt) {
        if (set[k]->type != TB_NULL && interfere(ctx, ra, set[k], other)) {
            return true;
        }
    }
    return false;
}

// PATCH(odin-tilde): pre-colored vregs skip every interference check (the
// wake-time conflict fix runs later and only for register classes), but the
// splits between rounds stretch intervals, so a carried-over color can land on
// a live vreg's slot (fmt@_arg_number: an i8 phi copy spilled over the live
// i64 `i`) or on a register some fixed-mask vreg needs (tank@observe: a split
// copy kept R0 across the x0 param; priority coloring evicts it, the next
// round's copy inherits R0 again, and the split loop never converges).
//
// Both scans below used to walk every sleeping gvn / every vreg for each
// pre-colored definition, i.e. "defs x vregs" per round. On a big function
// that is the whole compile (core/encoding/entity generated.odin's 4877-block
// switch: ~170s against LLVM's 1s). The register classes now answer from
// structures built per round:
//
//   narrow     a vreg whose one node n is a plain (non-PHI, non-PROJ) node,
//              is not live-out of its defining block B, has no user outside
//              B and no phi behind it in B: it is dead at B's end. The walk
//              is backwards, so a sleeper is defined below n or lives in
//              another block, and interfere_in_block only answers yes for:
//                - a value of B defined between n and n's last use (the
//                  earlier one, n, is still live at the sleeper's def);
//                - a value of B defined above n that is still live at n, i.e.
//                  asleep behind the walk's back (pc_pre) or a fixed-mask
//                  member whose use the walk already passed (pc_fx_act);
//                - a value of another block that is live-out of B (yes
//                  outright) or used in B past n (that upward-exposed use
//                  puts it in live_in).
//              Every other branch needs n live-out of B, a user of n in the
//              sleeper's block, or a phi input of B placed after n.
//   pc_bkt     the sleepers, filed under every register they cover.
//   pc_fixed   the fixed-mask vregs, per pinned register.
//
// The candidates are the scans' (pc_bkt / pc_fixed in scan order, the narrow
// path in window / list order — the pre-allocate loop reads "other" only for
// the stack class, which never takes these paths), so the allocation is
// unchanged. TB_PC_AUDIT=1 checks that claim query by query. Anything the
// structures don't model (a class with more than PC_MAX_REGS registers) takes
// the original scan.

static void pc_index_future(Ctx* ctx, Rogers* ra, int class) {
    SparseSet* future = &ra->future_active[class];
    while (ra->pc_upto[class] < aarray_length(future->stack)) {
        uint32_t og = future->stack[ra->pc_upto[class]++];
        int ov = og < aarray_length(ctx->vreg_map) ? ctx->vreg_map[og] : 0;
        if (ov <= 0) { continue; }
        VReg* o = &ctx->vregs[ov];
        if (o->class != class || o->assigned < 0) { continue; }
        if (o->assigned + o->reg_width > PC_MAX_REGS) {
            ra->pc_scan[class] = true;
            return;
        }
        FOR_N(r, 0, o->reg_width) {
            int reg = o->assigned + r;
            pc_push(ra->arena, (void**)&ra->pc_bkt[class][reg], &ra->pc_bkt_len[class][reg], &ra->pc_bkt_cap[class][reg], sizeof(uint32_t), &og);
        }
    }
}

static void pc_index_fixed(Ctx* ctx, Rogers* ra, int class) {
    // built once per round: nothing it files changes while the pre-allocate
    // loop runs (a re-pin keeps the register it was already fixed to)
    if (ra->pc_fixed_built[class]) { return; }
    ra->pc_fixed_built[class] = true;
    FOR_N(i, 1, aarray_length(ctx->vregs)) {
        VReg* o = &ctx->vregs[i];
        if (o->n == NULL || o->n->type == TB_NULL || o->mask == NULL || o->mask->class != class) { continue; }
        int reg = fixed_reg_mask(o->mask);
        if (reg < 0) { continue; }
        if (reg >= PC_MAX_REGS) {
            ra->pc_scan[class] = true;
            return;
        }
        uint32_t idx = (uint32_t)i;
        pc_push(ra->arena, (void**)&ra->pc_fixed[class][reg], &ra->pc_fixed_len[class][reg], &ra->pc_fixed_cap[class][reg], sizeof(uint32_t), &idx);
    }
}

// a same-block window longer than this is not scanned bit by bit; the query
// takes the original scan instead
enum { PC_WINDOW_MAX = 4096 };

// gvns live-in or live-out of `bb`, rebuilt when the block changes
static void pc_live_list(Ctx* restrict ctx, Rogers* restrict ra, TB_BasicBlock* bb) {
    if (ra->pc_live_bb == bb) { return; }
    ra->pc_live_bb = bb;
    ra->pc_live_len = 0;

    Set* sets[2] = { &bb->live_in, &bb->live_out };
    FOR_N(s, 0, 2) {
        Set* set = sets[s];
        FOR_N(j, 0, set->capacity / 64) {
            uint64_t w = set->data[j];
            while (w) {
                int b = tb_ffs64(w) - 1;
                w &= w - 1;
                uint32_t g = (uint32_t)(j * 64 + b);
                if (s == 0 || !set_get(&bb->live_in, g)) {
                    pc_push(ra->arena, (void**)&ra->pc_live, &ra->pc_live_len, &ra->pc_live_cap, sizeof(uint32_t), &g);
                }
            }
        }
    }
}

// `n` is queried at its definition in `bb`. Does it die inside `bb`, with
// nothing that could short-circuit the interference test? `last_use` is then
// what last_use_in_bb answers for n in `bb`.
static bool pc_dies_here(Ctx* restrict ctx, Rogers* restrict ra, TB_Node* n, size_t cnt, TB_Node** arr, TB_BasicBlock** scheduled, int* last_use) {
    if (cnt != 1 || arr[0] != n || n->type == TB_PHI || n->type == TB_PROJ) { return false; }

    TB_BasicBlock* bb = scheduled[n->gvn];
    if (set_get(&bb->live_out, n->gvn)) { return false; }

    int last = ra->order[n->gvn];
    FOR_USERS(u, n) {
        TB_Node* un = USERN(u);
        if (USERI(u) >= un->input_count) { continue; }
        if (scheduled[un->gvn] != bb) { return false; }
        if (ra->order[un->gvn] > last) { last = ra->order[un->gvn]; }
    }

    // a phi behind n could lend an input of bb to a use-in-bb test
    for (size_t k = (size_t)ra->order[n->gvn]; k < aarray_length(bb->items); k++) {
        if (bb->items[k]->type == TB_PHI) { return false; }
    }

    if (last - ra->order[n->gvn] > PC_WINDOW_MAX) { return false; }
    *last_use = last;
    return true;
}

// what can be in the way of a value that dies inside its own block (see
// `narrow` above): sleepers into pc_cand (gvns), and with `with_fixed` the
// fixed-mask vregs into pc_fcand. Both lists hold pairs.
//
// The second element says whether the interference test can be skipped.
// interfere_in_block(n, m) in the block of both compares the earlier value's
// last use with the later value's definition (or answers yes at once when
// both are live-out). For a narrow n every candidate of its own block was
// picked by exactly that comparison — defined between n and n's last use,
// or live across n's definition — so the answer is known: pc_cand carries
// (gvn, 1) and the caller only blocks the register. Candidates of other
// blocks carry 0 and take the real test. For the fixed-mask vregs of
// pc_fcand the pair is (vreg index, the member that is known to interfere,
// or 0).
static void pc_narrow_candidates(Ctx* restrict ctx, Rogers* restrict ra, int class, TB_Node* n, int last_use, bool with_fixed) {
    TB_BasicBlock** scheduled = ctx->f->scheduled;
    TB_BasicBlock* bb = scheduled[n->gvn];
    size_t b = (size_t)(bb - ctx->cfg.blocks);
    size_t base = ra->pc_pos_off[b];
    SparseSet* future = &ra->future_active[class];
    ra->pc_cand_len = 0;
    ra->pc_fcand_len = 0;

    int self = ra->order[n->gvn]; // n's ordinal (item index + 1)
    uint32_t pair[2];

    // another block, or this one above n: live-in or live-out of bb (a value
    // of bb defined at or after n is met by the window below)
    pc_live_list(ctx, ra, bb);
    FOR_N(t, 0, ra->pc_live_len) {
        uint32_t g = ra->pc_live[t];
        bool here = scheduled[g] == bb;
        if (here && ra->order[g] >= self) { continue; }
        int ov = g < aarray_length(ctx->vreg_map) ? ctx->vreg_map[g] : 0;
        if (ov <= 0) { continue; }
        VReg* o = &ctx->vregs[ov];
        // live-out of bb: the rule compares n's last use with m's definition
        uint32_t sure = (here && set_get(&bb->live_out, g)) ? 1 : 0;
        if (future->array[g] >= 0 && o->class == class && o->assigned >= 0) {
            pair[0] = g, pair[1] = sure;
            pc_push(ra->arena, (void**)&ra->pc_cand, &ra->pc_cand_len, &ra->pc_cand_cap, sizeof(pair), pair);
        }
        if (with_fixed && o->mask != NULL && o->mask->class == class && fixed_reg_mask(o->mask) >= 0) {
            pair[0] = (uint32_t)ov, pair[1] = sure ? g : 0;
            pc_push(ra->arena, (void**)&ra->pc_fcand, &ra->pc_fcand_len, &ra->pc_fcand_cap, sizeof(pair), pair);
        }
    }

    // same block, defined between n and n's last use (ordinals are item index + 1):
    // the item positions [self, last use - 1)
    for (size_t iw = (size_t)(self - 1) / 64; iw * 64 < (size_t)(last_use - 1); iw++) {
        uint64_t slept = ra->pc_slept[base + iw];
        uint64_t fxd = with_fixed ? ra->pc_fxbits[base + iw] : 0;
        // first word: only what is above n
        uint64_t keep = ~0ull;
        if (iw * 64 <= (size_t)(self - 1)) {
            keep = ((self - 1) % 64 == 63) ? 0 : ~0ull << ((self - 1) % 64 + 1);
        }
        // last word: only what is below n's last use
        size_t past = (size_t)(last_use - 1) - iw * 64;
        if (past < 64) {
            keep &= past == 0 ? 0 : ~0ull >> (64 - past);
        }
        slept &= keep;
        fxd &= keep;
        while (slept | fxd) {
            int bit = tb_ffs64(slept | fxd) - 1;
            uint64_t mask = 1ull << bit;
            TB_Node* m = bb->items[iw * 64 + bit];
            int ov = ctx->vreg_map[m->gvn];
            if (slept & mask) {
                slept &= ~mask;
                if (ov > 0 && future->array[m->gvn] >= 0) {
                    pair[0] = m->gvn, pair[1] = 1;
                    pc_push(ra->arena, (void**)&ra->pc_cand, &ra->pc_cand_len, &ra->pc_cand_cap, sizeof(pair), pair);
                }
            } else {
                fxd &= ~mask;
                if (ov > 0) {
                    pair[0] = (uint32_t)ov, pair[1] = m->gvn;
                    pc_push(ra->arena, (void**)&ra->pc_fcand, &ra->pc_fcand_len, &ra->pc_fcand_cap, sizeof(pair), pair);
                }
            }
        }
    }

    // same block, defined above n
    if (with_fixed) {
        // the pre-allocate loop: the sleepers put to sleep behind its back
        // (not picked by the rule, they take the real test), and the
        // fixed-mask members live across n's definition (the query is always
        // at n's own position there; a member whose last use is at n itself
        // dies there and never interferes)
        FOR_N(t, 0, ra->pc_pre_len[b]) {
            uint32_t g = ra->pc_pre[b][t];
            if (scheduled[g] == bb && ra->order[g] < self && future->array[g] >= 0) {
                pair[0] = g, pair[1] = 0;
                pc_push(ra->arena, (void**)&ra->pc_cand, &ra->pc_cand_len, &ra->pc_cand_cap, sizeof(pair), pair);
            }
        }
        FOR_N(t, 0, ra->pc_fx_act_len) {
            uint32_t g = ra->pc_fx_act[t];
            if (ra->pc_fx_act_last[t] > (uint32_t)self && ctx->vreg_map[g] > 0) {
                pair[0] = (uint32_t)ctx->vreg_map[g], pair[1] = g;
                pc_push(ra->arena, (void**)&ra->pc_fcand, &ra->pc_fcand_len, &ra->pc_fcand_cap, sizeof(pair), pair);
            }
        }
    } else {
        // The ranges of n's block hold every value of it. In the rule only n's
        // definition matters, and a query can sit above it (a use of n): the
        // values live across the current point, plus what stopped being live
        // between n's definition and here. The priority pass has no walk: it
        // loads the block on demand and arrives at n's definition, and since
        // its queries are not in position order nothing is ever removed
        // there — the range's end is kept with each entry instead.
        if (ra->pc_prio && ra->pc_fx_block != b) {
            FOR_N(t, 0, ra->pc_fx_act_len) {
                ra->pc_fx_slot[ra->pc_fx_act[t]] = -1;
            }
            ra->pc_fx_act_len = 0;
            pc_fixed_block(ctx, ra, bb, true);
        }
        if (ra->pc_prio) {
            pc_fixed_arrive(ra, self - 1);
        }
        FOR_N(t, 0, ra->pc_fx_act_len) {
            uint32_t g = ra->pc_fx_act[t];
            if (ra->order[g] < self && ra->pc_fx_act_last[t] > (uint32_t)self && future->array[g] >= 0) {
                pair[0] = g, pair[1] = 1;
                pc_push(ra->arena, (void**)&ra->pc_cand, &ra->pc_cand_len, &ra->pc_cand_cap, sizeof(pair), pair);
            }
        }
        if (!ra->pc_prio) {
            for (uint32_t t = ra->pc_fx_cursor; t < ra->pc_fx_sorted && ra->pc_fx_sort[t * 2 + 1] > (uint32_t)self; t++) {
                uint32_t g = ra->pc_fx_sort[t * 2];
                if (ra->order[g] < self && future->array[g] >= 0) {
                    pair[0] = g, pair[1] = 1;
                    pc_push(ra->arena, (void**)&ra->pc_cand, &ra->pc_cand_len, &ra->pc_cand_cap, sizeof(pair), pair);
                }
            }
        }
    }
}

// one sleeping gvn against the pre-colored vreg's coalesce set; `sure`: the
// interference is known (see pc_narrow_candidates)
static bool pc_sleeper_conflicts(Ctx* ctx, Rogers* ra, int class, uint32_t og, int vreg_id, VReg* vreg, size_t cnt, TB_Node** set, VReg** out_other, bool sure) {
    TB_Node* other = ra->gvn2node[og];
    int ov = og < aarray_length(ctx->vreg_map) ? ctx->vreg_map[og] : 0;
    if (other == NULL || ov <= 0 || ov == vreg_id) { return false; }

    VReg* o = &ctx->vregs[ov];
    if (o->class != class || o->assigned < 0 ||
        o->assigned + o->reg_width <= vreg->assigned || vreg->assigned + vreg->reg_width <= o->assigned) {
        return false;
    }

    if (sure || sets_interfere(ctx, ra, cnt, set, other)) {
        *out_other = o;
        return true;
    }
    return false;
}

// vreg `i` (fixed to register `reg`) against the pre-colored vreg's coalesce set
static bool pc_fixed_conflicts(Ctx* ctx, Rogers* ra, int class, uint32_t i, int vreg_id, int reg, size_t cnt, TB_Node** set, VReg** out_other, uint32_t sure_gvn) {
    VReg* o = &ctx->vregs[i];
    if (i == (uint32_t)vreg_id || o->n == NULL || o->n->type == TB_NULL || o->mask == NULL ||
        o->mask->class != class || fixed_reg_mask(o->mask) != reg) {
        return false;
    }

    size_t ocnt;
    TB_Node* on = o->n;
    TB_Node** oset = coalesce_set_array(ctx, ra, &on, &ocnt);
    // `sure_gvn`: this member is known to interfere with n (see
    // pc_narrow_candidates) — any one of them is enough
    if (sure_gvn != 0) {
        FOR_N(k, 0, ocnt) {
            if (oset[k]->gvn == sure_gvn && oset[k]->type != TB_NULL) {
                *out_other = o;
                return true;
            }
        }
    }
    FOR_N(k, 0, ocnt) {
        if (oset[k]->type != TB_NULL && sets_interfere(ctx, ra, cnt, set, oset[k])) {
            *out_other = o;
            return true;
        }
    }
    return false;
}

// the original scans, kept as the reference for TB_PC_AUDIT
static bool precolor_conflicts_scan(Ctx* ctx, Rogers* ra, int vreg_id, size_t cnt, TB_Node** set, VReg** out_other) {
    VReg* vreg = &ctx->vregs[vreg_id];
    int class = vreg->class;
    SparseSet* future = &ra->future_active[class];
    FOR_N(i, 0, aarray_length(future->stack)) {
        if (pc_sleeper_conflicts(ctx, ra, class, future->stack[i], vreg_id, vreg, cnt, set, out_other, false)) { return true; }
    }
    if (class != REG_CLASS_STK) {
        FOR_N(i, 1, aarray_length(ctx->vregs)) {
            if (pc_fixed_conflicts(ctx, ra, class, (uint32_t)i, vreg_id, vreg->assigned, cnt, set, out_other, 0)) { return true; }
        }
    }
    return false;
}

// a candidate as pc_narrow_candidates files it: a gvn and what is known about it
typedef struct {
    uint32_t gvn;
    uint32_t sure;
} PCCand;

static bool precolor_conflicts_index(Ctx* ctx, Rogers* ra, int vreg_id, size_t cnt, TB_Node** set, VReg** out_other) {
    VReg* vreg = &ctx->vregs[vreg_id];
    int class = vreg->class;
    SparseSet* future = &ra->future_active[class];
    TB_BasicBlock** scheduled = ctx->f->scheduled;

    // true while the indexes model this query; any surprise falls back to the scans
    bool indexed = class != REG_CLASS_STK && !ra->pc_scan[class] && vreg->assigned >= 0 && vreg->assigned < PC_MAX_REGS;

    // a value that dies inside its own block only meets the sleepers of that
    // block and the ones live in it (see `narrow` above)
    int last_use = 0;
    if (class != REG_CLASS_STK && pc_dies_here(ctx, ra, set[0], cnt, set, scheduled, &last_use)) {
        ra->pc_stat[0]++;
        pc_narrow_candidates(ctx, ra, class, set[0], last_use, true);
        ra->pc_stat[2] += ra->pc_cand_len, ra->pc_stat[3] += ra->pc_fcand_len;
        PCCand* cand = (PCCand*)ra->pc_cand;
        FOR_N(t, 0, ra->pc_cand_len) {
            ra->pc_stat[8] += cand[t].sure != 0;
            if (pc_sleeper_conflicts(ctx, ra, class, cand[t].gvn, vreg_id, vreg, cnt, set, out_other, cand[t].sure != 0)) {
                return true;
            }
        }
        PCCand* fcand = (PCCand*)ra->pc_fcand;
        FOR_N(t, 0, ra->pc_fcand_len) {
            if (pc_fixed_conflicts(ctx, ra, class, fcand[t].gvn, vreg_id, vreg->assigned, cnt, set, out_other, fcand[t].sure)) {
                return true;
            }
        }
        return false;
    }
    ra->pc_stat[1]++;

    if (indexed && vreg->reg_width == 1) {
        pc_index_future(ctx, ra, class);
        if (!ra->pc_scan[class]) {
            int reg = vreg->assigned;
            uint32_t* ids = ra->pc_bkt[class][reg];
            ra->pc_stat[4] += ra->pc_bkt_len[class][reg];
            FOR_N(i, 0, ra->pc_bkt_len[class][reg]) {
                if (pc_sleeper_conflicts(ctx, ra, class, ids[i], vreg_id, vreg, cnt, set, out_other, false)) { return true; }
            }
            goto fixed;
        }
    }

    FOR_N(i, 0, aarray_length(future->stack)) {
        if (pc_sleeper_conflicts(ctx, ra, class, future->stack[i], vreg_id, vreg, cnt, set, out_other, false)) { return true; }
    }

    fixed:
    if (class == REG_CLASS_STK) { return false; }

    if (indexed) {
        pc_index_fixed(ctx, ra, class);
        if (!ra->pc_scan[class]) {
            int reg = vreg->assigned;
            uint32_t* ids = ra->pc_fixed[class][reg];
            ra->pc_stat[5] += ra->pc_fixed_len[class][reg];
            FOR_N(t, 0, ra->pc_fixed_len[class][reg]) {
                if (pc_fixed_conflicts(ctx, ra, class, ids[t], vreg_id, reg, cnt, set, out_other, 0)) { return true; }
            }
            return false;
        }
    }

    FOR_N(i, 1, aarray_length(ctx->vregs)) {
        if (pc_fixed_conflicts(ctx, ra, class, (uint32_t)i, vreg_id, vreg->assigned, cnt, set, out_other, 0)) { return true; }
    }
    return false;
}

static bool pc_audit_enabled(void) {
    static int on = -1;
    if (on < 0) { on = getenv("TB_PC_AUDIT") != NULL; }
    return on;
}

static bool precolor_conflicts(Ctx* ctx, Rogers* ra, int vreg_id, size_t cnt, TB_Node** set, VReg** out_other) {
    // TB_PC_AUDIT=1: answer from the original scans and check the indexes
    // against them (see the Rogers fields pc_audit_*).
    if (!pc_audit_enabled()) {
        return precolor_conflicts_index(ctx, ra, vreg_id, cnt, set, out_other);
    }

    VReg* ref_other = NULL;
    VReg* got_other = NULL;
    bool ref = precolor_conflicts_scan(ctx, ra, vreg_id, cnt, set, &ref_other);
    bool got = precolor_conflicts_index(ctx, ra, vreg_id, cnt, set, &got_other);
    ra->pc_audit_checked++;
    // only the answer is compared: the narrow path visits candidates in
    // window / list order, so "other" may differ, and the pre-allocate loop
    // only reads it for the stack class (which never takes that path).
    if (ref != got) {
        ra->pc_audit_bad++;
        TB_Node* n0 = set[0];
        TB_Node* m = ref_other ? ref_other->n : NULL;
        TB_BasicBlock** sc = ctx->f->scheduled;
        TB_BasicBlock* bb = sc[n0->gvn];
        fprintf(stderr, "[pc-audit] %s: V%d scan=%d index=%d (other %td vs %td) reg=%d width=%d\n", ctx->f->super.name, vreg_id, ref, got,
            ref ? ref_other - ctx->vregs : (ptrdiff_t)-1, got ? got_other - ctx->vregs : (ptrdiff_t)-1,
            ctx->vregs[vreg_id].assigned, ctx->vregs[vreg_id].reg_width);
        fprintf(stderr, "[pc-audit]   n gvn=%u type=%d blk=%td order=%d cnt=%zu live_out=%d users=%u", n0->gvn, n0->type, bb - ctx->cfg.blocks, ra->order[n0->gvn], cnt,
            (int)set_get(&bb->live_out, n0->gvn), n0->user_count);
        FOR_USERS(u, n0) { fprintf(stderr, " [u gvn=%u type=%d blk=%td]", USERN(u)->gvn, USERN(u)->type, sc[USERN(u)->gvn] ? sc[USERN(u)->gvn] - ctx->cfg.blocks : (ptrdiff_t)-1); }
        fprintf(stderr, "\n");
        if (m) {
            fprintf(stderr, "[pc-audit]   m gvn=%u type=%d blk=%td order=%d in_future=%d fixed=%d sched_bb=%d live_in=%d live_out=%d | cls=%d mcls=%d mreg=%d\n", m->gvn, m->type,
                sc[m->gvn] ? sc[m->gvn] - ctx->cfg.blocks : (ptrdiff_t)-1, ra->order[m->gvn],
                (int)sparse_set_test(&ra->future_active[ctx->vregs[ref_other - ctx->vregs].class], m->gvn),
                (int)(ref_other->mask ? fixed_reg_mask(ref_other->mask) : -2), (int)(sc[m->gvn] == bb),
                (int)set_get(&bb->live_in, m->gvn), (int)set_get(&bb->live_out, m->gvn),
                ctx->vregs[vreg_id].class, ref_other->class, ref_other->assigned);
        }
    }
    if (ref) { *out_other = ref_other; }
    return ref;
}

// ---- allocate_reg: which sleepers can block the node being colored -------------
//
// PATCH(odin-tilde): this scan used to walk every sleeping gvn of the class
// for every node that asks for a register — three dependent loads per
// sleeper — i.e. "defs x sleepers" per round. Once precolor_conflicts
// stopped being quadratic that was the rest of the compile time on a big
// function (core/encoding/entity/example 26s, rexcode isa/x86/tests 142s).
//
// The walk is backwards, so at n's definition a sleeper is either a value
// defined further down n's own block, or a value of another block. n can
// only be in its way if n is live at the sleeper's definition, and for a
// value n that dies inside its own block B (one node, no user outside B, not
// live-out of B, no phi behind it) interfere_in_block answers yes only for:
//   - a sleeper in B defined between n and n's last use (n is still live at
//     the sleeper's definition);
//   - a sleeper in B defined above n and still live at n (asleep behind the
//     walk's back);
//   - a sleeper of another block that is live-out of B (yes outright), or
//     used in B past n (that upward-exposed use puts it in live_in).
// Every other branch needs n live-out of B, a user of n in the sleeper's
// block, or a phi input of B placed after n, and n has none.
//
// The candidates come from pc_narrow_candidates (the window above n, the
// block's behind-the-back list and its live sets); they are then ordered by
// the set's own index array, which is the reverse stack order the scan visits
// them in, so potential_spills and ra_mask come out exactly as the scan
// leaves them. TB_PC_AUDIT=1 runs the scan (pc_sleepers_full) as well and
// compares the two, hit by hit.

// does the sleeper `other_gvn` block the coalesce set `arr`? With `sure` the
// interference is known (see pc_narrow_candidates) and only the register is
// asked about.
static bool pc_sleeper_hits(Ctx* restrict ctx, Rogers* restrict ra, int def_class, RegMask* mask, uint64_t* ra_mask, uint32_t other_gvn, TB_Node** arr, size_t cnt, bool sure) {
    TB_Node* other = ra->gvn2node[other_gvn];
    if (other == NULL) { return false; }

    VReg* other_vreg = &ctx->vregs[ctx->vreg_map[other_gvn]];
    if (other_vreg->class != def_class || !within_reg_mask(mask, other_vreg->assigned)) {
        return false;
    }
    TB_ASSERT(other_vreg->assigned >= 0);

    int other_assigned  = other_vreg->assigned;
    uint64_t allot_mask = (UINT64_MAX >> (64ull - other_vreg->reg_width));
    uint64_t need_bits  = allot_mask << (other_assigned % 64ull);

    // if this sleeper's register is already blocked, the interference query
    // cannot change ra_mask, and as an eviction candidate it is useless
    // (evicting it alone cannot free the register).
    if ((ra_mask[other_assigned / 64ull] & need_bits) == need_bits) {
        return false;
    }
    if (sure) { return true; }

    TB_BasicBlock** scheduled = ctx->f->scheduled;
    int* order = ra->order;

    // inlined form of the "interfere()" function because speed reasons
    TB_BasicBlock* other_block = scheduled[other_gvn];
    FOR_N(j, 0, cnt) {
        uint32_t lhs_gvn = arr[j]->gvn;
        TB_BasicBlock* lhs_block = scheduled[lhs_gvn];

        if (interfere_in_block(ctx->cfg.blocks, order, scheduled, ra, arr[j], other, lhs_gvn, other_gvn, lhs_block)) {
            return true;
        }
        if (lhs_block != other_block && interfere_in_block(ctx->cfg.blocks, order, scheduled, ra, other, arr[j], other_gvn, lhs_gvn, other_block)) {
            return true;
        }
    }
    return false;
}

static void pc_sleeper_block(Ctx* restrict ctx, Rogers* restrict ra, uint64_t* ra_mask, uint32_t other_gvn) {
    VReg* other_vreg = &ctx->vregs[ctx->vreg_map[other_gvn]];
    int other_assigned = other_vreg->assigned;
    uint64_t allot_mask = (UINT64_MAX >> (64ull - other_vreg->reg_width));
    dyn_array_put(ra->potential_spills, other_gvn);
    ra_mask[other_assigned / 64ull] |= allot_mask << (other_assigned % 64ull);
}

// the original scan; with `record` the hits are also listed in pc_ref
static void pc_sleepers_full(Ctx* restrict ctx, Rogers* restrict ra, int def_class, RegMask* mask, uint64_t* ra_mask, TB_Node** arr, size_t cnt, bool record) {
    SparseSet* future = &ra->future_active[def_class];
    // reverse iteration because the stale-sleeper guard below removes from
    // the set while walking it.
    FOR_REV_N(i, 0, aarray_length(future->stack)) {
        uint32_t other_gvn = future->stack[i];

        // skip (and drop) stale sleepers whose node was killed by a later
        // split pass — a NULL gvn2node entry proves the node is no longer
        // scheduled (see the matching guard in choose_best_spill).
        if (ra->gvn2node[other_gvn] == NULL) {
            sparse_set_remove(future, other_gvn);
            ra->pc_audit_stale++;
            continue;
        }

        if (pc_sleeper_hits(ctx, ra, def_class, mask, ra_mask, other_gvn, arr, cnt, false)) {
            if (record) {
                pc_push(ra->arena, (void**)&ra->pc_ref, &ra->pc_ref_len, &ra->pc_ref_cap, sizeof(uint32_t), &other_gvn);
            }
            pc_sleeper_block(ctx, ra, ra_mask, other_gvn);
        }
    }
}

// the sleepers that can be in `n`'s way, in reverse stack order, in pc_cand
static void pc_sleepers_here(Ctx* restrict ctx, Rogers* restrict ra, int def_class, TB_Node* n, int last_use) {
    pc_narrow_candidates(ctx, ra, def_class, n, last_use, false);

    // the scan visits the stack from its top; the set's index array says
    // where each candidate sits in it
    SparseSet* future = &ra->future_active[def_class];
    PCCand* cand = (PCCand*)ra->pc_cand;
    for (size_t i = 1; i < ra->pc_cand_len; i++) {
        PCCand c = cand[i];
        int at = future->array[c.gvn];
        size_t j = i;
        while (j > 0 && future->array[cand[j - 1].gvn] < at) {
            cand[j] = cand[j - 1];
            j--;
        }
        cand[j] = c;
    }
}

static bool allocate_reg(Ctx* ctx, Rogers* ra, TB_Node* n) {
    int vreg_id = ctx->vreg_map[n->gvn];
    VReg* vreg = &ctx->vregs[vreg_id];
    int def_class = vreg->mask->class;

    // if we've spilled we shouldn't be able to wake up again.
    if (vreg_id == 0 || set_get(&ra->been_spilled, vreg_id)) {
        return false;
    }

    if (sparse_set_test(&ra->future_active[def_class], n->gvn)) {
        // we're done with some lifetime hole, time to lock in
        //
        // TILDE FIX: split insertion between rounds can extend an interval
        // (e.g. remat/coalesce rewiring) while keeping its old assignment,
        // so a sleeping node's reserved register may now be held by a live
        // node of a different vreg. Instead of asserting in mark_active,
        // evict the current holder and force its vreg to be split around
        // this region on the next round.
        if (vreg->assigned >= 0 && vreg->class > 0 && vreg->class < ctx->num_classes) {
            int old = ra->active[vreg->class][vreg->assigned];
            // PATCH(odin-tilde): a stale occupant (killed by a later split
            // pass) proves nothing about the register — free the slot (see
            // the matching guard in choose_best_spill). Also clear the
            // active_mask mirror bit: leaving it set blocks the register
            // for the rest of the round with no eviction candidate, which
            // deadlocks the split loop (fmt@fmt_arg).
            if (old != 0 && ra->gvn2node[old] == NULL) {
                int ov = old < aarray_length(ctx->vreg_map) ? ctx->vreg_map[old] : 0;
                int width = (ov > 0) ? ctx->vregs[ov].reg_width : 1;
                active_mask_clear(ra, vreg->class, vreg->assigned, width);
                ra->active[vreg->class][vreg->assigned] = 0;
                old = 0;
            }
            if (old != 0 && old != n->gvn && ctx->vreg_map[old] != vreg_id) {
                int old_vreg = ctx->vreg_map[old];
                // PATCH(odin-tilde): if the current holder is pinned to
                // exactly this register, evicting it is futile — the split
                // clone inherits the same pin and conflicts again next
                // round (infinite wake/evict cycle). Split our own stale
                // reservation instead so we get re-colored elsewhere.
                VReg* holder = &ctx->vregs[old_vreg];
                bool holder_pinned = holder->mask != NULL && fixed_reg_mask(holder->mask) == vreg->assigned;
                bool self_pinned   = vreg->mask != NULL && fixed_reg_mask(vreg->mask) == vreg->assigned;
                if (holder_pinned && !self_pinned) {
                    evict_vreg(ctx, ra, def_class, vreg_id);
                    // our split clones must avoid the holder's pinned
                    // register or the same wake conflict recurs next round.
                    // (only meaningful when the class has room to avoid into)
                    SplitDecision s = { .target = vreg_id };
                    if (ctx->num_regs[def_class] > 1) {
                        s.clobber = intern_regmask(ctx, def_class, false,
                            (1ull << fixed_reg_mask(holder->mask)));
                    }
                    dyn_array_put(ra->splits, s);
                    return false;
                }
                TB_ASSERT(!set_get(&ra->been_spilled, old_vreg));
                evict_vreg(ctx, ra, def_class, old_vreg);
                SplitDecision s = { .target = old_vreg };
                if (self_pinned && ctx->num_regs[def_class] > 1) {
                    // the holder's split clones must avoid our pinned
                    // register or it retakes it and we evict it forever.
                    s.clobber = intern_regmask(ctx, def_class, false,
                        (1ull << vreg->assigned));
                }
                dyn_array_put(ra->splits, s);
            }
        }
        wake_up_node(ctx, ra, def_class, n->gvn);
        return true;
    }

    if (vreg->assigned >= 0 && vreg->mask != NULL && vreg->mask != &TB_REG_EMPTY &&
        !within_reg_mask(vreg->mask, vreg->assigned)) {
        // PATCH(odin-tilde): a stale assignment carried over from an earlier
        // round (or inherited through coalesce-set surgery) can violate the
        // vreg's current mask — most dangerously fixed hardware pins like
        // umulpair's hi projection (RDX): the shortcut below would keep RAX
        // and the emitter reads the wrong half of the multiply (observed:
        // div-by-10 magic wrong, strconv_decimal@assign loops forever).
        // Drop it and let the normal mask-respecting path re-color.
        vreg->assigned = -1;
    }

    if (vreg->assigned >= 0) {
        TB_OPTDEBUG(REGALLOC)(printf("#   fixed %%%u V%u => ", n->gvn, vreg_id), print_reg_name(vreg->class, vreg->assigned), printf("\n"));
        mark_active(ctx, ra, n->gvn);
        return true;
    }

    cuikperf_region_start("a", NULL);
    int num_regs = def_class == REG_CLASS_STK ? ra->num_spills : ctx->num_regs[def_class];

    size_t mask_word_count = (num_regs + 63) / 64;
    RegMask* mask = vreg->mask;

    // what the regmask holds only applies up until num_regs[class], this is mostly
    // just relevant for the stack coloring i suppose
    size_t nr = ctx->num_regs[mask->class];
    uint64_t* ra_mask = ra->mask;
    {
        FOR_N(j, 0, mask->count) { ra_mask[j] = ~mask->mask[j]; }
        FOR_N(j, mask->count, (nr + 63) / 64) { ra_mask[j] = UINT64_MAX; }
        if (nr % 64) {
            ra_mask[nr / 64] &= UINT64_MAX >> (64ull - (nr % 64));
        }
    }

    // interfere live things
    dyn_array_clear(ra->potential_spills);
    // PATCH(odin-tilde): free stale active slots (and their mirror bits)
    // before OR-ing the occupied-bit mirror into ra_mask.
    sweep_dead_actives(ctx, ra, def_class);
    {
        uint64_t* am = ra->active_mask[def_class];
        FOR_N(j, 0, mask_word_count) { ra_mask[j] |= am[j]; }
    }
    int inactive_cnt = 0;

    // every node in the coalesced set needs to be checked for interference
    size_t cnt;
    TB_Node** arr = coalesce_set_array(ctx, ra, &n, &cnt);
    {
        // PATCH(odin-tilde): a value that dies inside its own block only has
        // to look at the sleepers of that block and the ones live in it (see
        // the comment before pc_sleeper_hits); everything else takes the
        // original scan.
        TB_BasicBlock** scheduled = ctx->f->scheduled;
        int last_use = 0;
        bool here = pc_dies_here(ctx, ra, n, cnt, arr, scheduled, &last_use);
        ra->pc_stat[6] += here, ra->pc_stat[9] += !here;

        if (pc_audit_enabled()) {
            // TB_PC_AUDIT=1: run the scan on a copy of the mask, run the
            // indexed path on another, compare, then let the scan decide.
            uint64_t* ref_mask = tb_arena_alloc(ra->arena, 2 * mask_word_count * sizeof(uint64_t));
            uint64_t* got_mask = ref_mask + mask_word_count;
            memcpy(ref_mask, ra_mask, mask_word_count * sizeof(uint64_t));
            memcpy(got_mask, ra_mask, mask_word_count * sizeof(uint64_t));
            ra->pc_ref_len = 0;
            ra->pc_got_len = 0;

            pc_sleepers_full(ctx, ra, def_class, mask, ref_mask, arr, cnt, true);
            if (here) {
                pc_sleepers_here(ctx, ra, def_class, n, last_use);
                FOR_N(t, 0, ra->pc_cand_len) {
                    PCCand c = ((PCCand*)ra->pc_cand)[t];
                    if (pc_sleeper_hits(ctx, ra, def_class, mask, got_mask, c.gvn, arr, cnt, c.sure != 0)) {
                        pc_push(ra->arena, (void**)&ra->pc_got, &ra->pc_got_len, &ra->pc_got_cap, sizeof(uint32_t), &c.gvn);
                        pc_sleeper_block(ctx, ra, got_mask, c.gvn);
                    }
                }
            } else {
                FOR_N(t, 0, ra->pc_ref_len) {
                    uint32_t g = ra->pc_ref[t];
                    pc_push(ra->arena, (void**)&ra->pc_got, &ra->pc_got_len, &ra->pc_got_cap, sizeof(uint32_t), &g);
                }
                memcpy(got_mask, ref_mask, mask_word_count * sizeof(uint64_t));
            }

            ra->pc_reg_checked++;
            bool same = ra->pc_ref_len == ra->pc_got_len && memcmp(ref_mask, got_mask, mask_word_count * sizeof(uint64_t)) == 0;
            FOR_N(t, 0, same ? ra->pc_ref_len : 0) {
                if (ra->pc_ref[t] != ra->pc_got[t]) { same = false; }
            }
            if (!same) {
                ra->pc_reg_bad++;
                TB_BasicBlock* nbb = ctx->f->scheduled[n->gvn];
                fprintf(stderr, "[pc-audit] %s: allocate_reg gvn=%u (V%d, class %d) scan=%u indexed=%u | n blk=%td order=%d last_use=%d | query item_pos=%d last_walk=%d here=%d\n",
                    ctx->f->super.name, n->gvn, ctx->vreg_map[n->gvn], def_class, (unsigned)ra->pc_ref_len, (unsigned)ra->pc_got_len,
                    nbb - ctx->cfg.blocks, ra->order[n->gvn], last_use, ra->pc_item_pos, (int)ra->pc_last_walk, (int)here);
                int shown = 0;
                FOR_N(t, 0, ra->pc_ref_len) {
                    uint32_t g = ra->pc_ref[t];
                    if (shown == 8 || pc_listed(ra->pc_got, ra->pc_got_len, g)) { continue; }
                    shown++;
                    TB_Node* o = ra->gvn2node[g];
                    TB_BasicBlock* obb = o ? ctx->f->scheduled[g] : NULL;
                    fprintf(stderr, "[pc-audit]   scan-only gvn=%u type=%d vreg=%d assigned=%d blk=%td order=%d last_use=%d\n", g,
                        o ? o->type : -1, ctx->vreg_map[g], ctx->vregs[ctx->vreg_map[g]].assigned,
                        obb ? obb - ctx->cfg.blocks : (ptrdiff_t)-1, o ? ra->order[g] : -1, o ? pc_last_use(ctx, ra, obb, o) : -1);
                }
            }

            memcpy(ra_mask, ref_mask, mask_word_count * sizeof(uint64_t));
            FOR_N(t, 0, ra->pc_ref_len) {
                dyn_array_put(ra->potential_spills, ra->pc_ref[t]);
            }
        } else if (here) {
            pc_sleepers_here(ctx, ra, def_class, n, last_use);
            ra->pc_stat[7] += ra->pc_cand_len;
            FOR_N(t, 0, ra->pc_cand_len) {
                PCCand c = ((PCCand*)ra->pc_cand)[t];
                if (pc_sleeper_hits(ctx, ra, def_class, mask, ra_mask, c.gvn, arr, cnt, c.sure != 0)) {
                    pc_sleeper_block(ctx, ra, ra_mask, c.gvn);
                }
            }
        } else {
            pc_sleepers_full(ctx, ra, def_class, mask, ra_mask, arr, cnt, false);
        }
    }

    int hint_vreg = vreg->hint_vreg;
    int hint_reg = hint_vreg > 0
        && ctx->vregs[hint_vreg].class == mask->class
        ?  ctx->vregs[hint_vreg].assigned
        :  -1;

    if (hint_vreg < 0) {
        hint_vreg = -hint_vreg;

        // PATCH(odin-tilde): the packed eviction hint is class<<24 | reg
        // (was class<<16 | reg): stack slot numbers can exceed 65535 in
        // very large functions now that VReg.assigned is an int.
        int hint_class = hint_vreg >> 24;
        hint_reg = hint_class == mask->class ? (hint_vreg & 0xFFFFFF) : -1;
    }

    int spilled = -1;
    if (hint_reg >= 0 && (ra_mask[hint_reg / 64ull] & (1ull << (hint_reg % 64ull))) == 0) {
        vreg->class    = def_class;
        vreg->assigned = hint_reg;
    } else if (!reg_assign(ctx, vreg, ra_mask, num_regs)) {
        // if a stack slot failed to color then it means we
        // need more stack slots (there's an indefinite amount :p)
        if (def_class == REG_CLASS_STK) {
            vreg->class = REG_CLASS_STK;
            vreg->assigned = stk_new_slot(ra, vreg->reg_width);
            mark_active(ctx, ra, n->gvn);
            put_to_sleep_vreg(ctx, ra, def_class, cnt, arr, n, vreg_id);

            IF_OPT(REGALLOC) {
                printf("#   \x1b[32massign\x1b[0m %%%-5u V%-5u => STACK%d (new stack slot)\n", n->gvn, vreg_id, vreg->assigned);
            }

            cuikperf_region_end();
            return true;
        }

        SplitDecision s = choose_best_spill(ctx, ra, n);
        VReg* evicted   = &ctx->vregs[s.target];
        int evicted_reg = evict_vreg(ctx, ra, mask->class, s.target);

        IF_OPT(REGALLOC) {
            printf("#   \x1b[32massign\x1b[0m %%%-5u V%-5u => UNCOLORED ", n->gvn, vreg_id);
            tb__print_regmask(&OUT_STREAM_DEFAULT, vreg->mask);
            printf(" (spilled V%u) ", s.target);
            printf("\n");
        }

        dyn_array_put(ra->splits, s);
        if (s.target == vreg_id) {
            cuikperf_region_end();
            return false;
        } else {
            // leave uncolored but we'll ideally steal the same reg as was evicted.
            TB_ASSERT(evicted_reg >= 0);
            vreg->hint_vreg = -((mask->class << 24) | evicted_reg);
            set_put(&ra->been_spilled, vreg_id);
            dyn_array_put(ra->prio_alloc, n);
            cuikperf_region_end();
            return false;
        }
    }

    mark_active(ctx, ra, n->gvn);
    put_to_sleep_vreg(ctx, ra, def_class, cnt, arr, n, vreg_id);

    IF_OPT(REGALLOC) {
        printf("#   \x1b[32massign\x1b[0m %%%-5u V%-5u => ", n->gvn, vreg_id);
        print_reg_name(vreg->class, vreg->assigned);
        printf(" ");
        tb__print_regmask(&OUT_STREAM_DEFAULT, vreg->mask);
        if (spilled >= 0) {
            printf(" (spilled V%u) ", spilled);
        }
        printf("\n");
    }

    cuikperf_region_end();
    return true;
}

// PATCH(odin-tilde): renumber one block's ordinals after a node was
// inserted into it during the CISC/phi resolution loops. compute_ordinals
// ran before those loops; copies created inside them were scheduled
// (f->scheduled set by tb__insert) but never numbered, so every
// interfere()/order_after()/last_use_in_bb() query against them read
// arena garbage — the verdict flipped run-to-run (identical IR, different
// machine code; 873 functions / 490 such reads in one small program).
// A zero fill is NOT a fix: 0 reads as "before everything" and relaxes
// interference (tank miscompiled). The copy's true position is what the
// interference semantics need, so rebuild the block timeline in place.
static void rogers_renumber_block(TB_BasicBlock* bb, Rogers* restrict ra) {
    // PATCH(odin-tilde): the arrays were sized for the node count at
    // compute_ordinals, rounded up to a power of two — next to a boundary
    // the slack is barely a dozen — while the loops that call this create
    // nodes. A block holding a node past the end grew nothing and the
    // ordinals went through the chunk into whatever the allocator had put
    // next (tests/core/encoding/json: the IR arena's chunk header).
    uint32_t max_gvn = 0;
    aarray_for(j, bb->items) {
        TB_Node* n = bb->items[j];
        if (n->gvn > max_gvn) { max_gvn = n->gvn; }
    }
    if (max_gvn >= ra->order_cap) {
        size_t new_cap = tb_next_pow2(max_gvn + 16);
        int* order = tb_arena_alloc(ra->arena, new_cap * sizeof(int));
        TB_Node** gvn2node = tb_arena_alloc(ra->arena, new_cap * sizeof(TB_Node*));
        memcpy(order, ra->order, ra->order_cap * sizeof(int));
        memcpy(gvn2node, ra->gvn2node, ra->order_cap * sizeof(TB_Node*));
        memset(gvn2node + ra->order_cap, 0, (new_cap - ra->order_cap) * sizeof(TB_Node*));
        ra->order = order;
        ra->gvn2node = gvn2node;
        ra->order_cap = new_cap;
    }
    int timeline = 1;
    aarray_for(j, bb->items) {
        TB_Node* n = bb->items[j];
        ra->gvn2node[n->gvn] = n;
        ra->order[n->gvn] = timeline++;
    }
}

static void compute_ordinals(Ctx* restrict ctx, Rogers* restrict ra, TB_Arena* arena) {
    size_t new_cap = tb_next_pow2(ctx->f->node_count + 16);
    ra->order = tb_arena_alloc(arena, new_cap * sizeof(int));
    ra->gvn2node = tb_arena_alloc(arena, new_cap * sizeof(TB_Node*));
    // PATCH(odin-tilde): zero-fill — lookups guard on NULL (e.g. the
    // ra_split pin-restore scan walks vreg_map entries whose nodes were
    // never scheduled, and arena garbage read as a node pointer crashes).
    memset(ra->gvn2node, 0, new_cap * sizeof(TB_Node*));
    ra->order_cap = new_cap;

    // just give the root node a fake ordinal
    TB_ASSERT(ctx->f->root_node->gvn == 0);
    ra->order[0] = 1;

    FOR_N(i, 0, ctx->bb_count) {
        TB_BasicBlock* bb = &ctx->cfg.blocks[i];
        int timeline = 1;
        for (size_t j = 0; j < aarray_length(bb->items); j++) {
            TB_Node* n = bb->items[j];

            ra->gvn2node[n->gvn] = n;
            ra->order[n->gvn] = timeline++;
        }
    }
}

// Probably slow...
static void compute_areas(Ctx* restrict ctx, Rogers* restrict ra, TB_Arena* arena) {
    size_t node_count = ctx->f->node_count;
    size_t new_cap = tb_next_pow2(node_count + 16);
    ra->order = tb_arena_alloc(arena, new_cap * sizeof(int));
    ra->gvn2node = tb_arena_alloc(arena, new_cap * sizeof(TB_Node*));
    // PATCH(odin-tilde): zero-fill — same rationale as compute_ordinals.
    memset(ra->gvn2node, 0, new_cap * sizeof(TB_Node*));
    ra->order_cap = new_cap;

    // just give the root node a fake ordinal
    TB_ASSERT(ctx->f->root_node->gvn == 0);
    ra->order[0] = 1;
    ra->is_vreg = tb_arena_alloc(arena, ((node_count + 63) / 64) * sizeof(uint64_t));

    CUIK_TIMED_BLOCK("areas") {
        // Sparse set repr
        TB_ArenaSavepoint sp = tb_arena_save(arena);
        int* array = tb_arena_alloc(arena, node_count * sizeof(int));
        ArenaArray(int) stack = aarray_create(arena, int, 30);

        aarray_for(i, ctx->vregs) {
            ctx->vregs[i].area = 0;
        }

        FOR_N(i, 0, (node_count + 63) / 64) {
            uint64_t mask = 0;
            size_t end = i*64 + 64;
            if (end > node_count) { end = node_count; }

            FOR_N(j, 0, end - i*64) {
                size_t k = i*64 + j;
                if (ctx->vreg_map[k] > 0) {
                    mask |= 1ull << j;
                }
                array[k] = -1;
            }
            ra->is_vreg[i] = mask;
        }

        FOR_REV_N(i, 0, ctx->bb_count) {
            TB_BasicBlock* bb = &ctx->cfg.blocks[i];
            uint64_t freq = bb->freq >= 0.1 ? (bb->freq * 10) : 1;
            TB_ASSERT(freq > 0);

            // clear live
            aarray_for(j, stack) {
                array[stack[j]] = -1;
            }
            aarray_clear(stack);

            int last_phi = 0;
            do {
                TB_Node* n = bb->items[last_phi++];
                ra->gvn2node[n->gvn] = n;
                ra->order[n->gvn] = last_phi;
            } while (last_phi < aarray_length(bb->items) && (bb->items[last_phi]->type == TB_PHI || NODE_ISA(bb->items[last_phi], PROJ)));
            uint64_t inst_count = aarray_length(bb->items) - last_phi;

            // start intervals
            BITS64_FOR_AND(j, bb->live_out.data, ra->is_vreg, bb->live_out.capacity) {
                int vreg_id = ctx->vreg_map[j];
                TB_ASSERT(vreg_id > 0);

                TB_ASSERT(array[j] < 0);
                array[j] = aarray_length(stack);
                aarray_push(stack, j);

                VReg* v = &ctx->vregs[vreg_id];
                v->area += inst_count*freq;
            }

            uint64_t proj_count = 0;
            FOR_REV_N(j, last_phi, aarray_length(bb->items)) {
                TB_Node* n = bb->items[j];

                // expire intervals
                if (array[n->gvn] >= 0) {
                    int last_gvn = stack[aarray_length(stack) - 1];
                    aarray_remove(stack, array[n->gvn]);
                    array[last_gvn] = array[n->gvn];
                    array[n->gvn] = -1;

                    // trim area
                    int vreg_id = ctx->vreg_map[n->gvn];
                    uint64_t delta = (proj_count + inst_count)*freq;
                    TB_ASSERT(ctx->vregs[vreg_id].area >= delta);
                    ctx->vregs[vreg_id].area -= delta;
                }

                // start intervals
                if (n->type != TB_PHI) {
                    FOR_N(k, 1, n->input_count) {
                        TB_Node* in = n->inputs[k];
                        if (in && ctx->vreg_map[in->gvn] > 0) {
                            // alive
                            if (array[in->gvn] < 0) {
                                array[in->gvn] = aarray_length(stack);
                                aarray_push(stack, in->gvn);

                                VReg* v = &ctx->vregs[ctx->vreg_map[in->gvn]];
                                v->area += (proj_count + inst_count)*freq;
                            }
                        }
                    }
                }

                ra->gvn2node[n->gvn] = n;
                ra->order[n->gvn] = 1 + j;

                proj_count += IS_PROJ(n);
                inst_count--;
            }
        }

        tb_arena_restore(arena, sp);
    }
}

static void expire_interval(Ctx* ctx, Rogers* restrict ra, TB_Node* n, size_t bb_i, int vreg_id);
static bool allocate_loop(Ctx* restrict ctx, Rogers* restrict ra, TB_Arena* arena) {
    size_t arena_size_start = tb_arena_current_size(arena);

    cuikperf_region_start("init", NULL);
    ra->inactive_cache = tb_arena_alloc(arena, (1ull << INACTIVE_CACHE_LOG2) * sizeof(InactiveCacheEntry));
    memset(ra->inactive_cache, 0, (1ull << INACTIVE_CACHE_LOG2) * sizeof(InactiveCacheEntry));
    cuikperf_region_end();

    ra->been_spilled  = set_create_in_arena(arena, aarray_length(ctx->vregs));
    ra->live          = set_create_in_arena(arena, ctx->f->node_count);

    // [class][reg]
    FOR_N(i, 1, ctx->num_classes) {
        int nr = (ctx->num_regs[i] + 15) & -16;
        ra->active[i] = tb_arena_alloc(arena, nr * sizeof(int));
        memset(ra->active[i], 0, nr * sizeof(int));
    }

    FOR_N(i, 0, ctx->num_classes) {
        ra->future_active[i] = sparse_set_alloc(arena, ctx->f->node_count);
    }

    // PATCH(odin-tilde): the precolor_conflicts indexes only describe this
    // round's future_active (fresh above) and start empty.
    memset(ra->pc_upto, 0, sizeof ra->pc_upto);
    memset(ra->pc_scan, 0, sizeof ra->pc_scan);
    memset(ra->pc_bkt, 0, sizeof ra->pc_bkt);
    memset(ra->pc_bkt_len, 0, sizeof ra->pc_bkt_len);
    memset(ra->pc_bkt_cap, 0, sizeof ra->pc_bkt_cap);
    memset(ra->pc_fixed, 0, sizeof ra->pc_fixed);
    memset(ra->pc_fixed_len, 0, sizeof ra->pc_fixed_len);
    memset(ra->pc_fixed_cap, 0, sizeof ra->pc_fixed_cap);
    // PATCH(odin-tilde): pc_index_fixed used to append the whole index again
    // on every call. A query that is not answered by the narrow path calls it
    // once, so the k-th such query of a round walked k copies of it:
    // reflect@write_type_writer 5748 queries → 91.6M candidate tests and 94M
    // interference calls, runtime@write_write_type 105M more — 98% of the
    // package's interference work, in two functions of a few thousand nodes.
    memset(ra->pc_fixed_built, 0, sizeof ra->pc_fixed_built);
    ra->pc_lv_built = false;
    ra->pc_walk_bb = SIZE_MAX;
    ra->pc_item_pos = INT_MAX;
    ra->pc_last_walk = false;

    // PATCH(odin-tilde): item-position bits, one word-aligned range per
    // block; allocate_loop nulls the arena pointers before it returns.
    ra->pc_pos_off = tb_arena_alloc(arena, (ctx->bb_count + 1) * sizeof(size_t));
    size_t pc_words = 0;
    FOR_N(i, 0, ctx->bb_count) {
        ra->pc_pos_off[i] = pc_words;
        pc_words += (aarray_length(ctx->cfg.blocks[i].items) + 63) / 64;
    }
    ra->pc_pos_off[ctx->bb_count] = pc_words;
    ra->pc_slept = tb_arena_alloc(arena, (pc_words + 1) * sizeof(uint64_t));
    ra->pc_fxbits = tb_arena_alloc(arena, (pc_words + 1) * sizeof(uint64_t));
    memset(ra->pc_slept, 0, (pc_words + 1) * sizeof(uint64_t));
    memset(ra->pc_fxbits, 0, (pc_words + 1) * sizeof(uint64_t));
    ra->pc_blocks = ctx->bb_count;

    // PATCH(odin-tilde): expire_interval used to test, for every node it
    // ends, every block before the node's own — O(blocks) cache misses per
    // node, quadratic on a many-block function. The answer is the lowest
    // block index whose live_out holds the node; the sets do not change
    // while the round runs, so it is computed once here.
    ra->pc_min_lo = tb_arena_alloc(arena, ctx->f->node_count * sizeof(uint32_t));
    FOR_N(i, 0, ctx->f->node_count) { ra->pc_min_lo[i] = UINT32_MAX; }
    FOR_N(k, 0, ctx->bb_count) {
        Set* s = &ctx->cfg.blocks[k].live_out;
        BITS64_FOR(j, s->data, s->capacity) {
            if (ra->pc_min_lo[j] > (uint32_t)k) { ra->pc_min_lo[j] = (uint32_t)k; }
        }
    }

    // per-block "asleep behind the walk's back" lists, and the slot table of
    // the pre-allocate walk's live fixed-mask members
    ra->pc_pre = tb_arena_alloc(arena, ctx->bb_count * sizeof(uint32_t*));
    ra->pc_pre_len = tb_arena_alloc(arena, ctx->bb_count * sizeof(uint32_t));
    ra->pc_pre_cap = tb_arena_alloc(arena, ctx->bb_count * sizeof(uint32_t));
    memset(ra->pc_pre, 0, ctx->bb_count * sizeof(uint32_t*));
    memset(ra->pc_pre_len, 0, ctx->bb_count * sizeof(uint32_t));
    memset(ra->pc_pre_cap, 0, ctx->bb_count * sizeof(uint32_t));
    ra->pc_fx_act = NULL;
    ra->pc_fx_act_last = NULL;
    ra->pc_fx_act_len = ra->pc_fx_act_cap = 0;
    ra->pc_fx_block = SIZE_MAX;
    ra->pc_prio = false;
    ra->pc_fx_sort = NULL;
    ra->pc_fx_cnt = NULL;
    ra->pc_max_items = 0;
    ra->pc_fx_sorted = ra->pc_fx_cursor = 0;
    ra->pc_fx_slot = tb_arena_alloc(arena, ctx->f->node_count * sizeof(int));
    FOR_N(i, 0, ctx->f->node_count) { ra->pc_fx_slot[i] = -1; }

    ra->pc_exp_checked = 0;
    ra->pc_live_bb = NULL;
    ra->pc_live = NULL;
    ra->pc_live_len = ra->pc_live_cap = 0;
    ra->pc_cand = NULL;
    ra->pc_cand_len = ra->pc_cand_cap = 0;
    ra->pc_fcand = NULL;
    ra->pc_fcand_len = ra->pc_fcand_cap = 0;
    ra->pc_ref = NULL;
    ra->pc_ref_len = ra->pc_ref_cap = 0;
    ra->pc_got = NULL;
    ra->pc_got_len = ra->pc_got_cap = 0;

    TB_BasicBlock** scheduled = ctx->f->scheduled;
    TB_Node* root = ctx->f->root_node;

    ra->stack_reg_count = tb_next_pow2(ctx->num_regs[REG_CLASS_STK] + ra->num_spills + 8);
    if (ra->stack_reg_count < 16) {
        ra->stack_reg_count = 16;
    }

    ra->mask_cap = ra->max_regs_in_class > ra->stack_reg_count ? ra->max_regs_in_class : ra->stack_reg_count;
    ra->mask = tb_arena_alloc(arena, ((ra->mask_cap+63)/64) * sizeof(uint64_t));

    ra->active[0] = tb_arena_alloc(arena, ra->stack_reg_count * sizeof(int));
    memset(ra->active[0], 0, ra->stack_reg_count * sizeof(int));

    // PATCH(odin-tilde): occupied-bit mirrors of active[][] (one per class,
    // sized to mask_cap words; STK's grows with stack_reg_count).
    ra->active_mask_words = (ra->mask_cap + 63) / 64;
    FOR_N(i, 0, ctx->num_classes) {
        ra->active_mask[i] = tb_arena_alloc(arena, ra->active_mask_words * sizeof(uint64_t));
        memset(ra->active_mask[i], 0, ra->active_mask_words * sizeof(uint64_t));
    }

    // Compute areas (probably slow...)
    compute_areas(ctx, ra, arena);

    // PATCH(odin-tilde): the members of fixed-mask vregs at their item
    // positions — after compute_areas, which hands out this round's
    // gvn2node / order arrays (nothing changes while the pre-allocate loop runs).
    FOR_N(i, 1, aarray_length(ctx->vregs)) {
        VReg* o = &ctx->vregs[i];
        if (o->n == NULL || o->n->type == TB_NULL || o->mask == NULL) { continue; }
        int cls = o->mask->class;
        if (cls <= 0 || cls == REG_CLASS_STK || cls >= ctx->num_classes || fixed_reg_mask(o->mask) < 0) { continue; }
        size_t ocnt;
        TB_Node* on = o->n;
        TB_Node** oset = coalesce_set_array(ctx, ra, &on, &ocnt);
        FOR_N(k, 0, ocnt) {
            TB_Node* m = oset[k];
            if (m->type == TB_NULL) { continue; }
            TB_BasicBlock* mb = ctx->f->scheduled[m->gvn];
            int pos = ra->order[m->gvn] - 1;
            if (mb != NULL && pos >= 0 && (size_t)pos < aarray_length(mb->items)) {
                size_t b = (size_t)(mb - ctx->cfg.blocks);
                ra->pc_fxbits[ra->pc_pos_off[b] + pos / 64] |= 1ull << (pos % 64);
            }
        }
    }
    TB_OPTDEBUG(REGALLOC)(printf("=== PRE-COLOR ===\n"));

    // Pre-allocate ranges
    CUIK_TIMED_BLOCK("pre-alloc") {
        FOR_REV_N(i, 0, ctx->bb_count) {
            TB_BasicBlock* bb = &ctx->cfg.blocks[i];
            // PATCH(odin-tilde): where the reverse walk is (see precolor_conflicts)
            pc_walk_block(ra, (size_t)i);
            pc_fixed_block(ctx, ra, bb, false);
            FOR_REV_N(j, 0, aarray_length(bb->items)) {
                TB_Node* n = bb->items[j];
                int vreg_id = ctx->vreg_map[n->gvn];
                ra->pc_item_pos = ra->order[n->gvn];
                pc_fixed_arrive(ra, (int)j);

                if (vreg_id > 0 && ctx->vregs[vreg_id].assigned >= 0) {
                    if (ctx->vregs[vreg_id].class != REG_CLASS_STK) {
                        // this value was colored by a previous allocation
                        // pass, we'll limit the mask accordingly, it's honestly
                        // unclear to me if we need to do this.
                        //
                        // PATCH(odin-tilde): ...and it is actively harmful for
                        // normally-masked vregs. Narrowing a wide mask to the
                        // previously assigned register manufactures a fixed
                        // mask out of a mere coloring decision; when that
                        // vreg is later evicted (e.g. a pinned wrapper copy
                        // legitimately needs its register), the split clones
                        // inherit the artificial pin, fail identically, and
                        // the split loop never converges. Intrinsically
                        // pinned vregs keep their exact mask either way (the
                        // constraint meets re-pin them), so only re-pin vregs
                        // whose mask is already fixed to this register.
                        RegMask* cur = ctx->vregs[vreg_id].mask;
                        if (fixed_reg_mask(cur) == ctx->vregs[vreg_id].assigned) {
                            ctx->vregs[vreg_id].mask = intern_regmask2(ctx, ctx->vregs[vreg_id].class, false, ctx->vregs[vreg_id].assigned);
                        }
                    }

                    int class = ctx->vregs[vreg_id].class;

                    size_t cnt;
                    TB_Node** set = coalesce_set_array(ctx, ra, &n, &cnt);
                    VReg* other = NULL;
                    VReg* vreg = &ctx->vregs[vreg_id];
                    bool pinned = class == REG_CLASS_STK || fixed_reg_mask(vreg->mask) == vreg->assigned;
                    if (!sparse_set_test(&ra->future_active[class], n->gvn) &&
                        (class == REG_CLASS_STK || !pinned) &&
                        precolor_conflicts(ctx, ra, vreg_id, cnt, set, &other)) {
                        if (class != REG_CLASS_STK) {
                            // let the normal coloring find it a register
                            TB_OPTDEBUG(REGALLOC)(printf("#   V%d drops its old color (conflicts with V%td)\n", vreg_id, other - ctx->vregs));
                            vreg->assigned = -1;
                            goto swept;
                        }

                        // incoming stack params can't move, the other side does
                        VReg* mover = vreg->assigned < ctx->num_regs[REG_CLASS_STK] ? other : vreg;
                        if (mover->assigned >= ctx->num_regs[REG_CLASS_STK]) {
                            TB_OPTDEBUG(REGALLOC)(printf("#   STACK%d conflict, V%td moves", mover->assigned, mover - ctx->vregs));
                            mover->assigned = stk_new_slot(ra, mover->reg_width);
                            TB_OPTDEBUG(REGALLOC)(printf(" to STACK%d\n", mover->assigned));
                        }
                    }

                    IF_OPT(REGALLOC) {
                        VReg* vreg = &ctx->vregs[vreg_id];
                        printf("#   \x1b[32massign\x1b[0m %%%-5u V%-5u => ", n->gvn, vreg_id);
                        print_reg_name(vreg->class, vreg->assigned);
                        printf(" ");
                        tb__print_regmask(&OUT_STREAM_DEFAULT, vreg->mask);
                        printf("\n");
                    }

                    // put the pre-colored regs to rest
                    FOR_N(k, 0, cnt) {
                        TB_ASSERT(set[k]->gvn < ctx->f->node_count);
                        put_to_sleep(ra, class, set[k]->gvn);
                    }
                }

                // PATCH(odin-tilde): the walk has passed this item
            swept:
                pc_fixed_sweep(ctx, ra, n);
            }
        }
    }

    TB_OPTDEBUG(REGALLOC)(printf("=== PRIORITY COLORING ===\n"));

    // Allocate regs which last failed first, this way they ideally
    // get their best placements.
    // PATCH(odin-tilde): no reverse walk during the priority pass; its
    // narrow queries load the ranges of their block on demand
    ra->pc_walk_bb = SIZE_MAX;
    ra->pc_prio = true;
    ra->pc_fx_block = SIZE_MAX;
    dyn_array_for(i, ra->prio_alloc) {
        TB_Node* n = ra->prio_alloc[i];
        TB_OPTDEBUG(REGALLOC)(printf("#\n#>"), tb_print_dumb_node(NULL, n), printf("\n"));

        if (allocate_reg(ctx, ra, n)) {
            // Move to inactive
            int vreg_id = ctx->vreg_map[n->gvn];
            int class = ctx->vregs[vreg_id].class;

            unmark_active(ctx, ra, n->gvn);
            put_to_sleep(ra, class, n->gvn);
        }
    }
    dyn_array_clear(ra->prio_alloc);
    ra->pc_prio = false;

    TB_OPTDEBUG(REGALLOC)(printf("=== STANDARD COLORING ===\n"));

    size_t uf_len = ctx->f->node_count;
    FOR_REV_N(bb_id, 0, ctx->bb_count) {
        ra->where_bb = bb_id;
        TB_BasicBlock* bb = &ctx->cfg.blocks[bb_id];
        Set* live_in      = &bb->live_in;
        Set* live_out     = &bb->live_out;

        // PATCH(odin-tilde): the last reverse walk over the items; its
        // ranges cover every value of the block (see precolor_conflicts)
        ra->pc_last_walk = true;
        pc_walk_block(ra, (size_t)bb_id);
        pc_fixed_block(ctx, ra, bb, true);

        TB_OPTDEBUG(REGALLOC)(printf("# ========= BB%zu =========\n", bb_id));

        cuikperf_region_start("head", NULL);
        // expire intervals for block:
        //   FOR EACH LIVE & ~LIVE_OUT
        BITS64_FOR_ANDN(j, ra->live.data, live_out->data, ra->is_vreg, live_out->capacity) {
            int vreg_id = ctx->vreg_map[j];
            TB_ASSERT(vreg_id > 0);
            TB_ASSERT(set_get(&ra->live, j) && !set_get(live_out, j));
            expire_interval(ctx, ra, ra->gvn2node[j], bb_id, vreg_id);
            set_remove(&ra->live, j);
        }

        // start intervals:
        //   FOR EACH ~LIVE & LIVE_OUT
        BITS64_FOR_ANDN(j, live_out->data, ra->live.data, ra->is_vreg, live_out->capacity) {
            int vreg_id = ctx->vreg_map[j];
            TB_Node* n = ra->gvn2node[j];
            TB_ASSERT(vreg_id > 0);

            if (allocate_reg(ctx, ra, n)) {
                // Move to inactive
                int class = ctx->vregs[vreg_id].class;
                unmark_active(ctx, ra, n->gvn);
                put_to_sleep(ra, class, n->gvn);
            }
        }
        cuikperf_region_end();

        cuikperf_region_start("body", NULL);
        // it's a backwards walk so...
        //   each use will try to place the node into the active set.
        //   each def will either put the node to sleep or kill it.
        FOR_REV_N(j, 0, aarray_length(bb->items)) {
            TB_Node* n = bb->items[j];
            TB_OPTDEBUG(REGALLOC)(printf("#\n#>"), tb_print_dumb_node(NULL, n), printf("\n"));
            ra->pc_item_pos = ra->order[n->gvn];
            pc_fixed_arrive(ra, (int)j);

            int def_t = ra->order[n->gvn];
            // cuikperf_region_start("step", NULL);
            // expire intervals
            int vreg_id = ctx->vreg_map[n->gvn];
            if (vreg_id > 0) {
                // if a node is never used before its def, it's still considered
                // for allocation for that single program point.
                if (!set_get(&ra->live, n->gvn)) {
                    allocate_reg(ctx, ra, n);
                }

                expire_interval(ctx, ra, n, bb_id, vreg_id);
                set_remove(&ra->live, n->gvn);
            }

            // if anything in the active set interferes with
            // the kill set, we need to spill around this op.
            RegMask** kills = ctx->ins;
            int kill_count = ctx->constraint_kill(ctx, n, kills);
            FOR_N(k, 0, kill_count) {
                // TB_OPTDEBUG(REGALLOC5)(printf("#       \x1b[33mCLOBBERING "), tb__print_regmask(&OUT_STREAM_DEFAULT, kills[k]), printf("\x1b[0m\n"));

                bool hrp_point = false;
                int kill_class = kills[k]->class;
                int nr = ctx->num_regs[kill_class];
                if (nr > 64*kills[k]->count) {
                    nr = 64*kills[k]->count;
                }

                int* active = ra->active[kill_class];
                BITS64_FOR(l, kills[k]->mask, nr) {
                    // TB_ASSERT(active[l] == 0);

                    #if 1
                    if (active[l] > 0) {
                        int in_use = active[l];
                        int in_use_vreg_id = ctx->vreg_map[in_use];

                        if (!hrp_point) {
                            hrp_point = true;
                            mark_point_as_hrp(ctx, ra, n, kill_class);
                        }

                        int best_spill = in_use_vreg_id;
                        int best_gvn = in_use;
                        double best_cost = rogers_get_spill_cost(ctx, ra, &ctx->vregs[in_use_vreg_id]);
                        // TB_OPTDEBUG(REGALLOC)(printf("#     %%%u V%u (", in_use, in_use_vreg_id), print_reg_name(kill_class, l), printf(") is us! %f\n", best_cost));

                        // if we can split an existing cheaper VReg (which isn't clobbered)
                        // around ourselves, we can steal its register.
                        double split_bias = 0.0;
                        FOR_N(i, 0, ctx->num_regs[kill_class]) {
                            uint32_t gvn = active[i];
                            if (gvn == 0) {
                                continue;
                            }

                            int other_vreg_id = ctx->vreg_map[gvn];
                            TB_ASSERT(other_vreg_id > 0);
                            TB_ASSERT(!set_get(&ra->been_spilled, other_vreg_id));

                            VReg* other_vreg = &ctx->vregs[other_vreg_id];
                            if (other_vreg->class == kill_class && !within_reg_mask(kills[k], other_vreg->assigned)) {
                                double cost = rogers_get_spill_cost(ctx, ra, other_vreg);
                                if (cost < best_cost) {
                                    // TB_OPTDEBUG(REGALLOC5)(printf("#     %%%u V%u (", gvn, other_vreg_id), print_reg_name(other_vreg->class, other_vreg->assigned), printf(") is a good eviction! %f\n", cost));

                                    best_spill = other_vreg_id;
                                    best_cost  = cost;
                                    best_gvn   = gvn;
                                } else {
                                    // TB_OPTDEBUG(REGALLOC5)(printf("#     %%%u V%u (", gvn, other_vreg_id), print_reg_name(other_vreg->class, other_vreg->assigned), printf(") is a bad eviction! %f\n", cost));
                                }
                            }
                        }

                        TB_OPTDEBUG(REGALLOC)(printf("#       \x1b[33mSPILLING V%u FOR %%%u (CLOBBERED BY %%%u)\x1b[0m\n", best_spill, in_use, n->gvn));

                        if (best_spill != in_use_vreg_id) {
                            evict_vreg(ctx, ra, kill_class, best_spill);
                            mark_node_as_hrp(ctx, ra, in_use, best_gvn, kill_class);

                            SplitDecision s = { .target = best_spill, .clobber = kills[k] };
                            dyn_array_put(ra->splits, s);
                        }

                        evict_vreg(ctx, ra, kill_class, in_use_vreg_id);
                        SplitDecision s = { .target = in_use_vreg_id, .clobber = kills[k] };
                        dyn_array_put(ra->splits, s);
                    }
                    #endif
                }

                // PATCH(odin-tilde): sleepers must respect clobbers too.
                // A value in a lifetime hole sits in future_active with its
                // register still reserved but does not occupy ra->active, so
                // the BITS64_FOR scan above never sees it. Block-head "start
                // interval" allocation also never inserts such nodes into
                // ra->live, so liveness here must be tested against the
                // block's live_out as well. A call clobber destroys the
                // physical register regardless; evict any sleeper live
                // across this point whose reserved register is clobbered
                // and split it around the clobber, like an active occupant.
                {
                    SparseSet* fut = &ra->future_active[kill_class];
                    // PATCH(odin-tilde): sized to the actual sleeper stack.
                    // Was int sleepers[32] with a TB_ASSERT bound, justified
                    // by "reserved registers are unique per vreg" — but split
                    // insertion can leave stale duplicate reservations (the
                    // TILDE FIX wake-conflict case), and in release builds
                    // the assert compiles out, so the 33rd sleeper smashed
                    // the stack and corrupted RA state nondeterministically.
                    int* sleepers = tb_arena_alloc(&ctx->f->tmp_arena,
                        (aarray_length(fut->stack) + 1) * sizeof(int));
                    size_t num_sleepers = 0;
                    aarray_for(i, fut->stack) {
                        uint32_t gvn = fut->stack[i];
                        if (!set_get(&ra->live, gvn) && !set_get(&bb->live_out, gvn)) {
                            continue;
                        }

                        int svreg_id = ctx->vreg_map[gvn];
                        if (svreg_id <= 0 || set_get(&ra->been_spilled, svreg_id)) {
                            continue;
                        }

                        VReg* svreg = &ctx->vregs[svreg_id];
                        if (svreg->class == kill_class && svreg->assigned >= 0 &&
                            within_reg_mask(kills[k], svreg->assigned)) {
                            bool dup = false;
                            FOR_N(j, 0, num_sleepers) {
                                if (sleepers[j] == svreg_id) { dup = true; break; }
                            }
                            if (!dup) {
                                TB_ASSERT(num_sleepers < aarray_length(fut->stack) + 1);
                                sleepers[num_sleepers++] = svreg_id;
                            }
                        }
                    }

                    FOR_N(i, 0, num_sleepers) {
                        TB_OPTDEBUG(REGALLOC)(printf("#       \x1b[33mSPILLING SLEEPER V%u (CLOBBERED BY %%%u)\x1b[0m\n", sleepers[i], n->gvn));
                        if (!hrp_point) {
                            hrp_point = true;
                            mark_point_as_hrp(ctx, ra, n, kill_class);
                        }

                        evict_vreg(ctx, ra, kill_class, sleepers[i]);

                        // escalate past the remat-hard split path: remat
                        // copies get their node's natural (fully clobberable)
                        // mask, and their block-granular ranges can span this
                        // same call, which made RA oscillate forever. With
                        // was_spilled != 0 the splitter does a plain spill
                        // into non-clobbered (or stack) masks instead, which
                        // is guaranteed to terminate.
                        VReg* svreg = &ctx->vregs[sleepers[i]];
                        if (svreg->was_spilled == 0) {
                            svreg->was_spilled = 1;
                        }

                        SplitDecision s = { .target = sleepers[i], .clobber = kills[k] };
                        dyn_array_put(ra->splits, s);
                    }
                }
            }

            // start intervals
            if (n->type != TB_PHI) {
                FOR_N(k, 1, n->input_count) {
                    TB_Node* in = n->inputs[k];
                    if (in && ctx->vreg_map[in->gvn] > 0 && allocate_reg(ctx, ra, in)) {
                        set_put(&ra->live, in->gvn);
                    }
                }
            }
            // cuikperf_region_end();

            // PATCH(odin-tilde): the walk has passed this item
            pc_fixed_sweep(ctx, ra, n);
        }
        cuikperf_region_end();
    }

    size_t inactive_cache_size = (1ull << INACTIVE_CACHE_LOG2) * sizeof(InactiveCacheEntry);
    size_t total_size = tb_arena_current_size(arena) - arena_size_start;
    log_debug("RA round memory: %.1f KiB inactive cache / %.1f KiB total (%.2f %%)",
        inactive_cache_size / 1024.0,
        total_size / 1024.0,
        (inactive_cache_size / (double) total_size) * 100.0
    );

    end:
    // PATCH(odin-tilde): the per-round structures live in this round's arena
    ra->pc_slept = NULL;
    ra->pc_fxbits = NULL;
    ra->pc_pre = NULL;
    ra->pc_fx_slot = NULL;
    ra->pc_min_lo = NULL;
    ra->pc_last_walk = false;
    ra->pc_walk_bb = SIZE_MAX;
    return dyn_array_length(ra->splits) == 0;
}

////////////////////////////////
// Expire or Start intervals
////////////////////////////////
static void expire_interval(Ctx* ctx, Rogers* restrict ra, TB_Node* n, size_t bb_i, int vreg_id) {
    uint32_t gvn = n->gvn;
    TB_ASSERT(gvn < ctx->f->node_count);

    if (set_get(&ra->been_spilled, ctx->vreg_map[gvn])) {
        return;
    }

    bool pause = false;
    // PATCH(odin-tilde): "live-out of any block before this one", read from
    // the per-round table (see allocate_loop). TB_PC_AUDIT=1 re-runs the
    // block-by-block test it replaces.
    if (ra->pc_min_lo != NULL && gvn < ctx->f->node_count) {
        pause = ra->pc_min_lo[gvn] < (uint32_t)bb_i;
        if (pc_audit_enabled()) {
            ra->pc_exp_checked++;
            bool scan = false;
            FOR_N(k, 0, bb_i) {
                if (set_get(&ctx->cfg.blocks[k].live_out, gvn)) { scan = true; break; }
            }
            if (scan != pause) {
                size_t found = SIZE_MAX;
                FOR_N(k, 0, bb_i) { if (set_get(&ctx->cfg.blocks[k].live_out, gvn)) { found = k; break; } }
                fprintf(stderr, "[pc-audit] %s: expire_interval gvn=%u vreg=%d bb=%zu scan=%d table=%d min_lo=%u found=%td\n", ctx->f->super.name, gvn, vreg_id, bb_i, scan, pause,
                    ra->pc_min_lo[gvn], found == SIZE_MAX ? (ptrdiff_t)-1 : (ptrdiff_t)found);
            }
        }
    } else {
        FOR_N(k, 0, bb_i) {
            TB_BasicBlock* other = &ctx->cfg.blocks[k];
            if (set_get(&other->live_out, gvn)) {
                // move to future active
                pause = true;
                break;
            }
        }
    }

    if (!pause) {
        // if there's any uses above bb_i (phis), we also need to pause
        FOR_USERS(u, n) {
            TB_Node* un = USERN(u);
            int other_bb = ctx->f->scheduled[un->gvn] - ctx->cfg.blocks;
            if (other_bb < bb_i) {
                // move to future active
                pause = true;
                break;
            }
        }
    }

    if (pause) {
        int class = ctx->vregs[vreg_id].class;
        put_to_sleep(ra, class, gvn);
    } else {
        TB_OPTDEBUG(REGALLOC)(printf("#   \x1b[31mexpire\x1b[0m %%%u\n", gvn));
    }
    unmark_active2(ctx, ra, gvn);
}

static void rogers_remat(Ctx* ctx, Rogers* ra, TB_Node* n, bool kill_node) {
    size_t extra = extra_bytes(n);
    TB_Function* f = ctx->f;
    TB_Node* root = f->root_node;
    TB_ArenaSavepoint sp = tb_arena_save(&f->tmp_arena);

    RegMask* src_mask = NULL;
    if (n->type == TB_MACH_COPY) {
        TB_NodeMachCopy* cpy = TB_NODE_GET_EXTRA(n);
        src_mask = cpy->use;
    }

    RegMask* def_mask = ctx->constraint(ctx, n, NULL);

    // aggressive reload
    double base_bias = ctx->vregs[ctx->vreg_map[n->gvn]].spill_bias;
    for (size_t i = 0; i < n->user_count;) {
        TB_Node* use_n = USERN(&n->users[i]);
        int use_i      = USERI(&n->users[i]);

        // it's never in[0] lmao
        assert(use_i != 0);

        VReg* reload_vreg;
        TB_Node* remat = NULL;
        // PATCH(odin-tilde): the hard-split path in tb__rogers calls remat
        // before ra->uf is allocated; without a union-find there are no
        // coalesced groups, so the copy-fusion branch cannot apply.
        if (ra->uf != NULL && use_n->type == TB_MACH_COPY && src_mask) {
            TB_NodeMachCopy* cpy = TB_NODE_GET_EXTRA(use_n);
            if (!reg_mask_is_stack(cpy->def) || !reg_mask_is_stack(src_mask)) {
                // gotta be in separate coalesced groups
                int leader = uf_find(ra->uf, ra->uf_len, use_n->gvn);
                if (uf_find(ra->uf, ra->uf_len, n->gvn) != leader) {
                    remat    = use_n;
                    cpy->use = src_mask;

                    // schedule the split right before use
                    set_input(f, use_n, n->inputs[1], use_i);
                    continue;
                }
            }
        }

        RegMask* in_mask = constraint_in(ctx, use_n, use_i);

        // remat per use site
        remat = tb_alloc_node(f, n->type, n->dt, n->input_count, extra);
        memcpy(remat->extra, n->extra, extra);
        FOR_N(j, 0, n->input_count) if (n->inputs[j]) {
            remat->inputs[j] = n->inputs[j];
            add_user(f, remat, n->inputs[j], j);
        }

        if (use_n->type == TB_PHI) {
            TB_Node* pred = cfg_get_pred(&ctx->cfg, use_n->inputs[0], use_i - 1);
            TB_BasicBlock* pred_bb = f->scheduled[pred->gvn];
            TB_ASSERT(pred->input_count != 0 && pred->type != TB_DEAD);

            int pos = aarray_length(pred_bb->items);
            TB_Node* last = pred_bb->items[pos - 1];
            if (IS_PROJ(last)) { last = last->inputs[0]; }
            if (tb_node_is_terminator(last)) {
                pos--;

                while (pos > 0 && pred_bb->items[pos] != pred_bb->start && IS_PROJ(pred_bb->items[pos])) {
                    pos--;
                }
            }

            // place at the end of the pred BB to the phi, basically the latest point
            rogers_insert_op(ctx, pred_bb - ctx->cfg.blocks, remat, pos);

            // phis hard coalesce
            int vreg_id = ctx->vreg_map[use_n->gvn];
            reload_vreg = &ctx->vregs[vreg_id];
            aarray_insert(ctx->vreg_map, remat->gvn, vreg_id);
        } else {
            // schedule the split right before use
            tb__insert_before(ctx, ctx->f, remat, use_n);
            reload_vreg = tb__set_node_vreg(ctx, remat);
            reload_vreg->mask = def_mask;
        }

        // insert copy because the mask can't be represented
        RegMask* mask = tb__reg_mask_meet(ctx, in_mask, reload_vreg->mask);
        // PATCH(odin-tilde): also route fixed (pinned) use masks through a
        // copy. Wiring the remat straight into a pinned use re-pins the
        // remat's vreg through the mask meet, so the next round splits it
        // again and the remat/split cycle never converges (infinite clone
        // churn on e.g. a mov pinned to RCX).
        bool remat_needs_copy = (mask == &TB_REG_EMPTY);
        if (!remat_needs_copy && use_n->type != TB_PHI &&
            fixed_reg_mask(in_mask) >= 0 &&
            fixed_reg_mask(reload_vreg->mask) != fixed_reg_mask(in_mask)) {
            remat_needs_copy = true;
        }
        if (remat_needs_copy) {
            reload_vreg->reg_width = tb__reg_width_from_dt(reload_vreg->mask->class, remat->dt);
            reload_vreg->spill_bias = 1e6;

            TB_Node* copy = tb_alloc_node(f, TB_MACH_COPY, remat->dt, 2, sizeof(TB_NodeMachCopy));
            set_input(f, copy, remat, 1);
            TB_NODE_SET_EXTRA(copy, TB_NodeMachCopy, .def = in_mask, .use = def_mask);

            tb__insert_before(ctx, ctx->f, copy, use_n);
            mask = in_mask;
            remat = copy;

            VReg* new_vreg = tb__set_node_vreg(ctx, copy);
            new_vreg->mask = in_mask;
            reload_vreg = new_vreg;
        }

        set_input(f, use_n, remat, use_i);

        // reload_vreg->hint_vreg = ctx->vreg_map[use_n->gvn];
        reload_vreg->mask = mask;
        reload_vreg->reg_width = tb__reg_width_from_dt(mask->class, remat->dt);
        reload_vreg->spill_bias = 1e6;
        TB_ASSERT(reload_vreg->mask != &TB_REG_EMPTY && "TODO hard split from rematerializing");

        TB_OPTDEBUG(REGALLOC)(printf("\x1b[33m#   V%zu:    use (%%%u)\x1b[0m\n", reload_vreg - ctx->vregs, remat->gvn));
    }
    tb_arena_restore(&f->tmp_arena, sp);

    if (kill_node) {
        // delete the original def
        ctx->vregs[ctx->vreg_map[n->gvn]].uses -= 1;
        ctx->vreg_map[n->gvn] = 0;
        tb__remove_node(ctx, f, n);
        tb_kill_node(f, n);
    }
}

