/* v233 (props pop-in): per-class / per-kind LOD distance scale + LOD statistics for the world-cull job
 * jobmod_50f371dc382d (included from lodx_v231.h; hooks = one line at the top of the 5 distance-test functions).
 *
 * RE (v233B, job disassembly): the program-stream opcodes that compare an object distance against params+0x48[cls]
 * (near iff tab[cls]^2 > dx^2 + dy^2 + (w*dz)^2, w = params+0x64, world = M(sp+0xA0) * local, cam = params+0x00):
 *   op 3 (12 B) test 0x66F0: int16 x,y,z *4 at +2/+4/+6, skip16 at +8   -> NEAR-ONLY inline block   (hard cull)
 *   op 4 (16 B) test 0x67F8: int16 pos, len16 +8, ptr +0xC                -> NEAR-ONLY sub-program   (hard cull)
 *   op 5 (24 B) test 0x6358: int16 pos, near len16 +8 ptr +0xC, far ptr +0x10 len16 +0x14 -> near/far swap
 *   op 6        = op 5 gated by a bit of the 128-bit mask params+0x10
 *   op C (20 B) test 0x6530: len16 +2, ptr +4, float x,y,z +8/+0xC/+0x10 -> NEAR-ONLY sub-program   (hard cull)
 *   op D (28 B) test 0x61A8: near len16 +2 ptr +4, float pos +8.., far ptr +0x14 len16 +0x18 -> near/far swap
 *   op E        = op D gated by the params+0x10 mask
 * A swap whose far len is 0 is a hard cull too (object vanishes). There is NO fade/dither parameter anywhere in
 * these paths: the compare is binary, so pop-in is the original behaviour (not a recomp bug).
 *
 * Env (all default off):
 *   LOD_STATS=1                print [LODST] every ~5 s: per op / class evaluations, near, far stand-in, culled,
 *                              and for far results the ratio d / tab_base histogram (how many objects a scale s
 *                              would turn near). Main view (planes > 8) and other views (shadows) separately.
 *   LOD_JOB_CLASS_SCALES=<set>[|<set>...]   <set> = "s0,s1,s2,s3,s4,s5[:c0,c1,c2,c3,c4,c5]"
 *                              s = scale of the swap distance (op 5/D with a far program), c = scale of the
 *                              hard-cull distance (op 3/4/C and op 5/D with an empty far program); without ':' c = s.
 *                              Multiplies whatever arrives in params (LOD_CLASS_SCALE table scale / LOD_JOB_SCALE).
 *                              Several sets: set = sets[g_ab_phase % nsets] (PS3_AB phases; phase 0 = set 0).
 *                              Optional per-set "@m,o" suffix = view factors (main, other views) for that set.
 *   LOD_JOB_VIEW_SCALES=m,o     extra factor for the main view (planes > 8) / the other views (shadow maps), e.g. 1,0.67
 *                              keeps the shadow views at the original distances while the main view is scaled.
 *                              F5 / LOD_CLASS_CTL '0' = all scales 1.
 */
#ifndef LODX_V233
#define LODX_V233
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern volatile int g_ab_phase;
/* v235A: rsx_d3d12_backend.c LOD_CLASS_SCALES active set (-1 = none); defined here so the link never depends on the backend */
#ifdef LODX233_OWNER
volatile int g_lodx_ab_set = -1;
#else
extern volatile int g_lodx_ab_set;
#endif
#define LODX233_MAXSETS 8
/* enum before the globals (sizes) */
enum { L233_EVAL, L233_NEAR, L233_FAR, L233_CULL, L233_B0, L233_NB = 7, L233_NF = L233_B0 + L233_NB };
/* ONE copy of the state for both TUs (spu_recomp.c defines LODX233_OWNER; the _rc twin and the plain lifted file
 * can both run parts of the same job -- v233B b120 had per-TU statics: job entry and tests disagreed). */
