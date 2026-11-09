#!/usr/bin/env python3
"""Generate typed context-first GLI wrappers from the surveyed target SDK.
No guessed offsets; assignment to the SDK struct checks every wrapper type.
"""
import argparse
import json
import pathlib
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("map", type=pathlib.Path)
p.add_argument("output", type=pathlib.Path)
p.add_argument("--dispatch-sdk", type=pathlib.Path,
               help="Compile the target SDK's checked GLI layout over the canonical Mavericks transport")
p.add_argument("--runtime-dispatch-count",type=int,choices=(759,968,975),
               help="Measured runtime layout: late Tiger759, Lion968 or Mojave975")
a = p.parse_args()
rows = json.loads(a.map.read_text())["table"]
if len(rows) != 974:
    raise SystemExit("the canonical GL transport requires the audited 10.9 GLI table")
context_has_responsible_process=True
mojave_tail={'index':974,'field':'multi_draw_elements_indirect_APPLE','result':'void',
    'parameters':'GLIContext ctx, GLenum mode, GLenum type, const GLvoid *indirect, GLsizei drawcount, GLsizei stride',
    'direct':[]}
if a.runtime_dispatch_count in (968,975):
    # Native 11G63 CGLGetParameter(224) returns 7744 bytes on x64. The
    # corresponding libGL stubs use the canonical offsets, unlike the 807-slot
    # SDK header. Build its measured prefix without changing transport opcodes.
    if a.runtime_dispatch_count==968:
        rows=rows[:968]
        context_has_responsible_process=False
    else:
        #18G9323 and the10.14 SDK agree: the974-entry canonical prefix plus
        # this typed private APPLE tail. Only this declaration is new; a full
        #10.14 SDK (whose link stubs omit i386) is not required to build it.
        rows.append(dict(mojave_tail))
    overlay=a.output.parent/'PeriodABI'/'OpenGL'
    overlay.mkdir(parents=True,exist_ok=True)
    declarations=['#ifndef _GLIDISPATCH_H','#define _GLIDISPATCH_H',
        '#include <OpenGL/gltypes.h>','#include <OpenGL/gliContext.h>',
        'typedef struct __GLIFunctionDispatchRec {']
    declarations += [f"    {row['result']} (*{row['field']})({row['parameters']});" for row in rows]
    declarations += ['} GLIFunctionDispatch;','#endif','']
    (overlay/'gliDispatch.h').write_text('\n'.join(declarations))
