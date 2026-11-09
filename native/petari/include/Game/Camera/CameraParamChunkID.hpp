#pragma once

namespace MR {
    class FormattedName;
}

class CameraParamChunkID {
public:
    CameraParamChunkID();
    CameraParamChunkID(const CameraParamChunkID&);

    bool operator>(const CameraParamChunkID&) const;
    bool operator==(const CameraParamChunkID&) const;

    virtual char* getBuffer(u32);

    bool equals(s32, const char*) const;
    void createCubeID(s32, u16);
    void createGroupID(s32, const char*, u32, u32);
    void createOtherID(s32, const char*);
    void createEventID(s32, const char*);
    void createStartID(s32, u16);
    void setName(const MR::FormattedName& rName);
    void copyName(const char* pName);

    s8 mZoneID;
    char* mName;
};

class CameraParamChunkID_Tmp : public CameraParamChunkID {
public:
    inline CameraParamChunkID_Tmp() : CameraParamChunkID() {
    }

    ~CameraParamChunkID_Tmp() {
        delete[] mLong;
    }

    CameraParamChunkID_Tmp(const CameraParamChunkID_Tmp&) = delete;
    CameraParamChunkID_Tmp& operator=(const CameraParamChunkID_Tmp&) = delete;

    virtual char* getBuffer(u32 size) override {
        if (size <= sizeof(mBuffer)) {
            return &mBuffer[0];
        }

        delete[] mLong;
        mLong = new char[size];
        return mLong;
    }

    char mBuffer[0x100];  // the common case in itself; a longer id is in mLong, whole
    char* mLong = nullptr;
};
