#pragma once

#include "Game/MapObj/CoinGroup.hpp"

class RailCoin : public CoinGroup {
public:
    RailCoin(const char*);

    virtual void initCoinArray(const TableIter&) override;
    virtual void placementCoin() override;
    virtual const char* getCoinName() const override;

    void placementNormalRail();
    void placementMercatorRail();
};

namespace MR {
    NameObj* createRailCoin(const char*);
    NameObj* createRailPurpleCoin(const char*);
};  // namespace MR
