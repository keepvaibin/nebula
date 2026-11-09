#pragma once

#include "Game/Ui/UiCommon.hpp"
#include "Game/Ui/Pane.hpp"

namespace Ui {
    struct ResBlockSet;
    class TexMap;

    class Picture : public Pane {
    public:
        static constexpr UiPaneKind cKind = UiPaneKind_Picture;

        Picture(Layout&, const TexMap& texMap);
        Picture(Layout&, const UiPaneData*, const ResBlockSet&);

        virtual ~Picture() override;
        virtual void DrawSelf(const DrawInfo& rInfo) override;
        virtual const Color8 GetVtxColor(u32 idx) const override;
        virtual void SetVtxColor(u32 idx, Color8 color) override;
        virtual u8 GetVtxColorElement(u32 idx) const override;
        virtual void SetVtxColorElement(u32 idx, u8 value) override;
        virtual void Append(const TexMap& rTexMap);

        void Init(u8);
        void ReserveTexCoord(u8);
        void SetTexCoordNum(u8);

        Color8 mVtxColors[4];
        detail::TexCoordAry mTexCoordAry;
    };
}  // namespace Ui
