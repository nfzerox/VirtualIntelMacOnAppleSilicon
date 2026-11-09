/* Public CGL/native/guest contract, including output widths and actual state.
 * SecondaryColor3 accepts RGB, but both current secondary-color queries
 * return RGBA (GL 2.1 table 6.5). Do not assume three-element query replies.
 * Native alpha/conversions are printed for period-driver comparison.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(test) do{++checks;if(!(test)){++failures;fprintf(stderr,"FAIL %d: %s\n",__LINE__,#test);}}while(0)
static void query(GLenum key,const GLfloat *expected,unsigned count)
{
    for(unsigned type=0;type<4;++type){
        union {GLdouble alignment;unsigned char bytes[80];} guarded;
        memset(&guarded,0xa5,sizeof(guarded));void *values=guarded.bytes+8;
        size_t element=type==0?sizeof(GLboolean):type==1?sizeof(GLint):type==2?sizeof(GLfloat):sizeof(GLdouble);
        if(type==0)glGetBooleanv(key,values);
        else if(type==1)glGetIntegerv(key,values);
        else if(type==2)glGetFloatv(key,values);
        else glGetDoublev(key,values);
        GLenum error=glGetError();CHECK(error==GL_NO_ERROR);
        for(unsigned j=0;j<8;++j)CHECK(guarded.bytes[j]==0xa5);
        for(size_t j=8+count*element;j<sizeof(guarded);++j)CHECK(guarded.bytes[j]==0xa5);
        printf("QUERY key=%x type=%u error=%x values=",key,type,error);
        for(unsigned j=0;j<count;++j){
            double value=type==0?((GLboolean *)values)[j]:type==1?((GLint *)values)[j]:
                type==2?((GLfloat *)values)[j]:((GLdouble *)values)[j];
            printf("%s%a",j?",":"",value);
            if(j<3||count==1){
                if(type==0)CHECK(value==!!expected[j]);
                else if(type==2||type==3)CHECK(value==expected[j]);
                else CHECK(expected[j]==0?value==0:value>0);
            }else CHECK(value==0||value==1||value==INT32_MAX);
        }
        putchar('\n');
    }
}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&format,&n)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);
    printf("renderer=%s version=%s bits=%u\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),(unsigned)sizeof(void *)*8);
    /* RasterPos and Begin/End need a complete render target even when the
     * fixture's observations are server-state queries, not rendered pixels. */
    GLuint framebuffer=0,renderbuffer=0;
    glGenRenderbuffersEXT(1,&renderbuffer);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,renderbuffer);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
    glGenFramebuffersEXT(1,&framebuffer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,framebuffer);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,renderbuffer);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    CHECK(glGetError()==GL_NO_ERROR);
    const GLfloat original[]={.25f,.5f,.75f},changed[]={.75f,0,.25f},zero[]={0},one[]={1};
    glDisable(GL_LIGHTING);glDisable(GL_COLOR_SUM);glSecondaryColor3fvEXT(original);
    query(GL_CURRENT_SECONDARY_COLOR,original,4);query(GL_COLOR_SUM,zero,1);
    glPushAttrib(GL_CURRENT_BIT|GL_ENABLE_BIT);
    glEnable(GL_COLOR_SUM);glSecondaryColor3fvEXT(changed);
    query(GL_CURRENT_SECONDARY_COLOR,changed,4);query(GL_COLOR_SUM,one,1);
    glPopAttrib();query(GL_CURRENT_SECONDARY_COLOR,original,4);query(GL_COLOR_SUM,zero,1);
    glRasterPos4f(0,0,0,1);CHECK(!glGetError());
    query(GL_CURRENT_RASTER_SECONDARY_COLOR,original,4);
    glSecondaryColor3fvEXT(changed);query(GL_CURRENT_RASTER_SECONDARY_COLOR,original,4);
    glRasterPos4f(0,0,0,1);query(GL_CURRENT_RASTER_SECONDARY_COLOR,changed,4);
    /* Get is not legal within Begin/End; a rejected query must not touch its
     * output. Never fix a missing shape by treating unknown enums as scalars. */
    GLfloat untouched[]={-9,-8,-7,-6};glBegin(GL_POINTS);
    glGetFloatv(GL_CURRENT_SECONDARY_COLOR,untouched);glEnd();
    CHECK(glGetError()==GL_INVALID_OPERATION);
    CHECK(untouched[0]==-9&&untouched[1]==-8&&untouched[2]==-7&&untouched[3]==-6);
    glGetFloatv(0xffffffffu,untouched);CHECK(glGetError()==GL_INVALID_ENUM);
    CHECK(untouched[0]==-9&&untouched[1]==-8&&untouched[2]==-7&&untouched[3]==-6);
    glDeleteFramebuffersEXT(1,&framebuffer);glDeleteRenderbuffersEXT(1,&renderbuffer);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("secondary-color queries checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
