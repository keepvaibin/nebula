#include "Game/Util/TextString.hpp"
#include "Game/Screen/CustomTagProcessor.hpp"
#include "Game/Screen/ReplaceTagProcessor.hpp"
#include "Game/System/Language.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <algorithm>
#include <cstdio>
#include <Game/Ui/Material.hpp>
#include <Game/Ui/TextBox.hpp>
#include <Game/Ui/TextWriterBase.hpp>
#include "Game/Util/ByteView.hpp"

namespace {
    const f32 mRubyBaseLineOffset = 3.0f;
    const f32 mRubyCharSpace = 2.0f;

    NebulaColor8 sTextColor[] = {{255, 255, 255, 255}, {220, 130, 130, 255}, {80, 170, 80, 255},   {80, 140, 210, 255},
                            {235, 200, 0, 255},   {180, 110, 200, 255}, {255, 190, 190, 255}, {110, 243, 70, 255},
                            {120, 255, 255, 255}, {255, 255, 80, 255},  {251, 188, 250, 255}, {190, 190, 200, 255}};


    u8 clampU8(s32 value);

    NebulaColor8 toColor8(NebulaColor16 color) {
        NebulaColor8 result;
        result.r = clampU8(color.r);
        result.g = clampU8(color.g);
        result.b = clampU8(color.b);
        result.a = clampU8(color.a);
        return result;
    }

    u8 clampU8(s32 value) {
        return value < 0 ? 0 : value > 255 ? 255 : value;
    }

    const NebulaColor8& getTextColor(s32 index) {
        // A colour number past the table (an edited message) is the default colour.
        if (index < 0 || index >= ARRAY_SIZE(::sTextColor)) {
            index = 0;
        }

        return ::sTextColor[index];
    }

    void setTextColor(Ui::TextWriterBase< char16_t >* writer, s32 index) {
        NebulaColor8 color = getTextColor(index);
        color.a = writer->GetTextColor().a;
        writer->SetTextColor(color);
    }
};  // namespace

CustomTagAlphaCtrl::CustomTagAlphaCtrl() : mStartDelay(), mEndDelay(), mFrame() {
    mCharInterval = 0.0f;
    mFadeSpeed = 0.0f;
    mCharIndex = 0;
    mCharCount = 0;
    mWaitTime = 0;
    mEnabled = false;
}

void CustomTagAlphaCtrl::init(u32 count, f32 speed, f32 interval, s32 startDelay, s32 endDelay) {
    if (0.0f == speed) {
        mEnabled = false;
        return;
    }

    mEnabled = true;
    mFrame = -startDelay;
    mCharIndex = 0;
    mCharInterval = interval;
    mFadeSpeed = speed;
    mCharCount = count;
    mWaitTime = 0;
    mStartDelay = startDelay;
    mEndDelay = endDelay;
}

u8 CustomTagAlphaCtrl::alpha() const {
    if (mEnabled) {
        f32 value = mFadeSpeed * (mFrame - mWaitTime) - mCharIndex * mCharInterval;
        return (s32)(255.0f * std::max(0.0f, std::min(1.0f, value)));
    }

    return 255;
}

void CustomTagAlphaCtrl::update() {
    s32 delay = mWaitTime + mEndDelay;
    mFrame = std::min(mFrame + 1, static_cast< s32 >((1.0f + mCharCount * mCharInterval) / mFadeSpeed) + delay);
}

bool CustomTagAlphaCtrl::isEnd() const {
    if (mEnabled) {
        s32 frame = mFrame;
        s32 delay = mWaitTime + mEndDelay;
        return frame >= static_cast< s32 >((1.0f + mCharCount * mCharInterval) / mFadeSpeed) + delay;
    }

    return true;
}

CustomTagProcessor::Impl::GroupFunctionInfo CustomTagProcessor::Impl::sGroupFunctionTable[] = {
    {1, &CustomTagProcessor::exeDisplayGroup},
    {2, &CustomTagProcessor::exeSoundGroup},
    {3, &CustomTagProcessor::exePictureGroup},
    {4, &CustomTagProcessor::exeFontSizeGroup},
    {5, &CustomTagProcessor::exeLocalizeGroup},
    {6, &CustomTagProcessor::exeNumberGroup},
    {7, &CustomTagProcessor::exeStringGroup},
    {255, &CustomTagProcessor::exeSystemGroup},
    {10, &CustomTagProcessor::exeFontGroup},


    {0, nullptr},
};

