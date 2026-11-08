#pragma once

#include <Game/Ui/TagProcessorBase.hpp>

class MessageTagSkipTagProcessor : public Ui::TagProcessorBase< char16_t > {
public:
    MessageTagSkipTagProcessor();

    virtual ~MessageTagSkipTagProcessor() {
    }

    virtual Ui::TagProcessorBase< char16_t >::Operation Process(u16, ContextType*);
    virtual Ui::TagProcessorBase< char16_t >::Operation CalcRect(Ui::Rect*, u16, ContextType*);

    Ui::TagProcessorBase< char16_t >::Operation skipTag(Ui::Rect*, ContextType*, bool);
};

class MessageEditorMessageTag {
public:
    MessageEditorMessageTag(const char16_t*);
    MessageEditorMessageTag(const Ui::PrintContext< char16_t >*);

    s32 getGroup() const {
        return reinterpret_cast< const u8* >(mMessage)[1];
    }
    int getTag() const {
        return mMessage[1];
    }

    bool isGroupTagId(int group, int tag) const {
        return reinterpret_cast< const u8* >(mMessage)[1] == group && mMessage[1] == tag;
    }

    u32 getTagLength() const;
    u32 getSkipLength() const;
    s32 getParamLength() const;
    u8 getParam8(int) const;
    u16 getParam16(int) const;
    u32 getParam32(int) const;
    char16_t* getParamPtr(int) const;
    // A string argument (group 7), in the tag's first, 4-byte parameter.
    void setParamString(const char16_t* pString) const;
    const char16_t* getParamString() const;

    const char16_t* mMessage;
};
