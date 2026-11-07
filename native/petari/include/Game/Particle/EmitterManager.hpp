#pragma once

#include <nebula_geometry.hpp>
#include "Game/Particle/ParticleCamera.hpp"
#include "JSystem/JParticle/JPAList.hpp"
#include "JSystem/JSupport/JSUList.hpp"

class EmitterCallBack;
class ParticleCallBack;
class EmitterCore;
class ParticleResources;
class ParticleCore;
struct ParticleChunk;
class NebulaArena;
struct EmitterWork;

class EmitterManager {
public:
    EmitterManager(u32 emitterCount, NebulaArena* pHeap, u8 groupCount, u8 resourceCount);
    EmitterCore* createSimpleEmitterID(Vec3f const&, u16, u8, u8, EmitterCallBack*, ParticleCallBack*);
    void calc(u8);
    void draw(ParticleCamera const*, u8);
    void forceDeleteAllEmitter();
    void forceDeleteGroupEmitter(u8);
    void forceDeleteEmitter(EmitterCore*);
    void entryResourceManager(ParticleResources*, u8);
    void clearResourceManager(u8);
    void calcYBBCam();
    ParticleResources* getResourceManager(u16 idx) const {
        return pResMgrAry[idx];
    }
    ParticleResources* getResourceManager(u8 res_mgr_id) const {
        return pResMgrAry[res_mgr_id];
    }
    void addEmitters(u32);
    void growEmitters();
    // A chunk of free particle slots for an emitter (a new one when none is free), and an emitter's chunks back.
    ParticleChunk* takeChunk();
    void giveChunks(ParticleChunk* pFirst, ParticleChunk* pLast);
    int getEmitterNumber() const {
        return emtrNum - mFreeEmtrList.getNumLinks();
    }

public:
    JSUList< EmitterCore >* pEmtrUseList;
    JSUList< EmitterCore > mFreeEmtrList;
    ParticleChunk* mpFreeChunks;
    ParticleResources** pResMgrAry;
    EmitterWork* pWd;
    u32 emtrNum;
    u8 gidMax;
    u8 ridMax;
    NebulaArena* mHeap;
};
