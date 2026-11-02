#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

// In host order as the importer cooks models and animations.
struct ResNTAB {
    u16 mEntryNum;
    u16 _2;

    struct Entry {
        u16 mKeyCode;
        u16 mOffs;
    } mEntries[1];
};

class JUTNameTab {
public:
    JUTNameTab();
    JUTNameTab(const ResNTAB*);

    virtual ~JUTNameTab() {
    }

    void setResource(const ResNTAB*);
    s32 getIndex(const char*) const;
    const char* getName(u16) const;
    u16 calcKeyCode(const char*) const;

    const ResNTAB* mResource;
    const char* mStrData;
    u16 mNameNum;
    u16 _E;
};
