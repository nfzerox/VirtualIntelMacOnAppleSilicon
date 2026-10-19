[Defines]
  PLATFORM_NAME = VIMBootPolicy
  PLATFORM_GUID = CEA5D04C-9A12-45CB-9CA8-3993B2C0DE4C
  PLATFORM_VERSION = 1.0
  DSC_SPECIFICATION = 0x0001001C
  OUTPUT_DIRECTORY = Build/VIMBootPolicy
  SUPPORTED_ARCHITECTURES = X64
  BUILD_TARGETS = RELEASE
  SKUID_IDENTIFIER = DEFAULT
[LibraryClasses]
  UefiDriverEntryPoint|MdePkg/Library/UefiDriverEntryPoint/UefiDriverEntryPoint.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLibRepStr/BaseMemoryLibRepStr.inf
  IoLib|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsic.inf
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  StackCheckLib|MdePkg/Library/StackCheckLibNull/StackCheckLibNull.inf
  RegisterFilterLib|MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf
[Components]
  VIMBootPolicy/BootPolicy.inf
