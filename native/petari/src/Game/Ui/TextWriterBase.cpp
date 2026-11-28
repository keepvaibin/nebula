#include "Game/Ui/TextWriterBase.hpp"
#include "Game/Ui/UiInlines.hpp"

namespace Ui {
    template < typename CharT >
    typename TextWriterBase< CharT >::TagProcessor TextWriterBase< CharT >::mDefaultTagProcessor;
    template < typename CharType >
    TextWriterBase< CharType >::TextWriterBase()
        : CharWriter(), mWidthLimit(cWidthUnlimited), mCharSpace(0), mLineSpace(0), mTabWidth(4), mDrawFlag(DEFAULT_DRAWFLAG),
          mTagProcessor(&mDefaultTagProcessor) {
    }

    template < typename CharType >
    TextWriterBase< CharType >::~TextWriterBase() {
    }

    template < typename CharType >
    f32 TextWriterBase< CharType >::GetLineHeight() const {
        const Font* font = GetFont();
        const int linefeed = (font != NULL) ? font->GetLineFeed() : 0;
        return linefeed * GetScaleV() + mLineSpace;
    }

    template < typename CharType >
    f32 TextWriterBase< CharType >::CalcStringWidth(StreamType str, int length) const {
        Rect rect;

        CalcStringRect(&rect, str, length);

        return rect.GetWidth();
    }

    template < typename CharType >
    f32 TextWriterBase< CharType >::Print(StreamType str, int length) {
        TextWriterBase< CharType > myCopy = *this;
        f32 width = myCopy.PrintImpl(str, length);
        EndRun();
        SetCursor(myCopy.GetCursorX(), myCopy.GetCursorY());
        return width;
    }

    template < typename CharType >
    f32 TextWriterBase< CharType >::CalcLineWidth(StreamType str, int length) {
        Rect rect;
        TextWriterBase< CharType > myCopy = *this;
        myCopy.SetCursor(0, 0);
        myCopy.CalcLineRectImpl(&rect, &str, length);
        return rect.GetWidth();
    }

