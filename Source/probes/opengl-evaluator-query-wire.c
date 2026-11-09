/* Exercise the generated decoder against a native accelerated CGL context. */
#define main vim_evaluator_unused_host_main
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include "direct-opcodes.h"

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)

static void query(struct DGHostContext *context, unsigned op, GLenum target,
                  GLenum name, const void *expected, size_t bytes)
{
    struct { struct DGMessage message; uint64_t payload[16]; } packet={0};
    struct DGReply reply={0};
    int seeded=op==DG_OP_get_query_objecti64v || op==DG_OP_get_query_objectui64v;
    packet.message.op=op;
    packet.message.flags=DG_REPLY|(seeded?0:DG_OUTPUT_ONLY);
    packet.message.args[0]=target;
    packet.message.args[1]=name;
    packet.message.args[2]=1;
    packet.message.bytes=(uint32_t)bytes;
    memset(packet.payload,0xa5,sizeof(packet.payload));
    CHECK(DGHeaderValid(&packet.message));
    CHECK(DGExecute(context,&packet.message,&reply)==0);
    CHECK(reply.result==(seeded?0:bytes) && reply.error==GL_NO_ERROR);
    CHECK(context->error==GL_NO_ERROR && glGetError()==GL_NO_ERROR);
    CHECK(!memcmp(packet.payload,expected,bytes));
    const unsigned char *p=(const void *)packet.payload;
    for(size_t i=bytes;i<sizeof(packet.payload);++i)CHECK(p[i]==0xa5);

    memset(packet.payload,0xa5,sizeof(packet.payload));
    packet.message.bytes--;
    CHECK(DGExecute(context,&packet.message,&reply)==-1);
    for(size_t i=0;i<sizeof(packet.payload);++i)CHECK(p[i]==0xa5);
    packet.message.bytes++;
    packet.message.args[2]=2;
    CHECK(DGExecute(context,&packet.message,&reply)==-1);
    for(size_t i=0;i<sizeof(packet.payload);++i)CHECK(p[i]==0xa5);
}

static void upload(struct DGHostContext *c)
{
    struct { struct DGMessage message; GLfloat payload[6]; } p={0};
    struct DGReply reply={0};
    p.message.op=DG_OP_map1f;
    p.message.flags=DG_REPLY;
    p.message.args[0]=GL_MAP1_VERTEX_3;
    GLfloat end=1;memcpy(&p.message.args[2],&end,sizeof(end));
    p.message.args[3]=4;
    p.message.args[4]=2;
    p.message.args[15]=1;
    CHECK(DGExecute(c,&p.message,&reply)==0 && reply.result==1);
    p.message.bytes=1;
    CHECK(DGExecute(c,&p.message,&reply)==-1);
    p.message.flags=0;
    p.message.args[15]=0;
    p.message.args[5]=1;
    p.message.bytes=sizeof(p.payload);
    const GLfloat expected[]={8,7,6,5,4,3};
    memcpy(p.payload,expected,sizeof(expected));
    CHECK(DGExecute(c,&p.message,&reply)==-1); // noncanonical stride
    p.message.args[3]=3;
    p.message.bytes--;
    CHECK(DGExecute(c,&p.message,&reply)==-1);
    p.message.bytes++;
    p.message.args[5]=2;
    CHECK(DGExecute(c,&p.message,&reply)==-1); // offset into the first scalar
    p.message.args[5]=1;
    CHECK(DGExecute(c,&p.message,&reply)==0 && !c->error && !glGetError());
    query(c,DG_OP_get_mapfv,GL_MAP1_VERTEX_3,GL_COEFF,expected,sizeof(expected));
}

