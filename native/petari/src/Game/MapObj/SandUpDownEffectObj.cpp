#include "Game/MapObj/SandUpDownEffectObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/TriangleFilter.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 100.0f);

namespace {
    struct LineCheckParam {
        const char* mObjName;
        f32 mLineOffset;
        f32 mLineLength;
    };

    static const LineCheckParam sLineCheckParamTable[] = {
        {"SandUpDownColumnAEffect", 800.0f, 1600.0f}, {"SandUpDownKGBaseEffect", 5000.0f, 2000.0f}, {"SandUpDownTowerTopEffect", 3500.0f, 3550.0f},
        {"UFOSandObstacleAEffect", 400.0f, 800.0f},   {"UFOSandObstacleBEffect", 200.0f, 400.0f},   {"UFOSandObstacleCEffect", 400.0f, 800.0f},
    };
};  // namespace

namespace {
    const LineCheckParam* findLineCheckParamData(const char* pObjName) {
        for (u32 i = 0; i < ARRAY_SIZE(::sLineCheckParamTable); i++) {
            const LineCheckParam* param = &::sLineCheckParamTable[i];

            if (MR::isEqualString(pObjName, param->mObjName)) {
                return param;
            }
        }

        return nullptr;
    }

    bool isNotGroundCodeNoStampSand(const Triangle* pTriangle) {
        return !MR::isGroundCodeNoStampSand(pTriangle);
    }
};  // namespace

namespace NrvSandUpDownEffectObj {
    NEW_NERVE(SandUpDownEffectObjNrvWait, SandUpDownEffectObj, Wait);
    NEW_NERVE(SandUpDownEffectObjNrvFallingRock, SandUpDownEffectObj, FallingRock);
    NEW_NERVE(SandUpDownEffectObjNrvFallingRockAndSmoke, SandUpDownEffectObj, FallingRockAndSmoke);
    NEW_NERVE(SandUpDownEffectObjNrvBurySmokePrepare, SandUpDownEffectObj, BurySmokePrepare);
    NEW_NERVE(SandUpDownEffectObjNrvBurySmoke, SandUpDownEffectObj, BurySmoke);
    NEW_NERVE(SandUpDownEffectObjNrvBuryEnd, SandUpDownEffectObj, BuryEnd);
};  // namespace NrvSandUpDownEffectObj

SandUpDownEffectObj::SandUpDownEffectObj(const char* pName) : LiveActor(pName), mLineOffset(), mLineLength(), mClippingCenter(0.0f, 0.0f, 0.0f), mTriFilter() {
    mEffectMtx.identity();
}

void SandUpDownEffectObj::init(const TableIter& rIter) {
    const char* objName;

    MR::initDefaultPos(this, rIter);
    MR::connectToSceneMapObjMovement(this);
    MR::getObjectName(&objName, rIter);
    initEffectKeeper(0, objName);
    MR::makeMtxTR(mEffectMtx.toMtxPtr(), this);
    MR::setEffectHostMtx(this, "BurySmoke", mEffectMtx.toMtxPtr());

    Vec3f yDir;
    mEffectMtx.getYDir(yDir);
    mGravity.negate(yDir);

    MR::useStageSwitchReadA(this, rIter);
    MR::useStageSwitchReadB(this, rIter);

    if (MR::isValidSwitchA(this)) {
        MR::listenStageSwitchOnA(this, MR::Functor(this, &SandUpDownEffectObj::startMoveDown));
    }

    MR::listenStageSwitchOnB(this, MR::Functor(this, &SandUpDownEffectObj::startMoveUp));

    // An object name the table lacks (a mod's) takes the first row.
    const LineCheckParam* pLineParam = ::findLineCheckParamData(objName);
    if (pLineParam == nullptr) {
        pLineParam = &::sLineCheckParamTable[0];
    }
    mLineOffset = pLineParam->mLineOffset;
    mLineLength = pLineParam->mLineLength;
    mTriFilter = MR::createTriangleFilterFunc(&::isNotGroundCodeNoStampSand);
    mClippingCenter.scaleAdd(mLineOffset - mLineLength * cNum_init_f1, yDir, mPosition);

    MR::setClippingTypeSphere(this, mLineLength * cNum_init_f2 + cNum_init_f3, &mClippingCenter);
    initNerve(GET_NERVE(SandUpDownEffectObj, SandUpDownEffectObjNrvWait));
    makeActorAppeared();
}

