#include "Game/Util/CameraUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/MapObj/TrampleStar.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f8, "init.f8", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f9, "init.f9", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f10, "init.f10", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f11, "init.f11", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f12, "init.f12", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f13, "init.f13", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f14, "init.f14", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f15, "init.f15", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f16, "init.f16", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f17, "init.f17", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f18, "init.f18", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f19, "init.f19", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f20, "init.f20", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f21, "init.f21", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f22, "init.f22", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f23, "init.f23", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f24, "init.f24", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f25, "init.f25", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f26, "init.f26", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_divide_f1, "divide.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_divide_f2, "divide.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveVtx_f1, "moveVtx.f1", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveVtx_f2, "moveVtx.f2", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveVtx_f3, "moveVtx.f3", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 5000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAnim_f1, "calcAnim.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAnim_f2, "calcAnim.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_offLod_f1, "offLod.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBindingCapture_f1, "exeBindingCapture.f1", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBindingCapture_f2, "exeBindingCapture.f2", 0.85f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBindingShoot_f1, "exeBindingShoot.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBindingShoot_f2, "exeBindingShoot.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBindingShoot_f3, "exeBindingShoot.f3", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBindingShoot_f4, "exeBindingShoot.f4", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBindingShoot_f5, "exeBindingShoot.f5", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBindingShoot_f6, "exeBindingShoot.f6", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBindingShoot_f7, "exeBindingShoot.f7", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcNearestVtxIndex_f1, "calcNearestVtxIndex.f1", 5000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f1, "receiveOtherMsg.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f2, "receiveOtherMsg.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f3, "receiveOtherMsg.f3", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f4, "receiveOtherMsg.f4", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f5, "receiveOtherMsg.f5", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f6, "receiveOtherMsg.f6", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSelf_f1, "drawSelf.f1", 1000000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSelf_f2, "drawSelf.f2", 1000000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSelf_f3, "drawSelf.f3", 1000000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSelf_f4, "drawSelf.f4", 255.0f);

namespace {
    // Shadow-space x and z onto the shadow texture: s = x * scale + 0.5, t = -z * scale + 0.5, q = 1.
    NEBULA_TUNABLE(f32, cShadowTexScale, 0.003331f);
    NEBULA_TUNABLE(f32, cShadowTexOffset, 0.5f);
    NEBULA_TUNABLE(f32, cShadowTexQ, 1.0f);
};  // namespace

namespace {
    NEW_NERVE(TrampleStarNrvWait, TrampleStar, Wait);
    NEW_NERVE(TrampleStarNrvBindingCapture, TrampleStar, BindingCapture);
    NEW_NERVE(TrampleStarNrvBindingShoot, TrampleStar, BindingShoot);
};  // namespace

TrampleStarShare::TrampleStarShare(TrampleStar* pTrampleStar) : LiveActor("TrampleStarShare"), mTrampleStar(pTrampleStar) {
    makeActorDead();
}

void TrampleStarShare::copy(TrampleStar* pTrampleStar) const {
    pTrampleStar->mVtxs = mTrampleStar->mVtxs;
    pTrampleStar->mSurfaces = mTrampleStar->mSurfaces;
    pTrampleStar->mLodSurfaces = mTrampleStar->mLodSurfaces;
    pTrampleStar->mVtxOverlap = mTrampleStar->mVtxOverlap;
    pTrampleStar->mTexST = mTrampleStar->mTexST;
    pTrampleStar->mNumSurfaces = mTrampleStar->mNumSurfaces;
    pTrampleStar->mNumVtxs = mTrampleStar->mNumVtxs;
}

TrampleStar::TrampleStar(const char* pName) : LiveActor(pName), mIsLod(false) {
}

