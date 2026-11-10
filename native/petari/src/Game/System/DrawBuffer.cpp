#include "Game/System/DrawBuffer.hpp"
#include "Game/Render/Material.hpp"
#include "Game/LiveActor/ActorLightCtrl.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Render/Model.hpp"
#include "Game/System/NebulaArena.hpp"

DrawBufferShapeDrawer::DrawBufferShapeDrawer(Material* pMaterial, MaterialEntry* pMatPacket)
    : mMaterial(pMaterial), mMatPacket(pMatPacket), mNumShapes(1), mMaxPackets(0), mNumPackets(0), mPackets(nullptr) {
}

void DrawBufferShapeDrawer::init(s32 maxActors) {
    mMaxPackets = maxActors * mNumShapes;
    mPackets = new PacketInfo[mMaxPackets]();
}

void DrawBufferShapeDrawer::reserve(s32 maxActors) {
    const s32 maxPackets = maxActors * mNumShapes;
    PacketInfo* pPackets = new (NebulaArena::findFromRoot(this), 0) PacketInfo[maxPackets]();
    for (s32 idx = 0; idx < mNumPackets; idx++) {
        pPackets[idx] = mPackets[idx];
    }
    delete[] mPackets;
    mPackets = pPackets;
    mMaxPackets = maxPackets;
}

void DrawBufferShapeDrawer::swap(DrawBufferShapeDrawer* pOther) {
    Material* material = mMaterial;
    MaterialEntry* matPacket = mMatPacket;
    s32 __8 = mNumShapes;

    mMaterial = pOther->mMaterial;
    mMatPacket = pOther->mMatPacket;
    mNumShapes = pOther->mNumShapes;

    pOther->mMaterial = material;
    pOther->mMatPacket = matPacket;
    pOther->mNumShapes = __8;
}

void DrawBufferShapeDrawer::draw() const {
    Material* pShared = mMatPacket->getMaterial();
    MaterialDraw draw(pShared);

    for (s32 idx = 0; idx < mNumPackets; idx++) {
        MeshEntry* packet = mPackets[idx].mShapePacket;

        if (getLightLoader(idx) != nullptr) {
            getLightLoader(idx)->loadLight();
        }

        if (!packet->mMesh->isHidden()) {
            if (packet->mMaterial != pShared) {
                draw.sendOwn(packet->mMaterial, pShared);
            }
            if (!packet->isHidden()) {
                draw.draw(packet->mModel, packet->mMesh);
            }
        }
    }
}

void DrawBufferShapeDrawer::add(const MeshEntry* pShapePacket, const ActorLightCtrl* pLightCtrl) {
    bool setLightLoader = false;
    s32 index = findLightSortIndex(pLightCtrl, &setLightLoader);

    for (s32 idx = mNumPackets - 1; idx >= index; idx--) {
        mPackets[idx + 1] = mPackets[idx];
    }

    PacketInfo& rPacket = mPackets[index];
    rPacket.mShapePacket = const_cast< MeshEntry* >(pShapePacket);
    rPacket.mLightCtrl = const_cast< ActorLightCtrl* >(pLightCtrl);

    if (setLightLoader) {
        rPacket.mLightLoader = const_cast< ActorLightCtrl* >(pLightCtrl);
    } else {
        rPacket.mLightLoader = nullptr;
    }

    mNumPackets++;
}

// A removed run head hands the light load to the packet after it.
void DrawBufferShapeDrawer::remove(const MeshEntry* pShapePacket) {
    for (s32 removeIdx = 0; removeIdx < mNumPackets; removeIdx++) {
        if (mPackets[removeIdx].mShapePacket == pShapePacket) {
            const bool loadsLight = mPackets[removeIdx].mLightLoader != nullptr;
            for (s32 idx = removeIdx; idx < mNumPackets - 1; idx++) {
                mPackets[idx] = mPackets[idx + 1];
            }
            if (loadsLight && removeIdx < mNumPackets - 1) {
                mPackets[removeIdx].mLightLoader = mPackets[removeIdx].mLightCtrl;
            }
            mNumPackets--;
            return;
        }
    }
}

