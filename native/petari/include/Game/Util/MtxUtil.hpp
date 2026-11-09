#pragma once

#include <nebula_geometry.hpp>
#include <nebula_math.h>
#include <NebulaGpu.h>

class LiveActor;

namespace MR {
    void makeMtxRotate(MtxPtr, s16, s16, s16);
    void makeMtxRotate(MtxPtr, f32, f32, f32);
    void makeMtxRotate(MtxPtr, const Vec3f&);
    void makeMtxRotateY(MtxPtr, f32);
    void makeMtxTR(MtxPtr, f32, f32, f32, f32, f32, f32);
    void makeMtxTR(MtxPtr, const Vec3f&, const Vec3f&);
    void makeMtxTransRotateY(MtxPtr, f32, f32, f32, f32);
    void rotateMtxLocalX(MtxPtr, f32);
    void rotateMtxLocalY(MtxPtr, f32);
    void rotateMtxLocalZ(MtxPtr, f32);
    void rotateMtxLocalXDegree(MtxPtr, f32);
    void rotateMtxLocalYDegree(MtxPtr, f32);
    void rotateMtxLocalZDegree(MtxPtr, f32);
    void preScaleMtx(MtxPtr, f32);
    void preScaleMtx(MtxPtr, const Vec3f&);

    void preScaleMtx(MtxPtr, f32, f32, f32);

    void scaleMtxToLocalMtx(MtxPtr, MtxPtr, MtxPtr, const Vec3f&);
    void scaleMtxToLocalMtx(MtxPtr, MtxPtr, const Vec3f&);
    void scaleMtxToDir(Pos3f*, const Vec3f&, const Vec3f&);
    void makeMtxTRS(MtxPtr, f32, f32, f32, f32, f32, f32, f32, f32, f32);
    void makeMtxTRS(MtxPtr, const Vec3f&, const Vec3f&, const Vec3f&);
    void setMtxAxisXYZ(MtxPtr, const Vec3f&, const Vec3f&, const Vec3f&);
    void flattenMtx(MtxPtr, MtxPtr, const Vec3f&);
    void flattenMtx(MtxPtr, const Vec3f&);
    void blendMtxRotate(MtxPtr, MtxPtr, f32, MtxPtr);
    void blendMtxRotateSlerp(MtxPtr, MtxPtr, f32, MtxPtr);
    void blendMtx(MtxPtr, MtxPtr, f32, MtxPtr);
    void makeMtxWithoutScale(Pos3f*, const Pos3f&);
    void makeRTFromMtxPtr(Vec3f*, Vec3f*, MtxPtr, bool);
    void makeMtxSideUp(Pos3f*, const Vec3f&, const Vec3f&);
    void makeMtxSideUpPos(Pos3f*, const Vec3f&, const Vec3f&, const Vec3f&);
    void makeMtxSideFront(Pos3f*, const Vec3f&, const Vec3f&);
    void makeMtxUpSide(MtxPtr, const Vec3f&, const Vec3f&);
    void makeMtxUpSide(Pos3f*, const Vec3f&, const Vec3f&);
    void makeMtxUpSidePos(Pos3f*, const Vec3f&, const Vec3f&, const Vec3f&);
    void makeMtxUpFront(MtxPtr, const Vec3f&, const Vec3f&);
    void makeMtxUpFront(Pos3f*, const Vec3f&, const Vec3f&);
    void makeMtxUpFrontPos(Pos3f*, const Vec3f&, const Vec3f&, const Vec3f&);
    void makeMtxFrontSidePos(Pos3f*, const Vec3f&, const Vec3f&, const Vec3f&);
    void makeMtxFrontUp(MtxPtr, const Vec3f&, const Vec3f&);
    void makeMtxFrontUp(Pos3f*, const Vec3f&, const Vec3f&);
    void makeMtxFrontUpPos(Pos3f*, const Vec3f&, const Vec3f&, const Vec3f&);
    void makeMtxUpNoSupport(Pos3f*, const Vec3f&);
    void makeMtxUpNoSupportPos(Pos3f*, const Vec3f&, const Vec3f&);
    void makeMtxFrontNoSupport(Pos3f*, const Vec3f&);
    void makeMtxFrontNoSupportPos(Pos3f*, const Vec3f&, const Vec3f&);
    void orthogonalize(Pos3f*);
    void turnMtxToXDirRate(Pos3f*, const Vec3f&, f32);
    void turnMtxToXDirDegree(Pos3f*, const Vec3f&, f32);
    void turnMtxToYDirRate(Pos3f*, const Vec3f&, f32);
    void turnMtxToYDirDegree(Pos3f*, const Vec3f&, f32);
    void turnMtxToLocalDirDegree(Pos3f*, const Vec3f&, const Vec3f&, f32);
    void makeMtxMoment(Pos3f*, const Vec3f&);
    void rotateMtxMoment(Pos3f*, const Pos3f&, const Vec3f&);
    void rotateMtxMoment(Pos3f*, const Vec3f&);
    void setMtxTrans(MtxPtr, f32, f32, f32);
    void addTransMtx(MtxPtr, const Vec3f&);
    void addTransMtxLocal(MtxPtr, const Vec3f&);
    void addTransMtxLocalX(MtxPtr, f32);
    void addTransMtxLocalY(MtxPtr, f32);
    void addTransMtxLocalZ(MtxPtr, f32);
    void extractMtxXDir(MtxPtr, Vec3f*);
    void extractMtxYDir(MtxPtr, Vec3f*);
    void extractMtxZDir(MtxPtr, Vec3f*);
    void extractMtxXYZDir(MtxPtr, Vec3f*, Vec3f*, Vec3f*);
    void extractMtxTrans(MtxPtr, Vec3f*);
    bool isSameMtx(MtxPtr, MtxPtr);
    bool isSameMtxRot(MtxPtr, MtxPtr);
    bool isRotAxisY(MtxPtr, MtxPtr);
    void calcMtxRotAxis(Vec3f*, MtxPtr, MtxPtr);
    MtxPtr tmpMtxTrans(const Vec3f&);
    MtxPtr tmpMtxScale(f32, f32, f32);
    MtxPtr tmpMtxRotXRad(f32);
    MtxPtr tmpMtxRotYRad(f32);
    MtxPtr tmpMtxRotZRad(f32);
    MtxPtr tmpMtxRotXDeg(f32);
    MtxPtr tmpMtxRotYDeg(f32);
    MtxPtr tmpMtxRotZDeg(f32);
    void orderRotateMtx(s16, const Vec3f&, MtxPtr);
    // *pOut = rVec turned rad about rAxis.
    void rotAxisVecRad(const Vec3f& rVec, const Vec3f& rAxis, Vec3f* pOut, f32 rad);

    void makeMtxTR(MtxPtr, const LiveActor*);

    inline void setMtxTrans(MtxPtr pMtx, const Vec3f& rVec) {
        MR::setMtxTrans(pMtx, rVec.x, rVec.y, rVec.z);
    }

    // Non-official symbol.
    inline void multMtx(Mtx pOut, const Mtx pA, const Mtx pB) {
        NebulaMtxConcat(pB, pA, pOut);
    }
};  // namespace MR
