#pragma once

#include <revolution/mtx.h>

class JPADrawInfo {
public:
    JPADrawInfo(const Mtx pCameraMtx) {
        NebulaMtxCopy(pCameraMtx, mCamMtx);
        NebulaMtxIdentity(mPrjMtx);
    }

    JPADrawInfo(const Mtx pCameraMtx, f32 fovY, f32 aspect) {
        NebulaMtxCopy(pCameraMtx, mCamMtx);
        NebulaMtxLightPerspective(mPrjMtx, fovY, aspect, 0.5f, -0.5f, 0.5f, 0.5f);
    }

    JPADrawInfo(const Mtx pCameraMtx, f32 top, f32 bottom, f32 left, f32 right) {
        NebulaMtxCopy(pCameraMtx, mCamMtx);
        NebulaMtxLightOrtho(mPrjMtx, top, bottom, left, right, 0.5f, 0.5f, 0.5f, 0.5f);
    }

    /* 0x00 */ Mtx mCamMtx;
    /* 0x30 */ Mtx mPrjMtx;

    void getCamMtx(Mtx pDst) const {
        NebulaMtxCopy(mCamMtx, pDst);
    }

    void getPrjMtx(Mtx pDst) const {
        NebulaMtxCopy(mPrjMtx, pDst);
    }

    void setPrjMtx(const Mtx pSrc) {
        NebulaMtxCopy(pSrc, mPrjMtx);
    }
};