CustomTagProcessor::Impl::GroupFunctionInfo* CustomTagProcessor::Impl::findGroupFunctionInfo(int group) {
    GroupFunctionInfo* info = sGroupFunctionTable;
    while (info->mFunction) {
        if (info->mGroup == group) {
            return info;
        }

        info++;
    }

    return nullptr;
}

namespace Ui {
    template <>
    f32 TextWriterBase< char16_t >::PrintImpl(StreamType str, int length) {

        f32 xOrigin = GetCursorX();
        f32 yOrigin = GetCursorY();
        const bool bUseLimit = (mWidthLimit < Ui::cWidthUnlimited);
        const f32 orgCursorY = yOrigin;
        f32 yCursorAdj = 0.0f;
        f32 textWidth = 0;
        bool bCharSpace = false;
        CharCode prevCode = 0;  // the character before on this line, for the font's kerning (0: none)
        StreamType prevStreamPos = str;
        StreamType prevNewLinePos = str;

        {
            textWidth = AdjustCursor(&xOrigin, &yOrigin, str, length);
            yCursorAdj = orgCursorY - GetCursorY();
        }

        PrintContext< CharType > context = {this, str, xOrigin, yOrigin, 0};
        CharStrmReader reader = GetFont()->GetCharStrmReader();
        reader.Set(str);

        CustomTagProcessor* processor = nullptr;
        const u8 alpha = GetAlpha();
        if (&mDefaultTagProcessor != mTagProcessor) {
            processor = static_cast< CustomTagProcessor* >(mTagProcessor);
            processor->reset(str);
        }

        for (CharCode code = reader.Next(); reinterpret_cast< StreamType >(reader.GetCurrentPos()) - str <= length;) {
            if (code < ' ')

            {
                typename TagProcessor::Operation operation;
                context.str = reinterpret_cast< StreamType >(reader.GetCurrentPos());
                context.flags = 0;
                context.flags |= bCharSpace ? 0 : CONTEXT_NO_CHAR_SPACE;
                prevCode = 0;

                if (bUseLimit && (code != '\n') && (prevStreamPos != prevNewLinePos)) {
                    PrintContext< CharType > context2 = context;
                    TextWriterBase< CharType > myCopy = *this;
                    Rect rect;

                    context2.writer = &myCopy;
                    operation = mTagProcessor->CalcRect(&rect, code, &context2);

                    if ((rect.GetWidth() > 0.0f) && (myCopy.GetCursorX() - context.xOrigin > mWidthLimit)) {
                        code = '\n';
                        reader.Set(prevStreamPos);
                        continue;
                    }
                }

                operation = mTagProcessor->Process(code, &context);

                if (operation == TagProcessor::OPERATION_NEXT_LINE) {

                    if (IsDrawFlagSet(HORIZONTAL_ALIGN_MASK, HORIZONTAL_ALIGN_CENTER)) {
                        const int remain = length - (context.str - str);
                        const f32 width = CalcLineWidth(context.str, remain);
                        const f32 offset = (textWidth - width) / 2;
                        SetCursorX(context.xOrigin + offset);
                    } else if (IsDrawFlagSet(HORIZONTAL_ALIGN_MASK, HORIZONTAL_ALIGN_RIGHT)) {
                        const int remain = length - (context.str - str);
                        const f32 width = CalcLineWidth(context.str, remain);
                        const f32 offset = textWidth - width;
                        SetCursorX(context.xOrigin + offset);
                    } else {
                        const f32 width = GetCursorX() - context.xOrigin;
                        textWidth = Max(textWidth, width);

                        SetCursorX(context.xOrigin);
                    }

                    if (bUseLimit) {
                        prevNewLinePos = reinterpret_cast< StreamType >(reader.GetCurrentPos());
                    }
                    bCharSpace = false;
                } else if (operation == TagProcessor::OPERATION_NO_CHAR_SPACE) {
                    bCharSpace = false;
                } else if (operation == TagProcessor::OPERATION_CHAR_SPACE) {
                    bCharSpace = true;
                } else if (operation == TagProcessor::OPERATION_END_DRAW) {
                    break;
                }

                reader.Set(context.str);
            } else

            {
                if (processor) {
                    SetAlpha(processor->mAlphaCtrl.alpha());
                    processor->mAlphaCtrl.mCharIndex++;
                }

                const f32 baseY = GetCursorY();

                // The space is used up by the line break (CalcLineRectImpl breaks the line there too).
                if (bUseLimit && code == ' ' && (prevStreamPos != prevNewLinePos) &&
                    IsWordBreak(reader, str + length, GetCursorX() - xOrigin, bCharSpace)) {
                    code = '\n';
                    continue;
                }

                // The font's kerning before this character (CalcLineRectImpl measures it the same).
                const f32 kern = IsWidthFixed() || prevCode == 0 ? 0.0f : GetFont()->GetKerning(prevCode, code) * GetScaleH();

                if (bUseLimit && (prevStreamPos != prevNewLinePos)) {
                    f32 baseX = GetCursorX();
                    f32 charSpace = bCharSpace ? GetCharSpace() : 0.0f;
                    f32 charWidth = IsWidthFixed() ? GetFixedWidth() : GetFont()->GetCharWidth(code) * GetScaleH() + kern;

                    if (baseX - xOrigin + charSpace + charWidth > mWidthLimit) {
                        code = '\n';
                        reader.Set(prevStreamPos);
                        continue;
                    }
                }

                if (bCharSpace) {
                    MoveCursorX(GetCharSpace());
                }
                bCharSpace = true;
                MoveCursorX(kern);

                {
                    const Font* pFont = GetFont();
                    const f32 adj = -pFont->GetBaselinePos() * GetScaleV();
                    MoveCursorY(adj);
                }

                CharWriter::Print(code);
                prevCode = code;


                SetCursorY(baseY);
            }

            if (bUseLimit) {
                prevStreamPos = reinterpret_cast< StreamType >(reader.GetCurrentPos());
            }

            code = reader.Next();
        }

        {
            const f32 width = GetCursorX() - context.xOrigin;
            textWidth = Max(textWidth, width);
        }

        if (IsDrawFlagSet(VERTICAL_ORIGIN_MASK, VERTICAL_ORIGIN_MIDDLE) || IsDrawFlagSet(VERTICAL_ORIGIN_MASK, VERTICAL_ORIGIN_BOTTOM)) {
            SetCursorY(orgCursorY);
        } else {
            MoveCursorY(yCursorAdj);
        }

        if (processor) {
            SetAlpha(alpha);
        }

        return textWidth;
    }
}  // namespace Ui

