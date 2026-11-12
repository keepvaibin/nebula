// The placement facts' fixture (tools/lumacore/places_fixture_test.py, plan 1.8 places): reads into a field, a local
// and through this->, a base's reads inherited, a read through a free helper, a column read by its own name, and the
// factory's three kinds of creator (NameObj/NameObjFactory.cpp).
#pragma once

#include "Game/Util/PlacementUtil.hpp"

struct PfObj {
    virtual ~PfObj();
};
struct PfArea : PfObj {
    s32 mObjArg0;
    void initArea(const TableIter& rIter);
};
struct PfCube : PfArea {
    f32 mSpeed;
    bool mLoop;
    f32 mRotateSpeed;
    void init(const TableIter& rIter);
};
struct PfDome : PfObj {
    s32 mDome;
    void init(const TableIter& rIter);
};
s32 pfDomeFromArg0(const TableIter& rIter);  // a helper: the dome reads its Obj_arg0 through it
s32 pfUnused(const TableIter& rIter);        // reads, but no method hands it an iterator: left
struct PfAlpha : PfObj {};
struct PfBravo : PfObj {};
struct PfCharlie : PfObj {};
PfObj* pfCreateCharlie(const char* pName);  // a creator whose body is in another unit (places_fixture.cpp)
