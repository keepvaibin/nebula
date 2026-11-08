#include "Game/Util/HashUtil.hpp"

namespace {
    int lowerCase(int c) {
        return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
    }
}  // namespace

namespace MR {
    u32 getHashCode(const char* pStr) {
        u32 hash;

        for (hash = 0; *pStr != '\0'; pStr++) {
            hash = static_cast< s8 >(*pStr) + hash * 31;  // cooked tables hold these: a byte above 127 counts negative
        }

        return hash;
    }

    u32 getHashCodeLower(const char* pStr) {
        u32 hash;

        for (hash = 0; *pStr != '\0'; pStr++) {
            hash = lowerCase(static_cast< s8 >(*pStr)) + hash * 31;
        }

        return hash;
    }
};  // namespace MR