void TrampleStar::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_MapObj, MR::DrawBufferType_None, MR::DrawType_FlexibleSphere);

    mTexture = new GameTexture(MR::loadTexFromArc("TrampleStar", "TrampleStar.bti"));

    f32 scale = cNum_init_f1;
    scale *= mScale.x;
    initHitSensor(1);
    MR::addHitSensorPriorBinder(this, "body", 4, scale, Vec3f(0.0f, 0.0f, 0.0f));

    mNumSurfaces = 12;
    mNumVtxs = 24;
    mDeformCoeff = new f32[mNumVtxs * 256];
    mDeformSpeed = new f32[mNumVtxs * 256];

    bool isShareStar = false;

    for (u32 actorIndex = 0; actorIndex < MR::getAllLiveActorGroup()->getObjNum(); actorIndex++) {
        LiveActor* actor = MR::getAllLiveActorGroup()->getActor(actorIndex);

        if (MR::isEqualString(actor->mName, "TrampleStarShare")) {
            isShareStar = true;
            static_cast< TrampleStarShare* >(actor)->copy(this);
            break;
        }
    }

    if (!isShareStar) {
        mVtxs = new Vec3f[mNumVtxs * 256];
        mSurfaces = new Surface[mNumSurfaces * 256];
        mLodSurfaces = new Surface[static_cast< u32 >(mNumSurfaces * 256) / 4];
        mVtxOverlap = new s16[mNumVtxs * 256];
        mTexST = new Vec2f[mNumVtxs * 256];

        mVtxs[0].set(-cNum_init_f2, -cNum_init_f3, -cNum_init_f4);
        mVtxs[1].set(-cNum_init_f5, -cNum_init_f6, cNum_init_f7);
        mVtxs[2].set(cNum_init_f8, -cNum_init_f9, cNum_init_f10);
        mVtxs[3].set(cNum_init_f11, -cNum_init_f12, -cNum_init_f13);
        mVtxs[4].set(-cNum_init_f14, cNum_init_f15, -cNum_init_f16);
        mVtxs[5].set(-cNum_init_f17, cNum_init_f18, cNum_init_f19);
        mVtxs[6].set(cNum_init_f20, cNum_init_f21, cNum_init_f22);
        mVtxs[7].set(cNum_init_f23, cNum_init_f24, -cNum_init_f25);

        mVtxs[8] = mVtxs[0];
        mVtxs[9] = mVtxs[3];
        mVtxs[10] = mVtxs[7];
        mVtxs[11] = mVtxs[4];

        mVtxs[12] = mVtxs[3];
        mVtxs[13] = mVtxs[2];
        mVtxs[14] = mVtxs[6];
        mVtxs[15] = mVtxs[7];

        mVtxs[16] = mVtxs[2];
        mVtxs[17] = mVtxs[1];
        mVtxs[18] = mVtxs[5];
        mVtxs[19] = mVtxs[6];

        mVtxs[20] = mVtxs[1];
        mVtxs[21] = mVtxs[0];
        mVtxs[22] = mVtxs[4];
        mVtxs[23] = mVtxs[5];

        mSurfaces[0].set(1, 0, 2);
        mSurfaces[1].set(2, 0, 3);
        mSurfaces[2].set(18, 17, 16);
        mSurfaces[3].set(18, 16, 19);
        mSurfaces[4].set(21, 23, 22);
        mSurfaces[5].set(21, 20, 23);
        mSurfaces[6].set(13, 12, 15);
        mSurfaces[7].set(14, 13, 15);
        mSurfaces[8].set(10, 9, 11);
        mSurfaces[9].set(11, 9, 8);
        mSurfaces[10].set(4, 5, 6);
        mSurfaces[11].set(4, 6, 7);

        for (u32 idx = 0; idx < 24; idx += 4) {
            mTexST[idx + 0].set(0.0f, 0.0f);
            mTexST[idx + 1].set(1.0f, 0.0f);
            mTexST[idx + 2].set(1.0f, 1.0f);
            mTexST[idx + 3].set(0.0f, 1.0f);
        }

        for (u32 idx = 0; idx < 3; idx++) {
            divide();

            if (idx == 1) {
                for (u32 surface = 0; surface < mNumSurfaces; surface++) {
                    mLodSurfaces[surface] = mSurfaces[surface];
                }
            }
        }

        for (u32 vtx = 0; vtx < mNumVtxs; vtx++) {
            mVtxOverlap[vtx] = -1;
        }

        for (u32 vtxA = 0; vtxA < mNumVtxs; vtxA++) {
            for (u32 vtxB = vtxA + 1; vtxB < mNumVtxs; vtxB++) {
                if ((mVtxs[vtxA] - mVtxs[vtxB]).length() < 1.0f) {
                    if (mVtxOverlap[vtxA] != -1) {
                        mVtxOverlap[vtxB] = mVtxOverlap[vtxA];
                        mVtxOverlap[vtxA] = vtxB;
                    } else {
                        mVtxOverlap[vtxA] = vtxB;
                        mVtxOverlap[vtxB] = vtxA;
                    }

                    break;
                }
            }

        }

        toSphere(scale);

        new TrampleStarShare(this);
    }

    mNumDrawVtxs = 0;
    mDrawPos = new Vec3f[mNumSurfaces * 3];
    mDrawNorm = new Vec3f[mNumSurfaces * 3];
    mDrawTex = new Vec2f[mNumSurfaces * 3];
    mDrawDeformCoeffs = new f32[mNumSurfaces * 3];

    for (u32 vtx = 0; vtx < mNumVtxs; vtx++) {
        mDeformCoeff[vtx] = 0.0f;
        mDrawDeformCoeffs[vtx] = -cNum_init_f26;
    }

    calcSurface(false);
    mIsLod = false;

    initNerve(GET_NERVE_ANON(TrampleStarNrvWait));
    MR::initLightCtrlNoDrawMapObj(this);
    makeActorAppeared();
    MR::setClippingTypeSphere(this, scale);
    MR::setClippingFarMax(this);
}

