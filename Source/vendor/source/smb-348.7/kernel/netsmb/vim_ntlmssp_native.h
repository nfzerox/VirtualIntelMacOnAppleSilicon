/* Native Darwin9 adapter for the original BSD-2-Clause wire codec. */
#ifndef VIM_NTLMSSP_NATIVE_H
#define VIM_NTLMSSP_NATIVE_H
struct smb_vc;struct smb_cred;struct smb_rq;
int vim_smb_ntlm_auth(struct smb_vc *,struct smb_cred *);
int vim_smb_native_session_token(struct smb_vc *,struct smb_cred *);
int vim_smb_sign(struct smb_rq *);
int vim_smb_verify(struct smb_rq *);
void vim_smb_clear_key(struct smb_vc *);
#endif
