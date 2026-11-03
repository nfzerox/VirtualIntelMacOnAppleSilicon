#!/usr/bin/env python3
"""Generate a desktop GL codec from the target SDK, not Mesa API metadata.

Scalar arguments keep their actual float/double/integer bit patterns. Pointer
arguments require an explicit, bounded shape; unknown shapes fail closed.
This is intentionally not a claim that all 974 GLI operations are supported.
"""
import argparse
import hashlib
import json
import pathlib
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("sdk", type=pathlib.Path)
p.add_argument("output", type=pathlib.Path)
a = p.parse_args()
h = a.sdk / "System/Library/Frameworks/OpenGL.framework/Headers"
text = re.sub(r"/\*.*?\*/", "", (h / "gliDispatch.h").read_text(), flags=re.S)
rows = re.findall(r"([^;{}]+?)\(\s*\*(\w+)\)\s*\((.*?)\)\s*;", text, re.S)
if len(rows) != 974:
    raise SystemExit("expected the audited Mavericks 974-slot GLI interface")
public = "\n".join((h / n).read_text() for n in ("gl.h", "glext.h"))
exports = set(re.findall(r"extern\s+[^;{}]*?\b(gl\w+)\s*\(", public))
norm = lambda n: n.removeprefix("gl").replace("_", "").lower()
names = {norm(n): n for n in sorted(exports)}
aliases = {"map_buffer_range":"glMapBufferRange", "flush_mapped_buffer_range":"glFlushMappedBufferRange",
           # These real public entry points are declared only in gl3ext.h.
           # Their GLI signatures are scalar-only; use the existing typed
           # generator and native validation, not a mutable TexImage substitute.
           "tex_storage1D":"glTexStorage1D", "tex_storage2D":"glTexStorage2D", "tex_storage3D":"glTexStorage3D",
           "get_buffer_parameteri64v":"glGetBufferParameteri64v", "copy_buffer_sub_data":"glCopyBufferSubData",
           "draw_arrays_instanced":"glDrawArraysInstanced", "draw_elements_instanced":"glDrawElementsInstanced",
           "vertex_attrib_divisor":"glVertexAttribDivisor",
           "get_stringi":"glGetStringi", "get_integer64v_sync": "glGetInteger64v", "bind_vertex_array_ARB": "glBindVertexArrayAPPLE",
           "gen_vertex_arrays_ARB": "glGenVertexArraysAPPLE", "delete_vertex_arrays_ARB": "glDeleteVertexArraysAPPLE",
           "is_vertex_array_ARB": "glIsVertexArrayAPPLE", "bind_vertex_array_EXT": "glBindVertexArrayAPPLE",
           "gen_vertex_arrays_EXT": "glGenVertexArraysAPPLE", "delete_vertex_arrays_EXT": "glDeleteVertexArraysAPPLE",
           "is_vertex_array_EXT": "glIsVertexArrayAPPLE"}
array_calls = {
    "draw_arrays_instanced": "DGDrawInstances(context,mode,first,count,0,NULL,instancecount,1);",
    "draw_elements_instanced": "DGDrawInstances(context,mode,0,count,type,indices,instancecount,1);",
    "vertex_attrib_divisor": "DGArrayDivisor(context,index,divisor);",
    "vertex_pointer": "DGArrayPointer(context,0,size,type,stride,pointer,GL_FALSE);",
    "normal_pointer": "DGArrayPointer(context,1,3,type,stride,pointer,GL_FALSE);",
    "color_pointer": "DGArrayPointer(context,2,size,type,stride,pointer,GL_FALSE);",
    "secondary_color_pointer": "DGArrayPointer(context,3,size,type,stride,pointer,GL_FALSE);",
    "fog_coord_pointer": "DGArrayPointer(context,4,1,type,stride,pointer,GL_FALSE);",
    "tex_coord_pointer": "DGArrayPointer(context,8+context->client_texture,size,type,stride,pointer,GL_FALSE);",
    "vertex_attrib_pointer_ARB": "if(index>=16) context->error=GL_INVALID_VALUE;else DGArrayPointer(context,16+index,size,type,stride,pointer,normalized);",
    "enable_client_state": "DGArrayEnable(context,DGArraySlot(context,array),1);",
    "disable_client_state": "DGArrayEnable(context,DGArraySlot(context,array),0);",
    "enable_vertex_attrib_array_ARB": "if(index>=16) context->error=GL_INVALID_VALUE;else DGArrayEnable(context,16+index,1);",
    "disable_vertex_attrib_array_ARB": "if(index>=16) context->error=GL_INVALID_VALUE;else DGArrayEnable(context,16+index,0);",
    "draw_arrays": "DGDraw(context,mode,first,count,0,NULL);",
    "draw_elements": "DGDraw(context,mode,0,count,type,indices);",
    "draw_range_elements": "(void)start;(void)end;DGDraw(context,mode,0,count,type,indices);",
    # APPLE_texture_range explicitly permits an indirect renderer to ignore
    # the mapping hint. No guest address is submitted to host OpenGL.
    "texture_range_APPLE": "DGTextureRange(context,target,length,pointer);",
    "get_tex_parameter_pointerv_APPLE": "DGTextureRangePointer(context,target,pname,params);",
}
shader_handles = {
    "get_handle_ARB": "GLint name=0;if(pname==GL_PROGRAM_OBJECT_ARB) glGetIntegerv(GL_CURRENT_PROGRAM,&name);else context->error=GL_INVALID_ENUM;reply->result=(uint32_t)name;",
    "create_shader_object_ARB": "GLhandleARB result=(GLhandleARB)(uintptr_t)glCreateShader(shaderType);reply->result=(uintptr_t)result;",
    "create_program_object_ARB": "GLhandleARB result=(GLhandleARB)(uintptr_t)glCreateProgram();reply->result=(uintptr_t)result;",
    "compile_shader_ARB": "glCompileShader((GLuint)(uintptr_t)shaderObj);",
    "attach_object_ARB": "glAttachShader((GLuint)(uintptr_t)containerObj,(GLuint)(uintptr_t)obj);",
    "detach_object_ARB": "glDetachShader((GLuint)(uintptr_t)containerObj,(GLuint)(uintptr_t)attachedObj);",
    "link_program_ARB": "DGForgetUniformShapes(context,(GLuint)(uintptr_t)programObj);DGHostLinkProgram(context,(GLuint)(uintptr_t)programObj);",
    "use_program_object_ARB": "glUseProgram((GLuint)(uintptr_t)programObj);",
    "validate_program_ARB": "glValidateProgram((GLuint)(uintptr_t)programObj);",
    "get_uniform_location_ARB": "DGHostUniformLocation(context,(GLuint)(uintptr_t)programObj,name,reply);",
    "get_attrib_location_ARB": "GLint result=glGetAttribLocation((GLuint)(uintptr_t)programObj,name);reply->result=DGBits(&result,sizeof(result));",
    "bind_attrib_location_ARB": "glBindAttribLocation((GLuint)(uintptr_t)programObj,index,name);",
    "delete_object_ARB": "GLuint name=(GLuint)(uintptr_t)obj;if(glIsShader(name)) glDeleteShader(name);else if(glIsProgram(name)) { DGForgetUniformShapes(context,name);glDeleteProgram(name); }else context->error=GL_INVALID_VALUE;",
    "get_object_parameteriv_ARB": "glGetObjectParameterivARB(obj,pname,params);",
    "get_object_parameterfv_ARB": "glGetObjectParameterfvARB(obj,pname,params);",
    "get_info_log_ARB": "glGetInfoLogARB(obj,maxLength,length,infoLog);",
    "get_shader_source_ARB": "glGetShaderSourceARB(obj,maxLength,length,source);",
    "get_active_uniform_ARB": "glGetActiveUniformARB(programObj,index,maxLength,length,size,type,name);",
    "get_active_attrib_ARB": "glGetActiveAttribARB(programObj,index,maxLength,length,size,type,name);",
}


