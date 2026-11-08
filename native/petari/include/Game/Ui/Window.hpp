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

        virtual ~Window();
        virtual void DrawSelf(const DrawInfo&);
        virtual void AnimateSelf(u32);
        virtual const Color8 GetVtxColor(u32) const;
        virtual void SetVtxColor(u32, Color8);
        virtual u8 GetVtxColorElement(u32) const;
        virtual void SetVtxColorElement(u32, u8);
        virtual Material* FindMaterialByName(const char*, bool);
        virtual void UnbindAnimationSelf(AnimTransform*);
        virtual void SetAnimationEnable(AnimTransform*, bool, bool);
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
