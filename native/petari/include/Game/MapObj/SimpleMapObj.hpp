#pragma once

#include "Game/MapObj/MapObjActor.hpp"
#include <nebula_geometry.hpp>


struct SoundEffectDataEntry {
    const char* objectName;
    const char* soundName;
    f32 rotatorRadius;
};

class SimpleMapObj : public MapObjActor {
public:
    SimpleMapObj(const char* pName) : MapObjActor(pName) {
    }

    virtual ~SimpleMapObj() {
    }

    virtual void init(const TableIter&);
};

class SimpleEnvironmentObj : public MapObjActor {
public:
    inline SimpleEnvironmentObj(const char* pName) : MapObjActor(pName) {
    }

    virtual void init(const TableIter&);
    virtual void connectToScene(const MapObjActorInitInfo&);
};

class SimpleMapObjFarMax : public SimpleMapObj {
public:
    inline SimpleMapObjFarMax(const char* pName) : SimpleMapObj(pName) {
    }

    virtual ~SimpleMapObjFarMax() {
    }

    virtual void init(const TableIter&);
};

class SimpleMapObjPush : public SimpleMapObj {
public:
    inline SimpleMapObjPush(const char* pName) : SimpleMapObj(pName) {
    }

    virtual void attackSensor(HitSensor*, HitSensor*);
};

class SimpleMapObjNoSilhouetted : public SimpleMapObj {
public:
    inline SimpleMapObjNoSilhouetted(const char* pName) : SimpleMapObj(pName) {
    }

    virtual void connectToScene(const MapObjActorInitInfo&);
};

class SimpleSeesawObj : public MapObjActor {
public:
    SimpleSeesawObj(const char*);

    virtual void init(const TableIter&);
    virtual void control();

};

class SimpleMirrorObj : public MapObjActor {
public:
    SimpleMirrorObj(const char* pName, const char* pObjName = nullptr, MtxPtr = nullptr);

    virtual void init(const TableIter&);
    virtual void calcAndSetBaseMtx();

    MtxPtr mMtx;
};

class SimpleMirrorReflectionObj : public SimpleMapObj {
public:
    inline SimpleMirrorReflectionObj(const char* pName) : SimpleMapObj(pName) {
    }

    virtual void init(const TableIter&);
    virtual void connectToScene(const MapObjActorInitInfo&);
};

class SimpleTextureSwitchChangeObj : public SimpleMapObj {
public:
    inline SimpleTextureSwitchChangeObj(const char* pName) : SimpleMapObj(pName) {
    }

    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&);
    virtual s32 getChangedTextureNo() const;

    void changeTexture();
};

class UFOKinokoUnderConstruction : public MapObjActor {
public:
    inline UFOKinokoUnderConstruction(const char* pName) : MapObjActor(pName) {
    }

    virtual void init(const TableIter&);
};

class GlaringLight : public SimpleMapObj {
public:
    inline GlaringLight(const char* pName) : SimpleMapObj(pName) {
    }

    virtual void connectToScene(const MapObjActorInitInfo&);
};

namespace MR {
};  // namespace MR
