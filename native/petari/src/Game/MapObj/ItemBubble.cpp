#include "Game/MapObj/ItemBubble.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/StarPiece.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 cHitSensorRadius = 120.f;
    const f32 cSwingRange = 120.0f;
    const f32 cShootStarSpeed = 24.0f;
};  // namespace

namespace NrvItemBubble {
    NEW_NERVE(ItemBubbleNrvWait, ItemBubble, Wait);
    NEW_NERVE(ItemBubbleNrvBreak, ItemBubble, Break);
};  // namespace NrvItemBubble

ItemBubble::ItemBubble(const char* pName) : LiveActor(pName), mCoins(), mStarPieces() {
    lumacore::setClass(this, lumacore::kClassId< ItemBubble >);
    mSwing = 0.0f;
}

void ItemBubble::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::useStageSwitchWriteDead(this, rIter);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
    }
    initModelManagerWithAnm("ItemBubble", nullptr, false);
    MR::connectToSceneMapObj(this);
    mScale.mult(1.2f);
    NebulaMtxScale(mTargetMtx, mScale.x, mScale.y, mScale.z);
    MR::setMtxTrans(mTargetMtx, mPosition.x, mPosition.y, mPosition.z);
    NebulaMtxCopy(mTargetMtx, mItemMtx);
    mBasePos = mPosition;
    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 8, mScale.x * ::cHitSensorRadius, Vec3f(0.0f, 0.0f, 0.0f));
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    initNerve(GET_NERVE(ItemBubble, ItemBubbleNrvWait));

    s32 itemType = 0;
    s32 itemCount = 1;

    if (MR::isValidInfo(rIter)) {
        MR::getPlacementArg0WithInit(rIter, &itemType);
        MR::getPlacementArg1WithInit(rIter, &itemCount);
    }

    if (itemCount < 1) {
        itemCount = 1;
    }

    mCoins = new RotPartsModel*[itemCount];

    if (itemType == 1) {
        mStarPieces = new StarPiece*[itemCount];
    }

    for (s32 i = 0; i < itemCount; i++) {
        switch (itemType) {
        case 0:
            mCoins[i] = new RotPartsModel(this, "ダミーコイン", "Coin", nullptr, MR::DrawBufferType_NoSilhouettedMapObjStrongLight, false);
            mCoins[i]->initWithoutIter();

            switch (itemCount) {
            case 1:
                mCoins[i]->initFixedPosition(mItemMtx, Vec3f(0.0f, -67.379997f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));
                continue;
            case 2:

                switch (i) {
                case 0:
                    mCoins[i]->initFixedPosition(mItemMtx, Vec3f(-55.66f, -92.290001f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));
                    continue;
                case 1:
                    mCoins[i]->initFixedPosition(mItemMtx, Vec3f(55.66f, -43.950001f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));
                    continue;
                }

                continue;
            case 3:

                switch (i) {
                case 0:
                    mCoins[i]->initFixedPosition(mItemMtx, Vec3f(0.0f, -19.040001f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));
                    continue;
                case 1:
                    mCoins[i]->initFixedPosition(mItemMtx, Vec3f(-55.66f, -130.37f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));
                    continue;
                case 2:
                    mCoins[i]->initFixedPosition(mItemMtx, Vec3f(67.379997f, -130.37f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));
                    continue;
                }

                continue;
            }

            break;
        case 1:
            mStarPieces[i] = new StarPiece("アイテムバブルピース");
            mStarPieces[i]->initWithoutIter();
            mCoins[i] = new RotPartsModel(this, "スターピース", "StarPiece", nullptr, MR::DrawBufferType_NoSilhouettedMapObj, true);
            MR::initMaterialColor0(mCoins[i], "StarPiece1_v", &mStarPieces[i]->mColor);
            MR::prepareMaterials(mCoins[i]);
            mCoins[i]->initWithoutIter();

            switch (itemCount) {
            case 1:
                mCoins[i]->initFixedPosition(mItemMtx, Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));
                continue;
            }

            break;
        default:
            mCoins[i] = nullptr;
        }
    }

    mItemCount = itemCount;
    mItemType = itemType;

    if (mItemType == 0) {
        MR::declareCoin(this, mItemCount);
    }

    if ((itemType == 0 && itemCount > 3) || (itemType == 1 && itemCount > 1)) {
        Vec3f ringOffset;
        Vec3f ringPos(0.0f, 60.0f, 0.0f);
        Vec3f zeroVelocity;
        zeroVelocity.zero();

        Rot3f ringStep;
        NebulaMtxRotRad(ringStep, (char)90, TWO_PI / static_cast< f32 >(itemCount));

        if (itemType == 0) {
            ringOffset.set< f32 >(0.0f, -60.0f, 0.0f);
        } else {
            ringOffset.zero();
        }

        for (s32 i = 0; i < itemCount; i++) {
            if (mCoins[i] == nullptr) {
                continue;
            }

            mCoins[i]->initFixedPosition(mItemMtx, ringPos + ringOffset, zeroVelocity);
            NebulaMtxMultVec(ringStep, ringPos, ringPos);
        }
    }

    mUseRail = false;

    if (MR::isValidInfo(rIter)) {
        mUseRail = MR::isConnectedWithRail(rIter);

        if (mUseRail) {
            initRailRider(rIter);
            MR::initAndSetRailClipping(&mClipCenter, this, 100.0f, 500.0f);
        }
    }

    mRailSpeed = 1.0f;
    makeActorAppeared();
    MR::setClippingFar100m(this);
}

