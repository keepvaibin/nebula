#include "Game/Util/BitArray.hpp"

namespace MR {
    BitArray::BitArray(int bitNum) : mArray(new u8[(bitNum + 7) / 8]()), mArraySize(bitNum) {
    }

    bool BitArray::isOn(int bitIdx) const {
        return (mArray[bitIdx / 8] & (1 << (bitIdx & 7))) != 0;
    }

    void BitArray::set(int bitIdx, bool isOn) {
        const u8 mask = static_cast< u8 >(1 << (bitIdx & 7));

        if (isOn) {
            mArray[bitIdx / 8] |= mask;
        } else {
            mArray[bitIdx / 8] &= static_cast< u8 >(~mask);
        }
    }
};  // namespace MR
