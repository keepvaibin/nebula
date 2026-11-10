#include "Game/MapObj/WarpPod.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initDraw_f1, "initDraw.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initDraw_f2, "initDraw.f2", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initDraw_f3, "initDraw.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initDraw_f4, "initDraw.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initDraw_f5, "initDraw.f5", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initDraw_f6, "initDraw.f6", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initDraw_f7, "initDraw.f7", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawCylinder_f1, "drawCylinder.f1", 2.0f);

namespace {
    // By arg 6.
    const NebulaColor8 cGlowColors[] = {
        {0, 100, 200}, {44, 255, 42}, {255, 60, 60}, {196, 166, 0}, {0, 255, 0}, {255, 0, 255}, {255, 255, 0}, {255, 255, 255},
    };

    NEBULA_TUNABLE(f32, cSensorRadiusHidden, 120.0f);
    NEBULA_TUNABLE(f32, cSensorRadiusVisible, 15.0f);
    const s32 cDefaultCameraTime = NebulaFrameClock::stepsOfSeconds(2);
    const u16 cRestDelay = NebulaFrameClock::stepsOfSeconds(1);
    const u16 cRestDelayAfterTouch = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, cStandOnDistance, 200.0f);
    NEBULA_TUNABLE(u16, cPathPointCount, 60);
    NEBULA_TUNABLE(f32, cPathLift, 200.0f);
    // Draw numbers a pod's tube owns (a segment for each of cPathPointCount points).
    NEBULA_TUNABLE(u32, cPartsPerPod, 256);
    NEBULA_TUNABLE(f32, cPathTubeRadius, 30.0f);

    // After a warp the far pod rests (closed) for a while before it takes the player back.
    void startRest(WarpPod* pPod) {
        // A pod placed without a partner (a mod's) has none to rest.
        if (pPod == nullptr) {
            return;
        }

        pPod->mDelay = cRestDelay;
        MR::startBck(pPod, "Wait");
        MR::startBrk(pPod, "Wait");
    }

    // The tube's cross for a path heading `rDirection`: a side and an up offset of the tube radius, square to the
    // view. False when the path runs along the view.
    bool calcTubeCross(Vec3f* pSide, Vec3f* pUp, const Vec3f& rDirection) {
        Vec3f onScreen;
        MR::vecKillElement(rDirection, MR::getCamZdir(), &onScreen);

        if (MR::normalizeOrZero(&onScreen)) {
            return false;
        }

        pSide->cross(onScreen, MR::getCamZdir());
        MR::normalizeOrZero(pSide);

        pUp->cross(*pSide, onScreen);
        MR::normalizeOrZero(pUp);

        pSide->setLength(cPathTubeRadius);
        pUp->setLength(cPathTubeRadius);
        return true;
    }

    struct TubeCross {
        Vec3f mSidePlus;
        Vec3f mSideMinus;
        Vec3f mUpPlus;
        Vec3f mUpMinus;

        void set(const Vec3f& rPoint, const Vec3f& rSide, const Vec3f& rUp) {
            mSidePlus = rPoint + rSide;
            mSideMinus = rPoint - rSide;
            mUpPlus = rPoint + rUp;
            mUpMinus = rPoint - rUp;
        }
    };

    void sendVertex(VertexPT*& rVtx, const Vec3f& rPosition, const Vec2f& rCoord) {
        *rVtx++ = {rPosition.x, rPosition.y, rPosition.z, rCoord.x, rCoord.y};
    }
};  // namespace

namespace MR {
    WarpPodMgr* getWarpPodManager() {
        if (!isExistSceneObj(SceneObj_WarpPodMgr)) {
            return nullptr;
        }

        return getSceneObj< WarpPodMgr >(SceneObj_WarpPodMgr);
    }
};  // namespace MR

WarpPodMgr::WarpPodMgr(const char* pName) : NameObj(pName) {
    mPods = new LiveActorGroup("ワープポッド群", 128);
    mCameraPod = nullptr;
    mNextPairIndex = 0;

    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_NormalMap);
}

// The other pod of the same placement group.
WarpPod* WarpPodMgr::getPairPod(const LiveActor* pActor) {
    const WarpPod* pPod = static_cast< const WarpPod* >(pActor);
    if (pPod->mPlacementId == nullptr) {
        return nullptr;
    }

    for (u32 i = 0; i < mPods->getObjNum(); i++) {
        WarpPod* pOther = static_cast< WarpPod* >(mPods->getActor(i));

        if (pOther == pActor || pOther->mPlacementId == nullptr) {
            continue;
        }

        if (*pOther->mPlacementId == *pPod->mPlacementId) {
            return pOther;
        }
    }

    return nullptr;
}

