#pragma once

#include "nw4r/lyt/resourceAccessor.h"
#include "nw4r/ut/LinkList.h"
#include <cstddef>

namespace nw4r {
    namespace lyt {
        class FontRefLink {
        public:
            FontRefLink();

            ut::Font* GetFont() const { 
                return mFont;
            }

            const char* GetFontName() const {
                return mFontName;
            }

            ut::LinkListNode mLink;
            char mFontName[128];
            ut::Font* mFont;
        };

        namespace detail {
            #if defined(__MWERKS__) || defined(TARGET_PC)
            typedef ut::LinkList<FontRefLink, offsetof(FontRefLink, mLink)>  FontRefList;
            #else
            typedef ut::LinkList<FontRefLink, 0> FontRefList;
            #endif
        };

        class ArcResourceAccessor : public ResourceAccessor {
        public:
            ArcResourceAccessor();
            
            virtual ~ArcResourceAccessor();
            virtual void* GetResource(ResType, const char *, u32 *);
            virtual ut::Font* GetFont(const char *);

            bool Attach(void *, const char *);

            ARCHandle mArcHandle;
            void* mArcBuf;
            detail::FontRefList mFontList;
            char mResRootDir[128];
        };
    };
};
