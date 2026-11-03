#include "JSystem/J3DGraphBase/J3DTransform.hpp"
#include "JSystem/J3DGraphBase/J3DStruct.hpp"
#include "JSystem/JMath.hpp"
#include "JSystem/JMath/JMATrigonometric.hpp"
#include <cmath>
#include <nebula_math.h>

const J3DTransformInfo j3dDefaultTransformInfo = {
    {1.0f, 1.0f, 1.0f},
    {0, 0, 0},
    0,
    {0.0f, 0.0f, 0.0f},
};

const Vec j3dDefaultScale = {1.0f, 1.0f, 1.0f};

const Mtx j3dDefaultMtx = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
};

void J3DCalcBBoardMtx(Mtx mtx) {
    f32 x = (mtx[0][0] * mtx[0][0]) + (mtx[1][0] * mtx[1][0]) + (mtx[2][0] * mtx[2][0]);
    f32 y = (mtx[0][1] * mtx[0][1]) + (mtx[1][1] * mtx[1][1]) + (mtx[2][1] * mtx[2][1]);
    f32 z = (mtx[0][2] * mtx[0][2]) + (mtx[1][2] * mtx[1][2]) + (mtx[2][2] * mtx[2][2]);

    if (x > 0.0f) {
        x = sqrt(x);
    }

    if (y > 0.0f) {
        y = sqrt(y);
    }

    if (z > 0.0f) {
        z = sqrt(z);
    }

    mtx[0][0] = x;
    mtx[0][1] = 0.0f;
    mtx[0][2] = 0.0f;
    mtx[2][0] = 0.0f;
    mtx[2][1] = 0.0f;
    mtx[1][0] = 0.0f;
    mtx[1][1] = y;
    mtx[1][2] = 0.0f;
    mtx[2][2] = z;
}

void J3DCalcYBBoardMtx(Mtx mtx) {
    f32 x = (mtx[0][0] * mtx[0][0]) + (mtx[1][0] * mtx[1][0]) + (mtx[2][0] * mtx[2][0]);
    f32 z = (mtx[0][2] * mtx[0][2]) + (mtx[1][2] * mtx[1][2]) + (mtx[2][2] * mtx[2][2]);

    if (x > 0.0f) {
        x = JMath::fastSqrt(x);
    }

    if (z > 0.0f) {
        z = JMath::fastSqrt(z);
    }

    Vec vec = {0.0f, -mtx[2][1], mtx[1][1]};
    NebulaVecNormalize(&vec, &vec);

    mtx[0][0] = x;
    mtx[0][2] = 0.0f;
    mtx[1][0] = 0.0f;

    mtx[1][2] = vec.y * z;
    mtx[2][0] = 0.0f;
    mtx[2][2] = vec.z * z;
}

// dst = the inverse transpose of src's 3x3 part (cofactors over the determinant); dst is left alone when the
// determinant is 0.
void J3DPSCalcInverseTranspose(Mtx src, Mtx33 dst) {
    const f32 c00 = src[1][1] * src[2][2] - src[2][1] * src[1][2];
    const f32 c01 = src[1][2] * src[2][0] - src[2][2] * src[1][0];
    const f32 c02 = src[1][0] * src[2][1] - src[1][1] * src[2][0];
    const f32 c10 = src[2][1] * src[0][2] - src[0][1] * src[2][2];
    const f32 c11 = src[2][2] * src[0][0] - src[0][2] * src[2][0];
    const f32 c12 = src[0][1] * src[2][0] - src[0][0] * src[2][1];
    const f32 c20 = src[0][1] * src[1][2] - src[1][1] * src[0][2];
    const f32 c21 = src[0][2] * src[1][0] - src[1][2] * src[0][0];
    const f32 c22 = src[0][0] * src[1][1] - src[0][1] * src[1][0];
    const f32 det = src[0][0] * c00 + src[1][0] * c10 + src[2][0] * c20;
    if (det == 0.0f) {
        return;
    }
    const f32 inv = 1.0f / det;
    dst[0][0] = c00 * inv;
    dst[0][1] = c01 * inv;
    dst[0][2] = c02 * inv;
    dst[1][0] = c10 * inv;
    dst[1][1] = c11 * inv;
    dst[1][2] = c12 * inv;
    dst[2][0] = c20 * inv;
    dst[2][1] = c21 * inv;
    dst[2][2] = c22 * inv;
}

