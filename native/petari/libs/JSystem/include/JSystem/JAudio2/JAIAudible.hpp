#pragma once

#include "JSystem/JGeometry/TVec.hpp"

struct JASSoundParams;

class JAIAudible {
public:
    virtual ~JAIAudible();
    virtual JASSoundParams* getOuterParams(int) = 0;
    virtual void calc() = 0;

    TVec3f& getPos() {
        return mPos;
    }
    void setPos(const TVec3f& other) {
        mPos.set(other);
    }

    TVec3f mPos;
};
