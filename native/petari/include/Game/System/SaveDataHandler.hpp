#pragma once

#include "Game/System/NerveExecutor.hpp"
#include "Game/System/SaveRequest.hpp"

class UserFile;

class SaveDataHandler : public NerveExecutor {
public:
    SaveDataHandler(const UserFile*);

    void update();
    void requestCheckEnableToCreate();
    void requestLoadSaveData(int copy = 0);
    bool requestVerifyAfterLoadGameDataFile();
    void initializeUserFileMemory(int, const UserFile*);
    void copyUserFileMemory(int, int);
    void restoreGameDataFile(const char*, void*, u32);
    void storeUserFile(const UserFile*);
    void requestSaveSaveData();
    void requestRemoveSaveData();
    static u32 getEnoughtTempBufferSize();
    bool isDone() const;
    SaveRequest::Result getLastResult() const;
    void exeWait();
    void exeProcessing();
    void exeSaveProcessingGameData();
    void exeRemoveProcessingGameData();
    static void resetSaveData(u8*);
    static void initializeAllFileInSaveData(u8*, const UserFile*);
    static bool isCorrectFileHeader(const u8*);
    static void copySaveDataEachFile(u8*, const u8*);
    void convertFileHeader(u8*);
    void convertUserFiles(u8*);
    static const u32 cSaveFileVersion = 3;
    void createCommunicationBuffer();
    bool tryRemoveFile(const char*, bool*);
    bool trySave(bool*, bool);

    SaveRequest* mSaveRequest;
    u32 _C;
    u8* _14;
    u8* _18;
};
