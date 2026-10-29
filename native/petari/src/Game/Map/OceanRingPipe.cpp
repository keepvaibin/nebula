#include "Game/Util/NativeMesh.hpp"
#include "Game/Map/OceanRingPipe.hpp"
#include "Game/Map/Flag.hpp"
#include "Game/Map/OceanRing.hpp"
#include "Game/Map/OceanRingPipeInside.hpp"
#include "Game/Map/OceanRingPipeOutside.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include <JSystem/JKernel/JKRHeap.hpp>

OceanRingPipe::~OceanRingPipe() {
}

OceanRingPipe::OceanRingPipe(const OceanRing* pRing, f32 a, f32 b)
    : LiveActor("\203\111\201\133\203\126\203\203\203\223\203\212\203\223\203\117\202\314\221\244\226\312" /* オーシャンリングの側面 */), mOceanRing(pRing), _90(), _94(), _98(), _9C(8), _A0(), _A4(), _A8(a), _AC(b), mPipeInside(),
      mPipeOutside() {
}

void OceanRingPipe::init(const JMapInfoIter& rIter) {
    MR::connectToSceneMapObjMovement(this);
    initRailRider(rIter);
    initPoints();
    MR::invalidateClipping(this);
    mPipeInside = new OceanRingPipeInside(this);
    mPipeInside->initWithoutIter();
    mPipeOutside = new OceanRingPipeOutside(this);
    mPipeOutside->initWithoutIter();

    if (MR::isName(mOceanRing, "\203\111\201\133\203\126\203\203\203\223\203\212\203\223\203\117\201\151\212\370\201\152" /* オーシャンリング（旗） */)) {
        f32 f30 = 300.0f;
        f32 f29 = 200.0f;
        f32 f28 = 500.0f;
        TVec3f vec(1.0f, 0.0f, 0.0f);
        for (s32 i = 0; i < _98; i += 7) {
            TVec3f grav(0.0f, -1.0f, 0.0f);
            MR::calcGravityVector(this, _A0[calcPointIndex(i, 0)], &grav, nullptr, 0);
            if (grav.y > -0.9f) {
                continue;
            }

            if (i & 1) {
                MR::setClippingFarMax(MR::createMapFlag("\212\370" /* 旗 */, "FlagSurfing", &_A0[calcPointIndex(i, 0)], vec, f28, f30, f29, 2, 3, -1.0f));
            } else {
                MR::setClippingFarMax(MR::createMapFlag("\212\370" /* 旗 */, "FlagSurfing", &_A0[calcPointIndex(i, _9C - 1)], vec, f28, f30, f29, 2, 3, -1.0f));
            }
        }
    }

    makeActorAppeared();
}

void OceanRingPipe::movement() {
    if (!MR::isValidMovement(this)) {
        return;
    }

    mPipeOutside->movement();
    if (WaterAreaFunction::getCameraWaterInfo()->mOceanRing == mOceanRing) {
        _90 = true;
        mPipeInside->movement();
    } else {
        _90 = false;
    }
}

void OceanRingPipe::initPoints() {
    _98 = static_cast< s32 >(MR::getRailTotalLength(this) / 300.0f) + 1;
    _94 = _98 * _9C;

    f32 length = MR::getRailTotalLength(this) / (_98 - 1);
    f32 angle = getAngle();

    _A0 = new (32) TVec3f[_94];
    _A4 = new (32) TVec3s[_94];

    s32 idx = 0;

    for (s32 i = 0; i < _98; i++) {
        TVec3f grav;
        MR::calcGravityVector(this, MR::getRailPos(this), &grav, nullptr, 0);

        TVec3f side;
        side.cross(MR::getRailDirection(this), -grav);
        MR::normalize(&side);

        TPos3f mtx;
        mtx.identity();
        mtx.setRotate(MR::getRailDirection(this), angle);

        f32 currDist = 0.0f;
        f32 nextDist = 0.0f;
        MR::calcDistanceToCurrentAndNextRailPoint(this, &currDist, &nextDist);
        f32 sumDist = currDist + nextDist;

        f32 flt = _AC / 100.0f;
        f32 flt2 = _AC / 100.0f;
        MR::getCurrentRailPointArg1NoInit(this, &flt);
        MR::getNextRailPointArg1NoInit(this, &flt2);

        f32 f3 = 1.0f;
        TVec3f v1(side);

        if (sumDist > 0.0f) {
            f3 = ((flt * nextDist + flt2 * currDist) / sumDist) / (_AC / 100.0f);
            f32 fff = flt - flt2;

            if (MR::abs(fff) > 0.0001f) {
                TVec3f v2(side);
                TVec3f v3(side);
                v2.scale(flt);
                v3.scale(flt2);

                TVec3f railPos;
                MR::calcRailPosAtCoord(&railPos, this, MR::repeat(MR::getRailCoord(this) - 300.0f, 0.0f, MR::getRailTotalLength(this)));
                v2.add(railPos);

                MR::calcRailPosAtCoord(&railPos, this, MR::repeat(MR::getRailCoord(this) + 300.0f, 0.0f, MR::getRailTotalLength(this)));
                v3.add(railPos);

                v2.sub(v3);
                v1.cross(-grav, v2);
                MR::normalize(&v1);
            }
        }

        for (s32 j = 0; j < _9C; j++) {
            TVec3f v4(side);
            v4.scale(_A8 * f3);
            v4.add(MR::getRailPos(this));

            _A0[idx].set< f32 >(v4.x, v4.y, v4.z);
            _A4[idx].set((s32)(v1.x * 32768.0f), (s32)(v1.y * 32768.0f), (s32)(v1.z * 32768.0f));  // TODO: sFloatToShortShift
            idx++;

            mtx.mult(side, side);
            mtx.mult(v1, v1);
        }

        MR::moveCoord(this, length);
    }

    // The points and normals as the renderer reads them (the normals as GX reads S16 normals: 1.14 fixed point).
    NativeMesh::sendArray(_A0, NEBULA_SHAPE_ARRAY_POSITION, _94, _A0);
    f32* pNormals = new f32[3 * _94];
    for (s32 i = 0; i < _94; i++) {
        pNormals[3 * i] = _A4[i].x / 16384.0f;
        pNormals[3 * i + 1] = _A4[i].y / 16384.0f;
        pNormals[3 * i + 2] = _A4[i].z / 16384.0f;
    }
    NativeMesh::sendArray(_A4, NEBULA_SHAPE_ARRAY_NORMAL, _94, pNormals);
    delete[] pNormals;
}
