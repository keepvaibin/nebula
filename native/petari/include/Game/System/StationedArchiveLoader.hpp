#pragma once

class NebulaArena;

namespace MR {
    struct StationedFileInfo;
};  // namespace MR

class StationedArchiveLoader {
public:
    class Condition {
    public:
        virtual ~Condition();
        // Pure (the DOL has no vtable of this base; every condition defines it).
        virtual bool isExecute(const MR::StationedFileInfo*) const = 0;
        virtual NebulaArena* getProperHeap(const MR::StationedFileInfo*) const;
    };

    static void loadResourcesFromTable(const StationedArchiveLoader::Condition&);
    static void createAndAddResourcesFromTable(const StationedArchiveLoader::Condition&);
    static void loadScenarioData(NebulaArena*);
};
