#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>

class SimpleJ3DModelDrawer : public LiveActor {
public:
    SimpleJ3DModelDrawer(NameObj*, const char*, const char*, int);

    void initDraw() const;

    J3DMaterial* mMaterial;
    J3DShape* mShape;
    J3DShapeDraw* mShapeDraw;
};
