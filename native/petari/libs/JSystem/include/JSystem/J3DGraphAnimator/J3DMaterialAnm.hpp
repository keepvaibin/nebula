#pragma once

#include "JSystem/J3DGraphAnimator/J3DAnimation.hpp"

class J3DMaterial;

class J3DMatColorAnm {
public:
    ~J3DMatColorAnm() {
    }

    J3DMatColorAnm() : field_0x0(0), mAnmFlag(1), mAnmColor(NULL) {
    }

    J3DMatColorAnm(u16 param_1, J3DAnmColor* pAnmColor) {
        field_0x0 = param_1;
        mAnmFlag = 1;
        mAnmColor = pAnmColor;
    }

    void operator=(J3DMatColorAnm const& other) {
        mAnmColor = other.mAnmColor;
        field_0x0 = other.field_0x0;
        mAnmFlag = other.mAnmFlag;
    }

    void setAnmFlag(bool flag) {
        mAnmFlag = flag;
    }
    bool getAnmFlag() const {
        return mAnmFlag;
    }

    void calc(NebulaColor8* pColor) const {
        mAnmColor->getColor(field_0x0, pColor);
    }

private:
    u16 field_0x0;
    u16 mAnmFlag;
    J3DAnmColor* mAnmColor;
};

class J3DTexMtxAnm {
public:
    ~J3DTexMtxAnm() {
    }
    J3DTexMtxAnm() : field_0x0(0), mAnmFlag(1), mAnmTransform(NULL) {
    }

    J3DTexMtxAnm(u16 param_1, J3DAnmTextureSRTKey* pSRTKey) {
        field_0x0 = param_1;
        mAnmFlag = 1;
        mAnmTransform = pSRTKey;
    }

    void operator=(J3DTexMtxAnm const& other) {
        mAnmTransform = other.mAnmTransform;
        field_0x0 = other.field_0x0;
        mAnmFlag = other.mAnmFlag;
    }

    void setAnmFlag(bool flag) {
        mAnmFlag = flag;
    }

    void calc(J3DTextureSRTInfo* pSRTInfo) const {
        mAnmTransform->getTransform(field_0x0, pSRTInfo);
    }

    bool getAnmFlag() const {
        return mAnmFlag;
    }

private:
    u16 field_0x0;
    u16 mAnmFlag;
    J3DAnmTextureSRTKey* mAnmTransform;
};

class J3DTexNoAnm {
public:
    ~J3DTexNoAnm() {
    }
    J3DTexNoAnm() : field_0x4(0), mAnmFlag(1), mAnmTexPattern(NULL) {
    }

    J3DTexNoAnm(u16 param_1, J3DAnmTexPattern* pAnmTexPattern) {
        field_0x4 = param_1;
        mAnmFlag = 1;
        mAnmTexPattern = pAnmTexPattern;
    }

    virtual void calc(u16* param_0) const {
        mAnmTexPattern->getTexNo(field_0x4, param_0);
    }

    void operator=(J3DTexNoAnm const& other) {
        mAnmTexPattern = other.mAnmTexPattern;
        field_0x4 = other.field_0x4;
        mAnmFlag = other.mAnmFlag;
    }

    void setAnmFlag(bool flag) {
        mAnmFlag = flag;
    }
    bool getAnmFlag() const {
        return mAnmFlag;
    }
    J3DAnmTexPattern* getAnmTexPattern() {
        return mAnmTexPattern;
    }

private:
    u16 field_0x4;
    u16 mAnmFlag;
    J3DAnmTexPattern* mAnmTexPattern;
};

class J3DTevColorAnm {
public:
    ~J3DTevColorAnm() {
    }
    J3DTevColorAnm() : field_0x0(0), mAnmFlag(1), mAnmTevReg(NULL) {
    }

    J3DTevColorAnm(u16 param_1, J3DAnmTevRegKey* pTevRegKey) {
        field_0x0 = param_1;
        mAnmFlag = 1;
        mAnmTevReg = pTevRegKey;
    }

    void operator=(J3DTevColorAnm const& other) {
        mAnmTevReg = other.mAnmTevReg;
        field_0x0 = other.field_0x0;
        mAnmFlag = other.mAnmFlag;
    }

    void setAnmFlag(bool flag) {
        mAnmFlag = flag;
    }
    bool getAnmFlag() const {
        return mAnmFlag;
    }

    void calc(NebulaColor16* pColor) const {
        mAnmTevReg->getTevColorReg(field_0x0, pColor);
    }

private:
    u16 field_0x0;
    u16 mAnmFlag;
    J3DAnmTevRegKey* mAnmTevReg;
};

class J3DTevKColorAnm {
public:
    ~J3DTevKColorAnm() {
    }
    J3DTevKColorAnm() : field_0x0(0), mAnmFlag(1), mAnmTevReg(NULL) {
    }

    J3DTevKColorAnm(u16 param_1, J3DAnmTevRegKey* pTevRegKey) {
        field_0x0 = param_1;
        mAnmFlag = 1;
        mAnmTevReg = pTevRegKey;
    }

    void operator=(J3DTevKColorAnm const& other) {
        mAnmTevReg = other.mAnmTevReg;
        field_0x0 = other.field_0x0;
        mAnmFlag = other.mAnmFlag;
    }

    void setAnmFlag(bool flag) {
        mAnmFlag = flag;
    }
    bool getAnmFlag() const {
        return mAnmFlag;
    }

    void calc(NebulaColor8* pColor) const {
        mAnmTevReg->getTevKonstReg(field_0x0, pColor);
    }

private:
    u16 field_0x0;
    u16 mAnmFlag;
    J3DAnmTevRegKey* mAnmTevReg;
};

class J3DMaterialAnm {
public:
    J3DMaterialAnm() : mDrawSlot(sNextDrawSlot++) {
        initialize();
    }

    // Its place in a draw buffer: by the order they were made, not by address.
    u32 mDrawSlot;
    static u32 sNextDrawSlot;

    void initialize();
    void setMatColorAnm(int, J3DMatColorAnm*);
    void setTexMtxAnm(int, J3DTexMtxAnm*);
    void setTexNoAnm(int, J3DTexNoAnm*);
    void setTevColorAnm(int, J3DTevColorAnm*);
    void setTevKColorAnm(int, J3DTevKColorAnm*);

    virtual ~J3DMaterialAnm() {
    }
    virtual void calc(J3DMaterial*) const;

    const J3DTexMtxAnm& getTexMtxAnm(int i) const {
        return mTexMtxAnm[i];
    }

private:
    J3DMatColorAnm mMatColorAnm[2];
    J3DTexMtxAnm mTexMtxAnm[8];
    J3DTexNoAnm mTexNoAnm[8];
    J3DTevColorAnm mTevColorAnm[4];
    J3DTevKColorAnm mTevKColorAnm[4];
};
