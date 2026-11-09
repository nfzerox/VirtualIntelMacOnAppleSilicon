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
           "sample_pass":"glSamplePass",
           # These real public entry points are declared only in gl3ext.h.
           # Their GLI signatures are scalar-only; use the existing typed
           # generator and native validation, not a mutable TexImage substitute.
           "tex_storage1D":"glTexStorage1D", "tex_storage2D":"glTexStorage2D", "tex_storage3D":"glTexStorage3D",
           "get_buffer_parameteri64v":"glGetBufferParameteri64v", "copy_buffer_sub_data":"glCopyBufferSubData",
           "clear_bufferiv":"glClearBufferiv", "clear_bufferuiv":"glClearBufferuiv",
           "clear_bufferfv":"glClearBufferfv", "clear_bufferfi":"glClearBufferfi",
           "get_query_objecti64v":"glGetQueryObjecti64vEXT",
           "get_query_objectui64v":"glGetQueryObjectui64vEXT",
           "query_counter":"glQueryCounter",
           "get_uniform_indices":"glGetUniformIndices", "get_active_uniformsiv":"glGetActiveUniformsiv",
           "get_active_uniform_name":"glGetActiveUniformName", "get_uniform_block_index":"glGetUniformBlockIndex",
           "get_active_uniform_blockiv":"glGetActiveUniformBlockiv",
           "get_active_uniform_block_name":"glGetActiveUniformBlockName", "uniform_block_binding":"glUniformBlockBinding",
           "get_integer64i_v":"glGetInteger64i_v",
           "gen_samplers":"glGenSamplers", "delete_samplers":"glDeleteSamplers",
           "is_sampler":"glIsSampler", "bind_sampler":"glBindSampler",
           "sampler_parameteri":"glSamplerParameteri", "sampler_parameteriv":"glSamplerParameteriv",
           "sampler_parameterf":"glSamplerParameterf", "sampler_parameterfv":"glSamplerParameterfv",
           "sampler_parameterIiv":"glSamplerParameterIiv", "sampler_parameterIuiv":"glSamplerParameterIuiv",
           "get_sampler_parameteriv":"glGetSamplerParameteriv", "get_sampler_parameterfv":"glGetSamplerParameterfv",
           "get_sampler_parameterIiv":"glGetSamplerParameterIiv", "get_sampler_parameterIuiv":"glGetSamplerParameterIuiv",
           "tex_buffer":"glTexBuffer",
           "tex_image2D_multisample":"glTexImage2DMultisample",
           "tex_image3D_multisample":"glTexImage3DMultisample",
           "get_multisamplefv":"glGetMultisamplefv", "sample_maski":"glSampleMaski",
           "bind_frag_data_location_indexed":"glBindFragDataLocationIndexed",
           "get_frag_data_index":"glGetFragDataIndex",
           "blend_func_i":"glBlendFunci", "blend_func_separate_i":"glBlendFuncSeparatei",
           "blend_equation_i":"glBlendEquationi", "blend_equation_separate_i":"glBlendEquationSeparatei",
           "viewport_arrayv":"glViewportArrayv","viewport_indexedf":"glViewportIndexedf",
           "viewport_indexedfv":"glViewportIndexedfv","scissor_arrayv":"glScissorArrayv",
           "scissor_indexed":"glScissorIndexed","scissor_indexedv":"glScissorIndexedv",
           "depth_range_arrayv":"glDepthRangeArrayv","depth_range_indexed":"glDepthRangeIndexed",
           "get_floati_v":"glGetFloati_v","get_doublei_v":"glGetDoublei_v",
           "patch_parameteri":"glPatchParameteri","patch_parameterfv":"glPatchParameterfv",
           "draw_arrays_instanced":"glDrawArraysInstanced", "draw_elements_instanced":"glDrawElementsInstanced",
           "vertex_attrib_divisor":"glVertexAttribDivisor",
           "primitive_restart_index":"glPrimitiveRestartIndex",
           "get_stringi":"glGetStringi", "get_integer64v_sync": "glGetInteger64v", "bind_vertex_array_ARB": "glBindVertexArrayAPPLE",
           "gen_vertex_arrays_ARB": "glGenVertexArraysAPPLE", "delete_vertex_arrays_ARB": "glDeleteVertexArraysAPPLE",
           "is_vertex_array_ARB": "glIsVertexArrayAPPLE", "bind_vertex_array_EXT": "glBindVertexArrayAPPLE",
           "gen_vertex_arrays_EXT": "glGenVertexArraysAPPLE", "delete_vertex_arrays_EXT": "glDeleteVertexArraysAPPLE",
           "is_vertex_array_EXT": "glIsVertexArrayAPPLE"}
pipeline_names = {
    "use_program_stages":"glUseProgramStages", "active_shader_program":"glActiveShaderProgram",
    "create_shader_programv":"glCreateShaderProgramv", "bind_program_pipeline":"glBindProgramPipeline",
    "delete_program_pipelines":"glDeleteProgramPipelines", "gen_program_pipelines":"glGenProgramPipelines",
    "is_program_pipeline":"glIsProgramPipeline", "get_program_pipelineiv":"glGetProgramPipelineiv",
    "validate_program_pipeline":"glValidateProgramPipeline", "get_program_pipeline_info_log":"glGetProgramPipelineInfoLog",
}
program_uniform_pattern = r"program_uniform(?:[1234](?:ui|[ifd])v?|_matrix[234](?:x[234])?[fd]v)"
for _, field, _ in rows:
    if re.fullmatch(program_uniform_pattern,field):
        pipeline_names[field]=re.sub(r"_(\w)",lambda m:m[1].upper(),"gl_"+field)
aliases.update(pipeline_names)
shader_include_names = {
    "named_string_ARB": "glNamedStringARB", "delete_named_string_ARB": "glDeleteNamedStringARB",
    "compile_shader_include_ARB": "glCompileShaderIncludeARB", "is_named_string_ARB": "glIsNamedStringARB",
    "get_named_string_ARB": "glGetNamedStringARB", "get_named_string_iv_ARB": "glGetNamedStringivARB",
}
aliases.update(shader_include_names)
apple_evaluators = {
    "enable_vertex_attrib_ARB": "glEnableVertexAttribAPPLE",
    "disable_vertex_attrib_ARB": "glDisableVertexAttribAPPLE",
    "is_vertex_attrib_enabled_ARB": "glIsVertexAttribEnabledAPPLE",
    **{f"map_vertex_attrib{dim}{kind}_ARB": f"glMapVertexAttrib{dim}{kind}APPLE"
       for dim in (1, 2) for kind in ("d", "f")},
}
aliases.update(apple_evaluators)
aliases["get_uniform_buffer_offset_EXT"]="glGetUniformOffsetEXT"
aliases["point_size_pointer"]="glPointSizePointerAPPLE"
apple_evaluator_getters={"get_vertex_attribiv_ARB":0, "get_vertex_attribfv_ARB":1, "get_vertex_attribdv_ARB":2}
fp64_names = {"get_uniformdv":"glGetUniformdv"}
for _, field, _ in rows:
    if re.fullmatch(r"uniform(?:[1234]dv?|_matrix[234](?:x[234])?dv)",field):
        fp64_names[field]=re.sub(r"_(\w)",lambda m:m[1].upper(),"gl_"+field)
aliases.update(fp64_names)
fp64_vertex_names = {"vertex_attrib_lpointer":"glVertexAttribLPointer",
                     "get_vertex_attrib_ldv":"glGetVertexAttribLdv"}
for n in range(1,5):
    for suffix in ("d","dv"):
        fp64_vertex_names[f"vertex_attribl{n}{suffix}"]=f"glVertexAttribL{n}{suffix}"
aliases.update(fp64_vertex_names)
feedback_names = {
    "bind_transform_feedback":"glBindTransformFeedback", "gen_transform_feedbacks":"glGenTransformFeedbacks",
    "delete_transform_feedbacks":"glDeleteTransformFeedbacks", "pause_transform_feedback":"glPauseTransformFeedback",
    "resume_transform_feedback":"glResumeTransformFeedback", "is_transform_feedback":"glIsTransformFeedback",
    "draw_transform_feedback":"glDrawTransformFeedback", "begin_query_indexed":"glBeginQueryIndexed",
    "end_query_indexed":"glEndQueryIndexed", "get_query_indexediv":"glGetQueryIndexediv",
    "draw_transform_feedback_stream":"glDrawTransformFeedbackStream",
}
aliases.update(feedback_names)
subroutine_names = {
    "get_subroutine_uniform_location":"glGetSubroutineUniformLocation",
    "get_subroutine_index":"glGetSubroutineIndex",
    "get_active_subroutine_uniformiv":"glGetActiveSubroutineUniformiv",
    "get_active_subroutine_uniform_name":"glGetActiveSubroutineUniformName",
    "get_active_subroutine_name":"glGetActiveSubroutineName",
    "uniform_subroutinesuiv":"glUniformSubroutinesuiv",
    "get_uniform_subroutineuiv":"glGetUniformSubroutineuiv",
    "get_program_stageiv":"glGetProgramStageiv",
}
aliases.update(subroutine_names)
subroutine_string_results={"get_active_subroutine_name","get_active_subroutine_uniform_name"}
subroutine_name_queries={"get_subroutine_uniform_location","get_subroutine_index"}
es2_names = {
    "release_shader_compiler":"glReleaseShaderCompiler","shader_binary":"glShaderBinary",
    "get_shader_precision_format":"glGetShaderPrecisionFormat",
    "depth_rangef":"glDepthRangef","clear_depthf":"glClearDepthf",
}
aliases.update(es2_names)
packed_attrib_names={f"vertex_attribP{i}ui{suffix}":f"glVertexAttribP{i}ui{suffix}"
                     for i in range(1,5) for suffix in ("","v")}
program_binary_names={"get_program_binary":"glGetProgramBinary","program_binary":"glProgramBinary"}
aliases.update(packed_attrib_names)
aliases.update(program_binary_names)
aliases["min_sample_shading"]="glMinSampleShading"
aliases.update({"get_internal_formativ":"glGetInternalformativ","draw_arrays_indirect":"glDrawArraysIndirect",
                "draw_elements_indirect":"glDrawElementsIndirect"})
state_getters={"get_integerv","get_floatv","get_doublev","get_booleanv","get_integer64v_sync"}
weight_vectors={f"weight{suffix}v_ARB" for suffix in ("b","s","i","f","d","ub","us","ui")}
aliases.update({"vertex_array_range_EXT":"glVertexArrayRangeAPPLE",
                "flush_vertex_array_range_EXT":"glFlushVertexArrayRangeAPPLE",
                "vertex_array_parameteri_EXT":"glVertexArrayParameteriAPPLE"})
