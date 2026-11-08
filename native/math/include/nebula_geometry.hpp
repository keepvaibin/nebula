// Vectors, quaternions, matrices, boxes and planes the game computes with. Every operation keeps its exact order and
// grouping: positions and physics depend on the bits (-ffp-contract=off holds fused multiply-add out).
#pragma once

#include <cfloat>
#include <cmath>
#include <cstring>
#include <math_types.hpp>
#include <nebula_math.h>

// Float helpers with the exact rounding the game's numbers depend on.
namespace FloatUtil {
    inline f32 invert(f32 value) {
        return 1.0f / value;
    }

    inline int epsilonEquals(f32 a1, f32 a2, f32 a3) {
        int ret = 0;

        a1 -= a2;

        if (-a3 <= a1 && a1 <= a3) {
            ret = 1;
        }

        return ret;
    }

    inline f32 sqrt(f32 val) {
        return NebulaSqrt(val);
    }

    inline f32 sqrtInline(f32 val) {
        return NebulaSqrt(val);
    }

    inline f32 asin(f32 val) {
        if (val >= 1.0f) {
            return 1.5707964f;
        }

        if (val <= -1.0f) {
            return -1.5707964f;
        }

        return NebulaAsin(val);
    }

    inline f32 acos(f32 val) {
        return NebulaAcos(val);
    }

    inline f32 PI() {
        return 3.1415927f;
    }

    inline f32 clamp(f32 val, f32 min, f32 max) {
        if (val < min) {
            return min;
        }

        if (val > max) {
            return max;
        }

        return val;
    }

    inline f32 inv_sqrt(f32 val) {
        if (val <= 0.0f) {
            return val;
        }
        return 1.0f / sqrtf(val);
    }

    inline f32 epsilon() {
        return 32.0f * FLT_EPSILON;
    }
}  // namespace FloatUtil

namespace Angle {
    constexpr f32 cRadian090 = 1.5707964f;
    constexpr f32 cRadian180 = 3.1415927f;
    constexpr f32 cRadian360 = 6.2831855f;
    constexpr f32 cRadianToDegree = 180.0f / cRadian180;

    // An angle as a short: a whole turn in 65536.
    inline void sinCosShort(s16 v, f32* pSin, f32* pCos) {
        NebulaSinCos(v * 0.000095873799f, pSin, pCos);
    }

    inline f32 cosShort(s16 v) {
        return NebulaCos(v * 0.000095873799f);
    }

    inline f32 sinShort(s16 v) {
        return NebulaSin(v * 0.000095873799f);
    }

    inline f32 acosRadian(f32 v) {
        return NebulaAcos(v);
    }

    inline f32 asinRadian(f32 v) {
        return NebulaAsin(v);
    }

    inline f32 cosDegree(f32 angle) {
        return NebulaCos(angle * 0.017453292f);
    }

    inline f32 cosRadian(f32 angle) {
        return NebulaCos(angle);
    }

    inline f32 cosLap(f32 angle) {
        return NebulaCos(angle * 6.2831855f);
    }

    inline f32 sinDegree(f32 angle) {
        return NebulaSin(angle * 0.017453292f);
    }

    inline f32 sinRadian(f32 angle) {
        return NebulaSin(angle);
    }

    inline f32 sinLap(f32 angle) {
        return NebulaSin(angle * 6.2831855f);
    }

    inline f32 atan2Radian(f32 x, f32 y) {
        return NebulaAtan2(x, y);
    }

    inline f32 acosDegree(f32 x) {
        return NebulaAcos(x) * cRadianToDegree;
    }

    // Through the C library's sin and cos.
    inline f32 libSin(f32 angle) {
        return sin(angle);
    }

    inline f32 libCos(f32 angle) {
        return cos(angle);
    }
}  // namespace Angle

inline void eulerToQuat(s16 x, s16 y, s16 z, Quaternion* quat) {
    f32 cosX, cosY, cosZ, sinX, sinY, sinZ;
    Angle::sinCosShort(x / 2, &sinX, &cosX);
    Angle::sinCosShort(y / 2, &sinY, &cosY);
    Angle::sinCosShort(z / 2, &sinZ, &cosZ);

    f32 cyz = cosY * cosZ;
    f32 syz = sinY * sinZ;
    quat->w = cosX * (cyz) + sinX * (syz);
    quat->x = sinX * (cyz)-cosX * (syz);
    quat->y = cosZ * (cosX * sinY) + sinZ * (sinX * cosY);
    quat->z = sinZ * (cosX * cosY) - cosZ * (sinX * sinY);
}

inline void eulerRadiansToQuat(f32 x, f32 y, f32 z, Quaternion* quat) {
    f32 sine[3], cosine[3];
    NebulaSinCos3(x * 0.5f, y * 0.5f, z * 0.5f, sine, cosine);
    const f32 sinX = sine[0], sinY = sine[1], sinZ = sine[2];
    const f32 cosX = cosine[0], cosY = cosine[1], cosZ = cosine[2];

    f32 cyz = cosY * cosZ;
    f32 syz = sinY * sinZ;
    quat->w = cosX * (cyz) + sinX * (syz);
    quat->x = sinX * (cyz)-cosX * (syz);
    quat->y = cosZ * (cosX * sinY) + sinZ * (sinX * cosY);
    quat->z = sinZ * (cosX * cosY) - cosZ * (sinX * sinY);
}

inline f32 hermiteInterpolation(f32 p1, f32 p2, f32 p3, f32 p4, f32 p5, f32 p6, f32 p7) {
    const f32 ff31 = p1 - p2;
    const f32 t = ff31 / (p5 - p2);
    const f32 squared = t * t;
    const f32 twice = t + t;
    const f32 ff27 = squared - t;
    const f32 ff30 = p3 - p6;
    f32 ff26 = twice * ff27 - squared;
    f32 ff25 = p4 * ff27 + p4;
    ff26 = ff26 * ff30 + p3;
    ff25 = p7 * ff27 + ff25;
    ff25 = t * p4 - ff25;
    return ff26 - ff31 * ff25;
}

inline f32 fastReciprocal(f32 value) {
    return 1.0f / value;
}

template < typename T >
inline T fastSqrt(T value) {
    return NebulaSqrt(value);
}

inline void copyFloats3(void* dest, const void* src) {
    memcpy(dest, src, 3 * sizeof(f32));
}

inline void copyFloats6(void* dest, const void* src) {
    memcpy(dest, src, 6 * sizeof(f32));
}

inline void copyFloats12(void* pDest, const void* pSrc) {
    memcpy(pDest, pSrc, 12 * sizeof(f32));
}

inline void copyFloats16(void* pDest, const void* pSrc) {
    memcpy(pDest, pSrc, 16 * sizeof(f32));
}

template < typename T >
inline T maxOf(T a, T b) {
    T ret;
    if (a > b) {
        ret = a;
    } else {
        ret = b;
    }
    return ret;
}

// The game's random numbers: the same sequence as long as the same calls happen.
class FastRandom {
public:
    FastRandom() {
    }

    constexpr FastRandom(u32 seed) : mSeed(seed) {
    }

    inline u32 rand() {
        return mSeed = 0x19660d * mSeed + 0x3c6ef35f;
    }

    inline float getRandF() {
        f32 f;
        const u32 s = (rand() >> 9) | 0x3f800000;
        memcpy(&f, &s, sizeof(f));
        return f - 1;
    }

    inline u32 getRand(u32 range) {
        const u32 value = rand() >> 9;
        return range != 0 ? value % range : value;
    }

    u32 mSeed;
};

inline void negateInternal(const f32* rSrc, f32* rDest) {
    rDest[0] = -rSrc[0];
    rDest[1] = -rSrc[1];
    rDest[2] = -rSrc[2];
}

// x and y are vec1 - vec2, z is vec2 - vec1: callers depend on this sign.
inline static void subInternal(const f32* vec1, const f32* vec2, f32* dst) {
    const f32 z = vec2[2] - vec1[2];
    dst[0] = vec1[0] - vec2[0];
    dst[1] = vec1[1] - vec2[1];
    dst[2] = z;
}

inline void mulInternal(const f32* vec1, const f32* vec2, f32* dst) {
    dst[0] = vec1[0] * vec2[0];
    dst[1] = vec1[1] * vec2[1];
    dst[2] = vec1[2] * vec2[2];
}

template < typename T >
struct Vec2 {
public:
    /* Constructors */
    inline Vec2() {
    }

    template < typename A >
    Vec2(A _x, A _y) {
        x = _x;
        y = _y;
    }

    void add(const Vec2< T >& other) {
        x += other.x;
        y += other.y;
    }

    /* General operations */
    template < typename A >
    void set(const Vec2< A >& rSrc) {
        x = rSrc.x;
        y = rSrc.y;
    }

    void set(T v) {
        y = x = v;
    }

    template < typename U >
    void set(const U x, const U y) {
        this->x = x;
        this->y = y;
    }

    void setMin(const Vec2< f32 >& min) {
        if (x >= min.x) {
            x = min.x;
        }

        if (y >= min.y) {
            y = min.y;
        }
    }

    void setMax(const Vec2< f32 >& max) {
        if (x <= max.x) {
            x = max.x;
        }

        if (y <= max.y) {
            y = max.y;
        }
    }

    inline bool isAbove(const Vec2< T >& other) const {
        return (x >= other.x) && (y >= other.y) ? true : false;
    }

    inline bool isZero() const {
        return dot(*this) <= FloatUtil::epsilon();
    }

    void sub(const Vec2< T >& rOther) {
        x = x - rOther.x;
        y = y - rOther.y;
    }

    void sub(const Vec2< T >& rA, const Vec2< T >& rB) {
        x = rA.x - rB.x;
        y = rA.y - rB.y;
    }

    inline Vec2 subInline(const Vec2< T >& other) const {
        Vec2 ret = *this;
        ret.sub(other);
        return ret;
    }

    T length() const {
        return FloatUtil::sqrt(squared());
    }

    T squared() const {
        return dot(*this);
    };

    T dot(const Vec2< T >& rOther) const {
        return x * rOther.x + y * rOther.y;
    }

    T distance(const Vec2< T >& rOther) const {
        return FloatUtil::sqrt(squareDist(rOther));
    }

    void zero() {
        x = y = 0.0f;
    }

    inline T squareDist(const Vec2< T >& rOther) const {
        return ((x - rOther.x) * (x - rOther.x)) + ((y - rOther.y) * (y - rOther.y));
    }

    void scale(f32 scalar) {
        x *= scalar;
        y *= scalar;
    }

