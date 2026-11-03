/* Finite SMB1 authentication diagnostic for the native Tiger wire codec.
 * Credentials are read from the terminal, never argv, files, or output.
 * No mount, file operations, service, authentication downgrade, or saved key.
 */
#define main synthetic_fixture_main
#include "smb-ntlmssp-wire.c"
#undef main
#include <arpa/inet.h>
#include <CoreFoundation/CoreFoundation.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

static void wipe(void *p,size_t n){volatile uint8_t *v=p;while(n--)*v++=0;}
static int exact(int fd,void *buffer,size_t count,int sending){
    uint8_t *p=buffer;while(count){ssize_t n=sending?send(fd,p,count,0):recv(fd,p,count,0);
        if(n<=0)return 0;p+=n;count-=(size_t)n;}return 1;
}
static size_t exchange(int fd,uint8_t *packet,size_t size,uint8_t *reply,size_t capacity){
    uint8_t header[4]={0,(uint8_t)(size>>16),(uint8_t)(size>>8),(uint8_t)size};
    if(!exact(fd,header,4,1)||!exact(fd,packet,size,1)||!exact(fd,header,4,0))return 0;
    size=((size_t)header[1]<<16)|((size_t)header[2]<<8)|header[3];
    if(header[0]||size>capacity||size<35||!exact(fd,reply,size,0)||memcmp(reply,"\xffSMB",4))return 0;
    printf("SMB reply command=%02x status=%08x flags=%04x words=%u bytes=%zu\n",
        reply[4],get32(reply+5),get16(reply+10),reply[32],size);return size;
}
static void header(uint8_t *p,unsigned command,unsigned uid,unsigned mid,unsigned flags){
    memset(p,0,32);memcpy(p,"\xffSMB",4);p[4]=command;p[9]=0x18;put16(p+10,flags);
    put16(p+24,0xffff);put16(p+26,getpid());put16(p+28,uid);put16(p+30,mid);
}
static size_t utf16(uint8_t *out,size_t capacity,const char *text,int upper){
    CFStringRef s=CFStringCreateWithCString(NULL,text,kCFStringEncodingUTF8);if(!s)return SIZE_MAX;
    CFMutableStringRef m=CFStringCreateMutableCopy(NULL,0,s);CFRelease(s);if(!m)return SIZE_MAX;
    if(upper)CFStringUppercase(m,NULL);CFIndex used=0,length=CFStringGetLength(m);
    CFIndex converted=CFStringGetBytes(m,CFRangeMake(0,length),kCFStringEncodingUTF16LE,0,false,out,capacity,&used);
    CFRelease(m);return converted==length?(size_t)used:SIZE_MAX;
}
static size_t setup(uint8_t *p,struct vim_ntlm_blob token,unsigned uid,unsigned mid,uint32_t caps){
    header(p,0x73,uid,mid,0xc815);p[32]=12;memset(p+33,0,26);p[33]=0xff;
    put16(p+37,61440);put16(p+39,16);put16(p+41,1);put16(p+47,token.size);
    put32(p+53,caps|0x80000054u);size_t n=59;
    memcpy(p+n,token.data,token.size);n+=token.size;if(n&1)p[n++]=0;
    n+=utf16(p+n,128,"Mac OS X",0);p[n++]=0;p[n++]=0;
    n+=utf16(p+n,128,"Virtual Intel Mac diagnostic",0);p[n++]=0;p[n++]=0;
    put16(p+57,n-59);return n;
}
static int run(int fd,const char *user_text,const char *domain_text,int corrupt){
    uint8_t packet[65536],reply[65536],type1[32],nonce[8],time[8]={0},random_session[16],key[16],session[16],mic[16],hash[16];
    uint8_t user[2048],upper[2048],domain[2048],password[2048];
    struct vim_ntlm_blob wrap={0},auth={0};struct vim_spnego_response response;struct vim_ntlm_challenge challenge;
    size_t usern=utf16(user,sizeof(user),user_text,0),uppern=utf16(upper,sizeof(upper),user_text,1);
    size_t domainn=utf16(domain,sizeof(domain),domain_text,0);int result=1;
    if(usern==SIZE_MAX||uppern==SIZE_MAX||domainn==SIZE_MAX)return 1;
    header(packet,0x72,0,1,0xc801);packet[32]=0;put16(packet+33,12);memcpy(packet+35,"\2NT LM 0.12",12);
    size_t size=exchange(fd,packet,47,reply,sizeof(reply));
    if(!size||get32(reply+5)||reply[32]!=17||size<73)goto done;
    uint32_t caps=get32(reply+52);if(!(caps&0x80000000u))goto done;
    printf("SMB negotiate security=%02x capabilities=%08x\n",reply[35],caps);
    vim_ntlm_negotiate(type1);if(!vim_spnego_wrap(&crypto,1,(struct vim_ntlm_slice){type1,32},NULL,&wrap))goto done;
    size=setup(packet,wrap,0,2,caps);release(wrap.data,wrap.size);wrap.data=NULL;
    size=exchange(fd,packet,size,reply,sizeof(reply));
    if(!size||get32(reply+5)!=0xc0000016u||reply[32]!=4||size<43)goto done;
    size_t tokenn=get16(reply+39);if(tokenn>size-43)goto done;
    if(!vim_spnego_response((struct vim_ntlm_slice){reply+43,tokenn},&response)||!vim_ntlm_challenge(response.token,&challenge))goto done;
    printf("NTLM challenge flags=%08x timestamp=%d SPNEGO-state=%d\n",challenge.flags,challenge.has_timestamp,response.state);
    char *secret=getpass("Host account password: ");if(!secret)goto done;
    size_t passwordn=utf16(password,sizeof(password),secret,0);wipe(secret,strlen(secret));if(passwordn==SIZE_MAX)goto done;
    CC_MD4(password,(CC_LONG)passwordn,hash);wipe(password,sizeof(password));
    struct vim_ntlm_slice parts[]={{upper,uppern},{domain,domainn}};hmac(hash,16,parts,2,key);wipe(hash,sizeof(hash));
    arc4random_buf(nonce,8);arc4random_buf(random_session,16);
    uint64_t timestamp=((uint64_t)CFAbsoluteTimeGetCurrent()+978307200u+11644473600ull)*10000000ull;
    for(unsigned i=0;i<8;i++)time[i]=(uint8_t)(timestamp>>(8*i));
    if(!vim_ntlm_authenticate(&crypto,(struct vim_ntlm_slice){type1,32},&challenge,key,
        (struct vim_ntlm_slice){domain,domainn},(struct vim_ntlm_slice){user,usern},
        (struct vim_ntlm_slice){NULL,0},nonce,time,random_session,&auth,session))goto done;
    vim_ntlm_mech_mic(&crypto,session,challenge.flags&VIM_NTLM_FLAGS,0,mic);
    if(corrupt)mic[4]^=1; /* finite negative integrity control, not an auth fallback */
    if(!vim_spnego_wrap(&crypto,0,(struct vim_ntlm_slice){auth.data,auth.size},mic,&wrap))goto done;
    unsigned uid=get16(reply+28);size=setup(packet,wrap,uid,3,caps);
    /* Apple's native Snow client uses the session-setup placeholder until
     * GSS completes, then starts ordinary signed requests at sequence2. */
    memcpy(packet+14,"BSRSPLY ",8);
    size=exchange(fd,packet,size,reply,sizeof(reply));
    if(corrupt){
        if(size&&get32(reply+5)==0xc000006du){puts("Host rejected the tampered negotiation MIC as required");result=0;}
        else puts("Negative integrity control failed");
        goto done;
    }
    if(!size||get32(reply+5))goto done;
    printf("Authenticated action=%04x signature-flag=%d\n",get16(reply+37),(get16(reply+10)&4)!=0);
    if(reply[32]!=4||size<43||(get16(reply+37)&1))goto done;
    tokenn=get16(reply+39);printf("Final security-token length=%zu\n",tokenn);if(tokenn>size-43)goto done;
    if(tokenn){if(!vim_spnego_response((struct vim_ntlm_slice){reply+43,tokenn},&response)||response.token.size||
        (response.state!=-1&&response.state!=0))goto done;
        printf("Final SPNEGO state=%d mech-MIC-length=%zu\n",response.state,response.mic.size);
        if(response.mic.size){vim_ntlm_mech_mic(&crypto,session,challenge.flags&VIM_NTLM_FLAGS,1,mic);
            if(!vim_ntlm_equal(mic,response.mic.data,16)){puts("Final server mech MIC mismatch");goto done;}}
    }else goto done;
    puts("Real non-guest SMB1 authentication passed; checking first signed post-authentication reply");
    uid=get16(reply+28);header(packet,0x2b,uid,4,0xc815);packet[32]=1;put16(packet+33,1);put16(packet+35,1);packet[37]=0;
    memset(packet+14,0,8);packet[14]=2;struct vim_ntlm_slice sign[]={{session,16},{packet,38}};
    crypto.md5(sign,2,hash);memcpy(packet+14,hash,8);size=exchange(fd,packet,38,reply,sizeof(reply));
    if(!size||get32(reply+5))goto done;
    memcpy(mic,reply+14,8);memset(reply+14,0,8);reply[14]=3;
    struct vim_ntlm_slice verify[]={{session,16},{reply,size}};crypto.md5(verify,2,hash);
    if(!vim_ntlm_equal(mic,hash,8)){puts("Post-authentication echo signature mismatch");goto done;}
    puts("Post-authentication SMB1 request/reply signing passed");result=0;
done:
    if(wrap.data)release(wrap.data,wrap.size);if(auth.data)release(auth.data,auth.size);
    wipe(key,sizeof(key));wipe(session,sizeof(session));wipe(random_session,sizeof(random_session));wipe(hash,sizeof(hash));wipe(packet,sizeof(packet));wipe(reply,sizeof(reply));return result;
}
int main(int argc,char **argv){
    int corrupt=argc>3&&!strcmp(argv[argc-1],"--corrupt-mic");if(corrupt)argc--;
    if(argc<3||argc>4){fprintf(stderr,"usage: %s host user [domain] [--corrupt-mic]\n",argv[0]);return 2;}
    struct addrinfo hints={0},*addresses=NULL;hints.ai_socktype=SOCK_STREAM;
    if(getaddrinfo(argv[1],"445",&hints,&addresses))return 2;int fd=-1;
    for(struct addrinfo *a=addresses;a;a=a->ai_next){fd=socket(a->ai_family,a->ai_socktype,a->ai_protocol);
        if(fd>=0&&connect(fd,a->ai_addr,a->ai_addrlen)==0)break;if(fd>=0)close(fd);fd=-1;}
    freeaddrinfo(addresses);if(fd<0)return 2;
    struct timeval timeout={15,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    int result=run(fd,argv[2],argc==4?argv[3]:"",corrupt);close(fd);return result;
}
