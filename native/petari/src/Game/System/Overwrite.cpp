#include "JSystem/JParticle/JPANativeColor.hpp"
#include "Game/System/ShapePacketUserData.hpp"
#include "Game/System/GameThreads.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/System/PlayerInput.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MotionSignal.hpp"

#include "Game/System/NebulaArena.hpp"

#include "JSystem/JUtility/JUTNativeLog.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>

#include <JSystem/JSupport/JSUInputStream.hpp>
#include <JSystem/JSupport/JSUOutputStream.hpp>

#include <JSystem/J3DGraphBase/J3DPacket.hpp>
#include <JSystem/J3DGraphBase/J3DShapeMtx.hpp>
#include <JSystem/J3DGraphBase/J3DSys.hpp>

#include <JSystem/JParticle/JPABaseShape.hpp>
#include <JSystem/JParticle/JPAEmitter.hpp>
#include <JSystem/JParticle/JPAEmitterManager.hpp>
#include <JSystem/JParticle/JPAFieldBlock.hpp>
#include <JSystem/JParticle/JPAParticle.hpp>

#include <JSystem/JAudio2/JASStream.hpp>
#include <JSystem/JAudio2/JASAiCtrl.hpp>
#include <JSystem/JAudio2/JASDriverIF.hpp>
#include <JSystem/JAudio2/JASHeapCtrl.hpp>
#include <JSystem/JAudio2/JASTrack.hpp>
#include <JSystem/JAudio2/JASWaveArcLoader.hpp>
#include <JSystem/JAudio2/JAUInitializer.hpp>

#include <cstdarg>
#include "Game/AudioLib/AudParams.hpp"
#include "Game/System/NebulaSystemSettings.hpp"

namespace {
};  // namespace

JSUOutputStream::~JSUOutputStream() {
}

JSUInputStream::~JSUInputStream() {
}

extern "C" void NebulaMtxRotRad(Mtx pMtx, char axis, f32 radians) {
    f32 sin = JMASinRadian(radians);
    f32 cos = JMACosRadian(radians);
    NebulaMtxRotTrig(pMtx, axis, sin, cos);
}

void J3DShapeMtx::loadMtxIndx_PNGP(int slot, u16 index) const {
    j3dSys.loadPosMtxIndx(slot, index);
    j3dSys.loadNrmMtxIndx(slot, index);
    ShapePacketUserData* pData = MR::getJ3DShapePacketUserData(j3dSys.getShapePacket());
    if (pData != nullptr) {
        pData->loadTexMtx(j3dSys.getShapePacket()->mpMaterial, slot, index);
    }
}

namespace {
    static void noLoadPrj(JPAEmitterWorkData const* pWork, const Mtx pSrt) {
    }

    void loadPrj(JPAEmitterWorkData const* pWork, const Mtx pSrt) {
        Mtx mtx;
        NebulaMtxConcat(pWork->mPrjMtx, pSrt, mtx);
        JPAParticleBatch::setProjection(mtx);
    }

    static void loadPrjAnm(JPAEmitterWorkData const* pWork, const Mtx pSrt) {
        f32 frame = pWork->mpEmtr->getAge();
        const JPABaseShape* pShape = pWork->mpRes->getBsp();
        f32 pivotS = 0.5f * (1.0f + pShape->getTilingS());
        f32 pivotT = 0.5f * (1.0f + pShape->getTilingT());
        f32 translationS = (frame * pShape->getIncTransX()) + pShape->getInitTransX();
        f32 translationT = (frame * pShape->getIncTransY()) + pShape->getInitTransY();
        f32 scaleS = (frame * pShape->getIncScaleX()) + pShape->getInitScaleX();
        f32 scaleT = (frame * pShape->getIncScaleY()) + pShape->getInitScaleY();
        s32 angle = (frame * pShape->getIncRot()) + pShape->getInitRot();
        f32 sin = JMASSin(angle);
        f32 cos = JMASCos(angle);
        Mtx matrix;
        matrix[0][0] = scaleS * cos;
        matrix[0][1] = -scaleS * sin;
        matrix[0][2] = (pivotS + (scaleS * ((sin * (pivotT + translationT)) - (cos * (pivotS + translationS)))));
        matrix[0][3] = 0.0f;
        matrix[1][0] = scaleT * sin;
        matrix[1][1] = scaleT * cos;
        matrix[1][2] = (pivotT + (-scaleT * ((sin * (pivotS + translationS)) + (cos * (pivotT + translationT)))));
        matrix[1][3] = 0.0f;
        matrix[2][0] = 0.0f;
        matrix[2][1] = 0.0f;
        matrix[2][2] = 1.0f;
        matrix[2][3] = 0.0f;
        NebulaMtxConcat(matrix, pWork->mPrjMtx, matrix);
        NebulaMtxConcat(matrix, pSrt, matrix);
        JPAParticleBatch::setProjection(matrix);
    }


    typedef void (*projectionFunc)(JPAEmitterWorkData const*, const Mtx);

    static projectionFunc p_prj[3] = {
        noLoadPrj,
        loadPrj,
        loadPrjAnm,
    };

