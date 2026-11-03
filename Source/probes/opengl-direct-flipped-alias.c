/* Exercise the production executor's opposite-row view and its lifetime. */
#include <OpenGL/gl.h>
static unsigned producer_flushes;
static void ProbeProducerFlush(void){++producer_flushes;glFlush();}
#define glFlush ProbeProducerFlush
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#undef glFlush
#include <assert.h>
static GLuint ProbeShader(GLenum type,const char *source)
{
    GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
    GLint compiled=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&compiled);assert(compiled);return shader;
}
static void ProbeSamplerHazards(struct DGHostContext *c,GLuint mirror)
{
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,mirror);
    DGRecordAliasBinding(c,GL_TEXTURE_RECTANGLE_ARB);glActiveTexture(GL_TEXTURE0);
    GLuint vertex=ProbeShader(GL_VERTEX_SHADER,"void main(){gl_Position=ftransform();}");
    GLuint fragment=ProbeShader(GL_FRAGMENT_SHADER,
        "#extension GL_ARB_texture_rectangle : enable\n"
        "uniform sampler2DRect image;void main(){gl_FragColor=texture2DRect(image,vec2(1.));}");
    GLuint program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);
    DGHostLinkProgram(c,program);glUseProgram(program);
    // Detaching a shader after successful link does not remove its executable.
    glDetachShader(program,fragment);
    GLint location=glGetUniformLocation(program,"image");assert(location>=0);
    // The direct alias is bound on unit0, but the shader reads unit1.
    glUniform1i(location,1);c->drawable->alias_dirty=1;
    unsigned before=producer_flushes;DGPrepareDrawableAliases(c,0);
    assert(producer_flushes==before && c->drawable->alias_dirty);
    // Observe actual uniform changes, without setter-result assumptions.
    glUniform1i(location,0);DGPrepareDrawableAliases(c,0);assert(producer_flushes==before+1);
    // A vertex-only executable still has fixed-function fragment texturing.
    DGForgetUniformShapes(c,program);DGHostLinkProgram(c,program);
    glUseProgram(program);c->drawable->alias_dirty=1;before=producer_flushes;
    DGPrepareDrawableAliases(c,0);assert(producer_flushes==before+1);
    // Reusing the SAME program name after relink must rebuild its layout.
    const char *array_source="#extension GL_ARB_texture_rectangle : enable\n"
        "uniform sampler2DRect images[2];void main(){gl_FragColor=texture2DRect(images[0],vec2(1.))+texture2DRect(images[1],vec2(2.));}";
    glShaderSource(fragment,1,&array_source,NULL);glCompileShader(fragment);glAttachShader(program,fragment);
    DGForgetUniformShapes(c,program);DGHostLinkProgram(c,program);glUseProgram(program);
    GLint units[]={1,1};location=glGetUniformLocation(program,"images[0]");assert(location>=0);
    glUniform1iv(location,2,units);c->drawable->alias_dirty=1;before=producer_flushes;
    DGPrepareDrawableAliases(c,0);assert(producer_flushes==before);
    GLint second=glGetUniformLocation(program,"images[1]");assert(second>=0);
    glUniform1i(second,0);DGPrepareDrawableAliases(c,0);assert(producer_flushes==before+1);
    // Unknown list bindings may select another executable or sampler.
    glUniform1i(second,1);c->drawable->alias_dirty=1;before=producer_flushes;
    DGPrepareDrawableAliases(c,1);assert(producer_flushes==before+1);
    // A failed relink leaves the old executable current, but reflection now
    // has failed-link semantics. Keep ordering rather than treating it empty.
    const char *bad="this is not GLSL";glShaderSource(fragment,1,&bad,NULL);glCompileShader(fragment);
    DGForgetUniformShapes(c,program);DGHostLinkProgram(c,program);
    GLint linked=1;glGetProgramiv(program,GL_LINK_STATUS,&linked);assert(!linked);
    c->drawable->alias_dirty=1;before=producer_flushes;
    DGPrepareDrawableAliases(c,0);assert(producer_flushes==before+1);
    glUseProgram(0);DGForgetUniformShapes(c,program);glDeleteProgram(program);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
    DGRecordAliasBinding(c,GL_TEXTURE_RECTANGLE_ARB);glActiveTexture(GL_TEXTURE0);
    glDeleteShader(fragment);glDeleteShader(vertex);assert(!glGetError());
}
int main(int argc,char **argv)
{
    int back=argc==2 && !strcmp(argv[1],"--back");
    if(argc!=1 && !back)return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;
    struct DGHostContext c={.profile=kCGLOGLPVersion_Legacy,.draw_mode=GL_FRONT,.read_mode=GL_FRONT};
    c.double_buffer=back;if(back)c.draw_mode=c.read_mode=GL_BACK;
    assert(!CGLChoosePixelFormat(attrs,&pf,&count) && pf && !CGLCreateContext(pf,NULL,&c.cgl));
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(c.cgl);
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    assert(glGetString(GL_RENDERER) && DGCreateAliases(&c,NULL));
    dg_storage_reuse=1;assert(DGSelectDrawable(&c,1,64,48));
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,c.framebuffer);glDrawBuffer(back?GL_COLOR_ATTACHMENT1_EXT:GL_COLOR_ATTACHMENT0_EXT);
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(0,24,64,24);glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
    DGMarkDrawableDamage(&c);glEnable(GL_FRAMEBUFFER_SRGB_EXT);
    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    struct DGMessage m={.op=DG_BIND_DRAWABLE_TEXTURE,.args={GL_RGBA,64,48,back?3:1}};
    struct DGReply reply={0};assert(!DGBindDrawableTexture(&c,&m,&reply) && !reply.error);
    GLint read=0,draw=0,bound=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&bound);
    assert(read==(GLint)c.framebuffer && draw==read && bound==(GLint)texture);
    assert(glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT));
    struct DGAlias *alias=c.alias_group->aliases;
    assert(alias && alias->producer_storage==(back?c.back_storage:c.color_storage) && c.alias_group->owned_surface_count==1);
    IOSurfaceRef mirror=alias->owned_storage;GLuint source=alias->source_storage;
    assert(!DGBindDrawableTexture(&c,&m,&reply) && !reply.error && alias->owned_storage==mirror);
    unsigned before=producer_flushes;DGPrepareDrawableAliases(&c,0);
    assert(producer_flushes==before); // same-texture GPU blit, no IOSurface reimport
    assert(!c.alias_bindings_dirty && c.rectangle_bindings[0]==texture && c.sampled_textures[0]==texture);
    // An invalid native bind must preserve both its GL error and the actual
    // bound-name cache, not optimistically record the rejected name.
    GLuint wrong=0;glGenTextures(1,&wrong);glBindTexture(GL_TEXTURE_2D,wrong);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,wrong);DGRecordAliasBinding(&c,GL_TEXTURE_RECTANGLE_ARB);
    assert(glGetError()==GL_INVALID_OPERATION && c.rectangle_bindings[0]==texture);
    glPushAttrib(GL_TEXTURE_BIT);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
    DGRecordAliasBinding(&c,GL_TEXTURE_RECTANGLE_ARB);assert(!c.rectangle_bindings[0]);
    glPopAttrib();c.alias_bindings_dirty=1;c.drawable->alias_dirty=1;DGPrepareDrawableAliases(&c,0);
    assert(c.rectangle_bindings[0]==texture && c.sampled_textures[0]==texture);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    DGRecordAliasBinding(&c,GL_TEXTURE_RECTANGLE_ARB);c.drawable->alias_dirty=1;DGPrepareDrawableAliases(&c,0);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&bound);assert(bound==GL_TEXTURE1 && c.sampled_textures[1]==texture);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);DGRecordAliasBinding(&c,GL_TEXTURE_RECTANGLE_ARB);
    glActiveTexture(GL_TEXTURE0);glDeleteTextures(1,&wrong);
    // A real direct IOSurface alias still requires producer Flush/rebind.
    GLuint direct_texture=0;glGenTextures(1,&direct_texture);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,direct_texture);
    struct DGMessage direct_import={.op=DG_BIND_DRAWABLE_TEXTURE,.args={GL_RGBA,64,48,back?2:0}};
    struct DGReply direct_reply={0};
    assert(!DGBindDrawableTexture(&c,&direct_import,&direct_reply)&&!direct_reply.error);
    DGRecordAliasBinding(&c,GL_TEXTURE_RECTANGLE_ARB);DGMarkDrawableDamage(&c);
    before=producer_flushes;DGPrepareDrawableAliases(&c,0);assert(producer_flushes==before+1);
    // Sampling the live drawable into a separate FBO requires the same
    // producer visibility, without changing the application's destination.
    GLuint auxiliary=0;glGenFramebuffersEXT(1,&auxiliary);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,auxiliary);
    c.drawable->alias_dirty=1;before=producer_flushes;
    DGPrepareDrawableAliases(&c,0);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    assert(producer_flushes==before+1 && draw==(GLint)auxiliary && !c.drawable->alias_dirty);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,c.framebuffer);glDeleteFramebuffersEXT(1,&auxiliary);
    ProbeSamplerHazards(&c,texture);
    glDeleteTextures(1,&direct_texture);DGForgetAliases(&c,1,&direct_texture);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);DGRecordAliasBinding(&c,GL_TEXTURE_RECTANGLE_ARB);
    GLuint fbo=0;glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);unsigned char low[4]={0},high[4]={0};
    glReadPixels(16,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,low);glReadPixels(16,40,1,1,GL_RGBA,GL_UNSIGNED_BYTE,high);
    assert(!low[0] && low[1]==255 && high[0]==255 && !high[1]);
    // A cropped top-down view covers the TOP of the full bottom-up source,
    // not a reversed bottom-left subrectangle. Keep the first full view live.
    GLuint cropped=0;glGenTextures(1,&cropped);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,cropped);
    struct DGMessage crop={.op=DG_BIND_DRAWABLE_TEXTURE,.args={GL_RGBA,32,24,back?3:1}};
    struct DGReply crop_reply={0};assert(!DGBindDrawableTexture(&c,&crop,&crop_reply)&&!crop_reply.error);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,cropped,0);
    glReadPixels(16,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,low);assert(!low[0]&&low[1]==255);
    glDeleteTextures(1,&cropped);DGForgetAliases(&c,1,&cropped);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    struct DGMessage flush={.op=DG_FLUSH_DRAWABLE_STATE};struct DGReply state={0};
    assert(!DGFlushDrawableState(&c,&flush,&state) && !state.error && state.result==fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,c.framebuffer);flush.args[0]=1;state=(struct DGReply){0};
    assert(!DGFlushDrawableState(&c,&flush,&state) && !state.error && !state.result);
    // Native GL errors and caller binding survive this compound observation.
    glEnable(0xffffffff);state=(struct DGReply){0};
    assert(!DGFlushDrawableState(&c,&flush,&state) && !state.error && !state.result);
    assert(glGetError()==GL_INVALID_ENUM && !glGetError());
    flush.args[0]=2;state=(struct DGReply){0};
    assert(!DGFlushDrawableState(&c,&flush,&state) && state.error==GL_INVALID_VALUE);
    /* Publication is independent of whether this producer samples its own
     * imported texture. A consumer in another context/drawable must not get
     * the old mirror after the producer's flush/finish. Exercise BOTH the
     * compound GLI flush and the audited SDK's plain dispatch slots. */
    for(unsigned mode=0;mode<3;++mode){
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,c.framebuffer);
        glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
        glClearColor(0,1,1,1);glClear(GL_COLOR_BUFFER_BIT);DGMarkDrawableDamage(&c);
        if(!mode){flush.args[0]=0;state=(struct DGReply){0};assert(!DGFlushDrawableState(&c,&flush,&state)&&!state.error);}
        else {struct DGMessage plain={.op=mode==1?90:89};struct DGReply result={0};
            assert(!DGExecute(&c,&plain,&result)&&!result.error);}
        assert(alias->copied_serial==c.drawable->write_serial);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        glReadPixels(16,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,low);
        assert(!low[0]&&low[1]==255&&low[2]==255);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,c.framebuffer);
        glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);glScissor(0,24,64,24);
        glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
        DGMarkDrawableDamage(&c);flush.args[0]=1;state=(struct DGReply){0};
        assert(!DGFlushDrawableState(&c,&flush,&state)&&!state.error);
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    DGDropDrawable(&c,1);if(!back)assert(!c.spare_storage[0]);
    for(unsigned i=0;i<4;++i)assert(!c.spare_storage[i] || IOSurfaceGetID(c.spare_storage[i])!=source);
    assert(DGSelectDrawable(&c,2,64,48) && IOSurfaceGetID(back?c.back_storage:c.color_storage)!=source);
    // Destroying/replacing a producer must neither recycle its identity nor
    // modify the already imported texture's last pixels.
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glReadPixels(16,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,low);assert(!low[0] && low[1]==255);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);DGForgetAliases(&c,1,&texture);
    assert(!c.alias_group->aliases && !c.alias_group->owned_surface_bytes && !c.alias_group->owned_surface_count);
    assert(!glGetError() && !c.error);DGDestroyDrawables(&c);DGDestroyAliases(&c);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
    puts("PASS flipped pixels, compound/plain producer publication, binding cache/invalid bind/PopAttrib/multitexture, state, reuse, producer identity and deletion lifetime");return 0;
}
