/* SGIS base-image side effects at the actual immediate upload boundary.
 * Native Metal GL checks level zero instead of TEXTURE_BASE_LEVEL. Native
 * display lists retain the host's behavior; no private callbacks are added.
 * Ordinary draws and groups without nonzero bases retain their fast path. */
#ifndef VIM_AUTOMATIC_MIPMAP_HOST_H
#define VIM_AUTOMATIC_MIPMAP_HOST_H
static GLenum DGAutomaticMipmapTarget(GLenum image)
{return image>=GL_TEXTURE_CUBE_MAP_POSITIVE_X&&image<=GL_TEXTURE_CUBE_MAP_NEGATIVE_Z?GL_TEXTURE_CUBE_MAP:image;}
struct DGAutomaticMipmapScope {GLenum target,prior,image;GLint level;int active,generate,suppressed;};
static struct DGAutomaticMipmapScope DGBeginAutomaticMipmapExecute(GLenum target,GLint level,
    int legacy,int inside_begin,GLenum list_mode,GLenum *error,int copy_upload,
    int nonzero_base_possible,const struct DGMipmapExecution *execution)
{
    struct DGAutomaticMipmapScope scope={0};
    GLenum image=target;target=DGAutomaticMipmapTarget(image);
    if(!legacy || inside_begin || list_mode || level<0 || (!level&&!nonzero_base_possible) ||
       (target!=GL_TEXTURE_1D && target!=GL_TEXTURE_2D && target!=GL_TEXTURE_3D && target!=GL_TEXTURE_CUBE_MAP))return scope;
    const char *version=(const char *)glGetString(GL_VERSION);
    if(!version || !strstr(version,"Metal"))return scope;
    GLenum prior=glGetError();if(prior && !*error)*error=prior;
    GLint enabled=0,base=0;
    glGetTexParameteriv(target,GL_GENERATE_MIPMAP,&enabled);
    glGetTexParameteriv(target,GL_TEXTURE_BASE_LEVEL,&base);
    GLenum query=glGetError();
    if(query){if(!*error)*error=query;return scope;}
    if(!enabled || (!level&&!base) || (level && !copy_upload && base!=level))return scope;
    GLint border=0;
    if(copy_upload){
        glGetTexLevelParameteriv(image,level,GL_TEXTURE_BORDER,&border);
        query=glGetError();if(query){if(!*error)*error=query;return scope;}
    }
    scope=(struct DGAutomaticMipmapScope){target,*error,image,level,1,base==level,!level||(copy_upload&&!border)};
    /* Separate an existing application error from a rejected upload. Nested
     * native upload helpers may consume their own GL error into this slot. */
    *error=GL_NO_ERROR;
    /* A level-zero write must not regenerate when another base is selected.
     * The native side effect can also discard a nonzero CopyTex(Sub)Image.
     * Suppress it while doing the actual upload; generate only at the base. */
    if(scope.suppressed){
        if(execution)execution->tex_parameteri(CGLGetCurrentContext()->rend,target,GL_GENERATE_MIPMAP,GL_FALSE);
        else glTexParameteri(target,GL_GENERATE_MIPMAP,GL_FALSE);
        GLenum disabled=glGetError();
        if(disabled){*error=scope.prior?scope.prior:disabled;scope.active=0;}
    }
    return scope;
}
static struct DGAutomaticMipmapScope DGBeginAutomaticMipmap(GLenum target,GLint level,
    int legacy,int inside_begin,GLenum list_mode,GLenum *error,int copy_upload,
    int nonzero_base_possible)
{return DGBeginAutomaticMipmapExecute(target,level,legacy,inside_begin,list_mode,error,
    copy_upload,nonzero_base_possible,NULL);}
static void DGEndAutomaticMipmap(const struct DGAutomaticMipmapScope *scope,
    const struct DGMipmapExecution *execution,GLenum *error)
{
    if(!scope->active)return;
    GLenum failed=*error,native=glGetError();if(!failed)failed=native;
    *error=scope->prior?scope->prior:failed;
    if(!failed && scope->generate){
        GLint maximum=0,width=0,height=0,depth=0,border=0;
        glGetTexParameteriv(scope->target,GL_TEXTURE_MAX_LEVEL,&maximum);
        glGetTexLevelParameteriv(scope->image,scope->level,GL_TEXTURE_WIDTH,&width);
        glGetTexLevelParameteriv(scope->image,scope->level,GL_TEXTURE_HEIGHT,&height);
        glGetTexLevelParameteriv(scope->image,scope->level,GL_TEXTURE_DEPTH,&depth);
        glGetTexLevelParameteriv(scope->image,scope->level,GL_TEXTURE_BORDER,&border);
        failed=glGetError();
        if(!failed && width>0 && height>0 && depth>0 && !border){
            GLint spatial=width;
            if(scope->target!=GL_TEXTURE_1D && height>spatial)spatial=height;
            if(scope->target==GL_TEXTURE_3D && depth>spatial)spatial=depth;
            GLint last=scope->level;while(spatial>1){++last;spatial/=2;}
            if(last>scope->level){
            CGLContextObj current=execution?CGLGetCurrentContext():NULL;
                /* SGIS derives the full pyramid, unlike explicit generation's
                 * MAX_LEVEL clipping. Execute these temporary parameters even
                 * when the caller's native dispatch is subsequently changed. */
                if(execution)execution->tex_parameteri(current->rend,scope->target,GL_TEXTURE_MAX_LEVEL,last);
                else glTexParameteri(scope->target,GL_TEXTURE_MAX_LEVEL,last);
                failed=glGetError();
                if(!failed)failed=scope->target==GL_TEXTURE_CUBE_MAP?
                    DGGenerateAutomaticCubeMipmapExecute(scope->image,scope->level,last,execution):
                    DGGenerateSrgbMipmapExecute(scope->target,1,0,execution);
                if(execution)execution->tex_parameteri(current->rend,scope->target,GL_TEXTURE_MAX_LEVEL,maximum);
                else glTexParameteri(scope->target,GL_TEXTURE_MAX_LEVEL,maximum);
                GLenum restored=glGetError();if(!failed)failed=restored;
            }
        }
    }
    if(scope->suppressed){
        if(execution)execution->tex_parameteri(CGLGetCurrentContext()->rend,scope->target,GL_GENERATE_MIPMAP,GL_TRUE);
        else glTexParameteri(scope->target,GL_GENERATE_MIPMAP,GL_TRUE);
        GLenum restored=glGetError();if(!failed)failed=restored;
    }
    if(failed && !*error)*error=failed;
}
#endif
