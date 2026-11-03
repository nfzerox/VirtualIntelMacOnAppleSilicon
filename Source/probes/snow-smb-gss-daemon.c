/* Call Snow Leopard's real task-local GSS service with explicit principals.
 * Generate gssd_mach.h and the client stub from the 10.6 SDK's MIG definition. */
#include <mach/mach.h>
#include <mach/task_special_ports.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "gssd_mach.h"

int main(int argc, char **argv)
{
    /* The period MIG definition declares string_t as c_string[*:1024]. */
    char client[1024], service[1024];
    if (argc != 3 || strlen(argv[1]) >= sizeof(client) ||
        strlen(argv[2]) >= sizeof(service)) return 64;
    strlcpy(client, argv[1], sizeof(client));
    strlcpy(service, argv[2], sizeof(service));
    mach_port_t gssd = MACH_PORT_NULL;
    kern_return_t kr = task_get_special_port(mach_task_self(), TASK_GSSD_PORT, &gssd);
    printf("GSS special port: %d, available: %d\n", kr, MACH_PORT_VALID(gssd));
    if (kr || !MACH_PORT_VALID(gssd)) return 1;
    gss_ctx context = 0;
    gss_cred credentials = 0;
    uint32_t flags = 0, major = 0, minor = 0;
    byte_buffer key = NULL, token = NULL;
    mach_msg_type_number_t key_size = 0, token_size = 0;
    kr = mach_gss_init_sec_context(gssd, SPNEGO_MECH, NULL, 0, getuid(), client, service,
        GSSD_MUTUAL_FLAG | GSSD_C_DELEG_POLICY_FLAG, 0, &context, &credentials,
        &flags, &key, &key_size, &token, &token_size, &major, &minor);
    printf("GSS service: %d, status: %u/%u, token bytes: %u\n",
           kr, major, minor, token_size);
    if (key) {
        volatile unsigned char *bytes = key;
        for (mach_msg_type_number_t i = 0; i < key_size; ++i) bytes[i] = 0;
        vm_deallocate(mach_task_self(), (vm_address_t)key, key_size);
    }
    if (token) vm_deallocate(mach_task_self(), (vm_address_t)token, token_size);
    mach_port_deallocate(mach_task_self(), gssd);
    return kr || !token_size;
}