def shape(field, param, typ, params):
    # Each expression is evaluated independently on both sides.
    if field.startswith(("compressed_tex_image", "compressed_tex_sub_image")):
        return "DGArrayBytes(imageSize,1)"
    if field == "draw_buffers_ARB":
        return "DGArrayBytes(n,sizeof(GLenum))"
    if field == "call_lists":
        return "DGCallListsBytes(n,type)"
    if field in {"gen_textures", "delete_textures", "gen_buffers", "delete_buffers",
                 "gen_framebuffers_EXT", "delete_framebuffers_EXT", "gen_renderbuffers_EXT",
                 "delete_renderbuffers_EXT", "gen_fences_APPLE", "delete_fences_APPLE",
                 "gen_vertex_arrays_APPLE", "delete_vertex_arrays_APPLE", "gen_vertex_arrays_ARB", "delete_vertex_arrays_ARB",
                 "gen_vertex_arrays_EXT", "delete_vertex_arrays_EXT", "gen_programs_ARB", "delete_programs_ARB",
                 "gen_queries", "delete_queries"}:
        return "DGArrayBytes(n,sizeof(GLuint))"
    if field in {"get_queryiv", "get_query_objectiv", "get_query_objectuiv"}:
        return f"sizeof({typ})"
    if field == "prioritize_textures":
        return f"DGArrayBytes(n,sizeof({typ}))"
    if field in {"pixel_mapfv", "pixel_mapuiv", "pixel_mapusv"}:
        return f"DGArrayBytes(mapsize,sizeof({typ}))"
    if field == "are_textures_resident":
        return "DGArrayBytes(n,sizeof(GLboolean))" if param == "residences" else "DGArrayBytes(n,sizeof(GLuint))"
    if field in {"get_integerv", "get_floatv", "get_doublev", "get_booleanv", "get_integer64v_sync"}:
        return f"DGGetCount(pname) ? DGGetCount(pname)*sizeof({typ}) : SIZE_MAX"
    if field in {"get_vertex_attribiv_ARB", "get_vertex_attribfv_ARB", "get_vertex_attribdv_ARB"}:
        return f"DGVertexAttribCount(pname) ? DGVertexAttribCount(pname)*sizeof({typ}) : SIZE_MAX"
    if field in {"get_shaderiv", "get_programiv", "get_tex_level_parameteriv", "get_tex_level_parameterfv", "get_tex_parameteriv",
                 "get_framebuffer_attachment_parameteriv_EXT", "get_renderbuffer_parameteriv_EXT"}:
        return f"DGParameterCount(pname)*sizeof({typ})"
    if field in {"get_shader_info_log", "get_program_info_log"}:
        return "sizeof(GLsizei)" if param == "length" else "DGArrayBytes(bufSize,1)"
    if field == "get_attached_shaders":
        return "sizeof(GLsizei)" if param == "count" else "DGArrayBytes(maxCount,sizeof(GLuint))"
    if field in {"get_uniform_location", "get_attrib_location", "bind_attrib_location",
                 "bind_frag_data_location_EXT", "get_frag_data_location_EXT",
                 "get_uniform_location_ARB", "get_attrib_location_ARB", "bind_attrib_location_ARB"} and typ in {"GLchar", "GLcharARB"}:
        return f"DGStringBytes({param})"
    if field in {"tex_image2D", "tex_sub_image2D", "read_pixels"}:
        return f"DGPixelBytes(context,width,height,format,type,{int(field == 'read_pixels')})"
    if field == "get_tex_image" and param == "pixels":
        return "DGPixelBytes(context,width,height,format,type,1)"
    if field in {"tex_image1D", "tex_sub_image1D"}:
        return "DGPixelBytes(context,width,1,format,type,0)"
    if field in {"tex_image3D", "tex_sub_image3D"}:
        return "DGPixelBytes3D(context,width,height,depth,format,type,0)"
    if field in {"buffer_data", "buffer_sub_data"}:
        return "DGArrayBytes(size,1)"
    if field == "program_string_ARB":
        return "DGArrayBytes(len,1)"
    if field in {"label_object_EXT", "insert_event_marker_EXT", "push_group_marker_EXT"}:
        return f"length!=0 ? DGArrayBytes(length,1) : DGStringBytes({param})"
    if field in {"get_programiv_ARB"}:
        return f"sizeof({typ})"
    if field in {"get_object_parameteriv_ARB", "get_object_parameterfv_ARB"}:
        return f"sizeof({typ})"
    if field in {"get_info_log_ARB", "get_shader_source_ARB", "get_active_uniform_ARB", "get_active_attrib_ARB"}:
        return "DGArrayBytes(maxLength,1)" if param in {"infoLog", "source", "name"} else f"sizeof({typ})"
    if re.fullmatch(r"(?:program_(?:local|env)_parameter4[fd]v_ARB|get_program_(?:local|env)_parameter[fd]v_ARB)", field):
        return f"4*sizeof({typ})"
    # OpenGL 2.1 glGetTexEnv: COLOR writes four values; every other accepted
    # pname writes one. Leave target/pname validation and integer conversion
    # to real host GL, just as for the existing setters. Invalid enums must
    # preserve the caller's output through the native query-reply mask.
    if field in {"tex_envfv", "tex_enviv", "get_tex_envfv", "get_tex_enviv"}:
        return f"(pname==GL_TEXTURE_ENV_COLOR ? 4 : 1)*sizeof({typ})"
    if field in {"lightfv", "lightiv", "get_lightfv", "get_lightiv"}:
        return f"DGArrayBytes(DGLightingCount(0,pname),sizeof({typ}))"
    if field in {"materialfv", "materialiv", "get_materialfv", "get_materialiv"}:
        return f"DGArrayBytes(DGLightingCount(1,pname),sizeof({typ}))"
    if field in {"light_modelfv", "light_modeliv"}:
        return f"DGArrayBytes(DGLightingCount(2,pname),sizeof({typ}))"
    if field in {"fogfv", "fogiv"}:
        return f"DGArrayBytes(DGLightingCount(3,pname),sizeof({typ}))"
    if field in {"tex_parameterfv", "tex_parameteriv", "get_tex_parameterfv"}:
        return f"(pname==GL_TEXTURE_BORDER_COLOR || pname==0x8e46 ? 4 : 1)*sizeof({typ})"
    if field in {"tex_genfv", "tex_gendv", "tex_geniv", "get_tex_genfv", "get_tex_gendv", "get_tex_geniv"}:
        return f"(pname==GL_OBJECT_PLANE || pname==GL_EYE_PLANE ? 4 : 1)*sizeof({typ})"
    if field in {"point_parameterfv", "point_parameteriv"}:
        return f"(pname==GL_POINT_DISTANCE_ATTENUATION ? 3 : 1)*sizeof({typ})"
    if field in {"load_matrixf", "load_matrixd", "mult_matrixf", "mult_matrixd",
                 "load_transpose_matrixf", "load_transpose_matrixd", "mult_transpose_matrixf", "mult_transpose_matrixd"}:
        return f"16*sizeof({typ})"
    if field in {"clip_plane", "get_clip_plane"}:
        return "4*sizeof(GLdouble)"
    if re.fullmatch(r"rect[dfis]v|eval_coord2[df]v", field):
        return f"2*sizeof({typ})"
    if re.fullmatch(r"edge_flagv|index(?:ub|s|i|f|d)v|eval_coord1[df]v|fog_coord[df]v", field):
        return f"sizeof({typ})"
    m = re.fullmatch(r"vertex_attrib([1234])N?(?:ub|us|ui|b|s|i|f|d)v_ARB", field)
    if m:
        return f"{m[1]}*sizeof({typ})"
    m = re.fullmatch(r"vertex_attribI([1234])(?:ub|us|ui|b|s|i)v_EXT", field)
    if m:
        return f"{m[1]}*sizeof({typ})"
    if field in {"program_env_parameters4fv_EXT", "program_local_parameters4fv_EXT"}:
        return f"DGArrayBytes(count,4*sizeof({typ}))"
    m = re.fullmatch(r"(?:color|vertex|tex_coord|multi_tex_coord|raster_pos|window_pos|secondary_color)([1234])(?:ub|us|ui|b|s|i|f|d)v(?:_ARB|_EXT)?", field)
    if m:
        return f"{m[1]}*sizeof({typ})"
    if field in {"normal3bv", "normal3sv", "normal3iv", "normal3fv", "normal3dv"}:
        return f"3*sizeof({typ})"
    m = re.fullmatch(r"uniform([1234])[fi]v(?:_ARB)?", field)
    if m:
        return f"DGArrayBytes(count,{m[1]}*sizeof({typ}))"
    m = re.fullmatch(r"uniform_matrix([234])(?:x([234]))?fv(?:_ARB)?", field)
    if m:
        return f"DGArrayBytes(count,{int(m[1])*int(m[2] or m[1])}*sizeof({typ}))"
    return None


