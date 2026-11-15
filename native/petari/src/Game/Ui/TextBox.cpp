#include "Game/Util/TextString.hpp"
#include "Game/Ui/DrawInfo.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Ui/Layout.hpp"
#include "Game/Ui/ResourceAccessor.hpp"
#include "Game/Ui/TextBox.hpp"
#include "Game/Ui/Font.hpp"
#include "Game/Ui/ResFont.hpp"
#include "Game/Ui/WideTextWriter.hpp"
#include "Game/System/NebulaArena.hpp"
#include <cstdio>

extern "C" {
}

namespace {

    inline u8 ClampColor(s16 colVal) {
        return u8(colVal < 0 ? 0 : (colVal > 255 ? 255 : colVal));
    }

    Color8 GetColor(const NebulaColor16& src) {
        NebulaColor8 dst;
        dst.r = ClampColor(src.r);
        dst.g = ClampColor(src.g);
        dst.b = ClampColor(src.b);
        dst.a = ClampColor(src.a);
        return Color8(dst);
    }
};  // namespace
namespace Ui {
    TextBox::TextBox(Layout& rLayout, const UiPaneData* pRes, const ResBlockSet& resBlockSet) : Pane(rLayout, pRes, resBlockSet) {
        const UiTextBoxData* const pBlock = resBlockSet.At< UiTextBoxData >(pRes->mPayload);
        u32 allocStrBufLen = pBlock->mBufferBytes / sizeof(char16_t);
        if (allocStrBufLen > 0) {
            allocStrBufLen -= 1;
        }

        Init(allocStrBufLen);

        if (pBlock->mTextBytes >= sizeof(char16_t) && mTextBuf) {
            const char16_t* const pBlockText = resBlockSet.At< char16_t >(pBlock->mText);
            const u32 resStrLen = static_cast< u32 >(pBlock->mTextBytes / sizeof(char16_t) - 1);
            SetString(pBlockText, 0, resStrLen);
        }

        for (int i = 0; i < 2; ++i) {
            mTextColors[i] = detail::RecordColor(pBlock->mColors[i]);
        }

        mFontSize = Size(pBlock->mFontSize[0], pBlock->mFontSize[1]);
        mTextPosition = pBlock->mPosition;

        mBits.textAlignment = pBlock->mAlignment;

        mCharSpace = pBlock->mCharSpace;
        mLineSpace = pBlock->mLineSpace;

        const char* const fontName = resBlockSet.FontName(pBlock->mFont);

        if (Font* pFont = resBlockSet.pResAccessor->GetFont(fontName)) {
            mpFont = pFont;
        } else if (void* fontRes = resBlockSet.pResAccessor->GetResource('font', fontName, 0)) {
            void* pMem = Layout::AllocMemory(sizeof(ResFont));
            if (pMem != nullptr) {
                ResFont* pResFont = new (pMem) ResFont;
                pResFont->SetResource(fontRes);
                mpFont = pResFont;
                mBits.bAllocFont = true;
            }
        }

        mpMaterial = rLayout.NewInArena< Material >(resBlockSet.MaterialAt(pBlock->mMaterial), resBlockSet);
    }

    void TextBox::Init(u32 allocStrLen) {
        mTextBuf = 0;
        mTextBufBytes = 0;
        mTextLen = 0;
        mpFont = 0;
        mFontSize = Size(0, 0);
        SetTextPositionH(1);
        SetTextPositionV(1);
        mLineSpace = 0;
        mCharSpace = 0;
        mpTagProcessor = 0;
        memset(&mBits, 0, sizeof(mBits));

        if (allocStrLen > 0) {
            AllocStringBuffer(allocStrLen);
        }
    }

    TextBox::~TextBox() {
        SetFont(nullptr);
        FreeStringBuffer();
    }

    const Color8 TextBox::GetVtxColor(u32 idx) const {
        return GetTextColor(idx / 2);
    }

    void TextBox::SetVtxColor(u32 idx, Color8 color) {
        SetTextColor(idx / 2, color);
    }

    void TextBox::SetTextColor(u32 type, Color8 value) {
        mTextColors[type] = value;
    }

    u8 TextBox::GetVtxColorElement(u32 idx) const {
        return reinterpret_cast< const u8* >(&mTextColors[idx / (4 * 2)])[idx % 4];
    }

    void TextBox::SetVtxColorElement(u32 idx, u8 value) {
        reinterpret_cast< u8* >(&mTextColors[idx / (4 * 2)])[idx % 4] = value;
    }

    const Rect TextBox::GetTextDrawRect() const {
        WideTextWriter writer;

        return GetTextDrawRect(&writer);
    }

    const Rect TextBox::GetTextDrawRect(const DrawInfo& drawInfo) const {
        WideTextWriter writer;
        Rect rect = GetTextDrawRect(&writer);
        if (drawInfo.IsYAxisUp()) {
            rect.top = -rect.top;
            rect.bottom = -rect.bottom;
        }
        return rect;
    }

