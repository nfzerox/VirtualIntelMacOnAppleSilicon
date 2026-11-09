/* Standalone client for the loopback research executor. No installed
 * OpenGL bundle or VM changes. The legacy context begins with default maps. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include <sys/mman.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)

static void uploads(struct DGContext *c)
{
    size_t page=(size_t)getpagesize();
    unsigned char *storage=mmap(NULL,8*page,PROT_NONE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(storage==MAP_FAILED){CHECK(0);return;}
    for(unsigned i=0;i<4;++i){
        if(mprotect(storage+2*i*page,page,PROT_READ|PROT_WRITE)){
            CHECK(0);munmap(storage,8*page);return;
        }
    }
    GLint fstride=(GLint)(2*page/sizeof(GLfloat));
    GLint dstride=(GLint)(2*page/sizeof(GLdouble));
    GLfloat values[]={1,2,3,4,5,6,7,8,9,10,11,12};
    for(unsigned i=0;i<4;++i)memcpy(storage+2*i*page,values+3*i,3*sizeof(GLfloat));
    glMap1f(GL_MAP1_VERTEX_3,-2,4,fstride,2,(const GLfloat *)storage);
    glMap2f(GL_MAP2_VERTEX_3,-3,4,2*fstride,2,-5,6,fstride,2,(const GLfloat *)storage);
    GLfloat coeff[13]={0};coeff[12]=99;
    glGetMapfv(GL_MAP2_VERTEX_3,GL_COEFF,coeff);
    CHECK(!memcmp(coeff,values,sizeof(values)) && coeff[12]==99);
    memset(coeff,0,sizeof(coeff));coeff[6]=99;
    glGetMapfv(GL_MAP1_VERTEX_3,GL_COEFF,coeff);
    CHECK(!memcmp(coeff,values,6*sizeof(GLfloat)) && coeff[6]==99);

    /* The first parameter can be the shorter stride, too. */
    const GLdouble expected[]={1,2,3,7,8,9,4,5,6,10,11,12};
    for(unsigned i=0;i<4;++i){
        GLdouble *point=(void *)(storage+2*i*page);
        for(unsigned j=0;j<3;++j)point[j]=values[3*i+j];
    }
    glMap1d(GL_MAP1_VERTEX_3,-2,4,dstride,2,(const GLdouble *)storage);
    glMap2d(GL_MAP2_VERTEX_3,-3,4,dstride,2,-5,6,2*dstride,2,(const GLdouble *)storage);
    GLdouble dcoeff[13]={0};dcoeff[12]=99;
    glGetMapdv(GL_MAP2_VERTEX_3,GL_COEFF,dcoeff);
    CHECK(!memcmp(dcoeff,expected,sizeof(expected)) && dcoeff[12]==99);
    GLint order[2]={0};
    glGetMapiv(GL_MAP2_VERTEX_3,GL_ORDER,order);
    CHECK(order[0]==2 && order[1]==2 && glGetError()==GL_NO_ERROR);

    const void *unreadable=storage+page;
    glMap1f(GL_MAP2_VERTEX_3,0,1,3,1,unreadable);
    CHECK(glGetError()==GL_INVALID_ENUM);
    glMap1f(GL_MAP1_VERTEX_3,1,1,3,1,unreadable);
    CHECK(glGetError()==GL_INVALID_VALUE);
    glMap1f(GL_MAP1_VERTEX_3,0,1,2,1,unreadable);
    CHECK(glGetError()==GL_INVALID_VALUE);
    glMap1f(GL_MAP1_VERTEX_3,0,1,3,INT32_MAX,unreadable);
    CHECK(glGetError()==GL_INVALID_VALUE);
    glActiveTexture(GL_TEXTURE1);
    glMap1f(GL_MAP1_TEXTURE_COORD_2,0,1,2,1,values);
    glActiveTexture(GL_TEXTURE0);
    GLfloat texcoords[]={0,0,99};
    glGetMapfv(GL_MAP1_TEXTURE_COORD_2,GL_COEFF,texcoords);
    CHECK(texcoords[0]==1 && texcoords[1]==2 && texcoords[2]==99);
    CHECK(glGetError()==GL_NO_ERROR);
    glBegin(GL_POINTS);
    glMap1f(GL_MAP1_VERTEX_3,0,1,3,1,unreadable);
    glEnd();
    CHECK(glGetError()==GL_INVALID_OPERATION);
    c->connection->features&=~DG_FEATURE_EVALUATOR_UPLOADS;
    glMap1f(GL_MAP1_VERTEX_3,0,1,3,1,unreadable);
    CHECK(glGetError()==GL_INVALID_OPERATION);
    memset(coeff,0,sizeof(coeff));coeff[6]=99;
    glGetMapfv(GL_MAP1_VERTEX_3,GL_COEFF,coeff);
    CHECK(!memcmp(coeff,values,6*sizeof(GLfloat)) && coeff[6]==99);
    CHECK(glGetError()==GL_NO_ERROR && !c->connection->dead);
    munmap(storage,8*page);
}

int main(void)
{
    unsigned char pixels[16*16*4]={0};
    struct DGContext *c=DGCreateContext(NULL,NULL);
    if(!c || !DGMakeCurrent(c,pixels,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(c->connection->features&DG_FEATURE_EVALUATOR_QUERIES);
    GLint order[]={-1,-1,99};
    glGetMapiv(GL_MAP2_VERTEX_3,GL_ORDER,order);
    CHECK(order[0]==1 && order[1]==1 && order[2]==99);
    GLdouble domain[]={-1,-1,-1,-1,99};
    glGetMapdv(GL_MAP2_VERTEX_3,GL_DOMAIN,domain);
    CHECK(domain[0]==0 && domain[1]==1 && domain[2]==0 && domain[3]==1 && domain[4]==99);
    GLfloat coeff[]={-1,-1,-1,99};
    glGetMapfv(GL_MAP2_VERTEX_3,GL_COEFF,coeff);
    CHECK(coeff[0]==0 && coeff[1]==0 && coeff[2]==0 && coeff[3]==99);
    CHECK(glGetError()==GL_NO_ERROR);

    glEnable(0xffffffffu);
    order[0]=-1;
    glGetMapiv(GL_MAP2_VERTEX_3,GL_ORDER,order);
    CHECK(order[0]==1 && order[1]==1 && order[2]==99);
    CHECK(glGetError()==GL_INVALID_ENUM && glGetError()==GL_NO_ERROR);

    glGetMapiv(GL_TEXTURE_2D,GL_ORDER,order+2);
    CHECK(order[2]==99 && glGetError()==GL_INVALID_ENUM);
    glBegin(GL_POINTS);
    glGetMapdv(GL_MAP2_VERTEX_3,GL_DOMAIN,domain+4);
    glEnd();
    CHECK(domain[4]==99 && glGetError()==GL_INVALID_OPERATION);
    CHECK(glGetError()==GL_NO_ERROR && !c->connection->dead);
    CHECK(c->connection->features&DG_FEATURE_EVALUATOR_UPLOADS);
    uploads(c);
    c->connection->features&=~DG_FEATURE_EVALUATOR_QUERIES;
    glGetMapiv(GL_MAP2_VERTEX_3,GL_ORDER,order);
    CHECK(order[0]==1 && order[1]==1 && glGetError()==GL_INVALID_OPERATION);
    CHECK(!c->connection->dead);
    DGDestroyContext(c);
    puts(failures?"FAIL evaluator client":"PASS negotiated evaluator round trip and unchanged guards");
    return failures?1:0;
}
