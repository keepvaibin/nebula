#pragma once


class CustomTagArg;
class LayoutActor;

// How a page's letters come in: quickly (talk), slowly (an event's text) or the whole page at once.
enum TalkTextForm {
    TalkTextForm_Talk = 0,
    TalkTextForm_Event = 1,
    TalkTextForm_Whole = 2,
};

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
    s32 mFormMode;
    const char* mPaneName;
};

namespace MR {
    void nextStepTagProcessorRecursive(LayoutActor*, const char*);
    bool isEndStepTagProcessorRecursive(const LayoutActor*, const char*, bool);
    void initTagProcessorRecursive(LayoutActor*, const char*, s32);
};  // namespace MR
