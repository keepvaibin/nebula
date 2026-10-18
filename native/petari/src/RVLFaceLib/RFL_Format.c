#include "RVLFaceLibInternal.h"
#include <mem.h>

#define MAGIC_OFFICIAL_DB 'RNOD'
#define MAGIC_HIDDEN_DB 'RNHD'

#define TABLE_DATA_STEP 1250
// TODO: How is this size calculated?
#define TEMP_BUFFER_SIZE (TABLE_DATA_STEP * sizeof(RFLiHiddenCharData) + 0x30E0)

void RFLiAllocateDB(RFLiDatabase* db) {
    memset((u8*)db + sizeof(RFLI_LD32(db->identifier)), 0, sizeof(RFLiDatabase));
}

static void RFLiClearTableData(RFLiTableData* data) {
    memset(data, 0, sizeof(RFLiTableData));
    RFLI_SET(RFLiTableData, data, next, -1);
    RFLI_SET(RFLiTableData, data, prev, -1);
}

void RFLiClearDBBuffer(void) {
    RFLiDatabase* db;
    RFLiHiddenDB* header;
    int i;

    db = RFLiGetDBManager()->database;
    header = &db->hidden;

    // @bug Four-byte buffer overrun
    RFLiAllocateDB(db);
    RFLI_ST32(db->identifier, MAGIC_OFFICIAL_DB);
    RFLI_SET(RFLiDatabase, db, isolation, TRUE);

    RFLI_ST32(header->identifier, MAGIC_HIDDEN_DB);
    RFLI_ST16(header->head, -1);
    RFLI_ST16(header->tail, -1);

    for (i = 0; i < RFLi_HDB_DATA_MAX; i++) {
        RFLiClearTableData(&header->data[i]);
    }
}
