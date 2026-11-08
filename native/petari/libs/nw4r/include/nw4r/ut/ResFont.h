#pragma once

#include "nw4r/ut/ResFontBase.h"

namespace nw4r {
    namespace ut {
        class ResFont : public detail::ResFontBase {
        public:
            ResFont();
            virtual ~ResFont();

            // A font the importer cooked ("NFNT", fontResourceFormat.h); false for anything else.
            bool SetResource(void* pBuffer);
            void RemoveResource();
        };
    };  // namespace ut
};  // namespace nw4r
