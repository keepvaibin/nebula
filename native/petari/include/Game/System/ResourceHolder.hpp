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


    const ResTable* getBvaResTable() const {
        return mBvaResTable;
    }

    const char* getModelName() const {
        return mModelResTable->getResName(static_cast<u32>(0));
    }

    // The archive's file of that name (a table: a .bcsv), or, when it has none, its lender's. Null when neither has it.
    void* findFile(const char* pName) const;
    // The archive's sound animation (.bas) of that name, or its lender's. Null when neither has it.
    void* findSoundAnimation(const char* pName) const;
    // Each of its animations the lender has one of the same name and kind of plays as the lender's does (once, looping,
    // back and forth): the class's code waits for the ends of those it plays once.
    void takePlayModesFromLender();

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
    // A mod's archive standing for a game class's (luma.json object_classes "like"): the game class's archive, whose
    // tables and sound animations its own lacks are read from there. Its models and animations are never lent.
    const ResourceHolder* mLender;
};