    inline Vec2& scaleInline(f32 scalar) {
        Vec2 ret(*this);
        ret.scale(scalar);
        return ret;
    }

    /* Operators */
    Vec2< T > operator+(const Vec2< T >& rOther) const {
        Vec2 ret(*this);
        ret.x += rOther.x;
        ret.y += rOther.y;
        return ret;
    }

    Vec2< T > operator-(const Vec2< T >& rOther) const {
        Vec2 ret(*this);
        ret.x -= rOther.x;
        ret.y -= rOther.y;
        return ret;
    }

    Vec2< T > operator*(f32 scale) const {
        Vec2 ret(*this);
        ret.x *= scale;
        ret.y *= scale;
        return ret;
    }

    inline void operator+=(const Vec2< T >& rOther) {
        x = x + rOther.x;
        y = y + rOther.y;
    }

    inline void operator-=(const Vec2< T >& rOther) {
        x = x - rOther.x;
        y = y - rOther.y;
    }

    f32 setLength(f32 newlength) {
        f32 oldlength = squared();

        if (oldlength <= FloatUtil::epsilon()) {
            return 0.0f;
        }

        f32 lengthinv = FloatUtil::inv_sqrt(oldlength);
        scale(lengthinv * newlength);
        return lengthinv * oldlength;
    };

    f32 normalize() {
        f32 lengthSq = squared();

        if (lengthSq <= FloatUtil::epsilon()) {
            return 0.0f;
        }

        f32 invLength = FloatUtil::inv_sqrt(lengthSq);
        scale(invLength);
        return invLength * lengthSq;
    }

    f32 squared(const Vec2& rOther) const {
        return (x - rOther.x) * (x - rOther.x) + (y - rOther.y) * (y - rOther.y);
    }

    T x, y;
};

template < typename T >
struct Vec3IntArg {
    typedef int Type;
};

template <>
struct Vec3IntArg< int > {
    struct Type {};
};

template < typename T >
struct Vec3 {
    T x;
    T y;
    T z;

    Vec3() {
    }

    Vec3< T >(T _x, T _y, T _z) {
        x = _x;
        y = _y;
        z = _z;
    }

    // The game's member template Vec3<f32>::Vec3<int>(int, int, int): each component converted. Vec3<int>
    // has it as (T, T, T), where s32 is int.
    Vec3< T >(typename Vec3IntArg< T >::Type _x, typename Vec3IntArg< T >::Type _y, typename Vec3IntArg< T >::Type _z) {
        x = _x;
        y = _y;
        z = _z;
    }

    inline Vec3(T val) {
        x = val;
        y = val;
        z = val;
    }

    void set(T x_, T y_, T z_) {
        x = x_;
        y = y_;
        z = z_;
    }

    template < typename J >
    Vec3(J _x, J _y, J _z) {
        x = _x;
        y = _y;
        z = _z;
    }
};

template <>
struct Vec3< s16 > {
    s16 x, y, z;

    Vec3() {
    }

    template < typename J >
    Vec3(J x, J y, J z) {
        set(x, y, z);
    }

    Vec3& operator=(const Vec3& b) {
        x = b.x;
        y = b.y;
        z = b.z;
        return *this;
    }

    void set(s16 x_, s16 y_, s16 z_) {
        x = x_;
        y = y_;
        z = z_;
    }
};

template <>
struct Vec3< f32 > : public Vec {
    Vec3(const Vec& vec) {
        x = vec.x;
        y = vec.y;
        z = vec.z;
    }
    Vec3(const Vec3< f32 >& vec) {
        x = vec.x;
        y = vec.y;
        z = vec.z;
    }

    template < typename T >
    Vec3(T _x, T _y, T _z) {
        x = _x;
        y = _y;
        z = _z;
    }

    Vec3(f32 xz, f32 _y) {
        x = xz;
        y = _y;
        z = xz;
    }

    Vec3(f32 val) {
        set(val, val, val);
    }

    template < typename T >
    inline Vec3(const Vec2< T >& rVec) {
        x = rVec.x;
        y = rVec.y;
        z = 0;
    }

    inline Vec3() = default;

    operator Vec*() {
        return (Vec*)&x;
    }

    operator const Vec*() const {
        return (Vec*)&x;
    }

    operator const Vec2< f32 >&() const {
        return *reinterpret_cast< const Vec2< f32 >* >(this);
    }

    void operator=(const Vec3& vec) {
        x = vec.x;
        y = vec.y;
        z = vec.z;
    }

    template < typename T >
    void set(const Vec3< T >& rVec) {
        x = rVec.x;
        y = rVec.y;
        z = rVec.z;
    }

    template < typename T >
    void set(T _x, T _y, T _z) {
        x = _x;
        y = _y;
        z = _z;
    }

    void set(f32 x_, f32 y_, f32 z_) {
        x = x_;
        y = y_;
        z = z_;
    }

    void set(const Vec& rVec) {
        x = rVec.x;
        y = rVec.y;
        z = rVec.z;
    }

    inline void set(f32 val) {
        x = val;
        y = val;
        z = val;
    }

    template < typename T >
    void setAll(f32 value) {
        x = value;
        y = value;
        z = value;
    }

    void setTrans(MtxPtr mtx) {
        set< f32 >(mtx[0][3], mtx[1][3], mtx[2][3]);
    }

    Vec3 operator+(const Vec3& op) const {
        Vec3 ret(*this);
        ret += op;
        return ret;
    }

    void operator+=(const Vec3& op) {
        add(op);
    }

    void add(const Vec3< f32 >& b) {
        NebulaVecAdd(this, &b, this);
    }

    void add(const Vec3& a, const Vec3& b) {
        NebulaVecAdd(&a, &b, this);
    }

    Vec3 operator-(const Vec3& op) const {
        Vec3 ret(*this);
        ret -= op;
        return ret;
    }

    void operator-=(const Vec3& op) {
        sub(op);
    }

    void sub(const Vec3< f32 >& b) {
        NebulaVecSubtract(this, &b, this);
    }

    void sub(const Vec3& a, const Vec3& b) {
        NebulaVecSubtract(&a, &b, this);
    }

    Vec3 operator*(f32 scalar) const {
        Vec3 ret(*this);
        ret *= scalar;
        return ret;
    }

    Vec3 operator/(f32 div) const {
        Vec3 ret(*this);
        f32 reciprocal = 1.0f / div;
        ret *= reciprocal;
        return ret;
    }

    void operator*=(f32 scalar) {
        scale(scalar);
    }

    void operator/=(f32 scalar) {
        scale(1.0f / scalar);
    }

    Vec3 scaleInline(f32 scalar) const {
        const Vec3& rSelf = *this;
        Vec3 ret(rSelf);
        ret.scale(scalar);
        return ret;
    }

    void scale(f32 scalar) {
        x = x * scalar;
        y = y * scalar;
        z = z * scalar;
    }

    void scale(f32 scalar, const Vec3& rVec) {
        x = rVec.x * scalar;
        y = rVec.y * scalar;
        z = rVec.z * scalar;
    }

    inline Vec3 multInLine(f32 val) const {
        Vec3 ret(*this);
        ret.x *= val;
        ret.y *= val;
        ret.z *= val;
        return ret;
    }

    inline Vec3 multInLine2(f32 val) const {
        Vec3 ret(*this);
        ret.mult(val);
        return ret;
    }

    inline void mult(f32 val) {
        x *= val;
        y *= val;
        z *= val;
    }

    // Vec-Vec mult
    Vec3 operator*(const Vec3& rOther) const {
        Vec3 ret;
        mulInternal(&x, &rOther.x, &ret.x);
        return ret;
    }

    void operator*=(const Vec3& op) {
        mulInternal(&x, &op.x, &x);
    }

    void mul(const Vec3& a) {
        mulInternal(&x, &a.x, &x);
    }

    void mul(const Vec3& a, const Vec3& b) {
        mulInternal(&a.x, &b.x, &x);
    }

    inline void lerp(const Vec3& a, const Vec3& b, f32 t) {
        NebulaVecLerp(&a, &b, this, t);
    }

    bool operator==(const Vec3& rVec) const {
        return FloatUtil::epsilonEquals(x, rVec.x, FloatUtil::epsilon()) &&
               FloatUtil::epsilonEquals(y, rVec.y, FloatUtil::epsilon()) &&
               FloatUtil::epsilonEquals(z, rVec.z, FloatUtil::epsilon());
    }

    bool operator!=(const Vec3& rVec) const {
        return !(*this == rVec);
    }

    Vec3 operator-() const {
        Vec3 ret;
        ret.negate(*this);
        return ret;
    }

    void negate() {
        negateInternal(&x, &x);
    }

    void negate(const Vec3& rVec) {
        negateInternal(&rVec.x, &x);
    }

    void zero() {
        x = y = z = 0;
    }

    inline void setZeroVec() {
        x = gZeroVec.x;
        y = gZeroVec.y;
        z = gZeroVec.z;
    }
    f32 dot(const Vec3& rOther) const {
        return (x * rOther.x + y * rOther.y) + z * rOther.z;
    }

    bool epsilonEquals(const Vec3< f32 >& a1, f32 a2) const {
        bool ret = false;

        if (FloatUtil::epsilonEquals(x, a1.x, a2)) {
            if (FloatUtil::epsilonEquals(y, a1.y, a2)) {
                if (FloatUtil::epsilonEquals(z, a1.z, a2)) {
                    ret = true;
                }
            }
        }

        return ret;
    }

    void scaleAdd(f32 scale, const Vec3& src) {
        x += src.x * scale;
        y += src.y * scale;
        z += src.z * scale;
    }

    void scaleAdd(f32 sc, const Vec3& a, const Vec3& b) {
        NebulaVecScaleAdd(a, b, this, sc);
    }

    Vec3 killElement(const Vec3& rKillDir) const {
        Vec3 ret;
        ret.killElement(*this, rKillDir);
        return ret;
    }

    void killElement(const Vec3& rVec, const Vec3& rKillDir) {
        const Vec3& kill = rKillDir;
        NebulaVecScaleAdd(kill, rVec, this, -kill.dot(rVec));
    }

    void killElement2(const Vec3& rVec, const Vec3& rKillDir) {
        NebulaVecScaleAdd(rKillDir, rVec, this, -rKillDir.dot(rVec));
    }

    void orthogonalize(const Vec3& rKillDir) {
        const Vec3& kill = rKillDir;
        NebulaVecScaleAdd(kill, this, this, -kill.dot(*this));
    }

    void orthogonalize2(const Vec3& rKillDir) {
        NebulaVecScaleAdd(rKillDir, this, this, -rKillDir.dot(*this));
    }

