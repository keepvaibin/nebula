#pragma once

#include <JSystem/JGeometry/TMatrix.hpp>
#include <JSystem/JGeometry.hpp>

class JMapInfoIter;

class AreaForm {
public:
    enum Type {
        Type_Cube1,
        Type_Cube2,
        Type_Sphere,
        Type_Cylinder,
        Type_Bowl,
    };

    AreaForm() : _4() {
    }

    virtual void init(const JMapInfoIter&);
    virtual bool isInVolume(const TVec3f&) const;

    TPos3f* _4;
};

class AreaFormCube : public AreaForm {
public:
    AreaFormCube(int);

    virtual void init(const JMapInfoIter&);
    virtual bool isInVolume(const TVec3f&) const;

    void calcWorldPos(TVec3f*) const;
    void calcWorldRotate(TVec3f*) const;
    void calcWorldBox(TDirBox3f*) const;
    void calcLocalPos(TVec3f*, const TVec3f&) const;
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
    void calcWorldMtx(TPos3f*) const;

    int _8;
    TVec3f mTranslation;
    TVec3f mRotation;
    TVec3f mScale;
    TBox3f mBounding;
    TPos3f _48;
};

class AreaFormSphere : public AreaForm {
public:
    AreaFormSphere();

    virtual void init(const JMapInfoIter&);
    virtual bool isInVolume(const TVec3f&) const;

    void calcUpVec(TVec3f*) const;
    void calcPos(TVec3f*) const;

    TVec3f mTranslation;
    f32 mRadius;
    TVec3f mUp;
};

class AreaFormBowl : public AreaForm {
public:
    AreaFormBowl();

    virtual void init(const JMapInfoIter&);
    virtual bool isInVolume(const TVec3f&) const;

    void calcUpVec(const TVec3f&);

    TVec3f mTranslation;
    TVec3f mUp;
    f32 _20;
};

class AreaFormCylinder : public AreaForm {
public:
    AreaFormCylinder();

    virtual void init(const JMapInfoIter&);
    virtual bool isInVolume(const TVec3f&) const;

    void calcPos(TVec3f*) const;
    void calcCenterPos(TVec3f*) const;
    void calcUpVec(TVec3f*) const;
    void calcDir(const TVec3f&);

    TVec3f mTranslation;
    TVec3f mRotation;
    f32 mRadius;
    f32 mHeight;
};
