/* Bounded, opt-in visual-effect evidence. This reads the actual display and
 * perturbs timing; it is NOT a performance measurement. Never run during FPS
 * qualification. A new output directory is required; existing files survive. */
#include <ApplicationServices/ApplicationServices.h>
#include <ImageIO/ImageIO.h>
#include <mach/mach_time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
static double now(void)
{
    static mach_timebase_info_data_t scale;
    if(!scale.denom)mach_timebase_info(&scale);
    return mach_absolute_time()*(double)scale.numer/scale.denom/1e9;
}
int main(int argc,char **argv)
{
    if(argc!=2&&argc!=7){fprintf(stderr,"usage: %s NEW_OUTPUT_DIRECTORY [X Y WIDTH HEIGHT HZ]\n",argv[0]);return 2;}
    CGRect region=CGRectNull;double hz=4;unsigned maximum=40;
    if(argc==7){
        long values[5];
        for(int i=0;i<5;i++){char *end=NULL;values[i]=strtol(argv[i+2],&end,10);if(!*argv[i+2]||*end)return 2;}
        if(values[0]<0||values[1]<0||values[2]<1||values[3]<1||values[2]>512||values[3]>512||values[4]<1||values[4]>60||
           values[0]+values[2]>(long)CGDisplayPixelsWide(CGMainDisplayID())||
           values[1]+values[3]>(long)CGDisplayPixelsHigh(CGMainDisplayID()))return 2;
        region=CGRectMake(values[0],values[1],values[2],values[3]);hz=values[4];maximum=(unsigned)(10*hz);
    }
    if(mkdir(argv[1],0700)){perror("new output directory");return 2;}
    double start=now();unsigned frames=0;
    printf("READY display capture (10 seconds, at most %u frames)\n",maximum);fflush(stdout);
    while(frames<maximum && now()-start<10){
        double began=now();CGImageRef image=CGRectIsNull(region)?CGDisplayCreateImage(CGMainDisplayID()):
            CGDisplayCreateImageForRect(CGMainDisplayID(),region);
        if(!image){fputs("Display capture unavailable\n",stderr);return 1;}
        char path[4096];int n=snprintf(path,sizeof(path),"%s/frame-%03u.png",argv[1],frames);
        if(n<0 || (size_t)n>=sizeof(path)){CGImageRelease(image);return 2;}
        CFURLRef url=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8 *)path,strlen(path),false);
        CGImageDestinationRef out=url?CGImageDestinationCreateWithURL(url,CFSTR("public.png"),1,NULL):NULL;
        if(out)CGImageDestinationAddImage(out,image,NULL);
        int okay=out && CGImageDestinationFinalize(out);
        if(out)CFRelease(out);if(url)CFRelease(url);CGImageRelease(image);
        if(!okay)return 1;
        printf("%u %.6f %.6f\n",frames,began-start,now()-began);fflush(stdout);++frames;
        double wait=began+1/hz-now();if(wait>0)usleep((useconds_t)(wait*1e6));
    }
    return 0;
}
