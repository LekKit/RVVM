/*
gpu-vulkan-spirv.h - SPIR-V compiler
Copyright (C) 2026  David Korenchuk <github.com/epoll-reactor-2>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef RVVM_GPU_VULKAN_SPIRV_H
#define RVVM_GPU_VULKAN_SPIRV_H

#include <stdint.h>
#include <string.h>
#include <util/compiler.h>
#include <util/mem_ops.h>
#include <util/vector.h>

// https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html
#define SPIRV_MAGIC                            0x07230203
#define SPIRV_VERSION_1_3                      0x00010300
#define SPIRV_GENERATOR_MAGIC                  0x000B0000

#define SPIRV_OP_NOP                           0
#define SPIRV_OP_UNDEF                         1
#define SPIRV_OP_NAME                          5
#define SPIRV_OP_EXT_INST_IMPORT               11
#define SPIRV_OP_EXT_INST                      12
#define SPIRV_OP_MEMORY_MODEL                  14
#define SPIRV_OP_ENTRY_POINT                   15
#define SPIRV_OP_EXECUTION_MODE                16
#define SPIRV_OP_CAPABILITY                    17
#define SPIRV_OP_TYPE_VOID                     19
#define SPIRV_OP_TYPE_BOOL                     20
#define SPIRV_OP_TYPE_INT                      21
#define SPIRV_OP_TYPE_FLOAT                    22
#define SPIRV_OP_TYPE_VECTOR                   23
#define SPIRV_OP_TYPE_ARRAY                    28
#define SPIRV_OP_TYPE_STRUCT                   30
#define SPIRV_OP_TYPE_POINTER                  32
#define SPIRV_OP_TYPE_FUNCTION                 33
#define SPIRV_OP_TYPE_EVENT                    34
#define SPIRV_OP_TYPE_DEVICE_EVENT             35
#define SPIRV_OP_TYPE_QUEUE                    37
#define SPIRV_OP_CONSTANT_TRUE                 41
#define SPIRV_OP_CONSTANT_FALSE                42
#define SPIRV_OP_CONSTANT                      43
#define SPIRV_OP_CONSTANT_COMPOSITE            44
#define SPIRV_OP_FUNCTION                      54
#define SPIRV_OP_FUNCTION_PARAMETER            55
#define SPIRV_OP_FUNCTION_END                  56
#define SPIRV_OP_VARIABLE                      59
#define SPIRV_OP_LOAD                          61
#define SPIRV_OP_STORE                         62
#define SPIRV_OP_ACCESS_CHAIN                  65
#define SPIRV_OP_DECORATE                      71
#define SPIRV_OP_MEMBER_DECORATE               72
#define SPIRV_OP_VECTOR_SHUFFLE                79
#define SPIRV_OP_COMPOSITE_CONSTRUCT           80
#define SPIRV_OP_COMPOSITE_EXTRACT             81
#define SPIRV_OP_F2U                           109
#define SPIRV_OP_CONVERT_FTOS                  110
#define SPIRV_OP_CONVERT_STOF                  111
#define SPIRV_OP_U2F                           112
#define SPIRV_OP_BITCAST                       124
#define SPIRV_OP_SNEG                          126
#define SPIRV_OP_F_NEGATE                      127
#define SPIRV_OP_I_ADD                         128
#define SPIRV_OP_F_ADD                         129
#define SPIRV_OP_I_SUB                         130
#define SPIRV_OP_F_SUB                         131
#define SPIRV_OP_I_MUL                         132
#define SPIRV_OP_F_MUL                         133
#define SPIRV_OP_UDIV                          134
#define SPIRV_OP_SDIV                          135
#define SPIRV_OP_F_DIV                         136
#define SPIRV_OP_UMOD                          137
#define SPIRV_OP_SREM                          138
#define SPIRV_OP_LOR                           166
#define SPIRV_OP_AND                           167
#define SPIRV_OP_LNOT                          168
#define SPIRV_OP_SELECT                        169
#define SPIRV_OP_I_EQUAL                       170
#define SPIRV_OP_INE                           171
#define SPIRV_OP_UGT                           172
#define SPIRV_OP_SGT                           173
#define SPIRV_OP_UGE                           174
#define SPIRV_OP_SGE                           175
#define SPIRV_OP_ULT                           176
#define SPIRV_OP_SLT                           177
#define SPIRV_OP_ULE                           178
#define SPIRV_OP_SLE                           179
#define SPIRV_OP_F_ORD_EQ                      180
#define SPIRV_OP_F_ORD_NE                      182
#define SPIRV_OP_F_UNE                         183
#define SPIRV_OP_F_ORD_LT                      184
#define SPIRV_OP_F_ORD_GT                      186
#define SPIRV_OP_F_ORD_LE                      188
#define SPIRV_OP_F_ORD_GE                      190
#define SPIRV_OP_SHRL                          194
#define SPIRV_OP_SHRA                          195
#define SPIRV_OP_SHLL                          196
#define SPIRV_OP_BIT_OR                        197
#define SPIRV_OP_BIT_XOR                       198
#define SPIRV_OP_BIT_AND                       199
#define SPIRV_OP_NOT                           200
#define SPIRV_OP_LABEL                         248
#define SPIRV_OP_BRANCH                        249
#define SPIRV_OP_RETURN                        253

#define SPIRV_CAPABILITY_SHADER                1
#define SPIRV_MEMORY_MODEL_LOGICAL             0
#define SPIRV_MEMORY_MODEL_GLSL450             1
#define SPIRV_EXECUTION_MODEL_VERTEX           0
#define SPIRV_EXECUTION_MODEL_FRAGMENT         4
#define SPIRV_EXECUTION_MODE_ORIGIN_UPPER_LEFT 7
#define SPIRV_STORAGE_CLASS_UNIFORM_CONSTANT   0
#define SPIRV_STORAGE_CLASS_INPUT              1
#define SPIRV_STORAGE_CLASS_UNIFORM            2
#define SPIRV_STORAGE_CLASS_OUTPUT             3
#define SPIRV_STORAGE_CLASS_FUNCTION           7
#define SPIRV_STORAGE_CLASS_PUSH_CONSTANT      9
#define SPIRV_DECORATION_LOCATION              30
#define SPIRV_DECORATION_BUILTIN               11
#define SPIRV_DECORATION_BINDING               33
#define SPIRV_DECORATION_DESCRIPTOR_SET        34
#define SPIRV_DECORATION_BLOCK                 2
#define SPIRV_DECORATION_ARRAY_STRIDE          6
#define SPIRV_DECORATION_OFFSET                35
#define SPIRV_BUILTIN_POSITION                 0
#define SPIRV_BUILTIN_VERTEX_INDEX             42
#define SPIRV_BUILTIN_FRAG_COORD               15
#define SPIRV_GLSL_STD450_ROUND                1
#define SPIRV_GLSL_STD450_ROUND_EVEN           2
#define SPIRV_GLSL_STD450_TRUNC                3
#define SPIRV_GLSL_STD450_FABS                 4
#define SPIRV_GLSL_STD450_SABS                 5
#define SPIRV_GLSL_STD450_FSIGN                6
#define SPIRV_GLSL_STD450_FLOOR                8
#define SPIRV_GLSL_STD450_CEIL                 9
#define SPIRV_GLSL_STD450_FRACT                10
#define SPIRV_GLSL_STD450_SIN                  13
#define SPIRV_GLSL_STD450_COS                  14
#define SPIRV_GLSL_STD450_POW                  26
#define SPIRV_GLSL_STD450_EXP                  27
#define SPIRV_GLSL_STD450_LOG                  28
#define SPIRV_GLSL_STD450_EXP2                 29
#define SPIRV_GLSL_STD450_LOG2                 30
#define SPIRV_GLSL_STD450_SQRT                 31
#define SPIRV_GLSL_STD450_INVERSE_SQRT         32
#define SPIRV_GLSL_STD450_FMIN                 37
#define SPIRV_GLSL_STD450_UMIN                 38
#define SPIRV_GLSL_STD450_SMIN                 39
#define SPIRV_GLSL_STD450_UMAX                 41
#define SPIRV_GLSL_STD450_SMAX                 42
#define SPIRV_GLSL_STD450_FMAX                 40
#define SPIRV_GLSL_STD450_FCLAMP               43

typedef vector_t(uint32_t) spirv_words_t;

// In case when internal container will change.
#define spirv_push vector_push_back

typedef struct {
    uint32_t next_id;
    int      oom;

    spirv_words_t caps;         // OpCapability
    spirv_words_t ext_imports;  // OpExtInstImport
    spirv_words_t mem_model;    // OpMemoryModel
    spirv_words_t entry_points; // OpEntryPoint
    spirv_words_t exec_modes;   // OpExecutionMode
    spirv_words_t debug;        // OpName/OpMemberName (optional)
    spirv_words_t annotations;  // OpDecorate/OpMemberDecorate
    spirv_words_t globals;      // OpType..., OpConstant..., OpVariable (global)
    spirv_words_t func_vars;    // OpVariable (Function storage, must be
                                // the first instructions of the entry
                                // block per spec sec 2.16.1)
    spirv_words_t func_body;    // The rest of the single basic block.

    uint32_t glsl_std_450; // Result ID of the OpExtInstImport.

    // Small linear caches so re-requesting "float32" or "the constant
    // 1.0f" a hundred times across one kernel doesn't emit a hundred
    // duplicate OpType/OpConstant definitions (legal per spec, but
    // wasteful and harder to read in spirv-dis output).
    uint32_t t_void;
    uint32_t t_bool;
    uint32_t t_f32;
    uint32_t t_i32;
    uint32_t t_u32;
    uint32_t t_vec3;
    uint32_t t_vec4;

    struct {
        uint32_t storage;
        uint32_t pointee;
        uint32_t id;
    } ptr_cache[32];

    size_t ptr_cache_n;

    // I think, these structures are useless piece of shit.
    struct {
        float    v;
        uint32_t id;
    } fconst_cache[64];

    size_t fconst_cache_n;

    struct {
        int32_t  v;
        uint32_t id;
    } iconst_cache[32];

    size_t iconst_cache_n;

    struct {
        uint32_t v;
        uint32_t id;
    } uconst_cache[32];

    size_t uconst_cache_n;
} spirv_module_t;

static forceinline void spirv_push_string(spirv_words_t* words, const char* string)
{
    size_t len    = strlen(string) + 1; // including NULL.
    size_t nwords = (len + 3) / 4;

    for (size_t i = 0; i < nwords; i++) {
        uint32_t w = 0;
        for (int j = 0; j < 4; j++) {
            size_t  idx  = i * 4 + (size_t)j;
            uint8_t c    = (idx < len) ? (uint8_t)string[idx] : 0;
            w           |= ((uint32_t)c) << (8 * j);
        }
        spirv_push(*words, w);
    }
}

// Initialize all fields/vectors to zero except next ID.
static forceinline void spirv_module_init(spirv_module_t* module)
{
    memset(module, 0, sizeof(*module));
    module->next_id = 1;
}

static forceinline uint32_t spirv_seq_id(spirv_module_t* module)
{
    return module->next_id++;
}

static forceinline void spirv_module_begin(spirv_module_t* module)
{
    spirv_push(module->caps, SPIRV_OP_CAPABILITY | (2 << 16));
    spirv_push(module->caps, SPIRV_CAPABILITY_SHADER);
    module->glsl_std_450 = spirv_seq_id(module);

    spirv_push(module->ext_imports, SPIRV_OP_EXT_INST_IMPORT | (6 << 16));
    spirv_push(module->ext_imports, module->glsl_std_450);
    spirv_push_string(&module->ext_imports, "GLSL.std.450");
    spirv_push(module->mem_model, SPIRV_OP_MEMORY_MODEL | (3 << 16));
    spirv_push(module->mem_model, SPIRV_MEMORY_MODEL_LOGICAL);
    spirv_push(module->mem_model, SPIRV_MEMORY_MODEL_GLSL450);
}

static forceinline uint32_t spirv_type_void(spirv_module_t* module)
{
    if (module->t_void) {
        return module->t_void;
    }
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->globals, SPIRV_OP_TYPE_VOID | (2 << 16));
    spirv_push(module->globals, id);
    module->t_void = id;
    return id;
}

static forceinline uint32_t spirv_type_bool(spirv_module_t* module)
{
    if (module->t_bool) {
        return module->t_bool;
    }
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->globals, SPIRV_OP_TYPE_BOOL | (2 << 16));
    spirv_push(module->globals, id);
    module->t_bool = id;
    return id;
}

static forceinline uint32_t spirv_type_float32(spirv_module_t* module)
{
    if (module->t_f32) {
        return module->t_f32;
    }
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->globals, SPIRV_OP_TYPE_FLOAT | (3 << 16));
    spirv_push(module->globals, id);
    spirv_push(module->globals, 32);
    module->t_f32 = id;
    return id;
}

static forceinline uint32_t spirv_type_int(spirv_module_t* module, uint32_t* type, uint32_t sign)
{
    if (*type) {
        return *type;
    }
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->globals, SPIRV_OP_TYPE_INT | (4 << 16));
    spirv_push(module->globals, id);
    spirv_push(module->globals, 32);
    spirv_push(module->globals, sign);
    *type = id;
    return id;
}

static forceinline uint32_t spirv_type_int32(spirv_module_t* module)
{
    return spirv_type_int(module, &module->t_i32, /*signed=*/1);
}