static void feedback_names(struct DGHostContext *c)
{
    GLuint program=glCreateProgram();
    struct {
        struct DGMessage message;
        char names[4];
    } p={.message={.op=DG_OP_transform_feedback_varyings_EXT,
        .bytes=4,.args={program,2,GL_INTERLEAVED_ATTRIBS_EXT}},.names={'a',0,'b',0}};
    struct DGReply reply={0};
    CHECK(DGExecute(c,&p.message,&reply)==0 && glGetError()==GL_NO_ERROR);
    p.message.bytes=3; // the second string is unterminated
    CHECK(DGExecute(c,&p.message,&reply)==-1);
    p.message.bytes=4;
    p.message.args[1]=1; // trailing unclaimed string
    CHECK(DGExecute(c,&p.message,&reply)==-1);
    p.message.args[1]=3; // fewer strings than the header declares
    CHECK(DGExecute(c,&p.message,&reply)==-1);
    p.message.args[1]=0;
    p.message.bytes=0;
    CHECK(DGExecute(c,&p.message,&reply)==0 && glGetError()==GL_NO_ERROR);
    glDeleteProgram(program);
}

static void feedback_extensions(void)
{
    const char *native="GL_EXT_draw_buffers2 GL_EXT_transform_feedback";
    uint64_t features=DG_FEATURE_INDEXED_STATE|DG_FEATURE_TRANSFORM_FEEDBACK_NAMES;
    char out[128]="";
    DGAppendIndexedExtensions(out,sizeof(out),native,1,features);
    CHECK(!out[0]); // Old guests can echo new host feature bits.
    DGAppendIndexedExtensions(out,sizeof(out),NULL,2,features);
    CHECK(!out[0]); // Core or a renderer without the native extension.
    DGAppendIndexedExtensions(out,sizeof(out),"GL_EXT_draw_buffers2_extra",2,features);
    CHECK(!out[0]);
    DGAppendIndexedExtensions(out,sizeof(out),native,2,DG_FEATURE_INDEXED_STATE);
    CHECK(!strcmp(out," GL_EXT_draw_buffers2"));
    out[0]=0;
    DGAppendIndexedExtensions(out,sizeof(out),native,2,features);
    CHECK(!strcmp(out," GL_EXT_draw_buffers2 GL_EXT_transform_feedback"));
}

// Both tested native GPUs have 32-bit elapsed and zero-bit timestamp
// counters. Exercise the decoder with a deterministic high-bit result too,
// without altering how the production executor obtains timer data.
static const GLuint64EXT wide_unsigned=UINT64_C(0xfedcba9876543210);
static const GLint64EXT wide_signed=-INT64_C(0x123456789abcdef);
static void synthetic_unsigned(GLuint id,GLenum pname,GLuint64EXT *value)
{ CHECK(id==7 && pname==GL_QUERY_RESULT);*value=wide_unsigned; }
static void synthetic_signed(GLuint id,GLenum pname,GLint64EXT *value)
{ CHECK(id==7 && pname==GL_QUERY_RESULT);*value=wide_signed; }
static void timer_reply_width(struct DGHostContext *c)
{
    struct DGCoreAPI original=dg_core_api;
    dg_core_api.unsigned_result=dg_core_api.unsigned_result_ext=synthetic_unsigned;
    dg_core_api.signed_result=dg_core_api.signed_result_ext=synthetic_signed;
    query(c,DG_OP_get_query_objectui64v,7,GL_QUERY_RESULT,&wide_unsigned,sizeof(wide_unsigned));
    query(c,DG_OP_get_query_objecti64v,7,GL_QUERY_RESULT,&wide_signed,sizeof(wide_signed));
    dg_core_api=original;
}
static void synthetic_uniform_indices(GLuint program,GLsizei count,const GLchar *const *names,GLuint *indices)
{
    CHECK(program==7 && count==2 && !strcmp(names[0],"a") && !strcmp(names[1],"b"));
    indices[0]=3;indices[1]=7;
}
static void synthetic_uniform_count(GLuint program,GLuint block,GLenum pname,GLint *count)
{
    CHECK(program==7 && block==1 && pname==0x8a42);
    *count=2;
}
static void uniform_block_bounds(struct DGHostContext *c)
{
    struct DGCoreAPI original=dg_core_api;
    dg_core_api.uniform_indices=synthetic_uniform_indices;
    dg_core_api.uniform_block_iv=synthetic_uniform_count;
    struct {struct DGMessage m;GLuint indices[2];char names[4];} list={
        .m={.op=DG_OP_get_uniform_indices,.flags=DG_REPLY,.bytes=12,.args={7,2,1}},
        .indices={~0u,~0u},.names={'a',0,'b',0}};
    struct DGReply r={0};
    --list.m.bytes;
    CHECK(DGHeaderValid(&list.m) && DGExecute(c,&list.m,&r)==-1);
    CHECK(list.indices[0]==~0u && list.indices[1]==~0u);
    ++list.m.bytes;
    CHECK(DGExecute(c,&list.m,&r)==0 && list.indices[0]==3 && list.indices[1]==7);
    struct {struct DGMessage m;GLint result;} range={
        .m={.op=DG_OP_get_active_uniform_blockiv,.flags=DG_REPLY,.bytes=4,.args={7,1,0x8a43,1,[14]=1}},
        .result=0x12345678};
    CHECK(DGExecute(c,&range.m,&r)==0 && c->error==GL_INVALID_OPERATION && range.result==0x12345678);
    c->error=0;
    range.m.args[14]=2; /* native count is correct but reserved payload is too short */
    CHECK(DGExecute(c,&range.m,&r)==-1 && range.result==0x12345678);
    dg_core_api=original;
}

