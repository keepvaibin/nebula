/* fv-mirror (M-056): the game's mirror reflects the mod's main camera. Run steps call MR::getRandom(0, 2475 + k) and
   the hook gives 1; the player stands 150 above the twin-falls lake mirror (the stage's first SimpleMirrorObj) of
   HeavenlyBeachGalaxy scenario 2, in its MirrorArea.
   k 1: "in_area" (MR::isPlayerInAreaObj("MirrorArea")), "mirror" (MR::isExistMirrorCamera), then main_eye 600 to the
        player's side and 300 up, looking at him ("main").
   k 2: the mirror's eye (from MR::getMirrorCameraViewMtx) against the main camera's eye E: they are mirror images when
        |mirror - E|^2 is (2 d)^2, d = MR::getDistanceToMirror(E) ("gap_milli": 1000 times the one over the other), and
        their middle is on the mirror ("mid": MR::getDistanceToMirror of it, rounded). Then main_eye null ("back").
   k 3: the same against the game's camera (MR::getCamPos): "back_gap_milli", "back_mid". */
#include "../loader/lt.h"
#include <luma_draw_view.h>

typedef struct Vec3 {
    float x, y, z;
} Vec3;

static const LumaDrawViewService* view;
static const LumaFunction* get_player_pos; /* Vec3f* MR::getPlayerPos() */
static const LumaFunction* in_area;        /* bool MR::isPlayerInAreaObj(const char*) */
static const LumaFunction* mirror_there;   /* bool MR::isExistMirrorCamera() */
static const LumaFunction* mirror_view;    /* const Pos3f& MR::getMirrorCameraViewMtx() */
static const LumaFunction* to_mirror;      /* f32 MR::getDistanceToMirror(const Vec3f&) */
static const LumaFunction* cam_pos;        /* const Vec3f MR::getCamPos() */
static Vec3 main_eye;

static long long rounded(float v) {
    return (long long)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

static Vec3 player_pos(void) {
    const Vec3* at = 0;
    void* none[] = {0};
    get_player_pos->invoke(none, &at);
    Vec3 out = {0, 0, 0};
    if (at != 0) out = *at;
    return out;
}

static float distance_to_mirror(Vec3 p) {
    /* A reference parameter's slot is the object's address (luma.h). */
    void* args[] = {0, &p};
    float d = 0.0f;
    to_mirror->invoke(args, &d);
    return d;
}

/* The eye of the mirror's view matrix (rows R | t): -R^T t. */
static Vec3 mirror_eye(void) {
    const float(*m)[4] = 0;
    void* none[] = {0};
    mirror_view->invoke(none, &m);
    Vec3 e = {0, 0, 0};
    if (m == 0) return e;
    e.x = -(m[0][0] * m[0][3] + m[1][0] * m[1][3] + m[2][0] * m[2][3]);
    e.y = -(m[0][1] * m[0][3] + m[1][1] * m[1][3] + m[2][1] * m[2][3]);
    e.z = -(m[0][2] * m[0][3] + m[1][2] * m[1][3] + m[2][2] * m[2][3]);
    return e;
}

/* The mirror's eye against `e`: facts `gap` (1000 |m - e|^2 / (2d)^2) and `mid`. */
static void against(Vec3 e, const char* gap, const char* mid) {
    const Vec3 m = mirror_eye();
    const float d = distance_to_mirror(e);
    const float dx = m.x - e.x, dy = m.y - e.y, dz = m.z - e.z;
    const float apart = dx * dx + dy * dy + dz * dz;
    lt_fact(gap, d != 0.0f ? rounded(1000.0f * apart / (4.0f * d * d)) : -1);
    const Vec3 middle = {(m.x + e.x) * 0.5f, (m.y + e.y) * 0.5f, (m.z + e.z) * 0.5f};
    lt_fact(mid, rounded(distance_to_mirror(middle)));
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 2475;
    if (k < 1 || k > 3) return 0;
    if (k == 1) {
        const char* name = "MirrorArea";
        void* args[] = {0, &name};
        uint8_t inside = 0;
        in_area->invoke(args, &inside);
        lt_fact("in_area", inside != 0);
        void* none[] = {0};
        uint8_t there = 0;
        mirror_there->invoke(none, &there);
        lt_fact("mirror", there != 0);
        const Vec3 p = player_pos();
        const LumaDrawViewEye eye = {{p.x + 600.0f, p.y + 300.0f, p.z}, {p.x, p.y + 80.0f, p.z}, {0.0f, 1.0f, 0.0f}, 50.0f};
        main_eye = (Vec3){eye.eye[0], eye.eye[1], eye.eye[2]};
        lt_fact("main", view->main_eye(lt_mod, &eye));
    }
    if (k == 2) {
        against(main_eye, "gap_milli", "mid");
        lt_fact("back", view->main_eye(lt_mod, 0));
    }
    if (k == 3) {
        Vec3 game = {0, 0, 0};
        void* none[] = {0};
        cam_pos->invoke(none, &game);
        against(game, "back_gap_milli", "back_mid");
    }
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(view, LUMA_DRAW_VIEW_NAME, "1.0"),
    LUMA_FUNCTION(get_player_pos, "MR::getPlayerPos", "Vec3f *()"),
    LUMA_FUNCTION(in_area, "MR::isPlayerInAreaObj", "bool(const char *)"),
    LUMA_FUNCTION(mirror_there, "MR::isExistMirrorCamera", "bool()"),
    LUMA_FUNCTION(mirror_view, "MR::getMirrorCameraViewMtx", "const Pos3f &()"),
    LUMA_FUNCTION(to_mirror, "MR::getDistanceToMirror", "f32(const Vec3f &)"),
    LUMA_FUNCTION(cam_pos, "MR::getCamPos", "const Vec3f()"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
