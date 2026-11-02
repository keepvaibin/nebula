#include "JSystem/JAudio2/JASWSParser.hpp"
#include "JSystem/JAudio2/JASBasicWaveBank.hpp"
#include "JSystem/JAudio2/JASSimpleWaveBank.hpp"
#include "Game/System/NebulaArena.hpp"

u32 JASWSParser::getGroupCount(void const* stream) {
    THeader* header = (THeader*)stream;
    TCtrlGroup* group = header->mCtrlGroupOffset.ptr(header);
    return group->mGroupCount;
}

JASWaveBank* JASWSParser::createWaveBank(void const* stream, NebulaArena* heap) {
    if (getGroupCount(stream) == 1) {
        return createSimpleWaveBank(stream, heap);
    } else {
        return createBasicWaveBank(stream, heap);
    }
}


JASBasicWaveBank* JASWSParser::createBasicWaveBank(void const* stream, NebulaArena* heap) {
    if (heap == nullptr) {
        heap = JASDram;
    }
    THeader* header = (THeader*)stream;
    JASBasicWaveBank* wave_bank = new (heap, 0) JASBasicWaveBank();
    if (wave_bank == nullptr) {
        return nullptr;
    }

    TCtrlGroup* ctrl_group = header->mCtrlGroupOffset.ptr(header);
    wave_bank->setGroupCount(ctrl_group->mGroupCount, heap);

    u32 waveTableSize = 0;
    for (u32 i = 0; i < ctrl_group->mGroupCount; i++) {
        TCtrlScene* ctrlScene = ctrl_group->mCtrlSceneOffsets[i].ptr(header);
        TCtrl* ctrl = ctrlScene->mCtrlOffset.ptr(header);
        JASBasicWaveBank::TWaveGroup* wave_group = wave_bank->getWaveGroup(i);
        TWaveArchiveBank* archiveBank = header->mArchiveBankOffset.ptr(header);
        TWaveArchive* archive = archiveBank->mArchiveOffsets[i].ptr(header);
        wave_group->setWaveCount(ctrl->mWaveCount, heap);
        for (int j = 0; j < ctrl->mWaveCount; j++) {
            TWave* wave = archive->mWaveOffsets[j].ptr(header);
            JASWaveInfo wave_info;
            wave_info.mFormat = wave->mFormat;
            wave_info.mBaseKey = wave->mBaseKey;
            wave_info.mSampleRate = wave->mSampleRate;
            wave_info.mAWStartOffs = wave->mAWStartOffs;
            wave_info.mAWLength = wave->mAWLength;
            wave_info.mLoopFlags = (u8)(wave->mLoopFlags == 0 ? 0 : 0xff);
            wave_info.mSampleLoopStart = wave->mSampleLoopStart;
            wave_info.mSampleLoopEnd = wave->mSampleLoopEnd;
            wave_info.mSampleCount = wave->mSampleCount;
            TCtrlWave* ctrl_wave = ctrl->mCtrlWaveOffsets[j].ptr(header);
            u16 waveId = JSULoHalf(ctrl_wave->mData);
            wave_bank->setWaveInfo(wave_group, j, waveId, wave_info);
            if (waveTableSize < waveId) {
                waveTableSize = waveId;
            }
        }
        wave_group->setFileName(archive->mFileName);
    }
    wave_bank->setWaveTableSize(waveTableSize + 1, heap);
    return wave_bank;
}

JASSimpleWaveBank* JASWSParser::createSimpleWaveBank(void const* stream, NebulaArena* heap) {
    if (heap == nullptr) {
        heap = JASDram;
    }
    THeader* header = (THeader*)stream;
    TCtrlGroup* ctrl_group = header->mCtrlGroupOffset.ptr(header);
    if (ctrl_group->mGroupCount != 1) {
        return nullptr;
    }

    JASSimpleWaveBank* wave_bank = new (heap, 0) JASSimpleWaveBank();
    if (wave_bank == nullptr) {
        return nullptr;
    }

    u32 max = 0;
    TCtrlScene* ctrlScene = ctrl_group->mCtrlSceneOffsets[0].ptr(header);
    TCtrl* ctrl = ctrlScene->mCtrlOffset.ptr(header);
    TWaveArchiveBank* archiveBank = header->mArchiveBankOffset.ptr(header);
    TWaveArchive* archive = archiveBank->mArchiveOffsets[0].ptr(header);
    for (int i = 0; i < ctrl->mWaveCount; i++) {
        TCtrlWave* ctrlWave = ctrl->mCtrlWaveOffsets[i].ptr(header);
        u32 tmp = JSULoHalf(ctrlWave->mData);
        if (max < tmp) {
            max = tmp;
        }
    }
    wave_bank->setWaveTableSize(max + 1, heap);

    for (int i = 0; i < ctrl->mWaveCount; i++) {
        TWave* wave = archive->mWaveOffsets[i].ptr(header);
        JASWaveInfo wave_info;
        wave_info.mFormat = wave->mFormat;
        wave_info.mBaseKey = wave->mBaseKey;
        wave_info.mSampleRate = wave->mSampleRate;
        wave_info.mAWStartOffs = wave->mAWStartOffs;
        wave_info.mAWLength = wave->mAWLength;
        wave_info.mLoopFlags = (u8)(wave->mLoopFlags == 0 ? 0 : 0xff);
        wave_info.mSampleLoopStart = wave->mSampleLoopStart;
        wave_info.mSampleLoopEnd = wave->mSampleLoopEnd;
        wave_info.mSampleCount = wave->mSampleCount;
        TCtrlWave* ctrl_wave = ctrl->mCtrlWaveOffsets[i].ptr(header);
        u32 tmp = JSULoHalf(ctrl_wave->mData);
        wave_bank->setWaveInfo(tmp, wave_info);
    }
    wave_bank->setFileName(archive->mFileName);

    return wave_bank;
}
