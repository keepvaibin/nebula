#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class KeySwitch;
class LiveActorGroup;

typedef LiveActor* (*CreationFunc)(const char*);

struct ExterminationEntry {
    const char* mChildName;
    CreationFunc mCreationFunc;
};

class ExterminationChecker : public LiveActor {
public:
    ExterminationChecker(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;

    void exeWatching();
    void exeTryStartDemoAppear();
    void exeAppearStar();
    void exeAppearKeySwitch();

    LiveActorGroup* mGroup;
    KeySwitch* mKeySwitch;
    Vec3f mKeySwitchPos;
    u8 mIsPowerStar;
    u8 mIsKeySwitch;
};

namespace MR {
    NameObj* createExterminationPowerStar(const char*);
    NameObj* createExterminationKeySwitch(const char*);
};  // namespace MR
