#pragma once

#include "Game/Screen/LayoutActor.hpp"

namespace nw4r {
    namespace lyt {
        class DrawInfo;
    };  // namespace lyt
};  // namespace nw4r

class GalaxyNamePlate;

class GalaxyMapDomeIcon : public LayoutActor {
public:
    GalaxyMapDomeIcon(int, LayoutActor*, const char*, const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();
    virtual void draw() const;
    virtual void calcAnim();
    virtual void appear();
    virtual void kill();
    virtual void control();

    void calcAnimForCapture(const nw4r::lyt::DrawInfo&);
    void setModeNormal();
    void setModeNewDomeDiscover();
    s32 calcDomeStatus() const;
    bool isComplete() const;
    void syncStatus();
    bool hasNewGalaxy() const;

    int _20;
    LayoutActor* mHost;
    const char* mPaneName;
    const char* _2C;
    GalaxyNamePlate* mNamePlate;
    u8 mMode;
};