    Vec3 getOrthogonal(const Vec3& rVec) const {
        Vec3 ret;
        ret.killElement(rVec, *this);
        return ret;
    }

    f32 normalize() {
        f32 magnitude = length();
        NebulaVecNormalize(this, this);
        return magnitude;
    }

    f32 normalize(const Vec3& rSrc) {
        set(rSrc);
        return normalize();
    }

    void cross(const Vec3< f32 >& a, const Vec3< f32 >& b) {
        NebulaVecCrossProduct(a, b, *this);
    }

    Vec3 cross(const Vec3& b) const {
        Vec3 ret;
        NebulaVecCrossProduct(this, &b, &ret);
        return ret;
    }

    f32 squared() const {
        return NebulaVecSquareMag(this);
    };

    operator f32*(void) {
        return reinterpret_cast< f32* >(this);
    }

    operator const f32*(void) const {
        return reinterpret_cast< const f32* >(this);
    }

    f32 squared(const Vec3& rB) const {
        const f32 dx = x - rB.x, dy = y - rB.y, dz = z - rB.z;
        return (dx * dx + dy * dy) + dz * dz;
    }

    bool isZero() const {
        return squared() <= FloatUtil::epsilon();
    }

    f32 setLength(f32 newlength) {
        f32 oldlength = squared();

        if (oldlength <= FloatUtil::epsilon()) {
            return 0.0f;
        }

        f32 lengthinv = FloatUtil::inv_sqrt(oldlength);
        scale(lengthinv * newlength);
        return lengthinv * oldlength;
    };

    f32 setLength(const Vec3& rVec, f32 newlength) {
        f32 oldlength = rVec.squared();

        if (oldlength <= FloatUtil::epsilon()) {
            zero();
            return 0.0f;
        }

        f32 lengthinv = FloatUtil::inv_sqrt(oldlength);
        scale(lengthinv * newlength, rVec);
        return lengthinv * oldlength;
    };

    f32 length() const {
        return NebulaVecMag(this);
    }

    inline f32 distance(const Vec3& rOther) const {
        return NebulaVecDistance(this, &rOther);
    }

    template < typename T >
    void cubic(const Vec3& rP0, const Vec3& rV0, const Vec3& rV1, const Vec3& rP1, T t) {
        // cubic hermite spline interpolation over a unit interval
        // p(t) = h00(t) * p0 + h01(t) * p1 + h10(t) * v0 + h11(t) * v1

        T h00, h01, h10, h11;

        T t2 = t * t;
        T t3 = t2 * t;
        h00 = 2 * t3 - 3 * t2 + 1;
        h01 = -2 * t3 + 3 * t2;
        h10 = t3 - 2 * t2 + t;
        h11 = t3 - t2;

        x = h00 * rP0.x + h01 * rP1.x + h10 * rV0.x + h11 * rV1.x;
        y = h00 * rP0.y + h01 * rP1.y + h10 * rV0.y + h11 * rV1.y;
        z = h00 * rP0.z + h01 * rP1.z + h10 * rV0.z + h11 * rV1.z;
    }

    f32 angle(const Vec3& rB) const {
        f32 crossPart = cross(rB).length();
        f32 dotPart = dot(rB);
        return fabsf(Angle::atan2Radian(crossPart, dotPart));
    }

    f32 turnRate(const Vec3& rB, f32 maxAngle) const {
        f32 a = angle(rB);
        f32 rate = 1.0f;
        if (a > maxAngle) {
            rate = maxAngle / a;
        }

        return rate;
    }

    inline Vec3 copy() const {
        Vec3 ret(*this);
        return ret;
    }

    inline bool orientation(const Vec3& rA, const Vec3& rB) const {
        return rA.cross(rB).dot(*this) < 0.0f;
    }
};

using Vec2s = Vec2< s16 >;
using Vec2f = Vec2< f32 >;
using Vec3f = Vec3< f32 >;
using Vec3Sc = Vec3< s8 >;
using Vec3s = Vec3< s16 >;

class Vec4f : public Quaternion {
public:
    /* Constructors */
    inline Vec4f() {
    }

    template < typename A >
    Vec4f(A _x, A _y, A _z, A _h) {
        x = _x;
        y = _y;
        z = _z;
        w = _h;
    }

    /* General operations */
    void set(const Vec4f& rVec) {
        x = rVec.x;
        y = rVec.y;
        z = rVec.z;
        w = rVec.w;
    }

    template < typename A >
    void set(A _x, A _y, A _z, A _w) {
        x = _x;
        y = _y;
        z = _z;
        w = _w;
    }

    void scale(f32 val) {
        x *= val;
        y *= val;
        z *= val;
        w *= val;
    }

    inline Vec3f* toVec3() {
        return reinterpret_cast< Vec3f* >(this);
    }

    operator Quaternion*() {
        return (Quaternion*)&x;
    }

    operator const Quaternion*() const {
        return (Quaternion*)&x;
    }
};

struct Quat4f : public Vec4f {
public:
    /* Constructors */
    inline Quat4f() {
    }

    inline Quat4f(f32 xyz, f32 _w) {
        x = xyz;
        y = xyz;
        z = xyz;
        w = _w;
    }

    template < typename A >
    Quat4f(A _x, A _y, A _z, A _w) {
        x = _x;
        y = _y;
        z = _z;
        w = _w;
    }

    Quat4f(const Quaternion& rOther) {
        x = rOther.x;
        y = rOther.y;
        z = rOther.z;
        w = rOther.w;
    }

    inline Vec3f* toTvec() {
        return reinterpret_cast< Vec3f* >(this);
    }

    /* General operations */
    void normalize() {
        f32 length = squared();

        if (length <= FloatUtil::epsilon()) {
            Vec4f::set< f32 >(0.0f, 0.0f, 0.0f, 1.0f);
            return;
        }

        f32 lengthinv = FloatUtil::inv_sqrt(length);
        Vec4f::scale(lengthinv);
    }

    void normalize(const Quat4f& rSrc) {
        f32 length = rSrc.squared();

        if (length <= FloatUtil::epsilon()) {
            set< f32 >(0.0f, 0.0f, 0.0f, 1.0f);
        } else {
            f32 inverse;

            if (length <= 0.0f) {
                inverse = length;
            } else {
                inverse = 1.0f / sqrtf(length);
            }

            x = rSrc.x * inverse;
            y = rSrc.y * inverse;
            z = rSrc.z * inverse;
            w = rSrc.w * inverse;
        }
    }

    void getXDir(Vec3f& rDest) const {
        rDest.set< f32 >(1.0f - y * y * 2.0f - z * z * 2.0f, x * y * 2.0f + w * z * 2.0f,
                                x * z * 2.0f - w * y * 2.0f);
    }

    void getYDir(Vec3f& rDest) const {
        rDest.set< f32 >(x * y * 2.0f - w * z * 2.0f, 1.0f - x * x * 2.0f - z * z * 2.0f,
                                y * z * 2.0f + w * x * 2.0f);
    }

    void getZDir(Vec3f& rDest) const {
        rDest.set< f32 >(x * z * 2.0f + w * y * 2.0f, y * z * 2.0f - w * x * 2.0f,
                                1.0f - x * x * 2.0f - y * y * 2.0f);
    }

    void getEuler(Vec3f& rDest) const {
        f32 m20 = 2.0f * (x * z - w * y);

        if (m20 - 1.0f >= -FloatUtil::epsilon()) {
            f32 numerator = 2.0f * (x * y - w * z);
            rDest.x = Angle::atan2Radian(-numerator, 1.0f - 2.0f * (x * x + z * z));
            rDest.y = -Angle::cRadian090;
            rDest.z = 0.0f;
        } else if (1.0f + m20 <= FloatUtil::epsilon()) {
            f32 numerator = 2.0f * (x * y - w * z);
            rDest.x = Angle::atan2Radian(numerator, 1.0f - 2.0f * (x * x + z * z));
            rDest.y = Angle::cRadian090;
            rDest.z = 0.0f;
        } else {
            f32 m10 = 2.0f * (x * y + w * z);
            f32 numerator = 2.0f * (y * z + w * x);
            f32 denominator = 1.0f - 2.0f * (x * x + y * y);
            f32 m00 = 1.0f - 2.0f * (y * y + z * z);
            rDest.x = Angle::atan2Radian(numerator, denominator);
            rDest.y = Angle::asinRadian(-m20);
            rDest.z = Angle::atan2Radian(m10, m00);
        }
    }

    void setEuler(f32 _x, f32 _y, f32 _z) {
        f32 cx = cos(0.5f * _x);
        f32 cy = cos(0.5f * _y);
        f32 cz = cos(0.5f * _z);
        f32 sx = sin(0.5f * _x);
        f32 sy = sin(0.5f * _y);
        f32 sz = sin(0.5f * _z);
        f32 cycz = cy * cz;
        f32 sycz = sy * cz;
        f32 cysz = cy * sz;
        f32 sysz = sy * sz;
        x = cycz * sx - sysz * cx;
        y = sycz * cx + cysz * sx;
        z = cysz * cx - sycz * sx;
        w = cycz * cx + sysz * sx;
    }

    void setEuler(const Vec3f& rpy) {
        setEuler(rpy.x, rpy.y, rpy.z);
    }

    void setEulerDegree(f32 _x, f32 _y, f32 _z) {
        setEuler(_x * PI_180, _y * PI_180, _z * PI_180);
    }

    void setEulerX(f32 _x) {
        f32 s = sin(_x * 0.5f);
        f32 c = cos(_x * 0.5f);
        x = s;
        y = 0.0f;
        z = 0.0f;
        w = c;
    }

    void setEulerY(f32 _y) {
        f32 s = sin(_y * 0.5f);
        f32 c = cos(_y * 0.5f);
        x = 0.0f;
        y = s;
        z = 0.0f;
        w = c;
    }

    void setEulerZ(f32 _z) {
        f32 s = sin(_z * 0.5f);
        f32 c = cos(_z * 0.5f);
        x = 0.0f;
        y = 0.0f;
        z = s;
        w = c;
    };

    f32 getRotate(Vec3f& rAxis) {
        f32 length = toTvec()->squared();
        f32 angle;

        if (length <= FloatUtil::epsilon()) {
            angle = 0.0f;
            rAxis.zero();
        } else {
            f32 lengthinv = FloatUtil::inv_sqrt(length);
            rAxis.scale(lengthinv, *toTvec());
            angle = FloatUtil::acos(w) * 2.0f;
        }

        return angle;
    }