CustomTagProcessor::CustomTagProcessor(Ui::TextBox* textBox)
    : mAlphaCtrl(), mPlayedSounds(), mColorMappingMin(::toColor8(textBox->GetMaterial()->GetStageColor(0))),
      mColorMappingMax(::toColor8(textBox->GetMaterial()->GetStageColor(1))) {
    mRubyFontWidth = 0.5f * textBox->mFontSize.width;
    mRubyFontHeight = 0.5f * textBox->mFontSize.height;
    mFontWidth = textBox->mFontSize.width;
    mFontHeight = textBox->mFontSize.height;


    mIsShadow = false;
    mIsText = false;
    mIsInfo = false;

    mNoCharSpace = true;

    mTextBox = textBox;
}

CustomTagProcessor::Operation CustomTagProcessor::CalcRect(Ui::Rect* rect, u16 code, ContextType* context) {
    if (isIgnoreTag(code, context)) {
        return MessageTagSkipTagProcessor::CalcRect(rect, code, context);
    }

    if (!mNoCharSpace && !(context->flags & 1)) {
        context->writer->MoveCursorX(context->writer->GetCharSpace());
    }

    MessageEditorMessageTag tag(context);
    Operation operation = OPERATION_DEFAULT;
    Impl::GroupFunctionInfo* info = Impl::findGroupFunctionInfo(tag.getGroup());
    if (info) {
        operation = (this->*info->mFunction)(rect, tag, context);
    }

    context->str += tag.getSkipLength();
    return operation;
}

