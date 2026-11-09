#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstring>
#include <nebula_geometry.hpp>

// Named matrices, vectors and floats that camera parameters refer to by name.
class CameraRegisterHolder : public NameObj {
public:
    CameraRegisterHolder(const char*);

    void declareMtxReg(const char*, MtxPtr);
    MtxPtr getMtx(const char*) const;
    void declareVecReg(const char*, const Vec3f*);
    const Vec3f* getVec(const char*) const;
    void declareFloatReg(const char*, const f32*);
    const char* getDummyVecRegName() const;

private:
    // Grows by doubling; entry 0 is the dummy that every unknown name gets.
    template < typename T >
    class Table {
    public:
        static const u32 cInitialCapacity = 64;

        Table() : mValues(new T[cInitialCapacity]), mNames(new const char*[cInitialCapacity]), mCount(0), mCapacity(cInitialCapacity) {
        }

        void declare(const char* pName, T value) {
            if (mCount == mCapacity) {
                mValues = MR::growArray(mValues, static_cast< s32 >(mCapacity));
                mNames = MR::growArray(mNames, static_cast< s32 >(mCapacity));
                mCapacity *= 2;
            }

            mValues[mCount] = value;
            mNames[mCount] = pName;
            mCount++;
        }

        T get(const char* pName) const {
            if (pName != nullptr) {
                for (u32 i = 0; i < mCount; i++) {
                    if (MR::isEqualString(mNames[i], pName)) {
                        return mValues[i];
                    }
                }
            }

            return mValues[0];
        }

    private:
        T* mValues;
        const char** mNames;
        u32 mCount;
        u32 mCapacity;
    };

    Table< MtxPtr > mMatrices;
    Table< const Vec3f* > mVectors;
    Table< const f32* > mFloats;
    Mtx34f mDummyMatrix;
    Vec3f mDummyVector;
    f32 mDummyFloat;
};
