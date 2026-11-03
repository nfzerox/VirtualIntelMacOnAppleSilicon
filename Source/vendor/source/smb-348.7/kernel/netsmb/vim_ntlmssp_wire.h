/* Virtual Intel Mac NTLMv2 wire codec. BSD-2-Clause.
 * Cold authentication only; no filesystem, socket, credential store or logging.
 * MS-NLMP2.2/3.3.2, RFC4178. The caller supplies native crypto and allocation.
 */
#ifndef VIM_NTLMSSP_WIRE_H
#define VIM_NTLMSSP_WIRE_H
#ifdef KERNEL
#include <sys/types.h>
#else
#include <stddef.h>
#endif
#include <stdint.h>
struct vim_ntlm_slice {const uint8_t *data;size_t size;};
struct vim_ntlm_blob {uint8_t *data;size_t size;};
struct vim_ntlm_crypto {
    void *(*allocate)(size_t);
    void (*release)(void *,size_t);
    void (*md5)(const struct vim_ntlm_slice *,size_t,uint8_t[16]);
    void (*hmac)(const uint8_t *,size_t,const struct vim_ntlm_slice *,size_t,uint8_t[16]);
};
struct vim_ntlm_challenge {
    struct vim_ntlm_slice token,target,info;
    uint32_t flags;
    uint8_t challenge[8],timestamp[8];
    int has_timestamp;
};
struct vim_spnego_response {
    struct vim_ntlm_slice token,mic;
    int state; /* -1 absent; RFC4178 accept-complete0/incomplete1/reject2/MIC3 */
};
/* Unicode, NTLMv2, ESS, signing and128-bit key exchange; no LM/NTLMv1. */
#define VIM_NTLM_FLAGS 0x60888215u
extern const uint8_t vim_ntlm_mechlist[14];
int vim_ntlm_challenge(struct vim_ntlm_slice,struct vim_ntlm_challenge *);
int vim_ntlm_authenticate(const struct vim_ntlm_crypto *,
    struct vim_ntlm_slice,const struct vim_ntlm_challenge *,
    const uint8_t[16],struct vim_ntlm_slice,struct vim_ntlm_slice,
    struct vim_ntlm_slice,const uint8_t[8],const uint8_t[8],const uint8_t[16],
    struct vim_ntlm_blob *,uint8_t[16]);
void vim_ntlm_negotiate(uint8_t[32]);
int vim_spnego_response(struct vim_ntlm_slice,struct vim_spnego_response *);
int vim_spnego_wrap(const struct vim_ntlm_crypto *,int,struct vim_ntlm_slice,
    const uint8_t *,struct vim_ntlm_blob *);
void vim_ntlm_mech_mic(const struct vim_ntlm_crypto *,const uint8_t[16],uint32_t,int,uint8_t[16]);
int vim_ntlm_equal(const uint8_t *,const uint8_t *,size_t);
#endif
