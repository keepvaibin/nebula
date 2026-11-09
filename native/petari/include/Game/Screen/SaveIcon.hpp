#pragma once

#include "Game/Screen/LayoutActor.hpp"

class SaveIcon : public LayoutActor {
public:
    SaveIcon(const LayoutActor* pActor);

    virtual void calcAnim() override;
    virtual void appear() override;

private:
    const LayoutActor* mActor;
};
