#include "Game/System/GalaxyNameSortTable.hpp"
#include "Game/System/GalaxyInfoTable.hpp"
#include "Game/Util/Table.hpp"

s32 GalaxyNameSortTable::getGalaxySortIndex(const char* pName) {
    Table info;
    info.attach(NebulaDataTables::getGalaxyInfoTable());

    TableIter iter = info.findElement("name", pName, 0);

    if (iter == info.end()) {
        return -1;
    }

    return iter.mIndex;
}
