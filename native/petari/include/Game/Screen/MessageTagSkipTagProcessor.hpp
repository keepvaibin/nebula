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

namespace MR {
    class Text16;
}  // namespace MR

// A tag as a group function reads it (CustomTagProcessor): a typed record of a text in runs, or a tag read in the text of
// version 1 (that side goes with version 1, G7.5). A record is read only inside its parameters and string.
class MessageTagView {
public:
    explicit MessageTagView(const MessageEditorMessageTag& rTag) : mInText(rTag), mRecord(nullptr), mText(nullptr) {
    }
    MessageTagView(const MessageTagRecord& rRecord, const TextRuns& rText)
        : mInText(static_cast< const char16_t* >(nullptr)), mRecord(&rRecord), mText(&rText) {
    }

    bool isRuns() const {
        return mRecord != nullptr;
    }
    // The tag read in the text of version 1 (only when !isRuns()).
    const MessageEditorMessageTag& getInText() const {
        return mInText;
    }
    s32 getGroup() const;
    int getTag() const;
    // The parameters as MessageEditorMessageTag reads them; a record's parameter past its units is 0.
    u8 getParam8(int index) const;
    u16 getParam16(int index) const;
    // In bytes.
    s32 getParamLength() const;
    // The number tag's value: its first two parameter units as the game writes them (CustomTagProcessor::setArgNumber).
    s32 getNumber() const;
    // A record's string (a sound's name, the number font's text, a ruby's reading) as UTF-16 units (a code point past
    // U+FFFF as its two surrogates) and a 0, in rDst; its units. A record without one: none.
    u32 getString(MR::Text16& rDst) const;

private:
    MessageEditorMessageTag mInText;
    const MessageTagRecord* mRecord;
    const TextRuns* mText;
};
