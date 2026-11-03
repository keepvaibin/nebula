/* The module's float math: the same results on every platform, from no system library. Floats are worked in double and
   rounded once. Series coefficients are the fdlibm minimax polynomials (Sun Microsystems; freely usable with this
   notice kept: "Developed at SunPro, a Sun Microsystems, Inc. business. Permission to use, copy, modify, and distribute
   this software is freely granted, provided that this notice is preserved."). */
#include <stdint.h>

#define NEBULA_NAN (0.0 / 0.0)

static inline uint64_t bits64(double x) {
    return __builtin_bit_cast(uint64_t, x);
}

static inline double from64(uint64_t b) {
    return __builtin_bit_cast(double, b);
}

static inline uint32_t bits32(float x) {
    return __builtin_bit_cast(uint32_t, x);
}

static inline float from32(uint32_t b) {
    return __builtin_bit_cast(float, b);
}

static inline double absd(double x) {
    return from64(bits64(x) & 0x7FFFFFFFFFFFFFFFull);
}

static inline double sqrtd(double x) {
    return __builtin_sqrt(x);
}

double trunc(double x) {
    const uint64_t b = bits64(x);
    const int e = (int)(b >> 52 & 0x7FF) - 1023;
    if (e >= 52) {
        return x;
    }
    if (e < 0) {
        return from64(b & 0x8000000000000000ull);
    }
    return from64(b & ~(0x000FFFFFFFFFFFFFull >> e));
}

double floor(double x) {
    const double t = trunc(x);
    return t > x ? t - 1.0 : t;
}

double round(double x) {
    const double t = trunc(x);
    const double d = absd(x - t);
    return d >= 0.5 ? t + (x < 0.0 ? -1.0 : 1.0) : t;
}

/* x * 2^n, n within the normal range of the result. */
static double scale2(double x, int n) {
    while (n > 1000) {
        x *= from64((uint64_t)(1023 + 1000) << 52);
        n -= 1000;
    }
    while (n < -1000) {
        x *= from64((uint64_t)(1023 - 1000) << 52);
        n += 1000;
    }
    return x * from64((uint64_t)(1023 + n) << 52);
}

static double sinKernel(double x, double y) {
    const double z = x * x;
    const double r = 8.33333333332248946124e-03 +
                     z * (-1.98412698298579493134e-04 +
                          z * (2.75573137070700676789e-06 + z * (-2.50507602534068634195e-08 + z * 1.58969099521155010221e-10)));
    return x - ((z * (0.5 * y - x * z * r) - y) - x * z * -1.66666666666666324348e-01);
}

static double cosKernel(double x, double y) {
    const double z = x * x;
    const double r = z * (4.16666666666666019037e-02 +
                          z * (-1.38888888888741095749e-03 +
                               z * (2.48015872894767294178e-05 +
                                    z * (-2.75573143513906633035e-07 +
                                         z * (2.08757232129817482790e-09 + z * -1.13596475577881948265e-11)))));
    const double half = 0.5 * z;
    const double w = 1.0 - half;
    return w + (((1.0 - w) - half) + (z * r - x * y));
}

/* x as a remainder within pi/4, its quadrant returned. Past 2^30 the remainder comes from fmod of a long multiple. */
static int reduce(double x, double* pHi, double* pLo) {
    if (absd(x) > 1073741824.0) {
        const double turn = 6.283185307179586476925286766559;
        const double n = __builtin_trunc(x / turn);
        x -= n * turn;
    }
    const double k = __builtin_round(x * 6.36619772367581382433e-01);
    const double t = x - k * 1.57079632673412561417e+00;
    const double w = k * 6.07710050650619224932e-11;
    double hi = t - w;
    double lo = (t - hi) - w;
    if (absd(hi) < 1e-5) {
        /* Close to a multiple: the second step keeps the digits the first cancelled. */
        const double a = k * 6.07710050630396597660e-11;
        const double t2 = t - a;
        const double w2 = k * 2.02226624879595063154e-21 - ((t - t2) - a);
        hi = t2 - w2;
        lo = (t2 - hi) - w2;
    }
    *pHi = hi;
    *pLo = lo;
    return (int)(int64_t)k & 3;
}

double sin(double x) {
    if ((bits64(x) >> 52 & 0x7FF) == 0x7FF) {
        return NEBULA_NAN;
    }
    double hi, lo;
    switch (reduce(x, &hi, &lo)) {
    case 0:
        return sinKernel(hi, lo);
    case 1:
        return cosKernel(hi, lo);
    case 2:
        return -sinKernel(hi, lo);
    default:
        return -cosKernel(hi, lo);
    }
}

double cos(double x) {
    if ((bits64(x) >> 52 & 0x7FF) == 0x7FF) {
        return NEBULA_NAN;
    }
    double hi, lo;
    switch (reduce(x, &hi, &lo)) {
    case 0:
        return cosKernel(hi, lo);
    case 1:
        return -sinKernel(hi, lo);
    case 2:
        return -cosKernel(hi, lo);
    default:
        return sinKernel(hi, lo);
    }
}

