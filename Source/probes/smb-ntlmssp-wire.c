/* Synthetic public MS-NLMP vectors and bounds checks. No real credentials,
 * network service, guest mutation, or mounted filesystem. */
#include "../vendor/source/smb-217.19/kernel/netsmb/vim_ntlmssp_wire.h"
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>
#include <CommonCrypto/CommonCryptor.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void hash(const struct vim_ntlm_slice *v,size_t count,uint8_t out[16]){
    CC_MD5_CTX md;CC_MD5_Init(&md);for(size_t i=0;i<count;i++)CC_MD5_Update(&md,v[i].data,(CC_LONG)v[i].size);CC_MD5_Final(out,&md);
}
static void hmac(const uint8_t *key,size_t n,const struct vim_ntlm_slice *v,size_t count,uint8_t out[16]){
    CCHmacContext ctx;CCHmacInit(&ctx,kCCHmacAlgMD5,key,n);for(size_t i=0;i<count;i++)CCHmacUpdate(&ctx,v[i].data,v[i].size);CCHmacFinal(&ctx,out);
}
static void release(void *p,size_t n){memset(p,0,n);free(p);}
static const struct vim_ntlm_crypto crypto={malloc,release,hash,hmac};
static void put16(uint8_t *p,unsigned x){p[0]=x;p[1]=x>>8;}
static void put32(uint8_t *p,uint32_t x){put16(p,x);put16(p+2,x>>16);}
static unsigned get16(const uint8_t *p){return p[0]|((unsigned)p[1]<<8);}
static uint32_t get32(const uint8_t *p){return get16(p)|((uint32_t)get16(p+2)<<16);}
static size_t unicode(uint8_t *p,const char *s){size_t n=strlen(s);for(size_t i=0;i<n;i++){p[i*2]=s[i];p[i*2+1]=0;}return n*2;}
static size_t av(uint8_t *p,unsigned id,const char *s){size_t n=unicode(p+4,s);put16(p,id);put16(p+2,n);return n+4;}
static void unhex(const char *text,uint8_t *p){for(size_t i=0;text[i];i+=2){unsigned n;assert(sscanf(text+i,"%2x",&n)==1);p[i/2]=n;}}
static void exercise(int timestamp,int key_exchange){
    uint8_t type1[32],type2[256]={0},domain[32],user[16],nonce[8],time[8]={0},key[16],session[16],expected[16];
    uint8_t mic[16],server_mic[16],random_session[16];size_t size=48,domain_size=unicode(domain,"Domain"),user_size=unicode(user,"User");
    struct vim_ntlm_challenge challenge;struct vim_spnego_response response;struct vim_ntlm_blob auth,wrap;
    vim_ntlm_negotiate(type1);assert(get32(type1+12)==VIM_NTLM_FLAGS);
    uint32_t flags=VIM_NTLM_FLAGS&~(key_exchange?0:0x40000000u);memset(random_session,0x55,16);
    memcpy(type2,"NTLMSSP",8);put32(type2+8,2);put32(type2+20,flags);unhex("0123456789abcdef",type2+24);
    put32(type2+16,48);put32(type2+44,48);
    size+=av(type2+size,2,"Domain");size+=av(type2+size,1,"Server");
    if(timestamp){put16(type2+size,7);put16(type2+size+2,8);memset(type2+size+4,0x11,8);size+=12;}
    size+=4;put16(type2+40,size-48);put16(type2+42,size-48);
    assert(vim_ntlm_challenge((struct vim_ntlm_slice){type2,size},&challenge));
    for(size_t n=0;n<size;n++){struct vim_ntlm_challenge bad;assert(!vim_ntlm_challenge((struct vim_ntlm_slice){type2,n},&bad));}
    uint8_t corrupt[256];memcpy(corrupt,type2,size);put32(corrupt+44,0xfffffff0u);
    struct vim_ntlm_challenge bad;assert(!vim_ntlm_challenge((struct vim_ntlm_slice){corrupt,size},&bad));
    memcpy(corrupt,type2,size);put32(corrupt+20,VIM_NTLM_FLAGS&~0x80000u);
    assert(!vim_ntlm_challenge((struct vim_ntlm_slice){corrupt,size},&bad));
    memcpy(corrupt,type2,size);put32(corrupt+20,flags&~0x20000000u);
    assert(!vim_ntlm_challenge((struct vim_ntlm_slice){corrupt,size},&bad));
    /* MS-NLMP4.2.4 NTOWFv2("Password","User","Domain"). */
    unhex("0c868a403bfd7a93a3001ef22ef02e3f",key);memset(nonce,0xaa,8);
    assert(vim_ntlm_authenticate(&crypto,(struct vim_ntlm_slice){type1,32},&challenge,key,
        (struct vim_ntlm_slice){domain,domain_size},(struct vim_ntlm_slice){user,user_size},
        (struct vim_ntlm_slice){NULL,0},nonce,time,random_session,&auth,session));
    assert(get32(auth.data+8)==3&&get32(auth.data+60)==flags);
    const uint8_t *lm=auth.data+get32(auth.data+16),*nt=auth.data+get32(auth.data+24);
    assert(get16(auth.data+12)==24);size_t nt_length=get16(auth.data+20);
    if(timestamp){uint8_t zero[24]={0};assert(!memcmp(lm,zero,24));assert(!memcmp(nt+24,type2+size-12-4+4,8));}
    else {uint8_t lm_expected[24];unhex("86c35097ac9cec102554764a57cccc19aaaaaaaaaaaaaaaa",lm_expected);assert(!memcmp(lm,lm_expected,24));}
    /* Independently hash the parsed output transcript and proof; do not use
     * the codec's own offsets or returned MIC to derive the expectation. */
    struct vim_ntlm_slice proof[]={{type2+24,8},{nt+16,nt_length-16}};
    hmac(key,16,proof,2,expected);assert(!memcmp(nt,expected,16));
    struct vim_ntlm_slice session_input={nt,16};hmac(key,16,&session_input,1,expected);
    if(key_exchange){uint8_t cipher[16];size_t written=0;
        assert(CCCrypt(kCCEncrypt,kCCAlgorithmRC4,0,expected,16,NULL,random_session,16,cipher,16,&written)==kCCSuccess&&written==16);
        assert(get16(auth.data+52)==16&&!memcmp(cipher,auth.data+get32(auth.data+56),16));
        assert(!memcmp(session,random_session,16));
    }else assert(!memcmp(session,expected,16)&&!get16(auth.data+52));
    uint8_t saved[16];memcpy(saved,auth.data+72,16);memset(auth.data+72,0,16);
    struct vim_ntlm_slice transcript[]={{type1,32},{type2,size},{auth.data,auth.size}};
    hmac(session,16,transcript,3,expected);assert(!memcmp(saved,expected,16));memcpy(auth.data+72,saved,16);
    vim_ntlm_mech_mic(&crypto,session,flags,0,mic);vim_ntlm_mech_mic(&crypto,session,flags,1,server_mic);
    /* Independent CommonCrypto implementation of the negotiated mech MIC. */
    static const char signmagic[]="session key to client-to-server signing key magic constant";
    struct vim_ntlm_slice sign[]={{session,16},{(const uint8_t *)signmagic,sizeof(signmagic)}};
    hash(sign,2,expected);uint8_t zero[4]={0},checksum[16];
    struct vim_ntlm_slice mech[]={{zero,4},{vim_ntlm_mechlist,sizeof(vim_ntlm_mechlist)}};
    hmac(expected,16,mech,2,checksum);
    if(key_exchange){static const char sealmagic[]="session key to client-to-server sealing key magic constant";
        sign[1]=(struct vim_ntlm_slice){(const uint8_t *)sealmagic,sizeof(sealmagic)};hash(sign,2,expected);size_t written=0;
        uint8_t cipher[8];assert(CCCrypt(kCCEncrypt,kCCAlgorithmRC4,0,expected,16,NULL,checksum,8,cipher,8,&written)==kCCSuccess&&written==8);
        assert(!memcmp(mic+4,cipher,8));
    }else assert(!memcmp(mic+4,checksum,8));
    assert(!vim_ntlm_equal(mic,server_mic,16));assert(get32(mic)==1&&get32(mic+12)==0);
    assert(vim_spnego_wrap(&crypto,0,(struct vim_ntlm_slice){auth.data,auth.size},mic,&wrap));
    assert(vim_spnego_response((struct vim_ntlm_slice){wrap.data,wrap.size},&response));
    assert(response.token.size==auth.size&&!memcmp(response.token.data,auth.data,auth.size));
    assert(response.mic.size==16&&vim_ntlm_equal(response.mic.data,mic,16));
    for(size_t n=0;n<wrap.size;n++)assert(!vim_spnego_response((struct vim_ntlm_slice){wrap.data,n},&response));
    wrap.data[1]=0x80;assert(!vim_spnego_response((struct vim_ntlm_slice){wrap.data,wrap.size},&response));
    crypto.release(wrap.data,wrap.size);crypto.release(auth.data,auth.size);
}
int main(void){exercise(0,0);exercise(1,0);exercise(0,1);exercise(1,1);puts("NTLMv2 LM vector, timestamps, key exchange, NT proof/session/MIC, SPNEGO directions and truncated/invalid wire bounds passed");return 0;}