    void setRotate(const Vec3f& rA, const Vec3f& rB, f32 ratio) {
        Vec3f dir = rA.cross(rB);
        f32 crossPart = dir.length();

        if (crossPart <= FloatUtil::epsilon()) {
            set< f32 >(0.0f, 0.0f, 0.0f, 1.0f);
        } else {
            f32 dotPart = rA.dot(rB);
            f32 halfAngle = ratio * (Angle::atan2Radian(crossPart, dotPart) * 0.5f);
            toTvec()->scale((f32)sin(halfAngle) / crossPart, dir);
            w = cos(halfAngle);
        }
    }

    void setRotate(const Vec3f& rA, const Vec3f& rB) {
        Vec3f dir = rA.cross(rB);
        f32 crossPart = dir.length();

        if (crossPart <= FloatUtil::epsilon()) {
            set< f32 >(0.0f, 0.0f, 0.0f, 1.0f);
        } else {
            f32 dotPart = NebulaVecDotProduct(&rA, &rB);
            f32 halfAngle = 0.5f * Angle::atan2Radian(crossPart, dotPart);
            f32 scale = static_cast< f32 >(sin(halfAngle)) / crossPart;
            x = dir.x * scale;
            y = dir.y * scale;
            z = dir.z * scale;
            w = cos(halfAngle);
        }
    }

    void setRotate(const Vec3f& pVec, f32 pAngle) {
        f32 halfAngle = pAngle * 0.5f;
        toTvec()->scale(sin(halfAngle), pVec);
        w = cos(halfAngle);
    }

    void setRotate(f32 x, f32 y, f32 z, f32 pAngle) {
        Vec3f pVec(x, y, z);
        setRotate(pVec, pAngle);
    }

    void rotate(Vec3f& v) const {
        transform(v, v);
    }

    void slerp(const Quat4f& a1, const Quat4f& a2, f32 a3) {
        x = a1.x;
        y = a1.y;
        z = a1.z;
        w = a1.w;
        slerp(a2, a3);
    }

    void slerp(const Quat4f& rOther, f32 ratio) {
        Quat4f a;
        Quat4f b;
        a.normalize(*this);
        b.normalize(rOther);
        f32 product = a.dot(b);
        bool reverse;

        if (product < 0.0f) {
            product = -product;
            reverse = true;
        } else {
            reverse = false;
        }

        f32 weight;

        if (1.0f - product <= FloatUtil::epsilon()) {
            weight = 1.0f - ratio;
        } else {
            f32 angle = Angle::acosRadian(product);
            f32 sinAngle = sin(angle);
            weight = (f32)sin((1.0f - ratio) * angle) / sinAngle;
            ratio = (f32)sin(ratio * angle) / sinAngle;
        }

        if (reverse) {
            ratio = -ratio;
        }

        set< f32 >(weight * a.x + ratio * b.x, weight * a.y + ratio * b.y, weight * a.z + ratio * b.z, weight * a.w + ratio * b.w);
    }

    void transform(const Vec3f& v, Vec3f& rDest) const {
        // transformation via hamiltonian multiplication of a unit quaternion
        // q*v*q`
        // where v is the input vector converted into a quaternion with w=0
        // and q` is the multiplicative inverse of q
        // (eg: q = w + xi + yj + zk, q` = w - xi - yj - zk)

        Quat4f r;
        r.x = (y * v.z) - (z * v.y) + (w * v.x);
        r.y = (-x * v.z) + (z * v.x) + (w * v.y);
        r.z = (x * v.y) - (y * v.x) + (w * v.z);
        r.w = (-x * v.x) - (y * v.y) - (z * v.z);

        rDest.set< f32 >(r.x * w + r.y * -z - r.z * -y + r.w * -x,
                                -r.x * -z + r.y * w + r.z * -x + r.w * -y,
                                r.x * -y - r.y * -x + r.z * w + r.w * -z);
    }

    void transform(Vec3f& v) const {
        transform(v, v);
    }

    void mult(const Quat4f& q) {
        NebulaQuatMultiply(&q, this, this);
    }

    void mult(const Quat4f& q1, const Quat4f& q2) {
        NebulaQuatMultiply(q1, q2, this);
    }

    f32 dot(const Quat4f& q) const {
        return NebulaQuatDotProduct(this, &q);
    }

    f32 squared() const {
        return NebulaQuatDotProduct(this, this);
    }

    void makeMtx(MtxPtr pMtx) const {
        f32 yy = 2.0f * y * y;
        f32 zz = 2.0f * z * z;
        f32 xx = 2.0f * x * x;

        f32 xy = 2.0f * x * y;
        f32 xz = 2.0f * x * z;
        f32 yz = 2.0f * y * z;

        f32 wx = 2.0f * w * x;
        f32 wy = 2.0f * w * y;
        f32 wz = 2.0f * w * z;

        pMtx[0][0] = 1.0f - yy - zz;
        pMtx[0][1] = xy - wz;
        pMtx[0][2] = xz + wy;

        pMtx[1][0] = xy + wz;
        pMtx[1][1] = 1.0f - xx - zz;
        pMtx[1][2] = yz - wx;

        pMtx[2][0] = xz - wy;
        pMtx[2][1] = yz + wx;
        pMtx[2][2] = 1.0f - xx - yy;
    }

    /* Operators */
    Quat4f& operator=(const Quat4f& rSrc) {
        x = rSrc.x;
        y = rSrc.y;
        z = rSrc.z;
        w = rSrc.w;
        return *this;
    }
};

template < typename T >
inline T geometryMax(T a, T b) {
    return a >= b ? a : b;
}

template < typename T >
struct Box2 {
    Box2() {
    }

    Box2(const Box2& rOther) {
        i.x = rOther.i.x;
        i.y = rOther.i.y;
        f.x = rOther.f.x;
        f.y = rOther.f.y;
    }

    Box2(const Vec2< T >& rMin, const Vec2< T >& rMax) {
        i.set(rMin);
        f.set(rMax);
    }

    Box2(T x0, T y0, T x1, T y1) {
        set(x0, y0, x1, y1);
    }

    void operator=(const Box2& rOther) {
        i = rOther.i;
        f = rOther.f;
    }

    f32 getWidth() const {
        return f.x - i.x;
    }

    f32 getHeight() const {
        return f.y - i.y;
    }

    bool isValid() const {
        return f.x >= i.x && f.y >= i.y;
    }

    void addPos(f32 x, f32 y) {
        addPos(Vec2< f32 >(x, y));
    }

    void addPos(const Vec2< f32 >& pos) {
        i.x += pos.x;
        i.y += pos.y;
        f.x += pos.x;
        f.y += pos.y;
    }

    bool intersect(const Box2& rOther) {
        i.setMax(rOther.i);
        f.setMin(rOther.f);
        return isValid();
    }

    void absolute() {
        if (!isValid()) {
            Box2 box = *this;
            i.setMin(box.i);
            i.setMin(box.f);
            f.setMax(box.i);
            f.setMax(box.f);
        }
    }

    void set(const Box2& rOther) {
        i.x = rOther.i.x;
        i.y = rOther.i.y;
        f.x = rOther.f.x;
        f.y = rOther.f.y;
    }

    void set(const Vec2< T >& rMin, const Vec2< T >& rMax) {
        i = rMin;
        f = rMax;
    }

    void set(T x0, T y0, T x1, T y1) {
        i.set(x0, y0);
        f.set(x1, y1);
    }

    void setInline(T x0, T y0, T x1, T y1) {
        i.set(x0, y0);
        f.set(x1, y1);
    }

    inline bool intersectsPoint(const Vec2< T >& rPos) const {
        return (rPos.x >= i.x && rPos.y >= i.y && rPos.x < f.x && rPos.y < f.y);
    }

    Vec2< T > i, f;
};

using Box2f = Box2< f32 >;
using Box2s = Box2< s16 >;

struct Box3f {
    Vec3f i, f;

    void add(const Vec3f& rOffset) {
        i.add(rOffset);
        f.add(rOffset);
    }

    void extend(const Vec3f& rPos) {
        if (i.x >= rPos.x) {
            i.x = rPos.x;
        }

        if (i.y >= rPos.y) {
            i.y = rPos.y;
        }

        if (i.z >= rPos.z) {
            i.z = rPos.z;
        }

        if (f.x <= rPos.x) {
            f.x = rPos.x;
        }

        if (f.y <= rPos.y) {
            f.y = rPos.y;
        }

        if (f.z <= rPos.z) {
            f.z = rPos.z;
        }
    }

    void extend(const Vec3f& rPos1, const Vec3f& rPos2) {
        if (i.x >= rPos1.x) {
            i.x = rPos1.x;
        }

        if (i.y >= rPos1.y) {
            i.y = rPos1.y;
        }

        if (i.z >= rPos1.z) {
            i.z = rPos1.z;
        }

        if (f.x <= rPos2.x) {
            f.x = rPos2.x;
        }

        if (f.y <= rPos2.y) {
            f.y = rPos2.y;
        }

        if (f.z <= rPos2.z) {
            f.z = rPos2.z;
        }
    }

    bool intersectsPoint(const Vec3f& rPos) const {
        return (rPos.x >= i.x && rPos.y >= i.y && rPos.z >= i.z && rPos.x < f.x && rPos.y < f.y &&
                rPos.z < f.z);
    }

    void set(const Vec3f& rMin, const Vec3f& rMax) {
        i.set(rMin);
        f.set(rMax);
    }

    void zero() {
        i.zero();
        f.zero();
    }

    void pad(f32 padding) {
        Vec3f p(padding);
        i.sub(p);
        f.add(p);
    }

    void getCenter(Vec3f* pCenter) {
        pCenter->lerp(f, i, 0.5f);
    }
};

struct DirBox3f {
    Vec3f mAxisX;
    Vec3f mAxisY;
    Vec3f mAxisZ;
    Vec3f mCorner;
    Vec3f mSize;
};

struct Plane3f {
    void set(const Vec3f& rFirst, const Vec3f& rSecond, const Vec3f& rThird) {
        Vec3f secondEdge;
        Vec3f firstEdge;
        firstEdge.sub(rSecond, rFirst);
        secondEdge.sub(rThird, rSecond);
        mNormal.cross(secondEdge, firstEdge);
        mNormal.normalize();
        mDot = mNormal.dot(rFirst);
    }

    void set(const Vec3f& rNormal, const Vec3f& rPoint) {
        mNormal.set(rNormal);
        mDot = mNormal.dot(rPoint);
    }

    Vec3f mNormal;
    f32 mDot;
};

