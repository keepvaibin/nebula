#include "JSystem/JAudio2/JASAramStream.hpp"
#include "Game/System/NebulaKernel.hpp"
#include "JSystem/JAudio2/JASAiCtrl.hpp"
#include "JSystem/JAudio2/JASChannel.hpp"
#include "JSystem/JAudio2/JASCriticalSection.hpp"
#include "JSystem/JAudio2/JASDspInterface.hpp"
#include "JSystem/JAudio2/JASDriverIF.hpp"
#include "JSystem/JAudio2/JASDvdThread.hpp"
#include "JSystem/JKernel/JKRAram.hpp"
#include "JSystem/JKernel/JKRDvdRipper.hpp"
#include "JSystem/JKernel/JKRSolidHeap.hpp"
#include "JSystem/JSupport/JSupport.hpp"

JASTaskThread* JASAramStream::sLoadThread;

u8* JASAramStream::sReadBuffer;

u32 JASAramStream::sBlockSize;

u32 JASAramStream::sChannelMax;

bool UNK_BOOL_B;

void JASAramStream::initSystem(u32 block_size, u32 channel_max) {
    // The console also registered a check of the drive here, which paused streams on a disc error; a PC's game
    // files have none.
    if (sLoadThread == nullptr) {
        sLoadThread = JASDvd::getThreadPointer();
    }

    sReadBuffer = new (JASDram, 0x20) u8[(block_size + 0x20) * channel_max];
    sBlockSize = block_size;
    sChannelMax = channel_max;
    UNK_BOOL_B = false;
}

JASAramStream::JASAramStream() : _0B8(), _0C4(), _0C8(), _114() {
    mUpdateChannel = nullptr;
    _0AC = false;
    _0AD = false;
    _0AE = 0;
    _0B0 = 0;
    _0B4 = 0;
    _0BC = 0;
    _0C0 = false;
    _108 = 0;
    _10C = 0;
    mBlock = 0;
    _118 = 0;
    _12C = 0;
    _148 = 0;
    _14C = 0;
    mCallback = nullptr;
    mCallbackData = nullptr;
    _158 = 0;
    mChannelNum = 0;
    mBufCount = 0;
    _160 = 0;
    _164 = 0;
    mLoop = false;
    mLoopStart = 0;
    mLoopEnd = 0;
    mVolume = 1.0f;
    mPitch = 1.0f;
    for (int i = 0; i < 6; i++) {
        mChannels[i] = nullptr;
        _130[i] = 0;
        _13C[i] = 0;
        mChannelVolume[i] = 1.0f;
        mChannelPan[i] = 0.5f;
        mChannelFxMix[i] = 0.0f;
        mChannelDolby[i] = 0.0f;
    }

    for (int i = 0; i < 6; i++) {
        _1DC[i] = 0;
    }
}

void JASAramStream::init(u32 param_0, u32 param_1, StreamCallback i_callback, void* i_callbackData) {
    _148 = param_0;
    _14C = param_1;
    _0C8 = 0.0f;
    _0AE = 0;
    _0AC = false;
    _0AD = false;
    _114 = 0;
    mChannelNum = 0;
    for (int i = 0; i < 6; i++) {
        mChannelVolume[i] = 1.0f;
        mChannelPan[i] = 0.5f;
        mChannelFxMix[i] = 0.0f;
        mChannelDolby[i] = 0.0f;
    }

    mVolume = 1.0f;
    mPitch = 1.0f;
    _1DC[0] = 0xffff;
    mCallback = i_callback;
    mCallbackData = i_callbackData;
    _000.init(_040, 0x10);
    _020.init(_080, 4);
}

bool JASAramStream::prepare(s32 param_0, int param_1) {
    if (!NebulaDisc::open(param_0, &mDvdFile)) {
        return false;
    }

    if (!JASDriver::registerSubFrameCallback(channelProcCallback, this)) {
        return false;
    }

    TaskData data;
    data.stream = this;
    data._4 = _14C;
    data._8 = param_1;
    if (!sLoadThread->sendCmdMsg(headerLoadTask, &data, sizeof(data))) {
        JASDriver::rejectCallback(channelProcCallback, this);
        return false;
    }

    return true;
}

