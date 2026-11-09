#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"

class LayoutActor;

struct GalaxyNamePlatePrioritied {
    const LayoutActor* mHostActor;
    u32 mZ;

    GalaxyNamePlatePrioritied(const LayoutActor* pActor) : mHostActor(pActor), mZ() {
    }
};

class GalaxyNamePlateDrawer : public NameObj {
public:
    /// @brief Creates a new `GalaxyNamePlateDrawer`.
    GalaxyNamePlateDrawer();

    virtual void init(const TableIter& rIter) override;
    virtual void draw() const override;

    GalaxyNamePlatePrioritied* registerPlate(const LayoutActor*);

private:
    MR::InlineVector< GalaxyNamePlatePrioritied*, 16 > mPlateArray;
    // The plates in drawing order, sorted again at each draw.
    mutable MR::InlineVector< const GalaxyNamePlatePrioritied*, 16 > mDrawOrder;
};

namespace MR {
    GalaxyNamePlatePrioritied* registerToGalaxyNamePlateDrawer(const LayoutActor*);
};  // namespace MR
