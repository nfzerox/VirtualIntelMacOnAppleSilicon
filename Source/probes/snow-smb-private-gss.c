/* Finite research service for one Snow Leopard NetFS connection.
 * A private task port, not a launchd job or replacement for system gssd. */
#include <Kerberos/gssapi.h>
#include <Kerberos/gssapi_krb5.h>
#include <mach/mach.h>
#include <mach/task_special_ports.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "gssd_machServer.h"

static mach_port_t receiver, original;
static pthread_t worker;
static const char *client_principal, *service_principal;
static gss_name_t target_name = GSS_C_NO_NAME;
static gss_cred_id_t client_creds = GSS_C_NO_CREDENTIAL;
static gss_ctx_id_t session = GSS_C_NO_CONTEXT;
static gss_OID_desc spnego = {6, (void *)"\x2b\x06\x01\x05\x05\x02"};

static int copy_reply(const void *data, size_t size, byte_buffer *buffer,
                      mach_msg_type_number_t *count)
{
    if (!size) return 1;
    if (size > 65536) return 0;
    vm_address_t address = 0;
    if (vm_allocate(mach_task_self(), &address, size, VM_FLAGS_ANYWHERE)) return 0;
    memcpy((void *)address, data, size);
    *buffer = (void *)address;
    *count = (mach_msg_type_number_t)size;
    return 1;
}

kern_return_t svc_mach_gss_init_sec_context(mach_port_t server, mechtype mech,
    byte_buffer input, mach_msg_type_number_t input_size, uint32_t uid,
    string_t client, string_t service, uint32_t flags, uint32_t gssd_flags,
    gss_ctx *context, gss_cred *cred, uint32_t *ret_flags, byte_buffer *key,
    mach_msg_type_number_t *key_size, byte_buffer *token, mach_msg_type_number_t *token_size,
    uint32_t *major, uint32_t *minor)
{
    (void)gssd_flags;
    *key = *token = NULL;
    *key_size = *token_size = *ret_flags = *minor = 0;
    *major = GSS_S_BAD_NAME;
    if (server != receiver || mech != SPNEGO_MECH || uid != getuid() ||
        strcmp(client, client_principal) || strcmp(service, service_principal) ||
        input_size > 65536 || *context != (session != GSS_C_NO_CONTEXT) ||
        *cred != *context) return KERN_SUCCESS;
    gss_buffer_desc incoming = {input_size, input}, outgoing = GSS_C_EMPTY_BUFFER;
    OM_uint32 ignored;
    *major = gss_init_sec_context(minor, client_creds, &session, target_name, &spnego,
        flags | GSS_C_MUTUAL_FLAG, 0, GSS_C_NO_CHANNEL_BINDINGS,
        input_size ? &incoming : GSS_C_NO_BUFFER, NULL, &outgoing, ret_flags, NULL);
    if ((*major == GSS_S_COMPLETE || *major == GSS_S_CONTINUE_NEEDED) &&
        !copy_reply(outgoing.value, outgoing.length, token, token_size))
        *major = GSS_S_FAILURE;
    gss_release_buffer(&ignored, &outgoing);
    if (*major == GSS_S_COMPLETE) {
        void *data = NULL;
        if (!(*ret_flags & GSS_C_MUTUAL_FLAG) ||
            gss_krb5_export_lucid_sec_context(minor, &session, 1, &data) ||
            !data) {
            *major = GSS_S_FAILURE;
        } else {
            gss_krb5_lucid_context_v1_t *state = data;
            const gss_krb5_lucid_key_t *value = NULL;
            if (state->version == 1 && state->protocol == 0) value = &state->rfc1964_kd.ctx_key;
            if (state->version == 1 && state->protocol == 1)
                value = state->cfx_kd.have_acceptor_subkey ?
                    &state->cfx_kd.acceptor_subkey : &state->cfx_kd.ctx_key;
            if (!value || !value->length || !copy_reply(value->data, value->length, key, key_size))
                *major = GSS_S_FAILURE;
            gss_krb5_free_lucid_sec_context(&ignored, data);
        }
    }
    *context = *cred = *major == GSS_S_CONTINUE_NEEDED;
    printf("Private GSS service: %u/%u, token %u, key %u bytes\n",
           *major, *minor, *token_size, *key_size);
    return KERN_SUCCESS;
}

kern_return_t svc_mach_gss_accept_sec_context(mach_port_t server, byte_buffer input,
    mach_msg_type_number_t input_size, string_t service, uint32_t gssd_flags,
    gss_ctx *context, gss_cred *cred, uint32_t *flags, uint32_t *uid,
    gid_list gids, mach_msg_type_number_t *gid_count, byte_buffer *key,
    mach_msg_type_number_t *key_size, byte_buffer *token, mach_msg_type_number_t *token_size,
    uint32_t *major, uint32_t *minor)
{
    (void)server; (void)input; (void)input_size; (void)service; (void)gssd_flags;
    (void)context; (void)cred; (void)flags; (void)uid; (void)gids; (void)gid_count;
    (void)key; (void)key_size; (void)token; (void)token_size; (void)major; (void)minor;
    return KERN_NOT_SUPPORTED;
}

kern_return_t svc_mach_gss_log_error(mach_port_t server, string_t mountpoint, uint32_t uid,
    string_t source, uint32_t major, uint32_t minor)
{
    (void)server; (void)mountpoint; (void)uid; (void)source;
    printf("Private GSS client report: %u/%u\n", major, minor);
    return KERN_SUCCESS;
}

static void *serve(void *unused)
{
    (void)unused;
    mach_msg_server(gssd_mach_server, 8192, receiver, 0);
    return NULL;
}

int snow_private_gss_start(const char *client, const char *target)
{
    gss_name_t source;
    gss_buffer_desc from = {strlen(client), (void *)client}, to = {strlen(target), (void *)target};
    gss_OID_desc type = *GSS_KRB5_NT_PRINCIPAL_NAME;
    gss_OID_set_desc desired = {1, &spnego};
    OM_uint32 minor, ignored;
    if (gss_import_name(&minor, &from, &type, &source) ||
        gss_import_name(&minor, &to, &type, &target_name) ||
        gss_acquire_cred(&minor, source, GSS_C_INDEFINITE, &desired, GSS_C_INITIATE,
                         &client_creds, NULL, NULL)) return 0;
    gss_release_name(&ignored, &source);
    client_principal = client;
    service_principal = target;
    mach_port_t task = mach_task_self();
    if (task_get_special_port(task, TASK_GSSD_PORT, &original) ||
        mach_port_allocate(task, MACH_PORT_RIGHT_RECEIVE, &receiver) ||
        mach_port_insert_right(task, receiver, receiver, MACH_MSG_TYPE_MAKE_SEND) ||
        task_set_special_port(task, TASK_GSSD_PORT, receiver)) return 0;
    if (!pthread_create(&worker, NULL, serve, NULL)) return 1;
    task_set_special_port(task, TASK_GSSD_PORT, original);
    mach_port_destroy(task, receiver);
    return 0;
}

void snow_private_gss_stop(void)
{
    mach_port_t task = mach_task_self();
    task_set_special_port(task, TASK_GSSD_PORT, original);
    mach_port_destroy(task, receiver);
    pthread_join(worker, NULL);
    if (MACH_PORT_VALID(original)) mach_port_deallocate(task, original);
    OM_uint32 minor;
    if (session != GSS_C_NO_CONTEXT) gss_delete_sec_context(&minor, &session, GSS_C_NO_BUFFER);
    gss_release_cred(&minor, &client_creds);
    gss_release_name(&minor, &target_name);
}
