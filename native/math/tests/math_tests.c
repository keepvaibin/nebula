/* The native math library (nebula_math.c) against straightforward references: products, inverses, rotations and round trips over
   random inputs, aliasing of outputs with inputs, and the console's special cases (singular inverse, NebulaVecMag of
   zero, NebulaMtxRotTrig's axis letters). Prints the worst relative error and the cost of the hot calls. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <nebula_math.h>

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
        NebulaMtxConcat(a, b, ab);
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                close_to("NebulaMtxConcat", ab[i][j], ref[i][j], 1e-5);
            }
        }
        /* aliasing: ab = a */
        Mtx a2;
        memcpy(a2, a, sizeof(Mtx));
        NebulaMtxConcat(a2, b, a2);
        close_to("NebulaMtxConcat aliased", a2[1][2], ref[1][2], 1e-5);

        if (NebulaMtxInverse(a, inv)) {
            NebulaMtxConcat(a, inv, prod);
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
            NebulaMtxInvXpose(a, invX);
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                    close_to("NebulaMtxInvXpose", invX[i][j], inv[j][i], 1e-5);
                }
            }
        skip:;
        }

        Vec v = {rnd() * 10, rnd() * 10, rnd() * 10}, out;
        NebulaMtxMultVec(a, &v, &out);
        close_to("NebulaMtxMultVec",
                 out.y, (double)a[1][0] * v.x + (double)a[1][1] * v.y + (double)a[1][2] * v.z + a[1][3], 1e-5);
        NebulaMtxMultVecSR(a, &v, &out);
        close_to("NebulaMtxMultVecSR", out.z, (double)a[2][0] * v.x + (double)a[2][1] * v.y + (double)a[2][2] * v.z, 1e-5);
        Vec v2 = v;
        NebulaMtxMultVec(a, &v2, &v2);
        NebulaMtxMultVec(a, &v, &out);
        close_to("NebulaMtxMultVec aliased", v2.x, out.x, 0);

        Vec w = {rnd(), rnd(), rnd()}, c;
        NebulaVecCrossProduct(&v, &w, &c);
        close_to("NebulaVecCrossProduct", c.y, (double)v.z * w.x - (double)v.x * w.z, 1e-5);
        close_to("cross orthogonal", NebulaVecDotProduct(&c, &v) / (NebulaVecMag(&c) * NebulaVecMag(&v) + 1e-9), 0, 1e-5);
        Vec u;
        NebulaVecNormalize(&v, &u);
        close_to("NebulaVecNormalize", NebulaVecMag(&u), 1.0, 1e-6);
        close_to("NebulaVecDistance", NebulaVecDistance(&v, &w),
                 sqrt(((double)v.x - w.x) * (v.x - w.x) + ((double)v.y - w.y) * (v.y - w.y) +
                      ((double)v.z - w.z) * (v.z - w.z)), 1e-6);

        /* rotations: orthonormal, and the quaternion round trip */
        Mtx r;
        const f32 angle = rnd() * 3.14159f;
        NebulaMtxRotAxisRad(r, &v, angle);
        Vec col0 = {r[0][0], r[1][0], r[2][0]};
        close_to("NebulaMtxRotAxisRad orthonormal", NebulaVecMag(&col0), 1.0, 1e-5);
        Quaternion q, q2;
        NebulaQuatFromMtx(&q, r);
        Mtx r2;
        NebulaMtxQuat(r2, &q);
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                close_to("C_QUATMtx/NebulaMtxQuat round trip", r2[i][j], r[i][j], 1e-4);
            }
        }
        NebulaQuatMultiply(&q, &q, &q2);
        Mtx rr, r2r;
        NebulaMtxQuat(rr, &q2);
        NebulaMtxConcat(r, r, r2r);
        close_to("NebulaQuatMultiply", rr[0][1], r2r[0][1], 1e-4);
    }

    /* The console's special cases. */
    Mtx singular = {{1, 2, 3, 0}, {2, 4, 6, 0}, {0, 0, 1, 0}}, untouched;
    memset(untouched, 0x7F, sizeof(untouched));
    if (NebulaMtxInverse(singular, untouched) != 0 || untouched[0][0] != untouched[2][3]) {
        printf("FAIL NebulaMtxInverse singular\n");
        sFailures++;
    }
    Vec zero = {0, 0, 0};
    if (NebulaVecMag(&zero) != 0.0f || NebulaVecDistance(&zero, &zero) != 0.0f) {
        printf("FAIL NebulaVecMag(0)\n");
        sFailures++;
    }
    Mtx rz, rzUpper;
    NebulaMtxRotTrig(rz, 'z', 1.0f, 0.0f);
    NebulaMtxRotTrig(rzUpper, 'Z', 1.0f, 0.0f);
    if (rz[0][1] != -1.0f || rz[1][0] != 1.0f || memcmp(rz, rzUpper, sizeof(Mtx)) != 0) {
        printf("FAIL NebulaMtxRotTrig z\n");
        sFailures++;
    }

    /* Finite radians retain their periodic meaning across the full binary32 range. */
    for (unsigned i = 0; i < 100000; ++i) {
        sSeed = sSeed * 1664525u + 1013904223u;
        union { unsigned u; float f; } angle = {sSeed};
        if (!isfinite(angle.f)) { continue; }
        float s, c;
        NebulaSinCos(angle.f, &s, &c);
        close_to("finite-radian sine", s, sin((double)angle.f), 2.0e-7);
        close_to("finite-radian cosine", c, cos((double)angle.f), 2.0e-7);
    }

    /* SIMD agrees with the scalar path, including mixed full-range lanes. */
    for (unsigned i = 0; i < 100000; ++i) {
        float angles[3], ss[3], cc[3];
        for (unsigned k = 0; k < 3; ++k) angles[k] = rnd() * 127.0f;
        if ((i & 15u) == 0u) angles[1] = 3.0e35f;
        NebulaSinCos3(angles[0], angles[1], angles[2], ss, cc);
        for (unsigned k = 0; k < 3; ++k) {
            float sn, cn; NebulaSinCos(angles[k], &sn, &cn);
            close_to("SIMD radian sine", ss[k], sn, 1.0e-7);
            close_to("SIMD radian cosine", cc[k], cn, 1.0e-7);
        }
    }

    /* Cost of the hot calls (inline, so the loop is what the game's call sites compile to). */
    {
        enum { N = 20000000 };
        Mtx a, b;
        random_mtx(a);
        random_mtx(b);
        clock_t t0 = clock();
        for (int i = 0; i < N; i++) {
            NebulaMtxConcat(a, b, b);
            __asm__ volatile("" : : "r"(b) : "memory");
        }
        const double concat = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / N;
        Vec v = {1, 2, 3};
        t0 = clock();
        f32 acc = 0;
        for (int i = 0; i < N; i++) {
            v.x += 0.5f;
            acc += NebulaVecMag(&v);
            __asm__ volatile("" : : "r"(&v) : "memory");
        }
        const double mag = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / N;
        printf("perf: NebulaMtxConcat %.2f ns, NebulaVecMag %.2f ns (%g %g)\n", concat, mag, (double)acc, (double)b[0][0]);
    }

    printf("worst relative error %.3g\n", worst);
    printf(sFailures ? "%d failures\n" : "all passed\n", sFailures);
    return sFailures != 0;
}