    template < typename CharType >
    bool TextWriterBase< CharType >::CalcLineRectImpl(Rect* pRect, StreamType* pStr, int length) {
        const StreamType str = *pStr;
        const StreamType end = str + length;
        const bool bUseLimit = (mWidthLimit < cWidthUnlimited);
        PrintContext< CharType > context = {this, str, 0, 0, 0, nullptr, TextRunPos(), TextRunPos()};
        f32 x = 0;
        bool bCharSpace = false;
        bool bOverLimit = false;
        CharCode prevCode = 0;  // the character before on this line, for the font's kerning (0: none)
        StreamType prevStreamPos = NULL;
        Rect prevRect;
        CharStrmReader reader = GetFont()->GetCharStrmReader();

        pRect->left = 0;
        pRect->right = 0;
        pRect->top = Min(0.0f, GetLineHeight());
        pRect->bottom = Max(0.0f, GetLineHeight());
        prevRect = *pRect;

        reader.Set(str);
        prevStreamPos = NULL;

        for (CharCode code = reader.Next(); reinterpret_cast< StreamType >(reader.GetCurrentPos()) <= end;) {
            if (code < ' ') {
                typename TagProcessor::Operation operation;
                Rect rect(x, 0, 0, 0);
                prevCode = 0;

                context.str = reinterpret_cast< StreamType >(reader.GetCurrentPos());
                context.flags = 0;
                context.flags |= bCharSpace ? 0 : CONTEXT_NO_CHAR_SPACE;
                SetCursorX(x);

                if (bUseLimit && (code != '\n') && (prevStreamPos != NULL)) {
                    PrintContext< CharType > context2 = context;
                    TextWriterBase< CharType > myCopy = *this;
                    Rect rect;

                    context2.writer = &myCopy;
                    operation = mTagProcessor->CalcRect(&rect, code, &context2);

                    if ((rect.GetWidth() > 0.0f) && (myCopy.GetCursorX() - context.xOrigin > mWidthLimit)) {
                        bOverLimit = true;
                        code = '\n';
                        reader.Set(prevStreamPos);
                        continue;
                    }
                }

                operation = mTagProcessor->CalcRect(&rect, code, &context);
                reader.Set(context.str);

                pRect->left = Min(pRect->left, rect.left);
                pRect->top = Min(pRect->top, rect.top);
                pRect->right = Max(pRect->right, rect.right);
                pRect->bottom = Max(pRect->bottom, rect.bottom);
                x = GetCursorX();

                if (operation == TagProcessor::OPERATION_END_DRAW) {
                    *pStr += length;
                    return false;
                } else if (operation == TagProcessor::OPERATION_NO_CHAR_SPACE) {
                    bCharSpace = false;
                } else if (operation == TagProcessor::OPERATION_CHAR_SPACE) {
                    bCharSpace = true;
                } else if (operation == TagProcessor::OPERATION_NEXT_LINE) {
                    break;
                }
            } else {
                // The space is used up by the line break.
                if (bUseLimit && code == ' ' && prevStreamPos != NULL && IsWordBreak(reader, end, x, bCharSpace)) {
                    bOverLimit = true;
                    code = '\n';
                    continue;
                }

                f32 dx = 0.0f;

                if (bCharSpace) {
                    dx += GetCharSpace();
                }

                if (IsWidthFixed()) {
                    dx += GetFixedWidth();
                } else {
                    const int kern = prevCode != 0 ? GetFont()->GetKerning(prevCode, code) : 0;
                    dx += (GetFont()->GetCharWidth(code) + kern) * GetScaleH();
                }

                if (bUseLimit && (prevStreamPos != NULL)) {
                    if (x + dx > mWidthLimit) {
                        bOverLimit = true;
                        code = '\n';
                        reader.Set(prevStreamPos);
                        continue;
                    }
                }

                x += dx;
                pRect->left = Min(pRect->left, x);
                pRect->right = Max(pRect->right, x);
                bCharSpace = true;
                prevCode = code;
            }

            if (bUseLimit) {
                prevStreamPos = reinterpret_cast< StreamType >(reader.GetCurrentPos());
            }

            code = reader.Next();
        }

        *pStr = reinterpret_cast< StreamType >(reader.GetCurrentPos());

        return bOverLimit;
    }

    template < typename CharType >
    f32 TextWriterBase< CharType >::CalcWordWidth(CharStrmReader reader, StreamType end) const {
        f32 width = 0.0f;
        CharCode prevCode = 0;
        for (CharCode code = reader.Next(); reinterpret_cast< StreamType >(reader.GetCurrentPos()) <= end && code > ' ';
             code = reader.Next()) {
            const int kern = prevCode != 0 ? GetFont()->GetKerning(prevCode, code) : 0;
            width += GetCharSpace() + (IsWidthFixed() ? GetFixedWidth() : (GetFont()->GetCharWidth(code) + kern) * GetScaleH());
            prevCode = code;
        }
        return width;
    }

    template < typename CharType >
    bool TextWriterBase< CharType >::IsWordBreak(const CharStrmReader& reader, StreamType end, f32 lineWidth,
                                                 bool bCharSpace) const {
        const f32 word = CalcWordWidth(reader, end);
        if (word <= 0.0f || word > mWidthLimit) {
            return false;
        }
        const f32 space = (bCharSpace ? GetCharSpace() : 0.0f) + (IsWidthFixed() ? GetFixedWidth() : GetFont()->GetCharWidth(' ') * GetScaleH());
        return lineWidth + space + word > mWidthLimit;
    }

    template < typename CharType >
    void TextWriterBase< CharType >::CalcStringRect(Rect* pRect, StreamType str, int length) const {
        TextWriterBase< CharType > myCopy = *this;
        myCopy.CalcStringRectImpl(pRect, str, length);
    }

