#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class BgmProhibitArea : public AreaObj {
public:
    BgmProhibitArea(int, const char*);

    virtual ~BgmProhibitArea();
    virtual void init(const JMapInfoIter&);
    virtual void movement();

    virtual const char* getManagerName() const {
        return "BgmProhibitArea";
    }

    bool _3C;
    bool _3D;
    TVec3f _40;
};
