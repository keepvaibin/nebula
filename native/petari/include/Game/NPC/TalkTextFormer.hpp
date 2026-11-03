#pragma once


class CustomTagArg;
class LayoutActor;

class TalkTextFormer {
public:
    TalkTextFormer(LayoutActor*, const char*);

    void formMessage(const char16_t*, s32);
    void setArg(const CustomTagArg&, s32);
    bool nextPage();
    bool hasNextPage() const;
    void updateTalking();
    void updateTalkingAtTextSpeed();
    bool isTextAppearedAll() const;

    LayoutActor* mHostActor;
    const char16_t* mMsg;
    s32 _8;
    const char* mPaneName;
};

namespace MR {
    void nextStepTagProcessorRecursive(LayoutActor*, const char*);
    bool isEndStepTagProcessorRecursive(const LayoutActor*, const char*, bool);
    void initTagProcessorRecursive(LayoutActor*, const char*, s32);
};  // namespace MR