    template < typename CharType >
    void TextWriterBase< CharType >::CalcStringRectImpl(Rect* pRect, StreamType str, int length) {
        const StreamType end = str + length;
        int remain = length;
        StreamType pos = str;

        pRect->left = 0;
        pRect->right = 0;
        pRect->top = 0;
        pRect->bottom = 0;

        SetCursor(0, 0);

        do {
            Rect rect;
            CalcLineRectImpl(&rect, &pos, remain);
            remain = (end - pos);

            pRect->left = Min(pRect->left, rect.left);
            pRect->top = Min(pRect->top, rect.top);
            pRect->right = Max(pRect->right, rect.right);
            pRect->bottom = Max(pRect->bottom, rect.bottom);
        } while (remain > 0);
    }

    template < typename CharType >
    f32 TextWriterBase< CharType >::AdjustCursor(f32* xOrigin, f32* yOrigin, StreamType str, int length) {
        f32 textWidth = 0;
        f32 textHeight = 0;

        if (!IsDrawFlagSet((HORIZONTAL_ALIGN_MASK | HORIZONTAL_ORIGIN_MASK | VERTICAL_ORIGIN_MASK),
                           (HORIZONTAL_ALIGN_LEFT | HORIZONTAL_ORIGIN_LEFT | VERTICAL_ORIGIN_BASELINE)) &&
            !IsDrawFlagSet((HORIZONTAL_ALIGN_MASK | HORIZONTAL_ORIGIN_MASK | VERTICAL_ORIGIN_MASK),
                           (HORIZONTAL_ALIGN_LEFT | HORIZONTAL_ORIGIN_LEFT | VERTICAL_ORIGIN_TOP))) {
            Rect textRect;
            CalcStringRect(&textRect, str, length);
            textWidth = textRect.left + textRect.right;
            textHeight = textRect.top + textRect.bottom;

            if (textWidth > mWidthLimit) {
                textWidth = mWidthLimit;
            }
        }

        if (IsDrawFlagSet(HORIZONTAL_ORIGIN_MASK, HORIZONTAL_ORIGIN_CENTER)) {
            *xOrigin -= textWidth / 2;
        } else if (IsDrawFlagSet(HORIZONTAL_ORIGIN_MASK, HORIZONTAL_ORIGIN_RIGHT)) {
            *xOrigin -= textWidth;
        }

        if (IsDrawFlagSet(VERTICAL_ORIGIN_MASK, VERTICAL_ORIGIN_MIDDLE)) {
            *yOrigin -= textHeight / 2;
        } else if (IsDrawFlagSet(VERTICAL_ORIGIN_MASK, VERTICAL_ORIGIN_BOTTOM)) {
            *yOrigin -= textHeight;
        }

        if (IsDrawFlagSet(HORIZONTAL_ALIGN_MASK, HORIZONTAL_ALIGN_CENTER)) {
            const f32 width = CalcLineWidth(str, length);
            const f32 offset = (textWidth - width) / 2;
            SetCursorX(*xOrigin + offset);
        } else if (IsDrawFlagSet(HORIZONTAL_ALIGN_MASK, HORIZONTAL_ALIGN_RIGHT)) {
            const f32 width = CalcLineWidth(str, length);
            const f32 offset = textWidth - width;
            SetCursorX(*xOrigin + offset);
        } else {
            SetCursorX(*xOrigin);
        }

        if (IsDrawFlagSet(VERTICAL_ORIGIN_MASK, VERTICAL_ORIGIN_BASELINE)) {
            SetCursorY(*yOrigin);
        } else {
            SetCursorY(*yOrigin + GetFontAscent());
        }

        return textWidth;
    }

    namespace {
        // An item of a text in runs given to the processor: a tag as its record, a code under ' ' as itself. pRect: measured.
        template < typename CharType >
        typename TagProcessorBase< CharType >::Operation processRunItem(TagProcessorBase< CharType >* pProcessor, Rect* pRect, bool isTag,
                                                                        u16 code, const MessageTagRecord* pTag, PrintContext< CharType >* pContext) {
            if (pRect != nullptr) {
                return isTag ? pProcessor->CalcRectRunTag(pRect, *pTag, pContext) : pProcessor->CalcRect(pRect, code, pContext);
            }
            return isTag ? pProcessor->ProcessRunTag(*pTag, pContext) : pProcessor->Process(code, pContext);
        }
    }  // namespace

