#pragma once

#include "JSystem/JSupport/JSupport.hpp"

class NebulaArena;
class JASWaveBank;
struct JASBasicWaveBank;
struct JASSimpleWaveBank;

// Wave systems (WSYS) in host order, as the importer cooks the audio archive (native_data.rs wave_system).
class JASWSParser {
public:
    template < class T >
    class TOffset {
    public:
        T* ptr(void const* param_0) const {
            return JSUConvertOffsetToPtr< T >(param_0, mOffset);
        }

    private:
        u32 mOffset;
    };

    struct TCtrlWave {
        union {
            struct {
                u16 mWaveGroupId;
                u16 mWaveId;
            };
            u32 mData;
        };
    };

    struct TWave {
        u8 _00;
        u8 mFormat;
        u8 mBaseKey;
        f32 mSampleRate;
        u32 mAWStartOffs;
        u32 mAWLength;
        u32 mLoopFlags;
        u32 mSampleLoopStart;
        u32 mSampleLoopEnd;
        u32 mSampleCount;
        s16 mpLastSample;
        s16 mpPenultSample;
    };

    struct TWaveArchive {
        char mFileName[0x70];
        s32 mWaveCount;
        TOffset< TWave > mWaveOffsets[0];
    };

    struct TWaveArchiveBank {
        char mMagic[4];
        s32 mWaveGroupCount;
        TOffset< TWaveArchive > mArchiveOffsets[0];
    };

    struct TCtrl {
        char mMagic[4];
        u32 mWaveCount;
        TOffset< TCtrlWave > mCtrlWaveOffsets[0];
    };

    struct TCtrlScene {
        char mMagic[4];
        u8 _04[8];
        TOffset< TCtrl > mCtrlOffset;
    };

    struct TCtrlGroup {
        char mMagic[4];
        u8 _04[4];
        u32 mGroupCount;
        TOffset< TCtrlScene > mCtrlSceneOffsets[0];
    };

    /** @fabricated */
    struct THeader {
        char mMagic[4];
        s32 mSize;
        s32 mId;
        u32 mWaveTableSize;
        TOffset< TWaveArchiveBank > mArchiveBankOffset;
        TOffset< TCtrlGroup > mCtrlGroupOffset;
    };

    static u32 getGroupCount(void const*);
    static JASWaveBank* createWaveBank(void const*, NebulaArena*);
    static JASBasicWaveBank* createBasicWaveBank(void const*, NebulaArena*);
    static JASSimpleWaveBank* createSimpleWaveBank(void const*, NebulaArena*);

};