CustomTagProcessor::Operation CustomTagProcessor::Process(u16 code, ContextType* context) {
    if (isIgnoreTag(code, context)) {
        return MessageTagSkipTagProcessor::Process(code, context);
    }

    if (!mNoCharSpace && !(context->flags & 1)) {
        context->writer->MoveCursorX(context->writer->GetCharSpace());
    }

    MessageEditorMessageTag tag(context);
    Operation operation = OPERATION_DEFAULT;
    Impl::GroupFunctionInfo* info = Impl::findGroupFunctionInfo(tag.getGroup());
    if (info) {
        operation = (this->*info->mFunction)(nullptr, tag, context);
    }

    context->str += tag.getSkipLength();
    return operation;
}

void CustomTagProcessor::setArgNumber(s32 number, s32 index) {
    for (s32 i = 0; i < 4; i++) {
        MessageEditorMessageTag tag = getReplaceTag(mTextBox->mTextBuf, 6, index, i);
        if (!tag.mMessage) {
            break;
        }

        writeBytes< s32 >(tag.getParamPtr(0), number);
    }
}

void CustomTagProcessor::setArgString(const char16_t* string, s32 index) {
    for (s32 i = 0; i < 4; i++) {
        MessageEditorMessageTag tag = getReplaceTag(mTextBox->mTextBuf, 7, index, i);
        if (!tag.mMessage) {
            break;
        }

        tag.setParamString(string);
    }
}

void CustomTagProcessor::initAlpha(f32 speed, f32 interval, s32 startDelay, s32 endDelay) {
    mAlphaCtrl.init(MR::countMessageChar(mTextBox->mTextBuf), speed, interval, startDelay, endDelay);
    mPlayedSounds = 0;
    mSoundIndex = 0;
    mColorIndex = 0;
}

void CustomTagProcessor::reset(const char16_t* string) {
    if (mTextBox->mTextBuf != string) {
        return;
    }

    mSoundIndex = 0;
    mColorIndex = 0;
    mAlphaCtrl.mWaitTime = 0;
    mAlphaCtrl.mCharIndex = 0;

}

MessageEditorMessageTag CustomTagProcessor::getReplaceTag(const char16_t* string, s32 group, s32 index, s32 occurrence) const {
    while (*string) {
        if (*string == 0x1A) {
            string++;
            MessageEditorMessageTag tag(string);
            string += tag.getSkipLength();
            s32 tagGroup = tag.getGroup();
            if (tagGroup == group && index == tag.getParam32(1)) {
                if (occurrence == 0) {
                    return tag;
                }

                occurrence--;
            }
        } else {
            string++;
        }
    }

    return MessageEditorMessageTag(static_cast< const char16_t* >(nullptr));
}

bool CustomTagProcessor::isIgnoreTag(u16 code, ContextType* context) const {
    if (code != 0x1A) {
        return true;
    }

    MessageEditorMessageTag tag(context);
    return !Impl::findGroupFunctionInfo(tag.getGroup());
}

bool CustomTagProcessor::writeString(Ui::Rect* rect, const char16_t* string, ContextType* context) {
    Ui::TextWriterBase< char16_t >* writer = context->writer;
    if (!string) {
        return false;
    }

    if (!rect) {
        writer->MoveCursorY(-writer->GetFontAscent());
        writer->Print(string, MR::getStringLengthWithMessageTag(string));
        writer->MoveCursorY(writer->GetFontAscent());
    } else {
        Ui::TextWriterBase< char16_t > copy = *writer;
        f32 x = copy.GetCursorX();
        f32 y = copy.GetCursorY();
        copy.CalcStringRect(rect, string, MR::getStringLengthWithMessageTag(string));
        context->writer->MoveCursorX(rect->GetWidth());
        context->writer->MoveCursorY(rect->GetHeight() - copy.GetFontHeight());
        rect->left += x;
        rect->right += x;
        rect->top += y;
        rect->bottom += y;
    }

    return true;
}