void J3DGetTranslateRotateMtx(const J3DTransformInfo& tx, Mtx dst) {
    f32 cxsz;
    f32 sxcz;

    f32 sx = JMASSin(tx.mRotation.x), cx = JMath::sSinCosTable.cosShort(tx.mRotation.x);
    f32 sy = JMASSin(tx.mRotation.y), cy = JMath::sSinCosTable.cosShort(tx.mRotation.y);
    f32 sz = JMASSin(tx.mRotation.z), cz = JMath::sSinCosTable.cosShort(tx.mRotation.z);

    dst[2][0] = -sy;
    dst[0][0] = cz * cy;
    dst[1][0] = sz * cy;
    dst[2][1] = cy * sx;
    dst[2][2] = cy * cx;

    cxsz = cx * sz;
    sxcz = sx * cz;
    dst[0][1] = sxcz * sy - cxsz;
    dst[1][2] = cxsz * sy - sxcz;

    cxsz = sx * sz;
    sxcz = cx * cz;
    dst[0][2] = sxcz * sy + cxsz;
    dst[1][1] = cxsz * sy + sxcz;

    dst[0][3] = tx.mTranslate.x;
    dst[1][3] = tx.mTranslate.y;
    dst[2][3] = tx.mTranslate.z;
}

void J3DGetTranslateRotateMtx(s16 rx, s16 ry, s16 rz, f32 tx, f32 ty, f32 tz, Mtx dst) {
    f32 cxsz;
    f32 sxcz;

    f32 sx = JMASSin(rx), cx = JMath::sSinCosTable.cosShort(rx);
    f32 sy = JMASSin(ry), cy = JMath::sSinCosTable.cosShort(ry);
    f32 sz = JMASSin(rz), cz = JMath::sSinCosTable.cosShort(rz);

    dst[2][0] = -sy;
    dst[0][0] = cz * cy;
    dst[1][0] = sz * cy;
    dst[2][1] = cy * sx;
    dst[2][2] = cy * cx;

    cxsz = cx * sz;
    sxcz = sx * cz;
    dst[0][1] = sxcz * sy - cxsz;
    dst[1][2] = cxsz * sy - sxcz;

    cxsz = sx * sz;
    sxcz = cx * cz;
    dst[0][2] = sxcz * sy + cxsz;
    dst[1][1] = cxsz * sy + sxcz;

    dst[0][3] = tx;
    dst[1][3] = ty;
    dst[2][3] = tz;
}

void J3DGetTextureMtx(const J3DTextureSRTInfo& srt, const Vec& center, Mtx dst) {
    f32 sr = JMASSin(srt.mRotation), cr = JMath::sSinCosTable.cosShort(srt.mRotation);

    f32 cx = srt.mScaleX * cr;
    f32 sx = srt.mScaleX * sr;
    f32 sy = srt.mScaleY * sr;
    f32 cy = srt.mScaleY * cr;

    dst[0][0] = cx;
    dst[0][1] = -sx;
    dst[0][2] = (-cx * center.x + sx * center.y) + center.x + srt.mTranslationX;

    dst[1][0] = sy;
    dst[1][1] = cy;
    dst[1][2] = (-sy * center.x - cy * center.y) + center.y + srt.mTranslationY;

    dst[0][3] = dst[1][3] = dst[2][0] = dst[2][1] = dst[2][3] = 0.0f;
    dst[2][2] = 1.0f;
}

void J3DGetTextureMtxOld(const J3DTextureSRTInfo& srt, const Vec& center, Mtx dst) {
    f32 sr = JMASSin(srt.mRotation), cr = JMath::sSinCosTable.cosShort(srt.mRotation);

    f32 cx = srt.mScaleX * cr;
    f32 sx = srt.mScaleX * sr;
    f32 sy = srt.mScaleY * sr;
    f32 cy = srt.mScaleY * cr;

    dst[0][0] = cx;
    dst[0][1] = -sx;
    dst[0][3] = (-cx * center.x + sx * center.y) + center.x + srt.mTranslationX;

    dst[1][0] = sy;
    dst[1][1] = cy;
    dst[1][3] = (-sy * center.x - cy * center.y) + center.y + srt.mTranslationY;

    dst[0][2] = dst[1][2] = dst[2][0] = dst[2][1] = dst[2][3] = 0.0f;
    dst[2][2] = 1.0f;
}

