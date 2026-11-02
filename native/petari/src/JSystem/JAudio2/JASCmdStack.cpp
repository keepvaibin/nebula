#include "JSystem/JAudio2/JASCmdStack.hpp"
#include "Game/System/GameThreads.hpp"

JASPortCmd::TPortHead JASPortCmd::sCommandListOnce;

JASPortCmd::TPortHead JASPortCmd::sCommandListStay;

bool JASPortCmd::addPortCmdOnce() {
    GameAudioExclusion cs;
    return sCommandListOnce.append(this);
}

bool JASPortCmd::setPortCmd(Command func, JASPortArgs* args) {
    if (getSupervisor()) {
        return false;
    }
    mFunc = func;
    mArgs = args;
    return true;
}

void JASPortCmd::execAllCommand() {
    sCommandListOnce.execCommandOnce();
    sCommandListStay.execCommandStay();
}

void JASPortCmd::TPortHead::execCommandOnce() {
    GameAudioExclusion cs;
    JSULink< JASPortCmd >* next;
    for (JSULink< JASPortCmd >* link = getFirst(); link != NULL; link = next) {
        next = link->getNext();
        JASPortCmd* cmd = link->getObject();
        cmd->getFunc()(cmd->getArgs());
        remove(link);
    }
}

void JASPortCmd::TPortHead::execCommandStay() {
    GameAudioExclusion cs;
    for (JSULink< JASPortCmd >* link = getFirst(); link != NULL; link = link->getNext()) {
        JASPortCmd* cmd = link->getObject();
        cmd->getFunc()(cmd->getArgs());
    }
}
