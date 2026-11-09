#include "Game/System/ResourceHolder.hpp"
#include "Game/Render/Clip.hpp"
#include "Game/Sound/SoundAnimator.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Render/Material.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>
#include "Game/Util/MotionSignal.hpp"
#include "Game/Render/TypedDraw.hpp"
#include <nebula_game_api.h>

// tableOf's order: a kind's table, then the file table.
ResTable* ResourceHolder::* const ResourceHolder::cTables[ResourceHolder::cTableCount] = {
    &ResourceHolder::mModelResTable, &ResourceHolder::mMotionResTable, &ResourceHolder::mBtkResTable, &ResourceHolder::mBpkResTable,
    &ResourceHolder::mBtpResTable,   &ResourceHolder::mBlkResTable,    &ResourceHolder::mBrkResTable, &ResourceHolder::mBasResTable,
    &ResourceHolder::mBmtResTable,   &ResourceHolder::mBvaResTable,    &ResourceHolder::mBanmtResTable, &ResourceHolder::mFileInfoTable,
};
static_assert(NEBULA_ASSET_KIND_MATERIAL_TABLE - NEBULA_ASSET_KIND_MODEL + 2 == ResourceHolder::cTableCount);

ResourceHolder::ResourceHolder(AssetFolder& rArchive)
    : mModelResTable(&mDefaultTable), mMotionResTable(&mDefaultTable), mBtkResTable(&mDefaultTable), mBpkResTable(&mDefaultTable),
      mBtpResTable(&mDefaultTable), mBlkResTable(&mDefaultTable), mBrkResTable(&mDefaultTable), mBasResTable(&mDefaultTable),
      mBmtResTable(&mDefaultTable), mBvaResTable(&mDefaultTable), mBanmtResTable(&mDefaultTable), mFileInfoTable(&mDefaultTable), mMaterialBound(false),
      mBckCtrl(nullptr), mBackupMaterialData(nullptr), mBackupMaterialFirst(nullptr), mArchive(&rArchive) {
    mArena = NebulaArena::current();
    initializeArc(*mArchive);

    if (mModelResTable->mCount != 0 && isExistMaterialAnm()) {
        bindMaterialAnimations(reinterpret_cast< ModelData* >(mModelResTable->getRes(getModelName())));
    }

    if (mMotionResTable->mCount != 0) {
        newBckCtrl();
    }

    if (mModelResTable->mCount != 0) {
        backupInitMaterialData(reinterpret_cast< ModelData* >(mModelResTable->getRes(getModelName())));
    }
}

const char* ResourceHolder::getMotionName(u32 index) const {
    if (mMotionResTable->mCount > index) {
        return mMotionResTable->getResName(index);
    }
    return nullptr;
}

bool ResourceHolder::isExistMaterialAnm() const {
    return mBpkResTable->mCount != 0 || mBtpResTable->mCount != 0 || mBtkResTable->mCount != 0 || mBrkResTable->mCount != 0;
}

void ResourceHolder::bindMaterialAnimations(ModelData* pModelData) {
    mMaterialBound = true;
    for (u32 r = 0; r < mBpkResTable->mCount; r++) {
        static_cast< Clip* >(mBpkResTable->getRes(r))->bind(pModelData);
    }
    for (u32 r = 0; r < mBtpResTable->mCount; r++) {
        static_cast< Clip* >(mBtpResTable->getRes(r))->bind(pModelData);
    }
    for (u32 r = 0; r < mBtkResTable->mCount; r++) {
        static_cast< Clip* >(mBtkResTable->getRes(r))->bind(pModelData);
    }
    for (u32 r = 0; r < mBrkResTable->mCount; r++) {
        static_cast< Clip* >(mBrkResTable->getRes(r))->bind(pModelData);
    }
}

void ResourceHolder::newBckCtrl() {
    NebulaArena::CurrentScope scope(mArena);
    // Its table has the archive's name.
    mBckCtrl = new BckCtrl(this, mArchive->getName());
}

Mtx44& ResourceHolder::getInitEffectMtx(int materialIndex, int mtxIndex) const {
    return mBackupMaterialData[mBackupMaterialFirst[materialIndex] + mtxIndex];
}

void ResourceHolder::initializeArc(AssetFolder& rArchive) {
    u32 counts[NEBULA_ASSET_KIND_COUNT] = {};
    count(rArchive, rArchive.getEntry(), counts);

    u32 sizes[cTableCount] = {};
    for (u32 kind = 0; kind < NEBULA_ASSET_KIND_COUNT; kind++) {
        sizes[tableOf(kind)] += counts[kind];
    }

    for (u32 table = 0; table < cTableCount; table++) {
        if (sizes[table] != 0) {
            ResTable* const pTable = new (mArena, 0) ResTable();
            pTable->newFileInfoTable(sizes[table]);
            this->*cTables[table] = pTable;
        }
    }

    mount(rArchive, rArchive.getEntry());
}