struct Hex3f {
    bool mayIntersectBall3(const Vec3f& rVec, f32 radius) const {
        f32 neg = -radius;
        const Plane3f* end = mPlanes + 6;
        const Plane3f* cur = mPlanes;

        while (cur < end) {
            if (cur->mNormal.dot(rVec) - cur->mDot < neg) {
                return false;
            }

            cur++;
        }

        return true;
    }

    Plane3f mPlanes[6];
};

// Three rows of four: rotation and scale in the first three columns, translation in the fourth.
struct Mtx34f {
    typedef f32 ArrType[4];
    void set(const ArrType* pSrc) {
        __builtin_memcpy(this, pSrc, sizeof(Mtx));
    }

    void set(const Mtx34f& rSrc) {
        __builtin_memcpy(this, rSrc, sizeof(Mtx));
    }

    void set(f32 xx, f32 xy, f32 xz, f32 tx, f32 yx, f32 yy, f32 yz, f32 ty, f32 zx, f32 zy, f32 zz, f32 tz) {
        mMtx[0][0] = xx;
        mMtx[0][1] = xy;
        mMtx[0][2] = xz;
        mMtx[0][3] = tx;
        mMtx[1][0] = yx;
        mMtx[1][1] = yy;
        mMtx[1][2] = yz;
        mMtx[1][3] = ty;
        mMtx[2][0] = zx;
        mMtx[2][1] = zy;
        mMtx[2][2] = zz;
        mMtx[2][3] = tz;
    }

    void scaleXYZ(f32 scale) {
        mMtx[0][0] *= scale;
        mMtx[0][1] *= scale;
        mMtx[0][2] *= scale;
        mMtx[1][0] *= scale;
        mMtx[1][1] *= scale;
        mMtx[1][2] *= scale;
        mMtx[2][0] *= scale;
        mMtx[2][1] *= scale;
        mMtx[2][2] *= scale;
    }

    inline void setInline(const Mtx34f& rSrc) {
        __builtin_memcpy(this, rSrc, sizeof(Mtx));
    }

    inline void setInline(const Mtx34f* rSrc) {
        __builtin_memcpy(this, rSrc, sizeof(Mtx));
    }

    inline void setInline(const MtxPtr rSrc) {
        __builtin_memcpy(this, rSrc, sizeof(Mtx));
    }

    inline void scaleInline(f32 scalar) {
        mMtx[0][0] *= scalar;
        mMtx[0][1] *= scalar;
        mMtx[0][2] *= scalar;
        mMtx[1][0] *= scalar;
        mMtx[1][1] *= scalar;
        mMtx[1][2] *= scalar;
        mMtx[2][0] *= scalar;
        mMtx[2][1] *= scalar;
        mMtx[2][2] *= scalar;
    }

    inline Mtx* toMtx() {
        return (Mtx*)mMtx;
    }

    inline const Mtx* toCMtx() const {
        return (const Mtx*)mMtx;
    }

    inline MtxPtr toMtxPtr() {
        return (MtxPtr)mMtx;
    }

    operator ArrType*() {
        return mMtx;
    }

    operator const ArrType*() const {
        return mMtx;
    }

    f32 get(int x, int y) const {
        return mMtx[x][y];
    }

    f32 operator()(int x, int y) const {
        return get(x, y);
    }

    inline f32 dot() const {
        return ((mMtx[1][0] * mMtx[1][0]) + (mMtx[0][0] * mMtx[0][0]) + (mMtx[2][0] * mMtx[2][0]) +
                (mMtx[0][1] * mMtx[0][1]) + (mMtx[1][1] * mMtx[1][1]) + (mMtx[2][1] * mMtx[2][1]) +
                (mMtx[0][2] * mMtx[0][2]) + (mMtx[1][2] * mMtx[1][2]) + (mMtx[2][2] * mMtx[2][2]));
    }

        inline f32 dotX() const {
        return (mMtx[1][0] * mMtx[1][0]) + (mMtx[0][0] * mMtx[0][0]) + (mMtx[2][0] * mMtx[2][0]);
    }

    inline f32 dotY() const {
        return (mMtx[1][1] * mMtx[1][1]) + (mMtx[0][1] * mMtx[0][1]) + (mMtx[2][1] * mMtx[2][1]);
    }

    inline f32 dotZ() const {
        return (mMtx[1][2] * mMtx[1][2]) + (mMtx[0][2] * mMtx[0][2]) + (mMtx[2][2] * mMtx[2][2]);
    }

    inline f32 dot(int x, int y) const {
        return (mMtx[x][y] * mMtx[x][y]);
    }

    void identity() {
        mMtx[1][0] = mMtx[2][0] = mMtx[0][1] = mMtx[2][1] = mMtx[0][2] = mMtx[1][2] = mMtx[0][3] =
            mMtx[1][3] = mMtx[2][3] = 0.0f;
        mMtx[0][0] = mMtx[1][1] = mMtx[2][2] = 1.0f;
    }

    void concat(const Mtx34f& rSrcA, const Mtx34f& rSrcB) {
        NebulaMtxConcat(rSrcA, rSrcB, mMtx);
    }

    void concat(const Mtx34f& rSrc) {
        NebulaMtxConcat(mMtx, rSrc, mMtx);
    }

    f32 determinant33() const {
        return (*this)(0, 0) * ((*this)(1, 1) * (*this)(2, 2) - (*this)(1, 2) * (*this)(2, 1)) -
               (*this)(0, 1) * ((*this)(1, 0) * (*this)(2, 2) - (*this)(1, 2) * (*this)(2, 0)) +
               (*this)(0, 2) * ((*this)(1, 0) * (*this)(2, 1) - (*this)(1, 1) * (*this)(2, 0));
    }

    bool invert(const Mtx34f& rSrc) {
        f32 determinant = rSrc.determinant33();

        if (determinant <= FloatUtil::epsilon() && determinant >= -FloatUtil::epsilon()) {
            return false;
        }

        NebulaMtxInverse(rSrc, mMtx);
        return true;
    }

    void mult(const Vec3f& rSrc, Vec3f& rDest) const {
        rDest.set< f32 >((*this)(0, 3) + (rSrc.z * (*this)(0, 2) + (rSrc.x * (*this)(0, 0) + rSrc.y * (*this)(0, 1))),
                         (*this)(1, 3) + (rSrc.z * (*this)(1, 2) + (rSrc.x * (*this)(1, 0) + rSrc.y * (*this)(1, 1))),
                         (*this)(2, 3) + (rSrc.z * (*this)(2, 2) + (rSrc.x * (*this)(2, 0) + rSrc.y * (*this)(2, 1))));
    }

    void multTranspose(const Vec3f& rSrc, Vec3f& rDest) const {
        Vec3f translated;
        translated.x = rSrc.x - (*this)(0, 3);
        translated.y = rSrc.y - (*this)(1, 3);
        translated.z = rSrc.z - (*this)(2, 3);

        rDest.set< f32 >(translated.z * (*this)(2, 0) + (translated.x * (*this)(0, 0) + translated.y * (*this)(1, 0)),
                         translated.z * (*this)(2, 1) + (translated.x * (*this)(0, 1) + translated.y * (*this)(1, 1)),
                         translated.z * (*this)(2, 2) + (translated.x * (*this)(0, 2) + translated.y * (*this)(1, 2)));
    }

    void scale(f32 scalar) {
        mMtx[0][0] *= scalar;
        mMtx[1][0] *= scalar;
        mMtx[2][0] *= scalar;
        mMtx[0][1] *= scalar;
        mMtx[1][1] *= scalar;
        mMtx[2][1] *= scalar;
        mMtx[0][2] *= scalar;
        mMtx[1][2] *= scalar;
        mMtx[2][2] *= scalar;
        mMtx[0][3] *= scalar;
        mMtx[1][3] *= scalar;
        mMtx[2][3] *= scalar;
    }

    f32 mMtx[3][4];
};

struct Rot3f : public Mtx34f {
    void identity33() {
        mMtx[0][0] = 1.0f;
        mMtx[1][0] = 0.0f;
        mMtx[2][0] = 0.0f;
        mMtx[0][1] = 0.0f;
        mMtx[1][1] = 1.0f;
        mMtx[2][1] = 0.0f;
        mMtx[0][2] = 0.0f;
        mMtx[1][2] = 0.0f;
        mMtx[2][2] = 1.0f;
    };

    void getXDir(Vec3f& rDest) const {
        f32 z = mMtx[2][0];
        f32 y = mMtx[1][0];
        f32 x = mMtx[0][0];
        rDest.set< f32 >(x, y, z);
    };

    void getYDir(Vec3f& rDest) const {
        f32 z = mMtx[2][1];
        f32 y = mMtx[1][1];
        f32 x = mMtx[0][1];
        rDest.set< f32 >(x, y, z);
    };

    void getZDir(Vec3f& rDest) const {
        f32 z = mMtx[2][2];
        f32 y = mMtx[1][2];
        f32 x = mMtx[0][2];
        rDest.set< f32 >(x, y, z);
    };

    void getXYZDir(Vec3f& rDestX, Vec3f& rDestY, Vec3f& rDestZ) const {
        f32 xz = mMtx[2][0];
        f32 xy = mMtx[1][0];
        f32 xx = mMtx[0][0];
        rDestX.set< f32 >(xx, xy, xz);
        f32 yz = mMtx[2][1];
        f32 yy = mMtx[1][1];
        f32 yx = mMtx[0][1];
        rDestY.set< f32 >(yx, yy, yz);
        f32 zz = mMtx[2][2];
        f32 zy = mMtx[1][2];
        f32 zx = mMtx[0][2];
        rDestZ.set< f32 >(zx, zy, zz);
    }

    void setXDir(const Vec3f& rSrc) {
        mMtx[0][0] = rSrc.x;
        mMtx[1][0] = rSrc.y;
        mMtx[2][0] = rSrc.z;
    }

    void setXDir(f32 x, f32 y, f32 z) {
        mMtx[0][0] = x;
        mMtx[1][0] = y;
        mMtx[2][0] = z;
    }

    void setYDir(const Vec3f& rSrc) {
        mMtx[0][1] = rSrc.x;
        mMtx[1][1] = rSrc.y;
        mMtx[2][1] = rSrc.z;
    }

    void setYDir(f32 x, f32 y, f32 z) {
        mMtx[0][1] = x;
        mMtx[1][1] = y;
        mMtx[2][1] = z;
    }

    void setZDir(const Vec3f& rSrc) {
        mMtx[0][2] = rSrc.x;
        mMtx[1][2] = rSrc.y;
        mMtx[2][2] = rSrc.z;
    }

    void setZDir(f32 x, f32 y, f32 z) {
        mMtx[0][2] = x;
        mMtx[1][2] = y;
        mMtx[2][2] = z;
    }