double tan(double x) {
    if ((bits64(x) >> 52 & 0x7FF) == 0x7FF) {
        return NEBULA_NAN;
    }
    double hi, lo;
    const int q = reduce(x, &hi, &lo);
    const double s = sinKernel(hi, lo);
    const double c = cosKernel(hi, lo);
    return q & 1 ? -c / s : s / c;
}

double atan(double x) {
    static const double kHi[] = {4.63647609000806093515e-01, 7.85398163397448278999e-01, 9.82793723247329054082e-01, 1.57079632679489655800e+00};
    static const double kLo[] = {2.26987774529616870924e-17, 3.06161699786838301793e-17, 1.39033110312309984516e-17, 6.12323399573676603587e-17};
    if (x != x) {
        return x;
    }
    const double a = absd(x);
    if (a >= 7.3786976294838206e+19) {
        return x > 0.0 ? kHi[3] + kLo[3] : -(kHi[3] + kLo[3]);
    }
    int id = -1;
    double u = a;
    if (a >= 0.4375) {
        if (a < 0.6875) {
            id = 0;
            u = (2.0 * a - 1.0) / (2.0 + a);
        } else if (a < 1.1875) {
            id = 1;
            u = (a - 1.0) / (a + 1.0);
        } else if (a < 2.4375) {
            id = 2;
            u = (a - 1.5) / (1.0 + 1.5 * a);
        } else {
            id = 3;
            u = -1.0 / a;
        }
    }
    const double z = u * u;
    const double w = z * z;
    const double s1 = z * (3.33333333333329318027e-01 +
                           w * (1.42857142725034663711e-01 +
                                w * (9.09088713343650656196e-02 +
                                     w * (6.66107313738753120669e-02 + w * (4.97687799461593236017e-02 + w * 1.62858201153657823623e-02)))));
    const double s2 = w * (-1.99999999998764832476e-01 +
                           w * (-1.11111104054623557880e-01 +
                                w * (-7.69187620504482999495e-02 + w * (-5.83357013379057348645e-02 + w * -3.65315727442169155270e-02))));
    const double r = id < 0 ? u - u * (s1 + s2) : kHi[id] - ((u * (s1 + s2) - kLo[id]) - u);
    return x < 0.0 ? -r : r;
}

static double atan2d(double y, double x) {
    const double pi = 3.14159265358979311600e+00;
    const double piLo = 1.2246467991473531772e-16;
    if (x != x || y != y) {
        return x + y;
    }
    const int yNeg = (int)(bits64(y) >> 63);
    const int xNeg = (int)(bits64(x) >> 63);
    if (y == 0.0) {
        return xNeg ? (yNeg ? -pi - piLo : pi + piLo) : y;
    }
    if (x == 0.0) {
        return yNeg ? -pi / 2.0 : pi / 2.0;
    }
    const int xInf = absd(x) == __builtin_inf();
    const int yInf = absd(y) == __builtin_inf();
    if (xInf && yInf) {
        const double r = xNeg ? 3.0 * pi / 4.0 : pi / 4.0;
        return yNeg ? -r : r;
    }
    if (xInf) {
        return xNeg ? (yNeg ? -pi - piLo : pi + piLo) : (yNeg ? -0.0 : 0.0);
    }
    if (yInf) {
        return yNeg ? -pi / 2.0 : pi / 2.0;
    }
    const double a = atan(absd(y / x));
    const double r = xNeg ? pi - (a - piLo) : a;
    return yNeg ? -r : r;
}

double atan2(double y, double x) {
    return atan2d(y, x);
}

double asin(double x) {
    if (absd(x) > 1.0 || x != x) {
        return NEBULA_NAN;
    }
    return atan2d(x, sqrtd((1.0 - x) * (1.0 + x)));
}

double acos(double x) {
    if (absd(x) > 1.0 || x != x) {
        return NEBULA_NAN;
    }
    return atan2d(sqrtd((1.0 - x) * (1.0 + x)), x);
}

