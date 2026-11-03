// Read-only native DirectoryService record-type/lifetime experiment.
// No values, user data, passwords, service settings or records are changed.
#include <DirectoryService/DirectoryService.h>
#include <stdio.h>
#include <stdlib.h>
static tRecordReference Open(tDirReference service,tDirNodeReference node,const char *type,const char *name){
    tDataNodePtr t=dsDataNodeAllocateString(service,type),n=dsDataNodeAllocateString(service,name);
    tRecordReference record=0;tDirStatus status=dsOpenRecord(node,t,n,&record);
    dsDataNodeDeAllocate(service,t);dsDataNodeDeAllocate(service,n);
    printf("open-status=%d\n",status);return record;
}
static void Type(tDirReference service,tRecordReference record){
    tRecordEntryPtr entry=NULL;char *type=NULL;
    tDirStatus status=dsGetRecordReferenceInfo(record,&entry);
    if(!status)status=dsGetRecordTypeFromEntry(entry,&type);
    printf("actual-record-type=%s status=%d\n",type?type:"<unavailable>",status);
    free(type);if(entry)dsDeallocRecordEntry(service,entry);
}
int main(int argc,char **argv){
    if(argc!=2)return 64;tDirReference service=0;tDirNodeReference node=0;
    if(dsOpenDirService(&service))return 1;
    tDataListPtr path=dsBuildFromPath(service,"/Local/Default","/");
    tDirStatus status=dsOpenDirNode(service,path,&node);dsDataListDeallocate(service,path);free(path);if(status)return 1;
    tRecordReference user=Open(service,node,kDSStdRecordTypeUsers,argv[1]);if(!user)return 1;
    puts("share-while-user-open");tRecordReference share=Open(service,node,kDSStdRecordTypeSharePoints,argv[1]);
    if(share){Type(service,share);dsCloseRecord(share);}dsCloseRecord(user);
    puts("share-after-user-closed");share=Open(service,node,kDSStdRecordTypeSharePoints,argv[1]);
    if(share){Type(service,share);dsCloseRecord(share);}dsCloseDirNode(node);dsCloseDirService(service);return share?0:1;
}