static forceinline uint32_t spirv_type_uint32(spirv_module_t* module)
{
    return spirv_type_int(module, &module->t_u32, /*signed=*/0);
}

static forceinline uint32_t spirv_type_vec3_float32(spirv_module_t* module)
{
    if (module->t_vec3) {
        return module->t_vec3;
    }
    uint32_t comp = spirv_type_float32(module);
    uint32_t id   = spirv_seq_id(module);
    spirv_push(module->globals, SPIRV_OP_TYPE_VECTOR | (4 << 16));
    spirv_push(module->globals, id);
    spirv_push(module->globals, comp);
    spirv_push(module->globals, 3);
    module->t_vec3 = id;
    return id;
}

static forceinline uint32_t spirv_type_vec4_float32(spirv_module_t* module)
{
    if (module->t_vec4) {
        return module->t_vec4;
    }
    uint32_t comp = spirv_type_float32(module);
    uint32_t id   = spirv_seq_id(module);
    spirv_push(module->globals, SPIRV_OP_TYPE_VECTOR | (4 << 16));
    spirv_push(module->globals, id);
    spirv_push(module->globals, comp);
    spirv_push(module->globals, 4); // Component count, not the component width.
    module->t_vec4 = id;
    return id;
}

// OpTypeArray. The length operand must be an OpConstant of integer type,
// not a literal - callers pass spirv_type_const_uint32() of the count.
static forceinline uint32_t spirv_type_array(spirv_module_t* module, uint32_t elem_ty, uint32_t len_const)
{
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->globals, SPIRV_OP_TYPE_ARRAY | (4 << 16));
    spirv_push(module->globals, id);
    spirv_push(module->globals, elem_ty);
    spirv_push(module->globals, len_const);
    return id;
}