    void setXYZDir(const Vec3f& rSrcX, const Vec3f& rSrcY, const Vec3f& rSrcZ) {
        mMtx[0][0] = rSrcX.x;
        mMtx[1][0] = rSrcX.y;
        mMtx[2][0] = rSrcX.z;
        mMtx[0][1] = rSrcY.x;
        mMtx[1][1] = rSrcY.y;
        mMtx[2][1] = rSrcY.z;
        mMtx[0][2] = rSrcZ.x;
        mMtx[1][2] = rSrcZ.y;
        mMtx[2][2] = rSrcZ.z;
    }

    inline void getYDir2(Vec3f& rDest) const {
        f32 x = mMtx[0][1];
        f32 y = mMtx[1][1];
        f32 z = mMtx[2][1];
        rDest.set< f32 >(x, y, z);
    }

    inline void getZDir2(Vec3f& rDest) const {
        rDest.set< f32 >(mMtx[0][2], mMtx[1][2], mMtx[2][2]);
    }

    inline void setXYZDir2(const Vec3f& rSrcX, const Vec3f& rSrcY, const Vec3f& rSrcZ) {
        mMtx[0][0] = rSrcX.x;
        mMtx[0][1] = rSrcX.y;
        mMtx[0][2] = rSrcX.z;
        mMtx[1][0] = rSrcY.x;
        mMtx[1][1] = rSrcY.y;
        mMtx[1][2] = rSrcY.z;
        mMtx[2][0] = rSrcZ.x;
        mMtx[2][1] = rSrcZ.y;
        mMtx[2][2] = rSrcZ.z;
    }

    void getEulerDegree(Vec3f& rDest) const {
        Vec3f rot;
        getEulerXYZ(rot);
        rDest.set< f32 >(rot.x * (180.0f / PI), rot.y * (180.0f / PI), rot.z * (180.0f / PI));
    }

    void getEuler(Vec3f& rDest) const {
        getEulerXYZ(rDest);
    }

    void getEulerXYZ(Vec3f& rDest) const {
        const f32 zx = mMtx[2][0];
        if (zx - 1.0f >= -(f32)FloatUtil::epsilon()) {
            rDest.set(Angle::atan2Radian(-mMtx[0][1], mMtx[1][1]), -HALF_PI, 0.0f);
            return;
        }

        f32 f1 = 1.0f;
        if (zx + f1 <= (f32)FloatUtil::epsilon()) {
            rDest.set(Angle::atan2Radian(mMtx[0][1], mMtx[1][1]), HALF_PI, 0.0f);
            return;
        }

        rDest.x = Angle::atan2Radian(mMtx[2][1], mMtx[2][2]);
        rDest.z = Angle::atan2Radian(mMtx[1][0], mMtx[0][0]);
        rDest.y = FloatUtil::asin(-mMtx[2][0]);
    }

    void setEulerX(f32 angle) {
        f32 s = Angle::libSin(angle);
        f32 c = Angle::libCos(angle);

        mMtx[0][0] = 1.0f;
        mMtx[2][1] = s;
        mMtx[1][1] = c;
        mMtx[1][2] = -s;
        mMtx[2][2] = c;
        mMtx[2][0] = 0.0f;
        mMtx[0][2] = 0.0f;
        mMtx[1][0] = 0.0f;
        mMtx[0][1] = 0.0f;
    }

    void setEulerY(f32 angle) {
        f32 s = Angle::libSin(angle);
        f32 c = Angle::libCos(angle);

        mMtx[0][2] = s;
        mMtx[1][1] = 1.0f;
        mMtx[0][0] = c;
        mMtx[2][0] = -s;
        mMtx[2][2] = c;
        mMtx[2][1] = 0.0f;
        mMtx[1][2] = 0.0f;
        mMtx[1][0] = 0.0f;
        mMtx[0][1] = 0.0f;
    }

    void setEulerZ(f32 angle) {
        f32 s = Angle::libSin(angle);
        f32 c = Angle::libCos(angle);

        mMtx[1][0] = s;
        mMtx[0][0] = c;
        mMtx[0][1] = -s;
        mMtx[1][1] = c;
        mMtx[2][2] = 1.0f;
        mMtx[2][1] = 0.0f;
        mMtx[1][2] = 0.0f;
        mMtx[2][0] = 0.0f;
        mMtx[0][2] = 0.0f;
    }

    void getQuat(Quat4f& rDest) const {
        if ((*this)(0, 0) + (*this)(1, 1) + (*this)(2, 2) >= 0.0f) {
            f32 root = (*this)(0, 0) + (*this)(1, 1) + (*this)(2, 2);
            root += 1.0f;
            root = FloatUtil::sqrt(root);
            f32 scale = 0.5f / root;
            rDest.w = 0.5f * root;
            rDest.x = scale * ((*this)(2, 1) - (*this)(1, 2));
            rDest.y = scale * ((*this)(0, 2) - (*this)(2, 0));
            rDest.z = scale * ((*this)(1, 0) - (*this)(0, 1));
        } else {
            f32 maximum = geometryMax(((*this)(0, 0) >= (*this)(1, 1) ? (*this)(0, 0) : (*this)(1, 1)), (*this)(2, 2));

            if (maximum == (*this)(0, 0)) {
                f32 root = FloatUtil::sqrt(1.0f + ((*this)(0, 0) - ((*this)(1, 1) + (*this)(2, 2))));
                f32 scale = 0.5f / root;
                rDest.x = 0.5f * root;
                rDest.y = scale * ((*this)(0, 1) + (*this)(1, 0));
                rDest.z = scale * ((*this)(2, 0) + (*this)(0, 2));
                rDest.w = scale * ((*this)(2, 1) - (*this)(1, 2));
            } else if (maximum == (*this)(1, 1)) {
                f32 root = FloatUtil::sqrt(1.0f + ((*this)(1, 1) - ((*this)(2, 2) + (*this)(0, 0))));
                f32 scale = 0.5f / root;
                rDest.y = 0.5f * root;
                rDest.z = scale * ((*this)(1, 2) + (*this)(2, 1));
                rDest.x = scale * ((*this)(0, 1) + (*this)(1, 0));
                rDest.w = scale * ((*this)(0, 2) - (*this)(2, 0));
            } else {
                f32 root = FloatUtil::sqrt(1.0f + ((*this)(2, 2) - ((*this)(0, 0) + (*this)(1, 1))));
                f32 scale = 0.5f / root;
                rDest.z = 0.5f * root;
                rDest.x = scale * ((*this)(2, 0) + (*this)(0, 2));
                rDest.y = scale * ((*this)(1, 2) + (*this)(2, 1));
                rDest.w = scale * ((*this)(1, 0) - (*this)(0, 1));
            }
        }
    }

    void setQuat(const Quat4f& q) {
        f32 yy = 2.0f * q.y * q.y;
        f32 zz = 2.0f * q.z * q.z;
        f32 xx = 2.0f * q.x * q.x;

        f32 xy = 2.0f * q.x * q.y;
        f32 xz = 2.0f * q.x * q.z;
        f32 yz = 2.0f * q.y * q.z;

        f32 wz = 2.0f * q.w * q.z;
        f32 wx = 2.0f * q.w * q.x;
        f32 wy = 2.0f * q.w * q.y;

        mMtx[0][0] = 1.0f - yy - zz;
        mMtx[0][1] = xy - wz;
        mMtx[0][2] = xz + wy;

        mMtx[1][0] = xy + wz;
        mMtx[1][1] = 1.0f - xx - zz;
        mMtx[1][2] = yz - wx;

        mMtx[2][0] = xz - wy;
        mMtx[2][1] = yz + wx;
        mMtx[2][2] = 1.0f - xx - yy;
    }

    f32 getRotate(Vec3f& rAxis) const {
        Quat4f rot;
        getQuat(rot);
        return (f64)rot.getRotate(rAxis);
    }

    void getScale(Vec3f& rDest) const {
        {
            f32 y = mMtx[1][0];
            f32 x = mMtx[0][0];
            f32 z = mMtx[2][0];
            rDest.x = FloatUtil::sqrt(x * x + y * y + z * z);
        }

        {
            f32 y = mMtx[1][1];
            f32 x = mMtx[0][1];
            f32 z = mMtx[2][1];
            rDest.y = FloatUtil::sqrt(x * x + y * y + z * z);
        }

        {
            f32 y = mMtx[1][2];
            f32 x = mMtx[0][2];
            f32 z = mMtx[2][2];
            rDest.z = FloatUtil::sqrt(x * x + y * y + z * z);
        }
    }

    void setScale(f32 x, f32 y, f32 z) {
        mMtx[0][0] = x;
        mMtx[0][1] = 0.0f;
        mMtx[0][2] = 0.0f;
        mMtx[1][0] = 0.0f;
        mMtx[1][1] = y;
        mMtx[1][2] = 0.0f;
        mMtx[2][0] = 0.0f;
        mMtx[2][1] = 0.0f;
        mMtx[2][2] = z;
    }

    void setRotate(const Vec3f& rAxis, f32 angle) {
        Vec3f vec;
        vec.normalize(rAxis);

        f32 s = Angle::libSin(angle);
        f32 c = Angle::libCos(angle);

        f32 negc = 1.0f - c;

        f32 x, y, z;

        x = vec.x;
        y = vec.y;
        z = vec.z;

        f32 xx, yy, zz;
        xx = x * x;
        yy = y * y;
        zz = z * z;

        mMtx[0][0] = c + negc * xx;
        mMtx[0][1] = negc * x * y - s * z;
        mMtx[0][2] = negc * x * z + s * y;
        mMtx[1][0] = negc * x * y + s * z;
        mMtx[1][1] = c + negc * yy;
        mMtx[1][2] = negc * y * z - s * x;
        mMtx[2][0] = negc * x * z - s * y;
        mMtx[2][1] = negc * y * z + s * x;
        mMtx[2][2] = c + negc * zz;
    }

    void setRotateDegree(const Vec3f& rAxis, f32 angle) {
        Vec3f vec;
        vec.normalize(rAxis);

        f32 s = Angle::libSin(angle * (PI / 180.0f));
        f32 c = Angle::libCos(angle * (PI / 180.0f));

        f32 negc = 1.0f - c;

        f32 x, y, z;

        x = vec.x;
        y = vec.y;
        z = vec.z;

        f32 xx, yy, zz;
        xx = x * x;
        yy = y * y;
        zz = z * z;

        mMtx[0][0] = c + negc * xx;
        mMtx[0][1] = negc * x * y - s * z;
        mMtx[0][2] = negc * x * z + s * y;
        mMtx[1][0] = negc * x * y + s * z;
        mMtx[1][1] = c + negc * yy;
        mMtx[1][2] = negc * y * z - s * x;
        mMtx[2][0] = negc * x * z - s * y;
        mMtx[2][1] = negc * y * z + s * x;
        mMtx[2][2] = c + negc * zz;
    }

