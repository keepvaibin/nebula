#pragma once

#include "Game/Ui/Rect.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

namespace Ui {
    template < typename CharType >
    class TextWriterBase;

    template < typename CharType >
    struct PrintContext {
        TextWriterBase< CharType >* writer;
        const CharType* str;
        f32 xOrigin;
        f32 yOrigin;
        u32 flags;
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

        void ProcessLinefeed(ContextType*);
        void ProcessTab(ContextType*);
    };
}  // namespace Ui