CustomTagProcessor::Operation CustomTagProcessor::exeDisplayGroup(Ui::Rect* rect, const MessageEditorMessageTag& tag, ContextType* context) {
    switch (tag.getTag()) {
    case 0:
        return exeDisplayGroupWait(rect, tag.getParam16(0), context);
    case 2:
        return exeDisplayGroupOffset(rect, tag, context);
    case 3:
        return exeDisplayGroupCenter(rect, tag, context);
    case 1:
        return OPERATION_END_DRAW;
    }

    return OPERATION_NO_CHAR_SPACE;
}

CustomTagProcessor::Operation CustomTagProcessor::exeSoundGroup(Ui::Rect* rect, const MessageEditorMessageTag& tag, ContextType* /*context*/) {
    if (rect || mIsText || !mAlphaCtrl.alpha()) {
        return OPERATION_NO_CHAR_SPACE;
    }

    // Only the first eight sounds of a message are tracked (a ninth made no mask before either).
    if (mSoundIndex >= 8) {
        return OPERATION_NO_CHAR_SPACE;
    }

    u8 mask = 1 << mSoundIndex;

    if (mask != static_cast< u8 >(mPlayedSounds & mask)) {
        MR::FormattedName name;
        const s32 length = static_cast< s32 >(tag.getParamLength()) / 2 + 1;
        MR::convertUTF16ToASCII(name.room(length), tag.getParamPtr(0), length);
        MR::startSystemSE(name.c_str());
        mPlayedSounds |= static_cast< u8 >(1 << mSoundIndex);
    }

    mSoundIndex++;
    return OPERATION_NO_CHAR_SPACE;
}

CustomTagProcessor::Operation CustomTagProcessor::exePictureGroup(Ui::Rect* rect, const MessageEditorMessageTag& tag, ContextType* context) {
    Ui::TextWriterBase< char16_t > writer = *context->writer;
    writer.SetFont(*MR::getPictureFont());
    // The text writer reads one character past the length it is given (its loop's next read precedes the bound
    // check); a terminator keeps that read inside the buffer.
    const char16_t pictureText[2] = {static_cast< char16_t >(tag.getTag() + u'0'), 0};
    const char16_t& picture = pictureText[0];
    f32 width = writer.CalcStringWidth(&picture, 1) + writer.GetCharSpace();
    context->writer->MoveCursorX(width);

    if (rect) {
        rect->right = rect->left + width;
        rect->SetHeight(writer.GetFontHeight() + writer.GetLineSpace());
    } else {
        if (!mIsShadow) {
            writer.ResetColorMapping();
            writer.Setup();
            if (mColorIndex) {
                ::setTextColor(&writer, 0);
            }
        }

        writer.MoveCursorY(-writer.GetFontAscent());

        writer.Print(&picture, 1);
        if (!mIsShadow) {
            context->writer->Setup();
        }
    }

    return OPERATION_DEFAULT;
}

CustomTagProcessor::Operation CustomTagProcessor::exeFontSizeGroup(Ui::Rect* rect, const MessageEditorMessageTag& tag, ContextType* context) {
    if (rect != nullptr) {
        return OPERATION_NO_CHAR_SPACE;
    }

    Ui::TextWriterBase< char16_t >* writer = context->writer;
    switch (tag.getTag()) {
    case 0:
        writer->SetFontSize(0.75f * mFontWidth, 0.75f * mFontHeight);
        break;
    case 1:
        writer->SetFontSize(mFontWidth, mFontHeight);
        break;
    case 2:
        writer->SetFontSize(1.5f * mFontWidth, 1.5f * mFontHeight);
        break;
    }


    return OPERATION_NO_CHAR_SPACE;
}

CustomTagProcessor::Operation CustomTagProcessor::exeSystemGroup(Ui::Rect* rect, const MessageEditorMessageTag& tag, ContextType* context) {
    switch (tag.getTag()) {
    case 0:
        return exeSystemGroupColor(rect, tag.getParam8(0), context);
    case 2:
        return exeSystemGroupRuby(rect, tag, context);
    }

    return OPERATION_NO_CHAR_SPACE;
}

CustomTagProcessor::Operation CustomTagProcessor::exeLocalizeGroup(Ui::Rect* rect, const MessageEditorMessageTag& tag, ContextType* context) {
    MR::Text16 string;
    ReplaceTagProcessor::exeLocalizeGroup(string.room(ReplaceTagProcessor::getLocalizeGroupRoom(tag)), tag);
    writeString(rect, string.c_str(), context);
    return OPERATION_DEFAULT;
}

