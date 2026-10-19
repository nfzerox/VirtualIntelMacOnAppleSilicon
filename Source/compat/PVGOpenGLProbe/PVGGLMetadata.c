/* Native Mojave AIR schema -> donor schema. All LLVM objects belong to the
 * matched private LLVM3902 image. No cross-version LLVM object ownership. */
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *Ref;
#define APIS(X) \
 X(void,LLVMDumpModule,(Ref)) \
 X(Ref,LLVMGetModuleContext,(Ref)) \
 X(Ref,LLVMInt32TypeInContext,(Ref)) \
 X(Ref,LLVMFloatTypeInContext,(Ref)) \
 X(Ref,LLVMMDStringInContext,(Ref,const char *,unsigned)) \
 X(Ref,LLVMMDNodeInContext,(Ref,Ref *,unsigned)) \
 X(Ref,LLVMConstInt,(Ref,unsigned long long,int)) \
 X(void,LLVMAddNamedMetadataOperand,(Ref,const char *,Ref)) \
 X(unsigned,LLVMGetNamedMetadataNumOperands,(Ref,const char *)) \
 X(void,LLVMGetNamedMetadataOperands,(Ref,const char *,Ref *)) \
 X(unsigned,LLVMGetMDNodeNumOperands,(Ref)) \
 X(void,LLVMGetMDNodeOperands,(Ref,Ref *)) \
 X(const char *,LLVMGetMDString,(Ref,unsigned *)) \
 X(Ref,LLVMValueAsMetadata,(Ref)) \
 X(unsigned long long,LLVMConstIntGetZExtValue,(Ref)) \
 X(Ref,LLVMIsAConstantInt,(Ref)) \
 X(Ref,LLVMGetTypeByName,(Ref,const char *)) \
 X(Ref,LLVMGetNamedFunction,(Ref,const char *)) \
 X(Ref,LLVMTypeOf,(Ref)) \
 X(Ref,LLVMGetElementType,(Ref)) \
 X(int,LLVMGetTypeKind,(Ref)) \
 X(unsigned,LLVMGetVectorSize,(Ref)) \
 X(Ref,LLVMIsAFunction,(Ref)) \
 X(unsigned,LLVMCountParams,(Ref)) \
 X(Ref,LLVMGetParam,(Ref,unsigned)) \
 X(unsigned,LLVMCountParamTypes,(Ref)) \
 X(void,LLVMGetParamTypes,(Ref,Ref *)) \
 X(Ref,LLVMGetFirstUse,(Ref)) \
 X(Ref,LLVMGetNextUse,(Ref)) \
 X(Ref,LLVMGetUser,(Ref)) \
 X(Ref,LLVMIsACallInst,(Ref)) \
 X(unsigned,LLVMGetNumArgOperands,(Ref)) \
 X(void,LLVMSetValueName,(Ref,const char *)) \
 X(Ref,LLVMGetReturnType,(Ref)) \
 X(Ref,LLVMFunctionType,(Ref,Ref *,unsigned,int)) \
 X(Ref,LLVMAddFunction,(Ref,const char *,Ref)) \
 X(Ref,LLVMCreateBuilderInContext,(Ref)) \
 X(Ref,LLVMGetOperand,(Ref,unsigned)) \
 X(void,LLVMPositionBuilderBefore,(Ref,Ref)) \
 X(Ref,LLVMBuildCall,(Ref,Ref,Ref *,unsigned,const char *)) \
 X(void,LLVMReplaceAllUsesWith,(Ref,Ref)) \
 X(void,LLVMInstructionEraseFromParent,(Ref)) \
 X(void,LLVMDisposeBuilder,(Ref)) \
 X(void,LLVMDeleteFunction,(Ref))
