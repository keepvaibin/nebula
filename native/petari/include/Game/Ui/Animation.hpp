#pragma once

#include "Game/Ui/UiCommon.hpp"
#include "Game/Ui/Material.hpp"

namespace Ui {
    class Pane;
    class Material;
    class AnimationLink;
    class ResourceAccessor;
    class Group;
    class Layout;

    class AnimTransform {
    public:
        AnimTransform();

        virtual ~AnimTransform();
        virtual void SetResource(const UiAnimationHeader*, ResourceAccessor*) = 0;
        virtual void SetResource(const UiAnimationHeader*, ResourceAccessor*, u16) = 0;
        virtual void Bind(Pane*, bool) = 0;
        virtual void Bind(Material*) = 0;
        virtual void Animate(u32, Pane*) = 0;
        virtual void Animate(u32, Material*) = 0;

        u16 GetFrameSize() const;
        bool IsLoopData() const;

        UiLink< AnimTransform > mLink;
        const UiAnimationHeader* mpRes;
        f32 mFrame;
    };

    class AnimTransformBasic : public AnimTransform {
    public:
        AnimTransformBasic();

        virtual ~AnimTransformBasic() override;
        virtual void SetResource(const UiAnimationHeader*, ResourceAccessor*) override;
        virtual void SetResource(const UiAnimationHeader*, ResourceAccessor*, u16) override;
        virtual void Bind(Pane*, bool) override;
        virtual void Bind(Material*) override;
        virtual void Animate(u32, Pane*) override;
        virtual void Animate(u32, Material*) override;

        template < typename T >
        AnimationLink* Bind(T* target, AnimationLink* link, u16 idx);
        AnimationLink* FindUnbindLink(AnimationLink* link) const;
        void** mpFileResAry;
        AnimationLink* mAnimLinkAry;
        u16 mAnimLinkNum;
    };

    // A cooked layout animation (UiData.hpp), or none when the file is not one.
    class AnimResource {
    public:
        explicit AnimResource(const void* anmResBuf);

        const UiAnimationHeader* GetResourceBlock() const {
            return mpResBlock;
        }

        const UiAnimationHeader* mpResBlock;
    };

    namespace detail {
        AnimationLink* FindAnimationLink(AnimationList* pAnimList, AnimTransform* pAnimTrans);
        void UnbindAnimationLink(AnimationList* pAnimList, AnimTransform* pAnimTrans);
    };  // namespace detail
}  // namespace Ui
