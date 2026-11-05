#pragma once

#include "Game/Animation/BckCtrl.hpp"
#include "Game/System/ResourceInfo.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/System/NebulaArena.hpp"

class J3DModelData;

typedef const char* ArchiveName;

class ResourceHolder {
public:
    ResourceHolder(AssetFolder&);

    const char* getMotionName(u32) const;
    bool isExistMaterialAnm() const;
    // Its material animations find their materials in the model.
    void bindMaterialAnimations(J3DModelData*);
    void newBckCtrl();

    Mtx44& getInitEffectMtx(int, int) const;
    void initializeArc(AssetFolder&);
    u32 initEachResTable(ResTable**, AssetFolder*, const ArchiveName*);

    static s32 count(AssetFolder*, const char*, s32);
    void mount(AssetFolder*, s32);

    ResFileInfo* createAndRegisterObject(const char*, void*);
    void backupInitMaterialData(const J3DModelData*);

    // bool checkCorrectMotionsAndModel(J3DModelData*, const char*) const;

    bool isCreatedAtSameHeap(ResourceHolder* pOther) const {
        return mHeap == pOther->mHeap;
    }

    // void makeStringAnmInfoAll(char*, u32) const;
    // bool verifyBckData(J3DModelData*, const char*) const;
    // bool verifyBvaData(J3DModelData*, const char*) const;
    // bool verifyBtpData(J3DModelData*, const char*) const;
    // bool verifyModelData(J3DModelData*, const char*) const;
    // bool verifyFileSize(u32, const char*) const;

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
    Mtx44* mBackupMaterialData;
    AssetFolder* mArchive;
    NebulaArena* mHeap;
    u32 mTotalResourceSize;
};
