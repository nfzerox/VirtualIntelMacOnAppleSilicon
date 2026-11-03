/* Real wire test, including client cache visibility/invalidation. Including
 * the client permits bounded counters/toggle assertions without public test
 * exports or an alternative production implementation. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include <math.h>
static int failed;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);failed=1;}}while(0)
static void verify(GLint x,GLint y,GLint width,GLint height,GLfloat scale)
{
    GLint viewport[4]={0},framebuffer=-1,buffer=-1,rectangle=-1;
    GLfloat model[16]={0},projection[16]={0};
    glGetIntegerv(GL_VIEWPORT,viewport);glGetFloatv(GL_MODELVIEW_MATRIX,model);
    glGetFloatv(GL_PROJECTION_MATRIX,projection);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&framebuffer);
    glGetIntegerv(GL_DRAW_BUFFER,&buffer);
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&rectangle);
    CHECK(viewport[0]==x && viewport[1]==y && viewport[2]==width && viewport[3]==height);
    CHECK(model[0]==scale && model[5]==scale && model[10]==scale && model[15]==1);
    CHECK(projection[0]==1 && projection[5]==1 && projection[10]==1 && projection[15]==1);
    CHECK(framebuffer==0 && buffer==GL_FRONT && rectangle==0);
}
int main(int argc,char **argv)
{
    int fallback=argc==2 && !strcmp(argv[1],"--fallback");
    if(argc>2 || (argc==2 && !fallback))return 2;
    unsigned char pixels[64*64*4]={0};
    struct DGContext *a=DGCreateContext(NULL,NULL),*b=DGCreateContext(NULL,NULL);
    CHECK(a && b);if(!a || !b)return 2;
    int negotiated=!!(a->connection->capabilities&DG_CAP_COMMON_STATE);
    CHECK(negotiated!=fallback);
    CHECK(DGMakeCurrent(a,pixels,GL_UNSIGNED_BYTE,64,64));
    int prefetch=negotiated && !!(a->connection->capabilities&DG_CAP_ERROR_STATE);
    printf("renderer=%s negotiated=%d error-state=%d\n",glGetString(GL_RENDERER),negotiated,prefetch);
    CHECK(strstr((const char *)glGetString(GL_RENDERER),"Intel")!=NULL);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glViewport(2,3,60,59);
    uint64_t hits=dg_common_state_hits,fills=dg_common_state_fills;
    verify(2,3,60,59,1);
    if(negotiated)CHECK(dg_common_state_fills==fills+1 && dg_common_state_hits==hits+5);
    else CHECK(dg_common_state_fills==fills && dg_common_state_hits==hits);
    CHECK(glGetError()==GL_NO_ERROR);
    // Error checks and the audited unrelated setters cannot change these
    // native matrices/viewport/bindings. They must not cause another snapshot,
    // nor suppress/duplicate real errors, including repeated invalid calls.
    fills=dg_common_state_fills;
    glEnable(GL_BLEND);glHint(GL_PERSPECTIVE_CORRECTION_HINT,GL_NICEST);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    CHECK(glGetError()==GL_NO_ERROR);verify(2,3,60,59,1);
    if(negotiated)CHECK(dg_common_state_fills==fills);
    for(int i=0;i<2;++i){
        glEnable(0xffffffffu);verify(2,3,60,59,1);
        CHECK(glGetError()==GL_INVALID_ENUM && glGetError()==GL_NO_ERROR);
        if(negotiated)CHECK(dg_common_state_fills==fills);
    }
    glHint(0xffffffffu,GL_NICEST);verify(2,3,60,59,1);
    CHECK(glGetError()==GL_INVALID_ENUM && glGetError()==GL_NO_ERROR);
    if(negotiated)CHECK(dg_common_state_fills==fills);
    glDisable(GL_BLEND);
    // Read-only texture queries still execute on the native driver, but must
    // not invalidate unrelated matrices, viewport or public bindings. Include
    // Sierra's measured rectangle WRAP_T query and a native invalid enum.
    verify(2,3,60,59,1);fills=dg_common_state_fills;
    GLint wrap=0;GLfloat wrapf=0;
    glGetTexParameteriv(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,&wrap);
    CHECK(wrap==GL_CLAMP_TO_EDGE);
    verify(2,3,60,59,1);
    glGetTexParameterfv(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,&wrapf);
    CHECK(wrapf==(GLfloat)wrap);verify(2,3,60,59,1);
    glGetTexParameteriv(GL_TEXTURE_RECTANGLE_ARB,0xffffffffu,&wrap);
    verify(2,3,60,59,1);
    CHECK(glGetError()==GL_INVALID_ENUM && glGetError()==GL_NO_ERROR);
    printf("texture-query observation extra_fills=%llu\n",(unsigned long long)(dg_common_state_fills-fills));
    if(negotiated)CHECK(dg_common_state_fills==fills);
    dg_common_state_reuse=0;verify(2,3,60,59,1);dg_common_state_reuse=1;
    verify(2,3,60,59,1);fills=dg_common_state_fills;
    if(a->connection->capabilities&DG_CAP_FLUSH_DRAWABLE_STATE){
        for(int finish=0;finish<=1;++finish){
            uint32_t observed=UINT32_MAX;
            CHECK(DGFlushDrawableState(finish,&observed)==1 && observed==0);
            verify(2,3,60,59,1);
        }
        printf("compound-flush observation extra_fills=%llu\n",(unsigned long long)(dg_common_state_fills-fills));
        if(negotiated)CHECK(dg_common_state_fills==fills);
        dg_common_state_reuse=0;verify(2,3,60,59,1);dg_common_state_reuse=1;
    }
    // Private GPU publication/copy helpers restore all public bindings.
    // Compare the same six fields both cached and directly from the driver;
    // actual pixels/held epochs are checked by the separate region fixtures.
    uint64_t object=0;arc4random_buf(&object,sizeof(object));if(!object)object=1;
    uint32_t lease=DGAcquireSurface(object,1,64,64);CHECK(lease);
    CHECK(DGUploadSurface(lease,pixels,64,64,256,1));
    verify(2,3,60,59,1);fills=dg_common_state_fills;
    struct DGSurfaceRegion patch={0,0,8,8,0,0};uint64_t epoch=0;
    CHECK(DGPublishSurfaceEpoch(lease,0,0,2));verify(2,3,60,59,1);
    CHECK(DGSealSurfaceVersion(lease,2));verify(2,3,60,59,1);
    CHECK(DGPublishSealedSurfaceRegions(lease,&patch,1,0,0,2,3,0));verify(2,3,60,59,1);
    CHECK(DGCopySurfaceRegions(lease,&patch,1,3,4));verify(2,3,60,59,1);
    CHECK(DGPublishSealedSurfaceRegions(lease,&patch,1,0,0,4,5,1));verify(2,3,60,59,1);
    CHECK(DGImportSurfaceRegion(lease,5,0,0,64,64,0));verify(2,3,60,59,1);
    CHECK(DGSurfaceEpoch(lease,&epoch) && epoch==5);verify(2,3,60,59,1);
    printf("surface-handoff observation extra_fills=%llu\n",(unsigned long long)(dg_common_state_fills-fills));
    if(negotiated)CHECK(dg_common_state_fills==fills);
    dg_common_state_reuse=0;verify(2,3,60,59,1);dg_common_state_reuse=1;
    DGReleaseSurface(lease);
    // Our captured-array draw packets change no field of DGCommonState.
    // Require real native pixels/errors, and independently bypass the cache
    // for the same six values before accepting the removed round trips.
    GLfloat positions[]={-1,-1,3,-1,-1,3},green[]={0,1,0,1,0,1,0,1,0,1,0,1};
    GLubyte indices[]={0,1,2};
    glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2,GL_FLOAT,0,positions);glColorPointer(4,GL_FLOAT,0,green);
    verify(2,3,60,59,1);fills=dg_common_state_fills;
    uint64_t draw_prefetches=dg_common_state_prefetches;
    for(unsigned i=0;i<8;++i){
        if(i==2 && (a->connection->capabilities&DG_CAP_INSTANCED_ARRAYS))
            DGDrawInstances(a,GL_TRIANGLES,0,3,0,NULL,1,1);
        else if(i==3 && (a->connection->capabilities&DG_CAP_INSTANCED_ARRAYS))
            DGDrawInstances(a,GL_TRIANGLES,0,3,GL_UNSIGNED_BYTE,indices,1,1);
        else if(i&1)glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_BYTE,indices);
        else glDrawArrays(GL_TRIANGLES,0,3);
        verify(2,3,60,59,1);CHECK(glGetError()==GL_NO_ERROR);
    }
    printf("draw-state observation instanced=%d extra_fills=%llu extra_prefetches=%llu\n",
        !!(a->connection->capabilities&DG_CAP_INSTANCED_ARRAYS),
        (unsigned long long)(dg_common_state_fills-fills),
        (unsigned long long)(dg_common_state_prefetches-draw_prefetches));
    if(negotiated)CHECK(dg_common_state_fills==fills && dg_common_state_prefetches==draw_prefetches);
    dg_common_state_reuse=0;verify(2,3,60,59,1);dg_common_state_reuse=1;
    CHECK(glGetError()==GL_NO_ERROR);
    glDrawArrays(0xffffffffu,0,3);verify(2,3,60,59,1);
    CHECK(glGetError()==GL_INVALID_ENUM && glGetError()==GL_NO_ERROR);
    unsigned char draw_pixel[4]={0};glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,draw_pixel);
    CHECK(draw_pixel[0]==0 && draw_pixel[1]==255 && draw_pixel[2]==0 && !glGetError());
    glDisableClientState(GL_COLOR_ARRAY);glDisableClientState(GL_VERTEX_ARRAY);
    // Actual compositor immediate quads and texture uploads must retain
    // independently observed native state, not infer state from setters.
    // Compare with cache disabled too, and require real pixels/native errors.
    verify(2,3,60,59,1);fills=dg_common_state_fills;
    uint64_t immediate_prefetches=dg_common_state_prefetches;
    for(unsigned i=0;i<8;++i){
        glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        glBlendEquation(GL_FUNC_ADD);
        glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
        GLfloat mode=GL_MODULATE;glTexEnvfv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,&mode);
        glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
        GLint imode=GL_MODULATE;glTexEnviv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,&imode);
        CHECK(!glIsEnabled(GL_BLEND));
        glColor4f(0,1,0,1);glBegin(GL_TRIANGLES);
        glMultiTexCoord2f(GL_TEXTURE1,0,0);glTexCoord2f(0,0);glVertex2f(-1,-1);
        glMultiTexCoord2f(GL_TEXTURE1,1,0);glTexCoord2f(1,0);glVertex2f(3,-1);
        glMultiTexCoord2f(GL_TEXTURE1,0,1);glTexCoord2f(0,1);glVertex2f(-1,3);glEnd();
        verify(2,3,60,59,1);CHECK(glGetError()==GL_NO_ERROR);
    }
    printf("immediate-state observation extra_fills=%llu extra_prefetches=%llu\n",
        (unsigned long long)(dg_common_state_fills-fills),
        (unsigned long long)(dg_common_state_prefetches-immediate_prefetches));
    if(negotiated)CHECK(dg_common_state_fills==fills && dg_common_state_prefetches==immediate_prefetches);
    glTexEnvi(GL_TEXTURE_ENV,0xffffffffu,GL_MODULATE);
    verify(2,3,60,59,1);
    CHECK(glGetError()==GL_INVALID_ENUM && !glGetError());
    if(negotiated)CHECK(dg_common_state_fills==fills && dg_common_state_prefetches==immediate_prefetches);
    dg_common_state_reuse=0;verify(2,3,60,59,1);dg_common_state_reuse=1;
    memset(draw_pixel,0,sizeof(draw_pixel));glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,draw_pixel);
    CHECK(draw_pixel[0]==0 && draw_pixel[1]==255 && draw_pixel[2]==0 && !glGetError());
    GLuint upload_texture=0;glGenTextures(1,&upload_texture);glBindTexture(GL_TEXTURE_2D,upload_texture);
    verify(2,3,60,59,1);fills=dg_common_state_fills;
    const GLubyte upload_pixels[]={19,43,71,255,83,101,127,255,139,151,173,255,191,211,239,255};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,upload_pixels);
    verify(2,3,60,59,1);CHECK(!glGetError());
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,upload_pixels);
    verify(2,3,60,59,1);CHECK(!glGetError());
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,-1,2,GL_RGBA,GL_UNSIGNED_BYTE,upload_pixels);
    verify(2,3,60,59,1);CHECK(glGetError()==GL_INVALID_VALUE && !glGetError());
    glAlphaFunc(0xffffffffu,0);verify(2,3,60,59,1);
    CHECK(glGetError()==GL_INVALID_ENUM && !glGetError());
    glBlendFunc(GL_ONE,GL_ZERO);glScissor(0,0,64,64);glColor4ub(255,255,255,255);
    glFlush();glFinish();verify(2,3,60,59,1);CHECK(!glGetError());
    printf("upload-state observation extra_fills=%llu\n",(unsigned long long)(dg_common_state_fills-fills));
    if(negotiated)CHECK(dg_common_state_fills==fills);
    dg_common_state_reuse=0;verify(2,3,60,59,1);dg_common_state_reuse=1;
    // Use the implemented native FBO/read-pixels route, not the currently
    // unsupported glGetTexImage codec (which would invalidate this control).
    GLuint upload_fbo=0;glGenFramebuffersEXT(1,&upload_fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,upload_fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,upload_texture,0);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    GLubyte uploaded[sizeof(upload_pixels)]={0};
    glReadPixels(0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,uploaded);
    CHECK(!memcmp(uploaded,upload_pixels,sizeof(uploaded)) && !glGetError());
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&upload_fbo);
    glBindTexture(GL_TEXTURE_2D,0);glDeleteTextures(1,&upload_texture);
    verify(2,3,60,59,1);fills=dg_common_state_fills;
    // Diagnostic off/on still observes the same state with normal refills.
    dg_common_neutral_reuse=0;glHint(GL_PERSPECTIVE_CORRECTION_HINT,GL_FASTEST);
    verify(2,3,60,59,1);
    if(negotiated)CHECK(dg_common_state_fills==fills+1);
    dg_common_neutral_reuse=1;
    GLhandleARB shader=glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB);
    const GLcharARB *source="#version 120\nuniform int offset;uniform vec2 shift;uniform mat4 transform;void main(){gl_Position=transform*gl_Vertex+vec4(shift+vec2(float(offset)*0.01),0.0,0.0);}";
    glShaderSourceARB(shader,1,&source,NULL);glCompileShaderARB(shader);
    GLhandleARB program=glCreateProgramObjectARB();glAttachObjectARB(program,shader);
    glLinkProgramARB(program);GLint linked=0;
    glGetObjectParameterivARB(program,GL_OBJECT_LINK_STATUS_ARB,&linked);CHECK(linked);
    GLint location=glGetUniformLocationARB(program,"offset");CHECK(location>=0);
    GLint shift=glGetUniformLocationARB(program,"shift"),transform=glGetUniformLocationARB(program,"transform");
    CHECK(shift>=0 && transform>=0);
    glUseProgramObjectARB(program);CHECK(glGetError()==GL_NO_ERROR);
    verify(2,3,60,59,1);fills=dg_common_state_fills;
    glUniform1iARB(location,3);verify(2,3,60,59,1);
    CHECK(glGetError()==GL_NO_ERROR);
    if(negotiated)CHECK(dg_common_state_fills==fills);
    const GLfloat identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    for(unsigned i=0;i<8;++i){
        glUseProgramObjectARB(program);
        CHECK(glGetUniformLocationARB(program,"shift")==shift);
        glUniform2fARB(shift,(GLfloat)i,.25f);
        glUniformMatrix4fvARB(transform,1,GL_FALSE,identity);
        verify(2,3,60,59,1);CHECK(!glGetError());
    }
    if(negotiated)CHECK(dg_common_state_fills==fills);
    // The same six values must match uncached host reads; wrong-type native
    // errors still pass through rather than being swallowed by state reuse.
    dg_common_state_reuse=0;verify(2,3,60,59,1);dg_common_state_reuse=1;
    verify(2,3,60,59,1);fills=dg_common_state_fills;
    glUniform2fARB(location,1,2);verify(2,3,60,59,1);
    CHECK(glGetError()==GL_INVALID_OPERATION && !glGetError());
    if(negotiated)CHECK(dg_common_state_fills==fills);
    glUniform1iARB(100000,3);verify(2,3,60,59,1);
    CHECK(glGetError()==GL_INVALID_OPERATION && glGetError()==GL_NO_ERROR);
    if(negotiated)CHECK(dg_common_state_fills==fills);
    glUseProgramObjectARB(0);glDeleteObjectARB(program);glDeleteObjectARB(shader);
    // Independently valid native fields: never infer a setter's result, but
    // do not re-query unaffected matrices for a viewport/texture/FBO change.
    verify(2,3,60,59,1);fills=dg_common_state_fills;
    GLfloat unaffected[16]={0};GLint viewport[4]={0},binding=-1;
    glViewport(4,5,52,53);glGetFloatv(GL_MODELVIEW_MATRIX,unaffected);
    CHECK(unaffected[0]==1 && unaffected[15]==1);
    if(negotiated)CHECK(dg_common_state_fills==fills);
    glGetIntegerv(GL_VIEWPORT,viewport);
    CHECK(viewport[0]==4 && viewport[1]==5 && viewport[2]==52 && viewport[3]==53);
    if(negotiated)CHECK(dg_common_state_fills==fills+1);
    fills=dg_common_state_fills;
    glPushMatrix();glScalef(2,2,2);glGetIntegerv(GL_VIEWPORT,viewport);
    CHECK(viewport[0]==4 && viewport[1]==5);
    if(negotiated)CHECK(dg_common_state_fills==fills);
    glGetFloatv(GL_MODELVIEW_MATRIX,unaffected);CHECK(unaffected[0]==2);
    if(negotiated)CHECK(dg_common_state_fills==fills+1);
    fills=dg_common_state_fills;
    glPopMatrix();glGetIntegerv(GL_VIEWPORT,viewport);
    if(negotiated)CHECK(dg_common_state_fills==fills);
    glGetFloatv(GL_MODELVIEW_MATRIX,unaffected);CHECK(unaffected[0]==1);
    if(negotiated)CHECK(dg_common_state_fills==fills+1);
    glViewport(2,3,60,59);verify(2,3,60,59,1);
    GLuint partial_texture=0;glGenTextures(1,&partial_texture);verify(2,3,60,59,1);
    fills=dg_common_state_fills;
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,partial_texture);
    glGetFloatv(GL_MODELVIEW_MATRIX,unaffected);
    if(negotiated)CHECK(dg_common_state_fills==fills);
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&binding);CHECK(binding==(GLint)partial_texture);
    if(negotiated)CHECK(dg_common_state_fills==fills+1);
    fills=dg_common_state_fills;
    glActiveTexture(GL_TEXTURE1);glGetIntegerv(GL_VIEWPORT,viewport);
    if(negotiated)CHECK(dg_common_state_fills==fills);
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&binding);CHECK(binding==0);
    if(negotiated)CHECK(dg_common_state_fills==fills+1);
    fills=dg_common_state_fills;
    glActiveTexture(GL_TEXTURE0);glGetFloatv(GL_MODELVIEW_MATRIX,unaffected);
    if(negotiated)CHECK(dg_common_state_fills==fills);
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&binding);CHECK(binding==(GLint)partial_texture);
    fills=dg_common_state_fills;
    glDeleteTextures(1,&partial_texture);glGetFloatv(GL_MODELVIEW_MATRIX,unaffected);
    if(negotiated)CHECK(dg_common_state_fills==fills);
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&binding);CHECK(binding==0);
    GLuint partial_fbo=0;glGenFramebuffersEXT(1,&partial_fbo);verify(2,3,60,59,1);
    fills=dg_common_state_fills;
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,partial_fbo);glGetFloatv(GL_MODELVIEW_MATRIX,unaffected);
    if(negotiated)CHECK(dg_common_state_fills==fills);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&binding);CHECK(binding==(GLint)partial_fbo);
    if(negotiated)CHECK(dg_common_state_fills==fills+1);
    fills=dg_common_state_fills;
    glDrawBuffer(GL_NONE);glGetIntegerv(GL_VIEWPORT,viewport);
    if(negotiated)CHECK(dg_common_state_fills==fills);
    glGetIntegerv(GL_DRAW_BUFFER,&binding);CHECK(binding==GL_NONE);
    if(negotiated)CHECK(dg_common_state_fills==fills+1);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&partial_fbo);
    verify(2,3,60,59,1);CHECK(glGetError()==GL_NO_ERROR);
    // The error barrier's actual native snapshot eliminates a subsequent
    // state RPC, including after an error. Multiple pending flags must each
    // survive once; no fake-clean flag or repeated-error suppression.
    fills=dg_common_state_fills;uint64_t prefetched=dg_common_state_prefetches;
    glViewport(6,7,48,49);CHECK(glGetError()==GL_NO_ERROR);verify(6,7,48,49,1);
    if(prefetch)CHECK(dg_common_state_fills==fills && dg_common_state_prefetches==prefetched+1);
    glEnable(0xffffffffu);glViewport(0,0,-1,20);
    GLenum first=glGetError(),second=glGetError(),third=glGetError();
    // Native GL may have one sticky flag rather than one flag per error kind.
    CHECK((first==GL_INVALID_ENUM && (second==GL_INVALID_VALUE || second==GL_NO_ERROR)) ||
          (first==GL_INVALID_VALUE && second==GL_INVALID_ENUM));
    CHECK(third==GL_NO_ERROR);verify(6,7,48,49,1);
    // Force one older error into the executor's existing pending slot via a
    // native state query, then generate another real native error. Unlike the
    // previous test this must retain BOTH even on a single-native-flag driver.
    glEnable(0xffffffffu);glViewport(5,6,46,47);glGetIntegerv(GL_VIEWPORT,viewport);
    CHECK(viewport[0]==5 && viewport[1]==6);
    glViewport(0,0,-1,20);
    first=glGetError();second=glGetError();third=glGetError();
    CHECK(first==GL_INVALID_ENUM && second==GL_INVALID_VALUE && third==GL_NO_ERROR);
    verify(5,6,46,47,1);
    // Diagnostic off is the genuine old-peer request, not alternate GL math.
    dg_error_state_prefetch=0;glViewport(2,3,60,59);
    prefetched=dg_common_state_prefetches;CHECK(glGetError()==GL_NO_ERROR);
    verify(2,3,60,59,1);CHECK(dg_common_state_prefetches==prefetched);
    dg_error_state_prefetch=1;
    glViewport(0,0,-1,20);verify(2,3,60,59,1);CHECK(glGetError()==GL_INVALID_VALUE);
    glEnable(0xffffffffu);verify(2,3,60,59,1);CHECK(glGetError()==GL_INVALID_ENUM);
    glPushAttrib(GL_VIEWPORT_BIT);glViewport(4,5,52,53);glPushMatrix();glScalef(2,2,2);
    verify(4,5,52,53,2);glPopMatrix();glPopAttrib();verify(2,3,60,59,1);
    CHECK(glGetError()==GL_NO_ERROR);
    // A private drawable change is a command too, not an invisible exception.
    CHECK(DGMakeCurrent(a,pixels+4,GL_UNSIGNED_BYTE,32,32));
    glViewport(0,0,32,32);verify(0,0,32,32,1);
    CHECK(DGMakeCurrent(a,pixels,GL_UNSIGNED_BYTE,64,64));
    glViewport(2,3,60,59);verify(2,3,60,59,1);
    // Public FBO names must never expose the executor's virtual-default FBO.
    GLuint fbo=0;glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    GLint bound=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&bound);CHECK(bound==(GLint)fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);verify(2,3,60,59,1);
    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&bound);CHECK(bound==(GLint)texture);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);glDeleteTextures(1,&texture);
    CHECK(glGetError()==GL_NO_ERROR);
    // Context switching preserves each actual context's state, not another's.
    CHECK(DGMakeCurrent(b,pixels,GL_UNSIGNED_BYTE,64,64));
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glViewport(7,8,40,41);glScalef(3,3,3);verify(7,8,40,41,3);
    CHECK(DGMakeCurrent(a,pixels,GL_UNSIGNED_BYTE,64,64));verify(2,3,60,59,1);
    // Ordinary list execution invalidates a previously captured snapshot.
    GLuint list=glGenLists(1);CHECK(list!=0);
    glNewList(list,GL_COMPILE);glViewport(6,7,46,47);glScalef(4,4,4);glEndList();
    verify(2,3,60,59,1);glCallList(list);verify(6,7,46,47,4);glDeleteLists(list,1);
    CHECK(glGetError()==GL_NO_ERROR);
    // A non-cached result type keeps the ordinary exact conversion route.
    GLdouble matrix[16]={0};glGetDoublev(GL_MODELVIEW_MATRIX,matrix);CHECK(matrix[0]==4);
    glBegin(GL_POINTS);GLfloat illegal[16]={0};fills=dg_common_state_fills;
    glGetFloatv(GL_MODELVIEW_MATRIX,illegal);glEnd();
    CHECK(dg_common_state_fills==fills && glGetError()==GL_INVALID_OPERATION);
    // Same-process off/on must invalidate, not resurrect pre-mutation values.
    dg_common_state_reuse=0;glViewport(8,9,38,39);verify(8,9,38,39,4);
    dg_common_state_reuse=1;verify(8,9,38,39,4);CHECK(glGetError()==GL_NO_ERROR);
    DGDestroyContext(b);DGDestroyContext(a);
    const int core[]={0x56494d02,0x3200,0};
    a=DGCreateContext(core,NULL);CHECK(a && DGMakeCurrent(a,pixels,GL_UNSIGNED_BYTE,64,64));
    if(a){int core_cache=negotiated && (a->connection->features&DG_FEATURE_CORE_COMMON_STATE);
        glViewport(1,2,30,31);fills=dg_common_state_fills;
        GLint v[4]={0};glGetIntegerv(GL_VIEWPORT,v);
        CHECK(v[0]==1 && v[1]==2 && v[2]==30 && v[3]==31 && dg_common_state_fills==fills+!!core_cache);
        fills=dg_common_state_fills;glGetIntegerv(GL_VIEWPORT,v);
        CHECK(dg_common_state_fills==fills && glGetError()==GL_NO_ERROR);
        // Snapshotting core state must never manufacture the removed fixed
        // function matrices or consume their actual native error.
        GLfloat removed[16]={0};glGetFloatv(GL_MODELVIEW_MATRIX,removed);
        CHECK(glGetError()==GL_INVALID_ENUM && glGetError()==GL_NO_ERROR);
        glViewport(4,5,20,21);glGetIntegerv(GL_VIEWPORT,v);
        CHECK(v[0]==4 && v[1]==5 && v[2]==20 && v[3]==21 && !glGetError());
        DGDestroyContext(a);}
    printf("%s typed common-state wire: cache hits, command/private invalidation, context isolation, FBO virtualization, matrix/attribute stacks, display lists, Begin errors, exact double and core/old-host fallback\n",failed?"FAIL":"PASS");
    return failed;
}