bool JASAramStream::start() {
    if (!_000.send((void*)0, false)) {
        return false;
    }

    return true;
}

bool JASAramStream::stop(u16 param_0) {
    if (!_000.send((void*)(uintptr_t)(param_0 << 0x10 | 1), false)) {
        return false;
    }

    return true;
}

bool JASAramStream::pause(bool param_0) {
    void* msg = param_0 ? (void*)2 : (void*)3;
    if (!_000.send(msg, false)) {
        return false;
    }

    return true;
}

bool JASAramStream::cancel() {
    _114 = 1;
    if (!sLoadThread->sendCmdMsg(finishTask, this)) {
        return false;
    }

    return true;
}

u32 JASAramStream::getBlockSamples() const {
    return _158 == 0 ? (sBlockSize << 4) / 9 : sBlockSize >> 1;
}

void JASAramStream::headerLoadTask(void* i_data) {
    TaskData* data = (TaskData*)i_data;
    data->stream->headerLoad(data->_4, data->_8);
}

void JASAramStream::firstLoadTask(void* i_data) {
    TaskData* data = (TaskData*)i_data;
    JASAramStream* _this = data->stream;
    if (!_this->load()) {
        return;
    }

    if (data->_8 > 0) {
        data->_8--;
        if (data->_8 == 0) {
            if (!sLoadThread->sendCmdMsg(prepareFinishTask, _this)) {
                UNK_BOOL_B = true;
            }
        }
    }

    if (data->_4 != 0) {
        data->_4--;
        if (!sLoadThread->sendCmdMsg(firstLoadTask, data, sizeof(*data))) {
            UNK_BOOL_B = true;
        }

        JASCriticalSection cs;
        _this->_118++;
    }
}

void JASAramStream::loadToAramTask(void* stream) {
    ((JASAramStream*)stream)->load();
}

void JASAramStream::finishTask(void* i_this) {
    JASAramStream* _this = (JASAramStream*)i_this;
    if (!JASDriver::rejectCallback(channelProcCallback, _this)) {
    }

    if (_this->mCallback != nullptr) {
        _this->mCallback(CB_START, _this, _this->mCallbackData);
        _this->mCallback = nullptr;
    }
}

void JASAramStream::prepareFinishTask(void* i_this) {
    JASAramStream* _this = (JASAramStream*)i_this;
    _this->_020.send((void*)4, true);
    if (_this->mCallback != nullptr) {
        _this->mCallback(CB_STOP, _this, _this->mCallbackData);
    }
}

bool JASAramStream::headerLoad(u32 param_0, int param_1) {
    if (UNK_BOOL_B) {
        return false;
    }

    if (_114 != 0) {
        return false;
    }

    if (NebulaDisc::read(&mDvdFile, sReadBuffer, sizeof(Header), 0) < 0) {
        UNK_BOOL_B = true;
        return false;
    }

    Header* header = (Header*)sReadBuffer;
    _158 = header->format;
    mChannelNum = header->channels;
    _164 = header->_10;
    mLoop = header->loop != 0;
    mLoopStart = header->loop_start;
    mLoopEnd = header->loop_end;
    mVolume = header->_28 / 127.0f;
    _118 = 0;
    mBlock = 0;
    _10C = 0;
    _160 = (param_0 / sBlockSize) / header->channels;
    mBufCount = _160;
    mBufCount--;
    _108 = mBufCount;
    u32 local_2c = (mLoopEnd - 1) / getBlockSamples();
    if (local_2c <= mBufCount && mLoop) {
    }

    if (param_1 < 0 || param_1 > _108) {
        param_1 = _108;
    }

    if (_114 != 0) {
        return false;
    }

    TaskData data;
    data.stream = this;
    data._4 = _108 - 1;
    data._8 = param_1;
    if (!sLoadThread->sendCmdMsg(firstLoadTask, &data, sizeof(data))) {
        UNK_BOOL_B = true;
        return false;
    }

    JASCriticalSection cs;
    _118++;
    return true;
}