CustomTagProcessor::Operation CustomTagProcessor::exeNumberGroup(Ui::Rect* rect, const MessageEditorMessageTag& tag, ContextType* context) {
    char16_t string[16];  // an s32 is at most 11 characters, "-2147483648", with the terminator
    s32 number = readBytes< s32 >(tag.getParamPtr(0));
    switch (tag.getTag()) {
    case 5:
        MR::formatText(string, ARRAY_SIZEU(string), u"%02d", number);
        break;
    case 6:
        MR::formatText(string, ARRAY_SIZEU(string), u"%03d", number);
        break;
    case 7:
        MR::formatText(string, ARRAY_SIZEU(string), u"%04d", number);
        break;
    case 8:
        MR::formatText(string, ARRAY_SIZEU(string), u"%05d", number);
        break;
    case 9:
        MR::formatText(string, ARRAY_SIZEU(string), u"%06d", number);
        break;
    default:
        MR::formatText(string, ARRAY_SIZEU(string), u"%d", number);
        break;
    }

    writeString(rect, string, context);
    return OPERATION_DEFAULT;
}

CustomTagProcessor::Operation CustomTagProcessor::exeStringGroup(Ui::Rect* rect, const MessageEditorMessageTag& tag, ContextType* context) {
    writeString(rect, tag.getParamString(), context);
    return OPERATION_DEFAULT;
}

CustomTagProcessor::Operation CustomTagProcessor::exeSystemGroupColor(Ui::Rect* rect, int index, ContextType* context) {
    if (mIsShadow) {
        return OPERATION_NO_CHAR_SPACE;
    }

    if (mIsInfo && index >= 1 && index < 6) {
        index += 5;
    }

    if (!rect) {
        mColorIndex = index;
        if (index == 0) {
            context->writer->SetColorMapping(mColorMappingMin, mColorMappingMax);
        } else {
            NebulaColor8 color = ::getTextColor(index);
            color.a = mColorMappingMin.a;
            context->writer->SetColorMapping(color, ::getTextColor(index));
        }

        context->writer->Setup();
        ::setTextColor(context->writer, index);
    }

    return OPERATION_NO_CHAR_SPACE;
}

CustomTagProcessor::Operation CustomTagProcessor::exeSystemGroupRuby(Ui::Rect* rect, const MessageEditorMessageTag& tag, ContextType* context) {
    if (MR::getLanguage() != LanguageId_JpJapanese) {
        return OPERATION_NO_CHAR_SPACE;
    }

    // A ruby tag with no room for its text (an edited message) draws no ruby.
    if (tag.getParamLength() < 2 + static_cast< s32 >(sizeof(char16_t))) {
        return OPERATION_NO_CHAR_SPACE;
    }

    s32 baseLength = tag.getParam8(0);
    const char16_t* rubyText = tag.getParamPtr(1);
    s32 rubyLength = (tag.getParamLength() - 2) / sizeof(char16_t);
    MR::Text16 rubyString;
    char16_t* const ruby = rubyString.room(static_cast< u32 >(rubyLength) + 1);
    MR::copyMemory(ruby, rubyText, rubyLength * sizeof(char16_t));
    ruby[rubyLength] = u'\0';
    MR::Text16 baseString;
    char16_t* const base = baseString.room(static_cast< u32 >(baseLength) + 1);
    MR::copyMemory(base, context->str + tag.getTagLength() / 2, baseLength * sizeof(char16_t));
    base[baseLength] = u'\0';
    Ui::TextWriterBase< char16_t >* writer = context->writer;
    writer->GetDrawFlag();
    writer->GetCharSpace();
    Ui::TextWriterBase< char16_t > rubyWriter = *writer;
    rubyWriter.SetDrawFlag(Ui::TextWriterBase< char16_t >::VERTICAL_ORIGIN_BASELINE);
    rubyWriter.SetLineSpace(0.0f);
    rubyWriter.SetCharSpace(mRubyCharSpace);
    rubyWriter.SetFontSize(mRubyFontWidth, mRubyFontHeight);
    rubyWriter.SetTagProcessor(this);
    f32 baseWidth = writer->CalcStringWidth(base, baseLength);
    f32 rubyWidth = rubyWriter.CalcStringWidth(ruby, rubyLength);
    if (!rect) {
        f32 extraWidth = baseWidth - rubyWidth;
        if (extraWidth > 0.0f) {
            f32 space = extraWidth / (rubyLength + 1);
            rubyWriter.MoveCursorX(space);
            rubyWriter.SetCharSpace(mRubyCharSpace + space);
        } else {
            rubyWriter.MoveCursorX(extraWidth / 2.0f);
        }

        rubyWriter.MoveCursorY(mRubyBaseLineOffset + -writer->GetFontAscent());
        CustomTagAlphaCtrl alpha = mAlphaCtrl;
        s32 index = baseLength != 0 ? rubyLength * alpha.mCharIndex / baseLength : 0;
        f32 interval = (baseLength * alpha.mCharInterval) / rubyLength;
        mAlphaCtrl.mCharInterval = interval;
        mAlphaCtrl.mCharIndex = index;
        rubyWriter.Print(ruby, rubyLength);
        mAlphaCtrl = alpha;
    }

    return OPERATION_NO_CHAR_SPACE;
}