static forceinline uint32_t spirv_type_ptr(spirv_module_t* module, uint32_t storage, uint32_t pointee)
{
    for (size_t i = 0; i < module->ptr_cache_n; ++i) {
        if (module->ptr_cache[i].storage == storage && module->ptr_cache[i].pointee == pointee) {
            return module->ptr_cache[i].id;
        }
    }

    uint32_t id = spirv_seq_id(module);
    spirv_push(module->globals, SPIRV_OP_TYPE_POINTER | (4 << 16));
    spirv_push(module->globals, id);
    spirv_push(module->globals, storage);
    spirv_push(module->globals, pointee);
    if (module->ptr_cache_n < 32) {
        module->ptr_cache[module->ptr_cache_n].storage = storage;
        module->ptr_cache[module->ptr_cache_n].pointee = pointee;
        module->ptr_cache[module->ptr_cache_n].id      = id;
        module->ptr_cache_n++;
    }
    return id;
}

static forceinline uint32_t spirv_type_func_void(spirv_module_t* module)
{
    uint32_t id = spirv_seq_id(module);
    uint32_t rt = spirv_type_void(module);
    spirv_push(module->globals, SPIRV_OP_TYPE_FUNCTION | (3 << 16));
    spirv_push(module->globals, id);
    spirv_push(module->globals, rt);
    return id;
}