bool JASAramStream::load() {
    {
        JASCriticalSection cs;
        _118--;
    }

    if (UNK_BOOL_B) {
        return false;
    }

    if (_114 != 0) {
        return false;
    }

    u32 loop_end_block = (mLoopEnd - 1) / getBlockSamples();
    u32 loop_start_block = mLoopStart / getBlockSamples();
    if (mBlock > loop_end_block) {
        return false;
    }

    u32 size = sBlockSize * mChannelNum + sizeof(BlockHeader);
    u32 offset = mBlock * size + sizeof(Header);
    if (mBlock == loop_end_block) {
        size = mDvdFile.mLength - offset;
    }

    if (NebulaDisc::read(&mDvdFile, sReadBuffer, size, offset) < 0) {
        UNK_BOOL_B = true;
        return false;
    }

    BlockHeader* bhead = (BlockHeader*)sReadBuffer;
    if (_114 != 0) {
        return false;
    }

    u32 sp08 = _148 + _10C * sBlockSize;
    for (int i = 0; i < mChannelNum; i++) {
        if (!JKRAram::mainRamToAram(sReadBuffer + sizeof(BlockHeader) + bhead->_4 * i, sp08 + sBlockSize * _160 * i, bhead->_4,
                                    EXPAND_SWITCH_UNKNOWN0, 0, nullptr, -1, nullptr)) {
            UNK_BOOL_B = 1;
            return false;
        }
    }

    _10C++;
    if (_10C >= _108) {
        u32 r28 = mBlock;
        r28 += _108 - 1;
        if (mLoop) {
            while (r28 > loop_end_block) {
                r28 -= loop_end_block;
                r28 += loop_start_block;
            }
        }

        if (r28 == loop_end_block || r28 + 2 == loop_end_block) {
            _108 = _160;
            _020.send((void*)5, true);
        } else {
            _108 = _160 - 1;
        }

        for (int i = 0; i < mChannelNum; i++) {
            _130[i] = (s16)bhead->_8[i]._0;
            _13C[i] = (s16)bhead->_8[i]._2;
        }

        _10C = 0;
    }

    mBlock++;
    if (mBlock > loop_end_block && mLoop) {
        mBlock = loop_start_block;
    }

    return true;
}

s32 JASAramStream::channelProcCallback(void* i_this) {
    JASAramStream* stream = (JASAramStream*)i_this;
    return stream->channelProc();
}

void JASAramStream::channelCallback(u32 i_callbackType, JASChannel* i_channel, JASDsp::TChannel* i_dspChannel, void* i_this) {
    JASAramStream* stream = (JASAramStream*)i_this;
    stream->updateChannel(i_callbackType, i_channel, i_dspChannel);
}

