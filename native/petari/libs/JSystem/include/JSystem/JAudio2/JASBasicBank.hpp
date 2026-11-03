#pragma once

#include "JSystem/JAudio2/JASBank.hpp"
#include "JSystem/JAudio2/JASBasicInst.hpp"
#include "Game/System/NebulaArena.hpp"

#define INST_NUM_MAX 0xF0

class JASBasicBank : public JASBank {
public:
    JASBasicBank();

    virtual ~JASBasicBank() {};
    virtual bool getInstParam(int, int, int, JASInstParam*) const;
    virtual u32 getType() const {
        return 'BSIC';
    }

    void newInstTable(u8, NebulaArena*);
    void setInst(int, JASInst*);
    JASInst* getInst(int) const;

    JASInst* mInstTable[INST_NUM_MAX];
};