    template < typename CharType >
    f32 TextWriterBase< CharType >::PrintRuns(const TextRuns& rText, const TextRunPos& rBegin, const TextRunPos& rEnd) {
        TextWriterBase< CharType > myCopy = *this;
        f32 width = myCopy.PrintRunsImpl(rText, rBegin, rEnd);
        EndRun();
        SetCursor(myCopy.GetCursorX(), myCopy.GetCursorY());
        return width;
    }

    template < typename CharType >
    f32 TextWriterBase< CharType >::CalcRunsWidth(const TextRuns& rText, const TextRunPos& rBegin, const TextRunPos& rEnd) const {
        Rect rect;
        CalcRunsRect(&rect, rText, rBegin, rEnd);
        return rect.GetWidth();
    }

    template < typename CharType >
    f32 TextWriterBase< CharType >::CalcRunsLineWidth(const TextRuns& rText, const TextRunPos& rBegin, const TextRunPos& rEnd) {
        Rect rect;
        TextWriterBase< CharType > myCopy = *this;
        TextRunPos pos = rBegin;
        myCopy.SetCursor(0, 0);
        myCopy.CalcRunsLineRectImpl(&rect, rText, &pos, rEnd);
        return rect.GetWidth();
    }

    template < typename CharType >
    bool TextWriterBase< CharType >::CalcRunsLineRectImpl(Rect* pRect, const TextRuns& rText, TextRunPos* pPos, const TextRunPos& rEnd) {
        const bool bUseLimit = (mWidthLimit < cWidthUnlimited);
        TextRunReader reader(rText, rEnd);
        reader.Set(*pPos);
        PrintContext< CharType > context = {this, nullptr, 0, 0, 0, &rText, reader.GetPos(), reader.GetEnd()};
        f32 x = 0;
        bool bCharSpace = false;
        bool bOverLimit = false;
        CharCode prevCode = 0;  // the character before on this line, for the font's kerning (0: none)
        bool bHasPrev = false;  // prevStreamPos is set (in version 1: not NULL)
        TextRunPos prevStreamPos = reader.GetPos();

        pRect->left = 0;
        pRect->right = 0;
        pRect->top = Min(0.0f, GetLineHeight());
        pRect->bottom = Max(0.0f, GetLineHeight());

        u16 code = 0;
        const MessageTagRecord* pTag = nullptr;
        for (typename TextRunReader::Item item = reader.Next(&code, &pTag); item != TextRunReader::Item_End;) {
            const bool isTag = item == TextRunReader::Item_Tag;
            if (isTag || code < ' ') {
                typename TagProcessor::Operation operation;
                Rect rect(x, 0, 0, 0);
                prevCode = 0;

                context.runPos = reader.GetPos();
                context.flags = 0;
                context.flags |= bCharSpace ? 0 : CONTEXT_NO_CHAR_SPACE;
                SetCursorX(x);

                if (bUseLimit && (isTag || code != '\n') && bHasPrev) {
                    PrintContext< CharType > context2 = context;
                    TextWriterBase< CharType > myCopy = *this;
                    Rect rect;

                    context2.writer = &myCopy;
                    operation = processRunItem(mTagProcessor, &rect, isTag, code, pTag, &context2);

                    if ((rect.GetWidth() > 0.0f) && (myCopy.GetCursorX() - context.xOrigin > mWidthLimit)) {
                        bOverLimit = true;
                        item = TextRunReader::Item_Char;
                        code = '\n';
                        reader.Set(prevStreamPos);
                        continue;
                    }
                }

                // A processor moves no place in runs (in version 1 it moves `str` past the tag it read).
                operation = processRunItem(mTagProcessor, &rect, isTag, code, pTag, &context);

                pRect->left = Min(pRect->left, rect.left);
                pRect->top = Min(pRect->top, rect.top);
                pRect->right = Max(pRect->right, rect.right);
                pRect->bottom = Max(pRect->bottom, rect.bottom);
                x = GetCursorX();

                if (operation == TagProcessor::OPERATION_END_DRAW) {
                    *pPos = reader.GetEnd();
                    return false;
                } else if (operation == TagProcessor::OPERATION_NO_CHAR_SPACE) {
                    bCharSpace = false;
                } else if (operation == TagProcessor::OPERATION_CHAR_SPACE) {
                    bCharSpace = true;
                } else if (operation == TagProcessor::OPERATION_NEXT_LINE) {
                    break;
                }
            } else {
                // The space is used up by the line break.
                if (bUseLimit && code == ' ' && bHasPrev && IsRunsWordBreak(reader, x, bCharSpace)) {
                    bOverLimit = true;
                    code = '\n';
                    continue;
                }

                f32 dx = 0.0f;

                if (bCharSpace) {
                    dx += GetCharSpace();
                }

                if (IsWidthFixed()) {
                    dx += GetFixedWidth();
                } else {
                    const int kern = prevCode != 0 ? GetFont()->GetKerning(prevCode, code) : 0;
                    dx += (GetFont()->GetCharWidth(code) + kern) * GetScaleH();
                }

                if (bUseLimit && bHasPrev) {
                    if (x + dx > mWidthLimit) {
                        bOverLimit = true;
                        code = '\n';
                        reader.Set(prevStreamPos);
                        continue;
                    }
                }

                x += dx;
                pRect->left = Min(pRect->left, x);
                pRect->right = Max(pRect->right, x);
                bCharSpace = true;
                prevCode = code;
            }

            if (bUseLimit) {
                prevStreamPos = reader.GetPos();
                bHasPrev = true;
            }

            item = reader.Next(&code, &pTag);
        }

        *pPos = reader.GetPos();

        return bOverLimit;
    }

