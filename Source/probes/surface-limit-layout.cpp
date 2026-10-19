// Host-only fixture for the bounded private-field recognizer; never loads a kext.
#include "../compat/PVGIOSurfaceABI/PVGSurfaceLimits.h"
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <fstream>
#include <iterator>

static std::vector<uint8_t> read(const char *path) {
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

int main(int argc, char **argv) {
    const uint8_t init[] = {0xc7,0x87,0xb8,0x0c,0,0,0,0x10,0,0x10};
    const uint8_t check[] = {0x0f,0xb7,0x88,0xb8,0x0c,0,0,0x0f,0xb7,0x80,0xba,0x0c,0,0};
    uint32_t offset = 0;
    assert(PVGSurfaceLimits::findDimensionOffset(init,sizeof(init),check,sizeof(check),offset));
    assert(offset == 0xcb8);
    offset = 0xdead;
    assert(!PVGSurfaceLimits::findDimensionOffset(init,sizeof(init)-1,check,sizeof(check),offset));
    assert(offset == 0xdead);
    auto ambiguous = std::vector<uint8_t>(init,init+sizeof(init));
    ambiguous.insert(ambiguous.end(),init,init+sizeof(init));
    assert(!PVGSurfaceLimits::findDimensionOffset(ambiguous.data(),ambiguous.size(),check,sizeof(check),offset));
    auto bad = std::vector<uint8_t>(check,check+sizeof(check));
    bad[10] = 0xbc;
    assert(!PVGSurfaceLimits::findDimensionOffset(init,sizeof(init),bad.data(),bad.size(),offset));
    auto unaligned = std::vector<uint8_t>(init,init+sizeof(init));
    unaligned[2]++;
    assert(!PVGSurfaceLimits::findDimensionOffset(unaligned.data(),unaligned.size(),check,sizeof(check),offset));
    assert(!PVGSurfaceLimits::findDimensionOffset(nullptr,10,check,sizeof(check),offset));
    const uint8_t populate[] = {0xc7,0x46,0x30,0,0x10,0,0x10};
    assert(PVGSurfaceLimits::findPopulatedDimensionOffset(populate,sizeof(populate),offset));
    assert(offset == 0x30);
    offset = 0xdead;
    assert(!PVGSurfaceLimits::findPopulatedDimensionOffset(populate,sizeof(populate)-1,offset));
    assert(offset == 0xdead);
    auto duplicate = std::vector<uint8_t>(populate,populate+sizeof(populate));
    duplicate.insert(duplicate.end(),populate,populate+sizeof(populate));
    assert(!PVGSurfaceLimits::findPopulatedDimensionOffset(duplicate.data(),duplicate.size(),offset));
    const uint8_t wide[] = {0xc7,0x86,0x30,0,0,0,0,0x10,0,0x10};
    assert(PVGSurfaceLimits::findPopulatedDimensionOffset(wide,sizeof(wide),offset));
    assert(offset == 0x30);
    const uint8_t wrongBase[] = {0x41,0xc7,0x46,0x30,0,0x10,0,0x10};
    assert(!PVGSurfaceLimits::findPopulatedDimensionOffset(wrongBase,sizeof(wrongBase),offset));
    if (argc == 3 || argc == 4) {
        auto actualInit = read(argv[1]), actualValidator = read(argv[2]);
        assert(PVGSurfaceLimits::findDimensionOffset(actualInit.data(),actualInit.size(),
            actualValidator.data(),actualValidator.size(),offset));
        printf("donor initializer + validator: unique offset 0x%x\n",offset);
        if(argc == 4) {
            auto actualPopulate = read(argv[3]);
            assert(PVGSurfaceLimits::findPopulatedDimensionOffset(actualPopulate.data(),actualPopulate.size(),offset));
            printf("donor populator: independent config offset 0x%x\n",offset);
        }
    } else assert(argc == 1);
    puts("surface-limit layout fixtures passed");
}
