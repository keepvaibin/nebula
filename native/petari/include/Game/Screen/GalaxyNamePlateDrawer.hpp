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

    virtual void init(const JMapInfoIter& rIter);
    virtual void draw() const;

    GalaxyNamePlatePrioritied* registerPlate(const LayoutActor*);

private:
    MR::Vector< MR::FixedArray< GalaxyNamePlatePrioritied*, 16 > > mPlateArray;
};

namespace MR {
    GalaxyNamePlatePrioritied* registerToGalaxyNamePlateDrawer(const LayoutActor*);
};  // namespace MR
