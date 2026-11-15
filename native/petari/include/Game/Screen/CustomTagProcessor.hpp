#pragma once

#include "Game/Screen/MessageTagSkipTagProcessor.hpp"
#include <NebulaGpuTypes.h>

namespace Ui {
    class TextBox;
}  // namespace Ui

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
    CustomTagProcessor(Ui::TextBox*);

    virtual ~CustomTagProcessor();
    virtual Operation Process(u16, ContextType*) override;
    virtual Operation CalcRect(Ui::Rect*, u16, ContextType*) override;
    virtual void BeginPrint(const char16_t* pStr) override;
    virtual bool NextCharAlpha(u8* pAlpha) override;

    void setArgNumber(s32, s32);
    void setArgString(const char16_t*, s32);
    void initAlpha(f32, f32, s32, s32);
    void reset(const char16_t*);
    MessageEditorMessageTag getReplaceTag(const char16_t*, s32, s32, s32) const;
    bool isIgnoreTag(u16, ContextType*) const;
    bool writeString(Ui::Rect*, const char16_t*, ContextType*);
    Operation exeDisplayGroup(Ui::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeSoundGroup(Ui::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exePictureGroup(Ui::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeFontSizeGroup(Ui::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeSystemGroup(Ui::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeLocalizeGroup(Ui::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeNumberGroup(Ui::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeStringGroup(Ui::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeSystemGroupColor(Ui::Rect*, int, ContextType*);
    Operation exeSystemGroupRuby(Ui::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeDisplayGroupWait(Ui::Rect*, u16, ContextType*);
    Operation exeDisplayGroupOffset(Ui::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeDisplayGroupCenter(Ui::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exeFontGroup(Ui::Rect*, const MessageEditorMessageTag&, ContextType*);
    Operation exePatchimuGroup(Ui::Rect*, const MessageEditorMessageTag&, ContextType*);

    struct Impl {
        struct GroupFunctionInfo {
            u8 mGroup;
            Operation (CustomTagProcessor::*mFunction)(Ui::Rect*, const MessageEditorMessageTag&, ContextType*);
        };

        static GroupFunctionInfo* findGroupFunctionInfo(int);
        static GroupFunctionInfo sGroupFunctionTable[];
    };

    bool mIsShadow;
    bool mIsText;
    bool mIsInfo;
    bool mNoCharSpace;
    CustomTagAlphaCtrl mAlphaCtrl;
    Ui::TextBox* mTextBox;
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
