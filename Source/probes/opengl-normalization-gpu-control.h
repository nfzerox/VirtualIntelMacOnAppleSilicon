/* Explicit owned-context algorithm control, not a production state adapter.
 * The fixture owns program/array/TF state and has no active user queries or
 * feedback. Production integration must preserve those states and handle
 * display-list compilation before using this path. No CPU buffer readback.
 */
static struct {GLuint program,input,output;GLint scale,bias;} gpu_conversion;
static int gpu_conversion_create(void)
{
    const char *sources[]={
        "#version 120\nattribute vec4 rawValue;uniform vec4 scale,bias;varying vec4 converted;"
        "void main(){converted=(rawValue*(vec4(1)+bias)+bias)/scale;gl_Position=vec4(0,0,0,1);}",
        "#version 120\nvoid main(){gl_FragColor=vec4(1);}"};
    GLuint object=glCreateProgram();
    for(unsigned i=0;i<2;++i){
        GLuint shader=glCreateShader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);
        glShaderSource(shader,1,sources+i,NULL);glCompileShader(shader);
        GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
        if(!ok){char log[2048];GLsizei n=0;glGetShaderInfoLog(shader,sizeof(log),&n,log);
            fprintf(stderr,"GPU normalization shader: %.*s\n",(int)n,log);glDeleteShader(shader);glDeleteProgram(object);return 0;}
        glAttachShader(object,shader);glDeleteShader(shader);
    }
    /* Compatibility DrawArrays needs a vertex/attribute-zero stream even
     * when the shader computes a constant gl_Position. A nonzero attribute
     * alone silently captures no vertices on native Metal GL. */
    glBindAttribLocation(object,0,"rawValue");
    const char *varying="converted";glTransformFeedbackVaryingsEXT(object,1,&varying,GL_INTERLEAVED_ATTRIBS_EXT);
    glLinkProgram(object);GLint linked=0;glGetProgramiv(object,GL_LINK_STATUS,&linked);
    if(!linked){char log[2048];GLsizei n=0;glGetProgramInfoLog(object,sizeof(log),&n,log);
        fprintf(stderr,"GPU normalization link: %.*s\n",(int)n,log);glDeleteProgram(object);return 0;}
    gpu_conversion.program=object;gpu_conversion.scale=glGetUniformLocation(object,"scale");
    gpu_conversion.bias=glGetUniformLocation(object,"bias");
    glGenBuffers(1,&gpu_conversion.input);glGenBuffers(1,&gpu_conversion.output);
    return gpu_conversion.scale>=0&&gpu_conversion.bias>=0&&gpu_conversion.input&&gpu_conversion.output&&!glGetError();
}
static GLuint gpu_convert(GLenum type,unsigned components,GLuint buffer,const void *data)
{
    if(!gpu_conversion.program&&!gpu_conversion_create())return 0;
    unsigned bytes=type==GL_BYTE?1:2;
    if(!buffer){buffer=gpu_conversion.input;glBindBuffer(GL_ARRAY_BUFFER,buffer);
        glBufferData(GL_ARRAY_BUFFER,3*components*bytes,data,GL_STREAM_DRAW);}
    else glBindBuffer(GL_ARRAY_BUFFER,buffer);
    glVertexAttribPointer(0,components,type,GL_FALSE,0,NULL);glEnableVertexAttribArray(0);
    GLfloat scale[]={1,1,1,1},bias[]={0,0,0,0};GLfloat maximum=type==GL_BYTE?255:65535;
    for(unsigned i=0;i<components;++i){scale[i]=maximum;bias[i]=1;}
    glUseProgram(gpu_conversion.program);glUniform4fv(gpu_conversion.scale,1,scale);glUniform4fv(gpu_conversion.bias,1,bias);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,gpu_conversion.output);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,3*4*sizeof(GLfloat),NULL,GL_STREAM_DRAW);
    glBindBufferBaseEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,0,gpu_conversion.output);
    glEnable(GL_RASTERIZER_DISCARD_EXT);glBeginTransformFeedbackEXT(GL_POINTS);
    glDrawArrays(GL_POINTS,0,3);glEndTransformFeedbackEXT();glDisable(GL_RASTERIZER_DISCARD_EXT);
    glBindBufferBaseEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,0,0);glDisableVertexAttribArray(0);
    GLenum error=glGetError();if(error){fprintf(stderr,"GPU conversion error=%x\n",(unsigned)error);return 0;}
    return gpu_conversion.output;
}
static void gpu_conversion_destroy(void)
{
    if(gpu_conversion.program)glDeleteProgram(gpu_conversion.program);
    if(gpu_conversion.input)glDeleteBuffers(1,&gpu_conversion.input);
    if(gpu_conversion.output)glDeleteBuffers(1,&gpu_conversion.output);
}
