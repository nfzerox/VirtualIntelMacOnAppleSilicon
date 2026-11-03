/* Virtual Intel Mac NTLMv2/SPNEGO codec. BSD-2-Clause.
 * This is original protocol glue, not a copy of Samba's GPL implementation.
 * Original Apple crypto/UTF conversion stays in the native kernel adapter.
 * No casted wire structs, unaligned integer loads, passwords or output logs.
 */
#include "vim_ntlmssp_wire.h"
#ifdef KERNEL
#include <sys/systm.h>
#else
#include <string.h>
#endif
const uint8_t vim_ntlm_mechlist[14]={0x30,12,6,10,0x2b,6,1,4,1,0x82,0x37,2,2,10};
static uint16_t u16(const uint8_t *p){return p[0]|((uint16_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p){return u16(p)|((uint32_t)u16(p+2)<<16);}
static void p16(uint8_t *p,uint16_t x){p[0]=x;p[1]=x>>8;}
static void p32(uint8_t *p,uint32_t x){p16(p,x);p16(p+2,x>>16);}
static void erase(void *p,size_t n){volatile uint8_t *v=p;while(n--)*v++=0;}
/* NTLM's protocol-required128-bit RC4, only for the random exported key and
 * negotiation MIC. Never a file cipher or a new legacy authentication mode. */
static void rc4(const uint8_t key[16],const uint8_t *in,uint8_t *out,size_t n){
    uint8_t state[256],swap;unsigned i,j=0,k;
    for(i=0;i<256;i++)state[i]=i;
    for(i=0;i<256;i++){j=(j+state[i]+key[i&15])&255;swap=state[i];state[i]=state[j];state[j]=swap;}
    i=j=0;for(k=0;k<n;k++){i=(i+1)&255;j=(j+state[i])&255;swap=state[i];state[i]=state[j];state[j]=swap;
        out[k]=in[k]^state[(state[i]+state[j])&255];}erase(state,sizeof(state));
}
int vim_ntlm_equal(const uint8_t *a,const uint8_t *b,size_t n){
    uint8_t difference=0;size_t i;for(i=0;i<n;i++)difference|=a[i]^b[i];return !difference;
}
static int buffer(struct vim_ntlm_slice in,size_t at,struct vim_ntlm_slice *out){
    size_t n,offset;if(at>in.size||in.size-at<8)return 0;
    n=u16(in.data+at);offset=u32(in.data+at+4);
    /* The received MaxLen is explicitly ignored by MS-NLMP. */
    if(offset>in.size||n>in.size-offset||(n&&offset<48))return 0;
    out->data=in.data+offset;out->size=n;return 1;
}
void vim_ntlm_negotiate(uint8_t out[32]){
    memset(out,0,32);memcpy(out,"NTLMSSP",8);p32(out+8,1);p32(out+12,VIM_NTLM_FLAGS);
    p32(out+20,32);p32(out+28,32);
}
int vim_ntlm_challenge(struct vim_ntlm_slice in,struct vim_ntlm_challenge *out){
    struct vim_ntlm_slice target,info;size_t at=0;unsigned seen=0;
    if(!in.data||in.size<48||in.size>65535||memcmp(in.data,"NTLMSSP",8)||u32(in.data+8)!=2||
       !buffer(in,12,&target)||!buffer(in,40,&info)||target.size%2||info.size<4)return 0;
    memset(out,0,sizeof(*out));out->token=in;out->target=target;out->info=info;out->flags=u32(in.data+20);
    if((out->flags&0x20888211u)!=0x20888211u)return 0; /*128-bit Unicode/ESS/signing, no weak fallback */
    memcpy(out->challenge,in.data+24,8);
    while(at<info.size){
        uint16_t id,n;if(info.size-at<4)return 0;id=u16(info.data+at);n=u16(info.data+at+2);at+=4;
        if(n>info.size-at)return 0;
        if(!id)return n==0&&at==info.size&&(seen&6)==6;
        if(id<12){if(seen&(1u<<id))return 0;seen|=1u<<id;}
        if((id>=1&&id<=5)&&n%2)return 0;
        if(id==6&&n!=4)return 0;
        if(id==7){if(n!=8)return 0;memcpy(out->timestamp,info.data+at,8);out->has_timestamp=1;}
        if(id==10&&n!=16)return 0;
        at+=n;
    }return 0;
}
static void secbuf(uint8_t *field,size_t offset,size_t size){p16(field,size);p16(field+2,size);p32(field+4,offset);}
int vim_ntlm_authenticate(const struct vim_ntlm_crypto *c,
    struct vim_ntlm_slice negotiate,const struct vim_ntlm_challenge *challenge,
    const uint8_t response_key[16],struct vim_ntlm_slice domain,struct vim_ntlm_slice user,
    struct vim_ntlm_slice workstation,const uint8_t nonce[8],const uint8_t time[8],const uint8_t random_session[16],
    struct vim_ntlm_blob *out,uint8_t session[16]){
    size_t at=0,flags_at=0,bindings_at=0,extra=0,info_size,nt_size,total,offset;uint8_t *auth,*lm,*nt,*temp,*info;
    int has_flags=0,has_bindings=0,key_exchange=(challenge->flags&0x40000000u)!=0;struct vim_ntlm_slice parts[3];
    out->data=NULL;out->size=0;memset(session,0,16);
    if(!negotiate.data||negotiate.size!=32||memcmp(negotiate.data,"NTLMSSP",8)||u32(negotiate.data+8)!=1||
       u32(negotiate.data+12)!=VIM_NTLM_FLAGS||!user.size||domain.size%2||user.size%2||workstation.size%2||
       domain.size>1024||user.size>1024||workstation.size>1024||challenge->info.size>32768)return 0;
    /* Challenge parsing already validates every AV boundary and terminator. */
    while(at+4<challenge->info.size){uint16_t id=u16(challenge->info.data+at),n=u16(challenge->info.data+at+2);
        if(id==6){has_flags=1;flags_at=at+4;}if(id==10){has_bindings=1;bindings_at=at+4;}at+=4+n;}
    if(!has_flags)extra+=8;if(!has_bindings)extra+=20;
    info_size=challenge->info.size+extra;
    nt_size=16+28+info_size+4;total=88+domain.size+user.size+workstation.size+24+nt_size+(key_exchange?16:0);
    if(total>65535)return 0;
    auth=c->allocate(total);if(!auth)return 0;memset(auth,0,total);memcpy(auth,"NTLMSSP",8);p32(auth+8,3);
    /* MS-NLMP requires a zero Version field when its flag is absent. The MIC
     * remains at72, not64 (also used by Chromium's independently built client). */
    p32(auth+60,challenge->flags&VIM_NTLM_FLAGS);offset=88;
    secbuf(auth+28,offset,domain.size);if(domain.size)memcpy(auth+offset,domain.data,domain.size);offset+=domain.size;
    secbuf(auth+36,offset,user.size);memcpy(auth+offset,user.data,user.size);offset+=user.size;
    secbuf(auth+44,offset,workstation.size);if(workstation.size)memcpy(auth+offset,workstation.data,workstation.size);offset+=workstation.size;
    secbuf(auth+12,offset,24);lm=auth+offset;offset+=24;
    secbuf(auth+20,offset,nt_size);nt=auth+offset;offset+=nt_size;secbuf(auth+52,offset,key_exchange?16:0);
    temp=nt+16;temp[0]=temp[1]=1;memcpy(temp+8,challenge->has_timestamp?challenge->timestamp:time,8);
    memcpy(temp+16,nonce,8);info=temp+28;
    memcpy(info,challenge->info.data,challenge->info.size-4);at=challenge->info.size-4;
    if(has_flags)p32(info+flags_at,u32(info+flags_at)|2u);
    else{p16(info+at,6);p16(info+at+2,4);p32(info+at+4,2);at+=8;}
    if(has_bindings)memset(info+bindings_at,0,16); /* No TLS channel binding; never trust a peer-supplied hash. */
    else{p16(info+at,10);p16(info+at+2,16);at+=20;}
    /* EOL and final reserved word were zeroed with the allocation. */
    parts[0]=(struct vim_ntlm_slice){challenge->challenge,8};parts[1]=(struct vim_ntlm_slice){temp,nt_size-16};
    c->hmac(response_key,16,parts,2,nt);
    parts[0]=(struct vim_ntlm_slice){nt,16};c->hmac(response_key,16,parts,1,session);
    if(key_exchange){rc4(session,random_session,auth+offset,16);memcpy(session,random_session,16);}
    if(!challenge->has_timestamp){parts[0]=(struct vim_ntlm_slice){challenge->challenge,8};parts[1]=(struct vim_ntlm_slice){nonce,8};
        c->hmac(response_key,16,parts,2,lm);memcpy(lm+16,nonce,8);}
    parts[0]=negotiate;parts[1]=challenge->token;parts[2]=(struct vim_ntlm_slice){auth,total};
    c->hmac(session,16,parts,3,auth+72);out->data=auth;out->size=total;return 1;
}
/* Definite, minimally encoded DER lengths; no indefinite or recursive parser. */
struct der {const uint8_t *p;size_t n;};
static int take(struct der *in,unsigned tag,struct der *value){
    size_t length,head=2,i,count;if(in->n<2||in->p[0]!=tag)return 0;length=in->p[1];
    if(length&128){count=length&127;if(!count||count>2||count>in->n-2||!in->p[2])return 0;
        length=0;for(i=0;i<count;i++)length=(length<<8)|in->p[2+i];head+=count;
        if(length<128||(count==2&&length<256))return 0;}
    if(head>in->n||length>in->n-head)return 0;
    value->p=in->p+head;value->n=length;in->p+=head+length;in->n-=head+length;return 1;
}
int vim_spnego_response(struct vim_ntlm_slice blob,struct vim_spnego_response *out){
    struct der in={blob.data,blob.size},outer,sequence,field,value;int prior=-1;
    memset(out,0,sizeof(*out));out->state=-1;
    if(!blob.data||blob.size>65535||!take(&in,0xa1,&outer)||in.n||!take(&outer,0x30,&sequence)||outer.n)return 0;
    while(sequence.n){unsigned tag=sequence.p[0];int index=(int)tag-0xa0;
        if(index<0||index>3||index<=prior||!take(&sequence,tag,&field))return 0;prior=index;
        if(index==0){if(!take(&field,10,&value)||field.n||value.n!=1||value.p[0]>3)return 0;out->state=value.p[0];}
        if(index==1){if(!take(&field,6,&value)||field.n||value.n!=10||memcmp(value.p,vim_ntlm_mechlist+4,10))return 0;}
        if(index==2){if(!take(&field,4,&value)||field.n)return 0;out->token=(struct vim_ntlm_slice){value.p,value.n};}
        if(index==3){if(!take(&field,4,&value)||field.n||value.n!=16)return 0;out->mic=(struct vim_ntlm_slice){value.p,value.n};}
    }return out->state!=2;
}
static size_t der_size(size_t n){return n+(n<128?2:n<256?3:4);}
static uint8_t *der_head(uint8_t *p,unsigned tag,size_t n){*p++=tag;
    if(n<128)*p++=n;else if(n<256){*p++=0x81;*p++=n;}else{*p++=0x82;*p++=n>>8;*p++=n;}return p;}
int vim_spnego_wrap(const struct vim_ntlm_crypto *c,int initial,struct vim_ntlm_slice token,
    const uint8_t *mic,struct vim_ntlm_blob *out){
    static const uint8_t spnego_oid[]={6,6,0x2b,6,1,5,5,2};
    size_t fields,seq,inner,total;uint8_t *p,*begin;
    out->data=NULL;out->size=0;if(!token.data||token.size>60000||(initial&&mic))return 0;
    fields=der_size(der_size(token.size))+(initial?16:mic?20:0);
    seq=der_size(fields);inner=der_size(seq);total=initial?der_size(sizeof(spnego_oid)+inner):inner;
    begin=c->allocate(total);if(!begin)return 0;p=begin;
    if(initial){p=der_head(p,0x60,sizeof(spnego_oid)+inner);memcpy(p,spnego_oid,sizeof(spnego_oid));p+=sizeof(spnego_oid);}
    p=der_head(p,initial?0xa0:0xa1,seq);p=der_head(p,0x30,fields);
    if(initial){p=der_head(p,0xa0,sizeof(vim_ntlm_mechlist));memcpy(p,vim_ntlm_mechlist,sizeof(vim_ntlm_mechlist));p+=sizeof(vim_ntlm_mechlist);}
    p=der_head(p,0xa2,der_size(token.size));p=der_head(p,4,token.size);memcpy(p,token.data,token.size);p+=token.size;
    if(mic){p=der_head(p,0xa3,18);p=der_head(p,4,16);memcpy(p,mic,16);p+=16;}
    if((size_t)(p-begin)!=total){c->release(begin,total);return 0;}out->data=begin;out->size=total;return 1;
}
void vim_ntlm_mech_mic(const struct vim_ntlm_crypto *c,const uint8_t session[16],uint32_t flags,int server,uint8_t out[16]){
    static const char client_magic[]="session key to client-to-server signing key magic constant";
    static const char server_magic[]="session key to server-to-client signing key magic constant";
    static const char client_seal[]="session key to client-to-server sealing key magic constant";
    static const char server_seal[]="session key to server-to-client sealing key magic constant";
    uint8_t signing[16],checksum[16],zero[4]={0};struct vim_ntlm_slice parts[2];
    parts[0]=(struct vim_ntlm_slice){session,16};parts[1]=(struct vim_ntlm_slice){(const uint8_t *)(server?server_magic:client_magic),sizeof(client_magic)};
    c->md5(parts,2,signing);parts[0]=(struct vim_ntlm_slice){zero,4};parts[1]=(struct vim_ntlm_slice){vim_ntlm_mechlist,sizeof(vim_ntlm_mechlist)};
    c->hmac(signing,16,parts,2,checksum);memset(out,0,16);p32(out,1);
    if(flags&0x40000000u){
        parts[0]=(struct vim_ntlm_slice){session,16};parts[1]=(struct vim_ntlm_slice){(const uint8_t *)(server?server_seal:client_seal),sizeof(client_seal)};
        c->md5(parts,2,signing);rc4(signing,checksum,out+4,8);
    }else memcpy(out+4,checksum,8);
    erase(signing,sizeof(signing));erase(checksum,sizeof(checksum));
}
