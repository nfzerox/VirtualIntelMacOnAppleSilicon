#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*
 * MTLCompiler 3902 was built against Apple's LLVM 5 ABI.  The Big Sur PVG
 * compiler uses Apple's LLVM 7 ABI.  Most of the ABI is still link-compatible;
 * this file supplies the one removed out-of-line helper.  It deliberately
 * returns the LLVM 5 MVT numbering expected by the 3902 caller while reading
 * an LLVM 7 Type, whose Type header and fixed-vector fields retain this layout.
 */
typedef struct {
    uint64_t simple_type;
    void *extended_type;
} PVGEVT;

typedef struct {
    void *context;
    uint32_t type_and_subclass;
} PVGTypeHeader;

typedef struct {
    PVGTypeHeader header;
    uint32_t reserved;
    uint32_t reserved2;
    void *element_type;
    uint32_t element_count;
} PVGVectorType;

typedef struct {
    void *begin;
    void *end;
    void *capacity;
} PVGLLVM5SmallVectorBase;

void pvg_llvm5_small_vector_grow(PVGLLVM5SmallVectorBase *vector,
                                 void *first_element,
                                 size_t minimum_bytes,
                                 size_t element_size)
    __asm__("__ZN4llvm15SmallVectorBase8grow_podEPvmm");

void pvg_llvm5_small_vector_grow(PVGLLVM5SmallVectorBase *vector,
                                 void *first_element,
                                 size_t minimum_bytes,
                                 size_t element_size)
{
    size_t current_bytes = (char *)vector->end - (char *)vector->begin;
    size_t new_bytes = 2 * ((char *)vector->capacity -
                            (char *)vector->begin) + element_size;
    if (new_bytes < minimum_bytes)
        new_bytes = minimum_bytes;

    void *elements;
    if (vector->begin == first_element) {
        elements = malloc(new_bytes);
        if (elements)
            memcpy(elements, vector->begin, current_bytes);
    } else {
        elements = realloc(vector->begin, new_bytes);
    }
    if (!elements)
        abort();

    vector->begin = elements;
    vector->end = (char *)elements + current_bytes;
    vector->capacity = (char *)elements + new_bytes;
}

/* MTLCompiler's undefined symbol is renamed to this non-colliding export.
 * A re-export dylib otherwise resolves the original name to LLVM 7 itself. */
void PVGLLVM5__SmallVectorBase8grow_podEPvmm(
    PVGLLVM5SmallVectorBase *vector, void *first_element,
    size_t minimum_bytes, size_t element_size)
{
    pvg_llvm5_small_vector_grow(vector, first_element, minimum_bytes,
                               element_size);
}

/* LLVM 7 compressed DataLayout's SmallVectors and changed the class layout.
 * Catalina's inline LLVM 5 copy-assignment calls clear() on its old layout. */
void PVGLLVM5__DataLayout5clearEv(void *opaque_layout)
{
    char *layout = opaque_layout;
    *(void **)(layout + 0x18) = *(void **)(layout + 0x10);
    *(void **)(layout + 0x38) = *(void **)(layout + 0x30);
    *(void **)(layout + 0xe8) = *(void **)(layout + 0xe0);

    /* StructLayoutMap is only a cache. Dropping it is semantically safe; the
     * short-lived compiler service may leak an existing cache at this bridge. */
    *(void **)(layout + 0x178) = 0;
}

void pvg_llvm7_data_layout_destroy(void *layout)
    __asm__("__ZN4llvm10DataLayoutD1Ev");

void PVGLLVM5__DataLayoutD1Ev(void *opaque_layout)
{
    pvg_llvm7_data_layout_destroy(opaque_layout);
}

/* MTLCompiler's stack-local copy is subsequently passed to LLVM 7 layout
 * queries, so construct an LLVM 7 object rather than copying LLVM 5
 * SmallVector offsets.  LLVM 7's exact Module::getDataLayout implementation
 * returns module + 0xf8; recover that module and use its exported constructor.
 * Every redirected caller obtains the source through getDataLayout(). */
void pvg_llvm7_data_layout_construct(void *destination, const void *module)
    __asm__("__ZN4llvm10DataLayoutC1EPKNS_6ModuleE");

void *PVGLLVM7__DataLayoutAssign__(void *destination, const void *source)
{
    const void *module = (const char *)source - 0xf8;
    pvg_llvm7_data_layout_construct(destination, module);
    return destination;
}