static forceinline uint32_t spirv_type_const_float32(spirv_module_t* module, float v)
{
    for (size_t i = 0; i < module->fconst_cache_n; i++) {
        if (module->fconst_cache[i].v == v) {
            return module->fconst_cache[i].id;
        }
    }
    uint32_t ty = spirv_type_float32(module);
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->globals, SPIRV_OP_CONSTANT | (4 << 16));
    spirv_push(module->globals, ty);
    spirv_push(module->globals, id);
    spirv_push(module->globals, read_uint32_le(&v));
    if (module->fconst_cache_n < 64) {
        module->fconst_cache[module->fconst_cache_n].v  = v;
        module->fconst_cache[module->fconst_cache_n].id = id;
        module->fconst_cache_n++;
    }
    return id;
}

static forceinline uint32_t spirv_type_const_int32(spirv_module_t* module, int32_t v)
{
    for (size_t i = 0; i < module->iconst_cache_n; i++) {
        if (module->iconst_cache[i].v == v) {
            return module->iconst_cache[i].id;
        }
    }
    uint32_t ty = spirv_type_int32(module);
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->globals, SPIRV_OP_CONSTANT | (4 << 16));
    spirv_push(module->globals, ty);
    spirv_push(module->globals, id);
    spirv_push(module->globals, (uint32_t)v);
    if (module->iconst_cache_n < 32) {
        module->iconst_cache[module->iconst_cache_n].v  = v;
        module->iconst_cache[module->iconst_cache_n].id = id;
        module->iconst_cache_n++;
    }
    return id;
}

static forceinline uint32_t spirv_type_const_uint32(spirv_module_t* module, uint32_t v)
{
    for (size_t i = 0; i < module->uconst_cache_n; i++) {
        if (module->uconst_cache[i].v == v) {
            return module->uconst_cache[i].id;
        }
    }
    uint32_t ty = spirv_type_uint32(module);
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->globals, SPIRV_OP_CONSTANT | (4 << 16));
    spirv_push(module->globals, ty);
    spirv_push(module->globals, id);
    spirv_push(module->globals, v);
    if (module->uconst_cache_n < 32) {
        module->uconst_cache[module->uconst_cache_n].v  = v;
        module->uconst_cache[module->uconst_cache_n].id = id;
        module->uconst_cache_n++;
    }
    return id;
}

static forceinline uint32_t spirv_type_const_composite4(spirv_module_t* module, uint32_t vec4_ty, uint32_t _0,
                                                        uint32_t _1, uint32_t _2, uint32_t _3)
{
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->globals, SPIRV_OP_CONSTANT_COMPOSITE | (7 << 16));
    spirv_push(module->globals, vec4_ty);
    spirv_push(module->globals, id);
    spirv_push(module->globals, _0);
    spirv_push(module->globals, _1);
    spirv_push(module->globals, _2);
    spirv_push(module->globals, _3);
    return id;
}

