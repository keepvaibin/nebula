#pragma once

#include <nebula_geometry.hpp>

struct JASSoundParams;

class JAIAudible {
public:
    virtual ~JAIAudible();
    virtual JASSoundParams* getOuterParams(int) = 0;
    virtual void calc() = 0;

    Vec3f& getPos() {
        return mPos;
    }
    void setPos(const Vec3f& other) {
        mPos.set(other);
    }

    Vec3f mPos;
};
