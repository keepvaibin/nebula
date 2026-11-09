#pragma once

#include "Game/NameObj/NameObjListExecutor.hpp"

class SceneNameObjListExecutor : public NameObjListExecutor {
public:
    SceneNameObjListExecutor() : NameObjListExecutor() {
    }

    virtual void initMovementList() override;
    virtual void initCalcAnimList() override;
    virtual void initCalcViewAndEntryList() override;
    virtual void initDrawList() override;
};
