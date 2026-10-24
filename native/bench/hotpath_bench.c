/* Hot-path micro-benchmarks behind docs/NATIVE_PERFORMANCE.md's before/after columns: each pair is the slower and the
   faster form of one path, built with the game module's flags. Times are per operation (ns) and cycles at the
   calibrated TSC rate.
   Windows/x86-64:
     clang -O3 -fsigned-char -fno-strict-aliasing -fno-strict-float-cast-overflow -fno-math-errno -fuse-ld=lld
           hotpath_bench.c -o hotpath_bench.exe
   `hotpath_bench cast <label>` runs the float-to-int pair alone, for a build with and one without
   -fno-strict-float-cast-overflow. */
#include <immintrin.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <intrin.h>

#define NOINLINE __attribute__((noinline))

static double sTscPerNs;

static uint64_t tsc(void) { return __rdtsc(); }

static void calibrate(void) {
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    const uint64_t t0 = tsc();
    do { QueryPerformanceCounter(&b); } while ((b.QuadPart - a.QuadPart) < f.QuadPart / 5);
    const uint64_t t1 = tsc();
    const double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart;
    sTscPerNs = (double)(t1 - t0) / ns;
    printf("QPC frequency %lld Hz, TSC %.3f GHz\n", (long long)f.QuadPart, sTscPerNs);
}

static void report(const char* name, uint64_t ticks, uint64_t ops) {
    const double ns = (double)ticks / sTscPerNs / (double)ops;
    printf("%-58s %8.3f ns  %7.2f TSC cycles\n", name, ns, (double)ticks / (double)ops);
}

/* A: the interrupt hot path (OSRestoreInterrupts re-enabling) */

static LARGE_INTEGER sClockStart, sClockFrequency;
static int64_t scale_to_time_base(int64_t count, int64_t rate) {
    return (count / rate) * 60750000LL + (count % rate) * 60750000LL / rate;
}
NOINLINE int64_t time_base(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return scale_to_time_base(now.QuadPart - sClockStart.QuadPart, sClockFrequency.QuadPart);
}
static volatile int sEnabledA;
static int64_t sDeadline = INT64_MAX;
static atomic_uint sPendingA;
static uint32_t sMaskA = 0xFFFFFFFFu;
static int sDelivered;
NOINLINE void restore_current(int level) {
    const int prev = sEnabledA;
    sEnabledA = level;
    if (level && !prev) {
        if (time_base() >= sDeadline) sDelivered++;
        unsigned pending = atomic_load(&sPendingA);
        unsigned deliverable = 0;
        for (int i = 0; i < 32; i++)
            if ((pending & (1u << i)) && !(sMaskA & (0x80000000u >> i))) deliverable |= 1u << i;
        if (deliverable) sDelivered++;
    }
}
static int sEnabledB;
static _Atomic uint64_t sPendingB;
NOINLINE void restore_candidate(int level) {
    const int prev = sEnabledB;
    sEnabledB = level;
    if (level && !prev) {
        if (atomic_load_explicit(&sPendingB, memory_order_relaxed) & ~(uint64_t)sMaskA) sDelivered++;
    }
}

static void bench_interrupts(void) {
    QueryPerformanceFrequency(&sClockFrequency);
    QueryPerformanceCounter(&sClockStart);
    enum { N = 2000000 };
    uint64_t t = tsc();
    for (int i = 0; i < N; i++) { sEnabledA = 0; restore_current(1); }
    report("A1 OSRestoreInterrupts(TRUE), QPC deadline check", tsc() - t, N);
    t = tsc();
    for (int i = 0; i < N; i++) { sEnabledB = 0; restore_candidate(1); }
    report("A2 OSRestoreInterrupts(TRUE), one atomic pending word", tsc() - t, N);
    t = tsc();
    for (int i = 0; i < N; i++) (void)time_base();
    report("A3 nebula_time_base() alone (QPC + scale)", tsc() - t, N);
}

