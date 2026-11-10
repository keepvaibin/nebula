#include <nebula_game_api.h>
#include "Game/MapObj/SpiderThread.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/SpiderThreadHangInfo.hpp"
#include "Game/MapObj/SpiderThreadMainPoint.hpp"
#include "Game/MapObj/SpiderThreadPart.hpp"
#include "Game/MapObj/SpiderThreadRadialLine.hpp"
#include "Game/MapObj/SpiderThreadWindCtrl.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Render/GameTexture.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initMainThreads_f1, "initMainThreads.f1", 360.0f);

namespace {
    const CutPointEntry sCutPointNoTable[] = {{0, 3}, {1, 1}, {2, 0}, {4, 3}, {6, 1}, {6, 4}};

    static const s32 sMainRadialLinePointNum = 8;
    NEBULA_TUNABLE(f32, sMainRadialLineInterval, 175.0f);
    NEBULA_TUNABLE(f32, sMainRadialLineLength, 1800.0f);
    NEBULA_TUNABLE(f32, sMainPointRadius, 100.0f);
    NEBULA_TUNABLE(f32, sPartPointRadius, 100.0f);
    NEBULA_TUNABLE(f32, sTouchPointerSpeedMin, 5.0f);
    NEBULA_TUNABLE(f32, sTouchPointerSpeedAccelRate, 2.0f);
    NEBULA_TUNABLE(f32, sTouchSpeedMax, 50.0f);
    static const s32 sHangActorNumMax = 8 * sMainRadialLinePointNum;
    NEBULA_TUNABLE(f32, sIndirectScaleX, 0.035f);
    NEBULA_TUNABLE(f32, sIndirectScaleY, 0.03f);
    // The strips SpiderThreadPart::draw writes for one part (drawLine and drawPoints each write three).
    NEBULA_TUNABLE(s32, sStripsPerPart, 3);
};  // namespace

inline SpiderThreadPart* connectPoints(SpiderThreadPoint* pPointA, SpiderThreadPoint* pPointB) {
    SpiderThreadMainPoint* pointB = static_cast< SpiderThreadMainPoint* >(pPointB);
    SpiderThreadMainPoint* pointA = static_cast< SpiderThreadMainPoint* >(pPointA);

    SpiderThreadPart* part = new SpiderThreadPart(pointA, pointB, ::sMainPointRadius);

    pointA->addNearMainPoint(pointB, part);
    pointB->addNearMainPoint(pointA, part);

    return part;
}

SpiderThread::SpiderThread(const char* pName)
    : LiveActor(pName), mMainPoint(nullptr), mHangInfos(nullptr), mNumHangInfos(0), mShootHangInfo(nullptr), mNumRadialLines(8),
      mRadialLines(nullptr), mAnchorPoints(nullptr), mRadialPartsBufferSize(0), mNumRadialParts(0), mRadialParts(nullptr), mNumCircleParts(0),
      mCircleParts(nullptr), mCutPointsBufferSize(0), mNumCutPoints(0), mCutPoints(nullptr), mFront(0.0f, 0.0f, 1.0f), mWindCtrl(nullptr),
      mIsBloomOn(false), mThreadTexture(nullptr), mIndirectTexture(nullptr) {
    mRadialPartsBufferSize = mNumRadialLines * (::sMainRadialLinePointNum + 1);
    mCirclePartsBufferSize = mNumRadialLines * ::sMainRadialLinePointNum;
    mRadialParts = new SpiderThreadPart*[mRadialPartsBufferSize];
    mCircleParts = new SpiderThreadPart*[mCirclePartsBufferSize];

    for (s32 idx = 0; idx < mRadialPartsBufferSize; idx++) {
        mRadialParts[idx] = nullptr;
    }

    for (s32 idx = 0; idx < mCirclePartsBufferSize; idx++) {
        mCircleParts[idx] = nullptr;
    }
}

void SpiderThread::init(const TableIter& /*rIter*/) {
}

