#include "Game/MapObj/MapPartsRailPosture.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvMapPartsRailPosture {
    NEW_NERVE(HostTypeDoNothing, MapPartsRailPosture, DoNothing);
    NEW_NERVE(HostTypeMove, MapPartsRailPosture, Move);
};  // namespace NrvMapPartsRailPosture

MapPartsRailPosture::MapPartsRailPosture(LiveActor* pHost) : MapPartsFunction(pHost, "姿勢制御"), mMovePosture(POSTURE_NONE) {
    lumacore::setClass(this, lumacore::kClassId< MapPartsRailPosture >);
    mPostureMtx.identity();
}

void MapPartsRailPosture::init(const TableIter& /*rIter*/) {
    MR::getMapPartsArgMovePosture(&mMovePosture, mHost);

    if (mMovePosture == POSTURE_NONE) {
        initNerve(GET_NERVE(MapPartsRailPosture, HostTypeDoNothing));
    } else {
        initNerve(GET_NERVE(MapPartsRailPosture, HostTypeMove));
    }
}

void MapPartsRailPosture::start() {
}

void MapPartsRailPosture::end() {
    mPostureMtx.identity();
    setNerve(GET_NERVE(MapPartsRailPosture, HostTypeDoNothing));
}

bool MapPartsRailPosture::isWorking() const {
    return isNerve(GET_NERVE(MapPartsRailPosture, HostTypeMove));
}

void MapPartsRailPosture::exeMove() {
    Vec3f gravity;
    if (isPostureTypeRailDirRailUseShadowGravity()) {
        MR::calcDropShadowVector(mHost, &gravity, nullptr, 0);
    } else {
        MR::calcGravityVector(mHost, &gravity, nullptr, 0);
    }

    Vec3f railDir = MR::getRailDirection(mHost);
    if (MR::isSameDirection(gravity, railDir)) {
        return;
    }

    if (!MR::isRailGoingToEnd(mHost)) {
        railDir.negate();
    }

    // Every mode stands up against gravity and faces the rail direction squared to it. Mode 2's
    // front-exactly-along-the-rail path was never reached (the test that guarded it was always true).
    Vec3f up(-gravity);
    Vec3f side;
    side.cross(up, railDir);
    MR::normalize(&side);
    Vec3f front;
    front.cross(side, up);
    MR::normalize(&front);

    Vec3f curSide;
    mPostureMtx.getXDir(curSide);
    MR::normalize(&curSide);

    if (MR::isSameDirection(curSide, front)) {
        MR::makeMtxFrontNoSupport(&mPostureMtx, front);
    } else {
        MR::makeMtxFrontUp(&mPostureMtx, front, up);
    }
}

void MapPartsRailPosture::exeDoNothing() {
}

bool MapPartsRailPosture::isPostureTypeRailDirRailUseShadowGravity() const {
    return mMovePosture == POSTURE_RAIL_DIR_SHADOW_GRAVITY;
}