static forceinline uint32_t spirv_composite_construct4(spirv_module_t* module, uint32_t vec4_ty, uint32_t x, uint32_t y,
                                                       uint32_t z, uint32_t w)
{
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->func_body, SPIRV_OP_COMPOSITE_CONSTRUCT | (7 << 16));
    spirv_push(module->func_body, vec4_ty);
    spirv_push(module->func_body, id);
    spirv_push(module->func_body, x);
    spirv_push(module->func_body, y);
    spirv_push(module->func_body, z);
    spirv_push(module->func_body, w);
    return id;
}

static forceinline uint32_t spirv_composite_extract1(spirv_module_t* module, uint32_t comp_ty, uint32_t composite,
                                                     uint32_t index)
{
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->func_body, SPIRV_OP_COMPOSITE_EXTRACT | (5 << 16));
    spirv_push(module->func_body, comp_ty);
    spirv_push(module->func_body, id);
    spirv_push(module->func_body, composite);
    spirv_push(module->func_body, index);
    return id;
}

static forceinline uint32_t spirv_access_chain1(spirv_module_t* module, uint32_t ptr_ty, uint32_t base,
                                                uint32_t index_const)
{
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->func_body, SPIRV_OP_ACCESS_CHAIN | (5 << 16));
    spirv_push(module->func_body, ptr_ty);
    spirv_push(module->func_body, id);
    spirv_push(module->func_body, base);
    spirv_push(module->func_body, index_const);
    return id;
}

// OpAccessChain with an arbitrary number of indices, each an id of an
// integer-typed value (constant or not). Walking into a struct member
// requires its index to be an OpConstant, per spec.
static forceinline uint32_t spirv_access_chain(spirv_module_t* module, uint32_t ptr_ty, uint32_t base,
                                               const uint32_t* indices, size_t n)
{
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->func_body, SPIRV_OP_ACCESS_CHAIN | ((4 + n) << 16));
    spirv_push(module->func_body, ptr_ty);
    spirv_push(module->func_body, id);
    spirv_push(module->func_body, base);
    for (size_t i = 0; i < n; i++) {
        spirv_push(module->func_body, indices[i]);
    }
    return id;
}

static forceinline uint32_t spirv_ext_inst1(spirv_module_t* module, uint32_t ty, uint32_t instr, uint32_t a)
{
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->func_body, SPIRV_OP_EXT_INST | (6 << 16));
    spirv_push(module->func_body, ty);
    spirv_push(module->func_body, id);
    spirv_push(module->func_body, module->glsl_std_450);
    spirv_push(module->func_body, instr);
    spirv_push(module->func_body, a);
    return id;
}

static forceinline uint32_t spirv_ext_inst2(spirv_module_t* module, uint32_t ty, uint32_t instr, uint32_t a, uint32_t b)
{
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->func_body, SPIRV_OP_EXT_INST | (7 << 16));
    spirv_push(module->func_body, ty);
    spirv_push(module->func_body, id);
    spirv_push(module->func_body, module->glsl_std_450);
    spirv_push(module->func_body, instr);
    spirv_push(module->func_body, a);
    spirv_push(module->func_body, b);
    return id;
}

static forceinline uint32_t spirv_ext_inst3(spirv_module_t* module, uint32_t ty, uint32_t instr, uint32_t a, uint32_t b,
                                            uint32_t c)
{
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->func_body, SPIRV_OP_EXT_INST | (8 << 16));
    spirv_push(module->func_body, ty);
    spirv_push(module->func_body, id);
    spirv_push(module->func_body, module->glsl_std_450);
    spirv_push(module->func_body, instr);
    spirv_push(module->func_body, a);
    spirv_push(module->func_body, b);
    spirv_push(module->func_body, c);
    return id;
}

static forceinline uint32_t spirv_global_var(spirv_module_t* module, uint32_t ptr_ty, uint32_t storage)
{
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->globals, SPIRV_OP_VARIABLE | (4 << 16));
    spirv_push(module->globals, ptr_ty);
    spirv_push(module->globals, id);
    spirv_push(module->globals, storage);
    return id;
}

static forceinline uint32_t spirv_local_var(spirv_module_t* module, uint32_t ptr_ty)
{
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->func_vars, SPIRV_OP_VARIABLE | (4 << 16));
    spirv_push(module->func_vars, ptr_ty);
    spirv_push(module->func_vars, id);
    spirv_push(module->func_vars, SPIRV_STORAGE_CLASS_FUNCTION);
    return id;
}

static forceinline uint32_t spirv_type_const_bool(spirv_module_t* module, bool v)
{
    uint32_t ty = spirv_type_bool(module);
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->globals, (v ? SPIRV_OP_CONSTANT_TRUE : SPIRV_OP_CONSTANT_FALSE) | (3 << 16));
    spirv_push(module->globals, ty);
    spirv_push(module->globals, id);
    return id;
}