void JASAramStream::updateChannel(u32 i_callbackType, JASChannel* i_channel, JASDsp::TChannel* i_dspChannel) {
    u32 block_samples = getBlockSamples();
    switch (i_callbackType) {
    case JASChannel::CB_START:
        if (mUpdateChannel == nullptr) {
            _0B4 = block_samples * mBufCount;
            _0B8 = 0;
            mUpdateChannel = i_channel;
            _0B0 = 0;
            _0BC = (mLoopEnd - 1) / block_samples;
            _0C0 = false;
            _0C4 = 0;
            _12C = 0;
        }

        break;
    case JASChannel::CB_PLAY:
        if (i_dspChannel->starting == 0) {
            if (i_channel == mUpdateChannel) {
                _12C = 0;
                u32 sp28 = i_dspChannel->left + i_dspChannel->leftover;
                if (sp28 <= _0B4) {
                    _0B8 += _0B4 - sp28;
                } else if (!_0C0) {
                    _0B8 += _0B4;
                    _0B8 += block_samples * mBufCount - sp28;
                } else {
                    _0B8 += _0B4;
                    _0B8 += block_samples * mBufCount - sp28 - i_dspChannel->loop_start;
                    _0B8 -= mLoopEnd;
                    _0B8 += mLoopStart;
                    i_dspChannel->loop_start = 0;
                    _120 = 0;
                    _12C |= 2;
                    if (_0C4 < 0xffffffff) {
                        _0C4++;
                    }

                    _0C0 = false;
                }

                if (_0B8 > mLoopEnd) {
                    UNK_BOOL_B = true;
                }

                f32 fvar1 = _0C4;
                fvar1 *= mLoopEnd - mLoopStart;
                if (_0C4 < 0xffffffff) {
                    fvar1 += _0B8;
                }

                fvar1 /= _164;
                _0C8 = fvar1;
                if (_0B8 + 400 >= mLoopEnd && !_0C0) {
                    if (mLoop) {
                        u32 uvar5 = _0BC + 1;
                        if (uvar5 >= mBufCount) {
                            uvar5 = 0;
                        }

                        i_dspChannel->loop_start = mLoopStart % block_samples + uvar5 * block_samples;
                        _120 = i_dspChannel->loop_start;
                        _12C |= 2;
                    } else {
                        i_dspChannel->loop = 0;
                        _128 = 0;
                        _12C |= 8;
                    }

                    i_dspChannel->left -= block_samples * mBufCount - _0BC * block_samples - mLoopEnd % block_samples;
                    _11C = i_dspChannel->left;
                    _12C |= 1;
                    _0BC += (mLoopEnd - 1) / block_samples - mLoopStart / block_samples + 1;
                    _0C0 = true;
                }

                u32 uvar4 = i_dspChannel->address - i_channel->mWavePtr;
                if (uvar4 != 0) {
                    uvar4--;
                }

                u32 sp18 = uvar4 / sBlockSize;
                u32 sp14 = (mLoopEnd - 1) / getBlockSamples();
                if (sp18 != _0B0) {
                    bool cmp = sp18 < _0B0;
                    while (sp18 != _0B0) {
                        if (!sLoadThread->sendCmdMsg(loadToAramTask, this)) {
                            UNK_BOOL_B = true;
                            break;
                        }

                        {
                            JASCriticalSection cs;
                            _118++;
                        }

                        _0B0++;
                        if (_0B0 >= mBufCount) {
                            _0B0 = 0;
                        }
                    }

                    if (cmp) {
                        _0BC -= mBufCount;
                        if (_0AD) {
                            if (!_0C0) {
                                i_dspChannel->left += block_samples;
                                _11C = i_dspChannel->left;
                                _12C |= 1;
                            }

                            i_dspChannel->end += block_samples;
                            _124 = i_dspChannel->end;
                            _12C |= 4;
                            mBufCount = _160;
                            _0AD = false;
                        } else {
                            if (mBufCount != _160 - 1) {
                                mBufCount = _160 - 1;
                                i_dspChannel->end -= block_samples;
                                _124 = i_dspChannel->end;
                                _12C |= 4;
                                if (!_0C0) {
                                    i_dspChannel->left -= block_samples;
                                    _11C = i_dspChannel->left;
                                    _12C |= 1;
                                }
                            }
                        }
                    }
                } else {
                    if (_118 == 0) {
                        _0AE &= ~2;
                        _0AE &= ~4;
                    }
                }

                _0B4 = i_dspChannel->left + i_dspChannel->leftover;
                if (_118 >= _160 - 2) {
                    _0AE |= (u8)4;
                }
            } else {
                if (_12C & 1) {
                    i_dspChannel->left = _11C;
                }

                if (_12C & 2) {
                    i_dspChannel->loop_start = _120;
                }

                if (_12C & 4) {
                    i_dspChannel->end = _124;
                }

                if (_12C & 8) {
                    i_dspChannel->loop = _128;
                }
            }

            int ch = 0;
            for (; ch < CHANNEL_MAX; ch++) {
                if (i_channel == mChannels[ch]) {
                    break;
                }
            }

            i_dspChannel->loop_history[0] = (s16)_130[ch];
            i_dspChannel->loop_history[1] = (s16)_13C[ch];
        }

        break;
    case JASChannel::CB_STOP:
        bool open_channel = false;
        for (int i = 0; i < CHANNEL_MAX; i++) {
            if (i_channel == mChannels[i]) {
                mChannels[i] = nullptr;
            } else if (mChannels[i] != nullptr) {
                open_channel = true;
            }
        }

        if (!open_channel) {
            _114 = 1;
            if (!sLoadThread->sendCmdMsg(finishTask, this)) {
                UNK_BOOL_B = true;
                return;
            }
        }

        break;
    }

    i_channel->setPauseFlag(_0AE != 0);
}

