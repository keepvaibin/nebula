#pragma once


namespace MR {
    struct StationedFileInfo {
        enum LoadType {
            LOAD_TYPE_FILE = 0,
            LOAD_TYPE_MOUNT = 1,
            LOAD_TYPE_MOUNT_RESOURCE_MARIO = 2,
            LOAD_TYPE_MOUNT_RESOURCE_LUIGI = 3,
            LOAD_TYPE_MOUNT_RESOURCE = 4,
            LOAD_TYPE_MOUNT_RESOURCE_LAYOUT = 5,
        };

        LoadType mLoadType;
        const char* mArchive;
    };

    const StationedFileInfo* getStationedFileInfoTable();
};  // namespace MR