    template < typename CharType >
    f32 TextWriterBase< CharType >::CalcRunsWordWidth(TextRunReader reader) const {
        f32 width = 0.0f;
        CharCode prevCode = 0;
        u16 code = 0;
        const MessageTagRecord* pTag = nullptr;
        while (reader.Next(&code, &pTag) == TextRunReader::Item_Char && code > ' ') {
            const int kern = prevCode != 0 ? GetFont()->GetKerning(prevCode, code) : 0;
            width += GetCharSpace() + (IsWidthFixed() ? GetFixedWidth() : (GetFont()->GetCharWidth(code) + kern) * GetScaleH());
            prevCode = code;
        }
        return width;
    }

    template < typename CharType >
    bool TextWriterBase< CharType >::IsRunsWordBreak(const TextRunReader& rReader, f32 lineWidth, bool bCharSpace) const {
        const f32 word = CalcRunsWordWidth(rReader);
        if (word <= 0.0f || word > mWidthLimit) {
            return false;
        }
        const f32 space = (bCharSpace ? GetCharSpace() : 0.0f) + (IsWidthFixed() ? GetFixedWidth() : GetFont()->GetCharWidth(' ') * GetScaleH());
        return lineWidth + space + word > mWidthLimit;
    }

    template < typename CharType >
    void TextWriterBase< CharType >::CalcRunsRect(Rect* pRect, const TextRuns& rText, const TextRunPos& rBegin, const TextRunPos& rEnd) const {
        TextWriterBase< CharType > myCopy = *this;
        myCopy.CalcRunsRectImpl(pRect, rText, rBegin, rEnd);
    }

    template < typename CharType >
    void TextWriterBase< CharType >::CalcRunsRectImpl(Rect* pRect, const TextRuns& rText, const TextRunPos& rBegin, const TextRunPos& rEnd) {
        const TextRunPos end = TextRunReader::normal(rText, rEnd);
        TextRunPos pos = TextRunReader::normal(rText, rBegin);

        pRect->left = 0;
        pRect->right = 0;
        pRect->top = 0;
        pRect->bottom = 0;

        SetCursor(0, 0);

        do {
            Rect rect;
            CalcRunsLineRectImpl(&rect, rText, &pos, end);

            pRect->left = Min(pRect->left, rect.left);
            pRect->top = Min(pRect->top, rect.top);
            pRect->right = Max(pRect->right, rect.right);
            pRect->bottom = Max(pRect->bottom, rect.bottom);
        } while (pos < end);
    }