static void pipeline_source_bounds(struct DGHostContext *c)
{
    struct { struct DGMessage m; char bytes[8]; } p={
        .m={.op=DG_OP_create_shader_programv,.flags=DG_REPLY|DG_INPUT_ONLY,
            .bytes=4,.args={GL_VERTEX_SHADER,2}},
        .bytes={'a',0,'b','c',0},
    };
    struct DGReply reply={0};
    CHECK(DGHeaderValid(&p.m));
    CHECK(DGExecute(c,&p.m,&reply)==-1); /* missing second terminator */
    p.m.bytes=6;
    CHECK(DGExecute(c,&p.m,&reply)==-1); /* trailing bytes after both strings */
    p.m.bytes=4;p.m.args[1]=UINT64_MAX;
    CHECK(DGExecute(c,&p.m,&reply)==-1); /* invalid 32-bit count encoding */
    p.m.args[1]=DG_FEEDBACK_NAME_LIMIT+1u;
    CHECK(DGExecute(c,&p.m,&reply)==-1); /* count exceeds allocation bound */
    CHECK(!reply.result && !c->error && !glGetError());
}

static void fp64_reply_width(struct DGHostContext *c)
{
    struct { struct DGMessage m; GLdouble out[16]; } p={
        .m={.op=DG_UNIFORM_QUERY,.flags=DG_REPLY|DG_OUTPUT_ONLY,
            .bytes=16*sizeof(GLfloat),.args={0,0,DG_UNIFORM_DOUBLE,1}},
    };
    struct DGReply reply={0};
    CHECK(DGUniformQuery(c,&p.m,&reply)==-1); /* double cannot use old width */
    p.m.bytes=sizeof(p.out);p.m.args[2]=DG_UNIFORM_FLOAT;
    CHECK(DGUniformQuery(c,&p.m,&reply)==-1); /* don't accept oversized replies */
    p.m.args[2]=DG_UNIFORM_DOUBLE;p.m.args[3]=2;
    CHECK(DGUniformQuery(c,&p.m,&reply)==-1); /* offset can't leave a short span */
    CHECK(!reply.result && !c->error && !glGetError());
}

