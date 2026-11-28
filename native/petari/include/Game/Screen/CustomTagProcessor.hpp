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
    // A tag of a text in runs: the group functions as for a tag of version 1, on its record.
    virtual Operation ProcessRunTag(const MessageTagRecord& rTag, ContextType* pContext) override;
    virtual Operation CalcRectRunTag(Ui::Rect* pRect, const MessageTagRecord& rTag, ContextType* pContext) override;
    Operation runTag(Ui::Rect* pRect, const MessageTagRecord& rTag, ContextType* pContext);
    // The rectangle of the page's text after the tag (the display group's offset and centre measure it).
    void calcRestRect(const Ui::TextWriterBase< char16_t >& rWriter, Ui::Rect* pRect, const MessageTagView& rTag, ContextType* pContext) const;

    void setArgNumber(s32, s32);
    void setArgString(const char16_t*, s32);
    void initAlpha(f32, f32, s32, s32);
    void reset(const char16_t*);
    MessageEditorMessageTag getReplaceTag(const char16_t*, s32, s32, s32) const;
    bool isIgnoreTag(u16, ContextType*) const;
    bool writeString(Ui::Rect*, const char16_t*, ContextType*);
    Operation exeDisplayGroup(Ui::Rect*, const MessageTagView&, ContextType*);
    Operation exeSoundGroup(Ui::Rect*, const MessageTagView&, ContextType*);
    Operation exePictureGroup(Ui::Rect*, const MessageTagView&, ContextType*);
    Operation exeFontSizeGroup(Ui::Rect*, const MessageTagView&, ContextType*);
    Operation exeSystemGroup(Ui::Rect*, const MessageTagView&, ContextType*);
    Operation exeLocalizeGroup(Ui::Rect*, const MessageTagView&, ContextType*);
    Operation exeNumberGroup(Ui::Rect*, const MessageTagView&, ContextType*);
    Operation exeStringGroup(Ui::Rect*, const MessageTagView&, ContextType*);
    Operation exeSystemGroupColor(Ui::Rect*, int, ContextType*);
    Operation exeSystemGroupRuby(Ui::Rect*, const MessageTagView&, ContextType*);
    Operation exeDisplayGroupWait(Ui::Rect*, u16, ContextType*);
    Operation exeDisplayGroupOffset(Ui::Rect*, const MessageTagView&, ContextType*);
    Operation exeDisplayGroupCenter(Ui::Rect*, const MessageTagView&, ContextType*);
    Operation exeFontGroup(Ui::Rect*, const MessageTagView&, ContextType*);
    Operation exePatchimuGroup(Ui::Rect*, const MessageTagView&, ContextType*);

    struct Impl {
        struct GroupFunctionInfo {
            u8 mGroup;
            Operation (CustomTagProcessor::*mFunction)(Ui::Rect*, const MessageTagView&, ContextType*);
        };

        static GroupFunctionInfo* findGroupFunctionInfo(int);
        static GroupFunctionInfo sGroupFunctionTable[];
    };

    bool mIsShadow;
    bool mIsText;
    bool mIsInfo;
    bool mNoCharSpace;
    // True while the step walks the text (MR::nextStepTagProcessorRecursive, TextBox::PrintSilent): the sound tags are
    // played there, never from the draw (P-214).
    bool mIsSoundPass;
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
