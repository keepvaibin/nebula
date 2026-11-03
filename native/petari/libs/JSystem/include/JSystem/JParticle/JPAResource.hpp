#pragma once


class NebulaArena;
struct JPAEmitterWorkData;
class JPABaseEmitter;
class JPABaseParticle;

struct JPAKeyBlock;
struct JPABaseShape;
class JPAExtraShape;
class JPAChildShape;
class JPAExTexShape;
class JPADynamicsBlock;
class JPAFieldBlock;

class JPAResource {
public:
    JPAResource();
    void init(NebulaArena*);
    bool calc(JPAEmitterWorkData*, JPABaseEmitter*);
    void draw(JPAEmitterWorkData*, JPABaseEmitter*);
    void drawP(JPAEmitterWorkData*);
    void drawC(JPAEmitterWorkData*);
    void setPTev();
    void setCTev(JPAEmitterWorkData*);
    void calc_p(JPAEmitterWorkData*, JPABaseParticle*);
    void calc_c(JPAEmitterWorkData*, JPABaseParticle*);
    void calcField(JPAEmitterWorkData*, JPABaseParticle*);
    void calcKey(JPAEmitterWorkData*);
    void calcWorkData_c(JPAEmitterWorkData*);
    void calcWorkData_d(JPAEmitterWorkData*);

    JPABaseShape* getBsp() const {
        return mpBaseShape;
    }
    JPAExtraShape* getEsp() const {
        return mpExtraShape;
    }
    JPAChildShape* getCsp() const {
        return mpChildShape;
    }
    JPAExTexShape* getEts() const {
        return mpExTexShape;
    }
    JPADynamicsBlock* getDyn() const {
        return mpDynamicsBlock;
    }

    u32 getTexIdx(u32 idx) const {
        return mpTDB1[idx];
    }
    u16 getUsrIdx() const {
        return mUsrIdx;
    }

public:
    typedef void (*EmitterFunc)(JPAEmitterWorkData*);
    typedef void (*ParticleFunc)(JPAEmitterWorkData*, JPABaseParticle*);
    EmitterFunc* mpCalcEmitterFuncList;
    EmitterFunc* mpDrawEmitterFuncList;
    EmitterFunc* mpDrawEmitterChildFuncList;
    ParticleFunc* mpCalcParticleFuncList;
    ParticleFunc* mpDrawParticleFuncList;
    ParticleFunc* mpCalcParticleChildFuncList;
    ParticleFunc* mpDrawParticleChildFuncList;

    JPABaseShape* mpBaseShape;
    JPAExtraShape* mpExtraShape;
    JPAChildShape* mpChildShape;
    JPAExTexShape* mpExTexShape;
    JPADynamicsBlock* mpDynamicsBlock;
    JPAFieldBlock** mpFieldBlocks;
    JPAKeyBlock** mpKeyBlocks;
    // The resource's texture index table.
    u16 const* mpTDB1;
    u16 mUsrIdx;
    u8 mFieldBlockNum;
    u8 mKeyBlockNum;
    u8 mTDB1Num;
    u8 mpCalcEmitterFuncListNum;
    u8 mpDrawEmitterFuncListNum;
    u8 mpDrawEmitterChildFuncListNum;
    u8 mpCalcParticleFuncListNum;
    u8 mpDrawParticleFuncListNum;
    u8 mpCalcParticleChildFuncListNum;
    u8 mpDrawParticleChildFuncListNum;
};
