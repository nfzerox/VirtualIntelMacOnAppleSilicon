/* Experimental adapter included by Darwin 10 smb_gss.c, after smb_gss_ssandx.
 * Reuse Leopard's wire codec; keep Snow Leopard's native request and signer.
 * BSD-2-Clause. Not packaged as a guest tool. */
#include <libkern/crypto/md5.h>
#include <sys/utfconv.h>
#include <fs/smbfs/smbfs_subr.h>
#include "vim_ntlmssp_wire.h"

static void trial_erase(void *p, size_t n)
{
    volatile uint8_t *bytes = p;
    while (n--) *bytes++ = 0;
}

static void *trial_allocate(size_t n) { return malloc(n, M_SMBTEMP, M_WAITOK); }
static void trial_release(void *p, size_t n)
{
    if (p) { trial_erase(p, n); free(p, M_SMBTEMP); }
}

static void trial_digest(const struct vim_ntlm_slice *v, size_t n, uint8_t out[16])
{
    MD5_CTX context;
    MD5Init(&context);
    for (size_t i = 0; i < n; ++i) MD5Update(&context, v[i].data, v[i].size);
    MD5Final(out, &context);
    trial_erase(&context, sizeof(context));
}

static void trial_hmac(const uint8_t *key, size_t size, const struct vim_ntlm_slice *v,
                       size_t n, uint8_t out[16])
{
    uint8_t pad[64] = {0}, short_key[16], inner[16];
    MD5_CTX context;
    if (size > sizeof(pad)) {
        struct vim_ntlm_slice part = { key, size };
        trial_digest(&part, 1, short_key);
        key = short_key;
        size = sizeof(short_key);
    }
    memcpy(pad, key, size);
    for (size_t i = 0; i < sizeof(pad); ++i) pad[i] ^= 0x36;
    MD5Init(&context);
    MD5Update(&context, pad, sizeof(pad));
    for (size_t i = 0; i < n; ++i) MD5Update(&context, v[i].data, v[i].size);
    MD5Final(inner, &context);
    for (size_t i = 0; i < sizeof(pad); ++i) pad[i] ^= 0x36 ^ 0x5c;
    MD5Init(&context);
    MD5Update(&context, pad, sizeof(pad));
    MD5Update(&context, inner, sizeof(inner));
    MD5Final(out, &context);
    trial_erase(pad, sizeof(pad));
    trial_erase(short_key, sizeof(short_key));
    trial_erase(inner, sizeof(inner));
    trial_erase(&context, sizeof(context));
}

static const struct vim_ntlm_crypto trial_crypto = {
    trial_allocate, trial_release, trial_digest, trial_hmac
};

static int trial_token(struct smb_vc *vc, vfs_context_t context, uint32_t caps,
                       uint16_t *action, struct vim_ntlm_blob *blob)
{
    struct smb_gss *gss = &vc->vc_gss;
    trial_release(gss->gss_token, gss->gss_tokenlen);
    gss->gss_token = blob->data;
    gss->gss_tokenlen = (uint32_t)blob->size;
    blob->data = NULL;
    blob->size = 0;
    return smb_gss_ssandx(vc, context, caps, action);
}