    JPANode< JPABaseParticle >* getNext(JPANode< JPABaseParticle >* pNode) {
        return pNode->getNext();
    }
    JPANode< JPABaseParticle >* getPrev(JPANode< JPABaseParticle >* pNode) {
        return pNode->getPrev();
    }
    void dirTypeVel(JPAEmitterWorkData const* pWork, JPABaseParticle const* pParticle, JGeometry::TVec3< f32 >* pDirection) {
        pParticle->getVelVec(*pDirection);
    }

    void dirTypePos(JPAEmitterWorkData const* pWork, JPABaseParticle const* pParticle, JGeometry::TVec3< f32 >* pDirection) {
        pParticle->getLocalPosition(*pDirection);
    }

    void dirTypePosInv(JPAEmitterWorkData const* pWork, JPABaseParticle const* pParticle, JGeometry::TVec3< f32 >* pDirection) {
        pParticle->getLocalPosition(*pDirection);
        pDirection->negate();
    }

    void dirTypeEmtrDir(JPAEmitterWorkData const* pWork, JPABaseParticle const* pParticle, JGeometry::TVec3< f32 >* pDirection) {
        pDirection->set(pWork->mGlobalEmtrDir);
    }

    void dirTypePrevPtcl(JPAEmitterWorkData const* pWork, JPABaseParticle const* pParticle, JGeometry::TVec3< f32 >* pDirection) {
        JGeometry::TVec3< f32 > position;
        pParticle->getGlobalPosition(position);
        JPANode< JPABaseParticle >* pEnd = pWork->mpAlivePtcl->getEnd();
        JPANode< JPABaseParticle >* pPrev = pWork->mpCurNode->getPrev();

        if (pEnd != pPrev) {
            JPABaseParticle* pParticle = pWork->mpCurNode->getPrev()->getObject();
            pParticle->getGlobalPosition(*pDirection);
        } else {
            pWork->mpEmtr->calcEmitterGlobalPosition(pDirection);
        }

        pDirection->sub(position);
    }

    static void rotTypeY(f32 sin, f32 cos, Mtx& rMtx) {
        rMtx[0][0] = cos;
        rMtx[0][1] = 0.0f;
        rMtx[0][2] = -sin;
        rMtx[0][3] = 0.0f;
        rMtx[1][0] = 0.0f;
        rMtx[1][1] = 1.0f;
        rMtx[1][2] = 0.0f;
        rMtx[1][3] = 0.0f;
        rMtx[2][0] = sin;
        rMtx[2][1] = 0.0f;
        rMtx[2][2] = cos;
        rMtx[2][3] = 0.0f;
    }

    static void rotTypeX(f32 sin, f32 cos, Mtx& rMtx) {
        rMtx[0][0] = 1.0f;
        rMtx[0][1] = 0.0f;
        rMtx[0][2] = 0.0f;
        rMtx[0][3] = 0.0f;
        rMtx[1][0] = 0.0f;
        rMtx[1][1] = cos;
        rMtx[1][2] = -sin;
        rMtx[1][3] = 0.0f;
        rMtx[2][0] = 0.0f;
        rMtx[2][1] = sin;
        rMtx[2][2] = cos;
        rMtx[2][3] = 0.0f;
    }

    static void rotTypeZ(f32 sin, f32 cos, Mtx& rMtx) {
        rMtx[0][0] = cos;
        rMtx[0][1] = -sin;
        rMtx[0][2] = 0.0f;
        rMtx[0][3] = 0.0f;
        rMtx[1][0] = sin;
        rMtx[1][1] = cos;
        rMtx[1][2] = 0.0f;
        rMtx[1][3] = 0.0f;
        rMtx[2][0] = 0.0f;
        rMtx[2][1] = 0.0f;
        rMtx[2][2] = 1.0f;
        rMtx[2][3] = 0.0f;
    }

    static void rotTypeXYZ(f32 sin, f32 cos, Mtx& rMtx) {
        f32 third = 0.333333f * (1.0f - cos);
        f32 scaledSin = 0.57735f * sin;
        f32 plus = third + scaledSin;
        rMtx[0][0] = (third + cos);
        rMtx[0][1] = (third - scaledSin);
        rMtx[0][2] = plus;
        rMtx[0][3] = 0.0f;
        rMtx[1][0] = plus;
        rMtx[1][1] = (third + cos);
        rMtx[1][2] = (third - scaledSin);
        rMtx[1][3] = 0.0f;
        rMtx[2][0] = (third - scaledSin);
        rMtx[2][1] = plus;
        rMtx[2][2] = (third + cos);
        rMtx[2][3] = 0.0f;
    }

    static void basePlaneTypeXY(MtxPtr pMtx, f32 scaleX, f32 scaleY) {
        pMtx[0][0] *= scaleX;
        pMtx[1][0] *= scaleX;
        pMtx[2][0] *= scaleX;
        pMtx[0][1] *= scaleY;
        pMtx[1][1] *= scaleY;
        pMtx[2][1] *= scaleY;
    }

    static void basePlaneTypeXZ(MtxPtr pMtx, f32 scaleX, f32 scaleY) {
        pMtx[0][0] *= scaleX;
        pMtx[1][0] *= scaleX;
        pMtx[2][0] *= scaleX;
        pMtx[0][2] *= scaleY;
        pMtx[1][2] *= scaleY;
        pMtx[2][2] *= scaleY;
    }

