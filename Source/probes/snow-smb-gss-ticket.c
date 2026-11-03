/* Diagnose Snow Leopard's stock GSS without sending or logging tokens. */
#include <Kerberos/gssapi.h>
#include <Kerberos/gssapi_krb5.h>
#include <CommonCrypto/CommonDigest.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static uint16_t read16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static uint32_t read32(const unsigned char *p) { return read16(p) | ((uint32_t)read16(p + 2) << 16); }
static void write16(unsigned char *p, unsigned n) { p[0] = n; p[1] = n >> 8; }
static void write32(unsigned char *p, uint32_t n) { write16(p, n); write16(p + 2, n >> 16); }

static int transfer(int fd, void *buffer, size_t n, int send_data)
{
    unsigned char *p = buffer;
    while (n) {
        ssize_t count = send_data ? send(fd, p, n, 0) : recv(fd, p, n, 0);
        if (count <= 0) return 0;
        p += count;
        n -= (size_t)count;
    }
    return 1;
}

static size_t exchange(int fd, unsigned char *packet, size_t n, unsigned char *reply, size_t capacity)
{
    unsigned char frame[4] = {0, n >> 16, n >> 8, n};
    if (!transfer(fd, frame, 4, 1) || !transfer(fd, packet, n, 1) ||
        !transfer(fd, frame, 4, 0)) return 0;
    n = ((size_t)frame[1] << 16) | ((size_t)frame[2] << 8) | frame[3];
    if (frame[0] || n < 35 || n > capacity || !transfer(fd, reply, n, 0) ||
        memcmp(reply, "\xffSMB", 4)) return 0;
    printf("Wire reply: command %02x status %08x words %u\n", reply[4], read32(reply + 5), reply[32]);
    return n;
}

static void header(unsigned char *p, unsigned command, unsigned uid, unsigned mid, unsigned flags)
{
    memset(p, 0, 32);
    memcpy(p, "\xffSMB", 4);
    p[4] = command; p[9] = 0x18;
    write16(p + 10, flags); write16(p + 24, 0xffff);
    write16(p + 26, getpid()); write16(p + 28, uid); write16(p + 30, mid);
}

static void sign_packet(const gss_krb5_lucid_key_t *key, unsigned char *packet,
                        size_t size, unsigned sequence, unsigned char signature[16])
{
    CC_MD5_CTX md5;
    memset(packet + 14, 0, 8);
    write32(packet + 14, sequence);
    CC_MD5_Init(&md5);
    CC_MD5_Update(&md5, key->data, key->length);
    CC_MD5_Update(&md5, packet, (CC_LONG)size);
    CC_MD5_Final(signature, &md5);
}

static int signed_echo(int fd, unsigned uid, unsigned mid, gss_ctx_id_t *ctx)
{
    OM_uint32 minor, ignored;
    void *data = NULL;
    OM_uint32 status = gss_krb5_export_lucid_sec_context(&minor, ctx, 1, &data);
    if (status != GSS_S_COMPLETE || !data) {
        printf("Session key unavailable: %u/%u\n", status, minor);
        return 0;
    }
    gss_krb5_lucid_context_v1_t *lucid = data;
    const gss_krb5_lucid_key_t *key = NULL;
    if (lucid->version == 1 && lucid->protocol == 0) key = &lucid->rfc1964_kd.ctx_key;
    if (lucid->version == 1 && lucid->protocol == 1)
        key = lucid->cfx_kd.have_acceptor_subkey ?
              &lucid->cfx_kd.acceptor_subkey : &lucid->cfx_kd.ctx_key;
    int valid = 0;
    if (key && key->data && key->length) {
        unsigned char packet[38], reply[256], digest[16], signature[8];
        header(packet, 0x2b, uid, mid, 0xc805);
        packet[32] = 1; write16(packet + 33, 1); write16(packet + 35, 1); packet[37] = 0;
        sign_packet(key, packet, sizeof(packet), 2, digest);
        memcpy(packet + 14, digest, 8);
        size_t n = exchange(fd, packet, sizeof(packet), reply, sizeof(reply));
        if (n && !read32(reply + 5) && (read16(reply + 10) & 4)) {
            memcpy(signature, reply + 14, 8);
            sign_packet(key, reply, n, 3, digest);
            valid = !memcmp(signature, digest, 8);
        }
    }
    gss_krb5_free_lucid_sec_context(&ignored, data);
    printf("Signed SMB1 request/reply: %s\n", valid ? "verified" : "not verified");
    return valid;
}