client, host, decl, mapping, report = [], [], [], [], []
gli_map = []
emitted = set()
for index, (ret, field, parameters) in enumerate(rows):
    ret = " ".join(ret.split())
    params = [" ".join(x.split()) for x in parameters.split(",")][1:]
    name = aliases.get(field) or names.get(norm(field))
    gli_map.append(dict(index=index, field=field, result=ret,
                        parameters=" ".join(parameters.split()), direct=[name] if name else []))
    # The SDK's GLI table spells this argument GLenum, whereas its public
    # gl.h declaration uses GLint. Both are 32-bit; preserve the bits on wire
    # and use the actual public signature in the client/host implementation.
    if field in {"tex_image1D", "tex_image2D", "tex_image3D"}:
        params = [re.sub(r"^GLenum (internal[Ff]ormat)$", r"GLint \1", s) for s in params]
    parsed = []
    for param in params:
        match = re.match(r"(.+?)(\w+)$", param)
        if not match or "(" in param:
            raise SystemExit("unrecognized SDK prototype: " + param)
        typ, arg = match.groups()
        parsed.append((typ.strip(), arg))
    signature = ", ".join(params) or "void"
    pointer_result = "*" in ret or ret == "GLsync"
    supported = name and not pointer_result and not any(t == "GLsync" for t, _ in parsed)
    # Apple's GLhandleARB is a pointer typedef, not a portable object name.
    # Represent this subset using real host core shader/program GLuint names;
    # never export a host driver object pointer or feed one supplied by guest.
    if ret == "GLhandleARB" or any(t == "GLhandleARB" for t, _ in parsed):
        supported = field in shader_handles
    shapes = {}
    for typ, arg in parsed:
        if "*" in typ:
            element = typ.replace("const", "").replace("*", "").strip()
            size = shape(field, arg, element, params) if typ.count("*") == 1 else None
            if not size:
                supported = False
            shapes[arg] = (size, element, "const" not in typ)
    # Output arrays with an explicit API count/capacity must reach native GL
    # even when that signed count is zero/negative. Bound positive storage;
    # never inspect an unused caller array merely to marshal an error call.
    output_counts = set()
    for size, _, output in shapes.values():
        match = re.match(r"DGArrayBytes\((\w+),", size or "")
        if output and match:
            output_counts.add(match[1])
    query_inputs = set()
    for arg, (size, element, output) in list(shapes.items()):
        match = re.match(r"DGArrayBytes\((\w+),", size or "")
        if match and match[1] in output_counts:
            shapes[arg] = (size.replace("DGArrayBytes(", "DGQueryArrayBytes(", 1), element, output)
            if not output:
                query_inputs.add(arg)
    # These counted input APIs ignore arrays at nonpositive counts. Send a
    # private placeholder so native validation sees the original count, not
    # an accidental transport error or a read from the caller's unused data.
    if field in {"delete_queries", "prioritize_textures", "draw_buffers_ARB", "pixel_mapfv", "pixel_mapuiv", "pixel_mapusv",
                 "program_env_parameters4fv_EXT", "program_local_parameters4fv_EXT"}:
        for arg,(size,element,output) in list(shapes.items()):
            if size and size.startswith("DGArrayBytes(") and not output:
                shapes[arg]=(size.replace("DGArrayBytes(","DGQueryArrayBytes(",1),element,output)
                query_inputs.add(arg)
    if name in {"glShaderSource", "glShaderSourceARB"}:
        supported = True  # dedicated nested-string codec, not a pointer cast
    if field in {"draw_arrays", "array_element"} or field.startswith(("draw_elements", "draw_range_elements", "multi_draw_")):
        supported = False  # client-array/VBO-offset codecs are a separate gate
    if field in array_calls:
        supported = True
    report.append(dict(slot=index, field=field, symbol=name, supported=bool(supported)))
    if not name:
        continue
    if name in emitted:
        continue  # two GLI alias slots resolve to one typed public forwarder
    emitted.add(name)
    decl.append(f"{ret} {name}({signature});")
    mapping.append(f'if(!strcmp(name,"{name}")) return (void (*)(void)){name};')
    if name in {"glGetString", "glGetStringi", "glGetError", "glGetProgramStringARB", "glGetUniformfvARB", "glGetUniformivARB", "glGetUniformfv", "glGetUniformiv", "glFenceSync", "glIsSync", "glDeleteSync", "glClientWaitSync", "glWaitSync", "glGetSynciv",
                "glGetBufferParameteriv", "glGetBufferParameteri64v", "glGetBufferSubData", "glCopyBufferSubData",
                "glMapBuffer", "glMapBufferRange", "glGetBufferPointerv", "glUnmapBuffer", "glFlushMappedBufferRange", "glFlushMappedBufferRangeAPPLE"}:
        report[-1]["supported"] = True
        continue  # bounded string reply and local transport errors are custom
    client.append(f"{ret} {name}({signature}) {{")
    client.append("    struct DGContext *context=dg_current;")
    compressed_upload=field.startswith(("compressed_tex_image", "compressed_tex_sub_image"))
    if compressed_upload:
        client.append("    if(context && !(context->connection->features&DG_FEATURE_COMPRESSED_TEXTURE_UPLOAD)) { context->error=GL_INVALID_OPERATION;return; }")
        client.append("    if(context && imageSize<0) { context->error=GL_INVALID_VALUE;return; }")
        if field.startswith("compressed_tex_image"):
            client.append("    if(DGCompressedProxy(target))data=NULL; // Proxy definitions never inspect application storage.")
    if output_counts:
        nonpositive=" || ".join(f"{count}<=0" for count in sorted(output_counts))
        ending="return;" if ret=="void" else f"return ({ret})0;"
        client.append(f"    if(context && ({nonpositive}) && !(context->connection->capabilities&DG_CAP_QUERY_REPLIES)) {{ context->error=GL_INVALID_OPERATION;{ending} }}")
    if field == "call_lists":
        client.append("    if(context && !(context->connection->capabilities&DG_CAP_CALL_LISTS)) { context->error=GL_INVALID_OPERATION;return; }")
        client.append("    if(context && DGCallListsBytes(n,type) && !lists) { context->error=GL_INVALID_VALUE;return; }")
    if field in {"get_integerv", "get_floatv", "get_doublev", "get_booleanv", "get_integer64v_sync"}:
        client.append("    if(context && (pname==GL_LIST_BASE || pname==GL_LIST_INDEX || pname==GL_LIST_MODE) && !(context->connection->capabilities&DG_CAP_CALL_LISTS)) { context->error=GL_INVALID_OPERATION;return; }")
    if field in {"tex_image1D", "tex_sub_image1D", "tex_image2D", "tex_sub_image2D", "tex_image3D", "tex_sub_image3D", "read_pixels", "get_tex_image"}:
        client.append("    if(context && (format==GL_YCBCR_422_APPLE || type==GL_UNSIGNED_SHORT_8_8_APPLE || type==GL_UNSIGNED_SHORT_8_8_REV_APPLE) && !(context->connection->capabilities&DG_CAP_APPLE_VIDEO_TEXTURES)) { context->error=GL_INVALID_OPERATION;return; }")
    location_query = field in {"get_uniform_location", "get_uniform_location_ARB"}
    if location_query:
        location_program = f"(GLuint)(uintptr_t){parsed[0][1]}"
        client.append(f"    uint64_t location_generation=0;GLint cached_location=0;if(DGProgramLocationGet(context,{location_program},name,&cached_location,&location_generation))return cached_location;")
    if field in {"tex_image3D", "tex_sub_image3D"}:
        client.append("    if(context && !(context->connection->capabilities&DG_CAP_TEXTURE_3D)) { context->error=GL_INVALID_OPERATION;return; }")
        if field == "tex_image3D":
            client.append("    if(target==GL_PROXY_TEXTURE_3D)pixels=NULL; // Proxy allocation never reads client memory.")
            client.append(f"    if(context && context->pixel_buffer[0]) {{ DGPBOPixel3D(context,0,target,level,{parsed[2][1]},0,0,0,width,height,depth,border,format,type,pixels);return; }}")
        else:
            client.append("    if(context && context->pixel_buffer[0]) { DGPBOPixel3D(context,1,target,level,0,xoffset,yoffset,zoffset,width,height,depth,0,format,type,pixels);return; }")
            client.append("    if(context && width && height && depth && !pixels) { context->error=GL_INVALID_VALUE;return; }")
    if field in {"tex_image1D", "tex_sub_image1D"}:
        client.append("    if(context && !(context->connection->capabilities&DG_CAP_TEXTURE_1D)) { context->error=GL_INVALID_OPERATION;return; }")
        if field == "tex_image1D":
            client.append("    if(context && context->pixel_buffer[0]) { DGPBOPixel(context,3,target,level,internalformat,0,0,width,1,border,format,type,pixels);return; }")
        else:
            client.append("    if(context && context->pixel_buffer[0]) { DGPBOPixel(context,4,target,level,0,xoffset,0,width,1,0,format,type,pixels);return; }")
            client.append("    if(context && width>0 && !pixels) { context->error=GL_INVALID_VALUE;return; }")
    if field == "delete_programs_ARB":
        client.append("    if(context && n<0) { context->error=GL_INVALID_VALUE;return; }")
    if field == "program_string_ARB":
        client.append("    if(context && len<0) { context->error=GL_INVALID_VALUE;return; }")
    if field == "bind_buffer":
        client.append("    if(DGPBOBind(context,target,buffer))return;")
    if field == "delete_buffers":
        client.append("    if(DGPBODelete(context,n,buffers))return;")
    if field == "push_client_attrib":
        client.append("    if(DGPBOStack(context,mask,1))return;")
    if field == "pop_client_attrib":
        client.append("    if(DGPBOStack(context,0,0))return;")
    if field == "tex_image2D":
        client.append("    if(context && context->pixel_buffer[0]) { DGPBOPixel(context,0,target,level,internalformat,0,0,width,height,border,format,type,pixels);return; }")
    if field == "tex_sub_image2D":
        client.append("    if(context && context->pixel_buffer[0]) { DGPBOPixel(context,1,target,level,0,xoffset,yoffset,width,height,0,format,type,pixels);return; }")
        client.append("    if(DGTightTexSubImage2D(context,target,level,xoffset,yoffset,width,height,format,type,pixels))return;")
    if field == "read_pixels":
        client.append("    if(context && context->pixel_buffer[1]) { DGPBOPixel(context,2,0,0,0,x,y,width,height,0,format,type,pixels);return; }")
    if field in {"get_vertex_attribiv_ARB", "get_vertex_attribfv_ARB", "get_vertex_attribdv_ARB"}:
        result_type=parsed[2][0].replace("*", "").strip()
        client.append("    if(context && !(context->connection->capabilities&DG_CAP_INSTANCED_ARRAYS)) { context->error=GL_INVALID_OPERATION;return; }")
        client.append(f"    if(context) {{ GLint value=0;int found=DGArrayState(context,index,pname,&value);if(found) {{ if(found>0 && params)*params=({result_type})value;return; }} }}")
    if field in {"get_integerv", "get_floatv"}:
        client.append(f"    if(DGCommonStateGet(context,pname,params,{int(field=='get_floatv')}))return;")
    if field in {"get_integerv", "get_floatv", "get_doublev", "get_booleanv", "get_integer64v_sync"}:
        client.append("    if(context && context->profile!=0x1000 && pname==0x821d) { if(params)*params=0;else context->error=GL_INVALID_VALUE;return; }")
        result_type=parsed[1][0].replace("*", "").strip()
        expression="!!(context->vertex && context->vertex->name)" if field=="get_booleanv" else "(context->vertex?context->vertex->name:0)"
        client.append(f"    if(context && pname==0x85b5) {{ if(params)*params=({result_type}){expression};else context->error=GL_INVALID_VALUE;return; }}")
        value="!!logical" if field=="get_booleanv" else "logical"
        client.append(f"    {{ GLint logical=0;if(DGClientArrayInteger(context,pname,&logical)) {{ if(params)*params=({result_type})({value});else context->error=GL_INVALID_VALUE;return; }} }}")
    if field == "is_enabled":
        client.append("    if(context && context->profile==0x1000 && !context->common_state_blocked && DGArraySlot(context,cap)>=0) { GLint logical=0;if(DGClientArrayInteger(context,cap,&logical))return !!logical; }")
    if field in {"begin", "end"}:
        # Invalid/nested Begin may disable the optimization unnecessarily,
        # never incorrectly enable a cached query inside a real Begin/End.
        client.append(f"    if(context)context->common_state_blocked={int(field=='begin')};")
    if field in array_calls:
        client += ["    if(!context) return;", "    " + array_calls[field], "}"]
        continue
    if not supported:
        client.append(f"    DGUnsupported(context,{index});")
        for _, arg in parsed:
            client.append(f"    (void){arg};")
        client.append("    return;" if ret == "void" else f"    return ({ret})0;")
        client.append("}")
        continue
    if name in {"glShaderSource", "glShaderSourceARB"}:
        client.append(f"    (void)context;")
        obj = "(GLuint)(uintptr_t)shaderObj" if name.endswith("ARB") else "shader"
        client += [f"    DGShaderSource(context,{obj},count,string,length);", "}"]
        client.append(f"unsigned DGShaderSourceSlot(void) {{ return {index}; }}")
        host += [f"case {index}: return DGHostShaderSource(context,dg_wire_message,reply);"]
        continue
    # Negative image dimensions are a real GL_INVALID_VALUE, not an unknown
    # pointer shape/unsupported operation. In particular native Mavericks
    # validates this before rejecting replacement of imported CGS storage.
    if field.startswith(("tex_image", "tex_sub_image", "copy_tex_image", "compressed_tex_image", "compressed_tex_sub_image")):
        dimensions = [arg for _, arg in parsed if arg in {"width", "height", "depth"}]
        if dimensions:
            bad = " || ".join(f"{arg}<0" for arg in dimensions)
            client += [f"    if(context && ({bad})) {{ if(!context->error) context->error=GL_INVALID_VALUE;return; }}"]
    client += ["    if(!context) {" + (" return;" if ret == "void" else f" return ({ret})0;") + " }",
               f"    struct DGPacket packet=DGPacketNew(context,{index});"]
    host += [f"case {index}: {{"]
    if field in {"tex_image1D", "tex_sub_image1D", "tex_image2D", "tex_sub_image2D", "tex_image3D", "tex_sub_image3D", "read_pixels"}:
        binding="GL_PIXEL_PACK_BUFFER_BINDING" if field=="read_pixels" else "GL_PIXEL_UNPACK_BUFFER_BINDING"
        host.append(f"    GLint pixel_buffer=0;if(context->inside_begin) {{ context->error=GL_INVALID_OPERATION;return 0; }}glGetIntegerv({binding},&pixel_buffer);if(pixel_buffer) {{ context->error=GL_INVALID_OPERATION;return 0; }}")
    if field == "get_tex_image":
        # Snow Leopard's native interactive window screenshot renders to a
        # rectangle FBO and reads the bound texture (not the display). Both
        # ends size the same packed output, using the actual mip dimensions.
        # PBO offsets and non-2D image layouts are intentionally not decoded
        # as client pointers.
        client += [
            "    if(!(context->connection->features&DG_FEATURE_TEXTURE_IMAGE_READBACK) || context->pixel_buffer[1] || (target!=GL_TEXTURE_RECTANGLE_ARB && target!=GL_TEXTURE_2D)) { if(!context->error)context->error=GL_INVALID_OPERATION;return; }",
            "    GLint width=0,height=0;glGetTexLevelParameteriv(target,level,GL_TEXTURE_WIDTH,&width);",
            "    glGetTexLevelParameteriv(target,level,GL_TEXTURE_HEIGHT,&height);",
            "    if(width<=0 || height<=0)return;",
        ]
    inputs, output_args = [], []
    for i, (typ, arg) in enumerate(parsed):
        if "*" not in typ:
            if typ in {"GLintptr", "GLsizeiptr", "GLintptrARB", "GLsizeiptrARB"}:
                # i386's signed pointer-sized offset/length must be sign-
                # extended into the fixed 64-bit wire slot. Copying four
                # bytes into a zeroed slot changes a negative value to a
                # large positive one on the native 64-bit executor.
                client.append(f"    packet.message.args[{i}]=(uint64_t)(int64_t){arg};")
                host.append(f"    {typ} {arg}=({typ})(int64_t)dg_wire_message->args[{i}];")
            else:
                client.append(f"    packet.message.args[{i}]=DGBits(&{arg},sizeof({arg}));")
                host.append(f"    {typ} {arg}=0;memcpy(&{arg},&dg_wire_message->args[{i}],sizeof({arg}));")
        else:
            size, element, output = shapes[arg]
            if field == "get_tex_image":
                host += [
                    "    GLint width=0,height=0,pack_buffer=0;",
                    "    if(context->inside_begin || (target!=GL_TEXTURE_RECTANGLE_ARB && target!=GL_TEXTURE_2D)) { context->error=GL_INVALID_OPERATION;return 0; }",
                    "    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack_buffer);if(pack_buffer) { context->error=GL_INVALID_OPERATION;return 0; }",
                    "    glGetTexLevelParameteriv(target,level,GL_TEXTURE_WIDTH,&width);",
                    "    glGetTexLevelParameteriv(target,level,GL_TEXTURE_HEIGHT,&height);",
                    "    if(width<=0 || height<=0)return 0;",
                ]
            if not size.startswith("DGStringBytes"):
                host.append(f"    {typ} {arg}=NULL;")
            else:
                host.append(f"    {typ} {arg}=DGHostString(dg_wire_message,{i});if(!{arg}) return -1;")
            inputs.append((i, typ, arg, size, output))
            if output:
                output_args.append(arg)
    for i, typ, arg, size, output in inputs:
        if compressed_upload:
            # A PBO byte offset is not a client-memory span. Both meanings
            # are negotiated, bounded and decoded explicitly, including zero.
            client += ["    static const unsigned char empty_compressed=0;",
                f"    if(context->pixel_buffer[0]) {{ packet.message.args[15]=1;packet.message.args[{i}]=(uintptr_t){arg}; }}",
                f"    else DGPacketPointer(&packet,{i},imageSize?{arg}:({arg}?&empty_compressed:NULL),imageSize?(size_t)imageSize:1,0);"]
            host += [f"    int compressed_input=DGHostCompressedInput(context,dg_wire_message,target,imageSize,{i},&{arg});",
                "    if(compressed_input<=0)return compressed_input;"]
            continue
        # Native GL owns exactly which output bytes change. Seed bounded
        # caller storage so errors, short names/logs and partial arrays do not
        # become zero writes. No extra error query or round trip is needed.
        pointer = f"n?{arg}:NULL" if field in {"gen_programs_ARB", "delete_programs_ARB"} else f"len?{arg}:NULL" if field == "program_string_ARB" else f"DGCallListsBytes(n,type)?{arg}:NULL" if field == "call_lists" else arg
        if output:
            client.append(f"    DGPacketOutput(&packet,context,{i},{arg},({size}));")
        elif arg in query_inputs:
            client.append(f"    DGPacketQueryInput(&packet,{i},{arg},({size}));")
        else:
            client.append(f"    DGPacketPointer(&packet,{i},{pointer},({size}),0);")
        if not size.startswith("DGStringBytes"):
            if field in {"label_object_EXT", "insert_event_marker_EXT", "push_group_marker_EXT"}:
                host.append(f"    {arg}=length==0 ? ({typ})DGHostString(dg_wire_message,{i}) : ({typ})DGHostPointer(dg_wire_message,{i},DGArrayBytes(length,1));")
                host.append(f"    if((length && !{arg}) || (dg_wire_message->args[{i}] && !{arg})) return -1;")
                continue
            # DGHostPointer intentionally rejects zero-length spans globally.
            # Only query/generator arrays use a one-byte private placeholder
            # when native GL will ignore their nonpositive-capacity storage.
            span = f"({size}) ? ({size}) : 1" if output or arg in query_inputs else size
            host.append(f"    {arg}=({typ})DGHostPointer(dg_wire_message,{i},({span}));")
            # Null is legal for optional GL data/result parameters; malformed
            # non-null offsets, not ordinary GL nulls, fail the connection.
            optional = (field in {"tex_image1D", "tex_image2D", "tex_image3D", "buffer_data"} or
                        (field in {"get_shader_info_log", "get_program_info_log", "get_info_log_ARB", "get_shader_source_ARB",
                                   "get_active_uniform_ARB", "get_active_attrib_ARB"} and arg == "length") or
                        (field == "get_attached_shaders" and arg == "count"))
            if field in {"gen_programs_ARB", "delete_programs_ARB", "program_string_ARB"}:
                count = "len" if field == "program_string_ARB" else "n"
                condition = f"({count} && !{arg}) || (dg_wire_message->args[{i}] && !{arg})"
            elif field == "tex_sub_image1D":
                condition = f"(width && !{arg}) || (dg_wire_message->args[{i}] && !{arg})"
            elif field == "tex_sub_image3D":
                condition = f"(width && height && depth && !{arg}) || (dg_wire_message->args[{i}] && !{arg})"
            elif field == "get_attached_shaders" and arg == "shaders":
                condition = f"(maxCount && !{arg}) || (dg_wire_message->args[{i}] && !{arg})"
            elif field == "call_lists":
                condition = f"(DGCallListsBytes(n,type) && !{arg}) || (dg_wire_message->args[{i}] && !{arg})"
            else:
                condition = f"dg_wire_message->args[{i}] && !{arg}" if optional else f"!{arg}"
            host.append(f"    if({condition}) return -1;")
    if name == "glPixelStorei":
        client.append("    DGRecordPixelStore(context,pname,param);")
    if name == "glPixelStoref":
        client.append("    DGRecordPixelStoreFloat(context,pname,param);")
    if name == "glBindBuffer":
        client.append("    DGRecordBuffer(context,target,buffer);")
    if name == "glClientActiveTexture":
        client.append("    DGClientTexture(context,target);")
    if name == "glBindVertexArrayAPPLE":
        client.append(f"    if(!DGBindVertexArray(context,{parsed[0][1]})) {{ DGPacketFree(&packet);return; }}")
    sync = ret != "void" or output_args or name in {"glFinish", "glFinishFenceAPPLE", "glFinishObjectAPPLE"}
    client.append(f"    struct DGReply reply=DGSubmit(context,&packet,{int(bool(sync))});")
    if name == "glFlush":
        client.append("    DGFlushTransport(context);")
    for i, typ, arg, size, output in inputs:
        if output:
            client.append(f"    DGPacketCopy(&packet,{i},{arg},({size}));")
    if name == "glDeleteVertexArraysAPPLE":
        client.append(f"    if(!packet.failed && !context->connection->dead)DGForgetVertexArrays(context,n,{parsed[1][1]});")
    if name == "glDeleteBuffers":
        client.append("    if(!packet.failed && !context->connection->dead)DGForgetClientBuffers(context,n,buffers);")
    if name == "glGenVertexArraysAPPLE":
        # A native name may be recycled after deletion. Drop any old local
        # attribute/divisor cache rather than attaching it to the new VAO.
        client.append(f"    if(packet.valid_reply) {{ DGForgetVertexArrays(context,n,{parsed[1][1]});DGGeneratedVertexArrays(context,n,{parsed[1][1]}); }}")
    client.append("    DGPacketFree(&packet);")
    if ret == "void":
        client.append("    (void)reply;")
    else:
        client.append(f"    {ret} result=0;memcpy(&result,&reply.result,sizeof(result));")
        if location_query:
            client.append(f"    if(packet.valid_reply)DGProgramLocationPut(context,{location_program},name,location_generation,result);")
        client.append("    return result;")
    client.append("}")
    if field in shader_handles:
        host += ["    " + shader_handles[field], "    return 0;", "}"]
        continue
    if field == "link_program":
        host += [f"    DGForgetUniformShapes(context,{parsed[0][1]});",
                 f"    DGHostLinkProgram(context,{parsed[0][1]});", "    return 0;", "}"]
        continue
    if field == "bind_framebuffer_EXT":
        host += ["    glBindFramebufferEXT(target,framebuffer ? framebuffer : context->framebuffer);",
                 "    if(target!=GL_READ_FRAMEBUFFER_EXT)context->iosurface_draw_dirty=1;", "    return 0;", "}"]
        continue
    vao_helpers={"glBindVertexArrayAPPLE":"DGBindHostVertexArray", "glGenVertexArraysAPPLE":"DGGenHostVertexArrays",
                 "glDeleteVertexArraysAPPLE":"DGDeleteHostVertexArrays", "glIsVertexArrayAPPLE":"DGIsHostVertexArray"}
    if name in vao_helpers:
        call=vao_helpers[name]+"(context,"+",".join(arg for _,arg in parsed)+")"
        host.append("    "+(call+";" if ret=="void" else "reply->result="+call+";"))
        host += ["    return 0;", "}"]
        continue
    if field in {"draw_buffer", "read_buffer"}:
        host += [f"    DGSetBuffer(context,mode,{int(field == 'read_buffer')});"]
        if field == "draw_buffer":host.append("    context->iosurface_draw_dirty=1;")
        host += ["    return 0;", "}"]
        continue
    if field == "draw_buffers_ARB":
        host += ["    DGSetDrawBuffers(context,n,bufs);", "    return 0;", "}"]
        continue
    if field in {"pixel_storei", "pixel_storef"}:
        host.append("    if(pname==GL_UNPACK_CLIENT_STORAGE_APPLE) { context->client_storage=!!param;return 0; }")
    if field in {"tex_image3D", "tex_sub_image3D"}:
        if field=="tex_image3D":
            host.append(f"    DGHostTexture3D(context,0,target,level,{parsed[2][1]},0,0,0,width,height,depth,border,format,type,pixels,0);")
        else:
            host.append("    DGHostTexture3D(context,1,target,level,0,xoffset,yoffset,zoffset,width,height,depth,0,format,type,pixels,0);")
        host += ["    return 0;", "}"]
        continue
    if field in {"test_fence_APPLE", "finish_fence_APPLE"}:
        host.append("    struct DGFenceName *f=DGFence(context,fence,0);")
        host.append("    if(f && !f->set) { reply->result=GL_TRUE;return 0; }")
    host.append(f"    typedef {ret} (*Proc)({signature});")
    host.append(f'    static Proc proc;if(!proc) proc=(Proc)dlsym(RTLD_DEFAULT,"{name}");')
    host.append("    if(!proc) { context->error=GL_INVALID_OPERATION;return 0; }")
    if field == "begin":
        # Mavericks' native menu-bar/Apple-logo compositor uses immediate
        # quads, not the array codec. Capture before glBegin: queries within
        # begin/end are invalid. This is diagnostic-only, never a hot check
        # that reads back textures during ordinary rendering.
        host.append("    if(dg_diagnostics && !context->inside_begin) DGTraceProgram(context,dg_wire_message,NULL,0);")
    if field in {"begin", "draw_pixels", "copy_pixels", "bitmap", "call_list", "call_lists",
                 "eval_mesh1", "eval_mesh2"} or re.fullmatch(r"rect[dfis]v?", field):
        host.append(f"    DGPrepareDrawableAliases(context,{int(field in {'call_list', 'call_lists'})});")
    if field in {"begin", "clear", "draw_pixels", "copy_pixels", "bitmap", "call_list", "call_lists",
                 "eval_mesh1", "eval_mesh2", "blit_framebuffer_EXT"} or re.fullmatch(r"rect[dfis]v?", field):
        host.append("    if(!DGDrawableWritable(context)) return 0;")
    if field == "vertex2f":
        host.append('    if(dg_diagnostics && context->immediate_vertex_budget) { --context->immediate_vertex_budget;fprintf(stderr,"direct immediate context=%p program=%d vertex=%g,%g\\n",(void *)context,context->traced_immediate_program,(double)x,(double)y); }')
    args = ",".join(arg for _, arg in parsed)
    if field == "program_string_ARB":
        host.append('    static const unsigned char empty_program=0;if(!len)string=&empty_program;')
        host += ['    DGHostARBProgramString(context,target,format,len,string);', '    return 0;', '}']
        continue
    if field == "get_programiv_ARB":
        host += ['    DGHostGetARBProgramiv(context,target,pname,params);', '    return 0;', '}']
        continue
    alias_redefine = field in {"tex_image2D", "copy_tex_image2D", "compressed_tex_image2D", "tex_storage2D"}
    if alias_redefine:
        level="0" if field=="tex_storage2D" else "level"
        host.append(f"    GLuint redefining_alias=DGBeginAliasRedefinition(context,target,{level});")
    if field in {"flush", "finish"}:
        host.append("    DGRefreshFlippedAliases(context,0);")
    if field in {"call_list", "call_lists"}:
        host += ["#ifdef VIM_ALIAS_REBIND_EXPERIMENT", "    DGInvalidateAliasStorage(context);", "#endif"]
    if field in {"tex_sub_image2D", "compressed_tex_sub_image2D"}:
        host.append("    if(!DGPrepareScopedTextureCPUWrite(context,target))return 0;")
    # Native compatibility GL permits creating ANY unused name by binding,
    # without GenTextures. Private drawable/import registrations must vacate
    # it before the public command. No guest name translation or pixel copy.
    if field == "bind_texture":
        host += ["    if(texture && !context->inside_begin && (target==GL_TEXTURE_1D || target==GL_TEXTURE_2D || target==GL_TEXTURE_3D || target==GL_TEXTURE_CUBE_MAP || target==GL_TEXTURE_RECTANGLE_ARB) && DGPrivateTextureContains(context->alias_group->private_textures,texture)){",
                 "        GLenum prior=glGetError();if(prior && !context->error)context->error=prior;",
                 "        GLenum moved=DGPrivateTextureClaim(context->alias_group->private_textures,texture);",
                 "        if(moved){if(!context->error)context->error=moved;return 0;}", "    }"]
    if field == "is_texture":
        host.append("    if(!context->inside_begin && DGPrivateTextureContains(context->alias_group->private_textures,texture)){reply->result=GL_FALSE;return 0;}")
    if field == "delete_textures":
        host += ["    if(n>=0 && !context->inside_begin){",
                 "        for(GLsizei i=0;i<n;++i)if(!DGPrivateTextureContains(context->alias_group->private_textures,textures[i]))proc(1,textures+i);",
                 "        DGForgetAliases(context,n,textures);return 0;", "    }"]
    if field == "are_textures_resident":
        host += ["    if(n>=0 && !context->inside_begin)for(GLsizei i=0;i<n;++i)",
                 "        if(DGPrivateTextureContains(context->alias_group->private_textures,textures[i])){context->error=GL_INVALID_VALUE;reply->result=GL_FALSE;return 0;}"]
    if field == "prioritize_textures":
        host += ["    if(n>=0 && !context->inside_begin){",
                 "        for(GLsizei i=0;i<n;++i)if(!DGPrivateTextureContains(context->alias_group->private_textures,textures[i]))proc(1,textures+i,priorities+i);",
                 "        return 0;", "    }"]
    if field.startswith("framebuffer_texture"):
        host.append("    if(!context->inside_begin && texture && DGPrivateTextureContains(context->alias_group->private_textures,texture)){context->error=GL_INVALID_OPERATION;return 0;}")
    host.append((f"    {ret} result=" if ret != "void" else "    ") + f"proc({args});")
    if field == "bind_texture":
        host += ["    if(dg_diagnostics && !context->inside_begin){",
                 "        GLenum error=glGetError();if(error){if(!context->error)context->error=error;",
                 "            if(context->texture_debug_count++<32)fprintf(stderr,\"PassthroughOpenGL native bind failed context=%p target=%x texture=%u error=%x\\n\",(void *)context,target,texture,error);}","    }"]
    if field in {"push_client_attrib", "pop_client_attrib"}:
        host.append("    context->array_enable_valid=0;")
    if field == "texture_barrier_NV":
        host.append("    DGRefreshFlippedAliases(context,0);")
    if field in {"buffer_data", "buffer_sub_data"}:
        host.append("    DGPBOWritten(context);")
    if field in {"program_binary", "link_program", "delete_program"}:
        host.append(f"    DGForgetUniformShapes(context,{parsed[0][1]});")
    if field in {"call_list", "call_lists"}:
        host.append("    DGForgetUniformShapes(context,0);")
    if field in {"flush", "finish"}:
        host.append("    DGSubmitIOSurfaceWrites(context);")
    if alias_redefine:
        host.append("    DGEndAliasRedefinition(context,redefining_alias);")
    if field == "bind_texture":
        host.append("    DGRecordAliasBinding(context,target);")
    if field in {"pop_attrib", "call_list", "call_lists"}:
        host.append("    context->alias_bindings_dirty=1;")
    if field == "delete_textures":
        host.append("    DGForgetAliases(context,n,textures);")
    if field.startswith("framebuffer_texture") or field in {"framebuffer_renderbuffer_EXT", "delete_framebuffers_EXT", "draw_buffers_ARB", "pop_attrib", "call_list", "call_lists"}:
        host.append("    context->iosurface_draw_dirty=1;")
    if field in {"call_list", "call_lists"}:
        host.append("    context->iosurface_mark_all=1;")
    if field in {"tex_sub_image2D", "copy_tex_sub_image2D", "compressed_tex_sub_image2D"}:
        host.append("    DGMarkIOSurfaceTextureWrite(context,target);")
    if field == "tex_image2D":
        host.append("    DGTraceTexture(context,width,height,format,type,pixels);")
    if field == "read_pixels":
        host.append("    DGTraceRead(context,width,height,format,type,pixels);")
    if field == "begin":
        host.append("    context->inside_begin=1;")
    if field == "end":
        host.append("    context->inside_begin=0;")
    # Conservative native scissor bounds, accumulated over the whole batch.
    # Not the mutable CGS surface shape queried after submission.
    if field in {"end", "draw_pixels", "copy_pixels", "bitmap", "call_list", "call_lists",
                 "eval_mesh1", "eval_mesh2", "blit_framebuffer_EXT"} or re.fullmatch(r"rect[dfis]v?", field):
        host.append("    DGMarkDrawableDamage(context);")
    if field == "clear":
        host.append("    if(mask&GL_COLOR_BUFFER_BIT) DGMarkDrawableDamage(context);")
    if field == "gen_fences_APPLE":
        host.append("    for(GLsizei i=0;i<n;++i) DGFence(context,fences[i],1);")
    if field == "delete_fences_APPLE":
        host.append("    for(GLsizei i=0;i<n;++i) DGForgetFence(context,fences[i]);")
    if field == "set_fence_APPLE":
        host.append("    struct DGFenceName *f=DGFence(context,fence,0);if(f) f->set=1;")
    if field in {"get_integerv", "get_floatv", "get_doublev", "get_booleanv", "get_integer64v_sync"}:
        out_type = parsed[1][0].replace("*", "").strip()
        host.append(f"    GLint virtual_value=0;if(params && DGVirtualInteger(context,pname,&virtual_value)) *params=({out_type})virtual_value;")
    if ret != "void":
        host.append("    reply->result=DGBits(&result,sizeof(result));")
    host += ["    return 0;", "}"]

