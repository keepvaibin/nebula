#pragma once

#include "Game/NameObj/NameObj.hpp"

class Coin;

class CoinReplica : public NameObj {
public:
    CoinReplica(const char*);

    virtual void init(const TableIter&) override;

    void activeCoin();
    void activeCoinWithGravity();
    void deactiveCoin();
    void removeCoin();

    Coin* mCoin;
};