    static void basePlaneTypeX(MtxPtr pMtx, f32 scaleX, f32 scaleY) {
        pMtx[0][0] *= scaleX;
        pMtx[1][0] *= scaleX;
        pMtx[2][0] *= scaleX;
        pMtx[0][1] *= scaleY;
        pMtx[1][1] *= scaleY;
        pMtx[2][1] *= scaleY;
        pMtx[0][2] *= scaleX;
        pMtx[1][2] *= scaleX;
        pMtx[2][2] *= scaleX;
    }

    typedef void (*dirTypeFunc)(JPAEmitterWorkData const*, JPABaseParticle const*, JGeometry::TVec3< f32 >*);
    static dirTypeFunc p_direction[5] = {
        dirTypeVel, dirTypePos, dirTypePosInv, dirTypeEmtrDir, dirTypePrevPtcl,
    };

    typedef void (*rotTypeFunc)(f32, f32, Mtx&);
    static rotTypeFunc p_rot[5] = {
        rotTypeY, rotTypeX, rotTypeZ, rotTypeXYZ, rotTypeY,
    };

    typedef void (*planeFunc)(MtxPtr, f32, f32);

    static planeFunc p_plane[3] = {
        basePlaneTypeXY,
        basePlaneTypeXZ,
        basePlaneTypeX,
    };

};  // namespace

void JPABaseEmitter::init(JPAEmitterManager* pManager, JPAResource* pResource) {
    MotionSignal::emitterInit(this);
    mpEmtrMgr = pManager;
    mpRes = pResource;
    const JPADynamicsBlockData* pData = mpRes->getDyn()->mpData;
    mLocalScl.set< f32 >(pData->mEmitterScl.x, pData->mEmitterScl.y, pData->mEmitterScl.z);
    pData = mpRes->getDyn()->mpData;
    mLocalTrs.set< f32 >(pData->mEmitterTrs.x, pData->mEmitterTrs.y, pData->mEmitterTrs.z);
    pData = mpRes->getDyn()->mpData;
    mLocalDir.set< f32 >(pData->mEmitterDir.x, pData->mEmitterDir.y, pData->mEmitterDir.z);
    if (!MR::isNearZero(mLocalDir)) {
        MR::normalize(&mLocalDir);
    }

    const JGeometry::TVec3< s16 >& rRotation = mpRes->getDyn()->mpData->mEmitterRot;
    const s16 x = rRotation.x;
    const s16 z = rRotation.z;
    const s16 y = rRotation.y;
    mLocalRot.set(x, y, z);
    mMaxFrame = mpRes->getDyn()->getMaxFrame();
    mLifeTime = mpRes->getDyn()->getLifetime();
    mVolumeSize = mpRes->getDyn()->getVolumeSize();
    mRate = mpRes->getDyn()->getRate();
    mRateStep = mpRes->getDyn()->getRateStep();
    mVolumeSweep = mpRes->getDyn()->getVolumeSweep();
    mVolumeMinRad = mpRes->getDyn()->getVolumeMinRad();
    mAwayFromCenterSpeed = mpRes->getDyn()->getInitVelOmni();
    mAwayFromAxisSpeed = mpRes->getDyn()->getInitVelAxis();
    mDirSpeed = mpRes->getDyn()->getInitVelDir();
    mSpread = mpRes->getDyn()->getInitVelDirSp();
    mRndmDirSpeed = mpRes->getDyn()->getInitVelRndm();
    mAirResist = mpRes->getDyn()->getAirRes();
    mRndm.set_seed(mpEmtrMgr->pWd->mRndm.get_rndm_u());
    NebulaMtxIdentity(mGlobalRot);
    mGlobalScl.set(1.0f, 1.0f, 1.0f);
    mGlobalTrs.zero();
    mGlobalPScl.set(1.0f, 1.0f);
    mGlobalPrmClr.r = mGlobalPrmClr.g = mGlobalPrmClr.b = mGlobalPrmClr.a = mGlobalEnvClr.r = mGlobalEnvClr.g = mGlobalEnvClr.b = mGlobalEnvClr.a =
        255;
    pResource->getBsp()->getPrmClr(&mPrmClr);
    pResource->getBsp()->getEnvClr(&mEnvClr);
    mpUserWork = 0;
    mScaleOut = 1.0f;
    mEmitCount = 0.0f;
    mStatus = 0x30;
    mDrawTimes = 1;
    mTick = 0;
    mWaitTime = 0;
    mRateStepTimer = 0;
    mTexAnmIdx = 0;
}

