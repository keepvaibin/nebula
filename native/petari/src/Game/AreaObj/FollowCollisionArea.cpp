#include "Game/AreaObj/FollowCollisionArea.hpp"
#include "Game/AreaObj/CollisionArea.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_movement_f1, "movement.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_hitCheck_f1, "hitCheck.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_hitCheck_f2, "hitCheck.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_hitCheck_f3, "hitCheck.f3", 0.5f);

FollowCollisionArea::FollowCollisionArea(const Vec3f& rSize, MtxPtr pMatrix, f32 offset) : NameObj("追従コリジョンエリア") {
    lumacore::setClass(this, lumacore::kClassId< FollowCollisionArea >);
    mSize = rSize;
    mFollowMtx = pMatrix;
    mOffset = offset;
    MR::connectToSceneAreaObj(this);
    mOutsideAxisCount = 0;
    mInsideDepth = 0.0f;
    mPushType = -1;
    mTeresaType = 0;
    mHitRadius = 0;
    mIsTeresaPass = false;
    mPolygon = new AreaPolygon();
    mPolygon->setMtx(mFollowMtx, mSize, mOffset);
    mPolygon->initWithoutIter();
    mPolygon->validate();
}

void FollowCollisionArea::movement() {
    if (mTeresaType == -1 && MR::isPlayerTeresaDisappear()) {
        mIsTeresaPass = true;
        mPolygon->invalidate();
    } else {
        Vec3f playerPos(*MR::getPlayerCenterPos());
        f32 radius = static_cast< f32 >(mHitRadius);
        if (mHitRadius == 0) {
            radius = MR::getPlayerHitRadius();
        }

        if (mIsTeresaPass) {
            radius += cNum_movement_f1;
        }

        Vec3f hitPos;
        Vec3f hitNormal;
        if (!hitCheck(playerPos, radius, &hitPos, &hitNormal)) {
            if (mIsTeresaPass) {
                mIsTeresaPass = false;
                mPolygon->validate();
            }
        } else if (!mIsTeresaPass && mPushType == -1) {
            Vec3f push;
            if (mOutsideAxisCount == 0) {
                push = hitNormal;
                push.setLength(radius + mInsideDepth);
            } else {
                push = playerPos - hitPos;
                push.setLength(radius);
                Vec3f contact(hitPos + push);
                push = contact - playerPos;
            }

            if (push.dot(hitNormal) > 0.0f) {
                MR::pushPlayerFromArea(push);
            }
        }
    }
}

bool FollowCollisionArea::hitCheck(const Vec3f& rPos, f32 radius, Vec3f* pPoint, Vec3f* pNormal) {
    s32 surface = -1;
    Pos3f matrix;
    NebulaMtxCopy(mFollowMtx, matrix);
    MR::extractMtxTrans(mFollowMtx, &mCenter);
    Vec3f localPoint;
    Vec3f axisX;
    Vec3f axisY;
    Vec3f axisZ;
    matrix.getXDir(axisX);
    matrix.getYDir(axisY);
    matrix.getZDir(axisZ);
    mCenter.add(axisY * mOffset);

    f32 sizeX = axisX.length();
    MR::normalizeOrZero(&axisX);
    f32 sizeY = axisY.length();
    MR::normalizeOrZero(&axisY);
    f32 sizeZ = axisZ.length();
    MR::normalizeOrZero(&axisZ);
    sizeX *= cNum_hitCheck_f1 * mSize.x;
    sizeY *= cNum_hitCheck_f2 * mSize.y;
    sizeZ *= cNum_hitCheck_f3 * mSize.z;
    Vec3f expanded(sizeX + radius, sizeY + radius, sizeZ + radius);
    Vec3f relative(rPos);
    relative.sub(mCenter);
    f32 x = relative.dot(axisX);
    f32 y = relative.dot(axisY);
    f32 z = relative.dot(axisZ);
    Vec3f absolute;
    f32 absX = MR::abs(x);
    f32 absY = MR::abs(y);
    f32 absZ = MR::abs(z);
    absolute.set(absX, absY, absZ);
    if (absolute.x >= expanded.x || absolute.y >= expanded.y || absolute.z >= expanded.z) {
        return false;
    }

    u32 count = 0;
    bool outside[3] = {false, false, false};
    if (absolute.x >= sizeX) {
        outside[0] = true;
        count++;
    }

    if (absolute.y >= sizeY) {
        outside[1] = true;
        count++;
    }

    if (absolute.z >= sizeZ) {
        outside[2] = true;
        count++;
    }

    mOutsideAxisCount = count;
    Vec3f corner;
    Vec3f depth;
    if (count == 3) {
        corner.zero();
        if (x < 0.0f) {
            corner.add(-axisX * sizeX);
        } else {
            corner.add(axisX * sizeX);
        }

        if (y < 0.0f) {
            corner.add(-axisY * sizeY);
        } else {
            corner.add(axisY * sizeY);
        }

        if (z < 0.0f) {
            corner.add(-axisZ * sizeZ);
        } else {
            corner.add(axisZ * sizeZ);
        }

        if ((corner - rPos).length() >= radius) {
            return false;
        }

        pPoint->set(corner);
        pNormal->set(axisX + axisY + axisZ);
        MR::normalizeOrZero(pNormal);
        return true;
    }

    if (count == 0) {
        depth.x = sizeX - absolute.x;
        depth.y = sizeY - absolute.y;
        depth.z = sizeZ - absolute.z;
        if (depth.x < depth.y && depth.x < depth.z) {
            mInsideDepth = depth.x;
            outside[0] = true;
        } else if (depth.y < depth.x && depth.y < depth.z) {
            mInsideDepth = depth.y;
            outside[1] = true;
        } else {
            mInsideDepth = depth.z;
            outside[2] = true;
        }

        count = 1;
    }

    if (count == 2) {
        localPoint.set(x, y, z);
        pNormal->zero();
        if (outside[0]) {
            if (x < 0.0f) {
                localPoint.x = -sizeX;
                pNormal->sub(axisX);
            } else {
                localPoint.x = sizeX;
                pNormal->add(axisX);
            }
        }

        if (outside[1]) {
            if (y < 0.0f) {
                localPoint.y = -sizeY;
                pNormal->sub(axisY);
            } else {
                localPoint.y = sizeY;
                pNormal->add(axisY);
            }
        }

        if (outside[2]) {
            if (z < 0.0f) {
                localPoint.z = -sizeZ;
                pNormal->sub(axisZ);
            } else {
                localPoint.z = sizeZ;
                pNormal->add(axisZ);
            }
        }
    }

    if (count == 1) {
        if (outside[0]) {
            if (x >= 0.0f) {
                pNormal->set(axisX);
                localPoint.set(sizeX, y, z);
                surface = 0;
            } else {
                pNormal->set(-axisX);
                localPoint.set(-sizeX, y, z);
                surface = 1;
            }
        }

        if (outside[2]) {
            if (z >= 0.0f) {
                pNormal->set(axisZ);
                localPoint.set(x, y, sizeZ);
                surface = 4;
            } else {
                pNormal->set(-axisZ);
                localPoint.set(x, y, -sizeZ);
                surface = 5;
            }
        }
    }

    pPoint->set(axisX * localPoint.x + axisY * localPoint.y + axisZ * localPoint.z + mCenter);
    MR::normalizeOrZero(pNormal);
    mPolygon->setSurfaceAndSync(surface);

    return true;
}
