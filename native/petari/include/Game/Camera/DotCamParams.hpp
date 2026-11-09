#pragma once

#include "Game/Util/Table.hpp"
#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class DotCamReader {
public:
    DotCamReader() {
    }

    virtual ~DotCamReader();

    virtual u32 getVersion() const = 0;
    virtual bool hasMoreChunk() const = 0;
    virtual void nextToChunk() = 0;
    virtual bool getValueInt(const char*, s32*) = 0;
    virtual bool getValueFloat(const char*, f32*) = 0;
    virtual bool getValueVec(const char*, Vec3f*) = 0;
    virtual bool getValueString(const char*, const char**) = 0;
};

class DotCamReaderInBin : public DotCamReader {
public:
    DotCamReaderInBin(const void*);

    virtual u32 getVersion() const override;
    virtual bool hasMoreChunk() const override;
    virtual void nextToChunk() override;
    virtual bool getValueInt(const char*, s32*) override;
    virtual bool getValueFloat(const char*, f32*) override;
    virtual bool getValueVec(const char*, Vec3f*) override;
    virtual bool getValueString(const char*, const char**) override;

    void init(const void*);

    u32 mVersion;
    Table mMapInfo;
    TableIter mMapIter;
};