/* B: the GX FIFO writer */

typedef struct { uint8_t* ptr; uint8_t* end; } Writer;
Writer gW;
NOINLINE void writer_full(void) { gW.ptr -= 1 << 20; }
static inline uint8_t* reserve(unsigned n) {
    if ((unsigned)(gW.end - gW.ptr) < n) writer_full();
    uint8_t* p = gW.ptr;
    gW.ptr = p + n;
    return p;
}
static inline void put8(uint8_t v) { *reserve(1) = v; }
static inline void put32(uint32_t v) {
    uint8_t* p = reserve(4);
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static inline void putf32(float f) { uint32_t b; memcpy(&b, &f, 4); put32(b); }

/* GXLoadPosMtxImm with a reservation per value. */
NOINLINE void load_mtx_current(const float m[3][4], uint32_t id) {
    put8(0x10);
    put32((uint32_t)(id * 4) | 0xB0000u);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) putf32(m[r][c]);
}
/* Room for the whole command checked once, the bytes written through a local pointer. */
static inline void st32(uint8_t* p, uint32_t v) { v = __builtin_bswap32(v); memcpy(p, &v, 4); }
NOINLINE void load_mtx_candidate(const float m[3][4], uint32_t id) {
    uint8_t* p = reserve(1 + 4 + 48);
    p[0] = 0x10;
    st32(p + 1, (uint32_t)(id * 4) | 0xB0000u);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) { uint32_t b; memcpy(&b, &m[r][c], 4); st32(p + 5 + (r * 4 + c) * 4, b); }
}
/* An immediate-mode vertex (position f32x3, colour u32, texcoord f32x2): GXPosition3f32/GXColor1u32/GXTexCoord2f32
   as three inline calls. */
NOINLINE void vertex_current(const float* v, uint32_t c) {
    putf32(v[0]); putf32(v[1]); putf32(v[2]); put32(c); putf32(v[3]); putf32(v[4]);
}
/* What the native GXVert.h does: one reservation per call (position, colour, texcoord). */
NOINLINE void vertex_per_call(const float* v, uint32_t c) {
    uint32_t b;
    uint8_t* p = reserve(12);
    memcpy(&b, &v[0], 4); st32(p, b);
    memcpy(&b, &v[1], 4); st32(p + 4, b);
    memcpy(&b, &v[2], 4); st32(p + 8, b);
    p = reserve(4);
    st32(p, c);
    p = reserve(8);
    memcpy(&b, &v[3], 4); st32(p, b);
    memcpy(&b, &v[4], 4); st32(p + 4, b);
}
NOINLINE void vertex_candidate(const float* v, uint32_t c) {
    uint8_t* p = reserve(24);
    uint32_t b;
    memcpy(&b, &v[0], 4); st32(p, b);
    memcpy(&b, &v[1], 4); st32(p + 4, b);
    memcpy(&b, &v[2], 4); st32(p + 8, b);
    st32(p + 12, c);
    memcpy(&b, &v[3], 4); st32(p + 16, b);
    memcpy(&b, &v[4], 4); st32(p + 20, b);
}

