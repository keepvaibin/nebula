#include "Game/Camera/CameraShaker.hpp"
#include "Game/Camera/CameraShakePatternImpl.hpp"
#include "Game/Camera/CameraShakeTask.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 cInfinityIntensity = 1.0f;
    const f32 cInfinitySpeed = 15.0f;
    const u32 cInfinityInitialNum = 4;
}  // namespace

CameraShaker::CameraShaker(const char* pName) : NameObj(pName) {
    lumacore::setClass(this, lumacore::kClassId< CameraShaker >);
    createSinglyVerticalTask();
    createSinglyHorizontalTask();
    createInfinityTask();
}

void CameraShaker::movement() {
    updateSinglyVerticalTask();
    updateSinglyHorizontalTask();
    updateInfinityTask();
    setOffset();
}

void CameraShaker::shakeVertical(ESinglyVerticalPower power) {
    if (mVerticalTasks[power]->isEnd()) {
        mVerticalTasks[power]->start(1, 0);
    }
}

void CameraShaker::shakeInfinity(NameObj* pNameObj, f32 intesity, f32 speed) {
    u32 i = 0;
    while (i < (u32)mInfinity.size() && !mInfinity[i].mTask->isEnd()) {
        i++;
    }

    if (i == (u32)mInfinity.size()) {
        NebulaArena* pArena = NebulaArena::findFromRoot(this);
        CameraShakePatternVerticalSin* sin = new (pArena, 0) CameraShakePatternVerticalSin(::cInfinityIntensity, ::cInfinitySpeed);
        InfinityShake shake = {new (pArena, 0) CameraShakeTask(sin), nullptr};
        mInfinity.push_back(shake);
    }

    startInfinity(i, intesity, speed);
    mInfinity[i].mOwner = pNameObj;
}

void CameraShaker::stopShakingInfinity(NameObj* pNameObj) {
    for (InfinityShake* pIter = mInfinity.begin(); pIter != mInfinity.end(); pIter++) {
        if (pIter->mOwner == pNameObj) {
            pIter->mTask->endForce();
            pIter->mOwner = nullptr;
            break;
        }
    }
}

void CameraShaker::createSinglyVerticalTask() {
    static const f32 cSinglyIntensity[] = {0.08f, 0.2f, 0.5f, 1.0f, 3.0f, 6.0f, 9.0f};

    for (u32 i = 0; i < cVerticalTaskNum; i++) {
        CameraShakePatternSingly* singly = new CameraShakePatternSingly(cSinglyIntensity[i]);
        CameraShakeTask* task = new CameraShakeTask(singly);

        mVerticalTasks[i] = task;
    }
}

void CameraShaker::createSinglyHorizontalTask() {
    static const f32 cSinglyIntensity[] = {0.3f, 1.0f, 3.0f};

    for (u32 i = 0; i < cHorizontalTaskNum; i++) {
        CameraShakePatternSingly* singly = new CameraShakePatternSingly(cSinglyIntensity[i]);

        singly->setDirection(Vec2f(1.0f, 0.0f));

        CameraShakeTask* task = new CameraShakeTask(singly);

        mHorizontalTasks[i] = task;
    }
}

void CameraShaker::createInfinityTask() {
    mInfinity.init(::cInfinityInitialNum);
    for (u32 i = 0; i < ::cInfinityInitialNum; i++) {
        CameraShakePatternVerticalSin* sin = new CameraShakePatternVerticalSin(::cInfinityIntensity, ::cInfinitySpeed);
        InfinityShake shake = {new CameraShakeTask(sin), nullptr};
        mInfinity.push_back(shake);
    }
}

void CameraShaker::updateSinglyVerticalTask() {
    for (u32 i = 0; i < cVerticalTaskNum; i++) {
        mVerticalTasks[i]->movement();
    }
}

void CameraShaker::updateSinglyHorizontalTask() {
    for (u32 i = 0; i < cHorizontalTaskNum; i++) {
        mHorizontalTasks[i]->movement();
    }
}

void CameraShaker::updateInfinityTask() {
    for (InfinityShake* pIter = mInfinity.begin(); pIter != mInfinity.end(); pIter++) {
        pIter->mTask->movement();
    }
}

void CameraShaker::setOffset() {
    Vec2f offset = Vec2f(0.0f, 0.0f);

    addSinglyVerticalTaskOffset(&offset);
    addSinglyHorizontalTaskOffset(&offset);
    addInfinityTaskOffset(&offset);
    adjustOffsetToScreen(&offset);
    MR::setShakeOffset(offset.x, offset.y);
}

void CameraShaker::addSinglyVerticalTaskOffset(Vec2f* pOffset) {
    for (u32 i = 0; i < cVerticalTaskNum; i++) {
        if (!mVerticalTasks[i]->isEnd()) {
            Vec2f offset;
            mVerticalTasks[i]->getOffset(&offset);

            pOffset->x += offset.x;
            pOffset->y += offset.y;
        }
    }
}

void CameraShaker::addSinglyHorizontalTaskOffset(Vec2f* pOffset) {
    for (u32 i = 0; i < cHorizontalTaskNum; i++) {
        if (!mHorizontalTasks[i]->isEnd()) {
            Vec2f offset;
            mHorizontalTasks[i]->getOffset(&offset);

            pOffset->x += offset.x;
            pOffset->y += offset.y;
        }
    }
}

void CameraShaker::addInfinityTaskOffset(Vec2f* pOffset) {
    for (InfinityShake* pIter = mInfinity.begin(); pIter != mInfinity.end(); pIter++) {
        if (!pIter->mTask->isEnd()) {
            Vec2f offset;
            pIter->mTask->getOffset(&offset);

            pOffset->x += offset.x;
            pOffset->y += offset.y;
        }
    }
}

void CameraShaker::adjustOffsetToScreen(Vec2f* pOffset) {
    pOffset->y *= 30.0f / (s32)NebulaFrameClock::cScreenLines;
    pOffset->x *= 30.0f / MR::getScreenWidth();
}

void CameraShaker::startInfinity(u32 index, f32 intesity, f32 speed) {
    CameraShakePatternVerticalSin* sin = static_cast< CameraShakePatternVerticalSin* >(mInfinity[index].mTask->mPattern);

    sin->mIntensity = intesity;
    sin->mSpeed = speed;

    mInfinity[index].mTask->startInfinity(0);
}
