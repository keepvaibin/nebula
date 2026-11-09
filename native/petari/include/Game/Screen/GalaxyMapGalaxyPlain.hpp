#pragma once

#include "Game/Screen/LayoutActor.hpp"
#include "Game/Util/StringUtil.hpp"

class GalaxyNamePlate;

class GalaxyMapGalaxyPlain : public LayoutActor {
public:
    /// @brief Creates a new `GalaxyMapGalaxyPlain`.
    /// @param pHost The pointer to the owning actor instance.
    GalaxyMapGalaxyPlain(const LayoutActor* pHost);

    virtual void init(const TableIter& rIter);
    virtual void movement();
    virtual void draw() const;
    virtual void calcAnim();
    virtual void appear();
    virtual void kill();
    virtual void control();

    void show(const char*, const char*);
    void adjustTransInScreen();
    void show(const char16_t*, const char*);

private:
    const LayoutActor* mHost;
    const char* mPaneName;
    GalaxyNamePlate* mNamePlate;
    MR::Text16 mGalaxyName;
    bool mIsShow;
};
