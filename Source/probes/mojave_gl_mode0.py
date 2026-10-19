"""Process-local native Mojave GL experiment; no on-disk patching."""
import lldb
import struct
import os

configs = {}
native_programs = []
expanded_descriptions = {}
glsl_descriptions = {}
uniform_trace_armed = False
draw_breakpoint = None
encoder_returns = {}
binding_selectors = {}
binding_addresses = set()
pipeline_shadows = {}
config_returns = {}
config_shadows = {}
config_adapters = {}
config_adapter_returns = {}
config_destroy_returns = {}

def dump_module(frame,module):
    # The compiled adapter has private function-pointer variables named like
    # LLVM APIs. Resolve the real LLVM export explicitly to avoid ambiguity.
    return frame.EvaluateExpression('''((void (*)(void *))dlsym(
        (void *)dlopen("/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/3902/Libraries/libLLVM.dylib",2),
        "LLVMDumpModule"))((void *)0x%x)''' % module)

def install_config_hooks(frame, bp_loc, _):
    target=frame.GetThread().GetProcess().GetTarget()
    addresses=[]
    for name in ('GLDContextRec::populateConfigData(GLDContextConfigRec*)',
                 'GLDContextRec::dealloc()'):
        matches=target.FindFunctions(name)
        if matches.GetSize()!=1:
            print('ambiguous/missing donor function: '+name)
            return True
        context=matches.GetContextAtIndex(0)
        symbol=context.GetSymbol()
        address=(symbol.GetStartAddress() if symbol.IsValid() else
                 context.GetFunction().GetStartAddress()).GetLoadAddress(target)
        addresses.append(address)
    result=frame.EvaluateExpression('(void *)dlopen("/Users/Shared/pvg-probes/libPVGGLConfig.dylib",2)')
    if result.GetError().Fail() or not result.GetValueAsUnsigned(): return True
    result=frame.EvaluateExpression('(int)PVGGLConfigHooksInstall((void *)0x%x,(void *)0x%x)' % tuple(addresses))
    print('compiled donor config hooks installed: '+str(result.GetValueAsUnsigned()))
    if result.GetError().Fail() or result.GetValueAsUnsigned()!=1: return True
    bp_loc.GetBreakpoint().SetEnabled(False)
    return False

def returned(frame, bp_loc, _):
    bp_loc.GetBreakpoint().SetEnabled(False)
    bp_id=bp_loc.GetBreakpoint().GetID()
    address = configs.pop(bp_id)
    process = frame.GetThread().GetProcess()
    error = lldb.SBError()
    value = process.ReadUnsignedFromMemory(address + 25, 1, error)
    if not error.Success():
        print(error)
        return True
    process.WriteMemory(address + 25, struct.pack('B', value & 128), error)
    print('native mode0 config=0x%x previous=%d write=%s' %
          (address, value, error.Success()))
    if bp_id in config_returns:
        original=config_returns.pop(bp_id)
        adapter=config_adapter_returns.pop(bp_id,None)
        if adapter:
            result=frame.EvaluateExpression('(int)PVGGLConfigPublish((void *)0x%x)' % adapter)
            print('compiled config publish: '+str(result.GetValueAsUnsigned()))
            if result.GetError().Fail() or result.GetValueAsUnsigned()!=1: return True
        else:
            data=process.ReadMemory(address,0x250,error)
            if not error.Success(): return True
            process.WriteMemory(original,data,error)
            print('published native config prefix: '+str(error.Success()))
    return not error.Success()

def entering(frame, bp_loc, _):
    process = frame.GetThread().GetProcess()
    error = lldb.SBError()
    # LLDB can place named breakpoints after the prologue. The caller frame's
    # PC is the return site; do not assume RSP still points at the return PC.
    ret = frame.GetThread().GetFrameAtIndex(1).GetPC()
    if not error.Success():
        return True
    bp = process.GetTarget().BreakpointCreateByAddress(ret)
    bp.SetThreadID(frame.GetThread().GetThreadID())
    bp.SetOneShot(True)
    original=frame.FindRegister('rsi').GetValueAsUnsigned()
    address=original
    if os.environ.get('PVG_NATIVE_CONFIG_SHADOW'):
        ctx=frame.FindRegister('rdi').GetValueAsUnsigned()
        data=process.ReadMemory(original,0x250,error)
        if not error.Success(): return True
        if os.environ.get('PVG_NATIVE_CONFIG_HELPER'):
            result=frame.EvaluateExpression('''(void *)dlopen(
                "/Users/Shared/pvg-probes/libPVGGLConfig.dylib",2)''')
            if result.GetError().Fail() or not result.GetValueAsUnsigned(): return True
            result=frame.EvaluateExpression('(void *)PVGGLConfigCreate((void *)0x%x,(unsigned long)0x250)' % original)
            adapter=result.GetValueAsUnsigned()
            if result.GetError().Fail() or not adapter: return True
            config_adapters[ctx]=adapter
            config_adapter_returns[bp.GetID()]=adapter
            result=frame.EvaluateExpression('(void *)PVGGLConfigDonor((void *)0x%x)' % adapter)
        else:
            result=frame.EvaluateExpression('(void *)calloc(1,0x260)')
        address=result.GetValueAsUnsigned()
        if not address or result.GetError().Fail(): return True
        process.WriteMemory(address,data,error)
        if not error.Success(): return True
        process.WriteMemory(ctx+0xa0,struct.pack('<Q',address),error)
        if not error.Success(): return True
        if not frame.FindRegister('rsi').SetValueFromCString(hex(address)): return True
        config_returns[bp.GetID()]=original
        config_shadows[ctx]=(original,address)
        print('CONFIG shadow original=0x%x donor=0x%x ctx=0x%x' % (original,address,ctx))
    configs[bp.GetID()] = address
    bp.SetScriptCallbackFunction(__name__ + '.returned')
    return False

def config_dealloc_enter(frame,bp_loc,_):
    ctx=frame.FindRegister('rdi').GetValueAsUnsigned()
    adapter=config_adapters.pop(ctx,None)
    if not adapter: return False
    target=frame.GetThread().GetProcess().GetTarget()
    bp=target.BreakpointCreateByAddress(frame.GetThread().GetFrameAtIndex(1).GetPC())
    bp.SetThreadID(frame.GetThread().GetThreadID());bp.SetOneShot(True)
    config_destroy_returns[bp.GetID()]=adapter
    bp.SetScriptCallbackFunction(__name__+'.config_dealloc_return')
    return False

