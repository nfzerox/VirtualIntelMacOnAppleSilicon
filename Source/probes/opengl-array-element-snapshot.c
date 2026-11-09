/* Native executor ownership/bounds controls for PowerPC VBO ArrayElement. */
#define main vim_snapshot_unused_host_main
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
static unsigned checks,failures;
static void check(int ok,const char *label)
{++checks;failures+=!ok;printf("%s %s\n",ok?"PASS":"FAIL",label);}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;struct DGHostContext c={.profile=kCGLOGLPVersion_Legacy};GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c.cgl)||CGLSetCurrentContext(c.cgl))return 2;CGLDestroyPixelFormat(pf);
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    GLuint buffer=0;glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    unsigned char raw[96];memset(raw,0xa5,sizeof(raw));
    for(unsigned i=0;i<4;++i){GLfloat values[]={3.0f+i,7.0f+i};DGSwapVertexComponents(raw+8+24*i,values,1,2,4,0);}
    glBufferData(GL_ARRAY_BUFFER,sizeof(raw),raw,GL_DYNAMIC_DRAW);glBindBuffer(GL_ARRAY_BUFFER,0);
    struct {struct DGMessage message;struct DGVertexArray array;} packet={0};
    packet.message.args[0]=1;packet.message.args[1]=1;packet.message.args[2]=1;
    packet.message.bytes=sizeof(packet.array);
    packet.array=(struct DGVertexArray){.slot=8,.size=2,.type=GL_FLOAT,.stride=24,.buffer=buffer,.pointer=8};
    check(!DGHostPrepareArrayElement(&c,&packet.message)&&c.element_vbo_limit==4&&c.element_snapshot_vertices==4,
        "full preparation snapshots the validated vertex range");
    check(c.element_snapshot_count==1&&!memcmp(c.element_snapshots[0].data,raw+8,8)&&
        !memcmp(c.element_snapshots[0].data+24,raw+80,8),"snapshot packs components without stride padding");
    GLint binding=-1;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&binding);check(binding==0,"preparation restores array buffer binding");
    glBindBuffer(GL_ARRAY_BUFFER,buffer);GLint mapped=-1;glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_MAPPED,&mapped);
    check(!mapped,"no native mapping survives preparation");
    memset(raw,0,sizeof(raw));glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(raw),raw);glBindBuffer(GL_ARRAY_BUFFER,0);
    struct DGMessage element={.args={3,0,0,1,1}};
    check(!DGHostArrayElement(&c,&element),"prepared element executes");GLfloat value[4]={0};glGetFloatv(GL_CURRENT_TEXTURE_COORDS,value);
    check(value[0]==6&&value[1]==10&&value[2]==0&&value[3]==1,"owned snapshot is independent of mutable VBO storage");
    DGHostReleaseArrayElements(&c);check(!c.element_snapshot_count&&!c.element_vbo_limit&&!c.element_snapshot_vertices,
        "snapshot retirement clears ownership and range");
    packet.message.args[3]=2;
    check(!DGHostPrepareArrayElement(&c,&packet.message)&&c.element_snapshot_vertices==1&&c.element_snapshot_first==1,
        "outside-Begin preparation copies only the requested vertex");
    element.args[0]=0;check(!DGHostArrayElement(&c,&element)&&c.error==GL_INVALID_OPERATION,"index before narrow snapshot is rejected");c.error=0;
    element.args[0]=2;check(!DGHostArrayElement(&c,&element)&&c.error==GL_INVALID_OPERATION,"index after narrow snapshot is rejected");c.error=0;
    element.args[0]=1;check(!DGHostArrayElement(&c,&element)&&!c.error,"requested narrow element is accepted");
    DGHostReleaseArrayElements(&c);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    void *caller=glMapBuffer(GL_ARRAY_BUFFER,GL_READ_ONLY);check(caller!=NULL,"caller map created");
    check(!DGHostPrepareArrayElement(&c,&packet.message)&&!c.element_vbo_limit&&!c.element_snapshot_count,
        "already-mapped source is not snapshotted");
    glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_MAPPED,&mapped);check(mapped,"caller mapping is not unmapped by failed preparation");
    if(caller)glUnmapBuffer(GL_ARRAY_BUFFER);
    packet.message.args[3]=0;packet.array.pointer=sizeof(raw)-4;
    check(!DGHostPrepareArrayElement(&c,&packet.message)&&!c.element_vbo_limit,"truncated component range is rejected before mapping");
    packet.message.args[2]=2;check(DGHostPrepareArrayElement(&c,&packet.message)==-1,"malformed byte order is rejected");
    packet.message.args[2]=0;packet.message.args[3]=1;
    check(DGHostPrepareArrayElement(&c,&packet.message)==-1,"narrow snapshot cannot be requested for native arrays");
    DGHostReleaseArrayElements(&c);glDeleteBuffers(1,&buffer);
    check(!glGetError(),"ownership and error controls leave native GL clean");
    CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
    printf("array-snapshot checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
