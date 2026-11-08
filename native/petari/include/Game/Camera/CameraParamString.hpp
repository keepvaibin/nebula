#pragma once

// A chunk's string parameter: an empty string reads as no string.
class CameraParamString {
public:
    CameraParamString() : mString(nullptr) {}

    CameraParamString& operator=(const CameraParamString&);

    void setCharPtr(const char*);
    const char* getCharPtr() const { return mString; }

private:
    const char* mString;
};