def config_dealloc_return(frame,bp_loc,_):
    bp_loc.GetBreakpoint().SetEnabled(False)
    adapter=config_destroy_returns.pop(bp_loc.GetBreakpoint().GetID(),None)
    if adapter:
        result=frame.EvaluateExpression('({ (void)PVGGLConfigDestroy((void *)0x%x); (int)1; })' % adapter)
        print('compiled config destroyed after donor dealloc: '+str(result.GetValueAsUnsigned()))
        return result.GetError().Fail() or result.GetValueAsUnsigned()!=1
    return False

def linking(frame, bp_loc, _):
    error = lldb.SBError()
    value = frame.GetThread().GetProcess().ReadUnsignedFromMemory(
        frame.FindRegister('rcx').GetValueAsUnsigned() + 20, 4, error)
    print('ShLink codegen=%d read=%s' % (value, error.Success()))
    requested = os.environ.get('PVG_NATIVE_CODEGEN')
    if requested and error.Success():
        value = int(requested, 0)
        if value not in (3, 11):
            raise ValueError('research codegen must be 3 or 11')
        frame.GetThread().GetProcess().WriteMemory(
            frame.FindRegister('rcx').GetValueAsUnsigned() + 20,
            struct.pack('<I', value), error)
        print('research combined codegen=%d write=%s' % (value, error.Success()))
    return False

def engine_link(frame, bp_loc, _):
    error = lldb.SBError()
    process = frame.GetThread().GetProcess()
    context = frame.FindRegister('rdi').GetValueAsUnsigned()
    program = frame.FindRegister('rsi').GetValueAsUnsigned()
    if program not in native_programs:
        native_programs.append(program)
    # Verified against this Mojave GLEngine disassembly; research only.
    config = process.ReadPointerFromMemory(context + 0xb0b8, error)
    mode = process.ReadUnsignedFromMemory(config + 25, 1, error)
    print('gleLinkProgram context=0x%x config=0x%x mode=%d read=%s' %
          (context, config, mode, error.Success()))
    return False

def metadata(frame, bp_loc, _):
    error = lldb.SBError()
    module = frame.GetThread().GetProcess().ReadPointerFromMemory(
        frame.FindRegister('rdi').GetValueAsUnsigned() + 0x108, error)
    if error.Success() and os.environ.get('PVG_NATIVE_METADATA_COMPILED'):
        result=frame.EvaluateExpression('(int)PVGGLMetadataBeforeLink((void *)0x%x)' % module)
        print('compiled metadata before link: '+str(result.GetValueAsUnsigned()))
        if result.GetError().Success() and result.GetValueAsUnsigned()==1 and os.environ.get('PVG_NATIVE_METADATA_REPEAT'):
            result=frame.EvaluateExpression('(int)PVGGLMetadataBeforeLink((void *)0x%x)' % module)
            print('repeated compiled metadata: '+str(result.GetValueAsUnsigned()))
        return result.GetError().Fail() or result.GetValueAsUnsigned()!=1
    if error.Success():
        result = dump_module(frame,module)
        print('native IR dump: %s' % result.GetError())
        if os.environ.get('PVG_NATIVE_STRICT_MATH'):
            # Research only: native dump lacks compile_options and contains no
            # fast-math attributes. Supply explicit strict-math metadata rather
            # than skipping the donor's metadata query or enabling fast math.
            result = frame.EvaluateExpression('''({
                void *m=(void *)0x%x;
                void *c=(void *)LLVMGetModuleContext(m);
                void *s=(void *)LLVMMDStringInContext(c,
                    "air.compile.fast_math_disable",29);
                void *n=(void *)LLVMMDNodeInContext(c,&s,1);
                (void)LLVMAddNamedMetadataOperand(m,"air.compile_options",n);
            })''' % module)
            print('strict math metadata: %s' % result.GetError())
        if os.environ.get('PVG_NATIVE_LANGUAGE_METADATA'):
            # Schema hypothesis from captured matched GL frontend output.
            # Not a claim that relabeling alone establishes compatibility.
            result = frame.EvaluateExpression('''({
                void *m=(void *)0x%x;
                void *c=(void *)LLVMGetModuleContext(m);
                void *t=(void *)LLVMInt32TypeInContext(c);
                void *v[4];
                v[0]=(void *)LLVMMDStringInContext(c,"OpenGL GLSL",11);
                v[1]=(void *)LLVMConstInt(t,1,0);
                v[2]=(void *)LLVMConstInt(t,0,0); v[3]=v[2];
                void *n=(void *)LLVMMDNodeInContext(c,v,4);
                (void)LLVMAddNamedMetadataOperand(m,"air.language_version",n);
            })''' % module)
            print('language metadata: %s' % result.GetError())
        if os.environ.get('PVG_NATIVE_AIR21_METADATA'):
            # Test the AIR2.1 consumer schema used by the existing Mojave
            # bridge. Rendering tests must establish compatibility, not labels.
            result = frame.EvaluateExpression('''({
                void *m=(void *)0x%x;
                void *c=(void *)LLVMGetModuleContext(m);
                void *t=(void *)LLVMInt32TypeInContext(c);
                void *v[3];
                v[0]=(void *)LLVMConstInt(t,2,0);
                v[1]=(void *)LLVMConstInt(t,1,0);
                v[2]=(void *)LLVMConstInt(t,0,0);
                void *n=(void *)LLVMMDNodeInContext(c,v,3);
                (void)LLVMAddNamedMetadataOperand(m,"air.version",n);
            })''' % module)
            print('AIR2.1 metadata: %s' % result.GetError())
    return False

def lowered_ir(frame,bp_loc,_):
    # Other compiler exports use this name with different internal arguments.
    # Inspect only the bridge call whose C signature is in PVGOpenGLMetal.m.
    caller=frame.GetThread().GetFrameAtIndex(1).GetFunctionName() or ''
    if 'PVGGLIRFunction newFunctionWithPluginData:' not in caller: return False
    print('COMPILER EXPORT '+str(frame.GetModule().GetFileSpec()))
    if frame.GetModule().GetFileSpec().GetFilename()!='libAppleParavirtCompilerPlugin.dylib': return False
    module=frame.FindRegister('r8').GetValueAsUnsigned()
    if module:
        print('BEGIN lowered compiler IR')
        result=dump_module(frame,module)
        print('END lowered compiler IR: %s' % result.GetError())
    return False

