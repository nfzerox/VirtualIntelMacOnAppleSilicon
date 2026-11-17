// Pure name/path test. Does not execute port I/O or firmware boot services.
#define VIM_SELECTOR_TEST 1
#include "../compat/FirmwareSelector/FirmwareSelector.c"
#include <stdio.h>
int main(void) {
  static const CHAR8 *const Profiles[]={"tiger","leopard","snowleopard","lion",
    "mountainlion","mavericks","yosemite","elcapitan","sierra","highsierra",
    "mojave","catalina","bigsur","monterey","ventura","sonoma","sequoia","tahoe","macos27"};
  UINTN I;CHAR16 Path[96];
  for(I=0;I<sizeof(Profiles)/sizeof(Profiles[0]);++I){
    if(!ProfilePath(Profiles[I],Path)||Path[0]!='\\')return 1;
  }
  if(ProfilePath("",Path)||ProfilePath("../bigsur",Path)||ProfilePath("bigsur/other",Path)||
     ProfilePath("BigSur",Path)||ProfilePath("bigsur\n",Path)||ProfilePath("big sur",Path)||
     ProfilePath("abcdefghijklmnopqrstuvwxyz0123456789",Path))return 2;
  puts("PASS: any one-component lowercase profile name; no path, case or length escape");return 0;
}