// Generic "OpXxx %ty %id operands..." in the function body.
static forceinline uint32_t spirv_emit_op(spirv_module_t* module, uint32_t opcode, uint32_t ty, const uint32_t* ops,
                                          uint32_t n)
{
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->func_body, opcode | ((3 + n) << 16));
    spirv_push(module->func_body, ty);
    spirv_push(module->func_body, id);
    for (uint32_t i = 0; i < n; i++) {
        spirv_push(module->func_body, ops[i]);
    }
    return id;
}

static forceinline uint32_t spirv_local_var_init(spirv_module_t* module, uint32_t ptr_ty, uint32_t init)
{
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->func_vars, SPIRV_OP_VARIABLE | (5 << 16));
    spirv_push(module->func_vars, ptr_ty);
    spirv_push(module->func_vars, id);
    spirv_push(module->func_vars, SPIRV_STORAGE_CLASS_FUNCTION);
    spirv_push(module->func_vars, init);
    return id;
}

static forceinline void spirv_decorate_0(spirv_module_t* module, uint32_t target, uint32_t decoration)
{
    spirv_push(module->annotations, SPIRV_OP_DECORATE | (3 << 16));
    spirv_push(module->annotations, target);
    spirv_push(module->annotations, decoration);
}

static forceinline void spirv_decorate_1(spirv_module_t* module, uint32_t target, uint32_t decoration, uint32_t operand)
{
    spirv_push(module->annotations, SPIRV_OP_DECORATE | (4 << 16));
    spirv_push(module->annotations, target);
    spirv_push(module->annotations, decoration);
    spirv_push(module->annotations, operand);
}

static forceinline void spirv_member_decorate_1(spirv_module_t* module, uint32_t struct_ty, uint32_t member,
                                                uint32_t decoration, uint32_t operand)
{
    spirv_push(module->annotations, SPIRV_OP_MEMBER_DECORATE | (5 << 16));
    spirv_push(module->annotations, struct_ty);
    spirv_push(module->annotations, member);
    spirv_push(module->annotations, decoration);
    spirv_push(module->annotations, operand);
}

static forceinline void spirv_name(spirv_module_t* module, uint32_t id, const char* name)
{
    size_t nwords = (strlen(name) + 4) / 4;
    spirv_push(module->debug, SPIRV_OP_NAME | ((2 + nwords) << 16));
    spirv_push(module->debug, id);
    spirv_push_string(&module->debug, name);
}

// Declares a std140-compatible uniform block holding a single vec4 array:
//
//   layout(set = set, binding = binding) uniform Block { vec4 c[count]; };
//
// A vec4 array is the one layout whose SPIR-V rules (ArrayStride 16) and
// raw byte image agree, so element i component j is simply dword
// (i * 4 + j) of the buffer - no padding to account for on the host side.
// Returns the variable id; out_elem_ptr_ty receives the pointer type used
// to address a single component.
static forceinline uint32_t spirv_uniform_vec4_array_block(spirv_module_t* module, uint32_t count, uint32_t set,
                                                           uint32_t binding, uint32_t* out_elem_ptr_ty)
{
    uint32_t f32   = spirv_type_float32(module);
    uint32_t v4    = spirv_type_vec4_float32(module);
    uint32_t len   = spirv_type_const_uint32(module, count);
    uint32_t arr   = spirv_type_array(module, v4, len);
    uint32_t block = spirv_seq_id(module);

    spirv_push(module->globals, SPIRV_OP_TYPE_STRUCT | (3 << 16));
    spirv_push(module->globals, block);
    spirv_push(module->globals, arr);

    spirv_decorate_1(module, arr, SPIRV_DECORATION_ARRAY_STRIDE, 16);
    spirv_decorate_0(module, block, SPIRV_DECORATION_BLOCK);
    spirv_member_decorate_1(module, block, 0, SPIRV_DECORATION_OFFSET, 0);

    uint32_t block_ptr = spirv_type_ptr(module, SPIRV_STORAGE_CLASS_UNIFORM, block);
    uint32_t var       = spirv_global_var(module, block_ptr, SPIRV_STORAGE_CLASS_UNIFORM);

    spirv_decorate_1(module, var, SPIRV_DECORATION_DESCRIPTOR_SET, set);
    spirv_decorate_1(module, var, SPIRV_DECORATION_BINDING, binding);

    if (out_elem_ptr_ty) {
        *out_elem_ptr_ty = spirv_type_ptr(module, SPIRV_STORAGE_CLASS_UNIFORM, f32);
    }
    return var;
}

static forceinline uint32_t spirv_op_1(spirv_module_t* module, uint32_t op, uint32_t type, uint32_t _0)
{
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->func_body, op | (4 << 16));
    spirv_push(module->func_body, type);
    spirv_push(module->func_body, id);
    spirv_push(module->func_body, _0);
    return id;
}

