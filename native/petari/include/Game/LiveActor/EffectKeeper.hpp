#pragma once

#include "Game/Util/Array.hpp"
#include "Game/Util/NameIndex.hpp"
#include <nebula_geometry.hpp>

class Binder;
class LiveActor;
class MultiEmitter;
class ResourceHolder;
class SyncBckEffectChecker;
class Triangle;
class XanimePlayer;

class EffectKeeper {
public:
    EffectKeeper(const char*, ResourceHolder*, int, const char*);

    void init(LiveActor*);
    void initAfterPlacementForAttributeEffect(MtxPtr);
    void setBinder(const Binder*);
    void addEffect(const char*, LiveActor*);
    // pUniqueName: the name the keeper finds the effect by; null: pName.
    void registerEffect(const char* pName, const Vec3f* pTranslation, const Vec3f* pRotation, const Vec3f* pScale, const char* pUniqueName);
    void registerEffect(const char* pName, MtxPtr pMtx, const char* pUniqueName);
    void registerEffect(const char* pName, MtxPtr pMtx, const Vec3f* pScale, const char* pUniqueName);
    void registerEffectWithoutSRT(const char* pName, const char* pUniqueName);
    void registerSyncBckEffect(XanimePlayer*, const char*, const char*, s32, f32, f32);
    MultiEmitter* createEmitter(const char*);
    void deleteEmitter(const char*);
    void forceDeleteEmitter(const char*);
    void deleteEmitterAll();
    void forceDeleteEmitterAll();
    MultiEmitter* getEmitter(const char*) const;
    MultiEmitter* getEmitter(s32) const;
    void changeEffectName(const char*, const char*);
    void update();
    void updateSyncBckEffect();
    void syncEffectBck(MultiEmitter*);
    void updateAttributeEffect();
    void stopEmitterOnClipped();
    void playEmitterOffClipped();
    void clear();
    void changeBck();
    void onDraw();
    void offDraw();
    bool isRegisteredEmitter(const char*) const;
    void checkExistenceAttributeEffect();
    void registMultiEmitter(MultiEmitter*, const char*, const char*);
    void updateFloorCode();
    void updateFloorCode(const Triangle*);
    bool isTypeAttributeEffect(const char*) const;

    const char* mName;
    ResourceHolder* mResourceHolder;
    const char* mResourceName;
    MR::Vector< MR::AssignableArray< MultiEmitter* > > mEmitters;
    // An emitter's name (its unique name, else its effect name) to its index in mEmitters. The names are held by pointer:
    // resource, table and literal strings, which outlive the keeper.
    MR::NameIndex mNames;
    SyncBckEffectChecker* mSyncBckChecker;
    const Binder* mBinder;
    s32 mFloorCode;  // of the ground below, for the effects that change with it
    s32 mPrevFloorCode;
    bool mHasAttributeEffect;
};
