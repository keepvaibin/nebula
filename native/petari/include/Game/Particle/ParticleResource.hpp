#pragma once


class NebulaArena;
struct EmitterWork;
class EmitterCore;
class ParticleCore;

struct ParticleKey;
struct ParticleShape;
class ExtraShape;
class ChildShape;
class ExTexShape;
class EmitterDynamics;
class ParticleField;

class ParticleResource {
public:
    ParticleResource();
    void init();
    bool calc(EmitterWork*, EmitterCore*);
    void draw(EmitterWork*, EmitterCore*);
    void drawP(EmitterWork*);
    void drawC(EmitterWork*);
    void setPTev();
    void setCTev(EmitterWork*);
    void calc_p(EmitterWork*, ParticleCore*);
    void calc_c(EmitterWork*, ParticleCore*);
    void drawParticle(EmitterWork*, ParticleCore*);
    void drawChild(EmitterWork*, ParticleCore*);
    void calcField(EmitterWork*, ParticleCore*);
    void calcKey(EmitterWork*);
    void calcWorkData_c(EmitterWork*);
    void calcWorkData_d(EmitterWork*);

    ParticleShape* getBsp() const {
        return mpBaseShape;
    }
    ExtraShape* getEsp() const {
        return mpExtraShape;
    }
    ChildShape* getCsp() const {
        return mpChildShape;
    }
    ExTexShape* getEts() const {
        return mpExTexShape;
    }
    EmitterDynamics* getDyn() const {
        return mpDynamicsBlock;
    }

    u32 getTexIdx(u32 idx) const {
        return mpTDB1[idx];
    }
    u32 getUsrIdx() const {
        return mUsrIdx;
    }

public:
    // What the resource's emitters and particles run each frame and each draw, chosen once at load (init) from its
    // shapes. 0 is "nothing" in each.
    enum Anim { Anim_None, Anim_Normal, Anim_Repeat, Anim_Reverse, Anim_Merge, Anim_Random };
    enum Alpha { Alpha_None, Alpha_Anim, Alpha_Flick };
    enum ScaleY { ScaleY_None, ScaleY_Own, ScaleY_Copy };
    enum ScaleAnim { ScaleAnim_None, ScaleAnim_Normal, ScaleAnim_Repeat, ScaleAnim_Reverse };
    enum Stripe { Stripe_None, Stripe_Plain, Stripe_Cross };
    enum LoadTex { LoadTex_None, LoadTex_Fixed, LoadTex_Anim };
    enum TexCrd { TexCrd_Identity, TexCrd_Projection, TexCrd_CalcAnim, TexCrd_Anim };
    enum Size { Size_None, Size_Point, Size_Line, Size_TexCrdAnim };
    enum EmitterColor { EmitterColor_None, EmitterColor_PrmEnv, EmitterColor_Env, EmitterColor_Prm };
    enum ParticleColor { ParticleColor_None, ParticleColor_PrmAlphaEnv, ParticleColor_PrmAlpha, ParticleColor_AlphaEnv, ParticleColor_Alpha,
                         ParticleColor_Env };
    enum Shape {
        Shape_None,
        Shape_Billboard,
        Shape_RotBillboard,
        Shape_YBillboard,
        Shape_RotYBillboard,
        Shape_Direction,
        Shape_RotDirection,
        Shape_DBillboard,
        Shape_Rotation,
        Shape_Point,
        Shape_Line
    };
    struct Plan {
        // Each emitter frame.
        u8 mEmitterTexIdx;  // Anim
        u8 mEmitterClrIdx;  // Anim
        bool mEmitterPrm;
        bool mEmitterEnv;
        // Each particle frame.
        u8 mTexIdx;  // Anim
        u8 mAlpha;   // Alpha
        bool mPrm;
        bool mEnv;
        u8 mClrIdx;  // Anim
        bool mColorCopy;
        u8 mScaleY;     // ScaleY
        u8 mScaleAnimY;  // ScaleAnim
        bool mScaleX;
        u8 mScaleAnimX;  // ScaleAnim
        bool mChildScaleOut;
        bool mChildAlphaOut;
        // The emitter's draw.
        u8 mStripe;  // Stripe
        bool mExTex;
        u8 mLoadTex;  // LoadTex
        u8 mTexCrd;   // TexCrd
        bool mPosMtxCam;
        u8 mSize;          // Size (no Size_TexCrdAnim)
        u8 mEmitterColor;  // EmitterColor
        // Each particle's draw.
        u8 mShape;  // Shape
        bool mTexAnim;
        u8 mParticleSize;   // Size
        u8 mParticleColor;  // ParticleColor
        // The children's draw.
        u8 mChildStripe;  // Stripe
        bool mChildPosMtxCam;
        bool mChildPrmEnv;
        u8 mChildShape;  // Shape
        u8 mChildSize;   // Size (no Size_TexCrdAnim)
        bool mChildPrmAlphaEnv;
    };
    Plan mPlan;

    ParticleShape* mpBaseShape;
    ExtraShape* mpExtraShape;
    ChildShape* mpChildShape;
    ExTexShape* mpExTexShape;
    EmitterDynamics* mpDynamicsBlock;
    ParticleField** mpFieldBlocks;
    ParticleKey** mpKeyBlocks;
    // The resource's texture index table (the file's textures).
    const u32* mpTDB1;
    u32 mUsrIdx;
    u32 mFieldBlockNum;
    u32 mKeyBlockNum;
};
