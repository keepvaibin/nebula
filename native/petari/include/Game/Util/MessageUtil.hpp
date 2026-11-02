#pragma once


namespace MR {
    const char16_t* getSystemMessageDirect(const char*);
    const char16_t* getGameMessageDirect(const char*);
    const char16_t* getLayoutMessageDirect(const char*);
    const char16_t* getCurrentGalaxyNameOnCurrentLanguage();
    const char16_t* getCurrentScenarioNameOnCurrentLanguage();
    const char16_t* getRaceNameOnCurrentLanguage(int);
    bool isExistGameMessage(const char*);
    const char16_t* getMessageLine(char16_t*, u32, const char16_t*, u32);
    s32 countMessageLine(const char16_t*);
    s32 countMessageChar(const char16_t*);
    s32 countMessageFigure(s32);
    const char16_t* getNextMessagePage(const char16_t*);
    const char16_t* getGalaxyNameOnCurrentLanguage(const char*);
    const char16_t* getGalaxyNameShortOnCurrentLanguage(const char*);
    const char16_t* getScenarioNameOnCurrentLanguage(const char*, s32);
    void getLayoutMessageID(char*, const char*, const char*);
    void makeCometMessageID(char*, u32, const char*);
};  // namespace MR
