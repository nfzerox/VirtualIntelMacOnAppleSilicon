#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef kern_return_t (*call_method_fn)(mach_port_t, uint32_t,
    const uint64_t *, uint32_t, const void *, size_t, uint64_t *, uint32_t *,
    void *, size_t *);

static void translate_lock_result(void *catalina_output,
    const void *bigsur_output)
{
    const size_t common_size = 0x44;
    const size_t catalina_plane_size = 0x6c;
    const size_t bigsur_plane_size = 0x78;
    const size_t bigsur_plane_offset = 0x54;
    const size_t plane_count = 32;
    const size_t trailer_size = 0xc;
    unsigned char *destination = catalina_output;
    const unsigned char *source = bigsur_output;

    memcpy(destination, source, common_size);
    for (size_t plane = 0; plane < plane_count; ++plane) {
        memcpy(destination + common_size + plane * catalina_plane_size,
            source + bigsur_plane_offset + plane * bigsur_plane_size,
            catalina_plane_size);
    }
    memcpy(destination + common_size + plane_count * catalina_plane_size,
        source + bigsur_plane_offset + plane_count * bigsur_plane_size,
        trailer_size);
}

static kern_return_t traced_IOConnectCallMethod(mach_port_t connection,
    uint32_t selector, const uint64_t *input, uint32_t input_count,
    const void *input_struct, size_t input_struct_count, uint64_t *output,
    uint32_t *output_count, void *output_struct, size_t *output_struct_count)
{
    static call_method_fn original;
    if (!original)
        original = (call_method_fn)dlsym(RTLD_NEXT, "IOConnectCallMethod");
    size_t before = output_struct_count ? *output_struct_count : 0;
    uint64_t mach_port_input[2];
    const uint64_t *donor_input = input;
    uint32_t donor_input_count = input_count;
    if (selector == 35 && input && input_count == 1) {
        mach_port_input[0] = input[0];
        mach_port_input[1] = 0;
        donor_input = mach_port_input;
        donor_input_count = 2;
    }
    kern_return_t result;
    if ((selector == 0 || selector == 2 || selector == 34) && output_struct &&
        output_struct_count &&
        before == 0xdd0) {
        size_t donor_count = 0xf60;
        void *donor_output = calloc(1, donor_count);
        result = original(connection, selector, donor_input, donor_input_count,
            input_struct, input_struct_count, output, output_count,
            donor_output, &donor_count);
        if (result == KERN_SUCCESS) {
            translate_lock_result(output_struct, donor_output);
            *output_struct_count = before;
        }
        fprintf(stderr, "selector-0 donor-struct-out=0x%zx\n", donor_count);
        free(donor_output);
    } else {
        result = original(connection, selector, donor_input, donor_input_count,
            input_struct, input_struct_count, output, output_count,
            output_struct, output_struct_count);
    }
    size_t after = output_struct_count ? *output_struct_count : 0;
    fprintf(stderr,
        "IOConnectCallMethod connection=0x%x selector=%u scalar-in=%u "
        "struct-in=%zu struct-out=%zu->%zu result=0x%x\n",
        connection, selector, input_count, input_struct_count, before, after,
        result);
    return result;
}

__attribute__((used)) static const struct {
    const void *replacement;
    const void *replacee;
} interpose_IOConnectCallMethod __attribute__((section("__DATA,__interpose"))) = {
    (const void *)traced_IOConnectCallMethod,
    (const void *)IOConnectCallMethod,
};
