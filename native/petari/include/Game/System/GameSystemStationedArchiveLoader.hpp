#pragma once

#include "Game/System/GameThreads.hpp"
#include "Game/System/NerveExecutor.hpp"
#include "Game/System/StationedArchiveLoader.hpp"
#include "Game/System/StationedFileInfo.hpp"

class NebulaArena;
class NebulaArena;
class PlayerHeapHolder;

namespace MR {
    class FunctorBase;
};  // namespace MR

class GameSystemStationedArchiveLoader : public NerveExecutor {
public:
    GameSystemStationedArchiveLoader();

    void update();
    bool isDone() const;
    void waitDone();
    void requestChangeArchivePlayer(bool);
    void exeLoadAudio1stWaveData();
    void exeLoadStationedArchivePlayer();
    void exeLoadStationedArchiveOthers();
    void exeInitializeGameData();
    void exeEnd();
    void exeChangeArchivePlayer();
    void startToLoadStationedArchivePlayer(bool);
    void startToLoadStationedArchiveOthers();
    void createAndAddPlayerArchives(bool);
    void createAndAddOtherArchives();

    PlayerHeapHolder* mHeapHolder;
    GameEvent mDone;
};

class ConditionIsEqualType : public StationedArchiveLoader::Condition {
public:
    ConditionIsEqualType(bool isDataMario) {
        MR::StationedFileInfo::LoadType var = MR::StationedFileInfo::LOAD_TYPE_MOUNT_RESOURCE_LUIGI;

        if (isDataMario) {
            var = MR::StationedFileInfo::LOAD_TYPE_MOUNT_RESOURCE_MARIO;
        }

        mLoadType = var;
    }

    virtual bool isExecute(const MR::StationedFileInfo*) const;

    MR::StationedFileInfo::LoadType mLoadType;
};

class ConditionIfIsNotPlayer : public StationedArchiveLoader::Condition {
public:
    virtual bool isExecute(const MR::StationedFileInfo*) const;
};

class ConditionUsePlayerHeap : public StationedArchiveLoader::Condition {
public:
    ConditionUsePlayerHeap();

    virtual bool isExecute(const MR::StationedFileInfo*) const;
    virtual NebulaArena* getProperHeap(const MR::StationedFileInfo*) const;

    NebulaArena* mNapaHeap;
    NebulaArena* mGDDRHeap;
    bool mIsDataMario;
};

class PlayerHeapHolder {
public:
    PlayerHeapHolder();

    void dispose();
    void setIsDataMario(bool isDataMario);


    ConditionUsePlayerHeap* mCondition;
    NebulaArena* mNapaHeap;
    NebulaArena* mGDDRHeap;
    bool mIsDataMario;
};
