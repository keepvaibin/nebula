#include "Game/Util/OctahedronBezierSurface.hpp"
#include "Game/Util/BezierSurface.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f1, "OctahedronBezierSurface.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f2, "OctahedronBezierSurface.f2", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f3, "OctahedronBezierSurface.f3", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f4, "OctahedronBezierSurface.f4", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f5, "OctahedronBezierSurface.f5", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f6, "OctahedronBezierSurface.f6", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f7, "OctahedronBezierSurface.f7", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f8, "OctahedronBezierSurface.f8", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f9, "OctahedronBezierSurface.f9", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f10, "OctahedronBezierSurface.f10", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f11, "OctahedronBezierSurface.f11", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f12, "OctahedronBezierSurface.f12", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f13, "OctahedronBezierSurface.f13", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f14, "OctahedronBezierSurface.f14", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f15, "OctahedronBezierSurface.f15", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f16, "OctahedronBezierSurface.f16", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f17, "OctahedronBezierSurface.f17", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f18, "OctahedronBezierSurface.f18", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f19, "OctahedronBezierSurface.f19", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f20, "OctahedronBezierSurface.f20", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f21, "OctahedronBezierSurface.f21", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f22, "OctahedronBezierSurface.f22", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f23, "OctahedronBezierSurface.f23", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f24, "OctahedronBezierSurface.f24", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f25, "OctahedronBezierSurface.f25", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f26, "OctahedronBezierSurface.f26", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f27, "OctahedronBezierSurface.f27", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f28, "OctahedronBezierSurface.f28", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f29, "OctahedronBezierSurface.f29", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f30, "OctahedronBezierSurface.f30", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f31, "OctahedronBezierSurface.f31", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f32, "OctahedronBezierSurface.f32", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f33, "OctahedronBezierSurface.f33", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_OctahedronBezierSurface_f34, "OctahedronBezierSurface.f34", 0.5f);

namespace {
    NEBULA_TUNABLE(s32, sCoefTableDivideNum, 6);

    void calcBezierHandlePoint(Vec3f* pHandle1, Vec3f* pHandle2, const Vec3f& rPointA, const Vec3f& rPointB, const Vec3f& rPointC,
                               f32 diagonalRate) {
        // Both handles at B lie on one tangent through B (BA scaled to BC's length, minus BC), each at diagonalRate of its
        // side's projection on it. A point on B divides by zero; the handles are then not finite, as the game's calculation also gives.
        Vec3f toA(rPointA - rPointB);
        Vec3f toC(rPointC - rPointB);
        f32 distAB = toA.length();
        f32 distCB = toC.length();

        Vec3f tangent;
        tangent.set(toA * (distCB / distAB) - toC);
        MR::normalizeOrZero(&tangent);

        pHandle1->set(rPointB + tangent * (diagonalRate * toA.dot(tangent)));
        pHandle2->set(rPointB + tangent * (diagonalRate * toC.dot(tangent)));
    }
};  // namespace