void SandUpDownEffectObj::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::deleteEffectAll(this);
    }
}

void SandUpDownEffectObj::exeFallingRock() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "FallingRock");
    }

    bool isOnSwitchAOnly = MR::isOnSwitchA(this) && !MR::isOnSwitchB(this);

    if (isOnSwitchAOnly) {
        MR::deleteEffect(this, "FallingRock");
        setNerve(GET_NERVE(SandUpDownEffectObj, SandUpDownEffectObjNrvWait));
    } else if (checkSandHit()) {
        setNerve(GET_NERVE(SandUpDownEffectObj, SandUpDownEffectObjNrvFallingRockAndSmoke));
    }
}

void SandUpDownEffectObj::exeFallingRockAndSmoke() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "BurySmoke");
    }

    if (!checkSandHit()) {
        MR::deleteEffect(this, "BurySmoke");
        setNerve(GET_NERVE(SandUpDownEffectObj, SandUpDownEffectObjNrvFallingRock));
    } else {
        bool isOnSwitchAOnly = MR::isOnSwitchA(this) && !MR::isOnSwitchB(this);

        if (isOnSwitchAOnly) {
            MR::deleteEffectAll(this);
            setNerve(GET_NERVE(SandUpDownEffectObj, SandUpDownEffectObjNrvWait));
        }
    }
}

void SandUpDownEffectObj::exeBurySmokePrepare() {
    if (checkSandHit()) {
        setNerve(GET_NERVE(SandUpDownEffectObj, SandUpDownEffectObjNrvBurySmoke));
    }
}

void SandUpDownEffectObj::exeBurySmoke() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "BurySmoke");
    }

    if (!checkSandHit()) {
        MR::deleteEffect(this, "BurySmoke");
        setNerve(GET_NERVE(SandUpDownEffectObj, SandUpDownEffectObjNrvBuryEnd));
    } else if (isUfoTop()) {
        setNerve(GET_NERVE(SandUpDownEffectObj, SandUpDownEffectObjNrvWait));
    }
}

void SandUpDownEffectObj::exeBuryEnd() {
    if (isUfoTop()) {
        setNerve(GET_NERVE(SandUpDownEffectObj, SandUpDownEffectObjNrvWait));
    }
}

void SandUpDownEffectObj::startMoveUp() {
    setNerve(GET_NERVE(SandUpDownEffectObj, SandUpDownEffectObjNrvBurySmokePrepare));
}

void SandUpDownEffectObj::startMoveDown() {
    setNerve(GET_NERVE(SandUpDownEffectObj, SandUpDownEffectObjNrvFallingRock));
}

bool SandUpDownEffectObj::checkSandHit() {
    Vec3f lineStart;
    Vec3f line;
    Vec3f hitPos;

    lineStart.scaleAdd(-mLineOffset, mGravity, mPosition);
    line.scale(mLineLength, mGravity);

    bool isHit = MR::getFirstPolyOnLineToMap(&hitPos, nullptr, lineStart, line, nullptr, mTriFilter);

    if (isHit) {
        mEffectMtx.setTrans(hitPos);
    }

    return isHit;
}

bool SandUpDownEffectObj::isUfoTop() const {
    if (MR::isValidSwitchA(this)) {
        return !MR::isOnSwitchA(this) && MR::isOnSwitchB(this);
    }

    return MR::isOnSwitchB(this);
}