void SpiderThread::initThread(const Vec3f& rPos) {
    mPosition.set(rPos);

    MR::connectToScene(this, MR::MovementType_Ride, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_SpiderThread);

    mWindCtrl = new SpiderThreadWindCtrl();
    initMainThreads();
    MR::invalidateClipping(this);

    mThreadTexture = new GameTexture(MR::loadTexFromArc("SpiderThread", "SpiderThread.bti"));
    mIndirectTexture = new GameTexture(MR::loadTexFromArc("SpiderThread", "Indirect.bti"));

    mHangInfos = new SpiderThreadHangInfo*[::sHangActorNumMax];
    for (s32 idx = 0; idx < ::sHangActorNumMax; idx++) {
        mHangInfos[idx] = new SpiderThreadHangInfo();
    }

    MR::registerDemoSimpleCastAll(this);
    makeActorDead();
}

void SpiderThread::movement() {
    if (!MR::isValidMovement(this)) {
        return;
    }

    mWindCtrl->update();
    updatePointerInfo();
    updateHangedPoint();
    tryTouchPoint(PLAYER_1);
    tryTouchPoint(PLAYER_2);

    mMainPoint->updateVelocity();
    for (s32 idx = 0; idx < mNumRadialLines; idx++) {
        getMainRadialLine(idx)->updateVelocity();
    }
    mMainPoint->updatePos();
    mMainPoint->mBasePos.set(mMainPoint->mPosition);
    for (s32 idx = 0; idx < mNumRadialLines; idx++) {
        getMainRadialLine(idx)->updatePos();
    }

    for (s32 idx = 0; idx < mNumRadialParts; idx++) {
        getRadialPart(idx)->update();
    }

    for (s32 idx = 0; idx < mNumCircleParts; idx++) {
        getCirclePart(idx)->update();
    }

    for (s32 idx = 0; idx < mNumHangInfos; idx++) {
        if (!getActorHangInfo(idx)->mIsHanging && getActorHangInfo(idx)->mThreadPart->mIsStill) {
            getActorHangInfo(idx)->mThreadPart->updatePointPosAndBasePosToLine();
        }
    }
}

void SpiderThread::draw() const {
    if (MR::isValidDraw(this)) {
        const SiteMaterial material = loadMaterial();
        TypedDraw site(material, nullptr, MR::getCameraViewMtx());

        // A free (cut) part draws nothing; each part owns its three strips' numbers so the parts after it keep theirs and
        // pair with themselves from frame to frame.
        for (s32 idx = 0; idx < mNumRadialParts; idx++) {
            site.setPart(static_cast< u32 >(idx * ::sStripsPerPart));
            getRadialPart(idx)->draw(site);
        }

        for (s32 idx = 0; idx < mNumCircleParts; idx++) {
            site.setPart(static_cast< u32 >((mNumRadialParts + idx) * ::sStripsPerPart));
            getCirclePart(idx)->draw(site);
        }
    }
}

void SpiderThread::startActorBind(LiveActor* pActor, const Vec3f** pPointPos, const Vec3f** pNeutralPos, const Vec3f** pUp, const Vec3f& rPos,
                                  const Vec3f& rVel, s32 priority) {
    s32 nearestIndex = 0;
    s32 pointNum = 0;
    findNearestPointPos(pPointPos, pNeutralPos, &nearestIndex, &pointNum, rPos);

    SpiderThreadHangInfo* hangInfo = getActorHangInfo(mNumHangInfos);
    hangInfo->startBind(pActor, &pActor->mPosition, mCircleParts[nearestIndex], pointNum, rVel, priority);
    *pUp = &hangInfo->mThreadPart->mUp;
    mNumHangInfos++;
}

bool SpiderThread::touchActor(const Vec3f& rPos, const Vec3f& rVel) {
    const Vec3f* pointPos = nullptr;
    const Vec3f* neutralPos = nullptr;
    s32 nearestIndex = 0;
    s32 pointNum = 0;
    findNearestPointPos(&pointPos, &neutralPos, &nearestIndex, &pointNum, rPos);

    return mCircleParts[nearestIndex]->touchActor(pointNum, rVel);
}

void SpiderThread::tryPush(const Vec3f& rPos, f32 radius) {
    mMainPoint->tryPush(rPos, radius);
    for (s32 radialIdx = 0; radialIdx < mNumRadialLines; radialIdx++) {
        for (s32 idx = 0; idx < getMainRadialLine(radialIdx)->mNumPoints; idx++) {
            getMainRadialLine(radialIdx)->getPoint(idx)->tryPush(rPos, radius);
        }
    }
}