static forceinline uint32_t spirv_op_2(spirv_module_t* module, uint32_t op, uint32_t type, uint32_t _0, uint32_t _1)
{
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->func_body, op | (5 << 16));
    spirv_push(module->func_body, type);
    spirv_push(module->func_body, id);
    spirv_push(module->func_body, _0);
    spirv_push(module->func_body, _1);
    return id;
}

static forceinline uint32_t spirv_op_3(spirv_module_t* module, uint32_t op, uint32_t type, uint32_t _0, uint32_t _1,
                                       uint32_t _2)
{
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->func_body, op | (6 << 16));
    spirv_push(module->func_body, type);
    spirv_push(module->func_body, id);
    spirv_push(module->func_body, _0);
    spirv_push(module->func_body, _1);
    spirv_push(module->func_body, _2);
    return id;
}

static forceinline uint32_t spirv_op_load(spirv_module_t* module, uint32_t type, uint32_t ptr)
{
    return spirv_op_1(module, SPIRV_OP_LOAD, type, ptr);
}

static forceinline void spirv_op_store(spirv_module_t* module, uint32_t ptr, uint32_t value)
{
    spirv_push(module->func_body, SPIRV_OP_STORE | (3 << 16));
    spirv_push(module->func_body, ptr);
    spirv_push(module->func_body, value);
}

#define spirv_op_bitcast(module, ty, val)        spirv_op_1(module, SPIRV_OP_BITCAST, ty, val)
#define spirv_op_fadd(module, ty, a, b)          spirv_op_2(module, SPIRV_OP_F_ADD, ty, a, b)
#define spirv_op_fsub(module, ty, a, b)          spirv_op_2(module, SPIRV_OP_F_SUB, ty, a, b)
#define spirv_op_fmul(module, ty, a, b)          spirv_op_2(module, SPIRV_OP_F_MUL, ty, a, b)
#define spirv_op_fdiv(module, ty, a, b)          spirv_op_2(module, SPIRV_OP_F_DIV, ty, a, b)
#define spirv_op_fneg(module, ty, a)             spirv_op_1(module, SPIRV_OP_F_NEGATE, ty, a)
#define spirv_op_iadd(module, ty, a, b)          spirv_op_2(module, SPIRV_OP_I_ADD, ty, a, b)
#define spirv_op_imul(module, ty, a, b)          spirv_op_2(module, SPIRV_OP_I_MUL, ty, a, b)
#define spirv_op_bit_and(module, ty, a, b)       spirv_op_2(module, SPIRV_OP_BIT_AND, ty, a, b)
#define spirv_op_bit_or(module, ty, a, b)        spirv_op_2(module, SPIRV_OP_BIT_OR, ty, a, b)
#define spirv_op_bit_xor(module, ty, a, b)       spirv_op_2(module, SPIRV_OP_BIT_XOR, ty, a, b)
#define spirv_op_shl(module, ty, a, b)           spirv_op_2(module, SPIRV_OP_SHLL, ty, a, b)
#define spirv_op_shr(module, ty, a, b)           spirv_op_2(module, SPIRV_OP_SHRL, ty, a, b)
#define spirv_cmp(module, opcode, bool_ty, a, b) spirv_op_2(module, opcode, bool_ty, a, b)
#define spirv_select(module, ty, cond, a, b)     spirv_op_3(module, SPIRV_OP_SELECT, ty, cond, a, b)

static forceinline void spirv_entry_point(spirv_module_t* module, uint32_t exec_model, uint32_t func_id,
                                          const char* name, const uint32_t* iface, size_t iface_n)
{
    size_t name_words = (strlen(name) + 1 + 3) / 4;
    spirv_push(module->entry_points, SPIRV_OP_ENTRY_POINT | ((3 + name_words + iface_n) << 16));
    spirv_push(module->entry_points, exec_model);
    spirv_push(module->entry_points, func_id);
    spirv_push_string(&module->entry_points, name);
    for (size_t i = 0; i < iface_n; i++) {
        spirv_push(module->entry_points, iface[i]);
    }
}

static forceinline void spirv_exec_mode0(spirv_module_t* module, uint32_t func_id, uint32_t mode)
{
    spirv_push(module->exec_modes, SPIRV_OP_EXECUTION_MODE | (3 << 16));
    spirv_push(module->exec_modes, func_id);
    spirv_push(module->exec_modes, mode);
}

static forceinline uint32_t spirv_func_begin(spirv_module_t* module, uint32_t void_ty, uint32_t fn_ty)
{
    uint32_t id = spirv_seq_id(module);
    spirv_push(module->func_body, SPIRV_OP_FUNCTION | (5 << 16));
    spirv_push(module->func_body, void_ty);
    spirv_push(module->func_body, id);
    spirv_push(module->func_body, 0); // FunctionControlMaskNone
    spirv_push(module->func_body, fn_ty);
    uint32_t label = spirv_seq_id(module);
    spirv_push(module->func_body, SPIRV_OP_LABEL | (2 << 16));
    spirv_push(module->func_body, label);
    return id;
}

