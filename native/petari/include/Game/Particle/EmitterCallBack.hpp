#pragma once

class EmitterCore;

class EmitterCallBack {
public:
    virtual ~EmitterCallBack() = 0;
    virtual void execute(EmitterCore*) {
    }
    virtual void executeAfter(EmitterCore*) {
    }
    virtual void draw(EmitterCore*) {
    }
    virtual void drawAfter(EmitterCore*) {
    }
    virtual void init(EmitterCore*) {
    }
};
