// One immutable EFI disk, preserving each OS profile's own OpenCore root.
// Runs once before OpenCore. No NVRAM selector, protocol hooks or runtime patch.
#include <Uefi.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/DevicePath.h>

static const CHAR8 *const Profiles[]={"tiger","leopard","snowleopard","lion",
  "mountainlion","mavericks","yosemite","elcapitan","sierra","highsierra",
  "mojave","catalina","bigsur"};
static BOOLEAN Equal(CONST CHAR8 *A,CONST CHAR8 *B) {
  while(*A&&*A==*B){++A;++B;}return *A==*B;
}
static BOOLEAN ProfilePath(CONST CHAR8 *Profile,CHAR16 Path[96]) {
  UINTN I,J,Used=0;BOOLEAN Found=FALSE;
  CONST CHAR16 Prefix[]=L"\\EFI\\VIM\\",Suffix[]=L"\\OpenCore.efi";
  for(I=0;I<sizeof(Profiles)/sizeof(Profiles[0]);++I)
    if(Equal(Profile,Profiles[I]))Found=TRUE;
  if(!Found)return FALSE;
  for(J=0;Prefix[J];++J)Path[Used++]=Prefix[J];
  for(J=0;Profile[J];++J)Path[Used++]=(CHAR16)Profile[J];
  for(J=0;Suffix[J];++J)Path[Used++]=Suffix[J];
  Path[Used]=0;return TRUE;
}
#ifndef VIM_SELECTOR_TEST
static VOID Select(UINT16 Value){__asm__ __volatile__("outw %0,%1"::"a"(Value),"Nd"((UINT16)0x510));}
static UINT8 Byte(VOID){UINT8 V;__asm__ __volatile__("inb %1,%0":"=a"(V):"Nd"((UINT16)0x511));return V;}
static UINT32 BE32(VOID){UINT32 V=0;UINTN I;for(I=0;I<4;++I)V=(V<<8)|Byte();return V;}
static BOOLEAN ReadProfile(CHAR8 Profile[32]) {
  CONST CHAR8 Key[]="opt/org.virtualintelmac/firmware-profile";
  UINT32 Count,I,Size;UINT16 Selector,Found=0;UINTN J,Length=0;CHAR8 Name[57];
  Select(0);if(BE32()!=0x51454d55)return FALSE;
  Select(0x19);Count=BE32();if(Count>4096)return FALSE;
  for(I=0;I<Count;++I){
    Size=BE32();Selector=(UINT16)Byte()<<8;Selector|=Byte();(VOID)Byte();(VOID)Byte();
    for(J=0;J<56;++J)Name[J]=(CHAR8)Byte();Name[56]=0;
    if(Equal(Name,Key)){if(Found||!Size||Size>=32)return FALSE;Found=Selector;Length=Size;}
  }
  if(!Found)return FALSE;Select(Found);
  for(J=0;J<Length;++J)Profile[J]=(CHAR8)Byte();
  // QEMU's -fw_cfg string= blob deliberately excludes the NUL terminator.
  Profile[Length]=0;
  for(J=0;J<Length;++J)if(!Profile[J])return FALSE;
  return TRUE;
}
static VOID Copy(UINT8 *To,CONST UINT8 *From,UINTN Count){UINTN I;for(I=0;I<Count;++I)To[I]=From[I];}
EFI_STATUS EFIAPI EfiMain(EFI_HANDLE Image,EFI_SYSTEM_TABLE *System) {
  EFI_BOOT_SERVICES *BS=System->BootServices;EFI_STATUS Status;
  EFI_GUID LoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID,PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;
  EFI_LOADED_IMAGE_PROTOCOL *Loaded;EFI_DEVICE_PATH_PROTOCOL *Device;
  EFI_HANDLE Child;CHAR8 Profile[32];CHAR16 Path[96];UINTN Base=0,Node,Chars=0,I;
  UINT8 *Combined,*D;
  if(!ReadProfile(Profile)||!ProfilePath(Profile,Path))return EFI_NOT_FOUND;
  Status=BS->HandleProtocol(Image,&LoadedGuid,(VOID **)&Loaded);if(EFI_ERROR(Status))return Status;
  Status=BS->HandleProtocol(Loaded->DeviceHandle,&PathGuid,(VOID **)&Device);if(EFI_ERROR(Status))return Status;
  D=(UINT8 *)Device;
  while(Base<4096){
    UINTN Length=D[Base+2]|((UINTN)D[Base+3]<<8);
    if(Length<4||Length>4096-Base)return EFI_COMPROMISED_DATA;
    if(D[Base]==0x7f&&D[Base+1]==0xff)break;
    Base+=Length;
  }
  if(Base>=4096)return EFI_COMPROMISED_DATA;
  while(Path[Chars])++Chars;Node=4+(Chars+1)*sizeof(CHAR16);
  Status=BS->AllocatePool(EfiBootServicesData,Base+Node+4,(VOID **)&Combined);if(EFI_ERROR(Status))return Status;
  Copy(Combined,D,Base);Combined[Base]=4;Combined[Base+1]=4;
  Combined[Base+2]=(UINT8)Node;Combined[Base+3]=(UINT8)(Node>>8);
  Copy(Combined+Base+4,(UINT8 *)Path,(Chars+1)*sizeof(CHAR16));
  for(I=0;I<4;++I)Combined[Base+Node+I]=0;
  Combined[Base+Node]=0x7f;Combined[Base+Node+1]=0xff;Combined[Base+Node+2]=4;
  Status=BS->LoadImage(FALSE,Image,(EFI_DEVICE_PATH_PROTOCOL *)Combined,NULL,0,&Child);
  BS->FreePool(Combined);if(EFI_ERROR(Status))return Status;
  return BS->StartImage(Child,NULL,NULL);
}
#endif