#ifdef LODX233_OWNER
#define L233_G
#else
#define L233_G extern
#endif
L233_G volatile long  g_l233_init;     /* 0 = not yet, 1 = inert, 2 = active, 3 = initialising */
L233_G int   g_l233_stats, g_l233_nsets;
L233_G float g_l233_sc[LODX233_MAXSETS][2][7];   /* [set][0 swap / 1 cull][class] */
L233_G float g_l233_sv[LODX233_MAXSETS][2];      /* [set][view] per-set view factor ("@m,o") */
L233_G int   g_l233_ident[LODX233_MAXSETS];      /* v235A: set is all-1 (hook returns at once) */
L233_G float g_l233_view[2];                     /* extra factor: [0] main view, [1] other views (shadows) */
/* per-SPU-context job state (keyed by ctx, not TLS: a context may resume on another host thread) */
typedef struct { void* volatile ctx; uint32_t params; float base[7]; } L233Cx;
#define L233_NCX 128
L233_G L233Cx g_l233_cx[L233_NCX];
/* stats: [kind 0..4 = op 3,4,5,C,D][view 0 main / 1 other][class 0..6][field] */
L233_G volatile long g_l233_st[5][2][7][L233_NF];
L233_G volatile long g_l233_jobs[2];
L233_G volatile long g_l233_next;
#define s_l233_init g_l233_init
#define s_l233_stats g_l233_stats
#define s_l233_nsets g_l233_nsets
#define s_l233_sc g_l233_sc
#define s_l233_st g_l233_st
#define s_l233_jobs g_l233_jobs
#define s_l233_next g_l233_next
#if defined(__clang__) || defined(__GNUC__)
#define L233_INC(x) __atomic_fetch_add(&(x), 1, __ATOMIC_RELAXED)
#else
#define L233_INC(x) ((x)++)
#endif

static void lodx233_parse_set(const char* s, float out[2][7], float vf[2])
{
    for (int k = 0; k < 2; k++) for (int c = 0; c < 7; c++) out[k][c] = 1.f;
    vf[0] = vf[1] = 1.f;
    int k = 0, c = 0; const char* p = s;
    { const char* at = s; while (*at && *at != '|' && *at != '@') at++;
      if (*at == '@') { char* e; float a = (float)strtod(at + 1, &e); if (a > 0.05f && a < 50.f) vf[0] = a;
          if (*e == ',') { float b = (float)strtod(e + 1, NULL); if (b > 0.05f && b < 50.f) vf[1] = b; } } }
    while (*p && *p != '|' && *p != '@' && k < 2) {
        char* e; float v = (float)strtod(p, &e);
        if (e == p) { p++; continue; }
        if (c < 6 && v > 0.05f && v < 50.f) out[k][c] = v;
        c++; p = e;
        if (*p == ',') p++;
        else if (*p == ':') { p++; k = 1; c = 0; }
    }
    if (k == 0) for (int i = 0; i < 6; i++) out[1][i] = out[0][i];
}

