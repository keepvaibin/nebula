#include "Game/MapObj/BigBubbleHolder.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/BigBubbleDrawer.hpp"
#include "Game/MapObj/BigBubbleMoveLimitter.hpp"
#include "Game/Ride/BigBubble.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    BigBubbleHolder* getBigBubbleHolder() {
        return MR::getSceneObj< BigBubbleHolder >(SceneObj_BigBubbleHolder);
    }
};  // namespace

BigBubbleHolder::BigBubbleHolder(const char* pName)
    : NameObj(pName), mDrawer(new BigBubbleDrawer()), mBubbles(), mDrawBubbles(), mLimitters(), mNumLimitters(), mNumBubbles(), mIsMarioBinded() {
    lumacore::setClass(this, lumacore::kClassId< BigBubbleHolder >);
    MR::registerPreDrawFunction(MR::Functor(&MR::setUpBigBubbleDraw), MR::DrawType_BigBubble);
    MR::connectToScene(this, MR::MovementType_EnemyDecoration, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_BigBubble);
}

void BigBubbleHolder::movement() {
    mIsMarioBinded = false;
    for (s32 idx = 0; idx < mBubbles.size(); idx++) {
        if (mBubbles[idx]->isBindMario()) {
            mIsMarioBinded = true;
            break;
        }
    }
}

void BigBubbleHolder::draw() const {
    // The bubbles draw far to near, an order that changes as they and the camera move: each draws as an object of its own
    // (its slot, its life), so its surface pairs with itself whatever its place in the order.
    for (s32 idx = 0; idx < mNumBubbles; idx++) {
        BigBubble* pBubble = mDrawBubbles[idx];
        MotionSignal::drawObject(pBubble->mDrawSlot, pBubble->mMotionEpoch);
        pBubble->draw();
    }
}

void BigBubbleHolder::addMoveLimitter(BigBubbleMoveLimitter* pLimitter) {
    for (s32 idx = 0; idx < mNumLimitters; idx++) {
        if (mLimitters[idx]->getLimitterID() == pLimitter->getLimitterID()) {
            return;
        }
    }
    mLimitters.at(mNumLimitters++) = pLimitter;
}

BigBubble* BigBubbleHolder::generateBigBubble(LiveActor* pHost, const Vec3f& rPos, const Vec3f& rUp, f32 volume, bool isObstruct, s32 actionType,
                                              s32 shapeType, s32 limitterID) {
    BigBubbleMoveLimitter* limitter = nullptr;
    if (limitterID >= 0) {
        limitter = getMoveLimitter(limitterID);
    }

    BigBubble* pBubble = nullptr;
    for (s32 idx = 0; idx < mBubbles.size(); idx++) {
        if (MR::isDead(mBubbles[idx])) {
            pBubble = mBubbles[idx];
            break;
        }
    }

    if (pBubble == nullptr) {
        pBubble = createBubble();
        pBubble->initAfterPlacement();
    }

    pBubble->setHost(pHost);
    pBubble->generate(rPos, rUp, volume, isObstruct, actionType, shapeType, limitter);

    return pBubble;
}

BigBubble* BigBubbleHolder::createBubble() {
    NebulaArena::CurrentScope scope(Arenas::scene());
    BigBubble* pBubble = new BigBubble("オオアワ[共有]");
    pBubble->initWithoutIter();
    mBubbles.push_back(pBubble);
    mDrawBubbles.push_back(nullptr);
    return pBubble;
}

BigBubbleMoveLimitter* BigBubbleHolder::getMoveLimitter(s32 limitterID) const {
    for (s32 idx = 0; idx < mNumLimitters; idx++) {
        if (mLimitters[idx]->getLimitterID() == limitterID) {
            return mLimitters[idx];
        }
    }
    return nullptr;
}

void BigBubbleHolder::sortDrawOrder() {
    mNumBubbles = 0;
    for (s32 idx = 0; idx < mBubbles.size(); idx++) {
        if (mBubbles[idx]->isDraw()) {
            bool skipSort;
            s32 other = mNumBubbles;
            for (; other > 0; other--) {
                if (!mDrawBubbles[other - 1]->isMerged() && mBubbles[idx]->isMerged()) {
                    skipSort = false;
                } else if (mDrawBubbles[other - 1]->isMerged() && !mBubbles[idx]->isMerged()) {
                    skipSort = true;
                } else {
                    skipSort = mDrawBubbles[other - 1]->getDrawZ() < mBubbles[idx]->getDrawZ();
                }

                if (!skipSort) {
                    break;
                }

                mDrawBubbles[other] = mDrawBubbles[other - 1];
            }
            mDrawBubbles[other] = mBubbles[idx];
            mNumBubbles++;
        }
    }
}

void MR::setUpBigBubbleDraw() {
    BigBubbleHolder* holder = ::getBigBubbleHolder();
    holder->sortDrawOrder();
    holder->mDrawer->setUpDraw();
}

void MR::createBigBubbleHolder() {
    MR::createSceneObj(SceneObj_BigBubbleHolder);
}

BigBubble* MR::generateBigBubble(LiveActor* pHost, const Vec3f& rPos, const Vec3f& rUp, f32 volume, bool isObstruct, s32 actionType, s32 shapeType,
                                 s32 limitterID) {
    return ::getBigBubbleHolder()->generateBigBubble(pHost, rPos, rUp, volume, isObstruct, actionType, shapeType, limitterID);
}

void MR::limitBigBubblePosition(Vec3f* pPos, f32 offset, s32 limitterID) {
    // A limitter number nothing registered (a bubble set to one a stage lacks) limits nothing.
    BigBubbleMoveLimitter* pLimitter = ::getBigBubbleHolder()->getMoveLimitter(limitterID);

    if (pLimitter != nullptr) {
        pLimitter->limitPosition(pPos, offset);
    }
}

void MR::addBigBubbleMoveLimitter(BigBubbleMoveLimitter* pLimitter) {
    ::getBigBubbleHolder()->addMoveLimitter(pLimitter);
}

bool MR::isPlayerBindedBigBubble() {
    if (!MR::isExistSceneObj(SceneObj_BigBubbleHolder)) {
        return false;
    }

    return ::getBigBubbleHolder()->mIsMarioBinded;
}

void BigBubbleHolder::init(const TableIter& /*rIter*/) {
    mBubbles.init(cBubbleCount);
    mDrawBubbles.init(cBubbleCount);
    for (s32 idx = 0; idx < cBubbleCount; idx++) {
        createBubble();
    }
}
