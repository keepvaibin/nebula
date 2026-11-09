#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class Koopa;

class KoopaPlanetShadow : public LiveActor {
public:
    KoopaPlanetShadow(const Koopa* pKoopa);

    virtual ~KoopaPlanetShadow() override;
    virtual void init(const TableIter& rIter) override;
    virtual void calcAndSetBaseMtx() override;

    const Koopa* mKoopa;
    MtxPtr mHipMtx;
};