void SpiderThread::stopAllPartsPoint() {
    for (s32 idx = 0; idx < mNumCircleParts; idx++) {
        getCirclePart(idx)->stopAllPoints();
    }
}

void SpiderThread::cutSpiderThread() {
    // The web is cut once: its cut points are all used (a second cut would take points past the buffer).
    if (mNumCutPoints != 0) {
        return;
    }

    mMainPoint->cutNearPoints(&mNumCutPoints, mCutPoints, mCutPointsBufferSize);

    for (s32 idx = 0; idx < ARRAY_SIZE(::sCutPointNoTable); idx++) {
        getMainRadialLine(::sCutPointNoTable[idx].mRadialIdx)->getPoint(::sCutPointNoTable[idx].mPointIdx)->cutNearPoints(&mNumCutPoints, mCutPoints, mCutPointsBufferSize);
    }
}

void SpiderThread::initMainThreads() {
    Rot3f rotMtx;
    MR::makeMtxRotate(rotMtx.toMtxPtr(), 0.0f, 0.0f, cNum_initMainThreads_f1 / mNumRadialLines);
    Vec3f down(0.0f, -1.0f, 0.0f);

    mMainPoint = new SpiderThreadMainPoint(mPosition, mNumRadialLines + 1);

    mAnchorPoints = new SpiderThreadMainPoint*[mNumRadialLines];
    for (s32 idx = 0; idx < mNumRadialLines; idx++) {
        mAnchorPoints[idx] = nullptr;
    }

    mCutPointsBufferSize = mNumRadialLines + ARRAY_SIZE(::sCutPointNoTable) * 4;
    mCutPoints = new SpiderThreadMainPoint*[mCutPointsBufferSize];
    for (s32 idx = 0; idx < mCutPointsBufferSize; idx++) {
        mCutPoints[idx] = new SpiderThreadMainPoint(mPosition, 5);
    }

    mRadialLines = new SpiderThreadRadialLine*[mNumRadialLines];
    for (s32 radialIdx = 0; radialIdx < mNumRadialLines; radialIdx++) {
        mRadialLines[radialIdx] = new SpiderThreadRadialLine(::sMainRadialLinePointNum);

        // main axial points
        for (s32 idx = 0; idx < ::sMainRadialLinePointNum; idx++) {
            Vec3f pos(down);
            pos.scale(::sMainRadialLineInterval * (idx + 1));
            pos.add(mMainPoint->mPosition);

            SpiderThreadMainPoint* point = new SpiderThreadMainPoint(pos, 5);
            getMainRadialLine(radialIdx)->addPoint(point);
            point->mRadialLine = getMainRadialLine(radialIdx);
        }

        // anchor point
        Vec3f pos(down);
        pos.scale(::sMainRadialLineLength);
        pos.add(mMainPoint->mPosition);
        mAnchorPoints[radialIdx] = new SpiderThreadMainPoint(pos, 5);
        mAnchorPoints[radialIdx]->mRadialLine = getMainRadialLine(radialIdx);

        // connect points
        SpiderThreadMainPoint* point = getMainRadialLine(radialIdx)->getPoint(0);
        SpiderThreadMainPoint* nextPoint = nullptr;
        connectMainPoint(point, mMainPoint, true);
        for (s32 idx = 0; idx < ::sMainRadialLinePointNum - 1; idx++) {
            nextPoint = getMainRadialLine(radialIdx)->getPoint(idx + 1);
            connectMainPoint(point, nextPoint, true);
            point = nextPoint;
        }
        connectMainPoint(nextPoint, mAnchorPoints[radialIdx], true);

        rotMtx.mult(down, down);
    }

    for (s32 radialIdx = 0; radialIdx < mNumRadialLines; radialIdx++) {
        for (s32 idx = 0; idx < ::sMainRadialLinePointNum; idx++) {
            connectMainPoint(getMainRadialLine(radialIdx)->getPoint(idx),
                             getMainRadialLine((radialIdx + mNumRadialLines + 1) % mNumRadialLines)->getPoint(idx), false);
        }
    }
}

