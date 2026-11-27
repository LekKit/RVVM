/*
gpu-vulkan.h - Vulkan GPU backend
Copyright (C) 2026  David Korenchuk <github.com/epoll-reactor-2>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef RVVM_GPU_VULKAN_H
#define RVVM_GPU_VULKAN_H

#include "compiler.h"
#include "rvvm/rvvm_base.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Important note. If you will suddenly encounter silent segfaults in
// functions like vkCreateGraphicsPipelines(), vkQueueSubmit() or others,
// it is a strong signal that your native Vulkan backend is buggy (like
// mine AMD RADV). Try launch your application with environment variable
// VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json or other.

typedef struct gpu_vulkan_ctx_t gpu_vulkan_ctx_t;

gpu_vulkan_ctx_t* gpu_vulkan_create(void);

void gpu_vulkan_destroy(gpu_vulkan_ctx_t* ctx);

// -----------------------------------------------------------
// Guest-driven draws
// -----------------------------------------------------------

// Programmable stages a draw may carry, in the order the graphics
// pipeline runs them.
typedef enum {
    GPU_VULKAN_STAGE_VERTEX = 0,
    GPU_VULKAN_STAGE_TESS_CTRL,
    GPU_VULKAN_STAGE_TESS_EVAL,
    GPU_VULKAN_STAGE_GEOMETRY,
    GPU_VULKAN_STAGE_FRAGMENT,
    GPU_VULKAN_STAGE_COUNT
} gpu_vulkan_stage_t;

static forceinline const char* gpu_vulkan_stage_to_string(gpu_vulkan_stage_t s)
{
    switch (s) {
        case GPU_VULKAN_STAGE_VERTEX:
            return "GPU_VULKAN_STAGE_VERTEX";
        case GPU_VULKAN_STAGE_TESS_CTRL:
            return "GPU_VULKAN_STAGE_TESS_CTRL";
        case GPU_VULKAN_STAGE_TESS_EVAL:
            return "GPU_VULKAN_STAGE_TESS_EVAL";
        case GPU_VULKAN_STAGE_GEOMETRY:
            return "GPU_VULKAN_STAGE_GEOMETRY";
        case GPU_VULKAN_STAGE_FRAGMENT:
            return "GPU_VULKAN_STAGE_FRAGMENT";
        default:
            return "<unknown GPU vertex kind>";
    }
}

// Values match VkPrimitiveTopology.
typedef enum {
    GPU_VULKAN_TOPOLOGY_POINT_LIST     = 0,
    GPU_VULKAN_TOPOLOGY_LINE_LIST      = 1,
    GPU_VULKAN_TOPOLOGY_LINE_STRIP     = 2,
    GPU_VULKAN_TOPOLOGY_TRIANGLE_LIST  = 3,
    GPU_VULKAN_TOPOLOGY_TRIANGLE_STRIP = 4,
    GPU_VULKAN_TOPOLOGY_TRIANGLE_FAN   = 5,
} gpu_vulkan_topology_t;

// Shader constants.
//
// Every programmable stage owns one uniform block of GPU_VULKAN_CONST_BYTES
// bytes, bound at descriptor set GPU_VULKAN_CONST_SET, binding
// GPU_VULKAN_CONST_BINDING(stage). The backend treats it as an opaque byte
// image: whatever the caller hands over is copied verbatim, so the shader
// alone decides how those bytes are interpreted. It must declare the block
// with an explicit layout (std140 / ArrayStride + Offset decorations) and a
// size of at most GPU_VULKAN_CONST_BYTES; the emulated device does that in
// gpu-xe2-shader.h, and spirv_uniform_vec4_array_block() in
// gpu-vulkan-spirv.h declares "vec4 c[N]" in which element i, component j is
// dword (i * 4 + j) of the block.
//
// There are two ways to feed the block:
//   1. gpu_vulkan_stage_desc_t.constants - the whole block, as part of a
//      draw. Everything past const_bytes (and the whole block when NULL) is
//      zero.
//   2. gpu_vulkan_update_constants() - patch a byte range of the current
//      block without resubmitting the draw. This is the per-frame path:
//      no SPIR-V copy, no pipeline lookup, just a memcpy.
#define GPU_VULKAN_CONST_SET            0
#define GPU_VULKAN_CONST_BYTES          1024
#define GPU_VULKAN_CONST_BINDING(stage) ((uint32_t)(stage))

typedef struct {
    // SPIR-V module for this stage. NULL leaves the stage disabled.
    // Copied by gpu_vulkan_submit_draw(), so the caller stays free to
    // recompile or free it right after.
    const uint32_t* spirv;
    uint32_t        spirv_nwords;

    // Contents of the stage's uniform block, see "Shader constants" above.
    // Anything past const_bytes (and everything when constants is NULL)
    // reads back as zero; const_bytes beyond GPU_VULKAN_CONST_BYTES is
    // truncated.
    const void* constants;
    uint32_t    const_bytes;
} gpu_vulkan_stage_desc_t;

// -----------------------------------------------------------
// Vertex input
// -----------------------------------------------------------
//
// Optional: a draw with vertex.binding_count == 0 has no bound vertex
// buffers, matching the previous behaviour (a VS that generates its own
// geometry from gl_VertexIndex). When present, everything here is
// copied by gpu_vulkan_submit_draw(), same as .spirv above.
#define GPU_VULKAN_MAX_VERTEX_BINDINGS 16
#define GPU_VULKAN_MAX_VERTEX_ATTRIBS  16

typedef struct {
    const void* data; // Raw bytes for this binding.
    uint32_t    size;
    uint32_t    stride;
} gpu_vulkan_vertex_binding_t;

// A few VkFormat values callers commonly need for vertex attributes,
// named so gpu_vulkan_vertex_attrib_t.format below can be filled in
// without pulling vulkan.h into every caller. Values match VkFormat
// exactly (Khronos guarantees these never change) - any other VkFormat
// works too, this is just the backend's own name for the common ones.
#define GPU_VULKAN_FORMAT_R32_SFLOAT          100
#define GPU_VULKAN_FORMAT_R32G32_SFLOAT       103
#define GPU_VULKAN_FORMAT_R32G32B32_SFLOAT    106
#define GPU_VULKAN_FORMAT_R32G32B32A32_SFLOAT 109
#define GPU_VULKAN_FORMAT_R8G8B8A8_UNORM      37
#define GPU_VULKAN_FORMAT_R8G8B8A8_UINT       41
#define GPU_VULKAN_FORMAT_R8G8B8A8_SINT       42

typedef struct {
    uint32_t location; // Shader input location (matches the SPIR-V Location decoration).
    uint32_t binding;  // Index into vertex.binding[].
    uint32_t format;   // VkFormat value, e.g. one of GPU_VULKAN_FORMAT_* above.
    uint32_t offset;   // Byte offset within one binding's stride.
} gpu_vulkan_vertex_attrib_t;

typedef struct {
    gpu_vulkan_vertex_binding_t binding[GPU_VULKAN_MAX_VERTEX_BINDINGS];
    uint32_t                    binding_count;
    gpu_vulkan_vertex_attrib_t  attrib[GPU_VULKAN_MAX_VERTEX_ATTRIBS];
    uint32_t                    attrib_count;

    // Indexed draw, e.g. vkCmdDrawIndexed(). index_data == NULL means a
    // plain vkCmdDraw(); when set, .vertex_count/.first_vertex above are
    // read as index count/first index instead of vertex count/first vertex.
    const void* index_data;
    uint32_t    index_size;
    uint32_t    index_type;    // VkIndexType value.
    int32_t     vertex_offset; // Added to each fetched index.
} gpu_vulkan_vertex_input_t;

typedef struct {
    gpu_vulkan_stage_desc_t   stage[GPU_VULKAN_STAGE_COUNT];
    gpu_vulkan_vertex_input_t vertex;

    uint32_t topology; // gpu_vulkan_topology_t
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t first_vertex;
    uint32_t first_instance;
} gpu_vulkan_draw_t;

// Hand a draw to the renderer.
//
// The draw is copied and kept as the scene the backend renders from that
// point on; the next render tick picks it up. A later submission
// replaces it, so a guest issuing several draws per frame currently gets
// the last one - there is no command list yet.
//
// Returns false when the draw carries no vertex shader - a graphics
// pipeline cannot be built without one - in which case the backend keeps
// rendering whatever it had.
//
// draw->vertex is optional; when set it is copied like the SPIR-V above,
// bound as vertex/index buffers, and folded into the pipeline's vertex
// input state (a layout change rebuilds the pipeline, same as new SPIR-V).
bool gpu_vulkan_submit_draw(gpu_vulkan_ctx_t* ctx, const gpu_vulkan_draw_t* draw);

// Overwrite sized memory region treated as uniform buffer. Shader's uniform buffer's
// size must comply with buffer size reported to Vulkan with `vkBindBufferMemory()`.
bool gpu_vulkan_update_constants(gpu_vulkan_ctx_t* ctx, gpu_vulkan_stage_t stage, uint32_t offset, const void* data,
                                 uint32_t size);

bool gpu_vulkan_render_frame(gpu_vulkan_ctx_t* ctx, uint32_t width, uint32_t height, uint8_t* dst, size_t dst_size,
                             uint32_t stride, rvvm_rgb_t format, uint32_t* out_width, uint32_t* out_height,
                             uint32_t* out_stride, rvvm_rgb_t* out_format);

#endif /* RVVM_GPU_VULKAN_H */
