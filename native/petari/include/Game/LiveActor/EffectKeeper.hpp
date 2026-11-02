#pragma once

#include "Game/Util/Array.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class Binder;
class HashSortTable;
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
    void registerEffect(const char*, const TVec3f*, const TVec3f*, const TVec3f*, const char*);
    void registerEffect(const char*, MtxPtr, const char*, const char*);
    void registerEffect(const char*, MtxPtr, const TVec3f*, const char*, const char*);
    void registerEffectWithoutSRT(const char*, const char*);
    void registerSyncBckEffect(XanimePlayer*, const char*, const char*, s32, f32, f32, bool);
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
    void enableSort();
    void finalizeSort();
    bool isRegisteredEmitter(const char*) const;
    void checkExistenceAttributeEffect();
    void registMultiEmitter(MultiEmitter*, const char*, const char*);
    void updateFloorCode();
    void updateFloorCode(const Triangle*);
    bool isTypeAttributeEffect(const char*) const;

    const char* _0;
    ResourceHolder* _4;  // mResourceHolder
    const char* _8;      // mResourceName
    MR::Vector< MR::AssignableArray< MultiEmitter* > > _C;
    HashSortTable* _18;
    u32 _1C;
    SyncBckEffectChecker* _20;
    const Binder* mBinder;
    s32 _28;
    s32 _2C;
    bool _30;
};
