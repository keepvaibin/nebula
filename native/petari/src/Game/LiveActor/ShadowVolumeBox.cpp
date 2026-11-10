#include "Game/LiveActor/ShadowVolumeBox.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ShadowController.hpp"
#include "Game/LiveActor/ShadowDrawer.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_makeVertexBuffer_f1, "makeVertexBuffer.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_makeVertexBuffer_f2, "makeVertexBuffer.f2", 2.0f);

ShadowVolumeBox::ShadowVolumeBox() : ShadowVolumeDrawer("影描画[ボリュームボックス]"), mSize(100.0f, 100.0f, 100.0f) {
    for (u32 idx = 0; idx < ARRAY_SIZE(mPoints); idx++) {
        mPoints[idx].zero();
    }
}

void ShadowVolumeBox::setSize(const Vec3f& rSize) {
    mSize.set(rSize);
}

void ShadowVolumeBox::loadModelDrawMtx(TypedDraw& rDraw) const {
    rDraw.setWorld(nullptr);
    makeVertexBuffer();
}

void ShadowVolumeBox::makeVertexBuffer() const {
    MtxPtr mtx;
    ShadowController* controller = getController();
    mtx = controller->mHostMtx;
    Vec3f side(mtx[0][0], mtx[1][0], mtx[2][0]);
    Vec3f up(mtx[0][1], mtx[1][1], mtx[2][1]);
    Vec3f front(mtx[0][2], mtx[1][2], mtx[2][2]);

    Vec3f pos, dir;
    calcBaseDropPosition(&pos);
    controller->getDropDir(&dir);

    bool dirRight = dir.dot(side) >= 0.0f;
    bool dirUp = dir.dot(up) >= 0.0f;
    bool dirFront = dir.dot(front) >= 0.0f;
    mVariant = (dirRight ? 1u : 0u) | (dirUp ? 2u : 0u) | (dirFront ? 4u : 0u);

    Vec3f size = mSize;
    if (controller->isFollowHostScale()) {
        Vec3f scale(controller->getHost()->mScale);
        scale /= cNum_makeVertexBuffer_f1;
        size *= scale;
    } else {
        size /= cNum_makeVertexBuffer_f2;
    }

    side.scale(size.x);
    up.scale(size.y);
    front.scale(size.z);

    dir.scale(calcBaseDropLength());
    mPoints[0].set(pos + side + up + front);
    mPoints[1].set(pos + side + up - front);
    mPoints[2].set(pos + side - up + front);
    mPoints[3].set(pos + side - up - front);
    mPoints[4].set(pos - side + up + front);
    mPoints[5].set(pos - side + up - front);
    mPoints[6].set(pos - side - up + front);
    mPoints[7].set(pos - side - up - front);

    if (dirRight) {
        if (dirUp) {
            if (dirFront) {
                mPoints[12].set(mPoints[6] + dir);
                mPoints[13].set(mPoints[1]);
            } else {
                mPoints[12].set(mPoints[0]);
                mPoints[13].set(mPoints[7] + dir);
            }

            mPoints[8].set(mPoints[4] + dir);
            mPoints[9].set(mPoints[5] + dir);
            mPoints[10].set(mPoints[2]);
            mPoints[11].set(mPoints[3]);
        } else {
            if (dirFront) {
                mPoints[12].set(mPoints[4] + dir);
                mPoints[13].set(mPoints[3]);
            } else {
                mPoints[12].set(mPoints[2]);
                mPoints[13].set(mPoints[5] + dir);
            }

            mPoints[8].set(mPoints[0]);
            mPoints[9].set(mPoints[1]);
            mPoints[10].set(mPoints[6] + dir);
            mPoints[11].set(mPoints[7] + dir);
        }

        mPoints[0] += dir;
        mPoints[1] += dir;
        mPoints[2] += dir;
        mPoints[3] += dir;
    } else {
        if (dirUp) {
            if (dirFront) {
                mPoints[12].set(mPoints[2] + dir);
                mPoints[13].set(mPoints[5]);
            } else {
                mPoints[12].set(mPoints[4]);
                mPoints[13].set(mPoints[3] + dir);
            }

            mPoints[8].set(mPoints[0] + dir);
            mPoints[9].set(mPoints[1] + dir);
            mPoints[10].set(mPoints[6]);
            mPoints[11].set(mPoints[7]);
        } else {
            if (dirFront) {
                mPoints[12].set(mPoints[0] + dir);
                mPoints[13].set(mPoints[7]);
            } else {
                mPoints[12].set(mPoints[6]);
                mPoints[13].set(mPoints[1] + dir);
            }

            mPoints[8].set(mPoints[4]);
            mPoints[9].set(mPoints[5]);
            mPoints[10].set(mPoints[2] + dir);
            mPoints[11].set(mPoints[3] + dir);
        }

        mPoints[4] += dir;
        mPoints[5] += dir;
        mPoints[6] += dir;
        mPoints[7] += dir;
    }
}