void JPADrawDirection(JPAEmitterWorkData* pWork, JPABaseParticle* pParticle) {
    if (pParticle->checkStatus(8)) {
        return;
    }

    TVec3f direction;
    TVec3f side;
    p_direction[pWork->mDirType](pWork, pParticle, &direction);
    if (MR::isNearZero(direction)) {
        return;
    }

    MR::normalize(&direction);
    side.cross(pParticle->mBaseAxis, direction);
    if (MR::isNearZero(side)) {
        return;
    }

    MR::normalize(&side);
    pParticle->mBaseAxis.cross(direction, side);
    MR::normalize(&pParticle->mBaseAxis);
    f32 scaleX = pWork->mGlobalPtclScl.x * pParticle->mParticleScaleX;
    f32 scaleY = pWork->mGlobalPtclScl.y * pParticle->mParticleScaleY;
    Mtx matrix;
    matrix[0][0] = pParticle->mBaseAxis.x;
    matrix[0][1] = direction.x;
    matrix[0][2] = side.x;
    matrix[0][3] = pParticle->mPosition.x;
    matrix[1][0] = pParticle->mBaseAxis.y;
    matrix[1][1] = direction.y;
    matrix[1][2] = side.y;
    matrix[1][3] = pParticle->mPosition.y;
    matrix[2][0] = pParticle->mBaseAxis.z;
    matrix[2][1] = direction.z;
    matrix[2][2] = side.z;
    matrix[2][3] = pParticle->mPosition.z;
    p_plane[pWork->mPlaneType](matrix, scaleX, scaleY);
    NebulaMtxConcat(pWork->mPosCamMtx, matrix, matrix);
    JPAParticleBatch::setMatrix(matrix);
    p_prj[pWork->mPrjType](pWork, matrix);
    JPADrawQuad(pWork->mListType);
}

void JPADrawRotDirection(JPAEmitterWorkData* pWork, JPABaseParticle* pParticle) {
    if (pParticle->checkStatus(8)) {
        return;
    }

    f32 sin = JMASSin(pParticle->mRotateAngle);
    f32 cos = JMASCos(pParticle->mRotateAngle);
    TVec3f direction;
    TVec3f side;
    p_direction[pWork->mDirType](pWork, pParticle, &direction);
    if (MR::isNearZero(direction)) {
        return;
    }

    MR::normalize(&direction);
    side.cross(pParticle->mBaseAxis, direction);
    if (MR::isNearZero(side)) {
        return;
    }

    MR::normalize(&side);
    pParticle->mBaseAxis.cross(direction, side);
    MR::normalize(&pParticle->mBaseAxis);
    f32 scaleX = pWork->mGlobalPtclScl.x * pParticle->mParticleScaleX;
    f32 scaleY = pWork->mGlobalPtclScl.y * pParticle->mParticleScaleY;
    Mtx rotation;
    Mtx matrix;
    p_rot[pWork->mRotType](sin, cos, rotation);
    p_plane[pWork->mPlaneType](rotation, scaleX, scaleY);
    matrix[0][0] = pParticle->mBaseAxis.x;
    matrix[0][1] = direction.x;
    matrix[0][2] = side.x;
    matrix[0][3] = pParticle->mPosition.x;
    matrix[1][0] = pParticle->mBaseAxis.y;
    matrix[1][1] = direction.y;
    matrix[1][2] = side.y;
    matrix[1][3] = pParticle->mPosition.y;
    matrix[2][0] = pParticle->mBaseAxis.z;
    matrix[2][1] = direction.z;
    matrix[2][2] = side.z;
    matrix[2][3] = pParticle->mPosition.z;
    NebulaMtxConcat(matrix, rotation, rotation);
    NebulaMtxConcat(pWork->mPosCamMtx, rotation, matrix);
    JPAParticleBatch::setMatrix(matrix);
    p_prj[pWork->mPrjType](pWork, matrix);
    JPADrawQuad(pWork->mListType);
}

void JPADrawDBillboard(JPAEmitterWorkData* pWork, JPABaseParticle* pParticle) {
    if (pParticle->checkStatus(8)) {
        return;
    }

    TVec3f direction;
    p_direction[pWork->mDirType](pWork, pParticle, &direction);
    TVec3f camera(pWork->mPosCamMtx[2][0], pWork->mPosCamMtx[2][1], pWork->mPosCamMtx[2][2]);
    NebulaVecCrossProduct(&direction, &camera, &direction);
    if (MR::isNearZero(direction)) {
        return;
    }

    MR::normalize(&direction);
    NebulaMtxMultVecSR(pWork->mPosCamMtx, &direction, &direction);
    TVec3f position;
    NebulaMtxMultVec(pWork->mPosCamMtx, &pParticle->mPosition, &position);
    f32 scaleX = pWork->mGlobalPtclScl.x * pParticle->mParticleScaleX;
    f32 scaleY = pWork->mGlobalPtclScl.y * pParticle->mParticleScaleY;
    Mtx matrix;
    matrix[0][0] = direction.x * scaleX;
    matrix[0][1] = -direction.y * scaleY;
    matrix[0][3] = position.x;
    matrix[1][0] = direction.y * scaleX;
    matrix[1][1] = direction.x * scaleY;
    matrix[1][3] = position.y;
    matrix[2][2] = 1.0f;
    matrix[2][3] = position.z;
    matrix[0][2] = matrix[1][2] = matrix[2][0] = matrix[2][1] = 0.0f;
    JPAParticleBatch::setMatrix(matrix);
    p_prj[pWork->mPrjType](pWork, matrix);
    JPADrawQuad(0);
}

