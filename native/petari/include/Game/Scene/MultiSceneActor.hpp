#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class EffectSystem;
class ModelManager;
class MultiSceneEffectKeeper;
class Nerve;
class Spine;

class MultiSceneActor : public NameObj {
public:
    MultiSceneActor(const char* pName, const char* pModelName, bool deferMaterials);

    virtual void init(const TableIter&) override;
    virtual void movement() override;
    virtual void calcAnim() override;
    virtual void calcViewAndEntry() override;
    virtual void appear();
    virtual void kill();
    virtual void control();

    ModelManager* getModelManager() const;
    EffectSystem* getEffectSystem() const;
    MultiSceneEffectKeeper* getEffectKeeper() const;
    void initEffect(EffectSystem*, int extraEmitterNum, const char* pResName);
    void setNerve(const Nerve*);
    bool isNerve(const Nerve*) const;
    void initNerve(const Nerve*);
    s32 getNerveStep() const;

    Vec3f mTranslation;
    Vec3f mRotation;
    Vec3f mScale;
    bool mIsDead;
    ModelManager* mModelManager;
    EffectSystem* mEffectSystem;
    MultiSceneEffectKeeper* mEffectKeeper;
    Spine* mSpine;
    // Set once in the constructor: this actor is the star-select sky.
    bool mIsScenarioSelectSky;
};

namespace MultiScene {
    void startBck(MultiSceneActor*, const char*);
    void startBtk(MultiSceneActor*, const char*);
    void startBtp(MultiSceneActor*, const char*);
    void startBva(MultiSceneActor*, const char*);
    void setBtpFrameAndStop(MultiSceneActor*, f32);
    void setBvaFrameAndStop(MultiSceneActor*, f32);
    bool isBckStopped(MultiSceneActor*);
    bool isStep(const MultiSceneActor*, s32);
    bool isFirstStep(const MultiSceneActor*);
    bool isGreaterStep(const MultiSceneActor*, s32);
    bool isLessStep(const MultiSceneActor*, s32);
    bool isLessEqualStep(const MultiSceneActor*, s32);
    void setNerveAtStep(MultiSceneActor*, const Nerve*, s32);
    void setNerveAtBckStopped(MultiSceneActor*, const Nerve*);
    f32 calcNerveRate(const MultiSceneActor*, s32);
    f32 calcNerveRate(const MultiSceneActor*, s32, s32);
    f32 calcNerveEaseOutRate(const MultiSceneActor*, s32);
    f32 calcNerveValue(const MultiSceneActor*, s32, f32, f32);
    void emitEffect(const MultiSceneActor*, const char* pEffectName);
    void deleteEffectAll(const MultiSceneActor*);
    void forceDeleteEffectAll(const MultiSceneActor*);
    MtxPtr getJointMtx(const MultiSceneActor*, const char* pJointName);
};  // namespace MultiScene
