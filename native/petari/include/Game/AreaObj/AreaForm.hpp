#pragma once

#include <nebula_geometry.hpp>


class TableIter;

class AreaForm {
public:
    enum Type {
        Type_Cube1,
        Type_Cube2,
        Type_Sphere,
        Type_Cylinder,
        Type_Bowl,
    };

    AreaForm() : mFollowMtx() {
    }

    virtual void init(const TableIter&);
    virtual bool isInVolume(const Vec3f&) const;

    Pos3f* mFollowMtx;
};

class AreaFormCube : public AreaForm {
public:
    AreaFormCube(int cubeType);

    virtual void init(const TableIter&);
    virtual bool isInVolume(const Vec3f&) const;

    void calcWorldPos(Vec3f*) const;
    void calcWorldRotate(Vec3f*) const;
    void calcWorldBox(DirBox3f*) const;
    void calcLocalPos(Vec3f*, const Vec3f&) const;
    static f32 getBaseSize();

    f32 getSizeX() const {
        return mScale.x * getBaseSize();
    }

    f32 getSizeY() const {
        return mScale.y * getBaseSize();
    }

    f32 getSizeZ() const {
        return mScale.z * getBaseSize();
    }

    void updateBoxParam();
    void calcWorldMtx(Pos3f*) const;

    int mCubeType;
    Vec3f mTranslation;
    Vec3f mRotation;
    Vec3f mScale;
    Box3f mBounding;
    Pos3f mBaseMtx;
};

class AreaFormSphere : public AreaForm {
public:
    AreaFormSphere();

    virtual void init(const TableIter&);
    virtual bool isInVolume(const Vec3f&) const;

    void calcUpVec(Vec3f*) const;
    void calcPos(Vec3f*) const;

    Vec3f mTranslation;
    f32 mRadius;
    Vec3f mUp;
};

class AreaFormBowl : public AreaForm {
public:
    AreaFormBowl();

    virtual void init(const TableIter&);
    virtual bool isInVolume(const Vec3f&) const;

    void calcUpVec(const Vec3f&);

    Vec3f mTranslation;
    Vec3f mUp;
    f32 mRadius;
};

class AreaFormCylinder : public AreaForm {
public:
    AreaFormCylinder();

    virtual void init(const TableIter&);
    virtual bool isInVolume(const Vec3f&) const;

    void calcPos(Vec3f*) const;
    void calcCenterPos(Vec3f*) const;
    void calcUpVec(Vec3f*) const;
    void calcDir(const Vec3f&);

    Vec3f mTranslation;
    Vec3f mUp;  // the axis, from the placement rotation
    f32 mRadius;
    f32 mHeight;
};
