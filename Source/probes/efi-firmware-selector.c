// Pure name/path test. Does not execute port I/O or firmware boot services.
#define VIM_SELECTOR_TEST 1
#include "../compat/FirmwareSelector/FirmwareSelector.c"
#include <stdio.h>
int main(void) {
  UINTN I;CHAR16 Path[96];
  for(I=0;I<sizeof(Profiles)/sizeof(Profiles[0]);++I){
    if(!ProfilePath(Profiles[I],Path)||Path[0]!='\\')return 1;
  }
  if(ProfilePath("",Path)||ProfilePath("unknown",Path)||
     ProfilePath("../bigsur",Path)||ProfilePath("bigsur/other",Path)||
     ProfilePath("BigSur",Path)||ProfilePath("bigsur\n",Path))return 2;
  puts("PASS: thirteen firmware profiles, no arbitrary path or unknown-profile fallback");return 0;
}
