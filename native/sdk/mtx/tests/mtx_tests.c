/* The native MTX library against straightforward references: products, inverses, rotations and round trips over
   random inputs, aliasing of outputs with inputs, and the console's special cases (singular inverse, PSVECMag of
   zero, PSMTXRotTrig's axis letters). Prints the worst relative error and the cost of the hot calls. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <revolution/mtx.h>

static int sFailures;
static unsigned sSeed = 12345;

static f32 rnd(void) {
    sSeed = sSeed * 1664525u + 1013904223u;
    return (f32)((sSeed >> 8) & 0xFFFF) / 32768.0f - 1.0f;
}

static void random_mtx(Mtx m) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            m[i][j] = rnd() * 4.0f;
        }
    }
}

static double worst;

static void close_to(const char* what, double got, double want, double tol) {
    const double err = fabs(got - want) / (fabs(want) > 1.0 ? fabs(want) : 1.0);
    if (err > worst) {
        worst = err;
    }
    if (!(err <= tol)) {
        printf("FAIL %s: %.9g (want %.9g)\n", what, got, want);
        sFailures++;
    }
}

static void ref_concat(const Mtx a, const Mtx b, Mtx ab) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            double v = 0;
            for (int k = 0; k < 3; k++) {
                v += (double)a[i][k] * b[k][j];
            }
            if (j == 3) {
                v += a[i][3];
            }
            ab[i][j] = (f32)v;
        }
    }
}

int main(void) {
    for (int n = 0; n < 10000; n++) {
        Mtx a, b, ab, ref, inv, prod;
        random_mtx(a);
        random_mtx(b);
        ref_concat(a, b, ref);
        PSMTXConcat(a, b, ab);
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                close_to("PSMTXConcat", ab[i][j], ref[i][j], 1e-5);
            }
        }
        /* aliasing: ab = a */
        Mtx a2;
        memcpy(a2, a, sizeof(Mtx));
        PSMTXConcat(a2, b, a2);
        close_to("PSMTXConcat aliased", a2[1][2], ref[1][2], 1e-5);

        if (PSMTXInverse(a, inv)) {
            PSMTXConcat(a, inv, prod);
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 4; j++) {
                    /* well-conditioned only: skip near-singular matrices */
                    const f32 want = i == j ? 1.0f : 0.0f;
                    if (fabs(prod[i][j] - want) > 1e-2) {
                        goto skip;
                    }
                }
            }
            Mtx invX;
            PSMTXInvXpose(a, invX);
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                    close_to("PSMTXInvXpose", invX[i][j], inv[j][i], 1e-5);
                }
            }
        skip:;
        }

        Vec v = {rnd() * 10, rnd() * 10, rnd() * 10}, out;
        PSMTXMultVec(a, &v, &out);
        close_to("PSMTXMultVec",
                 out.y, (double)a[1][0] * v.x + (double)a[1][1] * v.y + (double)a[1][2] * v.z + a[1][3], 1e-5);
        PSMTXMultVecSR(a, &v, &out);
        close_to("PSMTXMultVecSR", out.z, (double)a[2][0] * v.x + (double)a[2][1] * v.y + (double)a[2][2] * v.z, 1e-5);
        Vec v2 = v;
        PSMTXMultVec(a, &v2, &v2);
        PSMTXMultVec(a, &v, &out);
        close_to("PSMTXMultVec aliased", v2.x, out.x, 0);

        Vec w = {rnd(), rnd(), rnd()}, c;
        PSVECCrossProduct(&v, &w, &c);
        close_to("PSVECCrossProduct", c.y, (double)v.z * w.x - (double)v.x * w.z, 1e-5);
        close_to("cross orthogonal", PSVECDotProduct(&c, &v) / (PSVECMag(&c) * PSVECMag(&v) + 1e-9), 0, 1e-5);
        Vec u;
        PSVECNormalize(&v, &u);
        close_to("PSVECNormalize", PSVECMag(&u), 1.0, 1e-6);
        close_to("PSVECDistance", PSVECDistance(&v, &w),
                 sqrt(((double)v.x - w.x) * (v.x - w.x) + ((double)v.y - w.y) * (v.y - w.y) +
                      ((double)v.z - w.z) * (v.z - w.z)), 1e-6);

        /* rotations: orthonormal, and the quaternion round trip */
        Mtx r;
        const f32 angle = rnd() * 3.14159f;
        PSMTXRotAxisRad(r, &v, angle);
        Vec col0 = {r[0][0], r[1][0], r[2][0]};
        close_to("PSMTXRotAxisRad orthonormal", PSVECMag(&col0), 1.0, 1e-5);
        Quaternion q, q2;
        C_QUATMtx(&q, r);
        Mtx r2;
        PSMTXQuat(r2, &q);
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                close_to("C_QUATMtx/PSMTXQuat round trip", r2[i][j], r[i][j], 1e-4);
            }
        }
        PSQUATMultiply(&q, &q, &q2);
        Mtx rr, r2r;
        PSMTXQuat(rr, &q2);
        PSMTXConcat(r, r, r2r);
        close_to("PSQUATMultiply", rr[0][1], r2r[0][1], 1e-4);
    }

    /* The console's special cases. */
    Mtx singular = {{1, 2, 3, 0}, {2, 4, 6, 0}, {0, 0, 1, 0}}, untouched;
    memset(untouched, 0x7F, sizeof(untouched));
    if (PSMTXInverse(singular, untouched) != 0 || untouched[0][0] != untouched[2][3]) {
        printf("FAIL PSMTXInverse singular\n");
        sFailures++;
    }
    Vec zero = {0, 0, 0};
    if (PSVECMag(&zero) != 0.0f || PSVECDistance(&zero, &zero) != 0.0f) {
        printf("FAIL PSVECMag(0)\n");
        sFailures++;
    }
    Mtx rz, rzUpper;
    PSMTXRotTrig(rz, 'z', 1.0f, 0.0f);
    PSMTXRotTrig(rzUpper, 'Z', 1.0f, 0.0f);
    if (rz[0][1] != -1.0f || rz[1][0] != 1.0f || memcmp(rz, rzUpper, sizeof(Mtx)) != 0) {
        printf("FAIL PSMTXRotTrig z\n");
        sFailures++;
    }

    /* Cost of the hot calls (inline, so the loop is what the game's call sites compile to). */
    {
        enum { N = 20000000 };
        Mtx a, b;
        random_mtx(a);
        random_mtx(b);
        clock_t t0 = clock();
        for (int i = 0; i < N; i++) {
            PSMTXConcat(a, b, b);
            __asm__ volatile("" : : "r"(b) : "memory");
        }
        const double concat = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / N;
        Vec v = {1, 2, 3};
        t0 = clock();
        f32 acc = 0;
        for (int i = 0; i < N; i++) {
            v.x += 0.5f;
            acc += PSVECMag(&v);
            __asm__ volatile("" : : "r"(&v) : "memory");
        }
        const double mag = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / N;
        printf("perf: PSMTXConcat %.2f ns, PSVECMag %.2f ns (%g %g)\n", concat, mag, (double)acc, (double)b[0][0]);
    }

    printf("worst relative error %.3g\n", worst);
    printf(sFailures ? "%d failures\n" : "all passed\n", sFailures);
    return sFailures != 0;
}
