#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class Material;
class MaterialEntry;
class ModelData;
class Model;
class MeshEntry;
// class LiveActor;
class ActorLightCtrl;

struct PacketInfo {
    MeshEntry* mShapePacket;
    ActorLightCtrl* mLightLoader;
    ActorLightCtrl* mLightCtrl;
};

class DrawBufferShapeDrawer {
public:
    DrawBufferShapeDrawer(Material*, MaterialEntry*);

    void init(s32);
    void swap(DrawBufferShapeDrawer*);
    void draw() const;
    void add(const MeshEntry*, const ActorLightCtrl*);
    void remove(const MeshEntry*);
    void resetLightSort(const ActorLightCtrl*);
    s32 findLightSortIndex(const ActorLightCtrl*, bool*) const;

    MeshEntry* getMeshEntry(int index) const {
        return mPackets[index]->mShapePacket;
    }

    ActorLightCtrl* getLightLoader(int index) const {
        return mPackets[index]->mLightLoader;
    }

    Material* mMaterial;
    MaterialEntry* mMatPacket;
    s32 _8;
    s32 mMaxPackets;
    s32 mNumPackets;
    PacketInfo** mPackets;
};

class DrawBuffer {
public:
    DrawBuffer(Model* pModel);

    void init(int);
    void add(const LiveActor*);
    void remove(const LiveActor*);
    void resetLightSort(const ActorLightCtrl*);
    void drawOpa() const;
    void drawXlu() const;
    void initTable();
    void sortShapeDrawer(s32, s32);

    DrawBufferShapeDrawer* getShapeDrawerByMatNo(int matNo) const {
        return mShapeDrawers[mMaterialNos[matNo]];
    }

    DrawBufferShapeDrawer* getShapeDrawerByIndex(int index) const {
        return mShapeDrawers[index];
    }

    ModelData* mModelData;
    Model* mModel;
    u32 _8;
    s32 mNumActors;
    u32 _10;
    s32 mNumMaterials;
    s32* mMaterialNos;
    s32 mNumShapeDrawers;
    s32 mNumOpaShapeDrawers;
    DrawBufferShapeDrawer** mShapeDrawers;
};