static PVGEVT pvg_simple(uint8_t value)
{
    PVGEVT result = { value, 0 };
    return result;
}

static uint8_t pvg_vector_type(uint8_t element, uint32_t count)
{
    static const struct { uint32_t count; uint8_t value; } i1[] = {
        {1,14}, {2,15}, {4,16}, {8,17}, {16,18}, {32,19}, {64,20},
        {512,21}, {1024,22}
    };
    static const struct { uint32_t count; uint8_t value; } i8[] = {
        {1,23}, {2,24}, {4,25}, {8,26}, {16,27}, {32,28}, {64,29},
        {128,30}, {256,31}
    };
    static const struct { uint32_t count; uint8_t value; } i16[] = {
        {1,32}, {2,33}, {4,34}, {8,35}, {16,36}, {32,37}, {64,38}, {128,39}
    };
    static const struct { uint32_t count; uint8_t value; } i32[] = {
        {1,40}, {2,41}, {4,42}, {8,43}, {16,44}, {32,45}, {64,46}
    };
    static const struct { uint32_t count; uint8_t value; } i64[] = {
        {1,47}, {2,48}, {4,49}, {8,50}, {16,51}, {32,52}
    };
    static const struct { uint32_t count; uint8_t value; } i128[] = {{1,53}};
    static const struct { uint32_t count; uint8_t value; } f16[] = {
        {2,84}, {4,85}, {8,86}
    };
    static const struct { uint32_t count; uint8_t value; } f32[] = {
        {1,87}, {2,88}, {4,89}, {8,90}, {16,91}
    };
    static const struct { uint32_t count; uint8_t value; } f64[] = {
        {1,92}, {2,93}, {4,94}, {8,95}
    };
    const void *table = 0;
    unsigned entries = 0;

#define SELECT(name) do { table = name; entries = sizeof(name) / sizeof(name[0]); } while (0)
    switch (element) {
        case 2: SELECT(i1); break;
        case 3: SELECT(i8); break;
        case 4: SELECT(i16); break;
        case 5: SELECT(i32); break;
        case 6: SELECT(i64); break;
        case 7: SELECT(i128); break;
        case 8: SELECT(f16); break;
        case 9: SELECT(f32); break;
        case 10: SELECT(f64); break;
        default: return 0;
    }
#undef SELECT

    const typeof(i1[0]) *values = table;
    for (unsigned index = 0; index < entries; ++index) {
        if (values[index].count == count)
            return values[index].value;
    }
    return 0;
}

PVGEVT pvg_llvm5_get_evt(void *opaque_type, bool handle_unknown)
    __asm__("__ZN4llvm3EVT6getEVTEPNS_4TypeEb");

PVGEVT pvg_llvm5_get_evt(void *opaque_type, bool handle_unknown)
{
    const PVGTypeHeader *type = opaque_type;
    const uint8_t type_id = type->type_and_subclass & 0xff;
    const uint32_t subclass = type->type_and_subclass >> 8;

    switch (type_id) {
        case 0: return pvg_simple(110); /* isVoid */
        case 1: return pvg_simple(8);   /* f16 */
        case 2: return pvg_simple(9);   /* f32 */
        case 3: return pvg_simple(10);  /* f64 */
        case 4: return pvg_simple(11);  /* f80 */
        case 5: return pvg_simple(12);  /* f128 */
        case 6: return pvg_simple(13);  /* ppcf128 */
        case 7: return pvg_simple(handle_unknown ? 1 : 0);
        case 8: return pvg_simple(249); /* Metadata */
        case 9: return pvg_simple(108); /* x86mmx */
        case 10: return pvg_simple(248); /* token */
        case 11:
            switch (subclass) {
                case 1: return pvg_simple(2);
                case 8: return pvg_simple(3);
                case 16: return pvg_simple(4);
                case 32: return pvg_simple(5);
                case 64: return pvg_simple(6);
                case 128: return pvg_simple(7);
                default: {
                    PVGEVT extended = { 0, opaque_type };
                    return extended;
                }
            }
        case 15: return pvg_simple(254); /* iPTR */
        case 16: {
            const PVGVectorType *vector = opaque_type;
            PVGEVT element = pvg_llvm5_get_evt(vector->element_type, false);
            uint8_t value = pvg_vector_type((uint8_t)element.simple_type,
                                            vector->element_count);
            if (value)
                return pvg_simple(value);
            PVGEVT extended = { 0, opaque_type };
            return extended;
        }
        default: return pvg_simple(handle_unknown ? 1 : 0);
    }
}
