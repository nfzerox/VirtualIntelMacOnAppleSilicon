// Complete Catalina's abbreviated vertex-fetch buffer metadata. The generated
// LLVM function already contains the real typed loads and stride. Only its AIR
// reflection description is incomplete for newer host AIR upgraders.
#include <dlfcn.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
static int PVGCompleteGLFetchMetadata(void *image, void *module) {
#define GL_API(result,name,args) result (*name) args = dlsym(image,#name); if(!name) return -1
    GL_API(unsigned,LLVMGetNamedMetadataNumOperands,(void *,const char *));
    GL_API(void,LLVMGetNamedMetadataOperands,(void *,const char *,void **));
    GL_API(unsigned,LLVMGetMDNodeNumOperands,(void *));
    GL_API(void,LLVMGetMDNodeOperands,(void *,void **));
    GL_API(const char *,LLVMGetMDString,(void *,unsigned *));
    GL_API(const char *,LLVMGetValueName,(void *));
    GL_API(unsigned,LLVMCountParams,(void *));
    GL_API(void *,LLVMGetParam,(void *,unsigned));
    GL_API(void *,LLVMTypeOf,(void *));
    GL_API(int,LLVMGetTypeKind,(void *));
    GL_API(void *,LLVMGetElementType,(void *));
    GL_API(int,LLVMTypeIsSized,(void *));
    GL_API(const char *,LLVMGetDataLayoutStr,(void *));
    GL_API(void *,LLVMCreateTargetData,(const char *));
    GL_API(void,LLVMDisposeTargetData,(void *));
    GL_API(unsigned long long,LLVMABISizeOfType,(void *,void *));
    GL_API(unsigned,LLVMABIAlignmentOfType,(void *,void *));
    GL_API(unsigned long long,LLVMConstIntGetZExtValue,(void *));
    GL_API(void *,LLVMGetModuleContext,(void *));
    GL_API(void *,LLVMMDStringInContext,(void *,const char *,unsigned));
    GL_API(void *,LLVMMDNodeInContext,(void *,void **,unsigned));
    GL_API(void *,LLVMInt32TypeInContext,(void *));
    GL_API(void *,LLVMConstInt,(void *,unsigned long long,int));
    GL_API(void *,LLVMValueAsMetadata,(void *));
#undef GL_API
    void (*replace)(void *,unsigned,void *)=dlsym(image,"_ZN4llvm6MDNode18replaceOperandWithEjPNS_8MetadataE");
    if(!replace) return -1;
    if(LLVMGetNamedMetadataNumOperands(module,"air.vertex")!=1) return 0;
    void *root=NULL; LLVMGetNamedMetadataOperands(module,"air.vertex",&root);
    if(LLVMGetMDNodeNumOperands(root)!=3) return 0;
    void *entry[3]; LLVMGetMDNodeOperands(root,entry);
    const char *name=LLVMGetValueName(entry[0]);
    if(!name || strcmp(name,"air.vertexFetchFunction")) return 0;
    unsigned n=LLVMGetMDNodeNumOperands(entry[2]);
    void **args=calloc(n?n:1,sizeof(void *)); if(!args) return -1;
    LLVMGetMDNodeOperands(entry[2],args);
    void *context=LLVMGetModuleContext(module);
    void *layout=LLVMCreateTargetData(LLVMGetDataLayoutStr(module));
    int changed=0;
    for(unsigned i=0;i<n;i++) {
        if(LLVMGetMDNodeNumOperands(args[i])!=6) continue;
        void *values[14]; LLVMGetMDNodeOperands(args[i],values);
        unsigned len=0; const char *kind=LLVMGetMDString(values[1],&len);
        if(!kind || len!=10 || memcmp(kind,"air.buffer",10)) continue;
        const char *location=LLVMGetMDString(values[2],&len);
        if(!location || len!=18 || memcmp(location,"air.location_index",18)) continue;
        unsigned long long index=LLVMConstIntGetZExtValue(values[0]);
        if(index>=LLVMCountParams(entry[0])) { changed=-1; break; }
        void *pointer=LLVMTypeOf(LLVMGetParam(entry[0],(unsigned)index));
        // LLVMPointerTypeKind in the matching LLVM C API (Core.h).
        if(LLVMGetTypeKind(pointer)!=12) { changed=-1; break; }
        void *type=LLVMGetElementType(pointer);
        if(!LLVMTypeIsSized(type)) { changed=-1; break; }
        unsigned long long size=LLVMABISizeOfType(layout,type);
        unsigned alignment=LLVMABIAlignmentOfType(layout,type);
        if(size>UINT32_MAX || !alignment) { changed=-1; break; }
#define GL_STRING(text) LLVMMDStringInContext(context,text,sizeof(text)-1)
        values[6]=GL_STRING("air.arg_type_size");
        values[7]=LLVMConstInt(LLVMInt32TypeInContext(context),size,0);
        values[8]=GL_STRING("air.arg_type_align_size");
        values[9]=LLVMConstInt(LLVMInt32TypeInContext(context),alignment,0);
        values[10]=GL_STRING("air.arg_type_name"); values[11]=GL_STRING("PVGVertexFetchElement");
        values[12]=GL_STRING("air.arg_name"); values[13]=GL_STRING("pvg_vertex_fetch_buffer");
#undef GL_STRING
        void *replacement=LLVMMDNodeInContext(context,values,14);
        replace(LLVMValueAsMetadata(entry[2]),i,LLVMValueAsMetadata(replacement));
        changed++;
    }
    LLVMDisposeTargetData(layout); free(args); return changed;
}