def entry_dump(frame, bp_loc, _):
    error = lldb.SBError()
    if os.environ.get('PVG_NATIVE_METADATA_COMPILED'):
        module=frame.GetThread().GetProcess().ReadPointerFromMemory(
            frame.FindRegister('rdi').GetValueAsUnsigned()+0x108,error)
        if error.Fail(): return True
        result=frame.EvaluateExpression('(int)PVGGLMetadataEntry((void *)0x%x)' % module)
        print('compiled entry metadata: '+str(result.GetValueAsUnsigned()))
        if result.GetError().Success() and result.GetValueAsUnsigned()==1 and os.environ.get('PVG_NATIVE_METADATA_REPEAT'):
            result=frame.EvaluateExpression('(int)PVGGLMetadataEntry((void *)0x%x)' % module)
            print('repeated compiled entry metadata: '+str(result.GetValueAsUnsigned()))
        return result.GetError().Fail() or result.GetValueAsUnsigned()!=1
    if os.environ.get('PVG_NATIVE_VERTEX_KEY') and 'GLDVertexShader' in (frame.GetThread().GetFrameAtIndex(1).GetFunctionName() or ''):
        import binascii
        process=frame.GetThread().GetProcess()
        obj=frame.FindRegister('rdi').GetValueAsUnsigned()
        key=process.ReadPointerFromMemory(obj+0xc08,error)
        raw=process.ReadMemory(key,0xa0,error)
        if error.Success(): print('VERTEX KEY '+str(binascii.hexlify(raw)))
        if os.environ.get('PVG_NATIVE_ZERO_VERTEX_FETCH'):
            # Causal probe ONLY: this test shader uses gl_VertexID and no
            # attributes. Never apply to arbitrary programs or production.
            if not error.Success() or struct.unpack('<Q',raw[:8])[0]!=1:
                print('refused vertex-fetch causal probe');return True
            process.WriteMemory(key,struct.pack('<Q',0),error)
            print('research zero vertex-fetch key: '+str(error.Success()))
            if not error.Success(): return True
        if os.environ.get('PVG_NATIVE_REFERENCE_VERTEX_KEY'):
            # Exact paired no-attribute shader experiment, not a general key
            # adapter. Refuse any other shape rather than masking real inputs.
            if not error.Success() or struct.unpack('<Q',raw[:8])[0]!=1 or raw[0x70:0x80]!=b'\0\0\4'+b'\0'*13:
                print('refused paired vertex-key probe');return True
            paired=b'\0'*8+raw[8:0x70]+b'\0'*14+b'\1\0'
            process.WriteMemory(key,paired,error)
            print('research paired vertex key: '+str(error.Success()))
            if not error.Success(): return True
    module = frame.GetThread().GetProcess().ReadPointerFromMemory(
        frame.FindRegister('rdi').GetValueAsUnsigned() + 0x108, error)
    if error.Success():
        print('BEGIN untouched entry IR')
        result = dump_module(frame,module)
        print('END untouched entry IR: %s' % result.GetError())
        if os.environ.get('PVG_NATIVE_RESOURCE_TYPE_NAMES'):
            result = frame.EvaluateExpression('''({
                void *m=(void *)0x%x;
                void *image=(void *)dlopen("/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/3902/Libraries/libLLVM.dylib",2);
                void (*rename)(void*,const char*,unsigned long)=(void(*)(void*,const char*,unsigned long))
                    dlsym(image,"_ZN4llvm10StructType7setNameENS_9StringRefE");
                const char *oldNames[2]={"_texture_2d_t","_sampler_t"};
                const char *newNames[2]={"struct._texture_2d_t","struct._sampler_t"};
                for(unsigned i=0;rename && i<2;i++) {
                    void *t=(void *)LLVMGetTypeByName(m,oldNames[i]);
                    if(t && !(void *)LLVMGetTypeByName(m,newNames[i])) {
                        rename(t,newNames[i],(unsigned long)strlen(newNames[i]));
                        (int)printf("PVG resource type normalized %%s\\n",newNames[i]);
                    }
                }
            })''' % module)
            print('resource type research mapping: %s' % result.GetError())
        if os.environ.get('PVG_NATIVE_TEXTURE_INTRINSIC'):
            # Bounded 2D sampling experiment from paired frontend IR: native
            # has seven arguments, matched donor adds a trailing zero i32. Only
            # accept that exact call shape; do not infer other intrinsic ABIs.
            result = frame.EvaluateExpression('''({
                void *m=(void *)0x%x, *c=(void *)LLVMGetModuleContext(m);
                const char *pvgSampleName="air.sample_texture_2d.v4f32";
                void *fn=(void *)LLVMGetNamedFunction(m,pvgSampleName);
                if(fn) {
                    void *ty=(void *)LLVMGetElementType((void *)LLVMTypeOf(fn));
                    void *p[8];
                    unsigned ok=(unsigned)LLVMCountParamTypes(ty)==7;
                    if(ok) {
                        (void)LLVMGetParamTypes(ty,p);
                        ok=p[6]==(void *)LLVMFloatTypeInContext(c);
                    }
                    for(void *u=(void *)LLVMGetFirstUse(fn);ok && u;
                            u=(void *)LLVMGetNextUse(u)) {
                        void *call=(void *)LLVMGetUser(u);
                        if(!(void *)LLVMIsACallInst(call) ||
                           (unsigned)LLVMGetNumArgOperands(call)!=7) {ok=0;break;}
                    }
                    if(ok) {
                        (void)LLVMSetValueName(fn,"pvg_old_sample_texture_2d");
                        void *q[8]={p[0],p[1],p[2],p[3],p[4],p[5],p[6],
                            (void *)LLVMInt32TypeInContext(c)};
                        void *nt=(void *)LLVMFunctionType((void *)LLVMGetReturnType(ty),q,8,0);
                        void *nf=(void *)LLVMAddFunction(m,pvgSampleName,nt);
                        void *b=(void *)LLVMCreateBuilderInContext(c);
                        void *u;
                        while((u=(void *)LLVMGetFirstUse(fn))) {
                            void *call=(void *)LLVMGetUser(u), *a[8];
                            for(unsigned i=0;i<7;i++) a[i]=(void *)LLVMGetOperand(call,i);
                            a[7]=(void *)LLVMConstInt(q[7],0,0);
                            (void)LLVMPositionBuilderBefore(b,call);
                            void *nc=(void *)LLVMBuildCall(b,nf,a,8,"pvg_sample");
                            (void)LLVMReplaceAllUsesWith(call,nc);
                            (void)LLVMInstructionEraseFromParent(call);
                        }
                        (void)LLVMDisposeBuilder(b);
                        (void)LLVMDeleteFunction(fn);
                        (int)printf("PVG normalized bounded 2D sample calls\\n");
                    } else (int)printf("PVG refused unknown 2D sample ABI\\n");
                }
            })''' % module)
            print('texture intrinsic research mapping: %s' % result.GetError())
        if os.environ.get('PVG_NATIVE_SAMPLER_METADATA'):
            # Native ShLink11 emits the old sampler binding schema. Normalize
            # before the donor derives resource indices/names from metadata.
            result = frame.EvaluateExpression('''({
                void *m=(void *)0x%x, *c=(void *)LLVMGetModuleContext(m);
                void *t=(void *)LLVMInt32TypeInContext(c);
                void *image=(void *)dlopen("/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/3902/Libraries/libLLVM.dylib",2);
                void (*replace)(void*,unsigned,void*)=(void(*)(void*,unsigned,void*))
                    dlsym(image,"_ZN4llvm6MDNode18replaceOperandWithEjPNS_8MetadataE");
                const char *pvgSamplerStageNames[2]={"air.vertex","air.fragment"};
                for(unsigned s=0;replace && s<2;s++) {
                    if((unsigned)LLVMGetNamedMetadataNumOperands(m,pvgSamplerStageNames[s])!=1) continue;
                    void *e; (void)LLVMGetNamedMetadataOperands(m,pvgSamplerStageNames[s],&e);
                    if((unsigned)LLVMGetMDNodeNumOperands(e)!=3) continue;
                    void *v[3]; (void)LLVMGetMDNodeOperands(e,v);
                    unsigned n=(unsigned)LLVMGetMDNodeNumOperands(v[2]);
                    if(n>32) continue;
                    void *args[32]; (void)LLVMGetMDNodeOperands(v[2],args);
                    for(unsigned a=0;a<n;a++) {
                        if((void *)getenv("PVG_NATIVE_POSITION_METADATA") &&
                           (unsigned)LLVMGetMDNodeNumOperands(args[a])==2) {
                            void *f[2]; (void)LLVMGetMDNodeOperands(args[a],f);
                            unsigned len=0;
                            const char *kind=(const char *)LLVMGetMDString(f[1],&len);
                            if(kind && len==12 && !(int)memcmp(kind,"air.position",12)) {
                                void *out[4]={f[0],f[1],
                                    (void *)LLVMMDStringInContext(c,"air.arg_type_name",17),
                                    (void *)LLVMMDStringInContext(c,"float4",6)};
                                void *node=(void *)LLVMMDNodeInContext(c,out,4);
                                replace((void *)LLVMValueAsMetadata(v[2]),a,
                                        (void *)LLVMValueAsMetadata(node));
                                (int)printf("PVG position type normalized\\n");
                            }
                        }
                        if((unsigned)LLVMGetMDNodeNumOperands(args[a])!=4) continue;
                        void *f[4]; (void)LLVMGetMDNodeOperands(args[a],f);
                        unsigned len=0;
                        const char *kind=(const char *)LLVMGetMDString(f[1],&len);
                        if(!kind || len!=11 || (int)memcmp(kind,"air.sampler",11)) continue;
                        const char *binding=(const char *)LLVMGetMDString(f[2],&len);
                        if(!binding || len!=11 || (int)memcmp(binding,"air.binding",11)) continue;
                        void *out[5]={f[0],f[1],
                            (void *)LLVMMDStringInContext(c,"air.location_index",18),
                            f[3],(void *)LLVMConstInt(t,1,0)};
                        void *node=(void *)LLVMMDNodeInContext(c,out,5);
                        replace((void *)LLVMValueAsMetadata(v[2]),a,
                                (void *)LLVMValueAsMetadata(node));
                        (int)printf("PVG sampler schema stage=%%u argument=%%u\\n",s,a);
                    }
                }
            })''' % module)
            print('sampler schema research mapping: %s' % result.GetError())
        if os.environ.get('PVG_NATIVE_DEFAULT_UNIFORM'):
            # Bounded hypothesis for this probe: native default-uniform slot
            # 12 vs matched frontend slot0. Not a general UBO remapping policy.
            result = frame.EvaluateExpression('''({
                void *m=(void *)0x%x, *c=(void *)LLVMGetModuleContext(m);
                void *t=(void *)LLVMInt32TypeInContext(c);
                const char *pvgStageNames[2]={"air.vertex","air.fragment"};
                void *image=(void *)dlopen("/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/3902/Libraries/libLLVM.dylib",2);
                void (*replace)(void*,unsigned,void*)=(void(*)(void*,unsigned,void*))
                    dlsym(image,"_ZN4llvm6MDNode18replaceOperandWithEjPNS_8MetadataE");
                for(unsigned s=0;s<2;s++) {
                    unsigned n=(unsigned)LLVMGetNamedMetadataNumOperands(m,pvgStageNames[s]);
                    (int)printf("PVG stage=%%u entries=%%u\\n",s,n);
                    if(n!=1) continue;
                    void *e; (void)LLVMGetNamedMetadataOperands(m,pvgStageNames[s],&e);
                    if((unsigned)LLVMGetMDNodeNumOperands(e)!=3) continue;
                    void *v[3]; (void)LLVMGetMDNodeOperands(e,v);
                    unsigned count=(unsigned)LLVMGetMDNodeNumOperands(v[2]);
                    if(count>32) continue;
                    void *args[32]; (void)LLVMGetMDNodeOperands(v[2],args);
                    for(unsigned a=0;a<count;a++) {
                        unsigned fieldsCount=(unsigned)LLVMGetMDNodeNumOperands(args[a]);
                        (int)printf("PVG arg=%%u fields=%%u\\n",a,fieldsCount);
                        if(fieldsCount!=8) continue;
                        void *fields[8]; (void)LLVMGetMDNodeOperands(args[a],fields);
                        unsigned len=0;
                        const char *kind=(const char *)LLVMGetMDString(fields[1],&len);
                        if(!kind || len!=10 || (int)memcmp(kind,"air.buffer",10)) continue;
                        if((unsigned long long)LLVMConstIntGetZExtValue(fields[3])!=12) continue;
                        (int)printf("PVG remap stage=%%u argument=%%u slot12 to0\\n",s,a);
                        void *zero=(void *)LLVMConstInt(t,0,0);
                        replace((void *)LLVMValueAsMetadata(args[a]),3,
                                (void *)LLVMValueAsMetadata(zero));
                    }
                }
            })''' % module)
            print('default uniform research mapping: %s' % result.GetError())
            frame.EvaluateExpression('(void)LLVMDumpModule((void *)0x%x)' % module)
    return False

