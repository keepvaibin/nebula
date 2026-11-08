#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"
#include <nebula_geometry.hpp>

class AirBubble;

class AirBubbleHolder : public DeriveActorGroup< AirBubble > {
public:
    AirBubbleHolder(const char* pName);

    virtual void init(const TableIter&);

    void appearAirBubble(const Vec3f&, s32);
    void createAirBubbles();
};

namespace MR {
    AirBubbleHolder* createAirBubbleHolder();
    void appearAirBubble(const Vec3f&, s32);
};  // namespace MR
