#pragma once

#include "JSystem/JAudio2/JASBasicInst.hpp"
#include "JSystem/JAudio2/JASOscillator.hpp"
#include "JSystem/JSupport/JSupport.hpp"

class JASBank;
class JASBasicBank;
class NebulaArena;

// Banks (IBNK version 1) in host order, as the importer cooks the audio archive (native_data.rs bank).
namespace JASBNKParser {
    struct TFileHeader {
        s32 id;
        u32 mSize;
        u8 _08[4];
        u32 mVersion;
    };

    namespace Ver1 {
        struct TOsc {
            u32 id;
            u8 mTarget;
            f32 _08;
            u32 mTableOffset;
            u32 _10;
            f32 mScale;
            f32 _18;
        };

        struct TPercData {
            f32 mVolume;
            f32 mPitch;
            u8 mPan;
            u16 mRelease;
            u32 field_0xc;
        };

        struct TChunk {
            u32 mID;
            u32 mSize;
        };

        struct TEnvtChunk : TChunk {
            u8 mData[0];
        };

        struct TOscChunk : TChunk {
            u32 mCount;
            TOsc mOsc[0];
        };

        struct TListChunk : TChunk {
            u32 count;
            u32 mOffsets[0];
        };

        struct TRandData {
            u8 mType;
            f32 mBase;
            f32 mRange;
        };

        struct TSenseData {
            u8 mType;
            u8 mSenseType;
            u8 mMaxPoint;
            f32 mStartLvl;
            f32 mEndLvl;
        };

        struct TVeloData {
            u8 _0;
            u32 _4;
            f32 mVolume;
            f32 mPitch;
        };

        struct TInstData {
            f32 mVolume;
            f32 mPitch;
        };

        TChunk* findChunk(void const*, u32);
        JASInstEffect* createEffector(void const*, NebulaArena*);
        JASBasicBank* createBasicBank(void const*, NebulaArena*);
    };  // namespace Ver1

    namespace Ver0 {
        template < typename T >
        struct TOffset {
            u32 offset;
            T* ptr(void const* stream) const {
                return JSUConvertOffsetToPtr< T >(stream, offset);
            }
        };

        struct TOsc {
            u8 mTarget;
            f32 field_0x4;
            TOffset< JASOscillator::Point > mPointOffset;
            TOffset< JASOscillator::Point > field_0xc;
            f32 mScale;
            f32 field_0x14;
        };

        struct TVmap {
            u8 _0;
            u32 _4;
            f32 mVolume;
            f32 mPitch;
        };

        struct TRandEffect {
            u8 mType;
            f32 mBaseValue;
            f32 mRange;
        };

        struct TSenseEffect {
            u8 mType;
            u8 mSenseType;
            u8 mMaxPoint;
            f32 mStartLvl;
            f32 mEndLvl;
        };

        struct TKeymap {
            u8 mHighKey;
            u32 mVeloRegionCount;
            TOffset< TVmap > mVmapOffset[0];
        };

        struct TInst {
            u8 field_0x0[8];
            f32 mVolume;
            f32 mPitch;
            TOffset< TOsc > mOscOffset[2];
            TOffset< TRandEffect > mRandOffset[2];
            TOffset< TSenseEffect > mSenseOffset[2];
            u32 mKeyRegionCount;
            TOffset< TKeymap > mKeymapOffset[0];
        };

        struct TPmap {
            f32 mVolume;
            f32 mPitch;
            TOffset< TRandEffect > mRandOffset[2];
            u32 mVeloRegionCount;
            TOffset< TVmap > mVmapOffset[0];
        };

        struct TPerc {
            u32 mMagic;
            u8 _4[0x84];
            TOffset< TPmap > mPmapOffset[0x80];
            s8 mPan[0x80];
            u16 mRelease[0x80];
        };

        struct TOffsetData {
            u8 field_0x20[4];
            TOffset< TInst > mInstOffset[0x80];
            u8 field_0x204[0x190];
            TOffset< TPerc > mPercOffset[12];
        };

        struct THeader {
            u8 field_0x0[0x20];
            TOffsetData mOffsets;
        };

        JASBasicBank* createBasicBank(void const*, NebulaArena*);
        inline JASOscillator::Data* findOscPtr(JASBasicBank*, THeader const*, TOsc const*);
        JASOscillator::Point const* getOscTableEndPtr(JASOscillator::Point const*);
    };  // namespace Ver0

    JASBank* createBank(void const*, NebulaArena*);
    JASBasicBank* createBasicBank(void const*, NebulaArena*);

    inline u32 getBankNumber(const void* param_0) {
        u32* ptr = (u32*)param_0;
        return ptr[2];
    }

};  // namespace JASBNKParser