def uniform_stop(frame, bp_loc, _):
    global uniform_trace_armed
    # Stop before upload/draw; these offsets are from the donor renderer,
    # not claims about the native frontend's state layout.
    process = frame.GetThread().GetProcess()
    error = lldb.SBError()
    ctx = frame.FindRegister('rdi').GetValueAsUnsigned()
    state = process.ReadPointerFromMemory(ctx + 0x88, error)
    pipeline = process.ReadPointerFromMemory(ctx + 0x8f40, error)
    print('UNIFORM STOP ctx=0x%x state=0x%x pipeline=0x%x' % (ctx,state,pipeline))
    if os.environ.get('PVG_NATIVE_TRACE_TEXTURES'):
        # beginRenderPass loads its descriptor from this donor-owned field.
        descriptor=process.ReadPointerFromMemory(ctx+0x88f8,error)
        if error.Success() and descriptor: render_target_dump(frame,descriptor)
    import binascii
    for program in native_programs:
        # Native gleLinkProgram stores ShGetActiveNonbindableUniformsSize
        # at +0x1a80, allocation at +0x1a78 (verified static producer).
        vectors=process.ReadUnsignedFromMemory(program+0x1a80,4,error)
        size=vectors*16
        data=process.ReadPointerFromMemory(program+0x1a78,error)
        print('native program=0x%x uniform_vectors=%d bytes=%d data=0x%x' % (program,vectors,size,data))
        if data and 0<size<=256:
            raw=process.ReadMemory(data,size,error)
            if error.Success(): print('native uniform bytes %s' % binascii.hexlify(raw))
    for offset in (0x8418,0x8438,0x8440,0x8460):
        program=process.ReadPointerFromMemory(ctx+offset,error)
        if program:
            desc=process.ReadPointerFromMemory(program+0x18,error)
            raw=process.ReadMemory(desc,0xa0,error)
            if error.Success():
                print('program slot=0x%x desc=0x%x %s' % (offset,desc,binascii.hexlify(raw)))
    for address, length in ((state+0x4880,0x160),(pipeline+0x500,0x60),
                            (ctx+0x8ed0,0x80)):
        raw = process.ReadMemory(address,length,error)
        if error.Success():
            print('memory 0x%x %s' % (address,binascii.hexlify(raw)))
        else:
            print(error)
    if os.environ.get('PVG_NATIVE_TRACE_BINDINGS'):
        if not draw_breakpoint or not draw_breakpoint.GetNumLocations():
            print('refusing binding trace: no resolved pre-draw breakpoint')
            return True
        uniform_trace_armed=True
        return False
    return True

