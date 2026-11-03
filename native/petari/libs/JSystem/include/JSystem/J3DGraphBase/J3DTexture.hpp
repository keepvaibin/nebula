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

    void load(u32) const;
    void calc(const Mtx);
    void calcTexMtx(const Mtx);
    void calcPostTexMtx(const Mtx);
    inline void loadTexMtx(u32) const;
    inline void loadPostTexMtx(u32) const;

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
