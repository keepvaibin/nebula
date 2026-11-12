#include "Game/AreaObj/MercatorTransformCube.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/MapObj/MapPartsRailMover.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    MercatorTransformCube* getMercatorCube() {
        return static_cast< MercatorTransformCube* >(MR::getAreaObjManager("MercatorCube")->getAreaObj(0));
    }

    void calcRailPosForMercator(Vec3f* pPos, const LiveActor* pActor, f32 coord) {
        Pos3f rotation;
        Vec3f railPos;
        MR::calcRailPosAtCoord(&railPos, pActor, coord);
        getMercatorCube()->convertTransAndRotate(pPos, &rotation, railPos, true);
    }

};  // namespace

MercatorTransformCube::MercatorTransformCube(int formType, const char* pName) : AreaObj(formType, pName) {
    lumacore::setClass(this, lumacore::kClassId< MercatorTransformCube >);
    mPlacementMtx.identity();
}

void MercatorTransformCube::init(const TableIter& rIter) {
    AreaObj::init(rIter);

    mPlacementMtx.setInline(*MR::getZonePlacementMtx(rIter));
}

f32 MercatorTransformCube::calcLocalHeight(const Vec3f& rPos) const {
    Vec3f localPos;
    MR::calcCubeLocalPos(&localPos, this, rPos);
    return localPos.y;
}

void MercatorTransformCube::convertTransAndRotate(Vec3f* pPos, Pos3f* pRotation, const Vec3f& rPos, bool applyInverse) const {
    Vec2f uv;
    calcUV(&uv, rPos);
    Vec3f point;
    f32 radius = getSphereRadius() * MR::cos(uv.y);
    point.x = radius * MR::sin(uv.x);
    point.y = getSphereRadius() * MR::sin(uv.y);
    point.z = radius * MR::cos(uv.x);
    Vec3f offset(point);
    MR::normalize(&offset);
    offset *= calcLocalHeight(rPos);
    point.add(offset);
    Vec3f up(0.0f, 1.0f, 0.0f);
    Vec3f front;
    if (MR::isSameDirection(point, up, 0.01f)) {
        front.set(0.0f, 0.0f, MR::sign(point.y));
    } else {
        Vec3f side;
        side.cross(point, up);
        front.cross(point, side);
    }

    MR::makeMtxUpFront(pRotation, point, front);
    mPlacementMtx.mult(point, point);
    Mtx34f rotation(mPlacementMtx);
    rotation.mMtx[0][3] = 0.0f;
    rotation.mMtx[1][3] = 0.0f;
    rotation.mMtx[2][3] = 0.0f;
    if (applyInverse) {
        Mtx34f inverse;
        inverse.invert(rotation);
        pRotation->concat(*pRotation, inverse);
    }

    pRotation->concat(rotation, *pRotation);
    pPos->set(point);
}

f32 MercatorTransformCube::getSphereRadius() const {
    if (mObjArg0 == -1) {
        return 3000.0f;
    }

    return mObjArg0;
}

void MercatorTransformCube::calcUV(Vec2f* pUV, const Vec3f& rPos) const {
    Vec3f boxSize;
    calcLocalBoxSize(&boxSize);
    boxSize *= 0.5f;

    Vec3f localPos;
    MR::calcCubeLocalPos(&localPos, this, rPos);

    pUV->x = PI * localPos.x / boxSize.x;
    pUV->y = HALF_PI * -localPos.z / boxSize.z;
}

void MercatorTransformCube::calcLocalBoxSize(Vec3f* pPos) const {
    Box3f* box = MR::getCubeLocalBox(this);

    Vec3f max;
    Vec3f min;

    min.set(box->i);
    max.set(box->f);

    pPos->sub(max, min);
}

namespace MR {
    void convertMercatorPlaneToSphereTransAndRotate(Vec3f* pPos, Pos3f* pRotation, const Vec3f& rPos, bool applyInverse) {
        ::getMercatorCube()->convertTransAndRotate(pPos, pRotation, rPos, applyInverse);
    }

    void convertMercatorPlaneToSphereTransAndRotate(Vec3f* pPos, Vec3f* pRotation, const Vec3f& rPos, bool applyInverse) {
        Pos3f rotation;
        ::getMercatorCube()->convertTransAndRotate(pPos, &rotation, rPos, applyInverse);
        Vec3f angles;
        rotation.getEulerXYZ(angles);
        pRotation->set< f32 >(57.29577951308232f * angles.x, 57.29577951308232f * angles.y, 57.29577951308232f * angles.z);
    }

    void initDefaultPosForMercator(LiveActor* pActor, const TableIter& rIter, bool applyInverse) {
        getPlacementTrans(rIter, &pActor->mPosition);
        Vec3f angles;
        getPlacementRotate(rIter, &angles);
        Pos3f rotation;
        makeMtxRotate(rotation, angles.x, angles.y, angles.z);
        Vec3f pos;
        Pos3f sphereRotation;
        ::getMercatorCube()->convertTransAndRotate(&pos, &sphereRotation, pActor->mPosition, applyInverse);
        rotation.concat(sphereRotation, rotation);
        rotation.getEulerXYZ(angles);
        angles.set< f32 >(57.29577951308232f * angles.x, 57.29577951308232f * angles.y, 57.29577951308232f * angles.z);
        pActor->mPosition.set(pos);
        pActor->mRotation.set(angles);
        getPlacementScale(rIter, &pActor->mScale);
    }

