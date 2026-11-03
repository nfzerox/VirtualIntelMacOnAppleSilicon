/* Finite metadata-only native/owned GLI survey. All values are API arguments,
 * not private structure offsets. Force integrated Intel for a physical run.
 * Change wallpaper after READY to regenerate the menu's small drawable. */
#pragma D option quiet
BEGIN { printf("READY GLI surface parameters\n"); }
pid$target:GLEngine:gliSetInteger:entry
{ @keys[arg1]=count(); }
pid$target:GLEngine:gliSetInteger:entry
/arg2 && (arg1==298 || arg1==304)/
{
    this->a=(int32_t *)copyin(arg2,arg1==298?12:8);
    printf("%llu set key=%u context=%p values=%d,%d\n",(unsigned long long)timestamp,
        arg1,arg0,this->a[0],this->a[1]);
}
pid$target:GLEngine:gliSetInteger:entry
/arg2 && (arg1==200 || arg1==201 || arg1==305 || arg1==306)/
{ printf("%llu set key=%u context=%p value=%d\n",(unsigned long long)timestamp,
    arg1,arg0,*(int32_t *)copyin(arg2,4)); }
pid$target:GLEngine:gliAttachDrawableWithOptions:entry
/arg2 && (arg1==0x50 || arg1==0x5a)/
{
    this->a=(int32_t *)copyin(arg2,12);
    @attachments[arg1,arg0,this->a[0],this->a[1],this->a[2]]=count();
}
tick-12sec { exit(0); }
END { printf("set keys\n"); printa(@keys);
    printf("attachments type context record calls\n"); printa(@attachments); }