    void setRotate(const Vec3f& rFrom, const Vec3f& rTo) {
        Quat4f q;
        q.setRotate(rFrom, rTo);
        setQuat(q);
    };

    void setRotateDegree(const Vec3f& rRot) {
        setRotate(rRot * (PI / 180.0f));
    }

    void setRotate(const Vec3f& rRot) {
        f32 z = rRot.z;
        f32 y = rRot.y;
        f32 x = rRot.x;
        setRotate(x, y, z);
    }

    void setEuler(const Vec3f& rRot) {
        setRotate(rRot);
    }

    void setRotate(f32 rx, f32 ry, f32 rz) {
        f32 sinX, sinY, sinZ;
        f32 cosX, cosY, cosZ;

        cosZ = Angle::libCos(rz);
        cosY = Angle::libCos(ry);
        cosX = Angle::libCos(rx);
        sinZ = Angle::libSin(rz);
        sinY = Angle::libSin(ry);
        sinX = Angle::libSin(rx);

        f32 sXsY = sinX * sinY;
        f32 cXcZ = cosX * cosZ;
        f32 cXsZ = cosX * sinZ;

        mMtx[0][0] = cosY * cosZ;
        mMtx[1][0] = cosY * sinZ;
        mMtx[2][0] = -sinY;

        mMtx[0][1] = sXsY * cosZ - cosX * sinZ;
        mMtx[1][1] = cosX * cosZ + sXsY * sinZ;
        mMtx[2][1] = sinX * cosY;

        mMtx[0][2] = cXcZ * sinY + sinX * sinZ;
        mMtx[1][2] = cXsZ * sinY - sinX * cosZ;
        mMtx[2][2] = cosX * cosY;
    }

    void mult33(Vec3f& rVec) const {
        mult33(rVec, rVec);
    }

    void mult33(const Vec3f& rSrc, Vec3f& rDst) const {
        rDst.set< f32 >(rSrc.x * get(0, 0) + rSrc.y * get(0, 1) + rSrc.z * get(0, 2),
                        rSrc.x * get(1, 0) + rSrc.y * get(1, 1) + rSrc.z * get(1, 2),
                        rSrc.x * get(2, 0) + rSrc.y * get(2, 1) + rSrc.z * get(2, 2));
    }
};

struct Pos3f : public Rot3f {
    Pos3f() {};

    Pos3f(MtxPtr rSrc) {
        __builtin_memcpy(this, rSrc, sizeof(Mtx));
    }

    Pos3f(const Pos3f* rSrc) {
        __builtin_memcpy(this, rSrc, sizeof(Mtx));
    }

    void getTrans(Vec3f& rDest) const {
        f32 z = mMtx[2][3];
        f32 y = mMtx[1][3];
        f32 x = mMtx[0][3];
        rDest.set< f32 >(x, y, z);
    }

    inline void getTransInline2(Vec3f& rDest) const {
        f32 x = mMtx[0][3];
        f32 y = mMtx[1][3];
        f32 z = mMtx[2][3];
        rDest.set< f32 >(x, y, z);
    }

    void setTrans(const Vec3f& rSrc) {
        mMtx[0][3] = rSrc.x;
        mMtx[1][3] = rSrc.y;
        mMtx[2][3] = rSrc.z;
    }

    void setTrans(f32 x, f32 y, f32 z) {
        mMtx[0][3] = x;
        mMtx[1][3] = y;
        mMtx[2][3] = z;
    }

    void zeroTrans() {
        mMtx[0][3] = 0.0f;
        mMtx[1][3] = 0.0f;
        mMtx[2][3] = 0.0f;
    }

    inline void zeroTransInline2() {
        mMtx[0][3] = 0.0f;
        mMtx[1][3] = 0.0f;
        mMtx[2][3] = 0.0f;
    }

    inline void makeTrans(const Vec3f& rTrans) {
        identity();
        setTrans(rTrans);
    }

    inline void makeTrans(f32 x, f32 y, f32 z) {
        mMtx[0][0] = 1.0f;
        mMtx[1][0] = 0.0f;
        mMtx[2][0] = 0.0f;

        mMtx[0][1] = 0.0f;
        mMtx[1][1] = 1.0f;
        mMtx[2][1] = 0.0f;

        mMtx[0][2] = 0.0f;
        mMtx[1][2] = 0.0f;
        mMtx[2][2] = 1.0f;

        mMtx[0][3] = x;
        mMtx[1][3] = y;
        mMtx[2][3] = z;
    }

    void setTR(const Vec3f& a1, const Vec3f& a2, const Vec3f& a3, const Vec3f& a4) {
        mMtx[0][0] = a1.x;
        mMtx[1][0] = a1.y;
        mMtx[2][0] = a1.z;

        mMtx[0][1] = a2.x;
        mMtx[1][1] = a2.y;
        mMtx[2][1] = a2.z;

        mMtx[0][2] = a3.x;
        mMtx[1][2] = a3.y;
        mMtx[2][2] = a3.z;

        mMtx[0][3] = a4.x;
        mMtx[1][3] = a4.y;
        mMtx[2][3] = a4.z;
    }

    void makeRotate(const Vec3f& rVec, f32 angle) {
        zeroTrans();
        Rot3f::setRotate(rVec, angle);
    }

    void makeRotateDegree(const Vec3f& rVec, f32 angle) {
        zeroTrans();
        Rot3f::setRotateDegree(rVec, angle);
    }

    void makeRotate(const Vec3f& rFrom, const Vec3f& rTo, f32 angle) {
        Quat4f q;
        q.setRotate(rFrom, rTo, rFrom.turnRate(rTo, angle));
        makeQuat(q);
    }

    void makeRotateRate(const Vec3f& rFrom, const Vec3f& rTo, f32 ratio) {
        Quat4f q;
        q.setRotate(rFrom, rTo, ratio);
        makeQuat(q);
    }

    void makeQuat(const Quat4f& rSrcQuat) {
        zeroTrans();
        Rot3f::setQuat(rSrcQuat);
    }

    void setPositionFromLookAt(const Pos3f& rLookAt) {
        mMtx[0][0] = -rLookAt.get(0, 0);
        mMtx[1][1] = rLookAt.get(1, 1);
        mMtx[2][2] = -rLookAt.get(2, 2);

        f32 yx = rLookAt.get(1, 0);
        mMtx[1][0] = -rLookAt.get(0, 1);
        mMtx[0][1] = yx;

        f32 zx = rLookAt.get(2, 0);
        mMtx[2][0] = -rLookAt.get(0, 2);
        mMtx[0][2] = -zx;

        f32 yz = rLookAt.get(1, 2);
        mMtx[1][2] = -rLookAt.get(2, 1);
        mMtx[2][1] = yz;

        Vec3f pos;
        rLookAt.getTrans(pos);

        mMtx[0][3] = pos.x * get(0, 0) - pos.y * get(0, 1) + pos.z * get(0, 2);
        mMtx[1][3] = pos.x * get(1, 0) - pos.y * get(1, 1) + pos.z * get(1, 2);
        mMtx[2][3] = pos.x * get(2, 0) - pos.y * get(2, 1) + pos.z * get(2, 2);
    }

    void setPositionFromLookAt(const Vec3f& rLookAtPos, const Vec3f& rUp, const Vec3f& rPos) {
        Vec3f aim;
        aim.sub(rPos, rLookAtPos);

        Vec3f front, up, side;
        front.set(aim);
        front.normalize();
        front.negate();

        side.cross(rUp, front);
        up.cross(front, side);
        side.normalize();
        up.normalize();

        mMtx[0][0] = side.x;
        mMtx[0][1] = side.y;
        mMtx[0][2] = side.z;
        mMtx[1][0] = up.x;
        mMtx[1][1] = up.y;
        mMtx[1][2] = up.z;
        mMtx[2][0] = front.x;
        mMtx[2][1] = front.y;
        mMtx[2][2] = front.z;

        mMtx[0][3] = rLookAtPos.x * -mMtx[0][0] - rLookAtPos.y * mMtx[0][1] - rLookAtPos.z * mMtx[0][2];
        mMtx[1][3] = rLookAtPos.x * -mMtx[1][0] - rLookAtPos.y * mMtx[1][1] - rLookAtPos.z * mMtx[1][2];
        mMtx[2][3] = rLookAtPos.x * -mMtx[2][0] - rLookAtPos.y * mMtx[2][1] - rLookAtPos.z * mMtx[2][2];
    }

    void setQT(const Quat4f& rSrcQuat, const Vec3f& rSrcTrans) {
        Rot3f::setQuat(rSrcQuat);
        setTrans(rSrcTrans);
    }

    void setRTDegree(const Vec3f& rRot, const Vec3f& rSrcTrans) {
        Rot3f::setRotate(rRot);
        setTrans(rSrcTrans);
    }

    void setRT(const Vec3f& rRot, const Vec3f& rSrcTrans) {
        setRT(rRot.x, rRot.y, rRot.z, rSrcTrans);
    }

    void setRT(f32 rx, f32 ry, f32 rz, const Vec3f& rSrcTrans) {
        f32 sinX, sinY, sinZ;
        f32 cosX, cosY, cosZ;

        cosZ = Angle::libCos(rz);
        cosY = Angle::libCos(ry);
        cosX = Angle::libCos(rx);
        sinZ = Angle::libSin(rz);
        sinY = Angle::libSin(ry);
        sinX = Angle::libSin(rx);

        f32 sXsY = sinX * sinY;
        f32 cXsZ = cosX * sinZ;
        f32 cXcZ = cosX * cosZ;

        mMtx[0][0] = cosY * cosZ;
        mMtx[1][0] = cosY * sinZ;
        mMtx[2][0] = -sinY;

        mMtx[0][1] = sXsY * cosZ - cosX * sinZ;
        mMtx[1][1] = cosX * cosZ + sXsY * sinZ;
        mMtx[2][1] = sinX * cosY;

        mMtx[0][2] = cXcZ * sinY + sinX * sinZ;
        mMtx[1][2] = cXsZ * sinY - sinX * cosZ;
        mMtx[2][2] = cosX * cosY;

        const Vec3f* pTrans = &rSrcTrans;
        mMtx[0][3] = pTrans->x;
        mMtx[1][3] = pTrans->y;
        mMtx[2][3] = pTrans->z;
    }

