#pragma once

#include "Game/Render/Clip.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

void billboardMtx(Mtx mtx);
void billboardYMtx(Mtx mtx);
void textureMtx(const TextureTransform& rTransform, const Vec& rCenter, Mtx dst);
void textureMtxOld(const TextureTransform& rTransform, const Vec& rCenter, Mtx dst);
void textureMtxMaya(const TextureTransform& rTransform, Mtx dst);
void textureMtxMayaOld(const TextureTransform& rTransform, Mtx dst);
void projConcat(Mtx mtx1, Mtx mtx2, Mtx dst);
