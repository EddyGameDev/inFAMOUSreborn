#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <mutex>
#include <thread>
#include "../ps3recomp/runtime/ppu/ppu_store_observer.h"
// v150: F7 cap toggle. Declared by hand (no <windows.h> in this header: stubs.cpp includes it).
#ifdef _WIN32
extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int vKey);
static inline bool icp_f7_down() { return (GetAsyncKeyState(0x76) & 0x8000) != 0; }
#else
static inline bool icp_f7_down() { return false; }
#endif

extern "C" uint8_t* vm_base;
extern "C" volatile int g_ui_fps_cap;   // v194 settings menu (rsx_ui_overlay.c): -2 unset, 0 off, else fps
extern "C" void (*volatile g_ps3_fz_title_dump)(const char*);
// SPU coherency bitmap (spu_coherency.c). A marked 128-byte line makes the lifted
// code's inline stores fall back to vm_write32, the only place the observer runs.
extern "C" unsigned char s_coh_bitmap[];
extern "C" volatile int s_coh_armed;

namespace infamous_clock_probe {
// BCES00220: [TOC 0x78BFC0-0x7F08]=0x76959C,
// [0x76959C-0x7FF8]=0x7BC650. func_00078AA0 updates this clock.
constexpr uint32_t base = 0x7BC650;
using Clock = std::chrono::steady_clock;
static Clock::time_point start;
struct Change { uint32_t bits = 0, lr = 0; double time = 0; bool seen = false; };
static Change changes[16];
static std::mutex lock;
static uint32_t word(uint32_t addr)
{
    const volatile uint8_t* p = vm_base + addr;
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
static float number(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }
static double elapsed() { return std::chrono::duration<double>(Clock::now() - start).count(); }
static bool stopped(uint32_t bits)
{
    // Zero, near zero (|f| < 0.01: a slow-motion ramp's floor) or not finite.
    return (bits & 0x7f800000U) == 0x7f800000U || (bits & 0x7fffffffU) < 0x3c23d70aU;
}
static void observe(uint32_t addr, uint32_t value, uint32_t lr)
{
    if (addr < base + 0x34 || addr >= base + 0x74 || (addr & 3)) return;
    uint32_t old = word(addr);
    if (old == value) return;
    unsigned channel = (addr - base - 0x34) / 4;
    double now = elapsed();
    { std::lock_guard<std::mutex> guard(lock); changes[channel] = {value, lr, now, true}; }
    if (stopped(old) != stopped(value))
        std::fprintf(stderr, "[SIM-SCALE] t=%.3f channel=%u %g -> %g bits=%08X callerLR=%08X\n",
                     now, channel, number(old), number(value), value, lr);
}
static void dump(const char* why)
{
    // Diagnostic snapshots can straddle a guest update; they never modify it.
    uint32_t w[0x90 / 4];
    for (unsigned i = 0; i < sizeof(w)/sizeof(w[0]); ++i) w[i] = word(base + i*4);
    Change saved[16];
    { std::lock_guard<std::mutex> guard(lock); std::memcpy(saved, changes, sizeof saved); }
    std::fprintf(stderr,
        "[SIM-CLOCK] t=%.3f %s base=%08X raw_dt=%g | "
        "timer0 ticks=%u dt=%g scale=%g | timer1 ticks=%u dt=%g scale=%g | "
        "timer2 ticks=%u dt=%g scale=%g | ramp start=%d in=%d out=%d end=%d target=%g | lastTB=%08X%08X\n",
        elapsed(), why, base, number(w[0]), w[1], number(w[2]), number(w[4]),
        w[5], number(w[6]), number(w[8]), w[9], number(w[10]), number(w[12]),
        (int32_t)w[29], (int32_t)w[30], (int32_t)w[31], (int32_t)w[32], number(w[33]), w[34], w[35]);
    char line[1600];
    size_t used = (size_t)std::snprintf(line, sizeof line, "[SIM-FACTORS]");
    for (unsigned i = 0; i < 16 && used < sizeof(line) - 90; ++i) {
        const Change& c = saved[i];
        used += (size_t)std::snprintf(line + used, sizeof(line) - used,
            " %u=%g(last=%08X lr=%08X t=%.2f%s)", i, number(w[13+i]),
            c.bits, c.lr, c.time, c.seen ? "" : " unseen");
    }
    std::fprintf(stderr, "%s\n", line);
}
// v148 INFAMOUS_FPS_CAP=<fps> (default off): pace the game's frame to at most <fps> by sleeping on the main
// thread when func_00078AA0 (the once-per-frame clock update, only caller 0x192E2C) stores the frame's raw dt
// (clock +0x00). The game measures dt from the timebase itself, so a capped run is simply a slower-framing run -- game
// speed is unchanged. Use: INFAMOUS_FPS_CAP=30 to test whether parked-car / object pop-in depends on frame rate
// (the PS3 ran this game at <= 30 fps), or for a steadier frame pace. Log: [FPSCAP] every 10 s.
static Clock::duration cap_period{0};
static Clock::time_point cap_next{}, cap_win{};
static unsigned cap_frames = 0, cap_slept_frames = 0; static double cap_slept_ms = 0;
static bool sim_on = false;
static bool cap_key = false, cap_active = true;   // v150: INFAMOUS_FPS_CAP_KEY=1 -> F7 toggles the cap live
static void cap_pace_inner()
{
    {   // v194: the settings menu's Frame Rate Limit wins over F7 / the launcher
        const int uc = g_ui_fps_cap;
        static int cur = -3;
        if (uc >= 0) {
            if (uc == 0) { cur = 0; return; }
            if (uc != cur) { cur = uc; cap_period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / uc));
                             cap_next = Clock::time_point{}; std::fprintf(stderr, "[FPSCAP] v194 settings: %d fps cap\n", uc); }
            goto paced;
        }
    }
    if (cap_key) {
        static bool was = false;
        const bool down = icp_f7_down();
        if (down && !was) {
            cap_active = !cap_active; cap_next = Clock::time_point{};
            std::fprintf(stderr, "[FPSCAP] v150 F7 -> cap %s\n", cap_active ? "ON" : "OFF");
        }
        was = down;
        if (!cap_active) return;
    }
paced:
    const Clock::time_point now = Clock::now();
    if (cap_win == Clock::time_point{}) cap_win = now;
    if (cap_next == Clock::time_point{} || now > cap_next + 2 * cap_period) cap_next = now;   // first frame / after a hitch: resync
    else if (now < cap_next) {
        const Clock::duration rem = cap_next - now;
        if (rem > std::chrono::microseconds(1500)) std::this_thread::sleep_for(rem - std::chrono::microseconds(1500));
        while (Clock::now() < cap_next) std::this_thread::yield();
        cap_slept_frames++; cap_slept_ms += std::chrono::duration<double, std::milli>(rem).count();
    }
    cap_next += cap_period;
    cap_frames++;
    const Clock::time_point t = Clock::now();
    if (t - cap_win >= std::chrono::seconds(10)) {
        const double s = std::chrono::duration<double>(t - cap_win).count();
        std::fprintf(stderr, "[FPSCAP] v148 10s: %.2f fps (cap %.1f), %u of %u frames waited, %.2f ms avg wait\n", cap_frames / s,
                     1.0 / std::chrono::duration<double>(cap_period).count(), cap_slept_frames, cap_frames,
                     cap_slept_frames ? cap_slept_ms / cap_slept_frames : 0.0);
        cap_win = t; cap_frames = cap_slept_frames = 0; cap_slept_ms = 0;
    }
}
// v237B ADAPT_LOD (default 0 = off): ADAPTIVE draw distance. Measures the game's frame WORK time at the per-frame clock
// update (interval from the end of the previous cap_pace to the start of this one = frame interval minus any frame-cap
// sleep), keeps an EMA (ADAPT_LOD_TAU frames, default 30) and moves a factor g_v237_lodf in [ADAPT_LOD_MIN /
// LOD_CLASS_SCALE, 1] that the world-cull job hook (spu/jobmod_50f371dc382d_e3D90/lodx_v231.h) multiplies into each
// job's LOD switch table: below ADAPT_LOD_LO_FPS (default 58) the draw distance slides toward ADAPT_LOD_MIN (default 1 =
// original game), above ADAPT_LOD_HI_FPS (default 64) it slides back to the full LOD_CLASS_SCALE; in between it holds
// (hysteresis). A full sweep takes ADAPT_LOD_SEC (default 2) seconds of out-of-band frames (no flicker / pop thrash).
// ADAPT_LOD_PHASE=0|1: active only on that parity of the PS3_AB phase (in-run A/B). F5 still forces the original table.
// g_v237_lodf == 1 is exactly the pre-v237 path. Log: [LODADAPT] every 10 s.
extern "C" volatile float g_v237_lodf;
extern "C" volatile int g_ab_phase;
namespace lodadapt {
static int on = -1, phase = -1;
static float fmin = 1.f, cur = 1.f, kscale = 1.f;
static double ema = 0, lo_ms = 15.63, hi_ms = 17.24, sweep_ms = 2000, alpha = 1.0 / 30;
static Clock::time_point last_end{}, win{};
static double st_sum = 0, st_ms = 0; static unsigned st_n = 0, st_lo = 0, st_hi = 0, st_act = 0;
static double envd(const char* n, double d) { const char* e = std::getenv(n); return (e && *e) ? std::atof(e) : d; }
static void init()
{
    const char* e = std::getenv("ADAPT_LOD");
    on = (e && *e == '1') ? 1 : 0;
    if (!on) return;
    const char* k = std::getenv("LOD_CLASS_SCALE");
    kscale = k ? (float)std::atof(k) : 1.f;
    if (!(kscale >= 0.25f && kscale <= 20.f)) kscale = 1.f;
    const double mins = envd("ADAPT_LOD_MIN", 1.0);
    fmin = (float)(mins / kscale);
    if (!(fmin > 0.05f) || fmin >= 1.f) { on = 0; std::fprintf(stderr, "[LODADAPT] v237B off: LOD_CLASS_SCALE %.2f <= ADAPT_LOD_MIN %.2f\n", kscale, mins); return; }
    const double lf = envd("ADAPT_LOD_LO_FPS", 58.0), hf = envd("ADAPT_LOD_HI_FPS", 64.0);
    hi_ms = 1000.0 / (lf > 5 ? lf : 58.0); lo_ms = 1000.0 / (hf > lf ? hf : lf + 1.0);
    const double sec = envd("ADAPT_LOD_SEC", 2.0); sweep_ms = (sec > 0.1 ? sec : 2.0) * 1000.0;
    const double tau = envd("ADAPT_LOD_TAU", 30.0); alpha = 1.0 / (tau >= 1.0 ? tau : 1.0);
    const char* p = std::getenv("ADAPT_LOD_PHASE"); phase = (p && (*p == '0' || *p == '1')) ? *p - '0' : -1;
    std::fprintf(stderr, "[LODADAPT] v237B adaptive draw distance ON: scale %.2f..%.2f, lower below %.1f fps (%.2f ms), raise above %.1f fps (%.2f ms), sweep %.1f s, tau %.0f frames, phase %d\n",
                 kscale * fmin, kscale, 1000.0 / hi_ms, hi_ms, 1000.0 / lo_ms, lo_ms, sweep_ms / 1000.0, 1.0 / alpha, phase);
}
static void begin()
{
    if (on < 0) init();
    if (!on) return;
    const Clock::time_point now = Clock::now();
    if (win == Clock::time_point{}) win = now;
    if (last_end != Clock::time_point{}) {
        const double ms = std::chrono::duration<double, std::milli>(now - last_end).count();
        if (ms > 0 && ms < 250.0) {   // loading / pause gaps are not frames
            const double s = ms < 2.0 * hi_ms ? ms : 2.0 * hi_ms;   // one hitch must not swing the EMA
            ema = (ema <= 0) ? s : ema + (s - ema) * alpha;
            const bool act = phase < 0 || (g_ab_phase & 1) == phase;
            float c = cur;
            if (!act) c = 1.f;
            else {
                const float step = (float)((1.0 - fmin) * s / sweep_ms);
                if (ema > hi_ms) { c -= step; if (c < fmin) c = fmin; }
                else if (ema < lo_ms) { c += step; if (c > 1.f) c = 1.f; }
            }
            cur = c; g_v237_lodf = c;
            st_sum += c; st_ms += ms; st_n++; st_lo += c <= fmin; st_hi += c >= 1.f; st_act += act;
        }
    }
    if (now - win >= std::chrono::seconds(10) && st_n) {
        std::fprintf(stderr, "[LODADAPT] 10s: %u frames %.2f ms avg work, ema %.2f ms, phase %d %s, mean scale %.3f (x%.3f), at min %.0f%%, at full %.0f%%\n",
                     st_n, st_ms / st_n, ema, g_ab_phase, st_act ? "active" : "inactive", kscale * st_sum / st_n, st_sum / st_n,
                     100.0 * st_lo / st_n, 100.0 * st_hi / st_n);
        win = now; st_sum = st_ms = 0; st_n = st_lo = st_hi = st_act = 0;
    }
}
static void end() { if (on > 0) last_end = Clock::now(); }
}
static void cap_pace()
{
    lodadapt::begin();
    cap_pace_inner();
    lodadapt::end();
}
static void observe_all(uint32_t addr, uint32_t value, uint32_t lr)
{
    if (addr == base && cap_period.count() > 0) cap_pace();
    if (sim_on) observe(addr, value, lr);
}
static void mark_line(uint32_t a)
{
    uint32_t line = a >> 7;
    s_coh_bitmap[line >> 3] |= (unsigned char)(1u << (line & 7));
}
static void init()
{
    const char* e = std::getenv("INFAMOUS_SIM_TRACE");
    const char* cap = std::getenv("INFAMOUS_FPS_CAP");
    double cap_fps = cap ? std::atof(cap) : 0.0;
    { const char* k = std::getenv("INFAMOUS_FPS_CAP_KEY");
      if (k && *k == '1') { cap_key = true; cap_active = false; if (!(cap_fps >= 5.0 && cap_fps <= 240.0)) cap_fps = 30.0;
                            std::fprintf(stderr, "[FPSCAP] v150 F7 toggles a %.0f fps cap (starts OFF)\n", cap_fps); } }
    sim_on = e && !std::strcmp(e, "1");
    if (!sim_on && !(cap_fps >= 5.0 && cap_fps <= 240.0)) return;
    if (word(0x7840B8) != 0x76959C || word(0x7615A4) != base) {
        std::fprintf(stderr, "[SIM-CLOCK] disabled: ELF clock layout differs from BCES00220\n");
        return;
    }
    start = Clock::now();
    if (cap_fps >= 5.0 && cap_fps <= 240.0) {
        cap_period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / cap_fps));
        mark_line(base);   // the raw-dt store must leave the inline fast path to reach the observer
        s_coh_armed = 1;
        g_ppu_store_observer = {base, 0x74, observe_all};
        std::fprintf(stderr, "[FPSCAP] v148 frame cap %.1f fps (INFAMOUS_FPS_CAP; paced at the per-frame clock update)\n", cap_fps);
    }
    if (!sim_on) return;
    // Inline stores bypass vm_write32 unless their line is marked (bits are sticky).
    for (uint32_t a : {base + 0x34, base + 0x70}) {
        uint32_t line = a >> 7;
        s_coh_bitmap[line >> 3] |= (unsigned char)(1u << (line & 7));
    }
    s_coh_armed = 1;
    g_ppu_store_observer = {base, 0x74, observe_all};   // v148: + raw dt (FPS cap); observe() filters to the factors
    g_ps3_fz_title_dump = dump;
    std::fprintf(stderr, "[SIM-CLOCK] read-only timer probe enabled; F8 includes timers and factor callers\n");
    std::thread([] {
        // World timer (all 16 factors) vs. the factor-0-only timer: if the
        // latter advances and the former does not, the simulation is frozen.
        uint32_t world = word(base + 4), real = word(base + 0x24);
        bool frozen = false;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            uint32_t w2 = word(base + 4), r2 = word(base + 0x24);
            bool now_frozen = w2 == world && r2 != real;
            if (now_frozen != frozen)
                std::fprintf(stderr, "[SIM-FROZEN] t=%.3f %s: world ticks %u->%u, real ticks %u->%u\n",
                             elapsed(), now_frozen ? "WORLD CLOCK STOPPED" : "world clock running again",
                             world, w2, real, r2);
            frozen = now_frozen; world = w2; real = r2;
            dump(frozen ? "periodic FROZEN" : "periodic");
        }
    }).detach();
}
}
