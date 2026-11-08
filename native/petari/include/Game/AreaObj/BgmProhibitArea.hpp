#pragma once

#include "Game/AreaObj/AreaObj.hpp"

struct BgmMuteSet;

class BgmProhibitArea : public AreaObj {
public:
    BgmProhibitArea(int, const char*);

    virtual void init(const TableIter&);
    virtual void movement();

    virtual const char* getManagerName() const {
        return "BgmProhibitArea";
    }

    const BgmMuteSet* mMuteSet;  // this galaxy's stage BGM states; null: the area does nothing
    bool mIsMuteSet;
    bool mIsUnmuteSet;
    Vec3f mPrevPlayerPos;
};
