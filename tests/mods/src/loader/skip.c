/* lt-skip: loader contract 4, Z-2. A before on CameraLocalUtil::getNormalizedOr Vec3f(const Vec3f &, const Vec3f &)
   that skips without writing the result when the first vector's x is 777: the caller gets 0,0,0, never stale bytes.
   Any other call runs as the game's. A parameter of reference type: args[n] is the object's address (luma.h). */
#include "lt.h"

typedef struct {
    float x, y, z;
} V3;

static int32_t before(LumaCall* call, void* user) {
    (void)user;
    const V3* first = (const V3*)call->args[1];
    return first != 0 && first->x == 777.0f ? 1 : 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("CameraLocalUtil::getNormalizedOr", "Vec3f(const Vec3f &, const Vec3f &)", LUMA_BEFORE, before),
};

LT_START((void)0)
LUMA_MOD(imports, start, 0);