static int trial_leopard_ntlmssp(struct smb_vc *vc, vfs_context_t context, uint32_t caps)
{
    struct smb_gss *gss = &vc->vc_gss;
    struct vim_ntlm_blob blob = {0}, auth = {0};
    struct vim_spnego_response reply;
    struct vim_ntlm_challenge challenge;
    uint8_t type1[32], nonce[8], session_random[16], key[16], session[16], mic[16];
    uint16_t user[SMB_MAXUSERNAMELEN + 1], domain[SMB_MAXNetBIOSNAMELEN + 1];
    uint16_t action = 0, previous_flags = vc->vc_hflags2;
    int error = EAUTH, stage = 1;
    const char *domain_name = vc->vc_domain ? vc->vc_domain : "";
    size_t username_size = smb_strtouni(user, vc->vc_username, strlen(vc->vc_username),
        UTF_PRECOMPOSED | UTF_NO_NULL_TERM);
    size_t domain_size = smb_strtouni(domain, domain_name, strlen(domain_name),
        UTF_PRECOMPOSED | UTF_NO_NULL_TERM);
    uint64_t stamp;
    struct timespec now;
    if (!username_size) return EAUTH;
    smb_reset_sig(vc);
    vc->vc_hflags2 |= SMB_FLAGS2_SECURITY_SIGNATURE;
    vim_ntlm_negotiate(type1);
    if (!vim_spnego_wrap(&trial_crypto, 1, (struct vim_ntlm_slice){type1, sizeof(type1)},
                        NULL, &blob)) goto done;
    if (trial_token(vc, context, caps, &action, &blob)) goto done;
    stage = 2;
    if (gss->gss_smb_error != EAGAIN ||
        !vim_spnego_response((struct vim_ntlm_slice){gss->gss_token, gss->gss_tokenlen}, &reply) ||
        reply.state == 0 || reply.mic.size || !vim_ntlm_challenge(reply.token, &challenge))
        goto done;
    uint32_t flags = challenge.flags & VIM_NTLM_FLAGS;
    smb_ntlmv2hash(key, domain_name, vc->vc_uppercase_username, smb_vc_getpass(vc));
    read_random(nonce, sizeof(nonce));
    read_random(session_random, sizeof(session_random));
    nanotime(&now);
    smb_time_local2NT(&now, &stamp, 0);
    stamp = htoleq(stamp);
    if (!vim_ntlm_authenticate(&trial_crypto, (struct vim_ntlm_slice){type1, sizeof(type1)},
            &challenge, key, (struct vim_ntlm_slice){(const uint8_t *)domain, domain_size},
            (struct vim_ntlm_slice){(const uint8_t *)user, username_size},
            (struct vim_ntlm_slice){NULL, 0}, nonce, (const uint8_t *)&stamp,
            session_random, &auth, session)) goto done;
    vim_ntlm_mech_mic(&trial_crypto, session, flags, 0, mic);
    if (!vim_spnego_wrap(&trial_crypto, 0, (struct vim_ntlm_slice){auth.data, auth.size},
                        mic, &blob)) goto done;
    stage = 3;
    vc->vc_mackey = trial_allocate(sizeof(session));
    if (!vc->vc_mackey) goto done;
    memcpy(vc->vc_mackey, session, sizeof(session));
    vc->vc_mackeylen = sizeof(session);
    vc->vc_seqno = 0;
    if (trial_token(vc, context, caps, &action, &blob) ||
        gss->gss_smb_error || (action & SMB_ACT_GUEST)) goto done;
    stage = 4;
    if (gss->gss_tokenlen) {
        if (!vim_spnego_response((struct vim_ntlm_slice){gss->gss_token, gss->gss_tokenlen},
                                 &reply) ||
            (reply.state != -1 && reply.state != 0) || reply.token.size) goto done;
        if (reply.mic.size) {
            vim_ntlm_mech_mic(&trial_crypto, session, flags, 1, mic);
            if (!vim_ntlm_equal(mic, reply.mic.data, sizeof(mic))) goto done;
        }
    }
    error = 0;
done:
    if (error) {
        SMBERROR("VIM NTLMSSP trial failed at stage %d\n", stage);
        if (vc->vc_mackey) trial_erase(vc->vc_mackey, vc->vc_mackeylen);
        smb_reset_sig(vc);
        vc->vc_hflags2 = previous_flags;
    }
    smb_gss_reset(gss);
    trial_release(blob.data, blob.size);
    trial_release(auth.data, auth.size);
    trial_erase(key, sizeof(key));
    trial_erase(session, sizeof(session));
    trial_erase(session_random, sizeof(session_random));
    trial_erase(mic, sizeof(mic));
    return error;
}
