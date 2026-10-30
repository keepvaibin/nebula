#pragma once

#include <revolution/gx.h>
#include <revolution/mtx.h>
#include <revolution/os.h>

namespace nw4r {
    namespace db {
        namespace detail {
            inline void* DirectPrint_SetupFB(const GXRenderModeObj* rmode) {
                return nullptr;
            }
        };  // namespace detail
    };  // namespace db
};  // namespace nw4r
