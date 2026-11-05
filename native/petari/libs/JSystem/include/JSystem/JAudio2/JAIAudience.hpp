#pragma once

#include <nebula_geometry.hpp>

class JAIAudible;
class JAISoundID;
struct JASSoundParams;

class JAIAudience {
public:
    virtual ~JAIAudience();

    // TODO: Should all be pure virtual.
    virtual JAIAudible* newAudible(const Vec3f&, JAISoundID, const Vec3f*, u32) = 0;
    virtual int getMaxChannels() = 0;
    virtual void deleteAudible(JAIAudible*) = 0;
    virtual u32 calcPriority(JAIAudible*) = 0;
    virtual void mixChannelOut(const JASSoundParams&, JAIAudible*, int) = 0;
};