void WarpPodMgr::startEventCamera(const LiveActor* pWarpPod) {
    static_cast< const WarpPod* >(pWarpPod)->startEventCamera();
    mCameraPod = pWarpPod;
}

void WarpPodMgr::endEventCamera() {
    if (mCameraPod == nullptr) {
        return;
    }

    const_cast< WarpPod* >(static_cast< const WarpPod* >(mCameraPod))->endEventCamera();
    startRest(getPairPod(mCameraPod));
    mCameraPod = nullptr;
}

void WarpPodMgr::notifyWarpEnd(WarpPod* pWarpPod) {
    if (pWarpPod == nullptr) {
        return;
    }

    startRest(getPairPod(pWarpPod));
    mCameraPod = nullptr;
}

void WarpPodMgr::draw() const {
    // Each pod owns a range of draw numbers by its place in the group, so a pod without a tube does not renumber the others.
    for (u32 i = 0; i < mPods->getObjNum(); i++) {
        static_cast< WarpPod* >(mPods->getActor(i))->drawCylinder(i * ::cPartsPerPod);
    }
}

// Args: 0 the event camera's letter (A + n), 1 the visibility (0 hidden with a wide sensor, 1 shown with its path,
// 2 shown, path drawn evenly), 2 read by the player's warp, 3 = 0 opens by the saved path flag, 4 the domes needed
// to open, 5 the appear camera frames, 6 the glow color, 7 = 1 this end draws the path.
void WarpPod::init(const TableIter& rIter) {
    MR::createSceneObj(SceneObj_WarpPodMgr);
    MR::joinToGroup(this, "ワープポッド群");

    LiveActor::init(rIter);
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("WarpPod", nullptr, false);

    s32 groupId = -1;
    MR::getPlacementGroupID(rIter, &groupId);

    if (groupId >= 0) {
        mPlacementId = new PlacementId(groupId, rIter);
    }

    mGroupId = groupId;
    mVisibilityState = 1;
    mArg2 = -1;
    mPathOpenType = -1;
    mGrandstarReq = -1;
    mCameraTime = cDefaultCameraTime;
    mGlowColorIndex = 0;
    s32 drawsPathArg = -1;

    if (MR::isValidInfo(rIter)) {
        MR::getPlacementArg1NoInit(rIter, &mVisibilityState);
        MR::getPlacementArg2NoInit(rIter, &mArg2);
        MR::getPlacementArg3NoInit(rIter, &mPathOpenType);
        MR::getPlacementArg4NoInit(rIter, &mGrandstarReq);
        MR::getPlacementArg5NoInit(rIter, &mCameraTime);
        MR::getPlacementArg6NoInit(rIter, &mGlowColorIndex);
        MR::getPlacementArg7NoInit(rIter, &drawsPathArg);
    }
    if (mGlowColorIndex < 0 || mGlowColorIndex >= ARRAY_SIZE(::cGlowColors)) {
        mGlowColorIndex = 0;
    }

    mDrawsPath = drawsPathArg == 1;

    mCamInfo = new ActorCameraInfo(rIter);

    s32 cameraLetter;
    MR::getPlacementArg0WithInit(rIter, &cameraLetter);

    const MR::FormattedName eventCameraName("ワープカメラ %d-%c", groupId, 'A' + cameraLetter);
    MR::declareEventCamera(mCamInfo, eventCameraName.c_str());

    const size_t eventCameraSize = strlen(eventCameraName.c_str()) + 1;
    mEventCameraName = new char[eventCameraSize];
    memcpy(mEventCameraName, eventCameraName.c_str(), eventCameraSize);

    if (mVisibilityState == 0) {
        MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
    } else {
        MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_MapObj, MR::DrawBufferType_MapObj, MR::DrawType_None);
    }

    initSound(4, false);
    initHitSensor(1);

    f32 scale = mScale.x;
    f32 radius = mVisibilityState == 0 ? scale * cSensorRadiusHidden : scale * cSensorRadiusVisible;
    MR::addHitSensorEye(this, "eye", 8, radius, Vec3f(0.0f, 0.0f, 0.0f));

    mDelay = 0;
    mCameraStep = 0;
    mIsAppearDemoPending = false;

    initEffectKeeper(1, nullptr);

    MR::validateClipping(this);
    MR::setClippingFarMax(this);

    makeActorAppeared();

    if (mVisibilityState != 0) {
        MR::startBck(this, "Active");
        MR::startBrk(this, "Active");
    }

    bool isInactive = MR::calcOpenedAstroDomeNum() < mGrandstarReq;

    if (isSavedPath()) {
        mPathFlagIndex = MR::getWarpPodManager()->mNextPairIndex++;
        isInactive = !MR::isOnWarpPodPathFlag(mPathFlagIndex);
    } else if (!isInactive) {
        glowEffect();
    }

    if (isInactive) {
        MR::startBck(this, "Wait");
        MR::startBrk(this, "Wait");
        mIsInactive = true;
    } else {
        mIsInactive = false;
        glowEffect();
    }

    mIsPairCameraOn = false;

    mPairPod = MR::getWarpPodManager()->getPairPod(this);

    if (mPairPod != nullptr) {
        initPair();
        mPairPod->initPair();
    }

    MR::tryRegisterDemoCast(this, rIter);
}

