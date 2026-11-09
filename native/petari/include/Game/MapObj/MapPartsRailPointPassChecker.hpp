#pragma once

#include "Game/MapObj/MapPartsFunction.hpp"

class MapPartsRailPointPassChecker : public MapPartsFunction {
public:
    MapPartsRailPointPassChecker(LiveActor*);

    virtual void movement() override;
    virtual bool isWorking() const override;
    virtual void start() override;
    virtual void end() override {
    }

    bool isPassed() const;
    bool isReachedEnd() const;
    bool isPassedStartPoint() const;
    bool isPassedEndPoint() const;

    s32 mRailPointNo;
};