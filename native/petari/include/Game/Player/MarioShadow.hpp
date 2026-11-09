#pragma once

class TypedDraw;

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class PlayerModel;
class Triangle;
class GameTexture;

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

    u8 mMode;  // the shadow's mode (setMode; 255 none)
    f32 mRadius;  // the box's half size this frame (smaller over many polygons)
    f32 mMaxRadius;  // the mode's radius
    f32 mVolumeHeight;  // the volume box's up size (MarioActor::calcAnim)
    f32 mVolumeWidth;  // the volume box's side size
    f32 mLength;  // the shadow's length along mDirection (MarioActor sets it each frame)
    Vec3f mDirection;  // the shadow's direction, smoothed
    Vec3f mUp;  // the capture view's up
    Vec3f mVolumeUp;  // the volume box's up
    Vec3f mReferencePos;  // the shadow's point (MarioActor: the shadow position)
    u32 mPolygonCount;  // the polygons that take the shadow
    u32 mListCapacity;  // room for this many polygons in each list below; doubles when a query fills it
    u16 mVertsPerPolygon;  // corners per polygon (3)
    u32* mPolygonIndices;  // each shadow polygon's index in mPolygons
    Vec3f* mVertices;  // the distinct vertices
    Vec2f* mTexCoords;  // each vertex's texture coordinates
    u32* mVertexIndices;  // each polygon corner's vertex
    u32 mVertexCount;  // entries in mVertices
    u32 mEdgePolygonCount;  // polygons edge-on to the direction
    u32* mEdgePolygons;  // their indices in mPolygons
    s32* mSortOrder;  // the polygons by distance (doSortPolygons)
    f32* mSortDistances;
    Vec3f mCenter;  // the shadow's center (create)
    GameTexture* mCaptureTex;  // the captured shadow
    u8 mUpdate;  // the capture is due (setUpdateFlag)
    bool mSnapDirection;  // the direction snaps instead of blending (the bee's wall stick)
    u8 mViewDone;  // calcView ran: the capture may draw
    u8 mCaptureOn;  // the capture draws (drawShadow)
    u16 mDrawFlags;  // the capture and draw options (2, 4, 8); nothing sets it
    f32 mCheckOffset;  // the polygon box's offset back along the direction (by mode)
    f32 mDrawOffset;  // the vertices' offset along their normal (by mode)
    Triangle* mPolygons;  // the polygons found (createAreaPolygonListArray)
    u32 mFoundCount;  // polygons found: more makes mRadius smaller
    // The volume's drawing state (built; nothing in the game draws with it).
};