elif a.dispatch_sdk:
    header=a.dispatch_sdk/'System/Library/Frameworks/OpenGL.framework/Headers/gliDispatch.h'
    text=re.sub(r'/\*.*?\*/','',header.read_text(),flags=re.S)
    fields=re.findall(r'[^;{}]+?\(\s*\*\s*(\w+)\)\s*\(.*?\)\s*;',text,re.S)
    tiger=len(fields)==686
    leopard=len(fields)==759 and fields[-2:]==['depth_rangef','clear_depthf']
    if tiger or leopard:
        late_tiger=leopard and a.runtime_dispatch_count==759
        if a.runtime_dispatch_count==759 and not late_tiger:
            raise SystemExit('The late Tiger override requires its named759-entry SDK layout')
        count=686 if tiger else 759 if late_tiger else 757
        label='Tiger' if tiger or late_tiger else 'Leopard'
        expected=[row['field'] for row in rows[:count]]
        expected[441:448]=['combiner_parameterfv_NV','combiner_parameterf_NV',
            'combiner_parameteriv_NV','combiner_parameteri_NV','combiner_input_NV',
            'combiner_output_NV','final_combiner_input_NV']
        expected[633]='pad'
        if leopard:
            expected[710]='get_tex_parameterIiuv_EXT'
            expected[745:749]=['shader_binary_OES','get_shader_precision_format_OES',
                'release_shader_compiler_OES','get_renderbuffer_storage_formatsiv_OES']
            if late_tiger:expected[757:759]=['depth_rangef','clear_depthf']
        if fields[:count]!=expected:
            raise SystemExit(label+' SDK differs from the independently audited named runtime layout')
        # Actual 9L30 reports 757 entries, with non-prefix NV/OES fields.
        # The native libGL audit qualifies their positions independently.
        # Keep canonical wire opcodes/direct names, but compile each wrapper
        # and the inline CGL table using the real period declaration.
        prototypes=re.findall(r'([^;{}]+?)\(\s*\*\s*(\w+)\)\s*\((.*?)\)\s*;',text,re.S)[:count]
        canonical={row['field']:row for row in rows}
        aliases={'get_tex_parameterIiuv_EXT':'get_tex_parameterIuiv_EXT'}
        period=[]
        normalize=lambda s:re.sub(r'\s+|\bconst\b','',s).replace('GLintptrARB','GLintptr').replace('GLsizeiptrARB','GLsizeiptr')
        types=lambda s:[normalize(re.sub(r'\b\w+\s*(?:\[[^\]]*\])?$','',v.strip())) for v in s.split(',')]
        for index,(result,field,parameters) in enumerate(prototypes):
            row=canonical.get(aliases.get(field,field))
            if row and (normalize(result)!=normalize(row['result']) or types(parameters)!=types(row['parameters'])):
                raise SystemExit('Unqualified '+label+' prototype: '+field)
            if not row and field not in {
                'combiner_parameterfv_NV','combiner_parameterf_NV','combiner_parameteriv_NV',
                'combiner_parameteri_NV','combiner_input_NV','combiner_output_NV',
                'final_combiner_input_NV','pad','shader_binary_OES','get_shader_precision_format_OES',
                'release_shader_compiler_OES','get_renderbuffer_storage_formatsiv_OES'}:
                raise SystemExit('Unqualified '+label+' dispatch field: '+field)
            # Unsupported NV/OES contracts get typed errors, not an aliased
            # later slot or NULL. Do not advertise those host extensions.
            period.append(dict(row or dict(index=index,direct=[]),field=field,
                result=' '.join(result.split()),parameters=' '.join(parameters.split())))
        rows=period;context_has_responsible_process=False
        overlay=a.output.parent/'PeriodABI'/'OpenGL';overlay.mkdir(parents=True,exist_ok=True)
        declarations=['#ifndef _GLIDISPATCH_H','#define _GLIDISPATCH_H',
            *( ['#include <OpenGL/gl.h>','#include <OpenGL/glext.h>'] if tiger or late_tiger
               else ['#include <OpenGL/gltypes.h>']),
            '#include <OpenGL/gliContext.h>','typedef struct __GLIFunctionDispatchRec {']
        declarations += [f"    {row['result']} (*{row['field']})({row['parameters']});" for row in rows]
        declarations += ['} GLIFunctionDispatch;','#endif','']
        (overlay/'gliDispatch.h').write_text('\n'.join(declarations))
    elif (len(fields)==975 and fields[:-1]==[row['field'] for row in rows] and
          fields[-1]=='multi_draw_elements_indirect_APPLE'):
        # Actual18G9323 CGL reports3900/7800 bytes (i386/x64), matching
        # the10.14 SDK's one appended entry. Keep every canonical wire opcode
        # and the first974 offsets unchanged. The private APPLE operation has
        # no qualified codec yet: populate a typed GL error, not a NULL slot
        # or an unbounded guest pointer forwarded to host OpenGL. No extension
        # is advertised or guest renderer published merely by compiling this ABI.
        prototype=re.findall(r'([^;{}]+?)\(\s*\*\s*(\w+)\)\s*\((.*?)\)\s*;',text,re.S)[-1]
        result,field,parameters=prototype
        if result.strip()!='void' or re.sub(r'\s+','',parameters)!=('GLIContextctx,GLenummode,GLenumtype,'
                'constGLvoid*indirect,GLsizeidrawcount,GLsizeistride'):
            raise SystemExit('Unqualified Mojave appended dispatch prototype')
        rows.append(dict(mojave_tail))
        overlay=a.output.parent/'PeriodABI'/'OpenGL';overlay.mkdir(parents=True,exist_ok=True)
        declarations=['#ifndef _GLIDISPATCH_H','#define _GLIDISPATCH_H',
            '#include <OpenGL/gltypes.h>','#include <OpenGL/gliContext.h>',
            'typedef struct __GLIFunctionDispatchRec {']
        declarations += [f"    {row['result']} (*{row['field']})({row['parameters']});" for row in rows]
        declarations += ['} GLIFunctionDispatch;','#endif','']
        (overlay/'gliDispatch.h').write_text('\n'.join(declarations))
    elif len(fields) not in (764,972,974) or fields != [row['field'] for row in rows[:len(fields)]]:
        raise SystemExit('Target SDK dispatch is not the audited compatible prefix; Lion 10.7.5 needs its runtime layout')
    if a.runtime_dispatch_count==759 and not (tiger or leopard):
        raise SystemExit('Late Tiger requires the independently audited named759-slot layout')
    if not (tiger or leopard):rows=rows[:len(fields)]
    if len(fields)==764:
        # Native 10K549: CGLGetParameter(224) reports 6112/3056 bytes
        # (x64/i386). libGL's checked stubs use this exact SDK prefix.
        # Its public CGL context has no later responsible-process slot.
        context_has_responsible_process=False
        prototypes=re.findall(r'([^;{}]+?)\(\s*\*\s*(\w+)\)\s*\((.*?)\)\s*;',text,re.S)
        period=[]
        for row,(result,field,parameters) in zip(rows,prototypes):
            # Four old declarations omit a pointer's top-level const. Use
            # the actual prototype, but reject a different calling ABI.
            normalize=lambda s:re.sub(r'\s+|\bconst\b','',s).replace('GLintptrARB','GLintptr').replace('GLsizeiptrARB','GLsizeiptr')
            types=lambda s:[normalize(re.sub(r'\b\w+\s*(?:\[[^\]]*\])?$','',v.strip())) for v in s.split(',')]
            if normalize(result)!=normalize(row['result']) or types(parameters)!=types(row['parameters']):
                raise SystemExit('Unqualified Snow Leopard prototype: '+field)
            period.append(dict(row,result=' '.join(result.split()),parameters=' '.join(parameters.split())))
        rows=period
        overlay=a.output.parent/'PeriodABI'/'OpenGL'
        overlay.mkdir(parents=True,exist_ok=True)
        (overlay/'gliDispatch.h').write_text(header.read_text())