def binding_trace(frame, bp_loc, _):
    name=binding_selectors.get(bp_loc.GetBreakpoint().GetID(),frame.GetFunctionName() or '')
    texture_trace=os.environ.get('PVG_NATIVE_TRACE_TEXTURES') and 'Texture' in name
    if not uniform_trace_armed and not texture_trace and not os.environ.get('PVG_NATIVE_TRACE_ALL_BINDINGS'): return False
    values=[frame.FindRegister(r).GetValueAsUnsigned() for r in ('rdx','rcx','r8')]
    print('METAL BIND %s resource/bytes=0x%x offset/length=%d index=%d' %
          ((name,)+tuple(values)))
    if 'Buffers:offsets:withRange:' in name:
        import binascii
        error=lldb.SBError()
        process=frame.GetThread().GetProcess()
        count=frame.FindRegister('r9').GetValueAsUnsigned()
        if count>32: return True
        for i in range(count):
            buffer=process.ReadPointerFromMemory(values[0]+8*i,error)
            offset=process.ReadUnsignedFromMemory(values[1]+8*i,8,error)
            print('METAL BUFFER ARRAY index=%d object=0x%x offset=%d' % (values[2]+i,buffer,offset))
            if buffer:
                result=frame.EvaluateExpression('(void *)[(id)0x%x contents]' % buffer)
                pointer=result.GetValueAsUnsigned()
                if pointer and result.GetError().Success():
                    data=process.ReadMemory(pointer+offset,16,error)
                    if error.Success(): print('METAL BUFFER HEAD '+str(binascii.hexlify(data)))
    if texture_trace:
        error=lldb.SBError()
        textures=[values[0]]
        if 'Textures:' in name:
            if values[2]>32: return True
            textures=[frame.GetThread().GetProcess().ReadPointerFromMemory(
                values[0]+8*i,error) for i in range(values[2])]
        for texture in textures:
            if texture:
                result=frame.EvaluateExpression('''(int)printf(
                    "PVG texture %%p %%lux%%lu\\n",(void *)0x%x,
                    (unsigned long)[(id)0x%x width],
                    (unsigned long)[(id)0x%x height])''' % (texture,texture,texture))
                if result.GetError().Fail(): print(result.GetError())
    if 'Bytes:' in name and values[1]<=256:
        import binascii
        error=lldb.SBError()
        data=frame.GetThread().GetProcess().ReadMemory(values[0],values[1],error)
        if error.Success(): print('METAL BYTES '+str(binascii.hexlify(data)))
    if 'Buffer:offset:' in name and values[0] and os.environ.get('PVG_NATIVE_TRACE_ALL_BINDINGS'):
        result=frame.EvaluateExpression('(void *)[(id)0x%x contents]' % values[0])
        pointer=result.GetValueAsUnsigned()
        if pointer and result.GetError().Success():
            import binascii
            error=lldb.SBError()
            data=frame.GetThread().GetProcess().ReadMemory(pointer+values[1],16,error)
            if error.Success(): print('METAL BUFFER HEAD '+str(binascii.hexlify(data)))
    return False