digest = hashlib.sha256(text.encode()).hexdigest()
a.output.mkdir(parents=True, exist_ok=True)
for filename, lines in [("direct-client-generated.inc", client), ("direct-host-generated.inc", host),
                        ("direct-declarations.inc", decl), ("direct-lookup.inc", mapping)]:
    (a.output / filename).write_text("/* Generated from Mavericks SDK; do not edit. */\n" + "\n".join(lines) + "\n")
(a.output / "direct-layout.h").write_text(f'#define DG_LAYOUT "{digest}"\n')
# Typed research-wire fixtures must follow the same SDK-derived indices as
# the generated encoder/decoder, rather than hand-maintain numeric GL slots.
(a.output / "direct-opcodes.h").write_text(
    "/* Generated from Mavericks SDK; do not edit. */\n" +
    "\n".join(f"#define DG_OP_{field} {i}u" for i, (_, field, _) in enumerate(rows)) + "\n")
# Conservative pixel-write tracking for an explicit shared-target scope.
# Unlisted/unknown commands invalidate. Query/selection/state operations below
# cannot write color pixels; display-list execution and texture uploads still
# invalidate even when a particular invocation happens to be a no-op/error.
backing_read_only = {"flush", "finish", "bind_texture", "active_texture",
                     "bind_framebuffer_EXT", "read_buffer", "draw_buffer",
                     "viewport", "scissor", "color_mask", "enable", "disable"}
