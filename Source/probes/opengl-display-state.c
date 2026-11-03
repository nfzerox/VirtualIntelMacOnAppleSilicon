/* Read-only display publication probe; deliberately never initializes CGL. */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
extern int CGSGetOnlineDisplayList(unsigned,CGDirectDisplayID *,unsigned *);
extern unsigned CGSGetDisplayOpenGLDisplayMask(CGDirectDisplayID);
extern unsigned CGSGetDisplayOpenGLDisplayAliasMask(CGDirectDisplayID);
int main(void)
{
    CGDirectDisplayID ids[32]={0};unsigned n=0;
    CGError e=CGGetOnlineDisplayList(32,ids,&n);
    fprintf(stderr,"public online error=%d count=%u main=%x\n",e,n,CGMainDisplayID());
    for(unsigned i=0;i<n && i<32;++i)
        fprintf(stderr,"display=%x size=%zux%zu accelerated=%d mask=%x alias=%x\n",ids[i],
            CGDisplayPixelsWide(ids[i]),CGDisplayPixelsHigh(ids[i]),CGDisplayUsesOpenGLAcceleration(ids[i]),
            CGSGetDisplayOpenGLDisplayMask(ids[i]),CGSGetDisplayOpenGLDisplayAliasMask(ids[i]));
    n=0;e=CGSGetOnlineDisplayList(32,ids,&n);
    fprintf(stderr,"private online error=%d count=%u\n",e,n);
    return e!=0;
}