void TrampleStar::toSphere(f32 radius) {
    for (u32 vtx = 0; vtx < mNumVtxs; vtx++) {
        mVtxs[vtx].setLength(radius);
    }
}

void TrampleStar::initCheckFlag(u32 numFlags) {
    mCheckFlags = new u16[numFlags * numFlags];
    mNumFlags = numFlags;

    for (u32 idx = 0; idx < mNumFlags * mNumFlags; idx++) {
        mCheckFlags[idx] = 0;
    }
}

u16 TrampleStar::readCheckFlag(u16 vtxA, u16 vtxB) {
    return mCheckFlags[vtxA + vtxB * mNumFlags];
}

void TrampleStar::writeCheckFlag(u16 vtxA, u16 vtxB, u16 value) {
    mCheckFlags[vtxA + vtxB * mNumFlags] = value;
    mCheckFlags[vtxB + vtxA * mNumFlags] = value;
}

void TrampleStar::divide() {

    initCheckFlag(mNumVtxs);
    u16 numSurfaces = mNumSurfaces;
    u16 numVtxs = mNumVtxs;

    u16 midpoints[3];

    for (u32 surface = 0; surface < mNumSurfaces; surface++) {
        for (u32 idx = 0; idx < 3; idx++) {
            u16 vtx1 = mSurfaces[surface].mVtxs[idx];
            u16 vtx2 = mSurfaces[surface].mVtxs[(idx + 1) % 3];

            if (readCheckFlag(vtx1, vtx2) != 0) {
                midpoints[idx] = readCheckFlag(vtx1, vtx2);
            } else {
                mVtxs[numVtxs] = (mVtxs[vtx1] + mVtxs[vtx2]) * cNum_divide_f1;

                mTexST[numVtxs] = (mTexST[vtx1] + mTexST[vtx2]) * cNum_divide_f2;

                writeCheckFlag(vtx1, vtx2, numVtxs);
                midpoints[idx] = numVtxs;
                numVtxs++;
            }
        }

        mSurfaces[numSurfaces].set(midpoints[0], mSurfaces[surface].mVtxs[1], midpoints[1]);
        numSurfaces++;
        mSurfaces[numSurfaces].set(midpoints[0], midpoints[1], midpoints[2]);
        numSurfaces++;
        mSurfaces[numSurfaces].set(midpoints[1], mSurfaces[surface].mVtxs[2], midpoints[2]);
        numSurfaces++;
        mSurfaces[surface].set(mSurfaces[surface].mVtxs[0], midpoints[0], midpoints[2]);
    }

    mNumSurfaces = numSurfaces;
    mNumVtxs = numVtxs;
    delete[] mCheckFlags;
}

void TrampleStar::moveVtx(f32 deformRate, u32 vtxIdx, u32 /* unused */, f32 /* unused */, u32 /* unused */) {
    Vec3f vtxPos(mVtxs[vtxIdx]);

    for (u32 vtx = 0; vtx < mNumVtxs; vtx++) {
        f32 dist = (mVtxs[vtx] - vtxPos).length();

        if (!(dist >= cNum_moveVtx_f1)) {
            f32 t = (cNum_moveVtx_f2 - dist) / cNum_moveVtx_f3;
            mDeformCoeff[vtx] += deformRate * (t * t);
        }
    }
}

void TrampleStar::control() {
    if (MR::calcDistanceToPlayer(this) >= cNum_control_f1) {
        onLod();
    } else {
        offLod();
    }
}

