#pragma once

#include "JSystem/JAudio2/JASWaveArcLoader.hpp"
#include "Game/System/GameThreads.hpp"
#include "JSystem/JAudio2/JASWaveInfo.hpp"

struct JASBasicWaveBank : public JASWaveBank {
    struct TWaveInfo;

    struct TWaveGroup : JASWaveArc {
        TWaveGroup(JASBasicWaveBank*);
        virtual ~TWaveGroup();
        void setWaveCount(u32, NebulaArena*);
        virtual void onLoadDone();
        virtual void onEraseDone();
        u32 getWaveID(int index) const {
            return mCtrlWaveArray[index].mWaveId;
        }
        void setWaveInfo(int param_0, u32 param_1, const JASWaveInfo& waveInfo);

        JASBasicWaveBank* mBank;
        TWaveInfo* mCtrlWaveArray;
        u32 mWaveCount;

        u32 getWaveCount() const {
            return mWaveCount;
        }
    };

    struct TWaveHandle : public JASWaveHandle {
        TWaveHandle() {
            mArc = nullptr;
        }

        virtual const JASWaveInfo* getWaveInfo() const {
            return &mWaveInfo;
        }
        virtual const u8* getWavePtr() const;
        virtual const u8* getWaveLimit() const;

        JASWaveInfo mWaveInfo;
        JASWaveArc* mArc;
    };

    struct TWaveInfo {
        TWaveInfo() {
            mWaveId = 0;
            mNext = nullptr;
            mPrev = nullptr;
        }

        u32 getWaveID() const {
            return mWaveId;
        }

        TWaveHandle mWaveHandle;
        u32 mWaveId;
        TWaveInfo* mNext;
        TWaveInfo* mPrev;
    };

    JASBasicWaveBank();
    ~JASBasicWaveBank();
    TWaveGroup* getWaveGroup(u32);
    void setGroupCount(u32, NebulaArena*);
    void setWaveTableSize(u32, NebulaArena*);
    void incWaveTable(JASBasicWaveBank::TWaveGroup const*);
    void decWaveTable(JASBasicWaveBank::TWaveGroup const*);
    JASWaveHandle* getWaveHandle(u32) const;

    void setWaveInfo(JASBasicWaveBank::TWaveGroup* waveGroup, int index, u16 waveId, JASWaveInfo const& waveInfo) {
        waveGroup->setWaveInfo(index, waveId, waveInfo);
    }
    JASWaveArc* getWaveArc(u32 param_0) {
        return getWaveGroup(param_0);
    }
    u32 getArcCount() const {
        return mGroupCount;
    }

    GameMutex mMutex;
    TWaveInfo** mWaveTable;
    u32 mHandleCount;
    TWaveGroup** mWaveGroupArray;
    u32 mGroupCount;

    static u32 mNoLoad;
};