def draw_stop(frame, bp_loc, _):
    if uniform_trace_armed:
        print('STOP BEFORE METAL DRAW '+str(frame.GetFunctionName()))
        if 'drawIndexedPrimitives:' in (frame.GetFunctionName() or ''):
            import binascii
            process=frame.GetThread().GetProcess();error=lldb.SBError()
            buffer=frame.FindRegister('r9').GetValueAsUnsigned()
            rsp=frame.FindRegister('rsp').GetValueAsUnsigned()
            stack=process.ReadMemory(rsp,40,error)
            print('DRAW STACK '+str(binascii.hexlify(stack)))
            result=frame.EvaluateExpression('(void *)[(id)0x%x contents]' % buffer)
            pointer=result.GetValueAsUnsigned()
            if pointer and result.GetError().Success():
                data=process.ReadMemory(pointer,32,error)
                if error.Success(): print('INDEX BUFFER HEAD '+str(binascii.hexlify(data)))
        return True
    return False

def render_target_trace(frame, bp_loc, _):
    descriptor=frame.FindRegister('rdx').GetValueAsUnsigned()
    render_target_dump(frame,descriptor)
    return False

def render_target_dump(frame,descriptor):
    result=frame.EvaluateExpression('''({
        id pvgAttachments=(id)[(id)0x%x colorAttachments];
        id pvgAttachment=(id)[pvgAttachments objectAtIndexedSubscript:0];
        id pvgTexture=(id)[pvgAttachment texture];
        (int)printf("PVG render target %%p %%lux%%lu load=%%lu store=%%lu\\n",
            (void *)pvgTexture,(unsigned long)[pvgTexture width],
            (unsigned long)[pvgTexture height],(unsigned long)[pvgAttachment loadAction],
            (unsigned long)[pvgAttachment storeAction]);
    })''' % descriptor)
    print('render target trace: %s' % result.GetError())

def pipeline_descriptor_enter(frame,bp_loc,_):
    target=frame.GetThread().GetProcess().GetTarget()
    bp=target.BreakpointCreateByAddress(frame.GetThread().GetFrameAtIndex(1).GetPC())
    bp.SetThreadID(frame.GetThread().GetThreadID())
    bp.SetOneShot(True)
    bp.SetScriptCallbackFunction(__name__+'.pipeline_descriptor_return')
    return False

def vertex_array_trace(frame,bp_loc,_):
    import binascii
    process=frame.GetThread().GetProcess();error=lldb.SBError()
    ctx=frame.FindRegister('rdi').GetValueAsUnsigned()
    state=process.ReadMemory(ctx+0x3cc,0x24,error)
    if error.Success(): print('VERTEX PATH %s state=%s' % (frame.GetFunctionName(),binascii.hexlify(state)))
    policy=process.ReadPointerFromMemory(ctx+0xa0,error)
    raw=process.ReadMemory(policy,0x280,error)
    if error.Success(): print('PRIMITIVE POLICY '+str(binascii.hexlify(raw)))
    program=frame.FindRegister('rsi').GetValueAsUnsigned()
    desc=process.ReadPointerFromMemory(program+0x18,error)
    inputs=process.ReadPointerFromMemory(desc+0x68,error)
    count=process.ReadUnsignedFromMemory(inputs,4,error)
    if not error.Success() or count>32: return True
    raw=process.ReadMemory(inputs,8+8*count,error)
    if error.Success(): print('VERTEX INPUTS '+str(binascii.hexlify(raw)))
    return not error.Success()

def encoder_enter(frame,bp_loc,_):
    target=frame.GetThread().GetProcess().GetTarget()
    bp=target.BreakpointCreateByAddress(frame.GetThread().GetFrameAtIndex(1).GetPC())
    bp.SetThreadID(frame.GetThread().GetThreadID());bp.SetOneShot(True)
    encoder_returns[bp.GetID()]=frame.FindRegister('rdi').GetValueAsUnsigned()
    bp.SetScriptCallbackFunction(__name__+'.encoder_return')
    return False

def encoder_return(frame,bp_loc,_):
    process=frame.GetThread().GetProcess();error=lldb.SBError()
    bp_loc.GetBreakpoint().SetEnabled(False)
    ctx=encoder_returns.pop(bp_loc.GetBreakpoint().GetID(),None)
    if ctx is None: return False
    encoder=process.ReadPointerFromMemory(ctx+0x40,error)
    if not error.Success(): return True
    if not encoder: return False
    for selector in ('setVertexBuffer:offset:atIndex:','setVertexBytes:length:atIndex:',
                     'setFragmentBuffer:offset:atIndex:','setFragmentBytes:length:atIndex:'):
        result=frame.EvaluateExpression('(void *)[(id)0x%x methodForSelector:(SEL)sel_registerName("%s")]' % (encoder,selector))
        address=result.GetValueAsUnsigned()
        if result.GetError().Fail() or not address: return True
        if address not in binding_addresses:
            bp=process.GetTarget().BreakpointCreateByAddress(address)
            binding_selectors[bp.GetID()]=selector;binding_addresses.add(address)
            bp.SetScriptCallbackFunction(__name__+'.binding_trace')
            print('ENCODER IMP %s 0x%x' % (selector,address))
    return False

def raster_trace(frame,bp_loc,_):
    import binascii
    process=frame.GetThread().GetProcess()
    error=lldb.SBError()
    rsp=frame.FindRegister('rsp').GetValueAsUnsigned()
    stack=process.ReadMemory(rsp,72,error)
    print('RASTER %s pc=0x%x rdx=%s stack=%s' % (
        frame.GetFunctionName(),frame.GetPC(),frame.FindRegister('rdx').GetValue(),
        binascii.hexlify(stack) if error.Success() else str(error)))
    name=frame.GetFunctionName() or ''
    if 'Viewports:' in name or 'ScissorRects:' in name:
        count=frame.FindRegister('rcx').GetValueAsUnsigned()
        pointer=frame.FindRegister('rdx').GetValueAsUnsigned()
        if count>16: return True
        fmt='<6d' if 'Viewports:' in name else '<4Q'
        size=struct.calcsize(fmt)
        for i in range(count):
            data=process.ReadMemory(pointer+i*size,size,error)
            if error.Success(): print('RASTER array %d %s' % (i,struct.unpack(fmt,data)))
    return False

