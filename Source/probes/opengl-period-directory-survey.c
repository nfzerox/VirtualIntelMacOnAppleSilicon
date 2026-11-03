// Read-only DirectoryService control for a real period Mac. Reports schemas
// and numeric API status, not account identifiers, hashes or credentials.
#include <DirectoryService/DirectoryService.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int Attribute(tDirReference service,tRecordReference record,const char *name){
    tDataNodePtr type=dsDataNodeAllocateString(service,name);tAttributeEntryPtr info=NULL;
    tDirStatus status=type?dsGetRecordAttributeInfo(record,type,&info):eMemoryAllocError;
    printf("attribute=%s status=%d count=%u\n",name,(int)status,info?(unsigned)info->fAttributeValueCount:0);
    if(status==eDSNoErr&&info&&info->fAttributeValueCount){
        tAttributeValueEntryPtr value=NULL;status=dsGetRecordAttributeValueByIndex(record,type,1,&value);
        printf("first-value status=%d bytes=%u\n",(int)status,value?(unsigned)value->fAttributeValueData.fBufferLength:0);
        if(value)dsDeallocAttributeValueEntry(service,value);
    }
    if(info)dsDeallocAttributeEntry(service,info);if(type)dsDataNodeDeAllocate(service,type);
    return status==eDSNoErr?0:1;
}
int main(int argc,char **argv){
    if(argc!=2||strlen(argv[1])>255)return 64;
    tDirReference service=0;tDirNodeReference node=0;tRecordReference record=0;
    tDirStatus status=dsOpenDirService(&service);printf("directory-open=%d\n",(int)status);if(status)return 1;
    tDataListPtr path=dsBuildFromPath(service,"/Local/Default","/");
    status=path?dsOpenDirNode(service,path,&node):eMemoryAllocError;
    if(path){dsDataListDeallocate(service,path);free(path);}printf("local-node-open=%d\n",(int)status);
    int failed=1;
    if(!status){
        tDataNodePtr type=dsDataNodeAllocateString(service,kDSStdRecordTypeUsers),name=dsDataNodeAllocateString(service,argv[1]);
        status=type&&name?dsOpenRecord(node,type,name,&record):eMemoryAllocError;
        if(type)dsDataNodeDeAllocate(service,type);if(name)dsDataNodeDeAllocate(service,name);
        printf("user-record-open=%d\n",(int)status);
        if(!status){
            failed=Attribute(service,record,kDS1AttrUniqueID)|Attribute(service,record,kDS1AttrPrimaryGroupID)|
                Attribute(service,record,kDS1AttrGeneratedUID)|Attribute(service,record,kDS1AttrNFSHomeDirectory);
            dsCloseRecord(record);
        }
        dsCloseDirNode(node);
    }
    dsCloseDirService(service);return failed;
}
