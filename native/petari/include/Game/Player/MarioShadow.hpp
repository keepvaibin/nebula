#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class J3DModelX;
class Triangle;
class JUTTexture;

class CollisionShadow : public NameObj {
public:
    CollisionShadow(f32, f32);

    virtual ~CollisionShadow();
    virtual void draw() const;

    void setMode(u32);
    void create(const TVec3f&, const TVec3f&, const TVec3f&);
    void calcView(J3DModelX*, u32, J3DModelX*);
    void drawAndCaptureTex(J3DModelX*, const TVec3f&);
    void setUpdateFlag();
    void draw1() const;
    void sendVtx() const;
    void sendZsortedVtx(bool) const;
    void initCaptureTex();
    void setViewMtx(const TVec3f&);
    void drawVolumeBox(const TVec3f&, const TVec3f&) const;
    void draw2() const;
    void draw3() const;
    void createMesh();
    void drawVolume() const;
    void doSortPolygons();
    const TVec3f& getDrawPos(u32) const;
    const TVec3f& getDrawPos(u32, u32) const;
    const TVec2f& getDrawTx(u32, u32) const;

    u8 _C;
    u16 _E;
    f32 _10;
    f32 _14;
    f32 _18;
    f32 _1C;
    f32 _20;
    TVec3f _24;
    TVec3f _30;
    TVec3f _3C;
    TVec3f _48;
    u32 _54;
    u16 _58;
    u16 _5A;
    u8* _5C;
    TVec3f* _60;
    TVec2f* _64;
    u16* _68;
    u16 _6C;
    u16 _6E;
    u8 _70[128];
    s32 _F0[128];
    TVec3f _2F0;
    u16 _2FC;
    u16 _2FE;
    JUTTexture* _300;
    u8 _304;
    bool _305;
    u8 _306;
    u8 _307;
    u16 _308;
    f32 _30C;
    f32 _310;
    TVec3f _314;
    TVec3f _320;
    Triangle* _32C;
    Triangle* _330;
    Triangle* _334;
    u16 _338;
    u16 _33A;
    u16 _33C;
    J3DModelX* _340;
    // The volume's drawing state (built; nothing in the game draws with it).
};