def pipeline_descriptor_return(frame,bp_loc,_):
    descriptor=frame.FindRegister('rax').GetValueAsUnsigned()
    if not descriptor: return False
    result=frame.EvaluateExpression('''({
        id pvgDescriptor=(id)0x%x;
        id pvgAttachments=(id)[pvgDescriptor colorAttachments];
        id pvgColor=(id)[pvgAttachments objectAtIndexedSubscript:0];
        (int)printf("PVG pipeline raster=%%d format=%%lu mask=%%lu blend=%%d samples=%%lu\\n",
            (int)[pvgDescriptor isRasterizationEnabled],
            (unsigned long)[pvgColor pixelFormat],(unsigned long)[pvgColor writeMask],
            (int)[pvgColor isBlendingEnabled],(unsigned long)[pvgDescriptor sampleCount]);
    })''' % descriptor)
    print('pipeline trace: %s' % result.GetError())
    return False

def prepare_uniform_records(frame, bp_loc, _):
    """Rejected draw hypothesis, retained ONLY for stopped-state inspection."""
    if not os.environ.get('PVG_NATIVE_STOP_UNIFORMS'):
        print('refusing record-adapter draw: binding and ownership are unverified')
        return True
    process=frame.GetThread().GetProcess()
    error=lldb.SBError()
    ctx=frame.FindRegister('rdi').GetValueAsUnsigned()
    state=process.ReadPointerFromMemory(ctx+0x88,error)
    objects=process.ReadPointerFromMemory(ctx+0x90,error)
    for stage in (0,4):
        glsl=process.ReadPointerFromMemory(objects+0x448+8*stage,error)
        if not glsl: continue  # No GLSL program: internal clear/fixed-function.
        table=process.ReadPointerFromMemory(glsl+0x20,error)
        obj=process.ReadPointerFromMemory(table+8*stage,error)
        if not obj: continue
        desc=process.ReadPointerFromMemory(obj+0x18,error)
        if desc in expanded_descriptions: continue
        matches=[p for p in native_programs if desc==p+0x4f0+stage*0x528]
        if len(matches)!=1:
            print('record adapter refused unmapped descriptor 0x%x' % desc)
            print('stage=%d native program deltas=%s' % (stage,[hex(desc-p) for p in native_programs]))
            if os.environ.get('PVG_NATIVE_STOP_UNIFORMS'):
                continue  # Clear can load a separate internal program.
            return True
        program=matches[0]
        count=process.ReadUnsignedFromMemory(program+0x1a80,4,error)
        data=process.ReadPointerFromMemory(program+0x1a78,error)
        if count>4096 or (count and not data): return True
        original=process.ReadMemory(desc,0x88,error)
        if not error.Success(): return True
        allocation=frame.EvaluateExpression('(void *)calloc(1,0x90)')
        extended=allocation.GetValueAsUnsigned()
        if not extended or allocation.GetError().Fail(): return True
        process.WriteMemory(extended,original+struct.pack('<II',count,0),error)
        if not error.Success(): return True
        process.WriteMemory(obj+0x18,struct.pack('<Q',extended),error)
        # Donor's GLSL default-uniform upload uses these native state fields.
        # Research only; final code must not overwrite unrelated frontend state.
        # Stages are vertex0, fragment4; donor fragment load is +0x496c,
        # verified at setRenderProgramUniforms+0x10fe, not geometry +0x494c.
        offset=0x492c+stage*0x10
        process.WriteMemory(state+offset,struct.pack('<IQ',count,data),error)
        if not error.Success(): return True
        expanded_descriptions[extended]=desc
        print('expanded stage=%d descriptor=0x%x vectors=%d data=0x%x' %
              (stage,extended,count,data))
    return False

def prepare_glsl_record(frame, bp_loc, _):
    # Isolated uniform-only experiment: preserve original frontend records
    # and data ownership. Final adapter needs full record/lifetime coverage.
    process=frame.GetThread().GetProcess()
    error=lldb.SBError()
    ctx=frame.FindRegister('rdi').GetValueAsUnsigned()
    objects=process.ReadPointerFromMemory(ctx+0x90,error)
    if os.environ.get('PVG_NATIVE_PIPELINE_SHADOW'):
        for stage in (0,4):
            glsl=process.ReadPointerFromMemory(objects+0x448+stage*8,error)
            if not glsl: continue
            table=process.ReadPointerFromMemory(glsl+0x20,error)
            pipeline=process.ReadPointerFromMemory(table+stage*8,error)
            if not pipeline: continue
            desc=process.ReadPointerFromMemory(pipeline+0x18,error)
            if desc in pipeline_shadows: continue
            matches=[p for p in native_programs if desc==p+0x4f0+stage*0x528]
            if len(matches)!=1:
                print('pipeline shadow refused unknown descriptor 0x%x' % desc)
                return True
            count=process.ReadUnsignedFromMemory(matches[0]+0x1a80,4,error)
            if not error.Success() or count>4096: return True
            original=process.ReadMemory(desc,0x100,error)
            if not error.Success(): return True
            allocation=frame.EvaluateExpression('(void *)calloc(1,0x108)')
            shadow=allocation.GetValueAsUnsigned()
            if not shadow or allocation.GetError().Fail(): return True
            # The matched donor adds uniform count/reserved at +0x88.
            # Preserve trailing fields (native codegen+0x8c -> donor+0x94).
            # Unlike the rejected experiment, NEVER change frontend state.
            process.WriteMemory(shadow,original[:0x88]+struct.pack('<II',count,0)+original[0x88:],error)
            if not error.Success(): return True
            process.WriteMemory(pipeline+0x18,struct.pack('<Q',shadow),error)
            if not error.Success(): return True
            pipeline_shadows[shadow]=desc
            print('PIPELINE shadow stage=%d count=%d original=0x%x shadow=0x%x' % (stage,count,desc,shadow))
    for stage in (0,4):
        obj=process.ReadPointerFromMemory(objects+0x448+stage*8,error)
        if not obj: continue
        desc=process.ReadPointerFromMemory(obj+0x18,error)
        if desc in glsl_descriptions: continue
        if desc not in [p+0x1a68 for p in native_programs]:
            print('refusing unknown GLSL descriptor 0x%x' % desc)
            return True
        original=process.ReadMemory(desc,0x100,error)
        if not error.Success(): return True
        allocation=frame.EvaluateExpression('(void *)calloc(1,0x110)')
        extended=allocation.GetValueAsUnsigned()
        if not extended or allocation.GetError().Fail(): return True
        # Native uniform pointer/count +0x10/+0x18 become +0x20/+0x28.
        # Added donor fields are zero in matched uniform-only reference.
        process.WriteMemory(extended,original[:0x10]+b'\0'*16+original[0x10:],error)
        if not error.Success(): return True
        process.WriteMemory(obj+0x18,struct.pack('<Q',extended),error)
        if not error.Success(): return True
        glsl_descriptions[extended]=desc
        print('GLSL shadow stage=%d original=0x%x expanded=0x%x' % (stage,desc,extended))
    return False

