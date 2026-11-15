#pragma once

#include "Game/System/SaveFileId.hpp"
#include "Game/System/SaveRequest.hpp"
#include <nebula_types.h>

class UserFile;

// The save: each user's files in memory, read from and written to one file through the host (save_request).
class SaveDataHandler {
public:
    SaveDataHandler(const UserFile*);

    // The save's file at `index`, the newest first, then its copies, then an old save's. False past the last.
    bool requestLoadSaveData(int index);
    // What the read found: taken into memory, or not usable.
    bool acceptLoaded();
    void initializeUserFileMemory(int, const UserFile*);
    // Every user file in memory as `pUserFile` (a blank one: as the start holds them before the save is read).
    void initializeAllUserFileMemory(const UserFile* pUserFile);
    void copyUserFileMemory(int, int);
    const u8* getFile(SaveFileId, u32* pSize) const;
    void storeUserFile(const UserFile*);
    void requestSaveSaveData();
    // pSrc's game data and config into pDst, as a save and a load of them would (the backup the sequence keeps).
    void copyUserFile(UserFile* pDst, const UserFile* pSrc);
    bool isDone();
    SaveRequest::Result getLastResult() const;
    // FORGET (services/save.md 5): `pId`'s values out of each game-data file in memory (written by the next save). True
    // when a file held some.
    bool forgetLumaMod(const char* pId);

    // save.game's export (services/save.md 8): user file `user` (1 to 6) in the export form, cExportMagic, u16 version
    // 1, u16 parts 3, then Mario's game data, Luigi's and the config, each u32 length and its bytes; the frame's
    // numbers little-endian, each part as Nebula.sav holds it. The loaded file's game data for its character and its
    // config (pLoaded: the loaded user file, else null) as the game's next save would write them, every other part as
    // stored. Export_Ok: *ppBytes is new[] (the caller deletes it) of *pLength bytes.
    enum ExportResult {
        Export_Ok,
        Export_Empty,  // the user file is not created
        Export_Memory,
    };
    static const u32 cExportHead = 8;
    static const char cExportMagic[5];
    ExportResult makeExport(int user, const UserFile* pLoaded, u8** ppBytes, u32* pLength);
    // save.game's offer (services/save.md 8): the export form's parts (Mario's game data, Luigi's, the config) in
    // ppParts and pSizes, each pointing into pBytes (never null). False: not the form (the magic, version 1, 3 parts,
    // the lengths framing exactly `length` bytes).
    static bool splitExport(const u8* pBytes, u32 length, const u8* ppParts[], u32 pSizes[]);
    // User file `user` (1 to 6) in memory replaced by the parts, kind by kind, as copyUserFileMemory copies a file;
    // the next save writes it.
    void replaceUserFile(int user, const u8* const ppParts[], const u32 pSizes[]);

    // The settings' per-file import (nebula_save_import.h). Each call on the game thread, with no request with the host.
    // `pBytes` (a Nebula.sav, or a GameData.bin of any version) read as the start reads a save, an old order's files
    // made again in host order, and written in Nebula.sav's form into *ppSave (new[]; the caller deletes it). False:
    // neither form, damaged, or no memory.
    bool convertSave(u8* pBytes, u32 length, u8** ppSave, u32* pLength);
    // The save in memory in Nebula.sav's form, as the next save writes it (new[]; the caller deletes it). False: no
    // memory.
    bool makeSave(u8** ppSave, u32* pLength);
    // A Nebula.sav these make: each file (cFileNum, by user then kind) pointing into it, empty when it has no row for
    // one. False: not one.
    static bool splitSave(const u8* pSave, u32 length, const u8* ppParts[], u32 pSizes[]);
    static const u32 cFileNum = 18;  // 6 users, 3 kinds

private:
    // Indexed by user, then kind.
    struct File {
        u8* mData;
        u32 mSize;
        u32 mRoom;
    };

    // A read file into `pFiles` (cFileNum of them): Nebula.sav's form, or an old save's; `pName` names it in the log.
    bool readNative(const u8* pBytes, u32 length, const char* pName, File* pFiles);
    bool readOld(u8* pBytes, u32 length, const char* pName, File* pFiles);
    // Null for a user past the last.
    File* findFile(SaveFileId) const;
    void setFile(SaveFileId, const u8*, u32);
    // setFile into `pFiles`: the save in memory (mFiles, its mods' values counted), or another set.
    void putFile(File* pFiles, SaveFileId, const u8*, u32);
    // Nebula.sav of `pFiles`: its size, then the file made into `pOut` (that size).
    static u32 saveSize(const File* pFiles);
    static void makeSaveInto(const File* pFiles, u8* pOut, u32 total);
    // mMake made big enough for pUserFile's game data: the fixed part's room and its mods' values (services/save.md).
    void fitMake(const UserFile* pUserFile);
    // A request still with the host finished first: mBuffer is the host's until then (a sequence that takes over from
    // the quiet save starts its own request at once).
    void waitRequest() const;

    SaveRequest* mSaveRequest;
    int mReadIndex;
    u32 mReadLength;
    u8* mBuffer;
    u32 mBufferRoom;
    File* mFiles;
    u8* mMake;
    u32 mMakeRoom;
    // The room a file takes without mods' values, and room beside it.
    u32 mBaseRoom;
};
