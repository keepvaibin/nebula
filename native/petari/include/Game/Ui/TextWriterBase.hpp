#pragma once


#include "Game/Ui/CharWriter.hpp"
#include "Game/Ui/TagProcessorBase.hpp"
#include "Game/Ui/UiInlines.hpp"

namespace Ui {
    // A width limit that never cuts a line.
    constexpr f32 cWidthUnlimited = 3.40282347e+38F;

    template < typename CharT >
    class TextWriterBase : public CharWriter {
    public:
        typedef CharT CharType;
        typedef const CharType* StreamType;
        typedef TagProcessorBase< CharType > TagProcessor;
        typedef TextWriterBase< CharType > SelfType;

        enum PositionFlag {
            HORIZONTAL_ALIGN_LEFT = 0x0,
            HORIZONTAL_ALIGN_CENTER = 0x1,
            HORIZONTAL_ALIGN_RIGHT = 0x2,
            HORIZONTAL_ALIGN_MASK = 0x3,

            HORIZONTAL_ORIGIN_LEFT = 0x00,
            HORIZONTAL_ORIGIN_CENTER = 0x10,
            HORIZONTAL_ORIGIN_RIGHT = 0x20,
            HORIZONTAL_ORIGIN_MASK = 0x30,

            VERTICAL_ORIGIN_TOP = 0x000,
            VERTICAL_ORIGIN_MIDDLE = 0x100,
            VERTICAL_ORIGIN_BOTTOM = 0x200,
            VERTICAL_ORIGIN_BASELINE = 0x300,
            VERTICAL_ORIGIN_MASK = 0x300,
        };

        enum ContextFlag { CONTEXT_NO_CHAR_SPACE = 0x1 };

        static const u32 DEFAULT_DRAWFLAG = (HORIZONTAL_ALIGN_LEFT | HORIZONTAL_ORIGIN_LEFT | VERTICAL_ORIGIN_TOP);

        TextWriterBase();
        ~TextWriterBase();

        void SetWidthLimit(f32 limit) {
            mWidthLimit = limit;
        }

        f32 GetCharSpace() const {
            return mCharSpace;
        }

        void SetCharSpace(f32 space) {
            mCharSpace = space;
        }

        f32 GetLineSpace() const {
            return mLineSpace;
        }

        void SetLineSpace(f32 space) {
            mLineSpace = space;
        }

        int GetTabWidth() const {
            return mTabWidth;
        }

        u32 GetDrawFlag() const {
            return mDrawFlag;
        }

        void SetDrawFlag(u32 flag) {
            mDrawFlag = flag;
        }

        f32 GetLineHeight() const;

        void SetTagProcessor(TagProcessor* tagProcessor) {

            mTagProcessor = tagProcessor;
        }

        f32 CalcStringWidth(StreamType str, int length) const;
        void CalcStringRect(Rect* pRect, const CharT* pStr, int len) const;
        f32 Print(StreamType str, int length);
        f32 PrintImpl(StreamType, int);
        f32 CalcLineWidth(StreamType, int);
        bool CalcLineRectImpl(Rect*, StreamType*, int);
        // The width of the word the reader is at (up to a space, a tag or the end), each letter with its char space.
        f32 CalcWordWidth(CharStrmReader reader, StreamType end) const;
        // A line breaks at a space whose next word would pass the width limit, so a sentence with no line breaks of
        // its own (a mod's message or dialogue) wraps between words; a word longer than a line still breaks inside.
        bool IsWordBreak(const CharStrmReader& reader, StreamType end, f32 lineWidth, bool bCharSpace) const;
        void CalcStringRectImpl(Rect*, StreamType, int);
        f32 AdjustCursor(f32*, f32*, StreamType, int);

        bool IsDrawFlagSet(u32 mask, u32 flag) const {
            return (mDrawFlag & mask) == flag;
        }

        static TagProcessor mDefaultTagProcessor;

        f32 mWidthLimit;
        f32 mCharSpace;
        f32 mLineSpace;
        int mTabWidth;
        u32 mDrawFlag;
        TagProcessor* mTagProcessor;
    };
}  // namespace Ui
