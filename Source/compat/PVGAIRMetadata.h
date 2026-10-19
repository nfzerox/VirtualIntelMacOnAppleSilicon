/* Canonicalize legacy Core Image buffer metadata using LLVM's own API.
 * Mojave metalfe puts air.location_index immediately after air.buffer.
 * Core Image's DAG emitter puts it later, which crashes the host AIR upgrader.
 * LLVMValueAsMetadata / MDNode::replaceOperandWith semantics are verified in
 * research/apple-llvm-project/llvm/lib/IR/Core.cpp and IR/Metadata.h.
 */
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

/* Older metalfe fragment varyings carry a trailing air.location integer.
 * Modern Apple GPU compilation crashes parsing that obsolete pair. Keep the
 * generated varying name, type and interpolation qualifiers unchanged.
 * Only remove the exact trailing pair on a typed, named fragment input.
 * Returns the number repaired, or -1 if this LLVM API is unavailable.
 */
static int PVGRepairAIRFragmentLocations(void *image, void *module, void *context) {
    unsigned (*namedCount)(void *,const char *)=dlsym(image,"LLVMGetNamedMetadataNumOperands");
    void (*namedOperands)(void *,const char *,void **)=dlsym(image,"LLVMGetNamedMetadataOperands");
    unsigned (*count)(void *)=dlsym(image,"LLVMGetMDNodeNumOperands");
    void (*operands)(void *,void **)=dlsym(image,"LLVMGetMDNodeOperands");
    const char *(*string)(void *,unsigned *)=dlsym(image,"LLVMGetMDString");
    void *(*integer)(void *)=dlsym(image,"LLVMIsAConstantInt");
    void *(*node)(void *,void **,unsigned)=dlsym(image,"LLVMMDNodeInContext");
    void *(*metadata)(void *)=dlsym(image,"LLVMValueAsMetadata");
    void (*replace)(void *,unsigned,void *)=dlsym(image,"_ZN4llvm6MDNode18replaceOperandWithEjPNS_8MetadataE");
    if(!namedCount||!namedOperands||!count||!operands||!string||!integer||!node||!metadata||!replace) return -1;
    unsigned n=namedCount(module,"air.fragment");
    void **entries=calloc(n?n:1,sizeof(void *));
    if(!entries) return -1;
    namedOperands(module,"air.fragment",entries);
    int changed=0;
    for(unsigned e=0;e<n;e++) {
        if(count(entries[e])!=3) continue;
        void *entry[3]; operands(entries[e],entry);
        unsigned na=count(entry[2]);
        void **args=calloc(na?na:1,sizeof(void *));
        if(!args) { free(entries); return -1; }
        operands(entry[2],args);
        for(unsigned a=0;a<na;a++) {
            unsigned nc=count(args[a]);
            if(nc<8) continue;
            void **values=calloc(nc,sizeof(void *));
            if(!values) { free(args); free(entries); return -1; }
            operands(args[a],values);
            unsigned len=0;
            const char *kind=string(values[1],&len);
            int match=kind && len==18 && !memcmp(kind,"air.fragment_input",18);
            const char *key=string(values[nc-2],&len);
            match=match && key && len==12 && !memcmp(key,"air.location",12) && integer(values[nc-1]);
            int hasType=0, hasName=0;
            for(unsigned i=2;match && i+1<nc-2;i++) {
                key=string(values[i],&len);
                if(key && len==17 && !memcmp(key,"air.arg_type_name",17)) hasType=1;
                if(key && len==12 && !memcmp(key,"air.arg_name",12)) hasName=1;
            }
            if(match && hasType && hasName) {
                void *replacement=node(context,values,nc-2);
                replace(metadata(entry[2]),a,metadata(replacement));
                changed++;
            }
            free(values);
        }
        free(args);
    }
    free(entries);
    return changed;
}

static int PVGCanonicalizeAIRBuffers(void *image, void *module, void *context) {
    unsigned (*namedCount)(void *,const char *)=dlsym(image,"LLVMGetNamedMetadataNumOperands");
    void (*namedOperands)(void *,const char *,void **)=dlsym(image,"LLVMGetNamedMetadataOperands");
    unsigned (*count)(void *)=dlsym(image,"LLVMGetMDNodeNumOperands");
    void (*operands)(void *,void **)=dlsym(image,"LLVMGetMDNodeOperands");
    const char *(*string)(void *,unsigned *)=dlsym(image,"LLVMGetMDString");
    void *(*node)(void *,void **,unsigned)=dlsym(image,"LLVMMDNodeInContext");
    void *(*metadata)(void *)=dlsym(image,"LLVMValueAsMetadata");
    void (*replace)(void *,unsigned,void *)=dlsym(image,"_ZN4llvm6MDNode18replaceOperandWithEjPNS_8MetadataE");
    if (!namedCount||!namedOperands||!count||!operands||!string||!node||!metadata||!replace) return -1;
    int changed=0;
    const char *stages[]={"air.kernel","air.vertex","air.fragment"};
    for(unsigned stage=0;stage<3;stage++) {
        unsigned n=namedCount(module,stages[stage]);
        void **entries=calloc(n?n:1,sizeof(void*));
        if(!entries) return -1;
        namedOperands(module,stages[stage],entries);
        for(unsigned e=0;e<n;e++) {
            if(count(entries[e])!=3) continue;
            void *entry[3]; operands(entries[e],entry);
            unsigned na=count(entry[2]);
            void **args=calloc(na?na:1,sizeof(void*));
            if(!args) { free(entries); return -1; }
            operands(entry[2],args);
            for(unsigned a=0;a<na;a++) {
                unsigned nc=count(args[a]);
                if(nc<5) continue;
                void **values=calloc(nc,sizeof(void*));
                void **ordered=calloc(nc,sizeof(void*));
                if(!values||!ordered) { free(values);free(ordered);free(args);free(entries);return -1; }
                operands(args[a],values);
                unsigned length=0;
                const char *kind=string(values[1],&length);
                if(kind && length==10 && !memcmp(kind,"air.buffer",10)) {
                    for(unsigned i=2;i+2<nc;i++) {
                        const char *key=string(values[i],&length);
                        if(!key||length!=18||memcmp(key,"air.location_index",18)) continue;
                        if(i!=2) {
                            ordered[0]=values[0]; ordered[1]=values[1];
                            memcpy(ordered+2,values+i,3*sizeof(void*));
                            memcpy(ordered+5,values+2,(i-2)*sizeof(void*));
                            memcpy(ordered+i+3,values+i+3,(nc-i-3)*sizeof(void*));
                            void *replacement=node(context,ordered,nc);
                            replace(metadata(entry[2]),a,metadata(replacement));
                            changed++;
                        }
                        break;
                    }
                }
                free(ordered); free(values);
            }
            free(args);
        }
        free(entries);
    }
    return changed;
}
