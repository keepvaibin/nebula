#pragma once

#include "Game/Screen/MessageTagSkipTagProcessor.hpp"
#include <NebulaGpuTypes.h>

namespace nw4r {
    namespace lyt {
        class TextBox;
    };  // namespace lyt
};  // namespace nw4r

class CustomTagAlphaCtrl {
public:
    CustomTagAlphaCtrl();

    void init(u32, f32, f32, s32, s32);
    u8 alpha() const;
    void update();
    bool isEnd() const;

    s32 mStartDelay;
    s32 mEndDelay;
    s32 mFrame;
    s32 mCharIndex;
    s32 mWaitTime;
    u32 mCharCount;
    bool mEnabled;
    f32 mCharInterval;
    f32 mFadeSpeed;
};

class CustomTagProcessor : public MessageTagSkipTagProcessor {
public:
    CustomTagProcessor(nw4r::lyt::TextBox*);

    virtual ~CustomTagProcessor();
    virtual Operation Process(u16, ContextType*) override;
    virtual Operation CalcRect(nw4r::ut::Rect*, u16, ContextType*) override;

    void setArgNumber(s32, s32);
    void setArgString(const char16_t*, s32);
    void initAlpha(f32, f32, s32, s32);
    void reset(const char16_t*);
    MessageEditorMessageTag getReplaceTag(const char16_t*, s32, s32, s32) const;
    bool isIgnoreTag(u16, ContextType*) const;
    bool writeString(nw4r::ut::Rect*, const char16_t*, ContextType*);
    Operation exeDisplayGroup(nw4r::ut::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeSoundGroup(nw4r::ut::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exePictureGroup(nw4r::ut::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeFontSizeGroup(nw4r::ut::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeSystemGroup(nw4r::ut::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeLocalizeGroup(nw4r::ut::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeNumberGroup(nw4r::ut::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeStringGroup(nw4r::ut::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeSystemGroupColor(nw4r::ut::Rect*, int, ContextType*);
    Operation exeSystemGroupRuby(nw4r::ut::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeDisplayGroupWait(nw4r::ut::Rect*, u16, ContextType*);
    Operation exeDisplayGroupOffset(nw4r::ut::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeDisplayGroupCenter(nw4r::ut::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeFontGroup(nw4r::ut::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exePatchimuGroup(nw4r::ut::Rect*, const MessageEditorMessageTag&, ContextType*);

    struct Impl {
        struct GroupFunctionInfo {
            u8 mGroup;
            Operation (CustomTagProcessor::*mFunction)(nw4r::ut::Rect*, const MessageEditorMessageTag&, ContextType*);
        };

        static GroupFunctionInfo* findGroupFunctionInfo(int);
        static GroupFunctionInfo sGroupFunctionTable[];
    };

    bool mIsShadow;
    bool mIsText;
    bool mIsInfo;
    bool mNoCharSpace;
    CustomTagAlphaCtrl mAlphaCtrl;
    nw4r::lyt::TextBox* mTextBox;
    u8 mPlayedSounds;
    u8 mSoundIndex;
    u8 mColorIndex;
    NebulaColor8 mColorMappingMin;
    NebulaColor8 mColorMappingMax;
    f32 mRubyFontWidth;
    f32 mRubyFontHeight;
    f32 mFontWidth;
    f32 mFontHeight;
};