elif a.runtime_dispatch_count==759:
    raise SystemExit('Late Tiger requires --dispatch-sdk, not a canonical prefix')
out = ["/* Generated from the target SDK; do not edit. */",
       f"#define VIM_GLI_DISPATCH_COUNT {len(rows)}",
       # Existing 10.8 builds retain their audited canonical private context
       # slot even though it is outside the public 972-field dispatch prefix.
       '#define VIM_GLI_HAS_RESPONSIBLE_PROCESS '+str(int(context_has_responsible_process))]
for row in rows:
    ret, field, parameters = row["result"], row["field"], row["parameters"]
    params = parameters.split(",")
    if params[0].strip() != "GLIContext ctx":
        raise SystemExit("unexpected context parameter: " + parameters)
    # These SDK prototypes contain no nested callback declarators. Fail on
    # an unexpected one rather than generating an ABI-unsafe approximation.
    arguments = []
    for param in params[1:]:
        match = re.search(r"\b(\w+)\s*(?:\[.*?\])?$", param)
        if not match or "(" in param:
            raise SystemExit("unrecognized argument: " + param)
        arguments.append(match[1])
    declaration = ",".join(params[1:]) or "void"
    out += [f"typedef {ret} (*VIMProc_{field})({declaration});",
            f"static VIMProc_{field} VIMFunction_{field};",
            f"static {ret} VIMDispatch_{field}({parameters}) {{"]
    if field=="sample_pass" or (field.endswith("_NV") and "combiner" in field):
        # Native public exports are retained no-ops on tested renderers, but
        # the actual core GLI entries report INVALID_OPERATION. Keep the two
        # contracts distinct; don't touch output storage or send a packet.
        out += ["    struct VIMContext *owner=(void *)ctx;",
                "    if(owner->format->profile!=kCGLOGLPVersion_Legacy) { if(!owner->error)owner->error=GL_INVALID_OPERATION;return; }"]
    out += ["    struct VIMSavedBinding previous=VIMSaveGLI(ctx);",
            "    if (!VIMBindGLI(ctx)) { VIMDiscardGLI(previous);" + (" return;" if ret == "void" else f" return ({ret})0;") + " }" ]
    if field == "get_error":
        out += ["    GLenum pending=VIMTakeError(ctx); if(pending) { VIMRestoreGLI(previous,ctx); return pending; }"]
    out += [f"    if (!VIMFunction_{field}) {{ VIMUnsupported(ctx,{row['index']}); VIMRestoreGLI(previous,ctx);" +
            (" return;" if ret == "void" else f" return ({ret})0;") + " }"]
    if a.runtime_dispatch_count == 968 and (field in {
            "begin","accum","bitmap","clear","call_list","call_lists","copy_pixels",
            "read_pixels","blit_framebuffer_EXT"
        } or field.startswith(("clear_buffer","draw_arrays","draw_elements","draw_range_elements",
                              "draw_pixels","draw_element_array","draw_range_element_array",
                              "draw_transform_feedback","multi_draw","eval_mesh","rect","copy_tex_"))):
        out += ["#if defined(VIM_PASSTHROUGH_OPENGL_BACKEND) && defined(VIM_GLI_PLUGIN)",
                "    if(!VIMPrepareOrdinaryDrawing(ctx)) { VIMRestoreGLI(previous,ctx);" +
                (" return; }" if ret == "void" else f" return ({ret})0; }}"), "#endif"]
    transfer = field in {"tex_image1D","tex_image2D","tex_image3D","tex_sub_image1D",
        "tex_sub_image2D","tex_sub_image3D","draw_pixels","read_pixels","get_tex_image"}
    if transfer:
        pack = int(field in {"read_pixels","get_tex_image"})
        if field=="get_tex_image":
            out += ["#ifdef VIM_PASSTHROUGH_OPENGL_BACKEND",
                    f"    if(DGReadTextureWithRowBytes({','.join(arguments)},((struct VIMContext *)ctx)->pack_row_bytes)) {{",
                    "        VIMCheckBackendError(ctx,\"get_tex_image\",0,0);VIMRestoreGLI(previous,ctx);return;",
                    "    }", "#endif"]
        out += ["    struct VIMPixelTransfer transfer;",
            f"    if(!VIMPreparePixelTransfer(ctx,format,type,{pack},&transfer)) {{ VIMRestoreGLI(previous,ctx);return; }}"]
    redefine = (field.startswith(("tex_image", "copy_tex_image", "compressed_tex_image")) and
                any(re.search(r"\blevel$", x.strip()) for x in params))
    if field == "delete_textures":
        out += ["    VIMFlushBeforeTextureDelete(ctx,n,textures);"]
    if redefine:
        out += ["#if defined(VIM_PASSTHROUGH_OPENGL_BACKEND) && defined(VIM_GLI_PLUGIN)",
            "    GLuint cgs_redefinition=VIMCGSBeginRedefine(ctx,target,level);", "#endif"]
    out += [
            "    " + ("" if ret == "void" else f"{ret} result = ") +
            f"VIMFunction_{field}({','.join(arguments)});"]
    # Explicit finite-process research only; no production/default logging.
    if field == "map_buffer":
        out += ['    if(VIMTraceMappings(ctx))VIMGLStderr("MAP whole ctx=%p target=%x access=%x result=%p\\n",ctx,target,access,result);']
    elif field == "map_buffer_range":
        out += ['    if(VIMTraceMappings(ctx))VIMGLStderr("MAP range ctx=%p target=%x offset=%lld length=%lld access=%x result=%p\\n",ctx,target,(long long)offset,(long long)length,access,result);']
    elif field == "buffer_data":
        out += ['    if(VIMTraceMappings(ctx))VIMGLStderr("MAP storage ctx=%p target=%x size=%lld usage=%x\\n",ctx,target,(long long)size,usage);']
    if field == "bind_texture":
        out += ['    if(VIMTraceTextures(ctx))VIMGLStderr("TEX bind ctx=%p target=%x name=%u caller=%p\\n",ctx,target,texture,__builtin_return_address(0));']
    elif field in {"gen_textures","delete_textures"}:
        verb="gen" if field=="gen_textures" else "delete"
        out += [f'    if(VIMTraceTextures(ctx))for(GLsizei i=0;i<n&&i<16;++i)VIMGLStderr("TEX {verb} ctx=%p name=%u caller=%p\\n",ctx,textures[i],__builtin_return_address(0));']
    elif field == "get_integerv":
        out += ['    if((pname==GL_TEXTURE_BINDING_2D || pname==GL_TEXTURE_BINDING_RECTANGLE_ARB || pname==GL_ACTIVE_TEXTURE) && VIMTraceTextures(ctx))VIMGLStderr("TEX get ctx=%p key=%x value=%d caller=%p\\n",ctx,pname,*params,__builtin_return_address(0));']
    if redefine:
        out += ["#if defined(VIM_PASSTHROUGH_OPENGL_BACKEND) && defined(VIM_GLI_PLUGIN)",
            "    if(cgs_redefinition) VIMCGSEndRedefine(ctx,cgs_redefinition);", "#endif"]
    if transfer:
        out += ["    VIMRestorePixelTransfer(&transfer);"]
    if field.startswith("framebuffer_texture") or field in {
            "framebuffer_renderbuffer_EXT","delete_framebuffers_EXT","delete_textures","delete_renderbuffers_EXT"}:
        out += ["    VIMInvalidateFramebufferAttachments(ctx);"]
    if field in {"finish", "flush", "finish_fence_APPLE", "finish_object_APPLE"}:
        out += ["    VIMFlushSurfaces(ctx);"]
    elif field == "delete_textures":
        out += ["    VIMForgetTextures(ctx,n,textures);"]
    elif field in {"delete_buffers","delete_renderbuffers_EXT"}:
        typ = "GL_BUFFER_OBJECT_APPLE" if field=="delete_buffers" else "GL_RENDERBUFFER_EXT"
        names = "buffers" if field=="delete_buffers" else "renderbuffers"
        out += [f"    VIMForgetAppleObjects(ctx,{typ},n,{names});"]
    elif field in {"delete_shader","delete_program"}:
        typ = "GL_SHADER_OBJECT_EXT" if field == "delete_shader" else "GL_PROGRAM_OBJECT_EXT"
        name = "shader" if field == "delete_shader" else "program"
        out += [f"    VIMForgetAppleObjects(ctx,{typ},1,&{name});"]
    elif field in {"delete_framebuffers_EXT", "delete_queries", "delete_vertex_arrays_APPLE"}:
        typ, names = {"delete_framebuffers_EXT": ("GL_FRAMEBUFFER_EXT", "framebuffers"),
            "delete_queries": ("GL_QUERY_OBJECT_EXT", "ids"),
            "delete_vertex_arrays_APPLE": ("GL_VERTEX_ARRAY_OBJECT_EXT", "ids")}[field]
        out += [f"    VIMForgetAppleObjects(ctx,{typ},n,{names});"]
    elif field=="delete_samplers":
        out += ["    if(!((struct VIMContext *)ctx)->inside_begin) VIMForgetAppleObjects(ctx,VIM_GL_SAMPLER,count,samplers);"]
    elif field=="delete_transform_feedbacks":
        out += ["    if(!((struct VIMContext *)ctx)->inside_begin) VIMForgetAppleObjects(ctx,VIM_GL_TRANSFORM_FEEDBACK,n,ids);"]
    if field == "begin":
        out += ["    ((struct VIMContext *)ctx)->inside_begin=1;"]
    elif field == "end":
        out += ["    ((struct VIMContext *)ctx)->inside_begin=0;"]
    if field != "get_error":
        diagnostic = {"enable":"cap,0", "disable":"cap,0",
            "bind_texture":"target,texture",
            "blend_func":"sfactor,dfactor", "hint":"target,mode", "pixel_storei":"pname,param",
            "pixel_storef":"pname,(unsigned)param", "tex_parameteri":"pname,param",
            "object_purgeable_APPLE":"objectType,option"}.get(field,"0,0")
        out += [f'    VIMCheckBackendError(ctx,"{field}",{diagnostic});']
    out += ["    VIMRestoreGLI(previous,ctx);"]
    if ret != "void":
        out += ["    return result;"]
    out += ["}"]
out += ["static void VIMResolveDispatch(void) {"]
for row in rows:
    names = row.get("direct", [])
    if names:
        out += [f'    VIMFunction_{row["field"]}=(VIMProc_{row["field"]})DGGetProcAddress("{names[0]}");']
out += ["}", "static void VIMPopulateDispatch(GLIFunctionDispatch *dispatch) {"]
for row in rows:
    out += [f'    dispatch->{row["field"]}=VIMDispatch_{row["field"]};']
out += ["}"]
out += ["#ifdef VIM_GLI_PLUGIN", "static void VIMPopulateRoutedDispatch(GLIFunctionDispatch *primary, GLIFunctionDispatch *secondary, const GLIFunctionDispatch *source) {"]
for row in rows:
    field=row["field"]
    out += [f"    if (secondary && secondary->{field}) secondary->{field}=source->{field}; else primary->{field}=source->{field};"]
out += ["}", "#endif"]
a.output.write_text("\n".join(out) + "\n")