(a.output / "direct-backing-read-only.inc").write_text(
    "/* Generated from Mavericks SDK; do not edit. */\n"
    "static const unsigned char dg_backing_read_only[974]={\n" +
    "\n".join(f"    [{i}]=1, /* {field} */" for i, (_, field, _) in enumerate(rows)
              if field.startswith(("get_", "is_")) or field in backing_read_only) + "\n};\n")
(a.output / "direct-coverage.json").write_text(json.dumps(report, indent=2) + "\n")
(a.output / "passthrough-opengl-gli-map.json").write_text(json.dumps(dict(table=gli_map), indent=2) + "\n")
# Derive mutation slots from the target SDK, never baked-in ABI indices. This
# table is consulted under the share connection's submission lock, not before
# queuing, so another context cannot repopulate between invalidation and link.
program_ops = {
    "create_program_object_ARB": "DG_LOCATION_CREATE", "create_program": "DG_LOCATION_CREATE",
    "create_shader_programv": "DG_LOCATION_CREATE", "create_shader_program_EXT": "DG_LOCATION_CREATE",
    "link_program_ARB": "DG_LOCATION_LINK", "link_program": "DG_LOCATION_LINK",
    "program_binary": "DG_LOCATION_LINK",
    "delete_object_ARB": "DG_LOCATION_DELETE", "delete_program": "DG_LOCATION_DELETE",
    # Conservative opaque/list execution: do not presume which GL object
    # operations an existing native display list may contain.
    "call_list": "DG_LOCATION_FORGET", "call_lists": "DG_LOCATION_FORGET",
}
(a.output / "direct-program-location-ops.inc").write_text(
    "/* Generated from Mavericks SDK; do not edit. */\n"
    "static const unsigned char dg_program_location_ops[974]={\n" +
    "\n".join(f"    [{i}]={program_ops[field]}, /* {field} */"
              for i, (_, field, _) in enumerate(rows) if field in program_ops) + "\n};\n")