    template < typename CharType >
    f32 TextWriterBase< CharType >::AdjustCursorRuns(f32* xOrigin, f32* yOrigin, const TextRuns& rText, const TextRunPos& rBegin,
                                                     const TextRunPos& rEnd) {
        f32 textWidth = 0;
        f32 textHeight = 0;

        if (!IsDrawFlagSet((HORIZONTAL_ALIGN_MASK | HORIZONTAL_ORIGIN_MASK | VERTICAL_ORIGIN_MASK),
                           (HORIZONTAL_ALIGN_LEFT | HORIZONTAL_ORIGIN_LEFT | VERTICAL_ORIGIN_BASELINE)) &&
            !IsDrawFlagSet((HORIZONTAL_ALIGN_MASK | HORIZONTAL_ORIGIN_MASK | VERTICAL_ORIGIN_MASK),
                           (HORIZONTAL_ALIGN_LEFT | HORIZONTAL_ORIGIN_LEFT | VERTICAL_ORIGIN_TOP))) {
            Rect textRect;
            CalcRunsRect(&textRect, rText, rBegin, rEnd);
            textWidth = textRect.left + textRect.right;
            textHeight = textRect.top + textRect.bottom;

            if (textWidth > mWidthLimit) {
                textWidth = mWidthLimit;
            }
        }

        if (IsDrawFlagSet(HORIZONTAL_ORIGIN_MASK, HORIZONTAL_ORIGIN_CENTER)) {
            *xOrigin -= textWidth / 2;
        } else if (IsDrawFlagSet(HORIZONTAL_ORIGIN_MASK, HORIZONTAL_ORIGIN_RIGHT)) {
            *xOrigin -= textWidth;
        }

        if (IsDrawFlagSet(VERTICAL_ORIGIN_MASK, VERTICAL_ORIGIN_MIDDLE)) {
            *yOrigin -= textHeight / 2;
        } else if (IsDrawFlagSet(VERTICAL_ORIGIN_MASK, VERTICAL_ORIGIN_BOTTOM)) {
            *yOrigin -= textHeight;
        }

        if (IsDrawFlagSet(HORIZONTAL_ALIGN_MASK, HORIZONTAL_ALIGN_CENTER)) {
            const f32 width = CalcRunsLineWidth(rText, rBegin, rEnd);
            const f32 offset = (textWidth - width) / 2;
            SetCursorX(*xOrigin + offset);
        } else if (IsDrawFlagSet(HORIZONTAL_ALIGN_MASK, HORIZONTAL_ALIGN_RIGHT)) {
            const f32 width = CalcRunsLineWidth(rText, rBegin, rEnd);
            const f32 offset = textWidth - width;
            SetCursorX(*xOrigin + offset);
        } else {
            SetCursorX(*xOrigin);
        }

        if (IsDrawFlagSet(VERTICAL_ORIGIN_MASK, VERTICAL_ORIGIN_BASELINE)) {
            SetCursorY(*yOrigin);
        } else {
            SetCursorY(*yOrigin + GetFontAscent());
        }

        return textWidth;
    }