void TrampleStar::calcAnim() {
    calcSurface(false);

    if (!isNerve(GET_NERVE_ANON(TrampleStarNrvWait))) {
        for (u32 idx = 0; idx < mNumVtxs; idx++) {
            s16 idx2 = mVtxOverlap[idx];

            if (idx2 != -1) {
                f32 coeff = (mDeformCoeff[idx] + mDeformCoeff[idx2]) * cNum_calcAnim_f1;
                mDeformCoeff[idx] = coeff;
                mDeformCoeff[mVtxOverlap[idx]] = coeff;
                f32 speed = (mDeformSpeed[idx] + mDeformSpeed[mVtxOverlap[idx]]) * cNum_calcAnim_f2;
                mDeformSpeed[idx] = speed;
                mDeformSpeed[mVtxOverlap[idx]] = speed;
            }
        }
    }

    LiveActor::calcAnim();
}

void TrampleStar::calcSurface(bool calcDrawBuffers) {
    if (mIsLod) {
        return;
    }

    mNumDrawVtxs = 0;

    for (u32 surface = 0; surface < mNumSurfaces; surface++) {
        for (u32 idx = 0; idx < 3; idx++) {
            f32 currDeform = mDrawDeformCoeffs[mNumDrawVtxs];
            mDrawDeformCoeffs[mNumDrawVtxs] = mDeformCoeff[mSurfaces[surface].mVtxs[idx]];

            if (!calcDrawBuffers && currDeform == mDeformCoeff[mSurfaces[surface].mVtxs[idx]]) {
                mNumDrawVtxs++;
            } else {
                u32 vtx = mSurfaces[surface].mVtxs[idx];
                Vec3f pos = mVtxs[vtx];
                Vec3f up = pos;
                MR::normalizeOrZero(&up);
                Vec2f texST = mTexST[vtx];

                pos += mPosition - up * mDeformCoeff[vtx];
                mDrawPos[mNumDrawVtxs] = pos;
                mDrawNorm[mNumDrawVtxs] = up;
                mDrawTex[mNumDrawVtxs] = texST;
                mNumDrawVtxs++;
            }
        }
    }

}

void TrampleStar::calcLodSurface() {
    mNumDrawVtxs = 0;

    for (u32 surface = 0; surface < mNumSurfaces / 4; surface++) {
        for (u32 idx = 0; idx < 3; idx++) {
            u32 vtx = mLodSurfaces[surface].mVtxs[idx];
            Vec3f pos = mVtxs[vtx];
            Vec3f up = pos;
            MR::normalizeOrZero(&up);
            Vec2f& texST = mTexST[vtx];

            pos += mPosition;
            mDrawPos[mNumDrawVtxs] = pos;
            mDrawNorm[mNumDrawVtxs] = up;
            mDrawTex[mNumDrawVtxs] = texST;
            mNumDrawVtxs++;
        }
    }

}

void TrampleStar::onLod() {
    if (!mIsLod) {
        calcLodSurface();
        mIsLod = true;
    }
}

void TrampleStar::offLod() {
    if (mIsLod) {
        mIsLod = false;

        for (u32 idx = 0; idx < mNumVtxs; idx++) {
            mDeformCoeff[idx] = 0.0f;
            mDrawDeformCoeffs[idx] = -cNum_offLod_f1;
        }

        calcSurface(true);
    }
}

void TrampleStar::exeWait() {
}

void TrampleStar::exeBindingCapture() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("Wait");
        MR::startSoundPlayer("SE_OJ_TRAMPOLINE_LAND_L");
    }

    Vec3f down = -mVtxs[mCaptureVtx];
    MR::normalizeOrZero(&down);
    moveVtx(mDeformRate, mCaptureVtx, 8, cNum_exeBindingCapture_f1, 0);
    mDeformRate = cNum_exeBindingCapture_f2 * mDeformRate;

    if (getNerveStep() > mCaptureFrames) {
        setNerve(GET_NERVE_ANON(TrampleStarNrvBindingShoot));
    }
}

