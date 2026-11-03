/* Virtual Intel Mac Darwin8 NTLMv2 adapter. BSD-2-Clause.
 * Uses Apple's original password ioctl, UTF conversion, MD4/MD5 and IOD owner.
 * No new credential database, userland ABI, crypto provider or network helper.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/md5.h>
#include <sys/utfconv.h>
#include <sys/random.h>
#include <sys/smb_apple.h>
#include <sys/smb_iconv.h>
#include <sys/mchain.h>
#include <netsmb/smb.h>
#include <netsmb/smb_conn.h>
#include <netsmb/smb_rq.h>
#include <netsmb/smb_subr.h>
#include <netsmb/md4.h>
#include "vim_ntlmssp_wire.h"
#include "vim_ntlmssp_native.h"
static void erase(void *p,size_t n){volatile uint8_t *v=p;while(n--)*v++=0;}
static void *allocate(size_t n){return malloc(n,M_SMBTEMP,M_WAITOK);}
static void release(void *p,size_t n){if(p){erase(p,n);free(p,M_SMBTEMP);}}
static void digest(const struct vim_ntlm_slice *v,size_t count,uint8_t out[16]){
    MD5_CTX ctx;size_t i;MD5Init(&ctx);for(i=0;i<count;i++)MD5Update(&ctx,v[i].data,v[i].size);MD5Final(out,&ctx);erase(&ctx,sizeof(ctx));
}
static void hmac(const uint8_t *key,size_t n,const struct vim_ntlm_slice *v,size_t count,uint8_t out[16]){
    uint8_t pad[64],shortkey[16],inner[16];MD5_CTX ctx;size_t i;
    if(n>64){struct vim_ntlm_slice k={key,n};digest(&k,1,shortkey);key=shortkey;n=16;}
    memset(pad,0,64);memcpy(pad,key,n);for(i=0;i<64;i++)pad[i]^=0x36;
    MD5Init(&ctx);MD5Update(&ctx,pad,64);for(i=0;i<count;i++)MD5Update(&ctx,v[i].data,v[i].size);MD5Final(inner,&ctx);
    for(i=0;i<64;i++)pad[i]^=0x36^0x5c;
    MD5Init(&ctx);MD5Update(&ctx,pad,64);MD5Update(&ctx,inner,16);MD5Final(out,&ctx);
    erase(pad,sizeof(pad));erase(shortkey,sizeof(shortkey));erase(inner,sizeof(inner));erase(&ctx,sizeof(ctx));
}
static const struct vim_ntlm_crypto crypto={allocate,release,digest,hmac};
static int unicode(const char *text,struct vim_ntlm_blob *out){
    size_t n=strlen(text);if(n>1024)return EINVAL;
    out->data=allocate(n*2+2);if(!out->data)return ENOMEM;
    out->size=smb_strtouni((uint16_t *)out->data,text,n,UTF_PRECOMPOSED|UTF_NO_NULL_TERM);
    if(n&&!out->size){release(out->data,n*2+2);out->data=NULL;return EINVAL;}return 0;
}
void vim_smb_clear_key(struct smb_vc *vc){erase(vc->vc_vim_key,16);vc->vc_vim_key_set=0;vc->vc_vim_seqno=0;}
static void freeblob(struct vim_ntlm_blob *b){release(b->data,b->size);b->data=NULL;b->size=0;}
static struct vim_ntlm_slice slice(struct vim_ntlm_blob b){struct vim_ntlm_slice s={b.data,b.size};return s;}
static void drop_token(struct smb_vc *vc){release(vc->vc_outtok,vc->vc_outtoklen);vc->vc_outtok=NULL;vc->vc_outtoklen=0;}
int vim_smb_ntlm_auth(struct smb_vc *vc,struct smb_cred *cred){
    uint8_t type1[32],nonce[8],time[8],random_session[16],nthash[21],key[16],session[16],mic[16],expected[16];
    struct vim_ntlm_blob user={0},upper={0},domain={0},password={0},negotiate={0},auth={0},wrapped={0};
    struct vim_ntlm_blob challenge_token={0};struct vim_spnego_response response;struct vim_ntlm_challenge challenge;
    struct vim_ntlm_slice parts[2];struct timespec now;uint64_t stamp;MD4_CTX md4;
    char *uppercase=NULL;size_t length;int error=EAUTH,stage=0;
    if(!vc->vc_username[0]||(vc->vc_flags&SMBV_MINAUTH)>=SMBV_MINAUTH_KERBEROS)return EAUTH;
    vim_smb_clear_key(vc);drop_token(vc);length=strlen(vc->vc_username);uppercase=allocate(length+1);
    if(!uppercase)return ENOMEM;iconv_convstr(vc->vc_toupper,uppercase,vc->vc_username,length);uppercase[length]=0;
    /* NODOMAIN is the old kernel's internal placeholder, not a domain the
     * user supplied for NTLMv2 hashing or transmission. Preserve that intent. */
    if(unicode(vc->vc_username,&user)||unicode(uppercase,&upper)||unicode(vc->vc_vim_domain_empty?"":vc->vc_domain,&domain)||unicode(smb_vc_getpass(vc),&password))goto done;
    MD4Init(&md4);MD4Update(&md4,password.data,password.size);MD4Final(nthash,&md4);
    parts[0]=slice(upper);parts[1]=slice(domain);hmac(nthash,16,parts,2,key);
    read_random(nonce,sizeof(nonce));read_random(random_session,sizeof(random_session));nanotime(&now);smb_time_local2NT(&now,0,&stamp);stamp=htoleq(stamp);memcpy(time,&stamp,8);
    vim_ntlm_negotiate(type1);
    if(!vim_spnego_wrap(&crypto,1,(struct vim_ntlm_slice){type1,32},NULL,&negotiate))goto done;
    stage=1;vc->vc_vim_round=1;vc->vc_intok=(caddr_t)negotiate.data;vc->vc_intoklen=negotiate.size;
    error=vim_smb_native_session_token(vc,cred);vc->vc_intok=NULL;vc->vc_intoklen=0;
    if(error)goto done;error=EAUTH;stage=11;
    if(!vc->vc_vim_more)goto done;stage=12;
    if(!vim_spnego_response((struct vim_ntlm_slice){(uint8_t *)vc->vc_outtok,vc->vc_outtoklen},&response))goto done;stage=13;
    if(response.state==0||response.mic.size)goto done;stage=14;
    if(!vim_ntlm_challenge(response.token,&challenge))goto done;stage=15;
    challenge_token.data=(uint8_t *)vc->vc_outtok;challenge_token.size=vc->vc_outtoklen;vc->vc_outtok=NULL;vc->vc_outtoklen=0;
    if(!vim_ntlm_authenticate(&crypto,(struct vim_ntlm_slice){type1,32},&challenge,key,slice(domain),slice(user),
       (struct vim_ntlm_slice){NULL,0},nonce,time,random_session,&auth,session))goto done;
    vim_ntlm_mech_mic(&crypto,session,challenge.flags&VIM_NTLM_FLAGS,0,mic);
    if(!vim_spnego_wrap(&crypto,0,slice(auth),mic,&wrapped))goto done;
    /* Extended NTLMSSP SMB signing uses the exported session key alone,
     * not the old non-extended session-key || NT-response construction. */
    memcpy(vc->vc_vim_key,session,16);vc->vc_vim_key_set=1;vc->vc_vim_seqno=0;
    stage=2;vc->vc_vim_round=2;vc->vc_intok=(caddr_t)wrapped.data;vc->vc_intoklen=wrapped.size;
    error=vim_smb_native_session_token(vc,cred);vc->vc_intok=NULL;vc->vc_intoklen=0;
    if(error)goto done;error=EAUTH;stage=21;
    if(vc->vc_vim_more)goto done;
    if(vc->vc_outtoklen){
        stage=22;if(!vim_spnego_response((struct vim_ntlm_slice){(uint8_t *)vc->vc_outtok,vc->vc_outtoklen},&response))goto done;
        stage=23;if((response.state!=-1&&response.state!=0)||response.token.size)goto done;
        if(response.mic.size){stage=24;vim_ntlm_mech_mic(&crypto,session,challenge.flags&VIM_NTLM_FLAGS,1,expected);if(!vim_ntlm_equal(expected,response.mic.data,16))goto done;}
    }
    error=0;