void WarpPod::glowEffect() {
    if (mVisibilityState == 0) {
        return;
    }

    const NebulaColor8& rGlow = cGlowColors[mGlowColorIndex];
    MR::emitEffect(this, "EndGlow");
    MR::setEffectEnvColor(this, "EndGlow", rGlow.r, rGlow.g, rGlow.b);
}

// Picks the end that draws the path when neither was told: not a saved-path end, else by position. An inactive pair
// gets the camera that shows the path opening.
void WarpPod::initPair() {
    mPairPod = MR::getWarpPodManager()->getPairPod(this);

    bool isDrawEndByPos = false;
    if (mPairPod->mPosition.x > mPosition.x) {
        isDrawEndByPos = true;
    } else if (mPairPod->mPosition.x < mPosition.x) {
        isDrawEndByPos = false;
    } else if (mPairPod->mPosition.y < mPosition.y) {
        isDrawEndByPos = true;
    } else if (mPairPod->mPosition.z < mPosition.z) {
        isDrawEndByPos = true;
    }

    if (!mPairPod->mDrawsPath && !mDrawsPath) {
        if (isSavedPath()) {
            mDrawsPath = false;
        } else if (mPairPod->isSavedPath()) {
            mDrawsPath = true;
        } else {
            mDrawsPath = isDrawEndByPos;
        }
    }

    initDraw();

    if (!mIsInactive || (!mDrawsPath && mPairPod->mIsInactive)) {
        return;
    }

    const MR::FormattedName pairCameraName("wPod出現カメラ %d", mGroupId);

    const size_t size = strlen(pairCameraName.c_str()) + 1;
    mPairCameraName = new char[size];
    memcpy(mPairCameraName, pairCameraName.c_str(), size);

    MR::declareEventCamera(mCamInfo, mPairCameraName);
}

// Opening: the drawing end opens with its camera time (the other end follows when it ends).
void WarpPod::appear() {
    if (mIsInactive && mPairPod != nullptr && mPairPod->mIsInactive && !mDrawsPath) {
        mPairPod->appear();
        mIsInactive = false;
        return;
    }

    mCameraStep = mCameraTime;
    MR::invalidateClipping(this);
    mIsInactive = false;

    MR::startSound(this, "SE_OJ_WARP_POD_PATH_APPEAR");
    MR::startBck(this, "Active");
    MR::startBrk(this, "Active");
    glowEffect();
}

// Touching an open pod whose pair is closed opens the path with its demo (saving the flag).
void WarpPod::appearWithDemo() {
    if (mIsInactive) {
        if (isSavedPath()) {
            MR::setWarpPodPathFlag(mPathFlagIndex, true);
        }

        if (mPairPod != nullptr && mPairPod->mIsInactive && !mDrawsPath) {
            mPairPod->appearWithDemo();
            mIsInactive = false;
            return;
        }
    }

    mIsAppearDemoPending = true;
    MR::invalidateClipping(this);
}

