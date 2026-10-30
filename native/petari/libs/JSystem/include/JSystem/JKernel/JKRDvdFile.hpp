#pragma once

#include "Game/System/NebulaDisc.hpp"
#include "JSystem/JKernel/JKRDisposer.hpp"

class JKRADCommand;
class JKRAramBlock;
class JSUFileInputStream;

class JKRFile : public JKRDisposer {
public:
    JKRFile() : JKRDisposer() {
        mIsAvailable = false;
    }

    virtual ~JKRFile() {
    }

    /* vt[03] */ virtual bool open(const char*) = 0;
    /* vt[04] */ virtual void close() = 0;
    /* vt[05] */ virtual s32 readData(void*, s32, s32) = 0;
    /* vt[06] */ virtual s32 writeData(const void*, s32, s32) = 0;
    /* vt[07] */ virtual s32 getFileSize() const = 0;

    bool isAvailable() const {
        return mIsAvailable;
    }

    /* 0x18 */ bool mIsAvailable;
};

class JKRDvdFile : public JKRFile {
public:
    JKRDvdFile();
    JKRDvdFile(s32);

    virtual ~JKRDvdFile();
    virtual bool open(const char*);
    virtual void close();
    virtual s32 readData(void*, s32, s32);
    virtual s32 writeData(const void*, s32, s32);
    virtual s32 getFileSize() const {
        return mFile.mLength;
    }
    virtual bool open(s32);

    void initiate();

    const NebulaDisc::File* getFile() const {
        return &mFile;
    }

    /* 0x1C */ OSMutex mMutex1;
    /* 0x34 */ OSMutex mMutex2;
    /* 0x4C */ JKRAramBlock* mBlock;
    /* 0x50 */ OSThread* field_0x50;
    /* 0x54 */ JSUFileInputStream* mFileStream;
    /* 0x58 */ u32 field_0x58;
    NebulaDisc::File mFile;
    JSULink< JKRDvdFile > mDvdLink;

    static JSUList< JKRDvdFile > sDvdList;
};