done:
    vc->vc_vim_round=0;vc->vc_vim_more=0;vc->vc_intok=NULL;vc->vc_intoklen=0;drop_token(vc);
    if(error){vim_smb_clear_key(vc);SMBERROR("NTLMSSP stage%d failed (%d)\n",stage,error);}
    release(uppercase,length+1);freeblob(&user);freeblob(&upper);freeblob(&domain);freeblob(&password);
    freeblob(&negotiate);freeblob(&auth);freeblob(&wrapped);freeblob(&challenge_token);
    erase(nthash,sizeof(nthash));erase(key,sizeof(key));erase(session,sizeof(session));erase(random_session,sizeof(random_session));erase(mic,sizeof(mic));erase(expected,sizeof(expected));erase(&md4,sizeof(md4));
    return error;
}
/* Darwin9's SMB1 signing contract, expressed over Darwin8 mbuf chains. It is
 * keyed MD5 (not HMAC), including a substituted sequence/zero signature slot.
 * No assumption that the received SMB header fits in a single mbuf. */
static int secondary(unsigned cmd){return cmd==SMB_COM_TRANSACTION_SECONDARY||cmd==SMB_COM_TRANSACTION2_SECONDARY||cmd==SMB_COM_NT_TRANSACT_SECONDARY;}
int vim_smb_sign(struct smb_rq *rq){
    struct smb_vc *vc=rq->sr_vc;struct mbuf *m;MD5_CTX md;uint8_t result[16];
    /* Native Darwin10's NTLMSSP contract: authentication uses the protocol
     * placeholder, even after deriving the key. Type3 consumes sequence0/1;
     * the first ordinary signed request/reply is2/3. */
    if(rq->sr_cmd==SMB_COM_SESSION_SETUP_ANDX && (vc->vc_vim_round==1||vc->vc_vim_round==2)){
        rq->sr_vim_seqno=vc->vc_vim_seqno++;rq->sr_vim_rseqno=vc->vc_vim_seqno++;
        memcpy(rq->sr_vim_signature,"BSRSPLY ",8);return 0;
    }
    if(!vc->vc_vim_key_set)return EAUTH;
    if(!secondary(rq->sr_cmd)){rq->sr_vim_seqno=vc->vc_vim_seqno++;rq->sr_vim_rseqno=vc->vc_vim_seqno++;}
#ifdef VIM_SMB_RESEARCH
    if(rq->sr_vim_seqno<256 || rq->sr_sendcnt>1)
        SMBERROR("sign cmd%x mid%u seq%u send%d\n",rq->sr_cmd,rq->sr_mid,rq->sr_vim_seqno,rq->sr_sendcnt);
#endif
    {uint32_t seq=htolel(rq->sr_vim_seqno);memcpy(rq->sr_vim_signature,&seq,4);memset(rq->sr_vim_signature+4,0,4);}
    MD5Init(&md);MD5Update(&md,vc->vc_vim_key,16);
    for(m=rq->sr_rq.mb_top;m;m=m->m_next)MD5Update(&md,mtod(m,uint8_t *),m->m_len);
    MD5Final(result,&md);memcpy(rq->sr_vim_signature,result,8);erase(result,sizeof(result));erase(&md,sizeof(md));return 0;
}
int vim_smb_verify(struct smb_rq *rq){
    struct smb_vc *vc=rq->sr_vc;struct mbuf *m;MD5_CTX md;uint8_t result[16],actual[8],substitute[8]={0};size_t offset=0;uint32_t seq;
    /* Session-setup success is decided by GSS, never by a dummy SMB MAC.
     * Native Apple servers may leave that final reply unsigned. The very next
     * ordinary reply (including tree connect) must carry a verified real MAC. */
    if(rq->sr_cmd==SMB_COM_SESSION_SETUP_ANDX && (vc->vc_vim_round==1||vc->vc_vim_round==2))return 0;
    if(!vc->vc_vim_key_set)return EAUTH;
    if(!(rq->sr_rpflags2&SMB_FLAGS2_SECURITY_SIGNATURE))return EAUTH;
    seq=htolel(rq->sr_vim_rseqno);memcpy(substitute,&seq,4);MD5Init(&md);MD5Update(&md,vc->vc_vim_key,16);
    for(m=rq->sr_rp.md_top;m;m=m->m_next){
        const uint8_t *p=mtod(m,uint8_t *);size_t size=m->m_len,used=0;
        while(used<size){size_t absolute=offset+used,n=size-used;
            if(absolute<14){if(n>14-absolute)n=14-absolute;MD5Update(&md,p+used,n);}
            else if(absolute<22){if(n>22-absolute)n=22-absolute;memcpy(actual+absolute-14,p+used,n);MD5Update(&md,substitute+absolute-14,n);}
            else MD5Update(&md,p+used,n);
            used+=n;
        }offset+=size;
    }
    MD5Final(result,&md);{int good=offset>=32&&vim_ntlm_equal(result,actual,8);
#ifdef VIM_SMB_RESEARCH
        if(!good)SMBERROR("verify cmd%x mid%u seq%u rejected\n",rq->sr_cmd,rq->sr_mid,rq->sr_vim_rseqno);
#endif
        erase(result,sizeof(result));erase(&md,sizeof(md));return good?0:EAUTH;}
}
