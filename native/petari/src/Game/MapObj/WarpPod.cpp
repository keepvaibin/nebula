#include "Game/MapObj/WarpPod.hpp"
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
#include <JSystem/JUtility/JUTTexture.hpp>
#include <cstdio>

NebulaColor8 gGlowEffectEnvColor[] = {
    {0, 100, 200}, {44, 255, 42}, {255, 60, 60}, {196, 166, 0}, {0, 255, 0}, {255, 0, 255}, {255, 255, 0}, {255, 255, 255},
};

namespace {
    static f32 cSensorRadius0 = 120.0f;
    static f32 cSensorRadius1 = 15.0f;
};  // namespace

namespace MR {
    static u32 mDrawTimer;

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

    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_WarpPodPath);
}

WarpPod* WarpPodMgr::getPairPod(const LiveActor* pParam1) {
    if (static_cast< const WarpPod* >(pParam1)->mPlacementId == nullptr) {
        return nullptr;
    }

    for (u32 i = 0; i < mPods->getObjNum(); i++) {
        WarpPod* pWarpPod = static_cast< WarpPod* >(mPods->getActor(i));

        if (pWarpPod == pParam1) {
            continue;
        }

        if (pWarpPod->mPlacementId == nullptr) {
            continue;
        }

        if (*pWarpPod->mPlacementId == *static_cast< const WarpPod* >(pParam1)->mPlacementId) {
            return pWarpPod;
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

    WarpPod* pPairPod = getPairPod(mCameraPod);
    pPairPod->mDelay = 60;
    MR::startBck(pPairPod, "Wait");
    MR::startBrk(pPairPod, "Wait");

    mCameraPod = nullptr;
}

void WarpPodMgr::notifyWarpEnd(WarpPod* pWarpPod) {
    if (pWarpPod == nullptr) {
        return;
    }

    WarpPod* pPairPod = getPairPod(pWarpPod);
    pPairPod->mDelay = 60;
    MR::startBck(pPairPod, "Wait");
    MR::startBrk(pPairPod, "Wait");

    mCameraPod = nullptr;
}

void WarpPodMgr::draw() const {
    for (u32 i = 0; i < mPods->getObjNum(); i++) {
        static_cast< WarpPod* >(mPods->getActor(i))->drawCylinder(MR::mDrawTimer);
    }

    MR::mDrawTimer++;
}

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
    mArg3 = -1;
    mGrandstarReq = -1;
    mCameraTime = 120;
    mGlowColorIndex = 0;
    s32 arg7 = -1;

    if (MR::isValidInfo(rIter)) {
        MR::getPlacementArg1NoInit(rIter, &mVisibilityState);
        MR::getPlacementArg2NoInit(rIter, &mArg2);
        MR::getPlacementArg3NoInit(rIter, &mArg3);
        MR::getPlacementArg4NoInit(rIter, &mGrandstarReq);
        MR::getPlacementArg5NoInit(rIter, &mCameraTime);
        MR::getPlacementArg6NoInit(rIter, &mGlowColorIndex);
        MR::getPlacementArg7NoInit(rIter, &arg7);
    }

    if (arg7 == 1) {
        mArg7 = true;
    } else {
        mArg7 = false;
    }

    mCamInfo = new ActorCameraInfo(rIter);

    s32 arg0;
    MR::getPlacementArg0WithInit(rIter, &arg0);

    char eventCameraName[256];
    nebula_sprintf(eventCameraName, "ワープカメラ %d-%c", groupId, arg0 + 65);
    MR::declareEventCamera(mCamInfo, eventCameraName);

    const size_t eventCameraSize = strlen(eventCameraName) + 1;
    mEventCameraName = new char[eventCameraSize];
    memcpy(mEventCameraName, eventCameraName, eventCameraSize);

    if (mVisibilityState == 0) {
        MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
    } else {
        MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_MapObj, MR::DrawBufferType_MapObj, MR::DrawType_None);
    }

    initSound(4, false);
    initHitSensor(1);

    f32 scale = mScale.x;
    f32 radius = mVisibilityState == 0 ? scale * ::cSensorRadius0 : scale * ::cSensorRadius1;
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

    bool isNonActive = false;

    if (MR::calcOpenedAstroDomeNum() < mGrandstarReq) {
        isNonActive = true;
    }

    if (mArg3 == 0) {
        s32 index = MR::getWarpPodManager()->mNextPairIndex++;
        mPathFlagIndex = index;

        if (MR::isOnWarpPodPathFlag(mPathFlagIndex)) {
            isNonActive = false;
        } else {
            isNonActive = true;
        }
    } else if (!isNonActive) {
        glowEffect();
    }

    if (isNonActive) {
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

    MR::emitEffect(this, "EndGlow");
    MR::setEffectEnvColor(this, "EndGlow", gGlowEffectEnvColor[mGlowColorIndex].r, gGlowEffectEnvColor[mGlowColorIndex].g,
                          gGlowEffectEnvColor[mGlowColorIndex].b);
}

void WarpPod::initPair() {
    mPairPod = MR::getWarpPodManager()->getPairPod(this);

    bool someBool = false;

    if (mPairPod->mPosition.x > mPosition.x) {
        someBool = true;
    } else if (mPairPod->mPosition.x < mPosition.x) {
        someBool = false;
    } else if (mPairPod->mPosition.y < mPosition.y) {
        someBool = true;
    } else if (mPairPod->mPosition.y < mPosition.y) {
        someBool = false;
    } else if (mPairPod->mPosition.z < mPosition.z) {
        someBool = true;
    } else if (mPairPod->mPosition.z < mPosition.z) {
        someBool = false;
    }

    if (mPairPod->mArg7 != true && mArg7 != true) {
        if (!mArg3) {
            mArg7 = false;
        } else if (!mPairPod->mArg3) {
            mArg7 = true;
        } else {
            mArg7 = someBool;
        }
    }

    initDraw();

    if (!mIsInactive) {
        return;
    }

    if (!mArg7 && mPairPod->mIsInactive) {
        return;
    }

    char buf[256];
    nebula_sprintf(buf, "wPod出現カメラ %d", mGroupId);

    const size_t size = strlen(buf) + 1;
    mPairCameraName = new char[size];
    memcpy(mPairCameraName, buf, size);

    MR::declareEventCamera(mCamInfo, mPairCameraName);
}

void WarpPod::appear() {
    if (mIsInactive && mPairPod->mIsInactive && !mArg7) {
        mPairPod->appear();
        mIsInactive = false;
    } else {
        mCameraStep = mCameraTime;

        MR::invalidateClipping(this);

        mIsInactive = false;

        MR::startSound(this, "SE_OJ_WARP_POD_PATH_APPEAR");
        MR::startBck(this, "Active");
        MR::startBrk(this, "Active");
        glowEffect();
    }
}

void WarpPod::appearWithDemo() {
    if (mIsInactive) {
        if (mArg3 == 0) {
            MR::setWarpPodPathFlag(mPathFlagIndex, true);
        }

        if (mPairPod->mIsInactive && !mArg7) {
            mPairPod->appearWithDemo();

            mIsInactive = false;

            return;
        }
    }

    mIsAppearDemoPending = true;

    MR::invalidateClipping(this);
}

void WarpPod::control() {
    if (!mIsAppearDemoPending) {
        return;
    }

    if (!MR::tryStartDemoWithoutCinemaFrame(this, "出現")) {
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

void WarpPod::movement() {
    if (mCameraStep != 0) {
        mCameraStep--;

        if (mCameraStep == 0) {
            MR::validateClipping(this);

            if (mIsPairCameraOn) {
                MR::endDemo(this, "出現");
            }

            mPairPod->glowEffect();
            MR::startBck(mPairPod, "Active");
            MR::startBrk(mPairPod, "Active");
        }
    } else {
        if (mDelay != 0) {
            if (mDelay == 1 && MR::calcDistanceToPlayer(mPosition) < 200.0f) {
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
}

void WarpPod::startEventCamera() const {
    if (mIsPairCameraOn || mPairPod->mIsPairCameraOn) {
        return;
    }

    MR::startEventCameraNoTarget(mCamInfo, mEventCameraName);
}

void WarpPod::endEventCamera() {
    if (mIsPairCameraOn) {
        MR::endEventCamera(mCamInfo, mPairCameraName, true);
        mIsPairCameraOn = false;
    } else if (mPairPod->mIsPairCameraOn) {
        mPairPod->endEventCamera();
    } else {
        MR::endEventCamera(mCamInfo, mEventCameraName, true);
    }
}

void WarpPod::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mIsInactive) {
        return;
    }

    if (!MR::isSensorPlayer(pReceiver)) {
        return;
    }

    if (mPairPod->mIsInactive) {
        mPairPod->appearWithDemo();
        MR::invalidateClipping(this);
    } else if (mDelay != 0) {
        if (mDelay < 30) {
            mDelay = 30;
        }
    } else {
        MR::sendArbitraryMsg(ACTMES_WARP, pReceiver, pSender);
    }
}

namespace {
    void sendVertex(VertexPT*& rVtx, const Vec3f& rPosition, const Vec2f& rCoord) {
        *rVtx++ = {rPosition.x, rPosition.y, rPosition.z, rCoord.x, rCoord.y};
    }
}  // namespace

// FIXME: big tvec mess
void WarpPod::initDraw() {
    if (!mArg7) {
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
    Vec3f center(mPosition + direction * 0.5f);
    normal.cross(side, direction);
    MR::normalizeOrZero(&normal);

    f32 halfAngle = PI / 4.0f;
    f32 radius = (0.5f * distance) / MR::sin(halfAngle);
    f32 height = MR::sqrt(radius * radius - 0.5f * (0.5f * distance * distance));
    Vec3f axis(side);
    Vec3f arcCenter(center + normal * height);
    Vec3f arcStart = -normal * radius;

    f32 startAngle = -halfAngle;
    u16 count = 60;
    u16 remaining = count;
    mPathPoints = new Vec3f[count];
    mNumPathPoints = count;

    for (u32 i = 0; i < count; remaining--, i++) {
        f32 blend = (MR::sin(((count - remaining - 0.5f * count) / count) * MR::pi()) + 1.0f) / 2.0f;

        if (mVisibilityState == 2) {
            blend = 1.0f - (remaining - 1) / static_cast< f32 >(count);
        }

        Pos3f rotation;
        NebulaMtxRotAxisRad(rotation, axis, startAngle * (1.0f - blend) + halfAngle * blend);
        Vec3f point;
        NebulaMtxMultVecSR(rotation, arcStart, point);
        Vec3f position(arcCenter + point);
        mPathPoints[i] = position + up * 200.0f;
    }

    mGlowTexture = new JUTTexture(MR::getTexture(MR::getResourceHolder(this), "TestColor.bti"));
}

void WarpPod::drawCylinder(u32) const {
    f32 length = 30.0f;

    if (!mArg7) {
        return;
    }

    if (mPairPod->mIsInactive) {
        return;
    }

    if (mIsInactive) {
        return;
    }

    if (mVisibilityState != 1) {
        return;
    }

    // runtime/src/render/materials/tex_alpha_lerp.hlsl: the glow colour to white by the texture's alpha, opaque
    // added; depth tested.
    SiteMaterial material;
    TDDraw::setupNative(&material, 1, 2, 0);
    material.mShader = NEBULA_SHADER_TEX_ALPHA_LERP;
    material.setDepth(true, NEBULA_COMPARE_LEQUAL, false);
    const NebulaColor8& glow = ::gGlowEffectEnvColor[mGlowColorIndex];
    material.setColor(0, glow.r, glow.g, glow.b, glow.a);
    material.setColor(4, 255, 255, 255, 255);
    material.constants[10] = 1.0f;
    material.setTexture(0, mGlowTexture);

    s32 val1 = mCameraStep;
    u32 val2 = mNumPathPoints;
    s32 val3 = mCameraTime;

    if (val1 == 0) {
        val1 = mPairPod->mCameraStep;
        val3 = mPairPod->mCameraTime;
    }

    if (val1 != 0) {
        val2 *= 1 - static_cast< f32 >(val1) / val3;
    }

    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    VertexPT* vtx;
    Vec3f vec84, vec90, vec9C, vecA8, vecB4, vecC0, vecCC, vecD8, vecE4;
    Vec2f currentTexCoords[2];
    Vec2f previousTexCoords[2];

    for (u32 i = 0; i < val2; i++) {
        if (i == 0) {
            u32 j;

            for (j = 1; j < val2; j++) {
                Vec3f direction = mPathPoints[j] - mPathPoints[i];
                Vec3f vec11C;
                MR::vecKillElement(direction, MR::getCamZdir(), &vec11C);

                if (MR::normalizeOrZero(&vec11C)) {
                    continue;
                }

                Vec3f vec128;
                vec128.cross(vec11C, MR::getCamZdir());
                MR::normalizeOrZero(&vec128);

                Vec3f vec134;
                vec134.cross(vec128, vec11C);
                MR::normalizeOrZero(&vec134);

                vec128.setLength(length);
                vec134.setLength(length);

                vecA8 = mPathPoints[i] + vec128;
                vec9C = mPathPoints[i] - vec128;
                vec90 = mPathPoints[i] + vec134;
                vec84 = mPathPoints[i] - vec134;

                break;
            }

            if (j >= val2) {
                break;
            }

            vecE4 = mPathPoints[0];

            previousTexCoords[0].set(0.0f, 1.0f);
            previousTexCoords[1].set(1.0f, 1.0f);

            continue;
        }

        f32 val = 2.0f * (static_cast< f32 >(i + 1) / val2) - 1.0f;

        if (val < 0.0f) {
            val = -val;
        }

        currentTexCoords[0].set(0.0f, val);
        currentTexCoords[1].set(1.0f, val);

        Vec3f direction = mPathPoints[i] - vecE4;
        Vec3f vec14C;
        MR::vecKillElement(direction, MR::getCamZdir(), &vec14C);

        if (MR::normalizeOrZero(&vec14C)) {
            continue;
        }

        Vec3f vec158;
        vec158.cross(vec14C, MR::getCamZdir());
        MR::normalizeOrZero(&vec158);

        Vec3f vec164;
        vec164.cross(vec158, vec14C);
        MR::normalizeOrZero(&vec164);

        vec158.setLength(length);
        vec164.setLength(length);

        vecD8 = mPathPoints[i] + vec158;
        vecCC = mPathPoints[i] - vec158;
        vecC0 = mPathPoints[i] + vec164;
        vecB4 = mPathPoints[i] - vec164;

        vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_QUADS, 8);

        sendVertex(vtx, vecA8, previousTexCoords[0]);
        sendVertex(vtx, vecD8, currentTexCoords[0]);
        sendVertex(vtx, vecCC, currentTexCoords[1]);
        sendVertex(vtx, vec9C, previousTexCoords[1]);
        sendVertex(vtx, vec90, previousTexCoords[0]);
        sendVertex(vtx, vecC0, currentTexCoords[0]);
        sendVertex(vtx, vecB4, currentTexCoords[1]);
        sendVertex(vtx, vec84, previousTexCoords[1]);

        vecA8 = vecD8;
        previousTexCoords[0] = currentTexCoords[0];
        vec9C = vecCC;
        previousTexCoords[1] = currentTexCoords[1];
        vec90 = vecC0;
        vec84 = vecB4;
        vecE4 = mPathPoints[i];
    }
}

