#pragma once

#include "Game/Screen/LayoutActor.hpp"

class GalaxyNamePlate;

class GalaxyMapGalaxyPlain : public LayoutActor {
public:
    /// @brief Creates a new `GalaxyMapGalaxyPlain`.
    /// @param pHost The pointer to the owning actor instance.
    GalaxyMapGalaxyPlain(const LayoutActor* pHost);

    virtual void init(const JMapInfoIter& rIter);
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
    char16_t* mGalaxyName;
    bool mIsShow;
};