void DrawBufferShapeDrawer::resetLightSort(const ActorLightCtrl* pLightCtrl) {
    for (s32 idx = 0; idx < mNumPackets; idx++) {
        if (mPackets[idx].mLightCtrl == pLightCtrl) {
            MeshEntry* shapePacket = mPackets[idx].mShapePacket;
            remove(shapePacket);
            add(shapePacket, pLightCtrl);
            return;
        }
    }
}

s32 DrawBufferShapeDrawer::findLightSortIndex(const ActorLightCtrl* pLightCtrl, bool* pSetLightLoader) const {
    *pSetLightLoader = false;

    if (pLightCtrl == nullptr) {
        return mNumPackets;
    }

    if (pLightCtrl->mBlendFrom != 0) {
        *pSetLightLoader = true;
        return mNumPackets;
    }

    bool foundLight = false;
    for (s32 idx = 0; idx < mNumPackets; idx++) {
        if (mPackets[idx].mLightLoader != nullptr) {
            if (foundLight) {
                return idx;
            }

            if (pLightCtrl->isSameLight(mPackets[idx].mLightLoader)) {
                foundLight = true;
            }
        }
    }

    if (!foundLight) {
        *pSetLightLoader = true;
    }
    return mNumPackets;
}

DrawBuffer::DrawBuffer(Model* pModel)
    : mModelData(pModel->mModelData), mModel(pModel), mMaxActors(0), mNumActors(0), mNumMaterials(0), mMaterialNos(nullptr), mNumShapeDrawers(0),
      mNumOpaShapeDrawers(0), mShapeDrawers(nullptr) {
}

void DrawBuffer::init(int maxActors) {
    mMaxActors = maxActors;
    initTable();
}

void DrawBuffer::reserve(int maxActors) {
    mMaxActors = maxActors;
    for (s32 idx = 0; idx < mNumShapeDrawers; idx++) {
        mShapeDrawers[idx]->reserve(maxActors);
    }
}

void DrawBuffer::add(const LiveActor* pActor) {
    Model* model = MR::getModel(pActor);
    ActorLightCtrl* lightCtrl = MR::getLightCtrl(pActor);

    for (s32 idx = 0; idx < mNumMaterials; idx++) {
        getShapeDrawerByMatNo(idx)->add(model->getMeshEntry(mModelData->getMaterialAt(idx)->getMesh()->mIndex), lightCtrl);
    }
    mNumActors++;
}

void DrawBuffer::remove(const LiveActor* pActor) {
    Model* model = MR::getModel(pActor);

    for (s32 idx = 0; idx < mNumMaterials; idx++) {
        getShapeDrawerByMatNo(idx)->remove(model->getMeshEntry(mModelData->getMaterialAt(idx)->getMesh()->mIndex));
    }
    mNumActors--;
}

void DrawBuffer::resetLightSort(const ActorLightCtrl* pLightCtrl) {

    for (s32 idx = 0; idx < mNumMaterials; idx++) {
        getShapeDrawerByMatNo(idx)->resetLightSort(pLightCtrl);
    }
}

void DrawBuffer::drawOpa() const {
    if (mNumActors > 0) {
        for (s32 idx = 0; idx < mNumOpaShapeDrawers; idx++) {
            getShapeDrawerByIndex(idx)->draw();
        }
    }
}

void DrawBuffer::drawXlu() const {
    if (mNumActors > 0) {
        for (s32 idx = mNumOpaShapeDrawers; idx < mNumShapeDrawers; idx++) {
            getShapeDrawerByIndex(idx)->draw();
        }
    }
}

