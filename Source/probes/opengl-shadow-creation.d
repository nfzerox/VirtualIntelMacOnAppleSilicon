/* Finite, read-only native compositor/GLI observation. Run as guest root:
 * dtrace -Z -q -s opengl-shadow-creation.d -p WINDOWSERVER_PID
 * Create/activate/resize an ordinary window during the twenty-second interval.
 * No pixel data, private structure offsets, environment changes or hot logging. */
#pragma D option quiet
dtrace:::BEGIN {budget=32;printf("READY native window-shadow creation\n");}
pid$target:SkyLight:*CreateShadowFrom*:entry {self->shadow=timestamp;@calls[probefunc]=count();}
pid$target:SkyLight:*CreateShadowFrom*:return /self->shadow/ {
    @result[probefunc,arg1==0]=count();
    @microseconds[probefunc]=sum((timestamp-self->shadow)/1000);
    self->shadow=0;
}
pid$target:SkyLight:CGXWindowIsShadowed:return {@shadowed[arg1!=0]=count();}
pid$target:SkyLight:*GetTextureForShadow*:return {@textures[probefunc,arg1==0]=count();}
pid$target:GLEngine:gliSetInteger:entry {self->setting=1;self->key=arg1;}
pid$target:GLEngine:gliSetInteger:entry /arg1==0x38e && budget>0/ {
    printf("IOSURFACE id=%u target=%x internal=%x size=%u,%u format=%x type=%x plane=%u\n",
        *(uint32_t*)copyin(arg2,4),*(uint32_t*)copyin(arg2+4,4),*(uint32_t*)copyin(arg2+8,4),
        *(uint32_t*)copyin(arg2+12,4),*(uint32_t*)copyin(arg2+16,4),
        *(uint32_t*)copyin(arg2+20,4),*(uint32_t*)copyin(arg2+24,4),*(uint32_t*)copyin(arg2+28,4));
    budget--;
}
pid$target:GLEngine:gliSetInteger:return /self->setting/ {
    @settings[self->key,arg1]=count();self->setting=0;
}
pid$target:GLEngine:gliGetInteger:entry {self->getting=1;self->getkey=arg1;}
pid$target:GLEngine:gliGetInteger:return /self->getting/ {
    @queries[self->getkey,arg1]=count();self->getting=0;
}
tick-20s {exit(0);}
dtrace:::END {
    printf("END native window-shadow creation\n");
    printa("MASK %s calls=%@u\n",@calls);
    printa("MASK_RESULT %s null=%u calls=%@u\n",@result);
    printa("MASK_TIME %s total_us=%@u\n",@microseconds);
    printa("SHADOW_ENABLED %u calls=%@u\n",@shadowed);
    printa("TEXTURE %s null=%u calls=%@u\n",@textures);
    printa("GLI_SETTING key=%u error=%u calls=%@u\n",@settings);
    printa("GLI_QUERY key=%u error=%u calls=%@u\n",@queries);
}