    inline void normalizeBasis() {

        f32 invLenX = FloatUtil::inv_sqrt((get(0, 0) * get(0, 0)) + (get(1, 0) * get(1, 0)) +
                                                        (get(2, 0) * get(2, 0)));

        mMtx[0][0] = invLenX * get(0, 0);
        mMtx[1][0] = invLenX * get(1, 0);
        mMtx[2][0] = invLenX * get(2, 0);

        f32 invLenY = FloatUtil::inv_sqrt((get(0, 1) * get(0, 1)) + (get(1, 1) * get(1, 1)) +
                                                        (get(2, 1) * get(2, 1)));

        mMtx[0][1] = invLenY * get(0, 1);
        mMtx[1][1] = invLenY * get(1, 1);
        mMtx[2][1] = invLenY * get(2, 1);

        f32 invLenZ = FloatUtil::inv_sqrt((get(0, 2) * get(0, 2)) + (get(1, 2) * get(1, 2)) +
                                                        (get(2, 2) * get(2, 2)));

        mMtx[0][2] = invLenZ * get(0, 2);
        mMtx[1][2] = invLenZ * get(1, 2);
        mMtx[2][2] = invLenZ * get(2, 2);
    }
};

struct Mtx44f {
    typedef f32 ArrType[4];

    void set(const Mtx44f& rSrc) {
        __builtin_memcpy(this, rSrc, sizeof(Mtx44));
    }

    void set(f32 rxx, f32 ryx, f32 rzx, f32 tx, f32 rxy, f32 ryy, f32 rzy, f32 ty, f32 rxz, f32 ryz, f32 rzz, f32 tz, f32 wx, f32 wy, f32 wz, f32 ww) {
        mMtx[0][0] = rxx;
        mMtx[0][1] = ryx;
        mMtx[0][2] = rzx;
        mMtx[0][3] = tx;

        mMtx[1][0] = rxy;
        mMtx[1][1] = ryy;
        mMtx[1][2] = rzy;
        mMtx[1][3] = ty;

        mMtx[2][0] = rxz;
        mMtx[2][1] = ryz;
        mMtx[2][2] = rzz;
        mMtx[2][3] = tz;

        mMtx[3][0] = wx;
        mMtx[3][1] = wy;
        mMtx[3][2] = wz;
        mMtx[3][3] = ww;
    }

    inline Mtx44Ptr toMtx44() {
        return (Mtx44Ptr)mMtx;
    }

    inline const Mtx44* toCMtx44() const {
        return (const Mtx44*)mMtx;
    }

    inline Mtx44Ptr toMtx44Ptr() {
        return (Mtx44Ptr)mMtx;
    }

    operator ArrType*() {
        return mMtx;
    }

    operator const ArrType*() const {
        return mMtx;
    }

    f32 get(int x, int y) const {
        return mMtx[x][y];
    }

    f32 operator()(int x, int y) const {
        return get(x, y);
    }

    inline void setInline(const Mtx44Ptr rSrc) {
        __builtin_memcpy(this, rSrc, sizeof(Mtx44));
    }

    inline void setInline(const Mtx44f& rSrc) {
        __builtin_memcpy(this, rSrc, sizeof(Mtx44));
    }

    inline void setInline(const Mtx44f* rSrc) {
        __builtin_memcpy(this, rSrc, sizeof(Mtx44));
    }

    void identity() {
        mMtx[0][0] = 1.0f;
        mMtx[0][1] = 0.0f;
        mMtx[0][2] = 0.0f;
        mMtx[0][3] = 0.0f;

        mMtx[1][0] = 0.0f;
        mMtx[1][1] = 1.0f;
        mMtx[1][2] = 0.0f;
        mMtx[1][3] = 0.0f;

        mMtx[2][0] = 0.0f;
        mMtx[2][1] = 0.0f;
        mMtx[2][2] = 1.0f;
        mMtx[2][3] = 0.0f;

        mMtx[3][0] = 0.0f;
        mMtx[3][1] = 0.0f;
        mMtx[3][2] = 0.0f;
        mMtx[3][3] = 1.0f;
    }

    void concat(const Mtx44f& rA, const Mtx44f& rB) {
        set(rA[0][3] * rB[3][0] + (rA[0][2] * rB[2][0] + (rA[0][0] * rB[0][0] + rA[0][1] * rB[1][0])),
                  rA[0][3] * rB[3][1] + (rA[0][2] * rB[2][1] + (rA[0][0] * rB[0][1] + rA[0][1] * rB[1][1])),
                  rA[0][3] * rB[3][2] + (rA[0][2] * rB[2][2] + (rA[0][0] * rB[0][2] + rA[0][1] * rB[1][2])),
                  rA[0][3] * rB[3][3] + (rA[0][2] * rB[2][3] + (rA[0][0] * rB[0][3] + rA[0][1] * rB[1][3])),
                  rA[1][3] * rB[3][0] + (rA[1][2] * rB[2][0] + (rA[1][0] * rB[0][0] + rA[1][1] * rB[1][0])),
                  rA[1][3] * rB[3][1] + (rA[1][2] * rB[2][1] + (rA[1][0] * rB[0][1] + rA[1][1] * rB[1][1])),
                  rA[1][3] * rB[3][2] + (rA[1][2] * rB[2][2] + (rA[1][0] * rB[0][2] + rA[1][1] * rB[1][2])),
                  rA[1][3] * rB[3][3] + (rA[1][2] * rB[2][3] + (rA[1][0] * rB[0][3] + rA[1][1] * rB[1][3])),
                  rA[2][3] * rB[3][0] + (rA[2][2] * rB[2][0] + (rA[2][0] * rB[0][0] + rA[2][1] * rB[1][0])),
                  rA[2][3] * rB[3][1] + (rA[2][2] * rB[2][1] + (rA[2][0] * rB[0][1] + rA[2][1] * rB[1][1])),
                  rA[2][3] * rB[3][2] + (rA[2][2] * rB[2][2] + (rA[2][0] * rB[0][2] + rA[2][1] * rB[1][2])),
                  rA[2][3] * rB[3][3] + (rA[2][2] * rB[2][3] + (rA[2][0] * rB[0][3] + rA[2][1] * rB[1][3])),
                  rA[3][3] * rB[3][0] + (rA[3][2] * rB[2][0] + (rA[3][0] * rB[0][0] + rA[3][1] * rB[1][0])),
                  rA[3][3] * rB[3][1] + (rA[3][2] * rB[2][1] + (rA[3][0] * rB[0][1] + rA[3][1] * rB[1][1])),
                  rA[3][3] * rB[3][2] + (rA[3][2] * rB[2][2] + (rA[3][0] * rB[0][2] + rA[3][1] * rB[1][2])),
                  rA[3][3] * rB[3][3] + (rA[3][2] * rB[2][3] + (rA[3][0] * rB[0][3] + rA[3][1] * rB[1][3])));
    }

    inline void mult(const Vec3f& rSrc, Vec3f& rDest) const {
        Vec4f pos(rSrc.x * mMtx[0][0] + rSrc.z * mMtx[0][2],
                   rSrc.y * mMtx[1][1] + rSrc.z * mMtx[1][2],
                   rSrc.z * mMtx[2][2] + mMtx[2][3],
                   -rSrc.z);

        rDest.scale(1.0f / pos.w, *pos.toVec3());
    }

    f32 mMtx[4][4];
};

struct Proj3f : public Mtx44f {
    Proj3f() {};

    Proj3f(const Mtx44Ptr rSrc) {
        __builtin_memcpy((void*)this, rSrc, sizeof(Mtx44));
    }

    Proj3f(const Proj3f& rSrc) {
        __builtin_memcpy((void*)this, rSrc, sizeof(Mtx44));
    }

    f32 getFocalLength(f32 fov) {
        return 1.0f / (f32)::tan(((2.0f * FloatUtil::PI()) * fov) / (360.0f * 2.0f));
    }

    f32 getPower(f32 fov) {
        return ::tan(((2.0f * FloatUtil::PI()) * fov) / (360.0f * 2.0f));
    }

    void makePerspective(f32 fov, f32 aspect, f32 near, f32 far) {
        f32 power = ::tan(((2.0f * FloatUtil::PI()) * fov) / (360.0f * 2.0f));
        f32 focalLen = 1.0f / power;

        mMtx[0][0] = focalLen / aspect;
        mMtx[0][1] = 0.0f;
        mMtx[0][2] = 0.0f;
        mMtx[0][3] = 0.0f;

        mMtx[1][0] = 0.0f;
        mMtx[1][1] = focalLen;
        mMtx[1][2] = 0.0f;
        mMtx[1][3] = 0.0f;

        f32 scale = 1.0f / (far - near);

        mMtx[2][0] = 0.0f;
        mMtx[2][1] = 0.0f;
        mMtx[2][2] = -near * scale;
        mMtx[2][3] = -(far * near) * scale;

        mMtx[3][0] = 0.0f;
        mMtx[3][1] = 0.0f;
        mMtx[3][2] = -1.0f;
        mMtx[3][3] = 0.0f;
    }

    void makeTrans(const Vec2f& offset) {
        mMtx[0][0] = 1.0f;
        mMtx[0][1] = 0.0f;
        mMtx[0][2] = 0.0f;
        mMtx[0][3] = offset.x;

        mMtx[1][0] = 0.0f;
        mMtx[1][1] = 1.0f;
        mMtx[1][2] = 0.0f;
        mMtx[1][3] = offset.y;

        mMtx[2][0] = 0.0f;
        mMtx[2][1] = 0.0f;
        mMtx[2][2] = 1.0f;
        mMtx[2][3] = 0.0f;

        mMtx[3][0] = 0.0f;
        mMtx[3][1] = 0.0f;
        mMtx[3][2] = 0.0f;
        mMtx[3][3] = 1.0f;
    }

    void makeTrans(f32 offsetX, f32 offsetY) {
        mMtx[0][0] = 1.0f;
        mMtx[1][0] = 0.0f;
        mMtx[2][0] = 0.0f;
        mMtx[0][1] = 0.0f;
        mMtx[1][1] = 1.0f;
        mMtx[2][1] = 0.0f;
        mMtx[0][2] = 0.0f;
        mMtx[1][2] = 0.0f;
        mMtx[2][2] = 1.0f;
        mMtx[0][3] = offsetX;
        mMtx[1][3] = offsetY;
        mMtx[2][3] = 0.0f;
        mMtx[3][0] = 0.0f;
        mMtx[3][1] = 0.0f;
        mMtx[3][2] = 0.0f;
        mMtx[3][3] = 1.0f;
    }
};