void J3DGetTextureMtxMaya(const J3DTextureSRTInfo& srt, Mtx dst) {
    f32 sr = JMASSin(srt.mRotation), cr = JMath::sSinCosTable.cosShort(srt.mRotation);
    f32 tx = srt.mTranslationX - 0.5f;
    f32 ty = srt.mTranslationY - 0.5f;

    dst[0][0] = srt.mScaleX * cr;
    dst[0][1] = srt.mScaleY * sr;
    dst[0][2] = tx * cr - sr * (ty + srt.mScaleY) + 0.5f;

    dst[1][0] = -srt.mScaleX * sr;
    dst[1][1] = srt.mScaleY * cr;
    dst[1][2] = -tx * sr - cr * (ty + srt.mScaleY) + 0.5f;

    dst[0][3] = dst[1][3] = dst[2][0] = dst[2][1] = dst[2][3] = 0.0f;
    dst[2][2] = 1.0f;
}

void J3DGetTextureMtxMayaOld(const J3DTextureSRTInfo& srt, Mtx dst) {
    f32 sr = JMASSin(srt.mRotation), cr = JMath::sSinCosTable.cosShort(srt.mRotation);
    f32 tx = srt.mTranslationX - 0.5f;
    f32 ty = srt.mTranslationY - 0.5f;

    dst[0][0] = srt.mScaleX * cr;
    dst[0][1] = srt.mScaleY * sr;
    dst[0][3] = tx * cr - sr * (ty + srt.mScaleY) + 0.5f;

    dst[1][0] = -srt.mScaleX * sr;
    dst[1][1] = srt.mScaleY * cr;
    dst[1][3] = -tx * sr - cr * (ty + srt.mScaleY) + 0.5f;

    dst[0][2] = dst[1][2] = dst[2][0] = dst[2][1] = dst[2][3] = 0.0f;
    dst[2][2] = 1.0f;
}

// Scales the columns of mtx's 3x3 part by scl.
void J3DScaleNrmMtx(Mtx mtx, const Vec& scl) {
    for (int i = 0; i < 3; i++) {
        mtx[i][0] *= scl.x;
        mtx[i][1] *= scl.y;
        mtx[i][2] *= scl.z;
    }
}

void J3DScaleNrmMtx33(Mtx33 mtx, const Vec& scale) {
    for (int i = 0; i < 3; i++) {
        mtx[i][0] *= scale.x;
        mtx[i][1] *= scale.y;
        mtx[i][2] *= scale.z;
    }
}

// dst = mtx1 (3x4) times mtx2 read as 4x4 (all four of its rows).
void J3DMtxProjConcat(Mtx mtx1, Mtx mtx2, Mtx dst) {
    const f32(*m2)[4] = (const f32(*)[4])mtx2;
    for (int i = 0; i < 3; i++) {
        const f32 a0 = mtx1[i][0], a1 = mtx1[i][1], a2 = mtx1[i][2], a3 = mtx1[i][3];
        for (int j = 0; j < 4; j++) {
            dst[i][j] = a0 * m2[0][j] + a1 * m2[1][j] + a2 * m2[2][j] + a3 * m2[3][j];
        }
    }
}


// mAB[i] = mA * mB[i] for consecutive 3x4 matrices with the implied (0, 0, 0, 1) row. Each product is complete
// before it is stored, so mAB may be mB.
void J3DPSMtxArrayConcat(Mtx mA, Mtx mB, Mtx mAB, u32 count) {
    for (u32 n = 0; n < count; n++) {
        const f32(*b)[4] = mB + 3 * n;
        Mtx out;
        for (int i = 0; i < 3; i++) {
            const f32 a0 = mA[i][0], a1 = mA[i][1], a2 = mA[i][2];
            out[i][0] = a0 * b[0][0] + a1 * b[1][0] + a2 * b[2][0];
            out[i][1] = a0 * b[0][1] + a1 * b[1][1] + a2 * b[2][1];
            out[i][2] = a0 * b[0][2] + a1 * b[1][2] + a2 * b[2][2];
            out[i][3] = a0 * b[0][3] + a1 * b[1][3] + a2 * b[2][3] + mA[i][3];
        }
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                mAB[3 * n + i][j] = out[i][j];
            }
        }
    }
}