# Audited operations that cannot change ANY field in DGCommonState. This is
# deliberately not a "setter versus getter" heuristic: private commands,
# matrices, attribute stacks, display lists, active-unit changes, bindings and
# every unknown operation still invalidate the full ordered native snapshot.
# In particular an enable/hint/parameter error must still reach glGetError;
# preserving these unrelated values does not synthesize/clear native errors.
common_neutral = {
    "enable", "disable", "hint", "pixel_storef", "pixel_storei",
    "tex_parameterf", "tex_parameterfv", "tex_parameteri", "tex_parameteriv",
    "uniform1i_ARB", "get_booleanv", "get_integerv", "get_floatv", "get_doublev",
    # Actual desktop traffic includes immediate-mode quads and CPU texture
    # uploads, not only DG_DRAW packets. None changes the six snapshot fields:
    # viewport, model/projection, draw FBO/buffer or rectangle texture binding.
    # Preserve the native commands/errors, and keep queries inside Begin/End
    # on the existing uncached error-producing route. List/attribute playback,
    # bindings, matrix/viewport changes and unknown operations still invalidate.
    "begin", "end", "vertex2f", "tex_coord2f", "color4f", "color4ub",
    "alpha_func", "blend_func", "scissor", "clear", "clear_color",
    "tex_image2D", "tex_sub_image2D", "flush", "finish",
    # Measured Tiger compositor traffic uses a second coordinate set and
    # texture-combiner changes for virtually every quad. Neither changes
    # viewport, matrices, draw FBO/buffer, active unit or rectangle binding.
    # These commands STILL execute natively, including error validation.
    "multi_tex_coord2d", "multi_tex_coord2dv", "multi_tex_coord2f", "multi_tex_coord2fv",
    "multi_tex_coord2i", "multi_tex_coord2iv", "multi_tex_coord2s", "multi_tex_coord2sv",
    "tex_envf", "tex_envfv", "tex_envi", "tex_enviv", "blend_equation",
    "blend_equation_separate_EXT", "is_enabled", "test_fence_APPLE", "finish_fence_APPLE",
    "test_object_APPLE", "finish_object_APPLE",
    # Yosemite's live-backdrop compositor repeatedly sets shader uniforms and
    # asks for their locations between native viewport/FBO/matrix queries.
    # These change program state, not any of the six captured fields. Keep
    # executing every command and preserving native errors; do not discard an
    # otherwise valid snapshot merely because a different uniform was set.
    "use_program", "use_program_object_ARB",
    "get_uniform_location", "get_uniform_location_ARB",
    "object_purgeable_APPLE", "object_unpurgeable_APPLE",
    # Sierra's active-backdrop compositor asks for rectangle WRAP_T each
    # frame. A real native texture query cannot change any of the six common
    # fields. Previously it discarded them all, forcing another synchronous
    # native snapshot later in the same render pass. Keep executing queries
    # (including invalid enums) and returning their actual values/errors;
    # this does NOT cache texture parameters or infer successful setters.
    "get_tex_parameteriv", "get_tex_parameterfv",
    "get_tex_level_parameteriv", "get_tex_level_parameterfv",
    "get_tex_enviv", "get_tex_envfv", "is_texture",
}
for _, field, _ in rows:
    if re.fullmatch(r"uniform[1234](?:f|i|fv|iv)(?:_ARB)?", field) or \
       re.fullmatch(r"uniform_matrix[234](?:x[234])?fv(?:_ARB)?", field):
        common_neutral.add(field)
