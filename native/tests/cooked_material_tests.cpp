#include <nebula_cooked_material.h>

#include <cstdio>

int main() {
    alignas(16) unsigned char bytes[32 + sizeof(NebulaCookedMaterial) + sizeof(NebulaCookedMaterialFooter)]{};
    NebulaCookedMaterial material{};
    material.shader = 7;
    memcpy(bytes + 32, &material, sizeof(material));
    NebulaCookedMaterialFooter footer{{'N', 'M', 'A', 'T'}, 3, 1, sizeof(NebulaCookedMaterial)};
    memcpy(bytes + sizeof(bytes) - sizeof(footer), &footer, sizeof(footer));
    uint32_t count = 0;
    const auto* read = nebula_cooked_materials(bytes, sizeof(bytes), &count);
    if (count != 1 || read == nullptr || read->shader != 7) return 1;
    footer.stride = 31;
    memcpy(bytes + sizeof(bytes) - sizeof(footer), &footer, sizeof(footer));
    if (nebula_cooked_materials(bytes, sizeof(bytes), &count) != nullptr || count != 0) return 2;
    footer.stride = sizeof(NebulaCookedMaterial);
    footer.count = UINT32_MAX;
    memcpy(bytes + sizeof(bytes) - sizeof(footer), &footer, sizeof(footer));
    if (nebula_cooked_materials(bytes, sizeof(bytes), &count) != nullptr || count != 0) return 3;
    footer.count = 1;
    footer.version = 4;
    memcpy(bytes + sizeof(bytes) - sizeof(footer), &footer, sizeof(footer));
    if (nebula_cooked_materials(bytes, sizeof(bytes), &count) != nullptr) return 4;
    for (size_t size = 0; size < sizeof(bytes); ++size) {
        if (nebula_cooked_materials(bytes, size, &count) != nullptr) return 5;
    }
    std::puts("cooked material reader: valid trailer and invalid bounds/version/stride passed");
}
