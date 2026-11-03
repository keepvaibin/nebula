#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

namespace MR {

    class BothDirPtrList;

    class BothDirPtrLink {
    public:
        BothDirPtrLink(void*);
        ~BothDirPtrLink();

        void* mValue;
        BothDirPtrList* mList;
        BothDirPtrLink* mPrevLink;
        BothDirPtrLink* mNextLink;
    };

    class BothDirPtrList {
    public:
        BothDirPtrList() {
            initiate();
        }

        BothDirPtrList(bool);
        ~BothDirPtrList();

        void initiate();
        void setFirst(BothDirPtrLink*);
        bool append(BothDirPtrLink*);
        bool remove(BothDirPtrLink*);

        BothDirPtrLink* mHead;
        BothDirPtrLink* mTail;
        u32 mCount;
    };

    template < class T >
    class BothDirLink : public BothDirPtrLink {
    public:
        BothDirLink(T* pValue) : BothDirPtrLink(pValue) {
        }

        ~BothDirLink() {
        }
    };

    template < class T >
    class BothDirList : public BothDirPtrList {
    public:
        class iterator {
        public:
            iterator(BothDirPtrLink* pLink) : mLink(pLink) {
            }

            T* operator->() const {
                return static_cast< T* >(mLink->mValue);
            }

            void operator++(int) {
                mLink = mLink->mNextLink;
            }

            bool isEnd() const {
                return mLink == nullptr;
            }

            BothDirPtrLink* mLink;
        };

        iterator begin() const {
            return iterator(mHead);
        }

        BothDirList() : BothDirPtrList() {
        }

        BothDirList(bool param1) : BothDirPtrList(param1) {
        }

        ~BothDirList() {
        }
    };
};  // namespace MR
