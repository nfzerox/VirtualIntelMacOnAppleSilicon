/* Our GL backend's compile-time adaptation, not an Apple driver patch.
 * Token-aware and deliberately leaves ambiguous preprocessor/user-overload
 * cases unchanged. No app/test-name matching, environment flags or draw work. */
#ifndef VIM_PASSTHROUGH_OPENGL_SHADER_LOWERING_H
#define VIM_PASSTHROUGH_OPENGL_SHADER_LOWERING_H
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

static const char dg_glsl_roundeven[]=
    "\ndouble roundEven(double value){\n"
    "double lower=floor(value),fraction=value-lower,lowerHalf=lower*0.5;\n"
    "if(fraction>0.5||(fraction==0.5&&lowerHalf!=floor(lowerHalf)))lower+=1.0;\n"
    "return lower==0.0?value*0.0:lower;\n}\n"
    "dvec2 roundEven(dvec2 v){return dvec2(roundEven(v.x),roundEven(v.y));}\n"
    "dvec3 roundEven(dvec3 v){return dvec3(roundEven(v.x),roundEven(v.y),roundEven(v.z));}\n"
    "dvec4 roundEven(dvec4 v){return dvec4(roundEven(v.x),roundEven(v.y),roundEven(v.z),roundEven(v.w));}\n";
/* Cofactor expansion stays double throughout, unlike the backend's broken
 * native dmat3/dmat4 operation. Keep the 2x2 overload for the minors too. */
static const char dg_glsl_determinant[]=
    "\nfloat vim_passthrough_determinant(mat2 m){return determinant(m);}\n"
    "float vim_passthrough_determinant(mat3 m){return determinant(m);}\n"
    "float vim_passthrough_determinant(mat4 m){return determinant(m);}\n"
    "double vim_passthrough_determinant(dmat2 m){return m[0][0]*m[1][1]-m[1][0]*m[0][1];}\n"
    "double vim_passthrough_determinant(dmat3 m){\n"
    "return m[0][0]*(m[1][1]*m[2][2]-m[2][1]*m[1][2])"
    "-m[1][0]*(m[0][1]*m[2][2]-m[2][1]*m[0][2])"
    "+m[2][0]*(m[0][1]*m[1][2]-m[1][1]*m[0][2]);}\n"
    "double vim_passthrough_determinant(dmat4 m){\n"
    "double a01=m[0][0]*m[1][1]-m[1][0]*m[0][1],"
    "a02=m[0][0]*m[2][1]-m[2][0]*m[0][1],"
    "a03=m[0][0]*m[3][1]-m[3][0]*m[0][1],"
    "a12=m[1][0]*m[2][1]-m[2][0]*m[1][1],"
    "a13=m[1][0]*m[3][1]-m[3][0]*m[1][1],"
    "a23=m[2][0]*m[3][1]-m[3][0]*m[2][1];\n"
    "double b01=m[0][2]*m[1][3]-m[1][2]*m[0][3],"
    "b02=m[0][2]*m[2][3]-m[2][2]*m[0][3],"
    "b03=m[0][2]*m[3][3]-m[3][2]*m[0][3],"
    "b12=m[1][2]*m[2][3]-m[2][2]*m[1][3],"
    "b13=m[1][2]*m[3][3]-m[3][2]*m[1][3],"
    "b23=m[2][2]*m[3][3]-m[3][2]*m[2][3];\n"
    "return a01*b23-a02*b13+a03*b12+a12*b03-a13*b02+a23*b01;}\n";
static const char dg_glsl_determinant_name[]="vim_passthrough_determinant";

