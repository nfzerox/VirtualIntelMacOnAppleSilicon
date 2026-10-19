// Standalone import-probe interposer. Do not inject into a desktop or VM.
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <stdint.h>
static kern_return_t trace(io_connect_t connection, uint32_t selector,
    const uint64_t *input, uint32_t inputCount, const void *structure,
    size_t size, uint64_t *output, uint32_t *outputCount,
    void *outputStructure, size_t *outputSize)
{
    kern_return_t result = IOConnectCallMethod(connection, selector, input,
        inputCount, structure, size, output, outputCount, outputStructure, outputSize);
    if (selector == 9 && size == 0x68 &&
        ((const uint64_t *)structure)[0] == 0x80 &&
        (((const uint64_t *)structure)[9] == 8192 ||
         ((const uint64_t *)structure)[9] == 32768)) {
        fprintf(stderr, "resource-create result=%08x args=", result);
        const unsigned char *bytes = structure;
        for (size_t i = 0; i < size; i++) fprintf(stderr, "%02x", bytes[i]);
        fputc('\n', stderr);
    }
    return result;
}
__attribute__((used, section("__DATA,__interpose")))
static const struct { const void *replacement, *original; } hook = {
    (const void *)trace, (const void *)IOConnectCallMethod
};