void JPADrawLine(JPAEmitterWorkData* pWork, JPABaseParticle* pParticle) {
    if (pParticle->checkStatus(8)) {
        return;
    }

    TVec3f position(pParticle->mPosition);
    TVec3f end;
    end.set(pParticle->mVelocity);
    if (MR::isNearZero(end)) {
        return;
    }

    end.setLength(pWork->mGlobalPtclScl.y * (25.0f * pParticle->mParticleScaleY));
    end.sub(position, end);
    JPAParticleBatch::primitive(NEBULA_PRIMITIVE_LINES);
    JPAParticleBatch::corner(0, position.x, position.y, position.z, 0.0f, 0.0f);
    JPAParticleBatch::corner(1, end.x, end.y, end.z, 0.0f, 1.0f);
}

void JPADrawStripe(JPAEmitterWorkData* pWork) {
    const u32 count = pWork->mpAlivePtcl->getNum();
    JPABaseShape* pShape = pWork->mpRes->getBsp();
    if (count < 2) {
        return;
    }

    f32 texCoord = 0.0f;
    f32 texStep = 1.0f / (count - 1.0f);
    f32 leftWidth = (1.0f + pWork->mPivot.x) * (25.0f * pWork->mGlobalPtclScl.x);
    f32 rightWidth = (1.0f - pWork->mPivot.x) * (25.0f * pWork->mGlobalPtclScl.x);
    JPANode< JPABaseParticle >* pFirst;
    JPANode< JPABaseParticle >* (*pNext)(JPANode< JPABaseParticle >*);
    if (pShape->isDrawFwdAhead()) {
        texCoord = 1.0f;
        texStep = -texStep;
        pFirst = pWork->mpAlivePtcl->getLast();
        pNext = ::getPrev;
    } else {
        pFirst = pWork->mpAlivePtcl->getFirst();
        pNext = ::getNext;
    }

    JPAParticleBatch::setMatrix(pWork->mPosCamMtx);
    p_prj[pWork->mPrjType](pWork, pWork->mPosCamMtx);
    TVec3f position;
    TVec3f direction;
    TVec3f side;
    TVec3f vertices[2];
    Mtx matrix;
    JPAParticleBatch::primitive(NEBULA_PRIMITIVE_TRIANGLE_STRIP);
    for (JPANode< JPABaseParticle >* pNode = pFirst; pNode != pWork->mpAlivePtcl->getEnd();) {
        JPABaseParticle* pParticle = pNode->getObject();
        pWork->mpCurNode = pNode;
        JPAParticleBatch::particle(pParticle);
        position.set(pParticle->mPosition);
        f32 sin = JMASSin(pParticle->mRotateAngle);
        f32 cos = JMASCos(pParticle->mRotateAngle);
        vertices[0].set(-pParticle->mParticleScaleX * leftWidth, 0.0f, 0.0f);
        vertices[0].set< f32 >(vertices[0].x * cos, 0.0f, vertices[0].x * sin);
        vertices[1].set< f32 >(pParticle->mParticleScaleX * rightWidth, 0.0f, 0.0f);
        vertices[1].set< f32 >(vertices[1].x * cos, 0.0f, vertices[1].x * sin);
        p_direction[pWork->mDirType](pWork, pParticle, &direction);
        if (MR::isNearZero(direction)) {
            direction.set< f32 >(0.0f, 1.0f, 0.0f);
        } else {
            MR::normalize(&direction);
        }

        NebulaVecCrossProduct(&pParticle->mBaseAxis, &direction, &side);
        if (MR::isNearZero(side)) {
            side.set< f32 >(1.0f, 0.0f, 0.0f);
        } else {
            MR::normalize(&side);
        }

        NebulaVecCrossProduct(&direction, &side, &pParticle->mBaseAxis);
        MR::normalize(&pParticle->mBaseAxis);
        matrix[0][0] = side.x;
        matrix[0][1] = direction.x;
        matrix[0][2] = pParticle->mBaseAxis.x;
        matrix[0][3] = 0.0f;
        matrix[1][0] = side.y;
        matrix[1][1] = direction.y;
        matrix[1][2] = pParticle->mBaseAxis.y;
        matrix[1][3] = 0.0f;
        matrix[2][0] = side.z;
        matrix[2][1] = direction.z;
        matrix[2][2] = pParticle->mBaseAxis.z;
        matrix[2][3] = 0.0f;
        NebulaMtxMultVecArraySR(matrix, vertices, vertices, 2);
        JPAParticleBatch::corner(0, vertices[0].x + position.x, vertices[0].y + position.y, vertices[0].z + position.z, 0.0f, texCoord);
        JPAParticleBatch::corner(1, vertices[1].x + position.x, vertices[1].y + position.y, vertices[1].z + position.z, 1.0f, texCoord);
        pNode = pNext(pNode);
        texCoord += texStep;
    }

}

