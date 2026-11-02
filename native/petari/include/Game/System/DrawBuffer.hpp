#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class J3DMaterial;
class J3DMatPacket;
class J3DModelData;
class J3DModel;
class J3DShapePacket;
// class LiveActor;
class ActorLightCtrl;

struct PacketInfo {
    J3DShapePacket* mShapePacket;
    ActorLightCtrl* mLightLoader;
    ActorLightCtrl* mLightCtrl;
};

class DrawBufferShapeDrawer {
public:
    DrawBufferShapeDrawer(J3DMaterial*, J3DMatPacket*);

    void init(s32);
    void swap(DrawBufferShapeDrawer*);
    void draw() const;
    void add(const J3DShapePacket*, const ActorLightCtrl*);
    void remove(const J3DShapePacket*);
    void resetLightSort(const ActorLightCtrl*);
    s32 findLightSortIndex(const ActorLightCtrl*, bool*) const;

    J3DShapePacket* getShapePacket(int index) const {
        return mPackets[index]->mShapePacket;
    }

    ActorLightCtrl* getLightLoader(int index) const {
        return mPackets[index]->mLightLoader;
    }

    J3DMaterial* mMaterial;
    J3DMatPacket* mMatPacket;
    s32 _8;
    s32 mMaxPackets;
    s32 mNumPackets;
    PacketInfo** mPackets;
};

class DrawBuffer {
public:
    DrawBuffer(J3DModel* pModel);

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

    J3DModelData* mModelData;
    J3DModel* mModel;
    u32 _8;
    s32 mNumActors;
    u32 _10;
    s32 mNumMaterials;
    s32* mMaterialNos;
    s32 mNumShapeDrawers;
    s32 mNumOpaShapeDrawers;
    DrawBufferShapeDrawer** mShapeDrawers;
};
