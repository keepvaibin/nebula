/* tt-shape: the game's types as a mod binds them (types 1.2, TY-1 to TY-3).
   - LUMA_SIZE of Pos3f by its own name and of Vec3<f32> by its alias Vec3f;
   - LUMA_FIELD of Pos3f.mMtx, a field two bases up (Pos3f : Rot3f : Mtx34f), an array whose type text is its element's;
   - Vec3<f32>.x and .z through the base Vec, and .y by the alias's name;
   - LUMA_FUNCTION of an inline in an anonymous namespace, by its file ("Game/Boss/OtaKing.cpp#makeTranslationMatrix"),
     called with a pointer and a reference: it writes the identity and the translation into mMtx[i][3].
   Facts: the sizes; m03, m13, m23 (the translation) and m00, m11, m22, m01 (the identity), in thousandths; untouched,
   that the bytes past mMtx kept their mark. No C library: floats become integers by a cast. */
#include "../loader/lt.h"

static int32_t pos_size, vec_size, mtx_at, x_at, y_at, z_at;
static const LumaFunction* make;

static const LumaImport imports[] = {
    LUMA_SIZE(pos_size, "Pos3f"),
    LUMA_SIZE(vec_size, "Vec3f"),
    LUMA_FIELD(mtx_at, "Pos3f.mMtx", "f32"),
    LUMA_FIELD(x_at, "Vec3<f32>.x", "f32"),
    LUMA_FIELD(y_at, "Vec3f.y", "f32"),
    LUMA_FIELD(z_at, "Vec3<f32>.z", "f32"),
    LUMA_FUNCTION(make, "Game/Boss/OtaKing.cpp#makeTranslationMatrix", "void(Pos3f *, const Vec3f &)"),
};

static long long milli(float f) {
    return (long long)(f * 1000.0f + (f < 0.0f ? -0.5f : 0.5f));
}

static float* at(unsigned char* base, int32_t offset, int index) {
    return (float*)(void*)(base + offset) + index;
}

static void run(void) {
    _Alignas(16) static unsigned char pos[256];
    _Alignas(16) static unsigned char vec[64];
    volatile unsigned char* mark = pos;
    lt_fact("pos_size", pos_size);
    lt_fact("vec_size", vec_size);
    lt_fact("mtx_at", mtx_at);
    lt_fact("xyz_at", x_at * 10000 + y_at * 100 + z_at);
    if (pos_size < 48 || pos_size > 200 || vec_size < 12 || vec_size > 64) {
        lt_say("tt-shape: a size out of range; nothing called");
        return;
    }
    for (int i = 0; i < 256; i++) mark[i] = 0x5A;
    *at(vec, x_at, 0) = 1.5f;
    *at(vec, y_at, 0) = -2.25f;
    *at(vec, z_at, 0) = 3.0f;
    void* pointer = pos;
    void* args[3] = {0, &pointer, vec};
    make->invoke(args, 0);
    lt_fact("m03", milli(*at(pos, mtx_at, 3)));
    lt_fact("m13", milli(*at(pos, mtx_at, 7)));
    lt_fact("m23", milli(*at(pos, mtx_at, 11)));
    lt_fact("m00", milli(*at(pos, mtx_at, 0)));
    lt_fact("m11", milli(*at(pos, mtx_at, 5)));
    lt_fact("m22", milli(*at(pos, mtx_at, 10)));
    lt_fact("m01", milli(*at(pos, mtx_at, 1)));
    int untouched = 1;
    for (int i = mtx_at + 48; i < 256; i++) untouched &= mark[i] == 0x5A;
    lt_fact("untouched", untouched);
}

LT_START(run())
LUMA_MOD(imports, start, 0);