static int test_wire(const char *host, const char *port, gss_OID mechanism,
                     gss_name_t client, gss_name_t target)
{
    struct addrinfo hints = {0}, *addresses = NULL;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &addresses)) return 1;
    int fd = -1;
    for (struct addrinfo *a = addresses; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd >= 0 && !connect(fd, a->ai_addr, a->ai_addrlen)) break;
        if (fd >= 0) close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    if (fd < 0) return 1;
    struct timeval timeout = {10, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    unsigned char packet[8192], reply[8192];
    gss_cred_id_t creds = GSS_C_NO_CREDENTIAL;
    gss_ctx_id_t ctx = GSS_C_NO_CONTEXT;
    gss_OID_set_desc desired = {1, mechanism};
    OM_uint32 major, minor, ignored;
    int result = 1;
    header(packet, 0x72, 0, 1, 0xc801);
    packet[32] = 0; write16(packet + 33, 12); memcpy(packet + 35, "\2NT LM 0.12", 12);
    size_t n = exchange(fd, packet, 47, reply, sizeof(reply));
    if (n < 85 || read32(reply + 5) || reply[32] != 17 ||
        !(read32(reply + 52) & 0x80000000u)) goto done;
    uint32_t caps = read32(reply + 52);
    major = gss_acquire_cred(&minor, client, GSS_C_INDEFINITE, &desired,
                            GSS_C_INITIATE, &creds, NULL, NULL);
    if (major != GSS_S_COMPLETE) {
        printf("Wire acquire: %u/%u\n", major, minor);
        goto done;
    }
    gss_buffer_desc input = GSS_C_EMPTY_BUFFER;
    unsigned uid = 0;
    for (unsigned round = 0; round < 4; ++round) {
        gss_buffer_desc output = GSS_C_EMPTY_BUFFER;
        major = gss_init_sec_context(&minor, creds, &ctx, target, mechanism,
            GSS_C_MUTUAL_FLAG, 0, GSS_C_NO_CHANNEL_BINDINGS,
            input.length ? &input : GSS_C_NO_BUFFER, NULL, &output, NULL, NULL);
        printf("Wire GSS round %u: %u/%u, token bytes: %lu\n", round + 1, major, minor,
               (unsigned long)output.length);
        if (major == GSS_S_COMPLETE && !output.length && round && !read32(reply + 5)) {
            if (!(read16(reply + 37) & 1)) {
                puts("Registered Kerberos session accepted");
                result = signed_echo(fd, uid, round + 2, &ctx) ? 0 : 1;
            }
            gss_release_buffer(&ignored, &output);
            break;
        }
        if ((major != GSS_S_COMPLETE && major != GSS_S_CONTINUE_NEEDED) ||
            !output.length || output.length > sizeof(packet) - 64) {
            gss_release_buffer(&ignored, &output);
            break;
        }
        header(packet, 0x73, uid, round + 2, 0xc805);
        memcpy(packet + 14, "BSRSPLY ", 8);
        packet[32] = 12; memset(packet + 33, 0, 26);
        packet[33] = 0xff; write16(packet + 37, 61440); write16(packet + 39, 16);
        write16(packet + 41, 1); write16(packet + 47, output.length);
        write32(packet + 53, caps | 0x80000054u);
        n = 59 + output.length;
        memcpy(packet + 59, output.value, output.length);
        gss_release_buffer(&ignored, &output);
        if (n & 1) packet[n++] = 0;
        memset(packet + n, 0, 4); n += 4;
        write16(packet + 57, n - 59);
        n = exchange(fd, packet, n, reply, sizeof(reply));
        if (n < 43 || reply[32] != 4 ||
            (read32(reply + 5) && read32(reply + 5) != 0xc0000016u)) break;
        uid = read16(reply + 28);
        input.length = read16(reply + 39);
        if (input.length > n - 43) break;
        input.value = reply + 43;
    }
done:
    if (ctx != GSS_C_NO_CONTEXT) gss_delete_sec_context(&ignored, &ctx, GSS_C_NO_BUFFER);
    if (creds != GSS_C_NO_CREDENTIAL) gss_release_cred(&ignored, &creds);
    close(fd);
    return result;
}

static void check(const char *label, gss_OID mechanism, gss_name_t client, gss_name_t target)
{
    OM_uint32 major, minor, ignored;
    gss_OID_set_desc desired = { 1, mechanism };
    gss_cred_id_t credentials = GSS_C_NO_CREDENTIAL;
    gss_ctx_id_t context = GSS_C_NO_CONTEXT;
    gss_buffer_desc token = GSS_C_EMPTY_BUFFER;
    major = gss_acquire_cred(&minor, client, GSS_C_INDEFINITE, &desired,
        GSS_C_INITIATE, &credentials, NULL, NULL);
    printf("%s acquire: %u/%u\n", label, major, minor);
    if (major == GSS_S_COMPLETE) {
        major = gss_init_sec_context(&minor, credentials, &context, target,
            mechanism, GSS_C_MUTUAL_FLAG, 0, GSS_C_NO_CHANNEL_BINDINGS,
            GSS_C_NO_BUFFER, NULL, &token, NULL, NULL);
        printf("%s init: %u/%u, token bytes: %lu\n", label, major, minor,
            (unsigned long)token.length);
        gss_release_buffer(&ignored, &token);
        if (context != GSS_C_NO_CONTEXT)
            gss_delete_sec_context(&ignored, &context, GSS_C_NO_BUFFER);
        gss_release_cred(&ignored, &credentials);
    }
}

int main(int argc, char **argv)
{
    if (argc != 3 && argc != 5) return 64;
    OM_uint32 minor, major, ignored;
    gss_OID_desc principal_type = *GSS_KRB5_NT_PRINCIPAL_NAME;
    gss_name_t names[2];
    for (unsigned i = 0; i < 2; ++i) {
        gss_buffer_desc name = { strlen(argv[i + 1]), argv[i + 1] };
        major = gss_import_name(&minor, &name, &principal_type, &names[i]);
        printf("principal %u import: %u/%u\n", i, major, minor);
        if (major != GSS_S_COMPLETE) return 1;
    }
    gss_OID_desc spnego = { 6, (void *)"\x2b\x06\x01\x05\x05\x02" };
    int result = 0;
    if (argc == 5) {
        result = test_wire(argv[3], argv[4], &spnego, names[0], names[1]);
    } else {
        check("Kerberos", (gss_OID)gss_mech_krb5, names[0], names[1]);
        check("SPNEGO", &spnego, names[0], names[1]);
    }
    gss_release_name(&ignored, &names[0]);
    gss_release_name(&ignored, &names[1]);
    return result;
}
