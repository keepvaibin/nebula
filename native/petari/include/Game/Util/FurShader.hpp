#pragma once

#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class J3DModel;
class J3DModelData;
struct ResTIMG;
class J3DShape;

class CShader : public J3DVtxShader {
public:
    class CLengthMap {
    public:
        CLengthMap(const ResTIMG*);

        void setLengthMap(const ResTIMG*);
        f32 refer(f32, f32) const;
        u16 getTexelOrder(u16, f32, u8) const;

        const ResTIMG* _0;
        const u8* _4;
        u8 _8;
    };

    class CIndex {
    public:
        CIndex();

        u16 _0;
        u16 _2;
    };

    CShader(const J3DModelData*, const ResTIMG*);

    virtual void calc(J3DModel*);
    virtual void setup(J3DModelData*);
    virtual ~CShader();

    void makeIndexData(J3DShape*) const;
    void checkBorderVtx(J3DModelData*, u32);

    f32 _8;
    CShader::CIndex* mIndexArray;
    CShader::CLengthMap mLengthMap;
    f32 _1C;
};
