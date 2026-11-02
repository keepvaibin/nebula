#pragma once

#include "JSystem/JGeometry/TVec.hpp"
#include "JSystem/JParticle/JPADrawInfo.hpp"
#include "JSystem/JParticle/JPAList.hpp"
#include "JSystem/JSupport/JSUList.hpp"

class JPAEmitterCallBack;
class JPAParticleCallBack;
class JPABaseEmitter;
class JPAResourceManager;
class JPABaseParticle;
class NebulaArena;
struct JPAEmitterWorkData;

class JPAEmitterManager {
public:
    JPAEmitterManager(u32, u32, NebulaArena*, u8, u8);
    JPABaseEmitter* createSimpleEmitterID(JGeometry::TVec3< f32 > const&, u16, u8, u8, JPAEmitterCallBack*, JPAParticleCallBack*);
    void calc(u8);
    void draw(JPADrawInfo const*, u8);
    void forceDeleteAllEmitter();
    void forceDeleteGroupEmitter(u8);
    void forceDeleteEmitter(JPABaseEmitter*);
    void entryResourceManager(JPAResourceManager*, u8);
    void clearResourceManager(u8);
    void calcYBBCam();
    JPAResourceManager* getResourceManager(u16 idx) const {
        return pResMgrAry[idx];
    }
    JPAResourceManager* getResourceManager(u8 res_mgr_id) const {
        return pResMgrAry[res_mgr_id];
    }
    void addEmitters(u32);
    void addParticles(u32);
    void growEmitters();
    void growParticles();
    int getEmitterNumber() const {
        return emtrNum - mFreeEmtrList.getNumLinks();
    }

public:
    JSUList< JPABaseEmitter >* pEmtrUseList;
    JSUList< JPABaseEmitter > mFreeEmtrList;
    JPAList< JPABaseParticle > mPtclPool;
    JPAResourceManager** pResMgrAry;
    JPAEmitterWorkData* pWd;
    u32 emtrNum;
    u32 ptclNum;
    u8 gidMax;
    u8 ridMax;
    NebulaArena* mHeap;
};