static double logd(double x) {
    if (x != x || x < 0.0) {
        return NEBULA_NAN;
    }
    if (x == 0.0) {
        return -__builtin_inf();
    }
    if (x == __builtin_inf()) {
        return x;
    }
    int e = 0;
    if (bits64(x) >> 52 == 0) {
        x *= 18014398509481984.0;
        e -= 54;
    }
    const uint64_t b = bits64(x);
    e += (int)(b >> 52) - 1023;
    double m = from64((b & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull);
    if (m > 1.41421356237309504880) {
        m *= 0.5;
        e += 1;
    }
    const double f = m - 1.0;
    const double s = f / (2.0 + f);
    const double z = s * s;
    const double w = z * z;
    const double t1 = w * (3.999999999940941908e-01 + w * (2.222219843214978396e-01 + w * 1.531383769920937332e-01));
    const double t2 = z * (6.666666666666735130e-01 + w * (2.857142874366239149e-01 + w * (1.818357216161805012e-01 + w * 1.479819860511658591e-01)));
    const double r = t1 + t2;
    const double hf = 0.5 * f * f;
    const double k = (double)e;
    return k * 6.93147180369123816490e-01 - ((hf - (s * (hf + r) + k * 1.90821492927058770002e-10)) - f);
}

static double expd(double x) {
    if (x != x) {
        return x;
    }
    if (x > 709.782712893383973096) {
        return __builtin_inf();
    }
    if (x < -745.13321910194110842) {
        return 0.0;
    }
    const double k = __builtin_round(x * 1.44269504088896338700e+00);
    const double hi = x - k * 6.93147180369123816490e-01;
    const double lo = k * 1.90821492927058770002e-10;
    const double r = hi - lo;
    const double t = r * r;
    const double c = r - t * (1.66666666666666019037e-01 +
                              t * (-2.77777777770155933842e-03 +
                                   t * (6.61375632143793436117e-05 + t * (-1.65339022054652515390e-06 + t * 4.13813679705723846039e-08))));
    const double y = 1.0 - ((lo - (r * c) / (2.0 - c)) - hi);
    return scale2(y, (int)k);
}

double pow(double x, double y) {
    if (y == 0.0 || x == 1.0) {
        return 1.0;
    }
    if (x != x || y != y) {
        return NEBULA_NAN;
    }
    const double whole = __builtin_trunc(y);
    const int odd = whole == y && absd(y) < 9007199254740992.0 && ((int64_t)y & 1);
    if (x == 0.0) {
        const double r = y > 0.0 ? 0.0 : __builtin_inf();
        return odd && bits64(x) >> 63 ? -r : r;
    }
    if (x < 0.0 && whole != y) {
        return NEBULA_NAN;
    }
    const double r = expd(y * logd(absd(x)));
    return x < 0.0 && odd ? -r : r;
}

float sinf(float x) {
    return (float)sin(x);
}

float cosf(float x) {
    return (float)cos(x);
}

float tanf(float x) {
    return (float)tan(x);
}

float atanf(float x) {
    return (float)atan(x);
}

float atan2f(float y, float x) {
    return (float)atan2d(y, x);
}

float asinf(float x) {
    return (float)asin(x);
}

float acosf(float x) {
    return (float)acos(x);
}

float powf(float x, float y) {
    return (float)pow(x, y);
}

float floorf(float x) {
    const uint32_t b = bits32(x);
    const int e = (int)(b >> 23 & 0xFF) - 127;
    if (e >= 23) {
        return x;
    }
    if (e < 0) {
        if ((b & 0x7FFFFFFFu) == 0) {
            return x;
        }
        return b >> 31 ? -1.0f : 0.0f;
    }
    const uint32_t mask = 0x007FFFFFu >> e;
    if ((b & mask) == 0) {
        return x;
    }
    return from32(((b >> 31) ? b + mask : b) & ~mask);
}

float ceilf(float x) {
    return -floorf(-x);
}

/* The exact remainder, the sign of x: long division of the mantissas. */
float fmodf(float x, float y) {
    uint32_t ax = bits32(x);
    uint32_t ay = bits32(y);
    const uint32_t sign = ax & 0x80000000u;
    ax &= 0x7FFFFFFFu;
    ay &= 0x7FFFFFFFu;
    if (ay == 0 || ax >= 0x7F800000u || ay > 0x7F800000u) {
        return (x * y) / (x * y);
    }
    if (ax < ay) {
        return x;
    }
    int ex = (int)(ax >> 23);
    int ey = (int)(ay >> 23);
    uint32_t mx;
    uint32_t my;
    if (ex == 0) {
        mx = ax << 8;
        ex = 1;
        while ((mx & 0x80000000u) == 0) {
            mx <<= 1;
            --ex;
        }
        mx >>= 8;
    } else {
        mx = (ax & 0x007FFFFFu) | 0x00800000u;
    }
    if (ey == 0) {
        my = ay << 8;
        ey = 1;
        while ((my & 0x80000000u) == 0) {
            my <<= 1;
            --ey;
        }
        my >>= 8;
    } else {
        my = (ay & 0x007FFFFFu) | 0x00800000u;
    }
    for (; ex > ey; --ex) {
        if (mx >= my) {
            mx -= my;
        }
        mx <<= 1;
    }
    if (mx >= my) {
        mx -= my;
    }
    if (mx == 0) {
        return from32(sign);
    }
    while (mx < 0x00800000u) {
        mx <<= 1;
        --ex;
    }
    if (ex > 0) {
        return from32(sign | ((mx & 0x007FFFFFu) | ((uint32_t)ex << 23)));
    }
    return from32(sign | (mx >> (1 - ex)));
}

long lround(double x) {
    return (long)__builtin_round(x);
}
