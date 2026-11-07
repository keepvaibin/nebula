#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class Koopa;

class KoopaPlanetShadow : public LiveActor {
public:
    KoopaPlanetShadow(const Koopa* pKoopa);

    virtual ~KoopaPlanetShadow();
    virtual void init(const TableIter& rIter);
    virtual void calcAndSetBaseMtx();

    const Koopa* mKoopa;
    MtxPtr mHipMtx;
};
