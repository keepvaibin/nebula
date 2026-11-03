#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class DemoCameraInfo;

namespace DemoCameraFunction {
    bool isCameraTargetMario(const DemoCameraInfo*);
    bool setStringNullIfEmpty(const char**);
    void makeAnimCameraName(char*, u32, const DemoCameraInfo*);
};  // namespace DemoCameraFunction
