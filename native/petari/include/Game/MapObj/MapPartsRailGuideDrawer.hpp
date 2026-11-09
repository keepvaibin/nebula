#pragma once

#include "Game/MapObj/MapPartsFunction.hpp"
#include "Game/Util/Array.hpp"

class MapPartsRailGuidePoint;

class MapPartsRailGuideDrawer : public MapPartsFunction {
public:
    MapPartsRailGuideDrawer(LiveActor* pHost, const char* pModelName);

    virtual void init(const TableIter& rIter) override;
    virtual bool isWorking() const override;
    virtual void start() override;
    virtual void end() override;

    void initGuidePoints(const TableIter& rIter);

    void show();
    void hide();
    void exeHideAll();
    void exeDrawAll();
    void exeDrawForward();

    MR::InlineVector< MapPartsRailGuidePoint*, 256 > mGuidePoints;
    s32 mGuideType;
    s32 mRailId;
    const char* mModelName;
};