OctahedronBezierSurface::OctahedronBezierSurface(u32 flags) : mDiagonalRate(0.6f) {
    mVertices[0].set(0.0f, cNum_OctahedronBezierSurface_f1, 0.0f);
    mVertices[1].set(cNum_OctahedronBezierSurface_f2, cNum_OctahedronBezierSurface_f3, 0.0f);
    mVertices[2].set(0.0f, cNum_OctahedronBezierSurface_f4, cNum_OctahedronBezierSurface_f5);
    mVertices[3].set(-cNum_OctahedronBezierSurface_f6, cNum_OctahedronBezierSurface_f7, 0.0f);
    mVertices[4].set(0.0f, cNum_OctahedronBezierSurface_f8, -cNum_OctahedronBezierSurface_f9);
    mVertices[5].set(0.0f, cNum_OctahedronBezierSurface_f10, 0.0f);

    for (s32 idx = 0; idx < 8; idx++) {
        mSurfaces[idx] = new BezierSurface(::sCoefTableDivideNum, flags);
    }

    mSurfaces[0]->setST(Vec2f(cNum_OctahedronBezierSurface_f11, cNum_OctahedronBezierSurface_f12), Vec2f(0.0f, 0.0f), Vec2f(0.0f, 1.0f));
    mSurfaces[1]->setST(Vec2f(cNum_OctahedronBezierSurface_f13, cNum_OctahedronBezierSurface_f14), Vec2f(0.0f, 1.0f), Vec2f(cNum_OctahedronBezierSurface_f15, 1.0f));
    mSurfaces[2]->setST(Vec2f(cNum_OctahedronBezierSurface_f16, cNum_OctahedronBezierSurface_f17), Vec2f(cNum_OctahedronBezierSurface_f18, 1.0f), Vec2f(cNum_OctahedronBezierSurface_f19, 0.0f));
    mSurfaces[3]->setST(Vec2f(cNum_OctahedronBezierSurface_f20, cNum_OctahedronBezierSurface_f21), Vec2f(cNum_OctahedronBezierSurface_f22, 0.0f), Vec2f(0.0f, 0.0f));
    mSurfaces[4]->setST(Vec2f(cNum_OctahedronBezierSurface_f23, cNum_OctahedronBezierSurface_f24), Vec2f(1.0f, 1.0f), Vec2f(1.0f, 0.0f));
    mSurfaces[5]->setST(Vec2f(cNum_OctahedronBezierSurface_f25, cNum_OctahedronBezierSurface_f26), Vec2f(1.0f, 0.0f), Vec2f(cNum_OctahedronBezierSurface_f27, 0.0f));
    mSurfaces[6]->setST(Vec2f(cNum_OctahedronBezierSurface_f28, cNum_OctahedronBezierSurface_f29), Vec2f(cNum_OctahedronBezierSurface_f30, 0.0f), Vec2f(cNum_OctahedronBezierSurface_f31, 1.0f));
    mSurfaces[7]->setST(Vec2f(cNum_OctahedronBezierSurface_f32, cNum_OctahedronBezierSurface_f33), Vec2f(cNum_OctahedronBezierSurface_f34, 1.0f), Vec2f(1.0f, 1.0f));
    calcControlPoint();
}

const Vec3f& OctahedronBezierSurface::getVertex(s32 index) const {
    return mVertices[index];
}

Vec3f* OctahedronBezierSurface::getVertexPtr(s32 index) {
    return &mVertices[index];
}

void OctahedronBezierSurface::setDiagonalRate(f32 diagonalRate) {
    mDiagonalRate = diagonalRate;
}

void OctahedronBezierSurface::setDivideLevel(s32 divideLevel) {
    for (s32 idx = 0; idx < 8; idx++) {
        mSurfaces[idx]->setDivideLevel(divideLevel);
    }
}

void OctahedronBezierSurface::movement() {
    calcControlPoint();
}

void OctahedronBezierSurface::calcAnim() {
    for (s32 idx = 0; idx < 8; idx++) {
        mSurfaces[idx]->calcBuffer();
    }
}

