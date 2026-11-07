#include "Game/MapObj/GCaptureRibbon.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/ActorLightCtrl.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <nebula_geometry.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>

GCaptureRibbon::GCaptureRibbon(const char* pName) : LiveActor(pName), mTexture(), _990(), _994() {
}

void GCaptureRibbon::draw() const {
    if (_990 >= 2) {
        TDDraw::cameraInit3D();
        SiteMaterial material;
        TDDraw::setupNative(&material, 1, 2, 0);
        f32 width = calcLineWidth();
        material.setTexture(0, mTexture);
        material.setDepth(true, NEBULA_COMPARE_LEQUAL, false);

        TypedDraw site = TDDraw::site(material, 0);
    VertexPT* vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 2 * _990);
        {
            for (s32 i = 0; i < _990; i++) {
                f32 v2 = i / (_990 - 1.0f);

                Vec3f v1 = _690[i] * width;

                *vtx++ = {v1.x + _90[i].x, v1.y + _90[i].y, v1.z + _90[i].z, 0.0f, v2};
                *vtx++ = {_90[i].x - v1.x, _90[i].y - v1.y, _90[i].z - v1.z, 1.0f, v2};
            }
        }

        vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 2 * _990);
        {
            for (s32 j = 0; j < _990; j++) {
                f32 v2 = j / (_990 - 1.0f);

                Vec3f v1 = _390[j] * width;

                *vtx++ = {v1.x + _90[j].x, v1.y + _90[j].y, v1.z + _90[j].z, 0.0f, v2};
                *vtx++ = {_90[j].x - v1.x, _90[j].y - v1.y, _90[j].z - v1.z, 1.0f, v2};
            }
        }
    }
}

void GCaptureRibbon::reset() {
    _990 = 0;
    _994 = 0;
}

void GCaptureRibbon::lengthen(const Vec3f& a1, const Vec3f& a2) {
    _90[0].set(a1);
    _90[1].set(a2);
    _990 = 2;
    updateAxis();
}

void GCaptureRibbon::shorten(const Vec3f& a1, const Vec3f& a2) {
    _90[0].set(a1);
    _90[1].set(a2);
    _990 = 2;
    updateAxis();
}

void GCaptureRibbon::updateAxis() {
    Quat4f quat;
    if (_990 >= 2) {
        Vec3f v1 = _90[1] - _90[0];
        MR::normalizeOrZero(&v1);

        if (MR::isNearZero(v1)) {
            _390[0] = Vec3f(1, 0, 0);
            _690[0] = Vec3f(0, 1, 0);
            v1 = Vec3f(0, 0, 1);
        } else {
            MR::makeAxisVerticalZX(&_390[0], v1);
            _690[0].cross(v1, _390[0]);
        }

        for (s32 i = 1; i < _990 - 1; i++) {
            Vec3f v2 = _90[i + 1] - _90[i];
            if (MR::isNearZero(v2)) {
                _390[i] = _390[i - 1];
                _690[i] = _690[i - 1];
            } else {
                MR::normalize(&v2);
                if (v1.dot(v2) < 0.0f) {
                    v2 = -v2;
                }
                quat.setRotate(v1, v2);
                quat.transform(_390[i - 1], _390[i]);
                quat.transform(_690[i - 1], _690[i]);
                v1.set(v2);
            }
        }
        _390[_990 - 1] = _390[_990 - 2];
        _690[_990 - 1] = _690[_990 - 2];
    }
}

f32 GCaptureRibbon::calcLineWidth() const {
    f32 normalize = MR::normalize(_90[0].distance(_90[_990 - 1]), 500.0f, 2000.0f);
    return (20.0f * normalize) + (90.0f * (1.0f - normalize));
}

GCaptureRibbon::~GCaptureRibbon() {
}

void GCaptureRibbon::init(const TableIter& rIter) {
    mTexture = new JUTTexture(MR::loadTexFromArc("GCaptureRibbon", "GCaptureRibbon.bti"));
    MR::invalidateClipping(this);
    makeActorAppeared();
}