void TrampleStar::exeBindingShoot() {
    if (MR::isFirstStep(this)) {
        mDeformRate = 0.0f;
        mSpringRate = cNum_exeBindingShoot_f1;

        for (u32 vtx = 0; vtx < mNumVtxs; vtx++) {
            s16 vtx2 = mVtxOverlap[vtx];

            if (vtx2 != -1) {
                f32 coeff = (mDeformCoeff[vtx] + mDeformCoeff[vtx2]) * cNum_exeBindingShoot_f2;
                mDeformCoeff[vtx] = coeff;
                mDeformCoeff[mVtxOverlap[vtx]] = coeff;
            }
        }

        for (u32 idx = 0; idx < mNumVtxs; idx++) {
            mDeformSpeed[idx] = mSpringRate * mDeformCoeff[idx];
        }
    }

    for (u32 vtx = 0; vtx < mNumVtxs; vtx++) {
        f32 coeff = mDeformCoeff[vtx];
        f32 speed = mDeformSpeed[vtx];
        f32 springRate = mSpringRate;
        mDeformCoeff[vtx] = coeff - speed;
        mDeformSpeed[vtx] += coeff * springRate;
        mDeformCoeff[vtx] *= cNum_exeBindingShoot_f3;

        if (vtx == mCaptureVtx) {
            if (coeff * mDeformCoeff[vtx] <= 0.0f) {
                mSpringRate = cNum_exeBindingShoot_f4;

                if (MR::isPlayerInBind()) {
                    Vec3f up = mVtxs[mCaptureVtx];
                    MR::normalizeOrZero(&up);

                    if (MR::getPlayerHeldJump()) {
                        mJumpVel *= cNum_exeBindingShoot_f5;
                        MR::startSoundPlayer("SE_OJ_TRAMPOLINE_BOUND_L");
                        MR::startSoundPlayer("SE_PV_JUMP_JOY");
                    } else {
                        MR::startSoundPlayer("SE_OJ_TRAMPOLINE_BOUND_S");
                        MR::startSoundPlayer("SE_PV_JUMP_M");
                    }

                    MR::endBindAndPlayerJump(this, up * mJumpVel, 0);
                    MR::startBckPlayer("Jump2");
                    setNerve(GET_NERVE_ANON(TrampleStarNrvWait));
                }
            }

            if (speed * mDeformSpeed[vtx] <= 0.0f) {
                mSpringRate = cNum_exeBindingShoot_f6;
            }
        }
    }

    if (getNerveStep() == NebulaFrameClock::stepsOfSeconds(2)) {
        if (MR::isPlayerInBind()) {
            Vec3f up = mVtxs[mCaptureVtx];
            MR::normalizeOrZero(&up);

            if (MR::getPlayerHeldJump()) {
                mJumpVel *= cNum_exeBindingShoot_f7;
            }

            MR::endBindAndPlayerJump(this, up * mJumpVel, 0);
        }

        setNerve(GET_NERVE_ANON(TrampleStarNrvWait));

        for (u32 vtx = 0; vtx < mNumVtxs; vtx++) {
            mDeformSpeed[vtx] = 0.0f;
        }

        for (u32 vtx = 0; vtx < mNumVtxs; vtx++) {
            mDeformCoeff[vtx] = 0.0f;
        }
    }
}

u32 TrampleStar::calcNearestVtxIndex(const Vec3f& rPos) {
    f32 nearestDistance = cNum_calcNearestVtxIndex_f1;
    u32 nearestIndex = 0;

    for (u32 vtx = 0; vtx < mNumVtxs; vtx++) {
        f32 dist = (mVtxs[vtx] + mPosition - rPos).length();

        if (dist < nearestDistance) {
            nearestDistance = dist;
            nearestIndex = vtx;
        }
    }

    return nearestIndex;
}

void TrampleStar::draw() const {
    if (!MR::isDead(this)) {
        drawSelf();
        MR::resetDrawState();
    }
}

bool TrampleStar::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_AUTORUSH_BEGIN) {
        mCaptureFrames = 10;
        mDeformRate = MR::getPlayerVelocity()->length();

        if (mDeformRate < cNum_receiveOtherMsg_f1) {
            mDeformRate = cNum_receiveOtherMsg_f2;
        }

        if (mDeformRate > cNum_receiveOtherMsg_f3) {
            mDeformRate = cNum_receiveOtherMsg_f4;
            mCaptureFrames = 15;
        }

        mJumpVel = mDeformRate;

        if (mDeformRate < cNum_receiveOtherMsg_f5) {
            mJumpVel = cNum_receiveOtherMsg_f6;
        }

        mCaptureVtx = calcNearestVtxIndex(pSender->mPosition);

        if (MR::getPlayerVelocity()->dot(mVtxs[mCaptureVtx]) > 0.0f) {
            return false;
        }

        setNerve(GET_NERVE_ANON(TrampleStarNrvBindingCapture));
        return true;
    }

    if (msg == ACTMES_UPDATE_BASEMTX) {
        Vec3f side;
        MR::getPlayerSideVec(&side);
        Vec3f capturePos = mVtxs[mCaptureVtx];
        Vec3f down = -capturePos;
        down.normalize();
        Pos3f mtx;
        capturePos += mPosition + down * mDeformCoeff[mCaptureVtx];
        MR::makeMtxUpSidePos(&mtx, -down, side, capturePos);
        MR::setPlayerBaseMtx(mtx);
        return true;
    }

    return false;
}

