/* Tiger's Rosetta GL loader cannot bind the newer __stderrp data symbol.
 * Error/opt-in diagnostics must not prevent an otherwise valid renderer from
 * loading. Use a bounded local buffer and the native descriptor API on10.4;
 * later systems keep exactly their existing stdio call sites. No draw-time
 * formatting is introduced: callers retain their existing diagnostic gates. */
#ifndef VIM_PERIOD_STDERR_H
#define VIM_PERIOD_STDERR_H
#include "tiger-rosetta-platform.h"
#include <stdio.h>
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
static void VIMGLStderr(const char *format,...) __attribute__((format(printf,1,2)));
static void VIMGLStderr(const char *format,...)
{
    int saved_errno=errno;
    char text[1024];va_list arguments;va_start(arguments,format);
    int count=vsnprintf(text,sizeof(text),format,arguments);va_end(arguments);
    if(count>0){
        size_t length=(size_t)count<sizeof(text)?(size_t)count:sizeof(text)-1;
        size_t offset=0;
        while(offset<length){
            ssize_t written=write(STDERR_FILENO,text+offset,length-offset);
            if(written>0)offset+=(size_t)written;
            else if(written<0&&errno==EINTR)continue;
            else break;
        }
    }
    errno=saved_errno;
}
static void VIMGLSyslog(int priority,const char *format,...) __attribute__((format(printf,2,3)));
static void VIMGLSyslog(int priority,const char *format,...)
{
    char text[1024];va_list args;va_start(args,format);
    vsnprintf(text,sizeof(text),format,args);va_end(args);
    if(syslog)syslog(priority,"%s",text);else VIMGLStderr("%s\n",text);
}
#define syslog VIMGLSyslog
#else
#define VIMGLStderr(...) fprintf(stderr,__VA_ARGS__)
#endif
#endif
