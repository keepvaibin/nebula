#pragma once


/// @brief Stores archive names of NameObjs.
class NameObjArchiveListCollector {
public:
    /// @brief Creates a new `NameObjArchiveListCollector`.
    NameObjArchiveListCollector();

    void addArchive(const char*);
    const char* getArchive(s32) const;

    s32 getArchiveNum() const {
        return mArchiveNum;
    }

private:
    char mArchive[32][64];
    s32 mArchiveNum;
};
