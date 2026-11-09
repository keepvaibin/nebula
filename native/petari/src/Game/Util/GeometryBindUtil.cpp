#include "Game/Util/GeometryBindUtil.hpp"
#include "Game/Util/MathUtil.hpp"

namespace {
    inline s32 isWithinConeSide(f32 along, f32 length) {
        return 0.0f < along && along < length;
    }
};  // namespace

BindSphere::BindSphere(const Vec3f& rPosition, f32 radius) : mPosition(rPosition), mRadius(radius) {
}

BindCone::BindCone(const Vec3f& rPosition, const Vec3f& rDirection, f32 height, f32 radius)
    : mPosition(rPosition), mVertex(0.0f, 0.0f, 0.0f), mDirection(rDirection), mHeight(height), mRadius(radius), mGeneratrixLength() {
    MR::normalizeOrZero(&mDirection);
    calcVertexPosition();
    calcGeneratrixLength();
}

bool BindCone::isInPoint(const Vec3f& rPoint) const {
    f32 height = mDirection.dot(rPoint - mPosition);
    f32 radius = mRadius - height * (mRadius / mHeight);
    return rPoint.squared(mPosition + mDirection * height) < radius * radius;
}

void BindCone::calcVertexPosition() {
    mVertex = mPosition + mDirection * mHeight;
}

void BindCone::calcGeneratrixLength() {
    mGeneratrixLength = NebulaSqrt(mHeight * mHeight + mRadius * mRadius);
}

void BindCone::setPosition(const Vec3f& rPosition) {
    mPosition = rPosition;
    calcVertexPosition();
}

void BindCone::setDirection(const Vec3f& rDirection) {
    mDirection = rDirection;
    calcVertexPosition();
}

namespace MR {
    // collision detection and position correcting for a moving sphere against a cone
    bool bindSpereToCone(BindResult* pResult, const Vec3f& rMovement, const BindSphere& rSphere, const BindCone& rCone) {
        Vec3f nextPosition;
        nextPosition.set(rSphere.mPosition + rMovement);
        f32 expandedHeight = (rCone.getGeneratrixLength() / rCone.getRadius()) * rSphere.getRadius();
        Vec3f radial(nextPosition - rCone.mPosition);
        radial.orthogonalize(rCone.mDirection);

        if (radial.isZero()) {
            radial = rMovement;
            radial.orthogonalize(rCone.mDirection);

            if (radial.isZero()) {
                f32 height = rCone.mDirection.dot(rCone.mVertex - nextPosition);
                if (-rSphere.getRadius() < height && height < 0.0f) {
                    if (pResult != nullptr) {
                        pResult->mPosition = rCone.mVertex + rCone.mDirection * rSphere.getRadius();
                        pResult->mNormal = rCone.mDirection;
                        pResult->mIsBound = true;
                    }

                    return true;
                }

                if (0.0f <= height && height < expandedHeight) {
                    if (pResult != nullptr) {
                        pResult->mPosition = rCone.mVertex - rCone.mDirection * expandedHeight;
                        pResult->mNormal = -rCone.mDirection;
                        pResult->mIsBound = true;
                    }

                    return true;
                }

                if (pResult != nullptr) {
                    pResult->mPosition = nextPosition;
                    pResult->mIsBound = false;
                }

                return false;
            }
        }

        radial.setLength(rCone.getRadius());
        radial.add(rCone.mPosition);
        Vec3f generatrixDirection(radial - rCone.mVertex);
        f32 generatrixLength = rCone.getGeneratrixLength();
        generatrixDirection /= generatrixLength;
        f32 along = generatrixDirection.dot(nextPosition - rCone.mVertex);
        s32 within = isWithinConeSide(along, generatrixLength);
        along = MR::clamp(along, 0.0f, generatrixLength);
        Vec3f closest(rCone.mVertex + generatrixDirection * along);
        Vec3f contactDirection(nextPosition - closest);
        f32 distance;
        separateScalarAndDirection(&distance, &contactDirection, contactDirection);
        bool wasInside = rCone.isInPoint(rSphere.mPosition);
        bool isInside = rCone.isInPoint(nextPosition);

        if (wasInside) {
            f32 baseDistance = rCone.mDirection.dot(rCone.mPosition - rSphere.mPosition) - rSphere.getRadius();
            f32 motionAlong = rCone.mDirection.dot(rMovement);
            Vec3f baseProjection(rSphere.mPosition + rMovement * (motionAlong - baseDistance));
            baseProjection.killElement(baseProjection - rCone.mPosition, rCone.mDirection);

            if (baseProjection.squared() < rCone.getRadius() * rCone.getRadius()) {
                if (rCone.mDirection.dot(rCone.mVertex - nextPosition) < expandedHeight) {
                    if (pResult != nullptr) {
                        pResult->mPosition = rCone.mVertex - rCone.mDirection * expandedHeight;
                        pResult->mNormal = -rCone.mDirection;
                        pResult->mIsBound = true;
                    }

                    return true;
                }
            }
        }

        if (within && wasInside != isInside) {
            if (pResult != nullptr) {
                pResult->mNormal = -contactDirection;
                pResult->mIsBound = true;
            }

            return true;
        }

        if (distance < rSphere.getRadius()) {
            if (pResult != nullptr) {
                Vec3f pushedPosition(nextPosition + contactDirection * (rSphere.getRadius() - distance));
                pResult->mPosition = pushedPosition;
                pResult->mNormal = contactDirection;
                pResult->mIsBound = true;
            }

            return true;
        }

        if (pResult != nullptr) {
            pResult->mPosition = nextPosition;
            pResult->mIsBound = false;
        }

        return false;
    }

    void updateBindPositionAndVelocity(Vec3f* pPosition, Vec3f* pVelocity, const BindResult& rResult, f32 factor) {
        pPosition->set(rResult.mPosition);
        if (rResult.mIsBound) {
            f32 dot = pVelocity->dot(rResult.mNormal);
            if (dot < 0.0f) {
                pVelocity->add((-rResult.mNormal * dot) * factor);
            }
        }
    }
};  // namespace MR