static void indexed_blit_preserves_scissor(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,0};
    CGLPixelFormatObj format=NULL;GLint n=0;
    CGLContextObj previous=CGLGetCurrentContext();
    struct DGHostContext c={.profile=kCGLOGLPVersion_3_2_Core,.indexed_scissor=1};
    CHECK(CGLChoosePixelFormat(attrs,&format,&n)==kCGLNoError && format);
    if(!format)return;
    CHECK(CGLCreateContext(format,NULL,&c.cgl)==kCGLNoError && c.cgl);
    CGLReleasePixelFormat(format);
    if(!c.cgl)return;
    CHECK(CGLSetCurrentContext(c.cgl)==kCGLNoError);
    pthread_once(&dg_core_once,DGLoadCoreAPI);
    GLboolean (*enabled)(GLenum,GLuint)=dlsym(RTLD_DEFAULT,"glIsEnabledi");
    CHECK(enabled && dg_core_api.enable_indexed && dg_core_api.disable_indexed);
    if(enabled && dg_core_api.enable_indexed && dg_core_api.disable_indexed){
        dg_core_api.enable_indexed(GL_SCISSOR_TEST,0);
        dg_core_api.enable_indexed(GL_SCISSOR_TEST,1);
        DGSetBlitScissor(&c,GL_FALSE);
        CHECK(!enabled(GL_SCISSOR_TEST,0) && enabled(GL_SCISSOR_TEST,1));
        dg_core_api.disable_indexed(GL_SCISSOR_TEST,1);
        DGSetBlitScissor(&c,GL_TRUE);
        CHECK(enabled(GL_SCISSOR_TEST,0) && !enabled(GL_SCISSOR_TEST,1) && !glGetError());
    }
    CGLSetCurrentContext(previous);CGLReleaseContext(c.cgl);
}

