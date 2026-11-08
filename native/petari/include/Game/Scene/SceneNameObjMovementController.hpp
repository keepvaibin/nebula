#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Scene/MovementControlType.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/System/NerveExecutor.hpp"

class StopSceneStateControl : public NerveExecutor {
public:
    /// @brief Creates a new `StopSceneStateControl`.
    StopSceneStateControl();

    void requestStopSceneFor(MR::MovementControlType, const NameObj*);
    void requestStopSceneOverwrite(const NameObj*);
    void requestPlaySceneFor(MR::MovementControlType, const NameObj*);
    void executeStopCategories(MR::MovementControlType);
    void exeNone();
    void exeStopped();

private:
    MR::MovementControlType mMovementType;
    const NameObj* mRequester;
};

class SceneNameObjMovementController : public NameObj {
public:
    /// @brief Creates a new `SceneNameObjMovementController`.
    SceneNameObjMovementController();

    virtual void movement();

    void requestStopSceneFor(MR::MovementControlType, const NameObj*);
    void requestStopSceneOverwrite(const NameObj*);
    void requestPlaySceneFor(MR::MovementControlType, const NameObj*);
    void notifyRequestNameObjMovementOnOff(bool);

private:
    bool mIsSyncRequested;
    StopSceneStateControl* mStopSceneStateControl;
};

namespace MR {
    SceneNameObjMovementController* getSceneNameObjMovementController();
    void notifyRequestNameObjMovementOnOff();
};  // namespace MR
