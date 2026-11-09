#include "Game/System/DrawBufferExecuter.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/System/DrawBuffer.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/LightUtil.hpp"
#include <algorithm>

DrawBufferExecuter::DrawBufferExecuter(const char* pName, Model* pModel)
    : mActors(nullptr), mMaxNumActors(0), mNumActors(0), mName(pName), mDrawBuffer(nullptr), mLightType(-1), mDrawBufferCount(0) {
    mDrawBuffer = new DrawBuffer(pModel);
}

void DrawBufferExecuter::allocateActorListBuffer() {
    s32 count = mDrawBufferCount;
    if (count != 0) {
        mActors = new LiveActor*[count];
        mMaxNumActors = count;
        mDrawBuffer->init(mDrawBufferCount);
    }
}

// Room past the actors counted at scene start, for actors made later (CoinHolder::takeDeadCoin), in this executer's arena.
void DrawBufferExecuter::reserve(s32 count) {
    LiveActor** pActors = new (NebulaArena::findFromRoot(this), 0) LiveActor*[count];
    for (s32 i = 0; i < mNumActors; i++) {
        pActors[i] = mActors[i];
    }
    delete[] mActors;
    mActors = pActors;

    if (mMaxNumActors == 0) {
        mDrawBuffer->init(count);
    } else {
        mDrawBuffer->reserve(count);
    }
    mMaxNumActors = count;
}

void DrawBufferExecuter::add(LiveActor* pActor) {
    if (mNumActors >= mMaxNumActors) {
        // A new executer has its registrations (at least this actor's) counted in mDrawBufferCount.
        reserve(mMaxNumActors > 0 ? mMaxNumActors * 2 : mDrawBufferCount);
    }

    s32 lastActor = mNumActors;
    mNumActors++;
    mActors[lastActor] = pActor;
    mDrawBuffer->add(pActor);
}

void DrawBufferExecuter::remove(LiveActor* pActor) {
    LiveActor** it = std::find(mActors, mActors + mNumActors, pActor);
    mActors[it - mActors] = getActor(mNumActors - 1);
    mNumActors--;
    mDrawBuffer->remove(pActor);
}

void DrawBufferExecuter::findLightInfo(LiveActor* pActor) {
    MR::initActorLightInfoDrawBuffer(pActor, mDrawBuffer);
}

void DrawBufferExecuter::onExecuteLight(s32 lightType) {
    mLightType = lightType;
}
void DrawBufferExecuter::offExecuteLight() {
    mLightType = -1;
}

void DrawBufferExecuter::calcViewAndEntry() {
    for (auto it = &mActors[0], end = &mActors[mNumActors]; it != end; ++it) {
        (*it)->calcViewAndEntry();
    }
}

void DrawBufferExecuter::drawOpa() const {
    if (mLightType != -1) {
        MR::loadLight(mLightType);
    }
    mDrawBuffer->drawOpa();
}

void DrawBufferExecuter::drawXlu() const {
    if (mLightType != -1) {
        MR::loadLight(mLightType);
    }
    mDrawBuffer->drawXlu();
}
