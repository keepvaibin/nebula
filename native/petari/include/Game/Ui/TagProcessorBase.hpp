#pragma once

#include "Game/Ui/Rect.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>
#include "Game/Util/TextRuns.hpp"

namespace Ui {
    template < typename CharType >
    class TextWriterBase;

    // `str`: the text of version 1 at the code or tag met. For a text in runs (the writer's *Runs functions) `str` is
    // null and `runs` the text, `runPos` the place after the item met and `runEnd` the end of the print.
    template < typename CharType >
    struct PrintContext {
        TextWriterBase< CharType >* writer;
        const CharType* str;
        f32 xOrigin;
        f32 yOrigin;
        u32 flags;
        const TextRuns* runs;
        TextRunPos runPos;
        TextRunPos runEnd;
    };

    template < typename CharT >
    class TagProcessorBase {
    public:
        typedef CharT CharType;
        typedef PrintContext< CharType > ContextType;

        enum Operation {
            OPERATION_DEFAULT,
            OPERATION_NO_CHAR_SPACE,
            OPERATION_CHAR_SPACE,
            OPERATION_NEXT_LINE,
            OPERATION_END_DRAW,
        };

        TagProcessorBase();

        virtual ~TagProcessorBase();
        virtual Operation Process(u16, ContextType*);
        virtual Operation CalcRect(Rect*, u16, ContextType*);
        // A print starts over `str` (TextWriterBase::PrintImpl): a processor that fades characters in starts its count.
        virtual void BeginPrint(const CharType*) {}
        // The alpha of the next character printed, when this processor sets one (and counts the character); false: the
        // writer's own alpha.
        virtual bool NextCharAlpha(u8*) { return false; }
        // A tag of a text in runs, as its typed record: the writer meets it where the text has it (context->runPos is the
        // place after it). A processor that knows no tags skips it.
        virtual Operation ProcessRunTag(const MessageTagRecord&, ContextType*) { return OPERATION_DEFAULT; }
        virtual Operation CalcRectRunTag(Rect*, const MessageTagRecord&, ContextType*) { return OPERATION_DEFAULT; }
        // A print starts over a text in runs (TextWriterBase::PrintRunsImpl).
        virtual void BeginPrintRuns(const TextRuns*) {}

        void ProcessLinefeed(ContextType*);
        void ProcessTab(ContextType*);
    };
}  // namespace Ui