s32 JASAramStream::channelProc() {
    void* msg;
    while (_020.receive(&msg, false)) {
        switch ((u32)(uintptr_t)msg) {
        case 4:
            _0AC = true;
            break;
        case 5:
            _0AD = true;
            break;
        }
    }

    if (!_0AC) {
        return 0;
    }

    while (_000.receive(&msg, false)) {
        switch ((u32)(uintptr_t)msg & 0xff) {
        case 0:
            channelStart();
            break;
        case 1:
            channelStop(JSUHiHalf((u32)(uintptr_t)msg));
            break;
        case 2:
            _0AE |= 1;
            break;
        case 3:
            _0AE &= ~1;
            break;
        }
    }

    if (UNK_BOOL_B) {
        _0AE |= 8;
    }

    for (int i = 0; i < mChannelNum; i++) {
        JASChannel* channel = mChannels[i];
        if (channel != nullptr) {
            JASChannelParams params;
            params.mVolume = mVolume * mChannelVolume[i];
            params.mPitch = mPitch;
            params.mPan = mChannelPan[i];
            params.mFxMix = mChannelFxMix[i];
            params.mDolby = mChannelDolby[i];
            channel->setParams(params);
        }
    }

    return 0;
}

static JASOscillator::Point const OSC_RELEASE_TABLE[2] = {
    {0x0000, 0x0002, 0x0000},
    {0x000F, 0x0000, 0x0000},
};

static JASOscillator::Data const OSC_ENV = {0, 1.0f, nullptr, OSC_RELEASE_TABLE, 1.0f, 0.0f};

void JASAramStream::channelStart() {
    u8 r31;
    switch (_158) {
    case 0:
        r31 = 0;
        break;
    case 1:
        r31 = 3;
        break;
    }

    for (int i = 0; i < mChannelNum; i++) {
        JASWaveInfo wave_info;
        wave_info.mFormat = r31;
        wave_info.mLoopFlags = -1;
        wave_info.mSampleLoopStart = 0;
        wave_info.mSampleLoopEnd = mBufCount * getBlockSamples();
        wave_info.mSampleCount = wave_info.mSampleLoopEnd;
        wave_info.mpLastSample = 0;
        wave_info.mpPenultSample = 0;
        // probably a fake match, this should be set in the JASWaveInfo constructor
        static u32 const one = 1;
        wave_info._24 = &one;
        JASChannel* jc = new JASChannel(channelCallback, this);
        jc->setPriority(0x7f7f);
        for (u32 j = 0; j < 6; j++) {
            jc->setMixConfig(j, _1DC[j]);
        }

        jc->setInitPitch(_164 / JASDriver::getDacRate());
        jc->setOscInit(0, &OSC_ENV);
        jc->mWaveInfo = wave_info;
        jc->mWavePtr = _148 + sBlockSize * _160 * i;
        jc->mChannelType = JASChannel::CH_WAVE;
        int ret = jc->playForce();
        mChannels[i] = jc;
    }

    mUpdateChannel = nullptr;
}

void JASAramStream::channelStop(u16 i_directRelease) {
    for (int i = 0; i < mChannelNum; i++) {
        JASChannel* channel = mChannels[i];
        if (channel != nullptr) {
            channel->release(i_directRelease);
        }
    }
}
