/* Same destination-dependent linear blend seen in the native Mavericks CA
 * header path. Compare a distinct GPU copy with the live private import;
 * the fixed-function identity feedback probe alone cannot cover this case.
 * Only owned GL test drawables are written/read. No desktop capture.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static GLuint VIMFeedbackShader(GLenum,const char *);
/* Compare a neighborhood read through the native private destination import
 * with an independent GPU-copy control. Existing identity/same-pixel tests
 * cannot detect an alias feedback hazard across fragments. Use the measured
 * private key-300 barrier, not a presumed native NV extension implementation.
 * This is opt-in evidence: qualify the native Intel outcome before treating
 * equality as a required private-driver contract.
 */
static int VIMNeighborhoodFeedback(GLIContext context,
    CGLError (*barrier)(GLIContext,unsigned,const GLint *),GLuint live,int width,int height)
{
    GLuint vs=VIMFeedbackShader(GL_VERTEX_SHADER,
        "#version 120\nvarying vec2 uv;void main(){gl_Position=gl_Vertex;uv=gl_MultiTexCoord0.xy;}");
    GLuint fs=VIMFeedbackShader(GL_FRAGMENT_SHADER,
        "#version 120\n#extension GL_ARB_texture_rectangle : enable\n"
        "uniform sampler2DRect image;varying vec2 uv;void main(){"
        "gl_FragColor=(texture2DRect(image,uv+vec2(-17.,0.))+"
        "texture2DRect(image,uv)+texture2DRect(image,uv+vec2(11.,0.)))/3.;}");
    if(!vs||!fs)return 0;
    GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)return 0;
    size_t bytes=(size_t)width*height*4;unsigned char *control=malloc(bytes),*actual=malloc(bytes);
    if(!control||!actual){free(control);free(actual);return 0;}
    GLuint copy=0;glGenTextures(1,&copy);int good=1;
    for(unsigned alias=0;alias<2;++alias){
        glUseProgram(0);glActiveTexture(GL_TEXTURE0);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
        glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);glViewport(0,0,width,height);
        glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glEnable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        for(int x=0;x<width;x+=8){
            unsigned stripe=(unsigned)x/8;
            glScissor(x,0,width-x<8?width-x:8,height);
            glClearColor((stripe*47%251)/255.f,(stripe*97%251)/255.f,(stripe*173%251)/255.f,1);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glDisable(GL_SCISSOR_TEST);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,alias?live:copy);
        if(!alias)glCopyTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,0,0,width,height,0);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        GLint zero=0;CGLError ce=barrier(context,300,&zero);good=!ce&&good;
        glUseProgram(program);glUniform1i(glGetUniformLocation(program,"image"),0);
        glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(width,0);glVertex2f(1,-1);
        glTexCoord2f(width,height);glVertex2f(1,1);glTexCoord2f(0,height);glVertex2f(-1,1);glEnd();
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,alias?actual:control);
        GLenum error=glGetError();good=!error&&good;
        fprintf(stderr,"neighborhood alias=%u barrier=%d error=%x\n",alias,ce,error);
    }
    unsigned different=0,maximum=0;
    for(size_t i=0;i<bytes;++i){unsigned d=abs((int)control[i]-(int)actual[i]);different+=d>1;if(d>maximum)maximum=d;}
    fprintf(stderr,"%s private in-place neighborhood versus GPU-copy control: differing-channels=%u/%zu maximum=%u\n",
        good&&!different?"PASS":"FAIL",different,bytes,maximum);
    glUseProgram(0);glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);glDeleteTextures(1,&copy);
    free(control);free(actual);return good&&!different;
}
static int VIMQueuedAliasFeedback(GLuint texture,int width,int height)
{
    GLuint vs=0,fs=0,program=0;
    if(getenv("VIM_GL_QUEUED_GLSL")) {
        vs=VIMFeedbackShader(GL_VERTEX_SHADER,"#version 120\nvarying vec2 uv;void main(){gl_Position=ftransform();uv=gl_MultiTexCoord0.xy;}");
        fs=VIMFeedbackShader(GL_FRAGMENT_SHADER,"#version 120\n#extension GL_ARB_texture_rectangle : enable\nuniform sampler2DRect image;varying vec2 uv;void main(){gl_FragColor=texture2DRect(image,uv);}");
        if(!vs || !fs)return 0;
        program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
        GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)return 0;
        glUseProgram(program);glUniform1i(glGetUniformLocation(program,"image"),0);
    }
    glUseProgram(0);glActiveTexture(GL_TEXTURE0);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);glViewport(0,0,width,height);
    glDisable(GL_TEXTURE_2D);glDisable(GL_TEXTURE_RECTANGLE_ARB);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearColor(1,0,1,.5f);glClear(GL_COLOR_BUFFER_BIT);glFinish();
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glColor4ub(0,255,255,64);glBegin(GL_QUADS);
    glVertex2f(-.5f,-.5f);glVertex2f(.5f,-.5f);glVertex2f(.5f,.5f);glVertex2f(-.5f,.5f);glEnd();
    const char *sync=getenv("VIM_VIEW_SYNC");
    if(sync && !strcmp(sync,"flush"))glFlush();
    else if(sync && !strcmp(sync,"barrier"))glTextureBarrierNV();
    else if(sync && !strcmp(sync,"finish"))glFinish();
    if(program)glUseProgram(program);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    int arrays=getenv("VIM_GL_QUEUED_ARRAYS")!=NULL;
    if(arrays) {
        const GLfloat positions[]={-1,-1,1,-1,1,1,-1,1};
        const GLfloat coordinates[]={0,0,width,0,width,height,0,height};
        glBindBuffer(GL_ARRAY_BUFFER,0);glVertexPointer(2,GL_FLOAT,0,positions);
        glClientActiveTexture(GL_TEXTURE0);glTexCoordPointer(2,GL_FLOAT,0,coordinates);
        glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glDrawArrays(GL_QUADS,0,4);glDisableClientState(GL_TEXTURE_COORD_ARRAY);glDisableClientState(GL_VERTEX_ARRAY);
    } else {
        glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(width,0);glVertex2f(1,-1);
        glTexCoord2f(width,height);glVertex2f(1,1);glTexCoord2f(0,height);glVertex2f(-1,1);glEnd();
    }
    unsigned char pixel[4]={0};glReadPixels(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    GLenum error=glGetError();int good=!error && !pixel[0] && pixel[1]==255 && pixel[2]==255 && pixel[3]==64;
    fprintf(stderr,"%s queued geometry then private in-place alias arrays=%d glsl=%d sync=%s RGBA=%u,%u,%u,%u error=%x\n",
        good?"PASS":"FAIL",arrays,program!=0,sync?sync:"none",pixel[0],pixel[1],pixel[2],pixel[3],error);
    glUseProgram(0);if(program)glDeleteProgram(program);if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);return good;
}
static GLuint VIMFeedbackShader(GLenum type,const char *source)
{
    GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
    GLint compiled=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&compiled);
    if(!compiled) { char log[1024]={0};glGetShaderInfoLog(shader,sizeof(log),NULL,log);fprintf(stderr,"Feedback shader: %s\n",log);glDeleteShader(shader);return 0; }
    return shader;
}
static int VIMLinearFeedback(GLuint live,int width,int height)
{
    int projected=getenv("VIM_GL_FEEDBACK_PROJECTED")!=NULL;
    int arrays=projected || getenv("VIM_GL_FEEDBACK_ARRAYS")!=NULL;
    const char *vertex="#version 120\nvarying vec2 uv;void main(){gl_Position=gl_Vertex;uv=gl_MultiTexCoord0.xy;}";
    if(arrays)vertex="#version 120\nattribute vec4 position;attribute vec2 input_uv;varying vec2 uv;"
        "void main(){gl_Position=position;uv=input_uv;}";
    if(projected)vertex="#version 120\nattribute vec4 position;attribute vec2 input_uv;"
        "uniform mat4 vertex_matrix;uniform vec4 texmatD;varying vec2 uv;varying vec3 dest;"
        "void main(){gl_Position=vertex_matrix*position;uv=input_uv;"
        "dest=vec3(gl_Position.xy*texmatD.xy+texmatD.zw*gl_Position.w,gl_Position.w);}";
    const char *fragment="#version 120\n#extension GL_ARB_texture_rectangle : enable\n"
        "uniform sampler2D mask;uniform sampler2DRect destination;uniform vec2 extent;varying vec2 uv;"
        "vec4 mixLin(vec4 D,vec4 S,vec4 M){D.rgb=D.rgb/max(D.a,.005);D.rgb=D.rgb*D.rgb;"
        "S.rgb=S.rgb/max(S.a,.005);S.rgb=S.rgb*S.rgb;vec4 R=mix(D,S,M);"
        "R.rgb=R.rgb*inversesqrt(max(R.rgb,.005))*R.a;return R;}"
        "void main(){vec4 D=texture2DRect(destination,uv*extent);vec4 M=texture2D(mask,uv);"
        "gl_FragColor=mixLin(D,vec4(.462745,.509804,.568627,1.),M);}";
    if(projected)fragment="#version 120\n#extension GL_ARB_texture_rectangle : enable\n"
        "uniform sampler2D mask;uniform sampler2DRect destination;varying vec2 uv;varying vec3 dest;"
        "vec4 mixLin(vec4 D,vec4 S,vec4 M){D.rgb=D.rgb/max(D.a,.005);D.rgb=D.rgb*D.rgb;"
        "S.rgb=S.rgb/max(S.a,.005);S.rgb=S.rgb*S.rgb;vec4 R=mix(D,S,M);"
        "R.rgb=R.rgb*inversesqrt(max(R.rgb,.005))*R.a;return R;}"
        "void main(){vec4 D=texture2DRectProj(destination,dest);vec4 M=texture2D(mask,uv);"
        "gl_FragData[0]=mixLin(D,vec4(.462745,.509804,.568627,1.),M);}";
    GLuint vs=VIMFeedbackShader(GL_VERTEX_SHADER,vertex),fs=VIMFeedbackShader(GL_FRAGMENT_SHADER,fragment);
    if(!vs || !fs) { if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return 0; }
    GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);
    glBindAttribLocationARB((GLhandleARB)(uintptr_t)program,0,"position");
    glBindAttribLocationARB((GLhandleARB)(uintptr_t)program,3,"input_uv");glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(!linked) { char log[1024]={0};glGetProgramInfoLog(program,sizeof(log),NULL,log);fprintf(stderr,"Feedback link: %s\n",log); }
    GLuint mask=0,copy=0;glGenTextures(1,&mask);glGenTextures(1,&copy);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,mask);
    GLint swap=0,row=0,skip=0,skiprows=0;
    glGetIntegerv(GL_UNPACK_SWAP_BYTES,&swap);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skip);glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skiprows);
    glPixelStorei(GL_UNPACK_SWAP_BYTES,GL_FALSE);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
    const unsigned char pixels[]={0,0,0,0,255,255,255,255};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_SWAP_BYTES,swap);glPixelStorei(GL_UNPACK_ROW_LENGTH,row);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,skip);glPixelStorei(GL_UNPACK_SKIP_ROWS,skiprows);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
    glViewport(0,0,width,height);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,copy);
    glCopyTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,0,0,width,height,0);
    int good=linked!=0;
    GLhandleARB handle=(GLhandleARB)(uintptr_t)program;
    glUseProgram(program);glUniform1iARB(glGetUniformLocationARB(handle,"mask"),0);
    glUniform1iARB(glGetUniformLocationARB(handle,"destination"),1);glUniform2fARB(glGetUniformLocationARB(handle,"extent"),width,height);
    if(projected) {
        // CA's measured world-to-view matrix and projected destination lookup,
        // including a Y flip and non-fullscreen quad, rather than identity UV.
        const GLfloat matrix[]={2.f/width,0,0,0,0,-2.f/height,0,0,0,0,0,0,-1,1,0,1};
        glUniformMatrix4fvARB(glGetUniformLocationARB(handle,"vertex_matrix"),1,GL_FALSE,matrix);
        glUniform4fARB(glGetUniformLocationARB(handle,"texmatD"),width*.5f,height*.5f,width*.5f,height*.5f);
    }
    for(unsigned alias=0;alias<2;++alias) {
        if(getenv("VIM_GL_FEEDBACK_NO_REBIND")) {
            glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,alias?live:copy);
            glClear(GL_COLOR_BUFFER_BIT);
        } else {
            glClear(GL_COLOR_BUFFER_BIT);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,alias?live:copy);
        }
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        if(alias && getenv("VIM_GL_TEXTURE_BARRIER"))glTextureBarrierNV();
        if(arrays) {
            struct Vertex { GLfloat position[4];GLubyte color0[4],color1[4];GLfloat uv[2],uv1[2],uv2[2],normal[3];uint32_t pad; };
            struct Vertex vertices[4]={0};
            const GLfloat coordinates[][4]={{-1,-1,0,0},{1,-1,1,0},{1,1,1,1},{-1,1,0,1}};
            for(unsigned i=0;i<4;++i) { vertices[i].position[0]=coordinates[i][0];vertices[i].position[1]=coordinates[i][1];
                vertices[i].position[3]=1;vertices[i].uv[0]=coordinates[i][2];vertices[i].uv[1]=coordinates[i][3]; }
            if(projected)for(unsigned i=0;i<4;++i) {
                vertices[i].position[0]=width*(.125f+.75f*coordinates[i][2]);
                vertices[i].position[1]=height*(.25f+.5f*coordinates[i][3]);
            }
            glBindBuffer(GL_ARRAY_BUFFER,0);
            // The same seven interleaved attribute shapes/64-byte stride as
            // the measured CA header draw, not only a fixed-function quad.
            const void *pointers[]={vertices[0].position,vertices[0].color0,vertices[0].color1,vertices[0].uv,
                vertices[0].uv1,vertices[0].uv2,vertices[0].normal};
            const GLint sizes[]={4,4,4,2,2,2,3};
            for(unsigned i=0;i<7;++i) {
                glVertexAttribPointerARB(i,sizes[i],i==1||i==2?GL_UNSIGNED_BYTE:GL_FLOAT,i==1||i==2,sizeof(*vertices),pointers[i]);
                glEnableVertexAttribArrayARB(i);
            }
            glDrawArrays(GL_QUADS,0,4);
            for(unsigned i=0;i<7;++i)glDisableVertexAttribArrayARB(i);
        } else {
            glBegin(GL_QUADS);glMultiTexCoord2f(GL_TEXTURE0,0,0);glVertex2f(-1,-1);
            glMultiTexCoord2f(GL_TEXTURE0,1,0);glVertex2f(1,-1);
            glMultiTexCoord2f(GL_TEXTURE0,1,1);glVertex2f(1,1);
            glMultiTexCoord2f(GL_TEXTURE0,0,1);glVertex2f(-1,1);glEnd();
        }
        unsigned char left[4]={0},right[4]={0};glReadPixels(width/4,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,left);
        glReadPixels(width*3/4,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,right);GLenum error=glGetError();
        int pass=!error && !left[0] && left[1]==255 && !left[2] && left[3]==255 &&
            abs((int)right[0]-118)<=1 && abs((int)right[1]-130)<=1 && abs((int)right[2]-145)<=1 && right[3]==255;
        fprintf(stderr,"%s GLSL linear destination blend alias=%u arrays=%d projected=%d barrier=%d left=%u,%u,%u,%u right=%u,%u,%u,%u error=%x\n",
            pass?"PASS":"FAIL",alias,arrays,projected,getenv("VIM_GL_TEXTURE_BARRIER")!=NULL,
            left[0],left[1],left[2],left[3],right[0],right[1],right[2],right[3],error);
        good=pass && good;
    }
    glUseProgram(0);glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,0);glDeleteTextures(1,&mask);glDeleteTextures(1,&copy);return good;
}