u32 ResourceHolder::tableOf(u32 kind) {
    // The model through material-table kinds have a table each, in that order; textures and the rest are files.
    return kind >= NEBULA_ASSET_KIND_MODEL && kind <= NEBULA_ASSET_KIND_MATERIAL_TABLE ? kind - NEBULA_ASSET_KIND_MODEL : cTableCount - 1;
}

void ResourceHolder::count(const AssetFolder& rArchive, s32 folder, u32* pCounts) {
    AssetFolder::Item item;

    for (u32 i = 0; rArchive.getItem(folder, i, &item); i++) {
        if (item.mIsFolder) {
            count(rArchive, item.mEntry, pCounts);
        } else {
            pCounts[item.mKind < NEBULA_ASSET_KIND_COUNT ? item.mKind : NEBULA_ASSET_KIND_OTHER]++;
        }
    }
}

void ResourceHolder::mount(const AssetFolder& rArchive, s32 folder) {
    AssetFolder::Item item;

    for (u32 i = 0; rArchive.getItem(folder, i, &item); i++) {
        if (item.mIsFolder) {
            mount(rArchive, item.mEntry);
        } else {
            ResFileInfo* info = createAndRegisterObject(item);
            info->mData = item.mData;
            info->mSize = item.mSize;
        }
    }
}

ResFileInfo* ResourceHolder::createAndRegisterObject(const AssetFolder::Item& rItem) {
    const char* const pName = rItem.mName;
    void* const pData = rItem.mData;

    switch (rItem.mKind) {
    case NEBULA_ASSET_KIND_MODEL: {
        ModelData* const pModelData = ModelData::load(pData);
        pModelData->setResourceName(pName);
        return mModelResTable->add(pName, pModelData, true);
    }
    case NEBULA_ASSET_KIND_MOTION:
        return mMotionResTable->add(pName, Clip::load(pData), true);
    case NEBULA_ASSET_KIND_TEXTURE_SRT:
        return mBtkResTable->add(pName, Clip::load(pData), true);
    case NEBULA_ASSET_KIND_COLOR_KEY:
        return mBpkResTable->add(pName, Clip::load(pData), true);
    case NEBULA_ASSET_KIND_TEXTURE_PATTERN:
        return mBtpResTable->add(pName, Clip::load(pData), true);
    case NEBULA_ASSET_KIND_CLUSTER_KEY:
        return mBlkResTable->add(pName, Clip::load(pData), true);
    case NEBULA_ASSET_KIND_REGISTER_COLOR:
        return mBrkResTable->add(pName, Clip::load(pData), true);
    case NEBULA_ASSET_KIND_SOUND_ANIMATION:
        return mBasResTable->add(pName, SoundAnimation::load(pData, pName), true);
    case NEBULA_ASSET_KIND_MATERIALS:
        return mBmtResTable->add(pName, ModelData::loadMaterials(pData), true);
    case NEBULA_ASSET_KIND_VISIBILITY:
        return mBvaResTable->add(pName, Clip::load(pData), true);
    case NEBULA_ASSET_KIND_MATERIAL_TABLE:
        return mBanmtResTable->add(pName, pData, true);
    case NEBULA_ASSET_KIND_TEXTURE:
        // Found by its file name, extension and all.
        return mFileInfoTable->add(pName, new ResTIMG(ResTIMG::fromNative(*static_cast< const NebulaTextureInfo* >(pData))), false);
    default:
        return mFileInfoTable->add(pName, pData, false);
    }
}

void ResourceHolder::backupInitMaterialData(const ModelData* pModelData) {
    u32 numMaterials = pModelData->getMaterialCount();
    mBackupMaterialFirst = new u32[numMaterials];
    u32 numMatrices = 0;
    for (u32 matIdx = 0; matIdx < numMaterials; matIdx++) {
        mBackupMaterialFirst[matIdx] = numMatrices;
        numMatrices += pModelData->getMaterialAt(matIdx)->getSlotCount();
    }
    mBackupMaterialData = new Mtx44[numMatrices];

    for (u32 matIdx = 0; matIdx < numMaterials; matIdx++) {
        Material* material = pModelData->getMaterialAt(matIdx);
        for (u32 mtxIdx = 0; mtxIdx < material->getSlotCount(); mtxIdx++) {
            CoordMatrix* texMtx = material->getCoordMatrix(mtxIdx);
            if (texMtx != nullptr) {
                NebulaMtx44Copy(texMtx->getInfo().mEffectMtx, mBackupMaterialData[mBackupMaterialFirst[matIdx] + mtxIdx]);
            } else {
                NebulaMtx44Identity(mBackupMaterialData[mBackupMaterialFirst[matIdx] + mtxIdx]);
            }
        }
    }
}