static forceinline void spirv_func_end(spirv_module_t* module)
{
    spirv_push(module->func_body, SPIRV_OP_RETURN | (1 << 16));
    spirv_push(module->func_body, SPIRV_OP_FUNCTION_END | (1 << 16));
}

// High-level auxiliary functions.

// layout(location = %0) in/out %1 %2
static forceinline uint32_t spirv_declare_location(spirv_module_t* module, uint32_t storage, uint32_t location,
                                                   uint32_t ty, const char* name)
{
    uint32_t ptr = spirv_type_ptr(module, storage, ty);
    uint32_t var = spirv_global_var(module, ptr, storage);
    spirv_decorate_1(module, var, SPIRV_DECORATION_LOCATION, location);
    spirv_name(module, var, name);
    return var;
}

// layout(location = %0) in %1 %2
static forceinline uint32_t spirv_declare_location_in(spirv_module_t* module, uint32_t location, uint32_t ty,
                                                      const char* name)
{
    return spirv_declare_location(module, SPIRV_STORAGE_CLASS_INPUT, location, ty, name);
}

// layout(location = %0) out %1 %2
static forceinline uint32_t spirv_declare_location_out(spirv_module_t* module, uint32_t location, uint32_t ty,
                                                       const char* name)
{
    return spirv_declare_location(module, SPIRV_STORAGE_CLASS_OUTPUT, location, ty, name);
}

static forceinline uint32_t spirv_declare_builtin(spirv_module_t* module, uint32_t ty, uint32_t kind, uint32_t storage)
{
    uint32_t ptr = spirv_type_ptr(module, SPIRV_STORAGE_CLASS_OUTPUT, ty);
    uint32_t var = spirv_global_var(module, ptr, storage);
    spirv_decorate_1(module, var, SPIRV_DECORATION_BUILTIN, kind);
    return var;
}

static forceinline uint32_t spirv_declare_builtin_in(spirv_module_t* module, uint32_t ty, uint32_t kind)
{
    return spirv_declare_builtin(module, ty, kind, SPIRV_STORAGE_CLASS_INPUT);
}

static forceinline uint32_t spirv_declare_builtin_out(spirv_module_t* module, uint32_t ty, uint32_t kind)
{
    return spirv_declare_builtin(module, ty, kind, SPIRV_STORAGE_CLASS_OUTPUT);
}

static forceinline void spirv_merge(spirv_words_t* dst, const spirv_words_t* src)
{
    vector_foreach (*src, i) {
        uint32_t cmd = vector_at(*src, i);
        vector_push_back(*dst, cmd);
    }
}

// Serialize all compiled sections into final SPIR-V shader.
static forceinline int spirv_module_finish(spirv_module_t* module, uint32_t** out_dwords, uint32_t* out_n)
{
    spirv_words_t fixed_body = {0};
    if (vector_size(module->func_body) >= 7) {
        // OpFunction (5 words) + OpLabel (2 words) = 7 words header.
        for (size_t i = 0; i < 7 && module->func_body.data; i++) {
            spirv_push(fixed_body, module->func_body.data[i]);
        }
        spirv_merge(&fixed_body, &module->func_vars);
        for (size_t i = 7; i < module->func_body.count; i++) {
            spirv_push(fixed_body, module->func_body.data[i]);
        }
    } else {
        spirv_merge(&fixed_body, &module->func_vars);
    }

    spirv_words_t output = {0};
    spirv_push(output, SPIRV_MAGIC);
    spirv_push(output, SPIRV_VERSION_1_3);
    spirv_push(output, SPIRV_GENERATOR_MAGIC);
    spirv_push(output, module->next_id);
    spirv_push(output, 0);

    spirv_merge(&output, &module->caps);
    spirv_merge(&output, &module->ext_imports);
    spirv_merge(&output, &module->mem_model);
    spirv_merge(&output, &module->entry_points);
    spirv_merge(&output, &module->exec_modes);
    spirv_merge(&output, &module->debug);
    spirv_merge(&output, &module->annotations);
    spirv_merge(&output, &module->globals);
    spirv_merge(&output, &fixed_body);

    vector_free(fixed_body);

    if (!output.data && output.count > 0) {
        vector_free(output);
        return -1;
    }

    *out_dwords = output.data;
    *out_n      = output.count;

    return 0;
}

static forceinline void spirv_module_free(spirv_module_t* module)
{
    vector_free(module->caps);
    vector_free(module->ext_imports);
    vector_free(module->mem_model);
    vector_free(module->entry_points);
    vector_free(module->exec_modes);
    vector_free(module->debug);
    vector_free(module->annotations);
    vector_free(module->globals);
    vector_free(module->func_vars);
    vector_free(module->func_body);
}

#endif /* RVVM_GPU_VULKAN_SPIRV_H */
