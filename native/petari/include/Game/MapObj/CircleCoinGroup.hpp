#pragma once

#include "Game/MapObj/CoinGroup.hpp"

class CircleCoinGroup : public CoinGroup {
public:
    CircleCoinGroup(const char*);

    virtual void initCoinArray(const TableIter&);
    virtual void placementCoin();
    virtual const char* getCoinName() const;

    f32 mCoinRadius;
};

namespace MR {
    NameObj* createCircleCoinGroup(const char*);
    NameObj* createCirclePurpleCoinGroup(const char*);
};  // namespace MR