void WarpPod::control() {
    if (!mIsAppearDemoPending || !MR::tryStartDemoWithoutCinemaFrame(this, "出現")) {
        return;
    }

    mIsAppearDemoPending = false;
    mCameraStep = mCameraTime;

    MR::startEventCameraNoTarget(mCamInfo, mPairCameraName);
    MR::startSound(this, "SE_OJ_WARP_POD_PATH_APPEAR");
    MR::requestMovementOn(this);
    MR::pauseOffCameraDirector();

    mIsInactive = false;
    mIsPairCameraOn = true;

    MR::startBck(this, "Active");
    MR::startBrk(this, "Active");
    glowEffect();
}

// Counts down the opening (then opens the pair), else the rest, which holds its last step while the player stands
// on the pod.
void WarpPod::movement() {
    if (mCameraStep != 0) {
        mCameraStep--;

        if (mCameraStep == 0) {
            MR::validateClipping(this);

            if (mIsPairCameraOn) {
                MR::endDemo(this, "出現");
            }

            if (mPairPod != nullptr) {
                mPairPod->glowEffect();
                MR::startBck(mPairPod, "Active");
                MR::startBrk(mPairPod, "Active");
            }
        }

        return;
    }

    if (mDelay != 0) {
        if (mDelay == 1 && MR::calcDistanceToPlayer(mPosition) < cStandOnDistance) {
            return;
        }

        mDelay--;

        if (mDelay == 0) {
            MR::validateClipping(this);
            MR::startBck(this, "Active");
            MR::startBrk(this, "Active");

            if (mVisibilityState != 0) {
                MR::startSound(this, "SE_OJ_WARP_POD_ACTIVE");
            }
        }
    }

    LiveActor::movement();
}

void WarpPod::startEventCamera() const {
    if (mIsPairCameraOn || (mPairPod != nullptr && mPairPod->mIsPairCameraOn)) {
        return;
    }

    MR::startEventCameraNoTarget(mCamInfo, mEventCameraName);
}

void WarpPod::endEventCamera() {
    if (mIsPairCameraOn) {
        MR::endEventCamera(mCamInfo, mPairCameraName, true);
        mIsPairCameraOn = false;
    } else if (mPairPod != nullptr && mPairPod->mIsPairCameraOn) {
        mPairPod->endEventCamera();
    } else {
        MR::endEventCamera(mCamInfo, mEventCameraName, true);
    }
}

// An open pod takes the player: opening a closed pair first, holding its rest a little longer while touched.
void WarpPod::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mIsInactive || mPairPod == nullptr || !MR::isSensorPlayer(pReceiver)) {
        return;
    }

    if (mPairPod->mIsInactive) {
        mPairPod->appearWithDemo();
        MR::invalidateClipping(this);
    } else if (mDelay != 0) {
        if (mDelay < cRestDelayAfterTouch) {
            mDelay = cRestDelayAfterTouch;
        }
    } else {
        MR::sendArbitraryMsg(ACTMES_WARP, pReceiver, pSender);
    }
}

// The path: an arc of a quarter turn from this pod to its pair, bowed along the pods' cross direction and lifted, its
// points eased toward the ends (evenly for visibility 2).
void WarpPod::initDraw() {
    if (!mDrawsPath) {
        return;
    }

    Vec3f side;
    Vec3f normal;
    Vec3f direction(mPairPod->mPosition - mPosition);
    f32 distance = direction.length();
    Vec3f up;
    MR::calcUpVec(&up, this);
    side.cross(direction, -up);
    MR::normalizeOrZero(&side);
    Vec3f center(mPosition + direction * cNum_initDraw_f1);
    normal.cross(side, direction);
    MR::normalizeOrZero(&normal);

    f32 halfAngle = PI / cNum_initDraw_f2;
    f32 radius = (cNum_initDraw_f3 * distance) / MR::sin(halfAngle);
    f32 height = MR::sqrt(radius * radius - cNum_initDraw_f4 * (cNum_initDraw_f5 * distance * distance));
    Vec3f axis(side);
    Vec3f arcCenter(center + normal * height);
    Vec3f arcStart = -normal * radius;

    f32 startAngle = -halfAngle;
    s32 count = cPathPointCount;
    mPathPoints = new Vec3f[count];
    mNumPathPoints = count;

    for (s32 i = 0; i < count; i++) {
        s32 remaining = count - i;
        f32 blend = (MR::sin(((count - remaining - cNum_initDraw_f6 * count) / count) * MR::pi()) + 1.0f) / cNum_initDraw_f7;

        if (mVisibilityState == 2) {
            blend = 1.0f - (remaining - 1) / static_cast< f32 >(count);
        }

        Pos3f rotation;
        NebulaMtxRotAxisRad(rotation, axis, startAngle * (1.0f - blend) + halfAngle * blend);
        Vec3f point;
        NebulaMtxMultVecSR(rotation, arcStart, point);
        Vec3f position(arcCenter + point);
        mPathPoints[i] = position + up * cPathLift;
    }

    mGlowTexture = new GameTexture(MR::getTexture(MR::getResourceHolder(this), "TestColor.bti"));
}