static uint8_t sFifo[(1 << 20) + 4096];
static void bench_writer(void) {
    float m[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};
    float v[5] = {1, 2, 3, 4, 5};
    enum { N = 2000000 };
    gW.ptr = sFifo; gW.end = sFifo + (1 << 20);
    uint64_t t = tsc();
    for (int i = 0; i < N; i++) load_mtx_current(m, (uint32_t)i & 7);
    report("B1 GXLoadPosMtxImm (53 B), a reserve per value", tsc() - t, N);
    gW.ptr = sFifo;
    t = tsc();
    for (int i = 0; i < N; i++) load_mtx_candidate(m, (uint32_t)i & 7);
    report("B2 GXLoadPosMtxImm (53 B), one reserve per command", tsc() - t, N);
    gW.ptr = sFifo;
    t = tsc();
    for (int i = 0; i < N; i++) vertex_current(v, 0xFF00FF00u);
    report("B3 vertex pos3f+clr+tex2f (24 B), a reserve per value", tsc() - t, N);
    gW.ptr = sFifo;
    t = tsc();
    for (int i = 0; i < N; i++) vertex_per_call(v, 0xFF00FF00u);
    report("B3b vertex pos3f+clr+tex2f (24 B), a reserve per call", tsc() - t, N);
    gW.ptr = sFifo;
    t = tsc();
    for (int i = 0; i < N; i++) vertex_candidate(v, 0xFF00FF00u);
    report("B4 vertex pos3f+clr+tex2f (24 B), one reserve per vertex", tsc() - t, N);
    t = tsc();
    for (int i = 0; i < 64; i++) memcpy(sFifo + 4096, sFifo, 1 << 20);
    report("B5 capture memcpy, per KB of command stream", tsc() - t, 64 * 1024);
}

/* C: frsqrte as the game uses it (TUtil<f32>::sqrt: estimate, one Newton step) */

static inline float frsqrte_current(float x) { return 1.0f / sqrtf(x); }
static inline float frsqrte_candidate(float x) { return _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x))); }
NOINLINE float tutil_sqrt_current(float val) {
    if (val <= 0.0f) return val;
    float o = frsqrte_current(val);
    return val * (0.5f * o * (3.0f - (val * (o * o))));
}
NOINLINE float tutil_sqrt_candidate(float val) {
    if (val <= 0.0f) return val;
    float o = frsqrte_candidate(val);
    return val * (0.5f * o * (3.0f - (val * (o * o))));
}
NOINLINE float sqrt_native(float val) { return val > 0.0f ? __builtin_sqrtf(val) : val; }

static void bench_rsqrt(void) {
    enum { N = 20000000 };
    float acc = 0.0f;
    uint64_t t = tsc();
    for (int i = 0; i < N; i++) acc = tutil_sqrt_current(acc + 2.0f);
    report("C1 TUtil::sqrt, estimate = 1/sqrtf, dependent", tsc() - t, N);
    float acc2 = 0.0f;
    t = tsc();
    for (int i = 0; i < N; i++) acc2 = tutil_sqrt_candidate(acc2 + 2.0f);
    report("C2 TUtil::sqrt, estimate = rsqrtss, dependent", tsc() - t, N);
    float acc3 = 0.0f;
    t = tsc();
    for (int i = 0; i < N; i++) acc3 = sqrt_native(acc3 + 2.0f);
    report("C3 sqrtss alone, dependent", tsc() - t, N);
    /* Accuracy against the double-precision root over a sweep. */
    double worstNow = 0, worstCand = 0;
    for (float x = 1e-6f; x < 1e7f; x *= 1.0001f) {
        const double ref = sqrt((double)x);
        const double e1 = fabs(tutil_sqrt_current(x) - ref) / ref, e2 = fabs(tutil_sqrt_candidate(x) - ref) / ref;
        if (e1 > worstNow) worstNow = e1;
        if (e2 > worstCand) worstCand = e2;
    }
    printf("   worst relative error: 1/sqrtf+Newton %.3g, rsqrtss+Newton %.3g (float ulp 6e-8) [%g %g %g]\n",
           worstNow, worstCand, acc, acc2, acc3);
}

/* D: float to int conversion, as colour packing does it */

NOINLINE void to_u8(const float* in, uint8_t* out, int n) {
    for (int i = 0; i < n; i++) out[i] = (uint8_t)(int)in[i];
}
NOINLINE int to_s32(float x) { return (int)x; }

