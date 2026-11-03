/* Finite, process-local qualification of the exact native CGL instruction
 * guard. Never edits an Apple file, shared cache or another process. */
#include <OpenGL/OpenGL.h>
#include <pthread.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <syslog.h>
#include "../PassthroughOpenGL/native-cgl-recovery.c"
int main(void)
{
    pthread_once(&vim_recovery_once,VIMFixNativeRecovery);
    printf("native-multi-engine-fix=%s pointer-bits=%u\n",vim_recovery_ready?"PASS":"UNAVAILABLE",(unsigned)(sizeof(void*)*8));
    return vim_recovery_ready?0:1;
}
