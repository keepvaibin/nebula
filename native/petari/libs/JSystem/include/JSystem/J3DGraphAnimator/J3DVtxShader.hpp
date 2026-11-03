#pragma once


class J3DModel;
class J3DModelData;

class J3DVtxShader {
public:
    J3DVtxShader() : _4() {
    }

    virtual void calc(J3DModel*) = 0;
    virtual void setup(J3DModelData*) {
    }

    virtual ~J3DVtxShader() {
    }

    u32 _4;
};