static void bench_cast(const char* label) {
    static float in[4096];
    static uint8_t out[4096];
    for (int i = 0; i < 4096; i++) in[i] = (float)(i & 255) + 0.5f;
    enum { R = 4000 };
    uint64_t t = tsc();
    for (int r = 0; r < R; r++) to_u8(in, out, 4096);
    char name[96];
    snprintf(name, sizeof name, "D1 (u8)(int)f loop, %s", label);
    report(name, tsc() - t, (uint64_t)R * 4096);
    volatile int sink = 0;
    t = tsc();
    for (int i = 0; i < 20000000; i++) sink += to_s32((float)i);
    snprintf(name, sizeof name, "D2 (int)f call, %s", label);
    report(name, tsc() - t, 20000000);
}

/* E: big-endian field reads (BE<T>) against host-order reads */

typedef struct { uint32_t a; float b; uint16_t c, d; } Native;
typedef struct { uint32_t a; uint32_t b; uint16_t c, d; } Raw;
NOINLINE float sum_native(const Native* p, int n) {
    float s = 0; uint32_t k = 0;
    for (int i = 0; i < n; i++) { k += p[i].a + p[i].c + p[i].d; s += p[i].b; }
    return s + (float)k;
}
NOINLINE float sum_be(const Raw* p, int n) {
    float s = 0; uint32_t k = 0;
    for (int i = 0; i < n; i++) {
        k += __builtin_bswap32(p[i].a) + __builtin_bswap16(p[i].c) + __builtin_bswap16(p[i].d);
        uint32_t b = __builtin_bswap32(p[i].b); float f; memcpy(&f, &b, 4); s += f;
    }
    return s + (float)k;
}
static void bench_be(void) {
    static Native nat[8192];
    static Raw raw[8192];
    for (int i = 0; i < 8192; i++) { nat[i].a = i; nat[i].b = (float)i; nat[i].c = i; nat[i].d = 1; memcpy(&raw[i], &nat[i], 12); }
    enum { R = 4000 };
    volatile float sink;
    uint64_t t = tsc();
    for (int r = 0; r < R; r++) { __asm__ volatile("" : : "r"(nat) : "memory"); sink = sum_native(nat, 8192); }
    report("E1 struct reads (4 fields), host order", tsc() - t, (uint64_t)R * 8192);
    t = tsc();
    for (int r = 0; r < R; r++) { __asm__ volatile("" : : "r"(raw) : "memory"); sink = sum_be(raw, 8192); }
    report("E2 struct reads (4 fields), big-endian", tsc() - t, (uint64_t)R * 8192);
    (void)sink;
}

/* G: how late a host timer thread wakes for a deadline (the decrementer as a timer thread) */

static void bench_timer(void) {
    HANDLE hr = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    const int periods_us[] = {500, 1000, 6667};
    for (int k = 0; k < 3; k++) {
        double worst = 0, sum = 0;
        for (int i = 0; i < 200; i++) {
            LARGE_INTEGER a, b, due;
            QueryPerformanceCounter(&a);
            due.QuadPart = -(LONGLONG)periods_us[k] * 10;
            SetWaitableTimer(hr, &due, 0, NULL, NULL, FALSE);
            WaitForSingleObject(hr, INFINITE);
            QueryPerformanceCounter(&b);
            const double late = (double)(b.QuadPart - a.QuadPart) * 1e6 / (double)f.QuadPart - periods_us[k];
            sum += late; if (late > worst) worst = late;
        }
        printf("G  high-resolution waitable timer, %5d us: late mean %7.1f us, worst %7.1f us\n", periods_us[k], sum / 200, worst);
    }
    CloseHandle(hr);
}

int main(int argc, char** argv) {
    SetThreadAffinityMask(GetCurrentThread(), 1u << 2);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    calibrate();
    if (argc > 1 && strcmp(argv[1], "cast") == 0) { bench_cast(argc > 2 ? argv[2] : "?"); return 0; }
    bench_interrupts();
    bench_writer();
    bench_rsqrt();
    bench_be();
    bench_timer();
    return 0;
}
