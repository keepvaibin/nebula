#pragma once

#include "Game/Ui/UiCommon.hpp"
#include "Game/Ui/Pane.hpp"
#include "Game/Util/Color.hpp"

namespace Ui {
    class Font;
    template < typename CharType >
    class TagProcessorBase;

    template < typename CharType >
    class TextWriterBase;

    class DrawInfo;
    struct ResBlockSet;

    class TextBox : public Pane {
    public:
        static constexpr UiPaneKind cKind = UiPaneKind_TextBox;

        TextBox(Layout&, const UiPaneData*, const ResBlockSet&);

        virtual ~TextBox();
        virtual void DrawSelf(const DrawInfo& drawInfo);
        virtual const Color8 GetVtxColor(u32) const;
        virtual void SetVtxColor(u32, Color8);
        virtual u8 GetVtxColorElement(u32) const;
        virtual void SetVtxColorElement(u32, u8);
        virtual void AllocStringBuffer(u16);
        virtual void FreeStringBuffer();
        virtual u16 SetString(const char16_t*, u16 dstIdx = 0);
        virtual u16 SetString(const char16_t*, u16, u16);

        const Rect GetTextDrawRect(TextWriterBase< char16_t >*) const;
        void SetTextColor(Color8, Color8);
        void SetFont(const Font*);
        u16 GetStringBufferLength() const;
        u32 MakeDrawFlag() const;
        f32 GetTextMagH() const;
        f32 GetTextMagV() const;
        void SetTextColor(u32, Color8);
        const Rect GetTextDrawRect() const;
        const Rect GetTextDrawRect(const DrawInfo&) const;

        void Init(u16);
        void SetTagProcessor(TagProcessorBase< char16_t >* pTagProcessor) {
            mpTagProcessor = pTagProcessor;
        }

        void SetFontSize(const Size& rFontSize) {
            mFontSize = rFontSize;
        }

        void SetTextPositionH(u8 val) {
            detail::SetHorizontalPosition(&mTextPosition, val);
        }

        void SetTextPositionV(u8 val) {
            detail::SetVerticalPosition(&mTextPosition, val);
        }

        u8 GetTextPositionH() const {
            return detail::GetHorizontalPosition(mTextPosition);
        }

        u8 GetTextPositionV() const {
            return detail::GetVerticalPosition(mTextPosition);
        }

        const Color8 GetTextColor(u32 type) const {
            return mTextColors[type];
        }

        u8 GetTextAlignment() const {
            return mBits.textAlignment;
        }

        char16_t* mTextBuf;
        Color8 mTextColors[2];
        const Font* mpFont;
        Size mFontSize;
        f32 mLineSpace;
        f32 mCharSpace;
        TagProcessorBase< char16_t >* mpTagProcessor;
        u16 mTextBufBytes;
        u16 mTextLen;
        u8 mTextPosition;
        union {
            struct {
                u8 bAllocFont : 1;
                u8 textAlignment : 2;
            } mBits;
            u8 mBitsu8;
        };
    };
}  // namespace Ui