#define DECL(ret,name,args) static ret (*name)args;
APIS(DECL)
#undef DECL
static void (*renameType)(Ref,const char *,size_t);
static void (*replaceOperand)(Ref,unsigned,Ref);
static pthread_once_t once=PTHREAD_ONCE_INIT;
static void resolve(void) {
    void *image=dlopen("/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/3902/Libraries/libLLVM.dylib",RTLD_NOW|RTLD_LOCAL);
    if(!image) abort();
#define LOAD(ret,name,args) name=dlsym(image,#name); if(!name) abort();
    APIS(LOAD)
#undef LOAD
    renameType=dlsym(image,"_ZN4llvm10StructType7setNameENS_9StringRefE");
    replaceOperand=dlsym(image,"_ZN4llvm6MDNode18replaceOperandWithEjPNS_8MetadataE");
    if(!renameType||!replaceOperand) abort();
}
static int stringIs(Ref value,const char *expected) {
    unsigned length=0;const char *s=LLVMGetMDString(value,&length);
    return s&&length==strlen(expected)&&!memcmp(s,expected,length);
}

int PVGGLMetadataBeforeLink(Ref module) {
    if(!module)return 0;pthread_once(&once,resolve);
    Ref context=LLVMGetModuleContext(module),integer=LLVMInt32TypeInContext(context);
    if(!LLVMGetNamedMetadataNumOperands(module,"air.compile_options")) {
        Ref text=LLVMMDStringInContext(context,"air.compile.fast_math_disable",29);
        LLVMAddNamedMetadataOperand(module,"air.compile_options",LLVMMDNodeInContext(context,&text,1));
    }
    if(!LLVMGetNamedMetadataNumOperands(module,"air.version")) {
        Ref fields[]={LLVMConstInt(integer,2,0),LLVMConstInt(integer,1,0),LLVMConstInt(integer,0,0)};
        LLVMAddNamedMetadataOperand(module,"air.version",LLVMMDNodeInContext(context,fields,3));
    }
    return 1;
}