    void calcNearestRailPosForMercator(Vec3f* pPos, const LiveActor* pActor, f32 step) {
        pPos->zero();
        f32 nearest = 3.4028234663852886e38f;
        for (f32 coord = 0.0f; coord <= getRailTotalLength(pActor); coord += step) {
            Vec3f pos;
            ::calcRailPosForMercator(&pos, pActor, coord);
            f32 distance = pos.distance(pActor->mPosition);
            if (nearest > distance) {
                *pPos = pos;
                nearest = distance;
            }
        }
    }

    void calcRailClippingInfoForMercator(Vec3f* pCenter, f32* pRadius, LiveActor* pActor, f32 step, f32 padding) {
        f32 length = getRailTotalLength(pActor);
        s32 count = length / step;
        Vec3f start;
        ::calcRailPosForMercator(&start, pActor, 0.0f);
        Vec3f end;
        ::calcRailPosForMercator(&end, pActor, length);
        Box3f box;
        box.set(Vec3f(min(start.x, end.x), min(start.y, end.y), min(start.z, end.z)),
                Vec3f(max(start.x, end.x), max(start.y, end.y), max(start.z, end.z)));
        for (s32 i = 1; i < count; i++) {
            Vec3f pos;
            ::calcRailPosForMercator(&pos, pActor, i * step);
            box.extend(pos);
        }

        Vec3f pad(padding);
        box.i.sub(pad);
        box.f.add(pad);
        box.getCenter(pCenter);
        Vec3f size;
        size.sub(box.f, box.i);
        *pRadius = 0.5f * size.length();
    }

    void initAndSetRailClippingForMercator(Vec3f* pCenter, LiveActor* pActor, f32 step, f32 padding) {
        f32 radius = 0.0f;
        calcRailClippingInfoForMercator(pCenter, &radius, pActor, step, padding);
        setClippingTypeSphere(pActor, radius, pCenter);
    }

    f32 calcRailTotalLengthForMercator(const LiveActor* pActor, f32 step) {
        f32 length = getRailTotalLength(pActor);
        s32 count = length / step;
        Vec3f points[2];
        ::calcRailPosForMercator(&points[0], pActor, 0.0f);
        f32 total = 0.0f;
        for (s32 i = 1; i <= count; i++) {
            ::calcRailPosForMercator(&points[i % 2], pActor, length * i / count);
            total += points[0].distance(points[1]);
        }

        return total;
    }

    void getDivideMercatorRailPosition(DivideMercatorRailPosInfo* pInfo, const LiveActor* pActor, u32 count, f32 tolerance, u32 maxIterations) {
        if (count <= 1) {
            Vec3f pos;
            ::calcRailPosForMercator(&pos, pActor, 0.0f);
            pInfo->setPosition(0, pos);
            return;
        }

        f32 total = calcRailTotalLengthForMercator(pActor, 100.0f);
        f32 railSpacing;
        f32 spacing;
        if (isLoopRail(pActor)) {
            spacing = total / count;
        } else {
            spacing = total / (count - 1);
        }

        f32 railLength = getRailTotalLength(pActor);
        if (isLoopRail(pActor)) {
            railSpacing = railLength / count;
        } else {
            railSpacing = railLength / (count - 1);
        }

        f32 distance = 0.0f;
        f32 targetDistance = spacing;
        f32 lower = 0.0f;
        f32 coord = railSpacing;
        f32 upper = 2.0f * railSpacing;
        Vec3f previous;
        ::calcRailPosForMercator(&previous, pActor, 0.0f);
        pInfo->setPosition(0, previous);
        for (s32 i = 1; i < count; i++) {
            bool bracketed = false;
            u32 iteration = 0;
            while (iteration < maxIterations) {
                Vec3f pos;
                ::calcRailPosForMercator(&pos, pActor, coord);
                f32 segment = previous.distance(pos);
                f32 remaining = targetDistance - (distance + segment);
                if (MR::abs(remaining) < tolerance || iteration == maxIterations) {
                    pInfo->setPosition(i, pos);
                    previous = pos;
                    lower = coord;
                    coord += railSpacing;
                    distance += segment;
                    upper = coord + railSpacing;
                    targetDistance = spacing * (i + 1);
                    if (targetDistance < distance) {
                        targetDistance = distance;
                    }

                    break;
                }

                if (remaining > 0.0f) {
                    lower = coord;
                    if (!bracketed) {
                        upper += 2.0f * railSpacing;
                    }

                    coord = 0.5f * (coord + upper);
                    distance += segment;
                    previous = pos;
                } else {
                    if (!bracketed) {
                        bracketed = true;
                    }

                    upper = coord;
                    coord = 0.5f * (lower + coord);
                }

                if (bracketed) {
                    iteration++;
                }
            }
        }
    }

    MapPartsRailMover* createMapPartsRailMoverForMercator(LiveActor* pActor, const TableIter& rIter, bool setRailPos) {
        Vec3f savedPos(pActor->mPosition);
        Vec3f placementPos;
        getPlacementTrans(rIter, &placementPos);
        pActor->mPosition.set(placementPos);
        MapPartsRailMover* pMover = new MapPartsRailMover(pActor);
        pMover->init(rIter);
        if (setRailPos) {
            ::calcRailPosForMercator(&pActor->mPosition, pActor, getRailCoord(pActor));
        } else {
            pActor->mPosition.set(savedPos);
        }

        return pMover;
    }
};  // namespace MR
