#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

namespace Ui {
    class Font;

    typedef u32 ResType;

    class ResourceAccessor {
    public:
        ResourceAccessor();

        virtual ~ResourceAccessor();
        virtual void* GetResource(ResType, const char*, u32* = 0) = 0;
        virtual Font* GetFont(const char*);
    };
}  // namespace Ui
