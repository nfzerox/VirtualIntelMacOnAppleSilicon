// Long-mode kernel far jump with a transient null stack selector. Mirrors
// Leopard's early-boot trampoline, not a permanently eligible Engine context.
#define TEST_KERNEL_FAR_JUMPS 1
#define TEST_KERNEL_FAR_NULL_SS 1
#include "descriptor-widths.c"
