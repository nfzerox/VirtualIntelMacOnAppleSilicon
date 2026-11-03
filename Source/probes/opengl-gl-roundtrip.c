/* Bounded actual CGL/GLI query timing. The chosen query is not the bridge's
 * common-state cache. No window, pixels, private offsets or renderer substitute.
 * Transport-only microbenchmark: NOT desktop FPS or input-to-visible latency. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int compare(const void *a,const void *b)
{double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
int main(int argc,char **argv)
{
    if(argc!=2 || strcmp(argv[1],"--require-intel")){
        fputs("usage: passthrough-opengl-roundtrip --require-intel\n",stderr);return 2;
    }
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1060
        kCGLPFASupportsAutomaticGraphicsSwitching,
#endif
        0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint formats=0;
    if(CGLChoosePixelFormat(attrs,&format,&formats) || !format)return 1;
    CGLError error=CGLCreateContext(format,NULL,&context);CGLDestroyPixelFormat(format);
    if(error || !context || CGLSetCurrentContext(context))return 1;
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    int good=renderer && strstr(renderer,"Intel")!=NULL;
    GLint expected=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&expected);
    good=good && expected>=1024 && glGetError()==GL_NO_ERROR;
    printf("renderer=%s value=%d\n",renderer?renderer:"unavailable",(int)expected);
    if(good){
        mach_timebase_info_data_t clock;mach_timebase_info(&clock);
        for(unsigned phase=0;phase<3 && good;++phase){
            double values[1000],sum=0;
            for(unsigned i=0;i<1050;++i){
                GLint value=0;uint64_t start=mach_absolute_time();
                glGetIntegerv(GL_MAX_TEXTURE_SIZE,&value);
                uint64_t ticks=mach_absolute_time()-start;
                if(value!=expected){good=0;break;}
                if(i>=50){values[i-50]=ticks*(double)clock.numer/clock.denom/1000.0;sum+=values[i-50];}
            }
            if(good){qsort(values,1000,sizeof(*values),compare);
                printf("phase=%u queries=1000 mean_us=%.3f median_us=%.3f p95_us=%.3f\n",
                    phase,sum/1000,values[500],values[950]);}
        }
    }
    good=glGetError()==GL_NO_ERROR && good;
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    puts(good?"PASS actual typed query values and errors":"FAIL actual query contract");
    return good?0:1;
}