static L233Cx* l233_cx(spu_context* ctx, int create)
{
    unsigned h = (unsigned)(((uintptr_t)ctx >> 6) * 2654435761u) & (L233_NCX - 1);
    for (unsigned i = 0; i < L233_NCX; i++) {
        L233Cx* e = &g_l233_cx[(h + i) & (L233_NCX - 1)];
        void* c = e->ctx;
        if (c == (void*)ctx) return e;
        if (!c) {
            if (!create) return NULL;
#if defined(__clang__) || defined(__GNUC__)
            void* z = NULL;
            if (__atomic_compare_exchange_n((void**)&e->ctx, &z, (void*)ctx, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return e;
            if (z == (void*)ctx) return e;
#else
            e->ctx = ctx; return e;
#endif
        }
    }
    return NULL;
}
static void lodx233_init_body(void);
static void lodx233_init(void)
{
#if defined(__clang__) || defined(__GNUC__)
    long z = 0;
    if (!__atomic_compare_exchange_n(&g_l233_init, &z, 3, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;   /* other thread inits */
#endif
    lodx233_init_body();
}
static void lodx233_init_body(void)
{
    const char* st = getenv("LOD_STATS");
    s_l233_stats = (st && *st && *st != '0');
    const char* e = getenv("LOD_JOB_CLASS_SCALES");
    s_l233_nsets = 0;
    if (e && *e) {
        const char* p = e;
        while (*p && s_l233_nsets < LODX233_MAXSETS) {
            lodx233_parse_set(p, s_l233_sc[s_l233_nsets], g_l233_sv[s_l233_nsets]);
            fprintf(stderr, "[LODX233] set %d view %.2f,%.2f swap %.2f %.2f %.2f %.2f %.2f %.2f | cull %.2f %.2f %.2f %.2f %.2f %.2f\n",
                    s_l233_nsets, g_l233_sv[s_l233_nsets][0], g_l233_sv[s_l233_nsets][1], s_l233_sc[s_l233_nsets][0][0], s_l233_sc[s_l233_nsets][0][1], s_l233_sc[s_l233_nsets][0][2],
                    s_l233_sc[s_l233_nsets][0][3], s_l233_sc[s_l233_nsets][0][4], s_l233_sc[s_l233_nsets][0][5],
                    s_l233_sc[s_l233_nsets][1][0], s_l233_sc[s_l233_nsets][1][1], s_l233_sc[s_l233_nsets][1][2],
                    s_l233_sc[s_l233_nsets][1][3], s_l233_sc[s_l233_nsets][1][4], s_l233_sc[s_l233_nsets][1][5]);
            s_l233_nsets++;
            while (*p && *p != '|') p++;
            if (*p == '|') p++;
        }
    }
    fprintf(stderr, "[LODX233] v233B per-class job LOD: LOD_JOB_CLASS_SCALES sets=%d (phase-cycled when > 1), LOD_STATS=%d\n",
            s_l233_nsets, s_l233_stats);
    { const char* v = getenv("LOD_JOB_VIEW_SCALES");   /* "main,other" extra factor, default 1,1 */
      g_l233_view[0] = g_l233_view[1] = 1.f;
      if (v && *v) { char* e; float a = (float)strtod(v, &e); if (a > 0.05f && a < 50.f) g_l233_view[0] = a;
          if (*e == ',') { float b = (float)strtod(e + 1, NULL); if (b > 0.05f && b < 50.f) g_l233_view[1] = b; } }
      if (g_l233_view[0] != 1.f || g_l233_view[1] != 1.f) fprintf(stderr, "[LODX233] view factors main %.2f other %.2f (LOD_JOB_VIEW_SCALES)\n", g_l233_view[0], g_l233_view[1]); }
    for (int s = 0; s < LODX233_MAXSETS; s++) {   /* v235A identity sets */
        int id = g_l233_view[0] == 1.f && g_l233_view[1] == 1.f;
        if (s < s_l233_nsets) {
            if (g_l233_sv[s][0] != 1.f || g_l233_sv[s][1] != 1.f) id = 0;
            for (int k = 0; k < 2; k++) for (int c = 0; c < 7; c++) if (s_l233_sc[s][k][c] != 1.f) id = 0;
        }
        g_l233_ident[s] = id;
    }
    g_l233_init = (s_l233_stats || s_l233_nsets || g_l233_view[0] != 1.f || g_l233_view[1] != 1.f) ? 2 : 1;
}

/* job entry (from lodx_v231_apply, after the v231 scaling): remember this job's (fresh-DMA) base table */
static void lodx233_job(spu_context* ctx, uint32_t p)
{
    if (s_l233_init == 0) lodx233_init();
    if (s_l233_init != 2) return;
    L233Cx* e = l233_cx(ctx, 1);
    if (!e) return;
    e->params = p;
    for (int i = 0; i < 7; i++) { uint32_t w = spu_ls_read32(ctx, p + 0x48 + 4u * i); memcpy(&e->base[i], &w, 4); }
    if (s_l233_stats) L233_INC(s_l233_jobs[spu_ls_read32(ctx, p + 0x20) > 8 ? 0 : 1]);
}

static inline float l233_f(spu_context* ctx, uint32_t a) { uint32_t w = spu_ls_read32(ctx, a); float f; memcpy(&f, &w, 4); return f; }
static inline int l233_h(spu_context* ctx, uint32_t a) { return (int)(((uint32_t)ctx->ls[a & SPU_LS_MASK] << 8) | ctx->ls[(a + 1) & SPU_LS_MASK]); }

static void lodx233_print(void)
{
    static const char kn[5] = { '3', '4', '5', 'C', 'D' };
    long jm = s_l233_jobs[0], jo = s_l233_jobs[1];
    float tabc[7] = { 0 };
    for (int i = 0; i < L233_NCX; i++) if (g_l233_cx[i].ctx) { memcpy(tabc, g_l233_cx[i].base, sizeof tabc); break; }
    fprintf(stderr, "[LODST] window jobs main %ld other %ld phase %d (fields: eval near far(stand-in) cull | far d/tab bins <1.25 <1.5 <2 <2.5 <3 <4 >=4)\n",
            jm, jo, g_ab_phase);
    for (int v = 0; v < 2; v++)
        for (int k = 0; k < 5; k++)
            for (int c = 0; c < 7; c++) {
                volatile long* s = s_l233_st[k][v][c];
                if (!s[L233_EVAL]) continue;
                fprintf(stderr, "[LODST] %s op%c c%d tab %.0f: %ld %ld %ld %ld | %ld %ld %ld %ld %ld %ld %ld\n",
                        v ? "oth " : "main", kn[k], c, tabc[c], s[L233_EVAL], s[L233_NEAR], s[L233_FAR], s[L233_CULL],
                        s[L233_B0], s[L233_B0 + 1], s[L233_B0 + 2], s[L233_B0 + 3], s[L233_B0 + 4], s[L233_B0 + 5], s[L233_B0 + 6]);
            }
    memset((void*)s_l233_st, 0, sizeof s_l233_st);
    s_l233_jobs[0] = s_l233_jobs[1] = 0;
}

/* distance test entry: kind 0..4 = op 3,4,5,C,D; r82 = record */
static void lodx233_test(spu_context* ctx, int kind)
{
    if (s_l233_init != 2) return;
    int set = 0;   /* v235A: follow the LOD_CLASS_SCALES rotation when active, else PS3_AB phases */
    if (s_l233_nsets > 1) { const int a = g_lodx_ab_set; set = (int)((unsigned)(a >= 0 ? a : g_ab_phase) % (unsigned)s_l233_nsets); }
    if (!s_l233_stats && g_l233_ident[set]) return;
    const uint32_t r = ctx->gpr[82]._u32[0], p = ctx->gpr[87]._u32[0];
    L233Cx* e = l233_cx(ctx, 0);
    if (!e || p != e->params) return;            /* not the params block we captured: leave untouched */
    const float* s_l233_base = e->base;
    const int cls = ctx->ls[(r + 1) & SPU_LS_MASK];
    if (cls >= 7) return;
    int cull;   /* no far program */
    if (kind == 2) cull = l233_h(ctx, r + 0x14) == 0;
    else if (kind == 4) cull = l233_h(ctx, r + 0x18) == 0;
    else cull = 1;
    const int view = spu_ls_read32(ctx, p + 0x20) > 8 ? 0 : 1;
    float sc = 1.f;
    if (!g_lodx_f5_off) {
        if (s_l233_nsets) {
            sc = s_l233_sc[set][cull][cls] * g_l233_sv[set][view];
        }
        sc *= g_l233_view[view];
    }
    const float tab = s_l233_base[cls] * sc;
    { uint32_t w; memcpy(&w, &tab, 4); spu_ls_write32(ctx, p + 0x48 + 4u * cls, w); }
    if (!s_l233_stats) return;
    /* replicate the job's distance (0x4030 transform + 0x6400.. compare) */
    float lx, ly, lz;
    if (kind == 3 || kind == 4) { lx = l233_f(ctx, r + 8); ly = l233_f(ctx, r + 12); lz = l233_f(ctx, r + 16); }
    else { lx = (float)(int16_t)l233_h(ctx, r + 2) * 4.f; ly = (float)(int16_t)l233_h(ctx, r + 4) * 4.f; lz = (float)(int16_t)l233_h(ctx, r + 6) * 4.f; }
    const uint32_t m = ctx->gpr[1]._u32[0] + 0xA0;
    float d[3];
    for (int i = 0; i < 3; i++)
        d[i] = lx * l233_f(ctx, m + 4u * i) + ly * l233_f(ctx, m + 16 + 4u * i) + lz * l233_f(ctx, m + 32 + 4u * i)
             + l233_f(ctx, m + 48 + 4u * i) - l233_f(ctx, p + 4u * i);
    d[2] *= l233_f(ctx, p + 0x64);
    const float d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    volatile long* s = s_l233_st[kind][view][cls];
    L233_INC(s[L233_EVAL]);
    if (tab * tab > d2) L233_INC(s[L233_NEAR]);
    else {
        L233_INC(s[cull ? L233_CULL : L233_FAR]);
        const float b = s_l233_base[cls];
        const float q = (b > 0.f) ? d2 / (b * b) : 1e9f;   /* (d / tab_base)^2 */
        int bin = q < 1.5625f ? 0 : q < 2.25f ? 1 : q < 4.f ? 2 : q < 6.25f ? 3 : q < 9.f ? 4 : q < 16.f ? 5 : 6;
        L233_INC(s[L233_B0 + bin]);
    }
    { long now = (long)(clock() / (CLOCKS_PER_SEC / 10));   /* 0.1 s ticks */
      long nx = s_l233_next;
      if (now >= nx) {
          if (nx == 0) { s_l233_next = now + 50; }
#if defined(__clang__) || defined(__GNUC__)
          else if (__atomic_compare_exchange_n(&s_l233_next, &nx, now + 50, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) lodx233_print();
#else
          else { s_l233_next = now + 50; lodx233_print(); }
#endif
      } }
}
#endif