void JPADrawStripeX(JPAEmitterWorkData* pWork) {
    const u32 count = pWork->mpAlivePtcl->getNum();
    JPABaseShape* pShape = pWork->mpRes->getBsp();
    if (count < 2) {
        return;
    }

    f32 startTexCoord = 0.0f;
    f32 texCoord = 0.0f;
    f32 texStep = 1.0f / (count - 1.0f);
    f32 leftWidth = (1.0f + pWork->mPivot.x) * (25.0f * pWork->mGlobalPtclScl.x);
    f32 rightWidth = (1.0f - pWork->mPivot.x) * (25.0f * pWork->mGlobalPtclScl.x);
    f32 topWidth = (1.0f + pWork->mPivot.y) * (25.0f * pWork->mGlobalPtclScl.y);
    f32 bottomWidth = (1.0f - pWork->mPivot.y) * (25.0f * pWork->mGlobalPtclScl.y);
    JPANode< JPABaseParticle >* pFirst;
    JPANode< JPABaseParticle >* (*pNext)(JPANode< JPABaseParticle >*);
    if (pShape->isDrawFwdAhead()) {
        texCoord = 1.0f;
        startTexCoord = 1.0f;
        texStep = -texStep;
        pFirst = pWork->mpAlivePtcl->getLast();
        pNext = ::getPrev;
    } else {
        pFirst = pWork->mpAlivePtcl->getFirst();
        pNext = ::getNext;
    }

    JPAParticleBatch::setMatrix(pWork->mPosCamMtx);
    p_prj[pWork->mPrjType](pWork, pWork->mPosCamMtx);
    TVec3f position;
    TVec3f direction;
    TVec3f side;
    TVec3f vertices[2];
    Mtx matrix;
    JPAParticleBatch::primitive(NEBULA_PRIMITIVE_TRIANGLE_STRIP);
    for (JPANode< JPABaseParticle >* pNode = pFirst; pNode != pWork->mpAlivePtcl->getEnd();) {
        JPABaseParticle* pParticle = pNode->getObject();
        pWork->mpCurNode = pNode;
        JPAParticleBatch::particle(pParticle);
        position.set(pParticle->mPosition);
        f32 sin = JMASSin(pParticle->mRotateAngle);
        f32 cos = JMASCos(pParticle->mRotateAngle);
        vertices[0].set(-pParticle->mParticleScaleX * leftWidth, 0.0f, 0.0f);
        vertices[0].set< f32 >(vertices[0].x * cos, 0.0f, vertices[0].x * sin);
        vertices[1].set< f32 >(pParticle->mParticleScaleX * rightWidth, 0.0f, 0.0f);
        vertices[1].set< f32 >(vertices[1].x * cos, 0.0f, vertices[1].x * sin);
        p_direction[pWork->mDirType](pWork, pParticle, &direction);
        if (MR::isNearZero(direction)) {
            direction.set< f32 >(0.0f, 1.0f, 0.0f);
        } else {
            MR::normalize(&direction);
        }

        NebulaVecCrossProduct(&pParticle->mBaseAxis, &direction, &side);
        if (MR::isNearZero(side)) {
            side.set< f32 >(1.0f, 0.0f, 0.0f);
        } else {
            MR::normalize(&side);
        }

        if (MR::isSameDirection(direction, side, 0.01f)) {
            side.set< f32 >(0.0f, 1.0f, 0.0f);
        }

        NebulaVecCrossProduct(&direction, &side, &pParticle->mBaseAxis);
        MR::normalize(&pParticle->mBaseAxis);
        matrix[0][0] = side.x;
        matrix[0][1] = direction.x;
        matrix[0][2] = pParticle->mBaseAxis.x;
        matrix[0][3] = 0.0f;
        matrix[1][0] = side.y;
        matrix[1][1] = direction.y;
        matrix[1][2] = pParticle->mBaseAxis.y;
        matrix[1][3] = 0.0f;
        matrix[2][0] = side.z;
        matrix[2][1] = direction.z;
        matrix[2][2] = pParticle->mBaseAxis.z;
        matrix[2][3] = 0.0f;
        NebulaMtxMultVecArraySR(matrix, vertices, vertices, 2);
        JPAParticleBatch::corner(0, vertices[0].x + position.x, vertices[0].y + position.y, vertices[0].z + position.z, 0.0f, texCoord);
        JPAParticleBatch::corner(1, vertices[1].x + position.x, vertices[1].y + position.y, vertices[1].z + position.z, 1.0f, texCoord);
        pNode = pNext(pNode);
        texCoord += texStep;
    }

    texCoord = startTexCoord;
    JPAParticleBatch::primitive(NEBULA_PRIMITIVE_TRIANGLE_STRIP);
    for (JPANode< JPABaseParticle >* pNode = pFirst; pNode != pWork->mpAlivePtcl->getEnd();) {
        JPABaseParticle* pParticle = pNode->getObject();
        pWork->mpCurNode = pNode;
        JPAParticleBatch::particle(pParticle);
        position.set(pParticle->mPosition);
        f32 sin = -JMASSin(pParticle->mRotateAngle);
        f32 cos = JMASCos(pParticle->mRotateAngle);
        vertices[0].set(-pParticle->mParticleScaleY * topWidth, 0.0f, 0.0f);
        vertices[0].set< f32 >(vertices[0].x * sin, 0.0f, vertices[0].x * cos);
        vertices[1].set< f32 >(pParticle->mParticleScaleY * bottomWidth, 0.0f, 0.0f);
        vertices[1].set< f32 >(vertices[1].x * sin, 0.0f, vertices[1].x * cos);
        p_direction[pWork->mDirType](pWork, pParticle, &direction);
        if (MR::isNearZero(direction)) {
            direction.set< f32 >(0.0f, 1.0f, 0.0f);
        } else {
            MR::normalize(&direction);
        }

        NebulaVecCrossProduct(&pParticle->mBaseAxis, &direction, &side);
        if (MR::isNearZero(side)) {
            side.set< f32 >(1.0f, 0.0f, 0.0f);
        } else {
            MR::normalize(&side);
        }

        if (MR::isSameDirection(direction, side, 0.01f)) {
            side.set< f32 >(0.0f, 1.0f, 0.0f);
        }

        NebulaVecCrossProduct(&direction, &side, &pParticle->mBaseAxis);
        MR::normalize(&pParticle->mBaseAxis);
        matrix[0][0] = side.x;
        matrix[0][1] = direction.x;
        matrix[0][2] = pParticle->mBaseAxis.x;
        matrix[0][3] = 0.0f;
        matrix[1][0] = side.y;
        matrix[1][1] = direction.y;
        matrix[1][2] = pParticle->mBaseAxis.y;
        matrix[1][3] = 0.0f;
        matrix[2][0] = side.z;
        matrix[2][1] = direction.z;
        matrix[2][2] = pParticle->mBaseAxis.z;
        matrix[2][3] = 0.0f;
        NebulaMtxMultVecArraySR(matrix, vertices, vertices, 2);
        JPAParticleBatch::corner(2, vertices[0].x + position.x, vertices[0].y + position.y, vertices[0].z + position.z, 0.0f, texCoord);
        JPAParticleBatch::corner(3, vertices[1].x + position.x, vertices[1].y + position.y, vertices[1].z + position.z, 1.0f, texCoord);
        pNode = pNext(pNode);
        texCoord += texStep;
    }

}

