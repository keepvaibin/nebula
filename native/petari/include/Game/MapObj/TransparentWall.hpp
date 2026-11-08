#pragma once

#include "Game/MapObj/InvisiblePolygonObj.hpp"

// A wall that is solid only while the player is in one of the forms its arg 4 names.
class TransparentWall : public InvisiblePolygonObj {
public:
    TransparentWall(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void control();

    // Bits 0-9: element modes; bit 10: skating; bit 11: any form but the vanished Boo.
    s32 mSolidForms;
    bool mIsSolid;
};