void ShadowVolumeBox::drawShape(TypedDraw& rDraw) const {
    VertexP* vtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 14);
    {
        *vtx++ = {mPoints[0].x, mPoints[0].y, mPoints[0].z};
        *vtx++ = {mPoints[1].x, mPoints[1].y, mPoints[1].z};
        *vtx++ = {mPoints[2].x, mPoints[2].y, mPoints[2].z};
        *vtx++ = {mPoints[3].x, mPoints[3].y, mPoints[3].z};
        *vtx++ = {mPoints[10].x, mPoints[10].y, mPoints[10].z};
        *vtx++ = {mPoints[11].x, mPoints[11].y, mPoints[11].z};
        *vtx++ = {mPoints[6].x, mPoints[6].y, mPoints[6].z};
        *vtx++ = {mPoints[7].x, mPoints[7].y, mPoints[7].z};
        *vtx++ = {mPoints[4].x, mPoints[4].y, mPoints[4].z};
        *vtx++ = {mPoints[5].x, mPoints[5].y, mPoints[5].z};
        *vtx++ = {mPoints[8].x, mPoints[8].y, mPoints[8].z};
        *vtx++ = {mPoints[9].x, mPoints[9].y, mPoints[9].z};
        *vtx++ = {mPoints[0].x, mPoints[0].y, mPoints[0].z};
        *vtx++ = {mPoints[1].x, mPoints[1].y, mPoints[1].z};
    }

    vtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_FAN, 8);
    {
        *vtx++ = {mPoints[12].x, mPoints[12].y, mPoints[12].z};
        *vtx++ = {mPoints[0].x, mPoints[0].y, mPoints[0].z};
        *vtx++ = {mPoints[2].x, mPoints[2].y, mPoints[2].z};
        *vtx++ = {mPoints[10].x, mPoints[10].y, mPoints[10].z};
        *vtx++ = {mPoints[6].x, mPoints[6].y, mPoints[6].z};
        *vtx++ = {mPoints[4].x, mPoints[4].y, mPoints[4].z};
        *vtx++ = {mPoints[8].x, mPoints[8].y, mPoints[8].z};
        *vtx++ = {mPoints[0].x, mPoints[0].y, mPoints[0].z};
    }

    vtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_FAN, 8);
    {
        *vtx++ = {mPoints[13].x, mPoints[13].y, mPoints[13].z};
        *vtx++ = {mPoints[1].x, mPoints[1].y, mPoints[1].z};
        *vtx++ = {mPoints[9].x, mPoints[9].y, mPoints[9].z};
        *vtx++ = {mPoints[5].x, mPoints[5].y, mPoints[5].z};
        *vtx++ = {mPoints[7].x, mPoints[7].y, mPoints[7].z};
        *vtx++ = {mPoints[11].x, mPoints[11].y, mPoints[11].z};
        *vtx++ = {mPoints[3].x, mPoints[3].y, mPoints[3].z};
        *vtx++ = {mPoints[1].x, mPoints[1].y, mPoints[1].z};
    }
}
