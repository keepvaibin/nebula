#include "RVLFaceLibInternal.h"

// The library's default Miis (its guests), as character info.
static const RFLiCharInfo scDefaultInfo[] = {
    // "Guest A"
    {
        .faceline = {{.type = 0, .color = 4, .texture = 0}},
        .hair = {{.type = 68, .color = 0, .flip = 0}},
        .eye = {{.type = 2, .color = 0, .scale = 4, .rotate = 4, .x = 2, .y = 12}},
        .eyebrow = {{.type = 6, .color = 0, .scale = 4, .rotate = 6, .x = 2, .y = 10}},
        .nose = {{.type = 1, .scale = 4, .y = 9}},
        .mouth = {{.type = 23, .color = 0, .scale = 4, .y = 13}},
        .beard = {{.mustache = 0, .type = 0, .color = 0, .scale = 4, .y = 10}},
        .glass = {{.type = 0, .color = 0, .scale = 4, .y = 10}},
        .mole = {{.type = 0, .scale = 4, .x = 2, .y = 20}},
        .body = {.height = 64, .build = 64},
        .personal = {.name = L"no name", .creator = L"", .sex = 0, .bmonth = 0, .bday = 0, .color = 4, .favorite = 0, .localOnly = 1},
        .createID = {{0x80, 0x00, 0x00, 0x00, 0xEC, 0xFF, 0x82, 0xD2}},
    },
    // "Guest B"
    {
        .faceline = {{.type = 0, .color = 0, .texture = 0}},
        .hair = {{.type = 55, .color = 6, .flip = 0}},
        .eye = {{.type = 2, .color = 4, .scale = 4, .rotate = 4, .x = 2, .y = 12}},
        .eyebrow = {{.type = 6, .color = 6, .scale = 4, .rotate = 6, .x = 2, .y = 10}},
        .nose = {{.type = 1, .scale = 4, .y = 9}},
        .mouth = {{.type = 23, .color = 0, .scale = 4, .y = 13}},
        .beard = {{.mustache = 0, .type = 0, .color = 0, .scale = 4, .y = 10}},
        .glass = {{.type = 0, .color = 0, .scale = 4, .y = 10}},
        .mole = {{.type = 0, .scale = 4, .x = 2, .y = 20}},
        .body = {.height = 64, .build = 64},
        .personal = {.name = L"no name", .creator = L"", .sex = 0, .bmonth = 0, .bday = 0, .color = 5, .favorite = 0, .localOnly = 1},
        .createID = {{0x80, 0x00, 0x00, 0x01, 0xEC, 0xFF, 0x82, 0xD2}},
    },
    // "Guest C"
    {
        .faceline = {{.type = 0, .color = 1, .texture = 0}},
        .hair = {{.type = 33, .color = 1, .flip = 0}},
        .eye = {{.type = 2, .color = 0, .scale = 4, .rotate = 4, .x = 2, .y = 12}},
        .eyebrow = {{.type = 6, .color = 1, .scale = 4, .rotate = 6, .x = 2, .y = 10}},
        .nose = {{.type = 1, .scale = 4, .y = 9}},
        .mouth = {{.type = 23, .color = 0, .scale = 4, .y = 13}},
        .beard = {{.mustache = 0, .type = 0, .color = 0, .scale = 4, .y = 10}},
        .glass = {{.type = 0, .color = 0, .scale = 4, .y = 10}},
        .mole = {{.type = 0, .scale = 4, .x = 2, .y = 20}},
        .body = {.height = 64, .build = 64},
        .personal = {.name = L"no name", .creator = L"", .sex = 0, .bmonth = 0, .bday = 0, .color = 0, .favorite = 0, .localOnly = 1},
        .createID = {{0x80, 0x00, 0x00, 0x02, 0xEC, 0xFF, 0x82, 0xD2}},
    },
    // "Guest D"
    {
        .faceline = {{.type = 0, .color = 2, .texture = 0}},
        .hair = {{.type = 24, .color = 0, .flip = 0}},
        .eye = {{.type = 4, .color = 0, .scale = 4, .rotate = 3, .x = 2, .y = 12}},
        .eyebrow = {{.type = 0, .color = 0, .scale = 4, .rotate = 6, .x = 2, .y = 10}},
        .nose = {{.type = 1, .scale = 4, .y = 9}},
        .mouth = {{.type = 23, .color = 0, .scale = 4, .y = 13}},
        .beard = {{.mustache = 0, .type = 0, .color = 0, .scale = 4, .y = 10}},
        .glass = {{.type = 0, .color = 0, .scale = 4, .y = 10}},
        .mole = {{.type = 0, .scale = 4, .x = 2, .y = 20}},
        .body = {.height = 64, .build = 64},
        .personal = {.name = L"no name", .creator = L"", .sex = 1, .bmonth = 0, .bday = 0, .color = 2, .favorite = 0, .localOnly = 1},
        .createID = {{0x80, 0x00, 0x00, 0x03, 0xEC, 0xFF, 0x82, 0xD2}},
    },
    // "Guest E"
    {
        .faceline = {{.type = 0, .color = 0, .texture = 0}},
        .hair = {{.type = 14, .color = 7, .flip = 0}},
        .eye = {{.type = 4, .color = 5, .scale = 4, .rotate = 3, .x = 2, .y = 12}},
        .eyebrow = {{.type = 0, .color = 7, .scale = 4, .rotate = 6, .x = 2, .y = 10}},
        .nose = {{.type = 1, .scale = 4, .y = 9}},
        .mouth = {{.type = 23, .color = 0, .scale = 4, .y = 13}},
        .beard = {{.mustache = 0, .type = 0, .color = 0, .scale = 4, .y = 10}},
        .glass = {{.type = 0, .color = 0, .scale = 4, .y = 10}},
        .mole = {{.type = 0, .scale = 4, .x = 2, .y = 20}},
        .body = {.height = 64, .build = 64},
        .personal = {.name = L"no name", .creator = L"", .sex = 1, .bmonth = 0, .bday = 0, .color = 6, .favorite = 0, .localOnly = 1},
        .createID = {{0x80, 0x00, 0x00, 0x04, 0xEC, 0xFF, 0x82, 0xD2}},
    },
    // "Guest F"
    {
        .faceline = {{.type = 0, .color = 0, .texture = 0}},
        .hair = {{.type = 12, .color = 1, .flip = 0}},
        .eye = {{.type = 4, .color = 0, .scale = 4, .rotate = 3, .x = 2, .y = 12}},
        .eyebrow = {{.type = 0, .color = 1, .scale = 4, .rotate = 6, .x = 2, .y = 10}},
        .nose = {{.type = 1, .scale = 4, .y = 9}},
        .mouth = {{.type = 23, .color = 0, .scale = 4, .y = 13}},
        .beard = {{.mustache = 0, .type = 0, .color = 0, .scale = 4, .y = 10}},
        .glass = {{.type = 0, .color = 0, .scale = 4, .y = 10}},
        .mole = {{.type = 0, .scale = 4, .x = 2, .y = 20}},
        .body = {.height = 64, .build = 64},
        .personal = {.name = L"no name", .creator = L"", .sex = 1, .bmonth = 0, .bday = 0, .color = 7, .favorite = 0, .localOnly = 1},
        .createID = {{0x80, 0x00, 0x00, 0x05, 0xEC, 0xFF, 0x82, 0xD2}},
    },
};

void RFLiGetDefaultData(RFLiCharInfo* info, u16 index) {
    *info = scDefaultInfo[index];
}
