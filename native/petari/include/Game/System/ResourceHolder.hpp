#pragma once

#include "Game/Animation/BckCtrl.hpp"
#include "Game/System/ResourceInfo.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/System/NebulaArena.hpp"

class ModelData;

class ResourceHolder {
public:
    ResourceHolder(AssetFolder&);

    const char* getMotionName(u32) const;
    bool isExistMaterialAnm() const;
    // Its material animations find their materials in the model.
    void bindMaterialAnimations(ModelData*);
    void newBckCtrl();

    Mtx44& getInitEffectMtx(int, int) const;
    void initializeArc(AssetFolder&);

    // The tables: one per kind from NEBULA_ASSET_KIND_MODEL to _MATERIAL_TABLE, then the file table.
    static constexpr u32 cTableCount = 12;
    static ResTable* ResourceHolder::* const cTables[cTableCount];
    // The cTables slot of a NEBULA_ASSET_KIND_*.
    static u32 tableOf(u32);
    // The files below a folder, by kind (NEBULA_ASSET_KIND_COUNT counts).
    static void count(const AssetFolder&, s32, u32*);
    void mount(const AssetFolder&, s32);

    ResFileInfo* createAndRegisterObject(const AssetFolder::Item&);
    void backupInitMaterialData(const ModelData*);

    static int findBckIndex(const ResourceHolder* pResHolder, const char* pName) {
        return pResHolder->mMotionResTable->getResIndex(pName);
    }
    static int findBpkIndex(const ResourceHolder* pResHolder, const char* pName) {
        return pResHolder->mBpkResTable->getResIndex(pName);
    }
    static int findBtkIndex(const ResourceHolder* pResHolder, const char* pName) {
        return pResHolder->mBtkResTable->getResIndex(pName);
    }
    static int findBtpIndex(const ResourceHolder* pResHolder, const char* pName) {
        return pResHolder->mBtpResTable->getResIndex(pName);
    }
    static int findBrkIndex(const ResourceHolder* pResHolder, const char* pName) {
        return pResHolder->mBrkResTable->getResIndex(pName);
    }
    static int findBvaIndex(const ResourceHolder* pResHolder, const char* pName) {
        return pResHolder->mBvaResTable->getResIndex(pName);
    }

    const ResTable* getBvaResTable() const {
        return mBvaResTable;
    }

    const char* getModelName() const {
        return mModelResTable->getResName(static_cast<u32>(0));
    }

    ResTable* mModelResTable;
    ResTable* mMotionResTable;
    ResTable* mBtkResTable;
    ResTable* mBpkResTable;
    ResTable* mBtpResTable;
    ResTable* mBlkResTable;
    ResTable* mBrkResTable;
    ResTable* mBasResTable;
    ResTable* mBmtResTable;
    ResTable* mBvaResTable;
    ResTable* mBanmtResTable;
    ResTable* mFileInfoTable;
    ResTable mDefaultTable;
    bool mMaterialBound;
    BckCtrl* mBckCtrl;
    Mtx44* mBackupMaterialData;  // material n's slots from mBackupMaterialFirst[n]
    u32* mBackupMaterialFirst;
    AssetFolder* mArchive;
    NebulaArena* mArena;
};
