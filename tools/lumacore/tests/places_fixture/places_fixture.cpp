// The placement facts' fixture: the reads (see places_fixture.hpp).
#include "places_fixture.hpp"

PfObj::~PfObj() {}

void PfArea::initArea(const TableIter& rIter) {
    MR::getPlacementArg0NoInit(rIter, &mObjArg0);
}

void PfCube::init(const TableIter& rIter) {
    initArea(rIter);  // the base's own method: its read comes by inheritance, not as a helper
    MR::getPlacementArg1WithInit(rIter, &mSpeed);
    bool loop = false;
    MR::getPlacementArg2NoInit(rIter, &loop);
    mLoop = loop;
    MR::getPlacementArgNoInit(rIter, "RotateSpeed", &mRotateSpeed);
}

s32 pfDomeFromArg0(const TableIter& rIter) {
    s32 dome = -1;
    MR::getPlacementArg0NoInit(rIter, &dome);
    return dome;
}

s32 pfUnused(const TableIter& rIter) {
    s32 value = 0;
    MR::getPlacementArg5NoInit(rIter, &value);
    return value;
}

void PfDome::init(const TableIter& rIter) {
    mDome = pfDomeFromArg0(rIter);
    MR::getPlacementArg3NoInit(rIter, &this->mDome);
}

PfObj* pfCreateCharlie(const char*) {
    return new PfCharlie();
}
