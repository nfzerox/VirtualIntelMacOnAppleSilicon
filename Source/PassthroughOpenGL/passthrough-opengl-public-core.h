/* Native libGL, not the private transport dylib, is the application's API.
 * Lion omits e.g. TexStorage and transform-feedback object exports even when
 * the host and our wire implement them. Never advertise an unreachable ABI.
 * This cold capability check is independent of the host GPU's support mask.
 */
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1070 && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1090
#include <dlfcn.h>
static pthread_once_t dg_public_core_once=PTHREAD_ONCE_INIT;
static uint64_t dg_public_core_mask;
static void DGInspectPublicCoreAPI(void)
{
    const struct {uint64_t bit;const char *symbol;} required[]={
#define REQUIRED(family,symbol) {DG_CORE_EXT_##family,#symbol}
        REQUIRED(TEXTURE_STORAGE,glTexStorage1D),REQUIRED(TEXTURE_STORAGE,glTexStorage2D),REQUIRED(TEXTURE_STORAGE,glTexStorage3D),
        REQUIRED(TEXTURE_BUFFER_RGB32,glTexBuffer),REQUIRED(TEXTURE_CUBE_MAP_ARRAY,glTexImage3D),
        REQUIRED(SHADER_SUBROUTINE,glGetSubroutineUniformLocation),REQUIRED(SHADER_SUBROUTINE,glGetSubroutineIndex),
        REQUIRED(SHADER_SUBROUTINE,glUniformSubroutinesuiv),REQUIRED(SHADER_SUBROUTINE,glGetProgramStageiv),
        REQUIRED(ES2_COMPATIBILITY,glReleaseShaderCompiler),REQUIRED(ES2_COMPATIBILITY,glShaderBinary),
        REQUIRED(ES2_COMPATIBILITY,glGetShaderPrecisionFormat),REQUIRED(ES2_COMPATIBILITY,glDepthRangef),REQUIRED(ES2_COMPATIBILITY,glClearDepthf),
        REQUIRED(BLEND_FUNC_EXTENDED,glBindFragDataLocationIndexed),REQUIRED(BLEND_FUNC_EXTENDED,glGetFragDataIndex),
        REQUIRED(DRAW_BUFFERS_BLEND,glBlendEquationi),REQUIRED(DRAW_BUFFERS_BLEND,glBlendEquationSeparatei),
        REQUIRED(DRAW_BUFFERS_BLEND,glBlendFunci),REQUIRED(DRAW_BUFFERS_BLEND,glBlendFuncSeparatei),
        REQUIRED(SAMPLER_OBJECTS,glGenSamplers),REQUIRED(SAMPLER_OBJECTS,glBindSampler),REQUIRED(SAMPLER_OBJECTS,glGetSamplerParameteriv),
        REQUIRED(SAMPLER_OBJECTS,glDeleteSamplers),REQUIRED(SAMPLER_OBJECTS,glIsSampler),
        REQUIRED(SAMPLER_OBJECTS,glSamplerParameteri),REQUIRED(SAMPLER_OBJECTS,glSamplerParameterf),
        REQUIRED(SAMPLER_OBJECTS,glSamplerParameteriv),REQUIRED(SAMPLER_OBJECTS,glSamplerParameterfv),
        REQUIRED(SAMPLER_OBJECTS,glSamplerParameterIiv),REQUIRED(SAMPLER_OBJECTS,glSamplerParameterIuiv),
        REQUIRED(SAMPLER_OBJECTS,glGetSamplerParameterfv),REQUIRED(SAMPLER_OBJECTS,glGetSamplerParameterIiv),REQUIRED(SAMPLER_OBJECTS,glGetSamplerParameterIuiv),
        REQUIRED(SEPARATE_SHADER_OBJECTS,glGenProgramPipelines),REQUIRED(SEPARATE_SHADER_OBJECTS,glUseProgramStages),
        REQUIRED(SEPARATE_SHADER_OBJECTS,glProgramUniform1f),REQUIRED(SEPARATE_SHADER_OBJECTS,glGetProgramPipelineiv),
        REQUIRED(TESSELLATION_SHADER,glPatchParameteri),REQUIRED(TESSELLATION_SHADER,glPatchParameterfv),
        REQUIRED(TRANSFORM_FEEDBACK2,glGenTransformFeedbacks),REQUIRED(TRANSFORM_FEEDBACK2,glBindTransformFeedback),
        REQUIRED(TRANSFORM_FEEDBACK2,glDeleteTransformFeedbacks),REQUIRED(TRANSFORM_FEEDBACK2,glDrawTransformFeedback),
        REQUIRED(TRANSFORM_FEEDBACK3,glDrawTransformFeedbackStream),REQUIRED(TRANSFORM_FEEDBACK3,glBeginQueryIndexed),REQUIRED(TRANSFORM_FEEDBACK3,glGetQueryIndexediv),
        REQUIRED(VIEWPORT_ARRAY,glViewportArrayv),REQUIRED(VIEWPORT_ARRAY,glScissorArrayv),REQUIRED(VIEWPORT_ARRAY,glDepthRangeArrayv),
        REQUIRED(TIMER_QUERY,glQueryCounter),REQUIRED(TIMER_QUERY,glGetQueryObjecti64v),REQUIRED(TIMER_QUERY,glGetQueryObjectui64v),
        REQUIRED(INTERNAL_FORMAT,glGetInternalformativ),
        REQUIRED(DRAW_INDIRECT,glDrawArraysIndirect),REQUIRED(DRAW_INDIRECT,glDrawElementsIndirect),
        REQUIRED(FP64_VERTEX,glVertexAttribLPointer),REQUIRED(FP64_VERTEX,glVertexAttribL4dv),REQUIRED(FP64_VERTEX,glGetVertexAttribLdv),
        REQUIRED(DEBUG_LABEL,glLabelObjectEXT),REQUIRED(DEBUG_LABEL,glGetObjectLabelEXT),
        REQUIRED(SHADER_INCLUDE,glNamedStringARB),REQUIRED(SHADER_INCLUDE,glCompileShaderIncludeARB),REQUIRED(SHADER_INCLUDE,glGetNamedStringARB),
        REQUIRED(OBJECT_PURGEABLE,glObjectPurgeableAPPLE),REQUIRED(OBJECT_PURGEABLE,glObjectUnpurgeableAPPLE),REQUIRED(OBJECT_PURGEABLE,glGetObjectParameterivAPPLE),
        REQUIRED(GPU_SHADER_FP64,glUniform1d),REQUIRED(GPU_SHADER_FP64,glUniformMatrix4dv),REQUIRED(GPU_SHADER_FP64,glGetUniformdv)
#undef REQUIRED
    };
    void *library=dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/A/Libraries/libGL.dylib",RTLD_LAZY|RTLD_LOCAL);
    if(!library)return;
    uint64_t mask=DG_CORE_EXT_KNOWN;
    for(unsigned i=0;i<sizeof(required)/sizeof(*required);++i)
        if((mask&required[i].bit)&&!dlsym(library,required[i].symbol))mask&=~required[i].bit;
    /* sRGB decode has texture state even without ARB_sampler_objects.
     * Missing public sampler exports only suppress that optional family. */
    dg_public_core_mask=mask;dlclose(library);
}
static uint64_t DGPublicCoreMask(void)
{pthread_once(&dg_public_core_once,DGInspectPublicCoreAPI);return dg_public_core_mask;}
#else
static uint64_t DGPublicCoreMask(void){return DG_CORE_EXT_KNOWN;}
#endif
static int DGPublicCoreVersion(struct DGContext *c,GLenum key,GLint *value)
{
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1070 && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1080
    /* Lion's public API is GL3.2; a native GL4 backend cannot add missing
     * exports to Apple's libGL. Preserve the genuine legacy context version. */
    if(c && c->profile!=0x1000 && (key==0x821b || key==0x821c)){
        *value=key==0x821b?3:2;return 1;
    }
#else
    (void)c;(void)key;(void)value;
#endif
    return 0;
}
