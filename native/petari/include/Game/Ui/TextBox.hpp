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

        virtual ~TextBox() override;
        virtual void DrawSelf(const DrawInfo& drawInfo) override;
        virtual const Color8 GetVtxColor(u32) const override;
        virtual void SetVtxColor(u32, Color8) override;
        virtual u8 GetVtxColorElement(u32) const override;
        virtual void SetVtxColorElement(u32, u8) override;
        virtual void AllocStringBuffer(u32);
        virtual void FreeStringBuffer();
        virtual u32 SetString(const char16_t*, u32 dstIdx = 0);
        virtual u32 SetString(const char16_t*, u32, u32);

        const Rect GetTextDrawRect(TextWriterBase< char16_t >*) const;
        void SetTextColor(Color8, Color8);
        void SetFont(const Font*);
        u32 GetStringBufferLength() const;
        u32 MakeDrawFlag() const;
        f32 GetTextMagH() const;
        f32 GetTextMagV() const;
        void SetTextColor(u32, Color8);
        const Rect GetTextDrawRect() const;
        const Rect GetTextDrawRect(const DrawInfo&) const;

        void Init(u32);
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
        u32 mTextBufBytes;
        u32 mTextLen;
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