void SpiderThread::connectMainPoint(SpiderThreadPoint* pPointA, SpiderThreadPoint* pPointB, bool isMainPart) {
    SpiderThreadPart* part = connectPoints(pPointA, pPointB);

    if (isMainPart) {
        mRadialParts[mNumRadialParts] = part;
        mNumRadialParts++;
    } else {
        mCircleParts[mNumCircleParts] = part;
        mNumCircleParts++;
    }
}

bool SpiderThread::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_SLING_SHOOT_ACTOR_HANG_START) {
        SpiderThreadHangInfo* hangInfo = findHangInfo(pSender->mHost);
        if (mShootHangInfo != nullptr) {
            if (mShootHangInfo->mPriority < hangInfo->mPriority) {
                mShootHangInfo->endHang(true);
            } else {
                return false;
            }
        }
        mShootHangInfo = hangInfo;
        mShootHangInfo->startHang();
        return true;
    }

    if (msg == ACTMES_SLING_SHOOT_ACTOR_HANG_END) {
        // An end with no hang started ends nothing.
        if (mShootHangInfo != nullptr) {
            mShootHangInfo->endHang(false);
        }
        mShootHangInfo = nullptr;
        return true;
    }

    if (msg == ACTMES_SLING_SHOOT_ACTOR_APART) {
        SpiderThreadHangInfo* hangInfo = findHangInfo(pSender->mHost);
        hangInfo->endBind();
        if (mNumHangInfos > 1) {
            hangInfo->copyInfo(getActorHangInfo(mNumHangInfos - 1));
        }
        mShootHangInfo = nullptr;
        mNumHangInfos--;
        return true;
    }

    return false;
}

void SpiderThread::updatePointerInfo() {
    for (s32 idx = 0; idx < mNumCircleParts; idx++) {
        getCirclePart(idx)->updatePointerPos();
    }
}

void SpiderThread::updateHangedPoint() {
    for (s32 idx = 0; idx < mNumHangInfos; idx++) {
        if (getActorHangInfo(idx)->mIsHanging) {
            getActorHangInfo(idx)->updateHang();
            return;
        }
    }
}

void SpiderThread::tryTouchPoint(s32 padChannel) {
    if (!MR::isStarPointerInScreen(padChannel) || MR::getStarPointerScreenSpeed(padChannel) < ::sTouchPointerSpeedMin) {
        return;
    }

    Vec3f vel(0.0f, 0.0f, 0.0f);
    MR::calcStarPointerWorldVelocityDirectionOnPlane(&vel, mMainPoint->mPosition, mFront, padChannel);

    f32 speed = MR::getStarPointerScreenSpeed(padChannel) * ::sTouchPointerSpeedAccelRate;
    speed = speed >= ::sTouchSpeedMax ? ::sTouchSpeedMax : speed;

    vel.scale(speed);

    for (s32 idx = 0; idx < mNumCircleParts; idx++) {
        getCirclePart(idx)->tryTouch(::sPartPointRadius, vel, padChannel);
    }
}

void SpiderThread::findNearestPointPos(const Vec3f** pPointPos, const Vec3f** pNeutralPos, s32* nearestIndex, s32* pointNum,
                                       const Vec3f& rPos) const {
    Vec3f pos(rPos);
    f32 minDistance = ::sMainRadialLineLength;
    *pPointPos = nullptr;
    *pNeutralPos = nullptr;

    for (s32 idx = 0; idx < mNumCircleParts; idx++) {
        const Vec3f* pointPos = nullptr;
        const Vec3f* neutralPos = nullptr;
        s32 pointNo = -1;
        f32 dist = getCirclePart(idx)->calcNearestPointInfo(&pointPos, &neutralPos, &pointNo, pos);

        if (dist < minDistance) {
            *pPointPos = pointPos;
            *pNeutralPos = neutralPos;
            *nearestIndex = idx;
            *pointNum = pointNo;
            minDistance = dist;
        }
    }
}

