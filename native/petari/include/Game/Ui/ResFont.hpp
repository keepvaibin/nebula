#pragma once

#include "Game/Ui/ResFontBase.hpp"

namespace Ui {
    class ResFont : public detail::ResFontBase {
    public:
        ResFont();
        virtual ~ResFont() override;

        // A font the importer cooked ("NFNT", fontResourceFormat.h); false for anything else.
        bool SetResource(void* pBuffer);
        void RemoveResource();
    };
}  // namespace Ui
