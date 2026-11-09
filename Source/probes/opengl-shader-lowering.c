/* Portable source-adapter controls. These do not open a GL context. */
#include "../PassthroughOpenGL/passthrough-opengl-shader-lowering.h"
int main(int argc,char **argv)
{
    if(argc==2){
        FILE *file=fopen(argv[1],"rb");if(!file)return 2;
        if(fseek(file,0,SEEK_END)){fclose(file);return 2;}long size=ftell(file);
        if(size<0||fseek(file,0,SEEK_SET)){fclose(file);return 2;}
        char *source=malloc((size_t)size+1);if(!source){fclose(file);return 2;}
        size_t n=fread(source,1,(size_t)size,file);int error=ferror(file);fclose(file);
        if(error||n!=(size_t)size){free(source);return 2;}source[n]=0;
        char *lowered=NULL;size_t length=0;int result=DGLowerShaderSource(source,n,&lowered,&length);
        if(result<0){free(source);return 2;}
        if(result)fwrite(lowered,1,length,stdout);else fwrite(source,1,n,stdout);
        free(lowered);free(source);return 0;
    }
    static const struct {const char *text;int expected;const char *line;} tests[]={
        {"#version 400 core\nvoid main(){double x=roundEven(1.5);}",1,"#line 2\n"},
        {"// header\n/* another */ #version 410\nvoid main(){double x=roundEven(1.5);}",1,"#line 3\n"},
        {"/* header\ncontinues */ #version 400\nvoid main(){double x=roundEven(1.5);}",1,"#line 3\n"},
        {"#version 400\ndouble f(double x){return roundEven(x);}",1,"#line 2\n"},
        {"#version 400\nvoid main(){if(true){}else roundEven(1.5);}",1,"#line 2\n"},
        {"#version 330\nvoid main(){float x=roundEven(1.5);}",0,NULL},
        {"#version 150\n#extension GL_ARB_gpu_shader_fp64 : enable\nvoid main(){double x=roundEven(1.5);}",1,"#line 3\n"},
        {"#version 330 core\n#extension GL_ARB_gpu_shader_fp64 : require\nvoid main(){double x=determinant(dmat3(1));}",1,"#line 3\n"},
        {"#version 150\n#extension GL_ARB_gpu_shader_fp64 : warn\nvoid main(){double x=roundEven(1.5);}",1,"#line 3\n"},
        {"#version 150\n#extension /* comment */ GL_ARB_gpu_shader_fp64 : enable // comment\nvoid main(){double x=roundEven(1.5);}",1,"#line 3\n"},
        {"#version 150\n#extension GL_ARB_gpu_shader_fp64 : disable\nvoid main(){float x=roundEven(1.5);}",0,NULL},
        {"#version 150\n#extension GL_ARB_gpu_shader_fp64 : enable\n#extension all : disable\nvoid main(){float x=roundEven(1.5);}",0,NULL},
        {"#version 150\n#extension all : disable\n#extension GL_ARB_gpu_shader_fp64 : enable\nvoid main(){double x=roundEven(1.5);}",1,"#line 4\n"},
        {"#version 150\n#if 0\n#extension GL_ARB_gpu_shader_fp64 : enable\n#endif\nvoid main(){float x=roundEven(1.5);}",0,NULL},
        {"#version 150\n#extension GL_ARB_gpu_shader_fp64 : enable\n#if 0\n#extension GL_ARB_gpu_shader_fp64 : disable\n#endif\nvoid main(){double x=roundEven(1.5);}",0,NULL},
        {"#version 140\n#extension GL_ARB_gpu_shader_fp64 : enable\nvoid main(){double x=roundEven(1.5);}",0,NULL},
        {"void main(){float x=roundEven(1.5);}",0,NULL},
        {"#version 400\n// roundEven(1.5)\nvoid main(){}",0,NULL},
        {"#version 400\n/* roundEven(1.5) */ void main(){}",0,NULL},
        {"#version 400\nvoid main(){double x=my_roundEven(1.5);}",0,NULL},
        {"#version 400\ndouble roundEven(double x){return x;}void main(){double x=roundEven(1.5);}",0,NULL},
        {"#version 400\ndouble roundEven(double);void main(){double x=roundEven(1.5);}",0,NULL},
        {"#version 400\n#define RN roundEven\nvoid main(){double x=RN(1.5);}",0,NULL},
        {"#version 400\n#define roundEven(x) custom(x)\nvoid main(){double x=roundEven(1.5);}",0,NULL},
        {"#version 400\n#include <functions>\nvoid main(){double x=roundEven(1.5);}",0,NULL},
        {"#version 400\n#define NAME(a,b) a ## b\nvoid main(){double x=roundEven(1.5);}",0,NULL},
        {"#version 400\ndouble rou\\\nndEven(double x){return x;}void main(){double x=roundEven(1.5);}",0,NULL},
        {"#version 400\nvoid main(){\"roundEven\";roundEven(1.5);}",0,NULL},
        {"#version 400",0,NULL},
        {"#version 400\nvoid main(){double x=determinant(dmat3(1));}",1,"#line 2\n"},
        {"#version 400\nvoid main(){double x=determinant(dmat4(roundEven(1.5)));}",1,"#line 2\n"},
        {"#version 400\n#define det determinant\nvoid main(){double x=det(dmat3(1));}",0,NULL},
        {"#version 400\ndouble determinant(dmat3 m){return 1;}void main(){double x=determinant(dmat3(1));}",0,NULL},
        {"#version 400\n#extension GL_ARB_gpu_shader_fp64 : enable\nvoid main(){double x=roundEven(1.5);}",1,"#line 3\n"},
        {"#version 400\ndouble vim_passthrough_determinant;void main(){double x=determinant(dmat3(1));}",0,NULL},
        {"#version 400\n#define helper vim_passthrough_determinant\nvoid main(){double x=determinant(dmat3(1));}",0,NULL}
    };
    int failures=0;
    for(unsigned i=0;i<sizeof(tests)/sizeof(*tests);++i){
        char *lowered=NULL;size_t length=0;int result=DGLowerShaderSource(tests[i].text,strlen(tests[i].text),&lowered,&length);
        if(result!=tests[i].expected||(result&&(!lowered||strlen(lowered)!=length||
            !strstr(lowered,tests[i].line)||(!strstr(lowered,"void main")&&!strstr(lowered,"double f"))))){
            fprintf(stderr,"FAIL case=%u result=%d expected=%d\n",i,result,tests[i].expected);++failures;
        }
        free(lowered);
    }
    printf("shader lowering cases=%lu failures=%d\n",(unsigned long)(sizeof(tests)/sizeof(*tests)),failures);
    return failures?1:0;
}
