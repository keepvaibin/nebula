#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/ModelData.hpp"

class SimpleModelDrawer : public LiveActor {
public:
    SimpleModelDrawer(NameObj*, const char*, const char*, int);

    Material* mMaterial;
    Mesh* mShape;
};