int PVGGLMetadataEntry(Ref module) {
    if(!module)return 0;pthread_once(&once,resolve);
    if(getenv("PVG_NATIVE_METADATA_TRACE"))LLVMDumpModule(module);
    Ref context=LLVMGetModuleContext(module),integer=LLVMInt32TypeInContext(context);
    const char *oldNames[]={"_texture_2d_t","_sampler_t","_texture_3d_t"};
    const char *newNames[]={"struct._texture_2d_t","struct._sampler_t","struct._texture_3d_t"};
    for(unsigned i=0;i<sizeof(oldNames)/sizeof(oldNames[0]);++i) {
        Ref type=LLVMGetTypeByName(module,oldNames[i]);
        if(type&&!LLVMGetTypeByName(module,newNames[i])) renameType(type,newNames[i],strlen(newNames[i]));
    }
    const char *intrinsics[]={"air.sample_texture_2d.v4f32",
        "air.read_texture_2d.v4f32","air.read_texture_3d.v4f32"};
    for(unsigned intrinsic=0;intrinsic<3;++intrinsic) {
    const char *sampleName=intrinsics[intrinsic];
    unsigned oldCount=intrinsic?3:7,newCount=oldCount+1;
    Ref function=LLVMGetNamedFunction(module,sampleName);
    if(function) {
        Ref type=LLVMGetElementType(LLVMTypeOf(function)),params[8];
        unsigned n=LLVMCountParamTypes(type);
        if(n!=oldCount&&n!=newCount)return 0;
        LLVMGetParamTypes(type,params);
        if(n==oldCount) {
            if(params[oldCount-1]!=(intrinsic?integer:LLVMFloatTypeInContext(context)))return 0;
            for(Ref use=LLVMGetFirstUse(function);use;use=LLVMGetNextUse(use)) {
                Ref call=LLVMGetUser(use);
                if(!LLVMIsACallInst(call)||LLVMGetNumArgOperands(call)!=oldCount)return 0;
            }
            LLVMSetValueName(function,"pvg_old_texture_intrinsic");params[oldCount]=integer;
            Ref replacement=LLVMAddFunction(module,sampleName,LLVMFunctionType(LLVMGetReturnType(type),params,newCount,0));
            Ref builder=LLVMCreateBuilderInContext(context),use;
            while((use=LLVMGetFirstUse(function))) {
                Ref call=LLVMGetUser(use),args[8];
                for(unsigned i=0;i<oldCount;++i)args[i]=LLVMGetOperand(call,i);
                /* Big Sur Metal compiler emits trailing access::sample=0
                 * for both sampled reads and samples. GL textures use this
                 * access mode, including Photos' 3D color lookup texture. */
                args[oldCount]=LLVMConstInt(integer,0,0);
                LLVMPositionBuilderBefore(builder,call);
                Ref replacementCall=LLVMBuildCall(builder,replacement,args,newCount,"pvg_texture");
                LLVMReplaceAllUsesWith(call,replacementCall);LLVMInstructionEraseFromParent(call);
            }
            LLVMDisposeBuilder(builder);LLVMDeleteFunction(function);
        } else if(params[oldCount]!=integer)return 0;
    }
    }
    const char *stages[]={"air.vertex","air.fragment"};
    for(unsigned s=0;s<2;++s) {
        unsigned entries=LLVMGetNamedMetadataNumOperands(module,stages[s]);
        if(!entries)continue;if(entries!=1)return 0;
        Ref entry;LLVMGetNamedMetadataOperands(module,stages[s],&entry);
        if(LLVMGetMDNodeNumOperands(entry)!=3)return 0;
        Ref fields[3];LLVMGetMDNodeOperands(entry,fields);
        unsigned n=LLVMGetMDNodeNumOperands(fields[2]);
        Ref *args=calloc(n?n:1,sizeof(Ref));if(!args)return 0;
        LLVMGetMDNodeOperands(fields[2],args);
        for(unsigned a=0;a<n;++a) {
            unsigned count=LLVMGetMDNodeNumOperands(args[a]);
            if(count!=4&&count!=5&&count!=8)continue;
            Ref f[8];LLVMGetMDNodeOperands(args[a],f);
            if(count==5&&stringIs(f[1],"air.fragment_input")) {
                /* Paired frontend IR differs by a missing arg_type_name.
                 * Preserve interpolation qualifiers; derive float shape from
                 * the actual entry parameter, never a hardcoded float3. */
                if(!LLVMIsAConstantInt(f[0])||!LLVMIsAFunction(fields[0])) {free(args);return 0;}
                unsigned long long index=LLVMConstIntGetZExtValue(f[0]);
                if(index>=LLVMCountParams(fields[0])) {free(args);return 0;}
                Ref type=LLVMTypeOf(LLVMGetParam(fields[0],(unsigned)index));
                unsigned width=1;
                if(LLVMGetTypeKind(type)==13) {width=LLVMGetVectorSize(type);type=LLVMGetElementType(type);}
                /* Integer signedness cannot be inferred from LLVM i32. */
                if(type!=LLVMFloatTypeInContext(context)||width<1||width>4) {free(args);return 0;}
                char name[16];if(width==1)strcpy(name,"float");else snprintf(name,sizeof(name),"float%u",width);
                Ref out[]={f[0],f[1],f[2],f[3],f[4],LLVMMDStringInContext(context,"air.arg_type_name",17),
                    LLVMMDStringInContext(context,name,(unsigned)strlen(name))};
                Ref node=LLVMMDNodeInContext(context,out,7);
                replaceOperand(LLVMValueAsMetadata(fields[2]),a,LLVMValueAsMetadata(node));
            } else if(count==4&&stringIs(f[1],"air.sampler")&&stringIs(f[2],"air.binding")) {
                Ref out[]={f[0],f[1],LLVMMDStringInContext(context,"air.location_index",18),f[3],LLVMConstInt(integer,1,0)};
                Ref node=LLVMMDNodeInContext(context,out,5);
                replaceOperand(LLVMValueAsMetadata(fields[2]),a,LLVMValueAsMetadata(node));
            } else if(count==8&&stringIs(f[1],"air.buffer")&&
                      stringIs(f[2],"air.location_index")&&LLVMIsAConstantInt(f[3])&&
                      LLVMConstIntGetZExtValue(f[3])==12) {
                /* Exact observed default-uniform schema, NOT arbitrary UBOs. */
                replaceOperand(LLVMValueAsMetadata(args[a]),3,LLVMValueAsMetadata(LLVMConstInt(integer,0,0)));
            }
        }
        free(args);
    }
    return 1;
}
