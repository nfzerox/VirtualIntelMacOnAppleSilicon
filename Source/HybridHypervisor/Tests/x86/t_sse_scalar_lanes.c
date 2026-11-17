#include "flatlibc.h"

typedef uint32_t FloatLanes __attribute__((vector_size(16)));
typedef uint64_t DoubleLanes __attribute__((vector_size(16)));

int flat_main(unsigned long a0, unsigned long a1) {
    (void)a0;
    (void)a1;
    FloatLanes single = {0x40800000, 0x12345678, 0x87654321, 0xabcdef01}; // 4.0f
    FloatLanes single_src = {0x40000000, 0, 0, 0}; // 2.0f
    DoubleLanes dbl = {0x4010000000000000, 0x1234567887654321}; // 4.0
    DoubleLanes double_src = {0x4000000000000000, 0}; // 2.0
    DoubleLanes double_square = {0x4022000000000000, 0xfedcba0987654321}; // 9.0
    int32_t integer = 3;
    int64_t wide_integer = 3;
    int failures = 0;

#define CHECK(op, dest, src, constraint, low) do { \
    __typeof__(dest) result = dest, expected = dest; \
    expected[0] = low; \
    __asm__ volatile(#op " %1, %0" : "+x"(result) : constraint(src)); \
    if (memcmp(&result, &expected, sizeof result)) { \
        printf("FAIL %s: scalar result or preserved upper lanes differ\n", #op); \
        failures++; \
    } \
} while (0)

    CHECK(addss, single, single_src, "x", 0x40c00000); // 6.0f
    CHECK(subss, single, single_src, "m", 0x40000000);
    CHECK(mulss, single, single_src, "x", 0x41000000); // 8.0f
    CHECK(divss, single, single_src, "m", 0x40000000);
    CHECK(minss, single, single_src, "x", 0x40000000);
    CHECK(maxss, single, single_src, "m", 0x40800000);
    CHECK(sqrtss, single, single, "m", 0x40000000);
    CHECK(cvtsi2ssl, single, integer, "r", 0x40400000); // 3.0f
    CHECK(cvtsd2ss, single, double_src, "x", 0x40000000);

    CHECK(addsd, dbl, double_src, "m", 0x4018000000000000); // 6.0
    CHECK(subsd, dbl, double_src, "x", 0x4000000000000000);
    CHECK(mulsd, dbl, double_src, "m", 0x4020000000000000); // 8.0
    CHECK(divsd, dbl, double_src, "x", 0x4000000000000000);
    CHECK(minsd, dbl, double_src, "m", 0x4000000000000000);
    CHECK(maxsd, dbl, double_src, "x", 0x4010000000000000);
    CHECK(sqrtsd, dbl, double_square, "x", 0x4008000000000000);
    CHECK(cvtsi2sdq, dbl, wide_integer, "m", 0x4008000000000000); // 3.0
    CHECK(cvtss2sd, dbl, single_src, "m", 0x4000000000000000);

#undef CHECK
    printf("Scalar SSE upper-lane failures: %d\n", failures);
    return failures != 0;
}
