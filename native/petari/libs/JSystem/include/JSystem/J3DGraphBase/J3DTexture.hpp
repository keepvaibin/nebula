#pragma once

#include "JSystem/J3DGraphBase/J3DTevs.hpp"
#include "JSystem/J3DGraphBase/J3DNativeTexture.hpp"
#include "JSystem/JUtility/JUTTexture.hpp"
#include <stdint.h>

class J3DTexture {
private:
    u16 mNum;
    u16 unk_0x2;
    ResTIMG* mpRes;

public:
    J3DTexture(u16 num, ResTIMG* res) : mNum(num), unk_0x2(0), mpRes(res) {
    }

    void loadGX(u16, u8) const;
    void entryNum(u16);
    void addResTIMG(u16, ResTIMG const*);
    virtual ~J3DTexture() {
    }

    u16 getNum() const {
        return mNum;
    }

    ResTIMG* getResTIMG(u16 index) const {
        return &mpRes[index];
    }

    void setResTIMG(u16 index, const ResTIMG& timg) {
        mpRes[index] = timg;
        mpRes[index].mImage = timg.image();
        // The new image handed to the renderer, as the model's own were at load.
        J3DNativeTexture::registerRecord(timg.image());
    }
};

extern J3DTexMtxInfo const j3dDefaultTexMtxInfo;

class J3DTexMtx {
public:
    J3DTexMtx() {
        mTexMtxInfo = j3dDefaultTexMtxInfo;
    }

    J3DTexMtx(const J3DTexMtxInfo& info) {
        mTexMtxInfo = info;
    }

    // The matrix from eye-space positions or normals, or from the coordinate: its animation and effect matrix, and
    // `world` (eye to world) for a map fixed in the world.
    void calc(const Mtx world);
    bool followsView() const {
        const u32 mode = mTexMtxInfo.mInfo & 0x3F;
        return mode == 2 || mode == 5 || mode == 8 || mode == 10 || mode == 11;
    }
    // A map of the direction: the world's turn alone.
    bool isDirectionMap() const {
        const u32 mode = mTexMtxInfo.mInfo & 0x3F;
        return mode == 5 || mode == 10 || mode == 11;
    }

    J3DTexMtxInfo& getTexMtxInfo() {
        return mTexMtxInfo;
    }
    Mtx& getMtx() {
        return mMtx;
    }
    void setEffectMtx(Mtx effectMtx) {
        mTexMtxInfo.setEffectMtx(effectMtx);
    }

    J3DTexMtxInfo mTexMtxInfo;

private:
    Mtx mMtx;
};
