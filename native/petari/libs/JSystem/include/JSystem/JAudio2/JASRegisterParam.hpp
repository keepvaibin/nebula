#pragma once


class JASRegisterParam {
public:
    enum RegID : u32 { REG_A, REG_B, REG_S, REG_F, REG_X, REG_Y };

    JASRegisterParam();

    void init();
    void write(JASRegisterParam::RegID, u32);
    u32 read(JASRegisterParam::RegID);

    u16 _0[14];
};
