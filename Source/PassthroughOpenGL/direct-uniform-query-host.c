/* GL 2.1 GetUniform returns one scalar/vector/matrix ARRAY ELEMENT, at most
 * 16 components, not the entire array. Discover its type using real native
 * reflection; never infer adjacent uniform locations. Reference:
 * KhronosGroup/OpenGL-Refpages gl2.1/glGetUniform.xml. */
/* These core-3.2 sampler enums are in the target SDK's gl3.h, not its compatibility
 * gl.h/glext.h. Do not include both incompatible API declaration headers. */
enum {DG_SAMPLER_2D_MS=0x9108,DG_INT_SAMPLER_2D_MS=0x9109,DG_UINT_SAMPLER_2D_MS=0x910a,
    DG_SAMPLER_2D_MS_ARRAY=0x910b,DG_INT_SAMPLER_2D_MS_ARRAY=0x910c,DG_UINT_SAMPLER_2D_MS_ARRAY=0x910d};
static void DGForgetUniformShapes(struct DGHostContext *c,GLuint program)
{
    DGForgetAliasSamplers(c,program);
    if(!c->alias_group)return;
    for(unsigned i=0;i<256;++i){struct DGUniformShape *p=c->alias_group->uniform_shapes+i;
        if(!program||p->program==program)p->components=0;
    }
}
static unsigned DGUniformComponents(GLenum type)
{
    switch(type){
    case GL_FLOAT:case GL_INT:case GL_BOOL:case GL_UNSIGNED_INT:
    case GL_SAMPLER_1D:case GL_SAMPLER_2D:case GL_SAMPLER_3D:case GL_SAMPLER_CUBE:
    case GL_SAMPLER_1D_SHADOW:case GL_SAMPLER_2D_SHADOW:
    case GL_SAMPLER_2D_RECT_ARB:case GL_SAMPLER_2D_RECT_SHADOW_ARB:
    case GL_SAMPLER_1D_ARRAY_EXT:case GL_SAMPLER_2D_ARRAY_EXT:
    case GL_SAMPLER_1D_ARRAY_SHADOW_EXT:case GL_SAMPLER_2D_ARRAY_SHADOW_EXT:case GL_SAMPLER_CUBE_SHADOW_EXT:
    case GL_INT_SAMPLER_1D_EXT:case GL_INT_SAMPLER_2D_EXT:case GL_INT_SAMPLER_3D_EXT:case GL_INT_SAMPLER_CUBE_EXT:
    case GL_INT_SAMPLER_2D_RECT_EXT:case GL_INT_SAMPLER_1D_ARRAY_EXT:case GL_INT_SAMPLER_2D_ARRAY_EXT:
    case GL_UNSIGNED_INT_SAMPLER_1D_EXT:case GL_UNSIGNED_INT_SAMPLER_2D_EXT:case GL_UNSIGNED_INT_SAMPLER_3D_EXT:
    case GL_UNSIGNED_INT_SAMPLER_CUBE_EXT:case GL_UNSIGNED_INT_SAMPLER_2D_RECT_EXT:
    case GL_UNSIGNED_INT_SAMPLER_1D_ARRAY_EXT:case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY_EXT:
    case GL_SAMPLER_BUFFER_EXT:case GL_INT_SAMPLER_BUFFER_EXT:case GL_UNSIGNED_INT_SAMPLER_BUFFER_EXT:
    case DG_SAMPLER_2D_MS:case DG_INT_SAMPLER_2D_MS:case DG_UINT_SAMPLER_2D_MS:
    case DG_SAMPLER_2D_MS_ARRAY:case DG_INT_SAMPLER_2D_MS_ARRAY:case DG_UINT_SAMPLER_2D_MS_ARRAY:return 1;
    case GL_FLOAT_VEC2:case GL_INT_VEC2:case GL_BOOL_VEC2:case GL_UNSIGNED_INT_VEC2_EXT:return 2;
    case GL_FLOAT_VEC3:case GL_INT_VEC3:case GL_BOOL_VEC3:case GL_UNSIGNED_INT_VEC3_EXT:return 3;
    case GL_FLOAT_VEC4:case GL_INT_VEC4:case GL_BOOL_VEC4:case GL_UNSIGNED_INT_VEC4_EXT:case GL_FLOAT_MAT2:return 4;
    case GL_FLOAT_MAT2x3:case GL_FLOAT_MAT3x2:return 6;
    case GL_FLOAT_MAT2x4:case GL_FLOAT_MAT4x2:return 8;
    case GL_FLOAT_MAT3:return 9;
    case GL_FLOAT_MAT3x4:case GL_FLOAT_MAT4x3:return 12;
    case GL_FLOAT_MAT4:return 16;
    default:return 0;
    }
}
static unsigned DGUniformComponentCount(struct DGHostContext *c,GLuint program,GLint location)
{
    struct DGUniformShape *cached=&c->alias_group->uniform_shapes[(program*2654435761u^(uint32_t)location)&255u];
    if(cached->components&&cached->program==program&&cached->location==location)return cached->components;
    GLint active=0,maximum=0;glGetProgramiv(program,GL_ACTIVE_UNIFORMS,&active);
    glGetProgramiv(program,GL_ACTIVE_UNIFORM_MAX_LENGTH,&maximum);
    if(active<1||active>65536||maximum<1||maximum>65536)return 0;
    char *name=malloc((size_t)maximum),*candidate=malloc((size_t)maximum+16);unsigned result=0,searches=0;
    if(!name||!candidate)goto finished;
    for(GLuint i=0;i<(GLuint)active&&!result;++i){
        GLsizei length=0;GLint size=0;GLenum type=0;
        glGetActiveUniform(program,i,maximum,&length,&size,&type,name);
        if(length<0||length>=maximum||size<1||size>65536)goto finished;
        name[length]=0;unsigned components=DGUniformComponents(type);
        if(glGetUniformLocation(program,name)==location){result=components;break;}
        if(size==1||!components)continue;
        for(GLint element=1;element<size&&!result;++element){
            const char *index=strstr(name,"[0]");
            do {
                if(++searches>65536)goto finished;
                int used=index?snprintf(candidate,(size_t)maximum+16,"%.*s[%d]%s",(int)(index-name),name,element,index+3):
                    snprintf(candidate,(size_t)maximum+16,"%s[%d]",name,element);
                if(used<0||used>=maximum+16)goto finished;
                if(glGetUniformLocation(program,candidate)==location){result=components;break;}
                index=index?strstr(index+3,"[0]"):NULL;
            }while(index);
        }
    }
finished:
    free(name);free(candidate);
    if(result)*cached=(struct DGUniformShape){program,location,result};return result;
}
static int DGUniformQuery(struct DGHostContext *c,struct DGMessage *m,struct DGReply *reply)
{
    if(m->flags!=(DG_REPLY|DG_OUTPUT_ONLY)||m->bytes!=16*sizeof(GLfloat)||m->args[0]>UINT32_MAX||m->args[2]>1)return -1;
    if(m->args[1]!=(uint64_t)(int64_t)(GLint)m->args[1])return -1;
    for(unsigned i=4;i<sizeof(m->args)/sizeof(m->args[0]);++i)if(m->args[i])return -1;
    void *out=DGHostPointer(m,3,m->bytes);if(!out)return -1;
    if(c->inside_begin){reply->error=GL_INVALID_OPERATION;return 0;}
    GLuint program=(GLuint)m->args[0];GLint location=(GLint)m->args[1];
    union {GLfloat floating[16];GLint integer[16];} value={0};
    GLenum prior=glGetError();if(prior&&!c->error)c->error=prior;
    if(m->args[2])glGetUniformiv(program,location,value.integer);else glGetUniformfv(program,location,value.floating);
    reply->error=glGetError();if(reply->error)return 0;
    unsigned count=DGUniformComponentCount(c,program,location);GLenum reflected=glGetError();
    if(!count||count>16||reflected){reply->error=reflected?reflected:GL_INVALID_OPERATION;return 0;}
    memcpy(out,&value,count*sizeof(GLfloat));reply->result=count;return 0;
}