    template < typename CharType >
    f32 TextWriterBase< CharType >::PrintRunsImpl(const TextRuns& rText, const TextRunPos& rBegin, const TextRunPos& rEnd) {
        f32 xOrigin = GetCursorX();
        f32 yOrigin = GetCursorY();
        const bool bUseLimit = (mWidthLimit < cWidthUnlimited);
        const f32 orgCursorY = yOrigin;
        f32 yCursorAdj = 0.0f;
        f32 textWidth = 0;
        bool bCharSpace = false;
        CharCode prevCode = 0;  // the character before on this line, for the font's kerning (0: none)
        TextRunReader reader(rText, rEnd);
        reader.Set(rBegin);
        const TextRunPos begin = reader.GetPos();
        TextRunPos prevStreamPos = begin;
        TextRunPos prevNewLinePos = begin;

        {
            textWidth = AdjustCursorRuns(&xOrigin, &yOrigin, rText, begin, reader.GetEnd());
            yCursorAdj = orgCursorY - GetCursorY();
        }

        PrintContext< CharType > context = {this, nullptr, xOrigin, yOrigin, 0, &rText, begin, reader.GetEnd()};

        // The processor's own fade (CustomTagProcessor's): any other processor leaves the writer's alpha alone.
        const u8 alpha = GetAlpha();
        mTagProcessor->BeginPrintRuns(&rText);

        u16 code = 0;
        const MessageTagRecord* pTag = nullptr;
        for (typename TextRunReader::Item item = reader.Next(&code, &pTag); item != TextRunReader::Item_End;) {
            const bool isTag = item == TextRunReader::Item_Tag;
            if (isTag || code < ' ') {
                typename TagProcessor::Operation operation;
                context.runPos = reader.GetPos();
                context.flags = 0;
                context.flags |= bCharSpace ? 0 : CONTEXT_NO_CHAR_SPACE;
                prevCode = 0;

                if (bUseLimit && (isTag || code != '\n') && (prevStreamPos != prevNewLinePos)) {
                    PrintContext< CharType > context2 = context;
                    TextWriterBase< CharType > myCopy = *this;
                    Rect rect;

                    context2.writer = &myCopy;
                    operation = processRunItem(mTagProcessor, &rect, isTag, code, pTag, &context2);

                    if ((rect.GetWidth() > 0.0f) && (myCopy.GetCursorX() - context.xOrigin > mWidthLimit)) {
                        item = TextRunReader::Item_Char;
                        code = '\n';
                        reader.Set(prevStreamPos);
                        continue;
                    }
                }

                operation = processRunItem(mTagProcessor, static_cast< Rect* >(nullptr), isTag, code, pTag, &context);

                if (operation == TagProcessor::OPERATION_NEXT_LINE) {
                    if (IsDrawFlagSet(HORIZONTAL_ALIGN_MASK, HORIZONTAL_ALIGN_CENTER)) {
                        const f32 width = CalcRunsLineWidth(rText, context.runPos, reader.GetEnd());
                        const f32 offset = (textWidth - width) / 2;
                        SetCursorX(context.xOrigin + offset);
                    } else if (IsDrawFlagSet(HORIZONTAL_ALIGN_MASK, HORIZONTAL_ALIGN_RIGHT)) {
                        const f32 width = CalcRunsLineWidth(rText, context.runPos, reader.GetEnd());
                        const f32 offset = textWidth - width;
                        SetCursorX(context.xOrigin + offset);
                    } else {
                        const f32 width = GetCursorX() - context.xOrigin;
                        textWidth = Max(textWidth, width);

                        SetCursorX(context.xOrigin);
                    }

                    if (bUseLimit) {
                        prevNewLinePos = reader.GetPos();
                    }
                    bCharSpace = false;
                } else if (operation == TagProcessor::OPERATION_NO_CHAR_SPACE) {
                    bCharSpace = false;
                } else if (operation == TagProcessor::OPERATION_CHAR_SPACE) {
                    bCharSpace = true;
                } else if (operation == TagProcessor::OPERATION_END_DRAW) {
                    break;
                }
            } else {
                if (u8 charAlpha = 0; mTagProcessor->NextCharAlpha(&charAlpha)) {
                    SetAlpha(charAlpha);
                }

                const f32 baseY = GetCursorY();

                // The space is used up by the line break (CalcRunsLineRectImpl breaks the line there too).
                if (bUseLimit && code == ' ' && (prevStreamPos != prevNewLinePos) && IsRunsWordBreak(reader, GetCursorX() - xOrigin, bCharSpace)) {
                    code = '\n';
                    continue;
                }

                // The font's kerning before this character (CalcRunsLineRectImpl measures it the same).
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
                prevStreamPos = reader.GetPos();
            }

            item = reader.Next(&code, &pTag);
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

        SetAlpha(alpha);

        return textWidth;
    }

    template class TextWriterBase< char16_t >;
}  // namespace Ui
