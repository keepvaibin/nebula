#pragma once

#include "nw4r/lyt/common.h"
#include "nw4r/lyt/material.h"
#include "nw4r/ut/LinkList.h"

namespace nw4r {
    namespace lyt {
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
            const UiAnimationHeader* GetAnimResource() const {
                return mpRes;
            }

            ut::LinkListNode mLink;
            const UiAnimationHeader* mpRes;
            f32 mFrame;
        };

        class AnimTransformBasic : public AnimTransform {
        public:
            AnimTransformBasic();

            virtual ~AnimTransformBasic();
            virtual void SetResource(const UiAnimationHeader*, ResourceAccessor*);
            virtual void SetResource(const UiAnimationHeader*, ResourceAccessor*, u16);
            virtual void Bind(Pane*, bool);
            virtual void Bind(Material*);
            virtual void Animate(u32, Pane*);
            virtual void Animate(u32, Material*);

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
    };  // namespace lyt
};  // namespace nw4r
