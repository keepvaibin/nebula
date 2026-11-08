#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

namespace MR {
    class BitArray {
    public:
        BitArray(int bitNum);

        bool isOn(int bitIdx) const;
        void set(int bitIdx, bool isOn);

        // In bits.
        int size() const {
            return mArraySize;
        }

    private:
        u8* mArray;
        s32 mArraySize;
    };
};  // namespace MR
