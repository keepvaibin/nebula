#pragma once

class TypedDraw;

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class PlayerModel;
class Triangle;
class JUTTexture;

class CollisionShadow : public NameObj {
public:
    CollisionShadow(f32, f32);

    virtual ~CollisionShadow();
    virtual void draw() const;

    void setMode(u32);
    void create(const Vec3f&, const Vec3f&, const Vec3f&);
    void calcView(PlayerModel*, u32, PlayerModel*);
    void drawAndCaptureTex(PlayerModel*, const Vec3f&);
    void setUpdateFlag();
    void draw1() const;
    void sendVtx(TypedDraw& rDraw) const;
    void sendZsortedVtx(TypedDraw& rDraw, bool) const;
    void initCaptureTex();
    void setViewMtx(const Vec3f&);
    void drawVolumeBox(const Vec3f&, const Vec3f&) const;
    void draw3() const;
    void doSortPolygons();
    void makePolygonLists(u32 num);
    const Vec3f& getDrawPos(u32) const;
    const Vec3f& getDrawPos(u32, u32) const;
    const Vec2f& getDrawTx(u32, u32) const;

    u8 _C;
    u16 _E;
    f32 _10;
    f32 _14;
    f32 _18;
    f32 _1C;
    f32 _20;
    Vec3f _24;
    Vec3f _30;
    Vec3f _3C;
    Vec3f _48;
    u32 _54;
    u32 _58;  // room for this many polygons in each list below; doubles when a query fills it
    u16 _5A;
    u32* _5C;
    Vec3f* _60;
    Vec2f* _64;
    u32* _68;
    u32 _6C;
    u32 _6E;
    u32* _70;
    s32* _F0;
    f32* mSortDistances;
    Vec3f _2F0;
    JUTTexture* _300;
    u8 _304;
    bool _305;
    u8 _306;
    u8 _307;
    u16 _308;
    f32 _30C;
    f32 _310;
    Vec3f _314;
    Vec3f _320;
    Triangle* _32C;
    u32 _33C;
    // The volume's drawing state (built; nothing in the game draws with it).
};