CustomTagProcessor::Operation CustomTagProcessor::exeDisplayGroupWait(Ui::Rect* rect, u16 frames, ContextType* /*context*/) {
    if (!rect) {
        mAlphaCtrl.mWaitTime += frames;
    }

    return OPERATION_NO_CHAR_SPACE;
}

CustomTagProcessor::Operation CustomTagProcessor::exeDisplayGroupOffset(Ui::Rect* /*rect*/, const MessageEditorMessageTag& tag,
                                                                        ContextType* context) {
    Ui::TextWriterBase< char16_t >* writer = context->writer;
    const char16_t* string = context->str + tag.getSkipLength();
    Ui::Rect bounds;
    if (mTextBox->GetTextPositionV() == 0) {
        writer->CalcStringRect(&bounds, string, MR::getStringLengthWithMessageTag(string));
        writer->MoveCursorY((mTextBox->GetDrawSize().height - bounds.GetHeight()) / 2.0f);
    }

    return OPERATION_NO_CHAR_SPACE;
}

CustomTagProcessor::Operation CustomTagProcessor::exeDisplayGroupCenter(Ui::Rect* /*rect*/, const MessageEditorMessageTag& tag,
                                                                        ContextType* context) {
    Ui::TextWriterBase< char16_t > writer = *context->writer;
    const char16_t* string = context->str + tag.getSkipLength();
    Ui::Rect bounds;
    writer.CalcStringRect(&bounds, string, MR::getStringLengthWithMessageTag(string));
    f32 offset = (mTextBox->GetDrawSize().width - bounds.GetWidth()) / 2.0f;
    context->writer->MoveCursorX(offset);
    context->xOrigin += offset;
    return OPERATION_NO_CHAR_SPACE;
}

CustomTagProcessor::Operation CustomTagProcessor::exeFontGroup(Ui::Rect* rect, const MessageEditorMessageTag& tag, ContextType* context) {
    Ui::TextWriterBase< char16_t > writer = *context->writer;
    writer.SetFont(*MR::getNumberFont());
    const char16_t* string = tag.getParamPtr(0);
    s32 length = static_cast< s32 >(tag.getParamLength()) / 2;
    f32 width = writer.CalcStringWidth(string, length) + writer.GetCharSpace();
    context->writer->MoveCursorX(width);
    if (rect) {
        rect->right = rect->left + width;
        rect->SetHeight(writer.GetFontHeight() + writer.GetLineSpace());
    } else {
        f32 offset = -writer.GetFontAscent();
        if (!mIsShadow) {
            writer.ResetColorMapping();
            writer.Setup();
            ::setTextColor(&writer, 0);
        }

        writer.MoveCursorY(offset);
        writer.Print(string, length);
        if (!mIsShadow) {
            context->writer->Setup();
        }
    }

    return OPERATION_NO_CHAR_SPACE;
}


CustomTagProcessor::~CustomTagProcessor() {
}
