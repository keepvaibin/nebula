#include "Game/Util/ShareUtil.hpp"

ResourceShare::~ResourceShare() {
}

ResourceShare::ResourceShare() : NameObj("\216\221\214\271\213\244\227\114\213\100\215\134" /* 資源共有機構 */) {
    _C = new u8[0x80];
    _10 = new u8[0x80];
    _14 = 0;
}
