#include "Game/Camera/CameraParamString.hpp"

CameraParamString& CameraParamString::operator=(const CameraParamString& rOther) {
    setCharPtr(rOther.mString);

    return *this;
}

void CameraParamString::setCharPtr(const char* pString) {
    mString = pString != nullptr && *pString != 0 ? pString : nullptr;
}
