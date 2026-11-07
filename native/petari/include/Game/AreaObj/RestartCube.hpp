#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class PlacementId;

class RestartCube : public AreaObj {
public:
    RestartCube(int, const char*);

    virtual ~RestartCube();
    virtual void init(const TableIter&);

    void updatePlayerRestartIdInfo();
    void changeBgm();

    PlacementId* mIdInfo;
    s32 _40;
    s32 _44;
    bool _48;
};
