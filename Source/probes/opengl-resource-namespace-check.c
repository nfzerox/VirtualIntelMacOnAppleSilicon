/* Finite native/fallback lifetime-identity probe; no GL context or rendering. */
#include "../PassthroughOpenGL/guest-resource-namespace.h"
#include <stdio.h>
int main(void)
{
    uuid_t first,second;
    int valid=VIMGetResourceNamespace(first)&&VIMGetResourceNamespace(second)&&
        !uuid_is_null(first)&&!memcmp(first,second,sizeof(first));
    printf("resource-namespace=%s pointer-bits=%u\n",valid?"PASS":"UNAVAILABLE",(unsigned)(sizeof(void*)*8));
    return valid?0:1;
}