static void private_purgeability(struct DGHostContext *c)
{
    if(!DGCreateAliases(c,NULL)){CHECK(0);return;}
    c->depth_bits=24;
    if(!DGSelectDrawableMode(c,1,16,16,0)){CHECK(0);DGDestroyAliases(c);return;}
    int saved_support=c->purgeability;
    c->purgeability=1; /* exercise the private-name guard even on an unadvertised host */
    GLuint buffer=0;glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    glBufferData(GL_ARRAY_BUFFER,16,NULL,GL_STATIC_DRAW);
    struct {struct DGMessage m;GLint out;} public={0};
    public.m.op=DG_OP_get_object_parameteriv_APPLE;
    public.m.flags=DG_REPLY;public.m.bytes=sizeof(public.out);
    public.m.args[0]=GL_BUFFER_OBJECT_APPLE;public.m.args[1]=buffer;
    public.m.args[2]=GL_PURGEABLE_APPLE;public.m.args[3]=1;public.out=-9;
    struct DGReply queried={0};
    CHECK(DGExecute(c,&public.m,&queried)==0 && public.out==GL_FALSE && glGetError()==GL_NO_ERROR);
    glDeleteBuffers(1,&buffer);
    const GLenum types[]={GL_TEXTURE,GL_RENDERBUFFER_EXT};
    const GLuint names[]={c->color,c->depth};
    for(unsigned i=0;i<2;++i){
        CHECK(names[i] && DGPurgeablePrivateName(c,types[i],names[i]));
        struct {struct DGMessage m;GLint out;} p={0};
        p.m.op=DG_OP_get_object_parameteriv_APPLE;
        p.m.flags=DG_REPLY;p.m.bytes=sizeof(p.out);
        p.m.args[0]=types[i];p.m.args[1]=names[i];p.m.args[2]=GL_PURGEABLE_APPLE;p.m.args[3]=1;
        p.out=-9;struct DGReply r={0};
        CHECK(DGExecute(c,&p.m,&r)==0 && p.out==-9 && c->error==GL_INVALID_VALUE);
        c->error=GL_NO_ERROR;
        p.m.op=DG_OP_object_purgeable_APPLE;
        p.m.args[2]=GL_VOLATILE_APPLE;p.m.args[3]=0;p.m.bytes=0;
        CHECK(DGExecute(c,&p.m,&r)==0 && c->error==GL_INVALID_VALUE);
        c->error=GL_NO_ERROR;
        glGetObjectParameterivAPPLE(types[i],names[i],GL_PURGEABLE_APPLE,&p.out);
        CHECK(p.out==GL_FALSE && glGetError()==GL_NO_ERROR);
    }
    c->purgeability=saved_support;
    DGDestroyDrawables(c);DGDestroyAliases(c);
}
static void imaging_transfer_bounds(struct DGHostContext *c)
{
    const GLfloat row[]={0.5f,1.0f},column[]={0.25f,0.5f,0.75f};
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glSeparableFilter2D(GL_SEPARABLE_2D,GL_LUMINANCE,2,3,GL_LUMINANCE,GL_FLOAT,row,column);
    CHECK(glGetError()==GL_NO_ERROR);
    struct {
        struct DGMessage m;
        unsigned char pixels[32];
    } p={.m={.op=DG_OP_get_separable_filter,.bytes=19,
             .args={GL_SEPARABLE_2D,GL_RGBA,GL_UNSIGNED_BYTE,1,9}}};
    struct DGReply reply={0};
    memset(p.pixels,0xa5,sizeof(p.pixels));
    CHECK(DGExecute(c,&p.m,&reply)==-1); /* column needs 12 bytes after offset 8 */
    for(unsigned i=0;i<sizeof(p.pixels);++i)CHECK(p.pixels[i]==0xa5);
    p.m.bytes=20;p.m.args[3]=UINT64_MAX;
    CHECK(DGExecute(c,&p.m,&reply)==-1);
    for(unsigned i=0;i<sizeof(p.pixels);++i)CHECK(p.pixels[i]==0xa5);
    p.m.args[3]=1;p.m.args[15]=1; /* no real pack buffer binding */
    CHECK(DGExecute(c,&p.m,&reply)==0 && c->error==GL_INVALID_OPERATION);
    c->error=GL_NO_ERROR;p.m.args[15]=0;
    CHECK(DGExecute(c,&p.m,&reply)==0 && glGetError()==GL_NO_ERROR);
    CHECK(p.pixels[0]!=0xa5 && p.pixels[19]!=0xa5 && p.pixels[20]==0xa5);
    /* Compact packets are sized by actual pixels, not the original guest
     * prefix. Reject malformed pointers before changing native store state. */
    glPixelStorei(GL_PACK_SKIP_ROWS,17);glPixelStorei(GL_PACK_SKIP_PIXELS,23);
    glPixelStorei(GL_PACK_ROW_BYTES_APPLE,4096);
    p.m.args[15]=2;p.m.bytes=19;
    memset(p.pixels,0xa5,sizeof(p.pixels));
    CHECK(DGExecute(c,&p.m,&reply)==-1);
    for(unsigned i=0;i<sizeof(p.pixels);++i)CHECK(p.pixels[i]==0xa5);
    GLint value=0;glGetIntegerv(GL_PACK_SKIP_ROWS,&value);CHECK(value==17);
    glGetIntegerv(GL_PACK_SKIP_PIXELS,&value);CHECK(value==23);
    glGetIntegerv(GL_PACK_ROW_BYTES_APPLE,&value);CHECK(value==4096);
    p.m.bytes=20;p.m.args[3]=UINT64_MAX;
    CHECK(DGExecute(c,&p.m,&reply)==-1);
    p.m.args[3]=1;
    CHECK(DGExecute(c,&p.m,&reply)==0 && c->error==GL_NO_ERROR && glGetError()==GL_NO_ERROR);
    CHECK(p.pixels[0]!=0xa5 && p.pixels[19]!=0xa5 && p.pixels[20]==0xa5);
    glGetIntegerv(GL_PACK_SKIP_ROWS,&value);CHECK(value==17);
    glGetIntegerv(GL_PACK_SKIP_PIXELS,&value);CHECK(value==23);
    glGetIntegerv(GL_PACK_ROW_BYTES_APPLE,&value);CHECK(value==4096);
    p.m.args[15]=3;CHECK(DGExecute(c,&p.m,&reply)==-1);
    GLuint buffer=0;glGenBuffers(1,&buffer);glBindBuffer(GL_PIXEL_PACK_BUFFER,buffer);
    glBufferData(GL_PIXEL_PACK_BUFFER,256,NULL,GL_STREAM_READ);
    p.m.args[15]=2;
    CHECK(DGExecute(c,&p.m,&reply)==0 && c->error==GL_INVALID_OPERATION);
    c->error=GL_NO_ERROR;
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glDeleteBuffers(1,&buffer);
    glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    glPixelStorei(GL_PACK_ROW_BYTES_APPLE,0);
    glPixelStorei(GL_PACK_ALIGNMENT,4);
}