    void TextBox::DrawSelf(const DrawInfo& drawInfo) {
        if (!mTextBuf || !mpFont || !mpMaterial) {
            return;
        }

        LoadMtx(drawInfo);

        WideTextWriter writer;

        Rect textRect = GetTextDrawRect(&writer);

        Color8 topCol = detail::MultipleAlpha(mTextColors[0], mGlbAlpha);
        Color8 btmCol = detail::MultipleAlpha(mTextColors[1], mGlbAlpha);
        writer.SetGradationMode(topCol != btmCol ? CharWriter::GRADMODE_V : CharWriter::GRADMODE_NONE);
        writer.SetTextColor(topCol, btmCol);
        Color8 minCol = GetColor(mpMaterial->GetStageColor(0));
        Color8 maxCol = GetColor(mpMaterial->GetStageColor(1));
        writer.SetColorMapping(minCol, maxCol);
        writer.Setup();

        writer.SetCursor(textRect.left, textRect.top);
        writer.Print(mTextBuf, mTextLen);
    }

    u32 TextBox::GetStringBufferLength() const {
        if (mTextBufBytes == 0) {
            return 0;
        }

        return mTextBufBytes / sizeof(char16_t) - 1;
    }

    void TextBox::AllocStringBuffer(u32 len) {
        if (len == 0) {
            return;
        }

        u32 chars = len + 1;
        u32 bytes = chars * sizeof(char16_t);

        if (bytes > mTextBufBytes) {
            FreeStringBuffer();
            // In the arena that holds the box: a box grown later (a longer text) keeps its text as long as the box.
            mTextBuf = new (NebulaArena::findFromRoot(this), 0) char16_t[chars];

            if (mTextBuf != NULL) {
                mTextBufBytes = bytes;
            }
        }
    }

    void TextBox::FreeStringBuffer() {
        if (mTextBuf == NULL) {
            return;
        }

        Layout::FreeMemory(mTextBuf);
        mTextBuf = NULL;
        mTextBufBytes = 0;
    }

    u32 TextBox::SetString(const char16_t* pStr, u32 pos) {
        return SetString(pStr, pos, MR::textLength(pStr));
    }

    u32 TextBox::SetString(const char16_t* pStr, u32 pos, u32 len) {
        if (mTextBuf == NULL) {
            return 0;
        }

        const u32 maxlen = GetStringBufferLength();
        if (pos >= maxlen) {
            return 0;
        }

        const u32 chars = Min< u32 >(len, maxlen - pos);
        memcpy(&mTextBuf[pos], pStr, chars * sizeof(char16_t));

        mTextLen = pos + chars;
        mTextBuf[mTextLen] = u'\0';

        return chars;
    }

    void TextBox::SetFont(const Font* pFont) {
        if (mBits.bAllocFont) {
            mpFont->~Font();
            Layout::FreeMemory(const_cast< Font* >(mpFont));
            mBits.bAllocFont = false;
        }

        mpFont = pFont;

        if (mpFont != NULL) {
            SetFontSize(Size(static_cast< f32 >(mpFont->GetWidth()), static_cast< f32 >(mpFont->GetHeight())));
        } else {
            SetFontSize(Size(0.0f, 0.0f));
        }
    }

    const Rect TextBox::GetTextDrawRect(WideTextWriter* pWriter) const {
        Rect rect;

        pWriter->SetCursor(0.0f, 0.0f);

        pWriter->SetFont(*mpFont);
        pWriter->SetFontSize(mFontSize.width, mFontSize.height);

        pWriter->SetLineSpace(mLineSpace);
        pWriter->SetCharSpace(mCharSpace);

        // A number stays on one line, however many digits a mod's counters reach.
        bool isNumber = mTextLen > 0;
        for (u32 i = 0; i < mTextLen && isNumber; i++) {
            isNumber = mTextBuf[i] >= L'0' && mTextBuf[i] <= L'9';
        }
        pWriter->SetWidthLimit(isNumber ? cWidthUnlimited : GetDrawSize().width);
        pWriter->SetDrawFlag(MakeDrawFlag());

        if (mpTagProcessor != NULL) {
            pWriter->SetTagProcessor(mpTagProcessor);
        }

        pWriter->CalcStringRect(&rect, mTextBuf, mTextLen);

        const Vec2f base = GetVtxPos();
        rect.MoveTo(base.x + (GetDrawSize().width - rect.GetWidth()) * GetTextMagH(), base.y + (GetDrawSize().height - rect.GetHeight()) * GetTextMagV());

        return rect;
    }

    f32 TextBox::GetTextMagH() const {
        f32 mag = 0.0f;

        switch (GetTextPositionH()) {
        default:
        case 0: {
            mag = 0.0f;
            break;
        }

        case 1: {
            mag = 0.5f;
            break;
        }

        case 2: {
            mag = 1.0f;
            break;
        }
        }

        return mag;
    }

    f32 TextBox::GetTextMagV() const {
        f32 mag = 0.0f;

        switch (GetTextPositionV()) {
        default:
        case 0: {
            mag = 0.0f;
            break;
        }

        case 1: {
            mag = 0.5f;
            break;
        }

        case 2: {
            mag = 1.0f;
            break;
        }
        }

        return mag;
    }

    u32 TextBox::MakeDrawFlag() const {
        u32 flag = 0;
        // textAlignment is bits 5-6 of the byte: mBitsu8 >> 5.
        switch (GetTextAlignment()) {
        case 0:
        default:
            switch (GetTextPositionH()) {
            case 1:
                flag = 1;

                break;
            case 2:
                flag = 2;

                break;
            }

            break;
        case 1:
            flag = 0;

            break;
        case 2:
            flag = 1;

            break;
        case 3:
            flag = 2;

            break;
        }

        return flag;
    }
}  // namespace Ui
