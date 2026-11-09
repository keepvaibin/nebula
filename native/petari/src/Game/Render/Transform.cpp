#include "Game/Render/Model.hpp"
#include "Game/Render/Transform.hpp"
#include <nebula_geometry.hpp>

void billboardMtx(Mtx mtx) {
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

void billboardYMtx(Mtx mtx) {
    f32 x = (mtx[0][0] * mtx[0][0]) + (mtx[1][0] * mtx[1][0]) + (mtx[2][0] * mtx[2][0]);
    f32 z = (mtx[0][2] * mtx[0][2]) + (mtx[1][2] * mtx[1][2]) + (mtx[2][2] * mtx[2][2]);

    if (x > 0.0f) {
        x = fastSqrt(x);
    }

    if (z > 0.0f) {
        z = fastSqrt(z);
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

void Skeleton::translateRotateMtx(const TransformInfo& tx, Mtx dst) {
    f32 cxsz;
    f32 sxcz;

    f32 sine[3], cosine[3];
    NebulaSinCos3(tx.mRotation.x, tx.mRotation.y, tx.mRotation.z, sine, cosine);
    const f32 sx = sine[0], sy = sine[1], sz = sine[2];
    const f32 cx = cosine[0], cy = cosine[1], cz = cosine[2];

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

void Skeleton::translateRotateMtx(f32 rx, f32 ry, f32 rz, f32 tx, f32 ty, f32 tz, Mtx dst) {
    f32 cxsz;
    f32 sxcz;

    f32 sine[3], cosine[3];
    NebulaSinCos3(rx, ry, rz, sine, cosine);
    const f32 sx = sine[0], sy = sine[1], sz = sine[2];
    const f32 cx = cosine[0], cy = cosine[1], cz = cosine[2];

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

void textureMtx(const TextureTransform& srt, const Vec& center, Mtx dst) {
    f32 sr, cr;
    NebulaSinCos(srt.mRotation, &sr, &cr);

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

void textureMtxOld(const TextureTransform& srt, const Vec& center, Mtx dst) {
    f32 sr, cr;
    NebulaSinCos(srt.mRotation, &sr, &cr);

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

void textureMtxMaya(const TextureTransform& srt, Mtx dst) {
    f32 sr, cr;
    NebulaSinCos(srt.mRotation, &sr, &cr);
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

void textureMtxMayaOld(const TextureTransform& srt, Mtx dst) {
    f32 sr, cr;
    NebulaSinCos(srt.mRotation, &sr, &cr);
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

// dst = mtx1 (3x4) times mtx2 read as 4x4 (all four of its rows).
void projConcat(Mtx mtx1, Mtx mtx2, Mtx dst) {
    const f32(*m2)[4] = (const f32(*)[4])mtx2;
    for (int i = 0; i < 3; i++) {
        const f32 a0 = mtx1[i][0], a1 = mtx1[i][1], a2 = mtx1[i][2], a3 = mtx1[i][3];
        for (int j = 0; j < 4; j++) {
            dst[i][j] = a0 * m2[0][j] + a1 * m2[1][j] + a2 * m2[2][j] + a3 * m2[3][j];
        }
    }
}