// A tube of two crossed ribbons along the path, facing the view, its texture's v running 1 at the ends to 0 midway.
// While a pod of the pair opens, the tube grows from this end with the opening.
void WarpPod::drawCylinder(u32 firstPart) const {
    if (!mDrawsPath || mPairPod == nullptr || mPairPod->mIsInactive || mIsInactive || mVisibilityState != 1) {
        return;
    }

    // runtime/src/render/materials/tex_alpha_lerp.hlsl: the glow colour to white by the texture's alpha, opaque
    // added; depth tested.
    SiteMaterial material;
    TDDraw::setupNative(&material, 1, 2, 0);
    material.setShader(NEBULA_SHADER_TEX_ALPHA_LERP);
    material.setDepth(true, NEBULA_COMPARE_LEQUAL, false);
    const NebulaColor8& rGlow = cGlowColors[mGlowColorIndex];
    material.setColor(0, rGlow.r, rGlow.g, rGlow.b, rGlow.a);
    material.setColor(4, 255, 255, 255, 255);
    material.constants[10] = 1.0f;
    material.setTexture(0, mGlowTexture);

    s32 openStep = mCameraStep;
    u32 pointCount = mNumPathPoints;
    s32 openTime = mCameraTime;

    if (openStep == 0) {
        openStep = mPairPod->mCameraStep;
        openTime = mPairPod->mCameraTime;
    }

    if (openStep != 0) {
        pointCount *= 1 - static_cast< f32 >(openStep) / openTime;
    }

    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    TubeCross prev;
    TubeCross cur;
    Vec3f prevPoint;
    Vec2f curTexCoords[2];
    Vec2f prevTexCoords[2];

    for (u32 i = 0; i < pointCount; i++) {
        if (i == 0) {
            u32 j;

            for (j = 1; j < pointCount; j++) {
                Vec3f side;
                Vec3f up;
                if (!calcTubeCross(&side, &up, mPathPoints[j] - mPathPoints[i])) {
                    continue;
                }

                prev.set(mPathPoints[i], side, up);
                break;
            }

            if (j >= pointCount) {
                break;
            }

            prevPoint = mPathPoints[0];
            prevTexCoords[0].set(0.0f, 1.0f);
            prevTexCoords[1].set(1.0f, 1.0f);
            continue;
        }

        f32 v = cNum_drawCylinder_f1 * (static_cast< f32 >(i + 1) / pointCount) - 1.0f;

        if (v < 0.0f) {
            v = -v;
        }

        curTexCoords[0].set(0.0f, v);
        curTexCoords[1].set(1.0f, v);

        Vec3f side;
        Vec3f up;
        if (!calcTubeCross(&side, &up, mPathPoints[i] - prevPoint)) {
            continue;
        }

        cur.set(mPathPoints[i], side, up);

        // A segment skipped for a degenerate cross keeps the numbers of the ones after it.
        site.setPart(firstPart + i);
        VertexPT* vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_QUADS, 8);

        sendVertex(vtx, prev.mSidePlus, prevTexCoords[0]);
        sendVertex(vtx, cur.mSidePlus, curTexCoords[0]);
        sendVertex(vtx, cur.mSideMinus, curTexCoords[1]);
        sendVertex(vtx, prev.mSideMinus, prevTexCoords[1]);
        sendVertex(vtx, prev.mUpPlus, prevTexCoords[0]);
        sendVertex(vtx, cur.mUpPlus, curTexCoords[0]);
        sendVertex(vtx, cur.mUpMinus, curTexCoords[1]);
        sendVertex(vtx, prev.mUpMinus, prevTexCoords[1]);

        prev = cur;
        prevTexCoords[0] = curTexCoords[0];
        prevTexCoords[1] = curTexCoords[1];
        prevPoint = mPathPoints[i];
    }
}
