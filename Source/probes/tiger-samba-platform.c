/* Read-only runtime counterparts to the original Apple Samba configure tests.
 * Build with the genuine10.4u SDK and -liconv; never use host runtime results
 * to select guest APIs. No authentication material or file data is inspected. */
#include <stdlib.h>
#include <string.h>
#include <iconv.h>
_Static_assert(sizeof(off_t)==8,"Tiger i386 native file offsets are64 bits");
_Static_assert(sizeof(long long)==8,"Tiger i386 native long long is64 bits");
#ifdef VIM_TIGER_IFACE_AIX
#define HAVE_IFACE_AIX 1
#else
#define HAVE_IFACE_IFCONF 1
#endif
#define HAVE_SYS_SOCKIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define AUTOCONF_TEST 1
#define main samba_native_interfaces_test
#include "../vendor/source/samba-100.9/samba/source/lib/interfaces.c"
#undef main

int main(void)
{
    const char *names[]={"CP850","CP437","437","UTF-8","UTF-8-MAC"};
    unsigned i;int failed=0;struct timeval now;
    char text[4];int count=snprintf(text,sizeof(text),"%s","abcdef");
    printf("gettimeofday-two-arguments=%d c99-snprintf=%d\n",
           gettimeofday(&now,NULL)==0,count==6 && strcmp(text,"abc")==0);
    for(i=0;i<sizeof(names)/sizeof(names[0]);i++){
        iconv_t conversion=iconv_open(names[i],"UCS-2LE");
        int supported=conversion!=(iconv_t)-1;
        printf("native-iconv=%s supported=%d\n",names[i],supported);
        if(supported)iconv_close(conversion);else failed=1;
    }
    return samba_native_interfaces_test() || failed;
}