array_calls = {
    "draw_arrays_indirect": "DGDrawIndirect(context,mode,0,indirect,DG_DRAW_ARRAYS_INDIRECT);",
    "draw_elements_indirect": "DGDrawIndirect(context,mode,type,indirect,DG_DRAW_ELEMENTS_INDIRECT);",
    "draw_transform_feedback": "DGDrawFeedback(context,mode,name,0,DG_DRAW_FEEDBACK);",
    "draw_transform_feedback_stream": "DGDrawFeedback(context,mode,name,stream,DG_DRAW_FEEDBACK_STREAM);",
    "draw_arrays_instanced": "DGDrawInstances(context,mode,first,count,0,NULL,instancecount,1);",
    "draw_elements_instanced": "DGDrawInstances(context,mode,0,count,type,indices,instancecount,1);",
    "draw_elements_base_vertex": "DGDrawBase(context,mode,count,type,indices,1,DG_BASE_ELEMENTS,base_vertex,0,0);",
    "draw_range_elements_base_vertex": "DGDrawBase(context,mode,count,type,indices,1,DG_BASE_RANGE,base_vertex,start,end);",
    "draw_elements_instanced_base_vertex": "DGDrawBase(context,mode,count,type,indices,instancecount,DG_BASE_INSTANCED,base_vertex,0,0);",
    "multi_draw_arrays": "DGMultiDraw(context,mode,first,count,0,NULL,drawcount,NULL,DG_MULTI_ARRAYS);",
    "multi_draw_elements": "DGMultiDraw(context,mode,NULL,count,type,indices,drawcount,NULL,DG_MULTI_ELEMENTS);",
    "multi_draw_elements_base_vertex": "DGMultiDraw(context,mode,NULL,count,type,indices,drawcount,base_vertex,DG_MULTI_BASE);",
    "vertex_attrib_divisor": "DGArrayDivisor(context,index,divisor);",
    "vertex_pointer": "DGArrayPointer(context,0,size,type,stride,pointer,GL_FALSE);",
    "normal_pointer": "DGArrayPointer(context,1,3,type,stride,pointer,GL_FALSE);",
    "color_pointer": "DGArrayPointer(context,2,size,type,stride,pointer,GL_FALSE);",
    "secondary_color_pointer": "DGArrayPointer(context,3,size,type,stride,pointer,GL_FALSE);",
    "fog_coord_pointer": "DGArrayPointer(context,4,1,type,stride,pointer,GL_FALSE);",
    "edge_flag_pointer": "DGAuxArrayPointer(context,DG_VERTEX_EDGE_SLOT,GL_UNSIGNED_BYTE,stride,pointer);",
    "index_pointer": "DGAuxArrayPointer(context,DG_VERTEX_INDEX_SLOT,type,stride,pointer);",
    "weight_pointer_ARB": "DGWeightPointer(context,size,type,stride,pointer);",
    "point_size_pointer": "DGPointSizePointer(context,type,stride,pointer);",
    "interleaved_arrays": "DGInterleavedArrays(context,format,stride,pointer);",
    "get_pointerv": "DGGetClientPointer(context,pname,params);",
    "tex_coord_pointer": "DGArrayPointer(context,8+context->client_texture,size,type,stride,pointer,GL_FALSE);",
    "vertex_attrib_pointer_ARB": "if(index>=16) context->error=GL_INVALID_VALUE;else DGArrayPointer(context,16+index,size,type,stride,pointer,normalized);",
    "vertex_attribI_pointer_EXT": "DGIntegerArrayPointer(context,index,size,type,stride,pointer);",
    "vertex_attrib_lpointer": "DGDoubleArrayPointer(context,index,size,type,stride,pointer);",
    "enable_client_state": "DGEnableClientArray(context,array,1);",
    "disable_client_state": "DGEnableClientArray(context,array,0);",
    "element_pointer_APPLE": "DGAppleElementPointer(context,type,pointer);",
    "draw_element_array_APPLE": "DGDrawAppleElements(context,mode,0,0,0,&first,&count,1,0);",
    "draw_range_element_array_APPLE": "DGDrawAppleElements(context,mode,start,end,1,&first,&count,1,0);",
    "multi_draw_element_array_APPLE": "DGDrawAppleElements(context,mode,0,0,0,first,count,primcount,1);",
    "multi_draw_range_element_array_APPLE": "DGDrawAppleElements(context,mode,start,end,1,first,count,primcount,1);",
    "vertex_array_range_EXT": "DGAppleArrayRange(context,count,pointer,0);",
    "flush_vertex_array_range_EXT": "DGAppleArrayRange(context,count,pointer,1);",
    "vertex_array_parameteri_EXT": "DGAppleArrayHint(context,pname,param);",
    "enable_vertex_attrib_array_ARB": "if(index>=16) context->error=GL_INVALID_VALUE;else DGArrayEnable(context,16+index,1);",
    "disable_vertex_attrib_array_ARB": "if(index>=16) context->error=GL_INVALID_VALUE;else DGArrayEnable(context,16+index,0);",
    "draw_arrays": "DGDraw(context,mode,first,count,0,NULL);",
    "draw_elements": "DGDraw(context,mode,0,count,type,indices);",
    "draw_range_elements": "if(start>end){if(context && !context->error)context->error=GL_INVALID_VALUE;return;}DGDraw(context,mode,0,count,type,indices);",
    # APPLE_texture_range explicitly permits an indirect renderer to ignore
    # the mapping hint. No guest address is submitted to host OpenGL.
    "texture_range_APPLE": "DGTextureRange(context,target,length,pointer);",
    "get_tex_parameter_pointerv_APPLE": "DGTextureRangePointer(context,target,pname,params);",
}
integer_texture_fields = {"tex_parameterIiv_EXT", "tex_parameterIuiv_EXT",
                          "get_tex_parameterIiv_EXT", "get_tex_parameterIuiv_EXT"}
clear_buffer_inputs = {"clear_bufferiv":"GL_STENCIL", "clear_bufferuiv":None,
                       "clear_bufferfv":"GL_DEPTH"}
clear_buffer_fields = set(clear_buffer_inputs) | {"clear_bufferfi"}
clear_buffer_selectors = {
    f: "buffer==GL_COLOR" + (f" || buffer=={extra}" if extra else "")
    for f,extra in clear_buffer_inputs.items()
}
clear_buffer_selectors["clear_bufferfi"]="buffer==GL_DEPTH_STENCIL"
timer_query_results={"get_query_objecti64v":"signed_result", "get_query_objectui64v":"unsigned_result"}
timer_query_fields=set(timer_query_results) | {"query_counter"}
ubo_names={"get_active_uniform_name":"uniformName","get_active_uniform_block_name":"uniformBlockName"}
ubo_fields=set(ubo_names) | {"get_uniform_indices","get_active_uniformsiv","get_uniform_block_index",
                           "get_active_uniform_blockiv","uniform_block_binding"}
sampler_arrays={field for field in aliases if field.startswith(("sampler_parameter","get_sampler_parameter"))
                and field.endswith("v")}
sampler_fields=sampler_arrays | {"gen_samplers","delete_samplers","is_sampler","bind_sampler",
                                  "sampler_parameteri","sampler_parameterf"}
render_target_features = {
    "MULTISAMPLE_TEXTURE": {"tex_image2D_multisample","tex_image3D_multisample","get_multisamplefv","sample_maski"},
    "DUAL_SOURCE_BLEND": {"bind_frag_data_location_indexed","get_frag_data_index"},
    "INDEXED_BLEND": {"blend_func_i","blend_func_separate_i","blend_equation_i","blend_equation_separate_i"},
    "VIEWPORT_ARRAY": {"viewport_arrayv","viewport_indexedf","viewport_indexedfv","scissor_arrayv",
                       "scissor_indexed","scissor_indexedv","depth_range_arrayv","depth_range_indexed",
                       "get_floati_v","get_doublei_v"},
}
viewport_array_inputs={"viewport_arrayv":4,"scissor_arrayv":4,"depth_range_arrayv":2}
viewport_indexed_queries={"get_floati_v","get_doublei_v"}
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


pixel_map_setters = {"pixel_mapfv", "pixel_mapuiv", "pixel_mapusv"}
pixel_map_getters = {"get_"+field for field in pixel_map_setters}
pixel_map_fields = pixel_map_setters | pixel_map_getters
compat_raster_shapes = {
    "bitmap": "width,height,GL_COLOR_INDEX,GL_BITMAP",
    "draw_pixels": "width,height,format,type",
    "polygon_stipple": "32,32,GL_COLOR_INDEX,GL_BITMAP",
    "get_polygon_stipple": "32,32,GL_COLOR_INDEX,GL_BITMAP",
}
compat_pixel_fields = pixel_map_fields | compat_raster_shapes.keys()
compat_pixel_getters = pixel_map_getters | {"get_polygon_stipple"}
compact_texture_inputs = {"tex_image1D", "tex_sub_image1D", "tex_image2D", "tex_sub_image2D", "tex_image3D", "tex_sub_image3D"}

imaging_parameters = {
    prefix + suffix: operation
    for prefix, operation in {
        "color_table_parameter": "DG_COLOR_TABLE_SET",
        "get_color_table_parameter": "DG_COLOR_TABLE_GET",
        "convolution_parameter": "DG_CONVOLUTION_SET",
        "get_convolution_parameter": "DG_CONVOLUTION_GET",
        "get_histogram_parameter": "DG_HISTOGRAM_GET",
        "get_minmax_parameter": "DG_MINMAX_GET",
    }.items()
    for suffix in ("fv", "iv")
}
imaging_transfers = {
    "color_table": ("TABLE", "width", "1", "0"),
    "color_sub_table": ("SUBTABLE", "count", "1", "start"),
    "get_color_table": ("GET_TABLE", "0", "1", "0"),
    "convolution_filter1D": ("FILTER1D", "width", "1", "0"),
    "convolution_filter2D": ("FILTER2D", "width", "height", "0"),
    "get_convolution_filter": ("GET_FILTER", "0", "1", "0"),
    "separable_filter2D": ("SEPARABLE", "width", "height", "0"),
    "get_separable_filter": ("GET_SEPARABLE", "0", "1", "0"),
    "get_histogram": ("GET_HISTOGRAM", "0", "1", "0"),
    "get_minmax": ("GET_MINMAX", "0", "1", "0"),
}
combiner_arrays = {
    f"get_{stem}{suffix}_NV": 1
    for stem in ("combiner_input_parameter", "combiner_output_parameter", "final_combiner_input_parameter")
    for suffix in ("fv", "iv")
}
combiner_arrays.update(combiner_stage_parameterfv_NV=4, get_combiner_stage_parameterfv_NV=4)

