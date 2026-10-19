// Normal OpenCore EFI driver: runs after OpenCore's NVRAM Add/Delete, before
// loading boot.efi. One immutable EFI image, with eight boot-time fw_cfg bytes.
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

STATIC EFI_GUID mAppleGuid = {0x7c436110,0xab2a,0x4bbb,{0xa8,0x80,0xfe,0x41,0x99,0x5c,0x9f,0x82}};
#define POLICY_NAME "opt/org.virtualintelmac/boot-policy"

STATIC UINT32 ReadBE32 (VOID) {
  UINT32 Value=0; UINTN Index;
  for (Index=0;Index<4;++Index) Value=(Value<<8)|IoRead8(0x511);
  return Value;
}
STATIC BOOLEAN ReadPolicy (OUT CHAR8 Policy[8]) {
  UINT32 Count,Index,Size; UINT16 Selector,Found=0; CHAR8 Name[56]; UINTN Byte;
  IoWrite16(0x510,0);
  if (ReadBE32()!=0x51454d55) return FALSE; // QEMU
  IoWrite16(0x510,0x19); Count=ReadBE32(); // FW_CFG_FILE_DIR
  if (Count>4096) return FALSE;
  for (Index=0;Index<Count;++Index) {
    Size=ReadBE32(); Selector=(UINT16)IoRead8(0x511)<<8; Selector|=IoRead8(0x511);
    (VOID)IoRead8(0x511); (VOID)IoRead8(0x511);
    for (Byte=0;Byte<sizeof(Name);++Byte) Name[Byte]=(CHAR8)IoRead8(0x511);
    if (CompareMem(Name,POLICY_NAME,sizeof(POLICY_NAME))==0) {
      if (Found||Size!=8) return FALSE;
      Found=Selector;
    }
  }
  if (!Found) return FALSE;
  IoWrite16(0x510,Found);
  for (Byte=0;Byte<8;++Byte) Policy[Byte]=(CHAR8)IoRead8(0x511);
  return CompareMem(Policy,"VIM1",4)==0&&(Policy[4]=='L'||Policy[4]=='S')&&
    (Policy[5]=='0'||Policy[5]=='1')&&Policy[6]=='0'&&Policy[7]=='0';
}
STATIC BOOLEAN Prefix(CONST CHAR8 *Text,UINTN Length,CONST CHAR8 *Match) {
  UINTN Size=AsciiStrLen(Match);
  return Length>=Size&&CompareMem(Text,Match,Size)==0;
}
EFI_STATUS EFIAPI BootPolicyEntry (IN EFI_HANDLE ImageHandle,IN EFI_SYSTEM_TABLE *SystemTable) {
  CHAR8 Policy[8],Original[2048],Updated[2048];
  UINTN Size=sizeof(Original),Index,Start,Used=0,Length;
  UINT32 Attributes=0; EFI_STATUS Status; CONST CHAR8 *Flag;
  (VOID)ImageHandle; (VOID)SystemTable;
  if (!ReadPolicy(Policy)) return EFI_SUCCESS; // Qualified legacy fallback.
  Status=gRT->GetVariable(L"boot-args",&mAppleGuid,&Attributes,&Size,Original);
  if (EFI_ERROR(Status)||Size>=sizeof(Original)) return EFI_ERROR(Status)?Status:EFI_BAD_BUFFER_SIZE;
  Original[Size]=0;
  for (Index=0;Index<Size;) {
    while(Index<Size&&Original[Index]==' ')++Index;
    Start=Index;
    while(Index<Size&&Original[Index]!=' '&&Original[Index]!=0)++Index;
    Length=Index-Start;
    if (!Length) break;
    if (Prefix(Original+Start,Length,"amfi_get_out_of_my_way=")||
        Prefix(Original+Start,Length,"amfi_allow_any_signature=")||
        Prefix(Original+Start,Length,"amfi=")||Prefix(Original+Start,Length,"vim_gl=")) continue;
    if (Used+Length+64>=sizeof(Updated)) return EFI_BAD_BUFFER_SIZE;
    if (Used) Updated[Used++]=' ';
    CopyMem(Updated+Used,Original+Start,Length);Used+=Length;
  }
  Flag=Policy[4]=='S'?"amfi_allow_any_signature=1":"amfi_get_out_of_my_way=0x1";
  if (Used) Updated[Used++]=' ';
  Length=AsciiStrLen(Flag);CopyMem(Updated+Used,Flag,Length);Used+=Length;
  Flag=Policy[5]=='1'?" vim_gl=1":" vim_gl=0";
  Length=AsciiStrLen(Flag);CopyMem(Updated+Used,Flag,Length);Used+=Length;
  // These are the ordinary guest boot arguments, not a persistent policy flag.
  // OpenCore reasserts the baseline before this driver on every subsequent boot.
  return gRT->SetVariable(L"boot-args",&mAppleGuid,Attributes,Used,Updated);
}