void ItemBubble::initAfterPlacement() {
    Vec3f grav;
    MR::calcGravityVector(this, &grav, nullptr, 0);
    Pos3f mtx;
    MR::makeMtxRotate(mtx, mRotation);
    Vec3f vec;
    mtx.getZDir(vec);

    if (mUseRail) {
        MR::moveCoordAndTransToNearestRailPos(this);
    }
}

void ItemBubble::appear() {
    LiveActor::appear();
    MR::offSwitchDead(this);
}

void ItemBubble::kill() {
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    if (mItemCount != 0) {
        Vec3f grav;
        MR::calcGravityVector(this, &grav, nullptr, 0);

        for (u32 i = 0; i < mItemCount; i++) {
            if (mCoins[i] == nullptr) {
                continue;
            }

            switch (mItemType) {
            case 0:
                MR::appearCoinFix(this, getRotPartsPosition(i), 1);
                break;
            case 1:
                StarPiece* piece = getStarPiece(i);
                piece->launch(getRotPartsPosition(i), (-grav).multInLine(::cShootStarSpeed), false, false);
                MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
                break;
            }
        }
    }

    LiveActor::kill();
}

void ItemBubble::calcAndSetBaseMtx() {
    Vec3f camPos = MR::getCamPos();
    camPos.sub(mPosition);

    if (MR::isNearZero(camPos)) {
        return;
    }

    MR::normalize(&camPos);
    Vec3f YDir(MR::getCamYdir());
    Vec3f camcross(YDir.cross(camPos));

    if (MR::isNearZero(camcross)) {
        return;
    }

    MR::normalize(&camcross);
    MR::setMtxAxisXYZ(getBaseMtx(), camcross, YDir, camPos);
    MR::setMtxTrans(getBaseMtx(), mPosition);
}

void ItemBubble::exeWait() {
    if (MR::isFirstStep(this)) {
        // Useless...
    }

    mSwing = MR::sin(2.0f * ((static_cast< f32 >(getNerveStep()) / ::cSwingRange) * MR::pi()));

    if (mUseRail) {
        if (MR::isRailReachedGoal(this)) {
            MR::reverseRailDirection(this);
        }

        f32 speed;

        if (MR::getCurrentRailPointArg0WithInit(this, &speed)) {
            mRailSpeed = speed;
        }

        MR::moveCoordAndFollowTrans(this, mRailSpeed);
    }

    NebulaMtxScale(mTargetMtx, mScale.x, mScale.y, mScale.z);
    MtxPtr pMtx = MR::tmpMtxRotZDeg(mRotation.z);
    MR::multMtx(mTargetMtx, pMtx, mTargetMtx);

    mPosition = mBasePos + Vec3f(0.0f, 1.0f, 0.0f).multInLine2(mSwing).multInLine2(30.0f);

    MR::setMtxTrans(mTargetMtx, mPosition.x, mPosition.y, mPosition.z);

    if (mUseRail) {
        NebulaMtxCopy(mTargetMtx, mItemMtx);
    } else {
        MR::blendMtx(mItemMtx, mTargetMtx, 0.1f, mItemMtx);
    }

    for (u32 i = 0; i < mItemCount; i++) {
        if (mCoins[i] == nullptr) {
            continue;
        }

        mCoins[i]->mRotation.x = 0.0f;
        mCoins[i]->mRotation.z = 0.0f;
        mCoins[i]->mRotation.y += 8.0f;
    }
}

void ItemBubble::exeBreak() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_ITEM_BUBBLE_BREAK");
        MR::emitEffect(this, "BubbleBreak");
        kill();
    }
}

bool ItemBubble::receiveMsgPush(HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isSensorPlayer(pSender)) {
        setNerve(GET_NERVE(ItemBubble, ItemBubbleNrvBreak));
        return true;
    }

    return false;
}

bool ItemBubble::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgStarPieceAttack(msg)) {
        setNerve(GET_NERVE(ItemBubble, ItemBubbleNrvBreak));
        return true;
    }

    return false;
}
