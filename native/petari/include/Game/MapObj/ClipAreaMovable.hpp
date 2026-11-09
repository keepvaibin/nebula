#pragma once

#include "Game/MapObj/ClipArea.hpp"

class MapPartsRailMover;
class MapPartsRotator;

class ClipAreaMovable : public ClipArea {
public:
    ClipAreaMovable(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;
    virtual void control() override;

    void initMoveFunction(const TableIter& rIter);
    void startMoveFunction();
    void endMoveFunction();
    void movementMoveFunction();
    void updateMatrix();

    MapPartsRailMover* mMapPartsRailMover;
    MapPartsRotator* mMapPartsRotator;
};

namespace MR {
    NameObj* createClipAreaSphere(const char* pModel);
    NameObj* createClipAreaCenterBox(const char* pModel);
    NameObj* createClipAreaBottomBox(const char* pModel);
    NameObj* createClipAreaTopCone(const char* pModel);
    NameObj* createClipAreaBottomCone(const char* pModel);
};  // namespace MR
