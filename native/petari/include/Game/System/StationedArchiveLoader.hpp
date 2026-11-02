#pragma once

class JKRHeap;

namespace MR {
    struct StationedFileInfo;
};  // namespace MR

class StationedArchiveLoader {
public:
    class Condition {
    public:
        virtual ~Condition();
        // Pure, as on the console (the DOL has no vtable of this base; every condition defines it).
        virtual bool isExecute(const MR::StationedFileInfo*) const = 0;
        virtual JKRHeap* getProperHeap(const MR::StationedFileInfo*) const;
    };

    static JKRHeap* getProperHeap(const MR::StationedFileInfo*);
    static void loadResourcesFromTable(const StationedArchiveLoader::Condition&);
    static void createAndAddResourcesFromTable(const StationedArchiveLoader::Condition&);
    static void loadScenarioData(JKRHeap*);
};
