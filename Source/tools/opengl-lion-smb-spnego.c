/* Lion smbd compatibility. Apple GSS/Heimdal172.29 disables NTLM signing-key
 * initialization (8577017). Keep native SPNEGO, password checks, name mapping
 * and SMB signing; perform the missing negotiation MIC at the smbd/GSS API
 * boundary, using public opaque-context queries. No global DYLD environment,
 * private context offsets or executable patches.
 */
#include <GSS/GSS.h>
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include "../vendor/source/smb-217.19/kernel/netsmb/vim_ntlmssp_wire.h"
struct slice {const unsigned char *p;size_t n;};
static int enabled;
__attribute__((constructor)) static void initialize(void){
    char release[80]={0};size_t n=sizeof(release);
    enabled=!sysctlbyname("kern.osrelease",release,&n,NULL,0)&&!strncmp(release,"11.",3)&&!strcmp(getprogname(),"smbd");
    /* The job sets only this scoped library. Never propagate it to children. */
    if(enabled){unsetenv("DYLD_INSERT_LIBRARIES");unsetenv("VIM_LION_SMB_POLICY");}
}
static int take(struct slice *in,unsigned tag,struct slice *v){
    size_t n,h=2,k,i;if(in->n<2||in->p[0]!=tag)return 0;n=in->p[1];
    if(n&128){k=n&127;if(!k||k>2||k>in->n-2||!in->p[2])return 0;n=0;
        for(i=0;i<k;i++)n=(n<<8)|in->p[2+i];h+=k;if(n<128||(k==2&&n<256))return 0;}
    if(h>in->n||n>in->n-h)return 0;v->p=in->p+h;v->n=n;in->p+=h+n;in->n-=h+n;return 1;
}
static struct slice mechanism_list(gss_buffer_t input){
    static const unsigned char spnego[]={0x2b,6,1,5,5,2};
    struct slice in={input->value,input->length},app,oid,neg,seq,list,values,obj;
    if(!take(&in,0x60,&app)||in.n||!take(&app,6,&oid)||oid.n!=sizeof(spnego)||memcmp(oid.p,spnego,sizeof(spnego))||
       !take(&app,0xa0,&neg)||app.n||!take(&neg,0x30,&seq)||neg.n||!take(&seq,0xa0,&list)||list.n>4096)return (struct slice){0};
    values=list;if(!take(&values,0x30,&seq)||values.n||!seq.n)return (struct slice){0};
    while(seq.n)if(!take(&seq,6,&obj)||!obj.n)return (struct slice){0};return list;
}
static size_t dersize(size_t n){return n+(n<128?2:n<256?3:4);}
static unsigned char *head(unsigned char *p,unsigned tag,size_t n){*p++=tag;if(n<128)*p++=n;else if(n<256){*p++=0x81;*p++=n;}else{*p++=0x82;*p++=n>>8;*p++=n;}return p;}
static int without_mic(struct vim_ntlm_slice token,gss_buffer_t out){
    size_t field=dersize(dersize(token.size)),seq=dersize(field),total=dersize(seq);
    if(token.size>60000)return 0;unsigned char *begin=malloc(total),*p=begin;if(!p)return 0;
    p=head(p,0xa1,seq);p=head(p,0x30,field);p=head(p,0xa2,dersize(token.size));p=head(p,4,token.size);
    memcpy(p,token.data,token.size);out->value=begin;out->length=total;return 1;
}
static void erase(void *p,size_t n){volatile unsigned char *b=p;while(n--)*b++=0;}
static void md5(const unsigned char key[16],const char *magic,unsigned char out[16]){
    CC_MD5_CTX c;CC_MD5_Init(&c);CC_MD5_Update(&c,key,16);CC_MD5_Update(&c,magic,(CC_LONG)strlen(magic)+1);CC_MD5_Final(out,&c);
}
static void rc4(const unsigned char key[16],const unsigned char in[8],unsigned char out[8]){
    unsigned char s[256],t;unsigned i,j=0,k;for(i=0;i<256;i++)s[i]=i;
    for(i=0;i<256;i++){j=(j+s[i]+key[i&15])&255;t=s[i];s[i]=s[j];s[j]=t;}
    i=j=0;for(k=0;k<8;k++){i=(i+1)&255;j=(j+s[i])&255;t=s[i];s[i]=s[j];s[j]=t;out[k]=in[k]^s[(s[i]+s[j])&255];}erase(s,sizeof(s));
}
static void signature(const unsigned char key[16],uint32_t flags,struct slice list,int send,unsigned char out[16]){
    unsigned char derived[16],digest[16],seq[4]={0};CCHmacContext c;
    md5(key,send?"session key to server-to-client signing key magic constant":"session key to client-to-server signing key magic constant",derived);
    CCHmacInit(&c,kCCHmacAlgMD5,derived,16);CCHmacUpdate(&c,seq,4);CCHmacUpdate(&c,list.p,list.n);CCHmacFinal(&c,digest);
    memset(out,0,16);out[0]=1;
    if(flags&0x40000000u){md5(key,send?"session key to server-to-client sealing key magic constant":"session key to client-to-server sealing key magic constant",derived);rc4(derived,digest,out+4);}else memcpy(out+4,digest,8);
    erase(derived,16);erase(digest,16);erase(&c,sizeof(c));
}
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
struct record {gss_ctx_id_t context;uint32_t flags;struct slice list;struct record *next;};
static struct record *records;
static struct record *find(gss_ctx_id_t ctx){for(struct record *r=records;r;r=r->next)if(r->context==ctx)return r;return NULL;}
static void discard(gss_ctx_id_t ctx){struct record **p=&records;while(*p){struct record *r=*p;if(r->context==ctx){*p=r->next;free((void*)r->list.p);free(r);return;}p=&r->next;}}
static OM_uint32 Delete(OM_uint32 *minor,gss_ctx_id_t *ctx,gss_buffer_t token){
    if(!enabled)return gss_delete_sec_context(minor,ctx,token);
    pthread_mutex_lock(&mutex);discard(*ctx);pthread_mutex_unlock(&mutex);return gss_delete_sec_context(minor,ctx,token);
}
static OM_uint32 Accept(OM_uint32 *minor,gss_ctx_id_t *ctx,gss_cred_id_t cred,gss_buffer_t input,
    gss_channel_bindings_t bindings,gss_name_t *name,gss_OID *mech,gss_buffer_t output,OM_uint32 *flags,OM_uint32 *time,gss_cred_id_t *delegated){
    if(!enabled)return gss_accept_sec_context(minor,ctx,cred,input,bindings,name,mech,output,flags,time,delegated);
    struct slice list=mechanism_list(input);struct record *r;struct vim_spnego_response parsed;
    gss_buffer_desc adapted=GSS_C_EMPTY_BUFFER;gss_ctx_id_t before=*ctx;int repair=0;
    pthread_mutex_lock(&mutex);r=find(before);
    if(r&&r->flags&&r->list.n&&vim_spnego_response((struct vim_ntlm_slice){input->value,input->length},&parsed)&&
       parsed.mic.size==16&&parsed.token.size>=64&&!memcmp(parsed.token.data,"NTLMSSP\0\3\0\0\0",12)){
        repair=without_mic(parsed.token,&adapted);if(!repair){pthread_mutex_unlock(&mutex);*minor=ENOMEM;return GSS_S_FAILURE;}}
    pthread_mutex_unlock(&mutex);
    OM_uint32 result=gss_accept_sec_context(minor,ctx,cred,repair?&adapted:input,bindings,name,mech,output,flags,time,delegated),junk;
    if(adapted.value){erase(adapted.value,adapted.length);free(adapted.value);}
    pthread_mutex_lock(&mutex);
    if(result==GSS_S_CONTINUE_NEEDED){
        if(!r){r=calloc(1,sizeof(*r));if(r){r->context=*ctx;r->next=records;records=r;}}
        if(r){
            if(!list.n&&!r->list.n)list=mechanism_list(output);
            if(list.n){unsigned char *copy=malloc(list.n);if(copy){memcpy(copy,list.p,list.n);free((void*)r->list.p);r->list=(struct slice){copy,list.n};}}
            struct vim_spnego_response reply;
            if(vim_spnego_response((struct vim_ntlm_slice){output->value,output->length},&reply)&&reply.token.size>=24&&!memcmp(reply.token.data,"NTLMSSP\0\2\0\0\0",12)){
                const unsigned char *p=reply.token.data;r->flags=p[20]|((uint32_t)p[21]<<8)|((uint32_t)p[22]<<16)|((uint32_t)p[23]<<24);}
        }
    }
    if(repair&&result==GSS_S_COMPLETE){
        gss_buffer_set_t keys=GSS_C_NO_BUFFER_SET;gss_OID actual=GSS_C_NO_OID;int open=0,initiator=0;
        static const unsigned char ntlm[]={0x2b,6,1,4,1,0x82,0x37,2,2,10};
        gss_OID_desc keyOID={11,"\x2a\x86\x48\x86\xf7\x12\x01\x02\x02\x05\x05"};
        unsigned char expected[16];
        if(!r||gss_inquire_context(&junk,*ctx,NULL,NULL,NULL,&actual,NULL,&initiator,&open)||
           !actual||actual->length!=sizeof(ntlm)||memcmp(actual->elements,ntlm,sizeof(ntlm))||initiator||!open||
           gss_inquire_sec_context_by_oid(&junk,*ctx,&keyOID,&keys)||!keys||!keys->count||keys->elements[0].length!=16){result=GSS_S_FAILURE;*minor=EINVAL;}
        else{
            const unsigned char *auth=parsed.token.data;
            uint32_t finalFlags=r->flags&(auth[60]|((uint32_t)auth[61]<<8)|((uint32_t)auth[62]<<16)|((uint32_t)auth[63]<<24));
            signature(keys->elements[0].value,finalFlags,r->list,0,expected);
            if(!vim_ntlm_equal(expected,parsed.mic.data,16)){result=GSS_S_BAD_MIC;*minor=0;}
            else{
                signature(keys->elements[0].value,finalFlags,r->list,1,expected);
                unsigned char *p=malloc(29);
                if(!p){result=GSS_S_FAILURE;*minor=ENOMEM;}
                else{static const unsigned char header[]={0xa1,27,0x30,25,0xa0,3,10,1,0,0xa3,18,4,16};
                    memcpy(p,header,sizeof(header));memcpy(p+sizeof(header),expected,16);
                    gss_release_buffer(&junk,output);output->value=p;output->length=29;}
            }
        }
        erase(expected,16);if(keys){if(keys->count&&keys->elements[0].value)erase(keys->elements[0].value,keys->elements[0].length);gss_release_buffer_set(&junk,&keys);}
    }
    if(repair&&result==GSS_S_CONTINUE_NEEDED){result=GSS_S_FAILURE;*minor=EINVAL;}
    if(result!=GSS_S_CONTINUE_NEEDED)discard(before);
    pthread_mutex_unlock(&mutex);
    if(result!=GSS_S_COMPLETE&&result!=GSS_S_CONTINUE_NEEDED){
        if(*ctx)gss_delete_sec_context(&junk,ctx,GSS_C_NO_BUFFER);
        if(name&&*name)gss_release_name(&junk,name);
        if(output->value)gss_release_buffer(&junk,output);
    }return result;
}
__attribute__((used)) static const struct {const void *replacement,*original;} interpose[]
 __attribute__((section("__DATA,__interpose")))={{Accept,gss_accept_sec_context},{Delete,gss_delete_sec_context}};