static void compact_texture_bounds(struct DGHostContext *c)
{
    GLint saved[7];for(unsigned i=0;i<7;++i)glGetIntegerv(dg_compact_unpack_keys[i],saved+i);
    const GLint original[]={8,11,17,23,4096,7,2};
    for(unsigned i=0;i<7;++i)glPixelStorei(dg_compact_unpack_keys[i],original[i]);
    struct {struct DGMessage m;unsigned char pixels[16];} p={
        .m={.op=DG_OP_tex_image1D,.bytes=11,
            .args={GL_TEXTURE_1D,0,GL_RGBA8,3,0,GL_RGBA,GL_UNSIGNED_BYTE,1}}};
    p.m.args[13]=12;p.m.args[15]=2;
    for(unsigned i=0;i<16;++i)p.pixels[i]=(unsigned char)(i*13+7);
    struct DGReply reply={0};
    CHECK(DGExecute(c,&p.m,&reply)==-1);
    p.m.bytes=12;p.m.args[13]=11;CHECK(DGExecute(c,&p.m,&reply)==-1);
    p.m.args[13]=12;p.m.args[14]=1;CHECK(DGExecute(c,&p.m,&reply)==-1);
    p.m.args[14]=0;p.m.args[7]=UINT64_MAX;CHECK(DGExecute(c,&p.m,&reply)==-1);
    p.m.args[7]=0;CHECK(DGExecute(c,&p.m,&reply)==-1);
    p.m.args[7]=1;p.m.args[15]=3;CHECK(DGExecute(c,&p.m,&reply)==-1);
    for(unsigned i=0;i<7;++i){GLint value=-1;glGetIntegerv(dg_compact_unpack_keys[i],&value);CHECK(value==original[i]);}
    p.m.args[15]=2;
    CHECK(DGExecute(c,&p.m,&reply)==0 && c->error==GL_NO_ERROR && glGetError()==GL_NO_ERROR);
    unsigned char actual[12]={0};glGetTexImage(GL_TEXTURE_1D,0,GL_RGBA,GL_UNSIGNED_BYTE,actual);
    CHECK(!memcmp(actual,p.pixels,sizeof(actual)) && glGetError()==GL_NO_ERROR);
    for(unsigned i=0;i<7;++i){GLint value=-1;glGetIntegerv(dg_compact_unpack_keys[i],&value);CHECK(value==original[i]);}
    GLuint buffer=0;glGenBuffers(1,&buffer);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffer);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,32,NULL,GL_STREAM_DRAW);
    CHECK(DGExecute(c,&p.m,&reply)==0 && c->error==GL_INVALID_OPERATION);c->error=GL_NO_ERROR;
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glDeleteBuffers(1,&buffer);
    /* A failed redefinition must retain the imported-drawable registration.
     * EndAliasRedefinition needs the native error before store restoration. */
    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,3,2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    struct DGAliasGroup group={0};group.aliases=calloc(1,sizeof(*group.aliases));
    CHECK(group.aliases!=NULL);if(group.aliases){
        group.aliases->texture=texture;
        struct DGHostContext alias_context=*c;alias_context.alias_group=&group;
        struct {struct DGMessage m;unsigned char pixels[24];} a={
            .m={.op=DG_OP_tex_image2D,.bytes=24,
                .args={GL_TEXTURE_RECTANGLE_ARB,0,GL_NONE,3,2,0,GL_RGBA,GL_UNSIGNED_BYTE,1}}};
        a.m.args[13]=12;a.m.args[15]=2;
        CHECK(DGExecute(&alias_context,&a.m,&reply)==0 && alias_context.error!=GL_NO_ERROR);
        CHECK(group.aliases!=NULL);
        alias_context.error=GL_NO_ERROR;a.m.args[2]=GL_RGBA8;
        CHECK(DGExecute(&alias_context,&a.m,&reply)==0 && alias_context.error==GL_NO_ERROR);
        CHECK(group.aliases==NULL);
        free(group.aliases);
    }
    glDeleteTextures(1,&texture);
    for(unsigned i=0;i<7;++i)glPixelStorei(dg_compact_unpack_keys[i],saved[i]);
}
int main(void)
{
    CHECK(DGMapQueryBytes(GL_MAP1_VERTEX_3,GL_COEFF,2,0,sizeof(GLfloat))==24);
    CHECK(DGMapQueryBytes(GL_MAP2_TEXTURE_COORD_4,GL_COEFF,3,2,sizeof(GLdouble))==192);
    CHECK(DGMapQueryBytes(GL_MAP2_VERTEX_4,GL_COEFF,INT32_MAX,INT32_MAX,sizeof(GLdouble))==SIZE_MAX);
    CHECK(DGMapQueryBytes(GL_MAP2_VERTEX_4,GL_COEFF,2,0,sizeof(GLdouble))==SIZE_MAX);
    CHECK(DGMapQueryBytes(GL_TEXTURE_2D,GL_ORDER,0,0,sizeof(GLint))==SIZE_MAX);
    CHECK(DGMapQueryBytes(GL_MAP1_NORMAL,GL_DOMAIN,0,0,SIZE_MAX)==SIZE_MAX);

    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_Legacy,0};
    CGLPixelFormatObj format=NULL;
    GLint count=0;
    struct DGHostContext context={0};
    if(CGLChoosePixelFormat(attrs,&format,&count) || !format)return 2;
    if(CGLCreateContext(format,NULL,&context.cgl) || !context.cgl)return 2;
    CGLReleasePixelFormat(format);
    if(CGLSetCurrentContext(context.cgl))return 2;
    context.profile=kCGLOGLPVersion_Legacy;
    printf("native evaluator renderer: %s\n",glGetString(GL_RENDERER));
    compact_texture_bounds(&context);

    const GLfloat curve[]={1,2,3,99,4,5,6};
    glMap1f(GL_MAP1_VERTEX_3,-2,4,4,2,curve);
    const GLfloat curve_coeff[]={1,2,3,4,5,6};
    const GLint curve_order[]={2};
    query(&context,DG_OP_get_mapiv,GL_MAP1_VERTEX_3,GL_ORDER,curve_order,sizeof(curve_order));
    query(&context,DG_OP_get_mapfv,GL_MAP1_VERTEX_3,GL_COEFF,curve_coeff,sizeof(curve_coeff));

    const GLdouble surface[]={1,2,3,4,5,6,7,8,9,10,11,12};
    glMap2d(GL_MAP2_TEXTURE_COORD_2,-3,4,6,2,-5,6,2,3,surface);
    const GLint surface_order[]={2,3};
    const GLdouble surface_domain[]={-3,4,-5,6};
    query(&context,DG_OP_get_mapiv,GL_MAP2_TEXTURE_COORD_2,GL_ORDER,surface_order,sizeof(surface_order));
    query(&context,DG_OP_get_mapdv,GL_MAP2_TEXTURE_COORD_2,GL_DOMAIN,surface_domain,sizeof(surface_domain));
    query(&context,DG_OP_get_mapdv,GL_MAP2_TEXTURE_COORD_2,GL_COEFF,surface,sizeof(surface));
    upload(&context);
    feedback_names(&context);
    feedback_extensions();
    timer_reply_width(&context);
    uniform_block_bounds(&context);
    pipeline_source_bounds(&context);
    fp64_reply_width(&context);
    indexed_blit_preserves_scissor();
    private_purgeability(&context);
    imaging_transfer_bounds(&context);

    struct DGMessage invalid={.op=DG_OP_get_mapfv,.args={GL_TEXTURE_2D,GL_COEFF}};
    struct DGReply reply={0};
    CHECK(DGExecute(&context,&invalid,&reply)==0 && reply.error==GL_INVALID_ENUM);
    reply.error=GL_NO_ERROR;
    context.inside_begin=1;
    CHECK(DGExecute(&context,&invalid,&reply)==0 && reply.error==GL_INVALID_OPERATION);
    CGLSetCurrentContext(NULL);
    CGLReleaseContext(context.cgl);
    puts(failures?"FAIL evaluator query wire":"PASS evaluator values, bounds, and native-error guard");
    return failures?1:0;
}
