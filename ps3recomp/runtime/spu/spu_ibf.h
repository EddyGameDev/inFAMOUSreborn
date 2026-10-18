/* spu_ibf.h -- v177 direct computed-branch dispatch inside lifted WWS job modules (tools/spu_ibfast.py).
 * Included only by the post-processed jobmod_* spu_recomp_rc.c files (and spu_channels.c, which sets g_spu_ibf).
 *
 * SPU_IBF_SITE runs in front of the old "ctx->pc = rN; trampoline = spu_indirect_branch; return;" line. It takes
 * the branch itself only when spu_indirect_branch's lean path would have done exactly the same: no pending
 * interrupt / irq restore, no lockstep, no slow per-hop hook, the direct-tail mode on, no resident task or code
 * span, and a target that is an entry of THIS module (the resident job overlay) and not one of the resolver's
 * special PCs. It performs the same per-hop work (spu_tlc). Else it falls through to the old line.
 * SPU_IBF=0 turns it off (read once by spu_ib_fast_init; until then it is off). */
#ifndef SPU_IBF_H
#define SPU_IBF_H
#include "spu_context.h"
typedef void (*spu_ibf_fn)(spu_context*);
SPU_GATE int g_spu_ibf = -1;
static inline int spu_ibf_pc_ok(uint32_t pc)
{
    return pc >= 0x4000u && pc < 0x3E000u && pc != 0x4A40u;
}
#if defined(__clang__)
#  define SPU_IBF_TAIL(ctx, F) __attribute__((musttail)) return (F)(ctx)
#else
#  define SPU_IBF_TAIL(ctx, F) do { g_spu_trampoline_fn = (F); return; } while (0)
#endif
/* conditions under which spu_indirect_branch's lean path would run with nothing special to do */
#define SPU_IBF_COND(ctx)                                                                                  \
    (__builtin_expect(g_spu_ibf > 0, 1) && g_spu_dtail > 0 && !g_spu_tlc_slow && !g_yz_lockstep_on &&     \
     !(ctx)->irq_saved && !(ctx)->resident_task &&                                                        \
     !((ctx)->resident_code[0].image_id | (ctx)->resident_code[1].image_id |                              \
       (ctx)->resident_code[2].image_id | (ctx)->resident_code[3].image_id) &&                            \
     !((ctx)->int_enable && ((ctx)->event_status & (ctx)->event_mask)))
extern spu_ibf_fn spu_lookup(uint32_t addr, int image_id);   /* spu_channels.c (per-thread cached, rc-mapped) */
/* v178: targets in the policy module's JobApi/runtime range [0x1000, 0x4000) (no special PCs there) resolve via
 * the image's cached lookup -- exactly the lean resolver's second step (the job overlay has nothing below 0x4000). */
#define SPU_IBF_RESOLVE(ctx, t, cache, find)                                                               \
    (((t) >= 0x1000u && (t) < 0x4000u) ? spu_lookup((t), (ctx)->image_id) : find((t), &(cache)))
#define SPU_IBF_SITE(ctx, n, selfpc, selffn, cache, find)                                                   \
    { const uint32_t _ibt = (ctx)->gpr[n]._u32[0] & SPU_LS_MASK;                                            \
      if (SPU_IBF_COND(ctx)) {                                                                              \
          if (_ibt == (selfpc)) { (ctx)->pc = _ibt; spu_tlc((ctx), (void*)(selffn)); goto spu_ibf_top; }    \
          { spu_ibf_fn _ibf = SPU_IBF_RESOLVE(ctx, _ibt, cache, find);                                      \
            if (_ibf) { (ctx)->pc = _ibt; spu_tlc((ctx), (void*)_ibf); SPU_IBF_TAIL(ctx, _ibf); } }         \
      } }
/* v185: `bi $r0` used as a LOOP BRANCH. The SPU compiler closes many software-pipelined loops with
 * `r0 = selb(link, loop_head, more); bi $r0`, so every iteration of e.g. jobmod_6cbbde2f1bfc 0x9D40 (w2's hottest
 * function) left the C function through SPU_RET, host-returned into the caller's bracket, missed the return pc and
 * went through the drain resume + resolver, then reloaded ~64 registers from ctx. When r0 holds this function's own
 * entry and the lean resolver would have dispatched there with nothing special to do (SPU_IBF_COND), jump to the
 * top instead -- same per-hop work (spu_tlc), same host depth, locals already current (the spill on this path just
 * wrote them to ctx). Any other r0 -> the old SPU_RET. Emitted by spu_ibfast.py only for functions whose entry
 * address the module loads as a constant (ila), i.e. a possible loop head. */
#define SPU_IBF_RETSELF(ctx, selfpc, selffn)                                                                \
    { if ((((ctx)->gpr[0]._u32[0] & SPU_LS_MASK) == (selfpc)) && SPU_IBF_COND(ctx)) {                        \
          (ctx)->pc = (selfpc); spu_tlc((ctx), (void*)(selffn)); goto spu_ibf_top; } }
/* v178: indirect CALL (bisl): the lifter's bracket is "ctx->pc = target; host_depth++; spu_indirect_branch(ctx);
 * spu_drain_call(ctx, link); ..." -- call the resolved entry directly instead of the full resolver. */
#define SPU_IBF_CALL(ctx, cache, find)                                                                     \
    { const uint32_t _ibt = (ctx)->pc & SPU_LS_MASK; spu_ibf_fn _ibf = 0;                                   \
      if (SPU_IBF_COND(ctx)) _ibf = SPU_IBF_RESOLVE(ctx, _ibt, cache, find);                                \
      if (_ibf) { (ctx)->pc = _ibt; spu_tlc((ctx), (void*)_ibf); _ibf(ctx); } else spu_indirect_branch(ctx); }
#endif
