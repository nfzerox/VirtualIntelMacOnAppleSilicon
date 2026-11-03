/* Read-only native DirectoryService node discovery; no account mutation. */
#include <DirectoryService/DirectoryService.h>
#include <stdio.h>
#include <stdlib.h>
int main(void){
    tDirReference service=0;tDirStatus status=dsOpenDirService(&service);
    if(status){printf("open=%d\n",(int)status);return 1;}
    tDataBufferPtr buffer=dsDataBufferAllocate(service,16384);unsigned long count=0;tContextData continuation=0;
    status=buffer?dsFindDirNodes(service,buffer,NULL,eDSLocalNodeNames,&count,&continuation):eMemoryAllocError;
    printf("local-node-discovery=%d count=%lu continuation=%d\n",(int)status,count,continuation!=0);
    if(!status&&count<=16){unsigned long index;for(index=1;index<=count;index++){
        tDataListPtr path=NULL;status=dsGetDirNodeName(service,buffer,index,&path);if(status)break;
        char *text=dsGetPathFromList(service,path,"/");tDirNodeReference node=0;
        tDirStatus opened=dsOpenDirNode(service,path,&node);
        printf("local-node=%s open=%d\n",text?text:"unavailable",(int)opened);
        if(node)dsCloseDirNode(node);free(text);dsDataListDeallocate(service,path);free(path);
        if(opened){status=opened;break;}
    }}
    if(continuation){dsReleaseContinueData(service,continuation);status=eDSInvalidContinueData;}
    if(buffer)dsDataBufferDeAllocate(service,buffer);dsCloseDirService(service);return status||count!=1?1:0;
}
