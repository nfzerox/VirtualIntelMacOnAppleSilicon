/* Native server-side GLI admission in this isolated process only. No server
 * attachment, display surface, context, mutation or on-disk OS patch. */
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static void records(const char *label,const void *record)
{
    for(unsigned n=0;record&&n<4;++n){
        printf("%s[%u]",label,n);
        /* Both native server consumers read this measured metadata prefix.
         * Skip pointer words: show only scalar capability/format fields. */
        for(unsigned offset=sizeof(void *);offset<52;offset+=4){
            uint32_t word;memcpy(&word,(const char *)record+offset,4);
            printf(" %02x=%x",offset,word);
        }
        puts("");memcpy(&record,record,sizeof(record));
    }
}
int main(void)
{
    int (*initialize)(const io_service_t *,const unsigned char *,unsigned,void *)=dlsym(RTLD_DEFAULT,"cglsInitialize");
    int (*choose)(void **,const int *)=dlsym(RTLD_DEFAULT,"cglsChoosePixelFormat");
    int (*query)(void **,unsigned)=dlsym(RTLD_DEFAULT,"cglsQueryRendererInfo");
    int (*destroy)(void *)=dlsym(RTLD_DEFAULT,"cglsDestroyPixelFormat");
    int (*destroy_info)(void *)=dlsym(RTLD_DEFAULT,"cglsDestroyRendererInfo");
    if(!initialize||!choose||!query||!destroy||!destroy_info){puts("Native exported CGLS contract unavailable.");return 77;}
    io_service_t accelerator=0;UInt32 index=0;
    IOReturn status=IOAccelFindAccelerator(CGDisplayIOServicePort(CGMainDisplayID()),&accelerator,&index);
    if(status||!accelerator||index>255)return 1;
    unsigned char slot=index;int result=initialize(&accelerator,&slot,1,NULL);
    printf("initialize=%d\n",result);IOObjectRelease(accelerator);
    if(result)return 2;
    int attributes[]={80,20,8,21,8,22,8,12,0,13,0,0};void *record=NULL;
    result=choose(&record,attributes);printf("choose=%d\n",result);records("format",record);
    if(record)destroy(record);record=NULL;
    int info_result=query(&record,1);printf("query=%d\n",info_result);records("renderer",record);
    if(record)destroy_info(record);
    return result||info_result?3:0;
}