static int DGSLIdentifier(unsigned char c)
{return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';}
static int DGSLSpace(unsigned char c)
{return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\v'||c=='\f';}
struct DGSLToken {size_t begin,end;unsigned line;int kind;};
struct DGSLLexer {const char *text;size_t length,offset;unsigned line;int line_start;};
enum {DG_SL_IDENTIFIER=1,DG_SL_PUNCTUATION,DG_SL_DIRECTIVE,DG_SL_OPAQUE};
static void DGSLAdvance(struct DGSLLexer *l)
{
    unsigned char c=l->text[l->offset++];
    if(c=='\n'){++l->line;l->line_start=1;}
    else if(!DGSLSpace(c))l->line_start=0;
}
static int DGSLNext(struct DGSLLexer *l,struct DGSLToken *token)
{
    while(l->offset<l->length){
        size_t p=l->offset;
        if(DGSLSpace(l->text[p])){DGSLAdvance(l);continue;}
        if(p+1<l->length&&l->text[p]=='/'&&l->text[p+1]=='/'){
            while(l->offset<l->length&&l->text[l->offset]!='\n')DGSLAdvance(l);continue;
        }
        if(p+1<l->length&&l->text[p]=='/'&&l->text[p+1]=='*'){
            int line_start=l->line_start;unsigned start_line=l->line;DGSLAdvance(l);DGSLAdvance(l);
            while(l->offset+1<l->length&&!(l->text[l->offset]=='*'&&l->text[l->offset+1]=='/'))DGSLAdvance(l);
            if(l->offset+1>=l->length){l->offset=l->length;break;}
            DGSLAdvance(l);DGSLAdvance(l);l->line_start=l->line!=start_line||line_start;continue;
        }
        *token=(struct DGSLToken){p,p,l->line,DG_SL_PUNCTUATION};
        if(l->text[p]=='#'&&l->line_start){
            token->kind=DG_SL_DIRECTIVE;
            do {DGSLAdvance(l);}while(l->offset<l->length&&l->text[l->offset-1]!='\n');
        }else if(DGSLIdentifier(l->text[p])){
            token->kind=DG_SL_IDENTIFIER;
            do {DGSLAdvance(l);}while(l->offset<l->length&&DGSLIdentifier(l->text[l->offset]));
        }else if(l->text[p]=='\''||l->text[p]=='"'){
            /* Quoted tokens are not ordinary desktop GLSL expressions. */
            token->kind=DG_SL_OPAQUE;DGSLAdvance(l);
        }else DGSLAdvance(l);
        token->end=l->offset;return 1;
    }
    return 0;
}
static int DGSLTokenIs(const char *source,struct DGSLToken t,const char *word)
{size_t n=strlen(word);return t.end-t.begin==n&&!memcmp(source+t.begin,word,n);}
static int DGSLDirectiveHas(const char *source,struct DGSLToken t,const char *word)
{
    size_t n=strlen(word);
    for(size_t i=t.begin;i+n<=t.end;++i)if(!memcmp(source+i,word,n)&&
        (i==t.begin||!DGSLIdentifier(source[i-1]))&&(i+n==t.end||!DGSLIdentifier(source[i+n])))return 1;
    return 0;
}
/* FP64 predates GLSL400 through ARB_gpu_shader_fp64 (GLSL150+). Accept
 * literal unconditional header directives, not an inferred active #if branch.
 * 0: unrelated, 1: enabled, -1: disabled, 2: ambiguous/invalid directive. */
static int DGSLFp64Extension(const char *source,struct DGSLToken directive)
{
    struct DGSLLexer lexer={source,directive.end,directive.begin+1,directive.line,0};
    struct DGSLToken token={0};
    if(!DGSLNext(&lexer,&token)||!DGSLTokenIs(source,token,"extension")||
       !DGSLNext(&lexer,&token))return 0;
    int all=DGSLTokenIs(source,token,"all");
    if(!all&&!DGSLTokenIs(source,token,"GL_ARB_gpu_shader_fp64"))return 0;
    if(!DGSLNext(&lexer,&token)||!DGSLTokenIs(source,token,":")||
       !DGSLNext(&lexer,&token))return 2;
    int enabled=DGSLTokenIs(source,token,"enable")||DGSLTokenIs(source,token,"require")||
        DGSLTokenIs(source,token,"warn");
    int disabled=DGSLTokenIs(source,token,"disable");
    if(DGSLNext(&lexer,&token)||(!enabled&&!disabled))return 2;
    if(all&&!disabled)return 2;
    return disabled?-1:1;
}
/* Called only after the declaration/macro checks below. Rewrite call tokens,
 * not comments, substrings, variables, preprocessor directives or type names. */
static size_t DGSLDeterminantCalls(const char *source,size_t length,size_t begin,char *out)
{
    struct DGSLLexer lexer={source,length,begin,1,1};struct DGSLToken token={0},name={0};
    size_t copied=begin,written=0;int pending=0;
    while(DGSLNext(&lexer,&token)){
        if(pending&&DGSLTokenIs(source,token,"(")){
            size_t n=name.begin-copied;memcpy(out+written,source+copied,n);written+=n;
            memcpy(out+written,dg_glsl_determinant_name,sizeof(dg_glsl_determinant_name)-1);
            written+=sizeof(dg_glsl_determinant_name)-1;copied=name.end;
        }
        pending=token.kind==DG_SL_IDENTIFIER&&DGSLTokenIs(source,token,"determinant");
        if(pending)name=token;
    }
    memcpy(out+written,source+copied,length-copied);return written+length-copied;
}
/* 1: allocated adapted source, 0: unchanged, -1: allocation/size failure.
 * Caller retains the original native source and restores it after compilation.
 * Leave native syntax/error handling authoritative on declined inputs. */
static int DGLowerShaderSource(const char *source,size_t length,char **result,size_t *result_length)
{
    *result=NULL;*result_length=0;
    if(!length||memchr(source,0,length))return 0;
    for(size_t i=0;i+1<length;++i)if(source[i]=='\\'&&
        (source[i+1]=='\n'||(source[i+1]=='\r'&&i+2<length&&source[i+2]=='\n')))return 0;
    struct DGSLLexer lexer={source,length,0,1,1};
    struct DGSLToken token={0},previous={0},before_name={0};
    size_t insert=0,det_calls=0;unsigned restore_line=0;int version=0,pending=0,calls=0,prefix=0;
    int fp64_extension=0,ambiguous_fp64=0;
    while(DGSLNext(&lexer,&token)){
        if(token.kind==DG_SL_OPAQUE)return 0;
        if(DGSLTokenIs(source,token,dg_glsl_determinant_name))return 0;
        if(token.kind==DG_SL_DIRECTIVE){
            int extension=DGSLFp64Extension(source,token);
            if(extension){
                if(!prefix||extension==2)ambiguous_fp64=1;
                else fp64_extension=extension==1;
            }
            size_t p=token.begin+1;while(p<token.end&&DGSLSpace(source[p]))++p;
            if(p+7<=token.end&&!memcmp(source+p,"version",7)&&
               (p+7==token.end||!DGSLIdentifier(source[p+7]))){
                if(version)return 0;p+=7;
                while(p<token.end&&DGSLSpace(source[p]))++p;
                for(;p<token.end&&source[p]>='0'&&source[p]<='9';++p){
                    if(version>10000)return 0;version=version*10+source[p]-'0';
                }
                if(token.end==token.begin||source[token.end-1]!='\n')return 0;
                insert=token.end;restore_line=token.line+1;prefix=1;
            }else if(prefix&&p+9<=token.end&&!memcmp(source+p,"extension",9)&&
                     (p+9==token.end||!DGSLIdentifier(source[p+9]))){
                /* Extensions must precede declarations in a valid shader. */
                insert=token.end;restore_line=token.line+1;
            }else prefix=0;
            /* Macro aliases/definitions and include-provided overloads need
             * a real preprocessor, not speculative identifier substitution. */
            if(DGSLDirectiveHas(source,token,"roundEven")||DGSLDirectiveHas(source,token,"determinant")||
               DGSLDirectiveHas(source,token,dg_glsl_determinant_name)||DGSLDirectiveHas(source,token,"include"))return 0;
            for(size_t i=token.begin;i+1<token.end;++i)if(source[i]=='#'&&source[i+1]=='#')return 0;
            pending=0;continue;
        }
        if(pending&&DGSLTokenIs(source,token,"(")){
            if((before_name.kind==DG_SL_IDENTIFIER&&!DGSLTokenIs(source,before_name,"return")&&
                !DGSLTokenIs(source,before_name,"else"))||DGSLTokenIs(source,before_name,"]"))return 0;
            calls|=pending;
            if(pending==2)++det_calls;
        }
        prefix=0;
        pending=token.kind==DG_SL_IDENTIFIER?
            (DGSLTokenIs(source,token,"roundEven")?1:DGSLTokenIs(source,token,"determinant")?2:0):0;
        if(pending)before_name=previous;
        previous=token;
    }
    if(version<150||(version<400&&(!fp64_extension||ambiguous_fp64))||!insert||!calls)return 0;
    char line[48];int line_length=snprintf(line,sizeof(line),"\n#line %u\n",restore_line);
    if(line_length<0||(size_t)line_length>=sizeof(line))return -1;
    size_t rounding=(calls&1)?sizeof(dg_glsl_roundeven)-1:0;
    size_t matrix=(calls&2)?sizeof(dg_glsl_determinant)-1:0;
    size_t extra=rounding+matrix+(size_t)line_length;
    const size_t growth=sizeof(dg_glsl_determinant_name)-sizeof("determinant");
    if(length>SIZE_MAX-extra-1||det_calls>(SIZE_MAX-extra-length-1)/growth)return -1;
    size_t bytes=length+extra+det_calls*growth;
    char *lowered=malloc(bytes+1);if(!lowered)return -1;
    memcpy(lowered,source,insert);memcpy(lowered+insert,dg_glsl_roundeven,rounding);
    memcpy(lowered+insert+rounding,dg_glsl_determinant,matrix);
    memcpy(lowered+insert+rounding+matrix,line,(size_t)line_length);
    size_t body=DGSLDeterminantCalls(source,length,insert,lowered+insert+extra);
    *result_length=insert+extra+body;lowered[*result_length]=0;
    *result=lowered;return 1;
}
#endif
