#pragma once


namespace MR {
    class FormattedName;

    const char16_t* getSystemMessageDirect(const char*);
    const char16_t* getGameMessageDirect(const char*);
    const char16_t* getLayoutMessageDirect(const char*);
    const char16_t* getCurrentGalaxyNameOnCurrentLanguage();
    const char16_t* getCurrentScenarioNameOnCurrentLanguage();
    const char16_t* getRaceNameOnCurrentLanguage(int);
    bool isExistGameMessage(const char*);
    // Line `line` of a message's first page into pDst: at most capacity units with the terminator (none when capacity is
    // 0), a tag whole or not at all; returns the terminator. getMessageLineLength: the line's units, tags whole, without it.
    const char16_t* getMessageLine(char16_t* pDst, u32 capacity, const char16_t* pMessage, u32 line);
    u32 getMessageLineLength(const char16_t* pMessage, u32 line);
    s32 countMessageLine(const char16_t*);
    s32 countMessageChar(const char16_t*);
    s32 countMessageFigure(s32);
    const char16_t* getNextMessagePage(const char16_t*);
    const char16_t* getGalaxyNameOnCurrentLanguage(const char*);
    const char16_t* getGalaxyNameShortOnCurrentLanguage(const char*);
    const char16_t* getScenarioNameOnCurrentLanguage(const char*, s32);
    void getLayoutMessageID(FormattedName&, const char*, const char*);
    void makeCometMessageID(FormattedName&, const char*);
};  // namespace MR
