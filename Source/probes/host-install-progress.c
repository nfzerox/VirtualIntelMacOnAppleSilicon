#include "../compat/VIMHostInstallProgress.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    double previous=0;
    for(unsigned i=0;i<=10000;++i) {
        double input=i/10000.,output=VIMHostInstallProgressPosition(input,1);
        assert(output>=previous&&output>=0&&output<=1);
        assert(VIMHostInstallProgressPosition(input,0)==input);
        previous=output;
    }
    assert(VIMHostInstallProgressPosition(0,1)==0);
    assert(VIMHostInstallProgressPosition(1,1)==1);
    assert(fabs(VIMHostInstallProgressPosition(.95,1)-.73)<1e-12);
    assert(fabs(VIMHostInstallProgressPosition(.96,1)-.86)<1e-12);
    assert(fabs(VIMHostInstallProgressPosition(.98,1)-.98)<1e-12);
    /* Actual extraction and conversion byte fractions remain linear within
     * their own measured ranges. The caller still applies its existing MAX
     * across overlapping APFS/cache stages and reserves completion for exit. */
    double start=VIMHostInstallProgressPosition(.96,1),stop=VIMHostInstallProgressPosition(.98,1);
    assert(fabs(start+(stop-start)*.5-.92)<1e-12);
    assert(VIMHostInstallRemainingSeconds(30,.5,100,-1)==-1);
    assert(VIMHostInstallRemainingSeconds(30,.5,100,15)==45);
    assert(VIMHostInstallRemainingSeconds(10,-1,30,60)==80);
    assert(VIMHostInstallRemainingSeconds(40,-1,30,60)==-1);
    assert(VIMHostInstallRemainingSeconds(10,.01,30,0)==20);
    /* Same successful recipe on a drive/host twice as slow or fast. */
    assert(VIMHostInstallPaceScale(40,20,0,-1,0)==2);
    assert(VIMHostInstallPaceScale(10,20,0,-1,0)==.5);
    /* A large measured first phase supplies pace before its completion. */
    assert(VIMHostInstallPaceScale(0,0,60,.25,120)==2);
    assert(VIMHostInstallPaceScale(0,0,15,.25,120)==.5);
    /* Estimated animation, tiny/absent history and unknown future time do
     * not become invented work or a fabricated whole-install estimate. */
    assert(VIMHostInstallPaceScale(0,0,60,-1,120)==1);
    assert(VIMHostInstallPaceScale(1,.5,0,-1,0)==1);
    assert(VIMHostInstallPaceScale(0,0,30,.5,0)==1);
    double pace=VIMHostInstallPaceScale(40,20,30,.5,30);
    assert(pace==2);
    assert(VIMHostInstallRemainingSeconds(30,.5,30*pace,60*pace)==150);
    assert(VIMHostInstallRemainingSeconds(30,.5,30*pace,-1)==-1);
    puts("PASS: weighted stage progress, observed-pace ETA, measured fractions and unknown future time");
    return 0;
}
