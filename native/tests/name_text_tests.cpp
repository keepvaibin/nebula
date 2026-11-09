// Ui::NameText: a name of any length, copied: in itself when short, on the heap when longer.
#include "Game/Ui/UiTypes.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace {
    int sFailures;

    void check(bool ok, const char* pWhat) {
        if (!ok) {
            printf("FAIL %s\n", pWhat);
            sFailures++;
        }
    }

    typedef Ui::NameText< 16 > Name;

    bool same(const Name& rName, const char* pWant) {
        return strcmp(rName, pWant) == 0;
    }
}  // namespace

int main() {
    Name name;
    check(same(name, ""), "a new name is empty");

    name.set("T_Short");
    check(same(name, "T_Short"), "set short");

    // 15 characters fit in itself, 16 and more go to the heap; each reads back whole.
    for (size_t length : {0u, 1u, 15u, 16u, 17u, 63u, 64u, 255u, 256u, 1000u, 5000u}) {
        const std::string text(length, 'p');
        name.set(text.c_str());
        check(strlen(name) == length, "set keeps every character");
        check(same(name, text.c_str()), "set reads back");
    }

    // A text inside the name's own storage: copied before the old storage goes.
    const std::string longText = std::string(40, 'a') + "TAIL";
    name.set(longText.c_str());
    name.set(static_cast< const char* >(name) + 40);
    check(same(name, "TAIL"), "set from its own heap text");
    name.set("0123456789");
    name.set(static_cast< const char* >(name) + 4);
    check(same(name, "456789"), "set from its own inline text");

    if (sFailures != 0) {
        printf("%d name text test failures\n", sFailures);
        return 1;
    }
    printf("name text tests passed\n");
    return 0;
}