SpiderThreadHangInfo* SpiderThread::findHangInfo(LiveActor* pActor) const {
    for (s32 idx = 0; idx < mNumHangInfos; idx++) {
        if (getActorHangInfo(idx)->mBindedActor == pActor) {
            return getActorHangInfo(idx);
        }
    }

    return getActorHangInfo(0);
}

SiteMaterial SpiderThread::loadMaterial() const {
    // runtime/src/render/materials/spider_thread.hlsl: the thread (texture 0) times the vertex colour, its lookup moved
    // by the indirect texture (1) at sIndirectScaleX/Y thread texels a step; blended by its alpha where above zero,
    // depth tested and not written, no culling.
    SiteMaterial material;
    material.init(NEBULA_SHADER_SPIDER_THREAD, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, false,
                       NEBULA_CULL_NONE);
    material.constants[0] = ::sIndirectScaleX;
    material.constants[1] = ::sIndirectScaleY;
    material.constants[2] = static_cast< f32 >(mThreadTexture->getWidth());
    material.constants[3] = static_cast< f32 >(mThreadTexture->getHeight());
    material.setTexture(0, mThreadTexture);
    material.setTexture(1, mIndirectTexture);
    return material;
}

namespace {
    // The bloom pass's drawing of the web has a draw slot of its own.
    u32 sBloomSlot;

    SpiderThread* getSpiderThread() {
        return MR::getSceneObj< SpiderThread >(SceneObj_SpiderThread);
    }
};  // namespace

void MR::initSpiderThread(const Vec3f& rPos) {
    ::getSpiderThread()->initThread(rPos);
}

void MR::appearSpiderThread() {
    ::getSpiderThread()->appear();
}

void MR::startSpiderThreadBattle() {
    ::getSpiderThread()->stopAllPartsPoint();
    ::getSpiderThread()->mWindCtrl->startWindBattle();
}

void MR::startSpiderThreadChance() {
    ::getSpiderThread()->stopAllPartsPoint();
    ::getSpiderThread()->mWindCtrl->startWindChance();
}

void MR::startSpiderThreadBattleEnd() {
    ::getSpiderThread()->stopAllPartsPoint();
    ::getSpiderThread()->cutSpiderThread();
    ::getSpiderThread()->mWindCtrl->startWindBattleEnd();
}

void MR::pauseOffSpiderThread() {
    MR::requestMovementOn(::getSpiderThread());
}

f32 MR::getSpiderThreadPosZ() {
    if (!MR::isExistSceneObj(SceneObj_SpiderThread)) {
        return 0.0f;
    }

    return ::getSpiderThread()->mPosition.z;
}

void MR::startActorBindToSpiderThread(LiveActor* pActor, const Vec3f** pPointPos, const Vec3f** pNeutralPos, const Vec3f** pUp, const Vec3f& rPos,
                                      const Vec3f& rVel) {
    ::getSpiderThread()->startActorBind(pActor, pPointPos, pNeutralPos, pUp, rPos, rVel, 1);
}

bool MR::sendMsgToSpiderThread(u32 msg, HitSensor* pSender) {
    return ::getSpiderThread()->receiveMessage(msg, pSender, MR::getMessageSensor());
}

bool MR::touchActorToSpiderThread(const Vec3f& rPos, const Vec3f& rVel) {
    return ::getSpiderThread()->touchActor(rPos, rVel);
}

void MR::tryPushSpiderThread(const Vec3f& rPos, f32 radius) {
    ::getSpiderThread()->tryPush(rPos, radius);
}

void MR::onSpiderThreadBloom() {
    ::getSpiderThread()->mIsBloomOn = true;
}

void MR::drawSpiderThreadBloom() {
    if (!MR::isExistSceneObj(SceneObj_SpiderThread)) {
        return;
    }

    if (::getSpiderThread()->mIsBloomOn) {
        // Called outside the object lists: without this the web would draw under the object drawn last, on its numbers.
        if (::sBloomSlot == 0) {
            ::sBloomSlot = Frame::newSlot();
        }
        Frame::setObject(::sBloomSlot, ::getSpiderThread()->mMotionEpoch);
        ::getSpiderThread()->draw();
        CategoryList::drawOpa(MR::DrawBufferType_Ride);
    }
}
