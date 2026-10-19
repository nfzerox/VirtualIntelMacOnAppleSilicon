/* Read-only capability query; does not enable custom ABI or change security. */
#include <os/arch/arm64.h>
#include <stdio.h>
#include <unistd.h>
int main(void)
{
    printf("x86_64 cross-architecture kernel support: %s\n",
           os_cross_arch_is_supported(OS_CROSS_ARCH_X86_64) ? "yes" : "no");
    printf("custom x18 ABI currently enabled: %s\n",
           os_custom_x18_abi_enabled() ? "yes" : "no");
    printf("process page size: %ld\n", sysconf(_SC_PAGESIZE));
    return 0;
}