# A native snapshot has six independently valid fields. Partial invalidation
# does not simulate setters: values are still refilled from the actual host
# on the next affected query. Matrix mode only selects subsequent operations;
# any matrix mutation conservatively invalidates BOTH captured matrices.
common_retained = {name: "DG_COMMON_ALL" for name in common_neutral}
common_retained.update({
    "viewport": "(DG_COMMON_ALL&~DG_COMMON_VIEWPORT)",
    "bind_texture": "(DG_COMMON_ALL&~DG_COMMON_RECTANGLE)",
    "delete_textures": "(DG_COMMON_ALL&~DG_COMMON_RECTANGLE)",
    "active_texture": "(DG_COMMON_ALL&~DG_COMMON_RECTANGLE)",
    "bind_framebuffer_EXT": "(DG_COMMON_ALL&~(DG_COMMON_FRAMEBUFFER|DG_COMMON_DRAW_BUFFER))",
    "delete_framebuffers_EXT": "(DG_COMMON_ALL&~(DG_COMMON_FRAMEBUFFER|DG_COMMON_DRAW_BUFFER))",
    "draw_buffer": "(DG_COMMON_ALL&~DG_COMMON_DRAW_BUFFER)",
    "draw_buffers_ARB": "(DG_COMMON_ALL&~DG_COMMON_DRAW_BUFFER)",
    "matrix_mode": "DG_COMMON_ALL", "push_matrix": "DG_COMMON_ALL",
})
for field in ("load_identity", "load_matrixf", "load_matrixd", "mult_matrixf", "mult_matrixd",
              "translatef", "translated", "rotatef", "rotated", "scalef", "scaled",
              "ortho", "frustum", "pop_matrix"):
    common_retained[field] = "(DG_COMMON_ALL&~(DG_COMMON_MODELVIEW|DG_COMMON_PROJECTION))"
