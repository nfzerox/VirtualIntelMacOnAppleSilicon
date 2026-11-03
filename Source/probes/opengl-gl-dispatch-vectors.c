/* One bounded public-API test for native Intel and each period guest. These
 * are real driver results, not tests that a dispatch pointer is non-NULL. */
#include <ApplicationServices/ApplicationServices.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#undef check
static int failures;
static void check(int good,const char *name)
{GLenum e=glGetError();printf("%s %s error=%x\n",good&&!e?"PASS":"FAIL",name,e);if(!good||e)++failures;}
int main(void)
{
    setbuf(stdout,NULL);
    CGLRendererInfoObj info=NULL;GLint renderers=0,renderer=0;
    if(CGLQueryRendererInfo(CGDisplayIDToOpenGLDisplayMask(CGMainDisplayID()),&info,&renderers))return 2;
    for(GLint i=0;i<renderers;++i){GLint online=0,accelerated=0,id=0;
        CGLDescribeRenderer(info,i,kCGLRPOnline,&online);CGLDescribeRenderer(info,i,kCGLRPAccelerated,&accelerated);
        CGLDescribeRenderer(info,i,kCGLRPRendererID,&id);if(online&&accelerated){renderer=id;break;}}
    CGLDestroyRendererInfo(info);if(!renderer)return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFARendererID,(CGLPixelFormatAttribute)renderer,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf||CGLCreateContext(pf,NULL,&c)||!c)return 2;
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(c);
    printf("vendor=%s renderer=%s\n",glGetString(GL_VENDOR),glGetString(GL_RENDERER));
    if(!strstr((const char *)glGetString(GL_VENDOR),"Intel"))return 77;
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"framebuffer");
    GLfloat level=-1;glGetTexLevelParameterfv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&level);
    check(level==16,"float texture-level query");
    GLdouble plane[]={1,2,3,4},result[5]={0,0,0,0,123};
    glClipPlane(GL_CLIP_PLANE0,plane);glGetClipPlane(GL_CLIP_PLANE0,result);
    check(!memcmp(plane,result,sizeof(plane))&&result[4]==123,"clip plane and output tail");
    glTexGendv(GL_S,GL_OBJECT_PLANE,plane);memset(result,0,4*sizeof(GLdouble));
    glGetTexGendv(GL_S,GL_OBJECT_PLANE,result);
    check(!memcmp(plane,result,sizeof(plane))&&result[4]==123,"texture-generation double plane");
    GLfloat fp[5]={0,0,0,0,123};GLint ip[5]={0,0,0,0,123};
    glGetTexGenfv(GL_S,GL_OBJECT_PLANE,fp);glGetTexGeniv(GL_S,GL_OBJECT_PLANE,ip);
    check(fp[0]==1&&fp[3]==4&&fp[4]==123&&ip[0]==1&&ip[3]==4&&ip[4]==123,"texture-generation float/integer planes");
    const GLfloat attenuation[]={1,0.25f,0.5f};GLfloat points[4]={0,0,0,123};
    glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION,attenuation);
    glGetFloatv(GL_POINT_DISTANCE_ATTENUATION,points);
    check(!memcmp(attenuation,points,sizeof(attenuation))&&points[3]==123,"point attenuation vector");
    const GLfloat attribute[]={0.25f,0.5f,0.75f};GLfloat actual[4]={0};
    glVertexAttrib3fvARB(3,attribute);glGetVertexAttribfvARB(3,GL_CURRENT_VERTEX_ATTRIB_ARB,actual);
    check(!memcmp(attribute,actual,sizeof(attribute))&&actual[3]==1,"vertex attribute vector default fourth component");
    const GLubyte normalized[]={0,127,255,255};
    glVertexAttrib4NubvARB(3,normalized);glGetVertexAttribfvARB(3,GL_CURRENT_VERTEX_ATTRIB_ARB,actual);
    check(actual[0]==0&&fabsf(actual[1]-127.0f/255)<0.0001f&&actual[2]==1&&actual[3]==1,"normalized byte attribute");
    glFogCoordfv((const GLfloat[]){0.625f});GLfloat fog=0;glGetFloatv(GL_CURRENT_FOG_COORDINATE,&fog);
    check(fog==0.625f,"fog-coordinate pointer");
    glEdgeFlagv((const GLboolean[]){GL_FALSE});GLboolean edge=GL_TRUE;glGetBooleanv(GL_EDGE_FLAG,&edge);
    check(edge==GL_FALSE,"edge-flag pointer");
    GLfloat priority=0.375f;glPrioritizeTextures(1,&texture,&priority);GLfloat got_priority=0;
    glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_PRIORITY,&got_priority);
    check(got_priority==priority,"texture priority arrays");
    GLuint query=0;glGenQueries(1,&query);GLint bits=0;
    glGetQueryiv(GL_SAMPLES_PASSED,GL_QUERY_COUNTER_BITS,&bits);
    glViewport(0,0,16,16);glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glColor4f(1,0,0,1);
    glBeginQuery(GL_SAMPLES_PASSED,query);
    glRectfv((const GLfloat[]){-1,-1},(const GLfloat[]){1,1});glEndQuery(GL_SAMPLES_PASSED);
    GLuint samples=0;GLint available=0;glGetQueryObjectuiv(query,GL_QUERY_RESULT,&samples);
    glGetQueryObjectiv(query,GL_QUERY_RESULT_AVAILABLE,&available);
    check(bits>0&&samples>0&&available,"occlusion query lifecycle and vector rectangle");
    GLubyte pixel[4]={0};glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    check(pixel[0]==255&&!pixel[1]&&!pixel[2]&&pixel[3]==255,"rectangle GPU pixels");
    glDeleteQueries(1,&query);check(!glIsQuery(query),"query deletion");
    glDeleteQueries(0,NULL);glPrioritizeTextures(0,NULL,NULL);
    check(1,"zero-count input arrays do not touch null storage");
    GLuint untouched=0xa5a5a5a5;glGenQueries(-1,&untouched);GLenum error=glGetError();
    check(error==GL_INVALID_VALUE&&untouched==0xa5a5a5a5,"negative generator preserves output");
    GLint tail=0x12345678;glGetQueryiv(0xdead,GL_QUERY_COUNTER_BITS,&tail);error=glGetError();
    check(error==GL_INVALID_ENUM&&tail==0x12345678,"invalid query preserves output");
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
    printf("failures=%d\n",failures);return failures?1:0;
}
