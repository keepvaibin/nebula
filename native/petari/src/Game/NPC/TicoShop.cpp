#include "Game/NPC/TicoShop.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/MapObj/BenefitItemLifeUp.hpp"
#include "Game/MapObj/BenefitItemOneUp.hpp"
#include "Game/NPC/NPCActorItem.hpp"
#include "Game/NPC/TalkMessageFunc.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_kill_f1, "kill.f1", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f2, "kill.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 350.0f);

namespace {
    NEBULA_TUNABLE(s32, sStarPieceNeed, 30);
    NEBULA_TUNABLE(f32, sMouthSize, 30.0f);
    NEBULA_TUNABLE(f32, sSensorSize, 100.0f);
    NEBULA_TUNABLE(f32, sSensorOffset, 100.0f);
    NEBULA_TUNABLE(f32, sShadowLength, 500.0f);
};  // namespace

namespace NrvTicoShop {
    NEW_NERVE(TicoShopNrvDemo, TicoShop, Demo);
};  // namespace NrvTicoShop

TicoShop::TicoShop(const char* pName) : TicoComet(pName), mOneUp(), mLifeUp(), mChoseOneUp(), mChoseLifeUp() {
    lumacore::setClass(this, lumacore::kClassId< TicoShop >);
}

void TicoShop::kill() {
    if (mOneUp != nullptr && mChoseOneUp) {
        MR::appearKinokoOneUpPop(mOneUp, getBaseMtx(), cNum_kill_f1);
    }

    if (mLifeUp != nullptr && mChoseLifeUp) {
        MR::appearKinokoSuper(mLifeUp, getBaseMtx(), cNum_kill_f2);
    }

    NPCActor::kill();
}

void TicoShop::init(const TableIter& rIter) {
    NPCActorCaps caps("TicoShop");
    NPCActorItem item("TicoShop");
    caps.setDefault();
    caps.mSensorSize = ::sSensorSize;
    caps.mSensorOffset.y = ::sSensorOffset;
    caps.mUseShadow = true;
    caps.mSoundSize = 6;
    caps.mSensorMax = 2;
    caps.mSensorJoint = "Center";
    caps.mPointerJoint = "Center";
    initialize(rIter, caps);
    MR::addHitSensorAtJointEye(this, "Mouth", "Mouth", 8, ::sMouthSize, Vec3f(0.0f, 0.0f, 0.0f));
    MR::getNPCItemData(&item, 0);
    equipment(item, false);

    if (mGoods0 != nullptr) {
        MR::startAction(mGoods0, "Shake");
    }

    if (mGoods1 != nullptr) {
        MR::startAction(mGoods1, "Shake");
    }

    s32 starPieceNeed = ::sStarPieceNeed;
    MR::getPlacementArg0NoInit(rIter, &starPieceNeed);
    initStarPiece(starPieceNeed);
    initMessage("Common_TicoShop000");
    MR::setMessageArg(mMsgCtrl, mStarPieceNeed);
    MR::registerBranchFunc(getMsgCtrl(), TalkMessageFunc(this, &TicoShop::branchFunc));
    MR::registerEventFunc(getMsgCtrl(), TalkMessageFunc(this, &TicoShop::eventFunc));
    MR::setDistanceToTalk(mMsgCtrl, cNum_init_f1);
    mMetaNerve = GET_NERVE(TicoShop, TicoShopNrvDemo);
    mOneUp = MR::createKinokoOneUp();
    mLifeUp = MR::createKinokoSuper();
    MR::setShadowDropLength(mOneUp, nullptr, ::sShadowLength);
    MR::setShadowDropLength(mLifeUp, nullptr, ::sShadowLength);
    MR::startBva(this, "Small0");
    MR::startBrk(this, "Metamorphosis");
    MR::setBrkFrameAndStop(this, 0.0f);
    MR::setClippingFar200m(this);
    TicoEat::init(rIter);
    mLightColor.set(0, 0xE6, 0xFF, 0xFF);
}

bool TicoShop::branchFunc(u32) {
    return hasEnoughStarPiece();
}

bool TicoShop::eventFunc(u32 event) {
    // Events 0-2 are the eating Tico's; 3 and 4 the two purchases.
    if (event < 3) {
        return TicoComet::eventFunc(event);
    }

    if (event == 3) {
        mChoseLifeUp = true;
    }

    if (event == 4) {
        mChoseOneUp = true;
    }

    return true;
}

void TicoShop::startReactionSound() {
    if (mIsTrampledStart) {
        MR::startSound(this, "SE_SM_NPC_TRAMPLED");
        MR::startSound(this, "SE_SV_TICOSHOP_TRAMPLED");
    }

    if (isPointingSe()) {
        MR::startPointerHitSound();
        MR::startSound(this, "SE_SV_TICOSHOP_POINT");
    }

    if (mIsSpinAttackedStart) {
        MR::startSound(this, "SE_SM_TICOSHOP_SPIN");
    }

    if (mIsReactionStart) {
        MR::startSound(this, "SE_SV_TICOSHOP_STAR_PIECE_HIT");
    }
}

void TicoShop::exeDemo() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Demo");
        MR::startSound(this, "SE_SM_TICOFAT_META_ITEM");
        MR::startSound(this, "SE_SM_TICOFAT_META");
    }

    if (MR::isBckStopped(this)) {
        MR::startSound(this, "SE_SM_METAMORPHOSE_SMOKE");
        kill();
    }
}
