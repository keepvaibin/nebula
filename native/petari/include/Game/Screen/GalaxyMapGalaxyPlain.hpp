#pragma once

#include "Game/Screen/LayoutActor.hpp"
#include "Game/Util/StringUtil.hpp"

class GalaxyNamePlate;

class GalaxyMapGalaxyPlain : public LayoutActor {
public:
    /// @brief Creates a new `GalaxyMapGalaxyPlain`.
    /// @param pHost The pointer to the owning actor instance.
    GalaxyMapGalaxyPlain(const LayoutActor* pHost);

    virtual void init(const TableIter& rIter) override;
    virtual void movement() override;
    virtual void draw() const override;
    virtual void calcAnim() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;

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
