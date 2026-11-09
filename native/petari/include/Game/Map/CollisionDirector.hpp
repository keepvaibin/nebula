#pragma once

#include "Game/NameObj/NameObj.hpp"

class CollisionCategorizedKeeper;
class CollisionCode;

// The terrain category a CollisionParts registers in; indexes CollisionDirector's keepers.
enum CollisionCategory {
    CollisionCategory_Map = 0,
    CollisionCategory_Sunshade = 1,
    CollisionCategory_WaterSurface = 2,
    CollisionCategory_MoveLimit = 3,
    CollisionCategory_Num = 4,
};

class CollisionDirector : public NameObj {
public:
    CollisionDirector();

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void movement() override;

    CollisionCategorizedKeeper* getCategoryKeeper(s32 category) {
        return mCategoryKeeper[category];
    }

    CollisionCategorizedKeeper* mCategoryKeeper[CollisionCategory_Num];
    CollisionCode* mCode;
};

namespace MR {
    CollisionDirector* getCollisionDirector();
};  // namespace MR
