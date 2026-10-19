// Native, isolated EFI service/fw_cfg fixture. Never touches real I/O or NVRAM.
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

static UINT16 selected;static UINTN position;static CHAR8 policy[8];
static unsigned writes;static CHAR8 arguments[2048];static BOOLEAN present=TRUE;
EFI_RUNTIME_SERVICES runtime;
EFI_RUNTIME_SERVICES *gRT=&runtime;
VOID * EFIAPI CopyMem(VOID *out,CONST VOID *in,UINTN n){return memmove(out,in,(size_t)n);}
INTN EFIAPI CompareMem(CONST VOID *a,CONST VOID *b,UINTN n){return memcmp(a,b,(size_t)n);}
UINTN EFIAPI AsciiStrLen(CONST CHAR8 *s){return strlen(s);}
UINT16 EFIAPI IoWrite16(UINTN port,UINT16 value){assert(port==0x510);selected=value;position=0;return value;}
UINT8 EFIAPI IoRead8(UINTN port){
    static const CHAR8 name[]="opt/org.virtualintelmac/boot-policy";
    UINT8 directory[68]={0};directory[3]=1;directory[7]=8;directory[9]=0x20;
    memcpy(directory+12,name,sizeof(name));
    assert(port==0x511);
    if(selected==0){assert(position<4);return (present?"QEMU":"none")[position++];}
    if(selected==0x19){assert(position<sizeof(directory));return directory[position++];}
    assert(selected==0x20&&position<sizeof(policy));return (UINT8)policy[position++];
}
static EFI_STATUS EFIAPI get(CHAR16 *name,EFI_GUID *guid,UINT32 *attrs,UINTN *size,VOID *out){
    (void)name;(void)guid;*attrs=7;UINTN n=strlen(arguments);
    if(*size<n){*size=n;return EFI_BUFFER_TOO_SMALL;}memcpy(out,arguments,(size_t)n);*size=n;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI set(CHAR16 *name,EFI_GUID *guid,UINT32 attrs,UINTN size,VOID *value){
    (void)name;(void)guid;assert(attrs==7&&size<sizeof(arguments));
    memcpy(arguments,value,(size_t)size);arguments[size]=0;++writes;return EFI_SUCCESS;
}
#include "../compat/BootPolicy/BootPolicy.c"
int main(void){
    runtime.GetVariable=get;runtime.SetVariable=set;
    for(unsigned narrow=0;narrow<2;narrow++)for(unsigned gl=0;gl<2;gl++){
        memcpy(policy,"VIM1L000",8);policy[4]=narrow?'S':'L';policy[5]=gl?'1':'0';
        strcpy(arguments,"npci=0x2000 amfi_get_out_of_my_way=0x1 amfi_allow_any_signature=1 amfi=128 vim_gl=1 -lilubetaall");writes=0;
        assert(BootPolicyEntry(NULL,NULL)==EFI_SUCCESS&&writes==1);
        assert(strstr(arguments,"npci=0x2000")&&strstr(arguments,"-lilubetaall"));
        assert((strstr(arguments,"amfi_allow_any_signature=1")!=NULL)==narrow);
        assert((strstr(arguments,"amfi_get_out_of_my_way=0x1")!=NULL)==!narrow);
        assert(!strstr(arguments,"amfi=128"));
        assert(strstr(arguments,gl?"vim_gl=1":"vim_gl=0"));
    }
    for(unsigned byte=0;byte<8;byte++){
        memcpy(policy,"VIM1S100",8);policy[byte]='?';writes=0;
        assert(BootPolicyEntry(NULL,NULL)==EFI_SUCCESS&&writes==0);
    }
    present=FALSE;writes=0;assert(BootPolicyEntry(NULL,NULL)==EFI_SUCCESS&&writes==0);
    puts("EFI runtime policy: all four states, argument preservation and malformed/absent input passed.");
    return 0;
}