(a.output / "direct-common-neutral-ops.inc").write_text(
    "/* Generated from Mavericks SDK; do not edit. */\n"
    "static const unsigned char dg_common_neutral_ops[974]={\n" +
    "\n".join(f"    [{i}]={common_retained[field]}, /* {field} */" for i, (_, field, _) in enumerate(rows)
              if field in common_retained) + "\n};\n")
# These SDK Get limits are scalar except explicitly handled vector limits.
# Generate the full limit/binding family rather than discover each enum one
# boot at a time. Unknown non-family pnames still fail closed.
limits = {}
for enum, value in re.findall(r"#define\s+(GL_\w+)\s+(0[xX][0-9a-fA-F]+)", public):
    if enum.startswith("GL_MAX_") or enum.endswith("_BINDING"):
        limits[int(value, 16)] = enum
for value in (0x0D3A, 0x91BE, 0x91BF):  # viewport dims, compute work-group vectors
    limits.pop(value, None)
(a.output / "direct-get-counts.inc").write_text("\n".join(
    f"case 0x{value:x}: return 1; /* {name} */" for value, name in sorted(limits.items())) + "\n")
print(f"PassthroughOpenGL: {sum(r['supported'] for r in report)} supported / {len(rows)} GLI slots; SDK {digest[:12]}")