void JPAFieldAir::prepare(JPAEmitterWorkData* pWork, JPAFieldBlock* pBlock) {
    TVec3f direction(pBlock->getDir());
    MR::normalizeOrZero(&direction);
    if (pBlock->checkStatus(2)) {
        mAccel.scale(pBlock->getMag(), direction);
    } else {
        NebulaMtxMultVecSR(pWork->mRotationMtx, &direction, &mAccel);
        mAccel.scale(pBlock->getMag());
    }
}

void JPAFieldVortex::prepare(JPAEmitterWorkData* pWork, JPAFieldBlock* pBlock) {
    NebulaMtxMultVecSR(pWork->mGlobalRot, &pBlock->getDir(), &field_0x10);
    MR::normalizeOrZero(&field_0x10);
    field_0x1c = pBlock->getPos().z * pBlock->getPos().z;
    field_0x20 = 1.0f / field_0x1c;
}

void JPAFieldVortex::calc(JPAEmitterWorkData*, JPAFieldBlock* pBlock, JPABaseParticle* pParticle) {
    TVec3f radial;
    radial.scale(field_0x10.dot(pParticle->mLocalPosition), field_0x10);
    radial.sub(pParticle->mLocalPosition, radial);
    f32 distanceSquared = radial.squared();
    f32 magnitude;
    if (distanceSquared > field_0x1c) {
        magnitude = pBlock->getMagRndm();
    } else {
        f32 rate = distanceSquared * field_0x20;
        magnitude = (1.0f - rate) * pBlock->getMag() + rate * pBlock->getMagRndm();
    }

    MR::normalizeOrZero(&radial);
    mAccel.cross(radial, field_0x10);
    mAccel.scale(magnitude);
    calcAffect(pBlock, pParticle);
}

void JPAFieldConvection::prepare(JPAEmitterWorkData* pWork, JPAFieldBlock* pBlock) {
    TVec3f radial;
    TVec3f tangent;
    tangent.cross(pBlock->getPos(), pBlock->getDir());
    radial.cross(pBlock->getDir(), tangent);
    NebulaMtxMultVecSR(pWork->mGlobalRot, &radial, &field_0x10);
    NebulaMtxMultVecSR(pWork->mGlobalRot, &pBlock->getDir(), &field_0x1c);
    NebulaMtxMultVecSR(pWork->mGlobalRot, &tangent, &field_0x28);
    MR::normalizeOrZero(&field_0x10);
    MR::normalizeOrZero(&field_0x1c);
    MR::normalizeOrZero(&field_0x28);
}

void JPAFieldSpin::prepare(JPAEmitterWorkData* pWork, JPAFieldBlock* pBlock) {
    TVec3f axis;
    Mtx rotation;
    NebulaMtxMultVecSR(pWork->mGlobalRot, &pBlock->getDir(), &axis);
    MR::normalizeOrZero(&axis);
    NebulaMtxRotAxisRad(rotation, &axis, pBlock->getMag());
    field_0x10.set< f32 >(rotation[0][0], rotation[1][0], rotation[2][0]);
    field_0x1c.set< f32 >(rotation[0][1], rotation[1][1], rotation[2][1]);
    field_0x28.set< f32 >(rotation[0][2], rotation[1][2], rotation[2][2]);
}

