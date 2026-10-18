/* v231 LOD_JOB_SCALE: inFamous world-cull job (jobmod_50f371dc382d). spu_func_00004A90(r3 = params LS addr).
 * params+0x48 = 7 per-class LOD switch distances (f32 BE, e.g. 400 750 1500 3500 6000 12000 1e6): a record is drawn
 * with its DETAIL children when |d|^2 (z weighted by params+0x64) < thr^2, else its coarse stand-in. Scaling the
 * thresholds pushes the detail/coarse swap out (pop-in is original-game behaviour; this is an enhancement).
 * Params are a fresh DMA copy per job; bit 0 of the 7th entry (1e6, "always") marks a table as already scaled.
 * NOTE: the shipping knob is LOD_CLASS_SCALE (rsx_d3d12_backend.c lodtab_tick scales the SOURCE table in EBOOT .data, which
 * also covers cars/NPCs). This job-side hook is an alternative, LOD_JOB_SCALE=<f> (default 1 = off; never set both).
 * F5 in game = live on/off. LOD_OCC_MAXPLANES=<n> (experimental diagnostic, default off): clamp the
 * plane count at params+0x20 to n (main view uses ~35 = frustum + software occluder planes). */
#ifndef LODX_V231
#define LODX_V231
extern volatile int g_lodx_f5_off;   /* rsx_d3d12_backend.c, F5 */
extern volatile float g_v237_lodf;   /* v237B ADAPT_LOD factor (stubs.cpp / infamous_clock_probe.h); 1 = exactly the old path */
#include "lodx_v233.h"   /* v233B per-class job LOD + LOD_STATS */
static void lodx_v231_apply_inner(spu_context* ctx, uint32_t p)
{
    static volatile float s_scale = -1.f;
    static volatile int s_occ = -2;
    static volatile int s_logged = 0;
    if (s_scale < 0.f) {
        const char* e = getenv("LOD_JOB_SCALE");
        float v = e ? (float)atof(e) : 1.f;
        if (!(v > 0.f && v < 100.f)) v = 1.f;
        const char* o = getenv("LOD_OCC_MAXPLANES");
        s_occ = (o && *o) ? atoi(o) : -1;
        s_scale = v;
        fprintf(stderr, "[LODX] v231 world-cull job hook LOD_JOB_SCALE=%.3f LOD_OCC_MAXPLANES=%d\n", v, s_occ);
    }
    /* v237B LOD_SHADOW_ORIG=1 (default 0 = off): jobs of the non-main views (shadow maps: params+0x20 plane count <= 8;
     * the main view has ~35 = frustum + software occluders) get the ORIGINAL switch distances (factor 1/LOD_CLASS_SCALE),
     * the main view keeps the scaled draw distance. LOD_SHADOW_ORIG_PHASE=0|1 = only on that PS3_AB phase parity. */
    static volatile int s_sho = -2, s_shoph = -1;
    static volatile float s_shof = 1.f;
    if (s_sho == -2) {
        const char* e = getenv("LOD_SHADOW_ORIG");
        const char* k = getenv("LOD_CLASS_SCALE");
        const char* ph = getenv("LOD_SHADOW_ORIG_PHASE");
        float kv = k ? (float)atof(k) : 1.f;
        if (!(kv >= 0.25f && kv <= 20.f)) kv = 1.f;
        s_shof = 1.f / kv;
        s_shoph = (ph && (*ph == '0' || *ph == '1')) ? *ph - '0' : -1;
        s_sho = (e && *e == '1' && kv > 1.f) ? 1 : 0;
        if (s_sho > 0) fprintf(stderr, "[LODX] v237B LOD_SHADOW_ORIG: non-main views x%.3f (original distances), phase %d\n", s_shof, s_shoph);
    }
    float vf = 1.f;
    if (s_sho > 0 && (s_shoph < 0 || (g_ab_phase & 1) == s_shoph) && spu_ls_read32(ctx, p + 0x20) <= 8) vf = s_shof;
    const float sc = s_scale * g_v237_lodf * vf;   /* v237B: x adaptive factor x view factor (both 1.0f = unchanged) */
    if (s_occ > 0) {
        uint32_t n = spu_ls_read32(ctx, p + 0x20);
        if (n > (uint32_t)s_occ && n < 256) spu_ls_write32(ctx, p + 0x20, (uint32_t)s_occ);
    }
    if (sc == 1.f || g_lodx_f5_off) return;
    const uint32_t t = p + 0x48;
    uint32_t w6 = spu_ls_read32(ctx, t + 24);
    if (w6 & 1u) return;                       /* already scaled (or not the table we expect) */
    float last; memcpy(&last, &w6, 4);
    if (!(last > 0.f && last < 1e9f)) return;  /* sanity: not a distance table */
    float before[6], after[6];
    for (int i = 0; i < 6; i++) {
        uint32_t w = spu_ls_read32(ctx, t + 4u * i);
        float f; memcpy(&f, &w, 4);
        before[i] = f;
        if (f > 0.f && f < 1e5f) { f *= sc; memcpy(&w, &f, 4); spu_ls_write32(ctx, t + 4u * i, w); }
        after[i] = f;
    }
    spu_ls_write32(ctx, t + 24, w6 | 1u);
    if (s_logged < 6) {
        s_logged++;
        fprintf(stderr, "[LODX] params@LS %05X planes=%u tab %.0f %.0f %.0f %.0f %.0f %.0f %.0f -> %.0f %.0f %.0f %.0f %.0f %.0f\n",
                p, spu_ls_read32(ctx, p + 0x20), before[0], before[1], before[2], before[3], before[4], before[5], last,
                after[0], after[1], after[2], after[3], after[4], after[5]);
    }
}
static void lodx_v231_apply(spu_context* ctx, uint32_t p)
{
    lodx_v231_apply_inner(ctx, p);
    lodx233_job(ctx, p);   /* v233B: capture this job's table (after v231 scaling) */
}
#endif
