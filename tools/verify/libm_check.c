/* libm_check: the module's math (nebula_libm.c, compiled with every name prefixed nm_) against the C library over dense
   ranges: how often each float result differs, by how many units, and the largest difference in double. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

double nm_sin(double);
double nm_cos(double);
double nm_tan(double);
double nm_atan(double);
double nm_atan2(double, double);
double nm_asin(double);
double nm_acos(double);
double nm_pow(double, double);
float nm_sinf(float);
float nm_cosf(float);
float nm_tanf(float);
float nm_atanf(float);
float nm_atan2f(float, float);
float nm_asinf(float);
float nm_acosf(float);
float nm_powf(float, float);
float nm_floorf(float);
float nm_ceilf(float);
float nm_fmodf(float, float);

static uint64_t gState = 88172645463325252ull;

static uint64_t next64(void) {
    gState ^= gState << 13;
    gState ^= gState >> 7;
    gState ^= gState << 17;
    return gState;
}

static float uniform(float lo, float hi) {
    return lo + (hi - lo) * (float)(next64() >> 40) / 16777216.0f;
}

static int64_t ulps(float a, float b) {
    if (a != a && b != b) {
        return 0;
    }
    if (a != a || b != b) {
        return 1000000;
    }
    int32_t ia, ib;
    memcpy(&ia, &a, 4);
    memcpy(&ib, &b, 4);
    if (ia < 0) {
        ia = (int32_t)(0x80000000u - (uint32_t)ia);
    }
    if (ib < 0) {
        ib = (int32_t)(0x80000000u - (uint32_t)ib);
    }
    const int64_t d = (int64_t)ia - ib;
    return d < 0 ? -d : d;
}

typedef float (*F1)(float);
typedef float (*F2)(float, float);

static void check1(const char* name, F1 mine, F1 theirs, float lo, float hi) {
    int64_t worst = 0;
    int64_t diff = 0;
    float at = 0.0f;
    const int n = 4000000;
    for (int i = 0; i < n; ++i) {
        const float x = uniform(lo, hi);
        const int64_t d = ulps(mine(x), theirs(x));
        if (d != 0) {
            ++diff;
        }
        if (d > worst) {
            worst = d;
            at = x;
        }
    }
    printf("%-8s [%g, %g] differ %.4f%% worst %lld ulp at %.9g\n", name, lo, hi, 100.0 * diff / n, (long long)worst, at);
}

static void check2(const char* name, F2 mine, F2 theirs, float lo, float hi, float lo2, float hi2) {
    int64_t worst = 0;
    int64_t diff = 0;
    float ax = 0.0f, ay = 0.0f;
    const int n = 4000000;
    for (int i = 0; i < n; ++i) {
        const float x = uniform(lo, hi);
        const float y = uniform(lo2, hi2);
        const int64_t d = ulps(mine(x, y), theirs(x, y));
        if (d != 0) {
            ++diff;
        }
        if (d > worst) {
            worst = d;
            ax = x;
            ay = y;
        }
    }
    printf("%-8s differ %.4f%% worst %lld ulp at %.9g %.9g\n", name, 100.0 * diff / n, (long long)worst, ax, ay);
}

static void checkd(const char* name, double (*mine)(double), double (*theirs)(double), double lo, double hi) {
    double worst = 0.0;
    double at = 0.0;
    for (int i = 0; i < 4000000; ++i) {
        const double x = lo + (hi - lo) * (double)(next64() >> 11) / 9007199254740992.0;
        const double a = mine(x);
        const double b = theirs(x);
        const double e = b != 0.0 ? fabs((a - b) / b) : fabs(a - b);
        if (e > worst) {
            worst = e;
            at = x;
        }
    }
    printf("%-8s double worst relative %.3g at %.17g\n", name, worst, at);
}

int main(void) {
    check1("sinf", nm_sinf, sinf, -7.0f, 7.0f);
    check1("sinf", nm_sinf, sinf, -100000.0f, 100000.0f);
    check1("cosf", nm_cosf, cosf, -7.0f, 7.0f);
    check1("cosf", nm_cosf, cosf, -100000.0f, 100000.0f);
    check1("tanf", nm_tanf, tanf, -1.6f, 1.6f);
    check1("atanf", nm_atanf, atanf, -50.0f, 50.0f);
    check1("asinf", nm_asinf, asinf, -1.0f, 1.0f);
    check1("acosf", nm_acosf, acosf, -1.0f, 1.0f);
    check1("floorf", nm_floorf, floorf, -100000.0f, 100000.0f);
    check1("floorf", nm_floorf, floorf, -2.0f, 2.0f);
    check1("ceilf", nm_ceilf, ceilf, -100000.0f, 100000.0f);
    check2("atan2f", nm_atan2f, atan2f, -1000.0f, 1000.0f, -1000.0f, 1000.0f);
    check2("powf", nm_powf, powf, 0.0f, 100.0f, -4.0f, 4.0f);
    check2("fmodf", nm_fmodf, fmodf, -100000.0f, 100000.0f, 0.001f, 500.0f);
    checkd("sin", nm_sin, sin, -7.0, 7.0);
    checkd("cos", nm_cos, cos, -7.0, 7.0);
    checkd("tan", nm_tan, tan, -1.5, 1.5);
    checkd("atan", nm_atan, atan, -50.0, 50.0);
    checkd("asin", nm_asin, asin, -1.0, 1.0);
    checkd("acos", nm_acos, acos, -1.0, 1.0);
    printf("edge: sinf(0)=%g cosf(0)=%g atan2f(0,-1)=%.9g atan2f(-0,-1)=%.9g atan2f(1,0)=%.9g fmodf(5.5,2)=%g powf(2,10)=%g powf(0,0)=%g floorf(-0.5)=%g ceilf(-0.5)=%g\n",
           nm_sinf(0.0f), nm_cosf(0.0f), nm_atan2f(0.0f, -1.0f), nm_atan2f(-0.0f, -1.0f), nm_atan2f(1.0f, 0.0f), nm_fmodf(5.5f, 2.0f), nm_powf(2.0f, 10.0f), nm_powf(0.0f, 0.0f), nm_floorf(-0.5f), nm_ceilf(-0.5f));
    return 0;
}