def shape(field, param, typ, params):
    # Each expression is evaluated independently on both sides.
    if field in imaging_parameters:
        return f"DGImagingParameterCount({imaging_parameters[field]},pname)*sizeof({typ})"
    if field in combiner_arrays:
        return f"{combiner_arrays[field]}*sizeof({typ})"
    if field=="get_internal_formativ":
        return "DGArrayBytes(bufSize,sizeof(GLint))"
    if field in packed_attrib_names:
        return "sizeof(GLuint)"
    if field=="get_program_binary":
        return {"length":"sizeof(GLsizei)","binaryFormat":"sizeof(GLenum)","binary":"DGArrayBytes(bufSize,1)"}[param]
    if field=="program_binary":
        return "DGQueryArrayBytes(length,1)"
    if field=="get_shader_precision_format":
        return f"{2 if param=='range' else 1}*sizeof(GLint)"
    if field=="shader_binary":
        return "DGQueryArrayBytes(n<0 || length<0 ? 0 : n,sizeof(GLuint))" if param=="shaders" else "DGQueryArrayBytes(n<0 || length<0 ? 0 : length,1)"
    if field in subroutine_name_queries:
        return f"DGStringBytes({param})"
    if field in subroutine_string_results:
        return "sizeof(GLsizei)" if param=="length" else "DGArrayBytes(bufsize,1)"
    if field=="get_active_subroutine_uniformiv":
        return "DGQueryArrayBytes(compatible_count,sizeof(GLint))"
    if field=="get_program_stageiv":
        return "sizeof(GLint)"
    if field=="get_uniform_subroutineuiv":
        return "sizeof(GLuint)"
    if field=="uniform_subroutinesuiv":
        return "DGArrayBytes(count,sizeof(GLuint))"
    if field=="get_active_uniformsiv":
        return f"DGArrayBytes(uniformCount,sizeof({typ}))"
    if field in ubo_names:
        return "DGArrayBytes(bufSize,1)" if param==ubo_names[field] else "sizeof(GLsizei)"
    if field=="get_uniform_block_index":
        return f"DGStringBytes({param})"
    if field=="get_active_uniform_blockiv":
        return "DGQueryArrayBytes(block_count,sizeof(GLint))"
    if field=="get_integer64i_v":
        return "DGIndexedInteger64Count(target)*sizeof(GLint64)"
    if field in {"gen_samplers","delete_samplers"}:
        return "DGArrayBytes(count,sizeof(GLuint))"
    if field in sampler_arrays:
        return f"DGSamplerParameterCount(pname)*sizeof({typ})"
    if field in {"gen_program_pipelines","delete_program_pipelines","gen_transform_feedbacks","delete_transform_feedbacks"}:
        return "DGArrayBytes(n,sizeof(GLuint))"
    if field=="get_query_indexediv":
        return "sizeof(GLint)"
    if field == "get_program_pipelineiv":
        return "sizeof(GLint)"
    if field == "get_program_pipeline_info_log":
        return "sizeof(GLsizei)" if param=="length" else "DGArrayBytes(bufSize,1)"
    if field in fp64_names or (field in pipeline_names and field.startswith("program_uniform")):
        vector=re.fullmatch(r"(?:program_)?uniform([1234])(?:ui|[ifd])v",field)
        matrix=re.fullmatch(r"(?:program_)?uniform_matrix([234])(?:x([234]))?[fd]v",field)
        if vector or matrix:
            components=int(vector[1]) if vector else int(matrix[1])*int(matrix[2] or matrix[1])
            return f"DGQueryArrayBytes(location==-1 ? 0 : count,{components}*sizeof({typ}))"
    if field == "get_multisamplefv":
        return "2*sizeof(GLfloat)"
    if field in viewport_array_inputs:
        return f"DGArrayBytes(count,{viewport_array_inputs[field]}*sizeof({typ}))"
    if field in {"viewport_indexedfv","scissor_indexedv"}:
        return f"4*sizeof({typ})"
    if field in viewport_indexed_queries:
        return f"DGIndexedInteger64Count(target)*sizeof({typ})"
    if field=="patch_parameterfv":
        return "DGPatchLevelCount(pname)*sizeof(GLfloat)"
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
    if field in {"get_queryiv", "get_query_objectiv", "get_query_objectuiv"} | set(timer_query_results):
        return f"sizeof({typ})"
    if field in clear_buffer_inputs:
        # Color has four components; the depth/stencil variants take one.
        # Selectors are validated before either endpoint touches the pointer.
        return f"(buffer==GL_COLOR ? 4 : 1)*sizeof({typ})"
    if field in {"get_mapdv", "get_mapfv", "get_mapiv", "map1d", "map1f", "map2d", "map2f"}:
        return "map_bytes"
    if field in apple_evaluators and param=="points":
        return "map_bytes"
    if field == "prioritize_textures":
        return f"DGArrayBytes(n,sizeof({typ}))"
    if field in compat_pixel_fields:
        return "compat_pixel_bytes"
    if field == "are_textures_resident":
        return "DGArrayBytes(n,sizeof(GLboolean))" if param == "residences" else "DGArrayBytes(n,sizeof(GLuint))"
    if field in state_getters:
        return f"DGStateGetBytes(pname,binary_count,sizeof({typ}))"
    if field in {"get_vertex_attribiv_ARB", "get_vertex_attribfv_ARB", "get_vertex_attribdv_ARB",
                 "get_vertex_attribIiv_EXT", "get_vertex_attribIuiv_EXT", "get_vertex_attrib_ldv"}:
        return f"DGVertexAttribCount(pname) ? DGVertexAttribCount(pname)*sizeof({typ}) : SIZE_MAX"
    if (match := re.fullmatch(r"vertex_attribl([1-4])dv",field)):
        return f"{match[1]}*sizeof(GLdouble)"
    if field in {"get_tex_level_parameteriv", "get_tex_level_parameterfv"}:
        # Every texture-level pname is scalar, including later compressed
        # size, sample count and component-type queries. Native GL owns enums.
        return f"sizeof({typ})"
    if field in {"get_shaderiv", "get_framebuffer_attachment_parameteriv_EXT", "get_renderbuffer_parameteriv_EXT"}:
        # These object queries are scalar. Native GL owns pname validity;
        # a partial shared enum whitelist must not suppress valid results.
        return f"sizeof({typ})"
    if field=="get_programiv":
        # ARB_compute_shader defines the one three-integer result. Reserving
        # its correct shape does not advertise compute on a GL4.1 renderer.
        return f"(pname==0x8267 ? 3 : 1)*sizeof({typ})"
    if field in {"get_shader_info_log", "get_program_info_log"}:
        return "sizeof(GLsizei)" if param == "length" else "DGArrayBytes(bufSize,1)"
    if field in weight_vectors:
        return f"DGQueryArrayBytes(size,sizeof({typ}))"
    if field == "get_object_label_EXT":
        return "sizeof(GLsizei)" if param == "length" else "DGQueryArrayBytes(bufSize,1)"
    if field in shader_include_names:
        if param == "name":
            return "DGQueryArrayBytes(namelen,1)"
        if param == "string":
            return "DGQueryArrayBytes(bufSize,1)" if field == "get_named_string_ARB" else "DGQueryArrayBytes(stringlen,1)"
        return f"sizeof({typ})"
    if field=="get_transform_feedback_varying_EXT":
        return "DGArrayBytes(bufSize,1)" if param=="name" else f"sizeof({typ})"
    if field in {"get_integer_indexedv_EXT", "get_boolean_indexedv_EXT"}:
        return f"DGIndexedStateCount(param)*sizeof({typ})"
    if field == "get_attached_shaders":
        return "sizeof(GLsizei)" if param == "count" else "DGArrayBytes(maxCount,sizeof(GLuint))"
    if field in {"get_uniform_location", "get_attrib_location", "bind_attrib_location",
                 "bind_frag_data_location_EXT", "get_frag_data_location_EXT",
                 "bind_frag_data_location_indexed", "get_frag_data_index",
                 "get_uniform_location_ARB", "get_attrib_location_ARB", "bind_attrib_location_ARB"} and typ in {"GLchar", "GLcharARB"}:
        return f"DGStringBytes({param})"
    if field in {"tex_image2D", "tex_sub_image2D", "read_pixels"}:
        return f"DGPixelBytes(context,width,height,format,type,{int(field == 'read_pixels')})"
    if field == "get_tex_image" and param == "pixels":
        return "DGPixelBytes(context,width,height,format,type,1)"
    if field == "get_compressed_tex_image":
        return "0"  # Dedicated texture read command.
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
    if field in {"get_object_parameteriv_ARB", "get_object_parameterfv_ARB", "get_object_parameteriv_APPLE"}:
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
    # Desktop texture parameters have two vector pnames; every other native
    # pname is scalar. In particular the integer getter must not share the
    # shader/framebuffer whitelist: that underallocates valid swizzle,
    # anisotropy, comparison and other scalar results to one dummy byte.
    if field in {"tex_parameterfv", "tex_parameteriv", "get_tex_parameterfv", "get_tex_parameteriv"} | integer_texture_fields:
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
    m = re.fullmatch(r"uniform([1234])uiv_EXT", field)
    if m:
        return f"(count<=0 || location==-1) ? sizeof({typ}) : DGArrayBytes(count,{m[1]}*sizeof({typ}))"
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
    if field=="shader_binary":
        params = ["const GLuint *shaders" if s=="GLuint *shaders" else s for s in params]
    parsed = []
    for param in params:
        match = re.match(r"(.+?)(\w+)$", param)
        if not match or "(" in param:
            raise SystemExit("unrecognized SDK prototype: " + param)
        typ, arg = match.groups()
        parsed.append((typ.strip(), arg))
    signature = ", ".join(params) or "void"
    texture_shape_valid=None
    if field in compact_texture_inputs:
        texture_shape_valid=" && ".join([f"{arg}>=0" for _,arg in parsed
                                         if arg in {"width","height","depth"}]+
                                        ["DGRasterPixelBits(format,type)"])
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
            # Apple's EXT_texture_integer setters declare mutable pointers
            # in both headers, but they only consume the values.
            shapes[arg] = (size, element, "const" not in typ and
                           not (field in integer_texture_fields and field.startswith("tex_")))
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
    if field in {"delete_queries", "delete_samplers", "delete_program_pipelines", "delete_transform_feedbacks", "uniform_subroutinesuiv", "prioritize_textures", "draw_buffers_ARB", "pixel_mapfv", "pixel_mapuiv", "pixel_mapusv",
                 "program_env_parameters4fv_EXT", "program_local_parameters4fv_EXT"} | viewport_array_inputs.keys():
        for arg,(size,element,output) in list(shapes.items()):
            if size and size.startswith("DGArrayBytes(") and not output:
                shapes[arg]=(size.replace("DGArrayBytes(","DGQueryArrayBytes(",1),element,output)
                query_inputs.add(arg)
    if field in fp64_names or field.startswith("program_uniform") or field in {"shader_binary","program_binary"}:
        query_inputs.update(shapes)
    if field in weight_vectors:
        query_inputs.update(shapes)
    if name in {"glShaderSource", "glShaderSourceARB"} or field in {"transform_feedback_varyings_EXT","get_uniform_indices","create_shader_programv","compile_shader_include_ARB"}:
        supported = True  # dedicated nested-string codec, not a pointer cast
    if field=="get_attached_objects_ARB":
        supported = True  # guest adapter for the fixed-width core shader query
    if field in imaging_transfers:
        supported = bool(name)  # dedicated CPU pixel span and native PBO codec
    if field=="array_element":
        supported = True  # CPU value snapshot and separately prepared native VBO arrays
    if field in {"feedback_buffer","select_buffer","render_mode"}:
        supported = True  # GL retains private host allocations, not packet pointers
    if field == "draw_arrays" or field.startswith(("draw_elements", "draw_range_elements", "multi_draw_")):
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
    if name in {"glGetString", "glGetStringi", "glGetError", "glGetProgramStringARB", "glGetUniformfvARB", "glGetUniformivARB", "glGetUniformfv", "glGetUniformiv", "glGetUniformuivEXT", "glGetUniformdv", "glFenceSync", "glIsSync", "glDeleteSync", "glClientWaitSync", "glWaitSync", "glGetSynciv",
                "glGetBufferParameteriv", "glGetBufferParameteri64v", "glGetBufferSubData", "glCopyBufferSubData",
                "glMapBuffer", "glMapBufferRange", "glGetBufferPointerv", "glUnmapBuffer", "glFlushMappedBufferRange", "glFlushMappedBufferRangeAPPLE"}:
        report[-1]["supported"] = True
        continue  # bounded string reply and local transport errors are custom
    client.append(f"{ret} {name}({signature}) {{")
    client.append("    struct DGContext *context=dg_current;")
    # Measured period command semantics, not an OS-wide change to normalized
    # arrays: native immediate setters differ from the same driver's actual
    # DrawArrays/DrawElements/ArrayElement rendering. Keep the original wire
    # slots; express only these current attributes through existing float APIs.
    signed_attribute=re.fullmatch(r"(color[34]|secondary_color3|normal3)([bsi])(v?)",field)
    signed_generic=re.fullmatch(r"vertex_attrib4N([bsi])v_ARB",field)
    if signed_attribute or signed_generic:
        components=4 if signed_generic else int(signed_attribute[1][-1])
        kind=signed_generic[1] if signed_generic else signed_attribute[2]
        bits={'b':8,'s':16,'i':32}[kind]
        vector=bool(signed_generic or signed_attribute[3])
        source=parsed[-1][1] if vector else None
        normal=int(bool(signed_attribute and signed_attribute[1]=='normal3'))
        values=[f'{source}[{i}]' if vector else parsed[i][1] for i in range(components)]
        converted=','.join(f'DGPeriodSignedAttribute({value},{bits},{normal})' for value in values)
        function='glVertexAttrib4fvARB' if signed_generic else 'gl'+{
            'color3':'Color3fv','color4':'Color4fv','secondary_color3':'SecondaryColor3fv','normal3':'Normal3fv'
        }[signed_attribute[1]]
        arguments=(parsed[0][1]+',') if signed_generic else ''
        client += [f'#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040 || (__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1050 && {bits} != 32 && !{normal})',
                   f'    if(context) {{ GLfloat converted[{components}]={{{converted}}};{function}({arguments}converted);return; }}',
                   '#endif']
    if field=="primitive_restart_index":
        client.append("    if(context && !(context->connection->features&DG_FEATURE_BASE_VERTEX)) { context->error=GL_INVALID_OPERATION;return; }")
    if field=="get_object_label_EXT":
        client.append("    if(context && !(context->connection->features2&DG_FEATURE2_DEBUG_LABEL)) { if(!context->error)context->error=GL_INVALID_OPERATION;return; }")
    if field in shader_include_names:
        fallback="return GL_FALSE;" if ret=="GLboolean" else "return;"
        client.append(f"    if(context && !(context->connection->features2&DG_FEATURE2_SHADER_INCLUDE)) {{ if(!context->error)context->error=GL_INVALID_OPERATION;{fallback} }}")
    if field in apple_evaluators:
        fallback="return GL_FALSE;" if ret=="GLboolean" else "return;"
        client.append(f"    if(context && (context->profile!=0x1000 || !(context->connection->features2&DG_FEATURE2_APPLE_EVALUATORS))) {{ if(!context->error)context->error=GL_INVALID_OPERATION;{fallback} }}")
    if field in weight_vectors or field=="vertex_blend_ARB":
        client.append("    if(context && (context->profile!=0x1000 || !(context->connection->features2&DG_FEATURE2_VERTEX_BLEND))) { if(!context->error)context->error=GL_INVALID_OPERATION;return; }")
    if field in weight_vectors:
        vector=parsed[-1][1]
        client.append(f"    if(context && (size<0 || size>(GLint)DG_VERTEX_WEIGHT_MAX || (size && !{vector}))) {{ if(!context->error)context->error=GL_INVALID_VALUE;return; }}")
    unsigned_uniform=re.fullmatch(r"uniform[1234]uiv_EXT",field)
    if unsigned_uniform or field in integer_texture_fields:
        client.append("    if(context && !(context->connection->features&DG_FEATURE_INTEGER_PARAMETERS)) { if(!context->error)context->error=GL_INVALID_OPERATION;return; }")
    compressed_upload=field.startswith(("compressed_tex_image", "compressed_tex_sub_image"))
    if compressed_upload:
        client.append("    if(context && !(context->connection->features&DG_FEATURE_COMPRESSED_TEXTURE_UPLOAD)) { context->error=GL_INVALID_OPERATION;return; }")
        client.append("    if(context && imageSize<0) { context->error=GL_INVALID_VALUE;return; }")
        if field.startswith("compressed_tex_image"):
            client.append("    if(DGCompressedProxy(target) && (!context || !context->pixel_buffer[0]))data=NULL; // CPU proxy definitions never inspect application storage.")
    if output_counts:
        nonpositive=" || ".join(f"{count}<=0" for count in sorted(output_counts))
        ending="return;" if ret=="void" else f"return ({ret})0;"
        client.append(f"    if(context && ({nonpositive}) && !(context->connection->capabilities&DG_CAP_QUERY_REPLIES)) {{ context->error=GL_INVALID_OPERATION;{ending} }}")
    if field == "call_lists":
        client.append("    if(context && !(context->connection->capabilities&DG_CAP_CALL_LISTS)) { context->error=GL_INVALID_OPERATION;return; }")
        client.append("    if(context && DGCallListsBytes(n,type) && !lists) { context->error=GL_INVALID_VALUE;return; }")
    if field in {"get_integerv", "get_floatv", "get_doublev", "get_booleanv", "get_integer64v_sync"}:
        client.append("    if(context && (pname==GL_LIST_BASE || pname==GL_LIST_INDEX || pname==GL_LIST_MODE) && !(context->connection->capabilities&DG_CAP_CALL_LISTS)) { context->error=GL_INVALID_OPERATION;return; }")
        client.append("    if(context && pname==0x8e28 && !(context->connection->features&DG_FEATURE_TIMER_QUERY)) { context->error=GL_INVALID_ENUM;return; }")
    if field in {"tex_image1D", "tex_sub_image1D", "tex_image2D", "tex_sub_image2D", "tex_image3D", "tex_sub_image3D", "read_pixels", "get_tex_image"}:
        client.append("    if(context && (format==GL_YCBCR_422_APPLE || type==GL_UNSIGNED_SHORT_8_8_APPLE || type==GL_UNSIGNED_SHORT_8_8_REV_APPLE) && !(context->connection->capabilities&DG_CAP_APPLE_VIDEO_TEXTURES)) { context->error=GL_INVALID_OPERATION;return; }")
    location_query = field in {"get_uniform_location", "get_uniform_location_ARB"}
    if location_query:
        location_program = f"(GLuint)(uintptr_t){parsed[0][1]}"
        client.append(f"    uint64_t location_generation=0;GLint cached_location=0;if(DGProgramLocationGet(context,{location_program},name,&cached_location,&location_generation))return cached_location;")
    if field in {"tex_image3D", "tex_sub_image3D"}:
        client.append("    if(context && !(context->connection->capabilities&DG_CAP_TEXTURE_3D)) { context->error=GL_INVALID_OPERATION;return; }")
        if field == "tex_image3D":
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
    if field in {"buffer_data","buffer_sub_data"}:
        client.append("    if(context && size<0) { if(!context->error)context->error=GL_INVALID_VALUE;return; }")
        if field=="buffer_sub_data":
            client.append("    if(context && offset<0) { if(!context->error)context->error=GL_INVALID_VALUE;return; }")
        mode='DG_BUFFER_STAGE_DATA' if field=='buffer_data' else 'DG_BUFFER_STAGE_SUBDATA'
        offset='0' if field=='buffer_data' else 'offset'
        usage='usage' if field=='buffer_data' else '0'
        client.append(f"    if(context && data && (uint64_t)size>DG_MAX_BYTES) {{ DGLargeBufferTransfer(context,{mode},target,{offset},size,(void *)data,{usage});return; }}")
    if field in {"tex_image1D","tex_image2D","tex_image3D"}:
        client.append("    if(DGCompressedProxy(target) && (!context || !context->pixel_buffer[0]))pixels=NULL; // CPU proxy allocation ignores application storage.")
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
        client.append("    if(DGLargeReadPixels(context,x,y,width,height,format,type,pixels))return;")
    if field in apple_evaluator_getters:
        result_type=parsed[2][0].replace("*", "").strip()
        client.append(f"    if(DGAppleEvaluatorQuery(pname)) {{ DGQueryAppleEvaluator(context,{index},index,pname,params,sizeof({result_type}));return; }}")
        client.append("    if(context && !(context->connection->capabilities&DG_CAP_INSTANCED_ARRAYS)) { context->error=GL_INVALID_OPERATION;return; }")
        client.append(f"    if(context) {{ GLint value=0;int found=DGArrayState(context,index,pname,&value);if(found) {{ if(found>0 && params)*params=({result_type})value;return; }} }}")
    if field in {"get_vertex_attribIiv_EXT", "get_vertex_attribIuiv_EXT"}:
        result_type=parsed[2][0].replace("*", "").strip()
        client += [
            "    if(!context)return;",
            "    if(!(context->connection->features&DG_FEATURE_INTEGER_VERTEX_ARRAYS) || context->common_state_blocked) { context->error=GL_INVALID_OPERATION;return; }",
            "    if(!DGVertexAttribCount(pname)) { context->error=GL_INVALID_ENUM;return; }",
            "    if(!params) { context->error=GL_INVALID_VALUE;return; }",
            f"    {{ GLint value=0;int found=DGArrayState(context,index,pname,&value);if(found) {{ if(found>0)*params=({result_type})value;return; }} }}",
        ]
    if field in clear_buffer_fields:
        client.append("    if(context && !(context->connection->features&DG_FEATURE_CLEAR_BUFFER)) { context->error=GL_INVALID_OPERATION;return; }")
        client.append(f"    if(context && !({clear_buffer_selectors[field]})) {{ context->error=GL_INVALID_ENUM;return; }}")
    if field in timer_query_fields:
        client.append("    if(context && !(context->connection->features&DG_FEATURE_TIMER_QUERY)) { context->error=GL_INVALID_OPERATION;return; }")
    if field=="get_integer64i_v":
        client += [
            "    if(context && !(context->connection->features&DG_FEATURE_INDEXED_INT64)) { context->error=GL_INVALID_OPERATION;return; }",
            "    if(context && !DGIndexedInteger64Count(target)) { context->error=GL_INVALID_ENUM;return; }",
            "    if(context && !data) { context->error=GL_INVALID_VALUE;return; }",
        ]
    if field in ubo_fields:
        fallback="return ~0u;" if field=="get_uniform_block_index" else "return;"
        client.append(f"    if(context && !(context->connection->features&DG_FEATURE_UNIFORM_BLOCKS)) {{ context->error=GL_INVALID_OPERATION;{fallback} }}")
    if field in sampler_fields:
        fallback="return GL_FALSE;" if field=="is_sampler" else "return;"
        client.append(f"    if(context && !(context->connection->features&DG_FEATURE_SAMPLERS)) {{ context->error=GL_INVALID_OPERATION;{fallback} }}")
    if field in feedback_names:
        fallback="return GL_FALSE;" if ret=="GLboolean" else "return;"
        client.append(f"    if(context && !(context->connection->features2&DG_FEATURE2_FEEDBACK_OBJECTS)) {{ if(!context->error)context->error=GL_INVALID_OPERATION;{fallback} }}")
    if field in subroutine_names:
        fallback={"GLint":"return -1;","GLuint":"return ~0u;"}.get(ret,"return;")
        client.append(f"    if(context && !(context->connection->features2&DG_FEATURE2_SUBROUTINES)) {{ if(!context->error)context->error=GL_INVALID_OPERATION;{fallback} }}")
    if field in es2_names:
        client.append("    if(context && !(context->connection->features2&DG_FEATURE2_ES2_COMPAT)) { if(!context->error)context->error=GL_INVALID_OPERATION;return; }")
    if field in fp64_vertex_names:
        client.append("    if(context && (context->profile==0x1000 || !(context->connection->features2&DG_FEATURE2_FP64_VERTEX))) { if(!context->error)context->error=GL_INVALID_OPERATION;return; }")
    if field=="get_vertex_attrib_ldv":
        client += [
            "    if(!context)return;",
            "    if(!DGVertexAttribCount(pname)) { if(!context->error)context->error=GL_INVALID_ENUM;return; }",
            "    if(!params) { if(!context->error)context->error=GL_INVALID_VALUE;return; }",
            "    { GLint value=0;int found=DGArrayState(context,index,pname,&value);if(found) { if(found>0)*params=(GLdouble)value;return; } }",
        ]
    for fields,feature in ((packed_attrib_names,"PACKED_ATTRIBS"),(program_binary_names,"PROGRAM_BINARY"),
                           ({"min_sample_shading"},"SAMPLE_SHADING"),({"get_internal_formativ"},"INTERNAL_FORMAT")):
        if field in fields:
            client.append(f"    if(context && !(context->connection->features2&DG_FEATURE2_{feature})) {{ if(!context->error)context->error=GL_INVALID_OPERATION;return; }}")
    if field in packed_attrib_names and field.endswith("v"):
        client.append("    if(context && !value) { if(!context->error)context->error=GL_INVALID_VALUE;return; }")
    if field=="get_program_binary":
        client.append("    if(context && (bufSize<0 || !binaryFormat || (bufSize && !binary))) { if(!context->error)context->error=GL_INVALID_VALUE;return; }")
    if field=="get_internal_formativ":
        client.append("    if(context && bufSize>0 && !params) { if(!context->error)context->error=GL_INVALID_VALUE;return; }")
    if field=="program_binary":
        client.append("    if(context && length>0 && !binary) { if(!context->error)context->error=GL_INVALID_VALUE;return; }")
    if field=="shader_binary":
        client.append("    if(context && n>=0 && length>=0 && ((n && !shaders) || (length && !binary))) { if(!context->error)context->error=GL_INVALID_VALUE;return; }")
    if field=="get_shader_precision_format":
        client.append("    if(context && (!range || !precision)) { if(!context->error)context->error=GL_INVALID_VALUE;return; }")
    if field in subroutine_name_queries:
        client.append(f"    if(context && !name) {{ if(!context->error)context->error=GL_INVALID_VALUE;{fallback} }}")
    if field in subroutine_string_results:
        client.append("    if(context && bufsize>0 && !name) { if(!context->error)context->error=GL_INVALID_VALUE;return; }")
    if field=="uniform_subroutinesuiv":
        client.append("    if(context && count>0 && !indices) { if(!context->error)context->error=GL_INVALID_VALUE;return; }")
    if field in {"get_active_subroutine_uniformiv","get_program_stageiv","get_uniform_subroutineuiv"}:
        out="params" if field=="get_uniform_subroutineuiv" else "values"
        client.append(f"    if(context && !{out}) {{ if(!context->error)context->error=GL_INVALID_VALUE;return; }}")
    if field=="get_active_subroutine_uniformiv":
        client += [
            "    GLint compatible_count=1;",
            "    if(context && pname==0x8e4b) { /* GL_COMPATIBLE_SUBROUTINES */",
            "        compatible_count=-1;glGetActiveSubroutineUniformiv(program,shadertype,index,0x8e4a,&compatible_count);",
            "        if(compatible_count<0)return;",
            "    }",
        ]
    if field in {"gen_transform_feedbacks","delete_transform_feedbacks"}:
        client.append("    if(context && n>0 && !ids) { if(!context->error)context->error=GL_INVALID_VALUE;return; }")
    if field=="tex_buffer":
        client.append("    if(context && !(context->connection->features&DG_FEATURE_TEXTURE_BUFFER)) { context->error=GL_INVALID_OPERATION;return; }")
    if field in pipeline_names:
        ending="return 0;" if ret!="void" else "return;"
        client.append(f"    if(context && !(context->connection->features&DG_FEATURE_PROGRAM_PIPELINE)) {{ context->error=GL_INVALID_OPERATION;{ending} }}")
    if field in fp64_names:
        client.append("    if(context && !(context->connection->features&DG_FEATURE_FP64_UNIFORM)) { context->error=GL_INVALID_OPERATION;return; }")
    if field=="program_parameteri_EXT":
        client.append("    if(context && pname==0x8258 && !(context->connection->features&DG_FEATURE_PROGRAM_PIPELINE)) { context->error=GL_INVALID_ENUM;return; }")
        client.append("    if(context && pname==0x8257 && !(context->connection->features2&DG_FEATURE2_PROGRAM_BINARY)) { if(!context->error)context->error=GL_INVALID_ENUM;return; }")
    if field in {"gen_program_pipelines","delete_program_pipelines"}:
        client.append("    if(context && (n<0 || (n && !pipelines))) { context->error=GL_INVALID_VALUE;return; }")
    if field=="get_program_pipelineiv":
        client.append("    if(context && !params) { context->error=GL_INVALID_VALUE;return; }")
    if field=="get_program_pipeline_info_log":
        client.append("    if(context && (bufSize<0 || (bufSize && !infoLog))) { context->error=GL_INVALID_VALUE;return; }")
    if (field.startswith("program_uniform") or field in fp64_names) and shapes:
        client.append("    if(context && location!=-1 && count>0 && !value) { context->error=GL_INVALID_VALUE;return; }")
    for feature, fields in render_target_features.items():
        if field in fields:
            ending="return -1;" if field=="get_frag_data_index" else "return;"
            client.append(f"    if(context && !(context->connection->features&DG_FEATURE_{feature})) {{ context->error=GL_INVALID_OPERATION;{ending} }}")
    if field in {"patch_parameteri","patch_parameterfv"}:
        client.append("    if(context && !(context->connection->features2&DG_FEATURE2_TESSELLATION)) { if(!context->error)context->error=GL_INVALID_OPERATION;return; }")
    if field=="patch_parameterfv":
        client += [
            "    if(context && !DGPatchLevelCount(pname)) { if(!context->error)context->error=GL_INVALID_ENUM;return; }",
            "    if(context && !values) { if(!context->error)context->error=GL_INVALID_VALUE;return; }",
        ]
    if field == "get_multisamplefv":
        client += [
            "    if(context && pname!=0x8e50) { context->error=GL_INVALID_ENUM;return; }",
            "    if(context && !val) { context->error=GL_INVALID_VALUE;return; }",
        ]
    if field in viewport_indexed_queries:
        client += [
            "    if(context && !DGIndexedInteger64Count(target)) { context->error=GL_INVALID_ENUM;return; }",
            "    if(context && !data) { context->error=GL_INVALID_VALUE;return; }",
        ]
    if field in viewport_array_inputs:
        client.append("    if(context && count>0 && !v) { context->error=GL_INVALID_VALUE;return; }")
    if field in {"viewport_indexedfv","scissor_indexedv"}:
        client.append("    if(context && !v) { context->error=GL_INVALID_VALUE;return; }")
    if field in {"gen_samplers","delete_samplers"}:
        client.append("    if(context && (count<0 || (count && !samplers))) { context->error=GL_INVALID_VALUE;return; }")
    if field in sampler_arrays:
        arg="params" if field.startswith("get_") else "param"
        client += [
            "    if(context && !DGSamplerParameterCount(pname)) { context->error=GL_INVALID_ENUM;return; }",
            f"    if(context && !{arg}) {{ context->error=GL_INVALID_VALUE;return; }}",
        ]
    if field in sampler_arrays | {"sampler_parameteri","sampler_parameterf"}:
        # The preceding decoder rejected this otherwise scalar pname.
        # Unsupported peers must get a local GL error, not a new packet
        # which their bounds/enum validation would reject as a dead stream.
        client.append("    if(context && pname==0x8a48 && !(context->connection->features2&DG_FEATURE2_SRGB_DECODE)) { if(!context->error)context->error=GL_INVALID_ENUM;return; }")
    if field in imaging_parameters:
        client += [
            "    if(!context)return;",
            "    if(context->profile!=0x1000 || context->common_state_blocked || !(context->connection->features2&DG_FEATURE2_IMAGING_PARAMETERS)) { if(!context->error)context->error=GL_INVALID_OPERATION;return; }",
            f"    if(!DGImagingParameterCount({imaging_parameters[field]},pname)) {{ if(!context->error)context->error=GL_INVALID_ENUM;return; }}",
            "    if(!params) { if(!context->error)context->error=GL_INVALID_VALUE;return; }",
        ]
    if field in combiner_arrays or field=="sample_pass":
        feature="LEGACY_COMBINERS" if field in combiner_arrays else "LEGACY_SAMPLE_PASS"
        client.append(f"    if(context && !(context->connection->features2&DG_FEATURE2_{feature})) {{ if(!context->error)context->error=GL_INVALID_OPERATION;return; }}")
        if field in combiner_arrays:
            client.append("    if(!params)return;")
    if field in {"get_integerv", "get_floatv"}:
        client.append(f"    if(DGCommonStateGet(context,pname,params,{int(field=='get_floatv')}))return;")
    if field in state_getters:
        client.append("    if(context && DGAdditionalStateCount(pname) && !(context->connection->features2&DG_FEATURE2_ADDITIONAL_STATE)) { if(!context->error)context->error=GL_INVALID_ENUM;return; }")
        result_type=parsed[1][0].replace("*", "").strip()
        public_value="!!public_version" if field=="get_booleanv" else "public_version"
        client.append(f"    GLint public_version=0;if(DGPublicCoreVersion(context,pname,&public_version)) {{ if(params)*params=({result_type})({public_value});else if(context&&!context->error)context->error=GL_INVALID_VALUE;return; }}")
        core_count="!!DGCoreExtensionMask(context)" if field=="get_booleanv" else "__builtin_popcountll(DGCoreExtensionMask(context))"
        client.append(f"    if(context && context->profile!=0x1000 && pname==0x821d) {{ if(params)*params=({result_type})({core_count});else if(!context->error)context->error=GL_INVALID_VALUE;return; }}")
        client += [
            "    GLint binary_count=0;GLenum count_key=DGStateArrayCountKey(pname);",
            "    if(context && count_key) {",
            "        unsigned feature=pname==0x8df8 ? DG_FEATURE2_ES2_COMPAT : pname==GL_CURRENT_WEIGHT_ARB ? DG_FEATURE2_VERTEX_BLEND : pname==0x86a3 ? DG_FEATURE2_ADDITIONAL_STATE : DG_FEATURE2_PROGRAM_BINARY;",
            "        if(!(context->connection->features2&feature)) { if(!context->error)context->error=GL_INVALID_ENUM;return; }",
            "        if(pname==GL_CURRENT_WEIGHT_ARB && context->common_state_blocked) { if(!context->error)context->error=GL_INVALID_OPERATION;return; }",
            "        binary_count=-1;if(pname==GL_CURRENT_WEIGHT_ARB)binary_count=DGClientWeightUnits(context);else glGetIntegerv(count_key,&binary_count);",
            "        if(binary_count<=0)return;",
            "        if(!params) { if(!context->error)context->error=GL_INVALID_VALUE;return; }",
            "    }",
        ]
        expression="!!(context->vertex && context->vertex->name)" if field=="get_booleanv" else "(context->vertex?context->vertex->name:0)"
        client.append(f"    if(context && pname==0x85b5) {{ if(params)*params=({result_type}){expression};else context->error=GL_INVALID_VALUE;return; }}")
        value="!!logical" if field=="get_booleanv" else "logical"
        client.append(f"    {{ GLint logical=0;if(DGClientArrayInteger(context,pname,&logical)) {{ if(params)*params=({result_type})({value});else context->error=GL_INVALID_VALUE;return; }} }}")
        # Resolve native/client and counted selectors first. An unrecognized
        # fixed-state selector has no safe output shape: report the Get enum
        # error without submitting a SIZE_MAX reply or touching caller memory.
        # Begin/End still takes precedence, and earlier errors are retained.
        client.append("    if(context && !count_key && !DGGetCount(pname)) { if(!context->error)context->error=context->common_state_blocked?GL_INVALID_OPERATION:GL_INVALID_ENUM;return; }")
    if field == "is_enabled":
        client.append("    if(context && context->profile==0x1000 && !context->common_state_blocked && (DGArraySlot(context,cap)>=0 || cap==GL_ELEMENT_ARRAY_APPLE || cap==GL_VERTEX_ARRAY_RANGE_APPLE)) { GLint logical=0;if(DGClientArrayInteger(context,cap,&logical))return !!logical; }")
    if field in {"begin", "end"}:
        if field=="begin":
            client.append("    if(context && !context->common_state_blocked && mode<=GL_POLYGON)DGPrepareArrayElement(context);")
        # Invalid/nested Begin may disable the optimization unnecessarily,
        # never incorrectly enable a cached query inside a real Begin/End.
        client.append(f"    if(context)context->common_state_blocked={int(field=='begin')};")
    if field=="array_element":
        client += ["    if(context)DGArrayElement(context,"+str(index)+","+parsed[0][1]+");","}"]
        host += [f"case {index}: return DGHostArrayElement(context,dg_wire_message);"]
        continue
    if field in {"feedback_buffer","select_buffer"}:
        which=int(field=="feedback_buffer")
        client += [f"    if(context)DGRegisterRenderBuffer(context,{index},{which},size,{'type' if which else '0'},buffer);","}"]
        host += [f"case {index}: return DGHostRegisterRenderBuffer(context,dg_wire_message,reply,{which});"]
        continue
    if field=="render_mode":
        client += [f"    return context?DGRenderMode(context,{index},mode):0;","}"]
        host += [f"case {index}: return DGHostRenderMode(context,dg_wire_message,reply);"]
        continue
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
    if field in imaging_transfers:
        operation,width,height,start=imaging_transfers[field]
        pack=int(field.startswith("get_"))
        pointers=[(i,arg) for i,(typ,arg) in enumerate(parsed) if "*" in typ and arg!="span"]
        pointer_indices=",".join(str(i) for i,_ in pointers)
        pointer_names=",".join(arg for _,arg in pointers)
        if len(pointers)==1:
            pointer_indices+=",-1"
            pointer_names+=",NULL"
        common=f"DG_IMAGING_{operation},target,{width},{height},{start},format,type,{pack},(const int[2]){{{pointer_indices}}}"
        client += ["    if(!context)return;", f"    struct DGPacket packet=DGPacketNew(context,{index});"]
        host += [f"case {index}: {{", "    void *pixels[2]={0};struct DGImagingPixelScope scope={0};"]
        native_args=[]
        for i,(typ,arg) in enumerate(parsed):
            if "*" not in typ:
                client.append(f"    packet.message.args[{i}]=DGBits(&{arg},sizeof({arg}));")
                host.append(f"    {typ} {arg}=0;memcpy(&{arg},&dg_wire_message->args[{i}],sizeof({arg}));")
                native_args.append(arg)
            elif arg=="span":
                client.append("    (void)span; /* GL never uses this pointer. */")
                native_args.append("NULL")
            else:
                native_args.append(f"pixels[{len([p for p,_ in pointers if p<i])}]")
        client += [f"    DGImagingClientTransfer(context,&packet,{common},(const void *const[2]){{{pointer_names}}});",
                   "    DGPacketFree(&packet);", "}"]
        host += [f"    typedef void (*Proc)({signature});",
                 f'    static Proc proc;if(!proc)proc=(Proc)dlsym(RTLD_DEFAULT,"{name}");',
                 "    if(!proc){if(!context->error)context->error=GL_INVALID_OPERATION;return 0;}",
                 f"    int admitted=DGHostImagingTransfer(context,dg_wire_message,{common},pixels,&scope);",
                 "    if(admitted<=0)return admitted;",
                 f"    proc({','.join(native_args)});",
                 "    DGHostImagingEnd(context,&scope);"]
        if pack:
            host.append("    if(dg_wire_message->args[15]==1)DGPBOWritten(context);")
        host += ["    return 0;", "}"]
        continue
    if name in {"glShaderSource", "glShaderSourceARB"}:
        client.append(f"    (void)context;")
        obj = "(GLuint)(uintptr_t)shaderObj" if name.endswith("ARB") else "shader"
        client += [f"    DGShaderSource(context,{obj},count,string,length);", "}"]
        client.append(f"unsigned DGShaderSourceSlot(void) {{ return {index}; }}")
        host += [f"case {index}: return DGHostShaderSource(context,dg_wire_message,reply);"]
        continue
    if field=="get_attached_objects_ARB":
        client += ["    DGGetAttachedObjects(context,containerObj,maxCount,count,obj);","}"]
        continue
    if field=="transform_feedback_varyings_EXT":
        client += [f"    DGFeedbackVaryings(context,{index},program,count,varyings,bufferMode);","}"]
        host += [f"case {index}: return DGHostFeedbackVaryings(context,dg_wire_message);"]
        continue
    if field=="get_uniform_indices":
        client += [f"    DGUniformIndices(context,{index},program,uniformCount,uniformNames,uniformIndices);","}"]
        host += [f"case {index}: return DGHostUniformIndices(context,dg_wire_message);"]
        continue
    if field=="create_shader_programv":
        client += [f"    return DGCreateShaderProgram(context,{index},type,count,strings);","}"]
        host += [f"case {index}: return DGHostCreateShaderProgram(context,dg_wire_message,reply);"]
        continue
    if field=="compile_shader_include_ARB":
        client += [f"    DGCompileShaderInclude(context,{index},shader,count,path,length);","}"]
        host += [f"case {index}: return DGHostCompileShaderInclude(context,dg_wire_message);"]
        continue
    if field in ubo_names:
        client.append(f"    if(context && (bufSize<0 || (bufSize>0 && !{ubo_names[field]}))) {{ context->error=GL_INVALID_VALUE;return; }}")
    if field=="get_active_uniformsiv":
        client.append("    if(context && (uniformCount<0 || (uniformCount && (!uniformIndices || !params)))) { context->error=GL_INVALID_VALUE;return; }")
    if field=="get_active_uniform_blockiv":
        client += [
            "    if(context && !params) { context->error=GL_INVALID_VALUE;return; }",
            "    GLint block_count=1;",
            "    if(context && pname==0x8a43) {",
            "        block_count=-1;glGetActiveUniformBlockiv(program,uniformBlockIndex,0x8a42,&block_count);",
            "        if(block_count<0)return;",
            "    }",
        ]
    if field=="get_transform_feedback_varying_EXT":
        client += [
            "    if(context && !(context->connection->features&DG_FEATURE_TRANSFORM_FEEDBACK_NAMES)) { context->error=GL_INVALID_OPERATION;return; }",
            "    if(context && (bufSize<0 || !size || !type || (bufSize>0 && !name))) { context->error=GL_INVALID_VALUE;return; }",
        ]
    if field in {"get_integer_indexedv_EXT", "get_boolean_indexedv_EXT"}:
        client += [
            "    if(!context)return;",
            "    if(!(context->connection->features&DG_FEATURE_INDEXED_STATE)) { context->error=GL_INVALID_OPERATION;return; }",
            "    if(!DGIndexedStateCount(param)) { context->error=GL_INVALID_ENUM;return; }",
            "    if(!values) { context->error=GL_INVALID_VALUE;return; }",
        ]
    if field == "get_compressed_tex_image":
        client += [f"    DGReadTexture(context,{','.join(arg for _,arg in parsed[:2])},0,0,{parsed[2][1]},1,0);", "}"]
        continue
    if field == "get_polygon_stipple":
        client.append(f"    if(DGReadPolygonStipple(context,{parsed[-1][1]}))return;")
    # Ordinary texture uploads forward negative dimensions without data so
    # native GL retains deferred list error timing. The other upload codecs
    # still need an immediate rejection instead of an unknown pointer shape.
    if field not in compact_texture_inputs and field.startswith(("tex_image", "tex_sub_image", "copy_tex_image", "compressed_tex_image", "compressed_tex_sub_image")):
        dimensions = [arg for _, arg in parsed if arg in {"width", "height", "depth"}]
        if dimensions:
            bad = " || ".join(f"{arg}<0" for arg in dimensions)
            client += [f"    if(context && ({bad})) {{ if(!context->error) context->error=GL_INVALID_VALUE;return; }}"]
    if field in {"tex_image1D","tex_sub_image1D","tex_image2D","tex_sub_image2D","tex_image3D","tex_sub_image3D","draw_pixels","bitmap","polygon_stipple"} or compressed_upload:
        pointer=parsed[-1][1]
        dimensions=3 if "3D" in field else 2 if "2D" in field or field in compat_raster_shapes else 1
        if field in compat_raster_shapes:
            raster_width,raster_height,raster_format,raster_type=compat_raster_shapes[field].split(",")
            layout=f"DGLargeTextureLayout(context,2,{raster_width},{raster_height},1,{raster_format},{raster_type})"
        else:
            layout="(struct DGLargeTextureLayout){(uint32_t)imageSize,0,(uint32_t)imageSize,(uint32_t)imageSize,(uint32_t)imageSize,1,1}" if compressed_upload else f"DGLargeTextureLayout(context,{dimensions},width,{'height' if dimensions>=2 else '1'},{'depth' if dimensions==3 else '1'},format,type)"
        arguments=",".join("NULL" if "*" in typ else arg for typ,arg in parsed)
        admitted='context->profile==0x1000 && (context->connection->features2&DG_FEATURE2_COMPAT_RASTER)' if field in compat_raster_shapes else '!DGCompressedProxy(target)'
        if field=="draw_pixels":
            admitted+=" && !DGDrawPixelsNoInput(width,height,format,type)"
        client += [f"    if(context && {pointer} && !context->pixel_buffer[0] && ({admitted})) {{",
                   f"        struct DGLargeTextureLayout layout={layout};"]
        if field in compact_texture_inputs:
            client.append(f"        struct DGPacket compact=DGPacketNew(context,{index});")
            for i,(typ,arg) in enumerate(parsed):
                if "*" not in typ:
                    client.append(f"        compact.message.args[{i}]=DGBits(&{arg},sizeof({arg}));")
            client += [f"        int compacted=DGCompactTextureInput(context,&compact,{len(parsed)-1},{pointer},&layout);",
                       "        DGPacketFree(&compact);if(compacted)return;"]
        client += ["        struct DGLargeTextureInput input={0};",
                   f"        int staged=DGLargeTextureBegin(context,{pointer},&layout,&input);",
                   f"        if(staged){{if(staged>0){{{name}({arguments});DGLargeTextureEnd(context,&input);}}return;}}",
                   "    }"]
    client += ["    if(!context) {" + (" return;" if ret == "void" else f" return ({ret})0;") + " }",
               f"    struct DGPacket packet=DGPacketNew(context,{index});"]
    host += [f"case {index}: {{"]
    if field in compact_texture_inputs:
        host.append("    struct DGCompactUnpackScope compact_scope={0};")
        host.append(f"    if(dg_wire_message->args[15] && (dg_wire_message->args[15]!=2 || !dg_wire_message->args[{len(parsed)-1}]))return -1;")
    if field in weight_vectors:
        host.append("    if((GLint)(uint32_t)dg_wire_message->args[0]<0 || dg_wire_message->args[0]>DG_VERTEX_WEIGHT_MAX)return -1;")
    if field in apple_evaluator_getters:
        host.append(f"    if(DGAppleEvaluatorQuery((GLenum)dg_wire_message->args[1]))return DGHostAppleEvaluatorQuery(context,dg_wire_message,reply,{apple_evaluator_getters[field]});")
        host.append("    if(dg_wire_message->flags&DG_OUTPUT_ONLY)return -1;")
    if field in shader_include_names:
        host.append("    if(context->inside_begin) { if(!context->error)context->error=GL_INVALID_OPERATION;return 0; }")
    if field in pixel_map_fields:
        pack=int(field in pixel_map_getters)
        feature=f"({pack} || pixel_buffer) && !(context->connection->features2&DG_FEATURE2_PIXEL_MAPS)"
        client += [
            f"    int pixel_buffer=!!context->pixel_buffer[{pack}];",
            f"    if(context->profile!=0x1000 || context->common_state_blocked || ({feature})) {{ if(!context->error)context->error=GL_INVALID_OPERATION;return; }}",
            "    GLenum key=DGPixelMapSizeKey(map);if(!key) { if(!context->error)context->error=GL_INVALID_ENUM;return; }",
            "    if(!pixel_buffer && !values) { if(!context->error)context->error=GL_INVALID_VALUE;return; }",
        ]
        if pack:
            client.append("    GLint mapsize=0;if(!pixel_buffer){ glGetIntegerv(key,&mapsize);if(mapsize<=0)return; }")
        else:
            client += [
                "    GLint maximum=32;if(mapsize>maximum)glGetIntegerv(GL_MAX_PIXEL_MAP_TABLE,&maximum);",
                "    if(!DGPixelMapCountValid(map,mapsize,maximum)) { if(!context->error)context->error=GL_INVALID_VALUE;return; }",
            ]
        client.append("    size_t compat_pixel_bytes=DGArrayBytes(mapsize,sizeof(*values));")
    if field in compat_raster_shapes:
        pack=int(field in compat_pixel_getters)
        arg=parsed[-1][1]
        raster_width,raster_height,fmt,raster_type=compat_raster_shapes[field].split(",")
        client += [
            "    if(context->profile!=0x1000 || context->common_state_blocked || !(context->connection->features2&DG_FEATURE2_COMPAT_RASTER)) { if(!context->error)context->error=GL_INVALID_OPERATION;return; }",
            f"    int pixel_buffer=!!context->pixel_buffer[{pack}];",
        ]
        if field=="draw_pixels":
            client.append("    int no_input=DGDrawPixelsNoInput(width,height,format,type);")
        if raster_width=="width" and field!="draw_pixels":
            client.append("    if(width<0 || height<0) { if(!context->error)context->error=GL_INVALID_VALUE;return; }")
        valid_bits="!bits && !no_input" if field=="draw_pixels" else "!bits"
        no_span="pixel_buffer || no_input" if field=="draw_pixels" else "pixel_buffer"
        client += [
            f"    size_t bits=DGRasterPixelBits({fmt},{raster_type});if({valid_bits}) {{ if(!context->error)context->error=GL_INVALID_ENUM;return; }}",
            f"    uint64_t span=({no_span})?0:DGPixelLayoutSpan({raster_width},{raster_height},bits,context->row_length[{pack}],context->alignment[{pack}],context->skip_rows[{pack}],context->skip_pixels[{pack}],context->row_bytes[{pack}]);",
            "    if(span>DG_MAX_BYTES) { if(!context->error)context->error=GL_OUT_OF_MEMORY;return; }",
            f"    if(span && !{arg}) {{ if(!context->error)context->error=GL_INVALID_VALUE;return; }}",
            "    size_t compat_pixel_bytes=(size_t)span;",
        ]
    if field=="get_active_uniform_blockiv":
        client.append("    packet.message.args[14]=(uint32_t)block_count;")
    if field=="get_active_subroutine_uniformiv":
        client.append("    packet.message.args[14]=(uint32_t)compatible_count;")
    if field in state_getters:
        client.append("    if(count_key)packet.message.args[14]=(uint32_t)binary_count;")
    if field in {"tex_image1D", "tex_sub_image1D", "tex_image2D", "tex_sub_image2D", "tex_image3D", "tex_sub_image3D", "read_pixels"}:
        binding="GL_PIXEL_PACK_BUFFER_BINDING" if field=="read_pixels" else "GL_PIXEL_UNPACK_BUFFER_BINDING"
        host.append(f"    GLint pixel_buffer=0;if(context->inside_begin) {{ context->error=GL_INVALID_OPERATION;return 0; }}glGetIntegerv({binding},&pixel_buffer);if(pixel_buffer) {{ context->error=GL_INVALID_OPERATION;return 0; }}")
    if field == "get_tex_image":
        # Preserve the original 2D codec for older executors only.
        client += [
            "    if(DGReadTextureWithRowBytes(target,level,format,type,pixels,0))return;",
            "    if(!(context->connection->features&DG_FEATURE_TEXTURE_IMAGE_READBACK) || context->pixel_buffer[1] || (target!=GL_TEXTURE_RECTANGLE_ARB && target!=GL_TEXTURE_2D)) { if(!context->error)context->error=GL_INVALID_OPERATION;return; }",
            "    GLint width=0,height=0;glGetTexLevelParameteriv(target,level,GL_TEXTURE_WIDTH,&width);",
            "    glGetTexLevelParameteriv(target,level,GL_TEXTURE_HEIGHT,&height);",
            "    if(width<=0 || height<=0)return;",
        ]
    evaluator_query=field in {"get_mapdv", "get_mapfv", "get_mapiv"}
    apple_evaluator_upload=field.startswith("map_vertex_attrib") and field in apple_evaluators
    evaluator_upload=field in {"map1d", "map1f", "map2d", "map2f"} or apple_evaluator_upload
    if evaluator_query:
        element=parsed[-1][0].replace("*","").strip()
        client += [
            "    if(!(context->connection->features&DG_FEATURE_EVALUATOR_QUERIES) || context->profile!=0x1000) { if(!context->error)context->error=GL_INVALID_OPERATION;return; }",
            "    if(!DGMapDimensions(target) || (query!=GL_ORDER && query!=GL_DOMAIN && query!=GL_COEFF)) { if(!context->error)context->error=GL_INVALID_ENUM;return; }",
            "    if(!v) { if(!context->error)context->error=GL_INVALID_VALUE;return; }",
            "    GLint order[2]={0,0};",
            "    if(query==GL_COEFF) { glGetMapiv(target,GL_ORDER,order);if(order[0]<=0)return; }",
            f"    size_t map_bytes=DGMapQueryBytes(target,query,order[0],order[1],sizeof({element}));",
            "    packet.message.flags=DG_OUTPUT_ONLY;",
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
            if field == "read_pixels":
                # The host computes the pack layout once for both bounds and
                # the IOSurface correction; the guest packet is unchanged.
                inputs.append((i, typ, arg, size, output))
                output_args.append(arg)
                continue
            if field=="get_active_uniform_blockiv":
                host += [
                    "    if(dg_wire_message->args[14]>DG_MAX_BYTES/sizeof(GLint))return -1;",
                    "    GLint block_count=(GLint)dg_wire_message->args[14];",
                    "    if(pname!=0x8a43 && block_count!=1)return -1;",
                    "    if(pname==0x8a43 && !DGHostUniformBlockCapacity(context,program,uniformBlockIndex,block_count))return 0;",
                ]
            if field=="get_active_subroutine_uniformiv":
                host += [
                    "    if(dg_wire_message->args[14]>DG_MAX_BYTES/sizeof(GLint))return -1;",
                    "    GLint compatible_count=(GLint)dg_wire_message->args[14];",
                    "    if(pname!=0x8e4b && compatible_count!=1)return -1;",
                    "    if(pname==0x8e4b && !DGHostSubroutineCapacity(context,program,shadertype,index,compatible_count))return 0;",
                ]
            if field in state_getters:
                host += [
                    "    GLint binary_count=0;GLenum count_key=DGStateArrayCountKey(pname);",
                    "    if(count_key) {",
                    f"        if(dg_wire_message->args[14]>DG_MAX_BYTES/sizeof({element}))return -1;",
                    "        binary_count=(GLint)dg_wire_message->args[14];",
                    "        if(!DGHostStateArrayCapacity(context,count_key,binary_count))return 0;",
                    "    }",
                ]
            if field in clear_buffer_inputs:
                host.append(f"    if(!({clear_buffer_selectors[field]})) {{ context->error=GL_INVALID_ENUM;return 0; }}")
            if evaluator_query:
                host += [
                    "    if(context->inside_begin || context->profile!=kCGLOGLPVersion_Legacy) { reply->error=GL_INVALID_OPERATION;return 0; }",
                    "    if(!DGMapDimensions(target) || (query!=GL_ORDER && query!=GL_DOMAIN && query!=GL_COEFF)) { reply->error=GL_INVALID_ENUM;return 0; }",
                    "    GLenum prior=glGetError();if(prior && !context->error)context->error=prior;",
                    "    GLint order[2]={0,0};if(query==GL_COEFF){glGetMapiv(target,GL_ORDER,order);reply->error=glGetError();if(reply->error)return 0;}",
                    f"    size_t map_bytes=DGMapQueryBytes(target,query,order[0],order[1],sizeof({element}));",
                    "    if(dg_wire_message->args[2]!=1 || dg_wire_message->bytes!=map_bytes)return -1;",
                ]
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
        if field == "read_pixels":
            client.append(f"    DGPacketOutput(&packet,context,{i},{arg},({size}));")
            continue
        if field in compat_pixel_fields:
            pack=int(field in compat_pixel_getters)
            if field in pixel_map_fields:
                count="0" if pack else "mapsize"
                admit=f"DGHostPixelMapPointer(context,dg_wire_message,map,{count},sizeof(*{arg}),{i},{pack},&pixel_pointer)"
            else:
                admit=f"DGHostCompatRasterPointer(context,dg_wire_message,{compat_raster_shapes[field]},{i},{pack},&pixel_pointer)"
            client += [
                f"    if(pixel_buffer) {{ packet.message.args[15]=1;packet.message.args[{i}]=(uintptr_t){arg}; }}",
                f"    else if(compat_pixel_bytes)DGPacketPointer(&packet,{i},{arg},compat_pixel_bytes,{2 if pack else 0});",
            ]
            host += [
                "    void *pixel_pointer=NULL;",
                f"    int admitted={admit};",
                f"    if(admitted<=0)return admitted;{arg}=({typ})pixel_pointer;",
            ]
            continue
        if evaluator_upload:
            two=field.startswith(("map2","map_vertex_attrib2"))
            dimensions=2 if two else 1
            strides="ustride,uorder,vstride,vorder" if two else "stride,order,0,1"
            domain="u1!=u2 && v1!=v2" if two else "u1!=u2"
            element=typ.replace("const","").replace("*","").strip()
            target,width=("index","size") if apple_evaluator_upload else ("target","DGMapComponents(target)")
            client.append(f"    if(!DGPackEvaluatorPoints(context,&packet,{i},{arg},{width},{strides},sizeof({element}),{dimensions},{4 if apple_evaluator_upload else 3})) {{ DGPacketFree(&packet);return; }}")
            host += [
                f"    int admitted=DGHostEvaluatorInput(context,dg_wire_message,reply,{target},{width},{strides},{domain},sizeof({element}),{dimensions},{i},{int(apple_evaluator_upload)});",
                "    if(admitted<=0)return admitted;",
                f"    {arg}=({typ})(dg_wire_message+1);",
            ]
            continue
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
        if field in {"get_integer_indexedv_EXT", "get_boolean_indexedv_EXT"}:
            host.append("    if(!DGIndexedStateCount(param)) return -1;")
        if field=="get_integer64i_v":
            host.append("    if(!DGIndexedInteger64Count(target)) return -1;")
        if field in sampler_arrays:
            host.append("    if(!DGSamplerParameterCount(pname)) return -1;")
        if field in imaging_parameters:
            host += [
                "    if(context->inside_begin || context->profile!=kCGLOGLPVersion_Legacy) { if(!context->error)context->error=GL_INVALID_OPERATION;return 0; }",
                f"    if(!DGImagingParameterCount({imaging_parameters[field]},pname)) {{ if(!context->error)context->error=GL_INVALID_ENUM;return 0; }}",
            ]
        if field == "get_multisamplefv":
            host.append("    if(pname!=0x8e50) { context->error=GL_INVALID_ENUM;return 0; }")
        if field in viewport_indexed_queries:
            host.append("    if(!DGIndexedInteger64Count(target)) { context->error=GL_INVALID_ENUM;return 0; }")
        if field=="patch_parameterfv":
            host.append("    if(!DGPatchLevelCount(pname)) { if(!context->error)context->error=GL_INVALID_ENUM;return 0; }")
        pointer = f"n?{arg}:NULL" if field in {"gen_programs_ARB", "delete_programs_ARB"} else f"len?{arg}:NULL" if field == "program_string_ARB" else f"DGCallListsBytes(n,type)?{arg}:NULL" if field == "call_lists" else arg
        if evaluator_query:
            client.append(f"    DGPacketPointer(&packet,{i},{arg},map_bytes,1);")
        elif field in shader_include_names and not output:
            length="namelen" if arg=="name" else "stringlen"
            client.append(f"    DGPacketIncludeString(&packet,{i},{arg},{length});")
            host.append(f"    {arg}=DGHostIncludeString(dg_wire_message,{i},{length});")
            host.append(f"    if(dg_wire_message->args[{i}] && !{arg})return -1;")
            continue
        elif field in {"get_object_label_EXT","get_named_string_ARB","get_named_string_iv_ARB"}:
            # A NULL label requests the full length, unlike a non-NULL
            # zero-capacity buffer. Do not replace it with a dummy pointer.
            client.append(f"    if({arg})DGPacketOutput(&packet,context,{i},{arg},({size}));")
        elif output:
            client.append(f"    DGPacketOutput(&packet,context,{i},{arg},({size}));")
        elif arg in query_inputs:
            client.append(f"    DGPacketQueryInput(&packet,{i},{arg},({size}));")
        elif unsigned_uniform:
            client.append(f"    static const GLuint unused=0;DGPacketPointer(&packet,{i},count<=0 || location==-1?&unused:{arg},({size}),0);")
        elif field in clear_buffer_inputs:
            alternate=clear_buffer_inputs[field]
            valid="buffer==GL_COLOR && drawbuffer>=0"
            if alternate:
                valid=f"({valid}) || (buffer=={alternate} && drawbuffer==0)"
            element=typ.replace("const","").replace("*","").strip()
            client.append(f"    static const {element} unused[4]={{0}};DGPacketPointer(&packet,{i},({valid})?{arg}:unused,({size}),0);")
        elif field in {"buffer_data","buffer_sub_data"}:
            # Zero bytes do not inspect even a non-NULL application pointer.
            # NULL BufferData is allocation-only at any native-supported size.
            condition='size>0 && data' if field=='buffer_data' else 'size>0'
            client.append(f"    if({condition})DGPacketPointer(&packet,{i},{arg},({size}),0);")
        elif texture_shape_valid:
            # Invalid dimensions/enums cannot inspect application storage.
            # Forward the original arguments with NULL, so native GL owns both
            # immediate errors and GL_COMPILE's deferred error command.
            # NULL is allocation-only outside a PBO, even for images whose
            # byte count exceeds the transfer bound. Do not shape absent data.
            client.append(f"    if({texture_shape_valid} && {arg})DGPacketPointer(&packet,{i},{arg},({size}),0);")
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
            if field in compact_texture_inputs:
                dimensions=3 if "3D" in field else 2 if "2D" in field else 1
                span=f"DGHostUnpackBytes(context,dg_wire_message,{dimensions},width,{'height' if dimensions>=2 else '1'},{'depth' if dimensions==3 else '1'},format,type)"
            if field in {"get_tex_parameteriv", "get_shaderiv", "get_programiv", "get_framebuffer_attachment_parameteriv_EXT", "get_renderbuffer_parameteriv_EXT"}:
                # Old peers sent a one-byte dummy for scalar pnames absent
                # from their shared whitelist. Never let native GL write a
                # full GLint there, or drop the whole connection for this
                # previously unsupported query. New peers send a real span.
                host.append(f"    if(!DGParameterCount(pname) && dg_wire_message->bytes==1 && dg_wire_message->args[{i}]==1) {{ if(!context->error)context->error=context->inside_begin?GL_INVALID_OPERATION:GL_INVALID_ENUM;return 0; }}")
            if texture_shape_valid:
                host += [f"    if(!({texture_shape_valid})) {{",
                         f"        if(dg_wire_message->args[{i}] || dg_wire_message->bytes || dg_wire_message->args[15])return -1;",
                         "    }else {"]
            if field in {"tex_image1D","tex_image2D","tex_image3D"}:
                host.append(f"    if(dg_wire_message->args[{i}]){arg}=({typ})DGHostPointer(dg_wire_message,{i},({span}));")
            else:
                host.append(f"    {arg}=({typ})DGHostPointer(dg_wire_message,{i},({span}));")
            # Null is legal for optional GL data/result parameters; malformed
            # non-null offsets, not ordinary GL nulls, fail the connection.
            optional = (field in {"tex_image1D", "tex_image2D", "tex_image3D", "buffer_data", "buffer_sub_data", "get_object_label_EXT","get_named_string_ARB","get_named_string_iv_ARB"} or
                        (field in {"get_shader_info_log", "get_program_info_log", "get_info_log_ARB", "get_shader_source_ARB",
                                   "get_active_uniform_ARB", "get_active_attrib_ARB",
                                   "get_transform_feedback_varying_EXT","get_program_pipeline_info_log","get_program_binary"} and arg == "length") or
                        (field in (ubo_names.keys() | subroutine_string_results) and arg == "length") or
                        (field == "get_attached_shaders" and arg == "count"))
            if field in {"gen_programs_ARB", "delete_programs_ARB", "program_string_ARB"}:
                count = "len" if field == "program_string_ARB" else "n"
                condition = f"({count} && !{arg}) || (dg_wire_message->args[{i}] && !{arg})"
            elif field == "tex_sub_image1D":
                condition = f"(width && !{arg}) || (dg_wire_message->args[{i}] && !{arg})"
            elif field == "buffer_sub_data":
                condition = f"(size && !{arg}) || (dg_wire_message->args[{i}] && !{arg})"
            elif field == "tex_sub_image3D":
                condition = f"(width && height && depth && !{arg}) || (dg_wire_message->args[{i}] && !{arg})"
            elif field == "get_attached_shaders" and arg == "shaders":
                condition = f"(maxCount && !{arg}) || (dg_wire_message->args[{i}] && !{arg})"
            elif field == "get_transform_feedback_varying_EXT" and arg == "name":
                condition = f"(bufSize>0 && !{arg}) || (dg_wire_message->args[{i}] && !{arg})"
            elif field in ubo_names and arg==ubo_names[field]:
                condition = f"(bufSize>0 && !{arg}) || (dg_wire_message->args[{i}] && !{arg})"
            elif field == "call_lists":
                condition = f"(DGCallListsBytes(n,type) && !{arg}) || (dg_wire_message->args[{i}] && !{arg})"
            else:
                condition = f"dg_wire_message->args[{i}] && !{arg}" if optional else f"!{arg}"
            host.append(f"    if({condition}) return -1;")
            if texture_shape_valid:
                host.append("    }")
    if field in shader_include_names:
        invalid={"named_string_ARB":"!name || !string","delete_named_string_ARB":"!name",
                 "get_named_string_ARB":"!name || bufSize<0 || (bufSize>0 && !string)",
                 "get_named_string_iv_ARB":"!name || !params"}.get(field)
        if invalid:
            host.append(f"    if({invalid}) {{ if(!context->error)context->error=GL_INVALID_VALUE;return 0; }}")
        elif field=="is_named_string_ARB":
            host.append("    if(!name)return 0;")
    if field=="get_integerv":
        host.append("    if(pname==GL_MAX_VERTEX_UNITS_ARB && !context->inside_begin) { GLint units=DGHostWeightUnits(context);if(units){*params=units;return 0;} }")
    if field in {"get_object_parameteriv_APPLE","object_purgeable_APPLE","object_unpurgeable_APPLE"}:
        host.append("    if(context->inside_begin || !DGHostPurgeableSupported(context)) { if(!context->error)context->error=GL_INVALID_OPERATION;return 0; }")
        host.append("    if(DGPurgeablePrivateName(context,objectType,name)) { if(!context->error)context->error=GL_INVALID_VALUE;return 0; }")
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
    sync_expr="!pixel_buffer" if field in compat_pixel_getters else str(int(bool(sync)))
    client.append(f"    struct DGReply reply=DGSubmit(context,&packet,{sync_expr});")
    if name == "glFlush":
        client.append("    DGFlushTransport(context);")
    for i, typ, arg, size, output in inputs:
        if evaluator_query:
            client.append(f"    if(!reply.error && reply.result==map_bytes)DGPacketCopy(&packet,{i},{arg},map_bytes);")
        elif output and field in compat_pixel_getters:
            client.append(f"    if(!pixel_buffer)DGPacketCopy(&packet,{i},{arg},compat_pixel_bytes);")
        elif output:
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
    if field == "read_pixels":
        host += ["    return DGHostReadPixels(context,dg_wire_message,x,y,width,height,format,type);", "}"]
        continue
    if field == "link_program":
        host += [f"    DGForgetUniformShapes(context,{parsed[0][1]});",
                 f"    DGHostLinkProgram(context,{parsed[0][1]});", "    return 0;", "}"]
        continue
    if field == "bind_framebuffer_EXT":
        host += ["    glBindFramebufferEXT(target,framebuffer ? framebuffer : context->framebuffer);",
                 "    if(target!=GL_READ_FRAMEBUFFER_EXT)context->iosurface_draw_dirty=1;", "    return 0;", "}"]
        continue
    if field == "generate_mipmap_EXT":
        host += ["    DGHostGenerateMipmap(context,target);", "    return 0;", "}"]
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
    if field == "clear_bufferfi":
        host.append(f"    if(!({clear_buffer_selectors[field]})) {{ context->error=GL_INVALID_ENUM;return 0; }}")
    if field in {"tex_image3D", "tex_sub_image3D"}:
        host.append("    if(!DGHostCompactUnpackBegin(context,dg_wire_message,&compact_scope))return 0;")
        host.append("    struct DGAutomaticMipmapScope automatic=DGBeginAutomaticMipmap(target,level,context->profile==kCGLOGLPVersion_Legacy,context->inside_begin,context->list_mode,&context->error,0,DGNonzeroMipmapBasePossible(context));")
        if field=="tex_image3D":
            host.append(f"    DGHostTexture3D(context,0,target,level,{parsed[2][1]},0,0,0,width,height,depth,border,format,type,pixels,0);")
        else:
            host.append("    DGHostTexture3D(context,1,target,level,0,xoffset,yoffset,zoffset,width,height,depth,0,format,type,pixels,0);")
        host += ["    DGHostCompactUnpackEnd(context,&compact_scope);",
                 "    DGEndAutomaticMipmap(&automatic,NULL,&context->error);",
                 "    return 0;", "}"]
        continue
    if field in {"test_fence_APPLE", "finish_fence_APPLE"}:
        host.append("    struct DGFenceName *f=DGFence(context,fence,0);")
        host.append("    if(f && !f->set) { reply->result=GL_TRUE;return 0; }")
    if field=="get_polygon_stipple":
        host += [f"    DGHostGetPolygonStipple(context,{parsed[-1][1]});", "    return 0;", "}"]
        continue
    host.append(f"    typedef {ret} (*Proc)({signature});")
    if field in timer_query_results:
        member=timer_query_results[field]
        host.append(f"    Proc proc=context->profile==kCGLOGLPVersion_Legacy ? dg_core_api.{member}_ext : dg_core_api.{member};")
    elif field=="query_counter":
        host.append("    Proc proc=dg_core_api.query_counter;")
    elif field=="program_parameteri_EXT":
        host.append('    static Proc core,ext;Proc proc;')
        host.append('    if(pname==0x8258 || pname==0x8257) { if(!core)core=(Proc)dlsym(RTLD_DEFAULT,"glProgramParameteri");proc=core; }')
        host.append('    else { if(!ext)ext=(Proc)dlsym(RTLD_DEFAULT,"glProgramParameteriEXT");proc=ext; }')
    elif field in {"enable_indexed_EXT","disable_indexed_EXT","is_enabled_indexed_EXT",
                   "get_boolean_indexedv_EXT","get_integer_indexedv_EXT"}:
        target=parsed[0][1]
        core={"enable_indexed_EXT":"glEnablei","disable_indexed_EXT":"glDisablei","is_enabled_indexed_EXT":"glIsEnabledi",
              "get_boolean_indexedv_EXT":"glGetBooleani_v","get_integer_indexedv_EXT":"glGetIntegeri_v"}[field]
        host.append(f'    static Proc ext,core;Proc proc;')
        host.append(f'    if(({target}==GL_SCISSOR_TEST || {target}==GL_SCISSOR_BOX) && context->profile!=kCGLOGLPVersion_Legacy) {{ if(!core)core=(Proc)dlsym(RTLD_DEFAULT,"{core}");proc=core; }}')
        host.append(f'    else {{ if(!ext)ext=(Proc)dlsym(RTLD_DEFAULT,"{name}");proc=ext; }}')
        if field in {"enable_indexed_EXT","disable_indexed_EXT"}:
            host.append(f"    if({target}==GL_SCISSOR_TEST)context->indexed_scissor=1;")
    else:
        host.append(f'    static Proc proc;if(!proc) proc=(Proc)dlsym(RTLD_DEFAULT,"{name}");')
    host.append("    if(!proc) { context->error=GL_INVALID_OPERATION;" +
                ("reply->result=UINT64_MAX;" if field=="get_uniform_buffer_offset_EXT" else "") + "return 0; }")
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
                 "eval_mesh1", "eval_mesh2", "blit_framebuffer_EXT"} | clear_buffer_fields or re.fullmatch(r"rect[dfis]v?", field):
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
        guard=f"({texture_shape_valid})?" if texture_shape_valid else ""
        fallback=":0" if texture_shape_valid else ""
        host.append(f"    GLuint redefining_alias={guard}DGBeginAliasRedefinition(context,target,{level}){fallback};")
    if field in {"flush", "finish"}:
        host.append("    DGRefreshFlippedAliases(context,0);")
    if field in {"call_list", "call_lists"}:
        host += ["#ifdef VIM_ALIAS_REBIND_EXPERIMENT", "    DGInvalidateAliasStorage(context);", "#endif"]
    if field in {"tex_sub_image2D", "compressed_tex_sub_image2D"}:
        guard=f"({texture_shape_valid}) && " if texture_shape_valid else ""
        host.append(f"    if({guard}!DGPrepareScopedTextureCPUWrite(context,target))return 0;")
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
    if field in compact_texture_inputs:
        host.append("    if(!DGHostCompactUnpackBegin(context,dg_wire_message,&compact_scope))return 0;")
    automatic_mipmap = field in {"tex_image1D", "tex_image2D", "tex_sub_image1D", "tex_sub_image2D",
                                "compressed_tex_image2D", "compressed_tex_sub_image2D",
                                "copy_tex_image1D", "copy_tex_image2D", "copy_tex_sub_image1D", "copy_tex_sub_image2D", "copy_tex_sub_image3D"}
    if automatic_mipmap:
        copy_subimage=int(field.startswith("copy_tex_"))
        host.append(f"    struct DGAutomaticMipmapScope automatic=DGBeginAutomaticMipmap(target,level,context->profile==kCGLOGLPVersion_Legacy,context->inside_begin,context->list_mode,&context->error,{copy_subimage},DGNonzeroMipmapBasePossible(context));")
    if field.startswith("tex_parameter"):
        typ,value=parsed[-1]
        nonzero=f"({value} && {value}[0]!=0)" if '*' in typ else f"{value}!=0"
        host.append(f"    if(context->profile==kCGLOGLPVersion_Legacy && context->alias_group && pname==GL_TEXTURE_BASE_LEVEL && {nonzero})context->alias_group->nonzero_mipmap_base_possible=1;")
    host.append((f"    {ret} result=" if ret != "void" else "    ") + f"proc({args});")
    if field in compact_texture_inputs and not alias_redefine:
        host.append("    DGHostCompactUnpackEnd(context,&compact_scope);")
    if field in {"enable","disable"}:
        host.append(f"    if(cap==0x8f9d && context->profile!=kCGLOGLPVersion_Legacy)context->primitive_restart={int(field=='enable')};")
    if evaluator_query:
        host.append("    reply->error=glGetError();if(!reply->error)reply->result=map_bytes;")
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
    if field in compat_pixel_getters:
        host.append("    if(dg_wire_message->args[15])DGPBOWritten(context);")
    if field in {"program_binary", "link_program", "delete_program"}:
        host.append(f"    DGForgetUniformShapes(context,{parsed[0][1]});")
    if field in {"call_list", "call_lists"}:
        host.append("    DGForgetUniformShapes(context,0);")
    if field in {"flush", "finish"}:
        host.append("    DGSubmitIOSurfaceWrites(context);")
    if alias_redefine:
        host.append("    DGEndAliasRedefinition(context,redefining_alias);")
        if field in compact_texture_inputs:
            host.append("    DGHostCompactUnpackEnd(context,&compact_scope);")
    if automatic_mipmap:
        host.append("    DGEndAutomaticMipmap(&automatic,NULL,&context->error);")
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
    if field == "begin":
        host.append("    context->inside_begin=1;")
    if field == "end":
        host.append("    context->inside_begin=0;")
    if field in {"new_list", "end_list"}:
        host.append("    DGHostListModeChanged(context);")
    # Conservative native scissor bounds, accumulated over the whole batch.
    # Not the mutable CGS surface shape queried after submission.
    if field in {"end", "draw_pixels", "copy_pixels", "bitmap", "call_list", "call_lists",
                 "eval_mesh1", "eval_mesh2", "blit_framebuffer_EXT"} or re.fullmatch(r"rect[dfis]v?", field):
        host.append("    DGMarkDrawableDamage(context);")
    if field == "clear":
        host.append("    if(mask&GL_COLOR_BUFFER_BIT) DGMarkDrawableDamage(context);")
    if field in clear_buffer_inputs:
        host.append("    if(buffer==GL_COLOR) DGMarkDrawableDamage(context);")
    if field == "gen_fences_APPLE":
        host.append("    for(GLsizei i=0;i<n;++i) DGFence(context,fences[i],1);")
    if field == "delete_fences_APPLE":
        host.append("    for(GLsizei i=0;i<n;++i) DGForgetFence(context,fences[i]);")
    if field == "set_fence_APPLE":
        host.append("    struct DGFenceName *f=DGFence(context,fence,0);if(f) f->set=1;")
    if field in {"get_integerv", "get_floatv", "get_doublev", "get_booleanv", "get_integer64v_sync"}:
        out_type = parsed[1][0].replace("*", "").strip()
        virtual_value="!!virtual_value" if field=="get_booleanv" else "virtual_value"
        host.append(f"    GLint virtual_value=0;if(params && DGVirtualInteger(context,pname,&virtual_value)) *params=({out_type})({virtual_value});")
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
backing_read_only = sampler_fields | {"flush", "finish", "bind_texture", "active_texture",
                     "bind_framebuffer_EXT", "read_buffer", "draw_buffer",
                     "viewport", "scissor", "color_mask", "enable", "disable",
                     "get_mapdv", "get_mapfv", "get_mapiv", "query_counter"}
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
common_neutral = sampler_fields | pipeline_names.keys() | fp64_names.keys() | fp64_vertex_names.keys() | set().union(*render_target_features.values()) | {
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
    "object_purgeable_APPLE", "object_unpurgeable_APPLE", "get_object_parameteriv_APPLE",
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
    "viewport_arrayv": "(DG_COMMON_ALL&~DG_COMMON_VIEWPORT)",
    "viewport_indexedf": "(DG_COMMON_ALL&~DG_COMMON_VIEWPORT)",
    "viewport_indexedfv": "(DG_COMMON_ALL&~DG_COMMON_VIEWPORT)",
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
for enum, value in re.findall(r"#define\s+(GL_\w+)\s+(0[xX][0-9a-fA-F]+)",
                              public + "\n" + (h / "gl3.h").read_text()):
    if enum.startswith("GL_MAX_") or enum.endswith("_BINDING") or (enum.startswith("GL_PIXEL_MAP_") and enum.endswith("_SIZE")):
        limits[int(value, 16)] = enum
for value in (0x0D3A, 0x91BE, 0x91BF):  # viewport dims, compute work-group vectors
    limits.pop(value, None)
(a.output / "direct-get-counts.inc").write_text("\n".join(
    f"case 0x{value:x}: return 1; /* {name} */" for value, name in sorted(limits.items())) + "\n")
print(f"PassthroughOpenGL: {sum(r['supported'] for r in report)} supported / {len(rows)} GLI slots; SDK {digest[:12]}")