def __lldb_init_module(debugger, _):
    global draw_breakpoint
    target = debugger.GetSelectedTarget()
    if os.environ.get('PVG_NATIVE_STOP_COMPILER_ERROR'):
        target.BreakpointCreateByName('exit')
        target.BreakpointCreateByName('abort')
    if os.environ.get('PVG_NATIVE_CONFIG_AUTO'):
        pass # Normal donor dependency installs its own config hooks.
    elif os.environ.get('PVG_NATIVE_CONFIG_HOOKS'):
        bp=target.BreakpointCreateByName('gldCreateContext')
        bp.SetScriptCallbackFunction(__name__+'.install_config_hooks')
    elif os.environ.get('PVG_NATIVE_CONFIG_HELPER'):
        bp=target.BreakpointCreateByName('GLDContextRec::dealloc()')
        bp.SetScriptCallbackFunction(__name__+'.config_dealloc_enter')
    if os.environ.get('PVG_NATIVE_DUMP_LOWERED'):
        bp=target.BreakpointCreateByName('MTLCompilerBuildRequestWithOptions')
        bp.SetScriptCallbackFunction(__name__+'.lowered_ir')
    if os.environ.get('PVG_NATIVE_VERTEX_KEY'):
        bp=target.BreakpointCreateByName('GLDContextRec::buildVertexArrayDescriptor(GLDPipelineProgramRec*, GLDVertexArrayRec*)')
        bp.SetScriptCallbackFunction(__name__+'.vertex_array_trace')
        bp=target.BreakpointCreateByName('GLDContextRec::buildPrimitiveBufferVertexArrayDescriptor(GLDPipelineProgramRec*)')
        bp.SetScriptCallbackFunction(__name__+'.vertex_array_trace')
    if not os.environ.get('PVG_GL_OBSERVE_ONLY'):
        if not (os.environ.get('PVG_NATIVE_CONFIG_HOOKS') or os.environ.get('PVG_NATIVE_CONFIG_AUTO')):
            bp = target.BreakpointCreateByName(
                'GLDContextRec::populateConfigData(GLDContextConfigRec*)')
            bp.SetScriptCallbackFunction(__name__ + '.entering')
        if not os.environ.get('PVG_NATIVE_CODEGEN_AUTO'):
            bp = target.BreakpointCreateByName('ShLink')
            bp.SetScriptCallbackFunction(__name__ + '.linking')
        bp = target.BreakpointCreateByName('gleLinkProgram')
        bp.SetScriptCallbackFunction(__name__ + '.engine_link')
    if os.environ.get('PVG_NATIVE_DUMP_IR') and not os.environ.get('PVG_NATIVE_METADATA_AUTO'):
        bp = target.BreakpointCreateByName('GLDShader::setupGLDEntryPoint()')
        bp.SetScriptCallbackFunction(__name__ + '.entry_dump')
        bp = target.BreakpointCreateByName('GLDShader::queryMetadataBeforeLink()')
        bp.SetScriptCallbackFunction(__name__ + '.metadata')
    if os.environ.get('PVG_NATIVE_STOP_UNIFORMS'):
        bp = target.BreakpointCreateByName('GLDContextRec::setRenderProgramUniforms()')
        bp.SetScriptCallbackFunction(__name__ + '.uniform_stop')
    if os.environ.get('PVG_NATIVE_TRACE_BINDINGS'):
        bp=target.BreakpointCreateByRegex('set(Vertex|Fragment)(Buffer:offset:atIndex:|Buffers:offsets:withRange:|Bytes:length:atIndex:)')
        bp.SetScriptCallbackFunction(__name__+'.binding_trace')
        bp=target.BreakpointCreateByRegex('draw(Indexed)?Primitives:')
        bp.SetScriptCallbackFunction(__name__+'.draw_stop')
        draw_breakpoint=bp
    if os.environ.get('PVG_NATIVE_TRACE_ALL_BINDINGS'):
        bp=target.BreakpointCreateByName('GLDContextRec::beginRenderPass()')
        bp.SetScriptCallbackFunction(__name__+'.encoder_enter')
    if os.environ.get('PVG_NATIVE_TRACE_TEXTURES'):
        bp=target.BreakpointCreateByRegex('set(Vertex|Fragment)Textures?:')
        bp.SetScriptCallbackFunction(__name__+'.binding_trace')
        bp=target.BreakpointCreateByRegex('renderCommandEncoderWithDescriptor:')
        bp.SetScriptCallbackFunction(__name__+'.render_target_trace')
        bp=target.BreakpointCreateByName('GLRRenderPipelineKey::copyVSFSRenderDescriptor(GLDPipelineProgramRec**) const')
        bp.SetScriptCallbackFunction(__name__+'.pipeline_descriptor_enter')
        bp=target.BreakpointCreateByRegex('set(Viewports?|ScissorRects?|CullMode|FrontFacingWinding):')
        bp.SetScriptCallbackFunction(__name__+'.raster_trace')
    if os.environ.get('PVG_NATIVE_RECORD_ADAPTER'):
        bp=target.BreakpointCreateByName('GLDContextRec::loadCurrentPipelinePrograms(unsigned int*)')
        bp.SetScriptCallbackFunction(__name__+'.prepare_uniform_records')
    if os.environ.get('PVG_NATIVE_GLSL_ADAPTER') and not os.environ.get('PVG_NATIVE_PROGRAM_AUTO'):
        # Texture state is consumed before loadCurrentPipelinePrograms.
        bp=target.BreakpointCreateByName('gldUpdateDispatch')
        bp.SetScriptCallbackFunction(__name__+'.prepare_glsl_record')
