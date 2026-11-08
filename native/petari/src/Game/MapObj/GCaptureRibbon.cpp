#include "Game/MapObj/GCaptureRibbon.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/ActorLightCtrl.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <nebula_geometry.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>

GCaptureRibbon::GCaptureRibbon(const char* pName) : LiveActor(pName), mTexture(), mPointNum() {
}

void GCaptureRibbon::draw() const {
    if (mPointNum >= 2) {
        TDDraw::cameraInit3D();
        SiteMaterial material;
        TDDraw::setupNative(&material, 1, 2, 0);
        f32 width = calcLineWidth();
        material.setTexture(0, mTexture);
        material.setDepth(true, NEBULA_COMPARE_LEQUAL, false);

        TypedDraw site = TDDraw::site(material, 0);
    VertexPT* vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 2 * mPointNum);
        {
            for (s32 i = 0; i < mPointNum; i++) {
                f32 v2 = i / (mPointNum - 1.0f);

                Vec3f v1 = mUps[i] * width;

                *vtx++ = {v1.x + mPoints[i].x, v1.y + mPoints[i].y, v1.z + mPoints[i].z, 0.0f, v2};
                *vtx++ = {mPoints[i].x - v1.x, mPoints[i].y - v1.y, mPoints[i].z - v1.z, 1.0f, v2};
            }
        }

        vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 2 * mPointNum);
        {
            for (s32 j = 0; j < mPointNum; j++) {
                f32 v2 = j / (mPointNum - 1.0f);

                Vec3f v1 = mSides[j] * width;

                *vtx++ = {v1.x + mPoints[j].x, v1.y + mPoints[j].y, v1.z + mPoints[j].z, 0.0f, v2};
                *vtx++ = {mPoints[j].x - v1.x, mPoints[j].y - v1.y, mPoints[j].z - v1.z, 1.0f, v2};
            }
        }
    }
}

void GCaptureRibbon::reset() {
    mPointNum = 0;
}

void GCaptureRibbon::lengthen(const Vec3f& a1, const Vec3f& a2) {
    mPoints[0].set(a1);
    mPoints[1].set(a2);
    mPointNum = 2;
    updateAxis();
}

void GCaptureRibbon::shorten(const Vec3f& a1, const Vec3f& a2) {
    mPoints[0].set(a1);
    mPoints[1].set(a2);
    mPointNum = 2;
    updateAxis();
}

void GCaptureRibbon::updateAxis() {
    Quat4f quat;
    if (mPointNum >= 2) {
        Vec3f v1 = mPoints[1] - mPoints[0];
        MR::normalizeOrZero(&v1);

        if (MR::isNearZero(v1)) {
            mSides[0] = Vec3f(1, 0, 0);
            mUps[0] = Vec3f(0, 1, 0);
            v1 = Vec3f(0, 0, 1);
        } else {
            MR::makeAxisVerticalZX(&mSides[0], v1);
            mUps[0].cross(v1, mSides[0]);
        }

        for (s32 i = 1; i < mPointNum - 1; i++) {
            Vec3f v2 = mPoints[i + 1] - mPoints[i];
            if (MR::isNearZero(v2)) {
                mSides[i] = mSides[i - 1];
                mUps[i] = mUps[i - 1];
            } else {
                MR::normalize(&v2);
                if (v1.dot(v2) < 0.0f) {
                    v2 = -v2;
                }
                quat.setRotate(v1, v2);
                quat.transform(mSides[i - 1], mSides[i]);
                quat.transform(mUps[i - 1], mUps[i]);
                v1.set(v2);
            }
        }
        mSides[mPointNum - 1] = mSides[mPointNum - 2];
        mUps[mPointNum - 1] = mUps[mPointNum - 2];
    }
}

f32 GCaptureRibbon::calcLineWidth() const {
    f32 normalize = MR::normalize(mPoints[0].distance(mPoints[mPointNum - 1]), 500.0f, 2000.0f);
    return (20.0f * normalize) + (90.0f * (1.0f - normalize));
}

GCaptureRibbon::~GCaptureRibbon() {
}

void GCaptureRibbon::init(const TableIter& rIter) {
    mTexture = new JUTTexture(MR::loadTexFromArc("GCaptureRibbon", "GCaptureRibbon.bti"));
    MR::invalidateClipping(this);
    makeActorAppeared();
}
