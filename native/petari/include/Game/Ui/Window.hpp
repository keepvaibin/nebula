#pragma once

#include "Game/Ui/UiCommon.hpp"
#include "Game/Ui/Pane.hpp"
#include "Game/Ui/UiTypes.hpp"
#include "Game/Util/Color.hpp"

namespace Ui {
    class Window : public Pane {
    public:
        struct Content {
            Color8 vtxColors[4];
            detail::TexCoordAry texCoordAry;
        };

        struct Frame {
            Frame() : textureFlip(0), pMaterial(nullptr) {
            }

            u8 textureFlip;
            Material* pMaterial;
        };

        static constexpr UiPaneKind cKind = UiPaneKind_Window;

        Window(Layout&, const UiPaneData*, const ResBlockSet&);

        virtual ~Window() override;
        virtual void DrawSelf(const DrawInfo&) override;
        virtual void AnimateSelf(u32) override;
        virtual const Color8 GetVtxColor(u32) const override;
        virtual void SetVtxColor(u32, Color8) override;
        virtual u8 GetVtxColorElement(u32) const override;
        virtual void SetVtxColorElement(u32, u8) override;
        virtual Material* FindMaterialByName(const char*, bool) override;
        virtual void UnbindAnimationSelf(AnimTransform*) override;
        virtual void SetAnimationEnable(AnimTransform*, bool, bool) override;
        virtual Material* GetContentMaterial() const;
        virtual Material* GetFrameMaterial(u32) const;
        virtual void DrawContent(const Vec2f&, const WindowFrameSize&, u8);
        virtual void DrawFrame(const Vec2f&, const Frame&, const WindowFrameSize&, u8);
        virtual void DrawFrame4(const Vec2f&, const Frame*, const WindowFrameSize&, u8);
        virtual void DrawFrame8(const Vec2f&, const Frame*, const WindowFrameSize&, u8);

        const WindowFrameSize GetFrameSize(u8 frameNum, const Frame* pFrames);

        InflationLRTB mContentInflation;
        Content mContent;
        Frame* mFrames;
        u8 mFrameNum;
    };
}  // namespace Ui
