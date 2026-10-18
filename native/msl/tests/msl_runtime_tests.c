/* MSL behaviours the game relies on, checked against MSL_C/wprintf.c, wstring.c and extras.c. */
#include <stdio.h>
#include <string.h>

static int failures;

static void expect_wide(const wchar_t* got, const char* want, int count, int want_count, const char* what) {
    size_t i = 0;
    for (; want[i]; ++i) {
        if (got[i] != (unsigned char)want[i]) {
            break;
        }
    }
    if (want[i] || got[i] || count != want_count) {
        printf("FAIL %s: count %d (want %d)\n", what, count, want_count);
        ++failures;
    }
}

static void expect_narrow(const char* got, const char* want, int count, int want_count, const char* what) {
    if (strcmp(got, want) != 0 || count != want_count) {
        printf("FAIL %s: \"%s\" count %d (want \"%s\", %d)\n", what, got, count, want, want_count);
        ++failures;
    }
}

int main(void) {
    wchar_t buf[64];
    int n;

    n = swprintf(buf, 64, L"%02d", 5);
    expect_wide(buf, "05", n, 2, "%02d");
    n = swprintf(buf, 64, L"%06d", -12);
    expect_wide(buf, "-00012", n, 6, "zero fill keeps the sign first");
    n = swprintf(buf, 64, L"%d", -2147483647 - 1);
    expect_wide(buf, "-2147483648", n, 11, "INT_MIN");
    n = swprintf(buf, 64, L"%ld", 7);
    expect_wide(buf, "7", n, 1, "%ld reads the console's 32-bit long");
    n = swprintf(buf, 64, L"%x/%X/%#x", 0xabcu, 0xabcu, 0xabcu);
    expect_wide(buf, "abc/ABC/0xabc", n, 13, "hex");
    n = swprintf(buf, 64, L"%-4d|", 3);
    expect_wide(buf, "3   |", n, 5, "left justification");
    n = swprintf(buf, 64, L"%5.2s|", "abcd");
    expect_wide(buf, "   ab|", n, 6, "narrow string precision and width");

    /* The game's own pattern (PowerStarList): the destination is also the first argument. */
    n = swprintf(buf, 64, L"Star");
    n = swprintf(buf, 64, L"%ls%s%ls", buf, "\n", L"Name");
    expect_wide(buf, "Star\nName", n, 9, "destination as an argument");

    /* Too long: the first n - 1 characters, NUL, and -1. */
    n = swprintf(buf, 4, L"%d", 123456);
    expect_wide(buf, "123", n, -1, "truncation");

    /* An invalid conversion copies the rest of the format and ends the output. */
    n = swprintf(buf, 64, L"a%qb%d", 1);
    expect_wide(buf, "a%qb%d", n, 6, "invalid conversion");

    /* %c of NUL writes nothing (MSL's mbtowc). */
    n = swprintf(buf, 64, L"[%c]", 0);
    expect_wide(buf, "[]", n, 2, "%c NUL");

    if (wcslen(L"abc") != 3 || wcscmp(L"ab", L"ac") >= 0 || wcscmp(L"ab", L"ab") != 0) {
        printf("FAIL wide strings\n");
        ++failures;
    }
    wcsncpy(buf, L"xy", 5);
    if (buf[0] != L'x' || buf[2] != 0 || buf[4] != 0) {
        printf("FAIL wcsncpy pads with NULs\n");
        ++failures;
    }
    if (wcschr(L"abc", L'c') == 0 || wcschr(L"abc", L'z') != 0 || *wcschr(L"abc", 0) != 0) {
        printf("FAIL wcschr\n");
        ++failures;
    }
    if (stricmp("ABC_x", "abc_X") != 0 || stricmp("a", "B") != -1 || strcasecmp("b", "A") != 1) {
        printf("FAIL stricmp\n");
        ++failures;
    }
    /* Signed comparison: bytes from 0x80 sort below ASCII. */
    if (stricmp("\x80", "a") != -1) {
        printf("FAIL stricmp sign\n");
        ++failures;
    }
    if (abs(-3) != 3 || abs((int)-2.9f) != 2) {
        printf("FAIL abs\n");
        ++failures;
    }
    {
        float nan_f;
        unsigned int bits = 0x7FC00000u;
        memcpy(&nan_f, &bits, 4);
        if (__fpclassifyf(nan_f) != 1 || __fpclassifyf(1.0f) != 4 || __fpclassifyf(0.0f) != 3 ||
            __fpclassifyf(1e-40f) != 5) {
            printf("FAIL fpclassify codes\n");
            ++failures;
        }
    }

    /* The narrow formatter, as MSL_C/printf.c: a null string prints as nothing, %ld is 32 bits, %#s is a Pascal
       string, %c writes a NUL byte, the count is the whole output's even when it does not fit. */
    {
        char text[64];
        const char* none = 0;
        int count = sprintf(text, "[%s]/%s", none, "x");
        expect_narrow(text, "[]/x", count, 4, "null %s");
        count = sprintf(text, "%ld|%5.2s|%-3d|", 7L, "abcd", 2);
        expect_narrow(text, "7|   ab|2  |", count, 12, "%ld, precision, left");
        count = sprintf(text, "%#s", "\003abcdef");
        expect_narrow(text, "abc", count, 3, "Pascal string");
        count = snprintf(text, 4, "%d", 123456);
        expect_narrow(text, "123", count, 6, "truncation keeps the whole count");
        count = sprintf(text, "%02d:%02d", 3, 7);
        expect_narrow(text, "03:07", count, 5, "zero fill");
        count = sprintf(text, "a%qb%d", 1);
        expect_narrow(text, "a%qb%d", count, 6, "invalid conversion");
        count = sprintf(text, "%s%d", "/anim", 3);
        expect_narrow(text, "/anim3", count, 6, "path");
        if (sprintf(text, "%c", 0) != 1 || text[0] != 0) {
            printf("FAIL %%c NUL\n");
            ++failures;
        }
    }

    printf(failures ? "%d failures\n" : "all passed\n", failures);
    return failures != 0;
}
