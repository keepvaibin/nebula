#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class DemoCameraInfo;

namespace MR {
    class FormattedName;
}

namespace DemoCameraFunction {
    bool isCameraTargetMario(const DemoCameraInfo*);
    bool setStringNullIfEmpty(const char**);
    void makeAnimCameraName(MR::FormattedName&, const DemoCameraInfo*);
};  // namespace DemoCameraFunction
