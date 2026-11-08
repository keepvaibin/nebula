#pragma once

#include <nebula_geometry.hpp>
#include "Game/Particle/ParticleCamera.hpp"

class EmitterCallBack;
class ParticleCallBack;
class EmitterCore;
class ParticleResources;
class ParticleCore;
struct ParticleChunk;
class NebulaArena;
struct EmitterWork;

// A group's live emitters, oldest first: calc and draw go in that order.
struct EmitterList {
    EmitterCore* mpFirst;
    EmitterCore* mpLast;
};

class EmitterManager {
public:
    EmitterManager(u32 emitterCount, NebulaArena* pHeap, u8 groupCount, u8 resourceCount);
    EmitterCore* createSimpleEmitterID(Vec3f const&, u16, u8, u8, EmitterCallBack*, ParticleCallBack*);
    void calc(u8);
    void draw(ParticleCamera const*, u8);
    void forceDeleteEmitter(EmitterCore*);
    void entryResourceManager(ParticleResources*, u8);
    // Starts the emitters' seeds over: each emitter's seed comes from the scene, its resource and how many emitters of that
    // resource the scene made before it, so emitters added or removed elsewhere leave its particles as they were.
    void startScene(u32 sceneSeed);
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

public:
    // Live emitters by group, and the free ones (a stack through mpNextEmtr).
    EmitterList* pEmtrUseList;
    EmitterCore* mpFreeEmtrs;
    ParticleChunk* mpFreeChunks;
    ParticleResources** pResMgrAry;
    // Emitters made this scene, by resource manager and resource.
    u32** mpMadeCounts;
    u32 mSceneSeed;
    EmitterWork* pWd;
    u32 emtrNum;
    u8 gidMax;
    u8 ridMax;
    NebulaArena* mHeap;
};