void OctahedronBezierSurface::calcControlPoint() {
    mSurfaces[0]->mCtrlPts[0].set(mVertices[0]);
    mSurfaces[1]->mCtrlPts[0].set(mVertices[0]);
    mSurfaces[2]->mCtrlPts[0].set(mVertices[0]);
    mSurfaces[3]->mCtrlPts[0].set(mVertices[0]);
    mSurfaces[4]->mCtrlPts[0].set(mVertices[5]);
    mSurfaces[5]->mCtrlPts[0].set(mVertices[5]);
    mSurfaces[6]->mCtrlPts[0].set(mVertices[5]);
    mSurfaces[7]->mCtrlPts[0].set(mVertices[5]);

    mSurfaces[0]->mCtrlPts[9].set(mVertices[1]);
    mSurfaces[0]->mCtrlPts[12].set(mVertices[2]);
    mSurfaces[1]->mCtrlPts[9].set(mVertices[2]);
    mSurfaces[1]->mCtrlPts[12].set(mVertices[3]);
    mSurfaces[2]->mCtrlPts[9].set(mVertices[3]);
    mSurfaces[2]->mCtrlPts[12].set(mVertices[4]);
    mSurfaces[3]->mCtrlPts[9].set(mVertices[4]);
    mSurfaces[3]->mCtrlPts[12].set(mVertices[1]);
    mSurfaces[4]->mCtrlPts[9].set(mVertices[2]);
    mSurfaces[4]->mCtrlPts[12].set(mVertices[1]);
    mSurfaces[5]->mCtrlPts[9].set(mVertices[1]);
    mSurfaces[5]->mCtrlPts[12].set(mVertices[4]);
    mSurfaces[6]->mCtrlPts[9].set(mVertices[4]);
    mSurfaces[6]->mCtrlPts[12].set(mVertices[3]);
    mSurfaces[7]->mCtrlPts[9].set(mVertices[3]);
    mSurfaces[7]->mCtrlPts[12].set(mVertices[2]);

    Vec3f handle1, handle2;
    ::calcBezierHandlePoint(&handle1, &handle2, mVertices[1], mVertices[0], mVertices[3], mDiagonalRate);
    mSurfaces[0]->mCtrlPts[1].set(handle1);
    mSurfaces[3]->mCtrlPts[4].set(handle1);
    mSurfaces[2]->mCtrlPts[1].set(handle2);
    mSurfaces[1]->mCtrlPts[4].set(handle2);
    ::calcBezierHandlePoint(&handle1, &handle2, mVertices[2], mVertices[0], mVertices[4], mDiagonalRate);
    mSurfaces[1]->mCtrlPts[1].set(handle1);
    mSurfaces[0]->mCtrlPts[4].set(handle1);
    mSurfaces[3]->mCtrlPts[1].set(handle2);
    mSurfaces[2]->mCtrlPts[4].set(handle2);
    ::calcBezierHandlePoint(&handle1, &handle2, mVertices[2], mVertices[5], mVertices[4], mDiagonalRate);
    mSurfaces[4]->mCtrlPts[1].set(handle1);
    mSurfaces[7]->mCtrlPts[4].set(handle1);
    mSurfaces[6]->mCtrlPts[1].set(handle2);
    mSurfaces[5]->mCtrlPts[4].set(handle2);
    ::calcBezierHandlePoint(&handle1, &handle2, mVertices[1], mVertices[5], mVertices[3], mDiagonalRate);
    mSurfaces[5]->mCtrlPts[1].set(handle1);
    mSurfaces[4]->mCtrlPts[4].set(handle1);
    mSurfaces[7]->mCtrlPts[1].set(handle2);
    mSurfaces[6]->mCtrlPts[4].set(handle2);

    ::calcBezierHandlePoint(&handle1, &handle2, mVertices[0], mVertices[1], mVertices[5], mDiagonalRate);
    mSurfaces[0]->mCtrlPts[5].set(handle1);
    mSurfaces[3]->mCtrlPts[8].set(handle1);
    mSurfaces[4]->mCtrlPts[8].set(handle2);
    mSurfaces[5]->mCtrlPts[5].set(handle2);
    ::calcBezierHandlePoint(&handle1, &handle2, mVertices[0], mVertices[2], mVertices[5], mDiagonalRate);
    mSurfaces[1]->mCtrlPts[5].set(handle1);
    mSurfaces[0]->mCtrlPts[8].set(handle1);
    mSurfaces[7]->mCtrlPts[8].set(handle2);
    mSurfaces[4]->mCtrlPts[5].set(handle2);
    ::calcBezierHandlePoint(&handle1, &handle2, mVertices[0], mVertices[3], mVertices[5], mDiagonalRate);
    mSurfaces[2]->mCtrlPts[5].set(handle1);
    mSurfaces[1]->mCtrlPts[8].set(handle1);
    mSurfaces[6]->mCtrlPts[8].set(handle2);
    mSurfaces[7]->mCtrlPts[5].set(handle2);
    ::calcBezierHandlePoint(&handle1, &handle2, mVertices[0], mVertices[4], mVertices[5], mDiagonalRate);
    mSurfaces[3]->mCtrlPts[5].set(handle1);
    mSurfaces[2]->mCtrlPts[8].set(handle1);
    mSurfaces[5]->mCtrlPts[8].set(handle2);
    mSurfaces[6]->mCtrlPts[5].set(handle2);

    ::calcBezierHandlePoint(&handle1, &handle2, mVertices[1], mVertices[2], mVertices[3], mDiagonalRate);
    mSurfaces[0]->mCtrlPts[11].set(handle1);
    mSurfaces[4]->mCtrlPts[10].set(handle1);
    mSurfaces[1]->mCtrlPts[10].set(handle2);
    mSurfaces[7]->mCtrlPts[11].set(handle2);
    ::calcBezierHandlePoint(&handle1, &handle2, mVertices[2], mVertices[3], mVertices[4], mDiagonalRate);
    mSurfaces[1]->mCtrlPts[11].set(handle1);
    mSurfaces[7]->mCtrlPts[10].set(handle1);
    mSurfaces[2]->mCtrlPts[10].set(handle2);
    mSurfaces[6]->mCtrlPts[11].set(handle2);
    ::calcBezierHandlePoint(&handle1, &handle2, mVertices[3], mVertices[4], mVertices[1], mDiagonalRate);
    mSurfaces[2]->mCtrlPts[11].set(handle1);
    mSurfaces[6]->mCtrlPts[10].set(handle1);
    mSurfaces[3]->mCtrlPts[10].set(handle2);
    mSurfaces[5]->mCtrlPts[11].set(handle2);
    ::calcBezierHandlePoint(&handle1, &handle2, mVertices[4], mVertices[1], mVertices[2], mDiagonalRate);
    mSurfaces[3]->mCtrlPts[11].set(handle1);
    mSurfaces[5]->mCtrlPts[10].set(handle1);
    mSurfaces[0]->mCtrlPts[10].set(handle2);
    mSurfaces[4]->mCtrlPts[11].set(handle2);

    ::calcBezierHandlePoint(&handle1, &handle2, mSurfaces[0]->mCtrlPts[1], mSurfaces[1]->mCtrlPts[1], mSurfaces[2]->mCtrlPts[1], mDiagonalRate);
    mSurfaces[0]->mCtrlPts[3].set(handle1);
    mSurfaces[1]->mCtrlPts[2].set(handle2);
    ::calcBezierHandlePoint(&handle1, &handle2, mSurfaces[1]->mCtrlPts[1], mSurfaces[2]->mCtrlPts[1], mSurfaces[3]->mCtrlPts[1], mDiagonalRate);
    mSurfaces[1]->mCtrlPts[3].set(handle1);
    mSurfaces[2]->mCtrlPts[2].set(handle2);
    ::calcBezierHandlePoint(&handle1, &handle2, mSurfaces[2]->mCtrlPts[1], mSurfaces[3]->mCtrlPts[1], mSurfaces[0]->mCtrlPts[1], mDiagonalRate);
    mSurfaces[2]->mCtrlPts[3].set(handle1);
    mSurfaces[3]->mCtrlPts[2].set(handle2);
    ::calcBezierHandlePoint(&handle1, &handle2, mSurfaces[3]->mCtrlPts[1], mSurfaces[0]->mCtrlPts[1], mSurfaces[1]->mCtrlPts[1], mDiagonalRate);
    mSurfaces[3]->mCtrlPts[3].set(handle1);
    mSurfaces[0]->mCtrlPts[2].set(handle2);

    ::calcBezierHandlePoint(&handle1, &handle2, mSurfaces[4]->mCtrlPts[1], mSurfaces[5]->mCtrlPts[1], mSurfaces[6]->mCtrlPts[1], mDiagonalRate);
    mSurfaces[4]->mCtrlPts[3].set(handle1);
    mSurfaces[5]->mCtrlPts[2].set(handle2);
    ::calcBezierHandlePoint(&handle1, &handle2, mSurfaces[5]->mCtrlPts[1], mSurfaces[6]->mCtrlPts[1], mSurfaces[7]->mCtrlPts[1], mDiagonalRate);
    mSurfaces[5]->mCtrlPts[3].set(handle1);
    mSurfaces[6]->mCtrlPts[2].set(handle2);
    ::calcBezierHandlePoint(&handle1, &handle2, mSurfaces[6]->mCtrlPts[1], mSurfaces[7]->mCtrlPts[1], mSurfaces[4]->mCtrlPts[1], mDiagonalRate);
    mSurfaces[6]->mCtrlPts[3].set(handle1);
    mSurfaces[7]->mCtrlPts[2].set(handle2);
    ::calcBezierHandlePoint(&handle1, &handle2, mSurfaces[7]->mCtrlPts[1], mSurfaces[4]->mCtrlPts[1], mSurfaces[5]->mCtrlPts[1], mDiagonalRate);
    mSurfaces[7]->mCtrlPts[3].set(handle1);
    mSurfaces[4]->mCtrlPts[2].set(handle2);

    mSurfaces[0]->mCtrlPts[6].set((mSurfaces[0]->mCtrlPts[10] + mSurfaces[0]->mCtrlPts[5]) - mVertices[1]);
    mSurfaces[1]->mCtrlPts[6].set((mSurfaces[1]->mCtrlPts[10] + mSurfaces[1]->mCtrlPts[5]) - mVertices[2]);
    mSurfaces[2]->mCtrlPts[6].set((mSurfaces[2]->mCtrlPts[10] + mSurfaces[2]->mCtrlPts[5]) - mVertices[3]);
    mSurfaces[3]->mCtrlPts[6].set((mSurfaces[3]->mCtrlPts[10] + mSurfaces[3]->mCtrlPts[5]) - mVertices[4]);
    mSurfaces[4]->mCtrlPts[6].set((mSurfaces[4]->mCtrlPts[10] + mSurfaces[4]->mCtrlPts[5]) - mVertices[2]);
    mSurfaces[5]->mCtrlPts[6].set((mSurfaces[5]->mCtrlPts[10] + mSurfaces[5]->mCtrlPts[5]) - mVertices[1]);
    mSurfaces[6]->mCtrlPts[6].set((mSurfaces[6]->mCtrlPts[10] + mSurfaces[6]->mCtrlPts[5]) - mVertices[4]);
    mSurfaces[7]->mCtrlPts[6].set((mSurfaces[7]->mCtrlPts[10] + mSurfaces[7]->mCtrlPts[5]) - mVertices[3]);

    mSurfaces[0]->mCtrlPts[7].set((mSurfaces[0]->mCtrlPts[11] + mSurfaces[0]->mCtrlPts[8]) - mVertices[2]);
    mSurfaces[1]->mCtrlPts[7].set((mSurfaces[1]->mCtrlPts[11] + mSurfaces[1]->mCtrlPts[8]) - mVertices[3]);
    mSurfaces[2]->mCtrlPts[7].set((mSurfaces[2]->mCtrlPts[11] + mSurfaces[2]->mCtrlPts[8]) - mVertices[4]);
    mSurfaces[3]->mCtrlPts[7].set((mSurfaces[3]->mCtrlPts[11] + mSurfaces[3]->mCtrlPts[8]) - mVertices[1]);
    mSurfaces[4]->mCtrlPts[7].set((mSurfaces[4]->mCtrlPts[11] + mSurfaces[4]->mCtrlPts[8]) - mVertices[1]);
    mSurfaces[5]->mCtrlPts[7].set((mSurfaces[5]->mCtrlPts[11] + mSurfaces[5]->mCtrlPts[8]) - mVertices[4]);
    mSurfaces[6]->mCtrlPts[7].set((mSurfaces[6]->mCtrlPts[11] + mSurfaces[6]->mCtrlPts[8]) - mVertices[3]);
    mSurfaces[7]->mCtrlPts[7].set((mSurfaces[7]->mCtrlPts[11] + mSurfaces[7]->mCtrlPts[8]) - mVertices[2]);
}

void OctahedronBezierSurface::draw(TypedDraw& rDraw) const {
    for (s32 idx = 0; idx < 8; idx++) {
        mSurfaces[idx]->draw(rDraw);
    }
}