void TrampleStar::drawSelf() const {
    if (mNumDrawVtxs == 0) {
        return;
    }

    MR::loadActorLight(this);

    if (mTexture == nullptr) {
        return;
    }

    if (mTexST == nullptr) {
        return;
    }

    Vec3f playerPos = *MR::getPlayerPos();
    Vec3f diff = mPosition - playerPos;
    Vec3f shadowPos = MR::getMarioShadowVec();
    Pos3f texMtx;
    Pos3f toPlayer;
    Rot3f rot;
    Pos3f shadowFrame;

    if (MR::vecKillElement(diff, shadowPos, &diff) > 0.0f) {
        NebulaMtxTrans(toPlayer, -playerPos.x, -playerPos.y, -playerPos.z);
    } else {
        NebulaMtxTrans(toPlayer, cNum_drawSelf_f1, cNum_drawSelf_f2, cNum_drawSelf_f3);
    }

    MR::makeMtxRotate(rot, -*MR::getPlayerRotate());

    NebulaMtxIdentity(shadowFrame);

    Vec3f front;
    MR::getPlayerFrontVec(&front);

    if (MR::isNearZero(front)) {
        return;
    }

    if (MR::isNearZero(MR::getMarioShadowVec())) {
        return;
    }

    if (MR::isSameDirection(MR::getMarioShadowVec(), front) || MR::isOppositeDirection(MR::getMarioShadowVec(), front)) {
        return;
    }

    MR::makeMtxUpFront(&shadowFrame, -MR::getMarioShadowVec(), -front);
    NebulaMtxInverse(shadowFrame, shadowFrame);

    MR::multMtx(texMtx, toPlayer, shadowFrame);
    Pos3f texProj;

    for (s32 row = 0; row < 3; row++) {
        for (s32 col = 0; col < 4; col++) {
            texProj[row][col] = 0.0f;
        }
    }

    texProj[0][0] = cShadowTexScale;
    texProj[1][2] = -cShadowTexScale;
    texProj[2][3] = cShadowTexQ;
    texProj[0][3] = cShadowTexOffset;
    texProj[1][3] = cShadowTexOffset;
    MR::multMtx(texMtx, texMtx, texProj);

    // runtime/src/render/materials/trample_star.hlsl: blended by alpha, depth tested early and written, no culling.
    SiteMaterial material;
    TDDraw::setupNative(&material, 0, 1, 0);
    material.setShader(NEBULA_SHADER_TRAMPLE_STAR);
    material.setDepth(true, NEBULA_COMPARE_LEQUAL, true);
    material.setMatrix(3, 0, texMtx);
    material.setColor(0, 0xCC, 0xCC, 0xCC, 0xFF);
    material.setColor(4, 0x7F, 0x7F, 0x93, 0xC3);
    material.constants[8] = 0xB4 / cNum_drawSelf_f4;
    material.constants[9] = MR::getPlayerShadowHeight() < MR::calcDistanceToPlayer(this) ? 0.0f : 1.0f;
    material.setTexture(0, mTexture);
    material.setTexture(1, MR::getMarioShadowTexForLoad());

    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    VertexPNT* pOut = site.vertices< VertexPNT >(NEBULA_PRIMITIVE_TRIANGLES, mNumDrawVtxs);
    {
        Vec3f* pos = mDrawPos;
        Vec3f* norm = mDrawNorm;
        Vec2f* tex = mDrawTex;

        for (u32 vtx = 0; vtx < mNumDrawVtxs; vtx++) {
            *pOut++ = {pos->x, pos->y, pos->z, norm->x, norm->y, norm->z, tex->x, tex->y};
            pos++;
            norm++;
            tex++;
        }
    }

}