void JPAEmitterManager::calcYBBCam() {
    TVec3f axis(0.0f, pWd->mPosCamMtx[1][1], pWd->mPosCamMtx[2][1]);
    if (!MR::isNearZero(axis)) {
        MR::normalize(&axis);
        pWd->mYBBCamMtx[0][0] = 1.0f;
        pWd->mYBBCamMtx[0][1] = 0.0f;
        pWd->mYBBCamMtx[0][2] = 0.0f;
        pWd->mYBBCamMtx[0][3] = pWd->mPosCamMtx[0][3];
        pWd->mYBBCamMtx[1][0] = 0.0f;
        pWd->mYBBCamMtx[1][1] = axis.y;
        pWd->mYBBCamMtx[1][2] = -axis.z;
        pWd->mYBBCamMtx[1][3] = pWd->mPosCamMtx[1][3];
        pWd->mYBBCamMtx[2][0] = 0.0f;
        pWd->mYBBCamMtx[2][1] = axis.z;
        pWd->mYBBCamMtx[2][2] = axis.y;
        pWd->mYBBCamMtx[2][3] = pWd->mPosCamMtx[2][3];
    }
}

void JPADrawYBillboard(JPAEmitterWorkData* pWork, JPABaseParticle* pParticle) {
    if (pParticle->checkStatus(8)) {
        return;
    }

    TVec3f axis(0.0f, pWork->mPosCamMtx[1][1], pWork->mPosCamMtx[2][1]);
    if (MR::isNearZero(axis)) {
        return;
    }

    TVec3f position;
    NebulaMtxMultVec(pWork->mPosCamMtx, &pParticle->mPosition, &position);
    f32 scaleX = pWork->mGlobalPtclScl.x * pParticle->mParticleScaleX;
    f32 scaleY = pWork->mGlobalPtclScl.y * pParticle->mParticleScaleY;
    Mtx matrix;
    matrix[0][0] = scaleX;
    matrix[0][3] = position.x;
    matrix[1][1] = pWork->mYBBCamMtx[1][1] * scaleY;
    matrix[1][2] = pWork->mYBBCamMtx[1][2];
    matrix[1][3] = position.y;
    matrix[2][1] = pWork->mYBBCamMtx[2][1] * scaleY;
    matrix[2][2] = pWork->mYBBCamMtx[2][2];
    matrix[2][3] = position.z;
    matrix[0][1] = matrix[0][2] = matrix[1][0] = matrix[2][0] = 0.0f;
    JPAParticleBatch::setMatrix(matrix);
    p_prj[pWork->mPrjType](pWork, matrix);
    JPADrawQuad(0);
}

void JPADrawRotYBillboard(JPAEmitterWorkData* pWork, JPABaseParticle* pParticle) {
    if (pParticle->checkStatus(8)) {
        return;
    }

    TVec3f axis(0.0f, pWork->mPosCamMtx[1][1], pWork->mPosCamMtx[2][1]);
    if (MR::isNearZero(axis)) {
        return;
    }

    TVec3f position;
    NebulaMtxMultVec(pWork->mPosCamMtx, &pParticle->mPosition, &position);
    f32 scaleX = pWork->mGlobalPtclScl.x * pParticle->mParticleScaleX;
    f32 scaleY = pWork->mGlobalPtclScl.y * pParticle->mParticleScaleY;
    Mtx matrix;
    f32 sin = JMASSin(pParticle->mRotateAngle);
    f32 cos = JMASCos(pParticle->mRotateAngle);
    f32 sinX = sin * scaleX;
    f32 cosY = cos * scaleY;
    f32 y = pWork->mYBBCamMtx[1][1];
    f32 z = pWork->mYBBCamMtx[2][1];
    matrix[0][0] = (cos * scaleX);
    matrix[0][1] = (-sin * scaleY);
    matrix[0][2] = 0.0f;
    matrix[0][3] = position.x;
    matrix[1][0] = sinX * y;
    matrix[1][1] = cosY * y;
    matrix[1][2] = -z;
    matrix[1][3] = position.y;
    matrix[2][0] = sinX * z;
    matrix[2][1] = cosY * z;
    matrix[2][2] = y;
    matrix[2][3] = position.z;
    JPAParticleBatch::setMatrix(matrix);
    p_prj[pWork->mPrjType](pWork, matrix);
    JPADrawQuad(0);
}

void JAU_JASInitializer::initJASystem(NebulaArena* pHeap) {
    static bool sStarted = false;
    if (!sStarted) {
        sStarted = true;
        JASKernel::setupRootHeap(pHeap, heapSize_);
        JASTrack::newMemPool(field_0x1c);
        if (field_0x20 > 0) {
            JASTrack::TChannelMgr::newMemPool(field_0x20);
        }

        JASDriver::startAudio();
        JASDriver::setOutputLevel(outputLevel_);
    }

    if (waveArcDir_ != nullptr) {
        JASWaveArcLoader::setCurrentDir(waveArcDir_);
    }

    switch (NebulaSystemSettings::soundMode()) {
    case NebulaSystemSettings::SoundModeMono:
        JASDriver::setOutputMode(0);
        break;
    case NebulaSystemSettings::SoundModeStereo:
        JASDriver::setOutputMode(1);
        break;
    case NebulaSystemSettings::SoundModeSurround:
        JASDriver::setOutputMode(2);
        break;
    }
}