namespace {
    s32 getSortedMaterialIndex(int materialIndex, Model* pModel) {
        Material* material;
        material = MR::getMaterial(pModel, materialIndex);
        for (s32 idx = 0; idx < materialIndex; idx++) {
            if (MR::getMaterial(pModel, idx)->getID() == material->getID()) {
                return idx;
            }
        }
        return -1;
    }
};  // namespace

void DrawBuffer::initTable() {
    mNumMaterials = MR::getMaterialNum(mModel);
    Model* model = mModel;
    s32 idx, numOpaMaterials;
    numOpaMaterials = 0;
    mNumShapeDrawers = 0;
    for (idx = 0; idx < MR::getMaterialNum(model); idx++) {
        if (::getSortedMaterialIndex(idx, model) < 0) {
            mNumShapeDrawers++;
            if (!MR::getMaterial(model, idx)->isTranslucent()) {
                numOpaMaterials++;
            }
        }
    }
    mNumOpaShapeDrawers = numOpaMaterials;

    mMaterialNos = new s32[mNumMaterials];
    for (s32 idx = 0; idx < mNumMaterials; idx++) {
        mMaterialNos[idx] = -1;
    }

    mShapeDrawers = new DrawBufferShapeDrawer*[mNumShapeDrawers];

    for (s32 idx = 0; idx < mNumShapeDrawers; idx++) {
        mShapeDrawers[idx] = nullptr;
    }

    s32 opaIndex = 0;
    s32 xluIndex = 0;
    for (s32 idx = 0; idx < mNumMaterials; idx++) {
        Material* material = MR::getMaterial(mModel, idx);
        s32 index = ::getSortedMaterialIndex(idx, mModel);

        if (index >= 0) {
            s32 matNo = mMaterialNos[index];
            mMaterialNos[idx] = matNo;
            mShapeDrawers[matNo]->mNumShapes++;
            continue;
        }

        if (!material->isTranslucent()) {
            mMaterialNos[idx] = opaIndex;
            mShapeDrawers[opaIndex] = new DrawBufferShapeDrawer(material, mModel->getMaterialEntry(idx));
            opaIndex++;
        } else {
            mMaterialNos[idx] = xluIndex + mNumOpaShapeDrawers;
            mShapeDrawers[xluIndex + mNumOpaShapeDrawers] = new DrawBufferShapeDrawer(material, mModel->getMaterialEntry(idx));
            xluIndex++;
        }
    }

    // sort Opa
    for (s32 idx = 0; idx < mNumOpaShapeDrawers - 1; idx++) {
        for (s32 idx2 = idx + 1; idx2 < mNumOpaShapeDrawers; idx2++) {
            sortShapeDrawer(idx, idx2);
        }
    }

    // sort Xlu
    for (s32 idx = mNumOpaShapeDrawers; idx < mNumShapeDrawers - 1; idx++) {
        for (s32 idx2 = idx + 1; idx2 < mNumShapeDrawers; idx2++) {
            sortShapeDrawer(idx, idx2);
        }
    }

    for (s32 idx = 0; idx < mNumShapeDrawers; idx++) {
        mShapeDrawers[idx]->init(mMaxActors);
    }
}

void DrawBuffer::sortShapeDrawer(s32 matNo1, s32 matNo2) {
    DrawBufferShapeDrawer* drawerA = getShapeDrawerByIndex(matNo1);
    DrawBufferShapeDrawer* drawerB = getShapeDrawerByIndex(matNo2);
    const char* nameA = MR::getMaterialName(mModelData, drawerA->mMaterial->mIndex);
    const char* nameB = MR::getMaterialName(mModelData, drawerB->mMaterial->mIndex);

    if (nebula_stricmp(nameA, nameB) > 0) {
        drawerA->swap(drawerB);

        for (s32 idx = 0; idx < mNumMaterials; idx++) {
            if (matNo1 == mMaterialNos[idx]) {
                mMaterialNos[idx] = matNo2;
            } else if (matNo2 == mMaterialNos[idx]) {
                mMaterialNos[idx] = matNo1;
            }
        }
    }
}
