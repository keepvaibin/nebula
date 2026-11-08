#pragma once

#include "Game/AreaObj/AreaForm.hpp"
#include "Game/AreaObj/AreaObj.hpp"

class CameraRepulsiveArea : public AreaObj {
public:
    CameraRepulsiveArea(int formType, const char* pName) : AreaObj(formType, pName) {
    }

    virtual const char* getManagerName() const {
        return "CameraRepulsiveArea";
    }

    virtual Vec3f getRepulsion(const Vec3f&) = 0;
};

class CameraRepulsiveSphere : public CameraRepulsiveArea {
public:
    CameraRepulsiveSphere(const char* pName) : CameraRepulsiveArea(AreaForm::Type_Sphere, pName) {
    }

    virtual Vec3f getRepulsion(const Vec3f&);
};

class CameraRepulsiveCylinder : public CameraRepulsiveArea {
public:
    CameraRepulsiveCylinder(const char* pName) : CameraRepulsiveArea(AreaForm::Type_Cylinder, pName) {
    }

    virtual Vec3f getRepulsion(const Vec3f&);
};
