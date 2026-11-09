#pragma once

#include "Game/Scene/SceneNameObjListExecutor.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/NerveExecutor.hpp"

class Scene : public NerveExecutor {
public:
    Scene(const char*);

    virtual ~Scene() override;
    virtual void init();
    virtual bool initAfterScenarioChosen() { return true; }
    virtual void start();
    virtual void update();
    virtual void draw() const;
    virtual void calcAnim();

    void initNameObjListExecutor();
    void initSceneObjHolder();

    SceneNameObjListExecutor* mListExecutor;
    SceneObjHolder* mSceneObjHolder;
};
