// Public, synthetic vectors only. No service installation or credentials.
#include "../tools/opengl-lion-smb-spnego.c"
#include <assert.h>
static void *allocate(size_t n){return malloc(n);}
static void release(void *p,size_t n){erase(p,n);free(p);}
static void test_md5(const struct vim_ntlm_slice *p,size_t n,uint8_t out[16]){
    CC_MD5_CTX c;CC_MD5_Init(&c);for(size_t i=0;i<n;i++)CC_MD5_Update(&c,p[i].data,(CC_LONG)p[i].size);CC_MD5_Final(out,&c);
}
static void test_hmac(const uint8_t *key,size_t length,const struct vim_ntlm_slice *p,size_t n,uint8_t out[16]){
    CCHmacContext c;CCHmacInit(&c,kCCHmacAlgMD5,key,length);for(size_t i=0;i<n;i++)CCHmacUpdate(&c,p[i].data,p[i].size);CCHmacFinal(&c,out);
}
int main(void){
    const struct vim_ntlm_crypto crypto={allocate,release,test_md5,test_hmac};
    uint8_t key[16],type1[32],a[16],b[16];for(unsigned i=0;i<16;i++)key[i]=i;
    for(unsigned exchange=0;exchange<2;exchange++)for(unsigned server=0;server<2;server++){
        uint32_t flags=0x20888215u|(exchange?0x40000000u:0);
        signature(key,flags,(struct slice){vim_ntlm_mechlist,sizeof(vim_ntlm_mechlist)},server,a);
        vim_ntlm_mech_mic(&crypto,key,flags,server,b);assert(vim_ntlm_equal(a,b,16));
    }
    vim_ntlm_negotiate(type1);struct vim_ntlm_blob wrapped;
    assert(vim_spnego_wrap(&crypto,1,(struct vim_ntlm_slice){type1,sizeof(type1)},NULL,&wrapped));
    gss_buffer_desc initial={wrapped.size,wrapped.data};struct slice list=mechanism_list(&initial);
    assert(list.n==sizeof(vim_ntlm_mechlist)&&!memcmp(list.p,vim_ntlm_mechlist,list.n));
    for(size_t n=0;n<initial.length;n++){gss_buffer_desc short_input={n,initial.value};assert(!mechanism_list(&short_input).n);}
    release(wrapped.data,wrapped.size);
    memset(a,0x5a,sizeof(a));assert(vim_spnego_wrap(&crypto,0,(struct vim_ntlm_slice){type1,sizeof(type1)},a,&wrapped));
    struct vim_spnego_response parsed;assert(vim_spnego_response((struct vim_ntlm_slice){wrapped.data,wrapped.size},&parsed));
    assert(parsed.mic.size==16);gss_buffer_desc adapted=GSS_C_EMPTY_BUFFER;
    assert(without_mic(parsed.token,&adapted));struct vim_spnego_response native;
    assert(vim_spnego_response((struct vim_ntlm_slice){adapted.value,adapted.length},&native));
    assert(!native.mic.size&&native.token.size==sizeof(type1)&&!memcmp(native.token.data,type1,sizeof(type1)));
    release(adapted.value,adapted.length);release(wrapped.data,wrapped.size);
    puts("Lion SMB: signing/exchange directions, exact mechanism list, truncated DER and native token preservation passed");return 0;
}
